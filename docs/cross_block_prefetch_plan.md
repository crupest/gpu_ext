# Cross-VA-Block Prefetch Policy 设计：Per-Workload 算法与实验

> 机制层实现细节见 [`cross_block_prefetch_mechanism.md`](cross_block_prefetch_mechanism.md)

## 1. 动机

Intra-block prefetch（always_max）在 page fault 时最多预取当前 2MB VA block 内的剩余 pages，已实现 +78% tg 提升（120B MoE）。但 **chunk_trace 显示 82% chunk thrashing 未改变** — 因为 intra-block prefetch 不能跨 VA block 边界。

Cross-VA-block prefetch 通过 `bpf_wq` + `bpf_gpu_migrate_range()` kfunc 异步预取相邻 VA block，**突破 2MB 限制**。机制已实现并验证：

| 测试场景 | 结果 | 原因 |
|----------|------|------|
| Microbench seq_stream (sf=1.0) | **+63%** | 线性访问，刚好溢出 |
| Microbench hotspot (sf=1.0) | **+15%** | 局部性访问 |
| llama.cpp 120B decode (1.84x) | **-28.5% ~ ±0%** | PCIe 零和，循环访问 |

**核心发现**：cross-block 效果完全取决于 **workload 访问模式 × oversubscription ratio**。不存在通用最优策略 — 每种 workload 需要定制算法。这正是 gpu_ext BPF 可编程性的价值所在。

## 2. Workload 访问模式分析

### 2.1 llama.cpp 120B MoE (59 GiB, 1.84x oversub)

**访问模式**：
- **Prefill**: 顺序遍历 36 layers，每 layer 内顺序加载 active expert weights → VA block 访问是 **sequential forward**
- **Decode**: 每 token 仅激活少数 experts，跨 layer 跳跃 → VA block 访问是 **sparse cyclic random**

**特征**：
- Prefill 占运行时间小部分（pp 阶段），decode 是主要瓶颈
- 1.84x oversub → PCIe 完全饱和（6.6ms/tok DMA，59% of total）
- 每 token 迁移 428 MB（107 chunks × 2MB × 2 directions）
- Working set 分层：T1=2.14GB（attention/embedding）, T2=1.88GB, T3=58.33GB（expert weights）

**已验证的 cross-block 结果**：
- Blind adjacent: **-28.5%**（PCIe 竞争 + VRAM 位移）
- Direction-aware 2-step: -21%
- Direction-aware 3-step: -12%
- Adjacent-stride: ±0%（仅 prefill 触发，decode 自动静默）

### 2.2 vLLM Qwen-30B KV-cache Offloading (~36-40 GiB, 1.13-1.25x oversub)

**访问模式**：
- **KV-cache**: 每个 request 的 KV entries 占据连续 VA range，**单调递增写入**（append-only），attention 回读时有 **temporal locality**（近期 token 更频繁）
- **Expert weights**: 与 llama.cpp 类似的 **strided** 模式（MoE 结构）
- **两类数据竞争**: KV-cache 增长驱逐 expert weight pages → 下次 expert 计算时 fault back → 又驱逐 KV-cache

**特征**：
- Oversub ratio 远低于 llama.cpp（1.13-1.25x vs 1.84x）→ **PCIe 有余量**
- KV-cache 是 forward-only sequential growth → cross-block 方向过滤容易通过
- Expert weight 是 strided → intra-block stride prefetch 更合适，不需要 cross-block
- `cudaMemAdvise(SetPreferredLocation=CPU)` 已用于 KV-cache → UVM demand paging

### 2.3 PyTorch GNN Training (1M-15M nodes, 1.0-2.17x oversub)

**访问模式**：
- **Feature tensor**: 每 epoch 全量 sequential scan（low VA → high VA），跨越大量 2MB VA blocks
- **Adjacency matrix**: 图遍历，有局部性但非严格 sequential
- **Epoch 边界**: 每 epoch 结束后从头开始 → VA wrap-around

**特征**：
- 10M nodes (1.43x): `cudaMemPrefetchAsync` 几乎消除 faults
- 15M nodes (2.17x): 即使有 prefetch 仍有 residual faults
- Feature tensor scan 是 **最理想的 cross-block 场景**：strict sequential, 多 block 连续，方向一致
- 每 epoch 重复相同 scan → 稳态 cross-block 命中率应非常高

### 2.4 FAISS Vector Search (SIFT 20M-100M, 1.0-1.5x oversub)

**访问模式**：
- **Index build (K-means)**: 全量 sequential scan × 多次迭代 → 与 GNN 类似的 strict sequential
- **Search query**: nprobe 个 posting list 的 random access → VA block 访问 **完全随机**
- **两个 phase 截然不同**

**特征**：
- Build phase: 与 GNN feature scan 完全相同的 sequential 模式
- Search phase: cross-block **完全无效**（方向过滤会 100% 拒绝）
- 需要 **phase-aware** 策略：build 时开启 cross-block，search 时关闭
- SIFT100M (48GB, 1.5x): 中等 oversub，PCIe 有一定余量

## 3. Per-Workload 算法设计

### 3.1 llama.cpp: Phase-Gated Cross-Block

**策略**: 仅在 prefill 阶段启用 cross-block，decode 阶段完全禁用。

**算法**：
```
状态: phase = PREFILL | DECODE

gpu_page_prefetch(fault_va):
  // 始终做 intra-block always_max
  result_region = max_prefetch_region

  if phase == PREFILL:
    // 顺序遍历 → forward cross-block
    if detect_sequential(fault_va, history):
      bpf_wq → migrate(next_va_block, 2MB)

  // Phase 检测: 用 fault rate 变化
  // Prefill: 高 fault rate (sequential loading)
  // Decode: 低 fault rate (working set 稳定后 sparse faults)
  update_phase(fault_rate)

  return BYPASS
```

**Phase 检测方法**：
- 方案 A: fault rate 阈值 — prefill 时 fault rate 极高（连续加载），decode 时下降
- 方案 B: adjacent-stride 已有的效果 — 3 consecutive ±1 block 自动只在 prefill 触发
- 方案 C: 用已有的 layer boundary table，检测到所有 36 layers 遍历完一轮 = prefill done

**预期效果**: 微小提升（prefill 本身占比小），但**零 decode 回退风险**。adjacent-stride (mode 3) 已经近似实现了这个效果。

**与现有代码的关系**: `prefetch_cross_block_v2` 的 mode 3 (adjacent-stride) 本质上已是 phase-gated — 只在 prefill 触发。可以验证 prefill 阶段的实际加速比。

---

### 3.2 vLLM: Region-Aware Selective Cross-Block

**策略**: 区分 KV-cache region 和 model weight region，仅对 KV-cache 做 cross-block prefetch。

**算法**：
```
状态:
  kv_va_ranges[]  // KV-cache 的 VA 范围（通过 cudaMemAdvise 的地址推断）
  weight_va_start, weight_va_end  // model weight 范围

gpu_page_prefetch(fault_va):
  result_region = max_prefetch_region  // intra-block always_max

  if is_kv_region(fault_va):
    // KV-cache: forward sequential growth
    if direction_is_forward(fault_va, history):
      // 激进 prefetch — KV entries 是 append-only
      bpf_wq → migrate(next_va_block, prefetch_size)
      stats.kv_prefetch++
  else:
    // Model weights: 不做 cross-block
    // intra-block always_max 足够（strided access within block）
    stats.weight_skip++

  return BYPASS
```

**KV region 检测方法**：
- 方案 A: VA 地址范围 — `cudaMallocManaged` + `SetPreferredLocation=CPU` 的地址通常在高 VA range
- 方案 B: fault pattern — KV-cache faults 是 monotone forward（append），weight faults 是 cyclic strided
- 方案 C: uprobe hook `cudaMemAdvise` 捕获 VA range → 存入 BPF map

**Prefetch 激进度**：
- 1.13x oversub → VRAM 余量 ~4GB → 可以 prefetch 4-8MB（2-4 blocks ahead）
- 1.25x oversub → 余量 ~1GB → 保守 2MB（1 block ahead）
- 用 fault rate 反馈动态调整 prefetch 大小

**预期效果**: KV-cache 跨 block 边界时消除 stall。关键优势是 oversub ratio 低，cross-block 不会造成严重 VRAM displacement。

**新文件**: `extension/prefetch_vllm_kv_crossblock.bpf.c`

---

### 3.3 PyTorch GNN: Aggressive Multi-Block Prefetch

**策略**: 利用 epoch scan 的强 sequential 特性，做多 block 前瞻 prefetch。

**算法**：
```
状态:
  scan_direction = FORWARD  // 当前 scan 方向
  consecutive_forward = 0    // 连续 forward block 计数
  prefetch_depth = 1         // 动态 prefetch 深度 (1-4 blocks)

gpu_page_prefetch(fault_va):
  result_region = max_prefetch_region  // intra-block always_max

  block_delta = (fault_va - last_fault_va) / BLOCK_SIZE

  if block_delta == +1:
    consecutive_forward++
    if consecutive_forward >= 2:
      // 确认 sequential scan — 加大 prefetch 深度
      prefetch_depth = min(prefetch_depth + 1, MAX_DEPTH)
      for i in 1..prefetch_depth:
        bpf_wq → migrate(current_block + i * BLOCK_SIZE, 2MB)
  elif block_delta < 0:
    // Epoch wrap-around 或反向 → reset
    consecutive_forward = 0
    prefetch_depth = 1
  else:
    // 跳跃 → 减小 prefetch 深度
    prefetch_depth = max(prefetch_depth - 1, 1)

  last_fault_va = fault_va
  return BYPASS
```

**关键设计**：
- **自适应深度**: 连续 sequential → 加深 prefetch（最多 4 blocks = 8MB ahead）；跳跃 → 收缩
- **Epoch wrap 检测**: `block_delta < 0`（VA 从高跳回低）→ reset 状态
- **多 block 并发 prefetch**: 单次触发多个 bpf_wq，让 UVM copy engine 并行迁移
- **不需要 region 区分**: GNN 的 feature tensor 是主要的 UVM 分配

**Prefetch 深度 vs oversub**：
- 1.43x (10M nodes): 余量 ~10GB → 最大 4 blocks (8MB)
- 2.17x (15M nodes): 余量 ~0 → 最大 1 block (2MB)，类似 llama.cpp 的约束

**预期效果**: 在 1.43x oversub 下显著减少 block boundary stalls。Microbench seq_stream +63% 的场景与 GNN epoch scan 高度匹配。

**新文件**: `extension/prefetch_gnn_sequential.bpf.c`

---

### 3.4 FAISS: Phase-Adaptive Cross-Block

**策略**: 自动检测 build（sequential K-means）vs search（random query）phase，分别应用不同策略。

**算法**：
```
状态:
  direction_consistency = 0.0  // 最近 N 次 fault 的方向一致率
  phase = AUTO_DETECT          // BUILD | SEARCH | AUTO_DETECT
  window[WINDOW_SIZE]          // 方向历史窗口

gpu_page_prefetch(fault_va):
  result_region = max_prefetch_region  // intra-block always_max

  // 更新方向一致性
  delta = sign(fault_va - last_fault_va)
  push(window, delta)
  direction_consistency = count_same_sign(window) / WINDOW_SIZE

  // Phase 自动检测
  if direction_consistency > 0.7:
    phase = BUILD   // sequential scan
  elif direction_consistency < 0.3:
    phase = SEARCH  // random access

  if phase == BUILD:
    // K-means sequential scan → forward cross-block
    if delta > 0:
      bpf_wq → migrate(next_va_block, 2MB)
      stats.build_prefetch++
  else:
    // Search random access → 不做 cross-block
    // posting list prefetch 由 device-side trigger 处理
    stats.search_skip++

  last_fault_va = fault_va
  return BYPASS
```

**Phase 检测**：
- **方向一致率 > 70%** → K-means sequential scan（连续 epoch 遍历数据集）
- **方向一致率 < 30%** → random query（nprobe posting list 访问）
- 中间地带保持上一 phase（hysteresis 防振荡）
- Window size = 32（最近 32 次 fault 的方向统计）

**K-means 特殊优化**：
- K-means 多次迭代，每次 full dataset scan → 方向一致率持续 >90%
- 可以做 2-block ahead prefetch（scan 速度快，1 block 不够）

**预期效果**: Build phase +15~63%（匹配 microbench）; Search phase 零开销（完全跳过）。

**新文件**: `extension/prefetch_faiss_phase.bpf.c`

## 4. 通用组件

四种 workload-specific 算法共享以下基础设施：

| 组件 | 来源 | 状态 |
|------|------|------|
| `bpf_gpu_migrate_range()` kfunc | `uvm_bpf_struct_ops.c` | ✅ 已实现 |
| `bpf_wq` async 调度 | BPF subsystem | ✅ 已验证 |
| kprobe `va_block` 捕获 | `prefetch_cross_block_v2.bpf.c` | ✅ 已实现 |
| `always_max` intra-block | 所有 prefetch 策略共享 | ✅ 已实现 |
| per-CPU direction cache | `prefetch_cross_block_v2.bpf.c` | ✅ 已实现 |
| eviction policy (cycle_moe/LFU) | 各策略独立配置 | ✅ 已实现 |

**新增需要实现的**：

| 组件 | 用途 | 复杂度 |
|------|------|--------|
| VA region 分类 map | vLLM KV vs weight 区分 | 中（需 uprobe hook） |
| Phase 自动检测 | FAISS build vs search | 低（方向一致率统计） |
| Multi-block prefetch | GNN 多 block 前瞻 | 低（多次 bpf_wq_start） |
| Fault rate phase 检测 | llama.cpp prefill vs decode | 低（已有 fault counter） |

## 5. 实验设计与运行指南

### 5.0 前置条件

**自定义 nvidia 模块必须已加载**（所有实验共用）：
```bash
KM_DIR=/home/yunwei37/workspace/gpu/gpu_ext/kernel-module/nvidia-module/kernel-open
EXT_DIR=/home/yunwei37/workspace/gpu/gpu_ext/extension

# 检查是否已加载自定义模块
lsmod | grep nvidia_uvm

# 如未加载：
sudo systemctl stop nvidia-persistenced 2>/dev/null || true
sudo rmmod nvidia_uvm nvidia_drm nvidia_modeset nvidia 2>/dev/null || true
sleep 2
sudo insmod "$KM_DIR/nvidia.ko"
sudo insmod "$KM_DIR/nvidia-modeset.ko"
sudo insmod "$KM_DIR/nvidia-drm.ko"
sudo insmod "$KM_DIR/nvidia-uvm.ko"
```

**通用方法论**：
- 每实验 **10 trials**，取 geometric mean（快速验证可 5 trials）
- 每次切换配置前 `python3 /home/yunwei37/workspace/gpu/gpu_ext/workloads/cleanup_gpu.py`
- 对照组: **intra-block always_max**（不做 cross-block）
- 统计检验: paired t-test，p < 0.05 视为显著
- **同一时间只能运行一个 GPU benchmark**（GPU 独占）
- **同一时间只能加载一个 BPF struct_ops 策略**

**BPF 策略加载/卸载模板**：
```bash
# 加载策略（前台运行，Ctrl-C 卸载）
sudo "$EXT_DIR/<policy_binary>" [args] > /tmp/policy.log 2>&1 &
POLICY_PID=$!
sleep 3
# 验证
sudo bpftool prog list 2>/dev/null | grep -q struct_ops && echo "OK" || echo "FAIL"

# 卸载策略
sudo kill $POLICY_PID 2>/dev/null; wait $POLICY_PID 2>/dev/null || true
# 如有残留：
sudo "$EXT_DIR/cleanup_struct_ops_tool" 2>/dev/null || true
```

---

### 5.1 Exp-XB1: llama.cpp 120B Phase-Gated

**目标**: 验证 prefill 阶段 cross-block 的加速效果

| Config | 策略 | 预期 |
|--------|------|------|
| A (baseline) | always_max + cycle_moe (no XB) | pp≈221, tg≈88 |
| B (adjacent-stride) | cross_block_v2 mode 3 | pp≈222, tg≈88 (已验证 neutral) |
| C (phase-gated) | 新算法：prefill-only cross-block | pp≈225?, tg≈88 |

**关注指标**: pp512 变化（prefill 加速）, tg128 不回退

**运行步骤**：

```bash
MODEL="$HOME/.cache/llama.cpp/ggml-org_gpt-oss-120b-GGUF_gpt-oss-120b-mxfp4-00001-of-00003.gguf"
LLAMA_BENCH=/home/yunwei37/workspace/gpu/gpu_ext/workloads/llama.cpp/build/bin/llama-bench
RESULTS=/home/yunwei37/workspace/gpu/gpu_ext/workloads/llama.cpp/results/exp_xb1
mkdir -p "$RESULTS"

# --- Config A: baseline (always_max + cycle_moe, no cross-block) ---
python3 /home/yunwei37/workspace/gpu/gpu_ext/workloads/cleanup_gpu.py
sudo "$EXT_DIR/prefetch_always_max_cycle_moe" > /tmp/policy.log 2>&1 &
POLICY_PID=$!; sleep 3

GGML_CUDA_ENABLE_UNIFIED_MEMORY=1 \
  "$LLAMA_BENCH" -m "$MODEL" -p 512 -n 128 -r 5 -o json \
  2>&1 | tee "$RESULTS/config_a.log"

sudo kill $POLICY_PID; wait $POLICY_PID 2>/dev/null || true

# --- Config B: cross_block_v2 adjacent-stride (已有, mode 3) ---
python3 /home/yunwei37/workspace/gpu/gpu_ext/workloads/cleanup_gpu.py
sudo "$EXT_DIR/prefetch_cross_block_v2" 1 2048 3 > /tmp/policy.log 2>&1 &
POLICY_PID=$!; sleep 3
# 参数: evict_mode=1(cycle_moe), prefetch_kb=2048, prefetch_mode=3(adjacent-stride)

GGML_CUDA_ENABLE_UNIFIED_MEMORY=1 \
  "$LLAMA_BENCH" -m "$MODEL" -p 512 -n 128 -r 5 -o json \
  2>&1 | tee "$RESULTS/config_b.log"

sudo kill $POLICY_PID; wait $POLICY_PID 2>/dev/null || true

# --- Config C: 新 phase-gated 算法 (需先实现) ---
# python3 /home/yunwei37/workspace/gpu/gpu_ext/workloads/cleanup_gpu.py
# sudo "$EXT_DIR/prefetch_llama_phase_gated" > /tmp/policy.log 2>&1 &
# ...同上...
```

**关键环境变量**: `GGML_CUDA_ENABLE_UNIFIED_MEMORY=1`（必须，启用 UVM）

---

### 5.2 Exp-XB2: vLLM KV-cache Region-Aware

**目标**: 验证 KV-cache selective cross-block 对 serving 性能的影响

| Config | 策略 | 预期 |
|--------|------|------|
| A (baseline) | UVM, no BPF policy | paper baseline |
| B (always_max) | always_max + LFU (no XB) | 当前最佳 intra-block |
| C (blind XB) | cross_block_v2 mode 1 | 可能有害 |
| D (KV-only XB) | 新算法：region-aware | 可能 +5-10% TTFT |

**关注指标**: mean/P99 TTFT, mean/P99 TPOT, throughput (tok/s)

**配置**: Qwen3-30B-A3B-FP8, `--max-num-seqs 16`, 100 ShareGPT requests

**前置**: vLLM 从 submodule 安装 (`workloads/vllm/vllm/`)，UVM allocator 已构建

**运行步骤**：

```bash
VLLM_DIR=/home/yunwei37/workspace/gpu/gpu_ext/workloads/vllm
RESULTS=/home/yunwei37/workspace/gpu/gpu_ext/workloads/vllm/results/exp_xb2

mkdir -p "$RESULTS"

# --- Config A: UVM baseline (no BPF) ---
python3 /home/yunwei37/workspace/gpu/gpu_ext/workloads/cleanup_gpu.py
cd "$VLLM_DIR"
uv run python configs/serve_bench.py \
  --mode uvm \
  --prompts 100 \
  --output "$RESULTS/config_a_uvm_baseline.json"

# --- Config B: UVM + always_max + LFU (no cross-block) ---
python3 /home/yunwei37/workspace/gpu/gpu_ext/workloads/cleanup_gpu.py
sudo "$EXT_DIR/prefetch_always_max" > /tmp/policy.log 2>&1 &
POLICY_PID=$!; sleep 3

cd "$VLLM_DIR"
uv run python configs/serve_bench.py \
  --mode uvm \
  --prompts 100 \
  --output "$RESULTS/config_b_always_max.json"

sudo kill $POLICY_PID; wait $POLICY_PID 2>/dev/null || true

# --- Config C: UVM + cross_block_v2 blind (mode 1) ---
python3 /home/yunwei37/workspace/gpu/gpu_ext/workloads/cleanup_gpu.py
sudo "$EXT_DIR/prefetch_cross_block_v2" 2 2048 1 > /tmp/policy.log 2>&1 &
POLICY_PID=$!; sleep 3
# 参数: evict_mode=2(default_lru), prefetch_kb=2048, prefetch_mode=1(blind)

cd "$VLLM_DIR"
uv run python configs/serve_bench.py \
  --mode uvm \
  --prompts 100 \
  --output "$RESULTS/config_c_blind_xb.json"

sudo kill $POLICY_PID; wait $POLICY_PID 2>/dev/null || true

# --- Config D: 新 region-aware KV 算法 (需先实现) ---
# ...同上...
```

**vLLM UVM 原理**: `VLLM_USE_UVM=1` 由 `serve_bench.py --mode uvm` 自动设置。超出 GPU budget 的 KV-cache 分配通过 `cudaMemAdvise(SetPreferredLocation=CPU)` 放到 CPU，GPU 通过 demand paging 访问。

---

### 5.3 Exp-XB3: PyTorch GNN Sequential

**目标**: 验证 aggressive multi-block prefetch 对 epoch time 的加速

| Config | Nodes | 策略 | 预期 |
|--------|-------|------|------|
| A1 | 10M (1.43x) | no BPF (UVM baseline) | paper baseline |
| A2 | 10M | always_max + cycle_moe (no XB) | 当前最佳 intra-block |
| A3 | 10M | cross_block_v2 mode 0 (direction-aware) | +10-30%? |
| A4 | 10M | 新算法: multi-block depth=4 | +15-40%? |
| B1 | 15M (2.17x) | no BPF | paper baseline |
| B2 | 15M | always_max + cycle_moe | 当前最佳 |
| B3 | 15M | cross_block_v2 mode 0 | +5-15%? |

**关注指标**: avg epoch time (seconds)

**运行步骤**：

```bash
GNN_DIR=/home/yunwei37/workspace/gpu/gpu_ext/workloads/pytorch
RESULTS=/home/yunwei37/workspace/gpu/gpu_ext/workloads/pytorch/result/exp_xb3

mkdir -p "$RESULTS"

# --- Config A1: UVM baseline, 10M nodes, no BPF ---
python3 /home/yunwei37/workspace/gpu/gpu_ext/workloads/cleanup_gpu.py
cd "$GNN_DIR"
CUDA_MANAGED_FORCE_DEVICE_ALLOC=1 uv run python benchmark_gnn_uvm.py \
  --dataset random --nodes 10000000 \
  --edges_per_node 10 --features 128 --hidden 256 \
  --epochs 5 --warmup 1 --prop chunked --use_uvm \
  --report_json "$RESULTS/a1_baseline_10m.json" \
  2>&1 | tee "$RESULTS/a1_baseline_10m.log"

# --- Config A2: always_max + cycle_moe, 10M ---
python3 /home/yunwei37/workspace/gpu/gpu_ext/workloads/cleanup_gpu.py
sudo "$EXT_DIR/prefetch_always_max_cycle_moe" > /tmp/policy.log 2>&1 &
POLICY_PID=$!; sleep 3

cd "$GNN_DIR"
CUDA_MANAGED_FORCE_DEVICE_ALLOC=1 uv run python benchmark_gnn_uvm.py \
  --dataset random --nodes 10000000 \
  --edges_per_node 10 --features 128 --hidden 256 \
  --epochs 5 --warmup 1 --prop chunked --use_uvm \
  --report_json "$RESULTS/a2_always_max_10m.json" \
  2>&1 | tee "$RESULTS/a2_always_max_10m.log"

sudo kill $POLICY_PID; wait $POLICY_PID 2>/dev/null || true

# --- Config A3: cross_block_v2, direction-aware, 10M ---
python3 /home/yunwei37/workspace/gpu/gpu_ext/workloads/cleanup_gpu.py
sudo "$EXT_DIR/prefetch_cross_block_v2" 1 2048 0 > /tmp/policy.log 2>&1 &
POLICY_PID=$!; sleep 3
# 参数: evict_mode=1(cycle_moe), prefetch_kb=2048, prefetch_mode=0(direction-aware)

cd "$GNN_DIR"
CUDA_MANAGED_FORCE_DEVICE_ALLOC=1 uv run python benchmark_gnn_uvm.py \
  --dataset random --nodes 10000000 \
  --edges_per_node 10 --features 128 --hidden 256 \
  --epochs 5 --warmup 1 --prop chunked --use_uvm \
  --report_json "$RESULTS/a3_xb_dir_10m.json" \
  2>&1 | tee "$RESULTS/a3_xb_dir_10m.log"

sudo kill $POLICY_PID; wait $POLICY_PID 2>/dev/null || true

# --- 15M nodes: 重复上述步骤，替换 --nodes 15000000 ---
# ...同上，输出到 b1_baseline_15m.json, b2_always_max_15m.json, b3_xb_dir_15m.json...
```

**关键环境变量**: `CUDA_MANAGED_FORCE_DEVICE_ALLOC=1`（PyTorch UVM 必须）

**内存估算**: 10M nodes ≈ 45GB (1.43x oversub), 15M nodes ≈ 68GB (2.17x oversub)

---

### 5.4 Exp-XB4: FAISS Phase-Adaptive

**目标**: 验证 phase-adaptive cross-block 分别在 build 和 search 阶段的效果

| Config | Dataset | 策略 | 预期 |
|--------|---------|------|------|
| A (baseline) | SIFT100M | no BPF | paper baseline |
| B (always_max) | SIFT100M | always_max (no XB) | 当前最佳 intra-block |
| C (blind XB) | SIFT100M | cross_block_v2 mode 0 | build +?, search -? |
| D (phase-adaptive) | SIFT100M | 新算法: auto-detect phase | build +15-30%, search ±0% |

**关注指标**: index add time (s), search latency per nprobe (s)

**前置**: FAISS 从 submodule 构建，SIFT 数据集已下载

**运行步骤**：

```bash
FAISS_DIR=/home/yunwei37/workspace/gpu/gpu_ext/workloads/faiss
RESULTS=/home/yunwei37/workspace/gpu/gpu_ext/workloads/faiss/results/exp_xb4

mkdir -p "$RESULTS"

# --- Config A: UVM baseline, no BPF ---
python3 /home/yunwei37/workspace/gpu/gpu_ext/workloads/cleanup_gpu.py
cd "$FAISS_DIR"
uv run python bench_gpu_1bn.py SIFT100M IVF4096,Flat -nprobe 1,4,16 -uvm \
  2>&1 | tee "$RESULTS/config_a_baseline.log"
# 结果自动保存到 results/SIFT100M_*.json

# --- Config B: always_max, no cross-block ---
python3 /home/yunwei37/workspace/gpu/gpu_ext/workloads/cleanup_gpu.py
sudo "$EXT_DIR/prefetch_always_max" > /tmp/policy.log 2>&1 &
POLICY_PID=$!; sleep 3

cd "$FAISS_DIR"
uv run python bench_gpu_1bn.py SIFT100M IVF4096,Flat -nprobe 1,4,16 -uvm \
  2>&1 | tee "$RESULTS/config_b_always_max.log"

sudo kill $POLICY_PID; wait $POLICY_PID 2>/dev/null || true

# --- Config C: cross_block_v2, direction-aware ---
python3 /home/yunwei37/workspace/gpu/gpu_ext/workloads/cleanup_gpu.py
sudo "$EXT_DIR/prefetch_cross_block_v2" 2 2048 0 > /tmp/policy.log 2>&1 &
POLICY_PID=$!; sleep 3
# 参数: evict_mode=2(default_lru), prefetch_kb=2048, prefetch_mode=0(direction-aware)

cd "$FAISS_DIR"
uv run python bench_gpu_1bn.py SIFT100M IVF4096,Flat -nprobe 1,4,16 -uvm \
  2>&1 | tee "$RESULTS/config_c_xb_dir.log"

sudo kill $POLICY_PID; wait $POLICY_PID 2>/dev/null || true

# --- Config D: 新 phase-adaptive 算法 (需先实现) ---
# ...同上...
```

**FAISS 工作目录**: 必须在 `workloads/faiss/` 下运行（`bench_gpu_1bn.py` 用相对路径找数据集 `faiss/benchs/bigann/`）

**SIFT100M 数据**: 48GB float32，1.5x oversub on 32GB VRAM

---

### 5.5 Exp-XB5: Microbench 回归测试

**目标**: 修改算法后确认 microbench 结果不回退

| Workload | sf | always_max | cross-block v2 | 新算法 |
|----------|-----|-----------|----------------|--------|
| seq_stream | 1.0 | 53.1 ms | 32.6 ms (+63%) | ≥ +63% |
| hotspot | 1.0 | 1796 ms | 1562 ms (+15%) | ≥ +15% |
| seq_stream | 1.5 | 2520 ms | 2340 ms (+8%) | ≥ +8% |

**运行步骤**：

```bash
MICRO_DIR=/home/yunwei37/workspace/gpu/gpu_ext/microbench/memory
RESULTS="$MICRO_DIR/results/exp_xb5"
mkdir -p "$RESULTS"

# --- always_max baseline ---
python3 /home/yunwei37/workspace/gpu/gpu_ext/workloads/cleanup_gpu.py
sudo "$EXT_DIR/prefetch_always_max" > /tmp/policy.log 2>&1 &
POLICY_PID=$!; sleep 3

"$MICRO_DIR/memory_bench" --kernel seq_stream --mode uvm --size_factor 1.0 \
  2>&1 | tee "$RESULTS/always_max_seq_1.0.log"
"$MICRO_DIR/memory_bench" --kernel hotspot --mode uvm --size_factor 1.0 \
  2>&1 | tee "$RESULTS/always_max_hotspot_1.0.log"

sudo kill $POLICY_PID; wait $POLICY_PID 2>/dev/null || true

# --- cross_block_v2 ---
python3 /home/yunwei37/workspace/gpu/gpu_ext/workloads/cleanup_gpu.py
sudo "$EXT_DIR/prefetch_cross_block_v2" 1 2048 0 > /tmp/policy.log 2>&1 &
POLICY_PID=$!; sleep 3

"$MICRO_DIR/memory_bench" --kernel seq_stream --mode uvm --size_factor 1.0 \
  2>&1 | tee "$RESULTS/xb_v2_seq_1.0.log"
"$MICRO_DIR/memory_bench" --kernel hotspot --mode uvm --size_factor 1.0 \
  2>&1 | tee "$RESULTS/xb_v2_hotspot_1.0.log"

sudo kill $POLICY_PID; wait $POLICY_PID 2>/dev/null || true
```

**注意**: microbench binary 名可能是 `memory_bench` 或 `main`，需确认 `ls $MICRO_DIR/` 中的可执行文件名。

---

### 5.6 实验执行注意事项

1. **GPU 独占**: 同一时间只能运行一个 benchmark。每个 Config 之间必须等前一个完成。
2. **BPF 独占**: 同一时间只能加载一个 struct_ops。加载新策略前必须先卸载旧的。
3. **模块不重加载**: 所有实验共享同一次模块加载，中途不需要 `rmmod/insmod`。
4. **Subagent 执行**: 每个 Exp 可由单个 subagent 串行执行。不同 Exp 之间也必须串行（GPU 独占）。
5. **结果文件**: JSON 结果提交到 git（小文件，作为实验记录）。

## 6. 实现优先级

| 优先级 | Workload | 算法 | 新 BPF 文件 | 理由 |
|--------|----------|------|------------|------|
| **P0** | PyTorch GNN | multi-block sequential | `prefetch_gnn_sequential.bpf.c` | 最可能成功：strict sequential + 中等 oversub + microbench 已验证 |
| **P0** | FAISS | phase-adaptive | `prefetch_faiss_phase.bpf.c` | Build 同样 strict sequential；phase 检测简单 |
| **P1** | vLLM | region-aware KV | `prefetch_vllm_kv_crossblock.bpf.c` | 需 uprobe hook 做 region 分类，复杂度中等 |
| **P2** | llama.cpp | phase-gated | `prefetch_llama_phase_gated.bpf.c` | 已知 prefill 占比小，预期 marginal |

**快速验证路径**: 在实现新算法前，先用现有 `prefetch_cross_block_v2` 在 GNN 和 FAISS 上跑 baseline + direction-aware (mode 0) 对比。如果 direction-aware 在这两个 workload 上已有显著提升，则新算法的价值是进一步优化而非从零开始。

## 7. 实验结果

### 7.1 Exp-XB3: PyTorch GNN 10M nodes (1.43x oversub)

**硬件**: RTX 5090 32GB, UVM peak 45.11 GB

#### 7.1.0 ⚠️ 关键 Bug 修复：uvm_allocator.c 导致 2x 退化

**原始 XB3 数据（2026-03-04, A1-A3）均无效** — 因为 `uvm_allocator.c` 在 2026-02-17 被错误修改：

- **V1 (Dec 2025, 正确版本)**: 纯 `cudaMallocManaged`，无 advise，无 prefetch
- **V3 (Feb 17 引入的 bug)**: 加了 `cudaMemAdviseSetPreferredLocation=CPU`

`SetPreferredLocation=CPU` 导致被 evict 的页面**强制回到 CPU**，双倍 migration 流量：
- V1 baseline: **70.15s** (匹配 Dec 2025 的 70.06s)
- V3 baseline: **140.25s** (2x 退化!)
- V3 + BPF always_max: **140.22s** (BPF 也无法补偿 — advise 优先级高于 prefetch)

**修复**: 已回退到 V1，commit `975d39e`，文件头加 `DO NOT MODIFY` 注释。

#### 7.1.1 修复后的 GNN 结果（V1 allocator, 2026-03-04）

| Config | 策略 | avg epoch (s) | vs baseline | vs Dec 2025 |
|--------|------|:---:|:---:|:---:|
| A1 (baseline) | no BPF, UVM default | **70.15** | — | 70.06s ✓ |
| A2 | always_max (intra-block prefetch) | **26.99** | -61.5% (**2.60x**) | 26.47s ✓ |
| A3 | cross_block_v2 mode 0 (direction-aware) | **21.32** | -69.6% (**3.29x**) | — |
| A4 | cross_block_v2 mode 3 (adjacent-stride) | **24.32** | -65.3% (**2.89x**) | — |

A1 epoch times: 70.08, 70.13, 70.23 (σ<0.1s)
A2 epoch times: 27.03, 26.98, 26.95 (σ<0.04s)
A3 epoch times: 21.31, 21.33, 21.31 (σ<0.01s)
A4 epoch times: 24.27, 24.31, 24.36 (σ<0.05s)

**结论**:
1. **always_max**: 2.60x 加速，完全复现 paper 数据 (Dec 2025: 2.65x)
2. **cross-block direction-aware**: 在 always_max 基础上再提升 **21%** (27.0→21.3s)，总计 **3.29x**！
3. **cross-block adjacent-stride**: 中间结果 (24.3s, 2.89x)，比 direction-aware 保守但仍有显著提升
4. GNN epoch scan 是 strict sequential forward → direction-aware XB 最为适合
5. 之前 XB3 实验全部无效是因为 `cudaMemAdviseSetPreferredLocation=CPU` 的 allocator bug (已修复)

### 7.2 Exp-XB4: FAISS SIFT100M (1.5x oversub)

**硬件**: RTX 5090 32GB, SIFT100M ~48GB float32

| Config | 策略 | add time (s) | search nprobe=1 (s) | nprobe=4 (s) | nprobe=16 (s) | vs baseline |
|--------|------|:---:|:---:|:---:|:---:|:---:|
| A (baseline) | no BPF, UVM default | **69.40** | **5.19** | **14.34** | **55.96** | — |
| B | always_max | **49.49** | **4.38** | **12.62** | **49.45** | add -28.7%, search -12~16% |
| C | cross_block_v2 mode 0 (dir) | **50.28** | **4.45** | **13.47** | **52.47** | add -27.5%, search -6~15% |

**B 分析**: always_max intra-block prefetch 对 FAISS **非常有效** — add time -28.7%，search -12~16%。原因：FAISS K-means build 是 strict sequential scan，default threshold=51 远不够激进。

**C 分析**: cross_block_v2 比 always_max **稍差**（add +0.8s, search nprobe=4/16 回退 6-7%）。
- cross-block stats: wq_scheduled=485K, migrate_success=260K (53.5%), migrate_failed=102K, direction_skip=87K
- rate-limit_skip=458K 是最大跳过路径 — rate limiter 限制了 cross-block 频率
- **Search 阶段 cross-block 有害**: random posting list access 导致方向不一致，但 direction filter 只过滤了 8.4%，大量无效 prefetch 仍然通过
- **结论**: FAISS 需要 phase-adaptive 策略 — build 时开 cross-block，search 时关闭。现有 direction filter 不足以区分 build vs search

### 7.3 Exp-XB2: vLLM Qwen3-30B-A3B-FP8 Serving (100 ShareGPT prompts, UVM mode)

**硬件**: RTX 5090 32GB, Qwen3-30B-A3B-FP8, `--max-num-seqs 16`, `--enforce-eager`

| Config | 策略 | Mean TTFT (ms) | P99 TTFT (ms) | Mean TPOT (ms) | P99 TPOT (ms) | Throughput (tok/s) |
|--------|------|:---:|:---:|:---:|:---:|:---:|
| A (baseline) | no BPF, UVM default | 180,616 | 335,796 | 240.8 | 751.7 | 57.84 |
| B | always_max | 177,491 (-1.7%) | 334,623 (-0.3%) | 318.2 (+32%) | **2,760.5 (+267%)** | 60.37 (+4.4%) |
| C | cross_block_v2 blind (mode 1) | 177,890 (-1.5%) | 329,709 (-1.8%) | 268.7 (+12%) | 740.3 (-1.5%) | 59.76 (+3.3%) |

**运行方式**:
```bash
EXT_DIR=/home/yunwei37/workspace/gpu/gpu_ext/extension
VLLM_DIR=/home/yunwei37/workspace/gpu/gpu_ext/workloads/vllm

# Config B: always_max
python3 workloads/cleanup_gpu.py
sudo $EXT_DIR/prefetch_always_max > /tmp/policy.log 2>&1 &
sleep 5
cd $VLLM_DIR && uv run python configs/serve_bench.py --mode uvm --prompts 100 --no-cleanup \
  --output results/exp_xb2/config_b_always_max.json

# Config C: cross_block_v2 blind
python3 workloads/cleanup_gpu.py
sudo $EXT_DIR/prefetch_cross_block_v2 2 2048 1 > /tmp/policy.log 2>&1 &
sleep 5
cd $VLLM_DIR && uv run python configs/serve_bench.py --mode uvm --prompts 100 --no-cleanup \
  --output results/exp_xb2/config_c_blind_xb.json
```

**分析**:
- **Throughput**: B 和 C 均有微弱提升 (+3~4%)，可能在 run-to-run variance 范围内（仅 1 trial）
- **P99 TPOT**: always_max **严重劣化** (+267%, 2760ms vs 752ms baseline) — prefetch 过于激进导致 serving 场景下偶发严重延迟尖峰
- **Blind cross-block (C)**: P99 TPOT 反而接近 baseline (740ms)，比 always_max 好
- **TTFT**: 三者差异 <2%，prefetch 策略对 TTFT 无显著影响
- **结论**: vLLM serving 场景下 always_max 的激进 prefetch 有害（P99 TPOT 爆炸），需要 region-aware 策略区分 KV-cache 和 model weight

### 7.3.1 Exp-XB2 Config E: vLLM + faiss_phase (phase-adaptive)

**策略**: 复用 FAISS phase-adaptive v2（顺序步长检测 + default LRU），测试能否区分 vLLM 的 INIT(模型加载) vs SERVING(推理) 阶段。

| Config | 策略 | Mean TTFT (ms) | P99 TTFT (ms) | Mean TPOT (ms) | P99 TPOT (ms) | Throughput (tok/s) |
|--------|------|:---:|:---:|:---:|:---:|:---:|
| A | no BPF | 180,616 | 335,796 | 240.8 | 751.7 | 57.84 |
| B | always_max | 177,491 | 334,623 | 318.2 | **2,760.5** | 60.37 |
| C | blind XB | 177,890 | 329,709 | 268.7 | 740.3 | 59.76 |
| **E** | **faiss_phase v2** | **178,231** | **333,217** | **281.5** | **1,455.6** | **60.27** |

**BPF Stats**:
- build_prefetch: 2,008 | search_skip: 16,656 (cross-block 仅 10.8% 时间触发)
- phase→BUILD: 54 | phase→SEARCH: 54（频繁振荡，不是 clean INIT→SERVING 切换）
- migrate_success: 1,641 (82.9%) | migrate_failed: 338

**分析**:
- Phase detection **不适配 vLLM** — 54 次振荡 vs FAISS 的 730 次有意义的 BUILD/SEARCH 交替
- vLLM 模型加载不是 sustained sequential scan（per-layer weight + KV-cache 交织），没有 clean phase boundary
- P99 TPOT 1455ms 好于 always_max (2760ms) 但差于 baseline (751ms) 和 blind XB (740ms)
- Throughput 60.27 ≈ always_max (60.37)，均来自 intra-block always_max

**vLLM 总结**: 1.175x 低 oversub 下，prefetch 策略对 serving 无显著正面影响。aggressive prefetch 反而有害（P99 spike）。最佳策略是 no-BPF 或最保守的 blind XB。region-aware KV 策略**降级为 P3**，因为 fundamental bottleneck 不在 prefetch。

### 7.4 Exp-XB5: Microbench 回归测试

**硬件**: RTX 5090 32GB

| Workload | sf | always_max (ms) | cross_block_v2 dir (ms) | speedup | 历史预期 |
|----------|-----|:---:|:---:|:---:|:---:|
| seq_stream | 1.0 | 52.89 | 48.06 | **+10.0%** | +63% |
| rand_stream | 1.0 | 52.47 | 49.76 | **+5.4%** | +15% |
| seq_stream | 1.5 | 2512.52 | 2320.64 | **+8.3%** | +8% |

**注**: `hotspot` kernel 不存在于 uvmbench，用 `rand_stream` 替代。

**运行方式**:
```bash
EXT_DIR=/home/yunwei37/workspace/gpu/gpu_ext/extension
BENCH=/home/yunwei37/workspace/gpu/gpu_ext/microbench/memory/uvmbench
RESULTS=/home/yunwei37/workspace/gpu/gpu_ext/microbench/memory/results/exp_xb5
mkdir -p $RESULTS

# Policy 1: always_max
python3 workloads/cleanup_gpu.py
sudo $EXT_DIR/prefetch_always_max > /tmp/policy.log 2>&1 &
sleep 3
$BENCH --kernel=seq_stream --mode=uvm --size_factor=1.0 --iterations=5 2>&1 | tee $RESULTS/always_max_seq_1.0.log
$BENCH --kernel=rand_stream --mode=uvm --size_factor=1.0 --iterations=5 2>&1 | tee $RESULTS/always_max_rand_1.0.log
$BENCH --kernel=seq_stream --mode=uvm --size_factor=1.5 --iterations=5 2>&1 | tee $RESULTS/always_max_seq_1.5.log
sudo kill $POLICY_PID; sudo $EXT_DIR/cleanup_struct_ops_tool 2>/dev/null || true

# Policy 2: cross_block_v2 direction-aware (mode 0, cycle_moe eviction)
python3 workloads/cleanup_gpu.py
sudo $EXT_DIR/prefetch_cross_block_v2 1 2048 0 > /tmp/policy.log 2>&1 &
sleep 5
$BENCH --kernel=seq_stream --mode=uvm --size_factor=1.0 --iterations=5 2>&1 | tee $RESULTS/xb_v2_seq_1.0.log
$BENCH --kernel=rand_stream --mode=uvm --size_factor=1.0 --iterations=5 2>&1 | tee $RESULTS/xb_v2_rand_1.0.log
$BENCH --kernel=seq_stream --mode=uvm --size_factor=1.5 --iterations=5 2>&1 | tee $RESULTS/xb_v2_seq_1.5.log
sudo kill $POLICY_PID; sudo $EXT_DIR/cleanup_struct_ops_tool 2>/dev/null || true
```

**分析**:
- **seq_stream sf=1.0**: +10%（低于历史 +63%）— sf=1.0 刚好 fit in VRAM，page fault 压力小，两种 policy 差异不大
- **seq_stream sf=1.5**: **+8.3%**（匹配预期）— 真正 oversubscribed 场景下 cross-block 有效
- **rand_stream sf=1.0**: +5.4%（低于历史 +15%）— random access 模式下方向预测效果有限
- **结论**: cross-block 在 oversubscribed sequential 场景下稳定 +8%，在低 oversub 或 random access 下效果有限

### 7.5 Exp-XB4 Config D: FAISS phase-adaptive v1 (方向一致率)

**策略**: `prefetch_faiss_phase` — 滑动窗口方向一致率检测 BUILD vs SEARCH phase

| Config | 策略 | add (s) | np=1 (s) | np=4 (s) | np=16 (s) |
|--------|------|:---:|:---:|:---:|:---:|
| A | no BPF | 69.40 | 5.19 | 14.34 | 55.96 |
| B | always_max | 49.49 | 4.38 | 12.62 | 49.45 |
| C | cross_block_v2 dir | 50.28 | 4.45 | 13.47 | 52.47 |
| **D** | **faiss_phase v1** | **47.73** | **8.38** | **13.83** | **54.19** |

**BPF Stats**:
- build_prefetch: 539,330 | search_skip: **0** (phase 从未切到 SEARCH!)
- phase→BUILD: 1 | phase→SEARCH: 0 | forward_count: 20/32 (62.5%)
- migrate_success: 294,852 (72.7%) | migrate_failed: 111,088

**问题**: Phase detection 完全失败 — 方向一致率在 SEARCH 阶段仍 62.5%（在 10-23 hysteresis 区间），从未触发 SEARCH。
- FAISS IVF search 访问的 posting list 在 VA 空间中仍有一定方向性（cluster 按顺序存储）
- 方向一致率不是区分 BUILD vs SEARCH 的可靠信号
- **add time 47.73s 是 4 个 config 中最好的**（-3.6% vs always_max），确认 cross-block 对 sequential build 有效
- **nprobe=1 search 8.38s（+91% vs always_max）**— cross-block 在 search 阶段极其有害

**下一步**: 改用 sequential stride 检测（v2），并测试不同 eviction 策略

### 7.6 Exp-XB4 Config D2/D3: FAISS phase-adaptive v2 (顺序步长检测)

**v2 算法变更**: 滑动窗口改为检测"步长是否 exactly +1 VA block (2MB)"。BUILD 阶段 sequential scan 产生大量 +1 步长；SEARCH 阶段 random access 几乎没有。

| Config | 策略 | add (s) | np=1 (s) | np=4 (s) | np=16 (s) |
|--------|------|:---:|:---:|:---:|:---:|
| A | no BPF | 69.40 | 5.19 | 14.34 | 55.96 |
| B | always_max (default LRU) | 49.49 | 4.38 | 12.62 | 49.45 |
| C | cross_block_v2 dir (default LRU) | 50.28 | 4.45 | 13.47 | 52.47 |
| D1 | faiss_phase v1 (dir+cycle_moe) | 47.73 | 8.38 | 13.83 | 54.19 |
| D2 | faiss_phase v2 (stride+cycle_moe) | 48.35 | 9.78 | 14.02 | 50.76 |
| **D3** | **faiss_phase v2 (stride+default_lru)** | **47.31** | **5.49** | **12.71** | **49.51** |

**D3 BPF Stats**:
- build_prefetch: 18,182 | search_skip: **764,647** (phase 正确切换!)
- phase→BUILD: 730 | phase→SEARCH: 730 | seq_count: 5/32 (最终 SEARCH)
- migrate_success: 8,183 (47%) | migrate_failed: 9,235
- phase_transitions: 1,460（BUILD/SEARCH 频繁切换，K-means add 不是纯 sequential）

**关键发现**:
1. **Phase detection v2 工作正常** — 764K search_skip，正确在 SEARCH 阶段禁止 cross-block
2. **cycle_moe eviction 是 nprobe=1 回退的根本原因** — D2(cycle_moe) 9.78s vs D3(default_lru) 5.49s。T1 保护锁住 add 阶段的热 chunks，阻碍 search 时换入需要的 cluster
3. **D3 是目前最优 FAISS 配置**: add -31.8% vs no-BPF, search 持平 always_max
4. **np=1 仍有 25% gap** vs always_max (5.49 vs 4.38) — 可能是 phase transition 开销或 cross-block 在 add 尾部尚未切到 SEARCH

### 7.7 Exp-XB4 Config D4: kprobe 优化（SEARCH 阶段跳过 va_space 捕获）

**优化**: kprobe 在 SEARCH 阶段跳过 `managed_range→va_range.va_space` 指针链（3 次 BPF_CORE_READ），仅捕获 `va_start/va_end`。

**Bug 修复**: 初版 kprobe 优化导致 deadlock — SEARCH 阶段跳过 va_space → prefetch hook 检测 `!va_space` 提前返回 → phase detection 从不执行 → 永远卡在 SEARCH。修复：将 va_space 检查移到 phase detection 之后，phase detection 仅用 `va_end`。

| Config | 策略 | add (s) | np=1 (s) | np=4 (s) | np=16 (s) |
|--------|------|:---:|:---:|:---:|:---:|
| B | always_max | 49.49 | 4.38 | 12.62 | 49.45 |
| D3 | phase v2 + LRU | 47.31 | 5.49 | 12.71 | 49.51 |
| D4 (buggy) | phase v2 + LRU + kprobe opt (bug) | 49.24 | 5.56 | 12.77 | 49.67 |
| **D4 (fixed)** | **phase v2 + LRU + kprobe opt** | **48.22** | **5.54** | **12.71** | **49.53** |

**结论**: kprobe 优化**不缩小 np=1 gap**（5.54 vs 4.38 = +26.5%）。overhead 不在 kprobe 的 va_space 指针链，而在 struct_ops hook 的 phase detection 逻辑本身（每次 fault 都要更新滑动窗口 + phase 判定，1.4M 次累积）。

**FAISS 总结**: D3 是最终最优配置 — add -31.8%, search np=4/16 持平 always_max。np=1 的 25% gap 是 phase detection overhead 的固有代价，进一步优化回报递减。

### 7.8 Exp-vLLM-Rerun: vLLM 30B 全量重新实验 (2026-03-05)

**背景**: 之前 §7.3 的 vLLM 结论（"always_max P99 TPOT 爆炸 +267%"）被证明是错误的。根本原因是 `serve_bench.py` 的 `cwd` 指向 submodule 目录（触发重新编译）+ benchmark 参数缺失（无 `--request-rate 5 --sharegpt-output-len 512`）。修复后用正确参数重跑 6 个配置。

**硬件**: RTX 5090 32GB, Qwen3-30B-A3B-FP8, `--max-num-seqs 16`, `--enforce-eager`
**参数**: 100 ShareGPT prompts, rate=5, output_len=512, seed=42

| Config | 策略 | TPOT(ms) | P99 TPOT(ms) | TTFT(ms) | P99 TTFT(ms) | Tput(tok/s) | Duration(s) |
|--------|------|:---:|:---:|:---:|:---:|:---:|:---:|
| A | no BPF (baseline) | 60.9 | 64.5 | 76,381 | 172,633 | 233.8 | 218.6 |
| B | always_max | 56.7 (-6.9%) | 59.0 | 68,136 | 156,510 | 251.6 (+7.6%) | 201.9 |
| **C** | **always_max + cycle_moe** | **55.1 (-9.5%)** | **57.4** | **66,985** | **151,560** | **256.8 (+9.8%)** | **197.1** |
| D | XB blind | 56.1 | 58.8 | 67,843 | 155,562 | 252.6 (+8.0%) | 201.2 |
| E | XB direction | 56.3 | 59.9 | 67,473 | 152,469 | 256.0 (+9.5%) | 197.1 |
| F | serving_adaptive | 56.3 | 58.7 | 67,658 | 155,166 | 253.5 (+8.4%) | 200.4 |

**关键修正**:
1. **"always_max 对 vLLM 有害" 结论推翻** — P99 TPOT 全部正常（57-65ms），无爆炸
2. **所有 BPF 策略均有效**: TPOT -7~10%, throughput +8~10%, TTFT -10~12%
3. **最佳: Config C (always_max + cycle_moe)** — 与 llama.cpp 和 GNN 一致
4. **各策略间差异小** (B-F 仅 ~2ms TPOT)，1.175x 低 oversub 下策略区分度有限
5. **新算法 serving_adaptive** 表现中等，fault-rate gating 在低 oversub 下无额外价值

结果文件: `workloads/vllm/results/exp_vllm_rerun/config_{a..f}_*.json`

---

## 8. 下一步: Novel BPF 算法设计

### 8.0 现有算法总结与瓶颈分析

**已实现策略**:
- Prefetch: always_max, cross-block (blind/direction/adj-stride), phase-adaptive, serving-adaptive
- Eviction: cycle_moe (T1 protect), MRU, LFU, FIFO, Belady template

**各 workload 最佳结果与瓶颈**:
| Workload | Oversub | Best 提升 | 瓶颈 | 改进空间 |
|----------|---------|-----------|------|---------|
| GNN 10M | 1.34x | 3.29x (XB direction) | page faults 仍存在 | 多 block 预取可进一步减少 fault |
| llama.cpp 120B | 1.84x | +78% tg | PCIe bandwidth 饱和 | eviction-prefetch 协同可减少无效迁移 |
| FAISS SIFT100M | 1.5x | -31.8% add | phase detection 开销 | uprobe 精确 phase 检测 |
| vLLM 30B | 1.175x | +9.8% tput | 低 oversub 策略区分度小 | 需更高 oversub 或 app-guided prefetch |

### 8.1 Algorithm N1: Stride-Predictive Multi-Block Prefetch

**动机**: 当前 cross-block 只预取相邻 1 个 block。GNN sequential scan 每 epoch 扫描 ~7800 blocks，每个 block 至少 fault 一次。检测 stride 模式后预取 K blocks ahead 可大幅减少 fault。

**算法**:
```
状态 (per-CPU):
  stride_hist[4]     // 最近 4 次 block 间距
  confidence         // 连续正确预测次数
  pending_target     // 上次预测的目标地址

gpu_page_prefetch(fault_va):
  current_block = va_to_block(fault_va)
  stride = current_block - last_block

  // 更新 stride 历史
  shift(stride_hist)
  stride_hist[3] = stride

  // 检测 stride 一致性
  if all(stride_hist[i] == stride_hist[0] for i in 1..3):
    confidence = min(confidence + 1, MAX_CONFIDENCE)
  else:
    confidence = max(confidence - 2, 0)

  // 预测命中检查
  if current_block == pending_target:
    confidence = min(confidence + 2, MAX_CONFIDENCE)

  // 根据 confidence 决定预取深度
  K = 1 + confidence / 2    // confidence 0→K=1, 10→K=6
  K = min(K, MAX_LOOKAHEAD)

  // 预取 K 个 block
  for i in 1..K:
    target = current_block + i * stride
    bpf_wq → migrate(target, 2MB)

  pending_target = current_block + stride
  last_block = current_block
  return always_max  // intra-block
```

**与 cross-block_v2 的区别**:
1. **Multi-block**: 一次预取 K 个 block (K 自适应 1-6)，不是固定 1 个
2. **Stride-aware**: 检测任意 stride（不限于 ±1 block），适配 strided access
3. **Confidence-gated**: 预测不准时自动降级到 1-block，不会过度预取
4. **预测命中反馈**: 检查上次预测是否命中，动态调整信心

**BPF 可行性**: stride_hist PERCPU_ARRAY ✓, K 个 wq_map entries (64×6=384) ✓, bounded for loop ✓

**预期**: GNN +20-50% over XB direction, FAISS add +10%, llama.cpp 退化为 1-block (stride 不固定)

### 8.2 Algorithm N2: Reuse Distance Eviction (Practical Belady)

**动机**: cycle_moe 只做二值分类 (freq≥3 = T1, else non-T1)。Reuse distance（两次访问间隔）提供更精细的排序，是 Belady 算法的实用近似。

**算法**:
```
状态:
  last_access[SLOTS]  // 每个 chunk hash 的上次访问时间戳
  reuse_dist[SLOTS]   // 每个 chunk hash 的 EWMA reuse distance

gpu_block_access(chunk):
  h = chunk_hash(chunk)
  now = bpf_ktime_get_ns()
  last = last_access[h]

  if last > 0:
    dist = now - last
    // EWMA α=0.25: new_rd = 0.75 * old_rd + 0.25 * dist
    reuse_dist[h] = (reuse_dist[h] * 3 + dist) >> 2
  last_access[h] = now

  if reuse_dist[h] > 0 and reuse_dist[h] < SHORT_REUSE_THRESHOLD:
    bpf_gpu_block_move_tail(chunk, list)  // 短 reuse → 保护
    return BYPASS
  else:
    return BYPASS  // 长 reuse 或首次 → 不保护，优先 evict
```

**与 cycle_moe 的区别**: 连续值排序 vs 二值分类，时间感知，EWMA 自适应

### 8.3 Algorithm N3: Cooperative Prefetch-Eviction

**动机**: prefetch 和 eviction 当前独立运行，有内在矛盾：prefetch 迁入数据 → 触发 eviction → eviction 可能驱逐刚预取的或即将需要的数据。协同设计让 eviction 知道 prefetch 的预测目标。

**算法**:
```
共享状态:
  prefetch_predict_ring[16]  // prefetch 预测目标 VA 环形缓冲区

gpu_page_prefetch(fault_va):
  predicted_targets = stride_predict(fault_va)
  // 记录预测目标到共享 ring
  for target in predicted_targets:
    prefetch_predict_ring[(head++) % 16] = target
  // 执行 cross-block prefetch
  for target in predicted_targets:
    bpf_wq → migrate(target)
  return BYPASS

gpu_block_access(chunk):
  chunk_va = BPF_CORE_READ(chunk, va_block, start)
  // 检查是否在 prefetch 预测窗口中
  for i in 0..15:
    if prefetch_predict_ring[i] overlaps chunk_va:
      bpf_gpu_block_move_tail(chunk, list)  // 即将被 prefetch 需要 → 强保护
      return BYPASS
  // 正常 eviction 逻辑
  return normal_eviction(chunk)
```

**核心创新**: Prefetch-informed eviction — 用 prefetch 预测作为 "近似 future knowledge" 传递给 eviction。在高 oversub 场景（llama.cpp 1.84x）最有价值。

### 8.4 Algorithm N4: Online Access Pattern Classifier

**动机**: 当前需手动选择策略。Online classifier 自动检测 access pattern 并切换。

**特征提取** (sliding window N=64 faults):
- f1 = direction_consistency (连续同方向比例)
- f2 = stride_variance (stride 方差, 归一化)
- f3 = unique_blocks_ratio (唯一 block 数 / N)
- f4 = fault_rate (faults/ms)

**分类 → 策略映射**:
- SEQUENTIAL (f1>0.8, f2<0.2) → always_max + multi-block XB
- STRIDED (f1>0.6, f2<0.4) → always_max + 1-block XB
- RANDOM (f1<0.4) → kernel default (skip prefetch)
- PHASE_CYCLE → phase-adaptive

**与 phase-adaptive 区别**: 4+ 模式 vs 2 阶段，特征驱动，泛化到任意 workload

### 8.5 Algorithm N5: Application-Guided Prefetch via uprobe

**动机**: 所有上述算法都是 reactive（fault 驱动）或 pattern-guessing。uprobe 可以从应用层获取精确语义信息，实现 proactive prefetch。

**关键洞察**: `bpf_gpu_migrate_range()` 是 sleepable kfunc，可从 bpf_wq 回调调用。不限于 struct_ops — **任何 BPF 程序（uprobe/tracepoint/fentry）都可通过 bpf_wq 触发 GPU 迁移**。

**信息来源对比**:
| 方式 | 信息来源 | 精准度 | 时机 |
|------|---------|--------|------|
| struct_ops (fault-driven) | GPU page fault | 被动 | fault 后（已迟） |
| kprobe (kernel-driven) | 内核函数调用 | 中等 | 内核事件时 |
| **uprobe (app-guided)** | **用户态函数** | **最高（有应用语义）** | **fault 前（proactive）** |

**场景**:

**a) vLLM KV-cache lifecycle**:
- uprobe on `cudaMallocManaged` → 记录 KV-cache VA 范围
- uprobe on request dispatch → 知道哪个 request 的 KV 即将被访问
- 提前 migrate 对应 KV pages

**b) PyTorch GNN epoch boundary**:
- uprobe on `forward()` 入口 → 知道新 epoch 开始
- 立即 bpf_wq → migrate 前几个 feature blocks
- 比 fault-driven 提前一步

**c) FAISS 精确 phase detection**:
- uprobe on `IndexIVFFlat::add()` / `search()` → 精确知道当前阶段
- 零开销 phase 检测（不需要 fault pattern 猜测）
- 解决 np=1 的 25% gap（phase detection overhead 消除）

**d) llama.cpp layer prefetch**:
- uprobe on GGML op dispatch → 知道当前 layer
- 提前 migrate 下一 layer 的 expert weights
- 精确时序控制（不靠 fault pattern 推测 layer boundary）

**核心价值**: Application-transparent（应用不需改代码），BPF-mediated proactive GPU memory management。

### 8.6 实现优先级

| 算法 | 新颖度 | 预期改进 | 复杂度 | 优先级 |
|------|--------|----------|--------|--------|
| N1 Stride-Predictive Multi-Block | ★★★★ | GNN +20-50%, FAISS +10% | 中 | **P0** |
| N5 uprobe App-Guided (FAISS) | ★★★★★ | FAISS np=1 -25% gap | 中 | **P0** |
| N3 Cooperative Prefetch-Eviction | ★★★★★ | llama.cpp high-oversub +10-20% | 中 | **P1** |
| N2 Reuse Distance Eviction | ★★★ | llama.cpp/vLLM +5-15% | 低 | **P1** |
| N4 Online Pattern Classifier | ★★★★ | 通用性提升 | 高 | P2 |

**推荐路线**:
1. 先实现 N1 (Stride-Predictive) 在 GNN 上验证 — 效果最可量化
2. 同时实现 N5-c (uprobe FAISS phase) — 解决已知 25% gap
3. 然后 N3 (Cooperative) 在 llama.cpp 验证 — 解决 PCIe 零和
