# GPU_ext Intro: Framings, Analysis & Plan

## Key Observations (what we see)

1. GPU resource management policies have large, workload-dependent performance impact (up to 73% of execution time).
2. The GPU driver is the only layer with global visibility, hardware privilege and cross-tenant authority — the only viable coordination point for policy.
3. Synchronous advisory hooks (struct_ops L1) achieve 2.60x, but async (L2) adds +27% and proactive (L3) further reduces P99 — each layer adds non-redundant value.
4. AI agents are actively exploring system-level policies, and GPU is a high-value target but lacks a safe, deployable OS interface for such exploration.
5. SIMT execution on GPU requires distinguishing warp-uniform from lane-varying values — a standard eBPF verifier cannot ensure safety on GPU hardware.

## Key Insights (why it's so)

1. GPU is a separate processor across PCIe — this physical separation breaks the co-location assumption (policy, information, effect in one context) that makes CPU extensibility frameworks (sched_ext, cache_ext) work.
2. Three mismatches (timescale, information, visibility) are derived consequences of physical separation, not independent problems — each requires a distinct mechanism to cross.
3. No single-layer callback model can express the policies that account for the +27% and P99 gains — the interface, not the search, is the bottleneck.

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

### P2 options: What's the problem?

**P2-A: "Locked in driver"** (Draft 1, 2)
- "Policy controls 73% of time, yet implementing any new policy requires modifying the proprietary driver."
- ✓ Creates sharp tension between high impact and zero programmability; most honest if the contribution is primarily engineering.
- ✗ A complaint about vendor practice, not an architectural insight — reviewer says "so convince NVIDIA to open-source the driver" (they already have).
- ✗ Doesn't distinguish from other "make X extensible" papers — "X is important but not extensible" is the generic template for every eBPF paper.

**P2-B: "Co-location breaks"** (Framing 4)
- "Unlike CPU where policy/information/effect co-located, GPU spans a physical boundary via PCIe."
- ✓ Deepest insight — contrastive with CPU extensibility, explains WHY struct_ops fails, predictive for any PCIe/CXL accelerator. The only framing that makes the paper about more than GPUs.
- ✗ Demands reader familiarity with sched_ext/cache_ext internals; if the reader doesn't know how those work, the comparison falls flat.
- ✗ NUMA counter: CPU scheduling involves remote memory with variable latency. However, this counter is **easy to defend** — NUMA is the same processor with ~2-3x latency variation; GPU is a different processor, different ISA, different address space, different execution model, across PCIe with 1000x latency gap. The difference is qualitative (separate processor), not just quantitative (longer latency).

**P2-C: "Expressiveness wall"** (Framing 5)
- "Physical separation limits which policies can be expressed, not just how they execute."
- ✓ Connects directly to agent story (agents limited by interface, not search quality); most natural P2 partner for P1-B.
- ✗ "Expressiveness" has a precise meaning in PL (language expressiveness relative to formal model) — a PL-literate reviewer will note you're using it loosely.
- ✗ "Wall" implies a cliff, but data shows a slope (L1=2.60x, L2=3.29x, L3=more). No discontinuity.
- Note: The "richer return type" counter is weak — pushing async DMA descriptors, uprobe registrations, and device bytecode into a return struct constructs our system under a different name. A synchronous callback genuinely cannot initiate a ms-scale DMA and wait for completion. The expressiveness limit is real, though "wall" overstates its sharpness.

**P2-D: "Cross-layer fragmentation"** (Framing 6)
- "Effective GPU policy requires information and control that no single layer possesses."
- ✓ Most architecturally honest — correctly identifies policy is scattered, not just in driver.
- ✗ Generic — applies to networking (app/socket/TC/NIC), storage (app/FS/block/FTL), every layered system. What's GPU-specific? This is the fatal flaw.
- ✗ "Cross-layer" slightly overstates: the system IS genuinely cross-layer (host driver hooks + uprobes on application APIs + device-side BPF span three layers), but the driver is the clear coordination center. Better framing: "driver-coordinated cross-layer system" than "cross-layer framework."

**P2-E: "Driver not extensible"** (Framing 7, 8)
- "The GPU driver is the only component with visibility + privilege + fault path — yet its policies remain hardcoded."
- ✓ Honest, sets correct expectations if the paper is positioned as systems-building (like Bento or XRP).
- ✗ Not "unattackable" — reviewer can say "NVIDIA open-sourced the modules, contribute upstream" or "this is engineering, not research."
- ✗ Invites "just apply sched_ext" dismissal — if the problem is "driver not extensible" and sched_ext showed how to make kernel subsystems extensible, the contribution is "known pattern applied to new domain." P3b/P4 must preempt this by showing WHY direct application fails.

**P2-F: "Requirements + tradeoff"** (Current tex before latest edit)
- "Safe iterative exploration requires deployability, visibility, containment. No existing approach provides all three."
- ✓ Best framing for organizing a related-work comparison table (define axes, show gaps).
- ✗ Requirements are post-hoc — designed so only your system satisfies them. Reviewer asks "if I add module hot-reload to Forest, does it satisfy deployability?"
- ✗ "Requirements list" is a red flag signaling "we designed a checklist and checked it off" rather than discovering something surprising.

**P2-G: "Architectural barrier"** (Current tex latest edit)
- "Policy must be driver-resident (visibility, privilege, fault path). But GPU is separate processor via PCIe — driver separated from effects in time, space, context. Limits expressiveness. Agents confined."
- ✓ Most complete — derives WHY driver, explains physical separation, connects to agents.
- ✗ Four arguments (why driver, physical separation, expressiveness, agent confinement) in one paragraph. No single argument gets enough space to land. Reader processes four claims in rapid succession and retains none.
- ✓ **Best candidate for splitting into two paragraphs** — first derives "why driver," second explains "physical separation limits expressiveness."

**P2-H: "Impedance mismatch between policy timescales"** (NEW, suggested by opus)
- "GPU resource management operates at multiple timescales simultaneously (μs fault handling, ms migration, s-level scheduling), and any single-timescale interface sacrifices performance at the others."
- ✓ More precise than co-location — focuses on the specific technical mechanism that breaks synchronous callbacks.
- ✗ Only captures the timescale dimension; doesn't explain information mismatch (app intent) or visibility mismatch (device state).

**P2-I: "GPU lacks extensibility other Linux subsystems have"** (NEW, suggested by opus)
- "Linux has made CPU scheduling (sched_ext), page cache (cache_ext), storage (XRP), networking (XDP/TC) extensible — but GPU, the most economically important subsystem, has no extensibility story."
- ✓ Positions paper as filling an obvious gap in a well-established Linux research program; attractive to eBPF/Linux PC members.
- ✗ Frames the contribution as "gap-filling" which is weaker than "architectural insight"; reviewer may say "the gap is obvious, what's the research?"

### P3 options: What's the starting point?

**P3-A: Existing approaches → our insight** (Draft 1)
- "User-space, driver mods, advisory eBPF all insufficient. Even richer callbacks would require fixed async engine."
- ✓ The "richer callback" rebuttal anticipates the obvious reviewer objection ("just return DMA descriptors") and closes it preemptively.
- ✗ Treats struct_ops as previous work being criticized — dishonest when the eval shows "L1: struct_ops advisory hooks (our system)." Reviewer who reads eval will feel deceived.

**P3-B: struct_ops = our starting point** (Draft 3, Current tex)
- "eBPF struct_ops proven for CPU. We adopt synchronous advisory hooks as our starting point."
- ✓ Correctly positions struct_ops as OUR foundation; the "we adopt it → but it's not enough" handoff to P3b/P4 is clean and follows the proven pattern of incremental design justification.
- ✗ Loses the "richer callback" rebuttal from P3-A — reviewer can still ask "why not just return DMA descriptors?" and the intro doesn't preempt this.
- ✗ Transition must be very tight — any gap between "we adopt" and "it's insufficient" makes reader wonder why you adopted something broken.

**P3 is mostly settled:** P3-B with existing approaches as supporting detail. Consider adding one sentence from P3-A's "richer callback" rebuttal to close the obvious counter-argument.

### P4 options: What's the insight?

**P4-A: Two root causes** (Draft 1)
- "Timescale mismatch + information mismatch."
- ✓ Sharp, memorable names; simplest option — two things to remember is better than three. More honest if device-side evidence is thin.
- ✗ Two root causes for three mechanisms means one mechanism (device BPF) is unmotivated — will feel bolted on in eval.
- ✗ "Information mismatch" conflates two genuinely different gaps: driver↔app (vertical, between layers) and host↔device (horizontal, across PCIe). These have different mechanisms, overheads, and evidence.

**P4-B: Three boundaries** (Draft 2, 3)
- "Three execution boundaries: sync/async, driver/app, host/device."
- ✓ Complete, clean 3→3 mapping; makes the paper easy to structure (each section = one boundary).
- ✗ Three independently described boundaries without a unifying principle — reads as "three things that are hard about GPUs" rather than one deep insight with three manifestations.
- ✗ Vulnerable to "taxonomy" dismissal: "The authors classify challenges into three categories. While correct, this classification does not constitute a contribution." Must prove boundaries are *surprising* (not obvious), *necessary* (can't solve without all three), and *sufficient* (all three does solve it).

**P4-C: Co-location as root cause** (Framing 4)
- "GPU is PCIe-connected accelerator → breaks co-location assumption → three mismatches."
- ✓ The most publishable insight — provides the causal principle that P4-B lacks; the sentence a championing reviewer will quote: "physical separation between host and accelerator breaks the co-location assumption underlying existing kernel extensibility frameworks."
- ✓ Generalization to PCIe/CXL accelerators is a **strength** — SOSP reviewers value insights that extend beyond the tested platform. A well-reasoned prediction is expected, not penalized. The real risk is modest: "you haven't tested CXL, so the generalization is a hypothesis" — which is acceptable.
- ✗ Needs evidence to land — works after +27%/P99 data, feels asserted before it.

**P4-D: Discovery** (Framing 7)
- "We applied eBPF, measured +27%/P99 gap, found three root causes from physical separation."
- ✓ Most honest and most credible for the agent story ("here is what the agent and we found").
- ✗ "We tried and found" confuses methodology with contribution — SOSP papers present understanding gained, not process followed.
- ✗ Undermines completeness: "Are there more mismatches you haven't discovered? Is the list complete, or just what you happened to encounter?" P4-C answers completeness; P4-D cannot.

**P4-E: Expressiveness dimensions** (Framing 5)
- "Three mismatches = three dimensions of the expressiveness wall."
- ✓ Ties mismatches to agent story — each dimension truncates the reachable policy space.
- ✗ Vulnerable to "rebuttal by construction": reviewer proposes a single rich callback returning a struct with DMA descriptors + uprobe requests + device bytecode — technically synchronous and single-layer. Your counter is that such a callback pushes all policy into the return type, making the driver a general-purpose interpreter (i.e., your system by another name). But P4-E as stated doesn't make this counter.

**P4-F: "One insight, three manifestations"** (NEW, suggested by opus)
- "Physical separation is one root cause. Three mismatches are its necessary consequences: when policy and effect are separated by a high-latency interconnect, the interface must provide async execution, cross-boundary information relay, and remote observation."
- ✓ P4-C done right — single causal principle with three derived requirements, not three independent observations. Most principled and most defensible.
- ✗ Requires tight writing to avoid feeling like P4-C restated. The "derived requirements" must feel like logical consequences, not just a relabeled list.

### P5-P7: Stable across all framings

**P5:** gpu_ext — one mechanism per mismatch + SIMT verifier. (All framings agree.)

**P6:** Capability progression (2.60x → 3.36x → P99) + agent 59 policies 0 panics. (All agree.)

**P7:** Contributions: OS policy interface + eBPF runtime with three layers + validation. (All agree.)

---

## Comparison Matrix

| | Insight depth | Agent fit | Honesty | SOSP risk |
|--|--------------|-----------|---------|-----------|
| P2-A "locked" | Low | None | High | "complaint, not insight" |
| P2-B "co-location" | High | Low | High | "abstract, demands sched_ext knowledge; NUMA counter easy to defend" |
| P2-C "expressiveness" | Medium-High | Strong | Medium | "loose PL term, slope not cliff; but expressiveness limit is real" |
| P2-D "cross-layer" | Medium | Medium | Low | "generic, oversells design" |
| P2-E "not extensible" | Low | Medium | High | "engineering, just apply sched_ext; not truly unattackable" |
| P2-F "requirements" | Low | Strong | Medium | "post-hoc checklist, straw man risk" |
| P2-G "architectural" | High | Strong | High | "too dense; **split into two paras**" |
| P2-H "timescale impedance" | Medium-High | Low | High | "only captures one dimension" |
| P2-I "extensibility gap" | Medium | Low | High | "gap-filling, obvious" |
| | | | | |
| P4-A "two causes" | Medium | None | High | "incomplete, conflates two gaps" |
| P4-B "three boundaries" | Medium | None | High | "taxonomy without principle" |
| P4-C "co-location root" | High | Low | High | "CXL untested but generalization is a strength, not weakness" |
| P4-D "discovery" | Medium | Strong | Highest | "trial-and-error, completeness?" |
| P4-E "expressiveness dims" | Medium-High | Strong | Medium | "rebuttal by construction" |
| P4-F "one insight three manifestations" | High | Low | High | "must feel derived not relabeled; agent connection not inherent" |

---

## Synthesis: Opus-recommended combination

**P1-B** (agent as full P1, but trimmed — 3 sentences establishing use case, not a full agent paragraph) + **P2-G split into two paragraphs** (first: why driver is the right place; second: physical separation limits expressiveness) + **P4-F** (one root cause → three derived manifestations, after evidence in P3b).

Reasoning:
- P1-B provides "why now" (agents + open-source GPU drivers are both new in 2026); P1-A was writable in 2020.
- P2-G split gives the deepest problem statement without density; the co-location insight is earned, not front-loaded.
- P4-F is the most principled insight — single causal root (physical separation via PCIe) with three necessary consequences.
- P1-B's framing debt (agent eval expectations) is payable: 59 configs, 50 safety events, 0 panics is a respectable case study.

**Key principle:** Evidence before explanation. Discovery before derivation. Honest about what we found, not what we "argue."

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
