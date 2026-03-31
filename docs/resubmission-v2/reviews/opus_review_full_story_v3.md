# Opus Review: Full Intro Story V3 (2026-03-30)

**Verdict: Weak Accept (borderline)**

## Strengths

- S1: Problem is real and well-motivated (73% execution time, no programmable OS interface)
- S2: Co-location insight is architecturally deep, generalizable to PCIe/CXL accelerators
- S3: Capability progression well-demonstrated (L1 2.60x → L1+L2 3.36x → L1+L3 P99 -9.5%)
- S4: Agent framing is timely and honest — ALL policies agent-written, strengthens system contribution
- S5: Honest negative result at 1.84x oversub increases credibility
- S6: New intro substantially better than old (co-location insight, logical progression, specific evidence)

## Weaknesses

- W1: tex P4 still claims THREE mismatches but intro.md V3 says honest count is TWO. Inconsistency.
- W2: Memory-scheduling coupling claim unsupported — no experiment compares unified vs separate hooks.
- W3: Device-side BPF has no end-to-end performance result. Positioned as first-class but under-delivered.
- W4: No single workload uses all three mechanism layers together. Composability unvalidated.
- W5: Agent claim under-delivered — no comparison, no ablation, no search strategy analysis. 59 policies may be human-in-the-loop LLM iteration, not autonomous agent operation.
- W6: 4.8x number in abstract not contextualized (doesn't appear in capability progression which peaks at 3.36x).
- W7: P4 is too dense — co-location insight, mismatches, insufficient callbacks all in one paragraph.
- W8: "Richer callback" counterargument missing from tex.

## Questions for Authors

- Q1: Can you measure the cost of decoupling memory and scheduling hooks?
- Q2: What concrete policy uses device-side observation for end-to-end performance?
- Q3: Were the 59 policies generated autonomously or with human-in-the-loop iteration?
- Q4: What oversubscription regime makes L2 (async) critical? Narrow band or wide range?
- Q5: Any validation on non-GPU accelerator (FPGA, SmartNIC, TPU)?
- Q6: Paper says three mismatches, analysis says two. Which is the position?
- Q7: Are uprobes about timescale ("too late") or information (app intent)?

## Specific Suggestions

- SG1: Commit to two mismatches (V3). Rewrite P4.
- SG2: Split P4 into two paragraphs (insight + mechanisms).
- SG3: Add "richer callback" rebuttal.
- SG4: Downweight device-side BPF if no end-to-end result.
- SG5: Resolve uprobe attribution (timescale vs information).
- SG6: Contextualize 4.8x claim.
- SG7: Tone down agent framing in P6 — "AI agent generating the policy code" not "AI agents generated and deployed."
- SG8: Add L1+L2+L3 experiment if possible.
- SG9: Sharpen P1 "why now" — NVIDIA open-source 2022 + agents writing code.
- SG10: Add a schematic figure for P4-P5 mechanism layers.

## Comparison to Old Intro

Old intro would be desk-rejected ("engineering paper, just applied eBPF to GPU driver"). New intro has a fighting chance because of co-location insight, capability progression, and agent framing. But new intro has its own problems (W1-W8).

## Path to Solid Accept

Fix W1 (commit to two mismatches), W3 (honest about device-side BPF scope), W5 (precise agent claim). The co-location insight, capability progression, and honest negative result are the paper's strongest assets.
