# GPU_ext Intro: Framings, Analysis & Plan

## Background (the big picture)

1. GPUs are a primary compute platform for AI. Models increasingly exceed VRAM, making OS-level resource management (migration, scheduling, sharing) critical.
2. GPU hardware provides resource management mechanisms (MMU, TLB, fault queues, timeslice scheduler) but delegates resource management *policy* to software — no hardware page replacement policy, no hardware scheduling policy. Policy lives in the kernel driver and across the software stack.
3. Linux has made CPU scheduling (sched_ext), networking (XDP/TC), and storage (XRP) extensible via eBPF — but GPU has no extensibility story.
4. NVIDIA open-sourced its kernel-mode GPU driver modules (including UVM) in 2022, making driver-level extensibility feasible for the first time.
5. AI agents are evolving from configuration tuners to code writers — SchedCP generates BPF scheduling programs (not configs) via sched_ext, AlphaEvolve writes TPU layout code, ASAP tunes LLM training configs. As agents become capable of writing policy code, they need a **programmable** interface (not just fixed knobs) with safety guarantees (verified, sandboxed). [See `agent_systems_survey.md`, `agent_gpu_citations.md`.]

## Key Observations (specific facts that motivate the work)

1. GPU resource management policies have large, workload-dependent performance impact — external measurements show 3.5x speedup from prefetch policy (HELM), 43.7% stall reduction from scheduling policy (MOST), 93% speedup from memory placement (Ganguly). No single policy works across workloads. [See `gpu_policy_impact_evidence.md`.]
2. The GPU driver is the best-positioned coordination point for policy — on bare-metal, the primary software-accessible layer with global visibility across processes, hardware privilege over page tables and scheduling, and fault path access. Policy is scattered across the stack, but no other layer can coordinate across all of them.

## Problem statement (the gap)

No general-purpose, programmable OS extensibility interface exists for GPU resource management policy — a gap that grows more critical as AI agents evolve from configuration tuners to policy code writers. Existing interfaces (cudaMemAdvise, MPS priority, MIG partitioning) expose fixed knobs: agents can search over them, but cannot express new policies. User-space frameworks offer programmability but lack kernel-level visibility and privilege. Kernel driver modifications provide both, but are fragile, vendor-specific, and unsafe for agent-generated code. The result: agents can write arbitrarily sophisticated policy logic, but have nowhere safe to run it at the driver level where GPU policy lives.

## Key Insights — Mismatch Versions

### Version 1: Three mismatches (original, timescale/information/visibility)

1. The policy executor (host CPU in the driver) and the managed resource (device memory, GPU execution) are on opposite sides of a high-latency, low-bandwidth interconnect (PCIe) with no shared observability. This breaks the co-location assumption — policy, information, and effect in one execution context — that underlies CPU extensibility frameworks (sched_ext, cache_ext). Unlike NUMA (same processor, shared address space, ~2-3x latency), GPU separation is qualitative: different processor, different address space, requiring explicit DMA to effect any policy decision.
2. Because physical separation places the policy executor and the managed resource in different timescales, different information contexts, and different observability domains, a synchronous, driver-local callback cannot cross any of the resulting mismatches: it returns before DMA completes (timescale), the driver lacks upcall paths to application intent (information), and the host has no runtime visibility into GPU SMs (visibility). The interface, not the search, is the bottleneck. The co-location mismatch is real but its exploitability is bounded by PCIe bandwidth — at high oversubscription, crossing more boundaries yields no gain because the interconnect is saturated (see F3).

[Review: `reviews/opus_sharp_questions.md` Q7/Q8 — "why exactly three? taxonomy not insight? visibility ⊂ information?"]

### Version 2: Three mismatches revised (timescale/information-bidirectional/structural)

Previous version had: timescale, information, visibility. Two problems identified:
- Visibility (host can't observe device) is really the downward direction of information asymmetry, not a separate mismatch
- Memory-scheduling coupling on GPU is a real mismatch not captured

All three derive from one root cause: **physical separation across a high-latency interconnect.**

**Revised three mismatches:**

1. **Timescale mismatch** (temporal): policy decision must return in μs (fault-handler context), but the most impactful effects (cross-block DMA migration, GPU preemption) take ms. A synchronous callback cannot initiate these and wait. Root cause: effects must cross the interconnect.
   → Mechanism: async sleepable kfuncs + bpf_wq.

2. **Information mismatch** (spatial, bidirectional): the driver lacks application intent from above (what kernel to launch, when epochs end) AND device execution state from below (per-warp access patterns, SM utilization, computation phases). The two directions cross different boundaries and require distinct mechanisms. Root cause: boundary separates app, driver, and device into isolated information domains.
   → Mechanism (upward): uprobes on application APIs.
   → Mechanism (downward): device-side BPF instrumentation.

3. **Structural mismatch** (cross-subsystem): on CPU, scheduling and memory management are largely independent subsystems with separate extensibility interfaces (sched_ext vs cache_ext); the hardware page walker transparently handles TLB misses without software coordination. On GPU, memory must be physically moved across the interconnect (DMA), not just mapped. Every page fault involves both a memory migration decision and a scheduling stall. Eviction policy needs scheduling priority; scheduling policy needs memory pressure. The per-subsystem extensibility model that works for CPU breaks here. Root cause: if memory didn't need to cross the boundary, scheduling and memory could be largely independent as on CPU.
   → Mechanism: hooks in the GPU fault handler, which is inherently the intersection of memory and scheduling. The same BPF program at the eviction/fault hook receives both memory state (page location, fault counts) and scheduling state (process priority, context activity), enabling cross-cutting policies that separate per-subsystem hooks cannot express.

**Why this is better than the old three:**
- All three derive from physical separation (causal unity preserved):
  - Timescale: effects cross the interconnect (temporal)
  - Information: boundary creates isolated information domains (spatial)
  - Structural: memory must physically move, coupling scheduling and memory (structural)
- Visibility is no longer inflated into a separate mismatch; it's the downward direction of information, with device-side BPF as its dedicated mechanism
- Structural mismatch is a genuinely GPU-specific insight: the per-subsystem extensibility model that works for CPU (sched_ext independent of cache_ext) breaks for GPU. The insight is not that memory and scheduling are coupled (obvious), but that this coupling invalidates the extensibility design pattern
- Q7 ("why exactly three?") answered: timescale = temporal, information = spatial, structural = cross-subsystem. Three orthogonal dimensions of how physical separation affects the extensibility interface

**Addressing opus reviewer concerns from previous round:**
- "(a) loses device-side BPF motivation": FIXED — device-side BPF is explicitly the downward mechanism under information mismatch, with its own arrow (→). It remains first-class.
- "(b) unified interface is not a concrete mechanism": FIXED — mechanism is now "hooks in the GPU fault handler" (a specific placement), not "shared maps" (generic BPF feature). The fault handler is inherently at the memory-scheduling intersection; this is not something you get by just sharing BPF maps across arbitrary hooks.
- "(c) resource coupling is obvious": PARTIALLY FIXED — reframed as "the per-subsystem extensibility model breaks" (non-obvious to eBPF/Linux community), not "memory and scheduling are coupled" (obvious). Softened CPU claim to "largely independent" with qualifier about OOM/NUMA edge cases.
- "(d) CPU sched and memory not fully independent": FIXED — "largely independent" + "on CPU, the coupling is at the edges (OOM kills, NUMA balancing); on GPU, it is fundamental: every page fault involves both a memory migration decision and a scheduling stall."

**Remaining risk:** Does "structural mismatch" carry the same rhetorical weight as the other two? Timescale and information are sharp, memorable names. "Structural" is more abstract. Alternative names considered: resource coupling mismatch, independence mismatch, subsystem entanglement. "Structural" chosen because it describes what breaks (the structure of the extensibility interface), not just what exists (coupling).

[Review: `reviews/opus_review_mismatches_v2.md` — structural is corollary of timescale (not independent), upward information is software layering (not physical separation), honest count is two]

### Version 3: Two mismatches + design implications (timescale/information)

Physical separation has two layers:
- **Interconnect latency** (PCIe/NVLink): high-latency data movement. This is shrinking (PCIe → NVLink → CXL) but nonzero for foreseeable future.
- **Architectural separation** (independent processor): different ISA (SIMT vs scalar), different execution model (warps vs threads), independent fault handling. This is fundamental and will not disappear even with coherent shared memory (Grace Hopper, AMD MI300X, future CXL GPUs).

**Two mismatches (each with clear scope):**

1. **Timescale mismatch** (from interconnect latency): the sync callback model from CPU extensibility works at the fault handler level — BPF programs can make advisory policy decisions (eviction ordering, prefetch scope) synchronously at fault time (L1 baseline). But two temporal limitations remain:
   - **Too short**: the most impactful effects (cross-block DMA migration, GPU preemption) take ms because data must physically cross the interconnect. The sync callback returns in μs and cannot wait.
   - **Too late**: the fault handler is reactive (fires after faults). Optimal GPU policy is often proactive: preempt before a latency-critical kernel launch, prefetch before an epoch boundary. The fault handler fires too late for these.

   Because DMA physically moves data across the boundary, every page fault involves both a memory migration decision and a scheduling stall. Memory and scheduling are coupled through the fault handler — unlike CPU where sched_ext and cache_ext operate on largely independent subsystems. This is why hooks must be placed in the fault handler (the memory-scheduling nexus), and why all three mechanism layers (L1/L2/L3) operate at or around this intersection.

   → Mechanism (L1, baseline): sync advisory hooks in fault handler — change eviction/prefetch policy at fault time.
   → Mechanism (L2, "too short"): async sleepable kfuncs + bpf_wq — initiate DMA/preemption that outlives the callback.
   → Mechanism (L3, "too late"): uprobes on CUDA APIs — fire before events, enabling proactive policies.

2. **Information mismatch** (from architectural separation): the GPU is an independent processor with its own ISA and execution model. The host cannot observe device execution state (per-warp access patterns, SM utilization, computation phases) without device-side instrumentation. This is not a latency problem — even with zero-latency interconnect, the GPU's internal state is architecturally opaque because the ISA difference makes state uninterpretable without device-side code. This mismatch persists as long as GPUs remain architecturally distinct processors.
   → Mechanism: device-side BPF instrumentation + SIMT-aware verifier.

**What happened to V1's visibility and V2's structural:**
- Visibility (V1) → folded into information mismatch. It IS the information mismatch (downward direction). Device-side BPF is its mechanism.
- Structural/resource coupling (V2) → folded into timescale mismatch as design implication. Not independent because it disappears if DMA were instantaneous. Fault handler hook placement is the concrete result.
- Upward information / app intent (V1, V2) → not GPU-specific (sched_ext has same issue). Not listed as a mismatch. Uprobes are motivated by timescale mismatch ("too late"), not information mismatch.

**Why two is more honest than three:**
- Each mismatch has a distinct root cause: timescale from interconnect latency, information from architectural separation. Genuinely independent (one can exist without the other).
- No taxonomy padding: V1 padded visibility ⊂ information, V2 padded structural ⊂ timescale.
- Upward information (app intent) honestly acknowledged as generic kernel problem, not inflated into GPU-specific mismatch.
- Memory-scheduling coupling preserved as design implication, not lost.
- Two mismatches motivate three mechanism layers (L1/L2/L3) + device-side BPF, without forcing artificial 1:1 mapping.

**Risk:** Two mismatches may feel "less" than three. Counter: SOSP values depth over breadth. Two well-derived mismatches from clear root causes are stronger than three with questionable independence.

[Review: `reviews/opus_review_mismatches_v3.md` — V3 is borderline accept, fix upward info attribution + promote fault-handler nexus]

## Design Constraints (these shape the implementation)

1. SIMT execution on GPU requires distinguishing warp-uniform from lane-varying values — a standard eBPF verifier ensures memory safety and termination but cannot prevent warp-divergence deadlocks.
2. Existing CPU extensibility patterns (sched_ext, cache_ext) cannot be directly reused — each mechanism requires GPU-specific adaptation due to the qualitative differences in execution model, address space, and interconnect latency.

## Key Findings (discovered by building and evaluating gpu_ext — these validate, not motivate)

1. On GNN training at 1.34x oversubscription, L1 (advisory hooks) achieves 2.60x; L1+L2 (+ async cross-block) reaches 3.36x. On vLLM multi-tenant serving, L1+L3 (+ proactive preemption) reduces P99 by 9.5%. Device-side BPF provides low-overhead instrumentation but is not yet combined with L1+L2 in an end-to-end policy. At 1.84x oversubscription (llama.cpp), L2/L3 provide negligible benefit due to PCIe saturation. [**Weakness (yunwei37):** No single policy uses all three layers together. The capability progression is demonstrated per-mismatch across different workloads, not as a unified L1+L2+L3 stack on one workload. Need a workload/config where all three layers contribute jointly.]
2. An AI agent generated and deployed 59 BPF policy programs for GPU resource management — eviction ordering, prefetch strategies, scheduling logic — across 4 workloads. This is code generation (BPF programs), not configuration search (parameter tuning), analogous to PolicySmith (BPF CC) and NECC (BPF CCA). The verifier contained all 50 safety-relevant events (invalid memory access, excessive loop bounds) with 0 kernel panics. Hand-written policies matched or exceeded agent-found ones; the agent's value was rapid policy-space coverage with safety guarantees. [**Note:** 0 panics is the expected BPF guarantee; the finding is that this guarantee holds when extended to the GPU driver fault path.]
3. At high oversubscription (1.84x), PCIe bandwidth is the fundamental bottleneck — all prefetch strategies add DMA traffic that competes with demand paging. The system's benefit concentrates at moderate oversubscription (1.1x–1.5x). This bounds the insight: the co-location mismatch is exploitable only when PCIe is not saturated.
4. Baseline mechanism overhead: BPF hooks on the GPU fault path add <2% overhead when a trivial (no-op) policy is loaded.

---

## Per-paragraph options (2026-03-29)

Differences concentrate in P1, P2, P3, P4. P5-P7 are stable across all framings.

### P1 options: What motivates the paper?

**P1-A: Policy importance only** (Draft 1, 2)
- "GPU policies matter, diverse workloads, 73%." No agent.
- ✓ Safe floor option — avoids damage if agent story is weak; any GPU-systems reviewer accepts without pushback.
- ✗ No "why now" — could have been written in 2020; gives reviewer no reason to distinguish this from the 15 UVM/scheduling papers already published (Forest, HELM, SUV, DREAM, GCAPS, GPREEMPT, XSched).
- ✗ Motivates "better policies" but not "extensibility" specifically — every paper in the related work also claims better policies.

**P1-B: Agent as full P1** (Draft 3)
- "AI agents are exploring GPU policies... 59 configs, 75% fail... needs safe extensibility."
- ✓ The only option that naturally motivates ALL THREE design requirements (safety, dynamism, fast iteration) from a single concrete use case rather than asserting them.
- ✓ Provides "why now" — GPU extensibility could only be written in 2026, not 2020, because agents + open-source GPU drivers are both new.
- ✗ 59 configs / 75% fail is a methodology artifact from our own development, not a general observation — a reviewer says "this is anecdotal evidence dressed as empirical data; 75% failure rate is meaningless without knowing the search strategy."
- ✗ Creates framing debt: reviewer will expect rigorous agent evaluation (ablation, comparison to Bayesian optimization, agent architecture), not just "59 configs 0 panics" in the eval. If Section 5 is primarily workload benchmarks with manually-written BPF programs, the reader feels baited.

**P1-C: Policy + agent in one sentence** (Current tex)
- "GPU policies have large impact, and AI agents are actively optimizing such policies across the stack."
- ✓ Compact — establishes both themes without over-committing to either.
- ✓ If agent eval is modest (59 configs, no rigorous ablation), P1-C's lighter touch avoids P1-B's framing debt while still providing "why now." P1-C is the **best option when the paper cannot pay off P1-B's promise.**
- ✗ If agent threads through P2-P6 (as the throughline table suggests), P1-C under-leverages the agent story. If agents disappear after P1, creates tonal mismatch. Outcome depends on how consistently agent appears in subsequent paragraphs.
- ✗ Compared to P1-B, gives up the natural motivation for safety/dynamism/iteration from a single use case — these must be motivated separately.

### P2 options: What's the practical problem? (NOT insight — insight goes in P4)

**P2-A: "Locked in driver"** (Draft 1, 2)
- "Policy controls 73% of time, yet implementing any new policy requires modifying the proprietary driver."
- ✓ Sharp tension between high impact and zero programmability; honest if contribution is primarily engineering.
- ✗ Complaint about vendor practice, not architectural insight — "so convince NVIDIA to open-source the driver" (they already have).
- ✗ Generic "make X extensible" template — doesn't distinguish from every other eBPF paper.

**P2-E: "Driver not extensible"** (Framing 7, 8)
- "The GPU driver is the only component with visibility + privilege + fault path — yet its policies remain hardcoded."
- ✓ Honest, sets correct expectations for systems-building paper (like Bento or XRP).
- ✗ Reviewer can say "NVIDIA open-sourced the modules, contribute upstream" or "this is engineering, not research."
- ✗ Invites "just apply sched_ext" dismissal — P3b/P4 must preempt by showing WHY direct application fails.

**P2-F: "Requirements + tradeoff"**
- "Safe iterative exploration requires deployability, visibility, containment. No existing approach provides all three."
- ✓ Best for organizing a related-work comparison table.
- ✗ Post-hoc requirements — designed so only your system satisfies them.
- ✗ "Requirements list" red flag: "we designed a checklist and checked it off."

**P2-I: "GPU lacks extensibility other Linux subsystems have"**
- "Linux has made CPU scheduling (sched_ext), networking (XDP/TC), storage (XRP) extensible — but GPU has no extensibility story."
- ✓ Positions paper as filling a gap in Linux extensibility program; attractive to eBPF/Linux PC members.
- ✗ "Gap-filling" framing weaker than "architectural insight"; reviewer says "the gap is obvious, what's the research?"

**P2-J: "Gap + agent amplifier"** (NEW, 2026-03-30)
- "No programmable OS extensibility interface exists for GPU policy — gap grows as agents evolve from config tuners to code writers."
- Structure: (1) gap claim + agent amplifier, (2) fixed knobs not enough, (3) user-space not enough, (4) kernel mods not enough, (5) conclusion
- ✓ Agent capability growth is the throughline connecting all "not enough" arguments — answers Q11 (why agents need kernel extensibility, not just knobs)
- ✓ Each existing approach is evaluated against what agents actually need (programmability + safety + kernel-level visibility)
- ✗ Still a gap-filling framing at core — "no interface exists" is a complaint. Insight depth comes from P4, not P2.
- ✗ Ties P2 strongly to agents — if reviewer doesn't buy agent motivation, P2 also weakens

**User critiques on P2 (yunwei37, 2026-03-30):**
- "GPU resource management policy is coordinated in the kernel driver" overclaims — policy also exists in user-space (vLLM PagedAttention, DeepSpeed ZeRO, cudaMemAdvise, MPS, MIG, Paella, XSched). Driver is the coordination point, not the only policy location. Fixed: "policy is scattered across user-space frameworks, CUDA runtime, and the kernel driver, but the driver is the natural coordination point."
- "OS extensibility interface" needs definition on first use — reader doesn't know what this means. Fixed: added appositive "one that allows verified policy code to be safely loaded into the driver at runtime without modifying its source."
- P2 was too long when it included physical separation insight (which belongs in P4) AND detailed existing approaches. Fixed: removed physical separation from P2 (stays in P4 only), compressed existing approaches to 1 sentence each.

**P2 is NOT settled.** Current candidates: P2-E+I (practical gap, no agent), P2-J (gap + agent amplifier). See Problem Statement section above for P2-J draft text. Current tex uses P2-J.

### P3 options: What's the starting point?

**P3-A: Existing approaches → our insight** (Draft 1)
- "User-space, driver mods, advisory eBPF all insufficient. Even richer callbacks would require fixed async engine."
- ✓ "Richer callback" rebuttal anticipates "just return DMA descriptors" objection.
- ✗ Treats struct_ops as previous work — dishonest when eval shows "L1: struct_ops (our system)."

**P3-B: struct_ops = our starting point** (Draft 3, Current tex)
- "eBPF struct_ops proven for CPU. We adopt synchronous advisory hooks as our starting point."
- ✓ Correctly positions struct_ops as OUR foundation; clean "we adopt → but insufficient" handoff.
- ✗ Jumps directly to struct_ops without establishing eBPF as the right tool first.
- ✗ Loses "richer callback" rebuttal.

**P3-C: eBPF first → struct_ops → but insufficient** (NEW, 2026-03-30)
- Thesis: "We propose treating GPU resource management as a programmable OS subsystem, using eBPF to provide the safety and programmability that existing approaches lack."
- P3a: eBPF is the right tool — verified safety, arbitrary programmability, kernel-level, runtime deployable. Linux has proven this for CPU scheduling (sched_ext), networking (XDP/TC), storage (XRP).
- P3b: For GPU, we adopt the struct_ops pattern — synchronous advisory hooks in the driver, as sched_ext does for CPU scheduling. This is our starting point.
- P3c: But synchronous callbacks are insufficient — async path adds +27%, proactive further reduces P99. A richer callback returning DMA descriptors would reconstruct our system under a different name — the issue is not the return type but that policy effects outlive the callback.
- ✓ Logical progression: eBPF (general tool) → struct_ops (specific pattern) → adopt → insufficient. Reader follows each step.
- ✓ Directly answers P2's gap: P2 says "need kernel-level + programmable + safe," P3 says "eBPF is exactly that."
- ✓ Includes "richer callback" rebuttal from P3-A.
- ✗ Three sub-paragraphs may be too much for one P3 section. Consider whether P3a+P3b can be one paragraph.

**P3 candidate主旨句对比:**

| | 主旨句 | 问题 |
|--|--------|------|
| A (current tex) | "GPU resource management requires an OS policy interface providing safe, flexible and transparent programmability" | Checklist 感，"we argue" 弱 |
| B (eBPF first) | "eBPF — verified, programmable, runtime-deployable — is the natural mechanism to close this gap" | 直接回应 P2 gap |
| C (thesis + eBPF) | "We propose treating GPU resource management as a programmable OS subsystem, using eBPF to provide the safety and programmability that existing approaches lack" | Thesis + tool 合一 |
| D (pattern) | "Linux has already made CPU scheduling, networking, and storage extensible via eBPF; we extend this pattern to GPU" | 太像 gap-filling |

**User critiques on P3 (yunwei37, 2026-03-30):**
- Should NOT jump directly to struct_ops — need to first establish eBPF as the right tool. Logic: P2 eliminates knobs/user-space/kernel-mods → need kernel-level + programmable + safe → eBPF is exactly that → then narrow to struct_ops pattern.
- Old P3 主旨句 "we argue that GPU resource management requires an OS policy interface" is a checklist, not a thesis. "We argue" is weak for SOSP.
- P3-C (eBPF first) preferred: thesis → eBPF properties → CPU precedent (sched_ext) → struct_ops for GPU → but insufficient.

**P3 is NOT settled.** Current candidates: P3-B (current tex), P3-C (eBPF first). P3-C preferred for logical completeness. Current tex uses P3-C.

### P4 options: What's the insight?

**P4-A: Two root causes** (Draft 1)
- "Timescale mismatch + information mismatch."
- ✓ Sharp, memorable; simplest option.
- ✗ Two causes for three mechanisms → device BPF unmotivated.
- ✗ "Information mismatch" conflates driver↔app (vertical) and host↔device (horizontal).

**P4-B: Three boundaries** (Draft 2, 3)
- "Three execution boundaries: sync/async, driver/app, host/device."
- ✓ Complete 3→3 mapping.
- ✗ Taxonomy without unifying principle — "three things that are hard about GPUs."
- ✗ Vulnerable to "taxonomy" dismissal.

**P4-C: Co-location as root cause** (Framing 4) — *moved from old P2-B*
- "GPU is PCIe-connected accelerator → breaks co-location assumption → three mismatches."
- ✓ Most publishable insight — the sentence a championing reviewer quotes.
- ✓ Generalization to PCIe/CXL is a strength, not a weakness.
- ✗ Needs evidence to land — works after +27%/P99 data, feels asserted before it.
- ✗ NUMA counter: easy to defend — NUMA is same processor with ~2-3x latency; GPU is different processor, different ISA, different address space, 1000x gap. Qualitative, not quantitative.

**P4-D: Discovery** (Framing 7)
- "We applied eBPF, measured +27%/P99 gap, found three root causes from physical separation."
- ✓ Most honest for agent story.
- ✗ Methodology vs contribution confusion — SOSP wants understanding, not process.
- ✗ Undermines completeness.

**P4-E: Expressiveness wall** — *moved from old P2-C*
- "Physical separation limits which policies can be expressed, not just how they execute."
- ✓ Connects to agent story (interface, not search, is bottleneck).
- ✗ "Expressiveness" loose PL term. "Wall" implies cliff, data shows slope.
- Note: "Richer return type" counter is weak — constructs our system under a different name. Expressiveness limit is real, though "wall" overstates sharpness.

**P4-F: "One insight, three manifestations"** (opus-suggested)
- "Physical separation is one root cause. Three mismatches are its necessary consequences."
- ✓ P4-C done right — single causal principle with derived requirements. Most principled.
- ✗ Tight writing needed to avoid feeling like P4-C restated.

**P4-G: Cross-layer fragmentation** — *moved from old P2-D*
- "Effective GPU policy requires information and control that no single layer possesses."
- ✓ Architecturally honest — policy is scattered.
- ✗ Generic — applies to networking, storage, every layered system. Fatal flaw.

**P4-H: Impedance mismatch** — *moved from old P2-H*
- "GPU operates at multiple timescales simultaneously; any single-timescale interface sacrifices the others."
- ✓ Precise technical mechanism that breaks sync callbacks.
- ✗ Only captures timescale dimension; misses information and visibility.

**P4-I: Architectural barrier (split)** — *moved from old P2-G*
- "Physical separation limits expressiveness. Agents confined."
- ✓ Most complete when split: first paragraph = why driver, second = physical separation.
- ✗ Too dense as single paragraph — four arguments, none lands.

**P4 is mostly settled:** P4-C/P4-F — co-location as root cause with three derived manifestations, placed AFTER P3b evidence.

### P5-P7: Stable across all framings

**P5:** gpu_ext — one mechanism per mismatch + SIMT verifier. (All framings agree.)

**P6:** Capability progression (2.60x → 3.36x → P99) + agent 59 policies 0 panics. (All agree.)

**P7:** Contributions: OS policy interface + eBPF runtime with three layers + validation. (All agree.)

---

## Comparison Matrix

### P2 (practical problem — no insight here)

| | Honesty | SOSP risk |
|--|---------|-----------|
| P2-A "locked" | High | "complaint, not insight; generic eBPF template" |
| P2-E "not extensible" | High | "engineering, just apply sched_ext" |
| P2-F "requirements" | Medium | "post-hoc checklist" |
| P2-I "extensibility gap" | High | "gap-filling, obvious" |
| **P2-E+I (settled)** | **High** | **"practical + gap; insight in P4 preempts 'just apply sched_ext'"** |

### P4 (insight — the core contribution)

| | Insight depth | Agent fit | Honesty | SOSP risk |
|--|--------------|-----------|---------|-----------|
| P4-A "two causes" | Medium | None | High | "incomplete, conflates two gaps" |
| P4-B "three boundaries" | Medium | None | High | "taxonomy without principle" |
| P4-C "co-location root" | High | Low | High | "generalization is strength; needs evidence first" |
| P4-D "discovery" | Medium | Strong | Highest | "methodology vs contribution" |
| P4-E "expressiveness wall" | Medium-High | Strong | Medium | "loose PL term, slope not cliff" |
| P4-F "one insight, three manifestations" | High | Low | High | "must feel derived not relabeled" |
| P4-G "cross-layer" | Medium | Medium | Low | "generic, applies to every layered system" |
| P4-H "timescale impedance" | Medium-High | Low | High | "only one dimension" |
| P4-I "architectural barrier split" | High | Strong | High | "too dense as one para; split helps" |
| **P4-C/F (settled)** | **High** | **Low** | **High** | **"co-location + derived manifestations, after P3b evidence"** |

---

## Synthesis: Final combination (2026-03-30)

**P1-C** (policy impact + agent in one sentence, light touch) + **P2-E+I** (driver not extensible + Linux extensibility gap, practical problem) + **P3-B** (struct_ops starting point + insufficient) + **P4-C/F** (co-location breaks → three derived manifestations, AFTER P3b evidence)

Reasoning:
- P1-C: agent eval is modest (59 configs, no rigorous ablation), so lighter touch avoids P1-B's framing debt while providing "why now."
- P2-E+I: honest practical problem, no insight overclaim. Insight stays in P4 where it's earned by P3b evidence.
- P3-B: struct_ops is OUR starting point, not previous work. "But insufficient" with evidence sets up P4.
- P4-C/F: co-location is the deepest insight, placed after evidence (+27%, P99) so it feels discovered, not asserted.

**Key principle:** P2 = practical problem (honest), P4 = architectural insight (earned). Evidence before explanation.

Previous opus recommendation (P1-B + P2-G split + P4-F) was rejected because:
- P1-B creates framing debt the eval can't pay
- P2-G front-loads insight before evidence, creating redundancy with P4

---

## Questions that drove deeper thinking

1. **"SOSP 想要的是啥？"** → architectural understanding, not complaints
2. **"为什么 policy 在 driver 里面？"** → derive, don't assert. Actually: policy is scattered, driver is coordination point
3. **"有没有更深刻的？"** → Level 1 complaint → Level 2 observation → Level 3 co-location → Level 4 predictive principle
4. **"而且还要和 agent 关联"** → expressiveness ceiling, not just safety
5. **"是不是和下面完全重复了？"** → insight defines problem, system describes solution, no separate challenges
6. **"这个不是 design detail 吗？"** → intro = WHAT and WHY, sections = HOW
7. **"现有的 policy 分散在很多个部分吧？"** → policy scattered across stack, not just driver
8. **"我们没有很好的 evidence"** → honest: discovered three problems while making driver programmable

---

## Open question

Which combination produces the strongest P2 and P4? The main tension:
- P2 needs to state the problem without overclaiming insight
- P4 needs to deliver insight without feeling asserted before evidence
- Agent must thread through consistently
- The "co-location breaks" observation is the deepest insight but needs to be earned by evidence, not front-loaded

---

## Detailed analysis from earlier discussion sessions

### Why policy is in the driver (foundational observation)

GPU hardware delegates resource management to software — there is no hardware page replacement algorithm, no hardware fair scheduler. The kernel driver implements all of this: eviction ordering, prefetch decisions, migration scheduling, compute timeslicing. Why the driver specifically?

1. **Global visibility**: only the kernel driver sees all processes sharing the GPU. User-space sees only its own context.
2. **Hardware privilege**: page table manipulation, TSG scheduling, GPU preemption all require kernel privilege.
3. **Fault path**: GPU page faults land in the driver (nvidia-uvm.ko). Eviction, migration, prefetch decisions happen inside the fault handler.
4. **Cross-process authority**: multi-tenant coordination (preempting one process for another) requires kernel authority.

→ The driver is the **only layer** with global visibility + hardware privilege + fault path + cross-process authority. Policy is not accidentally in the driver — it **must** be there.

**Correction (later discussion):** Policy is actually **scattered across the stack** (user-space frameworks, CUDA runtime, driver, hardware/firmware). Each layer makes locally optimal but globally suboptimal decisions. The driver is the natural **coordination point**, not the only policy location.

### The physical separation problem (the deep architectural insight)

Prior kernel extensibility frameworks (sched_ext, cache_ext, XRP) all assume that **policy, information, and effect are co-located** in a single execution context:
- sched_ext: scheduler decides → context switch → immediate effect. Same processor, same timescale, same address space.
- cache_ext: eviction decision → page IO → same kernel context.

**GPU breaks this co-location assumption.** The GPU is a separate processor connected via PCIe. This physical separation creates:
- **Temporal separation**: policy decision in fault handler (μs) vs effect via PCIe DMA (ms) — 1000x gap
- **Spatial separation**: host memory vs device memory — different address spaces
- **Contextual separation**: fault handler (sync, non-sleepable) vs DMA/preemption (async, sleepable) — different execution models

### The expressiveness wall (connecting architecture to agents)

This physical separation doesn't just make GPU policy *hard* — it limits **which policies can be expressed at all** through a driver-local interface.

A synchronous struct_ops callback can express:
- ✓ Eviction ordering (which page to evict)
- ✓ Prefetch scope within the current VA block
- ✓ Scheduling timeslice hints

A synchronous callback CANNOT express:
- ✗ Cross-VA-block prefetch (requires async DMA — ms, sleepable)
- ✗ Proactive preemption at kernel launch (driver doesn't see application API calls)
- ✗ Device-aware prefetch strategy (driver can't observe per-warp access patterns)

**For agents exploring this policy space, the consequence is:** a synchronous, driver-local interface confines agents to a narrow region of the policy space. The most valuable policies — which account for +27% additional improvement and P99 reduction — are beyond the expressiveness ceiling. No amount of search sophistication helps if the interface can't express what you find.

### Three mismatches = three dimensions of the expressiveness wall

1. **Timescale mismatch** (temporal separation → sync/async boundary): decision in μs, effect in ms. A synchronous callback cannot initiate cross-block migration and wait.

2. **Information mismatch** (application above driver → driver/application boundary): driver sees faults after they happen; optimal timing is proactive (before kernel launch, before epoch boundary). Driver hooks fire only after events.

3. **Visibility mismatch** (device below driver → host/device boundary): host controls resources but can't observe per-warp access patterns, SM utilization, computation phases. Device observes execution but can't control host resources.

Each mismatch requires a distinct mechanism to cross; no single callback model addresses all three.

### Key insight (stated as predictive principle)

**The synchronous, co-located policy model that suffices for CPU extensibility (sched_ext, cache_ext) is fundamentally insufficient for any accelerator connected via a high-latency interconnect (PCIe, CXL). The physical separation between host and accelerator creates three execution boundaries — temporal, informational, and observational — each requiring a distinct extensibility mechanism. We demonstrate this for GPUs and show that crossing all three boundaries recovers performance that no subset can achieve.**

This is predictive: it applies to any PCIe/CXL accelerator, not just NVIDIA GPUs.

### Agent throughline

| Para | Agent role |
|------|-----------|
| P1 | Agents are actively optimizing system policies |
| P2 | Physical separation confines agents to narrow policy region |
| P3a | struct_ops gives agents a programmable interface |
| P3b | But the interface's expressiveness ceiling blocks agents |
| P4 | Three mismatches = three dimensions of the ceiling |
| P5 | gpu_ext removes the ceiling |
| P6 | Agent explored 59 policies, 0 panics |
| P7 | Contribution |

### LATEST revision: Policy is cross-layer, not driver-only

**Key correction:** Previous versions said "policy must reside in the driver." This is WRONG. GPU policies are **scattered across the stack** (user-space frameworks, CUDA runtime, driver, hardware/firmware). Each layer makes locally optimal but globally suboptimal decisions.

**Corrected insight:** Effective GPU policy is inherently cross-layer — needs application intent (above), driver control (middle), device execution state (below) simultaneously. But today's stack fragments policy across isolated layers with no coordination mechanism.

**Revised logical chain (LATEST):**

| Para | Topic sentence |
|------|---------------|
| P1 | GPU policies matter + agents exploring |
| P2 | Effective GPU policy is inherently cross-layer, but the stack is fragmented. Driver is coordination point but can't reach across layers. Agents confined to narrow policy space. Safety + deployability also needed. |
| P3a | eBPF struct_ops provides the starting point (safe, dynamic, driver-level) |
| P3b | Synchronous callbacks stay in one layer, leaving cross-layer performance unrealized |
| P4 | Three mismatches (timescale, information, visibility) explain why — arising from GPU's physical separation |
| P5 | gpu_ext: one mechanism per mismatch + SIMT verifier |
| P6 | Results: capability progression + agent 59 policies 0 panics |
| P7 | Contributions |
