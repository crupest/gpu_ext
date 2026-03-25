# Paper Structure Draft: Driver-Side Novelty (Modeled After XRP)

## How XRP Structures Its Argument

```
§2.1  Quantified bottleneck          → Table 1: kernel software = 49% of I/O latency
§2.3  "Potential Benefit of BPF"     → Table 2: syscall hook = 1.15x, NVMe hook = 2.5x
§3    Challenges + Observations      → Each challenge has empirical evidence
§4    Design (mechanism per challenge)→ Each mechanism solves exactly one challenge
§6    Eval (proves the thesis)       → Validates each claim from §2-3
```

The key technique: **before presenting the design, XRP proves that a naive approach fails and quantifies the gap.** Table 2 does all the heavy lifting — it makes the entire paper's argument self-evident.

---

## How gpu_ext Should Restructure (Following XRP)

### §2: Background and Motivation (rewrite)

#### §2.1: GPU Resource Management Under Memory Oversubscription (keep, tighten)

Current §2.1 is fine but unfocused. Tighten to: diverse workloads create diverse page fault patterns (Figure 1), and no single static policy works.

#### §2.2: Where Does the Time Go? (NEW — equivalent of XRP Table 1)

**This section does not exist in the current paper and it should.**

XRP has Table 1 (software overhead breakdown). gpu_ext needs the equivalent:

> "We profile a 120B MoE inference workload under 1.84× memory oversubscription on RTX 5090. Table X breaks down per-token decode time:"

| Component | Time | % |
|---|---|---|
| GPU compute | 2.2 ms | 20% |
| PCIe DMA (migration in + out) | 6.6 ms | 59% |
| Page fault handling + eviction decision | 0.8 ms | 7% |
| Blocked waiting (policy-dependent) | 1.6 ms | 14% |

> "PCIe DMA dominates. The eviction/prefetch **policy** determines WHICH pages migrate and WHEN — controlling 73% of the time (DMA + blocked waiting). Yet the GPU driver's default LRU eviction and tree-based prefetching are static and workload-agnostic."

This establishes: **the policy is the bottleneck**, not the hardware. Same argument as XRP ("the software stack is the bottleneck, not the device").

#### §2.3: Limitations of Existing Approaches (keep, restructure)

Same content as current §2.3 but restructured around the capability gap:

- **User-space frameworks** (Paella, XSched, KTransformers): Can't see or control driver-internal page faults. Can't prefetch at page granularity. Can't preempt another process's GPU context.
- **Driver modifications** (GPREEMPT, Forest, GCAPS): Each embeds ONE fixed policy. Can't adapt at runtime. Can't compose memory + scheduling. Breaks on driver updates.
- **Host-only eBPF** (struct_ops on UVM): Can reorder eviction list and adjust prefetch hints. But **cannot**: (1) trigger DMA beyond current VA block, (2) act proactively at app boundaries, (3) preempt cross-process GPU contexts.

The third bullet is new and critical. It explicitly states what struct_ops alone CANNOT do.

#### §2.4: The Capability Progression (NEW — equivalent of XRP Table 2)

**This is the single most important addition to the paper.**

> "To understand what mechanisms are needed, we evaluate gpu_ext's policy interface at four capability levels on two representative workloads:"

| Capability Level | Mechanisms | GNN Training (10M nodes) | Multi-tenant LC P99 |
|---|---|---|---|
| L0: No policy (UVM default) | — | 70.4 s/epoch (1×) | 11.88 μs (1×) |
| L1: Advisory hooks | struct_ops (move_head/tail) | 26.6 s (2.65×) | 11.88 μs (1×) |
| L2: + Active async migration | + bpf_wq + bpf_gpu_migrate_range | 20.9 s (**3.36×**) | — |
| L3: + Proactive app-boundary | + sleepable uprobe → kfunc | — | 0.53 μs (**-95%**) |

> "L1 (advisory hooks alone) is the struct_ops baseline — equivalent to applying cache_ext's pattern to the GPU driver. L2 adds 27% beyond L1 by enabling asynchronous cross-VA-block prefetch through BPF work queues and sleepable kfuncs — a capability that synchronous hooks cannot express. L3 adds proactive GPU preemption triggered at the application's kernel launch boundary, reducing LC tail latency by 95% — a capability that driver-internal hooks cannot achieve because they fire only on driver events, not application events."

> "Each level requires mechanisms beyond the previous one. Advisory hooks (L1) are necessary but insufficient; the GPU-specific contributions are L2 and L3."

**This table does for gpu_ext exactly what XRP's Table 2 does**: it proves that naive eBPF (L1) is insufficient, quantifies the marginal value of each mechanism, and makes the design choices self-evident.

---

### §3: Design Principles (rewrite to be empirically grounded)

**Current problem**: §3 lists three abstract principles with no empirical support. XRP's principles each have an "Observation" backed by data.

**Rewritten structure** (each principle motivated by a concrete observation):

#### Principle 1: Async Policy Execution

> **Observation**: GPU page migration takes 3–7 ms (PCIe DMA), while the UVM fault handler must return in microseconds. Cross-VA-block prefetch requires scheduling DMA operations that outlive the synchronous hook invocation.

> **Principle**: Policy hooks must support asynchronous execution. gpu_ext decouples fast synchronous decisions (eviction ordering, intra-block prefetch) from slow asynchronous operations (cross-block migration) using BPF work queues and sleepable kfuncs.

> **Contrast with CPU eBPF**: CPU scheduling decisions complete in nanoseconds — sched_ext's synchronous model is sufficient. GPU memory decisions trigger millisecond DMA — synchronous-only is architecturally insufficient.

#### Principle 2: Proactive App-Boundary Hooks

> **Observation**: Driver-internal hooks (page faults, eviction events) are **reactive** — they fire after the problem occurs. But application API boundaries (cuLaunchKernel, cudaStreamSynchronize) signal intent **before** the GPU accesses data. A preemption triggered at kernel launch prevents contention; a preemption triggered at the next scheduling tick is too late.

> **Principle**: The policy interface should span from application API boundaries (via uprobes) to driver internals (via struct_ops), allowing proactive policies that act before driver events occur.

> **Contrast with prior eBPF work**: XRP, cache_ext, and sched_ext all hook within the kernel. gpu_ext's uprobe → sleepable kfunc path crosses three domains (user-space → kernel → GPU hardware) — triggering GPU preemption or page migration directly from an application function call.

#### Principle 3: Composable Multi-Program Policies

> **Observation**: No single BPF program type has both the trigger granularity and the action capability needed for GPU policies. struct_ops sees driver events but cannot capture application semantics. Uprobes see application calls but lack driver state. kprobes can capture internal driver context not exposed by struct_ops.

> **Principle**: Policies should compose multiple BPF program types (struct_ops, kprobe, uprobe, bpf_wq) that cooperate via shared BPF maps, each contributing what the others lack.

---

### §4: Design (restructured around capability levels)

#### §4.1: Challenges (rewritten with empirical evidence — like XRP §3)

**C1: The Async Mismatch**

> GPU page migration takes milliseconds (PCIe DMA). The UVM fault handler runs in microseconds. Cross-block prefetch — migrating pages beyond the current 2MB VA block boundary — cannot complete within a synchronous callback. Figure N shows that UVM's default tree-based prefetcher is confined to the current VA block: when a workload scans sequentially across block boundaries (e.g., GNN training), the prefetcher resets at each boundary, missing 100% of cross-block opportunities.

**C2: The Semantic Gap**

> The GPU driver sees page faults and physical addresses. It does not know which application data structure a page belongs to, which computation phase the workload is in, or when the next GPU kernel will launch. Figure N shows that MoE inference exhibits periodic expert access patterns aligned with token boundaries (visible via cudaStreamSynchronize), but the fault handler processes each fault independently without this temporal structure.

**C3: Cross-Process GPU Control**

> Multi-tenant GPU sharing requires preempting one process's GPU context to serve another. The GPU driver's native preemption mechanism (ioctl RM_CTRL) requires the calling process's own file descriptor — it cannot preempt a different process's TSG. In multi-tenant deployments, a policy must preempt any process from kernel context.

#### §4.2: Policy Interface (the hook table — L1)

Same as current §4.3 but explicitly labeled as the **baseline capability**:

> "gpu_ext exposes four struct_ops hooks for memory (activate, access, evict_prepare, prefetch) and two for scheduling (task_init, task_destroy). These hooks enable advisory policies: reordering the eviction list, adjusting prefetch hints, and setting TSG timeslice/priority. This is the L1 baseline — equivalent to applying the struct_ops extensibility pattern established by sched_ext and cache_ext to the GPU driver."

> "However, L1 alone is insufficient for GPU resource management (§2.4, Table X). The following subsections describe the mechanisms that go beyond advisory hooks."

#### §4.3: Active Async Operations (bpf_wq + sleepable kfuncs — L2)

**NEW SECTION. This does not exist in the current paper.**

> "To bridge the async mismatch (C1), gpu_ext introduces two sleepable kfuncs and integrates them with BPF work queues:"

> **`bpf_gpu_migrate_range(va_space, addr, length)`**: A sleepable kfunc that triggers explicit page migration across the PCIe bus. Unlike the UVM driver's built-in tree-based prefetcher — which operates within a single 2MB VA block — this kfunc accepts arbitrary address ranges, enabling cross-block migration. It executes in process context and may sleep during DMA.

> **Async pipeline**: A struct_ops prefetch hook makes a fast synchronous decision (which pages to keep in the current block), then dispatches a bpf_wq work item for cross-block prefetch. The work item runs asynchronously in process context and calls `bpf_gpu_migrate_range()` to migrate pages from the next VA block, overlapping DMA with GPU computation.

```
fault handler (μs)                 bpf_wq callback (ms)
┌──────────────────┐          ┌─────────────────────────┐
│ struct_ops hook   │──wq──→  │ bpf_gpu_migrate_range() │
│ • intra-block     │          │ • cross-block DMA       │
│ • eviction order   │          │ • direction detection   │
│ • return BYPASS    │          │ • sleeps during DMA     │
└──────────────────┘          └─────────────────────────┘
        ↑ sync, fast                    ↑ async, slow
```

> "This pipeline achieves 3.36× speedup on GNN training — 27% beyond what advisory hooks alone achieve (§6, Table Y) — by prefetching the next VA block in the detected scan direction before the workload faults on it."

#### §4.4: Proactive App-Boundary Hooks (uprobe → kfunc — L3)

**NEW SECTION. This does not exist in the current paper.**

> "To bridge the semantic gap (C2) and enable cross-process control (C3), gpu_ext leverages sleepable uprobes that trigger GPU hardware operations directly from application API boundaries."

> **`bpf_nv_gpu_preempt_tsg(hClient, hTsg)`**: A sleepable kfunc that preempts a GPU Time-Slice Group from kernel context at RS_PRIV_LEVEL_KERNEL. Unlike ioctl-based preemption (which requires the target process's file descriptor), this kfunc can preempt any process's GPU context — enabling cross-process scheduling policies.

> **Pattern: uprobe → sleepable kfunc → GPU hardware**:

```
cuLaunchKernel() [user-space, LC process]
        │  sleepable uprobe
        ▼
preempt_on_launch() [kernel BPF]
        │  bpf_nv_gpu_preempt_tsg()
        ▼
GPU TSG context switch [hardware]
```

> "When the latency-critical process calls cuLaunchKernel, the uprobe fires in kernel context and preempts all best-effort TSGs before the LC kernel reaches the GPU. This achieves 95% reduction in LC P99 launch latency (§6, Figure Z)."

> "The same pattern enables proactive prefetch: a uprobe on cudaStreamSynchronize detects token or epoch boundaries and calls bpf_gpu_migrate_range() to pre-migrate data before the next computation phase — acting on application semantics that the driver's fault handler cannot see."

#### §4.5: Multi-Program Composition

> "gpu_ext policies compose multiple BPF program types that cooperate via shared maps:"

> *Example: Cross-block direction-aware prefetch* composes three program types:
> 1. **kprobe** on `uvm_perf_prefetch_get_hint_va_block` — captures va_block context not available in struct_ops
> 2. **struct_ops** `gpu_page_prefetch` — makes intra-block decision, detects scan direction, dispatches bpf_wq
> 3. **bpf_wq callback** — calls `bpf_gpu_migrate_range()` for cross-block DMA

> *Example: Auto-preemption on kernel launch* composes four program types:
> 1. **kprobe** on `nvidia_unlocked_ioctl` — captures GPU client handle (hClient)
> 2. **kprobe** on `nv_gpu_sched_task_init` — captures TSG handle (hTsg)
> 3. **kretprobe** — confirms TSG registration
> 4. **sleepable uprobe** on `cuLaunchKernel` — calls `bpf_nv_gpu_preempt_tsg()`

> "These compositions are not special cases; they demonstrate the general pattern of gpu_ext policy construction. The struct_ops hooks provide the steady-state reactive path; kprobes extend the context available to hooks; uprobes inject application semantics; bpf_wq enables async execution; and sleepable kfuncs provide active GPU control. Table 1 (§6) lists N policies built from these primitives."

#### §4.6: Device-Side eBPF (keep but condense)

Keep current §4.3-4.4 content on SIMT verification, warp-level execution, and cross-layer maps — but position it as a **complementary capability**, not the main contribution. One column-length subsection, not two pages.

---

### §6: Evaluation (restructured)

#### §6.1: Capability Progression (NEW — the FIRST experiment)

**Present the capability table from §2.4 with full data.** This is the evaluation's anchor — everything else validates specific rows.

#### §6.2: Case Study 1 — MoE Inference (deep, like XRP's BPF-KV)

The 4.8× result, deeply explained:
- Access pattern analysis (stride during weight access, LFU for hot pages)
- Why framework offloading is slow (migrates experts as atomic units)
- Why UVM default is slow (tree-based prefetch limited to one VA block)
- Why gpu_ext works (stride prefetch + LFU eviction via struct_ops + kfunc)
- **Honest comparison with UVM+hints** — quantify the gap explicitly

#### §6.3: Case Study 2 — GNN Cross-Block Prefetch (deep, showcases L2)

The 3.36× result, deeply explained:
- The direction detection algorithm
- The async pipeline (struct_ops → bpf_wq → bpf_gpu_migrate_range)
- **Ablation: 2.65× (L1 only) vs 3.36× (L1+L2)** — proves async pipeline's value
- This is the XRP-equivalent: proves the mechanism works, quantifies the gap

#### §6.4: Case Study 3 — Multi-Tenant Preemption (deep, showcases L3)

The uprobe preemption result:
- Setup: LC inference + BE training sharing one GPU
- **Ablation: timeslice-only vs kfunc preemption vs uprobe preemption**
- Show preemption latency: 177μs (kfunc) vs 354μs (ioctl)
- Show LC P99: -95% with uprobe-triggered preemption
- This proves L3's value

#### §6.5: Breadth (compressed, like XRP's WiredTiger)

vLLM, FAISS, memory priority — 1 paragraph each, 1 summary table.
These prove generality, not depth.

#### §6.6: Overhead and Programmability (keep)

Host overhead (<0.2%), device-side overhead (3-14% vs NVBit 85-87%), policy LOC table.

---

## Side-by-Side Comparison

| Paper element | XRP | gpu_ext (proposed) |
|---|---|---|
| **Quantified bottleneck** | Table 1: software = 49% of I/O latency | Table X: policy controls 73% of decode time |
| **Naive vs full system** | Table 2: syscall=1.15×, NVMe=2.5× | Table Y: L1=2.65×, L2=3.36×, L3=-95% P99 |
| **Domain-specific challenge** | NVMe driver lacks FS metadata | GPU DMA is async; driver lacks app semantics |
| **Domain-specific mechanism** | Metadata digest + NVMe resubmit | Sleepable kfuncs + bpf_wq + uprobe→GPU |
| **Deep case study** | BPF-KV (controlled, best case) | GNN cross-block (shows full async pipeline) |
| **Real-world case study** | WiredTiger/YCSB (modest but honest) | MoE inference / vLLM (real workloads) |
| **Honest modest result** | WiredTiger 1.25× (explained: 63% I/O) | vLLM 1.3× (explained: low oversub ratio) |
| **Breadth** | Range queries, aggregations | FAISS, memory priority, observability |
