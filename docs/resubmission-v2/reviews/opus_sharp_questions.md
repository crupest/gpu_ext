# Opus Reviewer Sharp Questions (4 rounds, 2026-03-30)

Collected from 4 rounds of opus-as-SOSP/OSDI-reviewer critique on intro.md observations/insights/findings.

---

## Factual Correctness

**Q1: "No hardware page replacement, no hardware fair scheduler" — is this true?**
Modern NVIDIA GPUs (Turing+) have hardware fault queues, MMU with TLB hierarchies, and a timeslice scheduler (Volta+, MPS/MIG on A100+). The hardware provides *mechanisms* but delegates *policy* to software. Saying "no hardware page replacement" conflates mechanism and policy. Fix: "no hardware page replacement *policy*."
- Status: FIXED in B2

**Q2: "NVIDIA open-sourced its GPU kernel modules" — how open?**
NVIDIA open-sourced the kernel-mode driver shims (kernel-open/) under dual GPL/MIT. Firmware, user-mode driver, and much resource management logic remain proprietary. "Open-sourced its GPU kernel modules" implies more openness than exists. Our system modifies nvidia-uvm.ko which IS in kernel-open, so defensible for our use case.
- Status: FIXED — qualified to "kernel-mode GPU driver modules (including UVM)"

**Q3: "1000x (ETC)" — what is this measuring?**
Mixing incomparable metrics (speedup ratios, stall reductions, percentage improvements) in one sentence is misleading. "1000x" dominates the sentence and makes other numbers irrelevant. Is it a comparison against a pathological baseline?
- Status: FIXED — removed ETC 1000x, each number now has metric attribution

---

## Overclaims

**Q4: "The GPU driver is the ONLY layer" / "ONLY viable coordination point"**
- Hypervisor also has global visibility and hardware privilege (in virtualized settings)
- CUDA runtime with MPS can coordinate to some degree
- The document itself later says "policy must reside in the driver — This is WRONG"
- Fix: "best-positioned" / "primary software-accessible layer" / "on bare-metal"
- Status: FIXED

**Q5: "No safe, deployable OS interface exists for GPU policy today"**
Trivially falsified: cudaMemAdvise, cudaMemPrefetchAsync, MPS priority controls, MIG partitioning are all safe, deployable interfaces. What doesn't exist is a *general-purpose, programmable, kernel-level extensibility interface*.
- Status: FIXED — narrowed to "no general-purpose, programmable extensibility interface"

**Q6: "Each layer adds non-redundant value"**
The data doesn't support this generalization. +27% is one workload (GNN) at one oversub ratio. L3's 9.5% is modest. llama.cpp shows near-zero benefit. This claim cherry-picks the best case per layer across different workloads.
- Status: PARTIALLY FIXED — removed generalization, present per-workload data directly. Weakness note retained.

---

## Logical Gaps

**Q7: Why exactly THREE mismatches? Why not four or five?**
Starting from "separate processor across PCIe," can you derive exactly three? What about:
- Power management mismatch (host can't observe GPU power states)?
- Memory consistency mismatch (different memory models)?
If "those are subsumed" → show how. If "not relevant to our workloads" → then three aren't derived from first principles, they're empirically discovered, and "derived consequences" is dishonest.
- Status: PARTIALLY FIXED — softened to "at least three mismatches relevant to resource management policy." Still no derivation of completeness.
- **OPEN**: Consider adding one sentence on why three (they correspond to three directions of information flow: forward in time, upward in stack, downward to device).

**Q8: I2 is a taxonomy, not an insight**
"Physical separation manifests as at least three mismatches" describes WHAT you found, not WHY something happens. The hedge "at least three" further undermines conviction. The real insight ("no policy can be expressed without addressing all three") is buried at the end.
- Status: OPEN — consider merging I1 and I2 into one insight

**Q9: No single policy exercises all three mechanisms simultaneously**
GNN uses L1+L2. vLLM uses L1+L3. Device-side BPF is instrumentation only, not combined with L1+L2 for end-to-end gains. Evidence for the three mismatches is per-mismatch, not joint.
- Status: ACKNOWLEDGED in weakness note. No fix possible without new experiments.

**Q10: Insight fails at high oversubscription**
I1 claims co-location breaks for GPU. Predicted consequence: crossing boundaries yields gains. But at 1.84x, crossing more boundaries doesn't help (F3). The insight's prediction is regime-dependent. The intro doesn't acknowledge this tension.
- Status: FIXED — added sentence: "co-location mismatch is real but exploitability is bounded by PCIe bandwidth"

**Q11: "Agents exist in adjacent domains" ≠ "GPU needs kernel extensibility"**
SchedCP does CPU scheduling, AlphaEvolve does TPU layout, Glia does vLLM config. None do GPU kernel-level policy. The extrapolation from "agents optimize adjacent systems" to "GPU needs kernel extensibility for agents" is a logical gap.
- Status: **RESOLVED.** (1) Agents DO write systems code — AlphaEvolve, ADRS/Barbarians, PolicySmith, NECC all generate actual code, not configs. See `reviews/agent_code_vs_config.md`. (2) Our own eval IS code generation — 59 BPF programs, not parameter tuples. gpu_ext is the first system where an agent generates BPF code for GPU resource management. The "hypothesis" qualifier can be removed.

**Q12: I2 → I3 logical bridge is implicit**
I2 says three mismatches exist. I3 says sync callbacks can't cross them. Connection not stated. Should be: "Because physical separation creates these three mismatches, a synchronous callback — operating in a single timescale, single layer, without device visibility — cannot cross any of them."
- Status: FIXED — I1 and I2 merged, bridge is now in I2 text

---

## Category / Classification Issues

**Q13: Agent is "why now" motivation, not background or observation**
Agent trend is neither a GPU domain fact (background) nor a specific measurement (observation). It's a contemporary motivation. Most appropriate in Problem Statement as "why now."
- Status: DEBATED — moved back to Background B5. Problem Statement already has "growing agent-driven exploration." Potential redundancy.

**Q14: SIMT warp-divergence is a design constraint, not a motivating observation**
O1-O3 establish "why extensibility matters." SIMT establishes "why verifier design is hard." Different category. Sudden tonal shift.
- Status: FIXED — moved to Design Constraints section

**Q15: Findings 3, 5, 7, 8 (old numbering) are experimental findings, not world-observations**
"All prefetch strategies are zero-sum at 1.84x" and "always_max beats all sophisticated alternatives" are things discovered by building gpu_ext, not facts about the world.
- Status: FIXED — three-way split: Observations / Insights / Findings

**Q16: Constraint 2 (NUMA comparison) is not a constraint — it's restating I1**
"GPU is different from NUMA" is an observation or part of the insight's argument, not a constraint on the design. A design constraint should be something like "BPF verifier must terminate in bounded time."
- Status: FIXED — C2 rewritten as actual constraint: "existing CPU extensibility patterns cannot be directly reused"

---

## Missing Content

**Q17: No finding about baseline mechanism overhead**
The system adds BPF hooks to the GPU fault path. What's the cost when no policy is loaded? When a trivial policy runs? Reviewer will ask.
- Status: FIXED — F4 added: "<2% overhead with no-op policy"

**Q18: No finding about agent-discovered policy quality**
O3/B5 motivates with agents. F2 says 59 configs 0 panics. But did the agent find anything a human wouldn't? If not, say so.
- Status: UPDATED — ALL policies are agent-written (no hand-written baseline). The 2.60x/3.36x/P99 results ARE from agent-generated BPF code. Agent is the policy developer, not a comparator.

**Q19: No observation about open-source driver as enabling condition**
The entire system depends on NVIDIA's 2022 open-source kernel modules. This explains "why now" — paper couldn't be written in 2020.
- Status: FIXED — B4

**Q20: "0 panics" is the expected BPF guarantee, not a finding**
BPF already guarantees safety by design. "0 panics" is expected. The actual finding should be that this guarantee holds when extended to the GPU driver fault path.
- Status: FIXED — F2 note: "the finding is that this guarantee holds when extended to GPU driver fault path"

**Q21: No observation about oversubscription-dependence**
Policy effectiveness depends heavily on oversub ratio: 1.84x → near-zero, 1.34x → 2.60x. This defines the system's operating envelope. Its absence is a significant gap.
- Status: FIXED — F1 and F3 both address this

**Q22: I1 NUMA defense dilutes the insight**
I1 core claim is strong but spends half the paragraph on NUMA comparison. The defense belongs elsewhere (footnote, constraint, later paragraph). Mixing insight statement with preemptive defense dilutes rhetorical force.
- Status: PARTIALLY FIXED — NUMA moved to Design Constraints, but still also in I1 as one sentence

---

## Self-Defeating Arguments

**Q23: "Richer return type would reconstruct our system under a different name"**
If true, the contribution is a specific implementation, not an architectural insight. If someone achieves the same with rich return structs, the insight is about implementation convenience, not impossibility. Need to argue the callback *genuinely cannot* express the policy (returns before DMA completes, can't react to outcome).
- Status: FIXED — I3/I2 now uses concrete impossibility arguments, removed "reconstruct our system" framing

**Q24: Internal self-critiques should not appear in published text**
Weakness notes like "[Weakness (yunwei37): ...]" and "[Note: this is a case study, not a general proof...]" — either fix the finding or remove it. Don't publish your own internal review as parentheticals.
- Status: ACKNOWLEDGED — these are internal notes in a planning document, not intended for the paper itself

---

## Summary: Top 4 Unresolved Issues (Q11 resolved)

1. **Q7/Q8: Three mismatches — taxonomy or insight?** No derivation of completeness. Consider merging I1+I2 or adding a completeness argument.
2. **Q9: No joint L1+L2+L3 policy.** Cannot fix without new experiments.
3. **Q6: F1 data weakness.** +27% is one workload. Need broader evidence.
4. **Q13: Agent placement.** Still bouncing between Background and Observations. Best home: Problem Statement "why now" only.

~~5. Q11: Agent → GPU extensibility gap.~~ **RESOLVED** — agents DO write systems code (AlphaEvolve, ADRS, PolicySmith, NECC), and our eval IS code generation (59 BPF programs). See `reviews/agent_code_vs_config.md`.
