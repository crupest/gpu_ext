# PyTorch GNN Training Benchmark (Experiment 3)

Benchmarks Graph Neural Network (GCN) training with CUDA Unified Virtual Memory, comparing default UVM vs gpu_ext eBPF-optimized UVM.

No external dataset needed -- graphs are randomly generated.

## Build

```bash
# Build custom CUDA allocators
make all    # produces uvm_allocator.so and gpu_allocator.so

# Create venv and install deps
uv venv && source .venv/bin/activate
uv pip install torch psutil torch-geometric
```

## Running Benchmarks

Three configurations, each across multiple node counts:

### Config 1: No UVM baseline (pure GPU, OOMs beyond ~7M nodes)

```bash
for NODES in 1000000 3000000 5000000 7000000; do
  uv run python benchmark_gnn_uvm.py --dataset random --nodes $NODES \
    --edges_per_node 10 --features 128 --hidden 256 \
    --epochs 2 --warmup 1 --prop chunked --use_gpu_allocator \
    --report_json without-user-prefetch/result_no_uvm1/${NODES}.json
done
```

### Config 2: UVM baseline (default driver, no eBPF)

```bash
for NODES in 5000000 7000000 8000000 10000000 12000000 15000000; do
  CUDA_MANAGED_FORCE_DEVICE_ALLOC=1 uv run python benchmark_gnn_uvm.py \
    --dataset random --nodes $NODES \
    --edges_per_node 10 --features 128 --hidden 256 \
    --epochs 2 --warmup 1 --prop chunked --use_uvm \
    --report_json without-user-prefetch/result_uvm_baseline1/${NODES}.json
done
```

### Config 3: UVM + gpu_ext eBPF

```bash
# Load sequential prefetch policy
sudo bpftool struct_ops register /path/to/gpu_ext/extension/.output/prefetch_adaptive_sequential.bpf.o

for NODES in 5000000 7000000 8000000 10000000 12000000 15000000; do
  CUDA_MANAGED_FORCE_DEVICE_ALLOC=1 uv run python benchmark_gnn_uvm.py \
    --dataset random --nodes $NODES \
    --edges_per_node 10 --features 128 --hidden 256 \
    --epochs 2 --warmup 1 --prop chunked --use_uvm \
    --report_json without-user-prefetch/result_uvm_ebpf1/${NODES}.json
done
```

For the "with-user-prefetch" variant (adds `cudaMemPrefetchAsync` in the allocator), repeat all three configs saving to `with-user-prefetch/` subdirectories.

## Verification

Each run produces a JSON file with `avg_epoch_time` as the key metric.

### Reference results (RTX 5090, without user prefetch)

| Nodes | No UVM | UVM Baseline | UVM + gpu_ext | Speedup |
|-------|--------|-------------|--------------|---------|
| 5M | 1.14s | 34.23s | 12.76s | 2.68x |
| 7M | 1.79s | 48.28s | 17.81s | 2.71x |
| 8M | OOM | 55.36s | 20.51s | 2.70x |
| 10M | OOM | 70.06s | 26.47s | 2.65x |
| 12M | OOM | 93.71s | 39.74s | 2.36x |
| 15M | OOM | 292.77s | 168.73s | 1.74x |

gpu_ext achieves ~2.65x speedup without user prefetch, ~1.44x additional with user prefetch at 15M nodes.

## Generate Figures

```bash
python visualize_all.py
# produces uvm_benchmark_comparison.pdf
```

## Directory Structure

```
pytorch/
├── Makefile                 # Build allocator .so files
├── README.md                # This file
├── benchmark_gnn_uvm.py     # Main benchmark script
├── visualize_all.py          # Figure generation
├── uvm_allocator.c           # Custom UVM CUDA allocator
├── gpu_allocator.c           # Pure GPU allocator (for baseline)
├── requirements.txt
├── with-user-prefetch/       # Results with cudaMemPrefetchAsync
├── without-user-prefetch/    # Results without user prefetch
└── docs/                     # Analysis notes
    ├── UVM_EVALUATION.md
    └── GCN_BENCHMARK.md
```
