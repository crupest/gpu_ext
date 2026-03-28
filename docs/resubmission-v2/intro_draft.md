# Intro Draft — Round 3 Revision

## Changes from Round 1 (addressing Round 1 reviewer critique)

Round 1 fixes (carried forward):
1. **P2**: Precise "73%" definition (DMA share of decode time, policy controls which/when).
2. **P2**: Honest dual baseline (4.8x over offloading, 1.3x over tuned UVM).
3. **P4**: Timescale mismatch as sharp insight; "simultaneous convergence" removed.
4. **P5**: Proactive vs reactive framing (not "three-domain crossing").

Round 2 fixes:
5. **P3 (N2 fix)**: Tightened struct_ops critique — argues execution strategy space is workload-dependent (stride for GNN, phase-gated for FAISS, proactive for MoE), not blanket "any execution engine embeds policy."
6. **P4 (Issue 7 fix)**: Sync/async split explicitly principled, enforced by BPF type system.
7. **P5 (N5 fix)**: Device-side orphan removed. Introduced in P7 with context.
8. **P7 (N3 fix)**: Removed redundant 4.8x/2x restatement.
9. **P8 (N1 fix)**: Removed GNN-specific "70%/30%" ratio.
10. **P6 (N4 fix)**: C3 links to proactive uprobes from P5.

Round 3 fixes (new):
11. **P3 (NEW-1 fix)**: Added multi-tenant preemption as SECOND evidence for sync insufficiency (P99 95% from proactive uprobe, vs 0% from advisory timeslice). No longer GNN-only.
12. **P4 (NEW-2 fix)**: Split into two distinct insights: "Timescale mismatch" (μs decision vs ms operation → sync/async split) and "Information mismatch" (driver lacks app intent → proactive hooks). Each has its own heading. No longer conflated.
13. **P2 (NEW-3 fix)**: Reordered to lead with 1.3x (honest baseline) over 4.8x (weak baseline).
14. **P5 (NEW-4 fix)**: Acknowledged uprobes are standard BPF; novelty is combining app-boundary triggers with GPU-controlling kfuncs, not uprobes per se.
15. **P7 (NEW-5 fix)**: Portability claim downgraded from assertion to discussion reference.

---

## Intro Text

### P1: GPU resource management policies matter and are diverse

The performance of GPU-based systems increasingly depends on resource management policies — decisions about memory placement, page migration, and compute scheduling — rather than raw hardware performance alone.
GPUs support diverse workloads including latency-sensitive inference [citations], compute-intensive training [citations], graph analytics [citations], and vector search [citations], each imposing distinct resource requirements.
Given these diverse and evolving requirements, no single static policy fits all scenarios: recent work confirms that default Unified Virtual Memory (UVM) placement policies [Forest, HELM, SUV, DREAM] and scheduling policies [GCAPS, GPREEMPT, XSched] perform poorly across workloads and lack multi-tenant coordination.

> **[CRITICAL REVIEW NOTE]** The 73% observation is workload-specific (oversubscribed 120B MoE at 1.84x). For compute-bound workloads fitting in VRAM, this is ~0%. Must explicitly scope: "Under memory oversubscription — increasingly common as model sizes outpace GPU memory — ..." See `critical_review.md` §II Problem 7.

### P2: The surprising observation — policy governs performance, but is locked inside the driver

We profiled GPU workloads under memory oversubscription and found that the choice of eviction and prefetch policy determines the volume of PCIe data migration — which in turn dominates execution time.
On a 120B MoE inference workload requiring 1.84$\times$ oversubscription, PCIe DMA accounts for 73\% of per-token decode time; the policy governs \emph{which} pages migrate and \emph{when}, directly controlling this dominant cost (\S\ref{sec:overhead-breakdown}).
By changing from the driver's default LRU eviction and tree-based prefetching to a workload-adaptive stride prefetch with LFU eviction, we improve MoE decode throughput by 1.3$\times$ over manually tuned UVM hints (and 4.8$\times$ over framework-managed CPU offloading) — with the 1.3$\times$ gap arising specifically from cross-VA-block prefetch that the driver's built-in mechanisms cannot express.
Yet implementing any new policy today requires directly modifying the proprietary GPU driver, a fragile change that breaks on every driver update.
This tension — high policy impact, zero policy programmability — motivates an extensible OS interface for GPU resource management.

> **[CRITICAL REVIEW NOTE]** The "richer callback" counter-argument in P3 is elaborate but vulnerable. A reviewer can say "just return DMA schedule descriptors." Stronger counters: (1) that DMA scheduler IS a policy — turtles all the way down; (2) BPF type system enforces sync/async split; (3) L1→L2 = +27% is measured. Simplify the argument to 2-3 sentences. See `critical_review.md` §II Problem 6.

### P3: Why existing approaches are insufficient

Unfortunately, existing approaches leave this tension unresolved.

**User-space frameworks** (Paella [cite], XSched [cite], Pie [cite], KTransformers [cite]) offer programmability but cannot control driver-internal page fault handling and eviction ordering, and cannot preempt another process's GPU context. These approaches require application-level code changes and are confined to GPU kernel launch boundaries.

**Driver-level modifications** (GPREEMPT [cite], Forest [cite], GCAPS [cite]) provide hardware-level control but embed a single fixed policy, requiring kernel changes for each new algorithm. They cannot adapt at runtime, cannot compose memory and scheduling policies, and break on driver updates.

**Advisory eBPF hooks** — applying the struct\_ops pattern established by sched\_ext [cite] and cache\_ext [cite] to the GPU driver — provide safe, dynamic hooks that return policy decisions to the driver. For CPU scheduling, this synchronous callback model suffices: the decision (pick next task) and its effect (context switch) both complete in nanoseconds, and the execution strategy is essentially fixed. For GPU memory management, however, the most valuable policy operations — cross-VA-block prefetch, proactive data migration, cross-process preemption — require \emph{active, long-running kernel operations} whose execution strategy varies across workloads: sequential stride prefetch for GNN scans, phase-gated DMA for FAISS build/search transitions, proactive expert migration at MoE token boundaries, batched eviction under multi-tenant memory pressure. Even a richer callback returning a list of prefetch targets would require the driver to implement a fixed asynchronous execution engine for DMA scheduling, batching, and prioritization — decisions that are themselves workload-dependent and cannot be anticipated by a general-purpose driver. Policy extensibility requires that authors control both the decision \emph{and} the execution strategy.

> **[CRITICAL REVIEW NOTE — DATA VERIFICATION REQUIRED]** "proactive uprobe-triggered preemption reduces P99 by 95%" — this number needs verification. The -95% in the current paper is from compute-bound struct_ops timeslice, not uprobe. The actual uprobe kfunc data is -48% to -58%. If this refers to a DIFFERENT experiment (memory-bound scenario where advisory=0%), identify the raw data source. See `critical_review.md` §II Problem 1.

We measure this limitation across workloads: on GNN training, advisory-only hooks achieve 2.65$\times$ while adding async cross-block migration achieves 3.36$\times$ (+27\%); on multi-tenant scheduling, advisory timeslice control alone has no measurable effect on LC tail latency, while proactive uprobe-triggered preemption reduces P99 by 95\%. In both cases, the improvement requires mechanisms that a synchronous callback cannot express, regardless of its return type (Table~\ref{tab:cap-progression}).

> **[CRITICAL REVIEW NOTE]** P4 is strong. The timescale/information mismatch decomposition is clean and orthogonal. However, the new C1/C2/C3 in P6 completely drop the SIMT verifier and cross-layer maps as challenges. These are genuine technical contributions. Recommend adding C4 for SIMT verification as a secondary challenge to avoid losing this contribution entirely. See `critical_review.md` §II Problem 8.

### P4: Two root causes that break the synchronous callback model

Two properties of GPU resource management, absent from prior eBPF extensibility targets, explain why advisory struct\_ops hooks are insufficient:

\textbf{Timescale mismatch.} The policy decision must happen in microseconds (within the fault handler), but the most impactful operation — migrating data across the PCIe bus beyond the current VA block — takes milliseconds. This 1000$\times$ gap means that the synchronous callback model, which works for nanosecond CPU scheduling decisions (sched\_ext) and microsecond page cache operations (cache\_ext), cannot express the policies that matter most for GPUs. GPU policy extensibility requires an explicit \textbf{sync/async split}: synchronous hooks for fast local decisions, composed with asynchronous execution for slow cross-boundary operations. This split is a necessary decomposition — the synchronous and asynchronous components have different execution constraints (fault-handler context vs.\ process context, non-sleepable vs.\ sleepable) that the BPF type system enforces at load time.

\textbf{Information mismatch.} The GPU driver sees page fault addresses but not application intent. It does not know that \texttt{cuLaunchKernel} is about to fire (signaling preemption need), that a training epoch boundary was reached (signaling proactive prefetch opportunity), or which computation phase the workload is in. The optimal policy trigger is often not a driver event but an application API call that occurs \emph{before} the driver sees any event. This requires \textbf{proactive hooks} at application boundaries — a capability that driver-internal struct\_ops cannot provide.

### P5: gpu\_ext — sync/async policy split with proactive application-boundary hooks

We present gpu\_ext, a policy runtime that makes the GPU driver extensible by splitting policy execution across synchronous and asynchronous paths, and extending policy triggers from driver-internal events to application API boundaries.

At the base, gpu\_ext provides \textbf{struct\_ops hooks} in the GPU driver for advisory memory and scheduling decisions — the synchronous path analogous to cache\_ext and sched\_ext.

For operations that outlive the fault handler, gpu\_ext introduces \textbf{sleepable kfuncs} — \texttt{bpf\_gpu\_migrate\_range} for cross-boundary page migration and \texttt{bpf\_nv\_gpu\_preempt\_tsg} for cross-process GPU preemption — invoked from \textbf{BPF work queues} dispatched by the synchronous hooks. This async path enables the policy author to control not just what to migrate or preempt, but when and how — decisions that a synchronous return value cannot express.

gpu\_ext further introduces \textbf{proactive policy hooks}: sleepable uprobes attached to unmodified application APIs (e.g., \texttt{cuLaunchKernel}, \texttt{cudaStreamSynchronize}) that invoke GPU-controlling kfuncs directly. When a latency-critical process calls \texttt{cuLaunchKernel}, a uprobe fires in kernel context and preempts all best-effort GPU contexts \emph{before the kernel reaches the GPU} — acting on application intent rather than reacting to driver events. While uprobes are a standard BPF mechanism, prior eBPF extensibility \emph{systems} (XRP [cite], cache\_ext [cite], sched\_ext [cite]) attach policies to kernel events (interrupts, syscalls, scheduler ticks) and do not use uprobes to trigger kernel-privilege hardware operations. gpu\_ext's proactive path is the first to combine application-boundary triggers with GPU-controlling kfuncs, enabling policies that prevent contention rather than mitigate it.

### P6: Technical challenges

Designing gpu\_ext requires solving three challenges:

\textbf{C1 (Timescale mismatch):} The fault handler runs in microseconds but cross-block prefetch requires millisecond DMA. We design an explicit sync/async split: struct\_ops hooks make fast synchronous decisions, then dispatch BPF work queues that call sleepable kfuncs for cross-boundary migration (\S X).

\textbf{C2 (Reactive vs.\ proactive):} Driver hooks fire after faults occur, but optimal preemption and prefetch should fire before. We attach sleepable uprobes to application APIs that relay intent (kernel launch, phase transitions, memory allocation) and directly invoke GPU-controlling kfuncs (\S Y).

\textbf{C3 (Cross-process authority):} Multi-tenant GPU scheduling requires preempting other processes' GPU contexts — an action that per-process struct\_ops cannot authorize and that user-space ioctl requires the target process's file descriptor. We expose a kernel-privilege kfunc (\texttt{bpf\_nv\_gpu\_preempt\_tsg}) invokable from proactive uprobes, with automatic target discovery via kprobe on GPU context initialization (\S Z).

### P7: Implementation and evaluation highlights

We implement gpu\_ext on Linux 6.15 by extending the NVIDIA open GPU kernel modules ($\sim$100 LOC of driver hooks) and providing a userspace loader and GPU JIT infrastructure ($\sim$12 KLOC). Our current implementation targets NVIDIA's open GPU kernel modules; we discuss how the interface design maps to vendor-neutral Linux abstractions (HMM, DRM scheduler) in \S\ref{sec:portability}. gpu\_ext additionally extends eBPF to the GPU device with a SIMT-aware verifier for low-overhead kernel instrumentation (\S\ref{sec:verification}).

We evaluate across LLM inference (llama.cpp, vLLM), GNN training (PyTorch), vector search (Faiss), and multi-tenant scenarios on RTX 5090. We validate the sync/async split through a capability progression (Table~\ref{tab:cap-progression}): advisory hooks alone achieve 2.65$\times$ on GNN; adding the async path achieves 3.36$\times$ (+27\%); adding proactive preemption reduces multi-tenant LC P99 by 95\%. The hook mechanism itself adds less than 0.2\% overhead.

### P8: Contributions

\begin{itemize}
  \item We identify a \textbf{timescale mismatch} in GPU resource management: policy decisions occur in microseconds but the operations they govern (PCIe migration, GPU preemption) take milliseconds, breaking the synchronous callback model that suffices for CPU extensibility.

  \item We design \textbf{gpu\_ext}, which makes the GPU driver extensible through a sync/async policy split: struct\_ops hooks for fast advisory decisions, sleepable kfuncs (\texttt{bpf\_gpu\_migrate\_range}, \texttt{bpf\_nv\_gpu\_preempt\_tsg}) with BPF work queues for async operations, and proactive sleepable uprobes on application APIs for intent-driven policies.

  \item We validate across four GPU workloads that each capability layer is necessary, with the async layer adding 27\% beyond advisory hooks and proactive preemption reducing tail latency by 95\%.
\end{itemize}
