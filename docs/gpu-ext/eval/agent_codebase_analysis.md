# Agent-Driven Development Analysis of `gpu_ext`

Date: 2026-03-26

## Scope and Method

This analysis answers a narrower question than "was any AI ever used in this repo?":

1. Is there direct evidence in this codebase that AI agents were used to develop GPU BPF policies?
2. Is there enough in-repo evidence for a credible "safe automated GPU policy exploration" case study?

I inspected four classes of evidence:

- Git history:
  - `git log --all --oneline | head -200`
  - `git log --all --oneline --grep='Claude'`
  - `git log --all --oneline --grep='codex'`
  - `git log --all --oneline --grep='agent'`
  - `git log --all --oneline --grep='Co-Authored-By'`
  - full-history trailer parsing of `Co-Authored-By`
- Docs and plans:
  - recursive keyword search under `docs/`
  - manual reading of all high-signal plan, reproduction, and evaluation docs
  - manual reading of `CLAUDE.md`, `plan.md`, and `workloads/REPRODUCTION_LOG.md`
- Code:
  - all `extension/*.bpf.c`
  - `extension/backup/prefetch_direction.bpf.c`
  - verifier-related patterns in source and comments
- Experiment artifacts:
  - workload result directories under `workloads/`
  - evaluation result directories under `docs/gpu-ext/eval/`
  - trial runners and collectors in `workloads/scripts/`

Important caveat: commit-level AI attribution here means "the commit contains an explicit AI-style `Co-Authored-By` trailer" or equivalent marker. That is strong evidence of AI assistance, but it is not a line-by-line authorship proof.

## Executive Summary

Yes, this repository contains strong evidence of AI-assisted development of GPU BPF policies.

The strongest evidence is not just one signal, but the combination of:

- 46 AI-attributable commits over a concentrated period from 2026-02-16 through 2026-03-26
- explicit repo workflow guidance in `CLAUDE.md` telling contributors to use Codex/subagents, including serialized GPU/BPF experiments
- plan documents that explicitly mention subagents, Codex-generated bugs, sessions, trials, reruns, and multi-round policy refinement
- AI-attributed commits that created nontrivial BPF policies and policy-related test programs
- persisted result artifacts that preserve both positive and negative outcomes rather than only cherry-picked wins
- repeated documentation of verifier constraints, sleepable-context constraints, unsafe runtime behaviors, and global correctness fixes

However, the repository is stronger as evidence of "AI-assisted policy development and evaluation under safety constraints" than as a complete standalone proof of "safe automated exploration" in the strongest SOSP-paper sense.

What is clearly present:

- AI helped generate and iterate on GPU BPF policies
- those policies were benchmarked across multiple workloads
- unsafe or invalid designs were caught by the verifier, by runtime failures, or by semantic bugs and then revised
- experimentation was systematic enough to look like a search process rather than a single hand-written policy

What is still missing for the strongest claim:

- a preserved corpus of raw verifier rejection logs tied directly to policy revisions
- a machine-readable mapping from agent sessions to commits to benchmark runs
- a crisp closed-world accounting of the explored policy space, search controller, and acceptance criteria

My bottom-line assessment is:

- as a codebase artifact, this is strong evidence of AI-assisted GPU policy exploration
- as a "safe automated GPU policy exploration" case study, it is promising and defensible, but not yet maximally airtight without the missing logs and provenance

## Summary Statistics

| Metric | Value | Notes |
| --- | ---: | --- |
| Total commits | 205 | `git rev-list --all --count` |
| Commits with any `Co-Authored-By` trailer | 47 | one is non-AI human coauthor |
| AI-attributable commits | 46 | Claude-marked trailers |
| AI-assisted date range | 2026-02-16 to 2026-03-26 | first: `f961dc3`, last: `c120348` |
| Top-level `extension/*.bpf.c` files | 54 | current top-level inventory |
| All `.bpf.c` files in `extension/` including backup | 55 | includes `extension/backup/prefetch_direction.bpf.c` |
| Heuristic policy/scheduler `.bpf.c` files | 42 | excluding tracing/tools/tests/backups |
| `.bpf.c` files touched by AI-attributable commits | 29 / 55 | broad but not universal AI involvement |
| `.bpf.c` files introduced in AI-attributable commits | 16 / 55 | strong direct creation evidence |
| AI-introduced policy/scheduler `.bpf.c` files | 11 | excludes test/demo-only BPF files |
| Top-level BPF files created before first AI-attributed commit | 19 / 54 | baseline foundation predates explicit AI markers |
| Top-level BPF files created on/after first AI-attributed commit | 35 / 54 | advanced policy expansion happens later |
| Persisted result-tree files scanned | 720 | all files in selected result directories |
| Persisted textual result artifacts scanned | 681 | `.json`, `.log`, `.csv`, `.txt`, `.md` |
| Conservative distinct named policy/config labels in artifacts | at least 59 | after normalization and de-duplication |

Additional git-trailer detail:

- 44 commits carry `Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>`
- 2 commits carry `Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>`
- 1 commit carries a non-AI coauthor trailer (`d0937b5`)

Important negative result:

- `git log --all --oneline --grep='codex'` returned no commit-subject matches
- `git log --all --oneline --grep='agent'` returned no commit-subject matches

So the primary git-level AI signal is the coauthor trailer, not commit-subject keywords.

## Git History Findings

## 1. AI-assisted development is real, but temporally concentrated

The AI-attributable commits are clustered in late February and early March 2026, not spread uniformly across the repo's life.

Daily AI-commit distribution:

| Date | AI-attributable commits |
| --- | ---: |
| 2026-02-16 | 3 |
| 2026-02-17 | 12 |
| 2026-02-23 | 1 |
| 2026-02-24 | 4 |
| 2026-02-25 | 1 |
| 2026-02-26 | 1 |
| 2026-03-03 | 5 |
| 2026-03-04 | 8 |
| 2026-03-05 | 6 |
| 2026-03-07 | 3 |
| 2026-03-24 | 1 |
| 2026-03-26 | 1 |

Interpretation:

- the baseline eBPF framework and simple policies largely predate explicit AI markers
- AI assistance is concentrated in the phase where the project expands into workload-specific policies, preemption, xCoord, and large-scale result production

## 2. High-signal AI-attributable commits

These are the strongest code-generation and iteration commits I found.

| Date | Commit | Subject | Why it matters |
| --- | --- | --- | --- |
| 2026-02-17 | `ac05ad3` | Add atomic experiment scripts and two-layer runner infrastructure | Creates the experiment automation substrate used later for repeated policy evaluation |
| 2026-02-17 | `ae602c0` | Update REPRODUCTION_LOG.md with Session 2 atomic script verification results | Preserves session-style verification evidence in repo history |
| 2026-02-26 | `c6dd4fa` | Add BPF prefetch/eviction policies and separate cross-block plan | Introduces multiple new policy files and policy-space expansion |
| 2026-03-03 | `3596cfd` | feat: GPU TSG preempt demo - zero kernel modification, multi-context tests | Large AI-attributed expansion across preemption, scheduling, and xCoord-related files |
| 2026-03-04 | `3cd2cf9` | Add FAISS phase-adaptive cross-block prefetch and per-workload XB experiment results | Direct AI-attributed workload-specific BPF policy generation plus results |
| 2026-03-05 | `d4826d3` | uprobe-driven GPU prefetch POC: application-guided proactive migration | Strong case study for application-guided proactive policy exploration |
| 2026-03-05 | `6646203` | uprobe direct kfunc + benchmark: 40-60% speedup over always_max | Shows follow-up optimization and empirical validation |
| 2026-03-07 | `b422b9f` | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies | Strong correctness fix across many policy files after discovering a semantic bug |
| 2026-03-07 | `1fe87b2` | P5 vLLM retest + paper comparison: complete gpu_block_access fix validation | Shows rerun discipline after the global fix |
| 2026-03-24 | `95a236d` | docs: add resubmission-v2 analysis, reference papers, and beyond-paper artifacts | Meta-analysis layer around the same development |

## 3. What the git history does and does not prove

The history clearly proves:

- repeated AI-assisted commits happened
- some of those commits introduced new BPF policy code
- some later AI-assisted commits fixed correctness issues and reran experiments

The history alone does not prove:

- fully autonomous end-to-end agent control
- every policy was primarily written by an agent rather than co-developed with a human
- every failed verifier interaction was preserved in git

That stronger claim requires combining git with docs, code patterns, and result artifacts, which is why the rest of this report matters.

## Docs and Workflow Evidence

I recursively scanned `docs/` for references to `Claude Code`, `Codex`, `AI agent`, `subagent`, `automated`, `session`, `iteration`, `trial`, and `attempt`, then manually read the high-signal hits.

Many files matched only because they discuss literature, related work, or paper drafts. The table below keeps only files that materially document repository development, experimentation, or agent workflow.

### Primary contemporaneous evidence in `docs/`

| File | One-line summary of why it matters |
| --- | --- |
| `docs/cross_block_prefetch_plan.md` | Strongest single development log for iterative policy exploration; explicitly mentions subagent execution, Codex-generated bugs, trial counts, failures, reruns, and multiple workload-specific policy variants. |
| `docs/cross_block_prefetch_plan_v2.md` | Condensed cross-workload synthesis tying specific policies to workloads, result directories, trial methodology, and final conclusions. |
| `docs/msched_reproduction_plan.md` | Excellent safety-oriented log of verifier restrictions, runtime Xid failures, O(1) verifier-safe redesigns, and cross-layer policy ideas. |
| `docs/retest_plan_gpu_block_access_fix.md` | Direct evidence of a discovered semantic bug, a repo-wide policy fix, and systematic retesting across workloads. |
| `docs/gpu_preempt_kfunc_plan.md` | Iterative round-by-round design log for GPU preemption; explicitly records a Codex-generated benchmark mistake and later bug fixes. |
| `docs/gpu_preempt_kfunc_plan_v2.md` | Cleaner summary of the same preemption work, including sleepable-uProbe constraints, round results, and bug/lesson tables. |
| `docs/xcoord_plan.md` | Long-form workflow log for xCoord/FPRS-style policy exploration, bugs, scenario sweeps, and trial methodology. |
| `docs/xcoord_plan_v2.md` | Condensed xCoord roadmap and result synthesis, including the `gpu_block_access` dead-callback lesson. |
| `docs/xcoord/bpf_core_access_findings.md` | Concrete verifier/CO-RE workaround notes: `#pragma unroll`, bitfield-copy via `bpf_probe_read_kernel`, and direct CO-RE access patterns. |
| `docs/gpu-ext/experiment/experiment_scripts_plan.md` | Explicit automation architecture for atomic scripts, N-trial wrappers, and standardized result collection. |

### Secondary but still relevant `docs/` evidence

| File | One-line summary |
| --- | --- |
| `docs/gpu-ext/eval/agent_session_analysis.md` | Post-hoc analysis of Claude session logs; useful as corroboration but weaker than contemporaneous repo artifacts. |
| `docs/gpu-ext/eval/suggestions/workflow.md` | Meta-level discussion of agentic workflow for analysis and writing; supports the surrounding agent-use culture. |
| `docs/gpu-ext/resubmission-v2/improvement_plan.md` | Shows the authors were actively trying to position agentic policy exploration and verifier evidence as a paper contribution. |
| `docs/gpu-ext/resubmission-v2/osdi_review.md` | Reviewer-facing notes explicitly asking for verifier rejection rate and iteration evidence, showing awareness of the missing proof points. |

### Direct AI-authored documentation inside `docs/`

These are not case-study proofs by themselves, but they are direct evidence that AI-generated technical documentation exists in-repo.

| File | One-line summary |
| --- | --- |
| `docs/gpu-ext/driver_docs/sched/Hook函数逐行详细分析_zh.md` | Ends with `作者: Claude Code`, showing direct AI-authored driver-analysis documentation. |
| `docs/gpu-ext/driver_docs/sched/eBPF_Hook点集成设计文档_zh.md` | Also explicitly marked `作者: Claude Code`. |
| `docs/gpu-ext/driver_docs/sched/完整调用链分析_从用户态到Hook点_zh.md` | Also explicitly marked `作者: Claude Code`. |

### Non-`docs/` workflow files that materially strengthen the case

| File | One-line summary |
| --- | --- |
| `CLAUDE.md` | Explicitly tells contributors to use Codex/subagents for independent work and to serialize GPU/BPF experiments. |
| `workloads/REPRODUCTION_LOG.md` | Records explicit sessions, attempts, single-trial verification, and troubleshooting during the experiment automation phase. |
| `plan.md` | Shows structured multi-phase implementation planning tied directly to later preemption files. |

### Specific workflow evidence worth citing

The following are especially important because they are unusually explicit:

- `CLAUDE.md`
  - says multiple subagents may write code/build in parallel, but only one may run a GPU/BPF experiment at a time
  - has a section `Codex CLI as Subagent`
  - says "Complex independent tasks -> subagent or codex"
  - says "Experiments -> Agent tool"
- `docs/cross_block_prefetch_plan.md`
  - explicitly says each experiment can be executed by a subagent, but experiments must still run serially because the GPU is exclusive
  - contains a section `Bugs (codex 生成代码问题)` documenting concrete Codex-generated mistakes
- `docs/gpu_preempt_kfunc_plan.md`
  - explicitly says Codex wrote a replacement benchmark because the original path was wrong, and all four modes then produced identical invalid results
- `workloads/REPRODUCTION_LOG.md`
  - explicitly labels `Session 2: Atomic Script Verification (2026-02-17)`

### Missing file note

The task asked me to inspect `MEMORY.md`, but there is no `MEMORY.md` in this repository.

## BPF Policy Inventory and Evolution

## 1. Inventory

Current `extension/` contains 55 `.bpf.c` files if the backup policy is included:

- 54 top-level `extension/*.bpf.c`
- 1 backup file: `extension/backup/prefetch_direction.bpf.c`

At a high level, these split into:

- foundational prefetch policies: `prefetch_none`, `prefetch_always_max`, `prefetch_stride`, `prefetch_adaptive_*`
- eviction policies: FIFO, LFU, MRU, PID-based, `cycle_moe`
- workload-specific or advanced policies: `prefetch_faiss_phase`, `prefetch_llama_phase`, `prefetch_vllm_phase`, `prefetch_serving_adaptive`, `prefetch_gnn_proactive`, `prefetch_reuse_dist`, `prefetch_stride_multiblock`, `prefetch_throttled_xb`, `prefetch_proactive_layer`
- scheduling and coordination policies: `sched_gpu_*`, `prefetch_always_max_xcoord`, `prefetch_always_max_qos`
- tracing and test programs: `chunk_trace`, `prefetch_trace`, `test_*`, `gpu_sched_trace`, `gpu_preempt_ctrl`

## 2. What predates AI and what does not

This distinction matters because it shows whether the repo is "AI built from scratch" or "AI used for the later exploration phase."

What clearly predates explicit AI markers:

- core `struct_ops` substrate
- `prefetch_none`
- `prefetch_always_max`
- early adaptive and stride policies
- FIFO/LFU/MRU eviction families
- tracing tools

What clearly expands during the AI-attributed period:

- phase-adaptive per-workload policies
- xCoord/FPRS-style coordination policies
- preemption kfunc and multi-target uprobe support
- transparent and application-guided uprobe policies
- large-scale benchmark/result production and rerun infrastructure

This is exactly the pattern one would expect if humans built the initial substrate and then agents helped explore a broader policy space on top of it.

## 3. Strongest iterative policy narratives

Per-file git counts alone understate the real amount of iteration, because much of the iteration is recorded in adjacent plan docs and result files rather than as a long commit series on one `.bpf.c` file.

The clearest policy-evolution stories are:

- `prefetch_faiss_phase.bpf.c`
  - created in AI-attributed commits
  - docs show v1 direction-consistency phase detection failed
  - then switched to v2 exact `+1 block` stride detection
  - then compared heuristic phase detection against uprobe-based phase detection
- `test_uprobe_prefetch.bpf.c`
  - initial POC uses application hints
  - follow-up commit switches to direct kfunc path and reports 40-60% speedup over `always_max`
- `test_preempt_kfunc.bpf.c`
  - evolves across demo, end-to-end testing, and benchmark validation
  - docs capture a benchmark bug and later process-filtering fix
- `prefetch_always_max_xcoord.bpf.c` and related `sched_gpu_*`
  - emerge with xCoord/FPRS experimentation
  - later get swept into the global `gpu_block_access -> gpu_block_activate` fix
- many workload-specific policies from `2026-03-06` to `2026-03-07`
  - `prefetch_llama_phase`
  - `prefetch_vllm_phase`
  - `prefetch_vllm_phase_transparent`
  - `prefetch_gnn_proactive`
  - `prefetch_reuse_dist`
  - `prefetch_stride_multiblock`
  - `prefetch_throttled_xb`
  - these appear as systematic branch-outs around a shared design question, which is exactly the pattern of policy-space exploration

## 4. AI-created policy files

The clearest direct code-generation evidence is the set of policy/scheduler programs introduced in AI-attributed commits:

- `extension/prefetch_always_max_cycle_moe.bpf.c`
- `extension/prefetch_max_mru_expert.bpf.c`
- `extension/prefetch_max_passive_mru.bpf.c`
- `extension/prefetch_always_max_qos.bpf.c`
- `extension/prefetch_always_max_xcoord.bpf.c`
- `extension/sched_gpu_coord.bpf.c`
- `extension/sched_gpu_xcoord.bpf.c`
- `extension/sched_gpu_xcoord_noad.bpf.c`
- `extension/prefetch_faiss_phase.bpf.c`
- `extension/prefetch_serving_adaptive.bpf.c`
- `extension/prefetch_moe_expert.bpf.c`

That list excludes pure tests and demos. In other words, this is not merely "AI helped with test harnesses"; it includes actual policy logic.

## Verifier and Safety Evidence

This is the most important section for the "safe automated exploration" framing.

## 1. Direct verifier-related evidence in docs

### `docs/msched_reproduction_plan.md`

This file is unusually strong evidence because it records concrete safety and verifier lessons rather than vague intent.

Documented issues include:

- `move_head` in `chunk_activate` is unsafe and caused `Xid 31`
- hash-map use on the GPU fault hot path caused `Xid 31`
- pointer arithmetic is forbidden by the BPF verifier, requiring scalarization via `bpf_probe_read_kernel`
- an initial `for (i = 0; i < MAX_LAYERS; i++)` layer-detection loop was rejected as `"infinite loop detected"`
- the replacement design was an O(1) sequential boundary check specifically described as verifier-safe

This is exactly the kind of evidence one wants for safe exploration:

- unsafe ideas were tried
- they failed
- the failure mode was understood
- the implementation was redesigned to fit verifier/runtime constraints

### `docs/xcoord/bpf_core_access_findings.md`

This file gives concrete verifier/CO-RE workaround patterns:

- list traversal requires `#pragma unroll` so the verifier accepts it
- BPF CO-RE cannot directly read bitfields in this context
- the documented workaround is copying the whole struct with `bpf_probe_read_kernel` and then extracting the bitfield

This is strong because it links abstract verifier limitations to exact programming patterns later visible in code.

### `docs/gpu_preempt_kfunc_plan.md` and `docs/gpu_preempt_kfunc_plan_v2.md`

These files document:

- `KF_SLEEPABLE` constraints
- the difference between non-sleepable hooks and sleepable uprobe context
- when `bpf_wq` is required and when direct kfunc calls are legal

That is not just performance tuning; it is safety-compatibility reasoning about which BPF program types may legally invoke a given helper path.

### `docs/retest_plan_gpu_block_access_fix.md`

This is not a verifier issue, but it is strong safety/correctness evidence.

The file states that:

- `gpu_block_access` never fired for the relevant TEMP_PINNED path
- logic placed there was therefore dead
- around 20 policy files were affected
- the fix was to move logic to `gpu_block_activate`
- workloads were then rerun to validate the change

This is a serious semantic bug caught by development iteration and then corrected systematically.

## 2. Current code still reflects verifier-aware implementation choices

Representative current-code patterns:

- `extension/prefetch_proactive_layer.bpf.c`
  - uses `bpf_probe_read_kernel` to scalarize pointers
  - contains a comment explicitly describing an O(1) layer-transition check as verifier-safe
- `extension/prefetch_llama_phase.bpf.c`
  - combines `BPF_CORE_READ` with `bpf_probe_read_kernel` to safely capture `va_space`
- `extension/prefetch_vllm_phase.bpf.c`
  - same safe mixed CO-RE/scalar-read pattern
- `extension/prefetch_faiss_phase.bpf.c`
  - explicitly separates a sleepable workqueue callback from fault-path logic
- `extension/test_preempt_kfunc.bpf.c`
  - uses raw `bpf_probe_read_kernel` reads from captured ioctl context
- `extension/test_uprobe_prefetch.bpf.c`
  - comments explain the tracing-program `bpf_wq` restriction and the sleepable-uProbe direct-kfunc alternative

Pattern counts in current `extension/` code are also revealing:

- `BPF_CORE_READ`: 142 matches across 30 files
- `bpf_probe_read_kernel`: 42 matches across 24 files
- `verifier` mentions: 14 matches across 6 files

That density is consistent with a codebase repeatedly adapting to verifier and kernel-API constraints.

## 3. Safety evidence is present, but raw verifier logs are limited

This is the key nuance.

The repo contains good evidence that verifier restrictions shaped the code:

- design docs explain rejected constructs
- current source contains the expected workaround idioms
- plans explicitly call out verifier-safe rewrites

What the repo only weakly preserves is the raw, repeated sequence:

- verifier reject log
- exact agent edit
- next verifier pass

That sequence exists in spirit and partially in docs, but not as a large directly inspectable corpus of load logs.

So the strongest defensible claim is:

- safe exploration is evidenced through documented constraints, rejection-aware rewrites, and runtime-correctness reruns

not:

- the repo alone provides a complete verifier transcript archive

## Results and Workload Exploration Breadth

## 1. The experiment process is systematic, not ad hoc

The experiment infrastructure is explicitly structured around repeated execution and aggregation:

- `docs/gpu-ext/experiment/experiment_scripts_plan.md`
  - proposes a two-layer architecture:
    - atomic scripts produce one JSON per run
    - wrapper scripts run N trials and aggregate results
- `workloads/scripts/run_trials.py`
  - runs repeated trials
  - cleans up before each run
  - writes per-trial files
  - invokes `collect_results.py`
- `workloads/scripts/collect_results.py`
  - computes geometric mean, mean, stddev, min, max, and sample count

This is exactly what one wants to see if arguing that an agent was not just hacking code once, but repeatedly exploring a policy space under a stable workflow.

## 2. Result-artifact breadth

Selected result trees contain:

- 720 total files
- 681 textual result artifacts (`.json`, `.log`, `.csv`, `.txt`, `.md`)
- at least 59 conservatively normalized named policy/config labels

Representative result locations include:

- `workloads/faiss/result`
- `workloads/faiss/results`
- `workloads/llama.cpp/results`
- `workloads/llama.cpp/results_uvm`
- `workloads/pytorch/result`
- `workloads/results_phase_detection`
- `workloads/vllm/results`
- `docs/gpu-ext/eval/multi-tenant-scheduler/kfunc_preempt_results`
- `docs/gpu-ext/eval/multi-tenant-memory/results_*`

This is much broader than one benchmark per policy.

## 3. Evidence of policy-space exploration rather than one-off tuning

Persisted artifact names show repeated evaluation of:

- baselines versus `always_max`
- `always_max + cycle_moe`
- phase-detection modes for llama and vLLM
- FAISS heuristic phase detection versus uprobe phase detection
- cooperative variants
- reuse-distance variants
- stride and multiblock variants
- proactive/uProbe variants
- xCoord / coord / xcoord-noad scheduler variants
- classic eviction families

Examples preserved in artifact names:

- `n5_faiss_phase`
- `n5_faiss_uprobe`
- `llama_B_phase_mode0_noxb`
- `llama_C_phase_mode1_r32_noxb`
- `llama_G_phase_mode0_xb`
- `vllm_3b_always_max_cycle_moe`
- `vllm_3c_phase_mode0_noxb`
- `vllm_3e_phase_mode1_r32`
- `uprobe_g3_proactive_v3`
- `novel_reuse20_xb`
- `novel_stride_k3`
- `stress_with_xcoord`

This is the signature of an explored design space, not a single algorithm polished after the fact.

## 4. Negative results and failures are preserved

This is one of the strongest signs that the repository is an honest exploration log.

Examples:

- `workloads/results_phase_detection/summary.md`
  - preserves negative conclusions such as decode-prefetch narrowing being harmful
- `workloads/results_phase_detection/summary.txt`
  - records `BENCH_FAIL` and `NO_OUTPUT` cases
- `docs/cross_block_prefetch_plan.md`
  - records failed variants, deadlocks, mis-hooked uprobes, and policies that did not beat `always_max`

That makes the repository much more credible as evidence. Cherry-picked success-only repos are weaker.

## 5. Search strategy visible in the repo

The search strategy is not expressed as one central algorithm, but it is visible in the workflow:

- start from strong baseline policies such as `always_max`
- branch into workload-conditional variants
- test multiple phase-detection or prefetch-gating strategies
- keep trial counts at 5-10 with geometric-mean summaries
- rerun after correctness fixes
- preserve negative results
- converge back to simpler policies when complex heuristics do not beat baseline

That is a plausible and inspectable form of policy-space exploration, even if it is not a fully automated reinforcement-learning loop.

## Development Timeline

## Phase 0: Foundation before explicit AI markers (2025-11 to 2026-01)

This period builds the substrate:

- `struct_ops.bpf.c`
- `prefetch_none.bpf.c`
- `prefetch_always_max.bpf.c`
- early adaptive and stride policies
- FIFO/LFU/MRU eviction families
- tracing and scheduler-debug tools

Interpretation:

- the repo already had a serious manual BPF foundation before explicit AI-attributed work appears

## Phase 1: Automation and workload scaffold (2026-02-16 to 2026-02-17)

AI-attributed commits add:

- workloads directory
- atomic experiment scripts
- two-layer runner infrastructure
- benchmark result preservation rules
- reproduction logs

Interpretation:

- this is the transition from "code exists" to "code can be systematically evaluated"

## Phase 2: xCoord / policy expansion (2026-02-23 to 2026-02-26)

AI-attributed commits and docs expand:

- xCoord planning
- new prefetch and eviction policy variants
- policy documentation and plan decomposition

Interpretation:

- this is the first clear sign of a widening policy search space

## Phase 3: Preemption, phase-adaptive policies, and uProbe-guided policies (2026-03-03 to 2026-03-05)

This is the densest period of advanced policy work:

- preempt demo and kfunc path
- FAISS phase-adaptive policy
- vLLM/llama phase policies
- application-guided prefetch POC
- direct-kfunc uProbe optimization

Interpretation:

- this is the strongest period of AI-assisted GPU policy exploration in the repo

## Phase 4: Global correctness fix and reruns (2026-03-07)

The `gpu_block_access` semantic bug is discovered and fixed across many policies, followed by retesting.

Interpretation:

- this is the strongest single safety/correctness moment in the repo because it shows the project can invalidate earlier assumptions and revalidate results

## Phase 5: Meta-analysis and paper-positioning (2026-03-24 to 2026-03-26)

This phase adds:

- resubmission analysis
- agent-session analysis
- evaluation framing around agentic development

Interpretation:

- by late March, the repo is no longer just developing policies; it is also trying to explain what kind of evidence the codebase contains

## Key Case Studies

## Case Study 1: FAISS phase-adaptive cross-block prefetch

Primary evidence:

- commit `3cd2cf9`
- `extension/prefetch_faiss_phase.bpf.c`
- `extension/prefetch_faiss_uprobe.bpf.c`
- `docs/cross_block_prefetch_plan.md`
- `docs/cross_block_prefetch_plan_v2.md`
- `workloads/faiss/results/`

Why this is compelling:

- the policy itself is AI-attributed
- the docs preserve v1 and v2 algorithm evolution
- heuristic phase detection is compared against uprobe-based phase detection
- the project keeps the result even when the conclusion is "phase gating matters more than the exact detection mechanism"

Safety relevance:

- a buggy optimization caused the policy to get stuck in one phase
- the docs explain the bug and the fix
- the eventual conclusion is nuanced rather than promotional

Assessment:

- this is the strongest "agent explored a policy idea, hit bugs, fixed them, and converged to a workload-specific conclusion" example

## Case Study 2: Application-guided proactive prefetch via uProbe

Primary evidence:

- commits `d4826d3` and `6646203`
- `extension/test_uprobe_prefetch.bpf.c`
- `docs/cross_block_prefetch_plan.md`
- related microbench artifacts under `workloads/` and plan docs

Why this is compelling:

- it is explicitly framed as proactive, application-guided migration rather than reactive fault handling
- the follow-up commit reports 40-60% speedup over `always_max`
- the docs record the tracing-program `bpf_wq` constraint and the switch to sleepable-uProbe direct kfunc

Safety relevance:

- the design is visibly shaped by legality constraints around tracing context versus sleepable context
- the repo documents both the invalid path and the valid one

Assessment:

- this is the strongest example of the repo exploring beyond simple heuristics into cross-layer semantic hints

## Case Study 3: GPU preempt kfunc and multi-target uProbe preemption

Primary evidence:

- commit `3596cfd`
- later iterations summarized in `docs/gpu_preempt_kfunc_plan.md` and `_v2.md`
- `extension/test_preempt_kfunc.bpf.c`
- `extension/test_uprobe_preempt.bpf.c`
- `extension/uprobe_preempt_multi.bpf.c`
- `docs/gpu-ext/eval/multi-tenant-scheduler/kfunc_preempt_results/`

Why this is compelling:

- it explores a qualitatively different control mechanism, not just another prefetch heuristic
- the docs explicitly preserve a Codex-generated benchmark mistake and a later process-filtering bug
- it has a structured round-based validation story

Safety relevance:

- the entire design is built around sleepable-kfunc legality and verifier compatibility
- invalid triggering semantics are caught and corrected

Assessment:

- this is the best case study for agent-assisted exploration of control-policy mechanisms under strong kernel/API constraints

## Case Study 4: Repo-wide `gpu_block_access` dead-callback fix

Primary evidence:

- commit `b422b9f`
- commit `1fe87b2`
- `docs/retest_plan_gpu_block_access_fix.md`
- many affected policy files

Why this is compelling:

- this is not a tiny bug fix; it invalidates assumptions baked into many policies
- the repo responds with a systematic correction and rerun
- the doc explicitly states the callback was dead for the relevant path and explains why

Safety relevance:

- this is direct evidence that the exploration process had a semantic correctness backstop
- it also shows willingness to invalidate earlier results rather than silently keep them

Assessment:

- this is the strongest "safety through semantic validation and rerun discipline" example in the repo

## Case Study 5: xCoord and FPRS-style coordination policies

Primary evidence:

- `docs/xcoord_plan.md`
- `docs/xcoord_plan_v2.md`
- commit `3596cfd`
- `extension/prefetch_always_max_xcoord.bpf.c`
- `extension/sched_gpu_coord.bpf.c`
- `extension/sched_gpu_xcoord.bpf.c`
- `extension/sched_gpu_xcoord_noad.bpf.c`

Why this is compelling:

- there is clear branching into multiple coordination strategies and scenarios
- the docs preserve false starts, non-activating state, and bug-driven redesign
- the result set includes both coord and xcoord variants under multiple scenarios

Safety relevance:

- the plan docs explicitly record that `gpu_block_access` did not fire and therefore the original design was invalid
- later logic is moved to a path that actually executes

Assessment:

- slightly weaker than the FAISS and preemption examples, but still a strong demonstration of structured policy branching and correction

## Overall Assessment

## Is this codebase itself evidence that AI agents can safely explore GPU policy space via eBPF?

Yes, but with an important qualifier.

The repository is strong evidence for:

- AI-assisted exploration of GPU policy ideas
- AI-assisted generation of real `.bpf.c` policy code
- systematic benchmark-driven iteration across workloads
- repeated adaptation to verifier/runtime/legal-context constraints
- semantic and performance reruns after bugs were discovered

The repository is weaker as sole evidence for:

- fully autonomous exploration without close human steering
- quantitatively measured safety of the exploration process itself
- a complete preserved history of verifier-rejection loops

## Strongest argument in favor

The strongest argument is the combination of:

- explicit AI-attributed commits creating and modifying policy code
- repo workflow docs that explicitly require Codex/subagent use and serialized experiments
- plan docs that preserve multi-round exploration, failures, and fixes
- code that visibly encodes verifier-safe and sleepable-safe implementation patterns
- rerun discipline after discovering a semantic bug that invalidated prior logic

That is a much stronger body of evidence than "there were some AI-written commits."

It shows an agent-assisted development process operating inside a constrained systems environment where many bad ideas are naturally rejected by:

- the BPF verifier
- program-type legality rules
- runtime GPU faults and Xid failures
- performance regressions
- semantic callback-misunderstanding bugs

That is exactly the structure of a plausible "safe exploration" story.

## What is missing

For a truly strong SOSP-style case study, I would still want:

- raw verifier logs or libbpf load logs linked to policy revisions
- a session-to-commit-to-result provenance chain
- an explicit count of explored candidate policies with accept/reject outcomes
- a clearer statement of how much was autonomous agent search versus human-directed branching
- a clean methodology section quantifying safety filters:
  - verifier rejects
  - build failures
  - runtime failures
  - semantically invalid-but-loading policies
  - performance failures

## Final judgment

This repo is already good evidence that AI agents were materially involved in developing and iterating GPU BPF policies.

It is also credible evidence for a "safe automated GPU policy exploration" story because:

- constraints and rejection mechanisms are real and documented
- the explored policies were not blindly accepted
- negative results were preserved
- incorrect designs triggered global reruns

But for publication-grade rigor, the repo should be supplemented with preserved verifier traces and a more explicit provenance map from agent session to code revision to benchmark artifact.

## Appendix A: Full `.bpf.c` History Table

This appendix covers all current top-level `extension/*.bpf.c` files plus the backup policy file. The `AI commits` column counts commits whose body/trailers contain an AI-style coauthor marker.

| File | Commits | AI commits | First | Last | Initial subject | Latest subject |
| --- | ---: | ---: | --- | --- | --- | --- |
| `extension/chunk_trace.bpf.c` | 6 | 0 | 2025-11-23 | 2026-01-17 | Add chunk trace tool for BPF hook call tracing and update Makefile | Refactor code structure for improved readability and maintainability; removed redundant code blocks and optimized functions. |
| `extension/eviction_cycle_moe.bpf.c` | 2 | 0 | 2026-02-25 | 2026-02-27 | Add xCoord GPU->CPU Coordination benchmark script and related documentation | Refactor BPF extension for GPU memory operations |
| `extension/eviction_fifo.bpf.c` | 6 | 0 | 2025-11-23 | 2026-02-27 | Add FIFO eviction policy implementation for GPU memory management | Refactor BPF extension for GPU memory operations |
| `extension/eviction_fifo_chance.bpf.c` | 3 | 0 | 2025-12-06 | 2026-02-27 | feat: Add FIFO with Second Chance eviction policy for GPU memory management | Refactor BPF extension for GPU memory operations |
| `extension/eviction_freq_pid_decay.bpf.c` | 5 | 0 | 2025-12-06 | 2026-02-27 | feat: Implement PID-based quota eviction policy for GPU memory management | Refactor BPF extension for GPU memory operations |
| `extension/eviction_lfu.bpf.c` | 3 | 0 | 2025-11-24 | 2026-02-27 | Add FIFO and LFU eviction policies with corresponding BPF programs and update Makefile | Refactor BPF extension for GPU memory operations |
| `extension/eviction_lfu_xcoord.bpf.c` | 2 | 0 | 2026-02-24 | 2026-02-27 | docs: Enhance README and suggestions with navigation and strategy details | Refactor BPF extension for GPU memory operations |
| `extension/eviction_mru.bpf.c` | 3 | 0 | 2025-11-24 | 2026-02-27 | Add MRU eviction policy implementation and update Makefile and .gitignore | Refactor BPF extension for GPU memory operations |
| `extension/eviction_pid_quota.bpf.c` | 5 | 0 | 2025-12-06 | 2026-02-27 | feat: Implement PID-based quota eviction policy for GPU memory management | Refactor BPF extension for GPU memory operations |
| `extension/gpu_preempt_ctrl.bpf.c` | 2 | 0 | 2025-11-27 | 2026-01-17 | Add GPU preempt control tool and associated test scripts | Refactor code structure for improved readability and maintainability; removed redundant code blocks and optimized functions. |
| `extension/gpu_sched_set_timeslices.bpf.c` | 3 | 0 | 2025-12-05 | 2026-01-17 | feat: Add GPU scheduler struct_ops for custom timeslice policies and enhance debug output | Refactor code structure for improved readability and maintainability; removed redundant code blocks and optimized functions. |
| `extension/gpu_sched_trace.bpf.c` | 4 | 0 | 2025-11-25 | 2026-01-17 | feat: Add GPU Scheduler Trace Tool with BPF program and userspace component | Refactor code structure for improved readability and maintainability; removed redundant code blocks and optimized functions. |
| `extension/prefetch_adaptive_sequential.bpf.c` | 4 | 0 | 2025-11-24 | 2026-02-27 | Add adaptive prefetch policies based on PCIe throughput and update Makefile | Refactor BPF extension for GPU memory operations |
| `extension/prefetch_adaptive_tree_iter.bpf.c` | 5 | 0 | 2025-11-23 | 2026-02-27 | Add adaptive threshold prefetch policy implementation and update Makefile | Refactor BPF extension for GPU memory operations |
| `extension/prefetch_always_max.bpf.c` | 10 | 0 | 2025-11-23 | 2026-02-27 | Add always_max BPF program and cleanup tool for struct_ops instances | Refactor BPF extension for GPU memory operations |
| `extension/prefetch_always_max_cycle_moe.bpf.c` | 4 | 2 | 2026-02-25 | 2026-03-07 | Add xCoord GPU->CPU Coordination benchmark script and related documentation | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| `extension/prefetch_always_max_qos.bpf.c` | 2 | 2 | 2026-03-03 | 2026-03-07 | feat: GPU TSG preempt demo — zero kernel modification, multi-context tests | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| `extension/prefetch_always_max_xcoord.bpf.c` | 5 | 3 | 2026-02-25 | 2026-03-07 | Add xCoord GPU->CPU Coordination benchmark script and related documentation | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| `extension/prefetch_cooperative.bpf.c` | 2 | 1 | 2026-03-06 | 2026-03-07 | Add new JSON result files for GCN and UProbe workloads with updated metrics | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| `extension/prefetch_cross_block_v2.bpf.c` | 4 | 1 | 2026-02-27 | 2026-03-07 | Add cross-block prefetch analysis scripts | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| `extension/prefetch_eviction_pid.bpf.c` | 4 | 1 | 2025-12-08 | 2026-03-07 | Add PID-based Prefetch and Probabilistic LRU Eviction Policy | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| `extension/prefetch_faiss_phase.bpf.c` | 2 | 2 | 2026-03-04 | 2026-03-04 | Add FAISS phase-adaptive cross-block prefetch and per-workload XB experiment results | Add FAISS phase-adaptive cross-block prefetch and per-workload XB experiment results |
| `extension/prefetch_faiss_uprobe.bpf.c` | 3 | 2 | 2026-03-04 | 2026-03-06 | Add FAISS phase-adaptive cross-block prefetch and per-workload XB experiment results | Add new JSON result files for GCN and UProbe workloads with updated metrics |
| `extension/prefetch_gnn_proactive.bpf.c` | 2 | 1 | 2026-03-06 | 2026-03-07 | Add new JSON result files for GCN and UProbe workloads with updated metrics | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| `extension/prefetch_llama_phase.bpf.c` | 2 | 1 | 2026-03-06 | 2026-03-07 | Add new JSON result files for GCN and UProbe workloads with updated metrics | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| `extension/prefetch_max_mru_expert.bpf.c` | 3 | 2 | 2026-02-26 | 2026-03-07 | Add BPF prefetch/eviction policies and separate cross-block plan | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| `extension/prefetch_max_passive_mru.bpf.c` | 3 | 2 | 2026-02-26 | 2026-03-07 | Add BPF prefetch/eviction policies and separate cross-block plan | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| `extension/prefetch_moe_expert.bpf.c` | 1 | 1 | 2026-03-07 | 2026-03-07 | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| `extension/prefetch_none.bpf.c` | 10 | 0 | 2025-11-23 | 2026-02-27 | Add always_max BPF program and cleanup tool for struct_ops instances | Refactor BPF extension for GPU memory operations |
| `extension/prefetch_pid_tree.bpf.c` | 3 | 0 | 2025-12-06 | 2026-02-27 | feat: Implement PID-based prefetch policy with user space loader and trace helper updates | Refactor BPF extension for GPU memory operations |
| `extension/prefetch_proactive_layer.bpf.c` | 2 | 1 | 2026-02-27 | 2026-03-07 | feat: Enhance cross-block prefetching and analysis tools | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| `extension/prefetch_reuse_dist.bpf.c` | 2 | 1 | 2026-03-06 | 2026-03-07 | Add new JSON result files for GCN and UProbe workloads with updated metrics | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| `extension/prefetch_serving_adaptive.bpf.c` | 1 | 1 | 2026-03-05 | 2026-03-05 | vLLM full re-benchmark: 6 configs, all BPF policies effective (+9-10%) | vLLM full re-benchmark: 6 configs, all BPF policies effective (+9-10%) |
| `extension/prefetch_stride.bpf.c` | 3 | 0 | 2025-12-08 | 2026-02-27 | Add stride-based prefetch policy implementation | Refactor BPF extension for GPU memory operations |
| `extension/prefetch_stride_multiblock.bpf.c` | 2 | 1 | 2026-03-06 | 2026-03-07 | Add new JSON result files for GCN and UProbe workloads with updated metrics | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| `extension/prefetch_template_belady.bpf.c` | 3 | 1 | 2026-02-27 | 2026-03-07 | Add adaptive threshold BPF experiment script and layer VA ranges JSON | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| `extension/prefetch_throttled_xb.bpf.c` | 2 | 1 | 2026-03-06 | 2026-03-07 | Add new JSON result files for GCN and UProbe workloads with updated metrics | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| `extension/prefetch_trace.bpf.c` | 6 | 0 | 2025-11-24 | 2026-01-17 | Add prefetch trace tool and related assets | Refactor code structure for improved readability and maintainability; removed redundant code blocks and optimized functions. |
| `extension/prefetch_vllm_phase.bpf.c` | 2 | 1 | 2026-03-06 | 2026-03-07 | Add new JSON result files for GCN and UProbe workloads with updated metrics | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| `extension/prefetch_vllm_phase_transparent.bpf.c` | 2 | 1 | 2026-03-06 | 2026-03-07 | Add new JSON result files for GCN and UProbe workloads with updated metrics | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| `extension/sched_gpu_baseline.bpf.c` | 4 | 0 | 2026-02-24 | 2026-02-28 | docs: Enhance README and suggestions with navigation and strategy details | Implement baseline GPU scheduling policy with blind priority boost |
| `extension/sched_gpu_coord.bpf.c` | 1 | 1 | 2026-03-03 | 2026-03-03 | feat: GPU TSG preempt demo — zero kernel modification, multi-context tests | feat: GPU TSG preempt demo — zero kernel modification, multi-context tests |
| `extension/sched_gpu_minimal.bpf.c` | 2 | 0 | 2026-02-28 | 2026-02-28 | Add FAISS and PyTorch workload results for SIFT100M and random datasets | Implement baseline GPU scheduling policy with blind priority boost |
| `extension/sched_gpu_serving.bpf.c` | 1 | 0 | 2026-02-28 | 2026-02-28 | Add FAISS and PyTorch workload results for SIFT100M and random datasets | Add FAISS and PyTorch workload results for SIFT100M and random datasets |
| `extension/sched_gpu_xcoord.bpf.c` | 1 | 1 | 2026-03-03 | 2026-03-03 | feat: GPU TSG preempt demo — zero kernel modification, multi-context tests | feat: GPU TSG preempt demo — zero kernel modification, multi-context tests |
| `extension/sched_gpu_xcoord_noad.bpf.c` | 1 | 1 | 2026-03-03 | 2026-03-03 | feat: GPU TSG preempt demo — zero kernel modification, multi-context tests | feat: GPU TSG preempt demo — zero kernel modification, multi-context tests |
| `extension/struct_ops.bpf.c` | 12 | 0 | 2025-11-19 | 2026-02-27 | Remove bootstrap BPF program and associated files; add new kernel module with struct_ops support | Refactor BPF extension for GPU memory operations |
| `extension/test_chunk_access.bpf.c` | 2 | 0 | 2026-02-23 | 2026-02-27 | feat(bpf): Implement BPF CO-RE access to chunk attributes | Refactor BPF extension for GPU memory operations |
| `extension/test_preempt_demo.bpf.c` | 1 | 1 | 2026-03-03 | 2026-03-03 | feat: GPU TSG preempt demo — zero kernel modification, multi-context tests | feat: GPU TSG preempt demo — zero kernel modification, multi-context tests |
| `extension/test_preempt_kfunc.bpf.c` | 3 | 1 | 2026-03-03 | 2026-03-04 | feat: GPU TSG preempt demo — zero kernel modification, multi-context tests | Add end-to-end testing and benchmarking for GPU preemption kfunc |
| `extension/test_preempt_multi.bpf.c` | 1 | 1 | 2026-03-03 | 2026-03-03 | feat: GPU TSG preempt demo — zero kernel modification, multi-context tests | feat: GPU TSG preempt demo — zero kernel modification, multi-context tests |
| `extension/test_uprobe_preempt.bpf.c` | 2 | 0 | 2026-03-04 | 2026-03-04 | Add support for sleepable uprobe to directly call kfunc for GPU preemption | Add support for sleepable uprobe to directly call kfunc for GPU preemption |
| `extension/test_uprobe_prefetch.bpf.c` | 2 | 2 | 2026-03-05 | 2026-03-05 | uprobe-driven GPU prefetch POC: application-guided proactive migration | uprobe direct kfunc + benchmark: 40-60% speedup over always_max |
| `extension/uprobe_preempt_multi.bpf.c` | 1 | 1 | 2026-03-24 | 2026-03-24 | docs: add resubmission-v2 analysis, reference papers, and beyond-paper artifacts | docs: add resubmission-v2 analysis, reference papers, and beyond-paper artifacts |
| `extension/backup/prefetch_direction.bpf.c` | 4 | 0 | 2025-12-03 | 2026-02-27 | feat: Add directional prefetch policy with user-configurable options | Refactor BPF extension for GPU memory operations |

## Appendix B: Interpretation Notes for the `.bpf.c` Table

- A file with `AI commits = 0` is not evidence that no AI touched it outside git; it only means the file's commit history does not include a preserved AI-style coauthor marker.
- Some files show only one or two commits even though the associated design iterated heavily; in those cases the real iteration is recorded in plan docs and result files.
- Several AI-touched files were later swept into the global `gpu_block_access` fix, which is why many of them share the same latest subject.
