# GPU_ext Intro: Framings, Analysis & Plan

## Documents

| File | Content |
|------|---------|
| `intro_draft.md` | Draft 1: Two root causes (timescale + information mismatch) |
| `intro_draft2.md` | Draft 2: Three execution boundaries (sync/async, driver/app, host/device) |
| `intro_draft3.md` | Draft 3: Agent motivation + three boundaries |
| `../paper/tex/intro.tex` | Current working LaTeX (being edited) |

---

## Per-paragraph options (2026-03-29)

Differences concentrate in P1, P2, P3, P4. P5-P7 are stable across all framings.

### P1 options: What motivates the paper?

**P1-A: Policy importance only** (Draft 1, 2)
- "GPU policies matter, diverse workloads, 73%." No agent.
- ✓ Uncontroversial opening that any GPU-systems reviewer accepts without pushback.
- ✗ No "why now" hook — could have been written in 2020; doesn't explain why extensibility is timely.

**P1-B: Agent as full P1** (Draft 3)
- "AI agents are exploring GPU policies... 59 configs, 75% fail... needs safe extensibility."
- ✓ Vivid and concrete — reader immediately understands the iteration loop and why safety matters.
- ✗ The 59-config / 75%-fail data is entirely our own, with no external citation; a reviewer may say "this is your system's eval, not a general trend."

**P1-C: Policy + agent in one sentence** (Current tex)
- "GPU policies have large impact, and AI agents are actively optimizing such policies across the stack."
- ✓ Compact — establishes both themes in one paragraph without over-committing to either.
- ✗ Agent is mentioned once and then disappears until P6; opus called this "a Chekhov's gun that fires once in a throwaway sentence."

### P2 options: What's the problem?

**P2-A: "Locked in driver"** (Draft 1, 2)
- "Policy controls 73% of time, yet implementing any new policy requires modifying the proprietary driver."
- ✓ Creates sharp tension between high impact and zero programmability in one sentence.
- ✗ This is a complaint about vendor practice, not an architectural insight — a reviewer says "so convince NVIDIA to open-source the driver."

**P2-B: "Co-location breaks"** (Framing 4)
- "Unlike CPU where policy/information/effect co-located, GPU spans a physical boundary via PCIe."
- ✓ The deepest insight — contrastive with CPU extensibility, explains WHY struct_ops fails, predictive for any PCIe/CXL accelerator.
- ✗ If placed in P2 before any evidence, it reads as an assertion the reader has no reason to believe; the co-location argument is more convincing AFTER seeing +27%/P99 data.

**P2-C: "Expressiveness wall"** (Framing 5)
- "Physical separation limits which policies can be expressed, not just how they execute."
- ✓ The "not just how but which" distinction is sharp and connects directly to the agent story (agents limited by interface, not by search quality).
- ✗ "Expressiveness" is our own concept, not a standard systems term — we cannot formally prove that certain policies are inexpressible vs merely difficult to implement.

**P2-D: "Cross-layer fragmentation"** (Framing 6)
- "Effective GPU policy requires information and control that no single layer possesses."
- ✓ Correctly identifies that policy is scattered (not just in driver) and explains why every single-layer approach fails.
- ✗ "Systems are fragmented across layers" applies to networking, storage, and every other stack — a reviewer asks "what's GPU-specific?" and the answer ("PCIe") isn't in this framing.

**P2-E: "Driver not extensible"** (Framing 7, 8)
- "The GPU driver is the only component with visibility + privilege + fault path — yet its policies remain hardcoded."
- ✓ Honest and unattackable — this is simply a true statement about the current state of GPU drivers.
- ✗ SOSP expects insight beyond "X hasn't been done"; a reviewer writes "the contribution is engineering — you just applied the sched_ext pattern to a new subsystem."

**P2-F: "Requirements + tradeoff"** (Current tex before latest edit)
- "Safe iterative exploration requires deployability, visibility, containment. No existing approach provides all three."
- ✓ Enumerates concrete requirements and uses them to organize the critique of existing approaches.
- ✗ The three requirements (deployability, visibility, containment) appear from nowhere — they aren't derived from the architecture, making the list feel arbitrary and checklist-like.

**P2-G: "Architectural barrier"** (Current tex latest edit)
- "Policy must be driver-resident (visibility, privilege, fault path). But GPU is separate processor via PCIe — driver separated from effects in time, space, context. Limits expressiveness. Agents confined."
- ✓ Derives WHY driver is the coordination point, explains physical separation, connects to agents — the most complete version.
- ✗ Packs four distinct arguments (why driver, physical separation, expressiveness, agent confinement) into one paragraph, making it dense and hard to parse on first read.

### P3 options: What's the starting point?

**P3-A: Existing approaches → our insight** (Draft 1)
- "User-space, driver mods, advisory eBPF all insufficient. Even richer callbacks would require fixed async engine."
- ✓ The "richer callback" rebuttal anticipates the obvious reviewer objection ("just return DMA descriptors") and closes it.
- ✗ Treats struct_ops as previous work being criticized, when it's actually our own system's L1 baseline.

**P3-B: struct_ops = our starting point** (Draft 3, Current tex)
- "eBPF struct_ops proven for CPU. We adopt synchronous advisory hooks as our starting point."
- ✓ Correctly positions struct_ops as OUR contribution's foundation, not someone else's failed attempt.
- ✗ Defers the "why insufficient" argument entirely to the next paragraph — reader may wonder why you're adopting something you're about to say is broken.

**P3 is mostly settled:** P3-B with existing approaches as supporting detail.

### P4 options: What's the insight?

**P4-A: Two root causes** (Draft 1)
- "Timescale mismatch + information mismatch."
- ✓ Sharp, memorable names that a reviewer can reference in discussion; each maps cleanly to a mechanism.
- ✗ Covers only sync/async and driver/app — device-side (host/device boundary) is absent, making it appear bolted on when introduced later as "additionally."

**P4-B: Three boundaries** (Draft 2, 3)
- "Three execution boundaries: sync/async, driver/app, host/device."
- ✓ Complete — device-side is integral from the start; clean 3-boundary → 3-mechanism mapping.
- ✗ Opus critique: "listing three things is classification, not insight" — the boundaries are described independently without a unifying causal principle explaining why they arise.

**P4-C: Co-location as root cause** (Framing 4)
- "GPU is PCIe-connected accelerator → breaks co-location assumption → three mismatches."
- ✓ Provides the causal principle that P4-B lacks: all three mismatches arise from one root cause (physical separation via PCIe), making the decomposition feel principled rather than ad-hoc.
- ✗ The co-location argument is abstract and needs concrete evidence to land; works well AFTER empirical data (+27%, P99 gap) but feels asserted if placed before evidence.

**P4-D: Discovery** (Framing 7)
- "We applied eBPF, measured +27%/P99 gap, found three root causes from physical separation."
- ✓ Matches our actual methodology — evidence first, explanation second; avoids claiming we derived the design from first principles.
- ✗ SOSP reviewers prefer principled design to post-hoc rationalization; "we tried and found" reads as trial-and-error rather than architectural reasoning.

**P4-E: Expressiveness dimensions** (Framing 5)
- "Three mismatches = three dimensions of the expressiveness wall."
- ✓ Ties the three mismatches back to the agent story — each mismatch is a dimension along which the synchronous interface truncates the reachable policy space.
- ✗ "Expressiveness wall" is a metaphor we invented, not a measurable property — a reviewer can dismiss it as rhetorical framing rather than technical substance.

### P5-P7: Stable across all framings

**P5:** gpu_ext — one mechanism per mismatch + SIMT verifier. (All framings agree.)

**P6:** Capability progression (2.60x → 3.36x → P99) + agent 59 policies 0 panics. (All agree.)

**P7:** Contributions: OS policy interface + eBPF runtime with three layers + validation. (All agree.)

---

## Comparison Matrix

| | Insight depth | Agent fit | Honesty | SOSP risk |
|--|--------------|-----------|---------|-----------|
| P2-A "locked" | Low | None | High | "complaint" |
| P2-B "co-location" | High | Medium | High | "abstract, asserted" |
| P2-C "expressiveness" | High | Strong | Medium | "not provable" |
| P2-D "cross-layer" | Medium | Medium | Low | "descriptive, oversells" |
| P2-E "not extensible" | Low | Medium | Highest | "engineering" |
| P2-F "requirements" | Low | Strong | Medium | "arbitrary list" |
| P2-G "architectural" | High | Strong | High | "too much in one para" |
| | | | | |
| P4-A "two causes" | Medium | None | High | "incomplete" |
| P4-B "three boundaries" | Medium | None | High | "taxonomy" |
| P4-C "co-location root cause" | High | Medium | High | "abstract" |
| P4-D "discovery" | Medium | Medium | Highest | "trial-and-error" |
| P4-E "expressiveness dims" | High | Strong | Medium | "metaphor" |

---

## Synthesis: Recommended combination

No single framing suffices. Combine:

- **P1**: Agent + policy importance (Framing 3)
- **P2**: Driver policies not extensible + WHY driver is right place (Framing 8, derived)
- **P3**: eBPF as starting point
- **P4**: Evidence first (Framing 7) + co-location as root cause (Framing 4): "we applied eBPF, measured gap, found three mismatches from physical separation"
- **P5**: One mechanism per mismatch
- **P6**: Capability progression + agent validation (Framing 3 + 5)

**Key principle:** Evidence before explanation. Discovery before derivation.

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
