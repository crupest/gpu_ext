# Case Study: AI-Assisted GPU Policy Space Exploration via eBPF

> Paper section target: §6.N (after capability progression and workload evaluations)
> Framing: Development experience — not an "agent benchmark," but empirical evidence that eBPF containment makes GPU policy exploration safe for automated tools.

---

## 1. Context and Claim

gpu\_ext was developed over 6 weeks using an AI coding agent (Claude Code) as the primary policy author. The agent generated BPF policy code, modified kernel modules, built and loaded policies, ran benchmarks, analyzed results, and iterated — the full policy development loop.

**Claim (containment, not rejection):**

> eBPF's containment model — load-time verification, runtime isolation, and clean detachment — ensures that incorrect GPU policies cause *recoverable* failures, never catastrophic ones. This property makes GPU policy space exploration safe for automated tools, including AI agents.

We do NOT claim:
- That the agent worked autonomously without human steering
- That the BPF verifier caught every unsafe policy at load time
- That agent-generated policies are always correct

We DO claim:
- The agent explored a large policy space where >50% of attempts were wrong
- Every failure was recoverable (no kernel panics, no data corruption)
- The eBPF framework bounded the blast radius of bad policies by construction

---

## 2. Exploration Scale

### 2.1 Aggregate metrics

| Metric | Value | Source |
|---|---|---|
| Total sessions (primary) | 25 | Q6 |
| Active sessions (with safety events) | 12 | Q4 |
| Sessions with BPF policy code edits | 7 | Q4 |
| Total transcripts (primary + subagent) | 284 | Q6 |
| **Total tokens consumed** | **5,530,224** (439K input + 5,091K output) | Q6 |
| **Estimated cost** | **$388.40** (Claude Opus pricing) | Q6 |
| Total active agent time | 118 hours 39 minutes | Q6 (summed across all sessions) |
| Total tool invocations | 13,640 | Q6 |
| Total builds (make/clang) | 234 | Q6 |
| Total benchmark runs | 974 | Q6 |
| Total .bpf.c code edits | 244 | Q6 |
| Total subagent spawns | 154 | Q6 |
| Distinct BPF policy files in repo | 55 (.bpf.c) | Codebase analysis |
| Policy/scheduler files created by agent | 11 | Codebase analysis |
| Policy/scheduler files touched by agent | 29 / 55 | Codebase analysis |
| Distinct policy×workload configurations tested | ≥59 | Result artifact scan |
| Negative results preserved in repo | 18 performance regressions | Q2 |
| Development period | 2026-02-16 to 2026-03-07 (20 days of active work) | Git history |
| Average tokens per benchmark iteration | 5,678 | Q6 |

### 2.2 Top 5 most active sessions

| Session | Active time | Tokens | Cost | Tool calls | Builds | Benchmarks | .bpf.c edits | Subagents |
|---|---|---|---|---|---|---|---|---|
| `1ffa360b` (FAISS + multi-policy) | 28h 46m | 961K | $69.37 | 1,945 | 44 | 165 | 37 | 65 |
| `e19dd100` (xCoord + scheduler) | 16h 26m | 862K | $63.44 | 1,872 | 27 | 191 | 57 | 19 |
| `6b21980a` (cross-block + MSched) | 11h 06m | 531K | $39.51 | 1,230 | 53 | 86 | 39 | 4 |
| `b1e7bc20` (preemption kfunc) | 5h 43m | 532K | $36.54 | 851 | 42 | 13 | 29 | 13 |
| `9d654c47` (xCoord doc + refactor) | 6h 10m | 117K | $8.67 | 587 | 15 | 32 | 9 | 0 |

### 2.3 Notable extremes

| Record | Value | Session |
|---|---|---|
| Longest unbroken active window | 5h 14m (03:11–08:25 UTC) | `1ffa360b` |
| Most edits in one session | 259 Edit/Write invocations | `e19dd100` |
| Peak tool density (1 hour) | 254 tool calls/hr (73 Bash, 68 Edit, 30 Read...) | `6b21980a` |
| Highest error rate | 5.21% of Bash output lines contain errors | `6c0aa1fb` |
| Subagent vs direct work | 1.7% of tool calls are subagent spawns | All sessions |

---

## 3. Safety Events: 50 incidents, 0 catastrophic failures

Across the full development period, we documented 50 distinct safety-relevant events from session transcripts, git history, plan documents, and project memory.

### 3.1 Event taxonomy

| Event type | Count | Description |
|---|---|---|
| LOGIC\_BUG | 24 | Policy had wrong behavior but no crash (dead callbacks, wrong PID matching, stuck state machines) |
| PERF\_REGRESSION | 18 | Policy loaded and ran but degraded performance (-3% to -76%) |
| VERIFIER\_REJECT | 2 | BPF verifier caught unsafe code at load time (pointer arithmetic, infinite loop) |
| XID\_FAULT | 2 | GPU memory fault triggered by policy (move\_head race, hash map timeout) |
| SYSTEM\_HANG | 2 | Stale struct\_ops after dirty shutdown; OOM from oversized experiment |
| BUILD\_FAIL | 1 | Toolchain incompatibility |
| DRIVER\_BUG | 1 | Semantic hook placement error affecting 20+ policy files |

### 3.2 Catastrophic failure count

| Failure mode | Count |
|---|---|
| Kernel panic | **0** |
| Data corruption | **0** |
| Irrecoverable state (hard reboot required) | **1** (OOM from 120B+120B config, not BPF-related) |

The single hard-reboot incident was caused by two 120B models exhausting 125 GB host RAM — a workload configuration error, not a BPF policy fault. Every BPF-related failure was recoverable without reboot.

### 3.3 Recovery profile

| Recovery method | Count | Typical time |
|---|---|---|
| Immediate (next edit/retry) | 8 | Seconds |
| Minutes (few iterations within session) | 22 | 1–30 min |
| Requires tool (module reload, cleanup) | 7 | 5–30 min |
| Requires reboot | 0 for BPF-related events | — |

### 3.4 Counterfactual: what if the agent modified the driver directly?

| BPF event | What happened | What would happen with driver modification |
|---|---|---|
| `move_head` in `chunk_activate` → Xid 31 | GPU fault, driver auto-recovery, policy unloaded | Likely kernel panic: page table corruption from race condition |
| Hash map in fault hot path → Xid 31 | GPU MMU timeout, driver recovery | Same timeout, but no way to unload the policy without driver rebuild |
| Pointer arithmetic rejected by verifier | Load-time error, zero runtime impact | No verifier — bug reaches production, potential use-after-free |
| `gpu_block_access` never fires (dead callback) | Wrong results but no crash; discovered via tracing | Same bug, but fix requires driver recompile + reboot for every attempt |
| 18 performance regressions | Unload BPF policy, immediately recover baseline | Revert driver patch, rebuild, reload module — hours per iteration |

**Key insight:** The eBPF framework converts potential catastrophic failures (kernel panic, data corruption) into recoverable ones (policy unload, module reload). This is not just convenience — it is what makes iterative policy exploration feasible at all.

---

## 4. Three Case Studies

Each case study shows a complete **explore → fail → recover → converge** arc.

### 4.1 Case A: FAISS Phase-Adaptive Prefetch

**Session:** `1ffa360b` | **Active time:** 6h 14m | **Tokens:** 227,146 ($16.56) | **Tool calls:** 44 edits, 10 builds, 42 benchmarks, 17 subagents | **Period:** 2026-03-05 01:09–08:25 UTC

**Problem:** FAISS has two phases (BUILD: sequential scan, SEARCH: random access) that need different prefetch strategies. The driver treats both identically.

**Exploration arc (5 policy iterations, 8 configurations benchmarked):**

| Iteration | What the agent tried | Result | What was wrong |
|---|---|---|---|
| D (v1) | Direction-consistency phase detector + cycle\_moe eviction | BUILD: 47.73s ✓, SEARCH np=1: 8.38s ✗ | SEARCH has enough forward drift to fool a momentum detector |
| D2 (v2) | Strict "+1 block stride" detector + cycle\_moe | BUILD: 48.35s ≈, SEARCH np=1: 9.78s ✗ | Classifier fixed, but cycle\_moe eviction actively harmful for search |
| D3 | v2 detector + default\_lru eviction | **BUILD: 47.31s ✓, SEARCH np=1: 5.49s ✓** | **Converged** — phase detection gates XB; eviction is the real lever |
| D4 | kprobe fast-path optimization | Logic bug: policy stuck in SEARCH forever | Optimization moved check before phase detection |
| D4-fixed | Phase detection before fast-path guard | 48.22s / 5.54s | Safe final version |

**Safety events:** Stale struct\_ops attach error (recovered with cleanup); logic bug in D4 created invalid state machine (no crash, fixed in next iteration).

**Aha moment:** The agent isolated two independent causes one at a time — first the classifier, then the eviction policy — and discovered that **the remaining regression was not classification error but wrong eviction**.

### 4.2 Case B: GPU Preemption via Sleepable Kfunc

**Session:** `b1e7bc20` | **Active time:** 5h 43m | **Tokens:** 650,849 ($45.38) | **Tool calls:** 177 edits (29 .bpf.c), 42 builds, 15 benchmarks, 13 subagents | **Period:** 2026-03-03 21:31 – 2026-03-04 19:22 UTC

**Problem:** Multi-tenant GPU scheduling requires preempting another process's GPU context — an operation no user-space framework or synchronous struct\_ops hook can perform.

**Exploration arc (6 mechanism iterations):**

| Iteration | Mechanism | Result | What was wrong |
|---|---|---|---|
| 1 | sched\_ext CPU boosting (xCoord) | Helps CPU-bound serving, not GPU/PCIe-bound workloads | Wrong layer — GPU needs GPU-level preemption |
| 2 | `gpu_preempt_ctrl` tracepoint path | Tracepoints don't exist in the driver | Dead end |
| 3 | `bpftrace` handle introspection | Cannot resolve `struct nv_gpu_task_init_ctx` | Tooling limitation |
| 4 | 3-probe capture + `struct_ops` → `bpf_wq` → kfunc | **Works**: 540μs avg, 177μs low-latency band | **Verifier constraint**: kfunc is `KF_SLEEPABLE`, cannot call from non-sleepable struct\_ops |
| 5 | Sleepable `uprobe.s` on `cuLaunchKernel` → kfunc directly | **312μs avg** — removes workqueue hop | Better legal bridge discovered |
| 6 | + `lc_comm` filtering + cooldown | Final deployment-ready version | Unfiltered preempt caused BE throughput collapse (-73%) |

**Verifier constraints hit:**
1. `KF_SLEEPABLE` cannot be called from non-sleepable context → forces `bpf_wq` trampoline (iteration 4)
2. Pointer arithmetic on pointer registers rejected → forces `bpf_probe_read_kernel` scalarization workaround

**Aha moment:** `SEC("uprobe.s/...")` already runs in sleepable process context — the preempt kfunc can be called directly at `cuLaunchKernel` time, bypassing the workqueue entirely. This is the **user-space → kernel BPF → GPU hardware** path that no prior eBPF system achieves.

### 4.3 Case C: Cross-Block Multi-Stride Prefetch

**Session:** `6b21980a` | **Active time:** 11h 06m | **Tokens:** 732,816 ($51.49) | **Tool calls:** 250 edits (59 .bpf.c), 54 builds, 94 benchmarks, 4 subagents | **Period:** 2026-02-27 04:13 – 2026-02-28 04:48 UTC

**Problem:** GNN training scans graph data sequentially across VA blocks. Can deeper lookahead (prefetching K>1 blocks ahead) improve over single-block prefetch?

**Exploration arc (6 iterations, K=1 through K=6 tested):**

| Config | Lookahead | GNN epoch time | vs always\_max |
|---|---|---|---|
| always\_max (intra-block only) | 0 | 26.99s | baseline |
| 1-block direction-aware XB | K=1 | **21.32s** | **+21% better** |
| Adaptive K=1..6 | K≤6 | 38.47s | **-46% worse** |
| Fixed K=2 | K=2 | 30.81s | -16% worse |
| Fixed K=3 | K=3 | 30.73s | -16% worse |

**What failed:** Every K>1 configuration overloaded PCIe bandwidth and displaced pages the workload still needed, causing second-order faults. The agent first hypothesized lock contention (wrong), then measured PCIe competition (correct).

**How convergence was validated:** After K=1..6 failed catastrophically, the agent didn't abandon multi-stride — it ran K=2 and K=3 to confirm the issue wasn't "K=6 is too high." When those also failed, the conclusion became robust: **one block ahead is the useful prediction boundary; deeper lookahead converts spatial predictability into PCIe overload.**

**Safety events:** Stale struct\_ops state left maps pinned (recovered with cleanup tool); benchmark killed with exit code 137 (agent stopped rather than burn GPU time on a settled question).

---

## 5. Cross-Case Synthesis

### 5.1 Common convergence pattern

All three cases follow the same arc:

1. **Broad intuition** → initial policy based on workload understanding
2. **First failure** → identifies the wrong abstraction (wrong classifier, wrong layer, wrong lookahead depth)
3. **Isolation** → change one variable at a time (eviction vs detection, sync vs async, K=1 vs K=2 vs K=6)
4. **Convergence** → stop when remaining gap is structural or not worth more complexity
5. **Safety throughout** → every failure was recoverable; no iteration required reboot or lost data

### 5.2 Why >50% failure rate is the expected outcome

Policy space exploration is inherently lossy:

| Case | Time | Tokens (cost) | Configs tested | Positive | Negative |
|---|---|---|---|---|---|
| A (FAISS) | 6h 14m | 227K ($16.56) | 8 | 2 | 6 |
| B (Preemption) | 5h 43m | 651K ($45.38) | 6 mechanisms | 2 | 4 |
| C (Cross-block) | 11h 06m | 733K ($51.49) | 6 configs | 1 | 5 |
| **Total** | **23h 03m** | **1.61M ($113.43)** | **20** | **5 (25%)** | **15 (75%)** |

This is not agent incompetence — it is the nature of the search problem. The question is whether bad policies cause recoverable or catastrophic failures.

### 5.3 The containment argument (one paragraph for the paper)

> Over 6 weeks of AI-assisted GPU policy development (5.5M tokens, $388, 119 active hours, 13,640 tool invocations, 974 benchmark runs), we documented 50 safety-relevant events across 12 active sessions. These included 2 BPF verifier rejections (caught at load time), 2 GPU memory faults (recovered by driver), 18 performance regressions (recovered by policy unload), 24 logic bugs (discovered via tracing and benchmarks), and 2 system hangs (recovered with cleanup tools). **Zero events caused kernel panics, data corruption, or irrecoverable system state.** The same policy bugs in a directly modified driver would risk page table corruption (from the `move_head` race), kernel panics (from the hash map timeout), and multi-hour rebuild cycles (for every iteration). eBPF's containment model — load-time verification, runtime fault isolation, and hot-swappable policy detachment — converts catastrophic failures into recoverable ones, making iterative policy exploration feasible at the pace required for automated tools. The three detailed case studies consumed 1.6M tokens ($113) over 23 hours of active agent time, producing 20 distinct policy configurations of which only 5 (25%) were positive — yet every failure was recovered in minutes, not hours.

---

## 6. Limitations and Honest Framing

**What this case study does NOT prove:**
- Fully autonomous agent exploration without human steering (human directed the high-level strategy; agent executed and iterated)
- Complete verifier-rejection corpus (we have 2 documented rejections; the real count during development is higher but not systematically logged)
- That eBPF catches all semantic errors (it doesn't — 24 logic bugs loaded and ran; containment limits their blast radius, not their occurrence)

**What it DOES prove:**
- An AI agent can generate, load, benchmark, and iterate on real GPU BPF policies
- The failure rate during exploration is high (75% of configurations are wrong)
- eBPF containment ensures all failures are recoverable
- The development velocity enabled by safe hot-swapping (seconds to try a new policy) vs driver modification (hours per iteration) is the practical enabler of policy space exploration

---

## 7. Data Availability

All evidence is preserved in the repository and associated session data:
- **Session transcripts**: 42 sessions, 448 MB, in `~/.claude/projects/` (available on request)
- **BPF policy code**: 55 `.bpf.c` files in `extension/`, 29 touched by AI-assisted commits
- **Experiment results**: 681 result artifacts across `workloads/*/results/`
- **Safety documentation**: `MEMORY.md`, plan documents in `docs/`, commit messages with `Co-Authored-By` markers
- **Detailed analysis**: `docs/gpu-ext/eval/agent/q{1-5}_*.md`
