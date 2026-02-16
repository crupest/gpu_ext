#!/usr/bin/env python3
"""
GraphSAGE Training Benchmark with UVM and Mini-batch Sampling

Implements 3-layer GraphSAGE with neighbor sampling, following SALIENT paper setup.
Tests whether UVM allows training larger graphs with mini-batch approach.

Usage:
    # Without UVM
    python benchmark_graphsage_uvm.py --nodes=10000000

    # With UVM
    CUDA_MANAGED_FORCE_DEVICE_ALLOC=1 python benchmark_graphsage_uvm.py --nodes=10000000 --use_uvm

Configuration:
    --nodes: Number of nodes (default: 10M)
    --edges_per_node: Average edges per node (default: 10)
    --features: Feature dimension (default: 128)
    --hidden: Hidden dimension (default: 256)
    --batch_size: Training batch size (default: 1024)
    --fanout: Neighbor sampling fanout per layer (default: [15,10,5])
    --epochs: Number of epochs (default: 3)
    --use_uvm: Enable UVM allocator
"""

import torch
import torch.nn as nn
import torch.nn.functional as F
from torch_geometric.nn import SAGEConv
from torch_geometric.data import Data
from torch_geometric.loader import NeighborLoader
import time
import argparse
import psutil
import os
import ctypes


class GraphSAGE(nn.Module):
    """3-layer GraphSAGE model following SALIENT/PyG examples"""
    def __init__(self, in_channels, hidden_channels, out_channels):
        super().__init__()
        self.conv1 = SAGEConv(in_channels, hidden_channels)
        self.conv2 = SAGEConv(hidden_channels, hidden_channels)
        self.conv3 = SAGEConv(hidden_channels, out_channels)

    def forward(self, x, edge_index):
        x = self.conv1(x, edge_index)
        x = F.relu(x)
        x = F.dropout(x, p=0.5, training=self.training)

        x = self.conv2(x, edge_index)
        x = F.relu(x)
        x = F.dropout(x, p=0.5, training=self.training)

        x = self.conv3(x, edge_index)
        return x


def generate_random_graph(num_nodes, num_edges, num_features, device='cuda'):
    """
    Generate a random graph in PyG format

    For large graphs, we generate on CPU first to avoid OOM,
    then move to GPU. PyG's Data object will handle the device placement.
    """
    print(f"  Generating graph with {num_nodes:,} nodes and {num_edges:,} edges...")

    # For large graphs, generate on CPU first to avoid initial OOM
    # The data will be moved to GPU in chunks during training via NeighborLoader
    # Use CPU for graphs > 500K nodes to be safe
    gen_device = 'cpu' if num_nodes > 500_000 else device

    print(f"    Generating on {gen_device}...")

    # Random edges (COO format for PyG)
    edge_index = torch.randint(0, num_nodes, (2, num_edges), device=gen_device)

    # Random node features
    x = torch.randn(num_nodes, num_features, device=gen_device)

    # Random labels
    y = torch.randint(0, 10, (num_nodes,), device=gen_device)

    # Create PyG Data object (stays on CPU for large graphs)
    data = Data(x=x, edge_index=edge_index, y=y)

    # Keep data on CPU for large graphs - NeighborLoader will handle GPU transfer
    if gen_device == 'cpu':
        print(f"    Keeping on CPU (NeighborLoader will transfer batches to GPU)")

    return data


def get_memory_stats(uvm_lib=None):
    """Get current memory usage"""
    cpu_used = psutil.Process().memory_info().rss / 1e9

    if uvm_lib is not None:
        # Use UVM allocator's custom statistics
        gpu_allocated = uvm_lib.uvm_get_allocated_bytes() / 1e9
        gpu_peak = uvm_lib.uvm_get_peak_allocated_bytes() / 1e9
        return {
            'gpu_allocated': gpu_allocated,
            'gpu_reserved': gpu_peak,
            'cpu_used': cpu_used,
            'total': gpu_allocated + cpu_used
        }
    else:
        gpu_allocated = torch.cuda.memory_allocated() / 1e9
        gpu_reserved = torch.cuda.memory_reserved() / 1e9
        return {
            'gpu_allocated': gpu_allocated,
            'gpu_reserved': gpu_reserved,
            'cpu_used': cpu_used,
            'total': gpu_allocated + cpu_used
        }


def print_memory(prefix="", uvm_lib=None):
    """Print current memory usage"""
    mem = get_memory_stats(uvm_lib)
    print(f"    {prefix}GPU: {mem['gpu_allocated']:.1f}GB / {mem['gpu_reserved']:.1f}GB, "
          f"CPU: {mem['cpu_used']:.1f}GB, Total: {mem['total']:.1f}GB")


def train_epoch(model, loader, optimizer, device, uvm_lib=None):
    """Train for one epoch using mini-batch"""
    model.train()

    total_loss = 0
    total_batches = 0

    for batch in loader:
        batch = batch.to(device)
        optimizer.zero_grad()

        # Forward
        out = model(batch.x, batch.edge_index)

        # Loss on batch nodes only (exclude sampled neighbors)
        loss = F.cross_entropy(out[:batch.batch_size], batch.y[:batch.batch_size])

        # Backward
        loss.backward()
        optimizer.step()

        total_loss += loss.item()
        total_batches += 1

    return total_loss / total_batches if total_batches > 0 else 0


def enable_uvm_allocator():
    """Enable UVM allocator and return library handle for statistics"""
    so_path = os.path.join(os.path.dirname(__file__), 'uvm_allocator.so')

    if not os.path.exists(so_path):
        raise FileNotFoundError(
            f"UVM allocator not found at {so_path}\n"
            f"Please run 'make' first to build uvm_allocator.so"
        )

    # Load the library for statistics access
    uvm_lib = ctypes.CDLL(so_path)

    # Define function signatures
    uvm_lib.uvm_get_allocated_bytes.restype = ctypes.c_size_t
    uvm_lib.uvm_get_allocated_bytes.argtypes = []

    uvm_lib.uvm_get_peak_allocated_bytes.restype = ctypes.c_size_t
    uvm_lib.uvm_get_peak_allocated_bytes.argtypes = []

    uvm_lib.uvm_get_num_allocs.restype = ctypes.c_size_t
    uvm_lib.uvm_get_num_allocs.argtypes = []

    uvm_lib.uvm_get_num_frees.restype = ctypes.c_size_t
    uvm_lib.uvm_get_num_frees.argtypes = []

    # Enable the allocator in PyTorch
    allocator = torch.cuda.memory.CUDAPluggableAllocator(
        so_path,
        'uvm_malloc',
        'uvm_free'
    )
    torch.cuda.memory.change_current_allocator(allocator)
    print("[UVM] UVM allocator enabled")

    return uvm_lib


def main():
    parser = argparse.ArgumentParser(description='GraphSAGE Training Benchmark with UVM')
    parser.add_argument('--nodes', type=int, default=10_000_000,
                       help='Number of nodes (default: 10M)')
    parser.add_argument('--edges_per_node', type=int, default=10,
                       help='Average edges per node (default: 10)')
    parser.add_argument('--features', type=int, default=128,
                       help='Feature dimension (default: 128)')
    parser.add_argument('--hidden', type=int, default=256,
                       help='Hidden dimension (default: 256)')
    parser.add_argument('--batch_size', type=int, default=1024,
                       help='Training batch size (default: 1024)')
    parser.add_argument('--fanout', type=str, default='15,10,5',
                       help='Neighbor sampling fanout (default: 15,10,5)')
    parser.add_argument('--epochs', type=int, default=3,
                       help='Number of epochs (default: 3)')
    parser.add_argument('--use_uvm', action='store_true',
                       help='Enable UVM allocator')
    args = parser.parse_args()

    # Parse fanout
    fanout = [int(x) for x in args.fanout.split(',')]

    # Calculate number of edges
    num_edges = args.nodes * args.edges_per_node

    # Enable UVM if requested (MUST be before any CUDA operations)
    uvm_lib = None
    if args.use_uvm:
        print("=" * 70)
        print("GraphSAGE Training Benchmark with UVM")
        print("=" * 70)
        print("[Step 0] Enabling UVM allocator...")
        uvm_lib = enable_uvm_allocator()

    # Print configuration
    print("=" * 70)
    print("GraphSAGE Training Benchmark with UVM")
    print("=" * 70)
    print("Configuration:")
    print(f"  Nodes: {args.nodes:,}")
    print(f"  Edges: {num_edges:,}")
    print(f"  Features: {args.features}")
    print(f"  Hidden dim: {args.hidden}")
    print(f"  Batch size: {args.batch_size}")
    print(f"  Fanout: {fanout}")
    print(f"  Epochs: {args.epochs}")
    print(f"  UVM: {'Yes' if args.use_uvm else 'No'}")
    print(f"  GPU: {torch.cuda.get_device_name(0)}")
    print(f"  GPU Memory: {torch.cuda.get_device_properties(0).total_memory / 1e9:.1f}GB")
    print("=" * 70)

    device = torch.device('cuda')

    # Step 1: Generate graph
    print("\n[Step 1/5] Generating random graph...")
    try:
        data = generate_random_graph(args.nodes, num_edges, args.features, device=device)
        print("  ✓ Graph generated successfully")
        print_memory(uvm_lib=uvm_lib)
    except RuntimeError as e:
        if 'out of memory' in str(e).lower():
            print(f"  ✗ OOM during graph generation")
            print(f"    Error: {e}")
            print("\n" + "=" * 70)
            print("Result: FAILED (OOM during data loading)")
            print("Suggestion: Try with --use_uvm flag")
            print("=" * 70)
            return
        raise

    # Step 2: Create train mask
    print("\n[Step 2/5] Preparing training split...")
    num_train = args.nodes // 10
    # Create mask on the same device as data to avoid device mismatch
    data_device = data.x.device
    train_mask = torch.zeros(args.nodes, dtype=torch.bool, device=data_device)
    train_mask[:num_train] = True
    data.train_mask = train_mask
    print(f"  Training nodes: {num_train:,} ({num_train/args.nodes*100:.1f}%)")
    print(f"  Data device: {data_device}")

    # Step 3: Create neighbor sampler
    print("\n[Step 3/5] Creating neighbor sampler...")
    train_loader = NeighborLoader(
        data,
        num_neighbors=fanout,
        batch_size=args.batch_size,
        input_nodes=train_mask,
        shuffle=True,
        num_workers=0,  # Avoid multiprocessing with UVM
    )
    num_batches = len(train_loader)
    print(f"  ✓ Sampler created")
    print(f"    Batches per epoch: {num_batches}")
    print_memory(uvm_lib=uvm_lib)

    # Step 4: Create model
    print("\n[Step 4/5] Creating GraphSAGE model...")
    try:
        model = GraphSAGE(args.features, args.hidden, 10).to(device)
        optimizer = torch.optim.Adam(model.parameters(), lr=0.01)

        num_params = sum(p.numel() for p in model.parameters())
        print(f"  ✓ Model created")
        print(f"    Parameters: {num_params:,}")
        print_memory(uvm_lib=uvm_lib)
    except RuntimeError as e:
        if 'out of memory' in str(e).lower():
            print(f"  ✗ OOM during model creation")
            print(f"    Error: {e}")
            return
        raise

    # Step 5: Training loop
    print(f"\n[Step 5/5] Training for {args.epochs} epochs...")
    epoch_times = []

    try:
        for epoch in range(args.epochs):
            start_time = time.time()

            loss = train_epoch(model, train_loader, optimizer, device, uvm_lib)

            epoch_time = time.time() - start_time
            epoch_times.append(epoch_time)

            print(f"  Epoch {epoch+1}/{args.epochs}: "
                  f"loss={loss:.4f}, time={epoch_time:.2f}s")

            if epoch == 0:  # Print memory after first epoch
                print_memory("    ", uvm_lib=uvm_lib)

        print("  ✓ Training completed successfully")

    except RuntimeError as e:
        if 'out of memory' in str(e).lower():
            print(f"  ✗ OOM during training at epoch {epoch+1}")
            print(f"    Error: {e}")
            print("\n" + "=" * 70)
            print("Result: FAILED (OOM during training)")
            print("Suggestion: Try with --use_uvm flag or reduce --batch_size")
            print("=" * 70)
            return
        raise

    # Final results
    print("\n" + "=" * 70)
    print("Final Results:")
    print("=" * 70)
    print(f"  Status: ✓ SUCCESS")
    print(f"  Total epochs: {args.epochs}")
    print(f"  Avg time/epoch: {sum(epoch_times)/len(epoch_times):.2f}s")
    print(f"  Total time: {sum(epoch_times):.2f}s")

    final_mem = get_memory_stats(uvm_lib)
    print(f"\n  Memory Usage:")
    print(f"    GPU: {final_mem['gpu_allocated']:.2f}GB")
    print(f"    CPU: {final_mem['cpu_used']:.2f}GB")
    print(f"    Total: {final_mem['total']:.2f}GB")

    if args.use_uvm and uvm_lib:
        cpu_portion = final_mem['cpu_used'] - 3.0  # Subtract baseline CPU
        if cpu_portion > 1.0:
            print(f"    UVM offload: ~{cpu_portion:.1f}GB to CPU")
        print(f"\n  UVM Statistics:")
        print(f"    Allocations: {uvm_lib.uvm_get_num_allocs()}")
        print(f"    Frees: {uvm_lib.uvm_get_num_frees()}")
        print(f"    Peak allocated: {uvm_lib.uvm_get_peak_allocated_bytes() / 1e9:.2f}GB")

    print("=" * 70)


if __name__ == '__main__':
    main()
