# FAISS Benchmark Setup

This directory contains the FAISS (Facebook AI Similarity Search) library setup for benchmarking vector similarity search on CPU and GPU.

uv run python bench_gpu_1bn.py SIFT100M IVF4096,Flat -nprobe 1,4,16 -uvm
uv run python bench_gpu_1bn.py SIFT100M IVF4096,Flat -nprobe 1,4,16 

 sudo /home/yunwei37/workspace/gpu/co-processor-demo/gpu_ext_policy/src/prefetch_adaptive_tree_iter -m 50 -b 4096

## Test on 5090

baseline

```
$ uv run python bench_gpu_1bn.py SIFT100M IVF4096,Flat -nprobe 1,4,16 -uvm
Preparing dataset SIFT100M
sizes: B (100000000, 128) Q (10000, 128) T (100000000, 128) gt (10000, 1000)
cachefiles:
None
/tmp/bench_gpu_1bn/cent_SIFT100M_IVF4096.npy
/tmp/bench_gpu_1bn/SIFT100M_IVF4096,Flat.index
preparing resources for 1 GPUs
load centroids /tmp/bench_gpu_1bn/cent_SIFT100M_IVF4096.npy
making an IVFFlat index
Training vector codes
  done 0.058 s
add...
99975168/100000000 (68.407 s)   Add time: 68.407 s
search...
0/10000 (0.003 s)      probe=1  : 5.135 s 1-R@1: 0.4486 1-R@10: 0.4488 
0/10000 (0.003 s)      probe=4  : 14.393 s 1-R@1: 0.7655 1-R@10: 0.7659 
0/10000 (0.003 s)      probe=16 : 56.511 s 1-R@1: 0.9476 1-R@10: 0.9477
```

sudo /home/yunwei37/workspace/gpu/co-processor-demo/gpu_ext_policy/src/prefetch_adaptive_tree_iter -M 50 -b 4096

```
 uv run python bench_gpu_1bn.py SIFT100M IVF4096,Flat -nprobe 1,4,16 -uvm
Preparing dataset SIFT100M
sizes: B (100000000, 128) Q (10000, 128) T (100000000, 128) gt (10000, 1000)
cachefiles:
None
/tmp/bench_gpu_1bn/cent_SIFT100M_IVF4096.npy
/tmp/bench_gpu_1bn/SIFT100M_IVF4096,Flat.index
preparing resources for 1 GPUs
load centroids /tmp/bench_gpu_1bn/cent_SIFT100M_IVF4096.npy
making an IVFFlat index
Training vector codes
  done 0.053 s
add...
99975168/100000000 (49.307 s)   Add time: 49.309 s
search...
0/10000 (0.003 s)      probe=1  : 4.532 s 1-R@1: 0.4486 1-R@10: 0.4488 
0/10000 (0.003 s)      probe=4  : 13.106 s 1-R@1: 0.7655 1-R@10: 0.7659 
0/10000 (0.004 s)      probe=16 : 51.440 s 1-R@1: 0.9476 1-R@10: 0.9477 
yunwei37@lab:~/workspace/gpu/schedcp/workloads/faiss$ 
```

## Directory Structure

```
faiss/
├── faiss/                      # FAISS source code (from eunomia-bpf/faiss)
│   ├── build/                  # Build artifacts
│   │   ├── faiss/libfaiss.a   # FAISS library with CUDA support
│   │   └── demos/              # Demo binaries
│   └── benchs/                 # Benchmark scripts and datasets
│       ├── bigann/             # SIFT1B dataset files
│       │   ├── bigann_base.bvecs    # Base vectors (375.5M vectors, 46GB)
│       │   ├── bigann_learn.bvecs   # Training vectors (100M vectors, 13GB)
│       │   ├── bigann_query.bvecs   # Query vectors (10K vectors, 1.3MB)
│       │   └── gnd/                 # Ground truth files
│       └── bench_polysemous_1bn.py  # Main CPU benchmark script
└── .venv/                      # Python virtual environment (managed by uv)
```

## Build Information

### Prerequisites
- CUDA 12.9+ with nvcc
- CMake 3.20+
- OpenBLAS (for BLAS/LAPACK)
- SWIG 4.0+ (for Python bindings)
- Python 3.12

### Build Configuration
```bash
cmake -B build \
  -DFAISS_ENABLE_GPU=ON \
  -DFAISS_ENABLE_PYTHON=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DFAISS_OPT_LEVEL=avx2 \
  -DBUILD_TESTING=OFF \
  -DBLA_VENDOR=OpenBLAS

make -C build -j8 swigfaiss
```

### Python Environment Setup
```bash
# Create virtual environment with uv
cd /home/yunwei37/workspace/gpu/schedcp/workloads/faiss
uv venv

# Activate and install dependencies
source .venv/bin/activate
uv pip install "numpy<2.0"

# Install FAISS Python bindings
cd faiss/build/faiss/python
uv pip install -e .
```

## Dataset Information

### SIFT1B Dataset
- **Source**: http://corpus-texmex.irisa.fr/
- **Format**: bvecs (binary vectors with 128 dimensions, uint8)
- **Current Status**: 375.5M vectors available (out of 1B total)

**File Sizes:**
- `bigann_base.bvecs`: 46.16 GB (375,489,504 vectors)
- `bigann_learn.bvecs`: 13 GB (100M vectors for training)
- `bigann_query.bvecs`: 1.3 MB (10K query vectors)
- `gnd/*.ivecs`: Ground truth files for SIFT1M to SIFT1000M

**Available Benchmark Scales:**
- SIFT1M: 1 million vectors (~0.12 GB)
- SIFT10M: 10 million vectors (~1.23 GB)
- SIFT100M: 100 million vectors (~12.3 GB)
- SIFT200M: 200 million vectors (~24.6 GB)
- SIFT300M: 300 million vectors (~36.9 GB)
- Up to SIFT375M with current dataset

## Running Benchmarks

### Activate Environment
```bash
cd /home/yunwei37/workspace/gpu/schedcp/workloads/faiss
source .venv/bin/activate
cd faiss/benchs
```

### CPU Benchmarks

**SIFT10M with IMI index:**
```bash
python bench_polysemous_1bn.py SIFT10M IMI2x12,PQ16 nprobe=16
```

**Output Example:**
```
Training time: 153.1s
Add time: 71.7s (10M vectors)
Query performance (nprobe=16):
  R@1: 0.2617 (26.17% accuracy)
  R@10: 0.3889 (38.89% recall)
  R@100: 0.3960 (39.60% recall)
  Time: 0.051s (51ms for 10K queries)
```

**Other Index Types:**
- `Flat`: Exact search (baseline, slow)
- `IVF4096,Flat`: Inverted file with flat vectors
- `IVF4096,PQ16`: Inverted file with product quantization
- `IMI2x12,PQ16`: Multi-index with product quantization
- `OPQ16_64,IMI2x12,PQ16`: Optimized product quantization

**Parameter Tuning:**
- `nprobe=N`: Search N nearest inverted lists (higher = more accurate, slower)
- Examples: `nprobe=1`, `nprobe=16`, `nprobe=64`, `nprobe=256`

### GPU Benchmarks

#### Testing GPU Availability
```bash
python -c "
import faiss
print('GPU support:', hasattr(faiss, 'StandardGpuResources'))
if hasattr(faiss, 'get_num_gpus'):
    print('GPUs available:', faiss.get_num_gpus())
"
```

#### GPU Device Memory Mode
```bash
# Standard GPU mode - uses GPU VRAM only
uv run python bench_gpu_1bn.py SIFT10M IVF4096,Flat -nprobe 1,4,16,64
```

**Common Options:**
- `-nprobe 1,4,16,64`: Test multiple nprobe values
- `-abs N`: Split adds into blocks of N vectors
- `-qbs N`: Split queries into blocks of N vectors
- `-nocache`: Don't use cached indices

**Example Output (SIFT10M, IVF4096,Flat):**
```
Training time: 0.056s
Add time: 3-10s (10M vectors to GPU)

Query results (10K queries):
  nprobe=1:  ~0.05s  (1-R@1: 0.40  = 40% recall)
  nprobe=4:  ~0.10s  (1-R@1: 0.72  = 72% recall)
  nprobe=16: ~0.30s  (1-R@1: 0.92  = 92% recall)
  nprobe=64: ~1.00s  (1-R@1: 0.99  = 99% recall)
```

#### GPU UVM Mode
```bash
# GPU with Unified Virtual Memory - can exceed GPU VRAM
uv run python bench_gpu_1bn.py SIFT100M IVF4096,Flat -uvm -nprobe 1,4,16,64
```

**UVM-Specific Notes:**
- Allows indexing datasets larger than GPU VRAM
- Automatically uses system RAM when GPU memory is full
- Performance degrades gracefully when exceeding VRAM
- Requires CUDA Compute Capability 6.0+ (Pascal or newer)

### Memory-Mapped File Handling

FAISS uses memory-mapped files for large datasets, meaning:
- Files are not loaded entirely into RAM
- Only accessed portions are loaded on-demand
- Can run benchmarks with partial dataset downloads
- Script automatically trims data: `xb = xb[:dbsize * 1000 * 1000]`

## Performance Results

### CPU vs GPU Performance Comparison (SIFT10M)

#### CPU Benchmark
**Index:** IMI2x12,PQ16 (Multi-Index with Product Quantization)
**Configuration:** nprobe=16

| Metric | Value |
|--------|-------|
| Training Time | 153.1s |
| Indexing Time | 71.7s |
| Query Time (10K queries) | 51ms |
| R@1 | 26.17% |
| R@10 | 38.89% |
| R@100 | 39.60% |

#### GPU Benchmark
**Index:** IVF4096,Flat (Inverted File Index with exact distances)
**Configuration:** Various nprobe values

| Metric | nprobe=1 | nprobe=4 | nprobe=16 | nprobe=64 |
|--------|----------|----------|-----------|-----------|
| Training Time | 9.6s | - | - | - |
| Indexing Time | 2.1s | - | - | - |
| Query Time (10K queries) | 3.3s | 37ms | 130ms | 491ms |
| 1-R@1 (Recall) | 40.5% | 71.8% | 92.1% | 99.0% |

**Key Observations:**
- **GPU indexing is ~34x faster** than CPU (2.1s vs 71.7s)
- **GPU training is ~16x faster** than CPU (9.6s vs 153.1s)
- GPU query speed varies greatly with nprobe:
  - nprobe=4: 37ms (71.8% recall) - **Best speed/accuracy tradeoff**
  - nprobe=16: 130ms (92.1% recall) - High accuracy
  - nprobe=64: 491ms (99.0% recall) - Near-perfect recall
- CPU uses compressed index (PQ16) for memory efficiency
- GPU uses exact distances (Flat) for higher accuracy

## Troubleshooting

### NumPy Version Compatibility
If you get NumPy compatibility errors:
```bash
uv pip install "numpy<2.0"
```

### File Format Issues
If you get reshape errors, the file may be truncated. Fix with:
```bash
python3 -c "
import os
size = os.path.getsize('bigann/bigann_base.bvecs')
vector_size = 132  # 4 bytes (dim) + 128 bytes (data)
complete_size = (size // vector_size) * vector_size
os.truncate('bigann/bigann_base.bvecs', complete_size)
print(f'Truncated to {complete_size} bytes')
"
```

### CUDA/GPU Issues
- Verify CUDA installation: `nvcc --version`
- Check GPU availability: `nvidia-smi`
- Ensure libfaiss.a was built with CUDA support

## Performance Comparison: CPU vs GPU vs UVM

### Three Benchmark Modes

This directory provides three benchmark scripts for comprehensive performance comparison:

1. **`bench_cpu_1bn.py`** - Pure CPU mode using system RAM
2. **`bench_gpu_1bn.py`** - GPU mode with Device memory (standard VRAM)
3. **`bench_gpu_1bn.py -uvm`** - GPU mode with Unified Virtual Memory

### Running Benchmarks

#### CPU Benchmark
```bash
cd /home/yunwei37/workspace/gpu/schedcp/workloads/faiss

# Run CPU benchmark with SIFT10M
uv run python bench_cpu_1bn.py SIFT10M IVF4096,Flat -nprobe 1,4,16,64

# Run CPU benchmark with SIFT100M
uv run python bench_cpu_1bn.py SIFT100M IVF4096,Flat -nprobe 1,4,16,64
```

**Performance characteristics:**
- Slowest for indexing (~36s for 10M vectors)
- Slowest for search (~4.3s for 10K queries with nprobe=16)
- ✅ Can handle datasets larger than GPU VRAM
- ✅ No GPU required

#### GPU Device Memory Benchmark
```bash
# Run GPU benchmark with standard device memory
uv run python bench_gpu_1bn.py SIFT10M IVF4096,Flat -nprobe 1,4,16,64

# Run with SIFT100M (requires 24GB+ VRAM)
uv run python bench_gpu_1bn.py SIFT100M IVF4096,Flat -nprobe 1,4,16,64
```

**Performance characteristics:**
- Fastest for indexing (~3-10s for 10M vectors)
- Fastest for search (~0.1-0.5s for 10K queries)
- ❌ Limited by GPU VRAM size
- ❌ Will OOM if dataset exceeds VRAM

#### GPU UVM Benchmark
```bash
# Run GPU benchmark with Unified Virtual Memory
uv run python bench_gpu_1bn.py SIFT10M IVF4096,Flat -uvm -nprobe 1,4,16,64

# Run with SIFT100M (can exceed GPU VRAM)
uv run python bench_gpu_1bn.py SIFT100M IVF4096,Flat -uvm -nprobe 1,4,16,64
```

**Performance characteristics:**
- Medium speed for indexing (~10-30s for 10M vectors)
- Medium speed for search (~0.2-1s for 10K queries)
- ✅ Can handle datasets larger than GPU VRAM
- ✅ Uses GPU acceleration when data fits in VRAM
- ⚠️ Performance depends on GPU-CPU memory transfers

### Performance Comparison Table

Based on SIFT10M benchmark (10 million 128-dim vectors):

| Mode | Add Time | Search Time (nprobe=16) | Memory Used | Max Dataset Size |
|------|----------|-------------------------|-------------|------------------|
| **CPU** | ~36s | ~4.3s | System RAM | Up to RAM (~256GB) |
| **GPU Device** | ~3-10s | ~0.1-0.5s | GPU VRAM | Up to VRAM (~32GB) |
| **GPU UVM** | ~10-30s | ~0.2-1s | GPU VRAM + RAM | Up to RAM (~256GB) |

### Use Case Recommendations

**Use CPU mode when:**
- No GPU available
- Dataset size > GPU VRAM and performance is not critical
- Running on cloud instances without GPU

**Use GPU Device mode when:**
- Dataset fits in GPU VRAM
- Maximum performance required
- Real-time search applications

**Use GPU UVM mode when:**
- Dataset larger than GPU VRAM
- Better performance than CPU needed
- Have sufficient system RAM (2-4x dataset size)

## Quick Start with Python Script

A Python benchmark runner (`run_benchmark.py`) is provided for structured benchmark execution with JSON output:

```bash
cd /home/yunwei37/workspace/gpu/schedcp/workloads/faiss

# List all available benchmark configurations
python3 run_benchmark.py --list

# Run all CPU benchmarks
python3 run_benchmark.py --mode cpu

# Run all GPU benchmarks
python3 run_benchmark.py --mode gpu

# Run both CPU and GPU
python3 run_benchmark.py --mode cpu gpu

# Run specific configuration
python3 run_benchmark.py --mode cpu --config SIFT10M-IVF4096

# Save results to custom file
python3 run_benchmark.py --mode gpu --output my_results.json
```

**Script Features:**
- **Structured JSON output** with detailed metrics
- **Multiple pre-configured benchmarks:**
  - CPU: SIFT10M/100M with IVF4096,Flat and IMI2x12,PQ16
  - GPU: SIFT10M/100M with IVF4096,Flat and multiple nprobe values
- **Automatic result parsing** (training time, indexing time, recall, query time)
- **Progress tracking** and summary reports
- **Easy configuration** via Python dictionary at top of script

**Output Format:**
```json
{
  "name": "SIFT10M-IVF4096",
  "mode": "cpu",
  "dataset": "SIFT10M",
  "index": "IVF4096,Flat",
  "parameters": ["nprobe=16"],
  "success": true,
  "elapsed_time": 45.2,
  "metrics": {
    "train_time": 9.6,
    "add_time": 2.1,
    "queries": {
      "nprobe_16": {
        "recall": 0.921,
        "time": 0.130
      }
    }
  },
  "timestamp": "2025-11-21T00:25:00"
}
```

## References

- FAISS GitHub: https://github.com/facebookresearch/faiss
- FAISS Documentation: https://faiss.ai/
- SIFT1B Dataset: http://corpus-texmex.irisa.fr/
- Paper: "Billion-scale similarity search with GPUs" (Johnson et al., 2017)

## Index Types Explained

### IVF (Inverted File Index)
- Partitions vectors into clusters
- Searches only nearest clusters during query
- Trade-off: speed vs accuracy controlled by `nprobe`

### PQ (Product Quantization)
- Compresses vectors into compact codes
- Reduces memory usage and speeds up distance computation
- PQ16 = 16-byte codes (128-dim → 16 bytes = 8x compression)

### IMI (Multi-Index)
- Extension of IVF using multiple independent quantizers
- IMI2x12 = 2 independent 12-bit quantizers (4096 × 4096 = 16M cells)
- More efficient for large-scale datasets

### OPQ (Optimized Product Quantization)
- Learns rotation matrix to improve PQ accuracy
- OPQ16_64 = 16 subquantizers, 64-dim intermediate space
- Better accuracy but slower training
