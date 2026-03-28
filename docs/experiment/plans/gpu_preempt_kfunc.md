# GPU Preempt kfunc：从内核 BPF 上下文直接抢占 GPU TSG

> **方向**: sleepable kfunc `bpf_nv_gpu_preempt_tsg` + 3-probe handle capture + uprobe 触发
> 上次更新：2026-03-19

---

## 0. 当前快照

- **内核修改**: 仅 3 处文件（osapi.c + nv-gpu-sched-hooks.c + exports_link_command.txt）
- **kfunc 低延迟带**: **177us** (vs ioctl 354us = **2x 提升**)
- **跨进程 preempt**: ✅ 已验证（CUDA-A TSG 被 CUDA-B 触发的 struct_ops→bpf_wq→kfunc preempt）
- **uprobe 直接调用**: ✅ sleepable uprobe on cuLaunchKernel → kfunc，avg **312us**，无需 bpf_wq
- **同进程 ioctl Tests**: E=-48.4% (两等量 context), F=-57.6% (短 LC + 长 BE)
- **Multi-tenant eval (Round 3)**: native P99=38us, timeslice=42us, kfunc=2302us (被 2 次 spike 拉高)
- **核心问题**: 当前 workload (2LC+4BE, 78ms kernel) 竞争不足，native P99 已经很低 (~38us)，timeslice 和 kfunc 都没有 show value 的空间
- **不在当前论文 eval 中**: kfunc 延迟数据和跨进程验证只在 repo docs，论文用的是 timeslice 分化

---

## 1. 核心 Thesis + Novelty

**Thesis**: BPF 程序需要从内核态直接触发 GPU TSG 抢占（跨进程），无法通过 ioctl 路径实现（需要目标进程的 fd）。kfunc 消除 userspace 往返，使 BPF 策略可以在检测到需求后立即 preempt。

**三种 preempt 路径对比**:

| 路径 | Avg | 触发时机 | 需要 bpf_wq | 跨进程 |
|------|-----|---------|-----------|--------|
| ioctl (userspace) | 354us | 手动/polling | 否 | 需 escape.c bypass |
| struct_ops + bpf_wq + kfunc | 540us (warm) | context 创建 | **是** | **是 (RS_PRIV_LEVEL_KERNEL)** |
| **uprobe.s + kfunc** | **312us** | **kernel launch** | **否** | **是** |

**kfunc 的真正价值**:
- 不是绝对延迟更低（RM→GSP RPC 是固定开销 ~200us）
- 而是**消除 userspace 往返 + 支持跨进程**
- uprobe 路径最优：hook cuLaunchKernel，在 GPU kernel 提交时精确触发 preempt

**论文中已有的 scheduling eval** (不依赖 kfunc):
- Compute-bound: timeslice (LC:1s, BE:200us) → LC P99 **-95%** (1188→53us)
- Memory-bound: priority eviction → completion **-55~92%**
- Two-tenant: llama+GNN → TPOT **-40~45%**, GNN **+28%**

### 1.1 背景与动机补充（原 §1）

**历史上观察到的 3 条相关路径**:

```
路径 A: BPF/kprobe -> ringbuf -> userspace poll -> ioctl(NV_ESC_RM_CONTROL)
      -> nvidia_ioctl -> rm_ioctl -> Nv04ControlWithSecInfo -> GSP RPC

路径 B: non-sleepable struct_ops -> bpf_wq_start -> kworker
      -> sleepable kfunc -> RM -> GSP RPC

路径 C: TSG init struct_ops -> non-sleepable kfunc(set_timeslice/interleave)
      -> 修改 ctx 字段 -> nv-kernel.o 读取并应用
```

| 路径 | 关键优点 | 关键限制 | 延迟结论 |
|------|---------|---------|---------|
| A: userspace ioctl | 功能最完整，`test_preempt_ioctl` 已验证 | 依赖 userspace 轮询/上下文切换；跨进程需要额外安全绕过 | 热路径约 **316-356us**，首次冷路径约 **669us** |
| B: bpf_wq | 保持内核态，适合从 non-sleepable hook 间接进入 sleepable kfunc | 多一跳 kworker 调度；原始驱动只用于 UVM migrate | kfunc warm avg **540us**，其中低延迟带 **177us** |
| C: init-time kfunc | 纯内核字段修改，几乎无额外软件路径 | 只能在 TSG 创建时设置，**不是运行时 preempt** | 适合 set_timeslice / set_interleave，不解决运行期抢占 |

**动机收敛**:
- 同进程 preempt 已可通过 fd 复用 + ioctl 实现，因此 kfunc 的重点不是“让 ioctl 更方便”。
- 真正缺口是 **BPF 在内核态对任意进程 TSG 做运行时 preempt**；这要求绕过目标进程 fd 绑定，并留在 sleepable 内核上下文内完成 RM→GSP RPC。
- 因此 v2 选择的核心方向是：**3-probe 负责捕获 handle，sleepable kfunc 负责跨进程执行 preempt**。

---

## 2. 设计架构

### 2.1 kfunc 内核修改（仅 3 处）

| 文件 | 内容 |
|------|------|
| `osapi.c` | `nv_gpu_sched_do_preempt(sp, hClient, hTsg)` — RS_PRIV_LEVEL_KERNEL 跨进程 |
| `nv-gpu-sched-hooks.c` | `bpf_nv_gpu_preempt_tsg(hClient, hTsg)` — KF_SLEEPABLE kfunc |
| `exports_link_command.txt` | 导出 `nv_gpu_sched_do_preempt` 符号 |

### 2.2 Handle 捕获（3-probe，零内核修改）

```
Probe 1: kprobe/nvidia_unlocked_ioctl (入口)
  → 过滤 cmd=0x2B (NV_ESC_RM_ALLOC) + hClass=0xa06c (TSG)
  → 保存 hRoot + NVOS21 ptr

Probe 2: kprobe/nv_gpu_sched_task_init (中间)
  → 补充 engine_type (offset 8)

Probe 3: kretprobe/nvidia_unlocked_ioctl (出口)
  → 读 hObjectNew = hTsg → 只保留 GR engine (type=1)
```

### 2.3 uprobe_preempt_multi（多目标版）

- 最多 16 BE TSG targets，auto-capture by process name
- per-CPU cooldown (default 100us)
- `lc_comm` rodata 过滤：只有 LC 进程的 cuLaunchKernel 触发 preempt
- Stats: hit/ok/err/skip/cooldown_skip/targets_hit/captured/filtered

### 2.4 现有代码分析（原 §2，压缩版）

```c
struct nv_gpu_sched_ops {
    int (*on_task_init)(struct nv_gpu_task_init_ctx *ctx);
    int (*on_bind)(struct nv_gpu_bind_ctx *ctx);
    int (*on_task_destroy)(struct nv_gpu_task_destroy_ctx *ctx);
};

NV_STATUS NV_API_CALL rm_ioctl(nvidia_stack_t *, nv_state_t *,
                               nv_file_private_t *, NvU32, void *, NvU32);
NV_STATUS kchangrpapiCtrlCmdPreempt_IMPL(
    KernelChannelGroupApi *pKernelChannelGroupApi,
    NVA06C_CTRL_PREEMPT_PARAMS *pPreemptParams);
```

| 主题 | 关键文件 / 接口 | 压缩结论 |
|------|----------------|---------|
| GPU sched struct_ops 框架 | `kernel-open/nvidia/nv-gpu-sched-hooks.{h,c}` | 已有 `on_task_init/on_bind/on_task_destroy`；现存 kfunc `bpf_nv_gpu_set_timeslice` / `bpf_nv_gpu_set_interleave` / `bpf_nv_gpu_reject_bind` 都是 non-sleepable，只改 ctx 字段 |
| UVM BPF 参考框架 | `kernel-open/nvidia-uvm/uvm_bpf_struct_ops.c` | 已有 `bpf_gpu_migrate_range(...)` 作为 sleepable GPU kfunc 先例；调用模式是 non-sleepable hook -> `bpf_wq` -> sleepable kfunc |
| RM preempt 路径 | `src/.../kernel_channel_group_api.c` 中 `kchangrpapiCtrlCmdPreempt_IMPL(...)` | 真正的 preempt 落点在 RM 对象系统；RTX 5090/GSP 架构下最终走 `NV_RM_RPC_CONTROL`，调用会 sleep |
| ioctl 调用链 | `nvidia_ioctl -> rm_ioctl -> Nv04ControlWithSecInfo -> RM dispatch -> kchangrpapiCtrlCmdPreempt_IMPL -> GSP RPC` | ioctl 路径本身可工作，但需要目标 fd / `nvfp` 匹配；跨进程不适合 BPF 策略直接复用 |
| 现有 BPF 工具 | `extension/gpu_preempt_ctrl.bpf.c`, `extension/prefetch_cross_block_v2.bpf.c` | `gpu_preempt_ctrl` 依赖不存在的 tracepoints，历史上从未真正加载；`prefetch_cross_block_v2` 提供了 `bpf_wq` 使用模式参考 |
| GPreempt 参考 | `docs/gpu-ext/driver_docs/sched/gpreempt-analysis/` | 论文路线是 `1us` timeslice + async RPC + GDRCopy；本项目保留同步 RPC，只去掉 userspace 往返，风险更低 |

### 2.5 技术约束（原 §3）

| 约束 | 原因 | 设计落点 |
|------|------|---------|
| preempt kfunc 必须是 sleepable | GSP RPC 等待 firmware response，RM 路径可能获取 mutex/semaphore，不能在 spinlock / hard non-sleepable 上下文里调用 | `bpf_nv_gpu_preempt_tsg` 标记 `KF_SLEEPABLE`；non-sleepable hook 通过 `bpf_wq` 间接调用 |
| kernel-open 与 nv-kernel.o 有清晰边界 | `nvidia.ko = kernel-open/*.o + nv-kernel.o`；kfunc 写在 kernel-open，真正的 RM control 在 nv-kernel.o | 通过 `osapi.c` 新增导出 helper，而不是在 BPF 侧拼 `nvfp` / `threadState` |
| RM 上下文只需 `(hClient, hTsg)` | handle 可由 3-probe 在 stock module 上捕获；RM 内部再据此查对象 | 执行侧不再依赖修改 ctx struct；同进程 ioctl 用 fd 复用，跨进程用 `RS_PRIV_LEVEL_KERNEL` |

### 2.6 `gpu_preempt.h` 公共接口（原 §8.5）

| 函数/宏 | 说明 |
|---------|------|
| `gp_rm_control(fd, hClient, hObject, cmd, params, size)` | RM control ioctl 封装 |
| `gp_preempt(fd, hClient, hTsg)` | TSG preempt，默认 `bWait=1`、`timeout=100ms` |
| `gp_set_timeslice(fd, hClient, hTsg, us)` | 动态设置 TSG timeslice |
| `gp_find_cuda_fd(hClient, hTsg, verbose)` | 扫描 `/proc/self/fd`，复用 CUDA 打开的 nvidia fd |
| `gp_get_time_us()` | `CLOCK_MONOTONIC` 微秒时间戳 |
| `gp_engine_str(engine_type)` | engine 类型字符串化 |
| `GP_CHECK_CUDA(call)` | CUDA 错误检查宏 |
| `gp_ptx_source` | busy-loop PTX，带 anti-DCE 写回 |
| `struct gp_worker` + `gp_worker_init/start/join` | GPU worker 线程与 sample 采集基础设施 |
| `gp_cuda_warmup(ctx)` | 触发 context/TSG 创建的 warmup helper |

### 2.7 实现方案补充（原 §4，保留关键签名）

**方案选择**:

| | osapi.c 方案（采用） | escape.c bypass（弃用） |
|---|---|---|
| 权限提升位置 | 内核 helper 内部封装 | 修改现有 ioctl 安全路径 |
| 谁能跨进程 preempt | 仅 kfunc / 内核态调用者 | 任意 root 进程也可借 ioctl 触发 |
| 攻击面 | 不扩大 userspace ioctl 表面 | 扩大 |

```c
NvU32 NV_API_CALL nv_gpu_sched_do_preempt(
    nvidia_stack_t *sp, NvU32 hClient, NvU32 hTsg);

__bpf_kfunc int bpf_nv_gpu_preempt_tsg(u32 hClient, u32 hTsg);
BTF_ID_FLAGS(func, bpf_nv_gpu_preempt_tsg, KF_SLEEPABLE);

extern int bpf_nv_gpu_preempt_tsg(u32 hClient, u32 hTsg) __ksym;
static int do_preempt_wq(void *map, int *key, void *val);
```

**实现要点**:
- `osapi.c` helper 内部走 `NV_ENTER_RM_RUNTIME` + `Nv04ControlWithSecInfo(RS_PRIV_LEVEL_KERNEL)` + `PARAM_LOCATION_KERNEL`，不要求 BPF 侧构造 `nv_file_private_t`。
- `nv-gpu-sched-hooks.c` 中的 kfunc 只负责参数校验、`nv_kmem_cache_alloc_stack/free_stack` 和错误码转换。
- BPF 测试程序侧只保留 `__ksym` 声明和 `bpf_wq` callback 签名；完整实现留在 `extension/test_preempt_kfunc.{bpf.c,c}`。
- Phase 2 方向是增加 sleepable `on_sched_tick` 类 hook，减少对 `bpf_wq` 的依赖。
- Phase 3 方向是探索 fire-and-forget async RPC；该方向风险高，因此未进入当前最小实现。

---

## 3. 决策记录

| 决策 | 原因 | 日期 |
|------|------|------|
| 选 osapi.c 方案而非 escape.c | 权限提升封装在内核函数内部，不扩大 ioctl 攻击面 | 2026-03-03 |
| Handle 捕获用 3-probe 而非修改 ctx struct | 零内核修改，stock module 也能用 | 2026-03-03 |
| 同进程 preempt 用 fd 复用而非 kfunc | 扫描 /proc/self/fd 找 CUDA 的 nvidia fd | 2026-03-03 |
| kfunc 从干净分支重新实现 | 初版多余修改（hClient/hTsg fields, escape.c bypass）全部不需要 | 2026-03-04 |
| uprobe.s 直接调用 kfunc（不用 bpf_wq）| sleepable uprobe 运行在 process context，可直接调 sleepable kfunc | 2026-03-04 |
| Per-launch preempt 在低竞争下有害 | preempt 是同步的 (~300us)，阻塞 cuLaunchKernel 返回 | 2026-03-04 |
| kfunc preempt 只在 BE 占满 GPU 时有效 | BE 未饱和时 LC 可并行跑，preempt 反而增加开销 | 2026-03-04 |
| uprobe_preempt_multi 加 lc_comm 过滤 | 不过滤时 BE 自己的 launch 也触发 preempt → BE 吞吐暴跌 73% | 2026-03-19 |

### 3.1 风险与注意事项（原 §6）

| 风险 | 影响 | 当前缓解 |
|------|------|---------|
| RM API 调用安全性 | `RS_PRIV_LEVEL_KERNEL` 可绕过 ownership 检查，若暴露到 ioctl 会扩大攻击面 | 权限提升只封装在 `nv_gpu_sched_do_preempt()` 内，不改 userspace ioctl 行为 |
| GPU lock 竞争 | 高频 preempt 可能与正常 RM control 争用 GPU 锁 | 当前只在策略判定明确时触发；评估中重点观察高频 preempt 对吞吐的副作用 |
| `hClient/hTsg` 有效性 | TSG 销毁后再 preempt 会得到 invalid object / status error | 由 3-probe + 生命周期管理减少悬空 handle；kfunc 直接传播失败 |
| BPF verifier 兼容性 | `KF_SLEEPABLE` 只能在 sleepable 程序类型或 `bpf_wq` callback 中调用 | non-sleepable hook 使用 `bpf_wq`；sleepable uprobe 直接调用 |

---

## 4. 任务追踪表

| # | 任务 | 状态 | 关键结果 |
|---|------|:---:|------|
| 1 | ioctl preempt 验证 (test_preempt_ioctl) | ✅ | status=0, 热路径 ~300us |
| 2 | test_preempt_demo (BPF+CUDA+ioctl 自包含) | ✅ | A: avg=337us; D: kernel +184% |
| 3 | 3-probe handle capture (零内核修改) | ✅ | stock module 上 3 TSG captured |
| 4 | 同进程 fd 复用 preempt | ✅ | 扫描 /proc/self/fd → fd=22 匹配 |
| 5 | gpu_preempt.h 公共头 | ✅ | gp_preempt/set_timeslice/find_cuda_fd |
| 6 | Test E: 两等量 context | ✅ | A latency **-48.4%** |
| 7 | Test F: 短 LC + 长 BE | ✅ | A latency **-57.6%** |
| 8 | kfunc 最小修改版（3 处文件）| ✅ | 编译+加载+kallsyms 确认 |
| 9 | kfunc 端到端测试 | ✅ | BPF→bpf_wq→kfunc→RM→GSP preempt_ok=1, preempt_err=0 |
| 10 | 跨进程 kfunc preempt | ✅ | CUDA-A(PID=130830) 被 CUDA-B(PID=131060) 触发的 kfunc preempt |
| 11 | kfunc vs ioctl 延迟 benchmark | ✅ | kfunc 低延迟带 177us (2x vs ioctl 354us) |
| 12 | Sleepable uprobe 直接调用 kfunc | ✅ | avg=312us, 无需 bpf_wq |
| 13 | 优先级调度实验 (§8.12) | ✅ | per-launch preempt 在低竞争下有害 (+7.5x LC latency) |
| 14 | uprobe_preempt_multi (多目标版) | ✅ | 16 targets, auto-capture, cooldown, lc_comm 过滤 |
| 15 | multi_stream_bench 复制 | ✅ | eunomia-bpf/co-processor-demo → microbench/multi-stream/ |
| 16 | 4-mode eval Round 1 (替代 benchmark) | ✅ 无效 | kernel ~13s，数据全部相同 |
| 17 | 4-mode eval Round 2 (正版 benchmark) | ✅ Bug | kfunc 未过滤进程名 → BE 吞吐 -73% |
| 18 | 4-mode eval Round 3 (lc_comm 过滤 + 78ms kernel) | ✅ | native P99=38us, ts=42us, kfunc=2302us (spike) |
| 19 | 增加竞争强度重跑 | ❌ | 选项: 更多 BE / 更短 kernel / 大 BE timeslice |
| 20 | 集成到 UVM 策略 (fault-triggered preempt) | ❌ | 在 gpu_block_activate 中根据 LC fault 压力触发 preempt |

### 4.1 实施进展补充（原 §8.1 / §8.2 / §8.4 / §8.11）

| 原节 | 任务/发现 | 状态 | 关键结论 |
|------|-----------|:---:|---------|
| §8.1 | `gpu_preempt_ctrl` 失效根因排查 | ✅ | 依赖的 `nvidia_gpu_tsg_create/schedule/destroy` tracepoints 在驱动源码中不存在；历史上实际从未成功加载 |
| §8.1 | `NV_ESC_RM_CONTROL` 常量纠错 | ✅ | 正确 inner cmd 是 **`0x2A`**；误用 `2` 会直接得到 `EINVAL` |
| §8.1 | 直接 ioctl preempt 成功 + 调用链定位 | ✅ | 热路径 preempt/status=0，约 **316us**；链路收敛为 `nvidia_ioctl -> rm_ioctl -> Nv04ControlWithSecInfo -> RM dispatch -> GSP RPC` |
| §8.2 | `test_preempt_demo` 设计压缩版 | ✅ | 单进程/单二进制整合 **BPF + CUDA + ioctl**；PTX busy-loop 通过写 global memory 防 DCE；应用 GR TSG 通常是最后创建的 `0x5c*`，而 `0xcaf*` 多为系统 TSG |
| §8.4 | fd reuse preempt | ✅ | 扫描 `/proc/self/fd` 中的 `/dev/nvidia*`，找到与 `hClient` 对应的 CUDA fd；实测 `fd=22` 命中 |
| §8.11 | sleepable uprobe 直接调用 kfunc | ✅ | `SEC("uprobe.s/...")` 运行在 process context，可直接调用 `KF_SLEEPABLE` kfunc，无需 `bpf_wq` |

### 4.2 3-probe Handle 捕获细节（原 §8.3）

**关键内存布局**:

| 结构 | offset | 字段 |
|------|--------|------|
| `NVOS21_PARAMETERS` | 0 | `hRoot` (= `hClient`) |
| `NVOS21_PARAMETERS` | 4 | `hObjectParent` |
| `NVOS21_PARAMETERS` | 8 | `hObjectNew` (= `hTsg`, RM alloc 回填) |
| `NVOS21_PARAMETERS` | 12 | `hClass` (= `0xa06c` for TSG) |

| 结构 | offset | 字段 |
|------|--------|------|
| `nv_ioctl_xfer_t` | 0 | `cmd` (inner cmd，例如 `0x2B`) |
| `nv_ioctl_xfer_t` | 4 | `size` |
| `nv_ioctl_xfer_t` | 8 | `ptr` (user pointer to `NVOS21`) |

**stock module 结果摘要**:
- 3-probe 在未改内核模块上稳定捕获到 3 个应用侧 TSG：`GR(1)`、`CE(13)`、`CE2(14)`。
- 一行结果：`Test A avg=358us`，`burst C 100/100 成功`，`Test D kernel +180%`，说明零内核修改方案既能抓 handle，也能证明 preempt 真正打断了 GPU 执行。

### 4.3 TODO 收敛（原 §8.8）

- 已完成：多 TSG 竞争测试、撤回多余内核修改、kfunc 编译/加载、端到端验证、跨进程 preempt 验证。
- 已转化为当前主线：把“集成到 BPF 调度策略”具体化为 `uprobe_preempt_multi` 与 4-mode eval。
- 剩余开放项：根据 fault rate / QoS 信号做动态 preempt 策略，以及在 UVM policy 中做 fault-triggered preempt。

---

## 5. 关键实验结果

### 5.1 kfunc 延迟

| 路径 | Avg | Min | Max |
|------|-----|-----|-----|
| ioctl (标准) | 354us | 313us | 398us |
| ioctl (突发 100x) | 181us | — | — |
| kfunc (warm avg) | 540us | 147us | 1380us |
| kfunc (低延迟带) | **177us** | 147us | 207us |
| kfunc (高延迟带) | 1014us | 772us | 1380us |
| uprobe.s + kfunc | **312us** | 277us | 328us |

#### 5.1.1 Sleepable uprobe 机制说明（原 §8.11）

- `SEC("uprobe.s/...")` 对应 sleepable uprobe，程序运行在进程上下文，可合法进入会 sleep 的 RM/GSP 路径。
- `bpf_nv_gpu_preempt_tsg` 注册在 `BPF_PROG_TYPE_UNSPEC` kfunc 集合中，因此不局限于 struct_ops；uprobe 程序可直接调用。
- 这条路径省掉了 `struct_ops -> bpf_wq -> kworker` 中间层，触发点又刚好落在 `cuLaunchKernel`，所以成为当前最精准的 launch-time preempt 机制。

### 5.2 同进程竞争 Tests

| Test | 场景 | 无 preempt | 有 preempt | 改善 |
|------|------|-----------|-----------|------|
| E | 两等量 context | 539ms | 278ms | **-48.4%** |
| F | 短 LC + 长 BE | 7.1ms | 3.0ms | **-57.6%** |
| D | 单 context 持续 preempt | 260ms | 739ms | +184% (开销) |

#### 5.2.1 Test E 细化：两等量 context

| Phase | A avg/min/max/stddev | B avg | Preempts issued | 解读 |
|------|-----------------------|------|-----------------|------|
| 无 preempt | `539455 / 539411 / 539501 / 33 us` | `539383 us` | — | 两个 ~260ms kernel 基本均分 GPU，A 的 observed time 约等于 `kernel 260ms + 等待 260ms` |
| 持续 preempt B | `278392 / 276919 / 280077 / 1013 us` | — | `23411` | B 被频繁打断后，A 接近拿回完整 GPU 份额，时间回落到接近无竞争 baseline |

**结论**:
- Test E 本质是在验证 **TSG preempt 能把 A 的 GPU 份额从约 50% 拉回到接近 100%**。
- preempt 次数高 (`23411`) 说明这里的收益来自持续压制 B，而不是偶发一次性调度修正。
- 从“SM/调度份额”视角看，A 在无 preempt 时只能拿到接近一半的服务窗口；持续 preempt B 后，A 的有效 GPU 份额回升到接近独占。

#### 5.2.2 Test F 细化：短 LC + 长 BE

| Phase | A avg/min/max/stddev | Preempts issued | 调度等待估算 | 解读 |
|------|-----------------------|-----------------|-------------|------|
| 无 preempt | `7091 / 7089 / 7093 / 1 us` | — | `~4ms` | A 的纯 kernel 时间约 `3ms`，其余主要是等待 B 的 GPU timeslice |
| 持续 preempt B | `3005 / 2811 / 4992 / 456 us` | `469` | 接近 `0ms` | 一旦 B 被中断，A 几乎立即上 GPU，observed time 回到纯 kernel 执行区间 |

**结论**:
- Test F 说明 kfunc/preempt 的核心收益不是“让 kernel 自己更快”，而是**消掉短任务的 scheduling wait**。
- preempt 次数仅 `469`，远低于 Test E，因为短 LC 一旦拿到 GPU 就会很快完成，不需要持续轰炸式 preempt。
- 这里真正被优化的是“排队等待 B 让出 GPU 的时间”，而不是 A 的 SM 执行效率本身。

**Test D 参考意义**:
- 单 TSG 持续 preempt 会把 kernel 时间从 `260ms` 拉高到 `739ms`（`+184%`），因此 preempt 本身是重量级操作，只应在存在真实竞争时触发。

### 5.3 Multi-tenant 4-mode Eval (Round 3)

| Mode | LC P99 (us) | BE Throughput (k/s) |
|------|------------|---------------------|
| native | 38.2 | 1.909 |
| timeslice_only | 42.2 | 1.908 |
| kfunc_only | 2302 | 1.810 |
| timeslice_kfunc | 2302 | 1.814 |

**Per-run**: kfunc 大多数 run P99=28-31us (< native 37-43us)，但 run 1 和 run 4 出现 ~11ms spike。

**根因**: native P99 已经很低 (38us)，当前 workload (2LC+4BE, 78ms kernel) 竞争不足以产生论文的 1188us 调度尖峰。

### 5.4 早期实验教训

| 实验 | 结果 | 教训 |
|------|------|------|
| uprobe on LC launch → preempt BE (BE 未饱和) | LC **+7.5x** | preempt 同步 ~300us 阻塞 cuLaunchKernel |
| uprobe on BE launch → preempt BE | LC **+34%** | 每次 BE launch 都 preempt = BE 变慢 |
| persistent BE kernel + preempt | LC **-57.6%** | **kfunc 只在 BE 占满 GPU 时有效** |

#### 5.4.1 实验 A/B：per-launch preempt 反而变慢（原 §8.12.1）

| 实验 | Hook 点 / BE 形态 | 无 preempt (avg/median/P99) | 有 preempt (avg/median/P99) | 结论 |
|------|-------------------|-----------------------------|-----------------------------|------|
| A | uprobe on LC `cuLaunchKernel`；BE `<<<512,256>>>` loop | `323 / 137 / 2235 us` | `2427 / 2569 / 2502 us` | LC **+7.5x**，因为每次 launch 都同步卡在 ~300us preempt 调用上 |
| B | uprobe on BE `cuLaunchKernel`；BE 占满 GPU | `3069 / 2340 / 6680 us` | `4108 / 4290 / 8770 us` | LC 仍变慢；每次 BE launch 都 preempt，BE 更慢，反而拉长总体等待 |

**为什么 A 会失败**:
- `<<<512,256>>>` loop 只给出 `512` blocks，而 RTX 5090 的满占用可到约 `1360` active blocks，BE 实际只占 GPU 约 **38%** 容量。
- 在这种低占用下，LC 原本就能与 BE 并行跑；额外插入同步 preempt 只会增加 host 侧 `cuLaunchKernel` 阻塞时间。

#### 5.4.2 修正实验：persistent kernel 占满 GPU（原 §8.12.2）

| 场景 | BE 模式 | LC Avg | LC Median | LC Max | LC P95 |
|------|---------|--------|-----------|--------|--------|
| S1 | `<<<512,256>>>` loop（部分占用） | 5184 us | 4960 us | 9542 us | 9542 us |
| S2 | persistent kernel，占满所有 SM | 6141 us | 5802 us | 8302 us | 8302 us |
| S3 | S2 + BPF preempt | **4389 us** | **4859 us** | **4899 us** | **4899 us** |

**S2 -> S3 改善**:
- `avg -29%` (`6141 -> 4389 us`)
- `max -41%` (`8302 -> 4899 us`)
- `P95 -41%` (`8302 -> 4899 us`)
- `方差 -66%`，尾延迟明显更可预测

**机制解释**:
- S2 中 persistent BE 把所有 SM slot 占满，LC 只能等硬件 timeslice 轮转，因此会看到接近 `8ms` 的尾延迟。
- S3 中 BPF preempt 强制打断 BE TSG，LC 不再完全受随机 timeslice 影响；但仍要支付约 `300us` 的同步 preempt 成本和 GPU context switch 成本，所以不会接近 `0us`。
- 原文观测到 S3 有双峰：一部分样本在 `~4.8ms`，一部分在 `~3.3ms`，对应 “显式 preempt + context switch” 与 “BE 恰好处于 yield 窗口” 两类情况。

**何时值得用 preempt**:

| 条件 | 效果 | 原因 |
|------|------|------|
| BE 部分占 GPU | 无效/有害 | LC 可并行运行，preempt 只增加同步开销 |
| BE 占满 GPU + 短 timeslice | 中等收益 | 主要改善尾延迟，平均值提升有限 |
| BE 占满 GPU + 长 timeslice | 显著收益 | 可把 LC 从 ms 级等待中救出来 |
| UVM eviction / memory pressure | 必要 | timeslice 不能释放 VRAM 页，preempt 才能强制让出 |
| QoS enforcement | 有效 | 可对超时/失控 BE 做一次性惩罚 |

### 5.5 验证方案清单（原 §7）

- [x] 编译验证：`kernel-module/nvidia-module` 执行 `make -j$(nproc) modules`，最小修改版可通过编译。
- [x] 加载验证：`nvidia` / `nvidia-modeset` / `nvidia-uvm` 模块可正常加载，`dmesg` 出现 `GPU sched struct_ops initialized`。
- [x] kfunc 可用性验证：`kallsyms` 可见 `bpf_nv_gpu_preempt_tsg` 与 `nv_gpu_sched_do_preempt`，BPF verifier 能接受对应程序。
- [x] 功能验证：`test_preempt_demo`、`test_preempt_kfunc`、`test_uprobe_preempt` 均已跑通；跨进程 preempt 和 direct uprobe preempt 已确认。
- [x] 延迟对比验证：`ioctl` 标准均值 `354us`，kfunc 低延迟带 `177us`，sleepable uprobe 直接调用 `312us`。

---

## 6. 下一步选项

| 选项 | 描述 | 预期 |
|------|------|------|
| A | 增加 BE 到 8-16 个进程 | 产生更强 TSG 竞争 |
| B | 缩短 kernel 到 1-5ms | 增加 launch 频率和调度压力 |
| C | 给 BE 设大 timeslice (1s) | 测试 kfunc 在 native 无法及时让出时的价值 |
| D | Memory-bound 混合场景 | kfunc 的真正独特价值：UVM fault 触发 preempt |

---

## Appendix: 原始数据位置

- 原始 plan: `docs/gpu_preempt_kfunc_plan.md` (1320 行完整历史)
- kfunc 内核代码: `kernel-module/nvidia-module/kernel-open/nvidia/nv-gpu-sched-hooks.c:113-145`
- osapi.c preempt: `kernel-module/nvidia-module/src/nvidia/arch/nvalloc/unix/src/osapi.c:5985-6030`
- BPF test 程序: `extension/test_preempt_{demo,multi,kfunc}.{bpf.c,c}`
- uprobe preempt: `extension/test_uprobe_preempt.{bpf.c,c}`
- 多目标版: `extension/uprobe_preempt_multi.{bpf.c,c}`
- gpu_preempt.h: `extension/gpu_preempt.h`
- Eval 脚本: `docs/gpu-ext/eval/multi-tenant-scheduler/kfunc_preempt_test.py`
- Eval 结果: `docs/gpu-ext/eval/multi-tenant-scheduler/kfunc_preempt_results/`
- Microbench: `microbench/multi-stream/`
- Priority demo: `scripts/extension/preempt/test_priority_demo.cu`, `run_priority_demo.sh`
- Bench scripts: `scripts/extension/preempt/bench_preempt_kfunc.{py,sh}`

### Appendix A. 关键文件清单（原 §5）

| 文件 | 角色 |
|------|------|
| `kernel-module/nvidia-module/src/nvidia/arch/nvalloc/unix/src/osapi.c` | `nv_gpu_sched_do_preempt()` helper；内核态 `RS_PRIV_LEVEL_KERNEL` preempt 执行入口 |
| `kernel-module/nvidia-module/kernel-open/nvidia/nv-gpu-sched-hooks.c` | `bpf_nv_gpu_preempt_tsg()` kfunc 定义与 BTF 注册 |
| `kernel-module/nvidia-module/src/nvidia/exports_link_command.txt` | 导出 `nv_gpu_sched_do_preempt` 供 kernel-open 调用 |
| `kernel-module/nvidia-module/kernel-open/nvidia/nv-gpu-sched-hooks.h` | `nv_gpu_sched_ops` / ctx struct 定义 |
| `kernel-module/nvidia-module/kernel-open/nvidia-uvm/uvm_bpf_struct_ops.c` | sleepable GPU kfunc + `bpf_wq` 参考实现 |
| `extension/gpu_preempt.h` | 同进程 ioctl preempt / timeslice / fd 复用公共 API |
| `extension/test_preempt_demo.{bpf.c,c}` | 自包含 BPF + CUDA + ioctl 验证 |
| `extension/test_preempt_multi.{bpf.c,c}` | 多 context 竞争 Tests E/F |
| `extension/test_preempt_kfunc.{bpf.c,c}` | kfunc 端到端测试（kprobe + struct_ops + `bpf_wq`） |
| `extension/test_uprobe_preempt.{bpf.c,c}` | sleepable uprobe 直接调用 kfunc 验证 |
| `extension/uprobe_preempt_multi.{bpf.c,c}` | 多目标 auto-capture + cooldown + `lc_comm` 过滤版 |
| `scripts/extension/preempt/test_priority_demo.cu` | 优先级调度 persistent-kernel demo |
| `docs/gpu-ext/eval/multi-tenant-scheduler/kfunc_preempt_test.py` | 4-mode kfunc vs timeslice eval runner |

### Appendix B. 参考文档（原 §9）

- `docs/gpu-ext/driver_docs/sched/gpu_preempt_ctrl_design.md`：现有 userspace preempt 工具设计。
- `docs/gpu-ext/driver_docs/sched/ebpf_preempt_design.md`：eBPF preempt 可行性分析。
- `docs/gpu-ext/driver_docs/sched/gpreempt-analysis/GPreempt_Implementation_Analysis.md`：GPreempt 实现分析。
- `docs/gpu-ext/driver_docs/sched/hook_enhancement_analysis.md`：GPU 调度 hook 增强分析。
- `extension/prefetch_cross_block_v2.bpf.c`：`bpf_wq` 使用模式参考。
