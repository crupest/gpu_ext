# Intro Draft 2 — Three Boundaries Framing

## Key difference from intro_draft.md

intro_draft.md uses **two root causes**: timescale mismatch + information mismatch.
This draft uses **three execution boundaries**: sync/async + driver/app + host/device.

The critical change: "information mismatch" from draft 1 conflated two distinct gaps:
- Driver doesn't see application intent → boundary 2 (driver/app)
- Host doesn't see device execution patterns → boundary 3 (host/device)

Draft 1 hid device-side inside "information mismatch" and introduced it as "additionally" in P7.
**Draft 2 makes device-side the THIRD boundary crossing — naturally integral, not bolted on.**

### Side-by-side comparison

| | intro_draft.md (two mismatches) | intro_draft2.md (three boundaries) |
|---|---|---|
| P1 | Same | Same |
| P2 | Same | Same |
| P3 | Three critiques (user-space, driver-mod, advisory eBPF) | Same three critiques, but evidence paragraph restructured |
| **P4** | **Two root causes: timescale + information** | **Three boundaries: sync/async + driver/app + host/device** |
| **P5** | **Two paths: sync/async + proactive uprobes. Device = "additionally" in P7** | **Three mechanisms: bpf_wq + uprobe + device BPF + cross-layer maps. Device is here.** |
| **P6** | **C1=timescale, C2=reactive/proactive, C3=cross-process** | **C1=sync/async, C2=driver/app, C3=host/device, C4=SIMT safety** |
| **P7** | Device-side mentioned in 1 sentence ("additionally extends eBPF to GPU device") | Device-side already introduced in P5; P7 focuses on capability progression results |
| **P8** | Contributions: timescale insight + sync/async design + validation | Contributions: three-boundary insight + composable cross-layer design + capability progression validation |

---

## Intro Text

### P1: GPU resource management policies matter and are diverse

*(Identical to intro_draft.md P1 — no change needed.)*

The performance of GPU-based systems increasingly depends on resource management policies — decisions about memory placement, page migration, and compute scheduling — rather than raw hardware performance alone.
GPUs support diverse workloads including latency-sensitive inference [citations], compute-intensive training [citations], graph analytics [citations], and vector search [citations], each imposing distinct resource requirements.
Given these diverse and evolving requirements, no single static policy fits all scenarios: recent work confirms that default Unified Virtual Memory (UVM) placement policies [Forest, HELM, SUV, DREAM] and scheduling policies [GCAPS, GPREEMPT, XSched] perform poorly across workloads and lack multi-tenant coordination.

### P2: The surprising observation — policy governs performance, but is locked inside the driver

*(Identical to intro_draft.md P2 — no change needed.)*

We profiled GPU workloads under memory oversubscription — increasingly common as model sizes outpace GPU memory — and found that the choice of eviction and prefetch policy determines the volume of PCIe data migration, which in turn dominates execution time.
On a 120B MoE inference workload requiring 1.84$\times$ oversubscription, PCIe DMA accounts for 73\% of per-token decode time; the policy governs \emph{which} pages migrate and \emph{when}, directly controlling this dominant cost.
By changing from the driver's default LRU eviction and tree-based prefetching to a workload-adaptive stride prefetch with LFU eviction, we improve MoE decode throughput by 1.3$\times$ over manually tuned UVM hints (and 4.8$\times$ over framework-managed CPU offloading) — with the 1.3$\times$ gap arising specifically from cross-VA-block prefetch that the driver's built-in mechanisms cannot express.
Yet implementing any new policy today requires directly modifying the proprietary GPU driver, a fragile change that breaks on every driver update.
This tension — high policy impact, zero policy programmability — motivates an extensible OS interface for GPU resource management.

### P3: Why existing approaches are insufficient

*(First two critiques identical to intro_draft.md. Third critique simplified per critical_review.md.)*

Unfortunately, existing approaches leave this tension unresolved.

**User-space frameworks** (Paella, XSched, Pie, KTransformers) offer programmability but cannot control driver-internal page fault handling and eviction ordering, and cannot preempt another process's GPU context. These approaches require application-level code changes and are confined to GPU kernel launch boundaries.

**Driver-level modifications** (GPREEMPT, Forest, GCAPS) provide hardware-level control but embed a single fixed policy, requiring kernel changes for each new algorithm. They cannot adapt at runtime, cannot compose memory and scheduling policies, and break on driver updates.

**Advisory eBPF hooks** — applying the struct\_ops pattern from sched\_ext and cache\_ext to the GPU driver — provide safe, dynamic hooks that return policy decisions. For CPU scheduling and page caching, this synchronous callback model suffices: decisions and effects complete at the same timescale, and the host kernel has full visibility into the relevant state. For GPU resource management, however, synchronous single-layer hooks leave significant performance unrealized. We measure this across workloads: on GNN training, advisory-only hooks achieve 2.60$\times$ while adding asynchronous cross-block migration achieves 3.29$\times$ (+27\%); on multi-tenant scheduling, advisory hooks alone have no measurable effect on tail latency, while proactive application-boundary preemption reduces P99 substantially. In both cases, the improvement requires mechanisms that a synchronous, driver-internal callback cannot express (Table~\ref{tab:cap-progression}).

> **[vs intro_draft.md P3]** Simplified. Removed the long "richer callback" argument (vulnerable to "just add a DMA scheduler API" counter). Instead, lead with empirical evidence (+27% gap, P99 gap) and let the data speak. The *why* comes in P4.

### P4: Three execution boundaries that break the single-layer model

**[THIS IS THE KEY CHANGE FROM intro_draft.md]**

Three properties of GPU resource management — absent from CPU scheduling, page caching, and NVMe I/O — explain why synchronous, single-layer struct\_ops hooks are insufficient. Each property corresponds to an execution boundary that policy must cross:

\textbf{Boundary 1: Synchronous / Asynchronous.}
The policy decision must happen in microseconds (within the fault handler), but the most impactful operation — migrating data across the PCIe bus beyond the current VA block — takes milliseconds. This 1000$\times$ gap does not exist in CPU scheduling (nanosecond decisions and effects) or page caching (microsecond decisions and effects). A synchronous callback cannot trigger a cross-block prefetch and wait for the DMA to complete. GPU policy extensibility requires an explicit \textbf{sync/async split}: synchronous hooks for fast local decisions, composed with asynchronous execution paths for slow cross-boundary operations. This split is enforced by the BPF type system at load time — synchronous hooks run in non-sleepable fault-handler context; asynchronous operations run in sleepable process context via BPF work queues.

\textbf{Boundary 2: Driver / Application.}
The GPU driver fires hooks on page faults and eviction events — it reacts to problems that have already occurred. But optimal policy timing is often \emph{proactive}: the best moment to preempt best-effort GPU contexts is when a latency-critical process calls \texttt{cuLaunchKernel}, \emph{before} the kernel reaches the GPU; the best moment to prefetch is at an epoch or token boundary signaled by \texttt{cudaStreamSynchronize}, \emph{before} the next batch of faults. The driver does not see these application API calls. Prior eBPF extensibility systems (XRP, cache\_ext, sched\_ext) attach policies to kernel-internal events and do not cross into application context for policy triggers. GPU policy extensibility requires \textbf{proactive hooks at application boundaries} — uprobes on unmodified application APIs that invoke GPU-controlling kernel functions directly.

\textbf{Boundary 3: Host / Device.}
The host driver controls memory placement (eviction ordering, page migration, prefetch range) and compute scheduling (timeslice, preemption). But it cannot observe \emph{how} the GPU uses its resources: per-warp memory access patterns (sequential scan vs random probe), SM load distribution, and computation phase boundaries are visible only inside the GPU device. Conversely, the device can observe execution but cannot control host-level resource allocation. Effective policies require a \textbf{closed observation-action loop}: device-side instrumentation classifies workload behavior and writes observations into shared state; host-side policy reads this state and selects the appropriate strategy. GPU policy extensibility requires extending BPF to the GPU device with cross-layer shared maps.

> **[vs intro_draft.md P4]** "Two root causes" → "Three boundaries." The old "information mismatch" is split into Boundary 2 (driver/app, → uprobe) and Boundary 3 (host/device, → device-side BPF). Device-side is no longer hidden inside a generic "information" bucket — it has its own boundary with its own mechanism and its own evidence slot in the capability progression.

### P5: gpu\_ext — composable BPF policy pipelines across three boundaries

We present gpu\_ext, a cross-layer policy runtime that makes GPU resource management extensible by providing composable BPF mechanisms that cross each boundary:

**Crossing Boundary 1 (sync/async):** gpu\_ext provides \textbf{struct\_ops hooks} in the GPU driver for fast advisory decisions (eviction ordering, prefetch hints). For operations that outlive the fault handler, hooks dispatch \textbf{BPF work queues} that invoke \textbf{sleepable kfuncs} — \texttt{bpf\_gpu\_migrate\_range} for cross-boundary page migration and \texttt{bpf\_nv\_gpu\_preempt\_tsg} for cross-process GPU preemption. The policy author controls not just what to migrate, but when and how.

**Crossing Boundary 2 (driver/app):** gpu\_ext attaches \textbf{sleepable uprobes} to unmodified application APIs (e.g., \texttt{cuLaunchKernel}, \texttt{cudaStreamSynchronize}) that invoke GPU-controlling kfuncs directly. When a latency-critical process calls \texttt{cuLaunchKernel}, a uprobe fires in kernel context and preempts all best-effort GPU contexts \emph{before the kernel reaches the GPU}. While uprobes are a standard BPF mechanism, prior eBPF extensibility systems attach policies to kernel events and do not use uprobes to trigger kernel-privilege hardware operations on a device.

**Crossing Boundary 3 (host/device):** gpu\_ext extends eBPF to the GPU device with a \textbf{SIMT-aware verifier} that enforces warp-uniform control flow and bounded resource usage, and a \textbf{warp-level execution model} that runs policy logic once per warp. Device-side handlers observe per-warp memory access patterns and SM utilization, writing observations into \textbf{cross-layer BPF maps} with snapshot-based consistency. Host-side policy hooks read these maps to select prefetch strategies and scheduling parameters — closing the observation-action loop across the host-device boundary.

These three mechanisms — async kfuncs, proactive uprobes, and device-side BPF with cross-layer maps — compose through shared BPF maps into policies that span the full GPU stack. No single mechanism suffices; each crosses a boundary that the others cannot.

> **[vs intro_draft.md P5]** Device-side is no longer a one-sentence afterthought in P7 ("additionally extends eBPF to GPU device"). It is the third mechanism in P5, with its own paragraph, motivated by Boundary 3. The SIMT verifier appears here as the safety prerequisite for crossing this boundary, not as an independent challenge.

### P6: Technical challenges

Designing gpu\_ext requires solving four challenges:

\textbf{C1 (Sync/async boundary):} The fault handler runs in microseconds but cross-block prefetch and GPU preemption require milliseconds. We design an explicit sync/async split: struct\_ops hooks make fast synchronous decisions, then dispatch BPF work queues that call sleepable kfuncs for cross-boundary migration. The BPF type system enforces this decomposition at load time (\S X).

\textbf{C2 (Driver/application boundary):} Driver hooks fire after faults occur, but optimal preemption and prefetch should fire before. We attach sleepable uprobes to application APIs that relay intent (kernel launch, phase transitions) and directly invoke GPU-controlling kfuncs. Multi-tenant preemption requires kernel-privilege authority over other processes' GPU contexts — we expose \texttt{bpf\_nv\_gpu\_preempt\_tsg} as a kfunc invokable from proactive uprobes (\S Y).

\textbf{C3 (Host/device boundary):} Host-side policy cannot observe device-internal execution patterns, and device-side observation cannot control host resources. We bridge this gap with cross-layer BPF maps using snapshot-based consistency: device-side handlers aggregate per-warp observations and periodically flush to host-visible map shards; host-side hooks read aggregated state for policy decisions (\S Z).

\textbf{C4 (SIMT safety):} Extending BPF to GPU devices introduces semantic conflicts with the SIMT execution model — scalar eBPF logic per thread causes warp divergence, serialization, and deadlock. We introduce a SIMT-aware verifier that distinguishes warp-uniform from lane-varying values, enforces uniform control flow at branch points, and bounds per-hook resource usage. A warp-level execution model reduces instrumentation overhead by 60--80\% compared to naive per-thread injection (\S W).

> **[vs intro_draft.md P6]** Draft 1 had C1=timescale, C2=reactive/proactive, C3=cross-process authority. Draft 2 has C1=sync/async, C2=driver/app, C3=host/device, C4=SIMT safety.
>
> Changes: (1) C3 (cross-process) is folded into C2 — it is a specific instance of the driver/app boundary (preemption requires kernel privilege triggered by app event). (2) Host/device boundary becomes C3 — a first-class challenge, not absent. (3) SIMT safety becomes C4 — the enabling technology for C3, explicitly present rather than dropped.

### P7: Implementation and evaluation highlights

We implement gpu\_ext on Linux 6.15 by extending the NVIDIA open GPU kernel modules ($\sim$100 LOC of driver hooks), providing a device-side eBPF runtime with SIMT-aware verification ($\sim$1 KLOC kernel module, $\sim$10 KLOC userspace JIT), and a cross-layer map infrastructure. We discuss how the interface design maps to vendor-neutral Linux abstractions (HMM, DRM scheduler) in \S\ref{sec:portability}.

We evaluate across LLM inference (llama.cpp, vLLM), GNN training (PyTorch), vector search (Faiss), and multi-tenant scenarios on RTX 5090. We validate the three-boundary architecture through a capability progression (Table~\ref{tab:cap-progression}): advisory hooks alone achieve 2.60$\times$ on GNN; crossing the sync/async boundary achieves 3.29$\times$ (+27\%); crossing the driver/app boundary reduces multi-tenant P99 where advisory hooks have no effect; crossing the host/device boundary enables device-assisted policy selection on Faiss. Each boundary crossing adds measurable value; removing any one degrades the result. The hook mechanism itself adds less than 0.2\% overhead; device-side instrumentation incurs 3--14\% (vs 85--87\% for NVBit).

> **[vs intro_draft.md P7]** Draft 1 mentioned device-side in one clause ("additionally extends eBPF to GPU device with a SIMT-aware verifier for low-overhead kernel instrumentation"). Draft 2 integrates device-side into the capability progression ("crossing the host/device boundary enables device-assisted policy selection on Faiss") and reports device-side overhead as a first-class result. No "additionally."

### P8: Contributions

\begin{itemize}
  \item We identify \textbf{three execution boundaries} in GPU resource management — synchronous/asynchronous, driver/application, host/device — that are absent from prior eBPF extensibility targets and that break the synchronous, single-layer callback model.

  \item We design \textbf{gpu\_ext}, a cross-layer eBPF policy runtime that provides a composable mechanism for each boundary: sleepable kfuncs with BPF work queues for async operations, proactive uprobes on application APIs for intent-driven policies, and device-side BPF with SIMT-aware verification and cross-layer maps for observation-action loops spanning host and GPU.

  \item We validate through a \textbf{capability progression} that each boundary crossing is necessary: the async path adds 27\% beyond advisory hooks, proactive preemption reduces tail latency where advisory hooks have no effect, and device-assisted observation enables workload-adaptive policy selection. Across four workloads, gpu\_ext improves throughput by up to 4.8$\times$ and reduces tail latency by up to 2$\times$ without application modifications.
\end{itemize}

> **[vs intro_draft.md P8]** Draft 1 contributions: (1) timescale mismatch insight, (2) sync/async design, (3) validation. Draft 2 contributions: (1) three-boundary insight, (2) cross-layer composable design, (3) capability progression with all three boundaries validated. Device-side is in every contribution bullet, not absent.

---

## Summary: What changed from intro_draft.md

| Paragraph | intro_draft.md | intro_draft2.md | Why |
|---|---|---|---|
| P1 | Same | Same | — |
| P2 | Same | Same (scoped "under oversubscription") | — |
| P3 | Long "richer callback" argument | Simplified: lead with empirical evidence | Less vulnerable to counter-arguments |
| **P4** | **Two root causes** | **Three boundaries** | Device-side gets its own boundary instead of hiding in "information mismatch" |
| **P5** | **Two mechanisms (sync/async + uprobe). Device = afterthought** | **Three mechanisms. Device is here with own paragraph** | No "additionally" |
| **P6** | **C1-C3 (no device challenge, no SIMT)** | **C1-C4 (host/device = C3, SIMT safety = C4)** | Device-side has a first-class challenge |
| **P7** | **Device in 1 clause** | **Device integrated into capability progression** | Consistent with three-boundary framing |
| **P8** | **Contributions don't mention device** | **Every contribution bullet includes device** | Unified story |

## Risks of this framing

1. **Three boundaries might feel like a LIST.** Mitigation: frame as "a single progression from no extensibility to full cross-layer policy" via the capability table. Three boundaries → four capability layers → one table.

2. **L4 (host/device boundary) evidence is thin.** Needs the FAISS device-assisted phase detection experiment. Without it, Boundary 3 is architecturally motivated but empirically weaker than 1 and 2. Mitigation: even without L4, the 3-14% vs 85-87% overhead result and the 1.77x microbenchmark are real results for device-side value.

3. **C4 (SIMT safety) might feel bolted on.** Mitigation: frame C4 as "the prerequisite for C3" — you can't cross the host/device boundary without solving SIMT safety first. C4 is not independent; it enables C3.

4. **Paper is more ambitious.** Draft 1 could deliver with host-side only. Draft 2 commits to demonstrating value at all three boundaries. Higher ceiling, higher risk.
