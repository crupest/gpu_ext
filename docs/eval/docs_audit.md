# `docs/` Audit and Reorganization Recommendation

Date: 2026-03-28

Scope:
- Markdown inventory only: `find docs/ -type f -name '*.md'`
- 110 Markdown files
- 71,041 total Markdown lines
- Cross-reference counts below are based on explicit Markdown links and inline path-style `.md` references, not free-text mentions

## 1. Current File Inventory

### Root
- `README.md` | 81 | REFERENCE | Top-level docs index and quick navigation page.
- `cross_block_prefetch_mechanism.md` | 1566 | PLAN | Mechanism-level design for cross-VA-block prefetch implementation.
- `cross_block_prefetch_plan.md` | 1809 | PLAN | Detailed v1 plan for cross-VA-block prefetch algorithms and experiments.
- `cross_block_prefetch_plan_v2.md` | 813 | PLAN | Condensed current snapshot of the cross-block prefetch plan.
- `gpu_preempt_kfunc_plan.md` | 1320 | PLAN | Detailed v1 design for direct BPF-triggered GPU preempt kfunc support.
- `gpu_preempt_kfunc_plan_v2.md` | 444 | PLAN | Condensed current snapshot of the GPU preempt kfunc plan.
- `msched_reproduction_plan.md` | 524 | PLAN | Active MSched reproduction and follow-on research plan.
- `retest_plan_gpu_block_access_fix.md` | 217 | PLAN | Retest plan after the `gpu_block_access` callback bug fix.
- `xcoord_plan.md` | 2475 | PLAN | Detailed xCoord research plan for CPU-GPU coordination.
- `xcoord_plan_v2.md` | 482 | PLAN | Condensed current snapshot of the xCoord direction.

### `gpu-ext/` Core and Legacy
- `gpu-ext/POLICY_OVERVIEW.md` | 283 | REFERENCE | Policy catalog and strategy-selection guide for `gpu_ext`.
- `gpu-ext/archive/backup/POLICY_DESIGN_GUIDE.md` | 497 | OBSOLETE | Older eviction-policy design guide kept under a backup path.
- `gpu-ext/archive/backup/related1.md` | 526 | OBSOLETE | Earlier GPU bug taxonomy draft superseded by a newer reference note.

### `gpu-ext/driver_docs/`
- `gpu-ext/driver_docs/MODULE_LOAD_UNLOAD_GUIDE.md` | 357 | REFERENCE | Practical guide for loading and unloading NVIDIA kernel modules.
- `gpu-ext/driver_docs/UVM_MODULE_ARCHITECTURE_CN.md` | 1299 | REFERENCE | Comprehensive architecture analysis of the NVIDIA UVM module.
- `gpu-ext/driver_docs/UVM_USER_API_MAPPING_CN.md` | 114 | REFERENCE | Maps user-space CUDA UVM APIs to kernel-side ioctl handling.
- `gpu-ext/driver_docs/WIC_INTEGRATION_ANALYSIS.md` | 1087 | REFERENCE | Analysis of WIC integration into the NVIDIA UVM driver.
- `gpu-ext/driver_docs/cflow_output/CALLGRAPH_GUIDE.md` | 585 | REFERENCE | Guide for generating UVM callgraphs.
- `gpu-ext/driver_docs/cflow_output/CALLGRAPH_QUICK_START.md` | 287 | REFERENCE | Quick-start reference for existing callgraph outputs.
- `gpu-ext/driver_docs/cflow_output/USE_CFLOW_DOXYGEN.md` | 477 | REFERENCE | Tooling guide for generating callgraphs with cflow and doxygen.
- `gpu-ext/driver_docs/lru/BPF_LIST_OPERATIONS_GUIDE.md` | 844 | REFERENCE | Explains BPF list operations relevant to UVM policy implementation.
- `gpu-ext/driver_docs/lru/CHUNK_VA_BLOCK_MAPPING_ANALYSIS.md` | 257 | REFERENCE | Analysis of physical chunk reuse across VA blocks.
- `gpu-ext/driver_docs/lru/HOOK_CALL_PATTERN_ANALYSIS.md` | 1454 | REFERENCE | Detailed study of LRU hook trigger patterns and semantics.
- `gpu-ext/driver_docs/lru/LRU_LIST_USAGE_COMPLETE_ANALYSIS.md` | 513 | REFERENCE | Inventory of how UVM uses LRU lists.
- `gpu-ext/driver_docs/lru/PMM_CHUNK_LIFECYCLE_ANALYSIS.md` | 701 | REFERENCE | Trace-based chunk lifecycle analysis to understand eviction behavior.
- `gpu-ext/driver_docs/lru/UVM_LIST_HELPERS.md` | 1218 | REFERENCE | Catalog of UVM list helpers and usage examples.
- `gpu-ext/driver_docs/lru/UVM_LRU_POLICY.md` | 3265 | REFERENCE | Deep dive into UVM LRU policy and BPF extension points.
- `gpu-ext/driver_docs/lru/UVM_LRU_USAGE_GUIDE.md` | 835 | REFERENCE | Practical guide for implementing custom eviction policies on top of UVM LRU.
- `gpu-ext/driver_docs/nvidia_symbol_tracing_investigation.md` | 419 | REFERENCE | Investigation into why some NVIDIA driver symbols cannot be traced.
- `gpu-ext/driver_docs/prefetch/COMPUTE_PREFETCH_REGION_DETAILED.md` | 562 | REFERENCE | Detailed walkthrough of `compute_prefetch_region`.
- `gpu-ext/driver_docs/prefetch/PREFETCH_MODIFICATION_GUIDE.md` | 1242 | PLAN | Actionable guide for modifying UVM automatic prefetch behavior.
- `gpu-ext/driver_docs/prefetch/UVM_PREFETCH_AND_POLICY_HOOKS.md` | 662 | REFERENCE | Overview of prefetch internals and candidate policy replacement hooks.
- `gpu-ext/driver_docs/prefetch/UVM_PREFETCH_POLICY_ANALYSIS.md` | 924 | REFERENCE | Analysis of UVM prefetch behavior and possible BPF policy extensions.
- `gpu-ext/driver_docs/sched/GPU_SCHEDULING_CONTROL_ANALYSIS.md` | 616 | REFERENCE | Compares user-space versus driver/kernel-level scheduling control.
- `gpu-ext/driver_docs/sched/Hook函数逐行详细分析_zh.md` | 1774 | REFERENCE | Line-by-line walkthrough of scheduling hook functions.
- `gpu-ext/driver_docs/sched/NVIDIA_FIFO模块架构分析_zh.md` | 2848 | REFERENCE | Architecture analysis of the NVIDIA FIFO scheduling subsystem.
- `gpu-ext/driver_docs/sched/callgraph_analysis.md` | 951 | REFERENCE | Detailed scheduling callgraph analysis.
- `gpu-ext/driver_docs/sched/eBPF_Hook点集成设计文档_zh.md` | 1003 | PLAN | Design proposal for integrating eBPF scheduling hook points.
- `gpu-ext/driver_docs/sched/ebpf_preempt_design.md` | 232 | PLAN | Feasibility/design note for eBPF-based GPU preemption control.
- `gpu-ext/driver_docs/sched/gpreempt-analysis/GPreempt_Implementation_Analysis.md` | 553 | REFERENCE | English analysis of the GPreempt implementation.
- `gpu-ext/driver_docs/sched/gpreempt-analysis/GPreempt_Implementation_Analysis_zh.md` | 553 | REFERENCE | Chinese translation of the GPreempt implementation analysis.
- `gpu-ext/driver_docs/sched/gpreempt-analysis/GPreempt补丁分析与FIFO调度能力评估_zh.md` | 1331 | REFERENCE | Analysis of the GPreempt patch and FIFO scheduling capability.
- `gpu-ext/driver_docs/sched/gpreempt-analysis/GPreempt论文深度解读与未来展望_zh.md` | 1534 | REFERENCE | Deep review of the GPreempt paper and future directions.
- `gpu-ext/driver_docs/sched/gpu_preempt_ctrl_design.md` | 214 | PLAN | Design doc for the user-space `gpu_preempt_ctrl` tool.
- `gpu-ext/driver_docs/sched/gpu_sched_trace_output_analysis.md` | 152 | REFERENCE | Explains the meaning of the GPU scheduling trace output.
- `gpu-ext/driver_docs/sched/hook_enhancement_analysis.md` | 246 | PLAN | Proposal for expanding the scheduling hook surface.
- `gpu-ext/driver_docs/sched/perf/perf_analysis.md` | 174 | EVAL | Perf profile comparison between device mode and UVM mode.
- `gpu-ext/driver_docs/sched/完整调用链分析_从用户态到Hook点_zh.md` | 1836 | REFERENCE | Full call-chain analysis from user space down to hook points.
- `gpu-ext/driver_docs/sched/最小侵入性GPU调度方案设计_zh.md` | 854 | PLAN | Minimal-intrusion GPU scheduling design note.
- `gpu-ext/driver_docs/sched/用户态控制vs_eBPF内核扩展对比分析_zh.md` | 1501 | REFERENCE | Comparison of user-space scheduling control and eBPF kernel extension.
- `gpu-ext/driver_docs/trace/test_uvm.md` | 247 | UNKNOWN | Raw terminal capture from a `bpftrace` UVM tracing attempt.

### `gpu-ext/eval/`
- `gpu-ext/eval/agent/q1_git_archaeology.md` | 577 | EVAL | Git-history archaeology for BPF policy development.
- `gpu-ext/eval/agent/q2_safety_taxonomy.md` | 107 | EVAL | Safety taxonomy drawn from sessions and repo memory.
- `gpu-ext/eval/agent/q3_case_studies.md` | 254 | EVAL | Case studies of policy-development arcs.
- `gpu-ext/eval/agent/q4_session_exploration_log.md` | 39 | EVAL | Session log table summarizing exploration sessions.
- `gpu-ext/eval/agent/q5_safety_events_from_sessions.md` | 501 | EVAL | Safety-event extraction from session transcripts.
- `gpu-ext/eval/agent/q6_precise_metrics.md` | 227 | EVAL | Precise transcript and subagent metrics for the project.
- `gpu-ext/eval/agent_codebase_analysis.md` | 878 | EVAL | Evidence-based analysis of agent-driven development in the repo.
- `gpu-ext/eval/agent_session_analysis.md` | 330 | EVAL | Session-level analysis of Claude Code work on the repo.
- `gpu-ext/eval/multi-tenant-memory/EVALUATION_REPORT.md` | 290 | EVAL | Formal report for multi-tenant GPU memory priority experiments.
- `gpu-ext/eval/multi-tenant-memory/README.md` | 6 | EVAL | Command stub for running the multi-tenant memory evaluation.
- `gpu-ext/eval/multi-tenant-scheduler/README.md` | 165 | EVAL | Write-up of the multi-tenant scheduler evaluation, despite the README filename.
- `gpu-ext/eval/suggestions/vllm.md` | 804 | PLAN | Recommendations for a stronger vLLM evaluation setup.
- `gpu-ext/eval/suggestions/workflow.md` | 658 | PLAN | Proposed workflow and workload design for an evaluation story.

### `gpu-ext/experiment/`
- `gpu-ext/experiment/experiment-record.md` | 6 | UNKNOWN | Scratch note with a single recorded experiment command.
- `gpu-ext/experiment/experiment_scripts_plan.md` | 263 | PLAN | Plan for refactoring experiment scripts into a cleaner architecture.

### `gpu-ext/paper/`
- `gpu-ext/paper/README.md` | 44 | PAPER | Paper source overview and abstract.
- `gpu-ext/paper/img/FIGURE_SOURCES.md` | 153 | PAPER | Figure provenance and source-tracking document.
- `gpu-ext/paper/img/pattern/vector_add/test_gpu_thread_exec.md` | 331 | REFERENCE | Example/tutorial for tracing CUDA thread scheduling, currently stored under a figure path.
- `gpu-ext/paper/img/results-raw/clc/result.md` | 318 | EVAL | Consolidated raw numbers and narrative for CLC-related results.
- `gpu-ext/paper/img/results-raw/runtime/micro_vec_add_result.md` | 83 | EVAL | Raw runtime microbenchmark results on RTX 5090.
- `gpu-ext/paper/img/results-raw/runtime/old/examples_vec_add_result_p40.md` | 28 | OBSOLETE | Archived old runtime benchmark result for Tesla P40.
- `gpu-ext/paper/img/results-raw/runtime/old/micro_vec_add_result.md` | 83 | OBSOLETE | Archived older runtime benchmark snapshot for RTX 5090.
- `gpu-ext/paper/img/results-raw/sync/memory/prefetch.md` | 14 | EVAL | Short raw note comparing prefetch coordination outcomes.
- `gpu-ext/paper/img/results-raw/sync/memory/test-overhead-prefetch.md` | 151 | EVAL | Raw test output for prefetch-overhead measurements.
- `gpu-ext/paper/tex/verifier_eval_outline.md` | 353 | PAPER | Outline and critique for the paper’s verifier-evaluation section.
- `gpu-ext/paper/tex-old-and-doc/bench_guide.md` | 975 | PAPER | Paper-facing guide for sharpening research questions and evaluation design.
- `gpu-ext/paper/tex-old-and-doc/brainstorm.md` | 73 | PAPER | Early brainstorming notes for the paper narrative.
- `gpu-ext/paper/tex-old-and-doc/keydesign.md` | 65 | PAPER | Draft prose for the execution-runtime design section.
- `gpu-ext/paper/tex-old-and-doc/policy-analysis.md` | 678 | PAPER | Paper-facing analysis of locality and policy strategies.
- `gpu-ext/paper/tex-old-and-doc/workload-policy-guide.md` | 685 | PAPER | Guide for mapping workloads to policy design and claims.

### `gpu-ext/profiling/` and `gpu-ext/reference/`
- `gpu-ext/profiling/WORKLOAD_ANALYSIS.md` | 270 | REFERENCE | Workload memory-access analysis and policy suggestions.
- `gpu-ext/reference/CHUNK_TRACE_FORMAT.md` | 175 | REFERENCE | Explains the output format of `chunk_trace`.
- `gpu-ext/reference/EVICTION_POLICIES.md` | 225 | REFERENCE | Reference overview of GPU memory eviction policies.
- `gpu-ext/reference/related.md` | 702 | REFERENCE | Full GPU bug taxonomy and verification-related notes.

### `gpu-ext/resubmission-v2/`
- `gpu-ext/resubmission-v2/case_study_agent_exploration.md` | 249 | PAPER | Draft paper section on AI-assisted policy exploration.
- `gpu-ext/resubmission-v2/driver_novelty.md` | 248 | PAPER | Draft positioning note on driver-side novelty.
- `gpu-ext/resubmission-v2/improvement_plan.md` | 292 | PLAN | Concrete resubmission improvement plan.
- `gpu-ext/resubmission-v2/intro_draft.md` | 98 | PAPER | Revised introduction draft.
- `gpu-ext/resubmission-v2/osdi_review.md` | 106 | PAPER | Internal OSDI-style review of the paper.
- `gpu-ext/resubmission-v2/paper_structure_draft.md` | 476 | PAPER | Draft structure and methodology note for the resubmission.

### `gpu-ext/test-verify/`
- `gpu-ext/test-verify/cuda_programming_issues.md` | 247 | REFERENCE | Reference note on classic CUDA correctness and performance pitfalls.
- `gpu-ext/test-verify/examples/README.md` | 125 | REFERENCE | Explains verification-gap examples that pass CPU-style checks but fail on GPUs.
- `gpu-ext/test-verify/explain.md` | 132 | PAPER | Note on how to motivate GPU-specific verification in the paper.
- `gpu-ext/test-verify/mem_divergence_bench/README.md` | 113 | REFERENCE | README for a benchmark covering major GPU bottlenecks.

### `presentation/`
- `presentation/gpu_ext_gpumode/README.md` | 11 | UNKNOWN | Boilerplate Slidev README for the GPU MODE deck.
- `presentation/gpu_ext_gpumode/outline.md` | 456 | PLAN | Talk outline for the deck, but the title still says “LPC 2025”.
- `presentation/gpu_ext_gpumode/pages/imported-slides.md` | 27 | UNKNOWN | Unused Slidev sample file.
- `presentation/gpu_ext_gpumode/slides.md` | 1221 | UNKNOWN | Main English Slidev deck for the GPU MODE talk.
- `presentation/gpu_ext_gpumode/slides.zh.md` | 2364 | UNKNOWN | Chinese Slidev deck; exact duplicate of the LPC Chinese deck.
- `presentation/gpu_ext_lpc/README.md` | 11 | UNKNOWN | Boilerplate Slidev README for the LPC deck.
- `presentation/gpu_ext_lpc/outline.md` | 456 | PLAN | LPC talk outline.
- `presentation/gpu_ext_lpc/pages/imported-slides.md` | 27 | UNKNOWN | Unused Slidev sample file.
- `presentation/gpu_ext_lpc/slides.md` | 1042 | UNKNOWN | Main English Slidev deck for the LPC talk.
- `presentation/gpu_ext_lpc/slides.zh.md` | 2364 | UNKNOWN | Chinese Slidev deck; exact duplicate of the GPU MODE Chinese deck.

### Top-Level `reference/`
- `reference/msched_analysis.md` | 271 | REFERENCE | Summary and analysis of the MSched paper.
- `reference/msched_paper/msched_full.md` | 1135 | REFERENCE | Extracted full-paper Markdown mirror for MSched.
- `reference/msched_reproduction_plan_old.md` | 2122 | OBSOLETE | Old archived version of the MSched reproduction plan.

### `xcoord/`
- `xcoord/bpf_core_access_findings.md` | 360 | REFERENCE | Design-relevant findings on using BPF CO-RE to access chunk attributes.
- `xcoord/xcoord_plan_old_20260228.md` | 1821 | OBSOLETE | Older xCoord plan kept with an explicit date suffix.

## 2. Problems Found

### 2.1 Root-Level Plan Sprawl
- The `docs/` root contains eight gpu_ext/xCoord planning files plus the main README, which makes the root look like a scratchpad instead of a navigable docs home.
- These plans are not grouped by project or status. The active/current versions, historical logs, and supporting findings all live at different depths.

### 2.2 Versioned Plan Fragmentation
- `cross_block_prefetch_plan.md` and `cross_block_prefetch_plan_v2.md` overlap heavily, while `cross_block_prefetch_mechanism.md` is a third document in the same topic area.
- `gpu_preempt_kfunc_plan.md` and `gpu_preempt_kfunc_plan_v2.md` have the same problem.
- `xcoord_plan.md`, `xcoord_plan_v2.md`, and `xcoord/xcoord_plan_old_20260228.md` split one topic across three locations.
- `msched_reproduction_plan.md` has an explicit “append future progress here” note, but the old full version lives in `docs/reference/`, which is the wrong semantic location.

### 2.3 Broken and Placeholder Navigation
- `docs/README.md` and `gpu-ext/POLICY_OVERVIEW.md` both link to `gpu-ext/policy/suggestions.md`, but `docs/gpu-ext/policy/` exists only as an empty directory.
- `gpu-ext/driver_docs/prefetch/PREFETCH_MODIFICATION_GUIDE.md` points to `docs/UVM_PREFETCH_AND_POLICY_HOOKS.md`, which is the wrong path.
- `gpu-ext/resubmission-v2/case_study_agent_exploration.md` points to a glob-like placeholder `docs/gpu-ext/eval/agent/q{1-5}_*.md`, which is not a real file.
- `reference/msched_reproduction_plan_old.md` links to `cross_block_prefetch_plan.md` as if it were inside `docs/reference/`.
- `xcoord_plan.md` uses `../reference/msched_analysis.md`, which resolves outside `docs/` from that file’s location.

### 2.4 Missing README/Index Files
- The highest-value orphan cluster is `gpu-ext/driver_docs/`: 25 orphan files with no README at the subtree root.
- `gpu-ext/eval/`, `gpu-ext/reference/`, `gpu-ext/experiment/`, `gpu-ext/resubmission-v2/`, and top-level `reference/` also lack effective index files.
- The current docs tree therefore depends too much on memory of filenames rather than discoverable navigation.

### 2.5 Mixed Content Types Inside One Subtree
- `gpu-ext/paper/` mixes paper source, drafting notes, figure provenance, raw result notes, and a standalone tutorial example.
- `gpu-ext/eval/` mixes formal reports, transcript analyses, and forward-looking evaluation design suggestions.
- `gpu-ext/test-verify/` mixes paper motivation notes and reusable reference examples.
- `gpu-ext/archive/backup/` double-archives two old notes but gives no context on what replaced them.

### 2.6 Inconsistent Naming Conventions
- Filenames mix `snake_case`, `kebab-case`, `SCREAMING_SNAKE`, date suffixes, `_v2`, `_old`, and bilingual naming patterns in the same subtree.
- `README.md` is used inconsistently. Sometimes it means “index” (`paper/README.md`), sometimes “evaluation report” (`eval/multi-tenant-scheduler/README.md`), sometimes “run commands” (`eval/multi-tenant-memory/README.md`), and sometimes just boilerplate (`presentation/*/README.md`).
- `paper/tex-old-and-doc/` is an especially unclear directory name because it mixes “old tex” and “doc” semantics in one label.

### 2.7 Duplicates and Near-Duplicates
- Exact duplicate pairs:
  - `presentation/gpu_ext_gpumode/README.md` == `presentation/gpu_ext_lpc/README.md`
  - `presentation/gpu_ext_gpumode/outline.md` == `presentation/gpu_ext_lpc/outline.md`
  - `presentation/gpu_ext_gpumode/pages/imported-slides.md` == `presentation/gpu_ext_lpc/pages/imported-slides.md`
  - `presentation/gpu_ext_gpumode/slides.zh.md` == `presentation/gpu_ext_lpc/slides.zh.md`
- `presentation/gpu_ext_gpumode/outline.md` is also stale: the title says “LPC 2025” even though the directory is `gpu_ext_gpumode`.
- `gpu-ext/archive/backup/related1.md` overlaps strongly with `gpu-ext/reference/related.md`.
- `gpu-ext/paper/img/results-raw/runtime/old/` contains archived runtime-result snapshots that are close to newer files under `runtime/`.

### 2.8 Raw Scratch Notes Mixed With Durable Docs
- `gpu-ext/experiment/experiment-record.md` is a 6-line scratch note with one command.
- `gpu-ext/eval/multi-tenant-memory/README.md` is a 6-line command stub with a hard-coded external path.
- `gpu-ext/driver_docs/trace/test_uvm.md` is a raw terminal dump rather than a structured note.
- `gpu-ext/paper/img/results-raw/sync/memory/prefetch.md` is only 14 lines of terse raw result text.

### 2.9 Generated or Non-Doc Artifacts Under `docs/`
- `docs/presentation/*/dist/` contains built slide artifacts.
- `docs/reference/msched_paper/static/` contains imported static website assets.
- `docs/gpu-ext/eval/__pycache__/` and `docs/gpu-ext/eval/multi-tenant-scheduler/__pycache__/` are Python cache directories.
- `docs/gpu-ext/eval/temp/` and multiple `results_*` directories are experiment artifacts, not narrative documentation.
- These do not belong in the same conceptual layer as authored docs, and they make the tree look noisier than it is.

## 3. Cross-Reference Check

### 3.1 Files That Explicitly Reference Other Markdown Files
- `docs/README.md` -> `gpu-ext/POLICY_OVERVIEW.md`, `gpu-ext/driver_docs/lru/UVM_LRU_USAGE_GUIDE.md`, `gpu-ext/driver_docs/UVM_MODULE_ARCHITECTURE_CN.md`, `gpu-ext/driver_docs/lru/UVM_LRU_POLICY.md`, `gpu-ext/driver_docs/lru/HOOK_CALL_PATTERN_ANALYSIS.md`, `gpu-ext/eval/multi-tenant-memory/README.md`, `msched_reproduction_plan.md`, `cross_block_prefetch_plan.md`, `xcoord_plan.md`, `xcoord/bpf_core_access_findings.md`, and broken `gpu-ext/policy/suggestions.md`
- `docs/cross_block_prefetch_plan.md` -> `cross_block_prefetch_mechanism.md`
- `docs/cross_block_prefetch_plan_v2.md` -> `docs/cross_block_prefetch_plan.md`, `docs/cross_block_prefetch_mechanism.md`, `docs/retest_plan_gpu_block_access_fix.md`
- `docs/gpu-ext/POLICY_OVERVIEW.md` -> `../README.md`, `reference/EVICTION_POLICIES.md`, `profiling/WORKLOAD_ANALYSIS.md`, `driver_docs/UVM_MODULE_ARCHITECTURE_CN.md`, `driver_docs/lru/UVM_LRU_POLICY.md`, `driver_docs/lru/HOOK_CALL_PATTERN_ANALYSIS.md`, `driver_docs/lru/UVM_LRU_USAGE_GUIDE.md`, `driver_docs/prefetch/UVM_PREFETCH_POLICY_ANALYSIS.md`, `driver_docs/sched/GPU_SCHEDULING_CONTROL_ANALYSIS.md`, `reference/related.md`, `eval/multi-tenant-memory/EVALUATION_REPORT.md`, `eval/multi-tenant-scheduler/README.md`, and broken `policy/suggestions.md`
- `docs/gpu-ext/eval/agent/q2_safety_taxonomy.md` -> `docs/cross_block_prefetch_plan.md`, `docs/xcoord_plan.md`, `docs/cross_block_prefetch_mechanism.md`, `docs/msched_reproduction_plan.md`, `docs/retest_plan_gpu_block_access_fix.md`, `docs/cross_block_prefetch_plan_v2.md`
- `docs/gpu-ext/eval/agent/q3_case_studies.md` -> `docs/cross_block_prefetch_plan.md`, `docs/cross_block_prefetch_mechanism.md`
- `docs/gpu-ext/eval/agent/q4_session_exploration_log.md` -> `docs/msched_reproduction_plan.md`
- `docs/gpu-ext/eval/agent_codebase_analysis.md` -> `docs/cross_block_prefetch_plan.md`, `docs/cross_block_prefetch_plan_v2.md`, `docs/msched_reproduction_plan.md`, `docs/retest_plan_gpu_block_access_fix.md`, `docs/gpu_preempt_kfunc_plan.md`, `docs/gpu_preempt_kfunc_plan_v2.md`, `docs/xcoord_plan.md`, `docs/xcoord_plan_v2.md`, `docs/xcoord/bpf_core_access_findings.md`, `docs/gpu-ext/experiment/experiment_scripts_plan.md`, `docs/gpu-ext/eval/agent_session_analysis.md`, `docs/gpu-ext/eval/suggestions/workflow.md`, `docs/gpu-ext/resubmission-v2/improvement_plan.md`, `docs/gpu-ext/resubmission-v2/osdi_review.md`, `docs/gpu-ext/driver_docs/sched/Hook函数逐行详细分析_zh.md`, `docs/gpu-ext/driver_docs/sched/eBPF_Hook点集成设计文档_zh.md`, `docs/gpu-ext/driver_docs/sched/完整调用链分析_从用户态到Hook点_zh.md`
- `docs/gpu-ext/eval/agent_session_analysis.md` -> `docs/cross_block_prefetch_plan.md`
- `docs/gpu-ext/test-verify/examples/README.md` -> `../explain.md`
- `docs/gpu_preempt_kfunc_plan.md` -> `gpu-ext/driver_docs/sched/gpu_preempt_ctrl_design.md`, `gpu-ext/driver_docs/sched/ebpf_preempt_design.md`, `gpu-ext/driver_docs/sched/gpreempt-analysis/GPreempt_Implementation_Analysis.md`, `gpu-ext/driver_docs/sched/hook_enhancement_analysis.md`
- `docs/gpu_preempt_kfunc_plan_v2.md` -> `docs/gpu_preempt_kfunc_plan.md`, `docs/gpu-ext/driver_docs/sched/gpu_preempt_ctrl_design.md`, `docs/gpu-ext/driver_docs/sched/ebpf_preempt_design.md`, `docs/gpu-ext/driver_docs/sched/gpreempt-analysis/GPreempt_Implementation_Analysis.md`, `docs/gpu-ext/driver_docs/sched/hook_enhancement_analysis.md`
- `docs/msched_reproduction_plan.md` -> `reference/msched_reproduction_plan_old.md`, `cross_block_prefetch_plan.md`
- `docs/presentation/gpu_ext_gpumode/README.md` -> `slides.md`
- `docs/presentation/gpu_ext_lpc/README.md` -> `slides.md`
- `docs/xcoord/xcoord_plan_old_20260228.md` -> `docs/reference/msched_analysis.md`
- `docs/xcoord_plan.md` -> broken `../reference/msched_analysis.md`
- `docs/xcoord_plan_v2.md` -> `docs/xcoord_plan.md`, `docs/xcoord/xcoord_plan_old_20260228.md`, `docs/xcoord/bpf_core_access_findings.md`, `docs/reference/msched_analysis.md`

### 3.2 Most Referenced Files
- `docs/cross_block_prefetch_plan.md` -> referenced by 7 files
- `docs/cross_block_prefetch_mechanism.md` -> referenced by 4 files
- `docs/msched_reproduction_plan.md` -> referenced by 4 files
- `docs/xcoord_plan.md` -> referenced by 4 files
- `docs/reference/msched_analysis.md` -> referenced by 3 files
- `docs/retest_plan_gpu_block_access_fix.md` -> referenced by 3 files
- `docs/xcoord/bpf_core_access_findings.md` -> referenced by 3 files

### 3.3 Broken References
- `docs/README.md` -> `gpu-ext/policy/suggestions.md`
- `docs/gpu-ext/POLICY_OVERVIEW.md` -> `policy/suggestions.md`
- `docs/gpu-ext/archive/backup/POLICY_DESIGN_GUIDE.md` -> `../docs/lru/BPF_LIST_OPERATIONS_GUIDE.md`
- `docs/gpu-ext/archive/backup/POLICY_DESIGN_GUIDE.md` -> `../../memory/UVM_KERNEL_PARAMETERS.md`
- `docs/gpu-ext/driver_docs/prefetch/PREFETCH_MODIFICATION_GUIDE.md` -> `docs/UVM_PREFETCH_AND_POLICY_HOOKS.md`
- `docs/gpu-ext/resubmission-v2/case_study_agent_exploration.md` -> `docs/gpu-ext/eval/agent/q{1-5}_*.md`
- `docs/reference/msched_reproduction_plan_old.md` -> `cross_block_prefetch_plan.md`
- `docs/xcoord_plan.md` -> `../reference/msched_analysis.md`

### 3.4 Orphan Files
- 68 files have no inbound docs-to-docs links.
- Largest orphan clusters:
  - `gpu-ext/driver_docs` -> 25 orphan files
  - `gpu-ext/paper` -> 15 orphan files
  - `gpu-ext/eval` -> 8 orphan files
  - `presentation/gpu_ext_gpumode` -> 4 orphan files
  - `presentation/gpu_ext_lpc` -> 4 orphan files
  - `gpu-ext/resubmission-v2` -> 4 orphan files
  - `gpu-ext/test-verify` -> 3 orphan files
- Interpretation: the problem is not that these files are useless; the problem is that the tree lacks local READMEs and topic indexes.

## 4. Proposed Reorganized Structure

```text
docs/
├── README.md
├── gpu-ext/
│   ├── README.md
│   ├── plans/
│   │   ├── README.md
│   │   ├── active/
│   │   │   ├── cross-block-prefetch/
│   │   │   │   ├── README.md
│   │   │   │   ├── plan.md
│   │   │   │   ├── mechanism.md
│   │   │   │   └── history.md
│   │   │   ├── gpu-preempt-kfunc/
│   │   │   │   ├── README.md
│   │   │   │   ├── plan.md
│   │   │   │   └── history.md
│   │   │   ├── msched/
│   │   │   │   ├── README.md
│   │   │   │   └── reproduction_plan.md
│   │   │   ├── xcoord/
│   │   │   │   ├── README.md
│   │   │   │   ├── plan.md
│   │   │   │   ├── snapshot.md
│   │   │   │   └── bpf_core_access_findings.md
│   │   │   └── infra/
│   │   │       └── gpu_block_access_fix_retest.md
│   │   └── archived/
│   │       ├── msched_reproduction_plan_old.md
│   │       └── xcoord_plan_20260228.md
│   ├── paper/
│   │   ├── README.md
│   │   ├── drafts/
│   │   ├── resubmission-v2/
│   │   ├── notes/
│   │   ├── figures/
│   │   └── raw-results/
│   ├── eval/
│   │   ├── README.md
│   │   ├── reports/
│   │   │   ├── multi-tenant-memory/
│   │   │   └── multi-tenant-scheduler/
│   │   ├── agent-study/
│   │   └── planning/
│   ├── driver_docs/
│   │   ├── README.md
│   │   ├── lru/
│   │   ├── prefetch/
│   │   ├── sched/
│   │   ├── cflow/
│   │   └── trace/
│   ├── reference/
│   │   ├── README.md
│   │   ├── policies/
│   │   ├── workloads/
│   │   ├── verification/
│   │   └── trace/
│   ├── experiments/
│   │   ├── README.md
│   │   ├── plans/
│   │   └── notes/
│   └── archive/
│       └── legacy/
├── presentation/
│   ├── README.md
│   ├── shared/
│   ├── gpu_ext_gpumode/
│   └── gpu_ext_lpc/
└── reference/
    ├── README.md
    └── msched/
        ├── analysis.md
        ├── paper/
        └── archived/
```

Why this layout:
- It keeps `gpu_ext` project docs together.
- It separates active plans from archived history.
- It keeps paper writing, evaluation, and reference material distinct.
- It preserves the existing `driver_docs` subtree to avoid unnecessary churn while still making it navigable with READMEs.
- It leaves top-level `reference/` for external paper notes and `presentation/` for slide decks.

## 5. Specific Move/Rename/Delete Recommendations

### 5.1 Move/Rename Recommendations

| Current path | Proposed path | Why |
|---|---|---|
| `docs/cross_block_prefetch_plan_v2.md` | `docs/gpu-ext/plans/active/cross-block-prefetch/plan.md` | This is the current snapshot and should live with related mechanism/history files, not at the docs root. |
| `docs/cross_block_prefetch_mechanism.md` | `docs/gpu-ext/plans/active/cross-block-prefetch/mechanism.md` | The mechanism note is part of the same topic bundle as the active plan. |
| `docs/cross_block_prefetch_plan.md` | `docs/gpu-ext/plans/active/cross-block-prefetch/history.md` | The v1 file still contains detailed history and is referenced by agent-study docs, so it should be kept but renamed as history, not treated as the active plan. |
| `docs/gpu_preempt_kfunc_plan_v2.md` | `docs/gpu-ext/plans/active/gpu-preempt-kfunc/plan.md` | The active/current plan should sit under a dedicated topic folder. |
| `docs/gpu_preempt_kfunc_plan.md` | `docs/gpu-ext/plans/active/gpu-preempt-kfunc/history.md` | The older detailed design remains useful as background, but its role should be explicit. |
| `docs/msched_reproduction_plan.md` | `docs/gpu-ext/plans/active/msched/reproduction_plan.md` | This is an active plan, not a root-level note. |
| `docs/reference/msched_reproduction_plan_old.md` | `docs/gpu-ext/plans/archived/msched_reproduction_plan_old.md` | The file is explicitly old and belongs with archived plan history, not external references. |
| `docs/retest_plan_gpu_block_access_fix.md` | `docs/gpu-ext/plans/active/infra/gpu_block_access_fix_retest.md` | This is an infrastructure retest plan that supports multiple policy efforts. |
| `docs/xcoord_plan.md` | `docs/gpu-ext/plans/active/xcoord/plan.md` | This is the detailed xCoord plan and should be grouped with the rest of the xCoord topic. |
| `docs/xcoord_plan_v2.md` | `docs/gpu-ext/plans/active/xcoord/snapshot.md` | The file reads like a current status snapshot, not a canonical standalone root doc. |
| `docs/xcoord/xcoord_plan_old_20260228.md` | `docs/gpu-ext/plans/archived/xcoord_plan_20260228.md` | This is explicitly dated historical material and should live under archive. |
| `docs/xcoord/bpf_core_access_findings.md` | `docs/gpu-ext/plans/active/xcoord/bpf_core_access_findings.md` | These findings are part of the xCoord design thread, not a detached side directory. |
| `docs/gpu-ext/resubmission-v2/*` | `docs/gpu-ext/paper/resubmission-v2/*` | All six files are paper-writing artifacts, so they should be under `paper/`, not parallel to it. |
| `docs/gpu-ext/paper/tex/verifier_eval_outline.md` | `docs/gpu-ext/paper/notes/verifier_eval_outline.md` | This is a paper note, not TeX source. |
| `docs/gpu-ext/paper/tex-old-and-doc/*` | `docs/gpu-ext/paper/notes/legacy/*` | The current directory name is vague; these are legacy paper notes and prose fragments. |
| `docs/gpu-ext/paper/img/FIGURE_SOURCES.md` | `docs/gpu-ext/paper/figures/README.md` | The doc functions as a figure index/manifest, so it should anchor a `figures/` subtree. |
| `docs/gpu-ext/paper/img/results-raw/**` | `docs/gpu-ext/paper/raw-results/**` | Raw result notes are not figure definitions; separating them clarifies source versus evidence. |
| `docs/gpu-ext/paper/img/pattern/vector_add/test_gpu_thread_exec.md` | `docs/gpu-ext/reference/verification/thread_scheduling_example.md` or nearby example code | This is a reusable example/tutorial, not a paper figure asset. |
| `docs/gpu-ext/eval/agent/q*.md`, `docs/gpu-ext/eval/agent_codebase_analysis.md`, `docs/gpu-ext/eval/agent_session_analysis.md` | `docs/gpu-ext/eval/agent-study/` | These files form one coherent study and should be discoverable as a bundle. |
| `docs/gpu-ext/eval/suggestions/*.md` | `docs/gpu-ext/eval/planning/` | These are forward-looking evaluation design notes, not reports. |
| `docs/gpu-ext/eval/multi-tenant-scheduler/README.md` | `docs/gpu-ext/eval/reports/multi-tenant-scheduler/EVALUATION_REPORT.md` | It is a report, not an index README. Renaming fixes semantics and makes the memory/scheduler report pair consistent. |
| `docs/gpu-ext/eval/multi-tenant-memory/README.md` | `docs/gpu-ext/eval/reports/multi-tenant-memory/RUNBOOK.md` or merge into the report appendix | The current file is only a run-command stub. It should either be a runbook or disappear into the main report. |
| `docs/gpu-ext/experiment/experiment_scripts_plan.md` | `docs/gpu-ext/experiments/plans/experiment_scripts_plan.md` | This is an experiment-automation plan, not a generic “experiment” top-level note. |
| `docs/gpu-ext/profiling/WORKLOAD_ANALYSIS.md` | `docs/gpu-ext/reference/workloads/WORKLOAD_ANALYSIS.md` | This is reference material on workload behavior, not profiling output that needs a separate top-level bucket. |
| `docs/gpu-ext/test-verify/*` | `docs/gpu-ext/reference/verification/` | These files are mostly reference examples and paper-motivation notes about verification, not a runnable test suite. |
| `docs/gpu-ext/POLICY_OVERVIEW.md` | `docs/gpu-ext/reference/policies/README.md` plus add a new project-level `docs/gpu-ext/README.md` | The current file is a policy index, not a project root README. The project lacks a true `gpu-ext/README.md`. |
| `docs/gpu-ext/archive/backup/*` | `docs/gpu-ext/archive/legacy/*` if retained | “archive/backup” is redundant and unclear. `legacy/` is clearer than “backup.” |

### 5.2 Structural Additions
- Add `README.md` files to:
  - `docs/gpu-ext/`
  - `docs/gpu-ext/plans/`
  - `docs/gpu-ext/driver_docs/`
  - `docs/gpu-ext/driver_docs/lru/`
  - `docs/gpu-ext/driver_docs/prefetch/`
  - `docs/gpu-ext/driver_docs/sched/`
  - `docs/gpu-ext/eval/`
  - `docs/gpu-ext/reference/`
  - `docs/gpu-ext/experiments/`
  - `docs/reference/`
  - `docs/presentation/`
- Either populate `docs/gpu-ext/policy/` with real docs or remove the directory and fix all links. Leaving an empty directory that is referenced by multiple indexes is worse than not having the directory.

### 5.3 Delete or Merge Candidates

| Path | Recommendation | Why |
|---|---|---|
| `docs/gpu-ext/archive/backup/related1.md` | Delete after confirming `gpu-ext/reference/related.md` covers the needed content | It is an older taxonomy draft under a backup path and strongly overlaps with the newer reference note. |
| `docs/gpu-ext/experiment/experiment-record.md` | Delete or merge into a dated experiments notebook | It is a 6-line scratch command, not durable documentation. |
| `docs/gpu-ext/eval/multi-tenant-memory/README.md` | Delete after merging commands into a runbook/report | It is a 6-line command stub with hard-coded external paths. |
| `docs/gpu-ext/paper/img/results-raw/runtime/old/examples_vec_add_result_p40.md` | Delete or keep only in an archive folder | It is explicitly under `old/` and is only a short archived raw result. |
| `docs/gpu-ext/paper/img/results-raw/runtime/old/micro_vec_add_result.md` | Delete or keep only in an archive folder | It is an older archived snapshot of a newer runtime result file. |
| `docs/gpu-ext/driver_docs/trace/test_uvm.md` | Delete after extracting any durable findings | It is a raw terminal capture, not a curated reference note. |
| `docs/presentation/gpu_ext_gpumode/README.md` | Delete | Boilerplate Slidev README; duplicated in the LPC deck and not project-specific. |
| `docs/presentation/gpu_ext_lpc/README.md` | Delete | Same reason. |
| `docs/presentation/gpu_ext_gpumode/pages/imported-slides.md` | Delete | Unused Slidev sample file; duplicated in the LPC deck. |
| `docs/presentation/gpu_ext_lpc/pages/imported-slides.md` | Delete | Same reason. |
| `docs/reference/msched_reproduction_plan_old.md` | Delete if Git history is enough; otherwise archive under `gpu-ext/plans/archived/` | It is explicitly old and no longer belongs in top-level references. |

### 5.4 Dedupe, Not Immediate Delete
- `presentation/gpu_ext_gpumode/outline.md` and `presentation/gpu_ext_lpc/outline.md` are byte-for-byte identical. Keep one canonical outline or move it to `presentation/shared/outline.md`.
- `presentation/gpu_ext_gpumode/slides.zh.md` and `presentation/gpu_ext_lpc/slides.zh.md` are byte-for-byte identical. Keep one canonical Chinese deck or generate one from a shared source.
- The English `slides.md` files differ, so they should not be merged blindly.

## 6. Files to Keep As-Is

These are the strongest docs that are already named well, scoped well, or content-stable enough that I would not rewrite them during reorganization. At most, I would move them under a cleaner folder or add README indexes around them.

- `docs/gpu-ext/driver_docs/UVM_MODULE_ARCHITECTURE_CN.md`
- `docs/gpu-ext/driver_docs/lru/UVM_LRU_POLICY.md`
- `docs/gpu-ext/driver_docs/lru/UVM_LRU_USAGE_GUIDE.md`
- `docs/gpu-ext/driver_docs/prefetch/UVM_PREFETCH_POLICY_ANALYSIS.md`
- `docs/gpu-ext/eval/multi-tenant-memory/EVALUATION_REPORT.md`
- `docs/gpu-ext/paper/README.md`
- `docs/gpu-ext/paper/img/FIGURE_SOURCES.md`
- `docs/gpu-ext/reference/CHUNK_TRACE_FORMAT.md`
- `docs/gpu-ext/reference/EVICTION_POLICIES.md`
- `docs/gpu-ext/reference/related.md`
- `docs/reference/msched_analysis.md`

## 7. Recommended First Pass Order

If this reorganization is done incrementally, I would do it in this order:

1. Create missing README/index files and fix the 8 broken links.
2. Move the root-level plan docs into `docs/gpu-ext/plans/`.
3. Move `gpu-ext/resubmission-v2/` under `gpu-ext/paper/`.
4. Rename the evaluation report/runbook files so `README.md` means “index,” not “random content.”
5. Clean presentation boilerplate and duplicated Chinese/outline files.
6. Move or delete scratch/raw-capture notes.
7. Only after the tree is stable, consider deeper renames such as reorganizing `paper/img/` into `paper/figures/` and `paper/raw-results/`.
