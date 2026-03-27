# Q3 Case Studies: Policy Development Arcs

This document reconstructs three policy-development stories from four evidence streams:

- session transcripts under `~/.claude/projects/-home-yunwei37-workspace-gpu-gpu-ext/`
- git history
- project memory (`MEMORY.md`)
- experiment artifacts under `workloads/*/result*`

Iteration counts below mean policy-level revisions or benchmarked variants, not every shell retry or cleanup command.

## Case A: FAISS phase-adaptive policy

### Short answer

- FAISS-specific policy iterations: **5** (`D`, `D2`, `D3`, `D4`, `D4-fixed`)
- Total measured configurations in the arc including baselines: **8** (`A/B/C/D/D2/D3/D4/D4-fixed`)
- First approach: **direction-consistency phase detection + inherited `cycle_moe` eviction**
- Final approach: **strict `+1` sequential-stride detection, cross-block only in BUILD, `default_lru` eviction, and phase detection before the `va_space` fast-path check**
- Final committed outcome: **add 47.31s vs 69.40s baseline (-31.8%)**, with search `nprobe=4/16` essentially at always-max parity; `nprobe=1` remained slower because phase detection still cost runtime overhead

### Timeline

| Date | Event | Outcome |
|------|-------|---------|
| 2026-02-26 | `docs/cross_block_prefetch_plan.md` defines FAISS as a two-phase workload: BUILD is sequential, SEARCH is random; initial detector uses direction consistency (`>70%` = BUILD, `<30%` = SEARCH) | Good hypothesis, but still too coarse |
| 2026-03-04 | Baselines established from `workloads/faiss/results/exp_xb4/`: A=no BPF, B=always_max, C=`cross_block_v2` direction-aware | `always_max` is already strong; blind/direction-gated XB helps build but hurts search |
| 2026-03-04 | First FAISS-specific run `D`: phase-adaptive v1, direction-consistency detector, inherited `cycle_moe` eviction | BUILD improves sharply (`47.73s` add), but search regresses badly (`np=1 = 8.38s`) |
| 2026-03-05 | Session note around `2026-03-05T01:45:00Z`: agent realizes SEARCH still has forward drift, so direction-consistency is not a clean phase separator | First real aha moment |
| 2026-03-05 | `D2`: detector switched to strict adjacent `+1` VA-block strides in a 32-fault window (`BUILD>=16`, `SEARCH<=8`) | Classification becomes cleaner, but search is still terrible (`np=1 = 9.78s`) |
| 2026-03-05 | Root cause analysis shifts from classifier to eviction policy; `cycle_moe` is isolated as the search killer | Second aha moment |
| 2026-03-05 | `D3`: same v2 detector, but eviction changed to `default_lru` | Converges: `47.31s` add, `12.71s` (`np=4`), `49.51s` (`np=16`) |
| 2026-03-05 | `D4`: kprobe optimization skips expensive `va_space` capture in SEARCH to close the remaining `np=1` gap | Overhead barely improves and a logic bug appears |
| 2026-03-05 | `D4-fixed`: phase detection moved ahead of the `va_space` guard after discovering the optimization could leave the policy stuck in SEARCH forever | Safe final code path; `48.22s` add, `5.54s` (`np=1`) |
| 2026-03-04 commit `a2606bc` | `extension/prefetch_faiss_phase.bpf.c` and results bundle are committed | Story is codified in the final policy and result files |

### Key numbers

| Config | add (s) | np=1 (s) | np=4 (s) | np=16 (s) | Interpretation |
|--------|:-------:|:--------:|:--------:|:---------:|----------------|
| A baseline | 69.40 | 5.19 | 14.34 | 55.96 | No BPF |
| B always_max | 49.49 | 4.38 | 12.62 | 49.45 | Strong baseline to beat |
| C XB dir | 50.28 | 4.45 | 13.47 | 52.47 | Search regression from ungated XB |
| D v1 | 47.73 | 8.38 | 13.83 | 54.19 | BUILD great, SEARCH bad |
| D2 v2 seq-stride + cycle_moe | 48.35 | 9.78 | 14.02 | 50.76 | Better classifier, wrong eviction |
| D3 v2 seq-stride + default_lru | **47.31** | 5.49 | 12.71 | 49.51 | Best overall FAISS-specific config |
| D4 opt | 49.24 | 5.56 | 12.77 | 49.67 | Overhead optimization gives little |
| D4 fixed | 48.22 | 5.54 | 12.71 | 49.53 | Safe final fast path |

### What failed

- The **first phase detector** failed because FAISS search still exhibited enough forward motion to look “build-like” under a direction-consistency heuristic.
- The **second version** fixed classification but still failed because the inherited **`cycle_moe` eviction policy was actively harmful for FAISS search**.
- The **fast-path optimization** failed on its first try because SEARCH skipped `va_space` capture, and the code checked `!va_space` before phase detection. That prevented the state machine from ever leaving SEARCH again.

### Safety events and recovery

- **Stale struct_ops attach state**: the session hit `Failed to attach struct_ops: File exists (-17)`. Recovery was explicit cleanup of old struct_ops before retrying.
- **Logic-safety bug in D4**: the optimization did not crash the GPU, but it created an invalid control-state condition: the policy could get stuck in SEARCH forever. Recovery was to run phase detection using `va_end` before consulting `va_space`.
- No Xid-class GPU fault is documented in this FAISS arc; the safety issues were control-path and loader-state failures, not device-fault events.

### How the agent converged

1. It first anchored FAISS against solid baselines (`A/B/C`) so later “wins” would be meaningful.
2. It accepted that `always_max` was already strong and treated phase-adaptive logic as a narrow attempt to keep BUILD gains without SEARCH regressions.
3. It isolated regressions one layer at a time: first the **phase detector**, then the **eviction policy**, then the **kprobe overhead**.
4. It stopped when `D3` showed the real answer: **phase detection mattered only as a gate for cross-block behavior; it was not a reason to keep FAISS on `cycle_moe`**.

### The aha moment

The first decisive insight came when the agent noticed that **SEARCH was still directionally biased enough to fool a momentum-style detector**. The correct signal was not “mostly forward” but **“strictly adjacent `+1` block strides in a recent window”**. The second insight was that even after fixing that, **the remaining search regression was not classification error at all; it was the wrong eviction policy**.

## Case B: GPU preemption kfunc

### Short answer

- Major mechanism iterations: **6**
- First approach: **stay outside the driver fast path** with sched_ext/xCoord and then userspace/ioctl preempt tooling
- Final approach: **sleepable preempt kfunc plus 3-probe handle capture, with direct `uprobe.s` invocation on `cuLaunchKernel`**
- Main verifier constraints: **`KF_SLEEPABLE` cannot run from non-sleepable struct_ops**, and **pointer registers cannot be shifted/XORed for hashing/arithmetic**
- Performance progression: **ioctl ~354us**, **struct_ops + `bpf_wq` + kfunc ~540us avg / 177us low-lat band**, **sleepable `uprobe.s` + kfunc ~312us avg**, but **end-to-end P99 could still lose badly when the workload was not contended enough**

### Timeline

| Date | Event | Outcome |
|------|-------|---------|
| 2026-02-27 to 2026-02-28 | Early xCoord work in `sched_gpu_aware.bpf.c` shows CPU boosting can help CPU-bound serving, but not GPU/PCIe-bound workloads | Motivates going closer to the GPU control path |
| 2026-03-03 | Session `b1e7bc20-...` starts from the question “can we add a kfunc to preempt kernel rather than workqueue?” | Explicit pivot from scheduler-only coordination to direct preemption |
| 2026-03-03 | Existing `gpu_preempt_ctrl` path is audited and effectively ruled out: it depends on tracepoints that do not exist in the driver | First approach discarded |
| 2026-03-03 | Same-process ioctl preempt path is validated first, proving the RM/GSP preempt chain works | Establishes a working but userspace-bound baseline |
| 2026-03-03 | Handle-introspection attempt with `bpftrace` fails: `Cannot resolve unknown type "struct nv_gpu_task_init_ctx"` | Tooling path abandoned |
| 2026-03-03 | Agent switches to a zero-kernel-modification **3-probe** capture scheme over `nvidia_unlocked_ioctl`, `nv_gpu_sched_task_init`, and ioctl return | Handle capture problem solved without BTF type support |
| 2026-03-03 to 2026-03-04 | Verifier constraints become clear: preempt must be sleepable; pointer arithmetic on pointer regs is rejected | Forces `bpf_wq` trampoline and pointer-to-scalar workaround |
| 2026-03-04 commit `4b49e0a` | End-to-end test and benchmark support for the kfunc path are added | `struct_ops -> bpf_wq -> kfunc` works |
| 2026-03-04 | Cross-process preempt through `bpf_nv_gpu_preempt_tsg(hClient, hTsg)` succeeds; warm average about `540us`, low-latency band `177us` | Mechanism proven |
| 2026-03-04 commit `f21467a` | Sleepable uprobe support is added so `uprobe.s` can call the preempt kfunc directly | Removes the workqueue hop |
| 2026-03-04 | `uprobe.s + kfunc` benchmarks at `312us` average | Main latency improvement lands |
| 2026-03-19 | Round-3 multi-tenant evaluation with `lc_comm` filtering shows native `38us` P99, timeslice `42us`, kfunc `2302us` | Final convergence: mechanism is valid, but deployment must be workload-aware |

### Key numbers

| Stage | Number | Meaning |
|------|:------:|---------|
| Same-process ioctl hot path | ~316-356us | First proof that RM preempt works |
| ioctl reference in v2 plan | 354us | Comparison point for kfunc path |
| `struct_ops + bpf_wq + kfunc` | 540us avg | Working cross-process kernel-triggered path |
| Low-latency band inside kfunc path | 177us | Best observed part of the warm distribution |
| `uprobe.s + kfunc` | 312us avg | Best direct mechanism |
| Multi-tenant Round 3 native | 38us P99 | Baseline was already very low |
| Multi-tenant Round 3 timeslice | 42us P99 | Essentially baseline-like |
| Multi-tenant Round 3 kfunc | 2302us P99 | Harmful under weak contention / spike-sensitive setup |
| Same-process synthetic tests E/F | -48.4%, -57.6% | Preempt can help when contention is real enough |

### Verifier constraints and the pointer-arithmetic issue

- **Sleepable-call constraint**: `bpf_nv_gpu_preempt_tsg` is a `KF_SLEEPABLE` kfunc. It cannot be called from ordinary non-sleepable struct_ops hooks. The first workaround was to trigger it through **`bpf_wq`**, which runs in a sleepable callback context.
- **Pointer-arithmetic constraint**: project memory records the exact limitation: **the BPF verifier prohibits pointer arithmetic on pointer registers; you cannot shift or XOR them directly**.
- The issue surfaced while trying to derive stable IDs / hashes from kernel pointers in BPF-side bookkeeping.
- The recovery pattern was: **first copy the pointer value into a scalar with `bpf_probe_read_kernel(&scalar, sizeof(scalar), &ptr)`, then perform shift/XOR/hash arithmetic on that scalar**.

### What failed

- **`gpu_preempt_ctrl`** failed as a practical path because the driver tracepoints it depended on did not exist.
- **`bpftrace`-based structure decoding** failed because module type information was not resolvable for `struct nv_gpu_task_init_ctx`.
- **The first kernel-context design** could not just “call the kfunc from struct_ops” because the kfunc was sleepable.
- **Per-launch multi-target preempt without filtering** was harmful: BE launches could trigger their own preemption path, and one evaluation round drove BE throughput down by roughly `73%`.

### Safety events and recovery

- **Privilege boundary risk**: the project explicitly rejected a broader `escape.c` / ioctl-bypass style interface. Cross-process preempt was encapsulated inside `nv_gpu_sched_do_preempt(..., RS_PRIV_LEVEL_KERNEL)` so the privilege elevation stayed internal to the helper, not exposed as a generic userspace surface.
- **Type/tooling failure**: when `bpftrace` could not resolve `struct nv_gpu_task_init_ctx`, the agent switched to raw offset reads and the 3-probe path rather than forcing more fragile tool-dependent instrumentation.
- **Policy-safety filter**: after seeing that unfiltered launch-triggered preempt could make BE throughput collapse, the implementation added **`lc_comm` filtering** and cooldown logic so only LC launches could trigger preempt.
- No GPU Xid event is the central story here. The main safety issues were verifier constraints, privilege containment, and avoiding self-triggered pathological preempt storms.

### How the agent converged

1. It started with the xCoord/sched_ext line and learned where CPU-side coordination stopped helping.
2. It validated the RM preempt path with userspace ioctl before moving anything into BPF.
3. It solved handle capture independently of the final preempt mechanism, which avoided conflating “can we identify the TSG?” with “can BPF preempt it?”
4. It accepted the verifier’s sleepability boundary and used `bpf_wq` as the first legal bridge.
5. It then found a better legal bridge: **sleepable uprobes**, which removed the workqueue hop entirely.
6. It ended with a more nuanced conclusion than “kfunc is faster”: **the kfunc is valuable because it enables in-kernel, cross-process preempt with no userspace round-trip, but it should only be deployed where the workload actually needs that capability**.

### The aha moment

The decisive insight was that the best design was **not** “struct_ops directly calls preempt,” and not even “workqueue all the way down.” It was realizing that **`SEC("uprobe.s/...")` already runs in a sleepable process context**, so the preempt kfunc could be called directly at `cuLaunchKernel` time. That changed the mechanism from a proof-of-concept trampoline into the final low-latency design.

## Case C: Cross-block multi-stride prefetch

### Short answer

- Major cross-block iterations in the story: **6**
- Explicit multi-stride configurations benchmarked: **3** (`adaptive K=1..6`, capped `K=2`, capped `K=3`)
- Effective lookahead depths covered by the search: **`K=1` through `K=6`**
- First approach: **keep increasing cross-block aggressiveness with stride history and confidence**
- Final answer: **one-block direction-aware XB (`K=1`) is the sweet spot; `K>1` overloads PCIe and causes harmful displacement**
- Final GNN conclusion: **1-block XB is optimal** (`20.96s` to `21.32s` range), while multi-stride was abandoned

### Timeline

| Date | Event | Outcome |
|------|-------|---------|
| 2026-02-26 | `docs/cross_block_prefetch_mechanism.md` proposes safe cross-block prefetch through `bpf_wq + bpf_gpu_migrate_range()` | Mechanism foundation established |
| 2026-02-27 | `prefetch_cross_block_v2` is implemented and iterated in session `6b21980a-...` | Cross-block policy experimentation begins |
| 2026-02-27 to 2026-02-28 | Llama experiments progress from blind adjacent to 2-step direction-aware, 3-step direction-aware, then adjacent-stride | Regressions shrink from about `-30%` to statistical parity, but XB is still not useful at 1.84x oversub |
| 2026-02-28 | Session corrects an early wrong theory: the problem is not `va_space` lock contention but **PCIe competition + VRAM displacement** | Important root-cause correction |
| 2026-02-28 | Operational failure: stale struct_ops state gets stuck; cleanup/rmmod attempts show maps and progs still pinned; one confirmation run is interrupted with `Exit code 137` | Safety/cleanup friction becomes part of the story |
| 2026-03-04 | After fixing the GNN allocator bug, GNN is re-evaluated correctly: baseline `70.15s`, always_max `26.99s`, one-block direction-aware XB `21.32s`, adjacent-stride `24.32s` | Confirms XB is genuinely strong on GNN |
| 2026-03-05 | `prefetch_stride_multiblock.bpf.c` is tried for N1: confidence-based multi-stride lookahead with `K = 1 + confidence/2`, up to `K=6` | First explicit multi-stride attempt |
| 2026-03-05 | `n1_stride_multiblock.json` records `38.47s` average epoch time | Catastrophic regression; PCIe overload discovered |
| 2026-03-06 | Follow-up capped retests `K=2` (`30.81s`) and `K=3` (`30.73s`) are run | Even conservative `K>1` still loses |
| 2026-03-07 | Follow-up retest after later eviction-hook cleanup reports GNN best result around `20.98s` for XB+cycle_moe | `K=1` remains the settled optimum |

### Key numbers

| Config | Avg epoch time (s) | Relative to no BPF | Relative to always_max | Meaning |
|--------|:------------------:|--------------------|------------------------|---------|
| No BPF baseline | 70.15 | 1.00x | 2.60x slower | Starting point |
| always_max | 26.37 to 26.99 | 2.60x to 2.66x faster | — | Strong intra-block baseline |
| 1-block XB direction-aware | 20.96 to 21.32 | **3.29x to 3.36x faster** | **~21% better** | Best result |
| Adjacent-stride 1-block | 24.32 | 2.89x faster | Worse than direction-aware | Safer but less aggressive |
| N1 adaptive `K=1..6` | 38.47 | 1.82x faster | **46% slower** | PCIe overload case |
| Fixed `K=2` | 30.81 | 2.28x faster | Still much worse | Recovery attempt failed |
| Fixed `K=3` | 30.73 | 2.28x faster | Still much worse | Recovery attempt failed |

### How many stride configurations were tested?

- **Three explicit multi-stride experiments** were benchmarked:
  - adaptive **`K=1..6`**
  - fixed **`K=2`**
  - fixed **`K=3`**
- The comparison set also included the **one-block `K=1` direction-aware XB** reference, which is what the story ultimately converged to.
- So the search space effectively covered **lookahead depths from `K=1` through `K=6`**, but every tested `K>1` variant lost.

### What failed

- The initial multi-stride idea assumed that GNN’s highly sequential epoch scan would benefit from **deeper and deeper lookahead**.
- In practice, the adaptive policy’s confidence mechanism drove prefetch depth toward **`K=6`**, which means up to **12MB of extra migration per triggering fault**.
- That did not create more useful overlap. It simply **consumed PCIe headroom and displaced pages that the workload still needed soon**, causing second-order faults.
- The follow-up capped versions (`K=2`, `K=3`) proved the issue was not “K=6 is too high but the idea is right.” Even smaller `K>1` was still wrong.

### PCIe overload discovery

The PCIe overload discovery is the core of this case:

- The first N1 run (`K=1..6`) dropped GNN from **`20.96s` / `21.32s`** for one-block XB to **`38.47s`**.
- That was too large to explain by classifier noise or benchmark variance.
- The project memory and `cross_block_prefetch_plan.md` both record the conclusion: **multi-block lookahead was spending more PCIe bandwidth on speculative migration than the system had spare**, and on 10M GNN it also **drove unnecessary displacement**, so the policy paid both the prefetch cost and the refault cost.

### How the agent determined `K=1` was optimal

The convergence logic was empirical and clean:

1. `always_max` established the intra-block floor at about **`26.4s` to `27.0s`**.
2. One-block direction-aware XB improved that to about **`21.0s` to `21.3s`**.
3. The first multi-stride expansion (`K=1..6`) collapsed to **`38.47s`**.
4. The recovery attempt then deliberately tested **smaller but still multi-block** depths:
   - `K=2` -> **`30.81s`**
   - `K=3` -> **`30.73s`**
5. Since every tested `K>1` setting lost while `K=1` remained best, the agent concluded that **the useful signal was “is the next block worth fetching?” not “how far ahead can we speculate?”**

### Safety events and recovery

- **Stale struct_ops state** during the 2026-02-28 session left old struct_ops maps and programs pinned. The module could not be cleanly removed, one attach path remained “in use,” and the agent eventually stopped chasing one extra passive-MRU confirmation run.
- **Interrupted benchmark / wasted-GPU prevention**: one adjacent-stride confirmation run ended with `Exit code 137`, and the session explicitly chose to stop rather than keep burning GPU time once the pattern was already clear.
- No GPU Xid is the headline here. The safety issues were operational: stuck BPF state, failed detach/reload attempts, and careful stopping once additional runs no longer improved the decision quality.

### How the agent converged

1. It first proved that cross-block could matter at all by getting one-block XB to work.
2. It corrected a wrong root-cause theory early: **not lock contention, but bandwidth competition and displacement**.
3. It then tried to exploit GNN’s sequential structure more aggressively with stride history and adaptive depth.
4. When the first multi-stride attempt failed, it did not immediately abandon the whole line; it ran **smaller fixed caps** (`K=2`, `K=3`) to test whether the issue was just “too much lookahead.”
5. Those retests failed too, so the conclusion became robust: **the right policy is one-block XB, not deeper stride speculation**.

### The aha moment

The aha moment was not “sequential workloads like more lookahead.” It was the opposite: **for this workload and this interconnect budget, one block ahead is the useful prediction boundary; deeper lookahead only converts spatial predictability into PCIe overload**.

## Cross-case synthesis

Across all three stories, the common convergence pattern was:

- start from a broad intuition about access pattern or control-path latency
- build a narrow measurable baseline
- let the first failure identify the wrong abstraction
- isolate one cause at a time
- stop when the remaining gap is either structural or not worth more complexity

The specific lessons were different:

- **Case A** converged by learning that **phase detection only matters if the signal cleanly separates phases and does not drag in the wrong eviction policy**.
- **Case B** converged by learning that **the real value of a preempt kfunc is cross-process in-kernel triggering, not just raw microseconds saved**.
- **Case C** converged by learning that **predictability does not justify arbitrary aggressiveness; at some point, better prediction just means more wasted DMA**.
