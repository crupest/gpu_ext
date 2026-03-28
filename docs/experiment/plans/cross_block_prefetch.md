# Cross-VA-Block Prefetch：Per-Workload 算法与实验

> **方向**: bpf_wq + bpf_gpu_migrate_range() kfunc 突破 2MB per-fault prefetch 限制
> 上次更新：2026-03-19

---

## 0. 当前快照

- **机制**: bpf_wq async + `bpf_gpu_migrate_range()` sleepable kfunc，内核新增 ~30 行
- **GNN 10M (1.34x)**: XB direction = **3.36x** speedup (+27% over 论文 2.65x)
- **FAISS 100M (1.5x)**: phase-adaptive add **-32%**，nprobe=1 cycle_moe 有独立价值
- **llama.cpp 120B (1.84x)**: XB **全部负面** (-28.5% ~ ±0%)，PCIe 零和
- **vLLM 30B (1.175x)**: XB ≈ always_max（差距极小）
- **Microbench sf=1.5**: XB +8.3%
- **Phase detection 结论**: always_max 两阶段都最优，narrowing decode -23%~-58%（灾难性）
- **Multi-block (N1)**: GNN K=1-6 = 38.47s (-46% vs always_max)，PCIe 过载
- **核心结论**: XB 效果完全取决于 workload 访问模式 × oversub ratio

---

## 1. 核心 Thesis + Novelty

**Thesis**: Intra-block prefetch (always_max) 在 page fault 时最多预取当前 2MB VA block 内的剩余 pages。Cross-block 通过 bpf_wq 异步预取相邻 VA block，突破 2MB 限制。

**Novelty**:
- **机制层**: 用 BPF workqueue (Linux 6.10+) 替代自写 kernel work queue，避免 va_space use-after-free、per-CPU 抢占安全等问题。内核侧仅 ~30 行 wrapper
- **策略层**: 不同 workload 需要不同 XB 算法，没有通用最优 → BPF 可编程性的价值

**论文中的体现**: 没有单独拆开讲 cross-block，融入各 workload case study（GNN 3.36x, FAISS -32%）

---

## 2. 设计架构

```
旧方案（废弃）:
  BPF hook → per-CPU buffer → 自写 kernel wq → kmalloc → 手动 uvm_va_space_down_read
  （~100 行新内核代码，va_space 生命周期问题）

新方案（已实现）:
  BPF hook → BPF map → bpf_wq_start() → BPF wq callback →
  bpf_gpu_migrate_range() kfunc → uvm_migrate_bpf() → uvm_migrate()
  （内核侧 ~30 行 wrapper + kfunc 注册）
```

**Cross-block prefetch 模式** (`prefetch_cross_block_v2`):

| Mode | 名称 | 描述 |
|------|------|------|
| 0 | direction-aware | 追踪 VA block 访问方向，顺向 prefetch 下一 block |
| 1 | blind adjacent | 无条件 prefetch 下一 block |
| 2 | both directions | prefetch 前后两个 block |
| 3 | adjacent-stride | 连续 3 次 ±1 block → prefetch（自动只在 prefill 触发）|

### 2.1 Workload 访问模式分析

原 `§2` 的访问模式分析保留如下，用于解释为什么 XB 必须按 workload 定制，而不是追求单一通用策略。

#### 2.1.1 llama.cpp 120B MoE (59 GiB, 1.84x oversub)

- **Prefill**: 顺序遍历 36 layers，每 layer 内顺序加载 active expert weights，VA block 访问接近 sequential forward。
- **Decode**: 每 token 仅激活少数 experts，跨 layer 稀疏跳跃，VA block 访问变成 sparse cyclic random。
- **Working set**: T1=2.14GB（attention/embedding），T2=1.88GB，T3=58.33GB（expert weights）；每 token 迁移约 428MB，DMA 约占总时间 59%。
- **XB 含义**: decode 是主瓶颈，1.84x oversub 下 PCIe 已饱和；blind adjacent -28.5%，direction-aware -21%，adjacent-stride ≈ 0%。

#### 2.1.2 vLLM Qwen-30B KV-cache Offloading (~36-40 GiB, 1.13-1.25x oversub)

- **KV-cache**: 单 request 的 KV entries 占据连续 VA range，append-only 单调递增写入；attention 回读近期 token 时有明显 temporal locality。
- **Expert weights**: 与 llama.cpp 类似，呈 strided 模式；KV-cache 增长与 expert weight fault-back 会互相驱逐。
- **Working set**: 总体 oversub 明显低于 llama.cpp，PCIe 仍有余量；KV-cache forward-only，weight 更像 intra-block stride。
- **XB 含义**: 更适合 region-aware 或 uniform always_max；理论上只应对 KV region 做 XB，对 weight region 保持 intra-block。

#### 2.1.3 PyTorch GNN Training (1M-15M nodes, 1.0-2.17x oversub)

- **Feature tensor**: 每 epoch 做全量 sequential scan（low VA → high VA），跨越大量 2MB VA blocks。
- **Adjacency matrix**: 图遍历存在局部性，但不是严格 sequential；epoch 结束会从高 VA 跳回低 VA。
- **Working set**: 10M nodes ≈ 45GB（1.43x），15M nodes ≈ 68GB（2.17x）；10M 时 sequential scan 几乎就是 XB 的理想场景。
- **XB 含义**: strict sequential + wrap-around reset，使 1-block direction-aware XB 命中率极高；多 block 反而容易过载 PCIe。

#### 2.1.4 FAISS Vector Search (SIFT 20M-100M, 1.0-1.5x oversub)

- **Build / add**: K-means 与 index add 都是 full-dataset sequential scan，且会多轮重复。
- **Search**: nprobe 个 posting list 的访问近似随机；方向过滤本身不足以可靠区分 build 与 search。
- **Working set**: SIFT100M 约 48GB，1.5x oversub，属于中等压力；build 与 search 的访问模式截然不同。
- **XB 含义**: 需要 phase-aware 策略，build 阶段开 XB，search 阶段关 XB；否则 search 很容易因为额外 DMA 回退。

#### 2.1.5 原 §2 补充特征矩阵

| Workload | 访问模式补充 | 工作集 / 压力补充 | 对 XB 的直接启示 |
|----------|--------------|-------------------|------------------|
| llama.cpp 120B | prefill 是 sequential forward；decode 是 sparse cyclic random；同一模型内部 phase 差异极大 | `T1=2.14GB`、`T2=1.88GB`、`T3=58.33GB`；每 token 约 `428MB` 迁移；blind / dir-2step / dir-3step / adj-stride 分别约 `-28.5% / -21% / -12% / ±0%` | decode 是主瓶颈，且 1.84x oversub 下 PCIe 已饱和，XB 只能做“保护性 gating”，不能指望净增益 |
| vLLM 30B | KV-cache 是 append-only monotone forward；expert weights 更像 strided；两类数据会互相驱逐 | 总体约 `36-40GB`，oversub `1.13-1.25x`；`cudaMemAdvise(SetPreferredLocation=CPU)` 已用于 KV-cache CPU 偏置 | 原始设计更适合 KV-only / region-aware XB；weight 区域保持 intra-block prefetch 即可 |
| GNN 10M / 15M | feature tensor 是理想的跨 block sequential scan；adjacency 有局部性但不严格顺序；epoch 边界会 wrap-around | 10M 时应用级 `cudaMemPrefetchAsync` 几乎消除 faults；15M 时仍有 residual faults；10M≈45GB，15M≈68GB | strict sequential + wrap reset 使 1-block direction-aware 成为最匹配的 fault-driven XB |
| FAISS SIFT100M | build/add 与 GNN scan 类似；search 的 posting list 访问近似随机，但在 VA 空间仍可能保留弱方向性 | SIFT100M 约 `48GB`；build 是多轮 full scan；search 的 `nprobe=1/4/16` 会显著改变 fault mix | 仅靠 direction consistency 无法可靠区分 build/search，必须引入更强的 phase signal |

### 2.2 Per-Workload 算法设计（压缩版）

原 `§3` 的初始算法设计后来演化为 `N1-N6` 和若干 rerun 版本。这里保留核心逻辑与当前文件映射，去掉重复 boilerplate。

#### 2.2.1 llama.cpp: Phase-Gated Cross-Block

当前落地: `extension/prefetch_llama_phase.bpf.c`

```text
on fault(va):
  region = always_max
  if phase == PREFILL and sequential(va):
    async_migrate(next_block(va), 2MB)
  phase = update_from_fault_rate_or_uprobe()
  return region
```

- 目标是只在 prefill 开 XB，decode 完全禁用。
- 实验结论不是“phase 检测失败”，而是 **phase 检测正确但 1.84x oversub 下 PREFILL XB 本身无收益**。

#### 2.2.2 vLLM: Region-Aware / Phase-Aware Selective Cross-Block

当前落地: `extension/prefetch_vllm_phase.bpf.c`、`extension/prefetch_serving_adaptive.bpf.c`

```text
on fault(va):
  region = always_max
  if in_kv_region(va) and forward(va):
    async_migrate(next_block(va), 2MB~8MB)
  else:
    skip_xb_for_weight_region()
  return region
```

- 原始设计是 region-aware KV-only XB；实际实现后又验证了 phase-gated / serving-adaptive 变体。
- 结果表明 1.175x 低 oversub 下 decode 也受益于大粒度 prefetch，最终最优仍是 `always_max + cycle_moe`。

#### 2.2.3 PyTorch GNN: Aggressive Multi-Block Prefetch

当前落地: `extension/prefetch_cross_block_v2.bpf.c`、`extension/prefetch_stride_multiblock.bpf.c`

```text
on fault(va):
  region = always_max
  delta = block(va) - last_block
  if delta == +1:
    seq_run += 1
    depth = min(depth + 1, MAX_DEPTH)
    prefetch(next 1..depth blocks)
  elif delta < 0:
    seq_run = 0; depth = 1
  else:
    depth = max(depth - 1, 1)
  return region
```

- 设计初衷是利用 epoch scan 的强 sequential 性做多 block lookahead。
- 实测证明 **1-block direction-aware 正好**，而 `K>1` 的 multi-block 版本会过量消耗 PCIe 带宽。

#### 2.2.4 FAISS: Phase-Adaptive Cross-Block

当前落地: `extension/prefetch_faiss_phase.bpf.c`、`extension/prefetch_faiss_uprobe.bpf.c`

```text
on fault(va):
  region = always_max
  update_stride_window(block(va))
  if phase == BUILD and forward_stride_is_dense():
    async_migrate(next_block(va), 2MB)
  else:
    skip_xb_in_search()
  return region
```

- v1 用方向一致率，无法切到 SEARCH；v2 改为检测 “exactly +1 VA block” 的顺序步长。
- uprobe 版本消除了启发式推断，但最终性能与 heuristic 基本等效，说明价值主要来自 **phase gating 本身**。

#### 2.2.5 原始设计补充注记

| Workload | 原设计中的检测 / 自适应要点 | 后续演化后的结论 |
|----------|-----------------------------|------------------|
| llama.cpp | phase 检测曾考虑 fault-rate 阈值、`adjacent-stride` 自然 gating、以及 layer-boundary table 三条路径 | 最终 uprobe `llama_decode()` 最准确；问题不在 phase 检测，而在 1.84x oversub 下 prefill XB 本身无收益 |
| vLLM | KV region 检测曾考虑 VA 范围、fault pattern、以及 uprobe 捕获 `cudaMemAdvise` / allocator 元数据；prefetch size 也曾计划按 oversub 在 `2MB~8MB` 间自适应 | 低 oversub 下 decode 也需要大粒度 prefetch，region-aware / phase-aware 的边际价值小于统一 `always_max + cycle_moe` |
| GNN | 设计时希望按连续 `+1 block` 长度自适应 `depth=1..4`，并在 epoch wrap (`delta<0`) 时 reset | 实测说明 “是否 1-block XB” 比 “更深 lookahead” 更重要；`K>1` 很快转化为 PCIe 过载 |
| FAISS | v1 用方向一致率窗口 (`>70%` BUILD, `<30%` SEARCH, `window=32`)；v2 改成 “exactly +1 block stride” 检测 | 真正保留下来的是 phase gating；direction-consistency 只保留为失败经验，不再推荐 |

### 2.3 通用组件

四类 workload-specific 算法共享以下基础设施。

| 组件 | 用途 | 当前落点 | 状态 |
|------|------|----------|------|
| `bpf_gpu_migrate_range()` kfunc | 异步跨 block 迁移任意 GPU VA range | `uvm_bpf_struct_ops.c` | ✅ |
| `bpf_wq` async 调度 | 将 sleepable migrate 放到 workqueue callback 中执行 | BPF subsystem | ✅ |
| kprobe `va_block` 捕获 | 从 UVM fault 路径缓存 `va_space` / `va_block` 信息 | `extension/prefetch_cross_block_v2.bpf.c` | ✅ |
| `always_max` intra-block | 每次 fault 直接返回整个 2MB VA block | 所有 prefetch 策略共享 | ✅ |
| per-CPU direction cache | 记录最近 block 方向/步长 | `extension/prefetch_cross_block_v2.bpf.c` | ✅ |
| eviction policy | `cycle_moe` / LRU / LFU / reuse-distance 等 | 各策略独立配置 | ✅ |
| heuristic phase detection | FAISS build/search 切换 | `extension/prefetch_faiss_phase.bpf.c` | ✅ |
| uprobe phase detection | llama / FAISS / vLLM 应用语义传递 | `extension/prefetch_*_phase*.bpf.c` | ✅ |
| multi-block XB | stride-aware lookahead | `extension/prefetch_stride_multiblock.bpf.c` | ✅（结论为退化） |
| cooperative eviction | 用 prefetch 预测保护候选 chunk | `extension/prefetch_cooperative.bpf.c` | ✅（结论为中性/有害） |
| reuse-distance eviction | EWMA 重访距离近似 Belady | `extension/prefetch_reuse_dist.bpf.c` | ✅（≈ `cycle_moe`） |
| throttled XB | 用 fault-rate 控制 XB 频率 | `extension/prefetch_throttled_xb.bpf.c` | ✅（llama 上有害） |

### 2.4 实验运行指南（压缩版）

- **模块前置**: 所有实验共用一次自定义模块加载，相关路径在 `kernel-module/nvidia-module/kernel-open/` 与 `extension/`。
- **方法论**: 每组配置做 5-10 次 trial，取 geometric mean；需要比较时使用 paired t-test，`p < 0.05` 视为显著。
- **独占约束**: 同一时间只跑一个 GPU benchmark、只加载一个 struct_ops 策略；切换策略前先清空 GPU 与 struct_ops。
- **清理流程**: 基础清理由 `workloads/cleanup_gpu.py` 负责；struct_ops 残留由 `extension/cleanup_struct_ops_tool` 处理。
- **工作负载入口**:
  - llama.cpp: `workloads/llama.cpp/build/bin/llama-bench`
  - vLLM: `workloads/vllm/configs/serve_bench.py`
  - GNN: `workloads/pytorch/benchmark_gnn_uvm.py`
  - FAISS: `workloads/faiss/bench_gpu_1bn.py`
  - microbench: `microbench/memory/uvmbench`
- **关键环境**:
  - llama.cpp 需要 `GGML_CUDA_ENABLE_UNIFIED_MEMORY=1`
  - PyTorch GNN 需要 `CUDA_MANAGED_FORCE_DEVICE_ALLOC=1`
  - vLLM 通过 `serve_bench.py --mode uvm` 自动启用 UVM allocator
- **结果位置**: 各 workload 结果分别写入 `workloads/*/results/`、`workloads/*/result/` 与 `microbench/memory/results/` 下的实验子目录。

#### 2.4.1 原 Exp-XB1 ~ Exp-XB5 的压缩映射

| 原实验 | Workload | 目标 | 入口/策略文件 | 结果目录 |
|--------|----------|------|---------------|----------|
| Exp-XB1 | llama.cpp 120B | 验证 prefill-only / phase-gated XB 是否能只加速 pp 而不伤 tg | `workloads/llama.cpp/build/bin/llama-bench`，`extension/prefetch_llama_phase.bpf.c` | `workloads/llama.cpp/results/` |
| Exp-XB2 | vLLM 30B | 比较 baseline / always_max / blind XB / phase-aware 变体 | `workloads/vllm/configs/serve_bench.py`，`extension/prefetch_vllm_phase*.bpf.c` | `workloads/vllm/results/` |
| Exp-XB3 | GNN 10M/15M | 验证 sequential scan 上的 XB 与 multi-block | `workloads/pytorch/benchmark_gnn_uvm.py`，`extension/prefetch_cross_block_v2.bpf.c`、`prefetch_stride_multiblock.bpf.c` | `workloads/pytorch/result/` |
| Exp-XB4 | FAISS SIFT100M | build/search phase-adaptive 与 eviction 组合 | `workloads/faiss/bench_gpu_1bn.py`，`extension/prefetch_faiss_phase*.bpf.c` | `workloads/faiss/results/exp_xb4/` |
| Exp-XB5 | microbench | 防止 XB 修改后在顺序/随机模式下回退 | `microbench/memory/uvmbench`，`extension/prefetch_cross_block_v2.bpf.c` | `microbench/memory/results/exp_xb5/` |

#### 2.4.2 原 §5.0 / §5.6 的 checklist（补充）

- **模块前置验证**: 需先确认自定义 `nvidia_uvm` 模块已从 `kernel-module/nvidia-module/kernel-open/` 加载，否则所有 struct_ops 结果无效。
- **策略独占**: 任一时刻只能保留一个 `extension/prefetch_*` loader 对应的 struct_ops；切换配置前必须先结束旧 loader，再做 `cleanup_struct_ops_tool` 清理。
- **GPU 独占**: 任一时刻只能运行一个 benchmark；不同 workload 之间也必须串行，避免互相污染 UVM 状态。
- **统一清理**: 每次切配置前跑 `workloads/cleanup_gpu.py`；若遇到残留 struct_ops，再补 `extension/cleanup_struct_ops_tool`。
- **统计口径**: 原始计划要求 10 trials 几何平均；快速验证可缩到 5 trials，但 rerun 结论必须回到稳定 trial 数。
- **结果组织**: 所有实验都应写到 workload 自己的 `results/` 或 `result/` 子目录，避免混用 cwd 造成 rerun 失效。

---

## 3. 决策记录

| 决策 | 原因 | 日期 |
|------|------|------|
| 选方案 C (bpf_wq) 而非方案 A/B | 最安全、自然提供 compute-migration overlap | 2026-02-26 |
| 废弃自写 kernel wq | va_space use-after-free + per-CPU 抢占安全 + 违反 gpu_ext 设计原则 | 2026-02-26 |
| GNN 用 direction-aware (mode 0) | strict sequential, 3.36x 最佳 | 2026-03-04 |
| llama.cpp 禁用 XB | 1.84x oversub → PCIe 零和，所有 XB 模式负面或中性 | 2026-02-27 |
| FAISS 用 phase-adaptive | build sequential + search random → phase 检测有价值 | 2026-03-05 |
| always_max 两阶段都最优 | N7 实验：narrowing decode -23%~-58%，小 prefetch 失去 batched PCIe 效率 | 2026-03-05 |
| Multi-block K>1 放弃 | N1: K=1-6 GNN 38.47s (-46%)，PCIe 过载 | 2026-03-06 |
| cycle_moe eviction FAISS 有独立价值 | search nprobe=1: cycle_moe 5.98s vs always_max 12.16s | 2026-03-07 |

---

## 4. 任务追踪表

| # | 任务 | 状态 | 关键结果 |
|---|------|:---:|------|
| 1 | bpf_wq + kfunc 机制实现 | ✅ | uvm_migrate_bpf() + bpf_gpu_migrate_range() kfunc |
| 2 | prefetch_cross_block_v2 (通用 4 mode) | ✅ | 方向/blind/both/stride |
| 3 | kprobe va_block 捕获 | ✅ | 获取 va_space 指针供 kfunc 使用 |
| 4 | Microbench 验证 | ✅ | seq_stream +63%, hotspot +15% |
| 5 | llama.cpp XB 实验 | ✅ | 全部负面: blind -28.5%, dir -21%, stride ±0% |
| 6 | GNN XB 实验 | ✅ | **direction-aware 3.36x** (+27% over always_max 2.65x) |
| 7 | FAISS phase-adaptive | ✅ | add -32%, search nprobe=1 cycle_moe 有价值 |
| 8 | vLLM XB 实验 | ✅ | XB ≈ always_max (差距极小) |
| 9 | Phase detection (uprobe) | ✅ | llama/FAISS/vLLM，always_max 两阶段最优 |
| 10 | N1 multi-block stride | ✅ | GNN K=1-6 退化 -46%，放弃 |
| 11 | N7 llama phase-adaptive decode | ✅ | 全部 narrowing -23%~-58%，灾难性 |
| 12 | Cooperative eviction | ✅ | GNN G5/G6 中性，llama L2 中性 |
| 13 | Reuse distance eviction | ✅ | GNN G7/G8 中性，llama L3/L4 中性 |
| 14 | Throttled XB | ✅ | llama L5/L6 中性 |
| 15 | MoE expert bitmap prefetch | ✅ | **负面**: pp -4.8%, tg -3.1% (+116% extra DMA) |
| 16 | Proactive layer migration | ✅ | **中性**: ≈ baseline at 1.84x oversub |
| 17 | Transparent uprobe (GNN/vLLM) | ✅ | GNN proactive ≈ XB; vLLM FlashAttention 不触发 hook |
| 18 | N5 uprobe app-guided prefetch (microbench) | ✅ | always_max 2009ms → uprobe+always_max 788ms（iter0） |
| 19 | FAISS / llama / vLLM uprobe phase rerun | ✅ | FAISS ≈ heuristic；llama tg neutral；vLLM ≈ baseline |
| 20 | 论文数据对比与贡献归因 | ✅ | GNN 是唯一显著超越论文 BPF 的 workload |

### 4.1 原 §6 实现优先级映射（已由上表覆盖）

| 原优先级 | Workload | 原始算法目标 | 实际落地 | 结论 |
|----------|----------|--------------|----------|------|
| P0 | PyTorch GNN | multi-block sequential | `prefetch_cross_block_v2` + `prefetch_stride_multiblock` | direction-aware 3.29x；multi-block 退化 |
| P0 | FAISS | phase-adaptive | `prefetch_faiss_phase` + `prefetch_faiss_uprobe` | add -31.8%，search ≈ always_max |
| P1 | vLLM | region-aware → phase-gated | `prefetch_vllm_phase` + `prefetch_serving_adaptive` | 最优仍是 `always_max + cycle_moe` |
| P2 | llama.cpp | phase-gated | `prefetch_llama_phase` | phase 检测正确，但 XB 在 1.84x oversub 下无益 |

### 4.2 验证：原 §6 已完整保留

原 `§6 实现优先级` 的四个条目已经全部映射到 `§4`：

- `P0 / GNN`、`P0 / FAISS`、`P1 / vLLM`、`P2 / llama.cpp` 都在 `§4.1` 保留了“原目标 → 实际落地 → 结论”。
- 原文中“快速验证路径已完成”的结论也已被 `§4 任务追踪表` 与 `§5.4/§5.5` 的结果矩阵覆盖，没有缺项需要单独恢复命令行。

---

## 5. 关键实验结果

### 5.1 Per-Workload Cross-Block

| Workload | Oversub | 最优策略 | 结果 | vs Baseline |
|----------|---------|---------|------|-------------|
| GNN 10M | 1.34x | XB direction + cycle_moe | 20.98s | **3.36x** |
| FAISS 100M | 1.5x | phase-adaptive + cycle_moe | add 47.7s | **-32.1%** |
| llama.cpp 120B | 1.84x | always_max only (禁 XB) | tg=86.76 | +0.05% |
| vLLM 30B | 1.175x | always_max + cycle_moe | TPOT 55.1ms | -9.5% vs UVM |
| Microbench | 1.5x | XB | — | +8.3% |

### 5.2 Phase Detection (N7, llama.cpp)

| Mode | 描述 | tg 变化 |
|------|------|--------|
| mode 0 | always_max both phases | +0.5% (≈baseline) |
| mode 1 r=32 | narrow decode | **-47.5%** |
| mode 1 r=8 | very narrow | **-57.6%** |
| mode 2 | default kernel decode | **-41.5%** |
| mode 3 | forward-only decode | **-23.1%** |

**结论**: always_max 在两个 phase 都最优。缩小 prefetch 失去 batched PCIe 效率。

### 5.3 论文数据对比

#### 5.3.1 论文原始数据（Paper Baselines）

| Workload | 指标 | Paper UVM Baseline |
|----------|------|:--:|
| llama.cpp 120B | pp512 (tok/s) | 238.48 |
| llama.cpp 120B | tg128 (tok/s) | 7.72 |
| GNN 10M | epoch (s) | 70.06 |
| GNN 15M | epoch (s) | 292.77 |
| FAISS SIFT100M | add (s) | 68.41 |
| FAISS SIFT100M | search np=1 (s) | 5.14 |
| FAISS SIFT100M | search np=16 (s) | 56.51 |
| vLLM 30B | TPOT (ms) | 374.23 |
| vLLM 30B | throughput (tok/s) | 307.26 |

论文中的 `ncmoe` 参考值: `ncmoe=64` 为 `pp=245.63 / tg=16.34`，`ncmoe=32` 为 `pp=260.14 / tg=18.18`。

#### 5.3.2 BPF 最佳结果 vs 论文 Baseline

| Workload | 指标 | Paper Baseline | 最佳 BPF 结果 | 最佳策略 | 改进 |
|----------|------|:--:|:--:|------|------|
| llama.cpp 120B | pp512 (tok/s) | 238.48 | 230.96 | `always_max + cycle_moe` | 基本持平 |
| llama.cpp 120B | tg128 (tok/s) | 7.72 | 91.97 | `always_max + cycle_moe` | **+1091% (11.9x)** |
| GNN 10M | epoch (s) | 70.06 | 21.15 | XB direction-aware | **-69.8% (3.32x)** |
| FAISS SIFT100M | add (s) | 68.41 | 47.31 | phase v2 + default LRU | **-30.8%** |
| FAISS SIFT100M | search np=1 (s) | 5.14 | 4.38 | `always_max` | **-14.8%** |
| FAISS SIFT100M | search np=16 (s) | 56.51 | 49.45 | `always_max` | **-12.5%** |
| vLLM 30B | TPOT / throughput | 论文配置不同 | 不直接可比 | — | 仅能与本地 UVM baseline 比较 |

> 注: vLLM 论文数据使用不同并发与 offload 配置；本地 rerun 的有效结论是 `TPOT 60.9 → 55.1ms`、`throughput 233.8 → 256.8 tok/s`。

#### 5.3.3 vs 论文 ncmoe (CPU Offload) 对比

| 方案 | pp512 | tg128 | vs ncmoe=32 |
|------|:---:|:---:|:---:|
| ncmoe=64 (paper) | 245.63 | 16.34 | — |
| ncmoe=32 (paper) | 260.14 | 18.18 | 1.0x |
| UVM baseline (paper) | 238.48 | 7.72 | tg 0.42x |
| **BPF `always_max + cycle_moe`** | **230.96** | **91.97** | **tg 5.06x** |

#### 5.3.4 各算法改进贡献分析

**有效算法**

| 算法 | 关键机制 | 生效 workload | 实际贡献 |
|------|----------|--------------|----------|
| `always_max` | 每次 fault 预取整个 2MB VA block | 全部 | llama tg +1091%，GNN 2.6x，FAISS add/search 均显著改善 |
| `cycle_moe` | 高频 T1 chunk 保护 | llama.cpp | 相比论文 eviction_lfu 带来小幅但稳定增益 |
| XB direction-aware | 检测访问方向并预取相邻 block | GNN | 在 `always_max` 基础上再快约 21% |
| phase-adaptive | build/search 切换 XB | FAISS | build 保留 XB，search 避免无效 DMA |

**无效或有害算法**

| 算法 | 主要问题 | 结论 |
|------|----------|------|
| stride multi-block (K>1) | PCIe 过载 | GNN 最差退化到 38.47s |
| cooperative eviction | 保护信息没有额外价值 | GNN ≈ XB；llama 有害 |
| reuse-distance eviction | 与 `cycle_moe` 几乎等效 | 没有稳定超越基线 |
| throttled XB | fault-rate 不是好代理 | llama 上持续回退 |
| phase-adaptive decode size | 失去 batch PCIe 效率 | llama / vLLM 一致有害 |
| proactive uprobe (粗粒度) | 预取量相对总工作集过小 | GNN ≈ XB direction |

#### 5.3.5 核心改进算法总结

- **最大单一改进** 仍是 `always_max`：绕过 NVIDIA 默认过于保守的 threshold=51 行为。
- **真正的新算法增益** 主要来自 GNN 的 **direction-aware 1-block XB**。
- **辅助但非决定性** 的只有 `cycle_moe` 与 FAISS phase gating。

#### 5.3.6 系统设计启示

- 简单策略最有效: `always_max` 的收益远大于复杂启发式。
- Oversub ratio 决定 XB 成败: `<1.5x` 常有利，`>1.5x` 往往变成 PCIe 零和。
- BPF 的主要价值是 **运行时可切换**，不是一定要写更复杂的策略。
- 默认驱动参数过于保守，BPF 让“激进但可控”的 prefetch 成为可实验接口。

#### 5.3.7 vs 论文最佳 BPF 算法

| 工作负载 | 论文 gpu_ext BPF | 本文最佳结果 | 差异 |
|----------|------------------|--------------|------|
| llama.cpp pp | 229.67 tok/s | 230.96 tok/s | 基本持平 |
| llama.cpp tg | 86.89 tok/s | 91.97 tok/s | `cycle_moe` 带来约 +5.8% |
| GNN 10M | 26.47s | 21.15s | **快 25.2%**，唯一显著超越论文 BPF 的 workload |
| FAISS add | 约 21-29% reduction | 47.31s (-31.8%) | 略优，主要来自 phase gating |
| vLLM | 不可直接比较 | 55.1ms TPOT | 测试配置不同 |

### 5.4 补充实验结果矩阵（覆盖原 §7.1-§7.15）

#### 5.4.1 原 §7.1-§7.4

| 原节次 | 实验 | 关键数字 | 结论 |
|--------|------|----------|------|
| 7.1.0 | GNN allocator bug fix | V1 baseline `70.15s`，V3 baseline `140.25s`，V3+always_max `140.22s` | `cudaMemAdviseSetPreferredLocation=CPU` 导致 2x 退化，必须回退 allocator |
| 7.1.1 | GNN 修复后 rerun | baseline `70.15s`，always_max `26.99s`，XB dir `21.32s`，adj-stride `24.32s` | 修复后 XB dir 在 always_max 之上再快约 21% |
| 7.2 | FAISS baseline / always_max / XB dir | add `69.40 → 49.49 → 50.28s`，np1 `5.19 → 4.38 → 4.45s` | always_max 已非常强；盲目 XB 比 always_max 略差 |
| 7.3 | vLLM 首轮结果（已失效） | old always_max P99 TPOT `2760.5ms` | 该结论因 benchmark 参数与 cwd 错误失效，必须以后续 rerun 为准 |
| 7.3.1 | `faiss_phase` 套到 vLLM | P99 TPOT `1455.6ms`，phase BUILD/SEARCH 各 `54` 次 | vLLM 没有 clean INIT/SERVING boundary，FAISS 型 phase 检测不适配 |
| 7.4 | Microbench 回归 | sf=1.0 seq `52.89 → 48.06ms`，rand `52.47 → 49.76ms`，sf=1.5 seq `2512.52 → 2320.64ms` | 真正 oversub sequential 场景下 XB 稳定约 +8.3% |

#### 5.4.2 原 §7.5-§7.10

| 原节次 | 实验 | 关键数字 | 结论 |
|--------|------|----------|------|
| 7.5 | FAISS phase-adaptive v1 | add `47.73s`，np1 `8.38s`，`search_skip=0` | 方向一致率检测从未切到 SEARCH，search 阶段极其有害 |
| 7.6 | FAISS phase-adaptive v2 / D3 | D3 add `47.31s`，np1 `5.49s`，`search_skip=764,647` | “exactly +1 block” 的 stride 检测有效，default LRU 比 `cycle_moe` 更适合 FAISS search |
| 7.7 | FAISS kprobe 优化 | D4 fixed add `48.22s`，np1 `5.54s` | 跳过 `va_space` 指针链几乎不改性能，真正开销不在 kprobe 侧 |
| 7.8 | vLLM 全量 rerun | baseline `TPOT 60.9ms / 233.8 tok/s`，Config C `55.1ms / 256.8 tok/s` | 原“always_max 有害”结论被推翻；最佳是 `always_max + cycle_moe` |
| 7.9 | Uprobe microbench | always_max iter0 `2009ms`，uprobe+always_max `788ms` | 应用 hint 可在 fault 之前启动 DMA，额外带来 40-60% 提升 |
| 7.10 | N1 multi-block stride | always_max `26.37s`，1-block XB `20.96s`，N1 `38.47s` | K=1-6 多 block 预取严重过载 PCIe |

#### 5.4.3 原 §7.11-§7.15

| 原节次 | 实验 | 关键数字 | 结论 |
|--------|------|----------|------|
| 7.11 | Uprobe FAISS phase detection | add `48.65s`，np1 `6.06s` | uprobe ≈ heuristic，价值在 phase gating 本身而非检测实现方式 |
| 7.12 | Uprobe llama.cpp phase detection | phase-gated `pp -28%`，`tg` 约 neutral | phase 检测正确，但 prefill XB 在 1.84x oversub 下仍无益 |
| 7.13 | Uprobe vLLM phase detection | baseline `61.40ms`，N6 `61.46ms`，always_max+cycle_moe `57.17ms` | decode 阶段也需要大粒度 prefetch，按 phase 关 XB 反而失去收益 |
| 7.14 | Uprobe phase detection 综合结论 | FAISS 有效，llama 中性，vLLM 近 baseline | phase detection 只有在 phase 访问模式真正分化时才有价值 |
| 7.15.1 | llama decode prefetch size | mode0 `84.31 tok/s`；mode1/2/3 为 `44.03 / 35.55 / 49.07 / 64.46` | 缩小 decode 预取范围会严重打碎 batched DMA |
| 7.15.2 | FAISS phase rerun | baseline add `98.5s`，always_max `48.9s`，uprobe `48.3s`，heuristic `48.9s` | 修复 uprobe 路径后，uprobe / heuristic / always_max 近乎等效 |
| 7.15.3 | vLLM decode prefetch size | baseline `61.26ms`，always_max `56.89ms`，mode1 `62.89ms`，mode2 `62.23ms` | vLLM 也不适合缩小 decode 预取范围 |
| 7.15.4 | Cross-workload 总结 | llama `-23%~-58%`，vLLM `+1.6%~+2.7% TPOT` | phase detection 的价值在 XB gating 或语义传递，不在于调节 intra-block prefetch size |

#### 5.4.4 FAISS 结果补全表（原 §7.2 / §7.5 / §7.6 / §7.7 / §7.15.2）

**首次 SIFT100M 主实验与 phase-adaptive 演化**

| Config | 策略 | add (s) | np=1 (s) | np=4 (s) | np=16 (s) | 备注 |
|--------|------|:---:|:---:|:---:|:---:|------|
| A | no BPF | 69.40 | 5.19 | 14.34 | 55.96 | 原始 baseline |
| B | `always_max` | 49.49 | 4.38 | 12.62 | 49.45 | add `-28.7%`，search `-12%~-16%` |
| C | XB dir | 50.28 | 4.45 | 13.47 | 52.47 | 比 `always_max` 略差 |
| D1 | phase v1 (`dir + cycle_moe`) | 47.73 | 8.38 | 13.83 | 54.19 | `search_skip=0`，从未切到 SEARCH |
| D2 | phase v2 (`stride + cycle_moe`) | 48.35 | 9.78 | 14.02 | 50.76 | stride 检测已工作，但 `cycle_moe` 伤害 search |
| D3 | phase v2 (`stride + default_lru`) | 47.31 | 5.49 | 12.71 | 49.51 | 最佳 add；`search_skip=764,647` |
| D4 fixed | phase v2 + kprobe opt | 48.22 | 5.54 | 12.71 | 49.53 | 跳过 `va_space` 指针链几乎不改性能 |

**修复 uprobe 路径后的 phase rerun（原 §7.15.2）**

| Config | 描述 | add (s) | np=1 (s) | np=4 (s) | np=16 (s) | 结论 |
|--------|------|:---:|:---:|:---:|:---:|------|
| 2a | no BPF | 98.5 | 5.12 | 14.95 | 62.94 | rerun baseline |
| 2b | `always_max_cycle_moe` | 48.9 | 4.42 | 12.52 | 49.39 | 与主实验结论一致 |
| 2c | uprobe phase（正确 inode 路径） | 48.3 | 4.39 | 12.62 | 49.23 | 与 heuristic / always_max 近乎等效 |
| 2d | heuristic phase | 48.9 | 4.40 | 12.59 | 49.38 | 再次说明 phase 检测实现方式不是主矛盾 |

#### 5.4.5 vLLM 结果补全表（原 §7.3 / §7.3.1 / §7.8 / §7.13 / §7.15.3）

**首轮 vLLM 结果（已失效，但仍保留原始数据）**

| Config | 策略 | Mean TTFT (ms) | P99 TTFT (ms) | Mean TPOT (ms) | P99 TPOT (ms) | Throughput (tok/s) |
|--------|------|:---:|:---:|:---:|:---:|:---:|
| A | no BPF | 180,616 | 335,796 | 240.8 | 751.7 | 57.84 |
| B | `always_max` | 177,491 | 334,623 | 318.2 | 2,760.5 | 60.37 |
| C | XB blind | 177,890 | 329,709 | 268.7 | 740.3 | 59.76 |
| E | `faiss_phase v2` | 178,231 | 333,217 | 281.5 | 1,455.6 | 60.27 |

说明: 这组数据受 `serve_bench.py` 的 `cwd` 与缺失参数影响，只保留作“错误结论来源”的历史记录。

**修正参数后的全量 rerun（原 §7.8）**

| Config | 策略 | TPOT (ms) | P99 TPOT (ms) | TTFT (ms) | P99 TTFT (ms) | Tput (tok/s) | Duration (s) |
|--------|------|:---:|:---:|:---:|:---:|:---:|:---:|
| A | no BPF | 60.9 | 64.5 | 76,381 | 172,633 | 233.8 | 218.6 |
| B | `always_max` | 56.7 | 59.0 | 68,136 | 156,510 | 251.6 | 201.9 |
| C | `always_max + cycle_moe` | 55.1 | 57.4 | 66,985 | 151,560 | 256.8 | 197.1 |
| D | XB blind | 56.1 | 58.8 | 67,843 | 155,562 | 252.6 | 201.2 |
| E | XB direction | 56.3 | 59.9 | 67,473 | 152,469 | 256.0 | 197.1 |
| F | `serving_adaptive` | 56.3 | 58.7 | 67,658 | 155,166 | 253.5 | 200.4 |

**uprobe / decode-size 系列（原 §7.13 / §7.15.3）**

| Config | 描述 | TPOT (ms) | Throughput (tok/s) | P99 TPOT (ms) | 结论 |
|--------|------|:---:|:---:|:---:|------|
| A | fresh baseline | 61.40 | 231.99 | 64.80 | 参考值 |
| C | `always_max + cycle_moe` | 57.17 | 249.97 | 59.20 | 有效参考 |
| N6 | uprobe phase-gated XB | 61.46 | 234.50 | 73.37 | 机制正确，但去掉了有益 decode prefetch |
| 3a | baseline rerun | 61.26 | 232.55 | 64.76 | — |
| 3b | `always_max_cycle_moe` | 56.89 | 250.99 | 59.43 | 最优 |
| 3c | phase mode0, no XB decode | 57.12 | 249.46 | 59.71 | ≈ `always_max` |
| 3d | phase mode0 + XB both phases | 57.83 | 247.73 | 61.42 | 比 no-XB 略差 |
| 3e | phase mode1 `r=32` | 62.89 | 228.94 | 65.97 | 缩小 decode 预取有害 |
| 3f | phase mode2 (default kernel) | 62.23 | 230.96 | 67.10 | 同样有害 |

#### 5.4.6 Phase Detection 详细计数器（原 §7.3.1 / §7.11 / §7.12 / §7.13 / §7.14）

| 场景 | 关键计数器 | 解读 |
|------|------------|------|
| vLLM + `faiss_phase`（原 §7.3.1） | `build_prefetch=2,008`，`search_skip=16,656`，`phase→BUILD=54`，`phase→SEARCH=54`，`migrate_success=1,641`，`migrate_failed=338` | INIT/SERVING 边界不 clean，FAISS 风格 phase detection 在 vLLM 上频繁振荡 |
| FAISS heuristic v2（原 §7.6 D3） | `build_prefetch=18,182`，`search_skip=764,647`，`phase_transitions=1,460`，`migrate_success=8,183`，`migrate_failed=9,235` | 说明 stride-based phase gating 已经正确禁止 SEARCH 阶段 XB |
| llama N6 p128（原 §7.12） | `prefill=6`，`decode=641`，`XB_prefill=28,592`，`decode_skip=54,771`，`migrate_ok=22,749` | phase 分类正确，但 PREFILL XB 导致 `pp` 回退 |
| llama N6 p512（原 §7.12） | `prefill=6`，`decode=641`，`XB_prefill=68,728`，`decode_skip=58,078`，`migrate_ok=55,666` | 更长 prefill 只会积累更多 DMA / VRAM 干扰 |
| vLLM N6（原 §7.13） | `prefill=66`，`decode=4,032`，`XB_prefill=48,799`，`decode_skip=345,046`，`migrate_ok=23,557`，`migrate_fail=21,368` | 检测机制正确，但 48% failure rate + 去掉 decode XB 让结果退回 baseline |

#### 5.4.7 Microbench 与 Uprobe 详细表（原 §7.4 / §7.9）

**Microbench 回归（fault-driven XB）**

| Workload | sf | `always_max` (ms) | XB dir (ms) | speedup |
|----------|----|:---:|:---:|:---:|
| seq_stream | 1.0 | 52.89 | 48.06 | +10.0% |
| rand_stream | 1.0 | 52.47 | 49.76 | +5.4% |
| seq_stream | 1.5 | 2512.52 | 2320.64 | +8.3% |

**uprobe-driven microbench（应用 hint）**

| Config | 说明 | iter 0 (ms) | iter 1 (ms) | iter 2 (ms) |
|--------|------|:---:|:---:|:---:|
| A | 无 BPF | 10,919 | 13,034 | 12,148 |
| B | `always_max` only | 2,009 | 4,051 | 3,006 |
| C | uprobe + `always_max` | 788 | 2,352 | 2,223 |
| D | uprobe 加载但不触发 | 2,012 | 4,074 | 3,012 |

#### 5.4.8 N7 Decode-Size 详细表（原 §7.15.1 / §7.15.3 / §7.15.4）

**llama.cpp 120B**

| Config | 描述 | pp (tok/s) | tg (tok/s) | tg delta |
|--------|------|:---:|:---:|:---:|
| A | `always_max_cycle_moe` | 213.94 | 83.87 | — |
| B | phase mode0, no XB | 209.12 | 84.31 | +0.5% |
| C | phase mode1 `r=32`, no XB | 213.40 | 44.03 | -47.5% |
| D | phase mode1 `r=8`, no XB | 215.99 | 35.55 | -57.6% |
| E | phase mode2, no XB | 215.71 | 49.07 | -41.5% |
| F | phase mode3, no XB | 216.37 | 64.46 | -23.1% |
| G | phase mode0 + XB prefill | 203.69 | 82.08 | -2.1% |

**跨 workload 结论**

| Workload | 最优策略 | 缩小 decode 预取后的结果 |
|----------|----------|--------------------------|
| llama.cpp 120B | `always_max` 两阶段 | `-23%~-58%` tg |
| FAISS SIFT100M | `always_max ≈ phase-adaptive` | phase 价值在 XB gating，不在 decode-size |
| vLLM 30B | `always_max_cycle_moe` | `+1.6%~+2.7%` TPOT（变差） |

### 5.5 Novel 算法摘要（覆盖原 §8.0-§8.12）

#### 5.5.1 现有算法总结与瓶颈

| Workload | Oversub | 当前最佳 | 主要瓶颈 |
|----------|---------|----------|----------|
| GNN 10M | 1.34x~1.43x | XB direction-aware 约 3.3x | PCIe 有余量但不支持 K>1 的 aggressive XB |
| llama.cpp 120B | 1.84x | `always_max + cycle_moe` | PCIe DMA 已接近饱和 |
| FAISS SIFT100M | 1.5x | add 最优是 phase gating；search 最优常接近 `always_max` | phase 切换 overhead / eviction 选择 |
| vLLM 30B | 1.175x | `always_max + cycle_moe` | oversub 低，复杂策略区分度很小 |

#### 5.5.2 N1: Stride-Predictive Multi-Block Prefetch

- 目标: 在 1-block XB 之上，用 stride history + confidence 预测 `K=1..6` 个 future blocks。
- 与 `prefetch_cross_block_v2` 的核心差异是 multi-block、任意 stride、预测命中反馈。
- 实测: `K=6` 为 `38.47s`，后续 `K=2/3` 仍是 `30.81 / 30.73s`，说明根本问题不是参数不够激进，而是多 block 本身过载 PCIe。

#### 5.5.3 N2: Reuse Distance Eviction

- 思路: 用 EWMA reuse distance 代替 `cycle_moe` 的二值 T1/T2 分类，逼近实用 Belady。
- 文件: `extension/prefetch_reuse_dist.bpf.c`
- 实测: GNN `21.04~21.16s`，llama `tg=91.29~91.64`，与 `cycle_moe` 基本等效。

#### 5.5.4 N3: Cooperative Prefetch-Eviction

- 思路: prefetch 将预测目标写入 ring，eviction 检测到“即将被 XB 使用”的 chunk 时做强保护。
- 文件: `extension/prefetch_cooperative.bpf.c`
- 实测: GNN `21.09~21.15s` ≈ 1-block XB；llama `tg=74.32`，在高 oversub 下反而有害。

#### 5.5.5 N4: Online Access Pattern Classifier

- 设想特征: direction consistency、stride variance、unique block ratio、fault rate。
- 目标映射: sequential → multi-block，strided → 1-block XB，random → skip XB，phase-cycle → phase-adaptive。
- 结论: 由于 oversub ratio 对收益主导性更强，分类器工程复杂度高而边际价值有限，保持低优先级。

#### 5.5.6 N5: Application-Guided Prefetch via uprobe

- 核心洞见: `bpf_gpu_migrate_range()` 可由 sleepable uprobe 直接触发，不必等 fault 到来。
- POC: `extension/test_uprobe_prefetch*.{bpf.c,c}`，microbench 中 `always_max 2009ms → uprobe+always_max 788ms`。
- 工程演化: tracing 程序不能直接用 `bpf_wq`，因此从 pending-map relay 过渡到 `SEC("uprobe.s") + direct kfunc`；multi-chunk ring-buffer pipeline 仍是后续方向。

##### 5.5.6.1 Uprobe POC 实现（原 §8.5.1）

- 三层 pipeline 是: `kprobe` 捕获 `va_space` → `uprobe` 写 pending request 或直接触发 → `struct_ops` / sleepable uprobe 调 `bpf_gpu_migrate_range()`。
- 一个重要实现细节是 tracing 程序无法直接使用 `bpf_wq`，因此早期版本必须走 pending-map relay；后续通过在内核中额外注册 `BPF_PROG_TYPE_KPROBE` 的 kfunc 集合，才让 `SEC("uprobe.s")` 直接同步调用 kfunc 成立。
- 这部分对应的代表文件是 `extension/test_uprobe_prefetch.bpf.c`、`extension/test_uprobe_prefetch.c`、`extension/test_uprobe_prefetch_target.cu`。

##### 5.5.6.2 Multi-Chunk Prefetch Pipeline（原 §8.5.2）

- 原始 POC 只有单 pending slot、单次 depth=1 迁移。
- 原计划中的下一步是把 hint 扩成 multi-slot ring buffer，让应用一次提供未来 `2-4` 个 chunk，并允许多个 DMA 同时在飞。
- 这部分尚未成为主线结果，但它解释了为什么 `iter1/iter2` 仍明显慢于 `iter0`。

#### 5.5.7 N6: Uprobe Phase Detection for llama.cpp / vLLM

- llama.cpp: hook `llama_decode()`，读取 `batch.n_tokens` 判定 prefill/decode；机制正确，但 prefill XB 仍然无益。
- vLLM: hook `uvm_set_phase()`；机制同样正确，但 phase gating 去掉了 decode 阶段本来有利的 prefetch。
- 文件: `extension/prefetch_llama_phase.bpf.c`、`extension/prefetch_vllm_phase.bpf.c`

##### 5.5.7.1 llama.cpp Phase Detection（原 §8.6.1）

- hook 点是 `libllama.so` 里的 `llama_decode()`。
- phase 判定依据是栈上传值的 `llama_batch.n_tokens`: `n_tokens > 1` 视为 PREFILL，`n_tokens == 1` 视为 DECODE。
- 设计目标始终是 “PREFILL 开 XB，DECODE 仅保留 `always_max + cycle_moe`”；实验结果证明 gating 本身正确，但 PREFILL XB 不值得保留。

##### 5.5.7.2 vLLM Phase Detection（原 §8.6.2）

- hook 点是 `uvm_allocator.abi3.so` 中新增的 `uvm_set_phase(int phase)`。
- 原始方案需要三处配合: allocator C++ 导出 `uvm_set_phase()` / `uvm_get_phase()`，Python binding 暴露 `set_uvm_phase()`，`gpu_model_runner.py` 在 `execute_model()` 入口切 PREFILL / DECODE。
- 机制层完全成立，但实验说明 vLLM 的 decode 也受益于大粒度 prefetch，因此 phase gating 不再是最优策略。

#### 5.5.8 实现优先级

| 算法 | 新颖度 | 结果 | 状态 |
|------|--------|------|------|
| N5 uprobe App-Guided (microbench) | ★★★★★ | +40-60% over always_max | ✅ |
| N1 Stride-Predictive Multi-Block | ★★★★ | GNN 退化到 38.47s | ✗ |
| N5 FAISS uprobe phase | ★★★★★ | ≈ heuristic | ✅ |
| N6 llama.cpp phase | ★★★★★ | pp -28%，tg neutral | ✗ |
| N6 vLLM phase | ★★★★★ | ≈ baseline | ✗ |
| N7 Phase-Adaptive Decode Size | ★★★ | 三 workload 一致有害 | ✗ |
| N3 Cooperative | ★★★★★ | GNN ≈ XB，llama 有害 | ✗ |
| N2 Reuse Distance | ★★★ | ≈ `cycle_moe` | ≈ |
| N4 Online Pattern Classifier | ★★★★ | 未投入实现 | 低优先级 |
| N8 Transparent Uprobe | ★★★★ | GNN / vLLM 近似现有最佳 | ✅（工程验证） |

补充: 原 `§8.7` 中的 `N8 GNN Proactive Uprobe` 与 `N8 vLLM Transparent Uprobe` 已在这里合并为同一类工程路线；其细节结果见下文 `§5.5.13`。

#### 5.5.9 实现状态（N1-N4 + 参考基线）

| 编号 | 文件 | 核心思路 | 关键结果 |
|------|------|----------|----------|
| N1 | `prefetch_stride_multiblock.bpf.c` | 可配置 `max_lookahead` 的 stride XB | `K=2/3/6` 均劣于 1-block XB |
| N2 | `prefetch_reuse_dist.bpf.c` | EWMA reuse distance eviction | GNN / llama 均 ≈ `cycle_moe` |
| N3 | `prefetch_cooperative.bpf.c` | prefetch ring 共享给 eviction | GNN 中性，llama 有害 |
| N4 | `prefetch_throttled_xb.bpf.c` | 低 fault-rate 时才启 XB | llama 上有害 |
| Ref A2 | `prefetch_always_max_cycle_moe.bpf.c` | 参考基线 | GNN `26.70s`；llama `tg=91.97` |
| Ref A3 | `prefetch_cross_block_v2.bpf.c` | 1-block direction-aware XB | GNN `21.14s` |

#### 5.5.10 当时测试计划（现已全部完成）

- **GNN 10M**: G1-G8 串行执行，覆盖 baseline、1-block XB、stride K=2/3、cooperative、reuse-distance。
- **llama.cpp 120B**: L1-L6 串行执行，覆盖 baseline、cooperative、reuse-distance、throttled XB。
- **清理约束**: 每个 config 之间都经过 BPF loader 退出、struct_ops 强制清理与 `workloads/cleanup_gpu.py`。

##### 5.5.10.1 原 §8.9 配置矩阵（命令省略版）

**GNN 10M**

| 序号 | Config | 策略文件 | workload 入口 |
|------|--------|----------|---------------|
| G1 | `always_max_cycle_moe` | `extension/prefetch_always_max_cycle_moe.bpf.c` | `workloads/pytorch/benchmark_gnn_uvm.py` |
| G2 | XB direction | `extension/prefetch_cross_block_v2.bpf.c` | `workloads/pytorch/benchmark_gnn_uvm.py` |
| G3 | stride `K=2` | `extension/prefetch_stride_multiblock.bpf.c` | `workloads/pytorch/benchmark_gnn_uvm.py` |
| G4 | stride `K=3` | `extension/prefetch_stride_multiblock.bpf.c` | `workloads/pytorch/benchmark_gnn_uvm.py` |
| G5 | cooperative `r=2` | `extension/prefetch_cooperative.bpf.c` | `workloads/pytorch/benchmark_gnn_uvm.py` |
| G6 | cooperative `r=4` | `extension/prefetch_cooperative.bpf.c` | `workloads/pytorch/benchmark_gnn_uvm.py` |
| G7 | reuse_dist `50ms + XB` | `extension/prefetch_reuse_dist.bpf.c` | `workloads/pytorch/benchmark_gnn_uvm.py` |
| G8 | reuse_dist `20ms + XB` | `extension/prefetch_reuse_dist.bpf.c` | `workloads/pytorch/benchmark_gnn_uvm.py` |

**llama.cpp 120B**

| 序号 | Config | 策略文件 | workload 入口 |
|------|--------|----------|---------------|
| L1 | `always_max_cycle_moe` | `extension/prefetch_always_max_cycle_moe.bpf.c` | `workloads/llama.cpp/build/bin/llama-bench` |
| L2 | cooperative `r=2` | `extension/prefetch_cooperative.bpf.c` | `workloads/llama.cpp/build/bin/llama-bench` |
| L3 | reuse_dist `50ms` | `extension/prefetch_reuse_dist.bpf.c` | `workloads/llama.cpp/build/bin/llama-bench` |
| L4 | reuse_dist `20ms` | `extension/prefetch_reuse_dist.bpf.c` | `workloads/llama.cpp/build/bin/llama-bench` |
| L5 | throttled_xb `1ms/50` | `extension/prefetch_throttled_xb.bpf.c` | `workloads/llama.cpp/build/bin/llama-bench` |
| L6 | throttled_xb `5ms/20` | `extension/prefetch_throttled_xb.bpf.c` | `workloads/llama.cpp/build/bin/llama-bench` |

#### 5.5.11 N1-N4 测试结果（原 §8.10）

**GNN 10M**

| Config | 策略 | avg epoch (s) | vs baseline 70.15s |
|--------|------|:---:|:---:|
| G1 | `always_max + cycle_moe` | 26.70 | 2.63x |
| G2 | XB direction-aware | 21.14 | 3.29x |
| G3 | stride K=2 | 30.81 | 2.28x |
| G4 | stride K=3 | 30.73 | 2.28x |
| G5 | cooperative r=2 | 21.09 | 3.33x |
| G6 | cooperative r=4 | 21.15 | 3.32x |
| G7 | reuse_dist 50ms + XB | 21.16 | 3.32x |
| G8 | reuse_dist 20ms + XB | 21.04 | 3.33x |

**llama.cpp 120B**

| Config | 策略 | pp (tok/s) | tg (tok/s) |
|--------|------|:---:|:---:|
| L1 | `always_max + cycle_moe` | 230.96 | 91.97 |
| L2 | cooperative r=2 | 216.86 | 74.32 |
| L3 | reuse_dist 50ms (no XB) | 230.26 | 91.64 |
| L4 | reuse_dist 20ms (no XB) | 229.56 | 91.29 |
| L5 | throttled_xb 1ms / 50 | 215.79 | 73.57 |
| L6 | throttled_xb 5ms / 20 | 225.89 | 82.37 |

#### 5.5.12 分析与结论（原 §8.11）

- **GNN**: cooperative / reuse-distance + XB 与 1-block XB 几乎等效，真正起作用的是 cross-block 本身而不是 eviction 改进。
- **llama.cpp**: 1.84x oversub 下所有 XB 变体都回到 PCIe 零和问题；reuse-distance 也无法稳定优于 `cycle_moe`。
- **跨 workload**: XB 价值由 oversub ratio 与访问模式共同决定；在高 oversub 下，复杂启发式通常只会引入额外 DMA。

#### 5.5.13 透明 Uprobe 应用引导预取（原 §8.12）

| Workload | 配置 | 关键数字 | 结论 |
|----------|------|----------|------|
| GNN 10M | transparent proactive v3 | `21.26s` vs XB `21.15s` | 机制正确，但 16MB proactive 相对 10GB 工作集太小，没有额外收益 |
| vLLM 30B | transparent phase | `57.33ms` vs always_max `56.41ms` | hook 目标随 backend 变化，静态 paged-attention hook 不可靠 |

- 重要工程教训 1: struct_ops / kprobe 运行在 UVM 内核线程上下文，不能直接依赖 `bpf_get_current_pid_tgid()` 做应用进程过滤。
- 重要工程教训 2: transparent hook 的难点常常不是 BPF 本身，而是找到稳定的用户态符号与真实运行时 inode。

##### 5.5.13.1 GNN Transparent Uprobe 的三轮演化

| Config | Avg Epoch | vs Baseline | 关键指标 / Bug 状态 |
|--------|-----------|-------------|---------------------|
| G1 `always_max_cycle_moe` | 26.61s | 2.64x | 参考基线 |
| G2 XB direction-aware | 21.15s | 3.32x | `XB wq=1.12M` |
| G3 v1 | 70.32s | 1.00x | `prefetch_hook=0`、`xb=0`、`sync=0`，三处 bug 全在 |
| G3 v2 | 26.85s | 2.61x | struct_ops / kprobe 进程过滤 bug 已修，`sync=0` 仍说明 uprobe 未命中 |
| G3 v3 | 21.26s | 3.30x | `sync=8`，`prefetch_hook=1.17M`，`xb=1.12M`，机制全部打通 |

- 三个关键 bug 分别是: struct_ops 里错误依赖应用 PID、kprobe 同样错误过滤 PID、以及硬编码错了 `libcudart.so.12` 的真实路径。
- v3 的 proactive 计数器显示 `direct proactive migrate ok=1 / fail=7`，pending request `set=8 / drained=0`，说明机制成立但收益被现有 fault-driven XB 基本淹没。

##### 5.5.13.2 vLLM Transparent Uprobe 的 backend 问题

| Config | TPOT (ms) | Throughput | 结论 |
|--------|:---:|:---:|------|
| A baseline | 61.36 | 230.07 tok/s | 参考值 |
| B `always_max` | 56.41 | 252.23 tok/s | 有效基线 |
| C transparent | 57.33 | 249.38 tok/s | ≈ `always_max` |

- 透明 hook 的原设想是挂 `paged_attention_v1/v2`，但实际 vLLM v1 engine 走的是 FlashAttention backend (`flash::mha_varlen_fwd`)，因此 paged-attention uprobe 根本不触发。
- 这也是原 `§8.12` 的核心工程结论之一: “透明” 不是难在 BPF，而是难在稳定锁定真实运行路径。

## 6. 后续论文改进路线（压缩版，覆盖原 §10）

### 6.1 Fault-Driven Heuristic 已接近天花板

- 已测试的 15+ 个 fault-driven 启发式大多无法稳定超越 `always_max`。
- llama.cpp 当前瓶颈已接近 PCIe DMA 串行极限；继续在 fault 后做更复杂判断，很难消除 fault latency 本身。
- 唯一出现量级跃迁的是 **uprobe 直接迁移 microbench**，说明真正的突破点在 **fault 之前**。

### 6.2 改进方向一：MoE Expert Proactive Prefetch

- 目标: 在 router / expert dispatch 处通过 uprobe 拿到 expert ID，提前迁移下一批 expert weights。
- 预期: 将 llama.cpp 120B decode 从 `91.97 tok/s` 推向 `120-140 tok/s`，逼近 DMA-compute overlap 上限。
- 所需步骤: 确认 expert VA 布局、实现 uprobe → map → migrate pipeline、对比 `always_max + cycle_moe`。

### 6.3 改进方向二：GNN Batch-Level Proactive Migration

- 当前 transparent uprobe 只在 epoch 边界搬 16MB，粒度太粗。
- 真正需要的是 dataloader / sampler 暴露下一 batch 的 feature rows，然后 BPF 在 batch N compute 时搬 batch N+1。
- 目标: 在 GNN 15M 上逼近应用级 `cudaMemPrefetchAsync` 的效果。

### 6.4 改进方向三：扩展 BPF Struct_Ops 接口

| 新接口 | 解决的问题 | 用哪个 workload 证明 |
|--------|------------|---------------------|
| `gpu_page_prefetch` 直接传 `va_block/va_space/PID` | 消除 kprobe side channel hack | 所有 XB 策略 |
| sleepable 原生跨块 prefetch hook | 消除 `bpf_wq` 调度延迟 | GNN XB |
| `bpf_gpu_get_pmm_stats()` | pressure-aware 决策 | FAISS |
| `bpf_gpu_get_copy_engine_backlog()` | bandwidth-aware rate limiting | llama.cpp |
| `bpf_gpu_block_insert_after()` | 更严格的 ordered eviction | llama eviction |
| `gpu_migrate_complete` / `gpu_fault_batch_begin/end` | pipeline 控制与批量 fault 语义 | semantic-driven prefetch |

### 6.5 改进优先级

| 优先级 | 方向 | 预期改进 | 论文价值 |
|--------|------|----------|----------|
| P0 | MoE Expert Prefetch | llama decode +30-50% | 最高 |
| P1 | GNN Batch Proactive | 逼近应用级 prefetch | 高 |
| P2 | 接口扩展 + A/B 实证 | 5-25% + 系统贡献 | 高 |
| P3 | vLLM KV-cache aware | 增量优化 | 中 |

---

## Appendix: 原始数据位置

- 原始 plan: `docs/cross_block_prefetch_plan.md` (1809 行完整历史)
- 机制设计: `docs/cross_block_prefetch_mechanism.md`
- 通用 XB 策略: `extension/prefetch_cross_block_v2.bpf.c`
- FAISS phase: `extension/prefetch_faiss_phase.bpf.c`, `prefetch_faiss_uprobe.bpf.c`
- llama phase: `extension/prefetch_llama_phase.bpf.c`
- vLLM phase: `extension/prefetch_vllm_phase.bpf.c`, `prefetch_vllm_phase_transparent.bpf.c`
- GNN proactive: `extension/prefetch_gnn_proactive.bpf.c`
- Multi-block: `extension/prefetch_stride_multiblock.bpf.c`
- MoE expert: `extension/prefetch_moe_expert.bpf.c`
- FAISS results: `workloads/faiss/results/exp_xb4/`
- vLLM rerun results: `workloads/vllm/results/exp_vllm_rerun/`
- GNN results: `workloads/pytorch/result/`
- Microbench results: `microbench/memory/results/exp_xb5/`
- Phase detection results: `workloads/results_phase_detection/20260305_run/`
- Retest (gpu_block_access fix): `docs/retest_plan_gpu_block_access_fix.md`
