# gpu_ext Driver-Side Novelty: Why This Is Not "Just struct_ops for UVM"

## The Core Problem with the Current Paper

The paper describes the driver contribution as: "we added 4 struct_ops hooks and 5 kfuncs to NVIDIA UVM (~100 LOC)." This is an accurate engineering description but it reads as the same pattern as cache_ext (hooks on page cache) and sched_ext (hooks on CPU scheduler). A reviewer reasonably concludes: "this is struct_ops applied to a new subsystem — incremental."

The paper hides its own novelty. The most important mechanisms — `bpf_gpu_migrate_range()`, `bpf_nv_gpu_preempt_tsg()`, `bpf_wq` async pipelines, multi-program composition — are either buried in implementation details or entirely absent from the text.

## The Insight: GPU Resource Management Is Fundamentally Different

The right question is: **what is genuinely different about extending a GPU driver vs extending a CPU subsystem?**

The answer: **GPU resource management is inherently asynchronous and cross-domain. CPU resource management is not.**

When the Linux CPU scheduler picks the next task, it is a **nanosecond-scale synchronous decision** within one coherent memory system. A struct_ops callback that returns a decision value is the perfect model. cache_ext and sched_ext work this way.

When the GPU driver handles a page fault, the "decision" requires a **millisecond-scale DMA transfer across PCIe** between two physically separate memory systems (CPU DRAM and GPU VRAM). A synchronous callback that returns an advisory value is **architecturally insufficient** — by the time the DMA completes, the fault handler has been blocking for 1000x longer than the decision itself. The interesting policy work (prefetching the next region, preempting a competing process, coordinating with the CPU scheduler) cannot happen inside a synchronous callback.

This is why gpu_ext requires mechanisms that cache_ext and sched_ext do not:

| GPU characteristic | Why struct_ops alone fails | What gpu_ext needs |
|---|---|---|
| **DMA takes milliseconds** | Can't block the fault handler to prefetch the next VA block | **bpf_wq** — decouple the fast synchronous decision from the slow asynchronous DMA operation |
| **Data lives in two physical domains** (CPU DRAM ↔ GPU VRAM via PCIe) | Advisory "evict this page" is not enough; must actively trigger physical data migration | **Sleepable kfunc** `bpf_gpu_migrate_range()` — BPF directly triggers DMA across the PCIe bus |
| **2MB VA block boundary is arbitrary** | Default UVM tree-based prefetcher is trapped within one VA block | `bpf_gpu_migrate_range()` accepts arbitrary address ranges — a **new kernel capability** that breaks this boundary |
| **GPU has its own hardware scheduler** | Memory hooks cannot control GPU compute scheduling | **Sleepable kfunc** `bpf_nv_gpu_preempt_tsg()` — BPF crosses the memory→scheduling subsystem boundary |
| **Multi-tenant requires cross-process control** | struct_ops only sees the current process's page faults | Preempt kfunc executes at `RS_PRIV_LEVEL_KERNEL` — can act on any process's GPU context |
| **Application semantics are invisible to the driver** | Fault handler doesn't know if this is MoE prefill, decode, GNN training, or FAISS search | **uprobe** on application APIs (cuLaunchKernel, PyTorch allocator) feeds semantics into struct_ops via shared maps |

**None of these rows apply to cache_ext or sched_ext.** The CPU page cache does not have a PCIe bandwidth bottleneck. The CPU scheduler does not need to trigger millisecond DMA operations. CPU processes share one coherent memory — there is no "cross-domain migration." This table is the novelty argument.

## The Contribution (Reframed)

The driver contribution is not "we added hooks to a GPU driver." It is:

> We identified that GPU resource management requires **active, asynchronous, cross-boundary operations** that the synchronous struct_ops model cannot express. We designed a minimal set of GPU-specific kfuncs (`bpf_gpu_migrate_range` for cross-boundary page migration, `bpf_nv_gpu_preempt_tsg` for cross-process GPU preemption) and composed them with BPF work queues to create an **async policy execution model** that matches the inherently asynchronous nature of GPU memory management. This ~100 LOC driver interface enables a policy space that previously required separate driver modifications for each algorithm.

## The Async Policy Pipeline (gpu_ext's Architectural Contribution)

The composition of struct_ops + bpf_wq + sleepable kfuncs creates an **async policy pipeline** that has no analogue in prior eBPF extensibility work:

```
GPU page fault
    │
    ▼
struct_ops hook (synchronous, microseconds)
    ├── Decision 1: intra-block eviction ordering (move_head/move_tail)
    ├── Decision 2: intra-block prefetch range (set_prefetch_region)
    └── Dispatch: schedule async work via bpf_wq
                      │
                      ▼
              bpf_wq callback (asynchronous, process context, sleepable)
                  ├── Direction detection: which way is the workload scanning?
                  ├── Cross-block prefetch: call bpf_gpu_migrate_range()
                  │       └── Triggers DMA across PCIe (milliseconds)
                  └── Stats update: record access pattern for future decisions
```

The synchronous path makes fast, local decisions (which pages to keep, which to evict). The asynchronous path makes slow, global decisions (prefetch the next VA block, coordinate with other subsystems). Neither path alone is sufficient:

- **Sync only** (struct_ops alone): Can reorder eviction list and set prefetch hints within the current VA block. Achieves ~2.65x on GNN. Cannot prefetch across VA block boundaries.
- **Sync + async** (struct_ops + bpf_wq + sleepable kfunc): Can additionally prefetch across VA block boundaries using direction detection. Achieves **3.36x on GNN** — 27% beyond sync-only.

The 27% gap between 2.65x and 3.36x **precisely measures the value of the async pipeline** — the part that goes beyond struct_ops.

> **[CRITICAL REVIEW NOTE]** The XRP parallel is useful but slightly forced. XRP's insight is about WHERE to hook (syscall vs NVMe driver). gpu_ext's insight is about WHAT KIND of mechanism (sync vs async, advisory vs active). Don't structure the paper as "we did what XRP did but for GPU." Better framing: "XRP showed eBPF placement matters; we show eBPF mechanism composition matters — complementary insights in the same extensibility trajectory." See `critical_review.md` §II Problem 10.

## The XRP Parallel

The parallel to XRP is exact:

| | XRP (storage) | gpu_ext (GPU) |
|---|---|---|
| **Insight** | Storage I/O resubmission needs to happen at the NVMe driver level, not the syscall level | GPU memory policies need active async operations (kfuncs + bpf_wq), not just advisory callbacks (struct_ops) |
| **"Naive eBPF" baseline** | BPF at syscall layer = 1.15x | struct_ops hooks only = ~2.65x (GNN) |
| **Full system** | BPF at NVMe driver + resubmission = 2.5x | struct_ops + kfuncs + bpf_wq = 3.36x (GNN) |
| **The gap = the novelty** | 2.5x vs 1.15x = 2.2x more from correct placement | 3.36x vs 2.65x = 27% more from async pipeline |
| **Domain-specific mechanism** | Metadata digest (caches file→block mappings) | `bpf_gpu_migrate_range()` (cross-VA-block DMA) |
| **Key challenge** | NVMe driver lacks file system context | UVM fault handler is synchronous but GPU DMA is async |

Both papers are about finding the right **level and mechanism** for eBPF in a new domain. XRP's metadata digest solves a storage-specific problem (address translation without file system context). gpu_ext's async kfuncs solve a GPU-specific problem (policy execution that outlives the synchronous fault handler).

## The Capability Progression Table (gpu_ext's "Table 2")

Following XRP's presentation pattern, gpu_ext should include a capability progression table that quantifies what each mechanism layer adds:

| Capability Layer | Mechanism | GNN Speedup | MoE Decode | Multi-tenant LC P99 |
|---|---|---|---|---|
| No policy (UVM default) | — | 1x | 1x | 1x (baseline) |
| Advisory hooks only | struct_ops (move_head/tail, set_prefetch_region) | ~2.65x | ~1.1x | — |
| + Active migration kfunc | + `bpf_gpu_migrate_range()` | 2.65x | **4.8x** | — |
| + Async pipeline | + bpf_wq (cross-block direction-aware prefetch) | **3.36x** | — | — |
| + Cross-process preemption | + `bpf_nv_gpu_preempt_tsg()` | — | — | **-95%** |
| + Application semantics | + uprobe on PyTorch/cuLaunchKernel | +uprobe-guided prefetch | — | auto preempt on LC kernel launch |

This table does exactly what XRP's Table 2 does:
1. **Proves that struct_ops alone ("naive eBPF") is insufficient** — row 2 vs rows 3-5
2. **Quantifies the marginal value of each mechanism** — each row adds measurable improvement
3. **Justifies every mechanism** — nothing is there "because we can"; each solves a measured gap
4. **Directly addresses "isn't this just struct_ops for UVM?"** — no, struct_ops is row 2; the contribution is rows 3-6

## Multi-Program Composition (Beyond Any Single Hook Table)

The most advanced gpu_ext policies compose **multiple BPF program types** into a coherent policy — a pattern that no single struct_ops table can express:

### Cross-block Direction-Aware Prefetch (3 program types)

1. **kprobe** on `uvm_perf_prefetch_get_hint_va_block` → captures va_block/va_space context (the driver doesn't expose this in struct_ops context)
2. **struct_ops** `gpu_page_prefetch` → makes synchronous intra-block decision + detects scan direction + dispatches bpf_wq
3. **bpf_wq callback** (sleepable, process context) → calls `bpf_gpu_migrate_range()` for cross-block DMA

### Cross-Process GPU Preemption (4 program types)

1. **kprobe** on `nvidia_unlocked_ioctl` → captures hClient handle
2. **kprobe** on `nv_gpu_sched_task_init` → captures hTsg handle
3. **kretprobe** → confirms registration succeeded
4. **sleepable uprobe** on `cuLaunchKernel` → triggers `bpf_nv_gpu_preempt_tsg()` when LC process launches a GPU kernel

### CPU-GPU Coordinated Scheduling (cross-subsystem)

1. **struct_ops** gpu_ext hooks → populate `gpu_state_map` with fault_rate, eviction_pressure
2. **Shared BPF map** → bridges GPU memory subsystem and CPU scheduler
3. **sched_ext** policy → reads GPU state, adjusts CPU priority for GPU-serving threads

These compositions demonstrate that gpu_ext is not "a hook table" but a **policy composition framework** where multiple eBPF program types cooperate across subsystem boundaries.

## The Uprobe → Sleepable Kfunc → GPU Hardware Path (The Real "Aha Moment")

The async struct_ops pipeline is novel, but there is an even stronger story already implemented in the repository: **sleepable uprobes that directly trigger GPU hardware operations from application API boundaries.**

### The Pattern

```
User-space application (unmodified)
        │  e.g., cuLaunchKernel(), cudaStreamSynchronize(), cudaMallocManaged()
        │
        ▼
sleepable uprobe (SEC("uprobe.s/..."))
        │  BPF program in kernel context, can sleep
        │
        ▼
sleepable kfunc
        │  bpf_nv_gpu_preempt_tsg()  — preempt GPU TSG
        │  bpf_gpu_migrate_range()   — trigger PCIe DMA migration
        ▼
GPU hardware (TSG context switch / page migration)
```

This path crosses **three domains** — user-space application → kernel BPF → GPU hardware — in a single causal chain. No prior eBPF extensibility work does this:

| System | Hook domain | Action domain | Crosses |
|---|---|---|---|
| XRP | Kernel (NVMe IRQ) | Kernel (NVMe resubmit) | kernel → kernel |
| cache_ext | Kernel (page cache) | Kernel (folio list) | kernel → kernel |
| sched_ext | Kernel (scheduler) | Kernel (dispatch queue) | kernel → kernel |
| **gpu_ext** | **User-space** (cuLaunchKernel) | **GPU hardware** (TSG preempt) | **user → kernel → GPU** |

### Concrete Instances in the Repository

**1. Preemption on kernel launch** (`uprobe_preempt_multi.bpf.c:229`)
```c
SEC("uprobe.s//usr/lib/x86_64-linux-gnu/libcuda.so:cuLaunchKernel")
int preempt_on_launch(struct pt_regs *ctx) {
    // ... filter by LC process comm ...
    // ... for each BE target TSG:
    ret = bpf_nv_gpu_preempt_tsg(tsg->hClient, tsg->hTsg);
}
```
When the latency-critical process calls cuLaunchKernel, this uprobe fires BEFORE the kernel reaches the GPU. It preempts all best-effort TSGs so the LC kernel gets immediate GPU access. Result: **-48% to -58% latency reduction** in multi-tenant scenarios.

The key: the preemption happens at the APPLICATION's decision point (kernel launch), not at the DRIVER's event (page fault or scheduling tick). This is fundamentally earlier and more precise than any driver-internal hook.

**2. Proactive prefetch on sync boundary** (`prefetch_gnn_proactive.bpf.c:399`)
```c
SEC("uprobe.s")
int BPF_UPROBE(cuda_sync_epoch_boundary) {
    // ... lookup feature tensor address from BPF map ...
    bpf_gpu_migrate_range(*va_space, feature->addr, length);
}
```
When cudaStreamSynchronize signals a training epoch boundary, this uprobe proactively migrates the feature tensor BEFORE the next epoch's GPU kernels start. The migration happens during the sync idle time, overlapping DMA with CPU work.

**3. MoE token boundary replay** (`prefetch_moe_expert.bpf.c:637`)
```c
SEC("uprobe")
int BPF_UPROBE(cuda_sync_token_boundary, void *stream) {
    // ... read expert fault bitmap from previous token ...
    // ... replay via bpf_gpu_migrate_range() for next token ...
}
```
At each token's sync point, this uprobe replays the previous token's page fault pattern to pre-migrate expert weights before they're needed.

**4. Allocation tracking** (`prefetch_moe_expert.bpf.c:551`)
```c
SEC("uprobe")
int BPF_UPROBE(cuda_malloc_managed_enter, void **dev_ptr, u64 size, u32 flags) {
    // ... record allocation address and size in BPF map ...
}
```
Transparently captures application memory allocation semantics (which addresses belong to which data structures) and makes them available to driver-level struct_ops policies.

### Why This Is Better Than the Struct_ops Story

The struct_ops + bpf_wq pipeline is reactive: it waits for a page fault, then makes a decision. The uprobe → kfunc path is **proactive**: it acts at application semantic boundaries (kernel launch, sync, allocation) BEFORE the driver even sees an event.

| | Reactive (struct_ops) | Proactive (uprobe → kfunc) |
|---|---|---|
| **Trigger** | Driver event (page fault, eviction) | Application event (kernel launch, sync, malloc) |
| **Timing** | After the problem occurs | Before the problem occurs |
| **Semantics** | Fault address, block size | Which kernel, which tensor, which phase |
| **Action** | Advisory (reorder list) or async (bpf_wq) | Direct and immediate (preempt NOW, migrate NOW) |

The reactive path handles the common case (steady-state memory management). The proactive path handles the critical case (multi-tenant preemption, phase transitions, proactive prefetch). Together, they form a complete policy surface.

### The Revised Capability Progression

| Layer | Mechanism | What it enables | Example |
|---|---|---|---|
| L1: Advisory hooks | struct_ops callbacks → return decision | Eviction ordering, prefetch hints | cache_ext / sched_ext level |
| L2: Active async ops | + bpf_wq + sleepable kfuncs | Cross-block prefetch, active migration | GNN 3.36x (27% beyond L1) |
| L3: Proactive app-boundary | + sleepable uprobe → kfunc | Pre-fault migration, instant preemption | LC preempt on cuLaunchKernel (-48% latency) |
| L4: Semantic relay | + uprobe captures → shared maps → struct_ops | App structure informs driver policy | Allocation tracking, phase detection |

> **[CRITICAL REVIEW NOTE]** This table uses -48% for L3 (from actual data), which is consistent. But `paper_structure_draft.md` and `intro_draft.md` use -95% for L3. The discrepancy must be resolved before writing paper text. See `critical_review.md` §II Problem 1.

L1 is what the paper currently presents. L2-L4 are what the paper hides. **The novelty lives in L2-L4.**

## What This Means for the Paper

### Section 4 (Design) should lead with:

1. **The async mismatch problem** (GPU DMA is milliseconds, fault handler is microseconds) — empirically demonstrated
2. **The capability table** showing struct_ops alone vs full system
3. **The async policy pipeline** as the architectural solution
4. **The kfuncs** as first-class design contributions, not implementation details

### Section 5 (Implementation) should explain:

1. How `bpf_gpu_migrate_range()` breaks the VA block boundary (what the UVM driver does internally)
2. How `bpf_nv_gpu_preempt_tsg()` achieves kernel-privilege GPU preemption
3. How multi-program composition works (kprobe captures context → struct_ops makes decision → bpf_wq executes)

### Section 6 (Evaluation) should include:

1. **The capability progression table** as the FIRST experiment (like XRP's Table 2)
2. **GNN 3.36x** deep-dived as the primary case study (shows the full async pipeline)
3. **Preemption kfunc** results as a secondary case study (shows cross-process GPU control)
4. Honest comparison: struct_ops-only vs full system, quantifying the gap

### The thesis becomes:

> "GPU resource management requires an extensible OS interface that goes beyond advisory hooks: it needs active, asynchronous, cross-boundary kernel operations exposed as safe eBPF kfuncs. We design gpu_ext, which composes struct_ops hooks with sleepable kfuncs and BPF work queues to create an async policy execution model for GPU drivers. gpu_ext's policies improve throughput by up to 4.8x and reduce tail latency by up to 2x, with the async pipeline alone contributing 27% beyond what synchronous hooks achieve."
