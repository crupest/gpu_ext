# Q2 Safety Taxonomy for `gpu_ext`

## Scope and source coverage

Reviewed sources:

- [MEMORY.md](/home/yunwei37/.claude/projects/-home-yunwei37-workspace-gpu-gpu-ext/memory/MEMORY.md)
- [feedback_codex_runs_experiments.md](/home/yunwei37/.claude/projects/-home-yunwei37-workspace-gpu-gpu-ext/memory/feedback_codex_runs_experiments.md)
- [feedback_codex_writes_code.md](/home/yunwei37/.claude/projects/-home-yunwei37-workspace-gpu-gpu-ext/memory/feedback_codex_writes_code.md)
- [CLAUDE.md](/home/yunwei37/workspace/gpu/gpu_ext/CLAUDE.md)
- [docs/cross_block_prefetch_plan.md](/home/yunwei37/workspace/gpu/gpu_ext/docs/cross_block_prefetch_plan.md)
- [docs/xcoord_plan.md](/home/yunwei37/workspace/gpu/gpu_ext/docs/xcoord_plan.md)
- [docs/cross_block_prefetch_mechanism.md](/home/yunwei37/workspace/gpu/gpu_ext/docs/cross_block_prefetch_mechanism.md)
- Additional `docs/` safety-hit files that added unique incidents or clarified root cause: [docs/msched_reproduction_plan.md](/home/yunwei37/workspace/gpu/gpu_ext/docs/msched_reproduction_plan.md), [docs/retest_plan_gpu_block_access_fix.md](/home/yunwei37/workspace/gpu/gpu_ext/docs/retest_plan_gpu_block_access_fix.md), [docs/cross_block_prefetch_plan_v2.md](/home/yunwei37/workspace/gpu/gpu_ext/docs/cross_block_prefetch_plan_v2.md)
- `extension/*.bpf.c` comment hits for `workaround|bug|fix|XXX|HACK|NOTE|WARNING|SAFETY|verifier`

Interpretation rule:

- Repeated mentions were merged only when they clearly referred to the same concrete incident with the same root cause and recovery.
- `Would driver-mod be worse?` means whether a direct driver modification would likely have increased blast radius or recovery cost relative to the observed eBPF/loader/workload bug.

## Complete event table

| # | Date | Event type | Description | Root cause | How discovered | Recovery method | Severity | Would driver-mod be worse? |
|---|---|---|---|---|---|---|---|---|
| 1 | 2026-02-23 | LOGIC_BUG | `llama-server` 120B UVM baseline segfaulted after 3/20 requests. | Unstable 120B UVM path in workload code. | POC-0 benchmark recorded only 3 successful requests. | Avoided using that baseline for conclusions; later stabilized environment and scenarios. | High | No |
| 2 | 2026-02-26 | XID_FAULT | Using `move_head` in `chunk_activate` triggered Xid 31 / `FAULT_PDE`. | Newly activated chunk could be evicted before page-table setup finished. | Safety-bug analysis in `msched_reproduction_plan` and later memory notes. | Use `move_tail` or `return 0` in `chunk_activate`. | Critical | Yes |
| 3 | 2026-02-26 | XID_FAULT | LFU eviction crashed with Xid 31 under heavy fault handling. | BPF hash-map lookups in the fault-handler hot path were too slow and timed out the GPU MMU. | LFU evaluation failed with Xid 31; docs tied it to HASH-map hot-path cost. | Replace HASH maps with `BPF_MAP_TYPE_PERCPU_ARRAY`; stop using LFU on that path. | Critical | Yes |
| 4 | 2026-02-26 | VERIFIER_REJECT | Pointer arithmetic on pointer registers was rejected by the BPF verifier. | Verifier forbids shifting/XORing pointer-typed registers. | Load-time verifier failure while implementing chunk hashing. | Convert pointer to scalar via `bpf_probe_read_kernel`. | Low | Yes |
| 5 | 2026-02-26 | PERF_REGRESSION | MRU eviction was a disaster (`18.97/9.62 tok/s`, about `-83%`). | `move_head` list manipulation cost overwhelmed any theoretical MRU benefit. | Phase-1 eviction benchmark in `msched_reproduction_plan`. | Abandon MRU in favor of safer/default or passive strategies. | High | No |
| 6 | 2026-02-26 | PERF_REGRESSION | `stride` / BYPASS prefetch behaved almost like disabling prefetch (`-68%` to `-76%`). | Returning BYPASS without useful prefetch skipped NVIDIA’s default prefetch on most faults. | Phase-2 prefetch benchmark showed `stride ≈ none`. | Return `DEFAULT (0)` when unsure; do not use BYPASS as a no-op. | High | No |
| 7 | 2026-02-27 | VERIFIER_REJECT | Proactive-layer implementation with a `for` loop over layers was rejected (`infinite loop detected`). | Loop structure was not verifier-friendly. | Load-time verifier error during `prefetch_proactive_layer` development. | Replace with O(1) boundary check on `current_layer + 1`. | Low | Yes |
| 8 | 2026-02-27 to 2026-03-07 | SYSTEM_HANG | Orphaned `struct_ops` after dirty shutdown could leave the extension stack hanging. | Eviction process exited without clean unregister, leaving stale `struct_ops` references. | Cleanup attempts hung or failed; memory and plan docs added dedicated cleanup steps. | Run `cleanup_struct_ops_tool`; if needed use `BPF_MAP_DELETE_ELEM`; worst case reload NVIDIA stack. | High | Yes |
| 9 | 2026-02-27 | BUILD_FAIL | `sched_ext` UEI/macros hit toolchain incompatibility with clang 18. | UEI path required 32-bit atomics not supported by local clang 18 setup. | xCoord build path analysis documented toolchain incompatibility. | Bypass UEI macros and use direct libbpf open/load/attach flow. | Medium | No |
| 10 | 2026-02-27 | LOGIC_BUG | xCoord `select_cpu()` bypass caused boost logic to be skipped. | GPU tasks were inserted directly from `select_cpu`, so `enqueue()` never ran. | POC-1 debug counters showed `gpu_boosted=1/39021`. | Stop inserting GPU tasks in `select_cpu`; let `enqueue()` handle boosting. | High | No |
| 11 | 2026-02-27 | LOGIC_BUG | xCoord had no direct PID matching for the 20B fit-in-VRAM case. | `uvm_worker_pids` stayed empty when there was no UVM paging. | 20B POC did not boost the intended GPU process. | Add `gpu_process_pids` map and `-p PID` registration path. | Medium | No |
| 12 | 2026-02-27 | LOGIC_BUG | FIFO and PRIQ semantics were mixed in the same DSQ design. | Same DSQ was being used with incompatible insertion semantics. | POC-1 bug audit before the fixed Round 2 scheduler. | Split GPU and non-GPU traffic into separate DSQs. | Medium | No |
| 13 | 2026-02-28 | LOGIC_BUG | UVM worker PID mismatch caused `gpu_boosted=0` even when `gpu_state_map` showed thrashing. | `gpu_state_map` used owner TGID, but the actual fault handler ran in a kernel worker TGID. | `gpu_state_map` showed high fault rate while sched-ext counters stayed at zero. | Add `uvm_worker_pids` map and check worker PID first. | High | No |
| 14 | 2026-02-28 | LOGIC_BUG | 120B `llama-server` runs hit CUDA OOM because stale GPU processes still occupied VRAM. | Residual GPU processes left hundreds of MiB allocated, enough to break cuBLAS/UVM allocations. | 120B serving crash/OOM disappeared after environment cleanup. | Always run `cleanup_gpu.py` and verify `0MiB` GPU use before experiments. | High | No |
| 15 | 2026-02-28 | LOGIC_BUG | `prefetch_cross_block_v2` never executed cross-block logic because its kprobe was not attached. | Loader only attached `struct_ops`, not `capture_va_block`. | Debug counters showed `kprobe fires: 0`, `wq scheduled: 0`. | Add explicit `bpf_program__attach()` for the kprobe. | High | No |
| 16 | 2026-02-28 | PERF_REGRESSION | Once fixed, cross-block on 120B still regressed decode throughput by about `-28%`. | Extra async DMA and lock contention competed with demand-fault traffic on PCIe. | Real benchmark after kprobe attach showed all XB modes at ~`60 tok/s` vs ~`84 tok/s` baseline. | Disable XB for 1.84x-oversubscribed llama.cpp; keep only `always_max`. | High | No |
| 17 | 2026-02-28 | PERF_REGRESSION | Old sched-ext vtime/global-DSQ design made GNN `2.3x` slower (`82.10s`). | Global DSQ/vtime dispatch overhead dominated a workload with weak CPU-GPU coupling. | GNN Round-3 xCoord benchmark. | Rework scheduler to prefer local fast paths; stop using that design as baseline. | High | No |
| 18 | 2026-02-28 | PERF_REGRESSION | Even without stress, xCoord boost added about `+3%` overhead on GNN. | Scheduler overhead exceeded the tiny benefit available in a compute-bound workload. | GNN `B5 no stress + boost` result. | Treat GNN UVM as a poor xCoord target; avoid forcing sched-ext there. | Medium | No |
| 19 | 2026-02-28 | LOGIC_BUG | 20B serving + 120B batch sharing one GPU could crash the 20B side with CUDA OOM. | Start order and VRAM occupancy mattered when two UVM-heavy CUDA processes coexisted. | Multi-workload experiment note in `xcoord_plan`. | Start 20B first so it occupies VRAM; launch 120B second in UVM mode. | High | No |
| 20 | 2026-02-28 | LOGIC_BUG | Cleanup used `pkill -f sched_gpu_serving` and matched its own Python command line. | Pattern match was too broad. | xCoord stale-cleanup bug review. | Switch to `pkill -x` exact-name matching. | Medium | No |
| 21 | 2026-02-28 | LOGIC_BUG | `SCX_ENQ_DSQ_PRIQ` mixed with kernel `enq_flags` and caused runtime scheduler failure (`invalid enq_flags`). | Extra PRIQ flag was ORed into flags the kernel already owned. | R4 scheduler run crashed with explicit runtime error. | Pass kernel `enq_flags` through unchanged; use FIFO DSQ. | High | No |
| 22 | 2026-03-01 | SYSTEM_HANG | A `120B + 120B` UVM scenario OOM-crashed the whole machine twice and required hard reboot. | Two 120B UVM processes consumed about `120GB`, exceeding available `125GB` host RAM once the rest of system use was included. | E11 attempt in `xcoord_plan` recorded two crashes. | Abandon that configuration; switch to `20B + FAISS` for FPRS activation. | Critical | No |
| 23 | 2026-03-01 | PERF_REGRESSION | Blind `sched_gpu_baseline` on vLLM serving dropped throughput by `-62%` and worsened TPOT/P99. | Global SHARED_DSQ overhead and boosting the wrong threads outweighed any benefit. | E3 vLLM stress experiment. | Stop using blind boost; require GPU-informed worker/process selection. | High | No |
| 24 | 2026-03-01 | PERF_REGRESSION | `sched_gpu_baseline` on FAISS without `gpu_ext` added `+67%` to `+759%` overhead. | Barrier-heavy FAISS plus global DSQ dispatch overhead produced massive scheduler cost. | FAISS stress results summarized in memory and `xcoord_plan`. | Do not use blind sched-ext on FAISS; require local DSQ or GPU-state-aware targeting. | High | No |
| 25 | 2026-03-01 | PERF_REGRESSION | `coord` with `1ms` throttle regressed FAISS `nprobe=1` by about `+15%`. | Dispatch overhead exceeded any CPU-throttling benefit. | FAISS + coord A/B results. | Increase interval to `5ms` and treat result as workload mismatch. | Medium | No |
| 26 | 2026-03-01 | LOGIC_BUG | `coord v1` used a one-way `global_thrashing` latch, invalidating its “closed-loop” story and E10 comparison. | Once set, the latch never cleared. | Honest post-hoc review of E10 runs in `xcoord_plan`. | Rewrite as FPRS (`coord v2`) with real feedback and decay. | High | No |
| 27 | 2026-03-01 | LOGIC_BUG | FPRS non-GPU backpressure throttled network/system tasks and stretched the LC first request to `45s`. | Backpressure was applied too broadly, not just to BE GPU tasks. | FPRS bug table in `xcoord_plan`. | Remove non-GPU backpressure; throttle only BE tasks in `gpu_state_map`. | High | No |
| 28 | 2026-03-01 | LOGIC_BUG | FPRS treated stale `fault_rate` as live pressure and kept throttling forever. | `fault_rate` did not decay when a process went idle. | FPRS bug table and controller analysis. | Add staleness check; treat updates older than `2s` as `fault_rate=0`. | High | No |
| 29 | 2026-03-01 | PERF_REGRESSION | FPRS controller response was about `50s` too slow to matter. | `max_integral` was too large and `ki_gain` too small. | FPRS bug table showed it took too long to reach useful throttle. | Reduce `max_integral` and raise `ki_gain` to get ~`500ms` response. | Medium | No |
| 30 | 2026-03-01 | LOGIC_BUG | First QoS-eviction design put protection in `gpu_block_access`, but `used=0` showed that hook was not actually protecting anything. | Protection logic was placed on a hook that did not fire in the intended scenario. | E12 first-run stats: `activate=1.8M`, `used=0`, `lc_prot=0`. | Move protection logic to `chunk_activate`. | High | Mixed |
| 31 | 2026-03-01 | LOGIC_BUG | E12 also lost the LC process to CUDA OOM, so `lc_fr=0` invalidated the feedback signal. | 20B server died under FAISS VRAM pressure before feedback could regulate. | First QoS-eviction experiment logged `lc_fr=0` despite intended contention. | Use more robust startup order or smaller FAISS dataset. | High | No |
| 32 | 2026-03-04 | LOGIC_BUG | `uvm_allocator.c` added `cudaMemAdviseSetPreferredLocation=CPU`, doubling migration traffic and slowing GNN roughly `2x`. | Userspace allocator forced evicted pages back to CPU, overriding the intended UVM/prefetch behavior. | V1 vs V3 allocator comparison (`70s` vs `140s`) exposed it. | Revert to plain `cudaMallocManaged`; mark file `DO NOT MODIFY`. | Critical | No |
| 33 | 2026-03-04 | PERF_REGRESSION | FAISS search-phase cross-block stayed harmful even with direction filtering. | Random posting-list access preserved enough weak directionality that many useless prefetches still passed. | Exp-XB4 config C was slightly worse than `always_max`, especially in search. | Add explicit phase detection so build keeps XB and search disables it. | Medium | No |
| 34 | 2026-03-05 | LOGIC_BUG | The first vLLM results used wrong `cwd` and missing benchmark args, creating a false “always_max explodes P99” conclusion. | Benchmark script ran from the wrong directory and omitted request-rate/output-length parameters. | Full rerun in §7.8 invalidated the earlier result set. | Fix `cwd` and benchmark flags; rerun all six configs. | High | No |
| 35 | 2026-03-05 | LOGIC_BUG | FAISS uprobe path bug meant the hook watched the wrong `_swigfaiss.so`. | Path pointed at build-dir `.so`, not the inode Python loaded at runtime. | Counters showed `uprobe_build=0`; corrected path restored hits. | Point the uprobe at `.../python/faiss/_swigfaiss.so`. | Medium | No |
| 36 | 2026-03-05 | LOGIC_BUG | FAISS phase v1 never switched to SEARCH. | Direction-consistency was not a reliable discriminator because SEARCH still looked partly directional. | `search_skip=0`, `phase→SEARCH=0`, and poor `np=1` results exposed it. | Replace heuristic with exact `+1 block` stride detector (phase v2). | Medium | No |
| 37 | 2026-03-05 | PERF_REGRESSION | FAISS `cycle_moe` eviction hurt search `nprobe=1` badly (`9.78s` vs `5.49s` with default LRU). | T1 protection kept BUILD-hot chunks resident and blocked SEARCH-needed clusters from entering VRAM. | D2 vs D3 comparison in Exp-XB4. | Use `default_lru` for FAISS search-facing policies. | High | No |
| 38 | 2026-03-05 | LOGIC_BUG | FAISS kprobe optimization checked `va_space` before phase detection and got stuck in SEARCH. | SEARCH skipped `va_space`, then the prefetch hook returned early before state could update. | D4 “buggy” behavior and code audit. | Run phase detection first; only consult `va_space` afterward. | Medium | No |
| 39 | 2026-03-05 | PERF_REGRESSION | Throttled XB hurt llama.cpp. | Fault rate was not a good proxy for “safe to prefetch more” under PCIe saturation. | `throttled_xb` results in plan docs and summary tables. | Stop using fault-rate gating for llama XB. | Medium | No |
| 40 | 2026-03-05 | PERF_REGRESSION | Narrow/phase-adaptive decode prefetch on llama.cpp was catastrophic. | Smaller prefetch regions destroyed batched PCIe efficiency and created many small DMAs. | N7 results showed `-23%` to `-58%` regressions. | Keep `always_max` for decode. | High | No |
| 41 | 2026-03-05 | PERF_REGRESSION | Narrow/phase-adaptive decode prefetch on vLLM was also harmful. | Same batching loss appeared, though at smaller magnitude than llama.cpp. | vLLM phase-adaptive table showed TPOT regressions. | Keep `always_max + cycle_moe` as the uniform policy. | Medium | No |
| 42 | 2026-03-06 | PERF_REGRESSION | GNN stride multi-block prefetch was `-46%` vs `always_max`. | `K=6` lookahead over-consumed PCIe bandwidth and displaced pages that were immediately needed. | N1 GNN benchmark. | Keep 1-block direction-aware XB; do not use wide lookahead. | High | No |
| 43 | 2026-03-06 | LOGIC_BUG | Transparent GNN proactive path used app-PID filtering in `struct_ops`/kprobe and therefore did nothing. | Those hooks ran in UVM kernel-thread context, so `bpf_get_current_pid_tgid()` was not the app PID. | `prefetch_hook=0`, `xb=0`, `sync=0` in G3 v1. | Remove PID filtering from `struct_ops` and kprobe path. | High | No |
| 44 | 2026-03-06 | LOGIC_BUG | Transparent GNN proactive path hardcoded the wrong `libcudart.so.12` path, so the sync uprobe never fired. | Loader targeted top-level `.venv` instead of `workloads/pytorch/.venv`. | `sync=0` until path correction. | Point the uprobe at the workload’s actual `libcudart.so.12`. | Medium | No |
| 45 | 2026-03-06 | LOGIC_BUG | Transparent vLLM uprobe targeted `paged_attention`, but the engine actually used FlashAttention. | Hook target did not exist in the runtime backend path. | Transparent vLLM experiment showed the uprobe never triggered. | Treat it as transparent no-op; target the real backend if phase hooks are needed. | Medium | No |
| 46 | 2026-03-06 | PERF_REGRESSION | vLLM phase-gated XB fell back to baseline and worsened P99. | Decode also benefited from XB at 1.175x oversub, and prefill XB had a `48%` migrate-fail rate. | N6v results showed `TPOT≈baseline`, `P99=73ms`, far worse than config C. | Do not gate XB by phase for vLLM serving. | High | No |
| 47 | 2026-03-06 | PERF_REGRESSION | llama.cpp phase-gated XB hurt prefill throughput by about `-28% pp`. | Extra prefill DMA saturated PCIe even though decode gating itself worked. | N6/N6b llama-phase results. | Keep no-XB policy for 1.84x-oversubscribed llama.cpp. | High | No |
| 48 | 2026-03-06 to 2026-03-07 | PERF_REGRESSION | MoE expert bitmap replay added about `+116%` extra DMA and lost throughput (`-4.8% pp`, `-3.1% tg`). | Fault replay moved too much data in an already PCIe-saturated regime. | MoE expert prefetch experiment and counters. | Abandon bitmap replay at 1.84x oversub; only revisit with sparser targets or lower oversub. | Medium | No |
| 49 | 2026-03-07 | DRIVER_BUG | `gpu_block_access` effectively never fired for cycle_moe, so earlier “cycle_moe” results were really `always_max` only. | Hook placement/driver-side semantics were wrong: pinned-state/update ordering prevented the BPF hook from observing the intended event. | ftrace showed `mark_root_chunk_used=21914` but `uvm_bpf_call_gpu_block_access=0`. | Move cycle_moe logic to `gpu_block_activate` and retest all affected results. | Critical | Yes |
| 50 | 2026-03-07 | LOGIC_BUG | `bpftool` 7.7.0 segfaulted on `struct_ops list/unregister` on kernel `6.15.11`. | Tool/kernel combination was unstable for that operation. | Cleanup failures reproduced the segfault; memory recorded a workaround. | Use `bpftool map list | grep struct_ops` and, if needed, `BPF_MAP_DELETE_ELEM` or driver reload. | Medium | No |

## Count by event type

| Event type | Count |
|---|---:|
| `VERIFIER_REJECT` | 2 |
| `BUILD_FAIL` | 1 |
| `XID_FAULT` | 2 |
| `PERF_REGRESSION` | 18 |
| `SYSTEM_HANG` | 2 |
| `LOGIC_BUG` | 24 |
| `DRIVER_BUG` | 1 |
| **Total** | **50** |

## Direct answers

- Kernel panics: **none documented**.
- Data corruption: **none documented**.
- Irrecoverable states requiring hard reboot: **rare, but not zero**. One documented case exists on **2026-03-01**, when the `120B + 120B` UVM configuration OOM-crashed the machine **twice** and required hard reboot.
- `CLAUDE.md` contributed an operational safety rule rather than a historical incident: experiments must run serially because parallel GPU/BPF experiments can corrupt results or crash.

## Typical recovery time by event type

| Event type | Typical recovery time | Notes |
|---|---|---|
| `VERIFIER_REJECT` | Minutes | Edit/reload loop; no runtime damage. |
| `BUILD_FAIL` | Minutes to hours | Usually toolchain or API-path changes only. |
| `XID_FAULT` | 5-30 minutes | Best case is policy reload; worst case needs NVIDIA stack reload. |
| `PERF_REGRESSION` | Minutes to hours | Usually rollback, parameter change, and rerun. |
| `SYSTEM_HANG` | 10-30 minutes, or reboot time | Most stale-`struct_ops` cases were recoverable; the 2026-03-01 OOM case needed hard reboot. |
| `LOGIC_BUG` | Hours to days | Often required tracing, counter instrumentation, or full reruns of invalidated experiments. |
| `DRIVER_BUG` | Hours to days | Highest validation cost because prior results usually had to be reinterpreted or rerun. |
