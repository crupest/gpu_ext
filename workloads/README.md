# Workloads: Reproducing gpu_ext Paper Experiments

This directory contains all benchmark workloads used in the gpu_ext paper evaluation.

## Hardware Requirements

| Server | CPU | RAM | GPU | Used For |
|--------|-----|-----|-----|----------|
| Server A | Intel Core Ultra 9 285K (24 cores) | 128 GB DDR5 | NVIDIA RTX 5090 (32GB) | Most experiments |
| Server B | Dual Intel Gold 6138 (80 cores) | 256 GB | NVIDIA P40 | Observability overhead |

## Software Requirements

- CUDA 12.8+ with nvcc
- Python 3.12+ with `uv` package manager
- PyTorch 2.9.0+
- vLLM 0.11.0+
- Modified NVIDIA kernel module (from `kernel-module/`)
- `bpftool` for loading eBPF policies (from `bpftool/`)

## Directory Structure

```
workloads/
├── README.md                # This file
├── llama.cpp/               # LLM inference (Expert Offloading, Multi-Tenant)
│   ├── Makefile             # Build & benchmark targets
│   ├── README.md            # How to run + verify
│   ├── llama.cpp/           # [submodule] eunomia-bpf/llama.cpp
│   ├── uvm/                 # UVM test scripts & visualization
│   ├── docs/                # Analysis: UVM bugs, MoE, test records
│   ├── datasets/            # ShareGPT (gitignored)
│   └── results/
├── vllm/                    # LLM serving (KV-cache Offloading)
│   ├── Makefile             # Quick bench + dataset download
│   ├── README.md            # How to run + verify
│   ├── uvm/                 # Baseline comparison framework
│   ├── docs/                # LMCache setup, test records
│   ├── datasets/            # ShareGPT (gitignored)
│   └── results/
├── pytorch/                 # GNN Training (UVM oversubscription)
│   ├── Makefile             # Build allocator .so
│   ├── README.md            # How to run + verify
│   ├── benchmark_gnn_uvm.py
│   ├── visualize_all.py
│   ├── docs/                # UVM evaluation, GCN benchmark notes
│   ├── with-user-prefetch/  # Results with cudaMemPrefetchAsync
│   └── without-user-prefetch/
└── faiss/                   # Vector Search (SIFT dataset)
    ├── README.md            # How to run + verify
    ├── bench_gpu_1bn.py     # Main GPU benchmark
    ├── faiss/               # [submodule] eunomia-bpf/faiss
    ├── docs/                # Dataset docs, prefetch analysis
    └── results/             # Logs & visualization
```

---

## Quick Start: Loading gpu_ext eBPF Policies

Before running any experiment with gpu_ext, you need the modified kernel module and eBPF policies loaded:

```bash
# 1. Build and load the modified nvidia-uvm kernel module
cd /path/to/gpu_ext/kernel-module
make && sudo make install

# 2. Build the eBPF extension policies
cd /path/to/gpu_ext/extension
make

# 3. Load a policy (example: stride prefetch + LFU eviction)
sudo bpftool struct_ops register prefetch_stride.bpf.o
sudo bpftool struct_ops register eviction_lfu.bpf.o

# 4. Verify
sudo bpftool struct_ops show
```

To unload policies:
```bash
sudo bpftool struct_ops show          # find the ID
sudo bpftool struct_ops unregister id <ID>
```

---

## Experiment 1: llama.cpp — Expert Offloading (RQ1, Figure 5)

**Paper claim**: gpu_ext achieves 4.8x decode speedup over framework offloading on GPT-OSS-120B MoE.

### Setup

```bash
cd workloads/llama.cpp

# Build llama.cpp with CUDA (no VMM, needed for UVM)
git submodule update --init llama.cpp
make build-cuda

# Download ShareGPT dataset
python download_sharegpt.py

# Download model (GPT-OSS-120B, ~59 GiB, auto-cached to ~/.cache/llama.cpp/)
make download-models
```

### Run Benchmarks

Five configurations need to be compared:

```bash
# Config 1: Framework CPU offload (ncmoe=64)
./build/bin/llama-bench -ncmoe 64 \
  -m ~/.cache/llama.cpp/ggml-org_gpt-oss-120b-GGUF_gpt-oss-120b-mxfp4-00001-of-00003.gguf \
  2>&1 | tee results/ncmoe64.log

# Config 2: Framework CPU offload (ncmoe=32)
./build/bin/llama-bench -ncmoe 32 \
  -m ~/.cache/llama.cpp/ggml-org_gpt-oss-120b-GGUF_gpt-oss-120b-mxfp4-00001-of-00003.gguf \
  2>&1 | tee results/ncmoe32.log

# Config 3: Default UVM (no policy)
GGML_CUDA_ENABLE_UNIFIED_MEMORY=1 ./build/bin/llama-bench \
  -m ~/.cache/llama.cpp/ggml-org_gpt-oss-120b-GGUF_gpt-oss-120b-mxfp4-00001-of-00003.gguf \
  2>&1 | tee results/uvm_baseline.log

# Config 4: UVM + user hints (cudaMemAdvise)
GGML_CUDA_ENABLE_UNIFIED_MEMORY=1 ./build/bin/llama-bench \
  -m ~/.cache/llama.cpp/ggml-org_gpt-oss-120b-GGUF_gpt-oss-120b-mxfp4-00001-of-00003.gguf \
  2>&1 | tee results/uvm_user_hint.log
# (requires llama.cpp built with cudaMemAdvise hints enabled)

# Config 5: UVM + gpu_ext eBPF (stride prefetch + LFU eviction)
# First load policies:
sudo bpftool struct_ops register /path/to/gpu_ext/extension/prefetch_stride.bpf.o
sudo bpftool struct_ops register /path/to/gpu_ext/extension/eviction_lfu.bpf.o
GGML_CUDA_ENABLE_UNIFIED_MEMORY=1 ./build/bin/llama-bench \
  -m ~/.cache/llama.cpp/ggml-org_gpt-oss-120b-GGUF_gpt-oss-120b-mxfp4-00001-of-00003.gguf \
  2>&1 | tee results/uvm_ebpf.log
```

Or use the Makefile shortcut:
```bash
make bench-120b-uvm   # runs 3 trials with UVM
```

### Expected Output

```
| model                   | size    | params   | backend | ngl | test  | t/s            |
| gpt-oss 120B MXFP4 MoE | 59.02G  | 116.83B  | CUDA    | 99  | pp512 | 238.48 ± 1.43  |
| gpt-oss 120B MXFP4 MoE | 59.02G  | 116.83B  | CUDA    | 99  | tg128 | 86.89 ± 0.50   |
```

Key metrics: `pp512` = prefill throughput (tok/s), `tg128` = decode throughput (tok/s).

### Generate Figure

```bash
cd uvm && python visbasic.py   # produces llama_uvm_combined_color.pdf
```

### Reference Results (RTX 5090)

| Config | pp512 (tok/s) | tg128 (tok/s) |
|--------|--------------|--------------|
| ncmoe=64 (framework offload) | 245.63 | 16.34 |
| ncmoe=32 (framework offload) | 260.14 | 18.18 |
| UVM baseline | 238.48 | 7.72 |
| UVM + user hint | 144.00 | 49.31 |
| **UVM + gpu_ext eBPF** | **229.67** | **86.89** |

---

## Experiment 2: vLLM — KV-cache Offloading (RQ1, Figure 6)

**Paper claim**: gpu_ext improves TTFT by 1.7-2x and decoding throughput by 1.3x over vLLM CPU-offload.

### Setup

```bash
cd workloads/vllm

# Create venv and install vLLM
uv venv && source .venv/bin/activate
uv pip install vllm

# Download dataset (if not present)
make download-datasets
# Model (Qwen3-30B-A3B-FP8) is auto-downloaded by vLLM on first run
```

### Run Benchmarks

The primary script compares 3 baselines automatically:

```bash
python uvm/test_uvm_baselines.py \
  --bench-args "--model Qwen/Qwen3-30B-A3B-FP8 \
    --dataset-name sharegpt \
    --num-prompts 100 \
    --dataset-path datasets/ShareGPT_V3_unfiltered_cleaned_split.json \
    --sharegpt-output-len 512 \
    --seed 42 --request-rate 5" \
  --baselines cpu_offload uvm_baseline \
  --output-dir results
```

For the gpu_ext UVM + eBPF configuration, load policies first then run:
```bash
# Load gpu_ext sequential prefetch policy
sudo bpftool struct_ops register /path/to/gpu_ext/extension/prefetch_adaptive_sequential.bpf.o
sudo bpftool struct_ops register /path/to/gpu_ext/extension/eviction_lfu.bpf.o

# Run UVM baseline with eBPF active
python uvm/test_uvm_baselines.py \
  --bench-args "--model Qwen/Qwen3-30B-A3B-FP8 \
    --dataset-name sharegpt --num-prompts 100 \
    --dataset-path datasets/ShareGPT_V3_unfiltered_cleaned_split.json \
    --sharegpt-output-len 512 --seed 42 --request-rate 5" \
  --baselines uvm_baseline \
  --output-dir results
```

**Note**: `test_uvm_baselines.py` defaults to `~/workspace/vllm` for vLLM and `~/workspace/gpu/LMCache` for LMCache. Override via `VLLM_SERVER_DIR` and `LMCACHE_SERVER_DIR` environment variables. Dataset path defaults to `datasets/ShareGPT_V3_unfiltered_cleaned_split.json` (relative to the vllm workload directory).

### Expected Output

Results saved to `results/uvm_baseline_results_YYYYMMDD_HHMMSS.json` with metrics:
- Mean/Median/P99 TTFT (ms), TPOT (ms), ITL (ms)
- Request throughput (req/s), Output token throughput (tok/s)

### Generate Figure

```bash
cd uvm/first-iter && python generate_figures.py
# produces ttft_tpot_combined.pdf
```

### Reference Results (RTX 5090, 100 concurrent requests)

| Config | Mean TTFT (ms) | Mean TPOT (ms) | Throughput (tok/s) |
|--------|---------------|----------------|-------------------|
| CPU Offload (8GB) | 8387.80 | 324.13 | 391.14 |
| UVM Baseline | 9642.27 | 374.23 | 307.26 |
| **UVM + gpu_ext eBPF** | **5042.22** | **235.68** | **376.53** |
| LMCache | 5401.71 | 222.24 | 571.54 |

---

## Experiment 3: PyTorch GNN — Graph Neural Network Training (RQ1, Figure 7)

**Paper claim**: gpu_ext achieves 2.65x speedup without user prefetch, 1.44x additional with user prefetch at 15M nodes.

### Setup

```bash
cd workloads/pytorch

# Build custom CUDA allocators
make all    # produces uvm_allocator.so and gpu_allocator.so

# Create venv
uv venv && source .venv/bin/activate
uv pip install torch psutil torch-geometric
```

No external dataset needed — graphs are randomly generated.

### Run Benchmarks

Three scripts for three configurations (results stored in separate dirs):

**Script 1: No UVM baseline (pure GPU, OOMs beyond ~7M nodes)**
```bash
for NODES in 1000000 3000000 5000000 7000000; do
  uv run python benchmark_gnn_uvm.py --dataset random --nodes $NODES \
    --edges_per_node 10 --features 128 --hidden 256 \
    --epochs 2 --warmup 1 --prop chunked --use_gpu_allocator \
    --report_json without-user-prefetch/result_no_uvm1/${NODES}.json
done
```

**Script 2: UVM baseline (default driver, no eBPF)**
```bash
for NODES in 5000000 7000000 8000000 10000000 12000000 15000000; do
  CUDA_MANAGED_FORCE_DEVICE_ALLOC=1 uv run python benchmark_gnn_uvm.py \
    --dataset random --nodes $NODES \
    --edges_per_node 10 --features 128 --hidden 256 \
    --epochs 2 --warmup 1 --prop chunked --use_uvm \
    --report_json without-user-prefetch/result_uvm_baseline1/${NODES}.json
done
```

**Script 3: UVM + gpu_ext eBPF (load policies first)**
```bash
# Load sequential prefetch policy
sudo bpftool struct_ops register /path/to/gpu_ext/extension/prefetch_adaptive_sequential.bpf.o

for NODES in 5000000 7000000 8000000 10000000 12000000 15000000; do
  CUDA_MANAGED_FORCE_DEVICE_ALLOC=1 uv run python benchmark_gnn_uvm.py \
    --dataset random --nodes $NODES \
    --edges_per_node 10 --features 128 --hidden 256 \
    --epochs 2 --warmup 1 --prop chunked --use_uvm \
    --report_json without-user-prefetch/result_uvm_ebpf1/${NODES}.json
done
```

Repeat all three with `--use_uvm` (which enables `cudaMemPrefetchAsync` in the allocator) for the "with-user-prefetch" variant, saving to `with-user-prefetch/` subdirectories.

### Expected Output

Each run produces a JSON file:
```json
{
  "config": {"nodes": 10000000, "use_uvm": true, ...},
  "epoch_times": [69.87, 70.12, ...],
  "avg_epoch_time": 69.87,
  "uvm_stats": {"peak_allocated_gb": 45.11, "allocations": 23934}
}
```

### Generate Figure

```bash
python visualize_all.py
# produces uvm_benchmark_comparison.pdf
```

### Reference Results (RTX 5090, without user prefetch)

| Nodes | No UVM | UVM Baseline | UVM + gpu_ext | Speedup |
|-------|--------|-------------|--------------|---------|
| 5M | 1.14s | 34.23s | 12.76s | 2.68x |
| 7M | 1.79s | 48.28s | 17.81s | 2.71x |
| 8M | OOM | 55.36s | 20.51s | 2.70x |
| 10M | OOM | 70.06s | 26.47s | 2.65x |
| 12M | OOM | 93.71s | 39.74s | 2.36x |
| 15M | OOM | 292.77s | 168.73s | 1.74x |

---

## Experiment 4: Faiss — Vector Search (RQ1, Figure 8)

**Paper claim**: gpu_ext reduces build time by 21-29% and query latency by 10-16%.

### Setup

```bash
cd workloads/faiss

# Build FAISS from submodule
git submodule update --init faiss
cd faiss
cmake -B build \
  -DFAISS_ENABLE_GPU=ON \
  -DFAISS_ENABLE_PYTHON=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DFAISS_OPT_LEVEL=avx2 \
  -DBUILD_TESTING=OFF \
  -DBLA_VENDOR=OpenBLAS
make -C build -j$(nproc) swigfaiss
cd build/faiss/python && pip install -e .
cd ../../../../

# Create venv
uv venv && source .venv/bin/activate
uv pip install "numpy<2.0"

# Download SIFT dataset
# The SIFT1B dataset must be placed at faiss/benchs/bigann/
# Download from: http://corpus-texmex.irisa.fr/
# Required files:
#   bigann_base.bvecs  (46 GB for 375M vectors)
#   bigann_learn.bvecs (13 GB for 100M training vectors)
#   bigann_query.bvecs (1.3 MB for 10K queries)
#   gnd/               (ground truth files)
```

### Run Benchmarks

```bash
# SIFT50M - UVM baseline (no eBPF)
uv run python bench_gpu_1bn.py SIFT50M IVF4096,Flat -nprobe 1,4,16 -uvm \
  2>&1 | tee results/SIFT50M_uvm_baseline.log

# SIFT100M - UVM baseline
uv run python bench_gpu_1bn.py SIFT100M IVF4096,Flat -nprobe 1,4,16 -uvm \
  2>&1 | tee results/SIFT100M_uvm_baseline.log

# SIFT50M - UVM + gpu_ext eBPF (load adaptive prefetch policy first)
sudo bpftool struct_ops register /path/to/gpu_ext/extension/prefetch_adaptive_sequential.bpf.o
uv run python bench_gpu_1bn.py SIFT50M IVF4096,Flat -nprobe 1,4,16 -uvm \
  2>&1 | tee results/SIFT50M_uvm_ebpf.log

# SIFT100M - UVM + gpu_ext eBPF
uv run python bench_gpu_1bn.py SIFT100M IVF4096,Flat -nprobe 1,4,16 -uvm \
  2>&1 | tee results/SIFT100M_uvm_ebpf.log
```

### Expected Output

Console output includes:
```
Add time: 68.407 s
probe=1  : 5.135 s 1-R@1: 0.4486
probe=4  : 14.393 s 1-R@1: 0.7655
probe=16 : 56.511 s 1-R@1: 0.9476
```

Results also saved as JSON in `results/` directory.

### Generate Figure

```bash
cd results && python vis_faiss.py
# produces faiss_benchmark_results.pdf
```

---

## Experiment 5: Multi-Tenant — Two-Tenant Co-location (RQ2, Figure 11)

**Paper claim**: gpu_ext achieves mutual improvement: LC TPOT reduced by 40-45%, BE training improved by 28%.

### Setup

Requires both llama.cpp and PyTorch workloads set up (see Experiments 1 & 3).

### Run Benchmarks

**Step 1: Single-tenant baselines**
```bash
# Baseline: llama.cpp single-tenant (gpt-oss-20b, UVM)
cd workloads/llama.cpp
GGML_CUDA_DISABLE_GRAPHS=1 GGML_CUDA_ENABLE_UNIFIED_MEMORY=1 \
  ./build/bin/llama-server --gpt-oss-20b-default -c 65536 &
LLAMA_PID=$!
sleep 30

# Run llama.cpp benchmark
uv run vllm bench serve \
  --model Qwen/Qwen3-30B-A3B-FP8 \
  --dataset-name sharegpt --num-prompts 100 \
  --dataset-path datasets/ShareGPT_V3_unfiltered_cleaned_split.json \
  --base-url http://127.0.0.1:8013 \
  --max-concurrency=1 --request-rate 0.2 \
  2>&1 | tee uvm/results_single_llama.log
kill $LLAMA_PID

# Baseline: GNN single-tenant
cd workloads/pytorch
CUDA_MANAGED_FORCE_DEVICE_ALLOC=1 uv run python benchmark_gnn_uvm.py \
  --dataset random --nodes 8000000 \
  --edges_per_node 10 --features 128 --hidden 256 \
  --epochs 25 --warmup 1 --prop chunked --use_uvm \
  --report_json result/single_gnn.json
```

**Step 2: Co-located (default UVM, no policy)**
```bash
# Terminal 1: Start llama.cpp server
GGML_CUDA_DISABLE_GRAPHS=1 GGML_CUDA_ENABLE_UNIFIED_MEMORY=1 \
  ./build/bin/llama-server --gpt-oss-20b-default -c 65536 &

# Terminal 2: Start GNN training (wait for server to load)
sleep 30
CUDA_MANAGED_FORCE_DEVICE_ALLOC=1 uv run python benchmark_gnn_uvm.py \
  --dataset random --nodes 8000000 --use_uvm --epochs 25 \
  --report_json result/colocated_gnn_baseline.json &

# Terminal 3: Run llama.cpp benchmark (wait for GNN to start)
sleep 10
uv run vllm bench serve \
  --dataset-name sharegpt --num-prompts 100 \
  --dataset-path datasets/ShareGPT_V3_unfiltered_cleaned_split.json \
  --base-url http://127.0.0.1:8013 \
  --max-concurrency=1 --request-rate 0.2
```

**Step 3: Co-located with gpu_ext per-tenant policies**
```bash
# Load per-tenant memory policies
sudo bpftool struct_ops register /path/to/gpu_ext/extension/prefetch_eviction_pid.bpf.o

# Then repeat Step 2 with policies active
```

### Generate Figure

```bash
cd workloads/llama.cpp/uvm
python plot_colocated_results.py
# produces fig_colocated_results.pdf
```

### Reference Results (RTX 5090)

| Metric | Single llama.cpp | Co-located (UVM) | Co-located (gpu_ext) |
|--------|-----------------|-------------------|---------------------|
| TPOT (ms) | 3.67 | 19.73 (5.38x worse) | 10.86 (2.96x, **45% better**) |
| GNN epoch (s) | - | 23.23 | 16.72 (**28% better**) |

---

## Experiment 6: Multi-Tenant Microbenchmarks (RQ2, Figures 9-10)

These experiments use CUDA microbenchmarks from `microbench/` directory.

### Compute-bound Timeslice Scheduler (Figure 9)
Uses gpu_ext's BPF struct_ops scheduling policies with LC+BE processes.

### Memory-bound Priority Differentiation (Figure 10)
Uses HotSpot, GEMM (PolyBench), and K-Means (UVMBench) kernels.
Scripts and data are in `microbench/memory/`.

---

## Experiment 7: Mechanism Overhead (RQ3, Figures 12, Tables 2-3)

### Host Runtime Overhead
Uses GEMM and HotSpot with hooks enabled but no policy attached.
Expected overhead: <0.2%.

### Device-side Observability
Uses llama.cpp prefill with Llama 1B on P40 GPU.
Tools: `kernelretsnoop`, `threadhist`, `launchlate` from `extension/` directory.

### Device-side Microbenchmarks (Figure 12)
Vector-add from cuda-samples, comparing eGPU-style vs gpu_ext SIMT-aware execution.
Scripts in `microbench/memory/`.

---

## Path Configuration

Scripts use relative paths by default. Override via environment variables or Makefile variables when needed:

| Variable | Used By | Default | Description |
|----------|---------|---------|-------------|
| `VLLM_SERVER_DIR` | `vllm/uvm/test_uvm_baselines.py` | `~/workspace/vllm` | vLLM installation directory |
| `LMCACHE_SERVER_DIR` | `vllm/uvm/test_uvm_baselines.py` | `~/workspace/gpu/LMCache` | LMCache installation directory |
| `DATASET_PATH` | `vllm/uvm/test_uvm_baselines.py` | `datasets/ShareGPT_V3_...json` | ShareGPT dataset path |
| `VENV` | `vllm/Makefile` | `.venv/bin/activate` | Python venv activate script |
| `MODEL_120B_CACHE` | `llama.cpp/Makefile` | `$HOME/.cache/llama.cpp/...` | GPT-OSS-120B model path |

---

## What's Still Missing From This Repo

The following items are needed to fully reproduce all experiments but are not yet in the gpu_ext repository:

| Item | Needed For | Size | How to Get |
|------|-----------|------|-----------|
| SIFT1B dataset | Faiss (Exp 4) | ~60 GB | Download from http://corpus-texmex.irisa.fr/ into `faiss/faiss/benchs/bigann/` |
| GPT-OSS-120B model | llama.cpp (Exp 1) | ~59 GiB | `huggingface-cli download ggml-org/gpt-oss-120b-GGUF` |
| Qwen3-30B-A3B-FP8 model | vLLM (Exp 2) | ~30 GB | Auto-downloaded by vLLM on first run |
| vLLM (modified for UVM) | vLLM (Exp 2) | - | Clone and patch vLLM with UVM support |
| LMCache | vLLM baseline comparison | - | Clone from github.com/LMCache/LMCache |
| HotSpot/GEMM/K-Means kernels | Multi-tenant microbench (Exp 6) | small | From Rodinia/PolyBench/UVMBench suites |
| cuda-samples vector-add | Device microbench (Exp 7) | small | From NVIDIA cuda-samples |
