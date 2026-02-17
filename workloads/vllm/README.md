# vLLM KV-cache Offloading Benchmark (Experiment 2)

Benchmarks vLLM with different KV-cache memory strategies to evaluate gpu_ext's UVM optimization for LLM serving.

## Prerequisites

```bash
# Create venv and install vLLM
uv venv && source .venv/bin/activate
uv pip install vllm

# Download ShareGPT dataset
make download-datasets

# Model (Qwen3-30B-A3B-FP8) is auto-downloaded by vLLM on first run
```

**External dependencies:**
- vLLM installed at `~/workspace/vllm` (or set `VLLM_SERVER_DIR`)
- LMCache at `~/workspace/gpu/LMCache` (or set `LMCACHE_SERVER_DIR`, only for lmcache baseline)

## Running Benchmarks

### Automated (3 baselines)

```bash
python uvm/test_uvm_baselines.py \
  --bench-args "--model Qwen/Qwen3-30B-A3B-FP8 \
    --dataset-name sharegpt --num-prompts 100 \
    --dataset-path datasets/ShareGPT_V3_unfiltered_cleaned_split.json \
    --sharegpt-output-len 512 --seed 42 --request-rate 5" \
  --baselines cpu_offload uvm_baseline \
  --output-dir results
```

### With gpu_ext eBPF policy

```bash
# Load policies first
sudo bpftool struct_ops register /path/to/gpu_ext/extension/.output/prefetch_adaptive_sequential.bpf.o
sudo bpftool struct_ops register /path/to/gpu_ext/extension/.output/eviction_lfu.bpf.o

# Run UVM baseline (eBPF is transparent)
python uvm/test_uvm_baselines.py \
  --bench-args "--model Qwen/Qwen3-30B-A3B-FP8 \
    --dataset-name sharegpt --num-prompts 100 \
    --dataset-path datasets/ShareGPT_V3_unfiltered_cleaned_split.json \
    --sharegpt-output-len 512 --seed 42 --request-rate 5" \
  --baselines uvm_baseline \
  --output-dir results
```

### Environment variable overrides

| Variable | Default | Description |
|----------|---------|-------------|
| `VLLM_SERVER_DIR` | `~/workspace/vllm` | vLLM installation directory |
| `LMCACHE_SERVER_DIR` | `~/workspace/gpu/LMCache` | LMCache installation directory |
| `DATASET_PATH` | `datasets/ShareGPT_V3_unfiltered_cleaned_split.json` | ShareGPT dataset path |

## Verification

Results are saved to `results/uvm_baseline_results_YYYYMMDD_HHMMSS.json`.

### Reference results (RTX 5090, 100 prompts)

| Config | Mean TTFT (ms) | Mean TPOT (ms) | Throughput (tok/s) |
|--------|---------------|----------------|-------------------|
| CPU Offload (8GB) | 8387.80 | 324.13 | 391.14 |
| UVM Baseline | 9642.27 | 374.23 | 307.26 |
| **UVM + gpu_ext eBPF** | **5042.22** | **235.68** | **376.53** |
| LMCache | 5401.71 | 222.24 | 571.54 |

gpu_ext should improve TTFT by ~1.7-2x and decoding throughput by ~1.3x over CPU-offload.

## Generate Figures

```bash
cd uvm/first-iter && python generate_figures.py
# produces ttft_tpot_combined.pdf
```

## Directory Structure

```
vllm/
├── Makefile              # Quick bench target + dataset download
├── README.md             # This file
├── uvm/
│   ├── test_uvm_baselines.py   # Main benchmark automation
│   └── first-iter/
│       └── generate_figures.py  # Plot generation
├── docs/                 # Analysis & setup notes
│   ├── RTX5090_setup_lmcache.md
│   └── uvm_test_records.md
├── datasets/             # ShareGPT (gitignored)
└── results/              # Benchmark output JSON + logs
```
