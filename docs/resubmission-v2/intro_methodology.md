# SOSP/OSDI Intro & Abstract Writing Methodology

## Core Principle

A SOSP/OSDI intro is an **argument**, not a feature list. One insight drives the entire paper. Every paragraph advances a single logical chain.

---

## Intro Structure (7 paragraphs, one function each)

| Para | Function | Key question |
|------|----------|-------------|
| P1 | **Why care** | Why is this domain important? Why now? |
| P2 | **What's broken** | What is the root cause of the problem? (not "what's bad" — "why is it bad") |
| P3 | **Why it's hard** | Why doesn't the obvious solution work? Preempt "just do X" rebuttals |
| P4 | **Our insight** | One sentence of architectural understanding a reviewer will quote |
| P5 | **Our system** | Design based on the insight; each mechanism answers one problem from P2/P3 |
| P6 | **Evidence** | Data proving the insight is correct and the system works |
| P7 | **Contributions** | 3 bullets: insight + system + evaluation |

---

## Per-Paragraph Rules

### P1 (Why care)
- Sentence 1 = domain fact (no citation needed)
- Sentence 2 = specific problem (quantified)
- Sentence 3 = "why now" (what changed to make the problem solvable/urgent)
- **Don't**: mix motivation with contribution

### P2 (What's broken)
- State the **root cause**, not the symptom
- Good P2: "X assumes Y, but Z breaks Y" (contrastive insight)
- Bad P2: "X is not extensible" (complaint, not insight)
- Bad P2: "X lacks A, B, C" (checklist — reviewer says "post-hoc requirements")

### P3 (Why hard)
- Preempt the most obvious rebuttal: "why not just use existing tool X?"
- Structure: acknowledge X's value -> specific shortcoming -> data/example
- **Don't**: attack prior work as wrong. Say "insufficient for this specific problem"

### P4 (Insight)
- **One sentence** that a championing reviewer quotes in the PC meeting
- Good insight = **explanatory** + **predictive**: explains what you observe AND predicts unseen scenarios
- Good: "Physical separation between host and accelerator breaks the co-location assumption"
- Bad: "We identify three challenges" (taxonomy, not insight)
- Bad: "We propose a new system" (contribution, not insight)

### P5 (System)
- Each mechanism maps directly to one problem from P2/P3
- Only say WHAT and WHY, not HOW (HOW goes in Section 3/4)
- One sentence per mechanism

### P6 (Evidence)
- Data must **validate the insight**, not just show "system is fast"
- Good: "Crossing boundary X yields +27%, confirming that co-location mismatch limits performance"
- Bad: "+27% speedup on workload Y" (no connection back to insight)
- **Must include limitation**: under what conditions does the system NOT help

### P7 (Contributions)
- 3 bullets, no more, no less
- Typically: (1) insight/analysis (2) system design + implementation (3) evaluation
- **Don't**: write "to the best of our knowledge, this is the first..."

---

## Abstract (5 sentences)

1. **Problem**: one sentence on domain and problem
2. **Why hard**: one sentence on why existing approaches fail
3. **Insight**: one sentence (same as P4)
4. **System**: one sentence on what you built
5. **Results**: strongest number + limitation

---

## Common Reject Patterns

| Pattern | Reviewer response |
|---------|------------------|
| Checklist P2 ("lacks A, B, C") | "Post-hoc requirements designed so only their system satisfies them" |
| Taxonomy insight ("three challenges") | "Classification, not contribution" |
| Only positive results | "Cherry-picked; where does the system NOT help?" |
| Motivation != Evaluation | P1 says agents, eval only has benchmarks -> "bait and switch" |
| "First to do X" | "Novelty is not a contribution; understanding is" |
| Too many mechanisms | "Engineering contribution, not research" |

---

## One-Sentence Test

After writing the intro, ask: **if a reviewer remembers only one sentence, which is it?**

- If that sentence is "we built a system that..." -> intro failed
- If that sentence is "X breaks Y, and crossing Z recovers..." -> intro succeeded

---

## Applying to gpu_ext

| Para | gpu_ext content | Status |
|------|----------------|--------|
| P1 | GPU policies matter, workload-dependent, agents exploring | OK |
| P2 | Root cause = physical separation breaks co-location | Needs clean rewrite |
| P3 | struct_ops is starting point but sync callbacks insufficient | OK |
| P4 | "Physical separation breaks co-location -> three mismatches -> each needs distinct mechanism" | OPEN: taxonomy vs insight concern |
| P5 | Three mechanisms (advisory hooks, sleepable kfuncs+bpf_wq, uprobes) + SIMT verifier | OK |
| P6 | L1 2.60x, L1+L2 3.36x, L3 P99 -9.5%, 59 configs 0 panics, limitation at 1.84x | Needs regime-dependence |
| P7 | (1) co-location analysis (2) gpu_ext system (3) evaluation across 4 workloads | OK |

### gpu_ext one-sentence test

Target: "Physical separation between host and GPU breaks the co-location assumption underlying CPU extensibility frameworks, requiring three composable mechanisms — each crossing a distinct boundary — to recover performance that no single-layer interface can express."
