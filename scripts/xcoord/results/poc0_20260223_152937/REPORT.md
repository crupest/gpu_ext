# POC-0 Results: CPU-GPU Coupling Experiment (20B Model)

**Date**: 2026-02-23
**Model**: gpt-oss-20b-mxfp4 (12GB, fits in 32GB GPU)
**GPU**: RTX 5090 (32GB)
**Workload**: llama-server + vllm bench serve, 50 ShareGPT prompts

---

## 1. Throughput Comparison

| Scenario | tok/s | Slowdown | Requests OK | TPOT Mean | TPOT P99 | TTFT Mean |
|----------|-------|----------|-------------|-----------|----------|-----------|
| Baseline | 198.67 | - | 50/50 | 3.72ms | 4.13ms | 57.82ms |
| CPU Stress | 175.40 | **-11.7%** | 40/50 | 4.54ms | 6.14ms | 62.46ms |
| CPU Stress + Pinned | 179.71 | **-9.5%** | 40/50 | 4.62ms | 6.56ms | 74.46ms |
| Heavy Load | 185.38 | **-6.7%** | 43/50 | 4.39ms | 6.19ms | 61.02ms |

### Key Findings

1. **CPU stress causes 11.7% throughput drop** and 20% request failures
2. **CPU pinning (taskset -c 0-5 + nice -10) barely helps**: only 2.5% improvement over unpinned (179.71 vs 175.40)
3. **TPOT P99 increases 49%** under CPU stress (4.13ms → 6.14ms)
4. **Heavy load (CPU+Net) causes 6.7% drop** — less than CPU-only, suggesting interference competition

---

## 2. GPU Hook Call Rates (chunk_trace)

| Scenario | Duration | Activate/s | Used/s | Evict/s | Total Hooks |
|----------|----------|------------|--------|---------|-------------|
| Baseline | 95.0s | 67.3 | 452.5 | 0.0 | 49,363 |
| CPU Stress | 106.2s | 60.2 | 408.3 | 0.0 | 49,747 |
| CPU Stress + Pinned | 107.4s | 59.5 | 402.0 | 0.0 | 49,567 |
| Heavy Load | 106.8s | 59.7 | 405.1 | 0.0 | 49,648 |

### Key Findings

1. **Zero evictions**: Model fits in GPU memory → no eviction pressure
2. **Activate rate drops ~10%** under stress (67.3 → 60.2/s) because workload runs slower
3. **~6,400 activations per run** (all during model loading, first 1-2 seconds)
4. **~43,000 POPULATE (chunk_used) events** mostly during model loading

---

## 3. UVM Fault Timing Analysis

### 98.3% of faults happen during model loading

| Time Window | ACTIVATE Events | % of Total |
|-------------|-----------------|------------|
| 0-1 seconds | 6,283 | 98.3% |
| 1-2 seconds | 107 | 1.7% |
| 2+ seconds | 0 | 0.0% |

**Implication**: The 20B model (12GB) fits entirely in 32GB GPU. During inference, there are **no page faults**. The 11.7% throughput drop under CPU stress is purely from **CPU thread scheduling** (kernel launch delays, context switches), not from page fault interference.

---

## 4. Analysis and Implications for xCoord

### 4.1 Two mechanisms of CPU-GPU coupling

1. **Thread scheduling mechanism** (demonstrated here):
   - CPU stress → llama-server threads get preempted → GPU kernel launch delays → throughput drop
   - This is the mechanism Meta's sched_ext addresses with thread identification
   - Confirmed: 11.7% drop from pure CPU contention

2. **Page fault mechanism** (not triggered with 20B model):
   - Needs model that exceeds GPU memory (120B = 60GB > 32GB GPU)
   - CPU stress → page fault handler delays → GPU idle time → compounded throughput drop
   - This is xCoord's unique contribution beyond Meta's approach

### 4.2 CPU pinning failure confirmed

CPU pinning (taskset + nice) provided only 2.5% improvement:
- 175.40 tok/s (unpinned) → 179.71 tok/s (pinned)
- Still 9.5% below baseline (vs 11.7% without pinning)
- **pinning cannot fully solve CPU-GPU coupling** — a dynamic solution is needed

### 4.3 Next step: 120B model UVM-heavy experiment

Running separately with gpt-oss-120b-mxfp4 (60GB):
- Will trigger massive page faults during inference
- Expected: much larger performance degradation under CPU stress
- Will demonstrate both mechanisms simultaneously
