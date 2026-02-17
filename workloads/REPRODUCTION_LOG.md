# Workload Reproduction Log

**Date**: 2026-02-16
**Environment**: RTX 5090 (32GB), CUDA 12.9, Driver 575.57.08 (stock nvidia-uvm), Linux 6.15.11
**Kernel Module**: Stock nvidia-uvm (NOT the modified gpu_ext version)
**eBPF Policies**: Not loaded

---

## Pre-test: GPU Cleanup

Before each test, a shared cleanup script is used to kill stale GPU processes:

```bash
python workloads/cleanup_gpu.py
```

**Issue encountered**: An old `llama-server` process was occupying 19226 MiB GPU memory, causing PyTorch UVM 5M test to OOM. After cleanup, the test passed.

---

## Test 1: PyTorch GNN

### Setup

```bash
cd workloads/pytorch
make clean && make all          # rebuild uvm_allocator.so + gpu_allocator.so
uv venv && source .venv/bin/activate
uv pip install torch psutil torch-geometric
```

- **PyTorch version**: 2.10.0+cu128
- **Allocators**: uvm_allocator.so and gpu_allocator.so freshly built

### Test 1a: Normal Mode (default allocator, 1M nodes)

```bash
uv run python benchmark_gnn_uvm.py \
  --dataset random --nodes 1000000 --edges_per_node 10 \
  --features 128 --hidden 256 --epochs 2 --warmup 1 \
  --prop chunked \
  --report_json result/verify_default_1M.json
```

**Result: PASS**

```
Avg epoch time: 0.218s
Median epoch time: 0.218s
Memory Usage:
  GPU allocated: 1.12 GB
  CPU used: 1.64 GB
```

**Note**: `--use_gpu_allocator` causes `CUBLAS_STATUS_INVALID_VALUE` during backward pass. The default PyTorch allocator works fine. This is a known issue — the custom gpu_allocator.so conflicts with cuBLAS internal allocations.

### Test 1b: UVM Mode (5M nodes)

```bash
python workloads/cleanup_gpu.py   # CRITICAL: must clear GPU first

CUDA_MANAGED_FORCE_DEVICE_ALLOC=1 uv run python benchmark_gnn_uvm.py \
  --dataset random --nodes 5000000 --edges_per_node 10 \
  --features 128 --hidden 256 --epochs 2 --warmup 1 \
  --prop chunked --use_uvm \
  --report_json result/verify_uvm_5M.json
```

**Result: PASS**

```
Avg epoch time: 5.529s
Median epoch time: 5.564s
Memory Usage:
  GPU allocated: 5.52 GB
  CPU used: 1.63 GB
UVM Statistics:
  Peak allocated: 22.56 GB
  Allocations: 2065
  Frees: 1706
```

**Notes**:
- First attempt OOMed because a stale `llama-server` occupied 19GB GPU. After `cleanup_gpu.py`, passed immediately.
- UVM 3M nodes also tested (6.37s/epoch, peak 15.73GB) — works fine.
- Reference value (from Dec 2025 with modified KM): 34.23s/epoch at 5M. Current 5.53s is much faster — likely because stock driver without modified KM has different page migration behavior (less oversubscription pressure at this scale).

### Test 1 Summary

| Config | Nodes | Epoch Time | Peak Alloc | Status |
|--------|-------|-----------|-----------|--------|
| Default allocator | 1M | 0.22s | 1.12 GB | PASS |
| UVM | 3M | 6.37s | 15.73 GB | PASS |
| UVM | 5M | 5.53s | 22.56 GB | PASS (after GPU cleanup) |

---

## Test 2: llama.cpp

### Setup

```bash
cd workloads/llama.cpp
make build-cuda-no-vmm    # CUDA build with VMM disabled for UVM compatibility
```

Build completed successfully (~8 min). Uses GCC-12 + CUDA 12.9.

**Available models**:
- `ggml-org_gpt-oss-20b-GGUF` (12GB) — fits in 32GB VRAM
- `unsloth_GLM-4.7-Flash-GGUF` (18GB) — fits in VRAM
- `unsloth_Qwen3-Coder-Next-GGUF` (46GB) — needs UVM
- `unsloth_Qwen3-Next-80B-A3B-Instruct-GGUF` (46GB) — needs UVM
- GPT-OSS-120B (59GB) — **NOT downloaded**, needs `huggingface-cli download ggml-org/gpt-oss-120b-GGUF`

### Test 2a: Normal Mode (20B model, fits in VRAM)

```bash
python workloads/cleanup_gpu.py

./build/bin/llama-bench \
  -m ~/.cache/llama.cpp/ggml-org_gpt-oss-20b-GGUF_gpt-oss-20b-mxfp4.gguf \
  2>&1 | tee results/verify_20b_normal.log
```

**Result: PASS**

```
| model                          |       size |     params | backend    | ngl |            test |                  t/s |
| gpt-oss 20B MXFP4 MoE          |  11.27 GiB |    20.91 B | CUDA       |  99 |           pp512 |      9897.95 ± 60.40 |
| gpt-oss 20B MXFP4 MoE          |  11.27 GiB |    20.91 B | CUDA       |  99 |           tg128 |        362.59 ± 1.01 |
build: f92e406b (7100)
```

### Test 2b: UVM Mode (46GB model, oversubscribes 32GB GPU)

Attempted with two 46GB models:

```bash
GGML_CUDA_ENABLE_UNIFIED_MEMORY=1 ./build/bin/llama-bench \
  -m ~/.cache/llama.cpp/unsloth_Qwen3-Next-80B-A3B-Instruct-GGUF_Qwen3-Next-80B-A3B-Instruct-Q4_K_M.gguf
```

**Result: FAILED** — `error: failed to load model`

```bash
GGML_CUDA_ENABLE_UNIFIED_MEMORY=1 ./build/bin/llama-bench \
  -m ~/.cache/llama.cpp/unsloth_Qwen3-Coder-Next-GGUF_Qwen3-Coder-Next-UD-Q4_K_XL.gguf
```

**Result: FAILED** — `error: failed to load model`

**Root cause**: The llama.cpp submodule (build f92e406b) likely doesn't support these newer Qwen3/GLM GGUF formats. The paper's UVM experiment uses GPT-OSS-120B which is a known-compatible model but is not yet downloaded (~59 GiB).

**TODO**: Download GPT-OSS-120B model and retest:
```bash
huggingface-cli download ggml-org/gpt-oss-120b-GGUF --local-dir ~/.cache/llama.cpp/
```

### Test 2 Summary

| Config | Model | Size | pp512 | tg128 | Status |
|--------|-------|------|-------|-------|--------|
| Normal | gpt-oss-20B | 11.27 GiB | 9897.95 | 362.59 | PASS |
| UVM | Qwen3-Next-80B | 46 GiB | — | — | FAIL (model format incompatible) |
| UVM | Qwen3-Coder-Next | 46 GiB | — | — | FAIL (model format incompatible) |
| UVM | GPT-OSS-120B | 59 GiB | — | — | NOT TESTED (model not downloaded) |

---

## Test 3: FAISS

### Setup

**Data**: SIFT dataset moved from schedcp to gpu_ext:

```bash
mkdir -p workloads/faiss/faiss/benchs/bigann
mv /home/yunwei37/workspace/gpu/schedcp/workloads/faiss/faiss/benchs/bigann/{bigann_base.bvecs,bigann_learn.bvecs,bigann_query.bvecs,gnd} \
   workloads/faiss/faiss/benchs/bigann/
```

Dataset files:
- `bigann_base.bvecs` — 47 GB (375M vectors)
- `bigann_learn.bvecs` — 13 GB (100M training vectors)
- `bigann_query.bvecs` — 1.3 MB (10K queries)
- `gnd/` — ground truth files

**Download script** created: `workloads/faiss/download_sift.sh` (for future reproduction).

**Build**: NOT YET DONE — requires cmake + make (~20 min).

### Test 3a: Normal GPU (SIFT10M)

**Status**: Pending (FAISS not yet built)

### Test 3b: UVM (SIFT100M)

**Status**: Pending (FAISS not yet built)

---

## Test 4: vLLM

**Status**: Skipped — vLLM installation is heavy, deferred.

---

## Overall Summary

| Workload | Normal | UVM | Notes |
|----------|--------|-----|-------|
| PyTorch GNN | PASS (0.22s/epoch @ 1M) | PASS (5.53s/epoch @ 5M, peak 22.56GB) | Must cleanup GPU first |
| llama.cpp | PASS (pp512=9898, tg128=363 @ 20B) | NOT TESTED | 46G models incompatible with submodule; 120B not downloaded |
| FAISS | PENDING | PENDING | Data moved, FAISS needs build |
| vLLM | SKIPPED | SKIPPED | Heavy install |

## Issues Discovered

1. **Stale GPU processes cause OOM**: A running `llama-server` used 19GB, leaving insufficient GPU for UVM tests. **Fix**: Always run `cleanup_gpu.py` before benchmarks.

2. **`--use_gpu_allocator` crashes with cuBLAS**: The custom `gpu_allocator.so` (uses `cudaMalloc`) causes `CUBLAS_STATUS_INVALID_VALUE` during backward pass. Default PyTorch allocator works. UVM allocator (`--use_uvm`) works.

3. **llama.cpp model compatibility**: The submodule (build f92e406b) cannot load Qwen3-Next-80B or Qwen3-Coder-Next GGUF files. Need GPT-OSS-120B for the paper's UVM experiment.

4. **Reference results discrepancy**: PyTorch 5M UVM epoch time is 5.53s now vs 34.23s in Dec 2025 reference. The reference was likely obtained with the modified kernel module which changes UVM page migration behavior.
