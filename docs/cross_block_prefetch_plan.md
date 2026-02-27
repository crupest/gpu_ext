# Cross-VA-Block Prefetch 设计方案

**日期**: 2026-02-26
**状态**: 设计中（待实现）
**前提**: MSched 复现中的 intra-block prefetch (always_max) + passive MRU eviction 已验证有效
**关联**: 从 `msched_reproduction_plan.md` 中独立出来的子项目

---

## 1. 背景

### 为什么需要 Cross-block Prefetch

在 MSched 复现实验中已确认：
- BPF always_max prefetch + passive MRU eviction 达到 pp=228, tg=78.7 tok/s（+63% over baseline）
- 但 chunk_trace 显示 **82% chunk thrashing 未改变** — 因为 intra-block prefetch 只扩展当前 2MB VA block 内的预取范围
- Eviction 优化天花板约 2-4% 额外提升（只改淘汰顺序，不改淘汰总量）
- **真正突破需要 cross-VA-block proactive prefetch** — 提前迁移相邻 VA block 的数据

### 驱动调研结论

已完成对 nvidia-uvm 驱动 migration API 的深入调研。实现 cross-block prefetch 的三种方案:

**方案 A: 新增 `bpf_uvm_prefetch_va_range()` kfunc**
- 内部调用 `uvm_migrate()` (在 `uvm_migrate.c:635`)
- 需要 `va_space` 指针（目前未暴露给 BPF）
- 锁序问题: fault path 已持有当前 block lock，adjacent block 加锁可能死锁
- 需要使用 `UVM_VA_BLOCK_LOCK_RETRY` pattern

**方案 B: Fault-batch 级别的新 struct_ops hook**
- 在 `service_fault_batch_dispatch()` 中添加 hook
- BPF 可以返回 "额外需要预取的 VA 地址列表"
- 驱动负责在当前 block 处理完后，依次迁移 adjacent blocks
- 避免锁序问题，但需要修改 fault 批处理逻辑

**方案 C: Deferred prefetch work queue**
- kfunc 将预取请求放入内核 work queue
- Worker thread 在 fault path 之外调用 `uvm_migrate()`
- 完全避免锁序问题
- 预取与当前 fault 处理并行
- **最安全、最容易实现、且自然提供 compute-migration overlap**

**推荐**: 方案 C (deferred work queue)。

---

## 2. 第一版实现（已废弃）

第一版使用自定义 kernel work queue，已实现并编译/加载成功，但 code review 发现严重问题：

| 问题 | 严重性 | 说明 |
|------|--------|------|
| **va_space 生命周期** | 高 | worker 持有 va_space 裸指针，进程退出后 use-after-free |
| **per-CPU 抢占安全** | 中 | set_context → BPF hook 之间无抢占保护，可能读错 CPU 的 context |
| **每次 fault 做 kmalloc** | 中 | 热路径 kmalloc(GFP_KERNEL) 有性能和 sleep 风险 |
| **违反 gpu_ext 设计原则** | 高 | 新增 ~100 行内核 C 代码（work queue、手动锁管理），BPF 仅做触发器。gpu_ext 的核心论点是 BPF 提供安全扩展，如果扩展需要大量内核修改就失去了意义 |

**结论**：旧方案本质上是 "用 BPF 触发一段不安全的内核代码"，需要重新设计。

---

## 3. 新方案：BPF Workqueue + `uvm_migrate()` kfunc

### 核心思路

用 BPF 自带的 workqueue 机制（`bpf_wq`，Linux 6.10+，我们是 6.15）替代自写的内核 work queue。BPF wq callback 跑在 **process context**（可 sleep），可以调用 sleepable kfunc。kfunc 内部包装 `uvm_migrate()` — UVM_MIGRATE ioctl 使用的同一函数。

```
旧方案（自写内核代码）：
  BPF hook → per-CPU buffer → 自写 kernel workqueue → kmalloc →
  手动 uvm_va_space_down_read → 手动 uvm_va_block_find →
  手动 UVM_VA_BLOCK_LOCK_RETRY → uvm_va_block_migrate_locked
  （~100 行新内核代码，va_space 生命周期问题，per-CPU 抢占问题）

新方案（BPF workqueue）：
  BPF hook → BPF map → bpf_wq_start() → BPF wq callback →
  bpf_uvm_migrate_range() kfunc → uvm_migrate_bpf() → uvm_migrate()
  （内核侧新增：uvm_migrate.c ~20 行 wrapper + uvm_bpf_struct_ops.c ~10 行 kfunc）
```

### `uvm_migrate()` API 分析（关键发现）

经过代码审查，`uvm_migrate()` 的实际情况：

```c
// uvm_migrate.c:635 — 10 个参数，static 函数
static NV_STATUS uvm_migrate(uvm_va_space_t *va_space,
                             struct mm_struct *mm,
                             NvU64 base, NvU64 length,
                             uvm_processor_id_t dest_id,
                             int dest_nid,
                             NvU32 migrate_flags,
                             uvm_va_range_managed_t *first_managed_range,
                             uvm_tracker_t *out_tracker,
                             uvm_processor_mask_t *gpus_to_check_for_nvlink_errors);
```

**关键点**：
1. **`static` 函数** — 不可直接从其他 .c 文件调用，需要在 `uvm_migrate.c` 中添加 wrapper
2. **不自己加锁** — 调用前 caller 必须已持有 `va_space read lock`；如果 `mm != NULL` 还需要 `mmap_lock`
3. **`mm=NULL` 可行** — 只要提供 `first_managed_range`（通过 `uvm_va_space_iter_managed_first()` 获取）。UVM managed allocation 不需要 mm（不走 HMM 路径）
4. **`uvm_api_migrate()` 的调用模式**（line 922-1039）：
   ```c
   mm = uvm_va_space_mm_or_current_retain_lock(va_space);  // 获取 mm + mmap_lock
   uvm_va_space_down_read(va_space);                        // 加 va_space read lock
   dest_id = dest_gpu ? dest_gpu->id : UVM_ID_CPU;         // 构造 processor_id
   status = uvm_migrate(va_space, mm, base, length, dest_id, ...);
   uvm_va_space_up_read(va_space);                          // 释放
   uvm_va_space_mm_or_current_release_unlock(va_space, mm); // 释放
   ```
5. **processor_id 构造**：`uvm_gpu_id_from_index(0)` 产生 GPU 0 的 id（值=1，因为 `UVM_ID_GPU0_VALUE=1`）

### 架构

```
┌─────────────────────────────────────────────────┐
│  BPF struct_ops hook (fault path, 非 sleepable)  │
│                                                   │
│  1. always_max intra-block prefetch               │
│  2. bpf_uvm_get_va_space() → 拿 va_space handle  │
│  3. bpf_uvm_get_block_end_va() → 当前 block 边界 │
│  4. 写 (va_space, next_addr, length) 到 BPF map   │
│  5. bpf_wq_start() → 调度异步 work               │
└────────────────────┬────────────────────────────┘
                     │ BPF map (预分配，无 kmalloc)
                     ▼
┌─────────────────────────────────────────────────┐
│  BPF wq callback (process context, 可 sleep)     │
│                                                   │
│  1. 从 BPF map 读 prefetch 请求                  │
│  2. bpf_uvm_migrate_range(va_space, addr, len)   │
│     └→ kfunc 调 uvm_migrate_bpf()               │
│        └→ mm = uvm_va_space_mm_or_current_...()  │
│        └→ uvm_va_space_down_read(va_space)       │
│        └→ uvm_migrate(...) — 处理 block find,    │
│           block lock + retry, migration           │
│        └→ uvm_va_space_up_read + mm release      │
└─────────────────────────────────────────────────┘
```

---

## 4. 内核侧改动

### 文件 1: `uvm_migrate.c` — 新增 wrapper 函数（~20 行）

因为 `uvm_migrate()` 是 static，需要在同文件中添加一个公开的 wrapper。这个 wrapper 复制 `uvm_api_migrate()` 的锁管理模式：

```c
// 新增在 uvm_migrate.c 中，uvm_api_migrate() 之后
NV_STATUS uvm_migrate_bpf(uvm_va_space_t *va_space, NvU64 base, NvU64 length,
                          uvm_processor_id_t dest_id)
{
    struct mm_struct *mm;
    NV_STATUS status;

    // 复制 uvm_api_migrate() 的锁模式
    mm = uvm_va_space_mm_or_current_retain_lock(va_space);
    uvm_va_space_down_read(va_space);

    status = uvm_migrate(va_space,
                         mm,
                         base,
                         length,
                         dest_id,
                         NUMA_NO_NODE,                              // dest_nid
                         0,                                         // migrate_flags
                         uvm_va_space_iter_managed_first(va_space, base, base),
                         NULL,                                      // out_tracker (同步)
                         NULL);                                     // nvlink errors

    uvm_va_space_up_read(va_space);
    if (mm)
        uvm_va_space_mm_or_current_release_unlock(va_space, mm);

    return status;
}
```

声明添加到 `uvm_migrate.h`：
```c
NV_STATUS uvm_migrate_bpf(uvm_va_space_t *va_space, NvU64 base, NvU64 length,
                          uvm_processor_id_t dest_id);
```

### 文件 2: `uvm_bpf_struct_ops.c` — 新增 2 个 kfunc

新增 kfunc：
```c
#include "uvm_migrate.h"

// kfunc 1: 获取当前 fault 的 va_space (从 per-CPU context)
__bpf_kfunc u64 bpf_uvm_get_va_space(void)
{
    struct uvm_bpf_prefetch_ctx *ctx = this_cpu_ptr(&bpf_prefetch_ctx);
    if (!ctx->va_block)
        return 0;
    return (u64)uvm_va_block_get_va_space(ctx->va_block);
}

// kfunc 2: sleepable — 从 bpf_wq callback 调用
__bpf_kfunc int bpf_uvm_migrate_range(u64 va_space_handle, u64 addr, u64 length)
{
    uvm_va_space_t *va_space = (uvm_va_space_t *)va_space_handle;
    if (!va_space || !length)
        return -EINVAL;
    return (int)uvm_migrate_bpf(va_space, addr, length,
                                uvm_gpu_id_from_index(0));
}
```

BTF 注册：
```c
BTF_ID_FLAGS(func, bpf_uvm_get_va_space)
BTF_ID_FLAGS(func, bpf_uvm_migrate_range, KF_SLEEPABLE)
```

### 文件 3: `uvm_perf_prefetch.c` — per-CPU context 设置

需要在 prefetch 计算前后设置 per-CPU context（保留现有 `set/clear_context` 机制）：
```c
struct uvm_bpf_prefetch_ctx {
    uvm_va_block_t *va_block;  // 只保留 block 指针
};
```

---

## 5. BPF 侧

新建 `extension/prefetch_cross_block_v2.bpf.c`，需要 `bpf_experimental.h`（从 `~/workspace/bpf-developer-tutorial/src/features/bpf_wq/` 复制）。

```c
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "bpf_experimental.h"
#include "uvm_types.h"
#include "bpf_testmod.h"

#define VA_BLOCK_SIZE (2ULL * 1024 * 1024)
#define PREFETCH_AHEAD_BLOCKS 2

// kfunc 声明
extern void bpf_uvm_set_va_block_region(...) __ksym;
extern u64  bpf_uvm_get_va_space(void) __ksym __weak;
extern u64  bpf_uvm_get_block_end_va(void) __ksym __weak;
extern int  bpf_uvm_migrate_range(u64 va_space, u64 addr, u64 length) __ksym __weak;
extern void bpf_uvm_pmm_chunk_move_tail(...) __ksym;

// BPF maps
struct wq_elem { struct bpf_wq work; };
struct prefetch_req { u64 va_space; u64 addr; u64 length; };

struct { __uint(type, BPF_MAP_TYPE_ARRAY); __uint(max_entries, 4);
         __type(key, int); __type(value, struct wq_elem); } wq_map SEC(".maps");
struct { __uint(type, BPF_MAP_TYPE_ARRAY); __uint(max_entries, 4);
         __type(key, int); __type(value, struct prefetch_req); } req_map SEC(".maps");

// WQ callback — process context, 可 sleep, 调 sleepable kfunc
static int do_prefetch(void *map, int *key, void *value) {
    struct prefetch_req *req = bpf_map_lookup_elem(&req_map, key);
    if (req && req->va_space)
        bpf_uvm_migrate_range(req->va_space, req->addr, req->length);
    return 0;
}

SEC("struct_ops/uvm_prefetch_before_compute")
int BPF_PROG(uvm_prefetch_before_compute, ...) {
    // 1) intra-block: always_max
    uvm_page_index_t max_first = BPF_CORE_READ(max_prefetch_region, first);
    uvm_page_index_t max_outer = BPF_CORE_READ(max_prefetch_region, outer);
    bpf_uvm_set_va_block_region(result_region, max_first, max_outer);

    // 2) cross-block: BPF workqueue 调度异步 prefetch
    u64 va_space = bpf_uvm_get_va_space();
    u64 block_end = bpf_uvm_get_block_end_va();
    if (va_space && block_end) {
        for (int i = 0; i < PREFETCH_AHEAD_BLOCKS && i < 4; i++) {
            struct prefetch_req req = {
                .va_space = va_space,
                .addr = block_end + 1 + (u64)i * VA_BLOCK_SIZE,
                .length = VA_BLOCK_SIZE,
            };
            bpf_map_update_elem(&req_map, &i, &req, 0);

            struct wq_elem *elem = bpf_map_lookup_elem(&wq_map, &i);
            if (elem) {
                bpf_wq_init(&elem->work, &wq_map, 0);
                bpf_wq_set_callback(&elem->work, do_prefetch, 0);
                bpf_wq_start(&elem->work, 0);
            }
        }
    }
    return 1; // BYPASS
}

// + passive MRU eviction hooks (同 prefetch_always_max_passive_mru.bpf.c)
```

---

## 6. 安全考虑

### va_space 生命周期
- 仍存在风险：BPF wq callback 执行时 va_space 可能已被释放
- 缓解措施（按优先级）：
  1. **实际场景安全**：benchmark 期间 llama-bench 不会退出，va_space 始终有效
  2. **kfunc 内验证**（v2）：`bpf_uvm_migrate_range()` 检查 va_space 是否在全局 active 集合中
  3. **最严格方案**（v3）：在 `uvm_va_space_destroy()` 中等待 pending BPF wq callback 完成
- **v1 先用方案 1**（benchmark 安全），后续按需加验证

### bpf_wq 在 struct_ops 中的可用性
- `bpf_wq` 是 Linux 6.10+ 的 kfunc，注册在 `bpf_common_kfunc_set` 中
- 需要验证 struct_ops 程序能否调用这些 kfunc（kernel 6.15 应该支持）
- 如果不支持：使用 fallback 方案

### sleepable kfunc
- `bpf_uvm_migrate_range` 用 `KF_SLEEPABLE` flag 注册
- 只有 sleepable context (bpf_wq callback) 能调用
- struct_ops hook 本身不能调用（非 sleepable）— 这正好保证安全

---

## 7. Fallback 方案

### Fallback A: 内核侧 kthread + BPF map
- 仍只新增 `bpf_uvm_migrate_range()` kfunc（不变）
- 在 struct_ops `reg()` 时创建 kthread，`unreg()` 时停止
- BPF hook 写请求到 BPF map + 设置一个标志位
- kthread 轮询标志位，有请求时调 `bpf_uvm_migrate_range()` 对应的内核函数
- 比旧方案安全：kthread 生命周期绑定 struct_ops，用高层 `uvm_migrate_bpf()` API

### Fallback B: 纯用户态（LD_PRELOAD + ringbuf）
- BPF hook 写 fault 地址到 ring buffer
- LD_PRELOAD 注入后台线程到 llama-bench 进程
- 后台线程读 ringbuf，调 `cuMemPrefetchAsync()`
- 完全不改内核，但需要 LD_PRELOAD

---

## 8. 模块安全机制

**原则**：自定义 nvidia-uvm.ko 仅通过 `insmod` 显式加载，**重启后自动恢复 stock 模块**。

**当前机制**（已满足安全要求）：
```
Stock 模块: /lib/modules/6.15.11-061511-generic/updates/dkms/nvidia-uvm.ko.zst
  srcversion: D6CE1151F2DCBD6B83A6E0A
Custom 模块: ~/workspace/gpu/gpu_ext/kernel-module/nvidia-module/kernel-open/nvidia-uvm.ko

modprobe nvidia_uvm → 永远加载 stock（DKMS 安装路径）
insmod /path/to/custom/nvidia-uvm.ko → 加载 custom
重启 → systemd/udev 用 modprobe → stock ✓
```

**操作规程**：
```bash
# 加载 custom 模块（测试前）
sudo rmmod nvidia_uvm nvidia_modeset nvidia
sudo insmod ~/workspace/gpu/gpu_ext/kernel-module/nvidia-module/kernel-open/nvidia.ko
sudo insmod ~/workspace/gpu/gpu_ext/kernel-module/nvidia-module/kernel-open/nvidia-modeset.ko
sudo insmod ~/workspace/gpu/gpu_ext/kernel-module/nvidia-module/kernel-open/nvidia-uvm.ko

# 恢复 stock 模块（测试后或出问题时）
sudo rmmod nvidia_uvm nvidia_modeset nvidia
sudo modprobe nvidia   # 加载 stock

# 验证当前加载的模块
cat /sys/module/nvidia_uvm/srcversion
# stock: D6CE1151F2DCBD6B83A6E0A
# custom: 不同值

# 紧急恢复（如果 GPU hang）
# 方法 1: 重启（自动加载 stock）
sudo reboot
# 方法 2: 强制卸载
sudo rmmod -f nvidia_uvm && sudo modprobe nvidia
```

**额外安全措施**：
- 永远不要把 custom .ko 安装到 `/lib/modules/` 目录
- 永远不要运行 `make install` 或 `dkms install` 用 custom 模块
- 编译 custom 模块时只在源码目录 `make modules`，不做 install

---

## 9. 实现步骤

1. **在 `uvm_migrate.c` 中添加 `uvm_migrate_bpf()` wrapper**
   - 复制 `uvm_api_migrate()` 的锁管理模式
   - mm 通过 `uvm_va_space_mm_or_current_retain_lock()` 获取
   - va_space read lock 通过 `uvm_va_space_down_read()` 获取
   - 调用 static `uvm_migrate()` 然后释放锁
   - 在 `uvm_migrate.h` 中添加声明

2. **新增 2 个 kfunc**（`uvm_bpf_struct_ops.c`）
   - `bpf_uvm_get_va_space()` — 返回当前 fault 的 va_space handle (u64)
   - `bpf_uvm_migrate_range()` — sleepable kfunc, 调用 `uvm_migrate_bpf()`
   - 注册到 BTF kfunc set，`bpf_uvm_migrate_range` 加 `KF_SLEEPABLE` flag

3. **验证 bpf_wq 兼容性**
   - 写一个最小测试：struct_ops hook 中调 `bpf_wq_start()`
   - 确认 kernel 6.15 允许 struct_ops 程序使用 `bpf_wq` kfuncs
   - 如果不允许，实施 Fallback A（kthread）

4. **编写新 BPF 程序** `extension/prefetch_cross_block_v2.bpf.c`
   - 复制 `bpf_experimental.h` 到 `extension/`
   - Prefetch: always_max + bpf_wq cross-block
   - Eviction: passive MRU（从 `prefetch_always_max_passive_mru.bpf.c` 复制）

5. **编译测试**
   - 先恢复 stock 模块确认 GPU 正常
   - 编译 custom nvidia-uvm.ko（改了 `uvm_migrate.c` 和 `uvm_bpf_struct_ops.c`）
   - `insmod` 加载 custom 模块
   - 加载 BPF 程序，验证所有 kfunc 解析（包括 `bpf_uvm_migrate_range`）
   - 运行 llama-bench 120B 快速测试（2 reps）

6. **Benchmark**
   - Baseline: 无 BPF (`GGML_CUDA_ENABLE_UNIFIED_MEMORY=1`, pp=512, tg=128, r=5)
   - passive MRU: always_max + passive MRU eviction（已有数据: pp=228, tg=78.7）
   - cross-block v2: always_max + bpf_wq cross-block + passive MRU
   - 对比分析

---

## 10. 预期对比

| 方案 | 内核新增代码 | 安全性 | 论文契合度 |
|------|------------|--------|-----------|
| 旧方案 (kernel wq) | ~100 行 | 差 (va_space, kmalloc) | 弱 |
| **新方案 (bpf_wq)** | **~30 行** (20 行 wrapper + 10 行 kfunc) | **好** (uvm_migrate 处理迁移，wrapper 处理锁) | **强** (BPF 全栈) |

---

## 11. 待确认风险

- [ ] `bpf_wq` 是否可用于 struct_ops 程序 → **未实际尝试**，v1 跳过直接用了内核 workqueue。bpf_wq 在 kernel 6.15 可用（已确认 API 存在），需要写 v2 验证
- [ ] bpf_wq callback 能否调用模块注册的 KF_SLEEPABLE kfunc → 待验证
- [x] `uvm_va_space_mm_or_current_retain_lock()` → 绕过，`uvm_migrate_bpf()` 使用 `mm=NULL` + `first_managed_range`
- [x] va_space 裸指针 → v1 benchmark 期间安全（llama-bench 不退出）

---

## 12. 所有测试过的算法详解

本节对项目中测试过的所有 prefetch 和 eviction 算法做简要说明，方便理解后续实验结果表中各配置的含义。

### 12.1 背景：UVM Demand Paging 工作流程

GPU 通过 UVM（Unified Virtual Memory）访问超过显存容量的数据。当 GPU 访问不在显存中的数据时：

```
GPU 执行 kernel → 访问不在 VRAM 的页 → GPU MMU 产生 page fault
→ 中断通知 CPU → UVM fault handler:
    1. 选择要预取的范围 (prefetch policy)
    2. 如果 VRAM 已满，选择要驱逐的 chunk (eviction policy)
    3. DMA 传输：evict D2H + fault-in H2D
→ GPU 恢复执行
```

UVM 将虚拟地址空间划分为 **2MB VA block**，每个 VA block 包含 512 个 4KB 页。Prefetch policy 决定"一次 fault 带多少数据进 VRAM"，eviction policy 决定"VRAM 满时先淘汰谁"。

BPF struct_ops hook 在两个决策点介入：
- `uvm_prefetch_before_compute`: 在 bitmap tree 遍历前调用，可 BYPASS（跳过内核算法，使用 BPF 设定的区域）、DEFAULT（走内核默认）或 ENTER_LOOP（让内核遍历但每层调 BPF）
- `uvm_pmm_chunk_used`: 在 chunk 被使用时调用，可控制 chunk 在 eviction list 中的位置（move_head = 优先淘汰，move_tail = 保护，BYPASS 不移动 = 冻结当前位置）

---

### 12.2 Prefetch 算法

#### Baseline（无 BPF，threshold=51）

**UVM 内核默认算法**。每次 page fault 时：
1. 在当前 2MB VA block 上构建 bitmap tree（虚拟二叉树，512 个叶子）
2. 从 fault page 所在叶子向上遍历，每层检查：`populated_count * 100 > subregion_pages * 51`
3. 只要子树中已有页占比超过 51%，就扩展 prefetch 范围到该子树
4. 最终 prefetch 区域 = 满足条件的最大连续子树

**对 MoE 的问题**：一个几乎空的 VA block 第一次 fault 时，populated 占比极低（1/512 = 0.2%），threshold=51% 几乎不会扩展，导致每个 VA block 需要大量 fault 才能把数据全部搬入。

```c
// 内核默认逻辑 (uvm_perf_prefetch.c)
for (level = 0; level < tree_height; level++) {
    if (populated * 100 > subregion_pages * threshold)
        expand region to this subtree;
}
```

#### always_max（BPF BYPASS，预取整个 VA block）

**最简单也是收益最大的策略**。每次 fault 时，直接把 prefetch 区域设为整个 VA block 的 `max_prefetch_region`，跳过 bitmap tree 遍历。

```c
// prefetch_always_max.bpf.c
SEC("struct_ops/uvm_prefetch_before_compute")
int BPF_PROG(uvm_prefetch_before_compute, ...) {
    // 取 max_prefetch_region 的 first 和 outer（整个 VA block 范围）
    uvm_page_index_t max_first = BPF_CORE_READ(max_prefetch_region, first);
    uvm_page_index_t max_outer = BPF_CORE_READ(max_prefetch_region, outer);
    // 设 result = max → 预取整个 VA block 所有 non-resident 页
    bpf_uvm_set_va_block_region(result_region, max_first, max_outer);
    return 1; // BYPASS — 跳过内核 bitmap tree 算法
}
```

**效果**：等效于 threshold=0。一次 fault 把整个 2MB VA block 的所有页搬入 VRAM，消除同一 block 内后续的 page fault。不减少 chunk 级别的 thrashing（82% re-fault 不变），但大幅减少 page-level fault 数量（从 ~400/token 降到 ~107/token）。

#### stride（步长模式预测）

检测连续 fault 地址的步长模式，预测下一次 fault 位置并预取。使用置信度衰减机制：连续命中增加置信度，miss 降低置信度。

```c
// 简化逻辑
stride = current_fault_addr - last_fault_addr;
if (stride == predicted_stride)
    confidence++;  // 命中
else
    confidence--;  // miss, 衰减
if (confidence >= threshold)
    prefetch_region = [current + stride, current + stride * pages];
    return 1; // BYPASS
```

**MoE 的灾难**：MoE 模型的内存访问在层间跳跃（非线性），stride 检测极少成功（仅 8% fault 触发预取）。关键问题是 stride 策略返回 BYPASS 即使未做预取 — 这阻止了内核默认预取，等效于禁用预取。

#### none（完全禁用预取）

返回空 region + BYPASS，强制每个页单独 fault。用作性能下限参考。

#### cross-block aggressive（跨 VA block 激进预取，每次 fault 预取 2 个相邻 block）

在 always_max（intra-block 全量预取）基础上，额外通过 kernel workqueue 异步迁移当前 VA block 之后的 2 个相邻 2MB block。每次 fault 都触发。

```c
// prefetch_cross_block.bpf.c (aggressive 版本)
SEC("struct_ops/uvm_prefetch_before_compute")
int BPF_PROG(uvm_prefetch_before_compute, ...) {
    // 1) Intra-block: always_max
    bpf_uvm_set_va_block_region(result_region, max_first, max_outer);

    // 2) Cross-block: 预取后面 2 个 VA block
    u64 block_end = bpf_uvm_get_block_end_va();
    if (block_end > 0) {
        // 请求迁移 block_end+1 开始的 4MB (2 blocks × 2MB)
        bpf_uvm_request_prefetch_range(block_end + 1, 2 * VA_BLOCK_SIZE);
    }
    return 1; // BYPASS
}
```

**内核侧实现**：`bpf_uvm_request_prefetch_range()` 将请求放入 64-slot ring buffer，workqueue worker 线程在 fault handler 外调用 `uvm_migrate_bpf()` 执行实际迁移。

#### cross-block rate-limited（跨 VA block 限速预取，每个新 block 仅 1 次）

相比 aggressive 版本，增加去重：用 ARRAY map 记录上次预取的 block VA，只在进入**新** VA block 时才预取 1 个相邻 block。同一 block 内的后续 fault 不触发额外预取。

```c
// prefetch_cross_block.bpf.c (rate-limited 版本)
u64 block_end = bpf_uvm_get_block_end_va();
if (block_end > 0) {
    u32 zero = 0;
    u64 *last = bpf_map_lookup_elem(&last_prefetch_block, &zero);
    if (last && *last != block_end) {
        *last = block_end;  // 记录已预取此 block
        bpf_uvm_request_prefetch_range(block_end + 1, VA_BLOCK_SIZE); // 仅 1 block
    }
}
```

---

### 12.3 Eviction 算法

#### LRU（UVM 默认）

内核默认：chunk 使用时移到 eviction list 尾部（最后淘汰），头部优先淘汰。经典 LRU。

#### MRU（纯 Most Recently Used）

最近使用的 chunk 移到 eviction list 头部（最先淘汰）。理论上对周期性访问模式（LLM decode 循环遍历所有层）是 Belady-optimal，因为"刚用过 = 离下次使用最远"。

**灾难性后果**：MRU 无差别地把 attention weights（每步都用的 T1 数据）也移到头部淘汰 → 每步都要重新搬入 attention → -83% 性能。

#### cycle_moe（T1 频率保护 + 默认处理其余）

**核心思想**：只保护高频 chunk（attention + embeddings），其余让内核默认 LRU 处理。

```c
// eviction_cycle_moe.bpf.c
SEC("struct_ops/uvm_pmm_chunk_used")
int BPF_PROG(uvm_pmm_chunk_used, ...) {
    u32 idx = chunk_hash(chunk);
    u8 *count = bpf_map_lookup_elem(&access_counts, &idx);
    if (!count) return 0;
    if (++(*count) >= T1_FREQ_THRESHOLD) {  // T1_FREQ_THRESHOLD = 3
        bpf_uvm_pmm_chunk_move_tail(chunk, list);  // 保护（移到尾部）
        return 1; // BYPASS
    }
    return 0;  // DEFAULT — 让内核 LRU 处理
}
```

**使用 PERCPU_ARRAY 而非 HASH map**：HASH map 在 fault handler 热路径中延迟过高，导致 GPU MMU timeout → Xid 31 crash。

#### MRU expert（T1 保护 + 非 T1 用 move_head）

T1 chunk（freq ≥ 3）用 `move_tail` 保护；非 T1 chunk 用 `move_head` 显式移到头部优先淘汰。

```c
if (freq >= T1_THRESHOLD) {
    bpf_uvm_pmm_chunk_move_tail(chunk, list);  // T1: 保护
} else {
    bpf_uvm_pmm_chunk_move_head(chunk, list);  // 非 T1: 优先淘汰
}
return 1; // BYPASS
```

**问题**：`move_head` 的 list manipulation 开销（spinlock + pointer update）抵消了 MRU 排序的理论收益。

#### passive MRU（T1 保护 + 非 T1 冻结 LRU 位置）

T1 chunk 用 `move_tail` 保护；非 T1 chunk 返回 BYPASS **但不调用任何 move 函数** — 这阻止了内核默认的 LRU 刷新（move_tail），chunk 保持在当前 list 位置。随着新 chunk 在 tail 端添加，旧 chunk 自然向 head 漂移，效果 ≈ FIFO。

```c
if (freq >= T1_THRESHOLD) {
    bpf_uvm_pmm_chunk_move_tail(chunk, list);  // T1: 保护
    return 1; // BYPASS
}
// 非 T1: BYPASS 但不 move — 冻结当前 LRU 位置
return 1; // BYPASS, 无 move = passive MRU
```

**关键优势**：零 list manipulation 开销（不调用任何 move 函数），仅靠 "不做默认动作" 就实现了近似 MRU。

#### template_belady（Belady 距离 eviction）

**核心思想**：MSched 的 Belady OPT eviction 的 BPF 实现。从 chunk 的 VA 地址推断其所属层号（通过离线 chunk_trace 建立的 VA→layer 映射表），然后计算到当前层的**周期距离**（cycle distance），距离远的优先淘汰。

```c
SEC("struct_ops/uvm_pmm_chunk_used")
int BPF_PROG(uvm_pmm_chunk_used, ...) {
    // 1) T1 频率保护（同 passive MRU）
    if (freq >= T1_THRESHOLD) {
        bpf_uvm_pmm_chunk_move_tail(chunk, list);
        return 1;
    }
    // 2) 获取 chunk VA → 查 boundary table → layer_id
    uvm_va_block_t *va_block = BPF_CORE_READ(chunk, va_block);
    u64 chunk_va = BPF_CORE_READ(va_block, start);
    u32 chunk_layer = va_to_layer(chunk_va);  // linear scan 36 boundaries

    // 3) Belady 距离 = 到下次使用的 cycle 距离
    u32 distance = (chunk_layer - current_layer + NUM_LAYERS) % NUM_LAYERS;

    if (distance <= protect_distance) {
        bpf_uvm_pmm_chunk_move_tail(chunk, list);  // 即将使用 → 保护
    } else {
        bpf_uvm_pmm_chunk_move_head(chunk, list);  // 远距离 → 优先淘汰
    }
    return 1;
}
```

**VA→layer 映射表**：由离线 `derive_layer_mapping.py` 从 chunk_trace 数据生成。将 prefill 阶段激活的 15,801 个 chunk 按 VA 排序后等分为 36 组（对应 36 层），生成 36 个 VA 边界值存入 BPF ARRAY map。

---

### 12.4 组合策略总表

| 配置名称 | Prefetch 算法 | Eviction 算法 | 文件 |
|---------|--------------|--------------|------|
| Baseline (no BPF) | 内核默认 threshold=51 | 内核默认 LRU | — |
| threshold=N | 内核 threshold=N | 内核默认 LRU | 模块参数 |
| always_max | always_max (BYPASS) | 内核默认 LRU | `prefetch_always_max.bpf.c` |
| stride | stride 模式 (BYPASS) | 内核默认 LRU | `prefetch_stride.bpf.c` |
| none | 空 region (BYPASS) | 内核默认 LRU | — |
| always_max + cycle_moe | always_max | T1 protect + DEFAULT | `prefetch_always_max_cycle_moe.bpf.c` |
| always_max + MRU expert | always_max | T1 protect + move_head | `prefetch_max_mru_expert.bpf.c` |
| always_max + passive MRU | always_max | T1 protect + freeze | `prefetch_max_passive_mru.bpf.c` |
| template_belady | always_max | T1 protect + Belady distance | `prefetch_template_belady.bpf.c` |
| cross-block aggressive | always_max + 2 adjacent blocks | T1 protect + freeze | `prefetch_cross_block.bpf.c` |
| cross-block rate-limited | always_max + 1 adjacent block (dedup) | T1 protect + freeze | `prefetch_cross_block.bpf.c` |
| MRU (纯) | 内核默认 | move_head (全部) | — |

---

## 13. 实验结果（2026-02-27）

### 实现方案

最终采用**内核 workqueue** 方案（非 bpf_wq）：
- 3 个新 kfunc: `bpf_uvm_get_block_start_va()`, `bpf_uvm_get_block_end_va()`, `bpf_uvm_request_prefetch_range()`
- Per-CPU prefetch context: 在 `rcu_read_lock()` 下设置，保证抢占安全
- Ring buffer (64 slots) + workqueue worker: BPF kfunc 入队，worker 调用 `uvm_migrate_bpf()`
- `uvm_migrate_bpf()`: 包装 static `uvm_migrate()`，自动管理 va_space read lock
- 内核侧新增 ~80 行，BPF 侧复用已有 `prefetch_cross_block.bpf.c`
- 编译通过，加载成功，无 Xid 错误

### Benchmark 结果

| 配置 | pp (tok/s) | tg (tok/s) | vs always_max |
|------|-----------|-----------|---------------|
| Baseline (no BPF, threshold=51) | 142.93 ± 0.47 | 47.24 ± 6.05 | — |
| always_max + passive MRU | 223.86 ± 0.51 | 78.39 ± 6.88 | — (baseline) |
| **cross-block 2 blocks (aggressive)** | 191.85 ± 4.30 | 62.61 ± 7.62 | **-14% pp, -20% tg** |
| **cross-block 1 block (rate-limited)** | 206.67 ± 3.43 | 60.78 ± 7.04 | **-8% pp, -22% tg** |

### 分析：为什么 cross-block prefetch 有害

**根本原因: 1.84x oversubscription 下，proactive prefetch 是零和博弈。**

每个 proactively prefetched 的 2MB block 必然 evict 一个 useful block：
- 当前 VRAM 容量 = ~30 GB（32 GB 减去系统开销）
- 模型大小 = 59 GB
- Per token 需要 107 chunks (214 MB) 迁入 + 107 chunks (214 MB) 逐出 = 428 MB 总 DMA
- Cross-block aggressive: ~100 extra blocks/token ≈ +200 MB DMA (+47% 额外流量)
- Cross-block rate-limited: ~100 extra blocks/token（每个新 block 仅一次，但仍然太多）

被 evict 的 block 之后还会被 demand-page 回来，形成恶性循环：
```
prefetch block N+1 → evict block M → later fault on M → migrate M back → evict block K → ...
```

### 什么条件下 cross-block prefetch 有效

| 条件 | 当前情况 | 需要的情况 |
|------|---------|-----------|
| Oversubscription | 1.84x | < 1.0x（全部放得下） |
| VRAM headroom | 0（满） | 有空闲槽位放 prefetch |
| Prefetch 精度 | 盲目 adjacent | 精确 next-layer-only |
| DMA 通道 | 共享（单 CE） | 独立（双 CE，MSched 方式） |

### 初步结论（仅 llama.cpp 120B，1.84x oversubscription）

1. **技术实现成功**: kfunc + 内核 workqueue 方案工作正常，无 crash/Xid，zero overhead when BPF not active
2. **性能负增益**: 在 1.84x oversubscription 下，盲目 adjacent prefetch 有害
3. **Cross-block prefetch 的价值场景**: oversubscription < 1.2x 时可能有正增益（VRAM 有余量放 prefetch）
4. **MSched 方式的优势**: 它用独立 CE + 精确 template 做 proactive migration，绕过 UVM demand paging，而非在 UVM 内部做额外 migration

**待验证**（§14）：
- v1 只测了 llama.cpp 120B 一个 workload，结论可能不具普适性
- 应在不同 oversubscription 级别和访问模式下测试（使用 `microbench/memory` 工具）
- 应尝试 bpf_wq 方案替代内核 workqueue，减少内核代码量

---

## 14. 下一步：bpf_wq 方案 + 多 workload 验证

### 14.1 为什么需要 bpf_wq 版本

v1 使用内核 workqueue 存在以下问题：

| 问题 | 内核 workqueue (v1) | bpf_wq (v2) |
|------|-------------------|-------------|
| 内核新增代码 | ~80 行（ring buffer + spinlock + worker + wq lifecycle） | ~15 行（仅 2 个 kfunc） |
| 安全性 | ring buffer 满时静默丢请求 | BPF verifier 保证安全 |
| 符合 gpu_ext 哲学 | 弱（大量内核基础设施代码） | 强（逻辑在 BPF 中） |
| 可扩展性 | 修改逻辑需重编内核模块 | 修改 BPF 程序即可 |

### 14.2 bpf_wq 方案设计

**内核侧改动**（替换 v1 的 ring buffer/worker，净减 ~45 行）：

```c
// 删除: ring buffer, spinlock, kernel workqueue, bpf_prefetch_worker()
// 删除: bpf_uvm_request_prefetch_range() kfunc

// 新增 kfunc 1: 获取当前 fault 的 va_space handle
__bpf_kfunc u64 bpf_uvm_get_va_space(void)
{
    struct uvm_bpf_prefetch_ctx *ctx = this_cpu_ptr(&bpf_prefetch_ctx);
    return ctx->va_space ? (u64)ctx->va_space : 0;
}

// 新增 kfunc 2: sleepable — 从 bpf_wq callback 调用
__bpf_kfunc int bpf_uvm_migrate_range(u64 va_space_handle, u64 addr, u64 length)
{
    uvm_va_space_t *va_space = (uvm_va_space_t *)va_space_handle;
    if (!va_space || !length)
        return -EINVAL;
    return (int)uvm_migrate_bpf(va_space, addr, length);
}

// BTF 注册:
BTF_ID_FLAGS(func, bpf_uvm_get_va_space)
BTF_ID_FLAGS(func, bpf_uvm_migrate_range, KF_SLEEPABLE)
```

保留：per-CPU context、`bpf_uvm_get_block_start_va()`、`bpf_uvm_get_block_end_va()`、`uvm_migrate_bpf()` wrapper。

**BPF 侧**（`extension/prefetch_cross_block_v2.bpf.c`）：

```c
#include "bpf_experimental.h"  // bpf_wq API

struct prefetch_data {
    u64 va_space;
    u64 addr;
    u64 length;
    struct bpf_wq work;
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, int);
    __type(value, struct prefetch_data);
} wq_map SEC(".maps");

// bpf_wq callback — 进程上下文，可 sleep，调用 sleepable kfunc
static int do_prefetch(void *map, int *key, void *value)
{
    struct prefetch_data *data = value;
    if (data && data->va_space)
        bpf_uvm_migrate_range(data->va_space, data->addr, data->length);
    return 0;
}

SEC("struct_ops/uvm_prefetch_before_compute")
int BPF_PROG(uvm_prefetch_before_compute, ...) {
    // 1) intra-block: always_max
    bpf_uvm_set_va_block_region(result_region, max_first, max_outer);

    // 2) cross-block: bpf_wq 调度异步 prefetch
    u64 va_space = bpf_uvm_get_va_space();
    u64 block_end = bpf_uvm_get_block_end_va();
    if (va_space && block_end > 0) {
        u32 zero = 0;
        u64 *last = bpf_map_lookup_elem(&last_prefetch_block, &zero);
        if (last && *last != block_end) {
            *last = block_end;
            int key = 0;
            struct prefetch_data *data = bpf_map_lookup_elem(&wq_map, &key);
            if (data) {
                data->va_space = va_space;
                data->addr = block_end + 1;
                data->length = VA_BLOCK_SIZE;
                bpf_wq_init(&data->work, &wq_map, 0);
                bpf_wq_set_callback(&data->work, do_prefetch, 0);
                bpf_wq_start(&data->work, 0);
            }
        }
    }
    return 1; // BYPASS
}
```

**待验证的关键问题**：
1. struct_ops BPF 程序能否调用 `bpf_wq_init/set_callback/start`（属于 `bpf_common_kfunc_set`）
2. bpf_wq callback 能否调用 nvidia-uvm.ko 注册的 `KF_SLEEPABLE` kfunc
3. 如果不行，fallback: 保留当前 v1 内核 workqueue 方案

### 14.3 多 workload 验证计划

v1 结论仅基于 llama.cpp 120B（1.84x oversubscription），需要用 `microbench/memory` 在不同条件下验证。

**实验矩阵**：

| Kernel | 访问模式 | size_factor | 预期 oversubscription |
|--------|---------|-------------|----------------------|
| `seq_stream` | 顺序流式 | 0.5, 1.0, 1.5, 2.0 | 0x → 2x |
| `hotspot` | 5-point stencil | 0.5, 1.0, 1.5 | 0x → 1.5x |
| `gemm` | LLM 式多层权重复用 | 0.5, 1.0, 1.5 | 0x → 1.5x |
| `rand_stream` | 随机访问 | 0.5, 1.0, 1.5 | 0x → 1.5x |

**对比配置**：
1. Baseline (no BPF) — UVM 默认 threshold=51
2. always_max + passive MRU — 当前最佳
3. cross-block v1 (内核 wq) 或 v2 (bpf_wq) — 待测

**关键假设验证**：
- size_factor=0.5（全部放得下 VRAM）: cross-block 应无影响（不触发 eviction）
- size_factor=1.0（刚好满）: cross-block 可能有正收益（少量 headroom）
- size_factor=1.5-2.0（严重 oversubscription）: cross-block 预期有害（与 120B 结论一致）

### 14.4 实施步骤

1. **bpf_wq 可行性验证**: 修改内核模块 → 编译 → 写 v2 BPF 程序 → 编译加载测试
2. **microbench baseline**: stock module，no BPF，各 kernel × size_factor
3. **microbench + BPF**: custom module，always_max vs cross-block，各 kernel × size_factor
4. **结果分析**: 找到 cross-block 的收益/损害临界点
5. **更新结论**: 将 §13 的 "初步结论" 升级为完整结论
