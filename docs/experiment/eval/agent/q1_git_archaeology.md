# Git Archaeology of BPF Policy Development

Generated on 2026-03-26 for the `gpu_ext` repository.

## Executive Summary

- Full history contains **205** commits; **46** are AI-assisted (`Co-Authored-By`), i.e. **22.44%** of all commits.
- AI-tagged commits run from **2026-02-16** to **2026-03-26** over **39** inclusive days, averaging **8.256** AI-assisted commits/week.
- There are **55** current `extension/**/*.bpf.c` files. Across history there are **57** extension-path identities: **54** kept, **1** archived, **1** abandoned.
- Full `.bpf.c` history touches **78** BPF-changing commits across the repo; **9** of those commits are AI-assisted. **16** policy files were first introduced in AI-assisted commits.
- Current BPF code footprint is **14,745** lines. Historical churn sums to **15,738** added lines and **873** deleted lines.
- Experiment artifacts show a **confirmed lower bound of 67 distinct policy×workload×configuration combinations**; adding script-enumerated configurations expands the exploration envelope to **99** combinations. **60** timestamped JSONs are retained but cannot be assigned to a policy from artifact names alone.
- Failure-keyword scan matches **21** commits (**10.24%** of repo history); **12** of them touch `extension/` or `workloads/` and are the most relevant to policy iteration.

## Scope And Method

- Q1 uses the exact `git log` commands requested by the task, including `--all`.
- Q2 follows the requested shape `git log --follow -- <file>` for each current `extension/**/*.bpf.c` file and therefore reports current-branch histories for today's file set.
- Q3 uses `git log --all` over `*.bpf.c` to build a full chronological BPF timeline, while file lifecycles use rename-following histories to connect older `src/` names to current `extension/` files.
- Q4 current LOC counts every current `extension/**/*.bpf.c` file, including `extension/backup/`. Lifetime LOC is estimated as cumulative added lines from `git log --all --numstat` over `.bpf.c` paths; this is an “ever written” estimate, not a de-duplicated semantic SLOC metric.
- Q5 distinguishes a confirmed lower bound from structured JSON/CSV artifacts versus a larger script-enumerated exploration space. Timestamp-only FAISS JSON reruns are kept as raw evidence but excluded from the confirmed distinct-combination count because the policy name is not recoverable from filenames/contents.
- Q6 scans full commit messages for `fix`, `bug`, `revert`, `workaround`, `Xid`, `verifier`, `crash`, `hang`, `broken`, and `regression`.

## Q1. Agent-Authored Commit Statistics

### Raw Command Outputs

```text
$ git log --all --oneline | wc -l
205

$ git log --all --oneline --grep='Co-Authored-By' | wc -l
46

$ git log --all --oneline --grep='Claude' | wc -l
46
```

`Co-Authored-By` and `Claude` match exactly: **46 / 46** AI-tagged commits mention Claude in the message body.

### Derived Statistics

| Metric | Value |
| --- | --- |
| Total commits | 205 |
| AI-assisted commits | 46 |
| AI-assisted share | 22.44% |
| First AI-assisted commit | 2026-02-16 12:02:53 -0800 (f961dc3e) |
| Last AI-assisted commit | 2026-03-26 18:59:48 -0700 (c1203488) |
| Inclusive date span | 39 days |
| AI-assisted commits / day | 1.179 |
| AI-assisted commits / week | 8.256 |
| AI-assisted commits / 30-day month | 35.385 |

### Monthly Frequency

| Month | All commits | AI-assisted commits | AI share |
| --- | --- | --- | --- |
| 2025-11 | 50 | 0 | 0.00% |
| 2025-12 | 39 | 0 | 0.00% |
| 2026-01 | 29 | 0 | 0.00% |
| 2026-02 | 49 | 22 | 44.90% |
| 2026-03 | 38 | 24 | 63.16% |

### Raw AI-Assisted Commit List (`git log --all --format='%H %ai %s' --grep='Co-Authored-By' | head -100`)

| Hash | Date | Subject |
| --- | --- | --- |
| c120348823b2 | 2026-03-26 18:59:48 -0700 | docs: sharpen Part IV core insights — integrate execution-strategy argument from intro_draft |
| 95a236dc6800 | 2026-03-24 20:15:01 -0700 | docs: add resubmission-v2 analysis, reference papers, and beyond-paper artifacts |
| 5fc9359457f2 | 2026-03-07 15:34:59 -0800 | docs: rewrite extension/README.md with complete policy inventory, remove outdated docs |
| 1fe87b25f461 | 2026-03-07 15:04:40 -0800 | P5 vLLM retest + paper comparison: complete gpu_block_access fix validation |
| b422b9f44b0a | 2026-03-07 14:23:35 -0800 | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| 193b86e3a8f7 | 2026-03-05 18:49:27 -0800 | refactor: reorganize repo structure and restructure plan doc |
| 6646203ce91f | 2026-03-05 18:46:03 -0800 | uprobe direct kfunc + benchmark: 40-60% speedup over always_max |
| d4826d37fc8e | 2026-03-05 18:24:30 -0800 | uprobe-driven GPU prefetch POC: application-guided proactive migration |
| 1e27a2ce845b | 2026-03-05 17:17:29 -0800 | docs: add vLLM rerun results + 5 novel algorithm designs to plan |
| 4787edeefabf | 2026-03-05 16:52:06 -0800 | vLLM full re-benchmark: 6 configs, all BPF policies effective (+9-10%) |
| f01e5eab86df | 2026-03-05 13:28:20 -0800 | vLLM submodule verification: baseline + always_max benchmark results |
| 01bfda10b348 | 2026-03-04 21:39:05 -0800 | Add vLLM as git submodule (eunomia-bpf/vllm@3ec7b051, branch=uvm) |
| 8915219921df | 2026-03-04 20:55:42 -0800 | GNN cross-block prefetch: 3.29x speedup with V1 allocator fix |
| cee028eabb77 | 2026-03-04 20:30:06 -0800 | chore: add sharegpt_vicuna.json to .gitignore (642MB exceeds GitHub limit) |
| a263d4a10455 | 2026-03-04 20:30:06 -0800 | chore: add sharegpt_vicuna.json to .gitignore (642MB exceeds GitHub limit) |
| 975d39e735d1 | 2026-03-04 20:28:47 -0800 | fix: revert PyTorch UVM allocator to V1 (plain cudaMallocManaged) |
| 60d858f7c689 | 2026-03-04 20:28:47 -0800 | fix: revert PyTorch UVM allocator to V1 (plain cudaMallocManaged) |
| 3cd2cf9dc38f | 2026-03-04 19:36:38 -0800 | Add FAISS phase-adaptive cross-block prefetch and per-workload XB experiment results |
| a2606bca6826 | 2026-03-04 19:36:38 -0800 | Add FAISS phase-adaptive cross-block prefetch and per-workload XB experiment results |
| 593c88de6529 | 2026-03-03 23:22:42 -0800 | docs: refine gpu_preempt_kfunc_plan — minimize kernel changes, add design rationale |
| 50fdeb47c893 | 2026-03-03 23:22:42 -0800 | docs: refine gpu_preempt_kfunc_plan — minimize kernel changes, add design rationale |
| c08a4cc73a9b | 2026-03-03 22:49:11 -0800 | docs: add GPU preempt demo implementation plan |
| 48c5c62bfef5 | 2026-03-03 22:49:11 -0800 | docs: add GPU preempt demo implementation plan |
| 3596cfd7f928 | 2026-03-03 22:46:15 -0800 | feat: GPU TSG preempt demo — zero kernel modification, multi-context tests |
| c6dd4fa3c9b6 | 2026-02-26 19:27:42 -0800 | Add BPF prefetch/eviction policies and separate cross-block plan |
| 2476eb6dd01e | 2026-02-25 00:10:20 -0800 | docs: download MSched paper and fix gitmodules submodule path |
| 3331cf16fdba | 2026-02-24 23:53:41 -0800 | docs: add MSched analysis and update xCoord plan with design improvements |
| 7e09fe35467a | 2026-02-24 23:20:28 -0800 | feat: POC-1 xCoord experiment results (120B, 3 scenarios) |
| b30fb73b9689 | 2026-02-24 22:05:55 -0800 | chore: reorganize presentations and update xcoord plan |
| d27e5527ef8d | 2026-02-24 22:02:47 -0800 | chore: gitignore large CSV trace files and remove from tracking |
| 5466a1a42ab1 | 2026-02-23 12:51:27 -0800 | Reorganize docs: separate gpu-ext and xcoord, add xCoord plan |
| d004fbf18e74 | 2026-02-17 22:25:39 -0800 | Fix README accuracy: simplify kernel build, add missing policies, gitignore prefetch_stride binary |
| 1be62ce7cbf2 | 2026-02-17 22:15:12 -0800 | Emphasize insmod-only loading in README, never global install |
| 609ed3104f44 | 2026-02-17 22:06:32 -0800 | Rewrite top-level README with build/install instructions for kernel module and eBPF policies |
| 2ded5043f85d | 2026-02-17 21:24:21 -0800 | Update workloads README to reflect new atomic script architecture |
| 3ea74caceded | 2026-02-17 11:05:56 -0800 | Extract shared utilities to scripts/common.py and move deprecated scripts |
| ae602c04ef6c | 2026-02-17 09:44:34 -0800 | Update REPRODUCTION_LOG.md with Session 2 atomic script verification results |
| cf7d26867580 | 2026-02-17 09:34:56 -0800 | Rewrite server_bench.py to use vllm bench serve instead of custom aiohttp client |
| f96259db7354 | 2026-02-17 09:18:22 -0800 | Add rule: always commit benchmark result JSONs |
| b9315320e964 | 2026-02-17 09:18:04 -0800 | Add benchmark result JSONs from atomic script testing |
| ac05ad3cb80c | 2026-02-17 09:15:25 -0800 | Add atomic experiment scripts and two-layer runner infrastructure |
| a523c886c102 | 2026-02-17 08:03:28 -0800 | Fix llama.cpp UVM warmup-then-migrate and exp1 model path |
| bf75d9f4ac9a | 2026-02-17 00:16:01 -0800 | Update workload READMEs and fix UVM allocators for NVIDIA driver 575+ |
| d7d71b2c7fff | 2026-02-16 15:15:48 -0800 | Add workloads directory with llama.cpp, vllm, pytorch, and faiss benchmarks |
| 6b6f78935ef2 | 2026-02-16 12:15:11 -0800 | Add gpu-ext-paper as a submodule under docs/ |
| f961dc3eab11 | 2026-02-16 12:02:53 -0800 | Fix Makefile: update src reference to extension |

## Q2. BPF Policy File Evolution

Current `extension/` tree contains **55** `.bpf.c` files. The most-touched current files are `prefetch_always_max.bpf.c`, `prefetch_none.bpf.c`, and `struct_ops.bpf.c` with **7** follow-history commits each. **11** current files appear only once in follow-history, indicating many one-shot exploratory policies/tests.

| Rank | File | Commits | AI commits | First commit date | Last commit date | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | extension/prefetch_always_max.bpf.c | 7 | 0 | 2025-11-23 | 2026-02-27 | kept |
| 2 | extension/prefetch_none.bpf.c | 7 | 0 | 2025-11-23 | 2026-02-27 | kept |
| 3 | extension/struct_ops.bpf.c | 7 | 0 | 2025-11-19 | 2026-02-27 | kept |
| 4 | extension/chunk_trace.bpf.c | 6 | 0 | 2025-11-23 | 2026-01-17 | kept |
| 5 | extension/eviction_fifo.bpf.c | 6 | 0 | 2025-11-23 | 2026-02-27 | kept |
| 6 | extension/prefetch_trace.bpf.c | 6 | 0 | 2025-11-24 | 2026-01-17 | kept |
| 7 | extension/eviction_freq_pid_decay.bpf.c | 5 | 0 | 2025-12-06 | 2026-02-27 | kept |
| 8 | extension/eviction_pid_quota.bpf.c | 5 | 0 | 2025-12-06 | 2026-02-27 | kept |
| 9 | extension/prefetch_adaptive_tree_iter.bpf.c | 5 | 0 | 2025-11-23 | 2026-02-27 | kept |
| 10 | extension/prefetch_always_max_xcoord.bpf.c | 5 | 3 | 2026-02-25 | 2026-03-07 | kept |
| 11 | extension/backup/prefetch_direction.bpf.c | 4 | 0 | 2025-12-03 | 2026-02-27 | archived |
| 12 | extension/gpu_sched_trace.bpf.c | 4 | 0 | 2025-11-25 | 2026-01-17 | kept |
| 13 | extension/prefetch_adaptive_sequential.bpf.c | 4 | 0 | 2025-11-24 | 2026-02-27 | kept |
| 14 | extension/prefetch_always_max_cycle_moe.bpf.c | 4 | 2 | 2026-02-25 | 2026-03-07 | kept |
| 15 | extension/prefetch_cross_block_v2.bpf.c | 4 | 1 | 2026-02-27 | 2026-03-07 | kept |
| 16 | extension/prefetch_eviction_pid.bpf.c | 4 | 1 | 2025-12-08 | 2026-03-07 | kept |
| 17 | extension/sched_gpu_baseline.bpf.c | 4 | 0 | 2026-02-24 | 2026-02-28 | kept |
| 18 | extension/eviction_fifo_chance.bpf.c | 3 | 0 | 2025-12-06 | 2026-02-27 | kept |
| 19 | extension/eviction_lfu.bpf.c | 3 | 0 | 2025-11-24 | 2026-02-27 | kept |
| 20 | extension/eviction_mru.bpf.c | 3 | 0 | 2025-11-24 | 2026-02-27 | kept |
| 21 | extension/gpu_sched_set_timeslices.bpf.c | 3 | 0 | 2025-12-05 | 2026-01-17 | kept |
| 22 | extension/prefetch_max_mru_expert.bpf.c | 3 | 2 | 2026-02-26 | 2026-03-07 | kept |
| 23 | extension/prefetch_max_passive_mru.bpf.c | 3 | 2 | 2026-02-26 | 2026-03-07 | kept |
| 24 | extension/prefetch_pid_tree.bpf.c | 3 | 0 | 2025-12-06 | 2026-02-27 | kept |
| 25 | extension/prefetch_stride.bpf.c | 3 | 0 | 2025-12-08 | 2026-02-27 | kept |
| 26 | extension/prefetch_template_belady.bpf.c | 3 | 1 | 2026-02-27 | 2026-03-07 | kept |
| 27 | extension/eviction_cycle_moe.bpf.c | 2 | 0 | 2026-02-25 | 2026-02-27 | kept |
| 28 | extension/eviction_lfu_xcoord.bpf.c | 2 | 0 | 2026-02-24 | 2026-02-27 | kept |
| 29 | extension/gpu_preempt_ctrl.bpf.c | 2 | 0 | 2025-11-27 | 2026-01-17 | kept |
| 30 | extension/prefetch_always_max_qos.bpf.c | 2 | 2 | 2026-03-03 | 2026-03-07 | kept |
| 31 | extension/prefetch_cooperative.bpf.c | 2 | 1 | 2026-03-06 | 2026-03-07 | kept |
| 32 | extension/prefetch_faiss_uprobe.bpf.c | 2 | 1 | 2026-03-04 | 2026-03-06 | kept |
| 33 | extension/prefetch_gnn_proactive.bpf.c | 2 | 1 | 2026-03-06 | 2026-03-07 | kept |
| 34 | extension/prefetch_llama_phase.bpf.c | 2 | 1 | 2026-03-06 | 2026-03-07 | kept |
| 35 | extension/prefetch_proactive_layer.bpf.c | 2 | 1 | 2026-02-27 | 2026-03-07 | kept |
| 36 | extension/prefetch_reuse_dist.bpf.c | 2 | 1 | 2026-03-06 | 2026-03-07 | kept |
| 37 | extension/prefetch_stride_multiblock.bpf.c | 2 | 1 | 2026-03-06 | 2026-03-07 | kept |
| 38 | extension/prefetch_throttled_xb.bpf.c | 2 | 1 | 2026-03-06 | 2026-03-07 | kept |
| 39 | extension/prefetch_vllm_phase.bpf.c | 2 | 1 | 2026-03-06 | 2026-03-07 | kept |
| 40 | extension/prefetch_vllm_phase_transparent.bpf.c | 2 | 1 | 2026-03-06 | 2026-03-07 | kept |
| 41 | extension/sched_gpu_minimal.bpf.c | 2 | 0 | 2026-02-28 | 2026-02-28 | kept |
| 42 | extension/test_chunk_access.bpf.c | 2 | 0 | 2026-02-23 | 2026-02-27 | kept |
| 43 | extension/test_preempt_kfunc.bpf.c | 2 | 1 | 2026-03-03 | 2026-03-04 | kept |
| 44 | extension/test_uprobe_prefetch.bpf.c | 2 | 2 | 2026-03-05 | 2026-03-05 | kept |
| 45 | extension/prefetch_faiss_phase.bpf.c | 1 | 1 | 2026-03-04 | 2026-03-04 | kept |
| 46 | extension/prefetch_moe_expert.bpf.c | 1 | 1 | 2026-03-07 | 2026-03-07 | kept |
| 47 | extension/prefetch_serving_adaptive.bpf.c | 1 | 1 | 2026-03-05 | 2026-03-05 | kept |
| 48 | extension/sched_gpu_coord.bpf.c | 1 | 1 | 2026-03-03 | 2026-03-03 | kept |
| 49 | extension/sched_gpu_serving.bpf.c | 1 | 0 | 2026-02-28 | 2026-02-28 | kept |
| 50 | extension/sched_gpu_xcoord.bpf.c | 1 | 1 | 2026-03-03 | 2026-03-03 | kept |
| 51 | extension/sched_gpu_xcoord_noad.bpf.c | 1 | 1 | 2026-03-03 | 2026-03-03 | kept |
| 52 | extension/test_preempt_demo.bpf.c | 1 | 1 | 2026-03-03 | 2026-03-03 | kept |
| 53 | extension/test_preempt_multi.bpf.c | 1 | 1 | 2026-03-03 | 2026-03-03 | kept |
| 54 | extension/test_uprobe_preempt.bpf.c | 1 | 0 | 2026-03-04 | 2026-03-04 | kept |
| 55 | extension/uprobe_preempt_multi.bpf.c | 1 | 1 | 2026-03-24 | 2026-03-24 | kept |

## Q3. Policy Exploration Timeline

Across history there are **57** extension-path identities: **54** kept, **1** archived, and **1** abandoned. The only abandoned extension-path identity is `extension/prefetch_cross_block.bpf.c`; its successor in the current tree is `extension/prefetch_cross_block_v2.bpf.c`.

**16** policy files were first created in AI-assisted commits, and **9 / 78** BPF-touching commits overall are AI-assisted.

### Per-Policy Lifecycle (Chronological By First Creation)

The table below omits raw rename-follow alias chains because `git log --follow` can over-approximate ancestry across very similar policy files. The high-signal transitions are:

- `extension/backup/prefetch_direction.bpf.c` is the archived descendant of the earlier `src/prefetch_direction.bpf.c`.
- `extension/sched_gpu_baseline.bpf.c` descends from `extension/sched_gpu_aware.bpf.c`.
- `extension/prefetch_cross_block.bpf.c` was abandoned and replaced by `extension/prefetch_cross_block_v2.bpf.c`.

| First date | Policy file | Status | Revisions | AI revs | Created in AI commit? | Last date |
| --- | --- | --- | --- | --- | --- | --- |
| 2025-11-19 | extension/struct_ops.bpf.c | kept | 7 | 0 | No | 2026-02-27 |
| 2025-11-23 | extension/prefetch_always_max.bpf.c | kept | 7 | 0 | No | 2026-02-27 |
| 2025-11-23 | extension/prefetch_none.bpf.c | kept | 7 | 0 | No | 2026-02-27 |
| 2025-11-23 | extension/prefetch_adaptive_tree_iter.bpf.c | kept | 5 | 0 | No | 2026-02-27 |
| 2025-11-23 | extension/eviction_fifo.bpf.c | kept | 6 | 0 | No | 2026-02-27 |
| 2025-11-23 | extension/chunk_trace.bpf.c | kept | 6 | 0 | No | 2026-01-17 |
| 2025-11-24 | extension/eviction_lfu.bpf.c | kept | 3 | 0 | No | 2026-02-27 |
| 2025-11-24 | extension/prefetch_adaptive_sequential.bpf.c | kept | 4 | 0 | No | 2026-02-27 |
| 2025-11-24 | extension/eviction_mru.bpf.c | kept | 3 | 0 | No | 2026-02-27 |
| 2025-11-24 | extension/prefetch_trace.bpf.c | kept | 6 | 0 | No | 2026-01-17 |
| 2025-11-25 | extension/gpu_sched_trace.bpf.c | kept | 4 | 0 | No | 2026-01-17 |
| 2025-11-27 | extension/gpu_preempt_ctrl.bpf.c | kept | 2 | 0 | No | 2026-01-17 |
| 2025-12-03 | extension/backup/prefetch_direction.bpf.c | archived | 4 | 0 | No | 2026-02-27 |
| 2025-12-05 | extension/gpu_sched_set_timeslices.bpf.c | kept | 3 | 0 | No | 2026-01-17 |
| 2025-12-06 | extension/eviction_freq_pid_decay.bpf.c | kept | 5 | 0 | No | 2026-02-27 |
| 2025-12-06 | extension/eviction_pid_quota.bpf.c | kept | 5 | 0 | No | 2026-02-27 |
| 2025-12-06 | extension/eviction_fifo_chance.bpf.c | kept | 3 | 0 | No | 2026-02-27 |
| 2025-12-06 | extension/prefetch_pid_tree.bpf.c | kept | 3 | 0 | No | 2026-02-27 |
| 2025-12-08 | extension/prefetch_eviction_pid.bpf.c | kept | 4 | 1 | No | 2026-03-07 |
| 2025-12-08 | extension/prefetch_stride.bpf.c | kept | 3 | 0 | No | 2026-02-27 |
| 2026-02-23 | extension/test_chunk_access.bpf.c | kept | 2 | 0 | No | 2026-02-27 |
| 2026-02-24 | extension/eviction_lfu_xcoord.bpf.c | kept | 2 | 0 | No | 2026-02-27 |
| 2026-02-24 | extension/sched_gpu_baseline.bpf.c | kept | 4 | 0 | No | 2026-02-28 |
| 2026-02-25 | extension/eviction_cycle_moe.bpf.c | kept | 2 | 0 | No | 2026-02-27 |
| 2026-02-25 | extension/prefetch_always_max_cycle_moe.bpf.c | kept | 4 | 2 | No | 2026-03-07 |
| 2026-02-25 | extension/prefetch_always_max_xcoord.bpf.c | kept | 5 | 3 | No | 2026-03-07 |
| 2026-02-26 | extension/prefetch_cross_block.bpf.c | abandoned | 3 | 1 | Yes | 2026-02-27 |
| 2026-02-26 | extension/prefetch_max_mru_expert.bpf.c | kept | 3 | 2 | Yes | 2026-03-07 |
| 2026-02-26 | extension/prefetch_max_passive_mru.bpf.c | kept | 3 | 2 | Yes | 2026-03-07 |
| 2026-02-27 | extension/prefetch_template_belady.bpf.c | kept | 3 | 1 | No | 2026-03-07 |
| 2026-02-27 | extension/prefetch_cross_block_v2.bpf.c | kept | 4 | 1 | No | 2026-03-07 |
| 2026-02-27 | extension/prefetch_proactive_layer.bpf.c | kept | 2 | 1 | No | 2026-03-07 |
| 2026-02-28 | extension/sched_gpu_minimal.bpf.c | kept | 2 | 0 | No | 2026-02-28 |
| 2026-02-28 | extension/sched_gpu_serving.bpf.c | kept | 1 | 0 | No | 2026-02-28 |
| 2026-03-03 | extension/prefetch_always_max_qos.bpf.c | kept | 2 | 2 | Yes | 2026-03-07 |
| 2026-03-03 | extension/sched_gpu_coord.bpf.c | kept | 1 | 1 | Yes | 2026-03-03 |
| 2026-03-03 | extension/sched_gpu_xcoord.bpf.c | kept | 1 | 1 | Yes | 2026-03-03 |
| 2026-03-03 | extension/sched_gpu_xcoord_noad.bpf.c | kept | 1 | 1 | Yes | 2026-03-03 |
| 2026-03-03 | extension/test_preempt_demo.bpf.c | kept | 1 | 1 | Yes | 2026-03-03 |
| 2026-03-03 | extension/test_preempt_kfunc.bpf.c | kept | 2 | 1 | Yes | 2026-03-04 |
| 2026-03-03 | extension/test_preempt_multi.bpf.c | kept | 1 | 1 | Yes | 2026-03-03 |
| 2026-03-04 | extension/test_uprobe_preempt.bpf.c | kept | 1 | 0 | No | 2026-03-04 |
| 2026-03-04 | extension/prefetch_faiss_phase.bpf.c | kept | 1 | 1 | Yes | 2026-03-04 |
| 2026-03-04 | extension/prefetch_faiss_uprobe.bpf.c | kept | 2 | 1 | Yes | 2026-03-06 |
| 2026-03-05 | extension/prefetch_serving_adaptive.bpf.c | kept | 1 | 1 | Yes | 2026-03-05 |
| 2026-03-05 | extension/test_uprobe_prefetch.bpf.c | kept | 2 | 2 | Yes | 2026-03-05 |
| 2026-03-06 | extension/prefetch_cooperative.bpf.c | kept | 2 | 1 | No | 2026-03-07 |
| 2026-03-06 | extension/prefetch_gnn_proactive.bpf.c | kept | 2 | 1 | No | 2026-03-07 |
| 2026-03-06 | extension/prefetch_llama_phase.bpf.c | kept | 2 | 1 | No | 2026-03-07 |
| 2026-03-06 | extension/prefetch_reuse_dist.bpf.c | kept | 2 | 1 | No | 2026-03-07 |
| 2026-03-06 | extension/prefetch_stride_multiblock.bpf.c | kept | 2 | 1 | No | 2026-03-07 |
| 2026-03-06 | extension/prefetch_throttled_xb.bpf.c | kept | 2 | 1 | No | 2026-03-07 |
| 2026-03-06 | extension/prefetch_vllm_phase.bpf.c | kept | 2 | 1 | No | 2026-03-07 |
| 2026-03-06 | extension/prefetch_vllm_phase_transparent.bpf.c | kept | 2 | 1 | No | 2026-03-07 |
| 2026-03-07 | extension/prefetch_moe_expert.bpf.c | kept | 1 | 1 | Yes | 2026-03-07 |
| 2026-03-24 | extension/uprobe_preempt_multi.bpf.c | kept | 1 | 1 | Yes | 2026-03-24 |
### Chronological Commit Timeline Of All BPF File Modifications

| Date | Hash | AI-assisted? | Files changed | Subject |
| --- | --- | --- | --- | --- |
| 2025-11-19 | b6166e12 | No | bootstrap.bpf.c | Initial commit |
| 2025-11-19 | 517aeb70 | No | bootstrap.bpf.c | Initial commit |
| 2025-11-19 | ba7bb7a6 | No | bootstrap.bpf.c<br>struct_ops.bpf.c | Remove bootstrap BPF program and associated files; add new kernel module with struct_ops support |
| 2025-11-19 | 52decf6e | No | bootstrap.bpf.c<br>struct_ops.bpf.c | Remove bootstrap BPF program and associated files; add new kernel module with struct_ops support |
| 2025-11-22 | f6b155d4 | No | struct_ops.bpf.c | Refactor struct_ops/test_1 to include kfunc test for substring search |
| 2025-11-22 | aa81dbe9 | No | struct_ops.bpf.c | Refactor struct_ops/test_1 to include kfunc test for substring search |
| 2025-11-23 | 587919d7 | No | struct_ops.bpf.c | Move kfunc test for substring search from test_1 to test_3 |
| 2025-11-23 | c38b456b | No | struct_ops.bpf.c | Move kfunc test for substring search from test_1 to test_3 |
| 2025-11-23 | 5a805702 | No | struct_ops.bpf.c | Refactor struct_ops to implement uvm_bpf_test_trigger_kfunc and update cleanup logic for struct_ops map |
| 2025-11-23 | 50d8e861 | No | struct_ops.bpf.c | Refactor struct_ops to implement uvm_bpf_test_trigger_kfunc and update cleanup logic for struct_ops map |
| 2025-11-23 | 3afa3f09 | No | always_max.bpf.c | Add always_max BPF program and cleanup tool for struct_ops instances |
| 2025-11-23 | 24b699ac | No | always_max.bpf.c | Add always_max BPF program and cleanup tool for struct_ops instances |
| 2025-11-23 | 58cfdd2e | No | always_max.bpf.c | Refactor code structure for improved readability and maintainability |
| 2025-11-23 | 4fa280af | No | always_max.bpf.c | Refactor code structure for improved readability and maintainability |
| 2025-11-23 | aa2084f3 | No | prefetch_always_max.bpf.c<br>struct_ops.bpf.c | Refactor code structure for improved readability and maintainability |
| 2025-11-23 | 6f2d812f | No | prefetch_always_max.bpf.c<br>struct_ops.bpf.c | Refactor code structure for improved readability and maintainability |
| 2025-11-23 | efb57d35 | No | prefetch_none.bpf.c | Add prefetch_always_max and prefetch_none BPF programs with necessary updates |
| 2025-11-23 | 26e14205 | No | prefetch_none.bpf.c | Add prefetch_always_max and prefetch_none BPF programs with necessary updates |
| 2025-11-23 | b1a6c548 | No | prefetch_adaptive_simple.bpf.c | Add adaptive threshold prefetch policy implementation and update Makefile |
| 2025-11-23 | 16801159 | No | prefetch_adaptive_simple.bpf.c<br>prefetch_always_max.bpf.c<br>prefetch_none.bpf.c | Enhance adaptive prefetch policy with NVML integration for PCIe throughput monitoring and update threshold dynamically |
| 2025-11-23 | 612b0424 | No | lru_fifo.bpf.c | Add FIFO eviction policy implementation for GPU memory management |
| 2025-11-23 | 242619e0 | No | chunk_trace.bpf.c | Add chunk trace tool for BPF hook call tracing and update Makefile |
| 2025-11-23 | 0d224561 | No | chunk_trace.bpf.c | Add chunk trace analysis script and enhance BPF hook tracing with VA block information |
| 2025-11-24 | 6475688b | No | chunk_trace.bpf.c<br>lru_fifo.bpf.c | Refactor chunk trace statistics and remove depopulate hook from FIFO eviction policy |
| 2025-11-24 | fabda22b | No | lru_fifo.bpf.c | Refactor FIFO eviction policy by removing chunk populate hook and updating struct_ops |
| 2025-11-24 | b4feeac3 | No | chunk_trace.bpf.c | Add scripts for analyzing and visualizing chunk trace data |
| 2025-11-24 | e53a6b43 | No | eviction_fifo.bpf.c<br>eviction_lfu.bpf.c | Add FIFO and LFU eviction policies with corresponding BPF programs and update Makefile |
| 2025-11-24 | 8ddb3ac5 | No | prefetch_adaptive_sequential.bpf.c<br>prefetch_adaptive_tree_iter.bpf.c<br>prefetch_always_max.bpf.c | Add adaptive prefetch policies based on PCIe throughput and update Makefile |
| 2025-11-24 | d3498143 | No | eviction_mru.bpf.c | Add MRU eviction policy implementation and update Makefile and .gitignore |
| 2025-11-24 | d1e3127d | No | prefetch_trace.bpf.c | Add prefetch trace tool and related assets |
| 2025-11-24 | c0b8d91a | No | prefetch_trace.bpf.c | Refactor prefetch trace tool: streamline event structure, update statistics handling, and enhance output format |
| 2025-11-24 | 5cc147bf | No | prefetch_trace.bpf.c | Enhance prefetch trace tool: add VA block tracking, update statistics, and improve output format |
| 2025-11-24 | 07777577 | No | prefetch_trace.bpf.c | feat: Add VA block analysis and visualization scripts |
| 2025-11-25 | b9660a96 | No | gpu_sched_trace.bpf.c | feat: Add GPU Scheduler Trace Tool with BPF program and userspace component |
| 2025-11-27 | a95f7b50 | No | gpu_preempt_ctrl.bpf.c | Add GPU preempt control tool and associated test scripts |
| 2025-12-03 | 1846eb1c | No | prefetch_direction.bpf.c | feat: Add directional prefetch policy with user-configurable options |
| 2025-12-04 | aa0be75f | No | gpu_sched_trace.bpf.c | bpf: Enhance GPU scheduler tracing with direct struct access |
| 2025-12-04 | 5f6a6c10 | No | gpu_sched_trace.bpf.c | feat: Implement GPU scheduler struct_ops for custom scheduling policies |
| 2025-12-05 | c08c846e | No | gpu_sched_set_timeslices.bpf.c | feat: Add GPU scheduler struct_ops for custom timeslice policies and enhance debug output |
| 2025-12-05 | 609327c0 | No | chunk_trace.bpf.c | feat: Enhance chunk trace with owner PID and va_space tracking in BPF events |
| 2025-12-06 | baa2eccd | No | eviction_pid_lfu.bpf.c | feat: Implement PID-based quota eviction policy for GPU memory management |
| 2025-12-06 | 3c56db50 | No | eviction_pid_lfu.bpf.c | feat: Enhance active chunk tracking with hash map for owner PID management |
| 2025-12-06 | 7307c4d0 | No | eviction_freq_pid_decay.bpf.c<br>eviction_pid_quota.bpf.c | feat: Implement PID-based eviction policies for GPU memory management with frequency decay and quota strategies |
| 2025-12-06 | 935c2c71 | No | eviction_fifo_chance.bpf.c<br>gpu_sched_set_timeslices.bpf.c | feat: Add FIFO with Second Chance eviction policy for GPU memory management |
| 2025-12-06 | 2d282ab4 | No | prefetch_trace.bpf.c | feat: Add fault PID and owner TGID tracking to prefetch trace events |
| 2025-12-06 | a4bff850 | No | prefetch_pid_tree.bpf.c | feat: Implement PID-based prefetch policy with user space loader and trace helper updates |
| 2025-12-08 | f0a6fe96 | No | prefetch_eviction_pid.bpf.c | Add PID-based Prefetch and Probabilistic LRU Eviction Policy |
| 2025-12-08 | efd682af | No | prefetch_stride.bpf.c | Add stride-based prefetch policy implementation |
| 2025-12-13 | 60146f54 | No | prefetch_direction.bpf.c<br>prefetch_adaptive_sequential.bpf.c | Enhance Prefetch Policies with Directional Support |
| 2026-01-17 | 0bd283fb | No | prefetch_direction.bpf.c<br>chunk_trace.bpf.c<br>eviction_fifo.bpf.c<br>eviction_fifo_chance.bpf.c<br>eviction_freq_pid_decay.bpf.c<br>eviction_lfu.bpf.c<br>eviction_mru.bpf.c<br>eviction_pid_quota.bpf.c<br>gpu_preempt_ctrl.bpf.c<br>gpu_sched_set_timeslices.bpf.c<br>gpu_sched_trace.bpf.c<br>prefetch_adaptive_sequential.bpf.c<br>prefetch_adaptive_tree_iter.bpf.c<br>prefetch_always_max.bpf.c<br>prefetch_eviction_pid.bpf.c<br>prefetch_none.bpf.c<br>prefetch_pid_tree.bpf.c<br>prefetch_stride.bpf.c<br>prefetch_trace.bpf.c<br>struct_ops.bpf.c | Refactor code structure for improved readability and maintainability; removed redundant code blocks and optimized functions. |
| 2026-01-17 | 089c19d9 | No | cuda_launch_trace.bpf.c<br>cuda_sched_trace.bpf.c | Add CUDA kernel launch and scheduler tracing with BPF |
| 2026-01-17 | 9d466f6b | No | cuda_launch_trace.bpf.c | Add GPU Scheduler Impact Analysis Tool |
| 2026-01-17 | fdc4237b | No | cuda_sched_trace.bpf.c | Add IRQ tracking to CUDA scheduler tracing for enhanced analysis |
| 2026-02-23 | a5eb4207 | No | test_chunk_access.bpf.c | feat(bpf): Implement BPF CO-RE access to chunk attributes |
| 2026-02-24 | 3ebc244d | No | eviction_lfu_xcoord.bpf.c<br>sched_gpu_aware.bpf.c | docs: Enhance README and suggestions with navigation and strategy details |
| 2026-02-25 | 991e2c9f | No | eviction_cycle_moe.bpf.c | Add xCoord GPU->CPU Coordination benchmark script and related documentation |
| 2026-02-26 | c6dd4fa3 | Yes | prefetch_always_max_cycle_moe.bpf.c<br>prefetch_cross_block.bpf.c<br>prefetch_max_mru_expert.bpf.c<br>prefetch_max_passive_mru.bpf.c | Add BPF prefetch/eviction policies and separate cross-block plan |
| 2026-02-27 | 0860609f | No | prefetch_template_belady.bpf.c | Add adaptive threshold BPF experiment script and layer VA ranges JSON |
| 2026-02-27 | 6ef761da | No | prefetch_cross_block.bpf.c | No code changes detected; skipping commit. |
| 2026-02-27 | 8eba67c4 | No | prefetch_cross_block_v2.bpf.c | Add cross-block prefetch analysis scripts |
| 2026-02-27 | f5ec67e3 | No | prefetch_direction.bpf.c<br>eviction_cycle_moe.bpf.c<br>eviction_fifo.bpf.c<br>eviction_fifo_chance.bpf.c<br>eviction_freq_pid_decay.bpf.c<br>eviction_lfu.bpf.c<br>eviction_lfu_xcoord.bpf.c<br>eviction_mru.bpf.c<br>eviction_pid_quota.bpf.c<br>prefetch_adaptive_sequential.bpf.c<br>prefetch_adaptive_tree_iter.bpf.c<br>prefetch_always_max.bpf.c<br>prefetch_always_max_cycle_moe.bpf.c<br>prefetch_cross_block.bpf.c<br>prefetch_cross_block_v2.bpf.c<br>prefetch_eviction_pid.bpf.c<br>prefetch_max_mru_expert.bpf.c<br>prefetch_max_passive_mru.bpf.c<br>prefetch_none.bpf.c<br>prefetch_pid_tree.bpf.c<br>prefetch_stride.bpf.c<br>prefetch_template_belady.bpf.c<br>struct_ops.bpf.c<br>test_chunk_access.bpf.c | Refactor BPF extension for GPU memory operations |
| 2026-02-27 | 92cb1ba7 | No | prefetch_cross_block_v2.bpf.c<br>prefetch_proactive_layer.bpf.c | feat: Enhance cross-block prefetching and analysis tools |
| 2026-02-27 | 8c6a3463 | No | sched_gpu_aware.bpf.c | feat: Add GPU process PID boosting support and enhance xCoord integration |
| 2026-02-28 | 981ac544 | No | sched_gpu_aware.bpf.c<br>sched_gpu_minimal.bpf.c<br>sched_gpu_serving.bpf.c | Add FAISS and PyTorch workload results for SIFT100M and random datasets |
| 2026-02-28 | 9bdfd794 | No | sched_gpu_baseline.bpf.c<br>sched_gpu_minimal.bpf.c | Implement baseline GPU scheduling policy with blind priority boost |
| 2026-03-03 | 3596cfd7 | Yes | prefetch_always_max_qos.bpf.c<br>prefetch_always_max_xcoord.bpf.c<br>sched_gpu_coord.bpf.c<br>sched_gpu_xcoord.bpf.c<br>sched_gpu_xcoord_noad.bpf.c<br>test_preempt_demo.bpf.c<br>test_preempt_kfunc.bpf.c<br>test_preempt_multi.bpf.c | feat: GPU TSG preempt demo — zero kernel modification, multi-context tests |
| 2026-03-04 | 4b49e0ac | No | test_preempt_kfunc.bpf.c | Add end-to-end testing and benchmarking for GPU preemption kfunc |
| 2026-03-04 | 6be905f5 | No | test_preempt_kfunc.bpf.c | Add end-to-end testing and benchmarking for GPU preemption kfunc |
| 2026-03-04 | f21467a2 | No | test_uprobe_preempt.bpf.c | Add support for sleepable uprobe to directly call kfunc for GPU preemption |
| 2026-03-04 | 8705122b | No | test_uprobe_preempt.bpf.c | Add support for sleepable uprobe to directly call kfunc for GPU preemption |
| 2026-03-04 | a2606bca | Yes | prefetch_faiss_phase.bpf.c | Add FAISS phase-adaptive cross-block prefetch and per-workload XB experiment results |
| 2026-03-04 | 3cd2cf9d | Yes | prefetch_faiss_phase.bpf.c | Add FAISS phase-adaptive cross-block prefetch and per-workload XB experiment results |
| 2026-03-05 | 4787edee | Yes | prefetch_serving_adaptive.bpf.c | vLLM full re-benchmark: 6 configs, all BPF policies effective (+9-10%) |
| 2026-03-05 | d4826d37 | Yes | test_uprobe_prefetch.bpf.c | uprobe-driven GPU prefetch POC: application-guided proactive migration |
| 2026-03-05 | 6646203c | Yes | test_uprobe_prefetch.bpf.c | uprobe direct kfunc + benchmark: 40-60% speedup over always_max |
| 2026-03-06 | 77051e68 | No | prefetch_cooperative.bpf.c<br>prefetch_faiss_uprobe.bpf.c<br>prefetch_gnn_proactive.bpf.c<br>prefetch_llama_phase.bpf.c<br>prefetch_reuse_dist.bpf.c<br>prefetch_stride_multiblock.bpf.c<br>prefetch_throttled_xb.bpf.c<br>prefetch_vllm_phase.bpf.c<br>prefetch_vllm_phase_transparent.bpf.c | Add new JSON result files for GCN and UProbe workloads with updated metrics |
| 2026-03-07 | b422b9f4 | Yes | prefetch_always_max_cycle_moe.bpf.c<br>prefetch_always_max_qos.bpf.c<br>prefetch_always_max_xcoord.bpf.c<br>prefetch_cooperative.bpf.c<br>prefetch_cross_block_v2.bpf.c<br>prefetch_eviction_pid.bpf.c<br>prefetch_gnn_proactive.bpf.c<br>prefetch_llama_phase.bpf.c<br>prefetch_max_mru_expert.bpf.c<br>prefetch_max_passive_mru.bpf.c<br>prefetch_moe_expert.bpf.c<br>prefetch_proactive_layer.bpf.c<br>prefetch_reuse_dist.bpf.c<br>prefetch_stride_multiblock.bpf.c<br>prefetch_template_belady.bpf.c<br>prefetch_throttled_xb.bpf.c<br>prefetch_vllm_phase.bpf.c<br>prefetch_vllm_phase_transparent.bpf.c | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| 2026-03-24 | 95a236dc | Yes | uprobe_preempt_multi.bpf.c | docs: add resubmission-v2 analysis, reference papers, and beyond-paper artifacts |

## Q4. Lines Of BPF Code

| Metric | Value |
| --- | --- |
| Current `.bpf.c` LOC in `extension/` | 14,745 |
| Historical added lines (`git log --all --numstat`) | 15,738 |
| Historical deleted lines | 873 |
| Historical net (`added - deleted`) | 14,865 |
| Added/current ratio | 1.07x |
| Deleted/added ratio | 5.55% |

Interpretation: the current tree retains most of what was ever written. The historical estimate shows only moderate rewrite/deletion churn relative to the present codebase size.

### Largest Current BPF Files By LOC

| Rank | File | Current LOC |
| --- | --- | --- |
| 1 | extension/prefetch_moe_expert.bpf.c | 768 |
| 2 | extension/prefetch_gnn_proactive.bpf.c | 621 |
| 3 | extension/sched_gpu_coord.bpf.c | 495 |
| 4 | extension/prefetch_llama_phase.bpf.c | 465 |
| 5 | extension/prefetch_cross_block_v2.bpf.c | 463 |
| 6 | extension/prefetch_stride_multiblock.bpf.c | 424 |
| 7 | extension/prefetch_vllm_phase_transparent.bpf.c | 424 |
| 8 | extension/prefetch_faiss_phase.bpf.c | 419 |
| 9 | extension/uprobe_preempt_multi.bpf.c | 414 |
| 10 | extension/prefetch_vllm_phase.bpf.c | 412 |
| 11 | extension/prefetch_eviction_pid.bpf.c | 409 |
| 12 | extension/prefetch_always_max_qos.bpf.c | 408 |
| 13 | extension/prefetch_proactive_layer.bpf.c | 403 |
| 14 | extension/prefetch_cooperative.bpf.c | 395 |
| 15 | extension/prefetch_faiss_uprobe.bpf.c | 393 |

## Q5. Experiment Configurations Tested

Methodological note: this section reports two numbers.

1. **Confirmed lower bound = 67** distinct policy×workload×configuration combinations recovered from structured JSON/CSV result artifacts.
2. **Exploration envelope = 99** combinations after adding configurations enumerated in `run_exp*.sh` scripts. The delta (**32**) captures combinations declared in scripts but not preserved as uniquely identifiable result artifacts.

There are **60** timestamped JSON result files (mostly FAISS reruns) whose filenames/contents do not encode the active policy; they are excluded from the confirmed distinct-combination count to avoid over-claiming.

### Counts By Workload

| Workload | Confirmed structured combos | Union incl. script-only combos |
| --- | --- | --- |
| faiss | 7 | 11 |
| gemm | 11 | 11 |
| hotspot | 11 | 11 |
| kmeans | 11 | 11 |
| llama.cpp | 10 | 30 |
| mixed_llama+pytorch | 0 | 1 |
| multi-tenant-scheduler | 4 | 4 |
| pytorch | 1 | 2 |
| vllm | 12 | 18 |

### Confirmed Structured Combinations

| Workload | Policy label | Configuration | Evidence source(s) |
| --- | --- | --- | --- |
| faiss | baseline | dbname=SIFT100M, index_key=IVF4096,Flat, nprobes=(1, 4, 16), uvm=True | workloads/faiss/results/SIFT100M_IVF4096_Flat_uvm_baseline.json |
| faiss | baseline | dbname=SIFT20M, index_key=IVF4096,Flat, nprobes=(1, 4, 16), uvm=False | workloads/faiss/results/SIFT20M_IVF4096_Flat_baseline.json |
| faiss | baseline | dbname=SIFT50M, index_key=IVF4096,Flat, nprobes=(1, 4, 16), uvm=True | workloads/faiss/results/SIFT50M_IVF4096_Flat_uvm_baseline.json |
| faiss | cpu | dbname=SIFT20M, index_key=IVF4096,Flat, nprobes=(1, 4, 16), uvm=None | workloads/faiss/results/SIFT20M_IVF4096_Flat_cpu.json |
| faiss | prefetch_adaptive_tree_iter | dbname=SIFT100M, index_key=IVF4096,Flat, nprobes=(1, 4, 16), uvm=True | workloads/faiss/results/SIFT100M_IVF4096_Flat_uvm_prefetch_adaptive_tree.json |
| faiss | prefetch_adaptive_tree_iter | dbname=SIFT50M, index_key=IVF4096,Flat, nprobes=(1, 4, 16), uvm=True | workloads/faiss/results/SIFT50M_IVF4096_Flat_uvm_prefetch_adaptive_tree.json |
| faiss | prefetch_faiss_phase_v2 | dbname=SIFT100M, index_key=IVF4096,Flat, nprobes=(1, 4, 16), uvm=True | workloads/faiss/results/exp_xb4/config_d2_faiss_phase_v2.json |
| gemm | eviction_freq_pid_decay | multi-tenant-memory; workload=gemm; high=1;low=1 | docs/gpu-ext/eval/multi-tenant-memory/results_gemm/policy_comparison_20251208_102321.csv |
| gemm | eviction_freq_pid_decay | multi-tenant-memory; workload=gemm; high=1;low=10 | docs/gpu-ext/eval/multi-tenant-memory/results_gemm/policy_comparison_20251208_102321.csv |
| gemm | no_policy | eval; workload=gemm; high=50;low=50 | docs/gpu-ext/eval/results_gemm/policy_comparison_20251208_031718.csv |
| gemm | no_policy | multi-tenant-memory; workload=gemm; high=50;low=50 | docs/gpu-ext/eval/multi-tenant-memory/results_gemm/policy_comparison_20251208_102321.csv |
| gemm | prefetch_eviction_pid | eval; workload=gemm; high=20;low=80 | docs/gpu-ext/eval/results_gemm/policy_comparison_20251208_031718.csv |
| gemm | prefetch_eviction_pid | multi-tenant-memory; workload=gemm; high=20;low=80 | docs/gpu-ext/eval/multi-tenant-memory/results_gemm/policy_comparison_20251208_102321.csv |
| gemm | prefetch_pid_tree | eval; workload=gemm; high=20;low=0 | docs/gpu-ext/eval/results_gemm/policy_comparison_20251208_031718.csv |
| gemm | prefetch_pid_tree | eval; workload=gemm; high=220;low=80 | docs/gpu-ext/eval/results_gemm/policy_comparison_20251208_031718.csv |
| gemm | prefetch_pid_tree | multi-tenant-memory; workload=gemm; high=0;low=0 | docs/gpu-ext/eval/multi-tenant-memory/results_gemm/policy_comparison_20251208_102321.csv |
| gemm | prefetch_pid_tree | multi-tenant-memory; workload=gemm; high=0;low=20 | docs/gpu-ext/eval/multi-tenant-memory/results_gemm/policy_comparison_20251208_102321.csv |
| gemm | prefetch_pid_tree | multi-tenant-memory; workload=gemm; high=20;low=80 | docs/gpu-ext/eval/multi-tenant-memory/results_gemm/policy_comparison_20251208_102321.csv |
| hotspot | eviction_freq_pid_decay | multi-tenant-memory; workload=hotspot; high=1;low=1 | docs/gpu-ext/eval/multi-tenant-memory/results_hotspot/policy_comparison_20251208_101609.csv |
| hotspot | eviction_freq_pid_decay | multi-tenant-memory; workload=hotspot; high=1;low=10 | docs/gpu-ext/eval/multi-tenant-memory/results_hotspot/policy_comparison_20251208_101609.csv |
| hotspot | no_policy | eval; workload=hotspot; high=50;low=50 | docs/gpu-ext/eval/results_hotspot/policy_comparison_20251208_031325.csv |
| hotspot | no_policy | multi-tenant-memory; workload=hotspot; high=50;low=50 | docs/gpu-ext/eval/multi-tenant-memory/results_hotspot/policy_comparison_20251208_101609.csv |
| hotspot | prefetch_eviction_pid | eval; workload=hotspot; high=20;low=80 | docs/gpu-ext/eval/results_hotspot/policy_comparison_20251208_031325.csv |
| hotspot | prefetch_eviction_pid | multi-tenant-memory; workload=hotspot; high=20;low=80 | docs/gpu-ext/eval/multi-tenant-memory/results_hotspot/policy_comparison_20251208_101609.csv |
| hotspot | prefetch_pid_tree | eval; workload=hotspot; high=20;low=0 | docs/gpu-ext/eval/results_hotspot/policy_comparison_20251208_031325.csv |
| hotspot | prefetch_pid_tree | eval; workload=hotspot; high=220;low=80 | docs/gpu-ext/eval/results_hotspot/policy_comparison_20251208_031325.csv |
| hotspot | prefetch_pid_tree | multi-tenant-memory; workload=hotspot; high=0;low=0 | docs/gpu-ext/eval/multi-tenant-memory/results_hotspot/policy_comparison_20251208_101609.csv |
| hotspot | prefetch_pid_tree | multi-tenant-memory; workload=hotspot; high=0;low=20 | docs/gpu-ext/eval/multi-tenant-memory/results_hotspot/policy_comparison_20251208_101609.csv |
| hotspot | prefetch_pid_tree | multi-tenant-memory; workload=hotspot; high=20;low=80 | docs/gpu-ext/eval/multi-tenant-memory/results_hotspot/policy_comparison_20251208_101609.csv |
| kmeans | eviction_freq_pid_decay | multi-tenant-memory; workload=kmeans; high=1;low=1 | docs/gpu-ext/eval/multi-tenant-memory/results_kmeans/policy_comparison_20251208_103714.csv |
| kmeans | eviction_freq_pid_decay | multi-tenant-memory; workload=kmeans; high=1;low=10 | docs/gpu-ext/eval/multi-tenant-memory/results_kmeans/policy_comparison_20251208_103714.csv |
| kmeans | no_policy | eval; workload=kmeans; high=50;low=50 | docs/gpu-ext/eval/results_kmeans/policy_comparison_20251208_032617.csv |
| kmeans | no_policy | multi-tenant-memory; workload=kmeans; high=50;low=50 | docs/gpu-ext/eval/multi-tenant-memory/results_kmeans/policy_comparison_20251208_103714.csv |
| kmeans | prefetch_eviction_pid | eval; workload=kmeans; high=20;low=80 | docs/gpu-ext/eval/results_kmeans/policy_comparison_20251208_032617.csv |
| kmeans | prefetch_eviction_pid | multi-tenant-memory; workload=kmeans; high=20;low=80 | docs/gpu-ext/eval/multi-tenant-memory/results_kmeans/policy_comparison_20251208_103714.csv |
| kmeans | prefetch_pid_tree | eval; workload=kmeans; high=20;low=0 | docs/gpu-ext/eval/results_kmeans/policy_comparison_20251208_032617.csv |
| kmeans | prefetch_pid_tree | eval; workload=kmeans; high=220;low=80 | docs/gpu-ext/eval/results_kmeans/policy_comparison_20251208_032617.csv |
| kmeans | prefetch_pid_tree | multi-tenant-memory; workload=kmeans; high=0;low=0 | docs/gpu-ext/eval/multi-tenant-memory/results_kmeans/policy_comparison_20251208_103714.csv |
| kmeans | prefetch_pid_tree | multi-tenant-memory; workload=kmeans; high=0;low=20 | docs/gpu-ext/eval/multi-tenant-memory/results_kmeans/policy_comparison_20251208_103714.csv |
| kmeans | prefetch_pid_tree | multi-tenant-memory; workload=kmeans; high=20;low=80 | docs/gpu-ext/eval/multi-tenant-memory/results_kmeans/policy_comparison_20251208_103714.csv |
| llama.cpp | baseline | poc1_serving_r1; cpu_stress=False, ctx=65536, max_concurrency=1, model=ggml-org_gpt-oss-20b-GGUF_gpt-oss-20b-mxfp4.gguf, prompts=20, request_rate=0.2, uvm=True, xcoord=False | workloads/llama.cpp/scripts/xcoord/results/poc1_serving_r1/poc1_20260228_015725/result_uvm_baseline.json |
| llama.cpp | baseline | poc1_serving_r1; cpu_stress=True, ctx=65536, max_concurrency=1, model=ggml-org_gpt-oss-20b-GGUF_gpt-oss-20b-mxfp4.gguf, prompts=20, request_rate=0.2, uvm=True, xcoord=False | workloads/llama.cpp/scripts/xcoord/results/poc1_serving_r1/poc1_20260228_015725/result_uvm_cpu_stress.json |
| llama.cpp | baseline | policy_sweep; bench_uvm_ncmoe64 | workloads/llama.cpp/results/policy_sweep/baseline_no_policy.json |
| llama.cpp | baseline | proactive_layer; n_prompt=512,n_gen=0 | workloads/llama.cpp/results/proactive_layer/baseline_run1.json<br>workloads/llama.cpp/results/proactive_layer/baseline_run2.json<br>workloads/llama.cpp/results/proactive_layer/baseline_run3.json<br>workloads/llama.cpp/results/proactive_layer/baseline_run4.json<br>workloads/llama.cpp/results/proactive_layer/baseline_run5.json |
| llama.cpp | eviction_fifo | policy_sweep; bench_uvm_ncmoe64 | workloads/llama.cpp/results/policy_sweep/eviction_fifo.json |
| llama.cpp | eviction_lfu | policy_sweep; bench_uvm_ncmoe64 | workloads/llama.cpp/results/policy_sweep/eviction_lfu.json |
| llama.cpp | eviction_mru | policy_sweep; bench_uvm_ncmoe64 | workloads/llama.cpp/results/policy_sweep/eviction_mru.json |
| llama.cpp | prefetch_proactive_layer | proactive_layer; chunk=4mb;n_prompt=512,n_gen=0 | workloads/llama.cpp/results/proactive_layer/proactive_4mb_run1.json<br>workloads/llama.cpp/results/proactive_layer/proactive_4mb_run2.json<br>workloads/llama.cpp/results/proactive_layer/proactive_4mb_run3.json<br>workloads/llama.cpp/results/proactive_layer/proactive_4mb_run4.json<br>workloads/llama.cpp/results/proactive_layer/proactive_4mb_run5.json |
| llama.cpp | prefetch_proactive_layer | proactive_layer; chunk=8mb;n_prompt=512,n_gen=0 | workloads/llama.cpp/results/proactive_layer/proactive_8mb_run1.json<br>workloads/llama.cpp/results/proactive_layer/proactive_8mb_run2.json |
| llama.cpp | xcoord | poc1_serving_r1; cpu_stress=True, ctx=65536, max_concurrency=1, model=ggml-org_gpt-oss-20b-GGUF_gpt-oss-20b-mxfp4.gguf, prompts=20, request_rate=0.2, uvm=True, xcoord=True | workloads/llama.cpp/scripts/xcoord/results/poc1_serving_r1/poc1_20260228_015725/result_uvm_xcoord.json |
| multi-tenant-scheduler | kfunc_only | multi-tenant-scheduler; summary_mode | docs/gpu-ext/eval/multi-tenant-scheduler/kfunc_preempt_results/mode_summary.csv |
| multi-tenant-scheduler | native | multi-tenant-scheduler; summary_mode | docs/gpu-ext/eval/multi-tenant-scheduler/kfunc_preempt_results/mode_summary.csv |
| multi-tenant-scheduler | timeslice_kfunc | multi-tenant-scheduler; summary_mode | docs/gpu-ext/eval/multi-tenant-scheduler/kfunc_preempt_results/mode_summary.csv |
| multi-tenant-scheduler | timeslice_only | multi-tenant-scheduler; summary_mode | docs/gpu-ext/eval/multi-tenant-scheduler/kfunc_preempt_results/mode_summary.csv |
| pytorch | coord | xcoord_gnn_v2; B5_no_stress_boost; epochs=5, nodes=5000000, uvm=True, warmup=1 | workloads/pytorch/scripts/xcoord/results/xcoord_gnn_v2/B5_no_stress_boost/result_B5.json |
| vllm | baseline | dataset-name=sharegpt, dataset-path=/home/yunwei37/workspace/gpu/schedcp/workloads/vllm/datasets/ShareGPT_V3_unfiltered_cleaned_split.json, model=Qwen/Qwen3-30B-A3B-FP8, num-prompts=100, request-rate=5 | workloads/vllm/results/uvm_baseline_results_20251208_020935.json<br>workloads/vllm/results/uvm_baseline_results_20251208_022225.json<br>workloads/vllm/results/uvm_baseline_results_20251208_022631.json<br>workloads/vllm/results/uvm_baseline_results_20251208_023149.json |
| vllm | baseline | dataset-name=sharegpt, dataset-path=datasets/ShareGPT_V3_unfiltered_cleaned_split.json, model=Qwen/Qwen3-30B-A3B-FP8, num-prompts=100, request-rate=5, sharegpt-output-len=512 | workloads/vllm/results/uvm_baseline_results_20260216_233956.json<br>workloads/vllm/results/uvm_baseline_results_20260216_234528.json<br>workloads/vllm/results/uvm_baseline_results_latest.json |
| vllm | baseline | mode=uvm, model=Qwen/Qwen3-30B-A3B-FP8, prompts=100 | workloads/vllm/results/exp_n6_vllm/config_a_baseline.json<br>workloads/vllm/results/exp_vllm_rerun/config_a_baseline.json<br>workloads/vllm/results/exp_vllm_retest_fix/v2_uvm_baseline.json<br>workloads/vllm/results/exp_vllm_transparent/config_a_baseline.json<br>workloads/vllm/results/exp_xb2/config_a_uvm_baseline.json<br>workloads/vllm/results/submodule_test_uvm.json<br>workloads/vllm/results/submodule_verify_uvm.json<br>workloads/vllm/results/verify_uvm_baseline.json |
| vllm | cpu_offload | mode=cpu_offload, model=Qwen/Qwen3-30B-A3B-FP8, prompts=100 | workloads/vllm/results/exp_vllm_retest_fix/v1_cpu_offload.json<br>workloads/vllm/results/submodule_test_cpu_offload.json<br>workloads/vllm/results/verify_cpu_offload.json |
| vllm | prefetch_always_max | mode=uvm, model=Qwen/Qwen3-30B-A3B-FP8, prompts=100 | workloads/vllm/results/exp_vllm_rerun/config_b_always_max.json<br>workloads/vllm/results/exp_vllm_transparent/config_b_always_max.json<br>workloads/vllm/results/exp_xb2/config_b_always_max.json<br>workloads/vllm/results/verify_uvm_always_max.json |
| vllm | prefetch_always_max_cycle_moe | mode=uvm, model=Qwen/Qwen3-30B-A3B-FP8, prompts=100 | workloads/vllm/results/exp_n6_vllm/config_c_always_max_cycle_moe.json<br>workloads/vllm/results/exp_vllm_rerun/config_c_always_max_cycle_moe.json<br>workloads/vllm/results/exp_vllm_retest_fix/v3_always_max_cycle_moe.json |
| vllm | prefetch_cross_block_v2 | mode=uvm, model=Qwen/Qwen3-30B-A3B-FP8, prompts=100 | workloads/vllm/results/exp_vllm_rerun/config_d_xb_blind.json<br>workloads/vllm/results/exp_xb2/config_c_blind_xb.json |
| vllm | prefetch_faiss_phase | mode=uvm, model=Qwen/Qwen3-30B-A3B-FP8, prompts=100 | workloads/vllm/results/exp_xb2/config_e_faiss_phase.json |
| vllm | prefetch_serving_adaptive | mode=uvm, model=Qwen/Qwen3-30B-A3B-FP8, prompts=100 | workloads/vllm/results/exp_vllm_rerun/config_f_serving_adaptive.json |
| vllm | prefetch_vllm_phase | mode=uvm, model=Qwen/Qwen3-30B-A3B-FP8, prompts=100 | workloads/vllm/results/exp_n6_vllm/config_n6_vllm_phase.json |
| vllm | prefetch_vllm_phase_transparent | mode=uvm, model=Qwen/Qwen3-30B-A3B-FP8, prompts=100 | workloads/vllm/results/exp_vllm_transparent/config_c_transparent.json |
| vllm | xb_direction | mode=uvm, model=Qwen/Qwen3-30B-A3B-FP8, prompts=100 | workloads/vllm/results/exp_vllm_rerun/config_e_xb_direction.json |

### Script-Enumerated Combinations Without Unique Structured Artifact Evidence

| Workload | Policy label | Configuration | Script |
| --- | --- | --- | --- |
| faiss | baseline | phase_detection; dataset=SIFT100M;index=IVF4096,Flat;uvm=1;nprobe=1,4,16; no_bpf | workloads/run_exp_phase_detection.sh |
| faiss | prefetch_always_max_cycle_moe | phase_detection; dataset=SIFT100M;index=IVF4096,Flat;uvm=1;nprobe=1,4,16; always_max_cycle_moe | workloads/run_exp_phase_detection.sh |
| faiss | prefetch_faiss_phase | phase_detection; dataset=SIFT100M;index=IVF4096,Flat;uvm=1;nprobe=1,4,16; faiss_heuristic | workloads/run_exp_phase_detection.sh |
| faiss | prefetch_faiss_uprobe | phase_detection; dataset=SIFT100M;index=IVF4096,Flat;uvm=1;nprobe=1,4,16; faiss_uprobe | workloads/run_exp_phase_detection.sh |
| llama.cpp | baseline | adaptive_threshold; bench_uvm_ncmoe64; pp=512,tg=128,reps=5 | workloads/llama.cpp/run_exp_adaptive_threshold.sh |
| llama.cpp | baseline | two_tenant; single_tenant_llama; model=gpt-oss-20b;ctx=65536;prompts=100;request_rate=0.2 | workloads/llama.cpp/run_exp5_two_tenant.sh |
| llama.cpp | prefetch_adaptive_sequential | adaptive_threshold; bench_uvm_ncmoe64; pp=512,tg=128,reps=5; pct=100 | workloads/llama.cpp/run_exp_adaptive_threshold.sh |
| llama.cpp | prefetch_adaptive_sequential | adaptive_threshold; bench_uvm_ncmoe64; pp=512,tg=128,reps=5; pct=25 | workloads/llama.cpp/run_exp_adaptive_threshold.sh |
| llama.cpp | prefetch_adaptive_sequential | adaptive_threshold; bench_uvm_ncmoe64; pp=512,tg=128,reps=5; pct=50 | workloads/llama.cpp/run_exp_adaptive_threshold.sh |
| llama.cpp | prefetch_adaptive_tree_iter | adaptive_threshold; bench_uvm_ncmoe64; pp=512,tg=128,reps=5; threshold=1 | workloads/llama.cpp/run_exp_adaptive_threshold.sh |
| llama.cpp | prefetch_adaptive_tree_iter | adaptive_threshold; bench_uvm_ncmoe64; pp=512,tg=128,reps=5; threshold=10 | workloads/llama.cpp/run_exp_adaptive_threshold.sh |
| llama.cpp | prefetch_adaptive_tree_iter | adaptive_threshold; bench_uvm_ncmoe64; pp=512,tg=128,reps=5; threshold=25 | workloads/llama.cpp/run_exp_adaptive_threshold.sh |
| llama.cpp | prefetch_adaptive_tree_iter | adaptive_threshold; bench_uvm_ncmoe64; pp=512,tg=128,reps=5; threshold=51 | workloads/llama.cpp/run_exp_adaptive_threshold.sh |
| llama.cpp | prefetch_always_max | adaptive_threshold; bench_uvm_ncmoe64; pp=512,tg=128,reps=5 | workloads/llama.cpp/run_exp_adaptive_threshold.sh |
| llama.cpp | prefetch_always_max_cycle_moe | phase_detection; pp=512,tg=128,reps=3; baseline_always_max_cycle_moe | workloads/run_exp_phase_detection.sh |
| llama.cpp | prefetch_llama_phase | phase_detection; pp=512,tg=128,reps=3; mode=0;radius=32;xb=0 | workloads/run_exp_phase_detection.sh |
| llama.cpp | prefetch_llama_phase | phase_detection; pp=512,tg=128,reps=3; mode=0;radius=32;xb=1 | workloads/run_exp_phase_detection.sh |
| llama.cpp | prefetch_llama_phase | phase_detection; pp=512,tg=128,reps=3; mode=1;radius=32;xb=0 | workloads/run_exp_phase_detection.sh |
| llama.cpp | prefetch_llama_phase | phase_detection; pp=512,tg=128,reps=3; mode=1;radius=8;xb=0 | workloads/run_exp_phase_detection.sh |
| llama.cpp | prefetch_llama_phase | phase_detection; pp=512,tg=128,reps=3; mode=2;radius=32;xb=0 | workloads/run_exp_phase_detection.sh |
| llama.cpp | prefetch_llama_phase | phase_detection; pp=512,tg=128,reps=3; mode=3;radius=32;xb=0 | workloads/run_exp_phase_detection.sh |
| llama.cpp | prefetch_max_passive_mru | adaptive_threshold; bench_uvm_ncmoe64; pp=512,tg=128,reps=5 | workloads/llama.cpp/run_exp_adaptive_threshold.sh |
| llama.cpp | prefetch_template_belady | adaptive_threshold; bench_uvm_ncmoe64; pp=512,tg=128,reps=5; layers=36;protect=3;profile=layer_va_ranges.json | workloads/llama.cpp/run_exp_adaptive_threshold.sh |
| llama.cpp | prefetch_template_belady | adaptive_threshold; bench_uvm_ncmoe64; pp=512,tg=128,reps=5; layers=36;protect=3;profile=none | workloads/llama.cpp/run_exp_adaptive_threshold.sh |
| mixed_llama+pytorch | baseline | two_tenant; colocated_default_uvm; llama_ctx=65536;gnn_nodes=8M;epochs=25;request_rate=0.2 | workloads/llama.cpp/run_exp5_two_tenant.sh |
| pytorch | baseline | two_tenant; single_tenant_gnn; dataset=random;nodes=8M;epochs=25;uvm=1 | workloads/llama.cpp/run_exp5_two_tenant.sh |
| vllm | baseline | phase_detection; mode=uvm;prompts=100; no_bpf | workloads/run_exp_phase_detection.sh |
| vllm | prefetch_always_max_cycle_moe | phase_detection; mode=uvm;prompts=100; always_max_cycle_moe | workloads/run_exp_phase_detection.sh |
| vllm | prefetch_vllm_phase | phase_detection; mode=uvm;prompts=100; mode=0;radius=32;xb_decode=0 | workloads/run_exp_phase_detection.sh |
| vllm | prefetch_vllm_phase | phase_detection; mode=uvm;prompts=100; mode=0;radius=32;xb_decode=1 | workloads/run_exp_phase_detection.sh |
| vllm | prefetch_vllm_phase | phase_detection; mode=uvm;prompts=100; mode=1;radius=32;xb_decode=0 | workloads/run_exp_phase_detection.sh |
| vllm | prefetch_vllm_phase | phase_detection; mode=uvm;prompts=100; mode=2;radius=32;xb_decode=0 | workloads/run_exp_phase_detection.sh |

### Unlabeled JSON Caveat

Representative examples of excluded unlabeled JSON reruns:

| Example unlabeled JSON file |
| --- |
| workloads/faiss/results/SIFT100M_IVF4096_Flat_uvm_20260216_185543.json |
| workloads/faiss/results/SIFT100M_IVF4096_Flat_uvm_20260217_090955.json |
| workloads/faiss/results/SIFT100M_IVF4096_Flat_uvm_20260217_103137.json |
| workloads/faiss/results/SIFT100M_IVF4096_Flat_uvm_20260228_004607.json |
| workloads/faiss/results/SIFT100M_IVF4096_Flat_uvm_20260228_004916.json |
| workloads/faiss/results/SIFT100M_IVF4096_Flat_uvm_20260228_005208.json |
| workloads/faiss/results/SIFT100M_IVF4096_Flat_uvm_20260228_010212.json |
| workloads/faiss/results/SIFT100M_IVF4096_Flat_uvm_20260228_010910.json |
| workloads/faiss/results/SIFT100M_IVF4096_Flat_uvm_20260228_011319.json |
| workloads/faiss/results/SIFT100M_IVF4096_Flat_uvm_20260228_012238.json |
| workloads/faiss/results/SIFT100M_IVF4096_Flat_uvm_20260228_012756.json |
| workloads/faiss/results/SIFT100M_IVF4096_Flat_uvm_20260228_014252.json |
| workloads/faiss/results/SIFT100M_IVF4096_Flat_uvm_20260228_235730.json |
| workloads/faiss/results/SIFT100M_IVF4096_Flat_uvm_20260301_000039.json |
| workloads/faiss/results/SIFT100M_IVF4096_Flat_uvm_20260301_003018.json |

These 15 examples are the start of a larger set of 60 timestamped JSONs, dominated by `workloads/faiss/results/SIFT100M_IVF4096_Flat_uvm_*.json` files that do not record the active policy in the filename or the JSON payload.

## Q6. Failure-Related Commits

| Exact keyword | Matched commits |
| --- | --- |
| bug | 5 |
| crash | 1 |
| fix | 19 |
| regression | 4 |
| revert | 2 |

| Category | Matched commits |
| --- | --- |
| correctness | 21 |
| rollback | 2 |
| runtime_stability | 1 |

| Area grouping | Matched commits |
| --- | --- |
| docs | 7 |
| extension+docs | 2 |
| extension+workloads | 1 |
| extension+workloads+docs | 1 |
| other | 2 |
| workloads | 4 |
| workloads+docs | 4 |
| docs_only | 7 |
| extension_or_workloads | 12 |
| other_only | 2 |

No commit messages matched `workaround`, `Xid`, `verifier`, `hang`, or `broken` in the scanned history. Most failure-tagged commits are generic `fix` commits; only a subset directly touches policy/workload code.

### Matched Commit List

| Date | Hash | Keywords | Areas | AI-assisted? | Subject |
| --- | --- | --- | --- | --- | --- |
| 2026-03-07 | 5fc93594 | bug | extension, docs | Yes | docs: rewrite extension/README.md with complete policy inventory, remove outdated docs |
| 2026-03-07 | 1fe87b25 | fix | workloads, docs | Yes | P5 vLLM retest + paper comparison: complete gpu_block_access fix validation |
| 2026-03-07 | b422b9f4 | fix, regression | extension, workloads, docs | Yes | fix: move eviction logic from gpu_block_access to gpu_block_activate across all BPF policies |
| 2026-03-05 | 4787edee | fix, bug | extension, workloads | Yes | vLLM full re-benchmark: 6 configs, all BPF policies effective (+9-10%) |
| 2026-03-05 | f01e5eab | fix | workloads | Yes | vLLM submodule verification: baseline + always_max benchmark results |
| 2026-03-04 | 89152199 | fix, bug, regression | workloads, docs | Yes | GNN cross-block prefetch: 3.29x speedup with V1 allocator fix |
| 2026-03-04 | 975d39e7 | fix, revert, regression | workloads | Yes | fix: revert PyTorch UVM allocator to V1 (plain cudaMallocManaged) |
| 2026-03-04 | 60d858f7 | fix, revert, regression | workloads | Yes | fix: revert PyTorch UVM allocator to V1 (plain cudaMallocManaged) |
| 2026-03-03 | 593c88de | fix | docs | Yes | docs: refine gpu_preempt_kfunc_plan — minimize kernel changes, add design rationale |
| 2026-03-03 | 50fdeb47 | fix | docs | Yes | docs: refine gpu_preempt_kfunc_plan — minimize kernel changes, add design rationale |
| 2026-02-28 | 443cb913 | fix, bug | docs | No | docs: update xcoord_plan with POC-1 completion status and next steps |
| 2026-02-25 | 2476eb6d | fix | docs | Yes | docs: download MSched paper and fix gitmodules submodule path |
| 2026-02-17 | d004fbf1 | fix | extension, docs | Yes | Fix README accuracy: simplify kernel build, add missing policies, gitignore prefetch_stride binary |
| 2026-02-17 | 609ed310 | fix | docs | Yes | Rewrite top-level README with build/install instructions for kernel module and eBPF policies |
| 2026-02-17 | 2ded5043 | fix | workloads, docs | Yes | Update workloads README to reflect new atomic script architecture |
| 2026-02-17 | a523c886 | fix, crash | workloads | Yes | Fix llama.cpp UVM warmup-then-migrate and exp1 model path |
| 2026-02-17 | bf75d9f4 | fix | workloads, docs | Yes | Update workload READMEs and fix UVM allocators for NVIDIA driver 575+ |
| 2026-02-16 | f961dc3e | fix | other | Yes | Fix Makefile: update src reference to extension |
| 2026-01-29 | 86bda8d4 | bug | docs | No | Add comprehensive canonical bug list for GPU synchronization issues |
| 2025-12-08 | b3dc2b43 | fix | docs | No | fix: Update run_experiment and main functions to use custom output directory for results |
| 2025-12-07 | ca961685 | fix | other | No | Add PageRank kernel and update existing kernels for fixed workload design |
