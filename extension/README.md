# GPU Memory Management BPF Policies

Pages: 4kb
VA block 2MB 512 Pages
Chunks 2MB



这个目录包含用于NVIDIA UVM (Unified Virtual Memory) 的BPF struct_ops策略实现，用于优化GPU内存管理。


timeout 30 sudo /home/yunwei37/workspace/gpu/co-processor-demo/gpu_ext_policy/src/chunk_trace > /tmp/test_trace.csv

python /home/yunwei37/workspace/gpu/co-processor-demo/gpu_ext_policy/scripts/visualize_eviction.py /tmp/test_trace.csv


## 📋 目录

- [系统架构](#系统架构)
- [Chunk状态转换](#chunk状态转换)
- [可用的Hook点](#可用的hook点)
- [Eviction策略](#eviction策略)
- [Prefetch策略](#prefetch策略)
- [如何使用](#如何使用)
- [Trace工具](#trace工具)
  - [chunk_trace](#chunk_trace---chunk生命周期追踪)
  - [prefetch_trace](#prefetch_trace---prefetch决策追踪)

---

## 系统架构

### 核心概念

#### 1. **物理Chunk (Physical Chunk)**
- GPU内存管理的基本单位（通常64KB）
- `chunk_addr` - 物理内存块地址
- **Eviction policy操作的对象**

#### 2. **VA Block (Virtual Address Block)**
- 代表虚拟地址范围 `[va_start, va_end)`
- 一个VA block通常映射到多个物理chunks
- `va_block_page_index` - chunk在VA block内的页索引

#### 3. **映射关系**
```
Virtual Address Space              Physical Memory (GPU VRAM)
┌─────────────────────┐           ┌──────────────┐
│   VA Block 1        │           │  Chunk A     │
│   [va_start, va_end]│────┬─────→│  (64KB)      │
│   2MB                │    │      │  [ACTIVE]    │
└─────────────────────┘    │      └──────────────┘
                           │      ┌──────────────┐
┌─────────────────────┐    └─────→│  Chunk B     │
│   VA Block 2        │           │  (64KB)      │
│   2MB                │──────────→│  [ACTIVE]    │
└─────────────────────┘           └──────────────┘
                                  ┌──────────────┐
     (unmapped)                   │  Chunk C     │
                                  │  (64KB)      │
                                  │  [UNUSED]    │
                                  └──────────────┘
```

**关键特性**（基于实际trace数据）：
- 平均每个VA block使用 **10.8个物理chunks**
- 平均每个chunk在生命周期内被 **16.5个不同VA blocks重用**
- **⚠️ 注意**：这不是"同时共享"，而是**时间上的重用**
  - 同一时刻，一个chunk只映射到**一个VA block**
  - Chunk被evict后，会被分配给新的VA block
  - "16.5个VA/chunk"是整个trace期间的**累积重用次数**

---

## Chunk状态转换

### 状态机图

```
                    ┌──────────────────────────────┐
                    │                              │
                    │     Unused Pool             │
                    │  (Free chunks available)    │
                    │                              │
                    └──────────┬───────────────────┘
                               │
                               │ assign (from kernel)
                               ↓
                    ┌──────────────────────────────┐
                    │                              │
                    │        Active               │
              ┌────→│  (Mapped to VA block)       │────┐
              │     │                              │    │
              │     └──────────────────────────────┘    │
              │                                          │
              │ reuse                            evict   │
              │ (reassign)                       ↓       │
              │                         ┌────────────────┤
              │                         │                │
              │                         │  In-Eviction  │
              │                         │                │
              │                         └────────┬───────┘
              │                                  │
              └──────────────────────────────────┘
                         回到 Unused Pool
```

### 详细说明

#### 状态1: **Unused（未使用）**
- Chunk在free pool中
- 没有映射到任何VA block
- 等待被分配

#### 状态2: **Active（活跃）**
- Chunk被分配给某个VA block
- 处于"evictable"状态，可以被访问或evict
- **这是BPF policy关注的主要状态**

子状态：
- **Activated** - 刚被分配，加入evictable list
  - Hook: `gpu_block_activate`
- **Being Used** - 正在被访问
  - Hook: `gpu_block_access`

#### 状态3: **In-Eviction（驱逐中）**
- 正在从当前VA block解除映射
- 即将回到unused pool
- Hook: `gpu_evict_prepare`

### 完整生命周期流程

```
1. Chunk从unused pool分配给VA Block X
   ↓
2. [gpu_block_activate]
   Chunk激活，加入evictable list（可被evict的候选）
   ↓
3. [gpu_block_access - 可能多次]
   VA Block X访问chunk，policy更新chunk的LRU位置
   ↓
4. [gpu_evict_prepare]
   内存压力触发，需要回收内存
   Policy选择victim chunks（最不值得保留的）
   ↓
5. Chunk从VA Block X解除映射
   Chunk → unused pool
   ↓
6. （稍后）Chunk被重新分配给VA Block Y
   回到步骤1
```

### 关键点

1. **Eviction ≠ 直接重新分配**
   ```
   错误理解：
     Chunk A: VA Block X → [evict] → VA Block Y

   正确理解：
     Chunk A: VA Block X → [evict] → unused pool → [assign] → VA Block Y
                                       ↑
                                    中间状态
   ```

2. **为什么需要unused pool？**
   - **解耦evict和assign**：Eviction policy只负责"谁该被踢出去"
   - **批量操作效率**：可以一次evict多个chunks
   - **内存压力缓冲**：Pool大小影响系统性能

3. **Policy的职责边界**
   - ✅ **Policy负责**：选择哪些chunks被evict（victim selection）
   - ❌ **Policy不负责**：Chunk分配给哪个VA block（由内核决定）

---

## ⚠️ 理解Trace数据的常见误区

### 误区1: "16.5个VA blocks/chunk = 同时共享"

**错误理解**:
```
           VA Block 1 ─┐
           VA Block 2 ─┤
           VA Block 3 ─┼─→ Chunk A (同时被17个VA blocks共享)
               ...     ─┤
           VA Block 17─┘
```

**正确理解**:
```
时刻 T1: VA Block 1  → Chunk A
时刻 T2: VA Block 1 evicted, Chunk A → unused pool
时刻 T3: VA Block 5  → Chunk A (被重新分配)
时刻 T4: VA Block 5 evicted, Chunk A → unused pool
时刻 T5: VA Block 12 → Chunk A (再次重新分配)
...
总计: Chunk A 被 17个不同的VA blocks重用过

结论：16.5是"累积重用次数"，不是"同时引用数"
```

### 误区2: "所有chunks都共享 = 需要保护高引用chunks"

**为什么这个逻辑不成立**:

对于**Sequential streaming workload** (如seq_stream):
- 所有chunks的访问频率都是1次
- 没有"热点"chunks
- 高重用次数只说明chunk被**循环利用**得好
- 保护高重用chunk没有意义，因为：
  - 被evict的chunk已经不会再被当前VA访问
  - 重用次数高 = 在内存中呆的时间久，**应该被evict**

对于**Random with hotspots** workload:
- 少数chunks被频繁访问（真正的热点）
- 这时候保护高频访问的chunks才有意义
- 但这要看**访问频率**，不是**重用次数**

### 如何正确分析Trace数据

#### 1. 看访问模式，不是统计数字

```python
# 错误：只看平均值
avg_reuse = total_va_accesses / unique_chunks  # 16.5

# 正确：看时间序列
for chunk in chunks:
    access_times = get_access_times(chunk)
    if len(access_times) == 1:
        print("One-time use - streaming pattern")
    elif has_temporal_locality(access_times):
        print("Reused - keep in cache")
```

#### 2. 区分"重用"和"共享"

- **重用** (Reuse): 时间维度，同一chunk被不同VA使用
  - `chunk → VA1 → evict → VA2 → evict → VA3`
  - 例子：Sequential streaming，chunk循环利用

- **共享** (Sharing): 空间维度，多个VA同时引用同一chunk
  - `chunk ← VA1, VA2, VA3同时引用`
  - 例子：Shared memory, read-only data

#### 3. 从图表看本质

**Sequential pattern的特征**:
```
VA访问热力图：
时间 →
  0ms    5ms    10ms
┌─────┬─────┬─────┐
│█████│     │     │ ← VA Range 1 (只在开始被访问)
├─────┼─────┼─────┤
│     │█████│     │ ← VA Range 2 (中间被访问)
├─────┼─────┼─────┤
│     │     │█████│ ← VA Range 3 (最后被访问)
└─────┴─────┴─────┘

特点：垂直条纹，无重复
```

**Random with hotspots的特征**:
```
VA访问热力图：
时间 →
  0ms    5ms    10ms
┌─────┬─────┬─────┐
│█████│█████│█████│ ← VA Range 1 (一直被访问 - 热点!)
├─────┼─────┼─────┤
│█    │     │  █  │ ← VA Range 2 (偶尔访问)
├─────┼─────┼─────┤
│  █  │█    │█    │ ← VA Range 3 (偶尔访问)
└─────┴─────┴─────┘

特点：某些VA一直热，有明显的热点行
```

---

## 可用的Hook点

NVIDIA UVM提供6个BPF struct_ops hook点，分为两类：

### 类别A: Eviction相关（内存回收）

#### 1. `gpu_block_activate`
**触发时机**: Chunk被分配给VA block后，进入evictable状态

**参数**:
```c
int gpu_block_activate(
    uvm_pmm_gpu_t *pmm,              // GPU内存管理器
    uvm_gpu_chunk_t *chunk,          // 被激活的chunk
    struct list_head *list           // Evictable list
);
```

**Policy可以做什么**:
- 初始化chunk的元数据（访问时间、频率等）
- 决定chunk在eviction list中的初始位置
- 返回0使用默认行为，返回1 bypass默认

**示例**: LRU默认行为将chunk加到list尾部

#### 2. `gpu_block_access`
**触发时机**: Chunk被访问/使用（最关键的hook）

**参数**:
```c
int gpu_block_access(
    uvm_pmm_gpu_t *pmm,
    uvm_gpu_chunk_t *chunk,          // 被访问的chunk
    struct list_head *list
);
```

**Policy可以做什么**:
- 更新访问时间戳（LRU）
- 增加访问计数器（LFU）
- **调整chunk在list中的位置**（决定eviction优先级）
- 考虑chunk的共享度（被多少VA blocks引用）

**重要性**: ⭐⭐⭐⭐⭐ 这是决定policy效果的关键hook

#### 3. `gpu_evict_prepare`
**触发时机**: 内存压力触发，需要evict内存

**参数**:
```c
int gpu_evict_prepare(
    uvm_pmm_gpu_t *pmm,
    struct list_head *va_block_used,   // Used VA blocks list
    struct list_head *va_block_unused  // Unused VA blocks list
);
```

**Policy可以做什么**:
- 最后调整列表顺序（如果需要）
- 检测内存压力程度
- 动态切换策略（aggressive vs conservative）

**注意**: LRU通常不需要额外操作，因为list已经按访问顺序排列

---

### 类别B: Prefetch相关（预取优化）

Prefetch机制用于在实际访问前将数据从CPU迁移到GPU，减少page fault延迟。

#### 4. `gpu_page_prefetch`
**触发时机**: 在GPU kernel开始计算前，决定要prefetch哪些页面

**参数**:
```c
int gpu_page_prefetch(
    uvm_page_index_t page_index,                    // 触发prefetch的页面索引
    uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,   // Prefetch候选页面树
    uvm_va_block_region_t *max_prefetch_region,     // 最大可prefetch区域
    uvm_va_block_region_t *result_region            // [OUT] 实际prefetch区域
);
```

**返回值**:
- `0` (DEFAULT) - 使用内核默认策略
- `1` (BYPASS) - 使用`result_region`，跳过默认逻辑
- `2` (ENTER_LOOP) - 进入迭代模式，逐个检查`bitmap_tree`

**策略示例**:
- **Always Max**: 直接prefetch整个`max_prefetch_region`
- **None**: 设置`result_region`为空，禁用prefetch
- **Adaptive**: 返回ENTER_LOOP，让`gpu_page_prefetch_iter`决定

#### 5. `gpu_page_prefetch_iter`
**触发时机**: 当`gpu_page_prefetch`返回ENTER_LOOP时，对每个候选区域调用

**参数**:
```c
int gpu_page_prefetch_iter(
    uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
    uvm_va_block_region_t *max_prefetch_region,
    uvm_va_block_region_t *current_region,    // 当前检查的区域
    unsigned int counter,                     // 区域内的访问计数
    uvm_va_block_region_t *prefetch_region    // [OUT] 如果选择此区域
);
```

**返回值**:
- `0` - 不选择此区域
- `1` - 选择此区域进行prefetch（设置`prefetch_region`）

**自适应阈值示例**:
```c
// 只prefetch "热"区域（访问率 > threshold%）
if (counter * 100 > subregion_pages * threshold) {
    bpf_gpu_set_prefetch_region(prefetch_region, first, outer);
    return 1;  // 选择这个区域
}
return 0;  // 跳过这个区域
```

#### 6. `gpu_test_trigger`
**用途**: 测试/调试用，通过proc文件触发

---

## Eviction 策略

UVM 从 list HEAD 开始 evict，TAIL 最安全。BPF 策略通过 `bpf_gpu_block_move_head/tail` 调整 chunk 在 list 中的位置来控制 eviction 优先级。

| 策略 | 文件 | 描述 | 适用场景 |
|------|------|------|---------|
| **LRU** | 内核默认 | 访问时移到 tail，最久未用的在 head 被 evict | 通用（有时间局部性） |
| **FIFO** | `eviction_fifo.bpf.c` | 不移动 chunk，保持插入顺序。bypass 所有 access | 顺序扫描、streaming |
| **FIFO + Second Chance** | `eviction_fifo_chance.bpf.c` | FIFO + PID-aware 二次机会。evict 时减 chance，被访问时如果 chance 被减则移到 tail 保护 | Multi-tenant + 混合访问模式 |
| **MRU** | `eviction_mru.bpf.c` | 访问时移到 head，最近使用的优先 evict | 全扫描负载（scan-resistant） |
| **LFU** | `eviction_lfu.bpf.c` | 按访问频率排序，低频在 head，高频移 tail | 热点数据保护 |
| **LFU + xCoord** | `eviction_lfu_xcoord.bpf.c` | LFU + xCoord 论文的跨 GPU 协调驱逐 | Multi-GPU 场景 |
| **PID Quota** | `eviction_pid_quota.bpf.c` | 按进程配额分配 GPU 内存，超额 chunk 不保护 | Multi-tenant 隔离 |
| **Freq Decay** | `eviction_freq_pid_decay.bpf.c` | PID-aware 频率衰减，频率随时间递减 | 时间局部性 + 租户隔离 |
| **Cycle MoE** | `eviction_cycle_moe.bpf.c` | MoE 模型感知：T1 层（attention/FFN）永久保护，非 T1 层冻结 LRU | MoE LLM inference |

---

## Prefetch 策略

Prefetch 在 page fault 时决定预取当前 VA block (2MB) 内的哪些页面。

| 策略 | 文件 | 描述 | 适用场景 |
|------|------|------|---------|
| **Always Max** | `prefetch_always_max.bpf.c` | 总是预取整个 VA block (512 pages) | 顺序访问、带宽充足 |
| **None** | `prefetch_none.bpf.c` | 禁用预取，纯 demand paging | 随机访问、带宽受限 |
| **Adaptive Sequential** | `prefetch_adaptive_sequential.bpf.c` | 按访问密度阈值决定是否预取子区域 | 动态访问模式 |
| **Adaptive Tree** | `prefetch_adaptive_tree_iter.bpf.c` | 遍历 bitmap tree，按每个子区域的 access count 决定 | 树结构遍历 |
| **Stride** | `prefetch_stride.bpf.c` | 检测固定步长访问模式，预取下一个预测位置 | 固定步长访问（BLAS、矩阵） |
| **PID Tree** | `prefetch_pid_tree.bpf.c` | PID-aware 预取带宽分配（高优先级进程更多预取） | Multi-tenant 预取隔离 |

---

## 组合策略（Prefetch + Eviction）

| 策略 | 文件 | 描述 | 适用场景 |
|------|------|------|---------|
| **PID Prefetch+Eviction** | `prefetch_eviction_pid.bpf.c` | PID-aware 预取阈值 + probabilistic LRU 驱逐，统一参数控制 | Multi-tenant 全面隔离 |
| **Always Max + Cycle MoE** | `prefetch_always_max_cycle_moe.bpf.c` | always_max 预取 + T1-protect 驱逐 | MoE LLM inference (最佳组合) |
| **Max + MRU Expert** | `prefetch_max_mru_expert.bpf.c` | always_max 预取 + expert 感知 MRU 驱逐 | MoE 模型（expert 层分级保护） |
| **Max + Passive MRU** | `prefetch_max_passive_mru.bpf.c` | always_max 预取 + T1 保护 + 非 T1 冻结 LRU | MoE 模型（轻量级） |
| **Template Belady** | `prefetch_template_belady.bpf.c` | always_max 预取 + Belady OPT 驱逐（基于层循环距离） | MoE 模型 + profiling 数据 |
| **Cross-Block v2** | `prefetch_cross_block_v2.bpf.c` | always_max 预取 + 跨 VA block 迁移 (bpf_wq) + passive MRU | 顺序访问 + 低 oversubscription |

### ⚠️ Prefetch的架构限制：只能在当前VA Block内

**重要结论：Prefetch 只能在当前 VA block (2MB) 内操作，无法跨 VA block prefetch。**

这是 NVIDIA UVM 驱动的架构限制，不是 BPF policy 可以改变的。

#### 为什么有这个限制？

从 UVM 源码 (`uvm_perf_prefetch.c`) 可以看到：

```c
// uvm_perf_prefetch_prenotify_fault_migrations() 中：
if (uvm_va_block_is_hmm(va_block)) {
    max_prefetch_region = uvm_hmm_get_prefetch_region(...);
} else {
    // 关键：max_prefetch_region 永远是当前 VA block 的范围
    max_prefetch_region = uvm_va_block_region_from_block(va_block);
}

// uvm_va_block_region_from_block() 定义：
static uvm_va_block_region_t uvm_va_block_region_from_block(uvm_va_block_t *va_block)
{
    return uvm_va_block_region(0, uvm_va_block_num_cpu_pages(va_block));
}
// 返回 (0, 512)，即 512 个 4KB 页 = 2MB
```

#### BPF hook 受到的限制

```c
int gpu_page_prefetch(
    uvm_page_index_t page_index,                    // 0-511 范围
    uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
    uvm_va_block_region_t *max_prefetch_region,     // 永远是 [0, 512)
    uvm_va_block_region_t *result_region            // 只能设置在 [0, 512) 内
);
```

即使 BPF policy 尝试设置超出范围的 `result_region`，内核也会 clamp 到 `max_prefetch_region`：

```c
// compute_prefetch_region() 中：
if (prefetch_region.outer > max_prefetch_region.outer)
    prefetch_region.outer = max_prefetch_region.outer;  // 强制限制
```

#### UVM 源码中的注释说明

```c
// Within a block we only allow prefetching to a single processor. Therefore,
// if two processors are accessing non-overlapping regions within the same
// block they won't benefit from prefetching.
//
// TODO: Bug 1778034: [uvm] Explore prefetching to different processors within
// a VA block.
```

**NVIDIA 自己也知道这个限制，但目前没有实现跨 VA block prefetch。**

#### 跨 VA block prefetch 的实现

已通过 `prefetch_cross_block_v2.bpf.c` 实现：
1. 添加 `bpf_gpu_migrate_range()` kfunc 到内核模块
2. BPF 用 kprobe 捕获 va_block/va_space 上下文
3. 通过 `bpf_wq` 异步调度相邻 VA block 的迁移
4. 仅需 1 个 kfunc（action only），无需 getter kfunc

#### 实际影响

从 trace 数据分析，我们观察到：
- **stride=69 页 (276KB)** 是主要访问模式
- 一个 2MB VA block 内约有 **~7 个访问条纹**
- 当访问跨越 VA block 边界时，需要等待新的 fault 触发新 VA block 的 prefetch

**这意味着对于顺序扫描 workload，每 ~300KB 就可能有一次额外的 fault 延迟。**

---

### Prefetch vs Eviction的关系

```
                    Prefetch                     Eviction
                       ↓                            ↑
         CPU Memory ←───→ GPU Memory (VRAM) ←─────→ Unused Pool
                    (主动迁移)                (被动回收)

Prefetch目标: 提前将CPU内存迁移到GPU，减少未来的page fault
Eviction目标: 在GPU内存不足时，回收最不重要的chunks
```

**协同优化**:
1. 好的**prefetch策略**减少page faults，降低eviction压力
2. 好的**eviction策略**保留重要数据，减少re-fetch需求
3. 两者配合可以显著提升整体性能

---

## 如何使用

### 1. 编译

```bash
cd extension
make        # 编译所有 BPF 策略和工具
```

### 2. 加载 Policy

每个策略编译为独立的 userspace loader 二进制文件，直接运行即可：

```bash
# 加载 prefetch 策略
sudo ./prefetch_always_max

# 加载 eviction 策略
sudo ./eviction_lfu

# 加载组合策略
sudo ./prefetch_always_max_cycle_moe

# 带参数加载（PID-aware 策略）
sudo ./prefetch_pid_tree -p 1234 -P 20 -l 5678 -L 80
```

Loader 会自动清理旧的 struct_ops、加载 BPF 程序、attach struct_ops，Ctrl-C 退出时自动 detach。

### 3. 验证

```bash
# 查看已加载的 struct_ops
sudo bpftool map list | grep struct_ops

# 查看 BPF trace 输出
sudo cat /sys/kernel/debug/tracing/trace_pipe
```

### 4. Trace 工具

```bash
# Chunk 生命周期追踪
sudo timeout 30 ./chunk_trace > /tmp/trace.csv

# Prefetch 决策追踪
sudo timeout 5 ./prefetch_trace > /tmp/prefetch.csv
```

---

## 设计新Policy的步骤

1. **收集数据**
   ```bash
   sudo ./chunk_trace -d 10 -o /tmp/workload_trace.csv
   ```

2. **分析访问模式**
   ```bash
   ./visualize_eviction.py /tmp/workload_trace.csv -o /tmp
   # 查看生成的图表和统计
   ```

3. **选择策略类型**
   - **Sequential streaming** (如seq_stream) → **FIFO** ⭐
   - **Random with hotspots** → LFU (基于访问频率)
   - **Temporal locality** (重复访问) → LRU (默认)
   - **Mixed pattern** → Adaptive (动态切换)

4. **实现BPF程序**
   - 参考 `eviction_fifo.bpf.c`（简单 eviction）或 `prefetch_always_max.bpf.c`（简单 prefetch）
   - 实现关键 hooks（至少 `gpu_block_access`）
   - 添加必要的 BPF maps（统计、配置等）
   - 在 Makefile 的 `BPF_APPS` 中添加新策略名

5. **测试验证**
   ```bash
   make my_policy
   sudo ./my_policy
   # 运行 workload，对比性能
   ```

6. **迭代优化**
   - 根据新的trace数据调整
   - A/B测试不同参数
   - 监控page faults和性能

---

## Trace工具

本目录包含两个基于kprobe的BPF trace工具，用于分析UVM内存管理行为。

### chunk_trace - Chunk生命周期追踪

追踪GPU内存chunk的生命周期事件（activate、used、eviction_prepare），包括VA block信息。

**编译和运行**:
```bash
make chunk_trace
sudo timeout 30 ./chunk_trace > /tmp/trace.csv
```

**输出格式（CSV）**:
```
time_ms,cpu,hook,chunk_addr,list_addr,va_block,va_start,va_end,va_page_index
```

**字段说明**:
| 字段 | 说明 |
|------|------|
| `time_ms` | 从第一个事件起的毫秒时间 |
| `cpu` | 执行hook的CPU核心 |
| `hook` | 事件类型：ACTIVATE/USED/EVICTION_PREPARE |
| `chunk_addr` | 物理chunk地址 |
| `list_addr` | Evictable list地址 |
| `va_block` | VA block指针 |
| `va_start` | VA block起始虚拟地址 |
| `va_end` | VA block结束虚拟地址 |
| `va_page_index` | Chunk在VA block内的页索引 |

**统计信息**（输出到stderr）:
```
================================================================================
CHUNK TRACE SUMMARY
================================================================================
ACTIVATE                    12345
USED                        67890
EVICTION_PREPARE               50
--------------------------------------------------------------------------------
TOTAL                       80285
================================================================================
```

**可视化分析**:
```bash
python /home/yunwei37/workspace/gpu/co-processor-demo/gpu_ext_policy/scripts/visualize_eviction.py /tmp/trace.csv
```

---

### prefetch_trace - Prefetch决策追踪

追踪UVM prefetch计算过程，了解预取决策的输入参数。

**编译和运行**:
```bash
make prefetch_trace
sudo timeout 5 ./prefetch_trace > /tmp/prefetch.csv 2> /tmp/prefetch_stats.txt
```

**输出格式（CSV）**:
```
time_ms,cpu,page_index,max_first,max_outer,tree_offset,leaf_count,level_count,pages_accessed
```

**字段说明**:
| 字段 | 说明 |
|------|------|
| `time_ms` | 从第一个事件起的毫秒时间 |
| `cpu` | 执行hook的CPU核心 |
| `page_index` | 触发prefetch的页面索引（0-511，在VA block内的偏移） |
| `max_first` | 最大prefetch区域的起始页（通常为0） |
| `max_outer` | 最大prefetch区域的结束页（通常为512，表示整个2MB VA block） |
| `tree_offset` | bitmap_tree在VA block内的偏移 |
| `leaf_count` | bitmap_tree的叶子节点数 |
| `level_count` | bitmap_tree的层级深度 |
| `pages_accessed` | 当前VA block中已访问的页面数（bitmap popcount） |

**关键概念**:

1. **page_index与max_region的关系**:
   - `max_region = {first:0, outer:512}` 表示整个2MB VA block（512个4KB页）
   - `page_index` 是触发prefetch计算的fault页面
   - Prefetch算法以`page_index`为中心，在`max_region`范围内计算预取区域

2. **虚拟地址计算**:
   - 实际虚拟地址 = `va_block->start + page_index * 4096`
   - **注意**: `prefetch_trace`无法获取`va_block`指针，因此无法直接输出虚拟地址
   - 如需虚拟地址，请使用`chunk_trace`或将两者trace关联分析

3. **pages_accessed的意义**:
   - 表示在当前VA block中已经fault过的页面数
   - 可用于判断访问密度和预取策略效果

**统计信息**（输出到stderr）:
```
================================================================================
PREFETCH TRACE SUMMARY
================================================================================
BEFORE_COMPUTE              329276
ON_TREE_ITER                     0
--------------------------------------------------------------------------------
TOTAL                       329276
================================================================================
```

**与chunk_trace配合使用**:

要获得完整的虚拟地址信息，可以同时运行两个trace工具：

```bash
# 终端1：运行chunk_trace
sudo timeout 30 ./chunk_trace > /tmp/chunk_trace.csv 2>&1

# 终端2：运行prefetch_trace
sudo timeout 30 ./prefetch_trace > /tmp/prefetch_trace.csv 2>&1

# 按时间戳关联分析
# chunk_trace提供va_start，prefetch_trace提供page_index
# 实际地址 = va_start + page_index * 4096
```

---

### Trace工具对比

| 特性 | chunk_trace | prefetch_trace |
|------|-------------|----------------|
| **追踪对象** | Chunk生命周期（eviction相关） | Prefetch决策过程 |
| **主要Hook** | activate, used, eviction_prepare | before_compute |
| **虚拟地址** | ✅ 可获取（va_start/va_end） | ❌ 无法直接获取 |
| **物理地址** | ✅ chunk_addr | ❌ 不涉及 |
| **页面索引** | ✅ va_page_index | ✅ page_index |
| **访问模式** | 通过USED事件分析 | 通过pages_accessed分析 |
| **典型用途** | 分析eviction策略效果 | 分析prefetch策略输入 |

---

## 参考资料

- [Policy Design Guide](../docs/POLICY_DESIGN_GUIDE.md) - 详细的策略设计指南
- [BPF List Operations](../../docs/lru/BPF_LIST_OPERATIONS_GUIDE.md) - BPF链表操作
- [UVM Kernel Parameters](../../memory/UVM_KERNEL_PARAMETERS.md) - UVM内核参数
- [Workload Analysis](../docs/WORKLOAD_ANALYSIS.md) - 工作负载分析文档

---

## Troubleshooting

### 问题1: Failed to attach struct_ops

**原因**: 内核模块未加载或BTF信息不匹配

**解决**:
```bash
# 检查nvidia-uvm模块
lsmod | grep nvidia_uvm

# 重新加载模块
sudo rmmod nvidia_uvm
sudo modprobe nvidia_uvm
```

### 问题2: BPF verifier错误

**原因**: BPF程序违反了verifier规则

**解决**:
- 检查数组边界访问
- 确保所有指针都经过NULL检查
- 使用`BPF_CORE_READ`读取内核结构

### 问题3: Struct_ops已存在

**原因**: 之前的instance未正确清理

**解决**:
```bash
# 找到并杀死持有struct_ops的进程
sudo bpftool map show | grep struct_ops
sudo kill <PID>

# 或强制卸载
sudo bpftool struct_ops unregister id <ID>
```

---

## 贡献

欢迎提交新的策略实现！请确保：
1. 添加详细的注释说明策略逻辑
2. 提供性能测试数据
3. 更新本README

Happy optimizing! 🚀
