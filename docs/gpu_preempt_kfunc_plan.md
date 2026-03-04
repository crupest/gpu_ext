# GPU Preempt kfunc 设计文档：从内核 BPF 上下文直接抢占 GPU TSG

## 1. 背景与动机

### 1.1 当前 GPU 抢占架构

目前系统中有两种 GPU TSG 抢占路径：

**路径 A：Userspace ioctl 路径（`gpu_preempt_ctrl` 工具）**

```
BPF tracepoint (内核态)
    → 捕获 hClient/hTsg → 存入 BPF ring buffer
    → 用户态 poll ring buffer
    → 用户态发起 ioctl(NV_ESC_RM_CONTROL)
    → escape.c 安全检查绕过 (privLevel=KERNEL)
    → Nv04ControlWithSecInfo()
    → RM dispatch → kchangrpapiCtrlCmdPreempt_IMPL()
    → NV_RM_RPC_CONTROL → GSP firmware
    → 等待 RPC response
    → 返回用户态
```

实测延迟：**~356us**（含 userspace context switch + ioctl 往返 + GSP RPC）

**路径 B：bpf_wq 路径（用于 cross-block prefetch 等场景）**

```
struct_ops hook (non-sleepable, 持有 spinlock)
    → bpf_wq_init() + bpf_wq_set_callback() + bpf_wq_start()
    → kworker 线程被调度执行
    → callback 中调用 sleepable kfunc（如 bpf_gpu_migrate_range）
```

bpf_wq 当前仅用于 UVM 内存迁移，未用于 GPU 抢占。

**路径 C：struct_ops + kfunc 路径（on_task_init timeslice 设置）**

```
nv-kernel.o 内部 TSG 创建
    → 调用 nv_gpu_sched_task_init() [EXPORT_SYMBOL]
    → RCU dispatch → BPF struct_ops on_task_init()
    → BPF 调用 bpf_nv_gpu_set_timeslice(ctx, us) [kfunc]
    → 修改 ctx->timeslice
    → nv-kernel.o 读取修改后的 ctx，应用 timeslice
```

此路径延迟极低（纯内核态、无 RPC），但只能在 TSG **创建时** 设置参数，不能运行时调整。

### 1.2 问题分析

| 路径 | 优点 | 缺点 |
|------|------|------|
| A: ioctl | 功能完整、已验证 | 延迟高(356us+)、需要 userspace 参与、无法自动化 |
| B: bpf_wq | 纯内核态 | 需要 kworker 调度延迟、当前未支持 preempt |
| C: kfunc | 延迟最低 | 只能在 init 时设置、无运行时 preempt 能力 |

**核心问题**：BPF 程序无法从内核上下文直接触发 GPU TSG 抢占。所有抢占操作都必须经过 userspace ioctl 路径。

### 1.3 目标

添加 sleepable kfunc `bpf_nv_gpu_preempt_tsg(hClient, hTsg)`，使 BPF 程序可以从内核上下文（sleepable BPF context，如 bpf_wq callback）直接触发 GPU TSG 抢占，实现**跨进程 preempt**。

**为什么需要 kfunc？** 同进程内已可通过 fd 复用 + ioctl 实现 preempt（见 §8 验证），但 BPF 调度策略需要从内核态 preempt **任意进程**的 TSG，无法复用目标进程的 fd → 必须通过 kfunc 绕过 ioctl 安全检查。

**Handle 捕获**：已验证 kprobe 3-probe 策略可在 stock nvidia module 上捕获任意进程的 hClient/hTsg（见 §8.3），**无需修改内核**。kfunc 只需实现执行侧。

**新架构（目标）**：

```
BPF kprobe 捕获目标进程 hClient/hTsg → 存入 BPF map
    ↓
事件触发（page fault / timer / struct_ops hook）
    → BPF 程序检测到需要抢占
    → bpf_wq callback（如在 non-sleepable 上下文）
        或直接调用（如在 sleepable struct_ops 上下文）
    → bpf_nv_gpu_preempt_tsg(hClient, hTsg) [sleepable kfunc]
    → nv_gpu_sched_do_preempt() [nvidia.ko 内部]
    → RM dispatch → GSP RPC → 抢占完成
```

预期延迟：消除 userspace context switch（~50-100us），总延迟从 ~356us 降至 ~200-250us。

## 2. 现有代码分析

### 2.1 GPU 调度 struct_ops 框架

**文件**: `kernel-module/nvidia-module/kernel-open/nvidia/nv-gpu-sched-hooks.c`

当前已实现的 struct_ops 和 kfuncs：

```c
// struct_ops 定义
struct nv_gpu_sched_ops {
    int (*on_task_init)(struct nv_gpu_task_init_ctx *ctx);     // TSG 创建
    int (*on_bind)(struct nv_gpu_bind_ctx *ctx);               // TSG 绑定到 runlist
    int (*on_task_destroy)(struct nv_gpu_task_destroy_ctx *ctx); // TSG 销毁
};

// 已注册的 kfuncs
bpf_nv_gpu_set_timeslice(ctx, timeslice_us)   // KF_TRUSTED_ARGS, 仅 init 时
bpf_nv_gpu_set_interleave(ctx, level)          // KF_TRUSTED_ARGS, 仅 init 时
bpf_nv_gpu_reject_bind(ctx)                    // KF_TRUSTED_ARGS, 仅 bind 时
```

这些 kfuncs 都是 **non-sleepable** 的（只修改 context struct 字段），且只在特定 hook 时机可用。

### 2.2 UVM BPF struct_ops 框架（参考）

**文件**: `kernel-module/nvidia-module/kernel-open/nvidia-uvm/uvm_bpf_struct_ops.c`

UVM 侧已有 sleepable kfunc 的先例：

```c
// 已注册的 sleepable kfunc
bpf_gpu_migrate_range(va_space, addr, length)  // KF_SLEEPABLE
```

此 kfunc 通过 `bpf_wq` 从 non-sleepable 的 eviction/prefetch hooks 间接调用，是当前唯一的 sleepable GPU kfunc。

### 2.3 RM 抢占实现路径

**文件**: `kernel-module/nvidia-module/src/nvidia/src/kernel/gpu/fifo/kernel_channel_group_api.c`

```c
// 抢占 API（通过 ioctl dispatch 调用）
NV_STATUS kchangrpapiCtrlCmdPreempt_IMPL(
    KernelChannelGroupApi *pKernelChannelGroupApi,
    NVA06C_CTRL_PREEMPT_PARAMS *pPreemptParams
);
```

此函数在 RM 对象系统内部运行，需要有效的 `KernelChannelGroupApi` 对象引用。在 GSP 系统（RTX 5090）上，通过 `NV_RM_RPC_CONTROL` 发送 RPC 到 GSP firmware，**会 sleep**。

### 2.4 ioctl 到 RM 的调用链

```
用户态 ioctl(fd, NV_ESC_RM_CONTROL, &params)
  → nvidia_ioctl() [kernel-open/nvidia/nv.c:2432]
  → rm_ioctl(sp, nv, nvfp, cmd, arg, size) [nv-kernel.o]
  → RM resource dispatch
  → kchangrpapiCtrlCmdPreempt_IMPL()
  → IS_GSP_CLIENT(pGpu) → NV_RM_RPC_CONTROL()
  → GSP firmware 执行抢占
  → 返回 NV_STATUS
```

关键：`rm_ioctl()` 是 `kernel-open` 调用 `nv-kernel.o` 的入口，声明在 `nv.h:959`：
```c
NV_STATUS NV_API_CALL rm_ioctl(nvidia_stack_t *, nv_state_t *, nv_file_private_t *, NvU32, void *, NvU32);
```

### 2.5 现有 GPU 抢占 BPF 工具

**文件**: `extension/gpu_preempt_ctrl.bpf.c`

使用 tracepoints 监控 TSG 生命周期：
- `tracepoint/nvidia/nvidia_gpu_tsg_create` → 捕获 hClient, hTsg
- `tracepoint/nvidia/nvidia_gpu_tsg_schedule` → 监控调度事件
- `tracepoint/nvidia/nvidia_gpu_tsg_destroy` → 清理跟踪

BPF maps 存储 TSG handle 映射（`tsg_map` by hTsg, `pid_tsg_map` by PID），供用户态读取后发送 ioctl preempt。

### 2.6 GPreempt 论文参考

**文件**: `docs/gpu-ext/driver_docs/sched/gpreempt-analysis/`

GPreempt (ATC'25) 的关键技术：
- TSG timeslice 设为 1us → 硬件快速 yield → <40us 抢占延迟
- 通过注释 `rpcRecvPoll()` 实现异步 RPC（fire-and-forget）
- 使用 GDRCopy (~1us) 实现 CPU→GPU hint-based pre-preemption

我们的方案与 GPreempt 的区别：
- GPreempt 修改驱动使 RPC 异步化（高风险，不等待 response）
- 我们保持 RPC 同步，仅消除 userspace 往返（低风险）
- 后续可选探索异步 RPC 作为 Phase 2

## 3. 技术约束

### 3.1 Sleepable vs Non-sleepable

**为什么 preempt kfunc 必须是 sleepable？**

RTX 5090 是 GSP 系统。GPU preempt 调用链：
```
bpf_nv_gpu_preempt_tsg()
  → nv_gpu_sched_do_preempt()
  → RM dispatch (可能获取 mutex/semaphore)
  → NV_RM_RPC_CONTROL() (等待 GSP response, completion_wait)
```

GSP RPC 使用 completion 机制等待 firmware 响应，**必须在可 sleep 的上下文中调用**。

因此：
- kfunc 标记为 `KF_SLEEPABLE`
- 从 non-sleepable hooks（eviction/prefetch，持有 spinlock）调用时，**必须** 通过 `bpf_wq`
- 从 sleepable hooks（如新增的 `on_sched_tick`）或 `bpf_wq` callback 中可直接调用

### 3.2 kernel-open 与 nv-kernel.o 的边界

```
kernel-open/nvidia/              ← 开源，可自由修改
    nv-gpu-sched-hooks.c         ← kfunc 定义在这里
    nv.c                         ← nvidia_ioctl 在这里

src/nvidia/                      ← RM 源码（开源但编译为 nv-kernel.o）
    kernel_channel_group_api.c   ← preempt 实现在这里

链接关系：
    nvidia.ko = kernel-open/*.o + nv-kernel.o
```

`kernel-open` 可以调用 `nv-kernel.o` 导出的函数（如 `rm_ioctl`）。反之，`nv-kernel.o` 调用 `kernel-open` 导出的函数（如 `nv_gpu_sched_task_init`）。

**新增 kfunc 的调用方向**：`kernel-open` (kfunc) → `nv-kernel.o` (RM preempt 实现)

有两种方式穿越这个边界：

1. **通过 rm_ioctl**：构造完整的 ioctl 参数，调用 `rm_ioctl()`。需要 `nvidia_stack_t`、`nv_state_t`、`nv_file_private_t`，获取这些上下文比较复杂。但 §8.1 的实践表明可以通过 `osapi.c` 中 `NV_ENTER_RM_RUNTIME` + `Nv04ControlWithSecInfo` 路径实现。
2. **新增 RM 导出函数**：在 RM 源码中添加 `nv_gpu_sched_do_preempt()` 并 `EXPORT_SYMBOL`。更干净但需要深入 RM 内部 API。

### 3.3 RM 上下文获取

kfunc 只需要 `hClient` 和 `hTsg` 两个参数（均为 NvU32），由 BPF kprobe 捕获（见 §8.3 的 3-probe 策略，零内核修改）。RM 内部通过 `(hClient, hTsg)` 查找 `KernelChannelGroupApi` 对象并执行 preempt。

## 4. 实现方案

### Phase 1: 添加 sleepable kfunc `bpf_nv_gpu_preempt_tsg`

**最小内核修改集**：仅 3 个文件。Handle 捕获由 BPF kprobe 完成（零内核修改，见 §8.3）。

#### 方案选择：osapi.c 新函数 vs escape.c bypass

两种方案本质相同，最终都调用 `Nv04ControlWithSecInfo` + `RS_PRIV_LEVEL_KERNEL` 实现跨进程 preempt。区别在于权限提升的位置和作用域：

| | osapi.c 方案（选定） | escape.c 方案（弃用） |
|---|---|---|
| 核心操作 | `Nv04ControlWithSecInfo(RS_PRIV_LEVEL_KERNEL)` | 相同 |
| 权限提升位置 | 新增专用内核函数内部 | 修改现有 ioctl 路径 |
| 谁能触发跨进程 preempt | **仅 kfunc**（BPF 程序） | kfunc + **任意 root 进程通过 ioctl** |
| 文件改动 | 3 处（osapi.c + exports + hooks.c） | 2 处（escape.c + hooks.c） |
| 安全性 | 权限提升不暴露给 userspace | ioctl 表面扩大，任何 root 进程可跨进程操控 GPU TSG |

**选择 osapi.c 方案**：虽然多 1 处文件改动，但权限提升被封装在内核函数内部，不对 userspace ioctl 路径引入安全漏洞。escape.c 方案会使任何 root 进程都能通过 ioctl 跨进程 preempt 其他进程的 GPU TSG，属于不必要的攻击面扩大。

#### 4.1 Step 1: RM 层新增内部 preempt 函数

**文件**: `kernel-module/nvidia-module/src/nvidia/src/kernel/gpu/osapi.c`

使用 `Nv04ControlWithSecInfo` 路径（已在 §8.1 实践验证），以内核特权级绕过 fd/client 安全检查：

```c
NV_STATUS
nv_gpu_sched_do_preempt(NvU32 hClient, NvU32 hTsg)
{
    NV_STATUS rmStatus;
    THREAD_STATE_NODE threadState;
    nvidia_stack_t *sp = NULL;

    if (nv_kmem_cache_alloc_stack(&sp) != 0)
        return NV_ERR_NO_MEMORY;

    NV_ENTER_RM_RUNTIME(sp, fp);
    threadStateInit(&threadState, THREAD_STATE_FLAGS_NONE);

    NVOS54_PARAMETERS params = { 0 };
    NVA06C_CTRL_PREEMPT_PARAMS preemptParams = { 0 };
    preemptParams.bWait = NV_TRUE;
    preemptParams.bManualTimeout = NV_TRUE;
    preemptParams.timeoutUs = 100000;

    params.hClient = hClient;
    params.hObject = hTsg;
    params.cmd = NVA06C_CTRL_CMD_PREEMPT;  // 0xa06c0105
    params.params = NvP64_VALUE(&preemptParams);
    params.paramsSize = sizeof(preemptParams);

    RS_PRIV_LEVEL privLevel = RS_PRIV_LEVEL_KERNEL;
    RMAPI_PARAM_COPY paramCopy = { 0 };
    rmStatus = Nv04ControlWithSecInfo(&params, &paramCopy, privLevel, NULL);

    threadStateFree(&threadState, THREAD_STATE_FLAGS_NONE);
    NV_EXIT_RM_RUNTIME(sp, fp);
    nv_kmem_cache_free_stack(sp);

    return rmStatus;
}
EXPORT_SYMBOL(nv_gpu_sched_do_preempt);
```

**关键点**：
- 使用 `RS_PRIV_LEVEL_KERNEL` 绕过 fd/client ownership 检查，实现跨进程 preempt
- `NV_ENTER_RM_RUNTIME` + `threadStateInit` 是从 `kernel-open` 调用 RM 的标准模式
- 需要在 `exports_link_command.txt` 和 `nv.h` 中添加导出声明

#### 4.2 Step 2: kfunc 注册

**文件**: `kernel-module/nvidia-module/kernel-open/nvidia/nv-gpu-sched-hooks.c`

在现有 kfunc 定义区域添加：

```c
/* Forward declaration - implemented in nv-kernel.o (osapi.c) */
extern NvU32 nv_gpu_sched_do_preempt(NvU32 hClient, NvU32 hTsg);

__bpf_kfunc int bpf_nv_gpu_preempt_tsg(u32 hClient, u32 hTsg)
{
    if (!hClient || !hTsg)
        return -EINVAL;
    return (int)nv_gpu_sched_do_preempt(hClient, hTsg);
}
```

在 BTF kfunc ID set 中注册（**添加到现有 set**）：

```c
BTF_KFUNCS_START(nv_gpu_sched_kfunc_ids_set)
BTF_ID_FLAGS(func, bpf_nv_gpu_set_timeslice, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_nv_gpu_set_interleave, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_nv_gpu_reject_bind, KF_TRUSTED_ARGS)
/* 新增: sleepable kfunc，可从 bpf_wq callback 调用 */
BTF_ID_FLAGS(func, bpf_nv_gpu_preempt_tsg, KF_SLEEPABLE)
BTF_KFUNCS_END(nv_gpu_sched_kfunc_ids_set)
```

#### 4.3 Step 3: BPF 程序示例

**新建文件**: `extension/gpu_sched_preempt.bpf.c`

```c
// SPDX-License-Identifier: GPL-2.0
/*
 * gpu_sched_preempt.bpf.c - GPU TSG preemption via BPF kfunc
 *
 * Demonstrates using bpf_nv_gpu_preempt_tsg() kfunc to preempt
 * GPU TSGs directly from kernel BPF context, without userspace ioctl.
 *
 * Architecture:
 *   1. Tracepoints capture TSG handles (hClient, hTsg) at creation
 *   2. Userspace or BPF timer triggers preemption decision
 *   3. bpf_wq callback calls bpf_nv_gpu_preempt_tsg() [sleepable kfunc]
 *
 * Usage:
 *   sudo ./gpu_sched_preempt [-p PID] [-i interval_ms]
 *     -p PID: only preempt TSGs owned by this process
 *     -i ms:  periodic preemption interval (0 = manual trigger only)
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "GPL";

/* kfunc declarations */
extern int bpf_nv_gpu_preempt_tsg(u32 hClient, u32 hTsg) __ksym;

/* TSG tracking */
struct tsg_entry {
    u32 hClient;
    u32 hTsg;
    u32 pid;
    u64 create_time;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 256);
    __type(key, u32);               /* hTsg */
    __type(value, struct tsg_entry);
} tsg_map SEC(".maps");

/* Preempt request queue (populated by userspace or timer) */
struct preempt_req {
    u32 hClient;
    u32 hTsg;
    struct bpf_wq work;
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 64);
    __type(key, u32);
    __type(value, struct preempt_req);
} preempt_wq_map SEC(".maps");

/* Stats */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 4);
    __type(key, u32);
    __type(value, u64);
} stats SEC(".maps");

enum { STAT_PREEMPT_OK = 0, STAT_PREEMPT_FAIL = 1, STAT_TSG_TRACK = 2 };

/* bpf_wq callback: execute preemption in sleepable context */
static int do_preempt(void *map, int *key, void *value)
{
    struct preempt_req *req = value;
    int ret;
    u32 stat_key;
    u64 *cnt;

    if (!req->hClient || !req->hTsg)
        return 0;

    ret = bpf_nv_gpu_preempt_tsg(req->hClient, req->hTsg);

    stat_key = (ret == 0) ? STAT_PREEMPT_OK : STAT_PREEMPT_FAIL;
    cnt = bpf_map_lookup_elem(&stats, &stat_key);
    if (cnt)
        __sync_fetch_and_add(cnt, 1);

    return 0;
}

/* Tracepoint: capture TSG creation */
struct trace_tsg_create {
    u64 __pad;
    u32 hClient;
    u32 hTsg;
    u64 tsg_id;
    u32 engine_type;
    u64 timeslice_us;
    u32 interleave_level;
    u32 runlist_id;
    u32 gpu_instance;
    u32 pid;
};

SEC("tracepoint/nvidia/nvidia_gpu_tsg_create")
int handle_tsg_create(struct trace_tsg_create *ctx)
{
    struct tsg_entry entry = {};
    u32 hTsg = ctx->hTsg;
    u32 stat_key = STAT_TSG_TRACK;
    u64 *cnt;

    entry.hClient = ctx->hClient;
    entry.hTsg = ctx->hTsg;
    entry.pid = bpf_get_current_pid_tgid() >> 32;
    entry.create_time = bpf_ktime_get_ns();

    bpf_map_update_elem(&tsg_map, &hTsg, &entry, BPF_ANY);

    cnt = bpf_map_lookup_elem(&stats, &stat_key);
    if (cnt)
        __sync_fetch_and_add(cnt, 1);

    return 0;
}

/* Tracepoint: cleanup on TSG destroy */
struct trace_tsg_destroy {
    u64 __pad;
    u32 hClient;
    u32 hTsg;
    u64 tsg_id;
    u32 pid;
};

SEC("tracepoint/nvidia/nvidia_gpu_tsg_destroy")
int handle_tsg_destroy(struct trace_tsg_destroy *ctx)
{
    u32 hTsg = ctx->hTsg;
    bpf_map_delete_elem(&tsg_map, &hTsg);
    return 0;
}
```

#### 4.4 Step 4: Loader 程序

**新建文件**: `extension/gpu_sched_preempt.c`

Loader 负责：
1. 加载 BPF 程序和 attach tracepoints
2. 提供命令行接口触发 preempt（写入 preempt_wq_map → 触发 bpf_wq）
3. 显示统计信息

### Phase 2（可选）: 添加 `on_sched_tick` sleepable hook

在 `nv_gpu_sched_ops` 中增加周期性 sleepable callback，由内核 timer 触发。BPF 程序在此 hook 中可直接调用 `bpf_nv_gpu_preempt_tsg()` 而不需要 bpf_wq。

**优势**：真正实现"无 workqueue"的 preempt 路径
**挑战**：需要在 nvidia.ko 内部设置 hrtimer/timer_list 周期性触发 hook

### Phase 3（探索性）: Fire-and-Forget Async RPC

参考 GPreempt 的做法，修改 `src/nvidia/src/kernel/vgpu/rpc.c` 使 preempt RPC 不等待 GSP response，使 kfunc 可标记为 non-sleepable。

**风险**：不知道 preempt 是否成功、可能引发 RM 状态不一致。

## 5. 关键文件清单

**内核修改（仅 3 处）**：

| 文件 | 操作 | 内容 |
|------|------|------|
| `src/nvidia/src/kernel/gpu/osapi.c` | 修改 | 添加 `nv_gpu_sched_do_preempt()` + EXPORT_SYMBOL |
| `kernel-open/nvidia/nv-gpu-sched-hooks.c` | 修改 | 添加 `bpf_nv_gpu_preempt_tsg` kfunc 定义和注册 |
| `src/nvidia/exports_link_command.txt` + `nv.h` | 修改 | 导出 `nv_gpu_sched_do_preempt` 符号 |

**不需要修改的文件**（handle 捕获由 BPF kprobe 完成）：
- ~~`nv-gpu-sched-hooks.h`~~ — 不需要新增 context struct 字段
- ~~`kernel_channel_group.c` / `kernel_channel_group_api.c`~~ — 不需要修改 hook 调用点
- ~~`escape.c`~~ — kfunc 绕过 ioctl，不需要安全检查 bypass

**BPF 程序（extension 目录）**：

| 文件 | 操作 | 内容 |
|------|------|------|
| `extension/gpu_sched_preempt.bpf.c` | 新建 | BPF 程序：kprobe handle 捕获 + bpf_wq + preempt kfunc |
| `extension/gpu_sched_preempt.c` | 新建 | Loader 程序 |
| `extension/Makefile` | 修改 | 添加编译目标 |

## 6. 风险与注意事项

### 6.1 RM API 调用安全性

`nv_gpu_sched_do_preempt()` 使用 `RS_PRIV_LEVEL_KERNEL` 绕过 client ownership 检查，实现跨进程 preempt。这与 ioctl 路径中 escape.c 的安全检查绕过逻辑相同，但在内核态完成，不暴露给用户空间。

### 6.2 GPU Lock 竞争

RM control 调用可能需要获取 GPU lock（`rmGpuLockAcquire`）。如果 preempt 在高频调用场景下使用，可能与正常的 RM 操作产生 lock 竞争。需要测试不同调用频率下的影响。

### 6.3 hClient/hTsg 有效性

BPF tracepoint 捕获的 handles 可能在调用 preempt 时已失效（TSG 已销毁）。`pRmApi->Control` 应该会返回 `NV_ERR_INVALID_OBJECT`，kfunc 需要正确传播此错误。

### 6.4 BPF verifier 兼容性

`KF_SLEEPABLE` kfuncs 在 `BPF_PROG_TYPE_STRUCT_OPS` 中使用时，需要：
- struct_ops 的 SEC 标注为 `struct_ops.s`（sleepable）
- 或从 `bpf_wq` callback 中调用（天然 sleepable）

需要验证 BPF verifier 是否允许在 `nv_gpu_sched_ops` 的 non-sleepable hooks 中通过 bpf_wq 间接调用 sleepable kfunc。

## 7. 验证方案

### 7.1 编译验证
```bash
cd kernel-module/nvidia-module
make -j$(nproc) modules
# 确认无编译错误
```

### 7.2 加载验证
```bash
sudo rmmod nvidia_uvm nvidia_modeset nvidia
sudo insmod kernel-open/nvidia.ko
sudo insmod kernel-open/nvidia-modeset.ko
sudo insmod kernel-open/nvidia-uvm.ko
dmesg | grep "GPU sched struct_ops"
# 应看到 "nvidia: GPU sched struct_ops initialized"
```

### 7.3 kfunc 可用性验证
```bash
cd extension
make gpu_sched_preempt
sudo ./gpu_sched_preempt
# BPF 程序应能通过 verifier 加载
```

### 7.4 功能测试
```bash
# 终端 1: 启动 preempt 工具
sudo ./gpu_sched_preempt -v

# 终端 2: 启动 long-running CUDA 程序
./test_cuda_kernel  # 运行一个持续几秒的 CUDA kernel

# 终端 1: 触发 preempt (通过命令行或自动)
# 观察 preempt 结果和延迟

# 对比：使用原有 gpu_preempt_ctrl 工具的 ioctl 路径延迟
```

### 7.5 延迟对比测试
```bash
# 测量 kfunc 路径延迟（bpf_wq callback → kfunc → RM → 返回）
# 测量 ioctl 路径延迟（userspace → ioctl → RM → 返回 userspace）
# 预期 kfunc 路径快 50-100us（消除 userspace context switch）
```

## 8. 实施进展记录

### 8.1 已完成工作 (2026-03-03)

#### 发现1: `gpu_preempt_ctrl` 无法工作
- `gpu_preempt_ctrl.bpf.c` 依赖的 tracepoints（`nvidia_gpu_tsg_create/schedule/destroy`）**不存在于驱动源码**
- `nv-tracepoint.h` 中仅定义了 `nvidia_dev_xid` 一个 tracepoint
- 因此 `gpu_preempt_ctrl` **从未能正常加载过**

#### 发现2: `NV_ESC_RM_CONTROL` 值错误
- `gpu_preempt_ctrl_event.h` 中定义 `NV_ESC_RM_CONTROL = 0x2A`（正确）
- 但如果其他代码中误用 `NV_ESC_RM_CONTROL = 2`（之前的 test_preempt_ioctl 就犯了这个错误），ioctl 会返回 EINVAL
- 正确值来自 `src/nvidia/arch/nvalloc/unix/include/nv_escape.h:30`

#### 发现3: ioctl preempt 直接测试成功

##### 测试步骤

**Step 1: 使用 bpftrace 从 kprobe 捕获 RM handles**

bpftrace 无法解析 nvidia 模块自定义 struct（BTF 类型带后缀如 `u32___2`），因此使用 raw memory offset 读取：

```bash
sudo bpftrace -e 'kprobe:nv_gpu_sched_task_init {
    printf("hClient=0x%x hTsg=0x%x tsg_id=%llu engine=%u\n",
        *(uint32*)(arg0+32), *(uint32*)(arg0+36),
        *(uint64*)arg0, *(uint32*)(arg0+8));
}'
```

`nv_gpu_task_init_ctx` struct 内存布局（加入 hClient/hTsg 后）：
```
offset 0:  u64 tsg_id
offset 8:  u32 engine_type
offset 16: u64 default_timeslice
offset 24: u32 default_interleave
offset 28: u32 runlist_id
offset 32: u32 hClient        ← 新增
offset 36: u32 hTsg            ← 新增
offset 40: u64 timeslice
offset 48: u32 interleave_level
```

**Step 2: 启动 CUDA 工作负载，触发 TSG 创建**

```bash
python3 -c "
import torch, time, os
print(f'PID: {os.getpid()}')
x = torch.randn(4096, 4096, device='cuda')
print('CUDA initialized, running matmuls...')
while True:
    y = torch.mm(x, x)
    torch.cuda.synchronize()
    time.sleep(0.1)
" &
```

bpftrace 输出（每个 CUDA 进程创建多个 TSG）：
```
hClient=0xc1e00050 hTsg=0xd          engine=13    # COPY engine
hClient=0xc1e00051 hTsg=0xf          engine=13    # COPY engine
hClient=0xc1e00052 hTsg=0xcaf00002   engine=1     # GRAPHICS/COMPUTE
hClient=0xc1d00053 hTsg=0xcaf00001   engine=1     # GRAPHICS/COMPUTE
hClient=0xc1d00002 hTsg=0xcaf0017b   engine=13    # COPY engine (UVM)
hClient=0xc1d00002 hTsg=0xcaf00193   engine=14    # COPY engine (UVM)
hClient=0xc1d0004b hTsg=0x5c000013   engine=1     # GRAPHICS/COMPUTE (应用)
hClient=0xc1d0004b hTsg=0x5c000038   engine=13    # COPY engine (应用)
hClient=0xc1d0004b hTsg=0x5c000046   engine=14    # COPY engine (应用)
```

**Engine types**: 1=GR (Graphics/Compute), 13=CE (Copy Engine), 14=CE (Copy Engine 2)

**Step 3: 运行 `test_preempt_ioctl` 测试各种控制命令**

```bash
# 编译测试工具
cd extension && make test_preempt_ioctl

# PREEMPT: 强制抢占 GPU TSG
sudo ./test_preempt_ioctl preempt 0xc1d0004b 0x5c000013

# SET_TIMESLICE: 动态修改 TSG 时间片
sudo ./test_preempt_ioctl timeslice 0xc1d0004b 0x5c000013 1000  # 1000us
sudo ./test_preempt_ioctl timeslice 0xc1d0004b 0x5c000013 1     # 1us (GPreempt风格)

# PREEMPT on COPY engine TSG
sudo ./test_preempt_ioctl preempt 0xc1d0004b 0x5c000038

# SET_INTERLEAVE
sudo ./test_preempt_ioctl interleave 0xc1d0004b 0x5c000013 1
```

##### 实测结果 (2026-03-03)

```
=== Test 1: PREEMPT on engine=1 TSG ===
PREEMPT hClient=0xc1d0004b hTsg=0x5c000013 status=0 duration=669 us

=== Test 2: PREEMPT again (same TSG) ===
PREEMPT hClient=0xc1d0004b hTsg=0x5c000013 status=0 duration=316 us

=== Test 3: SET_TIMESLICE to 1000us ===
SET_TIMESLICE hClient=0xc1d0004b hTsg=0x5c000013 timeslice=1000 us status=0 duration=313 us

=== Test 4: SET_TIMESLICE to 1us (GPreempt-style) ===
SET_TIMESLICE hClient=0xc1d0004b hTsg=0x5c000013 timeslice=1 us status=0 duration=267 us

=== Test 5: PREEMPT on engine=13 (COPY engine) TSG ===
PREEMPT hClient=0xc1d0004b hTsg=0x5c000038 status=0 duration=316 us

=== Test 6: SET_INTERLEAVE ===
SET_INTERLEAVE hClient=0xc1d0004b hTsg=0x5c000013 level=1 status=31 duration=5 us
```

CUDA 进程在所有 preempt 操作后继续正常运行（`nvidia-smi` 确认进程仍存在）。

##### 结果汇总

| 操作 | 结果 | 延迟 | 说明 |
|------|------|------|------|
| PREEMPT (首次) | **成功** status=0 | 669us | 首次调用包含冷路径开销 |
| PREEMPT (重复) | **成功** status=0 | 316us | 热路径稳定 ~300us |
| SET_TIMESLICE 1000us | **成功** status=0 | 313us | 可设为任意值 |
| SET_TIMESLICE 1us | **成功** status=0 | 267us | GPreempt 风格最小时间片 |
| PREEMPT (COPY engine) | **成功** status=0 | 316us | CE TSG 也可以 preempt |
| SET_INTERLEAVE | **失败** status=31 | 5us | NV_ERR_INVALID_ARGUMENT |

**关键发现**：
- Preempt 延迟稳定在 ~300us（不含首次冷路径 ~670us），与之前测试一致
- 延迟主要来自 GSP RPC 往返（RTX 5090 是 GSP 架构）
- SET_INTERLEAVE 失败是因为 interleave level 值不在有效范围内（不影响核心功能）
- CUDA 进程 preempt 后自动恢复执行，无 crash 或异常

##### ioctl 调用路径分析

```
Userspace test_preempt_ioctl
  │
  ├── open("/dev/nvidiactl")
  │
  ├── ioctl(fd, _IOWR('F', 211, nv_ioctl_xfer_t))
  │     cmd = NV_ESC_IOCTL_XFER_CMD (NV_IOCTL_BASE+11 = 211)
  │     payload = { cmd=0x2A (NV_ESC_RM_CONTROL), size, ptr→NVOS54_PARAMETERS }
  │
  │   NVOS54_PARAMETERS:
  │     hClient = 0xc1d0004b    ← 从 bpftrace 捕获
  │     hObject = 0x5c000013    ← TSG handle
  │     cmd     = 0xa06c0105    ← NVA06C_CTRL_CMD_PREEMPT
  │     params  → { bWait=1, bManualTimeout=1, timeoutUs=100000 }
  │
  └── 内核路径:
        nvidia_ioctl()                          [kernel-open/nvidia/nv.c]
          → rm_ioctl(sp, nv, nvfp, cmd, arg)    [nv-kernel.o / osapi.c]
            → case NV_ESC_RM_CONTROL:           [escape.c]
              → 安全绕过: (cmd & 0xffff0000) == 0xa06c0000
                → privLevel = RS_PRIV_LEVEL_KERNEL
                → clientOSInfo = NULL
              → Nv04ControlWithSecInfo(pApi, secInfo)
                → RM resource server dispatch
                  → rpcCtrlPreempt_HAL()        [rpc.c:4472]
                    → GSP RPC (等待 firmware response, ~300us)
                      → GPU 硬件执行 TSG preempt
```

**安全绕过机制** (`escape.c:762`):
正常 RM control 调用会检查调用者是否拥有目标 RM 对象（通过 fd 匹配 clientOSInfo）。
自定义修改将 `0xa06c****` 范围命令（TSG 调度控制）提升为内核特权级别，允许跨进程 preempt。

**GSP RPC 路径** (`rpc.c:4472`):
RTX 5090 的 RM 不直接操作 GPU 硬件，而是通过 RPC 发送到 GSP firmware。
`rpcCtrlPreempt_HAL()` 构造 RPC 消息 → 等待 completion → GSP firmware 执行实际抢占。
这是 ~300us 延迟的主要来源，kfunc 路径也无法消除此 RPC 延迟。

#### 已实现的代码修改（kernel module 侧，初版 — 含多余修改）

**注意**: 初版修改包含了不必要的变更。经过 §8.3-§8.4 的验证，最终只需要以下 **3 处修改**：

**需要保留的修改**（kfunc 核心）：

1. **`src/.../osapi.c`**
   - 新增 `nv_gpu_sched_do_preempt()`
   - 走 `NV_ENTER_RM_RUNTIME` + `threadStateInit` + `Nv04ControlWithSecInfo` + `RS_PRIV_LEVEL_KERNEL` 路径

2. **`kernel-open/nvidia/nv-gpu-sched-hooks.c`**
   - 新增 `bpf_nv_gpu_preempt_tsg()` (KF_SLEEPABLE kfunc)

3. **`src/nvidia/exports_link_command.txt`** + **`nv.h`**
   - 导出 `nv_gpu_sched_do_preempt` 符号

**应撤回的多余修改**（handle 捕获由 kprobe 完成，不需要内核改动）：

- ~~`nv-gpu-sched-hooks.h`: `nv_gpu_task_init_ctx` 新增 hClient/hTsg 字段~~ → kprobe 3-probe 策略已解决
- ~~`kernel_channel_group.c`: 移除 hook 调用~~ → 不需要
- ~~`kernel_channel_group_api.c`: 添加 hook 调用~~ → 不需要
- ~~`escape.c`: 安全检查绕过~~ → kfunc 用 `RS_PRIV_LEVEL_KERNEL`，不走 ioctl
- ~~`osapi.c`: `nv_gpu_sched_do_set_timeslice()`~~ → 不需要 set_timeslice kfunc
- ~~`nv-gpu-sched-hooks.c`: `bpf_nv_gpu_set_timeslice_runtime()`~~ → 不需要

#### 验证状态（初版，含多余修改）
- [x] 模块编译成功
- [x] 模块加载成功，dmesg 确认 struct_ops + kfunc 初始化
- [x] kallsyms 确认 `bpf_nv_gpu_preempt_tsg` 已注册
- [x] 现有 struct_ops 程序 (`gpu_sched_set_timeslices`) 在新模块上正常工作
- [ ] **撤回多余修改后重新编译验证**
- [ ] **kfunc 端到端测试**（BPF 程序 → bpf_wq → kfunc → RM → GSP → preempt）
- [ ] **回归测试**：现有 UVM BPF 程序（prefetch/eviction）在新模块上正常工作

### 8.2 `test_preempt_demo` — 自包含 BPF+CUDA+ioctl 测试工具 (2026-03-03)

**设计**：单进程单二进制，集成 BPF kprobe（捕获 TSG handles）+ CUDA driver API（运行 GPU kernel）+ ioctl preempt（抢占测试），一条命令展示完整 preempt 效果。

**文件**：
- `extension/test_preempt_demo.bpf.c` — kprobe on `nv_gpu_sched_task_init`，raw memory offset 读取 hClient/hTsg
- `extension/test_preempt_demo.c` — BPF loader + CUDA driver API + ioctl + 三项测试
- `extension/Makefile` — 新增 `CUDA_APPS` 链接规则（`-lcuda -lpthread`）

**实测结果** (2026-03-03)：

| 测试 | 结果 |
|------|------|
| **A: Preempt 延迟** | avg=337us, min=319, max=400 (10/10 成功) |
| **B: Timeslice=1us** | 单 TSG 无影响 (-0.0%)，需要多 TSG 竞争 |
| **C: Burst 100x** | 5749 preempts/sec, 100/100 成功, GPU alive |
| **D: 持续 preempt 影响** | **kernel 时间 +184%** (260ms → 739ms) |

**Test D 关键数据** — 证明 preempt 真正中断 GPU 执行：
```
Phase 1 (无 preempt):  10 kernels, avg=260151 min=259008 max=270364 us
Phase 2 (持续 preempt): 10 kernels, avg=739293 min=711454 max=745119 us
Preempts issued: 43211 in 7.4s (5859 preempts/sec)

*** kernel time +184.2% (260151 → 739293 us) ***
→ PREEMPT IS INTERRUPTING GPU EXECUTION
```

**Test D 分析**：
- 每次 preempt 触发 GPU 硬件保存 TSG 上下文 → 重新调度 → 恢复执行
- 单 TSG 场景下，preempt 后 GPU scheduler 立即恢复同一 TSG，但 save/restore 有固定开销
- 43211 次 preempt 使 10 个 kernel 额外花费 4.79s → 每次 preempt 约 111us GPU 侧开销
- 这 111us 是 GPU 硬件 context save/restore 的真实代价（host 侧看到的 ~350us 还包含 GSP RPC 往返）

**技术细节**：
- **系统 TSG vs 应用 TSG**：CUDA init 创建 ~6 个系统 TSG（`0xcaf*` handles），应用 TSG（`0x5c*` handles）最后创建。必须选最后一个 GR TSG。
- **BPF kprobe**：用 raw memory offset 读取（非 struct_ops），避免 vmlinux.h BTF 问题
- **PTX anti-DCE**：busy_loop 必须写 global memory，否则 GPU JIT 做 dead-code elimination
- **依赖**：~~需要自定义 nvidia 模块（hClient/hTsg 字段 + escape.c 安全绕过）~~ 见 §8.3 无需内核修改的方案

### 8.3 无需内核修改的 Handle 捕获方案 (2026-03-03)

**问题**：原始方案在 `nv_gpu_task_init_ctx` 中添加 hClient/hTsg 字段，需要修改内核模块。

**发现**：`nv-kernel.o` 中所有函数（`rm_ioctl`、`Nv04AllocWithSecInfo`、`kchangrpapiConstruct_IMPL` 等）都标记为 "notrace"，**无法 kprobe**。只有 `kernel-open/` 目录中编译的函数（`nv_gpu_sched_task_init`、`nvidia_unlocked_ioctl` 等）可以 kprobe。

**解决方案**：3-probe 策略，从 ioctl 路径拦截 TSG 分配：

```
Probe 1: kprobe/nvidia_unlocked_ioctl (入口)
  ├── 读取 user-space nv_ioctl_xfer_t → 过滤 inner cmd == 0x2B (NV_ESC_RM_ALLOC)
  ├── 读取 user-space NVOS21_PARAMETERS → 过滤 hClass == 0xa06c (TSG class)
  └── 保存 hRoot (=hClient) + NVOS21 user pointer 到 per-TID pending map

Probe 2: kprobe/nv_gpu_sched_task_init (中间)
  ├── 在 rm_ioctl 执行期间被调用（kchangrpInit_IMPL → nv_gpu_sched_task_init）
  └── 补充 engine_type + tsg_id 到 pending map entry

Probe 3: kretprobe/nvidia_unlocked_ioctl (出口)
  ├── nvidia_ioctl 已执行 copy_to_user 将结果写回 user space
  ├── 从 user-space NVOS21 offset 8 读取 hObjectNew (= hTsg)
  └── 输出完整 {hClient, hTsg, engine_type, tsg_id, pid} entry
```

**NVOS21_PARAMETERS 内存布局**（同 NVOS64 前 4 字段）：
```
offset 0:  u32 hRoot         (= hClient, 输入)
offset 4:  u32 hObjectParent
offset 8:  u32 hObjectNew    (= hTsg, 输出，由 RM alloc 填写)
offset 12: u32 hClass        (= 0xa06c for TSG)
```

**nv_ioctl_xfer_t 内存布局**（user space）：
```
offset 0:  u32 cmd   (inner command, e.g., 0x2B)
offset 4:  u32 size
offset 8:  u64 ptr   (user-space pointer to NVOS21)
```

**实测结果** (2026-03-03，stock nvidia module)：

```
Captured 3 TSG(s):
  [0] hClient=0xc1d0033e hTsg=0x5c000013 engine=GR(1) tsg_id=1
  [1] hClient=0xc1d0033e hTsg=0x5c000038 engine=CE(13) tsg_id=3
  [2] hClient=0xc1d0033e hTsg=0x5c000046 engine=CE2(14) tsg_id=1

Test A: avg=358us, 10/10 成功
Test C: 5612 preempts/sec, 100/100 成功
Test D: kernel time +180.2% (260120 → 728914 us)
  → PREEMPT IS INTERRUPTING GPU EXECUTION
```

**与旧方案对比**：

| 维度 | 旧方案 (custom module) | 新方案 (stock module) |
|------|----------------------|---------------------|
| Handle 捕获 | kprobe on `nv_gpu_sched_task_init` + custom fields (offset 32/36) | kprobe/kretprobe on `nvidia_unlocked_ioctl` + user-space read |
| 内核修改 | 需要在 `nv_gpu_task_init_ctx` 添加 hClient/hTsg | **不需要** |
| 捕获时机 | TSG 初始化时（struct_ops hook 调用点） | ioctl 返回时（NVOS21 copy_to_user 后） |
| Engine type | 直接从 custom ctx 字段读取 | 从 `nv_gpu_sched_task_init` kprobe 间接获取 |
| 捕获 TSG 数量 | 9 个（含系统 TSG） | 3 个（仅应用 TSG，系统 TSG 不走 class=0xa06c） |
| escape.c bypass | 需要（cross-fd preempt ioctl） | ~~仍需要~~ **不需要**（见 §8.4） |
| 性能 | 相同 | 相同（preempt 延迟相同） |

### 8.4 无需 escape.c bypass 的 Preempt 执行 (2026-03-03)

**问题**：§8.3 解决了 handle 捕获的内核修改依赖，但 preempt ioctl 仍然因为 cross-fd 访问被 RM 安全检查拦截（status=35）。

**根本原因**：`test_preempt_demo` 自己 `open("/dev/nvidiactl")` 得到新 fd，其 `nvfp`（file private）与 CUDA 创建 hClient 时的 fd 不匹配。RM `Nv04ControlWithSecInfo` 校验 `client->clientOSInfo == secInfo.clientOSInfo` 失败。

**解决方案**：既然 CUDA 和 test_preempt_demo 在**同一进程**，CUDA 打开的 nvidia fd 就在进程的 fd 表里。扫描 `/proc/self/fd`，逐个尝试 preempt，成功的就是 CUDA 的控制 fd（nvfp 匹配 hClient）。

```c
// 扫描 /proc/self/fd → 找到 /dev/nvidia* fds → 尝试 preempt → 成功即匹配
for (int fd = 3; fd < 1024; fd++) {
    readlink("/proc/self/fd/%d", link);
    if (strncmp(link, "/dev/nvidia", 11) != 0) continue;
    status = rm_control(fd, hClient, hTsg, NVA06C_CTRL_CMD_PREEMPT, ...);
    if (status == 0) return fd;  // Found CUDA's fd!
}
```

**实测结果** (2026-03-03，stock nvidia module，零内核修改)：

```
Scanning /proc/self/fd for CUDA's nvidia fd...
  fd=22 (/dev/nvidiactl): preempt status=0
Found CUDA's fd=22 — nvfp matches hClient

Test A: avg=358us, 10/10 成功
Test C: 5779 preempts/sec, 100/100 成功
Test D: kernel time +164.6% (260091 → 688310 us)
  → PREEMPT IS INTERRUPTING GPU EXECUTION
```

**最终架构**（零内核修改）：

```
test_preempt_demo (单进程, 单二进制, sudo)
  │
  ├── BPF Probe 1: kprobe/nvidia_unlocked_ioctl
  │     读取 user-space nv_ioctl_xfer_t + NVOS21
  │     过滤 cmd=0x2B, hClass=0xa06c → 保存 hClient
  │
  ├── BPF Probe 2: kprobe/nv_gpu_sched_task_init
  │     补充 engine_type, tsg_id
  │
  ├── BPF Probe 3: kretprobe/nvidia_unlocked_ioctl
  │     读取 hObjectNew (= hTsg) → 输出完整 entry
  │
  ├── CUDA Driver API: cuInit + cuCtxCreate + cuLaunchKernel
  │     → CUDA 内部 open("/dev/nvidiactl") → fd=22
  │
  └── Preempt ioctl: 复用 CUDA 的 fd=22
        → nvfp 匹配 hClient → RM 安全检查通过 → preempt 成功
```

**关键依赖总结**：

| 依赖 | 是否需要内核修改 |
|------|----------------|
| Handle 捕获 (hClient/hTsg) | 否（nvidia_unlocked_ioctl kprobe） |
| Engine type 捕获 | 否（nv_gpu_sched_task_init kprobe） |
| Preempt ioctl 执行 | 否（复用 CUDA 的 fd） |
| Timeslice 设置 | 否（同上） |
| **总计** | **零内核修改** |

**限制**：此方案要求 preempt 调用者和 CUDA 在**同一进程**内（共享 fd 表）。**跨进程 preempt 是 kfunc 的核心价值** — BPF 调度策略需要 preempt 任意进程的 TSG，必须通过 kfunc 路径（见 §4）。

### 8.5 代码重构：`gpu_preempt.h` 公共头文件 (2026-03-03)

将 preempt 机制代码提取为可复用头文件 `extension/gpu_preempt.h`，原始 `test_preempt_demo` 保留 Tests A-D，新增 `test_preempt_multi` 包含 Tests E-F。

**`gpu_preempt.h` 公共接口**（`gp_` 前缀）：

| 函数/宏 | 说明 |
|---------|------|
| `gp_rm_control(fd, hClient, hObject, cmd, params, size)` | RM control ioctl 封装 |
| `gp_preempt(fd, hClient, hTsg)` | TSG preempt (bWait=1, timeout=100ms) |
| `gp_set_timeslice(fd, hClient, hTsg, us)` | 动态设置 TSG timeslice |
| `gp_find_cuda_fd(hClient, hTsg, verbose)` | 扫描 /proc/self/fd 找 CUDA 的 nvidia fd |
| `gp_get_time_us()` | CLOCK_MONOTONIC 微秒时间戳 |
| `gp_engine_str(engine_type)` | Engine type → 字符串 (GR/CE/CE2) |
| `GP_CHECK_CUDA(call)` | CUDA 错误检查宏 |
| `gp_ptx_source` | busy_loop PTX kernel 源码 |
| `struct gp_worker` + `gp_worker_init/start/join` | GPU worker 线程基础设施（含 sample 采集） |
| `gp_cuda_warmup(ctx)` | CUDA context warmup（触发 TSG 创建） |

**文件清单**：
- `extension/gpu_preempt.h` — 公共头文件（所有 preempt 机制代码）
- `extension/test_preempt_demo.bpf.c` — BPF 3-probe 策略（不变）
- `extension/test_preempt_demo.c` — 重构使用 gpu_preempt.h，保留 Tests A-D
- `extension/test_preempt_multi.bpf.c` — 同 test_preempt_demo.bpf.c（独立 skeleton）
- `extension/test_preempt_multi.c` — 多 context 测试 Tests E-F

### 8.6 多 Context 竞争测试 `test_preempt_multi` (2026-03-03)

**目的**：证明 TSG preempt 在多 context 竞争场景下能有效提升优先任务的 GPU 时间分配。

**架构**：两个 CUDA context（A 和 B）在同一 GPU 上竞争，BPF 捕获双方 TSG handles，preempt B 的 TSG 观察 A 的性能变化。

**实测结果** (RTX 5090, stock nvidia module, 零内核修改)：

```
=== GPU Preempt Multi-Context Test ===

[Phase 2] Creating CUDA context A...
  Context A GR TSG: hClient=0xc1d0002b hTsg=0x5c000013

[Phase 3] Creating CUDA context B...
  Context B GR TSG: hClient=0xc1d0002b hTsg=0x5c000088
```

#### Test E: 两个等量 context 竞争 — preempt B → A 获得更多 GPU 时间

两个 context 运行相同工作负载（100M iterations, ~300ms/kernel）：

```
Phase 1 (无 preempt):
  A: avg=539455 min=539411 max=539501 stddev=33 us
  B: avg=539383 us
  → 两者均分 GPU 时间（~50%），kernel 因等待调度从 260ms 升至 539ms

Phase 2 (持续 preempt B):
  A: avg=278392 min=276919 max=280077 stddev=1013 us
  Preempts issued: 23411

  *** RESULT E: A kernel time -48.4% (539455 → 278392 us) ***
  → PREEMPTING B GIVES A MORE GPU TIME
```

**分析**：
- 无 preempt 时，GPU round-robin 两个 TSG，每个 context 获得 ~50% GPU 时间
  - kernel 本身 ~260ms，但需等待另一个 TSG 的 timeslice（~260ms），总计 ~540ms
- 持续 preempt B 后，B 的 kernel 被频繁中断，A 获得接近 100% GPU 时间
  - A 的 kernel 时间从 539ms 降回 278ms（接近无竞争的 260ms baseline + preempt 扰动开销）
- **48.4% 延迟降低 = 从 50% GPU 份额提升到接近 100%**

#### Test F: 短 kernel A + 长 kernel B — preempt 降低 A 的调度等待

Context A 运行短 kernel（1M iterations, ~3ms），Context B 运行长 kernel（100M iterations, ~300ms）：

```
Phase 1 (无 preempt):
  A: avg=7091 min=7089 max=7093 stddev=1 us
  → A 的 kernel 仅 ~3ms，但 observed time 是 7ms（含 ~4ms 调度等待）

Phase 2 (持续 preempt B):
  A: avg=3005 min=2811 max=4992 stddev=456 us
  Preempts issued: 469

  *** RESULT F: A kernel time -57.6% (7091 → 3005 us) ***
  → PREEMPT REDUCES A's SCHEDULING WAIT
```

**分析**：
- 无 preempt 时，A 的短 kernel 需等待 B 的 timeslice 到期才能被调度
  - observed time (7ms) = kernel time (~3ms) + scheduling wait (~4ms)
  - scheduling wait < 16ms timeslice 是因为 A 可能在 B timeslice 中间到达
- 持续 preempt B 后，B 被立即中断，A 几乎不用等待调度
  - observed time 降至 ~3ms ≈ 纯 kernel 执行时间
  - **调度等待从 ~4ms 降至接近 0**
- Preempt 仅发出 469 次（vs Test E 的 23411 次），因为 A 的 kernel 很短，每次 preempt B 后 A 很快完成

#### 结果汇总

| 测试 | 场景 | 无 preempt | 有 preempt | 改善 |
|------|------|-----------|-----------|------|
| **E** | 两等量 context | A=539ms | A=278ms | **-48.4%** |
| **F** | 短 A + 长 B | A=7.1ms | A=3.0ms | **-57.6%** |
| D (参考) | 单 context 连续 preempt | 260ms | 744ms | +185.8% (preempt 开销) |

**结论**：
1. **TSG preempt = 有效的 kernel 级 preempt**：preempt TSG 在 kernel 运行时调用 = 打断该 kernel 执行
2. **多 context 场景下 preempt 有明确收益**：可实现优先级调度（优先任务获得更多 GPU 时间）和延迟保护（latency-sensitive 任务不被 throughput 任务阻塞）
3. **零内核修改**：所有功能在 stock nvidia module 上实现

### 8.7 TODO

1. ~~多 TSG 竞争测试~~ ✅ Test E + Test F 完成（同进程 ioctl 验证）
2. **撤回多余内核修改** — 仅保留 osapi.c + nv-gpu-sched-hooks.c + exports 三处
3. **kfunc 端到端测试** — BPF 程序 → bpf_wq → `bpf_nv_gpu_preempt_tsg()` → RM → GSP → preempt
4. **跨进程 preempt 验证** — kfunc 的核心价值：从 BPF 程序 preempt 不同进程的 TSG
5. **集成到 BPF 调度策略** — 在 UVM eviction/prefetch hooks 中通过 bpf_wq 调用 kfunc preempt
6. **动态 preempt 策略** — 根据 fault rate / QoS 信号自动决定 preempt 目标和频率

## 9. 参考文档

- [gpu_preempt_ctrl_design.md](./gpu-ext/driver_docs/sched/gpu_preempt_ctrl_design.md) — 现有 userspace preempt 工具设计
- [ebpf_preempt_design.md](./gpu-ext/driver_docs/sched/ebpf_preempt_design.md) — eBPF preempt 可行性分析
- [GPreempt_Implementation_Analysis.md](./gpu-ext/driver_docs/sched/gpreempt-analysis/GPreempt_Implementation_Analysis.md) — GPreempt 论文实现分析
- [hook_enhancement_analysis.md](./gpu-ext/driver_docs/sched/hook_enhancement_analysis.md) — GPU 调度 hook 增强分析
- `kernel-open/nvidia/nv-gpu-sched-hooks.c` — 当前 GPU sched struct_ops 实现
- `kernel-open/nvidia-uvm/uvm_bpf_struct_ops.c` — UVM BPF struct_ops（sleepable kfunc 参考）
- `extension/gpu_preempt.h` — preempt 机制公共头文件（handle 捕获 + ioctl 封装）
- `extension/test_preempt_demo.bpf.c` — BPF 3-probe handle 捕获策略
- `extension/test_preempt_multi.c` — 多 context 竞争测试（Test E/F）
- `extension/prefetch_cross_block_v2.bpf.c` — bpf_wq 使用模式参考
