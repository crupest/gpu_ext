# OSDI Review: gpu_ext: Extensible OS Policies for GPUs via eBPF

## Summary

gpu_ext proposes treating the GPU driver and device as a programmable OS subsystem via eBPF. The system exposes host-side `struct_ops` hooks in NVIDIA's UVM driver for memory placement/eviction/prefetching and TSG scheduling, and introduces a device-side eBPF runtime with SIMT-aware verification that injects policy logic into GPU kernels via binary trampolines. Cross-layer eBPF maps bridge host-device state. Evaluation on llama.cpp, vLLM, GNN, and FAISS shows up to 4.8x decode throughput and 2x tail latency reduction.

## Strengths

**S1: Compelling and timely problem formulation.** The paper correctly identifies a real gap: GPU resource management is trapped between rigid kernel drivers and application-bound user-space runtimes. Extending the successful eBPF extensibility model (sched_ext, cache_ext) to GPUs is a natural and well-motivated research direction. The three-layer architecture (host hooks + device runtime + cross-layer maps) is clean.

**S2: Breadth of scope and real-world workloads.** The system addresses memory, scheduling, and observability across both host and device. The evaluation covers four realistic workloads (LLM inference, serving, GNN training, vector search) in both single-tenant and multi-tenant settings, which is much more convincing than synthetic benchmarks alone.

**S3: Strong headline numbers for host-side memory policies.** 4.8x llama.cpp decode, 2.65x GNN training, 1.3x vLLM throughput, 21-29% FAISS build reduction, 95% multi-tenant P99 scheduling -- these are significant and practical improvements.

**S4: Device-side SIMT-aware verifier is a genuine technical contribution.** The warp-uniform vs lane-varying distinction, warp-leader execution model, and the CFG-based uniformity checking are well-motivated solutions to a real problem (C2). The 60-80% overhead reduction vs naive per-thread injection is convincing.

**S5: Low mechanism overhead.** <0.2% host runtime overhead with hooks enabled but no policy, and the observability tools show 3-14% vs NVBit's 85-87% -- strong evidence for practical deployability.

**S6: Good policy composability story.** Table 1 shows 11 policy building blocks ranging from 16 to 925 LOC, demonstrating that non-trivial policies can be expressed concisely. The multi-tenant benchmark composing Quota LRU + Priority Tree Prefetch + Dynamic Timeslice is a compelling example.

## Weaknesses

**W1 (Major): Device-side eBPF evaluation is thin and disconnected from the headline results.**

The paper's key novelty is the device-side eBPF runtime (SIMT verification, binary trampolines, warp-level execution), yet the evaluation barely exercises it on real workloads:
- The device-side microbenchmark is a **32-element** vector-add -- trivially small and unrepresentative of real GPU kernels with complex control flow, shared memory, tensor cores, or cooperative groups.
- The block scheduler (the only device-side scheduling demo) uses **synthetic** GEMM with artificial imbalance. No real workload benefits from it.
- Device-side observability is only evaluated on **P40** (old architecture), not RTX 5090.
- The device-side memory prefetch (L2 trigger) only appears in the vector-add microbenchmark.

The paper does use cross-layer mechanisms in some results — the memory microbenchmark (1.77x) demonstrates device+host prefetch coordination, the CLC block scheduler is device-side, FAISS mentions device-side trigger, and GNN uses uprobe (beyond struct_ops). However, **the biggest headline numbers (llama.cpp 4.8x, vLLM 1.3x) come from pure host-side struct_ops policies**, while device-side contributions are concentrated in small-scale microbenchmarks and observability tools. The paper's most novel contribution (device-side eBPF with SIMT verification) hasn't been shown to produce headline improvements on real workloads. This creates a disconnect between what the design section promises and what the evaluation demonstrates.

**W2 (Major): Comparison baselines are selectively weak.**

- **llama.cpp 4.8x**: The comparison is against `ncmoe=32/64` CPU offloading, which is known to be slow for decode. The more relevant baseline is UVM + `cudaMemAdvise` hints, which the paper acknowledges performs closer but doesn't quantify the gap precisely. Readers need to know: is gpu_ext 4.8x better than the worst baseline, or 20% better than the most practical one?
- **vLLM 1.3x**: Compared against `--cpu-offload-gb 8`. The paper itself says this "matches LMCache" -- if gpu_ext matches an existing system paper, this is incremental. Moreover, vLLM's standard operation doesn't use UVM at all; the paper creates a UVM scenario that doesn't reflect typical deployment.
- **Missing experimental comparison** with Forest (ISCA '25), HELM (SC '25), DREAM (ICS '25), SUV (MICRO '24) -- all cited in S2 as work on UVM policies but never compared against. Even if reimplementation is difficult, the paper should explain why these baselines are excluded and discuss expected relative performance.
- **GNN**: Uses random graphs, not standard benchmarks (OGB, Reddit). Random graph locality may overstate prefetch benefits.

**W3 (Major): Security model and policy composition are underspecified for multi-tenant claims.**

The paper makes multi-tenant coordination a key selling point but:
- The threat model (S4.4) only considers "buggy or misconfigured" policies. In multi-tenant GPU environments, a malicious tenant could craft policies that aggressively prefetch their own pages while starving others -- the paper doesn't discuss how the kernel prevents this.
- **Policy composition is unexplored**: What happens when tenant A's eviction policy conflicts with tenant B's prefetch policy? The paper says "the kernel retains eviction authority under memory pressure" but this fallback is FIFO -- hardly fair.
- The scheduling interface allows setting arbitrary timeslice and priority. Who decides which tenant gets priority? The paper assumes a trusted administrator, but doesn't discuss the policy for the policy.

**W4 (Major): Host-side contribution may be incremental over the natural evolution of Linux kernel extensibility.**

The host-side `struct_ops` pattern for GPU memory management is essentially applying the existing Linux eBPF struct_ops mechanism to NVIDIA's UVM module. The paper should clearly articulate what is **novel** about the hook design beyond "we applied struct_ops to a new domain." Specifically:
- How does the memory interface design (activate/access/evict_prepare/prefetch) compare to cache_ext's interface design decisions?
- What makes these four hooks sufficient? Is there a formal argument for completeness, or was this empirically determined?
- The ~100 LOC kernel module change to NVIDIA UVM seems surprisingly small -- is this really a new "OS policy interface" or an instrumentation layer?

**W5 (Medium): Cross-layer map consistency model is hand-waved.**

"Relaxed, eventual consistency" with "periodic snapshot flush at GPU kernel completion boundaries" is vague:
- What is the staleness bound? Is it one kernel launch, or unbounded?
- The paper claims staleness "cannot violate correctness invariants such as memory integrity, which remain enforced by the GPU driver and hardware MMU." But correctness for **policies** is different from memory safety -- a stale eviction counter can cause systematic unfairness or thrashing that the MMU cannot prevent.
- No experiment measures the impact of map staleness on policy decision quality.

**W6 (Medium): Binary rewriting for device-side eBPF is fragile and compatibility concerns are unaddressed.**

- The paper says gpu_ext "dynamically intercepts CUDA runtime APIs to extract GPU kernel PTX, rewrite it with binary trampolines." What happens with:
  - GPU kernels that use the full register file (spilling to local memory)?
  - Cooperative groups or multi-block synchronization?
  - Tensor core operations (WMMA/MMA) with strict register layout?
  - CUDA toolkit version updates that change PTX semantics?
- No discussion of which GPU kernels are **incompatible** with binary rewriting, or what the failure mode is.

**W7 (Minor): Presentation issues.**

- Leftover internal annotations in source: `\xiangyu{...}`, `\yusheng{...}`, `\todo{...}` -- must be removed.
- `\speedupAccel` and `\speedupMonitor` are defined as "xxx" -- unfilled placeholders.
- S3 (Design Principles) and S4.1 (Challenges) are both previewed in S1, creating triple repetition.
- Figure 1 heatmaps are nearly illegible at column width.
- Table 1's "Preemption Control" at 925 LOC undermines the "tens to hundreds of lines" claim.
- The paper doesn't report confidence intervals or variance for any experiment.

## Questions for Authors

1. **Which headline results actually use device-side eBPF?** For each of the four workloads (llama.cpp, vLLM, GNN, FAISS), can you specify exactly which components (host hooks, device hooks, cross-layer maps) are exercised?

2. **What is the llama.cpp speedup vs UVM+cudaMemAdvise specifically?** The paper says hints "still fall behind our policy" -- is this 1.1x or 2x?

3. **How does the verifier handle real GPU kernel complexity?** What is the rejection rate for device-side policies? How many policy iterations did developers need before passing verification?

4. **What prevents a tenant from writing an eviction policy that preferentially evicts other tenants' pages?** The current interface allows reordering the eviction list -- can a malicious policy always move its own pages to tail and competitors' to head?

5. **Has any real workload benefited from the device-side block scheduler?** The GEMM evaluation uses synthetic imbalance. Is there evidence this mechanism helps production workloads?

6. **How does the system handle CUDA toolkit updates?** If NVIDIA changes PTX conventions or adds new instructions, does binary rewriting break?

## Minor Comments

- S2.2 "Device-Side Execution" paragraph: the phrasing "compiled into static binaries specifying precise thread-level SIMT behaviors" is slightly misleading -- PTX is an intermediate representation, not a final binary.
- S4.3.1: The interface listing shows `gdev_mem_prefetch(gmem_region_t* r)` returning `int` -- what do return values mean? This should be documented.
- S6.2.2 FAISS: "stride prediction" for K-means sequential scans -- this is essentially sequential prefetch, not prediction. The terminology overstates the mechanism.
- S7 Portability: "AMD ROCm with XNACK retryable faults" -- XNACK is disabled by default on most AMD GPUs and has significant performance implications. The portability story is weaker than presented.

## Overall Assessment

**Score: Weak Accept / Borderline**

This is an ambitious systems paper with a compelling vision and strong host-side memory policy results. The idea of treating the GPU driver as a programmable OS subsystem via eBPF is sound and timely. However, the paper has a significant tension between what it **promises** (cross-layer host+device eBPF) and what it **delivers** (almost entirely host-side driver hooks). The device-side eBPF -- the paper's most novel contribution -- is inadequately evaluated on trivially small benchmarks with no real workload demonstrating its value. The comparison baselines are selectively weak (4.8x vs worst-case offloading), the multi-tenant security model is underspecified, and several important design aspects (policy composition, map consistency, binary rewriting robustness) need deeper treatment.

**Recommendation**: The paper would benefit from (1) a real workload where device-side eBPF demonstrably helps, (2) stronger baselines including UVM+hints quantified precisely, (3) a clearer separation of what the host-side hooks alone achieve vs. the full system, and (4) discussion of policy composition and fairness in multi-tenant settings. If the device-side story were stronger, this would be a clear accept; as-is, the host-side results alone may not be sufficient novelty over applying struct_ops to NVIDIA UVM.
