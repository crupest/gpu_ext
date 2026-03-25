# Resubmission V2: Concrete Improvement Plan

## Core Problem

Reviewer's verdict: **"host-side results alone may not be sufficient novelty over applying struct_ops to NVIDIA UVM."**

### What the paper actually uses per result (corrected assessment)

**Cross-layer / device-side / beyond-struct_ops results in the paper:**
- Memory microbenchmark 1.77x: device-side L2 prefetch + host-side callback (cross-layer)
- CLC block scheduler 11%: device-side `should_try_steal` (GPU-side eBPF)
- FAISS 21-29%: paper mentions "device-side can also trigger prefetching of posting lists"
- GNN 2.65x: uses **uprobe** to hook PyTorch user-space allocations (beyond struct_ops)
- Observability 3-14%: device-side eBPF tools (kernelretsnoop, threadhist, launchlate)
- Runtime overhead 60-80% reduction: device-side SIMT-aware execution

**Pure host-side struct_ops results:**
- llama.cpp 4.8x: stride prefetch + LFU eviction (struct_ops hooks)
- vLLM 1.3x: sequential prefetch policy (struct_ops hooks)
- Multi-tenant scheduling 95%: BPF struct_ops timeslice
- Multi-tenant memory 55-92%: struct_ops priority prefetch/eviction
- Two-tenant co-location: per-tenant struct_ops policies

### The real gap

The paper does use device-side and cross-layer mechanisms — but **the biggest headline numbers (4.8x llama, 1.3x vLLM) are pure host-side struct_ops**, while device-side contributions are concentrated in microbenchmarks (small vector-add, synthetic GEMM) and observability tools. The core reviewer concern is: **device-side eBPF and cross-layer mechanisms haven't demonstrated their value on real workloads producing headline improvements.**

## Root Cause of the "Just struct_ops" Perception

**The paper hides its own novelty.** The implementation has powerful mechanisms that the paper barely mentions or entirely omits:

| Mechanism | In implementation | In paper |
|-----------|-------------------|----------|
| `bpf_gpu_migrate_range()` — sleepable kfunc for cross-VA-block page migration | Used by cross-block prefetch, GNN 3.36x | **NOT MENTIONED** |
| `bpf_wq` — async BPF work queue for background prefetch | Used by 10+ prefetch policies | **NOT MENTIONED** |
| `bpf_nv_gpu_preempt_tsg()` — kernel-privilege cross-process GPU preemption | Used by uprobe_preempt_multi, -48% to -58% latency | **NOT MENTIONED** |
| 3-probe TSG handle capture (kprobe + kretprobe) | Used by preemption policies | **NOT MENTIONED** |
| kprobe on `uvm_perf_prefetch_get_hint_va_block` for va_block context | Used by cross-block prefetch | **NOT MENTIONED** |
| Multi-program composition (kprobe + struct_ops + bpf_wq + uprobe) | Used by all advanced policies | **NOT MENTIONED** |

The paper only shows 5 kfuncs: `move_head`, `move_tail`, `set_attr`, `reject_bind`, `gdrv_sched_preempt`. These ARE just struct_ops actuators — no wonder the reviewer concludes it's "just struct_ops for UVM."

## Strategic Repositioning

**Current thesis**: "eBPF extends GPU driver and device into a programmable OS subsystem"
- Problem: Paper presents the host-side as a simple struct_ops table + 5 trivial kfuncs. Reviewer correctly concludes this is incremental.

**Repositioned thesis**: "gpu_ext provides a **composable eBPF policy runtime** for GPU resource management, where multiple BPF program types (struct_ops, kprobe, uprobe, bpf_wq) and new kernel capabilities (sleepable kfuncs for GPU page migration and TSG preemption) compose into policies that are impossible with any single mechanism."

**The four dimensions of host-side novelty (beyond struct_ops):**

### Novelty 1: Async Policy Pipelines (bpf_wq + sleepable kfuncs)

Traditional struct_ops hooks are synchronous — callback returns, kernel continues. gpu_ext's memory hooks enable an **async policy pipeline**:

```
page fault → struct_ops hook (sync: intra-block decision)
                   ↓
              bpf_wq (async: schedule background work)
                   ↓
         sleepable kfunc bpf_gpu_migrate_range() (process context: cross-block DMA)
```

CPU sched_ext doesn't need this — scheduling decisions are instantaneous. But GPU memory management decisions trigger millisecond-scale DMA operations. A synchronous-only model forces the choice: either block the fault handler (adding latency) or give up on cross-block prefetch. bpf_wq + sleepable kfuncs solve this by decoupling the fast synchronous decision from the slow asynchronous migration.

**Evidence**: GNN cross-block direction-aware prefetch achieves 3.36x (vs 2.65x with sync-only always_max). The 27% additional improvement comes entirely from the async cross-block pipeline.

### Novelty 2: New Kernel Capabilities as kfuncs

`bpf_gpu_migrate_range()` is not a wrapper around an existing kernel API — it's a new capability:
- NVIDIA UVM's default tree-based prefetcher ONLY works within a single 2MB VA block
- `bpf_gpu_migrate_range()` allows BPF to specify arbitrary address ranges for migration, breaking the VA block boundary
- This enables algorithms (direction-aware, stride-based multi-block) that the UVM driver literally cannot express

`bpf_nv_gpu_preempt_tsg()` is another new capability:
- ioctl-based preemption requires the target process's fd (cross-process cooperation needed)
- BPF kfunc executes at RS_PRIV_LEVEL_KERNEL — can preempt ANY process's GPU context
- Combined with uprobe on cuLaunchKernel: "LC launches kernel → automatically preempt all BE" — zero application changes

These are NOT "hooks into existing kernel functions." They are NEW kernel operations exposed exclusively through the BPF kfunc interface.

### Novelty 3: Multi-Program-Type Composition

Cross-block prefetch composes **3 BPF program types**:
1. **kprobe** on `uvm_perf_prefetch_get_hint_va_block` → capture va_block/va_space context
2. **struct_ops** `gpu_page_prefetch` → intra-block decision + direction detection + bpf_wq dispatch
3. **bpf_wq callback** (sleepable) → call `bpf_gpu_migrate_range()` for cross-block migration

GPU preemption composes **4 BPF program types**:
1. **kprobe** on `nvidia_unlocked_ioctl` → capture hClient
2. **kprobe** on `nv_gpu_sched_task_init` → capture hTsg
3. **kretprobe** → confirm registration
4. **sleepable uprobe** on `cuLaunchKernel` → call `bpf_nv_gpu_preempt_tsg()`

No single struct_ops table can express either of these. The composition of multiple BPF program types into a coherent policy is the host-side's architectural contribution.

### Novelty 4: Application-Semantic Transparency via Uprobe

struct_ops hooks see driver-internal events (page faults, eviction pressure). But optimal policies need application semantics:
- GNN: which PyTorch allocation is about to be accessed?
- FAISS: is the workload in build phase or search phase?
- LLM serving: is this a prefill or decode token?

gpu_ext uses **uprobe** to transparently capture these semantics from unmodified applications, then routes them to struct_ops hooks via shared BPF maps. This is the "transparent" in "transparent and dynamically programmable" — the application doesn't know it's being observed, but the policy has application-level understanding.

---

## Concrete Changes

### Change 1: Replace "GNN 2.65x" with "GNN 3.36x (cross-block direction-aware)"

**What exists**: `extension/prefetch_cross_block_v2.bpf.c`, results in `workloads/pytorch/result/v1_*.json`

**Why this matters**: The 2.65x result uses only `always_max` (simple struct_ops parameter). The 3.36x result uses **cross-block direction-aware prefetch** which requires:
- Host-side struct_ops hook at `gpu_activate` / `gpu_prefetch`
- BPF work queue (`bpf_wq`) for async background migration
- Sleepable kfunc `bpf_gpu_migrate_range()` to move pages beyond the current 2MB VA block
- Direction tracking algorithm that identifies sequential scan direction

This is **architecturally impossible** with a simple struct_ops table — it requires the async kfunc + bpf_wq composition that gpu_ext uniquely provides.

**Paper change**: In §6.2.2 GNN, replace or augment the current 5-configuration comparison with a 6th configuration (XB direction-aware). Rewrite the paragraph to emphasize: *"gpu_ext's cross-block prefetch, enabled by sleepable kfuncs and async BPF work queues, achieves 3.36x — 27% beyond what single-block struct_ops policies achieve — by prefetching across 2MB VA block boundaries using direction detection."*

**Effort**: LOW — data exists, need to update Figure 7 and text.

---

### Change 2: Add GPU Preemption kfunc as a New Multi-Tenant Case Study

**What exists**:
- `kernel-module/nvidia-module/kernel-open/nvidia/nv-gpu-sched-hooks.c` (kfunc implementation)
- `extension/uprobe_preempt_multi.bpf.c` + `.c` (loader, 414+500 LOC)
- Test results: -48.4% latency (Test E), -57.6% latency (Test F)
- kfunc latency band: 177us (2x faster than ioctl's 354us)

**Why this matters**: This demonstrates **cross-process GPU control from kernel BPF context** — something neither user-space runtimes nor simple struct_ops can achieve:
- 3-probe handle capture (kprobe + kretprobe on nvidia_unlocked_ioctl + nv_gpu_sched_task_init): automatically discovers BE TSG handles without kernel modification
- Sleepable uprobe on `cuLaunchKernel`: triggers preemption directly from application API boundary
- RS_PRIV_LEVEL_KERNEL access: BPF kfunc can preempt any process's GPU context; ioctl requires target process fd

**Paper change**: Add to §6.3 (RQ2 Multi-Tenant) as a new paragraph or subsection:
- "**Fine-grained Preemptive Scheduling via kfunc**" — compare native timeslice (current §6.3.1) vs kfunc preemption
- Show: latency reduction (-48% to -58%), preemption latency (177us kfunc vs 354us ioctl)
- Emphasize: this requires BPF-kernel cooperation (uprobe + kfunc + struct_ops) that no single mechanism provides

**Effort**: MEDIUM — results exist but need figure creation and integration into eval section.

---

### Change 3: Add CPU-GPU Coordination (xCoord) as New Cross-Layer Result

**What exists**:
- `extension/sched_gpu_aware.bpf.c` + `sched_gpu_xcoord.bpf.c`
- `extension/prefetch_always_max_xcoord.bpf.c`
- Shared BPF maps: `gpu_state_map`, `uvm_worker_pids`, `gpu_process_pids`
- vLLM 30B + stress: TPOT -7.4%, throughput +8.5%
- FPRS (E11, 20B + FAISS): mean -4.8%, P99 -4.9%, throughput +5.3%

**Why this matters**: This is the **strongest argument against "just struct_ops for UVM"** — it demonstrates:
- GPU memory state (fault_rate, eviction_pressure) flowing through shared BPF maps to CPU sched_ext scheduler
- Neither GPU-only nor CPU-only optimization achieves this; it requires cross-subsystem coordination
- First demonstration of GPU memory pressure as a first-class CPU scheduling signal

**Paper change**: Add to §6.3 or as a new RQ2 case study:
- "**Cross-Subsystem Coordination: GPU Memory + CPU Scheduling**"
- Show: vLLM serving + background stress, TPOT reduction via fault-aware CPU scheduling
- Architecture diagram: GPU fault handler → shared BPF map → sched_ext policy → CPU priority adjustment
- Compare: gpu_ext-only (memory policy), sched_ext-only (CPU policy), xCoord (coordinated)

**Effort**: MEDIUM-HIGH — results exist but need careful framing and new figure.

---

### Change 4: Strengthen Baselines

#### 4a: Add UVM+hints as explicit llama.cpp baseline
The paper says hints "still fall behind our policy" but doesn't quantify. From MEMORY.md: UVM hints achieve similar pp but lower tg than gpu_ext. Add this as an explicit bar in Figure 5 and quantify the gap.

**Effort**: LOW — data likely exists, just need to add to figure.

#### 4b: Explain missing baselines (Forest, HELM, DREAM, SUV)
Add a paragraph in §6.1 Methodology explaining why these concurrent works are not compared:
- Forest/SUV/DREAM modify the GPU driver with fixed policies (not programmable)
- They target different hardware (AMD, older NVIDIA)
- gpu_ext is an extensibility framework; these are fixed algorithms that could be *implemented as* gpu_ext policies
- Include Table: "X's policy could be expressed in gpu_ext as Y lines of code" (like sched_ext papers do for CPU schedulers)

**Effort**: LOW — text-only change, strengthen positioning.

#### 4c: Use real graph dataset for GNN (optional)
If feasible, replace random graphs with OGB-Products or Reddit. This addresses reviewer concern about random graph locality overstating results.

**Effort**: HIGH — requires new experiments. Consider for camera-ready.

---

### Change 5: Strengthen Device-Side Evaluation

#### 5a: Add verifier evaluation subsection (§6.4 RQ3)
From `verifier_eval_outline.md`, add a compact evaluation:
- Load-time verification latency for all 6 device-side artifacts (16-347 LOC)
- Rejection test suite: N programs rejected across 5 categories (non-uniform branch, non-uniform loop bound, non-uniform map key, forbidden sync/atomic, budget overflow)
- Compare: Linux verifier alone would accept 3/5 unsafe categories → SIMT verifier is necessary

**Effort**: MEDIUM — need to collect data, write test suite.

#### 5b: Device-side observability on RTX 5090
Current Table 2 only shows P40 results. Add RTX 5090 column for kernelretsnoop, threadhist, launchlate.

**Effort**: LOW — just run existing tools on 5090.

#### 5c: CLC block scheduler on a real workload
The synthetic GEMM result is unconvincing. Try CLC on:
- llama.cpp decode (known to have SM imbalance from expert routing)
- Or a real GEMM from training pipeline
If results are neutral, honestly report that and explain: device-side scheduling helps specific imbalance patterns, not all workloads.

**Effort**: MEDIUM-HIGH — needs experimentation, may yield negative result.

---

### Change 6: Rewrite Key Paper Sections

#### 6a: Reposition Introduction
Current §1 paragraph 4: "We present gpu_ext, a cross-layer policy runtime..."

Add explicit emphasis: *"Unlike applying existing struct_ops patterns to a new subsystem, gpu_ext enables policies that span multiple kernel subsystems (UVM, GPU scheduler, CPU scheduler) through sleepable kfuncs, async BPF work queues, and shared cross-layer maps — mechanisms that no single hook table can express."*

#### 6b: Add "Why Not Just struct_ops?" discussion
Either in §4 Design or §7 Discussion, add a subsection addressing this directly:
1. **Async cross-block prefetch** requires bpf_wq + sleepable kfuncs (struct_ops are synchronous)
2. **Cross-process preemption** requires kernel-privilege kfuncs (struct_ops can only act on current context)
3. **CPU-GPU coordination** requires shared maps across subsystems (struct_ops is single-subsystem)
4. **Phase detection** requires uprobe + struct_ops composition (single hook table can't see application semantics)

#### 6c: Remove placeholder values and internal annotations
- Fix `\speedupAccel` = "xxx" and `\speedupMonitor` = "xxx"
- Remove all `\xiangyu{...}`, `\yusheng{...}`, `\todo{...}` comments
- Clean up commented-out figure/text

**Effort**: LOW — editorial cleanup.

---

### Change 7: Add Confidence Intervals

All bar charts should include error bars (stddev or 95% CI) from the 10-trial data. This is a basic methodological expectation.

**Effort**: LOW — data exists, update plotting scripts.

---

## Priority Ordering

| Priority | Change | Impact on "host-side only" criticism | Effort |
|----------|--------|--------------------------------------|--------|
| **P0** | Change 1: GNN 3.36x cross-block | Directly shows cross-layer > struct_ops alone | LOW |
| **P0** | Change 6b: "Why Not Just struct_ops?" | Directly addresses the core criticism | LOW |
| **P0** | Change 6c: Remove placeholders/annotations | Removes easy rejection signals | LOW |
| **P1** | Change 2: kfunc preemption case study | Shows kernel-privilege GPU control via BPF | MEDIUM |
| **P1** | Change 4a: UVM+hints baseline | Removes "weak baseline" objection | LOW |
| **P1** | Change 4b: Explain missing baselines | Removes "missing comparison" objection | LOW |
| **P1** | Change 7: Confidence intervals | Removes methodological objection | LOW |
| **P2** | Change 3: xCoord CPU-GPU coordination | Strongest "cross-layer" evidence | MEDIUM-HIGH |
| **P2** | Change 5a: Verifier evaluation | Addresses "verifier unproven" objection | MEDIUM |
| **P2** | Change 5b: Device-side observability on 5090 | Removes "only P40" objection | LOW |
| **P3** | Change 5c: CLC on real workload | Addresses "synthetic only" for device scheduler | MEDIUM-HIGH |
| **P3** | Change 4c: Real graph dataset | Addresses "random graph" concern | HIGH |

---

## Expected Outcome

After P0+P1 changes, the paper can credibly claim:

> "gpu_ext is not merely struct_ops applied to NVIDIA UVM. Its value lies in cross-layer policy composition: async kfuncs enable cross-block prefetch (GNN 3.36x, +27% beyond single-block), kernel-privilege kfuncs enable cross-process GPU preemption (-48% to -58% latency), and shared BPF maps bridge GPU memory state with CPU scheduling. These policies require multiple framework mechanisms working together — bpf_wq, sleepable kfuncs, uprobe hooks, cross-subsystem maps — that no single struct_ops table can express."

After P2 changes, the paper additionally demonstrates:
- CPU-GPU coordination as a new capability (vLLM +8.5% via fault-aware CPU scheduling)
- Verified device-side safety guarantees (N programs tested, M rejected, load-time cost)
- Device-side observability on latest hardware

This repositions the paper from "eBPF for GPU" to **"cross-layer policy composition for heterogeneous systems"** — a thesis that OSDI reviewers would find genuinely novel.

---

## What NOT to Do

1. **Don't add device-side eBPF results that are negative or marginal** — this confirms the reviewer's suspicion that device-side isn't useful. Only add if genuinely positive.
2. **Don't overfit to reviewer's specific questions** — address the underlying concern (novelty) not the surface questions.
3. **Don't make the paper longer** — the paper is already at page limit. For every paragraph added, one must be removed. Target: remove §3 (Design Principles) by folding into §4 introduction, and condense the repetitive challenge descriptions.
4. **Don't claim device-side eBPF is the main contribution if it isn't** — instead, honestly reposition around cross-layer coordination, which is actually the paper's strongest story.
