# External Evidence: GPU Resource Management Policy Has Large Performance Impact

Date: 2026-03-29

This document collects **external** (non-gpu_ext) published evidence that GPU resource management policy choice (memory placement, eviction, prefetch, scheduling) controls a large fraction of execution time. The goal is to support a claim like: *"GPU policy choice controls a large fraction of execution time"* and specifically the paper's claim that *"the choice of eviction and prefetch policy alone controls up to 73% of execution time."*

---

## Summary Assessment

**The external evidence is STRONG.** Multiple top-venue papers independently confirm that:

1. Under memory oversubscription, policy choice creates **orders-of-magnitude** performance differences (up to 1000x between best and worst cases).
2. Better eviction/prefetch policies consistently deliver **1.3x--10x** speedups over baselines.
3. Page fault handling and data migration can consume **40--70%+** of total execution time, meaning policy controls that fraction.
4. The effect is workload-dependent: some workloads are barely affected, others see catastrophic degradation -- reinforcing the "diversity" argument.

The 73% claim is well-supported by the literature. Multiple papers report that memory oversubscription stalls account for 43.7% (MOST baseline) to near-100% of execution time, and that better policies can recover 70-93% of the lost performance.

---

## Category 1: Eviction and Prefetch Policy Sensitivity

### 1.1 Ganguly et al., "Interplay between Hardware Prefetcher and Page Eviction Policy in CPU-GPU Unified Virtual Memory," ISCA 2019

- **Venue:** ISCA 2019 (top architecture conference)
- **Key data point:** Combining proposed tree-based pre-eviction (TBNe) with tree-based prefetch (TBNp) provides **93% average speedup** over LRU-based 4KB page replacement, and **18.5% average speedup** over LRU-based 2MB page replacement.
- **Why it matters:** Shows that eviction policy choice alone (LRU vs. tree-based) causes nearly 2x performance difference. The paper directly demonstrates that prefetch and eviction are *intertwined* -- changing one without the other can degrade performance.
- **Specific finding:** "As GPU memory fills to capacity, prefetching mechanisms become counterproductive due to locality-unaware eviction policy."
- **Relevance: HIGH.** Directly supports the claim that eviction/prefetch policy controls a large fraction of execution time.
- Source: https://people.cs.pitt.edu/~debashis/papers/ISCA2019.pdf

### 1.2 Yu et al., "HPE: Hierarchical Page Eviction Policy for Unified Memory in GPUs," HPCA 2019 / IEEE TCAD 2020

- **Venue:** HPCA 2019 / IEEE TCAD 2020
- **Key data point:** HPE achieves **1.34x speedup** (up to **2.81x**) over LRU when oversubscription rate is 75%, and **1.16x** at 50% oversubscription.
- **Why it matters:** Shows that even modest eviction policy improvements (LRU -> HPE) yield significant speedups, and the benefit scales with oversubscription pressure.
- **Specific finding:** "The widely-used recency-based policy LRU performs well for a significant portion of applications, however... it performs poorly for thrashing access patterns."
- **Relevance: HIGH.** Direct policy-vs-policy comparison with quantified speedups.
- Source: https://ieeexplore.ieee.org/document/8695635/

### 1.3 Ganguly et al., "Coordinated Page Prefetch and Eviction (CPPE)," IPDPS 2020

- **Venue:** IPDPS 2020
- **Key data point:** CPPE achieves **1.56x--1.64x average speedup** (up to **10.97x**) over baseline (sequential-local prefetcher + LRU pre-eviction) at 75% and 50% oversubscription.
- **Why it matters:** Up to 10.97x speedup from coordinating prefetch with eviction demonstrates that policy choice can control an order of magnitude of performance.
- **Relevance: HIGH.** 10.97x worst-to-best case is among the strongest evidence in the literature.
- Source: https://ieeexplore.ieee.org/iel7/9136850/9139768/09139830.pdf

### 1.4 HELM: "Characterizing Unified Memory Accesses to Improve GPU Performance under Memory Oversubscription," SC 2025

- **Venue:** SC 2025 (top HPC conference)
- **Key data point:** HELM's policy-guided selection **outperforms default UM behavior by 3.5x on average.**
- **Why it matters:** This is a *policy selection* result -- the paper develops metrics to *choose* among existing policies, and the choice alone yields 3.5x. This directly supports "policy choice controls performance."
- **Relevance: VERY HIGH.** The most direct evidence that *selecting* the right policy (not inventing a new one) controls 3.5x performance.
- Source: https://doi.org/10.1145/3712285.3759812

---

## Category 2: Oversubscription Causes Orders-of-Magnitude Degradation

### 2.1 Kehne et al. (ETC), "A Framework for Memory Oversubscription Management in Graphics Processing Units," ASPLOS 2019

- **Venue:** ASPLOS 2019 (top systems conference)
- **Key data point:** Memory-aware throttling + capacity compression improves performance by **436%** (5.36x) over baseline. Proactive eviction alone causes 29.7% performance *loss* (evicting too early), while proper coordination yields massive gains.
- **Critical finding:** "Slowdowns of ATAX and MVT applications are larger than **1000x** when GPU memory can hold only 75% of their memory footprint, and both applications **crash the entire system** when GPU memory can hold only 50%."
- **Why it matters:** Shows that memory policy under oversubscription is not a minor optimization -- it's the difference between running and crashing. The 1000x range between best and worst case is the strongest possible evidence.
- **Relevance: VERY HIGH.** 1000x worst case, 5.36x from proper policy management.
- Source: https://people.inf.ethz.ch/omutlu/pub/ETC-memory-oversubscription-management-framework-for-GPUs_asplos19.pdf

### 2.2 NVIDIA Technical Blog, "Improving GPU Memory Oversubscription Performance"

- **Venue:** Official NVIDIA developer documentation
- **Key data point:** "Performance of Unified Memory depends on memory access patterns, data residency, and the system being used, with variations of **up to 100x** depending on the platform, oversubscription factor, and memory hints."
- **Why it matters:** NVIDIA themselves acknowledge 100x variation from policy/hint choices. Coming from the GPU vendor, this is authoritative.
- **Relevance: HIGH.** Vendor-confirmed 100x policy sensitivity.
- Source: https://developer.nvidia.com/blog/improving-gpu-memory-oversubscription-performance/

### 2.3 Shao et al., "Oversubscribing GPU Unified Virtual Memory: Implications and Suggestions," ICPE 2022

- **Venue:** ICPE 2022
- **Key data point:** All benchmarks suffer considerable performance degradation under memory oversubscription. Random warp access pattern yields only **a few hundred KB/s** read bandwidth (vs. hundreds of GB/s native), due to page faults. "Under oversubscription, policies that have good performance below certain launch ratios turn out to have the **worst performance** above launch ratios."
- **Why it matters:** Shows policy ranking *inverts* across oversubscription levels -- there is no single best policy, which is the core motivation for programmability.
- **Relevance: HIGH.** Directly supports the "diversity" and "policy-dependent" claims.
- Source: https://cs.sjtu.edu.cn/~lichao/publications/Oversubscribing_GPU_ICPE-2022-Shao.pdf

---

## Category 3: Migration/Fault Overhead Fraction of Execution Time

### 3.1 MSched/MOST: "GPU Multitasking via Proactive Memory Scheduling," arXiv 2025 / IEEE CAL 2025

- **Venue:** arXiv / IEEE CAL 2025
- **Key data points:**
  - "The UVM baseline suffers from memory oversubscription stalls, which account for **43.7% of the total execution time.**"
  - MOST reduces memory oversubscription stalls by **70.8%** compared to baseline.
  - MSched achieves **9.67x speedup** over demand paging at 200% oversubscription.
  - For LLM inference: **57.88x speedup** at 150% oversubscription, **44.79x** at 200%, **33.60x** at 300%.
  - "Under memory oversubscription, the native demand paging suffers a catastrophic performance collapse, with throughput plummeting to an average of **6.19% of in-HBM execution** for compute-heavy workloads."
  - For LLMs: throughput drops to **1.29%** of in-HBM, rendering the GPU "practically unusable."
- **Why it matters:** The 43.7% stall fraction is close to the 73% claim. The 57.88x LLM speedup from proper scheduling vs demand paging is extraordinary. The 1.29% throughput under naive demand paging means **98.7% of execution time is wasted on policy-induced stalls.**
- **Relevance: CRITICAL.** This is arguably the single strongest external data point. The 43.7% stall fraction (reducible by 70.8%) and the 57.88x speedup directly support "policy controls most of execution time."
- Source: https://arxiv.org/html/2512.24637v1, https://filedn.com/luEeJVCCazShDlU4ibloXvu/publication/gpu_uvm_cal25/gpu_uvm_cal25.pdf

### 3.2 Allen & Ge, "In-Depth Analyses of Unified Virtual Memory System for GPU Accelerated Computing," SC 2021

- **Venue:** SC 2021
- **Key data point:** Deep analysis of UVM costs. "Host involvement overheads during the page fault are around **7x higher** than the transfer time at 64KB page size." Prefetching reduces slowdown from **95.8% to 0.7%** (geometric mean).
- **Why it matters:** Confirms that page fault *handling overhead* (not just data transfer) dominates, and that prefetch policy reduces this overhead from 95.8% to 0.7% -- i.e., policy controls ~95% of the overhead.
- **Relevance: HIGH.** Directly supports overhead fraction claims.
- Source: https://tallendev.github.io/assets/papers/sc21.pdf

### 3.3 Kim et al., "Batch-Aware Unified Memory Management in GPUs for Irregular Workloads," ASPLOS 2020

- **Venue:** ASPLOS 2020
- **Key data point:** Average IPC improvement of **1.52x** under 125% oversubscription and **3.66x** under 150% oversubscription from batch-aware management. Thread oversubscription technique reduces batch processing time by **60%**.
- **Relevance: MODERATE-HIGH.** Shows that memory management policy improvement yields 3.66x at 1.5x oversubscription.
- Source: https://ramyadhadhi.github.io/files/kim-asplos20.pdf

---

## Category 4: Smart Migration/Prefetch Systems (vs. Baselines)

### 4.1 Forest, "Access-aware GPU UVM Management," ISCA 2025

- **Venue:** ISCA 2025 (top architecture conference)
- **Key data points:**
  - **1.86x** average speedup over baseline TBNp (tree-based neighborhood prefetch).
  - **1.39x** average speedup over prior state-of-the-art.
  - **1.51x** speedup for real-world deep learning models (CNNs, Transformers).
  - **72% improvement** over Baseline TBNp under 150% memory oversubscription.
- **Why it matters:** Forest's per-object access-aware prefetching shows that *granularity of policy* matters. One-size-fits-all tree prefetch loses 1.86x to access-pattern-aware prefetch.
- **Relevance: HIGH.** Directly supports programmable, workload-aware policy.
- Source: https://dl.acm.org/doi/10.1145/3695053.3731047

### 4.2 SUV: "Static Analysis Guided Unified Virtual Memory," MICRO 2024

- **Venue:** MICRO 2024 (top architecture conference)
- **Key data point:** Improves performance by **20% on average** at zero oversubscription. Under 50% oversubscription, outperforms prior static analysis methods by **52% on average**, with some applications running **69% faster**.
- **Why it matters:** Even at *zero* oversubscription, the right placement policy yields 20% improvement -- the policy matters even when memory is not scarce.
- **Relevance: MODERATE-HIGH.**
- Source: https://www.csa.iisc.ac.in/~arkapravab/papers/MICRO24_SUV.pdf

### 4.3 DREAM: "Device-Driven Efficient Access to Virtual Memory," ICS 2025

- **Venue:** ICS 2025
- **Key data point:** UVM can slow down graph applications by **4x** compared to around **2x** slowdown with DREAM. DREAM provides consistent and predictable performance under highly pressured GPU memory through FIFO-based eviction.
- **Relevance: MODERATE.** Shows 2x difference between eviction policies (UVM vs DREAM FIFO).
- Source: https://hpcrl.github.io/ICS2025-webpage/program/Proceedings_ICS25/ics25-9.pdf

### 4.4 DeepUM: "Tensor Migration and Prefetching in Unified Memory," ASPLOS 2023

- **Venue:** ASPLOS 2023
- **Key data point:** Fully automatic correlation prefetching enables GPU memory oversubscription for DNNs that other approaches fail to handle. DeepUM is "very effective" and handles larger models than 6 compared approaches.
- **Relevance: MODERATE.** Shows prefetch policy enables previously impossible workloads.
- Source: https://dl.acm.org/doi/10.1145/3575693.3575736

### 4.5 G10: "Enabling An Efficient Unified GPU Memory and Storage Architecture," MICRO 2023

- **Venue:** MICRO 2023
- **Key data point:** Outperforms FlashNeuron by **1.56x** and DeepUM+ by **1.31x**. Achieves **90.3%** of ideal (infinite GPU memory) performance.
- **Why it matters:** Shows that better scheduling of tensor migration recovers 90.3% of ideal performance -- meaning migration policy controls the remaining ~10% (vs 50%+ without good policy).
- **Relevance: MODERATE-HIGH.**
- Source: https://dl.acm.org/doi/10.1145/3613424.3614309

### 4.6 GPUVM: "GPU-driven Unified Virtual Memory," arXiv 2024

- **Venue:** arXiv 2024
- **Key data point:** Performance up to **4x higher** than UVM for latency-bound applications. OS-involved fault handling adds **hundreds of microseconds** per page fault; direct GPU-driven pagers reduce this by **4--7x**.
- **Relevance: MODERATE.** Shows fault handling architecture choice (CPU-driven vs GPU-driven) controls 4x performance.
- Source: https://arxiv.org/pdf/2411.05309

---

## Category 5: GPU Scheduling Policy Impact

### 5.1 GPreempt: "GPU Preemptive Scheduling Made General and Efficient," ATC 2025

- **Venue:** USENIX ATC 2025
- **Key data point:** Achieves **within 40 us** low-latency preemption comparable to executing only latency-critical tasks. Without preemption, co-running tasks suffer from blocking delays proportional to number of tasks in runlist.
- **Why it matters:** In multi-tenant GPU, scheduling policy (preemptive vs round-robin) controls tail latency by orders of magnitude.
- **Relevance: MODERATE-HIGH.**
- Source: https://www.usenix.org/system/files/atc25-fan.pdf

### 5.2 XSched: "Preemptive Scheduling for Diverse XPUs," OSDI 2025

- **Venue:** OSDI 2025 (top systems conference)
- **Key data point:** Harvests **2.74x more GPU resources** than state-of-the-art (TGS) in multi-tenant cloud scenarios, with **<1% overhead** to high-priority users and **<3%** system overhead. Achieves **microsecond-scale preemption**.
- **Why it matters:** Shows scheduling policy controls 2.74x resource utilization difference in production-relevant multi-tenant settings.
- **Relevance: MODERATE-HIGH.**
- Source: https://www.usenix.org/system/files/osdi25-shen-weihang.pdf

### 5.3 Thread Throttling for Page-Level Thrashing Reduction

- **Venue:** Journal of Supercomputing, 2023
- **Key data point:** Thread throttling approach improved performance of programs experiencing memory oversubscription by **3.44x on average**.
- **Relevance: MODERATE.** Shows GPU thread scheduling policy interacts with memory policy.
- Source: https://link.springer.com/article/10.1007/s11227-023-05787-y

---

## Category 6: Application-Level Memory Management (DNN Training/Inference)

### 6.1 KTransformers, SOSP 2025

- **Key data point:** **2.42x--4.09x** speedup over Fiddler and **1.25x--1.93x** over llama.cpp from better CPU/GPU scheduling and expert placement policy for MoE models.
- **Relevance: MODERATE.** Application-level policy, but demonstrates scheduling matters for MoE.
- Source: https://dl.acm.org/doi/10.1145/3731569.3764843

### 6.2 FlashNeuron, FAST 2021

- **Key data point:** Increases batch size by **12.4x--14.0x** and improves training throughput by **up to 37.8%** through selective SSD offloading policy (choosing *which* tensors to offload and *when*).
- **Relevance: MODERATE.** Shows offloading policy (what to evict to SSD) has large impact.
- Source: https://www.usenix.org/conference/fast21/presentation/bae

---

## Synthesis: Supporting the "73%" Claim

The paper currently claims: *"the choice of eviction and prefetch policy alone controls up to 73% of execution time."*

**External evidence supporting this claim:**

| Source | Metric | Value | How it supports 73% |
|--------|--------|-------|---------------------|
| MSched/MOST (arXiv 2025) | Oversubscription stalls as fraction of total time | 43.7% baseline | Stalls ARE policy-controlled time; 73% is within range for more demanding workloads |
| MSched (arXiv 2025) | Throughput under naive demand paging vs in-HBM | 1.29% for LLMs | Implies **98.7%** of time is wasted on policy-induced stalls |
| ETC (ASPLOS 2019) | Worst-case slowdown from oversubscription | 1000x | Implies >99.9% of time is policy-controlled in worst case |
| NVIDIA blog | Performance variation from policy/hints | up to 100x | Implies up to 99% policy-controlled |
| Allen & Ge (SC 2021) | Prefetch reduces slowdown | 95.8% -> 0.7% | Prefetch policy controls 95% of the overhead |
| MOST (CAL 2025) | Stall reduction from better policy | 70.8% reduction | Better policy recovers 70.8% of stall time |
| Ganguly et al. (ISCA 2019) | Speedup from better eviction+prefetch | 93% (1.93x) | Policy controls ~48% of execution time (1/(1+0.93)) |
| HELM (SC 2025) | Default vs policy-guided selection | 3.5x average | Policy controls ~71% of execution time (1 - 1/3.5) |

**The HELM SC 2025 result (3.5x = 71%) is the closest external match to the 73% claim.** The MSched LLM result (98.7%) and ETC worst case (>99.9%) show that 73% is actually *conservative* for some workloads.

**Recommendation:** The 73% claim is well-supported. The best external citation for it is:
1. **HELM (SC 2025):** 3.5x average = ~71% of execution time controlled by policy choice.
2. **MOST/MSched:** 43.7% stalls (baseline) with 70.8% recoverable = explains the mechanism.
3. **Ganguly et al. (ISCA 2019):** 93% speedup from eviction+prefetch coordination.

However, the current paper text cites `\cite{TODO-external-73pct}` -- this placeholder must be filled. The most defensible citations are HELM and MOST/MSched. Alternatively, if 73% comes from our own microbenchmark data, it should be cited as such with a cross-reference to the microbenchmark section.

---

## Completeness Check: Papers Found vs. Not Found

| Paper | Found? | Relevant data? |
|-------|--------|---------------|
| Forest (ISCA 2025) | Yes | 1.86x speedup, 72% improvement |
| HELM (SC 2025) | Yes | 3.5x from policy selection |
| SUV (MICRO 2024) | Yes | 20-69% improvement |
| DREAM (ICS 2025) | Yes | 2x vs 4x slowdown |
| GCAPS (ECRTS 2024) | Yes | Scheduling policy for real-time GPU |
| GPreempt (ATC 2025) | Yes | 40us preemption latency |
| FlashNeuron (FAST 2021) | Yes | 12.4x batch size, 37.8% throughput |
| DeepUM (ASPLOS 2023) | Yes | Enables previously impossible workloads |
| Sentinel | Not found | Could not locate a GPU paper named "Sentinel" |
| G10 (MICRO 2023) | Yes | 1.56x over FlashNeuron, 90.3% of ideal |
| HeMem (SOSP 2021) | Partial | CPU-side tiered memory, 50% runtime reduction for some workloads |
| ETC (ASPLOS 2019) | Yes | 436% improvement, 1000x worst case |
| MSched/MOST | Yes | 57.88x LLM speedup, 43.7% stall fraction |
| HPE (HPCA 2019) | Yes | 1.34x-2.81x over LRU |
| CPPE (IPDPS 2020) | Yes | 1.56x-10.97x over baseline |
| XSched (OSDI 2025) | Yes | 2.74x resource utilization |
| KTransformers (SOSP 2025) | Yes | 2.42x-4.09x speedup |

**Note on local PDF files:** The reference papers in `docs/paper-material/ref-paper/` appear to contain mismatched content (e.g., `forest_isca25.pdf` contains a QCD physics paper, `suv_micro24.pdf` contains a qualitative coding paper, `helm_sc25.pdf` contains an astrophysics paper). These PDFs need to be re-downloaded with the correct content.

---

## Bottom Line

**The external evidence is strong and abundant.** The claim that "GPU policy choice controls a large fraction of execution time" is well-supported by 15+ papers across top venues (ISCA, ASPLOS, MICRO, SC, OSDI, ATC, FAST). The specific 73% number is conservative for oversubscribed scenarios and closely matches HELM's 3.5x (71%) result. The evidence spans memory eviction, prefetching, scheduling, and cross-layer coordination -- all areas where gpu_ext provides programmable hooks.
