#!/usr/bin/env python3
"""
改进的 FAISS UVM 测试 - 准确显示内存使用情况
主要问题分析：
1. 之前的脚本每次循环都创建新的 numpy 数组，这些数组占用大量系统内存
2. 显示的 "MiB" 只是向量数据本身，不包括索引结构的开销
3. UVM 下 FAISS 在做什么：使用 cudaMallocManaged 分配统一内存，允许数据在 GPU 和 CPU 之间自动迁移
"""

import sys
sys.path.insert(0, '../faiss/build/faiss/python')

import faiss
import numpy as np
import time
import subprocess
import os
import psutil
import threading

# MemorySpace enum
MEMORY_SPACE_TEMPORARY = 0
MEMORY_SPACE_DEVICE = 1
MEMORY_SPACE_UNIFIED = 2

def get_gpu_memory():
    """获取 GPU 内存使用情况（MiB）"""
    try:
        result = subprocess.run(
            ['nvidia-smi', '--query-gpu=memory.used,memory.total', '--format=csv,noheader,nounits'],
            capture_output=True, text=True, check=True
        )
        used, total = map(int, result.stdout.strip().split(', '))
        return used, total
    except:
        return 0, 0

def get_process_memory():
    """获取当前进程的内存使用情况（MiB）"""
    process = psutil.Process(os.getpid())
    mem_info = process.memory_info()
    return mem_info.rss / 1024**2  # Convert to MiB

def format_mem(mb):
    """格式化内存显示"""
    if mb < 1024:
        return f"{mb:.0f} MiB"
    else:
        return f"{mb/1024:.1f} GiB"

print("=" * 80)
print("FAISS UVM 详细测试 - 内存使用分析")
print("=" * 80)

ngpu = faiss.get_num_gpus()
print(f"GPU 数量: {ngpu}")
if ngpu == 0:
    print("没有可用的 GPU！")
    sys.exit(1)

gpu_used, gpu_total = get_gpu_memory()
print(f"GPU 内存: {format_mem(gpu_used)} / {format_mem(gpu_total)}")
print(f"进程内存: {format_mem(get_process_memory())}")

print("\n" + "=" * 80)
print("测试配置")
print("=" * 80)

d = 128  # 维度
# 使用更小的测试规模来快速演示，但足够触发 OOM
test_vectors = 70_000_000  # 70M vectors = ~34 GiB，超过 GPU 的 32 GiB

print(f"维度: {d}")
print(f"向量数: {test_vectors:,}")
print(f"单个向量大小: {d * 4} bytes")
print(f"理论数据大小: {format_mem(test_vectors * d * 4 / 1024**2)}")
print(f"注意：实际内存占用会更大，因为索引结构有额外开销")

# 预先创建测试数据，避免在循环中重复分配
batch_size = 5_000_000  # 5M vectors per batch
num_batches = test_vectors // batch_size
nq = 100

print(f"\n预先分配测试数据...")
print(f"批次大小: {batch_size:,} vectors")
print(f"批次数: {num_batches}")

# 只创建一个 batch 的数据，重复使用
xb_batch = np.random.random((batch_size, d)).astype('float32')
xq = np.random.random((nq, d)).astype('float32')

print(f"测试数据占用内存: {format_mem(xb_batch.nbytes / 1024**2)}")
print(f"当前进程内存: {format_mem(get_process_memory())}")

# Test 1: Device Memory (应该 OOM)
print("\n" + "=" * 80)
print("测试 1: 标准 GPU 内存（Device Memory）")
print("=" * 80)

config_device = faiss.GpuIndexFlatConfig()
config_device.device = 0
config_device.memorySpace = MEMORY_SPACE_DEVICE

res = faiss.StandardGpuResources()
index_device = faiss.GpuIndexFlatL2(res, d, config_device)

oom_occurred = False
gpu_used_before, _ = get_gpu_memory()
proc_mem_before = get_process_memory()

print(f"开始前 - GPU: {format_mem(gpu_used_before)}, 进程: {format_mem(proc_mem_before)}")

try:
    print(f"\n添加 {test_vectors:,} 个向量（{num_batches} 批次）...")
    start = time.time()

    for i in range(num_batches):
        index_device.add(xb_batch)

        if (i + 1) % 2 == 0 or i == 0:
            elapsed = time.time() - start
            vectors_added = (i + 1) * batch_size
            gpu_used, _ = get_gpu_memory()
            proc_mem = get_process_memory()

            print(f"  批次 {i+1}/{num_batches}: {vectors_added:,} vectors")
            print(f"    GPU 内存: {format_mem(gpu_used)} (增加 {format_mem(gpu_used - gpu_used_before)})")
            print(f"    进程内存: {format_mem(proc_mem)} (增加 {format_mem(proc_mem - proc_mem_before)})")
            print(f"    耗时: {elapsed:.1f}s")

    add_time = time.time() - start
    gpu_used_after, _ = get_gpu_memory()

    print(f"\n✓ 添加完成！耗时 {add_time:.1f}s")
    print(f"  索引向量数: {index_device.ntotal:,}")
    print(f"  GPU 内存使用: {format_mem(gpu_used_after)} (增加 {format_mem(gpu_used_after - gpu_used_before)})")

    # 测试搜索
    print("\n执行搜索...")
    start = time.time()
    D, I = index_device.search(xq, 10)
    search_time = time.time() - start
    print(f"✓ 搜索完成，耗时 {search_time:.3f}s")

except RuntimeError as e:
    oom_occurred = True
    error_msg = str(e)
    print(f"\n✗ 预期的 OOM 错误！")
    print(f"  错误信息: {error_msg[:150]}...")

    gpu_used_fail, _ = get_gpu_memory()
    proc_mem_fail = get_process_memory()
    print(f"  失败时 GPU 内存: {format_mem(gpu_used_fail)}")
    print(f"  失败时进程内存: {format_mem(proc_mem_fail)}")

    if "out of memory" in error_msg.lower() or "cudamalloc" in error_msg.lower():
        print(f"  ✓ 这是预期的 GPU 内存不足错误")

# 清理
del index_device
import gc
gc.collect()
time.sleep(1)

# Test 2: Unified Memory (UVM)
print("\n" + "=" * 80)
print("测试 2: 统一内存（Unified Memory / UVM）")
print("=" * 80)
print("UVM 工作原理：")
print("  - 使用 cudaMallocManaged() 分配内存")
print("  - 数据可以在 GPU 和系统 RAM 之间自动迁移")
print("  - GPU 访问不在显存中的数据时，自动从系统内存页面迁移")
print("  - 允许使用超过 GPU 显存大小的数据集")
print()

config_unified = faiss.GpuIndexFlatConfig()
config_unified.device = 0
config_unified.memorySpace = MEMORY_SPACE_UNIFIED

gpu_used_before, _ = get_gpu_memory()
proc_mem_before = get_process_memory()

print(f"开始前 - GPU: {format_mem(gpu_used_before)}, 进程: {format_mem(proc_mem_before)}")

try:
    index_unified = faiss.GpuIndexFlatL2(res, d, config_unified)

    print(f"\n添加 {test_vectors:,} 个向量（{num_batches} 批次）使用 UVM...")
    start = time.time()

    for i in range(num_batches):
        index_unified.add(xb_batch)

        if (i + 1) % 2 == 0 or i == 0:
            elapsed = time.time() - start
            vectors_added = (i + 1) * batch_size
            gpu_used, _ = get_gpu_memory()
            proc_mem = get_process_memory()

            print(f"  批次 {i+1}/{num_batches}: {vectors_added:,} vectors")
            print(f"    GPU 内存: {format_mem(gpu_used)} (增加 {format_mem(gpu_used - gpu_used_before)})")
            print(f"    进程内存: {format_mem(proc_mem)} (增加 {format_mem(proc_mem - proc_mem_before)})")
            print(f"    耗时: {elapsed:.1f}s")

            # 当 GPU 内存超过阈值时提示
            if gpu_used > gpu_total * 0.9:
                print(f"    ⚠️  GPU 内存接近上限，UVM 开始使用系统 RAM")

    add_time = time.time() - start
    gpu_used_after, _ = get_gpu_memory()
    proc_mem_after = get_process_memory()

    print(f"\n✓ 添加完成！耗时 {add_time:.1f}s")
    print(f"  索引向量数: {index_unified.ntotal:,}")
    print(f"  GPU 内存: {format_mem(gpu_used_after)} (增加 {format_mem(gpu_used_after - gpu_used_before)})")
    print(f"  进程内存: {format_mem(proc_mem_after)} (增加 {format_mem(proc_mem_after - proc_mem_before)})")
    print(f"  注意：进程内存增加包含了 UVM 分配的统一内存")

    # 测试搜索（UVM 下可能较慢，因为需要页面迁移）
    print("\n执行搜索（可能较慢，因为需要 GPU<->RAM 数据迁移）...")
    start = time.time()
    D, I = index_unified.search(xq, 10)
    search_time = time.time() - start
    print(f"✓ 搜索完成，耗时 {search_time:.3f}s")

    print("\n" + "=" * 80)
    print("测试结果总结")
    print("=" * 80)
    if oom_occurred:
        print("✓ Device Memory: OOM（GPU 显存不足）")
        print("✓ Unified Memory: 成功（使用 UVM 自动管理内存）")
        print("\n关键发现：")
        print("  - UVM 允许索引大小超过 GPU 显存")
        print("  - 数据自动在 GPU 和系统 RAM 之间迁移")
        print("  - 性能会受影响（需要页面迁移），但避免了 OOM")
    else:
        print("✓ Device Memory: 成功（GPU 显存足够）")
        print("✓ Unified Memory: 成功")
        print("\n注意：GPU 显存足够大，未触发 OOM")
        print("  可以增加 test_vectors 来触发 OOM 场景")

except Exception as e:
    print(f"\n✗ UVM 测试失败: {e}")
    print("\n可能原因：")
    print("  1. GPU 不支持 UVM (需要 Compute Capability 6.0+)")
    print("  2. 系统内存不足")
    print("  3. FAISS 编译时未启用 UVM 支持")
    import traceback
    traceback.print_exc()
