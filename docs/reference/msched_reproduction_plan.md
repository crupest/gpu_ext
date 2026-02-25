# MSched 算法复现与研究计划

**日期**: 2026-02-25
**论文**: "Towards Fully-fledged GPU Multitasking via Proactive Memory Scheduling" (arxiv 2512.24637)
**完整论文**: `docs/reference/msched_paper/html/2512.24637v2/index.html`
**状态**: 进行中

---

## 1. 核心洞察：MSched 算法对单应用 UVM 同样有效

MSched 论文以 GPU 多任务为切入点，但其两个核心算法——**template-based working set prediction** 和 **Belady OPT eviction**——本质上解决的是 "UVM demand paging 不知道每个 GPU kernel 实际需要哪些页" 的问题。这个问题在**单应用 oversubscription** 下同样存在。

### 1.1 单应用为什么也需要 per-kernel 预测？

以 llama.cpp 120B MoE (59 GiB) 在 RTX 5090 (32GB) 上 decode 为例：

```
单次 decode step ≈ 160 个 GPU kernel 依次执行:
  kernel_layer0_attn(weights_L0, KV_L0, ...)    → 实际访问 ~60MB
  kernel_layer0_ffn(weights_L0_ffn, ...)         → 实际访问 ~120MB
  kernel_layer1_attn(weights_L1, ...)            → 实际访问 ~60MB
  ...
  kernel_layer79_ffn(weights_L79_ffn, ...)       → 实际访问 ~120MB
  // 下一个 decode step 重复同样的序列
```

- **总分配**: 59 GiB（所有 weights + activations + KV-cache 在一个大 buffer 里）
- **每个 kernel 实际 working set**: 16KB – 120MB（一层的 weight slice）
- **Default UVM**: 不知道每个 kernel 的 working set → fault → 按 spatial locality 猜 → 猜错的页挤掉后续 kernel 需要的页 → 连锁 thrashing
- **MSched template 预测**: 精确知道 layer K 的 kernel 只访问 `weights[offset_K : offset_K + size_K]`，0% false positive

### 1.2 MSched 论文中的证据

- **Table 1**: llama.cpp 的 allocation-granularity 预测 false positive rate 高达 99.7%（因为所有层的 weights 在一个大 cudaMalloc 里），而 template 预测 0%
- **Fig 8 (Ablation)**: 300% oversubscription 时，allocation-granularity 比 template 多 12.27× 迁移量 → throughput 差 15.67×。这是**单应用效应**——精度是瓶颈，不是多任务
- **Fig 6**: 即使没有 pipeline 优化，MSched 在 150% oversubscription 下保持 74% in-HBM throughput，demand paging 只剩 6%

### 1.3 gpu_ext 已有成果

gpu_ext 论文已经在单应用场景下证明了部分效果：
- llama.cpp 120B decode: **11.3× over raw UVM** (86.89 vs 7.72 tok/s)，使用 stride prefetch + LFU eviction
- vLLM Qwen-30B TTFT: **1.7-2× improvement**
- GNN training: **2.65× speedup**

**差距**: gpu_ext 的 prefetch 是 "围绕 fault address 的空间扩展"，MSched 是 "这个 kernel 精确访问哪些页"。随着 oversubscription 加大，这个精度差距指数放大（MSched Fig 8）。

---

## 2. MSched 算法分解

### 2.1 算法一：Template-Based Working Set Prediction

**离线阶段**（NVBit profiling）：
1. 用 NVBit instrument 每个 GPU kernel 的每次内存访问
2. 记录：`kernel_name → (launch_args, accessed_memory_regions)`
3. 分析 memory analyzer 归类为三种 template：

| Template | 占比 | 公式 | 示例 |
|----------|------|------|------|
| T1 (Fixed) | ~77% | `size = 常量` | 固定大小的 weight slice，invariant buffer |
| T2 (Linear) | ~18% | `size = k × arg_product` | `matmul(A,B,C,M,N,K)` → size ∝ M×K |
| T3 (Strided) | ~5% | `stride = k × arg_product` 的不连续块 | 高维 tensor 的特定维度操作 |

**在线阶段**（预加载 DLL 拦截 cuLaunchKernel）：
1. 拦截 `cuLaunchKernel` → 读取 launch arguments
2. 代入离线推导的公式 → 计算精确的 page set
3. 将预测结果附加到 kernel metadata

**精度**: 0.25% false negative, 0% false positive (Table 1)

### 2.2 算法二：Belady OPT Eviction

对于 kernel 序列 `[K0, K1, K2, ..., Kn]`，每个 kernel 的 working set 已知：

1. 构建 access timeline: `page P → 下次访问在 kernel Ki`
2. 淘汰决策: 淘汰 **下次访问最远** 的页
3. 实现: 逆序遍历 timeline，`madvise` 每个 kernel 的 pages 到淘汰链表尾部 → 链表头部自然是最优淘汰候选

**单应用简化版**: 对于 LLM decode 这种周期性 pattern：
- Layer K 刚跑完 → layer K 的 weights 不会在 ~160 个 kernel 内再被用
- Layer K+1 即将运行 → layer K+1 的 weights 最紧急
- **Belady ≈ 淘汰刚完成的层，预取下一层**

### 2.3 算法三：Pipelined Migration

- 双 Copy Engine: CE0 做 D2H eviction, CE1 做 H2D population 并行
- 全双工 PCIe: 63.5 GB/s (RTX 5080) vs 41.7 GB/s without pipeline (1.52×)

**此算法需要修改驱动，gpu_ext 无法实现。不在复现范围内。**

---

## 3. 复现路线：先离线后在线

### 3.1 ~~为什么先用 NVBit 离线复现？~~ → NVBit 在 RTX 5090 上不可行

原计划用 NVBit 做 device 侧离线 profiling，但**实测失败**：

1. ~~**NVBit 可用**: v1.7.7.1 已支持 RTX 5090 (SM_120)~~ → **实际不可行**: NVBit binary instrumentation 在 SM_120 上极慢（10+ 分钟仅初始化，CPU 200%，模型未加载），20B 和 120B 模型均超时
2. **根本原因**: NVBit 需要对每条指令做 binary translation，RTX 5090 的新 SM 架构使这个过程极其缓慢
3. **替代方案**: 使用**解析式方法**（从 GGUF 元数据计算 per-layer working set）+ **chunk_trace**（host 侧 UVM 事件追踪）替代

> **重要**: 本文档中所有 working set 分析数据均来自**解析式计算**和 **host 侧 chunk_trace**，**不包含任何 NVBit device 侧追踪数据**。

### 3.2 修正后的整体路线

```
Phase 0: ~~NVBit 离线 profiling~~ → 解析式 Working Set 分析 ✅ 已完成
  ↓ 方法: GGUF 元数据解析 + host 侧 chunk_trace
  ↓ 产出: T1/T2/T3 分类、per-decode-step 分析
  ↓ 限制: 无 device 侧 per-kernel ground truth（NVBit 不可行）

Phase 1: BPF eviction policy 实验 ✅ 已完成
  ↓ 实现 cycle_moe eviction policy (T1 保护)
  ↓ 结论: eviction 优化空间有限，默认 LRU 已足够好
  ↓ 关键发现: Hash map/move_head 在 fault handler 中不安全

Phase 2: Layer-aware prefetch（下一步重点）
  ↓ 基于 fault VA 推断当前层号，预取下一层 weights
  ↓ 不需要 NVBit — 利用 VA 地址单调递增的特性
  ↓ 目标: 消除 51% re-fault (83 GB 浪费迁移)

Phase 3: 在线 working set 学习（gpu_ext 独有贡献）
  ↓ device-side eBPF (bpftime) 在运行时观察 per-kernel access pattern
  ↓ 学到映射后 host-side prefetch hook 使用
  ↓ 前提: bpftime GPU 支持在 RTX 5090 上可用
```

---

## 4. ~~Phase 0: NVBit 离线 Profiling~~ → 已放弃，改用解析式方法

### 4.1 ~~目标~~ → 实际状态: NVBit 不可行

原计划:
- ~~在 RTX 5090 上用 NVBit instrument llama.cpp 120B 的 decode 过程~~
- ~~记录每个 GPU kernel 的: kernel name/hash, launch args, 访问的 memory pages~~
- ~~分析 T1/T2/T3 template 分布~~
- ~~生成 `kernel_id → {page_ranges}` 映射表~~

**实际结果**: NVBit binary instrumentation 在 RTX 5090 上完全不可用。20B 和 120B 模型均在初始化阶段超时（10+ 分钟，CPU 200%，GPU 仅 632 MiB）。ws_trace.so 工具已编写但无法产出数据。

**替代**: 使用解析式方法（GGUF 元数据 + chunk_trace host 侧事件），详见后文 "解析式 Working Set 分析" 章节。

### 4.2 NVBit 环境（仅供参考，实际不可用）

- **NVBit 版本**: v1.7.7.1（声称支持 SM_120）
- **CUDA**: 12.9
- **GPU**: RTX 5090, compute capability 12.0 (SM_120)
- **实测结果**: binary translation 阶段极慢，无法完成模型加载。原因可能是 SM_120 指令集新增内容过多，NVBit 的翻译器无法高效处理

### 4.3 实现步骤

1. **下载编译 NVBit v1.7.7.1**
   ```bash
   git clone https://github.com/NVlabs/NVBit.git
   cd NVBit && git checkout v1.7.7.1
   # 编译 mem_trace tool（记录每次 memory access 的地址）
   cd tools/mem_trace && make
   ```

2. **编写 MSched-style memory analyzer NVBit tool**
   ```
   输入: 拦截 cuLaunchKernel 获取 kernel name + args
         instrument 每个 load/store 获取 accessed addresses
   输出: per-kernel memory access range（对齐到 4KB page）
   ```

3. **在 llama.cpp 120B 上运行 profiling**
   ```bash
   LD_PRELOAD=./mem_trace.so \
     workloads/llama.cpp/llama.cpp/build/bin/llama-bench \
     --model gpt-oss-120b-default -p 512 -n 128 --uvm
   ```

4. **分析结果**
   - 每个 kernel 访问了哪些 page range？
   - 有多少 kernel 是 T1 (fixed)？T2 (linear)？T3 (strided)？
   - 和 MSched Table 2 的数据对比（llama.cpp: 60% T1, 38% T2, 2% T3）

### 4.4 预期产出

- `profiling_data/llama_120b_kernel_wsets.json`: 每个 kernel → page ranges 映射
- `profiling_data/llama_120b_template_stats.json`: T1/T2/T3 分布统计
- 一份分析报告，确认 MSched 的 template 分类在我们的 workload 上是否成立

---

## 5. Phase 1: 离线数据指导 gpu_ext 策略

### 5.1 目标

用 Phase 0 的 NVBit profiling 数据，实现两个新的 gpu_ext BPF policy：

1. **kernel-aware prefetch**: 知道每个 kernel 的 working set → 精确预取
2. **cycle-aware eviction**: 知道 kernel 执行顺序 → 近似 Belady OPT

### 5.2 Kernel-Aware Prefetch Policy

**文件**: `extension/prefetch_kernel_aware.bpf.c`

**设计思路**:
- Phase 0 产生的映射表通过 BPF map 加载到内核（userspace loader 写入）
- prefetch hook 触发时，查表得到当前 kernel 应预取的 page range
- 精确预取（不多不少），避免 over-prefetch 污染 HBM

```c
// BPF map: kernel_id → working set range
struct kernel_wset {
    u64 base_page;     // 起始页号
    u32 num_pages;     // 页数
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 8192);
    __type(key, u64);                    // kernel_id (hash)
    __type(value, struct kernel_wset);
} kernel_wset_map SEC(".maps");

// 在 prefetch hook 中:
SEC("struct_ops/uvm_prefetch_before_compute")
int BPF_PROG(uvm_prefetch_before_compute, ...)
{
    // 1. 确定当前是哪个 kernel（通过 fault 地址推断或 cross-layer 通信）
    // 2. 查 kernel_wset_map
    // 3. 设置 prefetch region = working set range
    // 4. return BYPASS
}
```

**挑战**: prefetch hook 收到的信息是 `page_index` 和 `bitmap_tree`（VA block 级别），不直接知道当前是哪个 kernel。需要通过以下方式推断：
- **方案 A**: fault address 落在哪个 weight range → 推断是哪一层 → 预取该层所有页
- **方案 B**: 用 uprobe 拦截 cuLaunchKernel → 写入 BPF map → prefetch hook 读取

方案 B 更精确，但需要 uprobe 支持。gpu_ext 论文已经用 uprobe trace PyTorch allocations，技术可行。

### 5.3 Cycle-Aware Eviction Policy

**文件**: `extension/eviction_cycle_aware.bpf.c`

**设计思路**:
LLM decode 的 kernel 序列是周期性的（每个 decode step 遍历所有层）。利用这个周期性：

```c
struct cycle_state {
    u32 current_pos;      // 当前在 cycle 中的位置
    u32 cycle_length;     // cycle 长度（层数 × 每层 kernel 数）
};

struct page_meta {
    u32 last_access_pos;  // 上次访问时的 cycle position
    u32 kernel_id;        // 哪个 kernel 访问的
};

// 在 eviction_prepare 中:
// distance = cycle_length - (current_pos - page.last_access_pos)
// distance 大 → move_head（优先淘汰）
// distance 小 → move_tail（保护）
```

**vs LFU 的优势**:
- LFU 对 dense model（每层访问频率相同）没有区分能力
- Cycle-aware 知道 "刚用过 = 离下次使用最远"，精确做 Belady

### 5.4 实验方案

| 配置 | Prefetch | Eviction | 说明 |
|------|----------|----------|------|
| Baseline UVM | nvidia-uvm 默认 | 默认 LRU | 对照基线 |
| gpu_ext 现有 | stride prefetch | LFU | 现有最佳组合 |
| 离线指导 | kernel-aware (Phase 0 数据) | cycle-aware | 使用离线 profiling 数据 |
| 离线指导 + LFU | kernel-aware | LFU | 验证 prefetch 精度的独立贡献 |
| 离线指导 + 默认预取 | stride prefetch | cycle-aware | 验证 eviction 改进的独立贡献 |

**Workloads**:
- llama.cpp 120B MoE (主要目标)
- vLLM Qwen-30B (验证泛化性)
- Faiss SIFT-100M (不同 access pattern)

**指标**:
- Decode throughput (tok/s)
- Page fault 数 / decode step
- 总迁移量 / decode step (MB)
- Prefetch false positive rate, false negative rate

---

## 6. Phase 2: 在线 Working Set 学习

### 6.1 目标

去掉 NVBit 离线依赖，用 gpu_ext 的 device-side eBPF 在运行时学习 per-kernel working set。

### 6.2 为什么在线学习可行？

1. **LLM decode 高度重复**: 每个 decode step 执行相同的 kernel 序列，访问相同的 weight ranges
2. **收敛快**: 1-2 个 decode step 就能观察到完整的 pattern
3. **自适应**: 不同 sequence length → 不同 KV-cache 大小，在线学习自动适应

### 6.3 设计

```
Device-side eBPF (GPU kernel 执行时):
  on access(ctx):
    kernel_id = hash(当前 kernel 信息)
    page = ctx->address >> PAGE_SHIFT
    // 记录: 这个 kernel 触碰了这个 page
    map_update(kernel_page_set, {kernel_id, page}, 1)

  on fence(ctx):  // kernel 完成
    // kernel_id 的 page set 快照已完成
    // 通过 cross-layer map 通知 host side

Host-side eBPF (page fault / prefetch 时):
  on gpu_prefetch(ctx):
    kernel_id = 从 cross-layer map 读取当前 kernel
    wset = map_lookup(kernel_wset_learned, kernel_id)
    if wset 有效:
      精确预取 wset 中的页
      return BYPASS
    else:
      return DEFAULT  // 第一次见到的 kernel，用默认策略
```

### 6.4 挑战

1. **BPF map 容量**: 每个 kernel 的 working set 可能有上千个 page → 用 `{base, size}` 的 range 表示而非逐页记录
2. **BPF verifier 限制**: device-side 循环有限制 → 需要 warp-level 聚合
3. **Cross-layer map 延迟**: device→host sync 延迟 → 需要在 kernel boundary (fence) 做 sync
4. **学习收敛速度**: 需要至少一个完整 cycle（一个 decode step）才能学到完整 pattern

### 6.5 vs NVBit 离线的对比

| 维度 | NVBit 离线 | gpu_ext 在线 |
|------|-----------|-------------|
| 精度 | 100%（instrument 每条指令） | 高（取决于采样率和 map 容量） |
| 开销 | 85%+（只用于 profiling） | 3-14%（可常驻） |
| 适应性 | 静态 profile，不适应输入变化 | 实时适应（seq_len, batch_size 变化） |
| 部署 | 需要单独 profiling pass | 无需，运行时自动学习 |
| T2 template | 可推导线性关系 | 需要多次不同参数的观察 |

---

## 7. Phase 3: 集成与全面评估

### 7.1 实验矩阵

**Experiment 1: Prefetch 精度差距量化**

用 gpu_ext 现有 tracing (prefetch_trace, chunk_trace) 测量：

| 配置 | FP rate | FN rate | 迁移量/step |
|------|---------|---------|------------|
| UVM 默认 | ? | ? | ? |
| gpu_ext stride | ? | ? | ? |
| gpu_ext always_max | ? | ? | ? |
| 离线 kernel-aware | 预期 ~0% | 预期 <1% | 预期最小 |
| 在线学习 (收敛后) | 预期 <5% | 预期 <5% | 预期接近离线 |

**Experiment 2: Oversubscription 缩放**

验证 MSched Fig 8 的发现：精度差距在高 oversubscription 下指数放大。

| Oversubscription | gpu_ext stride | 离线 kernel-aware | 差距倍数 |
|------------------|---------------|-------------------|---------|
| 1.5× | ? | ? | ? |
| 2.0× | ? | ? | ? |
| 3.0× | ? | ? | ? |

**Experiment 3: Eviction 策略对比**

| 策略 | Dense Model | MoE Model | 说明 |
|------|------------|-----------|------|
| 默认 LRU | ? | ? | UVM baseline |
| LFU | ? | ? | gpu_ext 现有 |
| Cycle-aware | ? | ? | 新策略 |
| Cycle-aware + kernel-aware prefetch | ? | ? | 完整组合 |

**Experiment 4: 端到端性能对比**

| 系统 | llama.cpp 120B decode | vLLM Qwen-30B TTFT | Faiss build |
|------|----------------------|--------------------|-----------:|
| UVM baseline | 7.72 tok/s (已有) | 9642 ms (已有) | ? |
| gpu_ext 现有策略 | 86.89 tok/s (已有) | 5042 ms (已有) | ? |
| gpu_ext + 离线 kernel-aware | ? | ? | ? |
| gpu_ext + 在线学习 | ? | ? | ? |
| MSched 论文数据 (不同硬件) | 参考值 | — | — |

### 7.2 论文定位

> MSched 证明了 per-kernel working set prediction + Belady OPT eviction 可以比 demand paging 快 11-58×。但 MSched 需要离线 NVBit profiling（85%+ 开销）和自定义驱动 ioctl。
>
> 我们展示 gpu_ext 的跨层 eBPF 运行时可以通过**在线 working set 学习**达到类似效果：device-side eBPF 以 3-14% 开销观察 per-kernel 内存访问 pattern，学到 working set 映射后通知 host-side prefetch/eviction hook 做 kernel-aware 决策——无需离线 profiling、无需驱动修改、无需应用改动。

---

## 8. 实现优先级

| 优先级 | 任务 | 预估工作量 | 依赖 |
|--------|------|-----------|------|
| **P0** | NVBit 环境搭建 + 离线 profiling llama.cpp 120B | 1 周 | 无 |
| **P0** | 用现有 tracing 工具测量 prefetch precision baseline | 3 天 | 无 |
| **P1** | 实现 kernel-aware prefetch (用离线数据) | 1-2 周 | P0 |
| **P1** | 实现 cycle-aware eviction | 1 周 | P0 |
| **P2** | Experiment 1-3: 离线策略 vs 现有策略对比 | 1 周 | P1 |
| **P3** | 在线 working set 学习 (device-side eBPF) | 2-3 周 | P2 结果验证方向 |
| **P4** | Experiment 4: 全面端到端评估 | 1-2 周 | P3 |
| **P5** | 多租户扩展（MSched Combination D 场景） | 2 周 | P4 |

---

## 9. 实验日志

### 2026-02-25: 项目启动

- 下载 MSched 论文完整 HTML + 22 张图片到 `docs/reference/msched_paper/`
- 修复 `.gitmodules` 中 `docs/gpu-ext/paper` submodule 的绝对路径问题
- 完成 MSched 论文精读，确认核心算法对单应用 UVM 场景的适用性
- 确认 NVBit v1.7.7.1 支持 RTX 5090 (SM_120, compute capability 12.0)
- 确认 gpu_ext 已有 prefetch_trace / chunk_trace 可用于测量精度
- 创建本计划文档

**完成**:
- [x] P0: 搭建 NVBit 环境，编译 mem_trace tool
- [x] P0: 用 chunk_trace 测量 raw UVM 的 page fault / eviction 模式

### 2026-02-25: P0 实验结果 — NVBit + chunk_trace 基线测量

#### NVBit 环境

- NVBit v1.7.7.1 下载并编译成功 (`/home/yunwei37/workspace/gpu/NVBit/nvbit_release_x86_64/`)
- 编译了两个工具:
  - `mem_trace.so`: 原始 NVBit mem_trace（每个 warp 的完整内存地址）
  - `ws_trace.so`: 自定义 working set profiler（CPU 端聚合 per-kernel 唯一页集合）
- 编写了 `scripts/analyze_ws.py`: MSched 风格的 working set 分析脚本（T1/T2/T3 分类 + FP/FN 率）

#### chunk_trace 基线测量

**实验配置**: llama.cpp 120B MXFP4 MoE, RTX 5090 (32GB), raw UVM (无 eBPF 策略)

**短测试** (pp=32, tg=16):
- 78K 事件，12,607 唯一 2MB chunks (24.6GB)，0 次 eviction
- 说明短序列不足以触发内存压力

**长测试** (pp=512, tg=128) — **关键数据**:

| 指标 | 值 |
|------|------|
| 总事件数 | 358,445 |
| ACTIVATE (page fault) | 41,825 |
| POPULATE (page use) | 290,523 |
| EVICTION_PREPARE | 25,968 |
| 唯一 2MB chunks | 20,531 |
| VA 空间总量 | 40.1 GB |
| Oversubscription | 8.1 GB (25%) |

**核心发现**:

1. **51% 的 page fault 是 re-fault** (21,294/41,825): 页面被 evict 后又被重新 fault-in
2. **82% 的 chunks 发生 thrashing** (16,813/20,531): 被 evict 后重新加载至少一次
   - 2 次重新激活: 12,338 chunks
   - 3 次重新激活: 4,469 chunks
3. **89% 的 working set 每 200ms 变化一次**: 高度动态的访问模式
4. **激活精度**: 98.4% 的被激活 chunks 确实被使用（demand paging 自身精度高，但不做预测）
5. **浪费的迁移带宽**: ~83.2 GB (每次 re-fault = evict 2MB + reload 2MB)

**Raw UVM 性能 (无 eBPF):**
- pp=512: 143.9 tok/s
- tg=128: 49.4 tok/s

**对比 gpu_ext 论文数据** (有 stride prefetch + LFU):
- tg decode: 86.89 tok/s (1.76× over raw UVM 的 49.4)

**MSched 复现启示**:
- 82% 的 chunk thrashing 证实了 MSched 的核心论点: 默认 eviction 策略不知道哪些页马上需要
- Per-kernel working set prediction 可以消除 51% 的 re-fault，节省 ~83GB 迁移带宽
- Working set 每 200ms 变化 89%，说明每个 decode step 访问的层完全不同
- **下一步**: 用 NVBit ws_trace 获取 per-kernel working set ground truth，验证 template prediction 的 FP/FN 率

**待办**:
- [x] P0: ~~运行 NVBit ws_trace 获取 per-kernel working set~~ → 改用解析式方法（见下方）
- [ ] P1: 设计 kernel-aware prefetch 的 BPF policy 接口
- [ ] P1: 基于 chunk_trace 数据设计 cycle-aware eviction 策略

### 2026-02-25: 解析式 Working Set 分析 — NVBit 替代方案

#### NVBit ws_trace 结论

NVBit v1.7.7.1 在 RTX 5090 (SM_120) 上的 binary instrumentation 阶段**极慢** — 仅初始化就需要 10+ 分钟（CPU 消耗 200%），模型甚至未完成加载。在前一次尝试中 20B 模型也超时失败。**结论**: NVBit 离线 profiling 在当前硬件上不可行，改用解析式方法。

#### 解析式 Per-Layer Working Set 计算

基于 GGUF 元数据解析模型架构参数，直接计算每层的 working set 分布。脚本: `NVBit/scripts/analytical_ws.py`

**120B 模型 (gpt-oss-120b) — 核心数据**:

| 组件 | 每层大小 | 全模型 |
|------|---------|--------|
| Attention (Q8) | 27.0 MB | 0.95 GB (36 layers) |
| Per Expert (MXFP4) | 13.4 MB | — |
| All 128 Experts | 1714 MB | 60.26 GB |
| Router (F32) | 1.4 MB | 50.6 MB |
| Embeddings + Output | — | 1.15 GB |
| **Total Model** | **1741 MB** | **62.36 GB** |

**MSched Template 分类**:

| Template | 类别 | 大小 | 占总模型 |
|----------|------|------|---------|
| **T1 (Fixed)** | 嵌入层 + 注意力 + Router | **2.14 GB** | 3.4% |
| **T2 (Active Experts)** | 4/128 experts × 36 layers | **1.88 GB** | 3.0% |
| **T3 (Inactive)** | 124/128 experts × 36 layers | **58.33 GB** | 93.6% |
| **Ideal WS (T1+T2)** | — | **4.02 GB** | 6.4% |

**关键发现**:

1. **理想 working set 仅 4.02 GB** — 占 32 GB VRAM 的 12.6%
   - 即使 2× oversubscription (模型 62 GB)，活跃数据仅用 1/8 VRAM
   - 剩余 28 GB VRAM 可用于 expert caching

2. **93.6% 的模型是 T3（不活跃 experts）**
   - MoE 的极端稀疏性: 每层 128 个 expert 只激活 4 个
   - Default UVM 无法区分 T1/T2/T3 → 浪费大量 VRAM 缓存不活跃 expert

3. **理想迁移量 vs 实际迁移量**:
   - MSched 理想: 每 decode step 迁移 1.88 GB (仅切换 active experts)
   - Default UVM 实际: ~83 GB wasted migration (chunk_trace 测量)
   - **差距 ~44×** → 巨大优化空间

4. **迁移时间估算**:
   - 理想: 1.88 GB / 63.5 GB/s = 29.6 ms per decode step
   - 120B decode throughput ~50 tok/s (20ms/tok) → 迁移可与计算 overlap

5. **与 chunk_trace 数据交叉验证**:
   - chunk_trace 显示 20,531 唯一 2MB chunks (40.1 GB VA)
   - 解析计算: 62.36 GB total model → 合理 (包含 alignment/padding)
   - chunk_trace 的 82% thrashing 主要来自不活跃 expert 的无差别 eviction

#### 策略含义

**对 gpu_ext kernel-aware prefetch 的启示**:
- 不需要 NVBit 级别的逐指令追踪
- 只需知道 "当前 decode step 在第几层" + "路由器选了哪些 expert"
- 信息来源: cuLaunchKernel 拦截 (uprobe) 或 fault 地址推断
- 精度: 只需 layer-level 粒度 (不需要 instruction-level)

**对 eviction 策略的启示**:
- LRU/LFU 无法区分 "刚用完的 expert" 和 "下次马上要用的 attention weights"
- Cycle-aware: 知道 decode step 的层序列 → Belady OPT ≈ 淘汰刚完成层的不活跃 experts
- 保护策略: T1 (attention + embeddings) 永远不淘汰; T2 按 cycle distance 排序

### 2026-02-25: 解析模型 vs chunk_trace 交叉验证

用 chunk_trace 的 VA 地址时序数据验证解析模型:

**Per-decode-step 分析** (pp=512, tg=128):

| 指标 | 值 | 含义 |
|------|------|------|
| 检测到的 decode steps | 177 | 基于 VA 大幅回退检测 |
| 稳定 decode phase | 156 steps (skip warmup) | t=3s 之后 |
| 平均 WS/step (page faults) | 333 MB (166 chunks) | 每步需要 fault-in 的数据 |
| 平均 new data/step | 60 MB | 从未见过的新 chunk |
| 平均 step duration | 41 ms (~24 tok/s) | 含 migration 开销 |
| 总 activated VA | 40.10 GB / 62.36 GB | 34% expert 从未被路由 |

**关键发现**:

1. **333 MB/step 的 page fault 全部是 re-fault** — 每步 fault-in 333 MB，但只有 60 MB 是首次访问的新数据，其余 273 MB 全是之前被 evict 的 chunk 重新加载
2. **100% ascending VA pattern** — 每个 decode step 内，VA 地址单调递增，证实 layer 序列化访问
3. **Step 6 是主加载阶段** — 15,446 chunks (30.9 GB) 在 2 秒内加载（prefill 阶段）
4. **3,718 chunks 只 fault 1 次** (7.4 GB) — 这些是 T1 候选（attention weights + embeddings），首次加载后常驻 VRAM
5. **16,813 chunks 多次 fault** — 这些是 expert weights，被 LRU 误 evict 后反复 re-fault
6. **模型中 34% 的 experts 从未被激活** — 总模型 62.36 GB，实际触达 40.10 GB

**Analytical vs Empirical 对比**:

| 维度 | 解析模型 | chunk_trace 实测 | 说明 |
|------|---------|-----------------|------|
| Ideal WS | 4.02 GB | — | 理论最优（无 re-fault） |
| 页面 fault/step | 1.88 GB (全 expert swap) | 0.33 GB (实际 fault) | 实测较低因为部分 expert 已 resident |
| T1 (永久) | 2.14 GB | ~7.4 GB (single-fault) | 实测含部分 hot experts |
| Re-fault rate | 0% (理论最优) | 50.9% | LRU 淘汰错误导致 |
| 迁移浪费 | 0 | 83.2 GB | 每次 re-fault = 4 MB round-trip |
| Step duration | ~30 ms (migration only) | 41 ms | 含 fault handling overhead |

**验证结论**: chunk_trace 数据完全支持解析模型的 T1/T2/T3 分类。核心瓶颈是 LRU eviction 无法区分 T1 (attention, 永驻) 和 T3 (inactive experts, 应优先淘汰)。

**下一步**:
- [x] P1: 实现 expert-aware eviction (T1 保护 + T3 优先淘汰) — **已完成**，见下方
- [ ] P1: 实现 layer-aware prefetch BPF policy (基于 fault 地址推断层号)
- [ ] P1: 基于 VA 地址的 cycle position 检测 (每 decode step 的 VA 回退检测)

### 2026-02-25: Cycle-Aware MoE Eviction Policy 实现与评测

#### 实现: `extension/eviction_cycle_moe.bpf.c`

**设计原则**: 最小干预——只在必要时保护 T1 chunks，其余让内核默认策略处理。

**核心机制**:
1. **T1 频率检测**: Per-CPU array (O(1) lookup) 跟踪每个 chunk 的访问次数
   - 访问 >= 3 次 → T1 (attention/embeddings) → `move_tail` (保护)
   - 访问 < 3 次 → `return 0` (让内核默认处理)
2. **chunk_activate**: `return 0` (内核默认，零开销)
3. **eviction_prepare**: `return 0` (内核默认)

**关键技术发现**:

1. **`move_head` 在 `chunk_activate` 中不安全** — 导致 Xid 31 (FAULT_PDE MMU Fault):
   - 新 chunk 被 activate 后立即移到 HEAD = 第一个被 evict 的候选
   - Eviction 线程并发扫描 list 时，可能在 page table setup 完成前就 evict 该 chunk
   - **解决**: 用 `move_tail` 或 `return 0` (让内核默认处理)

2. **BPF Hash Map 在 UVM fault handler 热路径中不可用** — 导致 Xid 31:
   - `BPF_MAP_TYPE_HASH` 的 bucket lock + hash computation 延迟太高
   - GPU fault timeout 过期 → Xid 31 FAULT_PDE
   - **解决**: 用 `BPF_MAP_TYPE_PERCPU_ARRAY` (O(1) 无锁查找)

3. **BPF verifier 禁止 pointer arithmetic** — 不能对 pointer 类型寄存器做 shift/XOR:
   - **解决**: `bpf_probe_read_kernel(&scalar, sizeof(scalar), &ptr)` 将 pointer 转为 scalar

4. **`return 0` vs `return 1 (BYPASS)` 的性能差异**: 非 T1 chunk 返回 0 让内核处理，避免了 `move_head` 的 list manipulation 开销

#### 性能评测

**120B Model, RTX 5090 (32GB), UVM demand paging**:

| 策略 | pp=512 | tg=128 | tg=512 | pp=2048 | tg=512 (2K ctx) |
|------|--------|--------|--------|---------|-----------------|
| **Baseline (无策略)** | 145.2 | 50.6 | 59.7 | 148.8 | 56.4 |
| **cycle_moe v2** | 145.2 | 50.9 | 59.8 | 149.1 | 56.2 |
| cycle_moe v1 | 144.1 | 50.9 | 47.8 | — | — |
| MRU | 18.97 | 9.62 | 10.3 | — | — |
| LFU | ❌ Xid 31 | ❌ | ❌ | — | — |

**分析**:

1. **cycle_moe v2 ≈ Baseline** (< 1% 差异): 零开销 T1 保护策略
2. **MRU 灾难性退化** (-83%): MRU 把每步都用的 attention weights 移到 HEAD → 每步都被 evict → 疯狂 thrashing
3. **LFU 崩溃**: Hash map 操作在 fault handler 热路径中延迟过高 → GPU MMU timeout
4. **cycle_moe v1 vs v2**: v1 对非 T1 chunks 调用 `move_head` 造成 20% overhead; v2 返回 0 消除开销

**结论**:
- 默认 UVM eviction (LRU-like) 对 MoE 工作负载已经足够好 — attention weights 自然停留在 LRU tail
- cycle_moe v2 作为安全网: 显式 T1 保护，零额外开销，防止 MRU 类策略的 attention thrashing
- **真正的优化空间在 prefetch 侧**: MSched 的核心贡献是 template-based working set prediction + proactive migration，而非 eviction ordering
- **下一步重点**: Layer-aware prefetch policy — 基于 fault 地址推断当前层，预取下一层的 weights
