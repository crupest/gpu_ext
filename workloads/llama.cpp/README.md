# llama.cpp Expert Offloading Benchmark (Experiment 1)

Benchmarks llama.cpp with GPT-OSS-120B MoE model (~59 GiB) on a 32GB GPU, comparing framework CPU offloading vs UVM with gpu_ext eBPF policies.

## Build

```bash
# Initialize submodule
git submodule update --init llama.cpp

# Build with CUDA support (uses GCC-12 for CUDA 12.9 compatibility)
make build-cuda
```

## Download Model & Dataset

```bash
# GPT-OSS-120B (~59 GiB, cached to ~/.cache/llama.cpp/)
make download-models

# ShareGPT dataset
python download_sharegpt.py
```

Override model path: `MODEL_120B_CACHE=/your/path make bench-120b-uvm`

## Running Benchmarks

Five configurations to compare:

```bash
MODEL=~/.cache/llama.cpp/ggml-org_gpt-oss-120b-GGUF_gpt-oss-120b-mxfp4-00001-of-00003.gguf

# Config 1: Framework CPU offload (ncmoe=64)
./build/bin/llama-bench -ncmoe 64 -m $MODEL 2>&1 | tee results/ncmoe64.log

# Config 2: Framework CPU offload (ncmoe=32)
./build/bin/llama-bench -ncmoe 32 -m $MODEL 2>&1 | tee results/ncmoe32.log

# Config 3: UVM baseline (no policy)
GGML_CUDA_ENABLE_UNIFIED_MEMORY=1 ./build/bin/llama-bench -m $MODEL \
  2>&1 | tee results/uvm_baseline.log

# Config 4: UVM + user hints (cudaMemAdvise)
# Requires llama.cpp built with cudaMemAdvise hints enabled
GGML_CUDA_ENABLE_UNIFIED_MEMORY=1 ./build/bin/llama-bench -m $MODEL \
  2>&1 | tee results/uvm_user_hint.log

# Config 5: UVM + gpu_ext eBPF (stride prefetch + LFU eviction)
sudo bpftool struct_ops register /path/to/gpu_ext/extension/.output/prefetch_stride.bpf.o
sudo bpftool struct_ops register /path/to/gpu_ext/extension/.output/eviction_lfu.bpf.o
GGML_CUDA_ENABLE_UNIFIED_MEMORY=1 ./build/bin/llama-bench -m $MODEL \
  2>&1 | tee results/uvm_ebpf.log
```

Or use the Makefile shortcut:
```bash
make bench-120b-uvm
```

## Verification

Key metrics: `pp512` = prefill throughput (tok/s), `tg128` = decode throughput (tok/s).

### Reference results (RTX 5090)

| Config | pp512 (tok/s) | tg128 (tok/s) |
|--------|--------------|--------------|
| ncmoe=64 (framework offload) | 245.63 | 16.34 |
| ncmoe=32 (framework offload) | 260.14 | 18.18 |
| UVM baseline | 238.48 | 7.72 |
| UVM + user hint | 144.00 | 49.31 |
| **UVM + gpu_ext eBPF** | **229.67** | **86.89** |

gpu_ext achieves ~4.8x decode speedup over framework offloading (86.89 vs 18.18 tok/s).

## Generate Figures

```bash
cd uvm && python visbasic.py
# produces llama_uvm_combined_color.pdf
```

## Directory Structure

```
llama.cpp/
├── Makefile                # Build targets + benchmark shortcuts
├── README.md               # This file
├── llama.cpp/              # [submodule] eunomia-bpf/llama.cpp
├── download_sharegpt.py    # Dataset downloader
├── download_test_model.py  # Small model downloader (for quick tests)
├── requirements.txt
├── uvm/                    # UVM test scripts & visualization
│   ├── visbasic.py                  # Figure generation
│   └── plot_colocated_results.py    # Co-location figure
├── docs/                   # Analysis & investigation notes
│   ├── UVM_BUG_INVESTIGATION.md
│   ├── test-record-single.md
│   ├── test-record-co-located.md
│   ├── moe_cuda_implementation_analysis.md
│   ├── moe_offload_analysis.md
│   └── ...
└── results/                # Benchmark output logs
```
