# Opus Review: Revised Three Mismatches v2 (2026-03-30)

## Verdict: honest count is two mismatches, not three

### Per-mismatch assessment

**Timescale**: Clean derivation from physical separation. Effects cross interconnect → ms. OK.

**Information (bidirectional)**:
- Downward (host can't observe device): derives from physical separation. Clean.
- Upward (driver lacks app intent): does NOT derive from physical separation. This is a software layering problem (sched_ext has the same issue on CPU). Smuggling a vertical problem into a horizontal framing.

**Structural**: NOT independent of timescale. Causal chain: physical separation → DMA required → DMA takes time → stalls GPU → scheduling affected. This shares the same path as timescale mismatch up to the last step. Test: if DMA were instantaneous, structural mismatch disappears → it's a corollary, not independent.

### What works in v2

- Folding visibility into information (downward direction) is honest
- "Hooks in the fault handler" is a concrete mechanism (significant improvement over "unified interface")
- Device-side BPF adequately motivated as downward mechanism under information mismatch
- "Per-subsystem extensibility model breaks" is a real design insight (better than "coupling exists")

### What doesn't work

- Causal unity not achieved: timescale = clean, information upward = fabricated, structural = corollary of timescale
- "Three orthogonal dimensions" claim is false (structural not orthogonal to temporal)
- Structural mismatch needs quantitative evidence (e.g., cite GNN cross-cutting policy result)
- No numbers cited in mismatch statements

### Recommendation

Two honest mismatches + one design implication:
1. **Timescale mismatch** (independent)
2. **Information asymmetry** (independent, bidirectional)
3. Structural coupling as **design implication** of the first two, not a third mismatch

Or: find a genuinely independent third mismatch.
