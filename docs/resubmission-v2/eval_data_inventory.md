# Evaluation Data Inventory for Resubmission V2

Date: 2026-03-28

This documents the exact data sources, plotting scripts, and gaps for each eval update.

## TODO: Figure Updates (deferred)

- [ ] **GNN**: Add capability progression bar chart to paper (new figure, generated at `docs/paper/img/results-raw/pytorch/gnn_capability_progression.pdf`). Data: v1_baseline=70.15s, v1_always_max=26.99s (2.60x), v1_xb_dir=21.32s (3.29x). Script: `docs/paper/img/generate_updated_figures.py`.
- [ ] **FAISS**: Decide whether to update to phase-adaptive D3 data (build -31.8% but search np=1 +5.6%) or keep current `prefetch_adaptive_tree` (build -28.9%, search np=1 -16%). Draft v2 figure at `docs/paper/img/results-raw/faiss/faiss_benchmark_results_v2.pdf`.
- [ ] **GNN node-scaling figure**: Consider running XB direction-aware at 5M/7M/8M/12M/15M nodes to add 6th line to existing figure (requires new experiments).
- [ ] **Multi-tenant kfunc preemption**: Re-run `kfunc_preempt_test.py` with higher contention to get clean data (current data has spike contamination). Then write 4-mode plotting function.
- [ ] **All figures**: Add confidence intervals / error bars from 10-trial data.
- [ ] **eval.tex text updates**: GNN 2.65x→3.29x, FAISS 21-29%→31.8%, vLLM transparency framing, capability progression table.

---

## 1. GNN Cross-Block (3.29x–3.35x)

**Status: Data ready, need new figure**

### Data Files

All in `workloads/pytorch/result/`:

| File | Config | avg_epoch_time_s | Speedup |
|---|---|---|---|
| `v1_baseline_10m.json` | No BPF (A1) | 70.148s | 1.00x |
| `v1_always_max_10m.json` | always_max (A2) | 26.988s | 2.60x |
| `v1_xb_dir_10m.json` | XB direction-aware (A3) | 21.316s | **3.29x** |
| `v1_xb_adjstride_10m.json` | XB adj-stride (A4) | 24.316s | 2.88x |

Second run (`n1_*`):
| `n1_always_max.json` | always_max | 26.368s | 2.66x |
| `n1_xb_direction.json` | XB direction | 20.962s | **3.35x** |

Mar 7 retests:
| `gcn_random_chunked_uvm_20260307_115517.json` | baseline | 70.421s | 1.00x |
| `gcn_random_chunked_uvm_20260307_115722.json` | always_max+cycle_moe | 26.591s | 2.65x |
| `gcn_random_chunked_uvm_20260307_115902.json` | XB+cycle_moe | 20.984s | 3.34x |

Setup: 10M nodes, 10 edges/node, 3 epochs (warmup=1), RTX 5090, UVM 45.1 GB peak (1.34x oversub).

**Note:** MEMORY.md says "3.36x" — actual data ranges from 3.29x to 3.35x depending on run. Use geometric mean of available runs or best consistent number. Recommend citing **3.3x** conservatively.

### Current Paper Figure

- **File:** `docs/paper/img/results-raw/pytorch/uvm_benchmark_comparison.pdf`
- **Script:** `workloads/pytorch/visualize_all.py`
- **Problem:** Uses OLD multi-node-scale data (5M–15M nodes across 5 configs). Does NOT include cross-block (XB) configs. The current figure shows the node-scaling curve, not the A1/A2/A3/A4 bar comparison.

### Action

Option A: Add XB direction-aware as 6th config line in the existing node-scaling figure (requires running XB at multiple node sizes — new experiments).

Option B (recommended): Add a NEW supplementary bar chart showing the capability progression at 10M nodes: baseline → always_max → XB direction → XB adj-stride. Keep existing node-scaling figure for the UVM scaling story. The new bar chart becomes the "capability progression" figure.

---

## 2. llama.cpp UVM+Hints Baseline

**Status: Data exists in figure, value is valid**

### Data (hardcoded in plotting script)

**Script:** `workloads/llama.cpp/uvm/visbasic.py`

| Config | pp512 (tok/s) | tg128 (tok/s) |
|---|---|---|
| ncmoe=64 (framework offload) | 245.63 | 16.34 |
| ncmoe=32 (framework offload) | 260.14 | 18.18 |
| UVM only (no hint, no BPF) | 238.48 | 7.72 |
| UVM user hint (cudaMemAdvise) | 144.00 | 49.31 |
| UVM eBPF | 229.67 | 86.89 |

Key ratios:
- 4.8x = 86.89 / 18.18 (eBPF vs best framework offload, decode)
- **1.76x = 86.89 / 49.31** (eBPF vs UVM+hints, decode)
- UVM+hints pp is low (144) because SetPreferredLocation=CPU forces demand-paging from CPU side

**Paper figure:** `docs/paper/img/results-raw/llama.cpp/llama_uvm_combined_color.pdf`

The UVM+hints bar IS already in the figure (the paper text says "Static hints can affect default LRU algorithms... but still falls behind our policy"). The data point tg=49.31 is valid — it represents the standard cudaMemAdvise hint strategy.

### Action

The figure already contains UVM+hints. The eval text already mentions it. The update needed is in the **intro** — intro_draft.md should cite "1.76x over UVM+hints" as the honest baseline (currently says "1.3x over manually tuned UVM hints" — need to verify which number to use; 1.76x comes from the paper's own data).

Minor: Consider re-running with current driver to confirm the 49.31 number is reproducible. But for paper text, the existing data is usable.

---

## 3. vLLM Transparency Framing

**Status: Data ready, only text change needed**

### Data

**Script:** `workloads/vllm/uvm/first-iter/generate_figures.py`
**Paper figure:** `docs/paper/img/results-raw/vllm/ttft_tpot_combined.pdf`

Figure already shows 4 configs: CPU Offload, UVM Baseline, UVM eBPF, LMCache.

Canonical results in `workloads/vllm/results/exp_vllm_rerun/`:
- Baseline TPOT: 60.9ms, tput: 233.76 tok/s
- always_max+cycle_moe (best BPF): TPOT 55.09ms, tput 256.77 tok/s (+9.8%)
- XB direction: TPOT 56.28ms, tput 255.96 tok/s

### Action

**Text-only change in eval.tex.** Reframe the vLLM paragraph to emphasize:
1. gpu_ext matches LMCache performance **without modifying vLLM code**
2. gpu_ext policies compose with multi-tenant scheduling (LMCache cannot)
3. Transparency: same policy works across vLLM, llama.cpp, any UVM workload

No figure update needed unless we want to add error bars from the 6-config rerun data.

---

## 4. Multi-Tenant Kfunc Preemption

**Status: Data has quality issues, need re-run or careful framing**

### Current Paper Figure

- **File:** `docs/paper/img/results-raw/multi-tenant/scheduler_latency_throughput.pdf`
- **Script:** `docs/eval/multi-tenant-scheduler/plot_figures.py` (function `plot_main_result()`)
- **Shows:** 2 modes only — native vs BPF timeslice policy
- **Result:** P99 1188µs → 53µs = **-95.5%** (this IS from struct_ops timeslice, NOT from kfunc)

### Kfunc Preemption Data

**Directory:** `docs/eval/multi-tenant-scheduler/kfunc_preempt_results/` (5 runs × 4 modes)

| Mode | LC P99 mean (µs) | Issue |
|---|---|---|
| native | 38.2 | Clean |
| timeslice_only | 42.2 | Clean |
| kfunc_only | 2302 | **Spike contamination** — 2/5 runs hit ~11ms, other 3 runs show 28-31µs |
| timeslice_kfunc | 2302 | Same spike issue |

**Benchmark script:** `docs/eval/multi-tenant-scheduler/kfunc_preempt_test.py`

### Test E/F Data (-48%/-58%)

These are from **single-process ioctl preemption demos**, NOT the multi-tenant benchmark:
- Test E: 539ms → 278ms = -48.4%
- Test F: 7.1ms → 3.0ms = -57.6%
- Source: `docs/experiment/plans/gpu_preempt_kfunc.md` §5.2 (text only, no JSON)

### Action

**Option A (safe):** Don't add kfunc preemption to the paper figure yet. Mention it in text as "gpu_ext additionally supports kfunc-based preemption (177µs kfunc vs 354µs ioctl)" with the mechanism description in design, and defer full eval to future work.

**Option B (requires work):** Re-run `kfunc_preempt_test.py` with higher contention (more BE processes, bigger timeslice gap) to get clean data without spikes. Then write a new 4-mode plotting function in `plot_figures.py`.

**Option C (use existing data carefully):** Use the 3 clean runs (filter spikes), report median instead of mean P99. Needs justification in text.

---

## 5. Capability Progression Table (NEW — the "Table 2")

**Status: Need to assemble from multiple sources**

This is NOT an existing figure — it's a NEW table combining results from different experiments:

| Layer | Mechanism | GNN 10M | Source |
|---|---|---|---|
| L0: No BPF | Stock UVM | 70.15s (1.00x) | `v1_baseline_10m.json` |
| L1: Advisory | struct_ops always_max | 26.99s (2.60x) | `v1_always_max_10m.json` |
| L2: Async | + bpf_wq + migrate_range (XB direction) | 21.32s (3.29x) | `v1_xb_dir_10m.json` |
| L3: Proactive | + uprobe → kfunc preemption | **TBD** | Needs data — different workload (multi-tenant) |

**Problem:** L3 uses a different workload (multi-tenant scheduling) than L0-L2 (GNN memory). The capability progression table mixes workloads. This is OK conceptually (each layer adds a capability), but the table can't show a single workload progressing through all 4 layers.

**Recommendation:** Present as two sub-tables or a table with multiple workload columns:

| Layer | GNN 10M | Multi-tenant LC P99 |
|---|---|---|
| L0 | 1.00x | baseline |
| L1 | 2.60x | -95% (timeslice) |
| L2 | 3.29x (+27%) | — |
| L3 | — | TBD (kfunc preempt) |

---

## Figure Generation Summary

| Figure | Can generate now? | Script | Action |
|---|---|---|---|
| GNN capability bar chart | **Yes** | New script from `v1_*.json` | Write new `plot_capability.py` |
| llama.cpp (existing) | Already exists | `uvm/visbasic.py` | No change needed |
| vLLM (existing) | Already exists | `uvm/first-iter/generate_figures.py` | No change needed |
| Multi-tenant kfunc | **No** — data quality issue | `plot_figures.py` (extend) | Re-run or filter |
| Capability progression table | **Yes** (as LaTeX table) | Manual | Assemble from sources above |
