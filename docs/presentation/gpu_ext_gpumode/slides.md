---
theme: academic
title: 'Extending eBPF to GPU Device and Driver Contexts'
info: |
  ## Extending eBPF to GPU Device and Driver Contexts
  GPU MODE Community Talk
class: text-center
drawings:
  persist: false
transition: fade
mdc: true
layout: cover
colorSchema: light
---

<div class="text-center">

<div class="text-4xl font-bold leading-relaxed">Extending eBPF to GPU Device and Driver Contexts</div>

<div class="mt-6 text-lg">
Yusheng Zheng, Tong Yu
</div>

<div class="text-sm opacity-80 mt-2">
eunomia-bpf community
</div>

</div>

<div class="abs-tr m-4 flex flex-col items-end gap-3">
  <div class="flex items-center gap-3">
    <img src="/eunomia-logo.png" class="h-8" alt="eunomia-bpf" />
    <a href="https://github.com/eunomia-bpf" class="text-sm opacity-70">github.com/eunomia-bpf</a>
  </div>
</div>

---

# Agenda

<div class="grid grid-cols-2 gap-8 mt-4">

<div>

### Background

- GPU Stack Overview
- Workload Diversity

### The Problem

- Black Boxes: What's Happening Inside?
- Static Policies vs Diverse Workloads
- Existing Solutions & Limitations

### Insight
- GPU needs an observable, extensible OS policy interface

</div>

<div>

### Our Exploration

**Device eBPF**: Offloading eBPF to GPU (bpftime)
   - Observability Tools and probes
   - Prefetch & Schedule (?)

**gpu_ext**: Extending GPU Driver with eBPF
   - Memory & Scheduling struct_ops for resource management

**Cross-layer Coordination**
   - Cross Device eBPF Maps

</div>

</div>

---

# Background: GPU Stack Overview

<div class="grid grid-cols-2 gap-6">

<div class="flex-1 flex flex-col items-center justify-center">

<!-- GPU Stack Architecture Diagram -->
<div class="w-full">

<!-- User Space Layer - Multiple Apps with CUDA inside -->
<div class="text-sm font-bold text-blue-700 mb-1">User Space</div>
<div class="grid grid-cols-4 gap-1">
<div class="border-2 border-blue-400 rounded p-1 bg-blue-50 text-center">
<div class="text-xs font-semibold">vLLM</div>
<div class="border border-purple-400 rounded px-1 bg-purple-100 text-xs text-purple-600 mt-1">CUDA</div>
</div>
<div class="border-2 border-blue-400 rounded p-1 bg-blue-50 text-center">
<div class="text-xs font-semibold">PyTorch</div>
<div class="border border-purple-400 rounded px-1 bg-purple-100 text-xs text-purple-600 mt-1">CUDA</div>
</div>
<div class="border-2 border-blue-400 rounded p-1 bg-blue-50 text-center">
<div class="text-xs font-semibold">Faiss</div>
<div class="border border-purple-400 rounded px-1 bg-purple-100 text-xs text-purple-600 mt-1">CUDA</div>
</div>
<div class="border-2 border-blue-400 rounded p-1 bg-blue-50 text-center">
<div class="text-xs font-semibold">TensorRT</div>
<div class="border border-purple-400 rounded px-1 bg-purple-100 text-xs text-purple-600 mt-1">CUDA</div>
</div>
</div>

<div class="flex justify-center my-2">
<div class="text-gray-400 text-sm">↓ ioctl / mmap ↓</div>
</div>

<!-- Kernel Driver Layer -->
<div class="border-2 border-green-500 rounded-lg p-2 bg-green-50">
<div class="text-sm font-bold text-green-700 text-center mb-1">Kernel Driver</div>
<div class="grid grid-cols-3 gap-1">
<div class="border border-green-400 rounded p-1 bg-white text-center text-xs">UVM</div>
<div class="border border-green-400 rounded p-1 bg-white text-center text-xs">Channel</div>
<div class="border border-green-400 rounded p-1 bg-white text-center text-xs">MMU</div>
</div>
</div>

<!-- CPU/GPU Boundary -->
<div class="flex items-center my-2 gap-2">
<div class="text-xs text-gray-500 font-semibold -mt-4">CPU ↑</div>
<div class="flex-1 border-t-2 border-dashed border-gray-400"></div>
<div class="text-gray-500 text-xs px-2 bg-white">PCIe / NVLink</div>
<div class="flex-1 border-t-2 border-dashed border-gray-400"></div>
<div class="text-xs text-gray-500 font-semibold -mb-4">↓ GPU</div>
</div>

<!-- Device Layer -->
<div class="border-2 border-orange-500 rounded-lg p-2 bg-orange-50">
<div class="text-sm font-bold text-orange-700 text-center mb-1">GPU Device</div>
<!-- Firmware + Scheduler -->
<div class="grid grid-cols-2 gap-1 mb-1">
<div class="border border-orange-400 rounded p-1 bg-white text-center text-xs">Firmware</div>
<div class="border border-orange-400 rounded p-1 bg-white text-center text-xs">HW Scheduler</div>
</div>
<!-- SMs -->
<div class="border border-orange-400 rounded p-1 bg-white mb-1">
<div class="text-xs text-center text-orange-600 mb-1">Streaming Multiprocessors (SMs)</div>
<div class="grid grid-cols-4 gap-1">
<div class="border border-orange-300 rounded px-1 bg-orange-50 text-center text-xs">SM0</div>
<div class="border border-orange-300 rounded px-1 bg-orange-50 text-center text-xs">SM1</div>
<div class="border border-orange-300 rounded px-1 bg-orange-50 text-center text-xs">...</div>
<div class="border border-orange-300 rounded px-1 bg-orange-50 text-center text-xs">SMn</div>
</div>
</div>
<!-- Memory -->
<div class="grid grid-cols-2 gap-1">
<div class="border border-orange-400 rounded p-1 bg-white text-center text-xs">HBM / VRAM</div>
<div class="border border-orange-400 rounded p-1 bg-white text-center text-xs">L2 Cache</div>
</div>
</div>

</div>

</div>

<div>

### User Space
- Applications: vLLM, PyTorch, Faiss, TensorRT...
- Runtime: CUDA, cuDNN, cuBLAS
- Rich semantic info (model structure, SLOs)

### Kernel Driver
- GPU's "OS component"
- Memory management (UVM, page tables)
- Scheduling (channels, TSG)

### GPU Device
- User-defined GPU kernels
- Vendor firmware (proprietary)
- Hardware: SMs, Warps, HBM

</div>

</div>

---

# Background: Workload Diversity

<div class="grid grid-cols-2 gap-4">

<div>

<div class="flex flex-col gap-2">
<div class="flex items-center gap-2">
<div class="text-xs font-semibold w-20">Faiss Build</div>
<img src="/patterns/build-pattern.png" class="rounded shadow" style="height: 70px; flex: 1;" />
<div class="text-xs text-blue-600 w-16">Sequential</div>
</div>
<div class="flex items-center gap-2">
<div class="text-xs font-semibold w-20">Faiss Query</div>
<img src="/patterns/query-pattern.png" class="rounded shadow" style="height: 70px; flex: 1;" />
<div class="text-xs text-orange-600 w-16">Random</div>
</div>
<div class="flex items-center gap-2">
<div class="text-xs font-semibold w-20">LLM Prefill</div>
<img src="/patterns/prefill-pattern.png" class="rounded shadow" style="height: 70px; flex: 1;" />
<div class="text-xs text-blue-600 w-16">Stride</div>
</div>
<div class="flex items-center gap-2">
<div class="text-xs font-semibold w-20">LLM Decode</div>
<img src="/patterns/decode-pattern.png" class="rounded shadow" style="height: 70px; flex: 1;" />
<div class="text-xs text-orange-600 w-16">Sparse</div>
</div>
<div class="flex items-center gap-2">
<div class="text-xs font-semibold w-20">PyTorch DNN</div>
<img src="/patterns/dnn.png" class="rounded shadow" style="height: 70px; flex: 1;" />
<div class="text-xs text-green-600 w-16">Periodic</div>
</div>
</div>

</div>

<div>

### Diverse Resource & Behavior

- **Compute-bound** vs **Memory-bound**
- Different access patterns → different optimal policies

### Memory Placement / Offloading

- HBM expensive & limited (RTX 5090: 32GB)
- Models exceed VRAM: MoE, KV-cache in inference / Dataset big in traning

### Multi-tenancy Scheduling

- **LC**: LLM inference, needs low P99 latency
- **BE**: Training, needs high throughput
- Conflicts: memory competition, compute interference

</div>

</div>

---

# The Problem: Invisible & Inflexible

<div class="grid grid-cols-2 gap-6">

<div>

### Invisible — Black Boxes

- **Runtime (CUDA)**: closed-source, internal state not exposed
- **Driver **: why did it evict this page? What access pattern?
- **Firmware**: completely closed-source
- **Device execution**: no per-thread/warp metrics from host
- Cannot correlate CPU launch → GPU execution

</div>

<div>

### Inflexible — Static Policies

- **Driver UVM Memory**: LRU eviction, tree-based prefetch
- **Driver Scheduling**: Round-robin, fixed timeslice
- **Device Scheduling**: CLC (Hopper+) limited to block placement
- No per-workload or per-tenant differentiation
- Hard to change: LD_PRELOAD limited scope, kernel driver code hard to modify safely

</div>

</div>

---

# Existing Solutions & Limitations

<div class="grid grid-cols-2 gap-4">

<div class="border-l-4 border-blue-500 pl-3">

**User-space Runtimes** (vLLM, Sglang, ktransformer) and
**Userspace shims** (XSched..)
- Application-bound
- No cross-tenant visibility and control
- Cannot access low level driver mechanisms

</div>

<div class="border-l-4 border-green-500 pl-3">

**Driver Modifications** (TimeGraph, Gdev, GPreempt)
- Policies are hard code, hard to maintain and deploy
- Safety risks

</div>

<div class="border-l-4 border-orange-500 pl-3">

**Device Profilers** (NVBit, Neutrino, CUPTI)
- Read-only: cannot modify behavior or inject logic
- High overhead (NVBit 85-93%), NVBit needs LD_PRELOAD
- No per-thread visibility (CUPTI), no cross-layer (NVBit)

</div>

<div class="border-l-4 border-purple-500 pl-3">

**Host eBPF**
- GPU device remains a black box
- No programmable hooks in GPU driver for control

</div>

</div>

---

# What is eBPF?

<div class="flex flex-col items-center">

<div class="text-lg mb-4">
Safe, dynamic, verified programs that extend the Linux kernel — <b>without modifying kernel source</b>
</div>

<img src="/ebpf.png" class="rounded shadow-lg" style="max-height: 320px;" alt="eBPF Overview" />

<div class="flex gap-6 mt-6 text-base">
<div class="px-3 py-1 bg-blue-100 rounded text-blue-700 font-semibold">Networking</div>
<div class="px-3 py-1 bg-green-100 rounded text-green-700 font-semibold">Security</div>
<div class="px-3 py-1 bg-orange-100 rounded text-orange-700 font-semibold">Observability</div>
<div class="px-3 py-1 bg-purple-100 rounded text-purple-700 font-semibold">Scheduling</div>
<div class="px-3 py-1 bg-red-100 rounded text-red-700 font-bold">GPU?</div>
</div>

<div class="text-xs mt-3 opacity-50">Source: ebpf.io</div>

</div>

---

# Why eBPF?

<div class="grid grid-cols-2 gap-6 text-sm">

<div class="border-2 border-blue-500 rounded-lg p-3">

### Proven in Traditional Systems

- **Observability**: bpftrace, BCC — trace any kernel function
- **sched_ext**: custom CPU schedulers (Linux 6.12, Meta & Google)
- **XDP**: programmable packet processing


</div>

<div class="border-2 border-green-500 rounded-lg p-3">

### Same Problem in GPU

**Invisible**: profilers are read-only, high overhead, coarse-grained

**Inflexible**: hardcoded eviction, prefetch, scheduling — no per-tenant differentiation

→ Same problem as CPU before sched_ext

**eBPF Makes GPU Visible + Flexible**
- **gpu_ext**: driver policy hooks + access pattern tracing
- **bpftime**: per-thread profiling on GPU (3-14% overhead)
- **No app modifications** needed

</div>

</div>

---

# How to Write eBPF

<div class="grid grid-cols-2 gap-4">

<div class="text-xs">

```c
// Define shared data structure (Map)
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, u32);
    __type(value, u64);
} call_counts SEC(".maps");

// Attach to kernel function (Hook)
SEC("kprobe/do_sys_open")
int count_opens(struct pt_regs *ctx) {
    u32 pid = bpf_get_current_pid_tgid() >> 32;
    u64 *count = bpf_map_lookup_elem(
        &call_counts, &pid);
    if (count)
        (*count)++;
    else {
        u64 init = 1;
        bpf_map_update_elem(
            &call_counts, &pid, &init, BPF_ANY);
    }
    return 0;
}
```

</div>

<div class="text-sm">

### Core Concepts

- **Programs**: small C → eBPF bytecode (clang/LLVM)
- **Hooks**: attach to kernel functions, tracepoints, etc.
- **Maps**: shared key-value data (kernel ↔ userspace)
- **Helpers**: safe kernel API (`bpf_get_current_pid_tgid`, `bpf_ktime_get_ns`, ...)
- **Verifier**: guarantees safety before execution
  - No crashes, no infinite loops, bounded memory

### Analogy for GPU Devs

Think of it like **CUPTI callbacks**, but:
- You write **your own logic** (not just read events)
- **Dynamically attach** without restarting
- **Verified safe** — kernel guarantees no harm
- Can **modify behavior**, not just observe

</div>

</div>

---

# Insight: GPU Needs an Observable, Extensible OS Interface

<div class="grid grid-cols-2 gap-6">

<div class="border-2 border-orange-500 rounded-lg p-4">

### Observable — Why?

- Device execution state invisible from host
  - Warp divergence, SM load, memory access patterns
- Existing profilers: read-only, high overhead, coarse-grained
- Need **fine-grained, low-overhead, cross-layer** observability

<div class="mt-3 p-2 bg-orange-50 rounded text-base">
→ Requires instrumentation <b>on GPU device</b>
</div>

</div>

<div class="border-2 border-blue-500 rounded-lg p-4">

### Extensible — Why?

- Driver has **global cross-tenant visibility** and hardware control
- But policies are hardcoded, hard to change safely
- Need **safe, dynamic, per-workload** policy customization

<div class="mt-3 p-2 bg-blue-50 rounded text-base">
→ Programmable hooks <b>in GPU driver</b> (like sched_ext)
</div>

</div>

</div>

---

# Our Exploration: eBPF for GPU

<div class="grid grid-cols-2 gap-8 mt-4">

<div class="border-2 border-green-500 rounded-lg p-4">

### Part 1: Device eBPF

**Running eBPF on GPU Device (bpftime)**

- Fine-grained profiling: per-thread/warp/SM observability
- Compile eBPF to PTX/SPIR-V with Device-side hooks and helpers
- Dynamic instrumentation (no recompile, no restart)
- Cross-layer eBPF Maps (CPU ↔ GPU)

</div>

<div class="border-2 border-blue-500 rounded-lg p-4">

### Part 2: gpu_ext

**Extending Linux GPU Driver with eBPF**

- Driver-level tracing: access patterns, fault behavior, scheduling events
- Programmable memory policy: eviction, prefetch hooks in UVM
- Programmable scheduling: TSG lifecycle hooks
- Uses standard eBPF verifier + struct_ops

</div>

</div>

---
layout: center
class: text-center
---

# Part 1: Device eBPF

Running eBPF on GPU Device (bpftime)

---

# GPU Execution Model: Why eBPF on GPU is Hard

<div class="grid grid-cols-2 gap-6">

<div class="text-base">

### SIMT Recap (Why It Matters for eBPF)

- **Warp** (32 threads) executes in lockstep
- Different branches → **serialization (Divergence)**
- This means: naive per-thread eBPF would cause divergence & waste

### Thread Hierarchy

Thread → Warp (32) → Block → Grid → SM

</div>

<div>

### Key Differences from CPU eBPF

| Feature | CPU | GPU |
|---------|-----|-----|
| Thread count | Tens | Tens of thousands |
| Scheduling unit | Single thread | Warp (32 threads) |
| Branch handling | Prediction | Serialization |
| Preemption | Full | Limited |

**Challenge**: eBPF assumes scalar execution — GPU is SIMT

</div>

</div>

---

# What Can GPU eBPF Do?

<div class="grid grid-cols-2 gap-6">

<div class="text-base">

### Fine-grained Profiling

- Instruction-level observability
- Per-thread/warp/SM metrics
- Memory access pattern detection

### Runtime Adaptation

- Respond to device state
- Safe and Dynamic policy adjustment in GPU kernel

### Help Host-side Policies

- Provide device visibility/controlility to host
- Cross-layer coordination

</div>

<div>

### e.g. SM Load Imbalance Trace

<img src="/sm thread sched.png" class="rounded shadow-lg" style="max-height: 240px;" />

**127x** difference observed between SMs

Traced by [bpftime/gpu/threadscheduling](https://github.com/eunomia-bpf/bpftime/tree/master/example/gpu/threadscheduling)

</div>

</div>

---

# Example: launchlate - Kernel Launch Latency Profiler

<div class="grid grid-cols-2 gap-4">

<div class="text-xs">

```c
BPF_MAP_DEF(BPF_MAP_TYPE_ARRAY, launch_time);

// CPU-side uprobe captures launch time
SEC("uprobe/app:cudaLaunchKernel")
int uprobe_launch(struct pt_regs *ctx) {
    u64 ts_cpu = bpf_ktime_get_ns();
    bpf_map_update_elem(&launch_time, &key, &ts_cpu, BPF_ANY);
}

// GPU-side kprobe captures execution start
SEC("kprobe/_Z9vectorAddPKfS0_Pf")
int kprobe_exec() {
    u64 ts_gpu = bpf_get_globaltimer();
    u64 *ts_cpu = bpf_map_lookup_elem(&launch_time, &key);
    u64 latency = ts_gpu - *ts_cpu;
    // Update histogram...
}
```

</div>

<div class="text-sm">

### Problem

CUPTI shows kernel "started" quickly, but it's slow. Why?

**Hidden issue**: Thread blocks competing for SMs with other kernels (multi-process, multi-stream)

- **CUPTI sees**: Kernel start/end time (looks fine)
- **Reality**: Many blocks waiting for SM resources
- **bpftime**: Per-thread block/warp scheduling timestamp inside kernel

### How It Works

1. **CPU uprobe**: Record T1 at `cudaLaunchKernel()`
2. **GPU kprobe**: Record T2 **per-thread block** at kernel entry
3. See **when each thread block gets scheduled**


[bpftime/gpu/launchlate](https://github.com/eunomia-bpf/bpftime/tree/master/example/gpu/launchlate)

</div>

</div>

---

# More Tools: threadhist & kernelretsnoop

<div class="grid grid-cols-2 gap-4">

<div>

### threadhist — Per-Thread Count

<div class="text-xs">

```c
// 89 LOC total — Per-thread isolated counter
struct {
    __uint(type, BPF_MAP_TYPE_PERGPUTD_ARRAY_MAP);
    __uint(max_entries, 1);
    __type(key, u32); __type(value, u64);
} call_count SEC(".maps");

SEC("kretprobe/_Z9vectorAddPKfS0_Pf")
int cuda__retprobe() {
    u32 key = 0;
    u64 *cnt = bpf_map_lookup_elem(&call_count, &key);
    if (cnt) *cnt += 1;
    return 0;
}
```

</div>

- **PERGPUTD_ARRAY**: each GPU thread gets isolated storage
- Diagnose: grid-stride loop bugs, idle threads
- Thread 4: 158K vs Thread 0-3: 210K → boundary bug

</div>

<div>

### kernelretsnoop — Exit Timestamps

<div class="text-xs">

```c
// 153 LOC total — Stream per-thread events to host
struct data { u64 x, y, z; u64 timestamp; };
struct {
    __uint(type, BPF_MAP_TYPE_GPU_RINGBUF_MAP);
    __uint(max_entries, 16);
} rb SEC(".maps");

SEC("kretprobe/_Z9vectorAddPKfS0_Pf")
int cuda__retprobe() {
    struct data d;
    bpf_get_thread_idx(&d.x, &d.y, &d.z);
    d.timestamp = bpf_get_globaltimer();
    bpf_perf_event_output(NULL, &rb, 0, &d, sizeof(d));
    return 0;
}
```

</div>

- **GPU_RINGBUF**: lock-free ring buffer → host
- Diagnose: warp divergence, memory bottlenecks
- Thread 31 finishes 750ns late → divergent branch

</div>

</div>

---

# Performance: Observability Tools Overhead

Tested on a P40 GPU with llama.cpp 1B inference.

| Tool | LOC | bpftime | NVBit |
|------|-----|---------|-------|
| kernelretsnoop | 153 | **8%** | 85% |
| threadhist | 89 | **3%** | 87% |
| launchlate | 347 | **14%** | 93% |

**Key**: Warp-uniform execution achieves **3-14%** overhead vs NVBit's **85-93%**

---

# bpftime vs Existing GPU Profilers

<div class="text-sm">

| Capability | CUPTI | Nsight Compute | NVBit | **bpftime** |
|------------|-------|----------------|-------|-------------|
| Runtime overhead | Low | Low | 85-93% | **3-14%** |
| Per-thread metrics | ✗ | ✗ | ✓ | **✓** |
| Cross CPU+GPU | Partial | ✓ | ✗ | **✓** |
| Attach to running process | ✗ | ✗ | ✗ | **✓** |
| Custom logic | ✗ | ✗ | ✓ | **✓** |
| No recompile needed | ✓ | ✓ | ✓ | **✓** |
| Can modify behavior | ✗ | ✗ | ✗ | **✓** |

</div>

<div class="mt-2 p-2 bg-blue-50 rounded text-sm">

**bpftime** = low overhead of CUPTI + programmability of NVBit + cross-layer visibility of Nsight

</div>

---

# bpftime Architecture

<div class="flex justify-center">

<img src="/bpftime.png" class="rounded shadow-lg" style="max-height: 420px;" alt="Device eBPF Architecture" />

</div>

---

# Instrumentation: Fatbin Hook & PTX Injection

<div class="flex justify-center">

<img src="/fatbin.jpg" class="rounded shadow-lg" style="max-height: 420px;" alt="Fatbin Hook and PTX Injection" />

</div>

---

# PTX Injection: Patching & Wrapping

<div class="flex justify-center">

<img src="/injection.png" class="rounded shadow-lg" style="max-height: 420px;" alt="PTX Injection Details" />

</div>

---

# bpftime GPU Support: Maps, Helpers, Attach Types

<div class="grid grid-cols-3 gap-3 text-xs">

<div class="border rounded p-2">

### Attach Types (3)

User can define a compiler pass to define any
hook points at instruction level, e.g.:

- `CUDA_PROBE` (entry)
- `CUDA_RETPROBE` (exit)
- `__memcapture` (ld/st)
- `Cluster launch Control Scheduler` {Thread block scheduler POC}

```c
__device__ static bool
should_try_steal(State& s,
    int current_block) {
        return true;  // Always try to steal
}
```

</div>

<div class="border rounded p-2">

### GPU Maps (5)

- `PERGPUTD_ARRAY`
- `GPU_ARRAY`
- `GPU_HASH`
- `GPU_RINGBUF`
- `GPU_KERNEL_SHARED`

(Can use all userspace CPU maps with high cost)

</div>

<div class="border rounded p-2">

### GPU Helpers (15+)

- `ebpf_puts`
- `get_globaltimer`
- `get_block_idx`
- `get_block_dim`
- `get_thread_idx`
- `exit`
- `get_grid_dim`
- `get_sm_id`
- `get_warp_id`
- `get_lane_id`
- + standard userspace BPF helpers (high cost)

</div>

</div>

---

# Optimizations: Warp-level Execution & Map Placement

<div class="grid grid-cols-2 gap-6 text-sm">

<div class="border-l-4 border-blue-500 pl-4">

### Warp-level Execution

**Problem**: Per-thread eBPF causes warp divergence & bandwidth waste

**Solution**: Execute eBPF **once per warp** (32 threads), not per thread

- Warp leader executes, broadcasts result / updates maps
- Reduces overhead by **60-81%** vs naive injection
- Avoids divergence and deadlock risks

</div>

<div class="border-l-4 border-green-500 pl-4">

### Hierarchical Map Placement

**Problem**: PCIe latency ~40μs vs GPU local ~100ns (**400-1000x difference**)

**Solution**: Logically Verify once, place at runtime

| Data Type | Placement |
|-----------|-----------|
| Hot state (frequent) | GPU local, batch sync |
| Cold config | Host DRAM |
| Bidirectional | Hierarchical shards |

- Relaxed consistency: staleness affects optimality, not correctness

</div>

Improved 60-80% performance for probes and helpers.

</div>

---
layout: center
class: text-center
---

# Part 2: gpu_ext

Extending Linux GPU Driver with eBPF

---

# GPU Scheduling Concepts

<div class="grid grid-cols-3 gap-4">

<div>

### Key Concepts
- **Channel**: Command queue (per CUDA stream)
- **Task Group (TSG)**: Scheduling unit, groups channels
- **Runlist**: HW scheduler's queue of TSGs

</div>

<div>

### Why TSG, Not GPU Kernels?
- **Kernel launch bypasses driver** - userspace writes pushbuffer + doorbell via MMIO
- **Driver only sees TSG lifecycle** - create, bind, destroy

</div>

<div class="row-span-2 flex flex-col items-center">

### Task Group Lifecycle

<img src="/mermaid-sched.png" class="rounded" style="max-height: 320px;" />

</div>

<div class="col-span-2">

### Scheduling Parameters
- **Timeslice**: Time before preemption (1s LC / 200μs BE)
- **Interleave Level**: Priority (LOW/MED/HIGH)

</div>

</div>

---

# GPU Memory Concepts

<div class="grid grid-cols-3 gap-4">

<div>

### Key Concepts
- **Unified Memory**: CPU & GPU share VA space
- **VA Block**: Virtual address range
- **Chunk**: Physical block (2MB)
- **Replayable Fault**: Warp paused → driver migrates → replay

</div>

<div class="flex flex-col items-center">

### Page Fault Handling

<img src="/pagefault.png" class="rounded" style="max-height: 320px;" />

</div>

<div class="flex flex-col items-center">

### Chunk-VABlock Lifecycle

<img src="/chunk.png" class="rounded" style="max-height: 320px;" />

</div>

</div>

---

# Challenge: Expressiveness vs Safety

<div class="text-lg mt-4">

GPU drivers were **not designed** to expose a programmable interface

- **More Expressiveness** → Expose low-level mechanisms (page tables, command buffers)
  - Risk driver safety and isolation

- **More Safety** → Constrain to high-level abstractions
  - Risk: limits complex memory/scheduling decisions

</div>

<div class="mt-6 p-4 bg-blue-50 rounded-lg">

### Our Approach: Narrow, Safe Interface

- Policy **advises**, kernel **decides**
- Expose **structured hooks**, not raw mechanisms; **Bounded operations** via kfuncs
- Implemented as **struct_ops**

</div>

---

# Memory Management Interface

<div class="grid grid-cols-2 gap-4">

<div class="text-xs overflow-y-auto" style="max-height: 420px;">

```c
struct gpu_mem_ops {
  // Eviction hooks (2MB block granularity)
  // Called when block added to eviction list
  // Trigger: first alloc from block, becomes evictable
  int (*gpu_block_activate)(pmm, block, list);
  // Called when any page in block is accessed
  // Trigger: page fault on va_block mapped to this block
  int (*gpu_block_access)(pmm, block, list);
  // Called before selecting victim for eviction
  // Trigger: memory pressure, need to free blocks
  // Can: reorder used/unused lists
  int (*gpu_evict_prepare)(pmm, list);
  // Prefetch hooks (page granularity)
  // Called before computing prefetch region
  // Trigger: after page fault handled
  int (*gpu_page_prefetch)(page_index, bitmap_tree,
    max_prefetch_region, result_region);
};
// kfuncs
void bpf_gpu_block_move_head(block, list);
void bpf_gpu_block_move_tail(block, list);
void bpf_gpu_set_prefetch_region(region, first, outer);
```

</div>

<div class="text-sm">

### Policies

The default policy is LRU + tree-based prefetching. We impl:

- LFU, MRU, FIFO eviction
- Stride / sequential prefetch
- Per-process memory priority based on PID
- Application-specific...

### Safety: Programmable Cache Model

- Policy can **reorder** eviction list, but **cannot remove**
- Kernel picks final victim
- kfuncs only allow **move_head/move_tail** operations
- Prefetch policy sets region, kernel validates bounds

</div>

</div>

---

# Scheduling Interface

<div class="grid grid-cols-2 gap-4">

<div class="text-xs overflow-y-auto" style="max-height: 400px;">

```c
struct gpu_sched_ops {
  // Called when task group is created
  // Trigger: cuCtxCreate / cudaSetDevice
  // Can: set timeslice, interleave level
  // Ctx: tsg_id, engine_type, default_timeslice
  int (*task_init)(struct gpu_task_init_ctx *ctx);
  // Called when task group binds to runlist (ONE-TIME)
  // Trigger: first kernel launch activates the TSG
  // Note: subsequent kernel launches bypass driver!
  // Can: admission control (reject bind)
  int (*task_bind)(struct gpu_task_bind_ctx *ctx);
  // Called when task group is destroyed
  // Trigger: cuCtxDestroy / process exit
  // Can: cleanup BPF map state
  int (*task_destroy)(struct gpu_task_ctx *ctx);
};
// kfuncs to set timeslice, interleave level
void bpf_gpu_set_attr(ctx, u64 us);
void bpf_gpu_reject_bind(ctx);
```

</div>

<div class="text-sm">

### Policy Can Set

- Timeslice (1s for LC, 200μs for BE)
- Interleave level (LOW/MED/HIGH priority)
- Accept/reject task binding

### Policy

The default is round-robin / FIFO, we can impl:

- LC vs BE differentiation by process name
- Multi-tenant fairness / isolation

</div>

</div>

---

# Implementation: Extending NVIDIA Open GPU Modules (POC)

<div class="grid grid-cols-2 gap-6 text-base">

<div class="border-l-4 border-blue-500 pl-4">

### Modifications

- UVM module: ~100 lines instrumentation
- Page fault handler hooks
- Prefetch logic hooks
- TSG lifecycle event hooks

</div>

<div class="border-l-4 border-green-500 pl-4">

### Driver Independence

- ~1000 lines eBPF framework integration
- Uses Linux eBPF verifier + GPU-specific struct_ops/kfunc via BTF
- (May be **extracted** as standalone module)

</div>

</div>

<div class="mt-4 p-3 bg-gray-100 rounded text-sm">

**POC Code**: [github.com/eunomia-bpf/gpu_ext_policy](https://github.com/eunomia-bpf/gpu_ext_policy) (eBPF policies) | [github.com/eunomia-bpf/gpu_ext-kernel-modules](https://github.com/eunomia-bpf/gpu_ext-kernel-modules) (kernel modules)

</div>

---

# Use Cases Summary

<div class="grid grid-cols-2 gap-4 text-sm">

<div class="border-2 border-blue-400 rounded-lg p-3">

### Single Application

| Workload | Policy | Speedup |
|----------|--------|---------|
| LLM Expert (llama.cpp) | Stride prefetch + LFU eviction | **~4x** decode speedup vs default framework offloading |
| KV-cache (vLLM) | LFU eviction + sequential prefetch | **~1.5x** less TTFT vs default framework offloading, close to LMCache|

**Key**: 1) Hardware faster / sofware algorithm old -> Need to do more prefetching 2) Tree-based prefetch not optimal for LLM/ML (ALso tested with GNN / Vector DB)

</div>

<div class="border-2 border-green-400 rounded-lg p-3">

### Multi-Process

| Scenario | Policy | Improvement |
|----------|--------|-------------|
| LC+BE Scheduling | LC 1s / BE 200μs timeslice | **95%** P99 ↓ |
| Memory Priority | HP more prefetch and eviction protection, LP less | **55-92%** time ↓ |

**Key**: Default policy does not allow different process has different behavior: we can have priority.
-  Compute-bound → Scheduling;
- Memory-bound → Memory policy

</div>

</div>

<div class="mt-4 p-3 bg-blue-50 rounded text-base">

**All use cases**: No application modifications needed

</div>

---

# Problems & Next Steps

Why not extend HMM or DRM?

- Nvidia cuda computing is bypass the DRM.
- HMM is like a interface, mechaism is still in driver.

The design is portable:
- POC in SPIR-v
- ARM also has similar feature set.

More standard API for all GPU drivers?

Cgroups?


---

# Thanks & Questions

<div class="mt-8 text-xl">

**POC Code**

[github.com/eunomia-bpf/gpu_ext_policy](https://github.com/eunomia-bpf/gpu_ext_policy) | [github.com/eunomia-bpf/gpu_ext-kernel-modules](https://github.com/eunomia-bpf/gpu_ext-kernel-modules)

**GPU eBPF (bpftime)**

[github.com/eunomia-bpf/bpftime](https://github.com/eunomia-bpf/bpftime)

Arxiv will be released soon.

</div>
