# Agent Framing Decision for SOSP Resubmission

Date: 2026-03-28 (revised)

## Context

Andi proposed making AI agents the core motivation: "agents hallucinate → eBPF provides safety → rethink eBPF for GPU." This would require comprehensive agent evaluation (N=3 case studies + 1 safety violation demonstration).

After analysis, we discovered we already have the data Andi asked for — it fell out of our development process naturally.

## What we actually have

| What Andi asked for | Do we have it? | Where |
|---|---|---|
| N=3 case studies | **Yes** | FAISS (5 iterations), preemption (6 iterations), cross-block (6 iterations) |
| Safety violation without eBPF | **Yes** (counterfactual) | move_head Xid 31 → kernel panic without BPF; hash map timeout → same |
| Agent is effective | **Yes** | Agent-developed policies achieved 3.36x GNN, -31.8% FAISS, P99 -95% |
| Agent is safe with eBPF | **Yes** | 50 events, 0 kernel panics, 0 data corruption |
| Quantitative metrics | **Yes** (Q6 pending) | 42 sessions, 448 MB transcript, 59+ configs, per-session token counts |

We don't need 2-4 weeks to collect data. It's already analyzed (Q1-Q6 reports in `docs/gpu-ext/eval/agent/`).

## Decision: Agent as Empirical Evidence, Not Backdrop and Not Core Motivation

### The three options

| | Agent as core motivation (Andi's original) | Agent as backdrop (our first revision) | **Agent as empirical evidence (current decision)** |
|---|---|---|---|
| What it claims | "We enable agents to safely optimize GPU" | "In an era of agents, safety matters" | "eBPF containment enables iterative policy exploration; we demonstrate this through AI-assisted development" |
| Needs eval? | Full agent benchmark | No | Case study (which we already have) |
| Agent is... | The motivation | A subordinate clause | The evidence |
| Core contribution | Agent framework | Composable pipelines | Composable pipelines |
| Risk | "You didn't eval agents properly" | Missed opportunity | Low — data backs the claim |

### Why "empirical evidence" is the right level

1. **Agent case studies directly validate capability progression.** Each case study maps to a different L1→L2→L3 layer:
   - Case C (cross-block): Agent needed L2 (bpf_wq + migrate_range) because L1 prefetch was intra-block only
   - Case A (FAISS phase): Agent needed L2 (async phase-gated DMA) because phase detection requires workload-specific execution strategy
   - Case B (preemption): Agent discovered L3 (sleepable uprobe → kfunc) as a better legal bridge than L2 (struct_ops → bpf_wq → kfunc)

   The exploration process IS evidence that each capability layer is necessary — the agent couldn't solve the problem until it reached the right layer.

2. **Containment argument strengthens "why eBPF?"** — the hardest question for the paper:
   - Without BPF: each failed policy = driver rebuild + potential crash → hours per iteration
   - With BPF: each failed policy = unload + reload → seconds per iteration
   - This velocity difference is what makes iterating through 59+ configurations feasible
   - This answers "why not just patch the driver?" directly: because 75% of your attempts will be wrong, and you need each failure to be cheap

3. **It's honest about what we did.** gpu_ext was actually developed this way. Not mentioning it hides our methodology. Showing it as evidence is more transparent than either overpromising (Andi's "agents as motivation") or underplaying (backdrop).

### Why NOT core motivation (still holds)

- Our core contribution is still the architectural insight (composable multi-mechanism pipelines for heterogeneous subsystems)
- We do NOT claim agent autonomy — human directed strategy, agent executed and iterated
- We do NOT claim verifier catches all errors — 24 logic bugs loaded and ran
- Making agents the motivation would make the paper about agents, not about OS extensibility

### The precise claim line (updated)

| Can say (evidence-backed) | Still cannot say (unsupported claim) |
|---|---|
| "GPU policy exploration is inherently iterative — our experience shows >75% of configurations fail" | "AI agents can autonomously discover optimal GPU policies" |
| "eBPF containment ensures all 50 documented failures were recoverable — no kernel panics, no data corruption" | "eBPF verification guarantees safety of agent-generated policies" |
| "Each convergence arc traces a path through the capability progression, empirically validating why L2/L3 mechanisms are necessary" | "Agents need our specific system to be effective" |
| "Development velocity: seconds per policy iteration (BPF hot-swap) vs hours (driver rebuild)" | "Agents are faster than humans at GPU policy development" |

Left column = empirical observation backed by data. Right column = causal/comparative claim requiring controlled experiment.

## Integration Plan (Revised)

### Abstract (1 sentence)
> "We validate gpu\_ext's containment properties through AI-assisted policy development: across 59+ configurations, all 50 documented safety events were recoverable, with zero kernel panics or data corruption."

This is a result, not a motivation. It goes after the performance numbers.

### Intro P1 (2-3 sentences, empirical observation)
> "GPU policy exploration is inherently iterative: the right policy depends on workload, hardware, and deployment, and most candidate policies fail. Our development experience confirms this — across 59+ policy configurations tested over 6 weeks, 75% produced neutral or negative results. Safe, dynamically extensible frameworks are essential to make this exploration feasible, whether the explorer is a human expert, an automated tuning system, or an AI coding agent."

This is NOT backdrop (passive observation about the era). It is an EMPIRICAL OBSERVATION from our own work that motivates the technical problem. Then pivot to 73% surprising observation.

### Intro P8 (additional contribution bullet)
> "We validate eBPF's containment model through AI-assisted policy development: 3 convergence arcs across 59+ configurations demonstrate that each capability layer (advisory hooks, async execution, proactive app-boundary hooks) is necessary, and that all 50 documented safety events were recoverable without kernel panics or data loss (§6.N)."

### Background
Mention development velocity as one of four "why eBPF?" properties:
- kernel-resident (policy must run inside fault handler)
- safe (containment bounds blast radius of bad policies)
- dynamic (hot-swap policies in seconds, not hours — enabling iterative exploration)
- composable (multi-mechanism pipelines for diverse execution strategies)

The "dynamic" property naturally references iterative exploration without making agents the subject.

### Design
No change. Design is driven by timescale mismatch + information mismatch. Agent evidence appears in eval, not design.

> **[CRITICAL REVIEW NOTE]** 1-1.5 pages is too much for agent content in a system design paper. Agent case studies prove methodology (safe development process), not system design (architecture correctness). SOSP reviewers care about the latter. Recommend 0.5 pages total: 1 safety table + 1 velocity paragraph + 1 representative case study paragraph + 1 counterfactual sentence. Full 3 case studies → technical report / appendix. See `critical_review.md` §II Problem 4.

### Eval §6.N — "Iterative Policy Exploration Under eBPF Containment" (~1-1.5 pages)

**Framing:** This section validates two claims: (1) eBPF containment makes iterative GPU policy exploration safe, and (2) the capability progression (L1→L2→L3) is empirically necessary — each case study shows the agent couldn't converge until it reached the right capability layer.

**Structure:**

1. **Setup paragraph** (~0.25 page): gpu_ext developed with AI agent (Claude Code) as primary policy author. 42 sessions, 6 weeks, 59+ configurations across 4 workloads. >75% of configurations neutral or negative.

2. **Safety events table** (~0.25 page): The 50-event taxonomy table (7 categories) + the punchline: 0 kernel panics, 0 data corruption, 0 irrecoverable state. Recovery profile: 60% in minutes, 14% requires tool, 0 requires reboot for BPF-related events.

3. **Three convergence arcs** (~0.5 page, each 1 paragraph):

   **Case A — FAISS phase-adaptive (validates L2: async execution strategy):**
   Agent iterated 5 times: wrong phase classifier → fixed classifier but wrong eviction → isolated cause (cycle_moe harmful for search) → converged to phase-gated cross-block with default_lru. The key insight — that the execution strategy (which eviction, which phase gate) is workload-dependent — could only be discovered through iterative exploration. BPF containment: stale struct_ops recovered with cleanup tool; logic bug in D4 (stuck state machine) caused no crash, fixed in next iteration.

   **Case B — GPU preemption kfunc (validates L3: sleepable uprobe → kfunc):**
   Agent iterated 6 mechanisms: sched_ext → ioctl → struct_ops+bpf_wq+kfunc → sleepable uprobe+kfunc. Verifier rejected pointer arithmetic (load-time catch). Verifier enforced sleepable/non-sleepable boundary (forced bpf_wq trampoline, then agent discovered uprobe.s as better legal bridge). Without BPF type system enforcement, the sleepable-context violation would be a kernel deadlock, not a load-time error.

   **Case C — Cross-block multi-stride (validates L2: bpf_wq + migrate_range):**
   Agent tested K=1 through K=6 lookahead. K>1 all failed (PCIe overload, -46%). Agent didn't abandon after first failure — ran K=2, K=3 to confirm issue was structural, not parametric. Converged to K=1 direction-aware (+21% over intra-block). Safety: stale struct_ops recovered with cleanup; benchmark killed with code 137, agent stopped rather than waste GPU time.

4. **Development velocity paragraph** (~0.15 page):
   > BPF hot-swap enables seconds-per-iteration policy exploration: load a new .bpf.c, run a benchmark, unload if wrong. The same exploration via driver modification would require: edit driver source → rebuild kernel module → reload driver stack → restart workload → run benchmark — hours per iteration. At 75% failure rate across 59+ configurations, the cumulative time difference is the practical enabler of the search.

5. **Counterfactual paragraph** (~0.1 page):
   > Two of the 50 safety events (move_head race → Xid 31, hash map timeout → Xid 31) would likely cause kernel panics or page table corruption if they occurred in a direct driver modification rather than a BPF program. The BPF containment model — load-time verification, runtime fault isolation, and clean policy detachment — converts these from catastrophic failures into recoverable ones.

### Discussion (2-3 paragraphs, expanded)

1. **Generalization:** "gpu_ext demonstrates that eBPF's containment model extends beyond CPU subsystems to heterogeneous, async, cross-domain resources. The same properties that enabled safe iterative exploration of GPU policies — load-time verification, runtime isolation, hot-swappable detachment — apply to CXL memory tiering, DPU offload, and NPU scheduling policies, where the execution strategy is similarly workload-dependent and exploration-intensive."

2. **Agent implications:** "Our AI-assisted development experience (§6.N) suggests that eBPF-style frameworks are well-suited as substrates for automated policy optimization. The key property is not that the verifier catches every error — it does not (24 of 50 events were semantic bugs that loaded successfully) — but that the containment model bounds the blast radius of every error. This distinction between rejection-based safety (verifier catches bugs at load time) and containment-based safety (framework limits damage at runtime) is important for automated exploration, where the error rate is inherently high."

3. **Honest limitations:** "eBPF cannot prevent semantically incorrect policies from running. A policy that evicts the wrong pages or prefetches the wrong VA blocks will load, execute, and degrade performance — the verifier only ensures it cannot corrupt kernel state. Future work could combine load-time verification with runtime semantic checks (e.g., performance regression detection with automatic rollback) to further tighten the safety envelope."

## What this achieves

**Answers Andi's "why eBPF?" question directly:** Because 75% of your policy attempts will be wrong, and eBPF makes each failure cheap and safe. This applies whether the author is human or AI.

**Satisfies Andi's N=3 + safety violation requirement:** 3 case studies with full arcs + 2 Xid faults with counterfactual analysis + 2 verifier rejections.

**Doesn't trigger "you motivated with agents but didn't eval agents":** Because agents are evidence, not motivation. The motivation is technical (timescale mismatch, information mismatch, execution strategy). The agent section is in eval, not intro.

**Adds ~1.5 pages to the paper** (§6.N + expanded discussion). This is feasible within SOSP page limits if we compress device-side eval (currently ~1.5 pages of microbenchmarks) or fold §3 Design Principles into §4 intro.

## Data availability

All evidence is preserved and analyzed:
- `docs/gpu-ext/eval/agent/q1_git_archaeology.md` — commit statistics, BPF file evolution
- `docs/gpu-ext/eval/agent/q2_safety_taxonomy.md` — 50 events with full context
- `docs/gpu-ext/eval/agent/q3_case_studies.md` — 3 detailed convergence arcs
- `docs/gpu-ext/eval/agent/q4_session_exploration_log.md` — per-session metrics
- `docs/gpu-ext/eval/agent/q5_safety_events_from_sessions.md` — session transcript evidence
- `docs/gpu-ext/eval/agent/q6_precise_metrics.md` — tokens, time, commands (pending)
- `docs/gpu-ext/eval/agent_session_analysis.md` — session data overview
- `docs/gpu-ext/eval/agent_codebase_analysis.md` — repo-wide evidence
- `docs/gpu-ext/resubmission-v2/case_study_agent_exploration.md` — paper-ready case study draft
- Raw session transcripts: 42 sessions, 448 MB in `~/.claude/projects/` (available on request)
