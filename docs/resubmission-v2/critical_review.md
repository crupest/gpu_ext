# Critical Review of Resubmission V2 Planning Documents

Date: 2026-03-28

This document reviews all planning documents in `docs/resubmission-v2/` against the actual paper (`.tex` files) and MEMORY.md evidence. It identifies what's correct, what's wrong, what's risky, and provides a concrete recommended structure.

---

## I. What the Planning Docs Get Right

### 1. "Paper hides its own contribution" — CORRECT

The diagnosis in `paper_structure_draft.md` Part III and `improvement_plan.md` is spot-on. The paper only shows 5 trivial kfuncs (move_head, move_tail, set_attr, reject_bind, sched_preempt). The real contribution — `bpf_gpu_migrate_range`, `bpf_wq`, `bpf_nv_gpu_preempt_tsg`, uprobe→kfunc→GPU — is entirely absent from the paper text. This is the single biggest fixable problem.

### 2. Capability progression table as "gpu_ext's Table 2" — CORRECT

The L0→L3 table (paper_structure_draft.md Insight 6, driver_novelty.md) is the right centerpiece. L1=2.65x → L2=3.36x (+27%) precisely measures the async pipeline's value. This table should be the FIRST experiment in eval or placed in §2 as motivation.

### 3. Timescale mismatch insight — CORRECT and STRONG

μs decision vs ms DMA is clean, quantifiable, and directly explains why struct_ops alone is insufficient. This should be the paper's primary "why is this hard?" argument.

### 4. intro_draft.md structure — MUCH BETTER than current intro

Leading with 73% PCIe observation, honest dual baselines (1.3x/4.8x), explicit sync/async split are all correct moves. The 8-paragraph structure (P1-P8) is well-organized.

### 5. improvement_plan.md P0 priorities — CORRECT

- GNN 3.36x (data exists, low effort, high impact)
- "Why Not Just struct_ops?" section
- Cleanup placeholders/annotations ("xxx" values, `\xiangyu{}` comments)

These are the highest-ROI changes.

---

## II. What's Wrong or Risky

### PROBLEM 1: P99 -95% Data Attribution May Be Incorrect

**Severity: HIGH — this could undermine the entire L3 evidence claim.**

Current paper `eval.tex:108`:
> "\sys reduces LC P99 launch latency by 95%"

This is the **compute-bound timeslice scheduler** result (struct_ops differentiated timeslice: LC 1s, BE 200μs). It is NOT the uprobe→kfunc preemption result.

But `paper_structure_draft.md` Insight 6 attributes -95% to L3 (proactive uprobe→kfunc):

| L3: Proactive app-boundary | + sleepable uprobe → kfunc | LC P99 **-95%** |

And `intro_draft.md` P3 writes:
> "advisory timeslice control alone has no measurable effect on LC tail latency, while proactive uprobe-triggered preemption reduces P99 by 95%"

Meanwhile, `improvement_plan.md` Change 2 reports the actual uprobe kfunc results as:
> "Test results: -48.4% latency (Test E), -57.6% latency (Test F)"

**The discrepancy:** -95% (attributed to L3 uprobe) vs -48% to -58% (actual uprobe kfunc data). These could be reconciled if -95% refers to P99 tail specifically while -48%/-58% are mean — tail latency is disproportionately helped by preemption. But this needs explicit verification.

**Possible explanations:**
- The -95% is from a DIFFERENT experiment (memory-bound multi-tenant where struct_ops timeslice has 0% effect, uprobe preemption has -95% P99)
- The -48%/-58% are mean latency reduction, while -95% is P99 (plausible but unverified)
- The planning docs conflated two different experiments

**Action required:** Before writing any paper text using -95% for L3, verify:
1. Which exact experiment produces this number?
2. Is it mean or P99?
3. Is it from a scenario where advisory struct_ops has demonstrably 0% effect?
4. What is the raw data source (file path, experiment config)?

If the real number is -50%, use -50%. An honest -50% where advisory=0% is still an extremely strong result. An unverifiable -95% will destroy credibility if challenged.

### PROBLEM 2: 6 Insights Is Too Many — Dilutes the Paper

**Severity: MEDIUM — structural risk to narrative coherence.**

`paper_structure_draft.md` Part IV lists 6 ranked insights. Insights 1-2 are diagnostic ("paper doesn't change thinking", "paper hides contribution") — useful for internal analysis but NOT for the paper. Insights 3-6 are the actual content.

A SOSP paper needs ONE core insight with 1-2 supporting arguments. Having 6 "insights" makes the paper feel like an analysis report, not a research contribution.

**The one true insight:**
> Synchronous eBPF hooks (struct_ops) are architecturally insufficient for GPU extensibility. Composable multi-mechanism BPF pipelines (struct_ops + bpf_wq + sleepable kfuncs + proactive uprobes) are the necessary abstraction.

**Two supporting root causes:**
- Timescale mismatch (μs/ms) → sync/async split
- Information mismatch (faults ≠ intent) → proactive app-boundary hooks

Everything else (capability progression, three-domain crossing, BPF type system enforcement) is EVIDENCE for this insight, not separate insights.

**Action:** In the paper, present one thesis, two root causes, and a capability progression table as evidence. Do not present 6 separate "insights."

### PROBLEM 3: "Execution Strategy Is Workload-Dependent" Argument Is Over-Engineered

**Severity: MEDIUM — invites philosophical counter-arguments.**

`paper_structure_draft.md` elevates "execution strategy is workload-dependent" to "THE DEEPEST" insight and wants it in the abstract. The 4-column table (Workload / Decision / Execution strategy / Why callback can't express) is elaborate.

**The vulnerability:** A reviewer can counter: "Then add an extensible DMA scheduler API that callbacks configure. The callback returns DMA schedule descriptors, the driver executes them. You don't need BPF running in the fault handler."

The real counter to this is simpler and stronger:
1. That "extensible DMA scheduler" IS a policy engine — you've just moved the policy from the callback to the engine. Turtles all the way down.
2. The BPF type system **enforces** the sync/async split at load time (non-sleepable vs sleepable context). It's not a design choice — it's a safety invariant.
3. Empirically: L1→L2 = +27%. This is a measured gap, not a philosophical argument.

**Action:** In the paper, lead with the empirical gap (27%) and the BPF type system enforcement. Use the "execution strategy" argument as a 2-sentence motivation, not a centerpiece table. The simpler the argument, the harder it is to attack.

### PROBLEM 4: Agent Section at 1.5 Pages Is Too Large

**Severity: MEDIUM — eats space from more important content.**

`agent_framing_decision.md` plans §6.N at 1-1.5 pages with:
- Setup paragraph (0.25p)
- Safety events table (0.25p)
- Three convergence arcs (0.5p)
- Development velocity (0.15p)
- Counterfactual (0.1p)

**Problems:**
1. Agent case studies prove **methodology** (development process is safe), not **system design** (architecture is correct). SOSP reviewers care about system design.
2. 3 convergence arcs read like a blog post, not a systems paper.
3. 1.5 pages means cutting 1.5 pages elsewhere. Device-side is already being compressed. What else gets cut?
4. The containment argument can be made in 3 sentences + 1 table — it doesn't need 3 case studies.

**Recommendation:** 0.5 pages total:
- 1 table: 50 safety events taxonomy (7 categories, 0 panics, 0 corruption) — 0.15p
- 1 paragraph: development velocity (seconds vs hours, 75% failure rate makes this essential) — 0.1p
- 1 paragraph: ONE representative case study (pick cross-block, it maps cleanest to L2) — 0.15p
- 1 sentence: counterfactual (move_head Xid 31 → would be kernel panic without BPF) — 0.05p
- Full 3 case studies → extended version / technical report / appendix

### PROBLEM 5: Device-Side Story Is Unresolved

**Severity: HIGH — structural tension in the paper.**

All planning docs focus on strengthening the host-side story. This is correct. But the current paper has:
- ~2 pages in design on SIMT verifier + binary rewriting + cross-layer maps
- Microbenchmarks only (32-element vector-add) in eval
- No real-workload device-side headline number

The plans suggest "condense device-side to ~0.5 page." But if design keeps 2 pages and eval gives 0.5 pages, the reviewer will ask: **"Why did you spend 2 pages designing something you barely evaluated?"**

**Two options:**

**(a) Match depth:** Compress device-side DESIGN to ~0.75 pages AND eval to ~0.5 pages. Device-side becomes a secondary contribution: "Additionally, we extend eBPF to GPU devices with a SIMT-aware verifier for low-overhead observability." Honest about scope.

**(b) Strengthen eval:** Keep device-side design at ~1.5 pages but add a real-workload observability result (e.g., use threadhist to detect and fix SM load imbalance in a real kernel, producing a measurable improvement). This is higher effort.

**Recommendation:** Option (a). The paper's core story is host-side composable pipelines. Device-side observability is complementary. Don't promise more than you deliver.

### PROBLEM 6: The "Richer Callback" Counter-Argument Needs Sharpening

**Severity: LOW-MEDIUM — important for intro/design, but fixable.**

`paper_structure_draft.md` Insight 3 and `intro_draft.md` P3 argue that "even a richer callback returning a list of prefetch targets" fails because "execution strategy is workload-dependent."

A sophisticated reviewer can counter: "Your argument proves that the current NVIDIA UVM API is too narrow, not that struct_ops is insufficient. A struct_ops callback that returns `{target_addr, size, priority, batch_hint}` tuples, with the driver implementing a general-purpose async DMA scheduler, would work."

**Stronger counter-arguments (use these instead):**
1. **That DMA scheduler IS a policy.** You've just moved the policy from the BPF callback to the driver engine. The driver now needs to implement scheduling, batching, prioritization — exactly the monolithic-policy problem the paper solves. This is turtles-all-the-way-down.
2. **BPF type system enforces the split.** The sync/async boundary isn't a design choice — it's a safety invariant. Non-sleepable BPF context (fault handler) physically cannot call sleepable functions. The verifier rejects it at load time. This means the decomposition follows from safety, not taste.
3. **Empirical evidence.** L1 (advisory only) = 2.65x. L2 (+ programmable async) = 3.36x. The 27% gap is measured, not argued. No amount of return-type richness can close it without giving the policy author control over the async execution.

### PROBLEM 7: 73% Observation Is Workload-Specific

**Severity: LOW — fixable with framing.**

"PCIe DMA accounts for 73% of per-token decode time" applies to oversubscribed 120B MoE inference at 1.84x oversubscription. For compute-bound workloads fitting in VRAM, this number is ~0%.

If presented as a universal GPU observation, a reviewer will correctly call it out as cherry-picked.

**Fix:** Frame as: "Under memory oversubscription — increasingly common as model sizes outpace GPU memory — the choice of eviction and prefetch policy dominates execution time. We measured this on a 120B MoE model at 1.84x oversubscription: PCIe DMA accounts for 73% of decode time."

Explicitly scope the claim. The scoped version is still compelling because oversubscription IS the interesting case.

### PROBLEM 8: New Challenges (C1/C2/C3) Drop SIMT Verifier and Cross-Layer Maps

**Severity: MEDIUM — risks losing a genuine technical contribution.**

Current paper challenges:
- C1: Safe GPU policy interface
- C2: SIMT mismatch (verifier)
- C3: Host-device shared state (cross-layer maps)

intro_draft.md new challenges:
- C1: Timescale mismatch (sync/async split)
- C2: Reactive → proactive
- C3: Cross-process authority

The SIMT-aware verifier (old C2) and cross-layer maps (old C3) completely disappear from the challenges. These are genuine technical contributions — the warp-uniform vs lane-varying verification, the snapshot-based consistency model.

**Recommendation:** Keep the new C1/C2/C3 as primary challenges (they match the primary contribution: host-side composable pipelines). Add a C4 or secondary paragraph: "Extending eBPF to GPU devices introduces SIMT-specific verification challenges (warp uniformity, bounded divergence) that we address with a SIMT-aware verifier (§X)." This acknowledges the device-side work without making it the primary story.

### PROBLEM 9: vLLM Story Lacks Clear Strategy

**Severity: LOW-MEDIUM — one of four workloads, not fatal.**

The vLLM result (1.3x throughput) "matches LMCache" per planning docs. If gpu_ext only matches an existing application-level library, that's not compelling.

**Better framing:** The value is **transparency** — gpu_ext achieves LMCache-level performance without modifying vLLM code, without framework-specific offload logic, and while supporting co-located tenants that LMCache cannot coordinate. The comparison should be:
- LMCache: 1.3x, but requires framework integration, single-tenant only
- gpu_ext: 1.3x, transparent, composable with multi-tenant policies

This turns a weak number into a strong architectural argument.

### PROBLEM 10: XRP Parallel Is Slightly Forced

**Severity: LOW — useful analogy, don't overplay.**

XRP's insight is about WHERE to place the hook (syscall vs NVMe driver). gpu_ext's insight is about WHAT KIND of mechanism to use (sync vs async, advisory vs active). These are different dimensions. Calling gpu_ext's table "gpu_ext's Table 2" is a useful framing device, but the papers solve different problems.

Don't structure the paper as "we did what XRP did but for GPU." Structure it as "XRP showed eBPF placement matters; we show eBPF mechanism composition matters. These are complementary insights in the same extensibility trajectory."

---

## III. Recommended Paper Structure

### Abstract (rewrite completely)

```
GPU resource management policies — memory placement, page migration, compute
scheduling — control up to 73% of execution time under memory oversubscription,
yet are locked inside monolithic drivers. Applying the synchronous struct_ops
model (sched_ext, cache_ext) to GPU drivers captures advisory decisions but
leaves significant performance unrealized: GPU DMA operations take milliseconds
while fault handlers return in microseconds, and optimal policies require
application intent invisible to the driver.

We present gpu_ext, which makes GPU resource management extensible through
composable eBPF policy pipelines: struct_ops hooks for fast advisory decisions,
sleepable kfuncs with BPF work queues for async cross-boundary migration, and
proactive uprobes on application APIs for intent-driven policies spanning
user-space, kernel, and GPU hardware. gpu_ext additionally extends eBPF to
GPU devices with a SIMT-aware verifier for low-overhead observability.

A capability progression validates each mechanism layer: advisory hooks alone
achieve 2.65x on GNN training; adding async execution achieves 3.36x (+27%);
adding proactive preemption reduces multi-tenant tail latency by [VERIFIED
NUMBER]% where advisory hooks have zero effect. Across LLM inference, GNN
training, vector search, and multi-tenant scenarios, gpu_ext improves
throughput by up to 4.8x and reduces tail latency by up to 2x without
modifying applications.
```

### §1 Introduction

Use intro_draft.md P1-P8 with these fixes:
- P3: Simplify "richer callback" argument — 2 sentences, not a table
- P6: Add C4 for SIMT verification (don't drop completely)
- P7: Use verified numbers only (check -95% claim)
- P8: Keep 3 contributions; third can mention containment evidence in 1 sentence

### §2 Background and Motivation

Keep current structure. ADD:
- §2.4 "Where Does the Time Go?" — PCIe 73% profile (scoped to oversubscribed workloads)
- Capability progression table here (in §2, not §6) — this is motivation, not evaluation
- Explicit "struct_ops alone CANNOT do X, Y, Z" statement

### §3 Design (reorganized)

- Fold "Design Principles" into §3 intro (3 sentences, not 0.5 page)
- §3.1 Policy Interface (current memory + scheduling hooks) — ~1 page
- §3.2 **Async Policy Pipeline (NEW)** — bpf_wq + sleepable kfuncs, with async pipeline diagram — ~0.75 page
- §3.3 **Proactive App-Boundary Hooks (NEW)** — uprobe → kfunc → GPU hardware — ~0.5 page
- §3.4 Device-side SIMT Verification (compressed) — ~0.75 page
- §3.5 Cross-layer Maps (current, slightly compressed) — ~0.5 page

### §4 Implementation

Keep current structure. ADD:
- How `bpf_gpu_migrate_range()` breaks VA block boundary
- How `bpf_nv_gpu_preempt_tsg()` achieves kernel-privilege preemption
- Multi-program composition example (kprobe + struct_ops + bpf_wq)

### §5 Evaluation (reorganized)

- **Lead with capability progression** (L0→L3, GNN workload) — the "Table 2"
- RQ1: Single-tenant (GNN with 3.36x, llama.cpp with honest UVM+hints baseline, vLLM with transparency framing, FAISS)
- RQ2: Multi-tenant (current + kfunc preemption case study)
- RQ3: Programmability + Overhead (current + confidence intervals)
- **RQ4 (NEW, 0.5 page):** eBPF Containment — safety events table + development velocity + 1 case study paragraph

### §6 Discussion (expanded from 7 lines to ~0.75 page)

- Portability (current)
- Generalizability (CXL/DPU/NPU as DISCUSSION, not claim — 1 paragraph)
- Containment vs Rejection (honest framing — 1 paragraph)
- Limitations (device-side is observability-focused; real graph datasets future work)

### §7 Related Work (if separate section exists, or fold into §2)

### §8 Conclusion (rewrite with new thesis)

---

## IV. Space Budget

SOSP = 12 pages + references. Estimated delta from current paper:

| Section | Current | Proposed | Delta |
|---------|---------|----------|-------|
| Abstract | 0.3p | 0.3p | 0 |
| Intro | 1.2p | 1.5p | +0.3 |
| Background | 2.0p | 2.5p (+ capability table + profile) | +0.5 |
| Design Principles | 0.5p | 0p (folded into Design) | -0.5 |
| Design | 3.0p | 3.5p (+ async pipeline + proactive hooks, - SIMT compress) | +0.5 |
| Implementation | 1.5p | 1.5p | 0 |
| Eval | 3.0p | 3.5p (+ cap progression + preemption + containment, - compress device microbench) | +0.5 |
| Discussion | 0.3p | 0.75p | +0.45 |
| Conclusion | 0.2p | 0.2p | 0 |
| **Total delta** | | | **+1.75** |

**Must cut ~1.75 pages:**
- Design Principles section: -0.5p
- SIMT verifier compression (2p → 0.75p): -1.25p
- Device-side microbenchmark compression: -0.25p
- Total cuts: ~2.0p (sufficient with margin)

---

## V. The Biggest Risk

**Risk: The paper tries to change TOO MUCH and becomes two incoherent stories stitched together.**

The current paper tells a clear (if mediocre) story: "eBPF for GPU driver + device." The planning docs propose a better but more complex story: "composable multi-mechanism pipelines with async execution and proactive hooks." If the rewrite is partial — new intro but old design, or new design but old eval — the paper becomes a patchwork.

**Mitigation: Choose ONE story. Stick with it end-to-end.**

Recommended core story:
> GPU resource management needs MORE than struct_ops. We identify why (timescale mismatch + information mismatch), design the minimal composition (struct_ops + async kfuncs + proactive uprobes), and prove each layer is necessary (capability progression). Additionally, we extend eBPF to GPU devices with SIMT-aware verification for low-overhead observability.

Device-side is "additionally" — a secondary contribution. Don't let it compete with the primary story.

---

## VI. Immediate Action Items (Prioritized)

| Priority | Action | Effort | Blocks |
|----------|--------|--------|--------|
| **P0** | Verify -95% P99 data: which experiment, which scenario, raw data file | 1h | All L3 claims |
| **P0** | GNN 3.36x into eval (data exists in `workloads/pytorch/result/v1_*.json`) | Low | Figure update |
| **P0** | Remove `\xiangyu{}`, `\yusheng{}`, `\todo{}`, fix "xxx" placeholders | Low | None |
| **P0** | Add `bpf_gpu_migrate_range`, `bpf_wq`, `bpf_nv_gpu_preempt_tsg` to design section | Medium | None |
| **P1** | Write "Why Not Just struct_ops?" subsection in design or discussion | Low | None |
| **P1** | Add honest UVM+hints baseline bar to llama.cpp figure | Low | Data may need collection |
| **P1** | Add capability progression table to §2 or lead of §5 | Medium | P0 (verify -95%) |
| **P1** | Compress Design Principles to 3 sentences in §3 intro | Low | None |
| **P1** | Add confidence intervals to all figures | Low | Plotting script update |
| **P2** | Kfunc preemption case study in §5 RQ2 | Medium | Figure creation |
| **P2** | Rewrite abstract with new thesis | Medium | P0, P1 complete |
| **P2** | Rewrite intro following intro_draft.md (with fixes from this review) | High | P0, P1 complete |
| **P2** | Compress SIMT verifier from ~2p to ~0.75p | Medium | None |
| **P3** | Agent containment section (§5 RQ4, 0.5 page) | Medium | None |
| **P3** | Expand discussion section | Low | None |
| **P3** | Explain missing baselines (Forest, HELM, DREAM, SUV) | Low | Text only |
