#!/usr/bin/env python3
"""Test PyTorch installation and CUDA functionality"""

import torch
import sys

print("="*60)
print("PyTorch Installation Test")
print("="*60)

# Check PyTorch version
print(f"\nPyTorch version: {torch.__version__}")
print(f"Python version: {sys.version}")

# Check CUDA availability
cuda_available = torch.cuda.is_available()
print(f"\nCUDA available: {cuda_available}")

if cuda_available:
    print(f"CUDA version: {torch.version.cuda}")
    print(f"cuDNN version: {torch.backends.cudnn.version()}")
    print(f"Number of CUDA devices: {torch.cuda.device_count()}")

    for i in range(torch.cuda.device_count()):
        print(f"\nDevice {i}: {torch.cuda.get_device_name(i)}")
        print(f"  Compute capability: {torch.cuda.get_device_capability(i)}")
        props = torch.cuda.get_device_properties(i)
        print(f"  Total memory: {props.total_memory / 1024**3:.2f} GB")
        print(f"  Multi-processor count: {props.multi_processor_count}")
else:
    print("\nCUDA is not available!")
    sys.exit(1)

# Test basic tensor operations
print("\n" + "="*60)
print("Testing basic tensor operations...")
print("="*60)

try:
    # CPU tensor
    cpu_tensor = torch.randn(3, 3)
    print(f"\nCPU tensor:\n{cpu_tensor}")

    # GPU tensor
    gpu_tensor = torch.randn(3, 3, device='cuda')
    print(f"\nGPU tensor:\n{gpu_tensor}")

    # Matrix multiplication on GPU
    result = torch.mm(gpu_tensor, gpu_tensor)
    print(f"\nMatrix multiplication result:\n{result}")

    # Move tensor between CPU and GPU
    cpu_to_gpu = cpu_tensor.to('cuda')
    gpu_to_cpu = gpu_tensor.cpu()
    print("\nSuccessfully moved tensors between CPU and GPU")

    print("\n" + "="*60)
    print("All tests PASSED!")
    print("="*60)

except Exception as e:
    print(f"\nError during testing: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)
