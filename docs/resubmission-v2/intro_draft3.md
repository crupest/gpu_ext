# Intro Draft 3 — Agent Motivation + Three Boundaries
## Evolution

- **Draft 1**: Two root causes (timescale mismatch + information mismatch). Device-side = "additionally." No agent.
- **Draft 2**: Three boundaries (sync/async + driver/app + host/device). Device-side integral. No agent.
- **Draft 3**: Agent as motivation (WHY). Three boundaries as insight (WHAT). Device-side integral. Agent case study in eval.

## Logic chain

```
P1: Agents need safe, dynamic GPU policy extensibility (WHY)
P2: Policy controls the dominant cost, but is locked in the driver (OBSERVATION)
P3: Existing approaches don't solve this (CRITIQUE)
P4: Three execution boundaries explain why naive eBPF is insufficient (INSIGHT)
P5: gpu_ext — composable BPF across three boundaries (DESIGN)
P6: Four technical challenges (CHALLENGES)
P7: Implementation + capability progression results (RESULTS)
P8: Contributions (CLAIMS)
```

---

## Intro Text

### P1: Agents need safe GPU policy extensibility

AI coding agents and automated optimization systems are increasingly used to explore and tune systems-level policies, including GPU resource management — memory placement, page migration, eviction ordering, and compute scheduling. An agent might iteratively test dozens of prefetch strategies, eviction algorithms, and scheduling configurations before converging on the right policy for a given workload and hardware combination. This exploration is inherently high-failure: across 59+ GPU policy configurations explored by an AI agent during our development, 75\% produced neutral or negative results before the agent converged on effective policies for each workload.

For such exploration to be practical, the GPU stack must provide a \textbf{safe, dynamically extensible policy interface}: policies must be loadable, testable, and replaceable in seconds without risking kernel crashes; failed policies must be automatically contained rather than corrupting system state. Yet today, GPU resource management policies are hardcoded inside monolithic, vendor-specific kernel drivers. Implementing a new eviction algorithm or prefetch strategy requires modifying driver source, rebuilding the kernel module, and reloading the entire driver stack — a process too slow and too fragile for iterative exploration, whether the explorer is an AI agent, an automated tuning system, or a human researcher.

> **[vs draft 1/2 P1]** Draft 1/2 opened with "GPU performance depends on policies" — generic, no urgency. Draft 3 opens with agents exploring GPU policies — specific, timely, backed by our own data. The design requirements (safe, dynamic, fast iteration) emerge naturally from the agent use case rather than being asserted abstractly.

### P2: Policy controls the dominant cost, but is locked in the driver

Under memory oversubscription — increasingly common as model sizes outpace GPU memory — the choice of eviction and prefetch policy determines the volume of PCIe data migration, which dominates execution time. We profiled a 120B MoE inference workload at 1.84$\times$ oversubscription and found that PCIe DMA accounts for 73\% of per-token decode time; the policy governs \emph{which} pages migrate and \emph{when}, directly controlling this dominant cost.

By replacing the driver's default LRU eviction and tree-based prefetching with a workload-adaptive stride prefetch and LFU eviction — a policy discovered through the iterative exploration described above — we improve decode throughput by 1.3$\times$ over manually tuned UVM hints (and 4.8$\times$ over framework-managed CPU offloading). The 1.3$\times$ gap arises specifically from cross-VA-block prefetch that the driver's built-in mechanisms cannot express.

This quantifies the tension: policy has high impact, but zero programmability.

> **[vs draft 1/2 P2]** Nearly identical, with one addition: "a policy discovered through the iterative exploration described above" — connects the surprising observation back to P1's agent motivation. The 73% number, dual baselines, and scoping ("under memory oversubscription") are preserved from draft 1/2.

### P3: Existing approaches are insufficient

Existing approaches leave this tension unresolved.

\textbf{User-space frameworks} (Paella, XSched, Pie, KTransformers) offer programmability but cannot control driver-internal page fault handling and eviction ordering, and cannot preempt another process's GPU context. These approaches require application-level modifications and are confined to framework-specific APIs.

\textbf{Driver-level modifications} (GPREEMPT, Forest, GCAPS) provide hardware-level control but embed a single fixed policy per driver build. They cannot adapt at runtime, cannot compose memory and scheduling policies, and break on driver updates — making them incompatible with iterative exploration.

\textbf{GPU profiling frameworks} (NVBit, Neutrino, CUPTI), including our prior workshop version, enable kernel-level observation but lack runtime policy enforcement and driver integration.

The natural candidate is eBPF: recent CPU-side extensibility frameworks — sched\_ext for scheduling, cache\_ext for the page cache — demonstrate that struct\_ops hooks provide safe, dynamic, runtime-updatable policy programmability. Applying this pattern to the GPU driver would satisfy the safety and dynamism requirements. We do this as our starting point.

> **[vs draft 1/2/earlier-draft-3 P3]** Struct_ops is NO LONGER criticized as previous work. It is our own starting point — "we do this." The critique of struct_ops's *insufficiency* moves to P4, where it becomes the motivation for the three boundaries. P3 now cleanly separates: other people's work (user-space, driver-mods, profiling) vs our starting point (struct_ops).

### P4: Struct_ops is our starting point — but three boundaries limit it

However, applying struct\_ops to the GPU driver — while providing the safety and dynamism that iterative exploration demands — leaves significant performance unrealized. On GNN training, advisory-only hooks achieve 2.60$\times$; adding asynchronous cross-block migration achieves 3.29$\times$ (+27\%). On multi-tenant scheduling, advisory hooks have no measurable effect on tail latency, while proactive application-triggered preemption reduces P99 substantially. The improvement in both cases comes from mechanisms that synchronous, driver-internal callbacks cannot express (Table~\ref{tab:cap-progression}).

Why does the struct\_ops model — sufficient for CPU scheduling (sched\_ext) and page caching (cache\_ext) — leave this gap? Because GPU resource management requires policy to cross three execution boundaries that do not arise in the same combination in any prior eBPF extensibility target:

\textbf{Boundary 1: Synchronous / Asynchronous.}
The policy decision must return in microseconds (fault handler context), but the most impactful operation — migrating data across PCIe beyond the current VA block — takes milliseconds. This 1000$\times$ timescale gap means a synchronous callback cannot initiate cross-block prefetch and wait for completion. CPU scheduling decisions and effects both complete in nanoseconds; page cache operations in microseconds. GPU policy requires a \textbf{sync/async split}: synchronous hooks for fast local decisions, composed with asynchronous execution for slow cross-boundary operations — a decomposition that the BPF type system enforces at load time (non-sleepable vs.\ sleepable context).

\textbf{Boundary 2: Driver / Application.}
The GPU driver fires hooks on page faults and eviction events — it reacts to problems already occurring. But optimal policy timing is often \emph{proactive}: preempt best-effort GPU contexts when a latency-critical process calls \texttt{cuLaunchKernel}, \emph{before} the kernel reaches the GPU; prefetch at epoch boundaries signaled by \texttt{cudaStreamSynchronize}, \emph{before} the next batch of faults. The driver does not see these application API calls. CPU extensibility systems (sched\_ext, cache\_ext, XRP) attach policies to kernel-internal events; GPU policy additionally requires \textbf{proactive hooks at application boundaries} that invoke GPU-controlling kernel functions directly.

\textbf{Boundary 3: Host / Device.}
The host driver controls memory placement and compute scheduling but cannot observe \emph{how} the GPU executes: per-warp memory access patterns (sequential vs.\ random), SM load distribution, and computation phase boundaries are visible only inside the GPU. Conversely, the device observes execution but cannot control host-level resource allocation. A policy exploring prefetch strategies cannot determine whether the workload is scanning sequentially or probing randomly without device-side visibility. Effective policies require a \textbf{closed observation-action loop}: device-side instrumentation classifies workload behavior and writes to shared state; host-side policy reads this state and selects strategy accordingly.

> **[vs earlier draft 3 P4]** Key changes: (1) Struct_ops insufficiency evidence (27% gap, P99 gap) moved here from P3 — it is OUR baseline analysis, not a critique of others' work. P3 ends with "we do this as our starting point"; P4 begins with "but it is not enough." Clean handoff. (2) "Absent from CPU" softened to "do not arise in the same combination" — honest about CPU having weak forms of individual boundaries. (3) "An agent exploring" → "A policy exploring" — P4 is purely technical; P1 already established the agent context.

### P5: gpu\_ext — composable BPF across three boundaries

We present gpu\_ext, a cross-layer eBPF policy runtime that makes GPU resource management safely extensible by providing composable mechanisms that cross each boundary:

\textbf{Crossing Boundary 1 (sync/async):} gpu\_ext provides \textbf{struct\_ops hooks} in the GPU driver for fast advisory decisions. For operations that outlive the fault handler, hooks dispatch \textbf{BPF work queues} that invoke \textbf{sleepable kfuncs} — \texttt{bpf\_gpu\_migrate\_range} for cross-boundary page migration and \texttt{bpf\_nv\_gpu\_preempt\_tsg} for cross-process GPU preemption.

\textbf{Crossing Boundary 2 (driver/app):} gpu\_ext attaches \textbf{sleepable uprobes} to unmodified application APIs (\texttt{cuLaunchKernel}, \texttt{cudaStreamSynchronize}) that invoke GPU-controlling kfuncs directly. A uprobe on \texttt{cuLaunchKernel} preempts all best-effort GPU contexts \emph{before} the latency-critical kernel reaches the GPU — acting on application intent rather than reacting to driver events.

\textbf{Crossing Boundary 3 (host/device):} gpu\_ext extends eBPF to the GPU device with a \textbf{SIMT-aware verifier} and \textbf{warp-level execution model}. Device-side handlers observe per-warp memory access patterns and write classifications into \textbf{cross-layer BPF maps}; host-side hooks read these maps to select prefetch strategies — closing the observation-action loop.

These mechanisms compose through shared BPF maps into policies spanning the full GPU stack. No single mechanism suffices; each crosses a boundary the others cannot. BPF's containment model — load-time verification, runtime fault isolation, clean policy detachment — ensures that every failed attempt during iterative exploration is recoverable, with zero kernel panics across 50 documented safety events during development (\S\ref{sec:containment}).

> **[vs draft 2 P5]** Final sentence added: connects back to P1's safety requirement and previews the containment evidence from §6.N. The system description naturally motivates containment as a design goal, not an afterthought.

### P6: Technical challenges

Designing gpu\_ext requires solving four challenges:

\textbf{C1 (Sync/async boundary):} The fault handler runs in microseconds but cross-block prefetch and GPU preemption take milliseconds. We design a sync/async split: struct\_ops hooks make fast decisions, then dispatch BPF work queues that call sleepable kfuncs. The BPF type system enforces this decomposition at load time (\S X).

\textbf{C2 (Driver/application boundary):} Driver hooks fire after faults, but optimal policy fires before. We attach sleepable uprobes to application APIs that invoke GPU-controlling kfuncs directly. Cross-process preemption requires kernel-privilege authority — we expose \texttt{bpf\_nv\_gpu\_preempt\_tsg} invokable from uprobes, with automatic target discovery via kprobe (\S Y).

\textbf{C3 (Host/device boundary):} Host policy cannot observe device execution patterns; device observation cannot control host resources. We bridge this with cross-layer BPF maps using snapshot-based consistency: device handlers aggregate per-warp observations and flush to host-visible shards at synchronization boundaries (\S Z).

\textbf{C4 (SIMT safety):} Extending BPF to GPU devices conflicts with the SIMT execution model — scalar per-thread logic causes warp divergence and deadlocks. We introduce a SIMT-aware verifier distinguishing warp-uniform from lane-varying values, and a warp-level execution model reducing overhead by 60--80\% vs.\ naive injection (\S W).

> **[vs draft 2 P6]** Identical. C1-C3 map to three boundaries, C4 enables C3.

### P7: Results

We implement gpu\_ext on Linux 6.15 by extending the NVIDIA open GPU kernel modules ($\sim$100 LOC driver hooks), providing a device-side eBPF runtime with SIMT-aware verification, and cross-layer map infrastructure. We evaluate across LLM inference (llama.cpp, vLLM), GNN training (PyTorch), vector search (Faiss), and multi-tenant scenarios on RTX 5090.

We validate the three-boundary architecture through a capability progression (Table~\ref{tab:cap-progression}):

\begin{itemize}[itemsep=0pt]
  \item Advisory hooks alone (within sync boundary): GNN 2.60$\times$.
  \item + async cross-block migration (crossing sync/async): GNN 3.29$\times$ (+27\%).
  \item + proactive uprobe preemption (crossing driver/app): multi-tenant P99 reduction where advisory hooks have zero effect.
  \item + device-assisted policy selection (crossing host/device): workload-adaptive prefetch on Faiss.
\end{itemize}

Each boundary crossing adds measurable value; removing any one degrades the result. The hook mechanism adds less than 0.2\% overhead; device-side instrumentation incurs 3--14\% (vs.\ 85--87\% for NVBit). Across 50 documented safety events during AI-assisted policy exploration, all failures were recoverable with zero kernel panics or data corruption.

> **[vs draft 2 P7]** Added the containment sentence at the end — ties back to P1's motivation (safe exploration) and previews §6.N. Capability progression now explicitly lists all four boundary crossings, including device-side.

### P8: Contributions

\begin{itemize}
  \item We identify \textbf{three execution boundaries} in GPU resource management — synchronous/asynchronous, driver/application, host/device — that are absent from prior eBPF extensibility targets and that break the synchronous, single-layer callback model sufficient for CPU subsystems.

  \item We design \textbf{gpu\_ext}, a cross-layer eBPF policy runtime providing a composable mechanism for each boundary: sleepable kfuncs with BPF work queues for async operations, proactive uprobes for intent-driven policies, and device-side BPF with SIMT-aware verification and cross-layer maps for observation-action loops spanning host and GPU.

  \item We validate through a \textbf{capability progression} that each boundary crossing is necessary, and demonstrate that BPF's containment model enables safe iterative policy exploration — across 50 safety events during AI-assisted development, all failures were recoverable with zero kernel panics.
\end{itemize}

> **[vs draft 2 P8]** Third bullet adds containment/agent evidence. Links back to P1: we motivated with agent safety, and we deliver agent safety evidence.

---

## Three-draft comparison

| Aspect | Draft 1 | Draft 2 | **Draft 3** |
|---|---|---|---|
| **P1 opening** | "GPU performance depends on policies" | Same | **"AI agents are exploring GPU policies"** |
| **Core insight** | Two mismatches | Three boundaries | **Three boundaries** (same as draft 2) |
| **Device-side** | "Additionally" in P7 | Integral (Boundary 3) | **Integral (Boundary 3)** |
| **Agent** | Absent | Absent | **Motivation (P1) + evidence (P7, P8, §6.N)** |
| **"Why eBPF?"** | Implicit | Implicit | **Explicit: containment enables safe exploration** |
| **Design requirements** | Asserted ("safe, flexible") | Asserted | **Derived from agent use case** |
| **Containment evidence** | None | None | **50 events, 0 panics — in P5, P7, P8** |
| **SOSP 2026 appeal** | Medium | Medium-High | **High** (agent + systems intersection) |

## Narrative arc of the full paper

| Section | What it does | Agent connection |
|---|---|---|
| **§1 P1** | Agents need safe GPU policy extensibility | Agent is the motivating use case |
| **§1 P2** | 73% observation — policy is the bottleneck | "Policy discovered through iterative exploration" |
| **§1 P3** | Existing approaches insufficient; struct_ops is our starting point | Driver mods "incompatible with iterative exploration"; struct_ops = our own baseline |
| **§1 P4** | Struct_ops leaves 27% gap → three boundaries explain why | Pure technical — no agent reference needed here |
| **§1 P5** | gpu_ext design overview | "BPF containment ensures failed attempts are recoverable" |
| **§2** | Background + capability progression table | Standalone — no agent needed |
| **§3** | Design (by capability, not by layer) | Standalone — pure systems |
| **§4** | Implementation | Standalone |
| **§5** | Eval: capability progression + case studies | L1→L4 validated on real workloads |
| **§5.N** | **Eval: Containment & iterative exploration** | **Safety events table + 1 case study + velocity** |
| **§6** | Discussion | Generalizability: CXL/DPU/NPU have same three boundaries; containment extends to other automated optimization |
| **§7** | Conclusion | "Three boundaries + containment = extensible heterogeneous subsystems" |

## Risks

1. **"You motivated with agents but this is a systems paper"** — Mitigated: P1 makes clear that agents are the USE CASE, not the contribution. Technical contribution (three boundaries, composable BPF, capability progression) is pure systems. §6.N is 0.5 pages of evidence, not a full agent benchmark.

2. **"Is the agent evidence rigorous enough?"** — We have 42 sessions, 59+ configs, 50 safety events, 3 convergence arcs. This is more evidence than most systems papers provide for their methodology. Frame as "empirical observation from development" not "controlled agent experiment."

3. **"Would this paper exist without agents?"** — Yes. Three boundaries and composable BPF are the contribution regardless of who explores the policy space. Agents make the motivation more compelling and the containment argument more concrete, but the system stands on its own.

4. **"Agent motivation is trendy but shallow"** — Mitigated: our P1 is backed by REAL DATA (59+ configs, 75% fail, 50 safety events), not a hypothetical future. We actually built and used this system with an AI agent. The evidence is first-party.
