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

- [ ] `bpf_wq` 是否可用于 struct_ops 程序（kernel 6.15）
- [ ] sleepable kfunc (`KF_SLEEPABLE`) 是否可从 bpf_wq callback 调用
- [ ] `uvm_va_space_mm_or_current_retain_lock()` 从 kworker 线程调用是否安全（current->mm 为 NULL）
- [ ] va_space 裸指针传递的安全性（v1 接受风险，v2 加验证）
