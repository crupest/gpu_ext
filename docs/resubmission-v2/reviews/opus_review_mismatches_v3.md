# Opus Review: Two Mismatches V3 (2026-03-30)

## Verdict: V3 is the correct choice. Borderline accept.

"V3 is the only version I would not actively attack."

## Independence test: PASS

- Timescale without information: coherent coprocessor with same ISA, DMA still takes ms. Plausible.
- Information without timescale: zero-latency interconnect to opaque device. Plausible.
- Genuinely independent. Strongest property V3 has over V1 and V2.

## Issues to fix

### 1. Upward information attribution is WRONG
App→driver gap is from kernel software layering, not architectural separation. sched_ext also lacks app intent without uprobes. This is not GPU-specific.
- Fix: either (a) split downward (GPU-specific, from architectural separation) and upward (generic, from kernel layering), or (b) drop the claim that information derives from "architectural separation."

### 2. Fault-handler nexus insight needs more prominence
"Put hooks in the fault handler, not in separate memory and scheduling subsystems" is one of the paper's most concrete prescriptive claims. Currently buried as a sub-bullet under timescale's design implication. Promote to P5 or contribution list.

### 3. No joint evaluation across both mismatches
GNN uses L1+L2 (timescale mechanisms). vLLM uses L1+L3 (information mechanisms, sort of). No workload demonstrates crossing both simultaneously.

### 4. "Any PCIe/CXL accelerator" generalization unsupported
Paper evaluates one GPU from one vendor. Either remove or add disclaimer.

### 5. "Information persists with zero-latency interconnect" needs nuance
Grace Hopper has coherent shared memory. Could host inspect GPU state via shared-memory instrumentation without device-side BPF? "Different ISA" is necessary but not sufficient — need to argue ISA difference makes state *uninterpretable*, not just *unreachable*.

### 6. P1-P4 still unsettled
Mismatch analysis is one component. Intro framing remains in flux with multiple competing options. "Stop debating and commit."

## V1 vs V2 vs V3

| Version | Fatal flaw | Verdict |
|---------|-----------|---------|
| V1 (timescale, information, visibility) | Visibility ⊂ information | Weak reject |
| V2 (timescale, info-bidirectional, structural) | Structural ⊂ timescale | Weak reject |
| V3 (timescale, information) | Upward info attribution fixable | **Borderline accept** |
