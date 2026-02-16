#!/usr/bin/env python3
"""
Test CUDA Unified Virtual Memory (UVM) to avoid OOM errors.
This script demonstrates that UVM allows using more memory than available on GPU.
"""

import sys
sys.path.insert(0, 'build/faiss/python')

import faiss
import numpy as np
import time
import subprocess

# MemorySpace enum values (from GpuResources.h)
MEMORY_SPACE_TEMPORARY = 0
MEMORY_SPACE_DEVICE = 1
MEMORY_SPACE_UNIFIED = 2

print("=" * 70)
print("FAISS GPU UVM OOM Test")
print("=" * 70)

# Check GPU availability and memory
ngpu = faiss.get_num_gpus()
print(f"Number of GPUs: {ngpu}")

if ngpu == 0:
    print("No GPU available!")
    sys.exit(1)

# Get GPU memory info
try:
    result = subprocess.run(['nvidia-smi', '--query-gpu=name,memory.total,memory.free',
                           '--format=csv,noheader,nounits'],
                          capture_output=True, text=True)
    gpu_info = result.stdout.strip().split(', ')
    gpu_name = gpu_info[0]
    total_mem_mb = int(gpu_info[1])
    free_mem_mb = int(gpu_info[2])

    print(f"GPU: {gpu_name}")
    print(f"Total memory: {total_mem_mb} MiB ({total_mem_mb/1024:.1f} GiB)")
    print(f"Free memory: {free_mem_mb} MiB ({free_mem_mb/1024:.1f} GiB)")
except:
    print("Could not get GPU memory info")
    total_mem_mb = 32000  # Assume 32GB for RTX 5090
    free_mem_mb = 30000

print("\n" + "=" * 70)
print("Test Strategy:")
print("=" * 70)
print("We will create an index with enough vectors to exceed GPU memory.")
print("Test 1: Device memory - should cause OOM")
print("Test 2: Unified memory (UVM) - should succeed by using system RAM")
print()

# Calculate test size to cause OOM on device memory
# Each vector: 128 dimensions * 4 bytes (float32) = 512 bytes
# Plus index overhead
d = 128
bytes_per_vector = d * 4
# Target: use ~40GB to exceed the GPU's 32GB
# But for faster testing, let's use a size that's 1.5x the GPU memory
target_vectors = int((free_mem_mb * 1.5 * 1024 * 1024) / bytes_per_vector)
# Cap at reasonable size for testing
target_vectors = min(target_vectors, 150_000_000)  # 150M vectors max

print(f"Test configuration:")
print(f"  Dimension: {d}")
print(f"  Target vectors: {target_vectors:,} (~{target_vectors * bytes_per_vector / 1024**3:.1f} GiB)")
print(f"  Bytes per vector: {bytes_per_vector}")
print(f"  Estimated memory needed: {target_vectors * bytes_per_vector / 1024**2:.0f} MiB")
print()

# Create large dataset in batches
print("Generating test data...")
batch_size = 1_000_000
num_batches = target_vectors // batch_size
nq = 100  # queries

xq = np.random.random((nq, d)).astype('float32')

# Test 1: Device Memory (should OOM)
print("\n" + "=" * 70)
print("Test 1: Standard Device Memory (expecting OOM...)")
print("=" * 70)

config_device = faiss.GpuIndexFlatConfig()
config_device.device = 0
config_device.memorySpace = MEMORY_SPACE_DEVICE

res = faiss.StandardGpuResources()
index_device = faiss.GpuIndexFlatL2(res, d, config_device)

oom_occurred = False
try:
    print(f"Adding {target_vectors:,} vectors in {num_batches} batches...")
    start = time.time()

    for i in range(num_batches):
        xb_batch = np.random.random((batch_size, d)).astype('float32')
        index_device.add(xb_batch)

        if (i + 1) % 10 == 0:
            elapsed = time.time() - start
            vectors_added = (i + 1) * batch_size
            print(f"  Added {vectors_added:,} vectors ({vectors_added * bytes_per_vector / 1024**2:.0f} MiB) "
                  f"in {elapsed:.1f}s")

    add_time = time.time() - start
    print(f"\n✓ Add completed in {add_time:.1f}s")
    print(f"  Total vectors: {index_device.ntotal:,}")

    # Try search
    print("Performing search...")
    start = time.time()
    D, I = index_device.search(xq, 10)
    search_time = time.time() - start
    print(f"✓ Search completed in {search_time:.3f}s")

except RuntimeError as e:
    oom_occurred = True
    error_msg = str(e)
    print(f"\n✗ OOM Error occurred as expected!")
    print(f"  Error: {error_msg[:200]}")
    if "out of memory" in error_msg.lower() or "cudamalloc" in error_msg.lower():
        print("  This is the expected out-of-memory error.")
except Exception as e:
    print(f"\n✗ Unexpected error: {e}")

# Test 2: Unified Memory (should work)
print("\n" + "=" * 70)
print("Test 2: Unified Memory (UVM) - should succeed")
print("=" * 70)

config_unified = faiss.GpuIndexFlatConfig()
config_unified.device = 0
config_unified.memorySpace = MEMORY_SPACE_UNIFIED

try:
    index_unified = faiss.GpuIndexFlatL2(res, d, config_unified)

    print(f"Adding {target_vectors:,} vectors in {num_batches} batches with UVM...")
    start = time.time()

    for i in range(num_batches):
        xb_batch = np.random.random((batch_size, d)).astype('float32')
        index_unified.add(xb_batch)

        if (i + 1) % 10 == 0:
            elapsed = time.time() - start
            vectors_added = (i + 1) * batch_size
            print(f"  Added {vectors_added:,} vectors ({vectors_added * bytes_per_vector / 1024**2:.0f} MiB) "
                  f"in {elapsed:.1f}s")

    add_time = time.time() - start
    print(f"\n✓ Add completed in {add_time:.1f}s")
    print(f"  Total vectors: {index_unified.ntotal:,}")

    # Try search
    print("Performing search...")
    start = time.time()
    D, I = index_unified.search(xq, 10)
    search_time = time.time() - start
    print(f"✓ Search completed in {search_time:.3f}s")

    print("\n" + "=" * 70)
    print("RESULT: UVM successfully handled the large dataset!")
    print("=" * 70)
    if oom_occurred:
        print("✓ Device memory: OOM (as expected)")
    else:
        print("✓ Device memory: Succeeded (GPU has enough memory)")
    print("✓ Unified memory: Succeeded (using UVM)")
    print("\nUVM allows the GPU to access system RAM when GPU memory is insufficient.")

except Exception as e:
    print(f"\n✗ UVM test failed: {e}")
    print("\nPossible reasons:")
    print("  1. GPU does not support UVM (requires Compute Capability 6.0+)")
    print("  2. FAISS was not compiled with proper CUDA UVM support")
    print("  3. System does not have enough RAM")
    import traceback
    traceback.print_exc()
