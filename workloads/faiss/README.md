# FAISS Vector Search Benchmark (Experiment 4)

Benchmarks FAISS IVF index build and search with UVM, comparing default driver vs gpu_ext eBPF-optimized UVM on the SIFT dataset.

## Build FAISS

```bash
# Initialize submodule
git submodule update --init faiss

# Build with GPU + Python support
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
```

## Download SIFT Dataset

The SIFT1B dataset must be placed at `faiss/benchs/bigann/`:

```bash
# Download from http://corpus-texmex.irisa.fr/
# Required files (~60 GB total):
#   bigann_base.bvecs   (46 GB, 375M vectors)
#   bigann_learn.bvecs  (13 GB, 100M training vectors)
#   bigann_query.bvecs  (1.3 MB, 10K queries)
#   gnd/                (ground truth files)
```

See `docs/DATASET.md` for format details and download instructions.

## Running Benchmarks

```bash
# UVM baseline (no eBPF)
uv run python bench_gpu_1bn.py SIFT50M IVF4096,Flat -nprobe 1,4,16 -uvm \
  2>&1 | tee results/SIFT50M_uvm_baseline.log

uv run python bench_gpu_1bn.py SIFT100M IVF4096,Flat -nprobe 1,4,16 -uvm \
  2>&1 | tee results/SIFT100M_uvm_baseline.log

# UVM + gpu_ext eBPF (load adaptive prefetch policy first)
sudo bpftool struct_ops register /path/to/gpu_ext/extension/.output/prefetch_adaptive_sequential.bpf.o

uv run python bench_gpu_1bn.py SIFT50M IVF4096,Flat -nprobe 1,4,16 -uvm \
  2>&1 | tee results/SIFT50M_uvm_ebpf.log

uv run python bench_gpu_1bn.py SIFT100M IVF4096,Flat -nprobe 1,4,16 -uvm \
  2>&1 | tee results/SIFT100M_uvm_ebpf.log
```

Other useful options: `-abs N` (add block size), `-qbs N` (query block size), `-nocache` (skip cached indices).

## Verification

Key metrics: **Add time** (index build) and **search time** per nprobe level.

### Reference results (RTX 5090, SIFT100M)

| Metric | UVM Baseline | UVM + gpu_ext | Improvement |
|--------|-------------|--------------|-------------|
| Add time | 68.41 s | 49.31 s | 28% faster |
| Search (nprobe=1) | 5.14 s | 4.53 s | 12% faster |
| Search (nprobe=4) | 14.39 s | 13.11 s | 9% faster |
| Search (nprobe=16) | 56.51 s | 51.44 s | 9% faster |

gpu_ext reduces build time by 21-29% and query latency by 10-16%.

## Generate Figures

```bash
cd results && python vis_faiss.py
# produces faiss_benchmark_results.pdf
```

## Directory Structure

```
faiss/
├── README.md             # This file
├── bench_gpu_1bn.py      # GPU benchmark (with -uvm flag)
├── bench_cpu_1bn.py      # CPU benchmark (for comparison)
├── faiss/                # [submodule] eunomia-bpf/faiss
│   └── benchs/bigann/   # SIFT dataset location
├── docs/                 # Dataset docs & analysis
│   ├── DATASET.md
│   ├── cpu-gpu-analysis.md
│   ├── gpu-kernel-prefetch-analysis.md
│   └── ivf-flat-prefetch-optimization.md
└── results/              # Benchmark logs & visualization
    └── vis_faiss.py
```
