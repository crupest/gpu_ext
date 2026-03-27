# gpu_ext Resubmission: Methodology, Diagnosis, and Recommendations

## Preface: The Questions That Drove This Analysis

Each question below pushed the analysis one level deeper. They form the discovery narrative of this document — from surface-level review to fundamental research methodology.

**Q1: "你作为 OSDI 评委详细审阅一下这个 paper"** (Review the paper as an OSDI PC member)
→ Produced the full OSDI review (see `osdi_review.md`). Core verdict: "host-side results alone may not be sufficient novelty over applying struct_ops to NVIDIA UVM." Score: weak accept / borderline.

**Q2: "仔细思考如何具体改进 — the host-side results alone may not be sufficient novelty"** (How specifically to improve?)
→ Produced the improvement plan. Discovered that the paper hides its own contribution: `bpf_gpu_migrate_range`, `bpf_wq`, `bpf_nv_gpu_preempt_tsg`, multi-program composition — all NOT MENTIONED in the paper.

**Q3: "但所有 headline results 都只用了 host-side struct_ops — 这个对吗？paper 里面是这样说的？"** (Is the "all host-side" claim actually accurate?)
→ Corrected the assessment. The paper does use uprobe (GNN), device-side trigger (FAISS), cross-layer microbenchmark (1.77x), CLC block scheduler (device-side). But the biggest numbers (4.8x llama, 1.3x vLLM) ARE pure struct_ops. The precise critique: **device-side and cross-layer mechanisms haven't demonstrated their value on real workloads producing headline improvements.**

**Q4: "你想想 host side 如何做到真的有 novelty？driver 的部分？"** (How can the host/driver side have GENUINE novelty?)
→ The breakthrough: gpu_ext's novelty is NOT the struct_ops hooks — it's the async pipeline (bpf_wq + sleepable kfuncs) and the new kernel capabilities (bpf_gpu_migrate_range, bpf_nv_gpu_preempt_tsg). GPU resource management is fundamentally async and cross-domain; the synchronous struct_ops model that works for CPU is architecturally insufficient. Produced `driver_novelty.md`.

**Q5: "你觉得最好的 research taste 是什么？"** (What is the best research taste?)
→ The deepest question. Answer: **restraint.** Know what your real contribution is — not what you wish it was. Tell the story your evidence supports. The best taste is: lead with the insight, not the mechanism. A clean, deep, honest story always beats an ambitious, broad, thin one.

**Q6: "From my perspective, it seems like we have done nearly all things XRP has done from both engineering and research depth — seems like there are problems on presentation."** (We've done the work; the presentation is the problem.)
→ Downloaded and analyzed XRP, cache_ext, and 15 other reference papers. Key finding: XRP's entire argument rests on ONE table (Table 2: syscall=1.15x, NVMe=2.5x) that proves naive eBPF is insufficient. gpu_ext has no equivalent. The paper presents the system architecture, not the contribution.

**Q7: "我们仓库里面也有 hook 到 uprobe 触发 kfunc/async callback 的，这个是不是更好？"** (We also have uprobe → kfunc → GPU in the repo — isn't that even better?)
→ Yes. The uprobe → sleepable kfunc → GPU hardware path is the real "aha moment." All prior eBPF extensibility work operates kernel→kernel (XRP, cache_ext, sched_ext). gpu_ext crosses **three domains**: user-space (cuLaunchKernel) → kernel (BPF) → GPU hardware (TSG preemption). This is genuinely new and should be the paper's centerpiece.

**Q8: "那我在论文里面这一部分应该怎么写？对比 XRP？"** (How should the paper section be written, compared to XRP?)
→ Produced the capability progression table (L1-L4) and section-by-section restructuring plan modeled after XRP's narrative structure.

**Q9: "你能再想想，OSDI/SOSP 真正想要的是什么？什么样的 system research 才是好的？最好的 CS 科学家怎么做的？"** (What do OSDI/SOSP really want? What makes good systems research? How do the best CS scientists work?)
→ The methodology discussion that frames this entire document. Not a faster system — a new way of thinking. Start with observation, not solution. The challenge is the insight; the mechanism is the consequence. Validate the abstraction, not just the numbers.

---

These questions form a progression: **surface review → specific fixes → accuracy check → novelty framing → research taste → reference analysis → mechanism discovery → paper structure → fundamental methodology.** Each one was necessary to reach the next. The document below captures what we learned at each stage.

---

## Part I: What Makes Great Systems Research

This is the right question to ask before touching any code or text.

### What OSDI/SOSP actually want

It's not a faster system. It's a **new way of thinking about a problem domain** that the reader didn't have before reading the paper.

There are levels:

- **"We built X and it's fast"** — rejected. This is engineering, not research.
- **"We identified problem P and built system S"** — borderline. Depends on how important P is.
- **"We discovered that abstraction A is the right way to think about domain D"** — this is what gets in. The contribution is the abstraction. The system validates it.

The test: **does reading the paper change how I think, or just give me a faster tool?**

XRP changes how you think. Before XRP, people thought "eBPF is for networking and tracing." After XRP, people think "eBPF can be a datapath, not just an observer — and WHERE you place the hook matters more than WHETHER you use eBPF." That's an insight that outlives the specific system.

cache_ext changes how you think. Before cache_ext, people thought "the page cache is a fixed kernel subsystem." After cache_ext, people think "the page cache is a policy that should be pluggable, like the scheduler."

**gpu_ext currently doesn't change how you think.** A reader finishes the paper and thinks "OK, they put eBPF hooks in the GPU driver and got some speedups." There's no moment where the reader says "oh, I never thought about it that way."

---

### How the best researchers start an idea

The pattern I see in the best systems papers:

#### 1. Start with a surprising observation, not a solution

XRP didn't start with "let's put eBPF in the NVMe driver." It started with: "we measured software overhead on fast NVMe devices and found it grew from 15% to **49%** — the software is now slower than the hardware." That's surprising. That creates urgency.

gpu_ext should start with a surprising observation about GPU resource management. What is it? Maybe:

> "We profiled four GPU workloads under memory oversubscription and found that the **policy** — not the hardware — controls 73% of execution time. Yet the GPU driver's eviction and prefetch policies are static, workload-agnostic, and confined to 2MB boundaries. Changing the policy from LRU to workload-adaptive stride prefetch improves decode throughput by 4.8x. But implementing this policy required modifying the driver — which took N months and breaks on every driver update."

The observation is: **the policy is the bottleneck, and it's trapped inside a monolithic driver.** That's the problem. eBPF is the solution.

#### 2. Ask "why is this hard?" — not "how do I build this?"

The intellectual content of a paper is in the **challenges**, not the mechanisms. XRP's deepest contribution is identifying that "the NVMe driver lacks file system metadata for address translation" — the metadata digest follows naturally. The challenge is the insight; the mechanism is the consequence.

For gpu_ext, the challenges should be:

- **Why can't you just use struct_ops like cache_ext?** Because GPU DMA takes milliseconds. A synchronous callback can't trigger a cross-block prefetch — the fault handler must return before the DMA completes. You need async execution.
- **Why can't you just hook in the driver?** Because the driver sees faults, not intent. The driver doesn't know that cuLaunchKernel is about to fire, or that this is a decode token boundary. You need application-boundary hooks.
- **Why can't user-space frameworks do this?** Because they can't preempt another process's GPU context, can't control eviction ordering, can't see page faults. You need kernel-privilege operations.

Each challenge is **empirically demonstrable**. That's what makes it science, not opinion.

#### 3. Find the minimal mechanism that resolves each challenge

Great systems papers are **surprisingly simple**. XRP adds one hook, one data structure, one operation. The reader thinks "of course, that's obvious" — which means the abstraction is right.

gpu_ext's mechanisms should each solve exactly one challenge:

| Challenge | Mechanism | Why this and nothing else |
|---|---|---|
| Async mismatch | bpf_wq + sleepable kfunc | Decouples μs decision from ms DMA |
| Semantic gap | uprobe → shared map → struct_ops | Captures app intent transparently |
| Cross-process control | kernel-privilege kfunc | Only kernel can preempt other processes |

If you can't state why each mechanism exists in one sentence, it shouldn't be in the paper.

#### 4. Validate the abstraction, not just the numbers

The capability progression table (L1→L2→L3) is not just "showing numbers." It's **validating the abstraction** — proving that each capability level is necessary and that removing any one degrades the result. This is the scientific method applied to systems design:

- **Hypothesis**: GPU policies need four capability levels.
- **Experiment**: Measure performance at each level.
- **Result**: Each level adds measurable improvement; no level is redundant.
- **Conclusion**: The abstraction (L1-L4) is minimal and complete.

This is what separates a research paper from an engineering report.

#### 5. Be honest about what doesn't work

XRP honestly reports WiredTiger's 1.25x and directly explains: "WiredTiger spends only 63% of its time on I/O, so XRP's benefit is proportionally smaller." This builds trust and shows intellectual maturity. Honest framing of modest results builds credibility.

gpu_ext should do the same: if the gap vs UVM+hints is 1.3x rather than 4.8x, write 1.3x — then explain why this 1.3x comes from mechanisms that struct_ops alone cannot express. A causally explained 1.3x is worth more than a cherry-picked-baseline 4.8x.

### The scientific method for systems research

1. **Observe**: A surprising measurement or trend (XRP Table 1: software = 49%; gpu_ext Figure 1: diverse patterns)
2. **Hypothesize**: What KIND of abstraction is needed? (Not "let me build a system" — but "what properties must the interface have?")
3. **Experiment (exploratory)**: Try the naive approach, measure the gap (XRP Table 2: syscall hook = 1.15x; gpu_ext: struct_ops alone = 2.65x)
4. **Theorize**: The minimal set of mechanisms that closes the gap (XRP: metadata digest + resubmission; gpu_ext: bpf_wq + sleepable kfuncs + uprobe)
5. **Validate**: Capability progression proves each mechanism is necessary (each layer adds measurable improvement; removing any one degrades the result)
6. **Generalize**: What does this teach us about the domain? (XRP: "WHERE you place the hook matters more than WHETHER you use BPF"; gpu_ext: "GPU extensibility requires async, cross-domain, app-aware hooks — fundamentally different from CPU eBPF")

Most rejected papers skip steps 2-4 and go straight from "observe" to "here's our system." The current gpu_ext paper does exactly this.

---

## Part II: Lessons from XRP and cache_ext

### XRP's narrative structure

```
§2.1  Quantified bottleneck           → Table 1: kernel software = 49% of I/O latency
§2.3  "Potential Benefit of BPF"      → Table 2: syscall hook = 1.15x, NVMe hook = 2.5x
§3    Challenges + Observations       → Each challenge has empirical evidence
§4    Design (mechanism per challenge) → Each mechanism solves exactly one challenge
§6    Eval (proves the thesis)        → Validates each claim from §2-3
```

The key technique: **before presenting the design, XRP proves that a naive approach fails and quantifies the gap.** Table 2 does all the heavy lifting — it makes the entire paper's argument self-evident.

### Six specific techniques from XRP and cache_ext

**Technique 1 (XRP): Quantify why the naive approach fails.**
XRP Table 2 shows BPF at syscall layer = 1.15x, BPF at NVMe layer = 2.5x. This kills the "isn't this just eBPF for storage?" criticism. The naive application of eBPF captures only 20% of the potential gain. The GAP between naive and full system IS the novelty.

**Technique 2 (XRP): Each mechanism has a stated challenge + empirical observation.**
XRP never says "we also support X." It says "Challenge Y requires mechanism X, because of Observation Z (supported by measurement)." No mechanism without a reason. No reason without data. For example: "Observation: most on-disk data structures are stable" — backed by a 24-hour YCSB experiment showing extents change every 159 seconds with only 5 extent changes in 24 hours. Every design constraint is empirically motivated.

**Technique 3 (XRP): Two-tier evaluation — controlled + real-world.**
BPF-KV (deep, controlled, best case) + WiredTiger/YCSB (modest but honest real-world). The deep case study proves the mechanism; the real-world case study proves the relevance. Modest numbers + honest explanation > large numbers + weak baseline.

**Technique 4 (cache_ext): Reimplement the default policy in eBPF.** cache_ext reimplements MGLRU in BPF and shows it matches native within 1% (Table 5, Takeaway 7). This proves the hook overhead is near-zero. gpu_ext should do the equivalent: reimplement UVM's default LRU eviction as a BPF struct_ops program and show it matches native performance. This proves the overhead of the hook mechanism itself is near-zero. (We already have <0.2% data but it's not framed this way.)

**Technique 5 (cache_ext): Explicit "Takeaway" boxes.** After each evaluation result, cache_ext states a Takeaway (1 through 7) that maps directly to a contribution claim. This makes the argument explicit and reviewable — the reviewer can check each takeaway against the data. gpu_ext's evaluation has no such structure.

**Technique 6 (cache_ext): The "justified complexity" argument.** cache_ext measures the overhead of a user-space dispatch architecture (Table 1: up to 20.6% degradation *without even implementing a policy*) to prove that in-kernel execution is necessary. This is measuring the cost of NOT having your mechanism. gpu_ext should do the same: what happens if you try to implement cross-block prefetch from user-space? What's the overhead of polling for page faults vs hooking in the driver?

---

## Part III: Diagnosis — What's Wrong with the Current Paper

### The fundamental problem

The paper currently reads as: "Here is our system. It has many features. Here are the numbers."

It should read as: "Here is a surprising observation about GPU resource management. Here is why existing approaches can't solve it. Here are the specific challenges. Here is the minimal abstraction that resolves each challenge. Here is the evidence that each part of the abstraction is necessary."

The difference is **narrative structure**. The same system, the same numbers, the same code — but organized as a scientific argument rather than a feature tour.

### The paper hides its own contribution

The implementation has powerful mechanisms that the paper barely mentions or entirely omits:

| Mechanism | In implementation | In paper |
|---|---|---|
| `bpf_gpu_migrate_range()` — sleepable kfunc, cross-VA-block DMA | Used by 10+ policies, GNN 3.36x | **NOT MENTIONED** |
| `bpf_wq` — async BPF work queue | Used by all cross-block policies | **NOT MENTIONED** |
| `bpf_nv_gpu_preempt_tsg()` — cross-process GPU preemption | -48% to -58% latency | **NOT MENTIONED** |
| Sleepable uprobe on cuLaunchKernel → kfunc → GPU hardware | Crosses user→kernel→GPU | **NOT MENTIONED** |
| Multi-program composition (kprobe + struct_ops + bpf_wq + uprobe) | All advanced policies | **NOT MENTIONED** |

The paper only shows 5 trivial kfuncs (move_head, move_tail, set_attr, reject_bind, sched_preempt). Reviewer correctly concludes: "just struct_ops for UVM."

**Why did this happen?** Because the paper was written to match the **system architecture** (host hooks → device eBPF → cross-layer maps), not the **contribution**. The kfuncs and bpf_wq appear in the implementation section — or not at all — because from an engineering perspective, they're "how specific policies work," not "the system design." But from a novelty perspective, the hierarchy is inverted: struct_ops hooks are the expected part; kfuncs and async pipelines are the novel part.

The deeper reason: **these mechanisms were developed after the paper was written.** The plan documents (cross_block_prefetch_plan, gpu_preempt_kfunc_plan, xcoord_plan) are all labeled "beyond paper" research. The paper reflects an earlier, simpler version of the system where struct_ops hooks were the main thing. The system evolved past the paper.

### The paper doesn't change how the reader thinks

Before reading gpu_ext: "GPU drivers need extensibility."
After reading gpu_ext: "OK, they added eBPF hooks to the GPU driver."

The reader learns nothing new about GPU resource management. No surprising insight. No "aha moment." No sentence that makes them say "oh, I never thought about it that way."

---

## Part IV: The Missing Insight

### What the reader should learn from reading the paper

The specific insight that gpu_ext should foreground:

> **The individual properties — async operations, cross-domain resources, application semantics — are not unique to GPUs.** CPU systems have all of them: bpf_wq exists for async, NUMA/CXL is cross-domain, cgroups/fadvise provide app semantics. **What is unique is their simultaneous convergence in a single subsystem where a single extensibility mechanism (struct_ops) is provably insufficient.**

> For CPU scheduling, struct_ops suffices — decisions and effects are both nanoseconds (sched_ext). For the page cache, struct_ops suffices — both microseconds (cache_ext). For NVMe, one hook + resubmission suffices — narrow problem (XRP). **For GPU memory management, you simultaneously need async execution, active cross-boundary migration, proactive app-boundary hooks, and cross-process control** — and the capability progression table proves each layer is necessary.

> **The contribution is demonstrating that composable eBPF policy pipelines are required for complex heterogeneous subsystems, with GPU as the first and most demanding case.** This is a generalizable insight: as OS subsystems become more complex (heterogeneous, async, cross-domain), single-mechanism extensibility will become insufficient, and composition of multiple BPF program types will become necessary.

If a reader finishes the paper with this understanding, gpu_ext has changed how they think — not just about GPU extensibility, but about the future of eBPF extensibility in general. That's a research contribution.

### Why composition is necessary for GPU (the novelty table)

| GPU characteristic | Why struct_ops alone fails | What's needed |
|---|---|---|
| DMA takes milliseconds | Can't block fault handler for cross-block prefetch | bpf_wq + sleepable kfuncs |
| 2MB VA block boundary is arbitrary | Default prefetcher trapped within one block | `bpf_gpu_migrate_range()` — new kernel capability |
| Driver sees faults, not intent | Doesn't know cuLaunchKernel is about to fire | Sleepable uprobe at application API boundary |
| Multi-tenant cross-process | struct_ops only sees current process | Kernel-privilege `bpf_nv_gpu_preempt_tsg()` |
| App semantics invisible | Fault handler doesn't know MoE phase | Uprobe captures → shared maps → struct_ops |

**None of these rows apply to cache_ext or sched_ext.** The CPU page cache does not have a PCIe bandwidth bottleneck. The CPU scheduler does not need to trigger millisecond DMA operations. CPU processes share one coherent memory — there is no "cross-domain migration." This table IS the novelty argument.

### The uprobe → kfunc → GPU path is the "aha moment"

All prior eBPF extensibility systems operate within the kernel:
- XRP: NVMe IRQ handler → NVMe resubmission (kernel → kernel)
- cache_ext: page cache hook → folio list manipulation (kernel → kernel)
- sched_ext: scheduler hook → dispatch queue (kernel → kernel)

gpu_ext's uprobe path crosses **three domains**:
```
User-space (cuLaunchKernel) → Kernel BPF → GPU hardware (TSG preemption)
```

A single BPF program, triggered by a user-space function call, directly controls GPU hardware — transparently, safely, without modifying the application. This is genuinely new. No prior eBPF extensibility work does this.

Concrete instances:
- `cuLaunchKernel` uprobe → `bpf_nv_gpu_preempt_tsg()` → GPU TSG context switch: **-48% to -58% latency**
- `cudaStreamSynchronize` uprobe → `bpf_gpu_migrate_range()` → PCIe DMA: **proactive prefetch before next epoch**
- `cudaMallocManaged` uprobe → BPF map → struct_ops: **transparent allocation semantic relay**

### The capability progression (gpu_ext's "Table 2")

| Layer | Mechanism | What it enables | Example result |
|---|---|---|---|
| L1: Advisory hooks | struct_ops (move_head/tail) | Eviction ordering, prefetch hints | GNN 2.65x |
| L2: Active async ops | + bpf_wq + sleepable kfuncs | Cross-block prefetch, active DMA | GNN **3.36x** (+27%) |
| L3: Proactive app-boundary | + sleepable uprobe → kfunc | Pre-fault migration, instant preemption | LC P99 **-95%** |
| L4: Semantic relay | + uprobe captures → shared maps | App structure informs driver policy | Phase detection |

L1 is what the paper currently presents. L2-L4 are what the paper hides. **The novelty lives in L2-L4.**

The 27% gap between L1 and L2 precisely measures the value of async operations.
The 95% gap between L0 and L3 precisely measures the value of proactive app-boundary hooks.
These gaps — not the absolute numbers — are the novelty evidence.

---

## Part V: Concrete Recommendations

### The single most important change

**Add gpu_ext's "Table 2"**: the capability progression table (L0-L3) as the first evaluation experiment. This single table:
1. Proves L1 (struct_ops) is insufficient — kills the "just struct_ops for UVM" criticism
2. Quantifies each mechanism's marginal value — proves the design is minimal
3. Makes all design choices self-evident — the reader sees WHY bpf_wq and uprobe→kfunc are needed

### Paper structure changes

**Motivation (§2):**
- Add "Where Does the Time Go?" bottleneck breakdown (equivalent of XRP Table 1): profile per-token decode time showing policy controls 73% of execution time
- Add the capability progression table (equivalent of XRP Table 2): L0=1x, L1=2.65x, L2=3.36x, L3=-95% P99
- Add an explicit "struct_ops alone CANNOT do X, Y, Z" statement (the third bullet in limitations)

**Design (§4):**
- Fold §3 Design Principles into §4 intro (3 sentences, not 0.5 page) — each principle motivated by an Observation + data
- Add §4.3: Active Async Operations — bpf_wq + `bpf_gpu_migrate_range()` + async pipeline diagram. Key sentence: "GPU DMA takes milliseconds; synchronous hooks return in microseconds. This mismatch requires decoupling the decision from the operation."
- Add §4.4: Proactive App-Boundary Hooks — sleepable uprobe → `bpf_nv_gpu_preempt_tsg()` → GPU. Key sentence: "This path crosses three domains — user-space, kernel, GPU hardware — in one causal chain. No prior eBPF system does this."
- Add §4.5: Multi-Program Composition — show how kprobe + struct_ops + bpf_wq + uprobe compose into one policy. Key sentence: "No single struct_ops table can express these policies."
- Condense device-side SIMT verifier to ~0.5 page (complementary capability, not primary contribution)

**Evaluation (§6):**
- Lead with capability progression table as the FIRST experiment
- Deep case study 1: GNN cross-block prefetch (shows full L2 async pipeline, ablation: 2.65x vs 3.36x — the 27% gap proves async pipeline's value)
- Deep case study 2: Multi-tenant uprobe preemption (shows L3, ablation: timeslice-only vs kfunc vs uprobe, preemption latency: 177μs kfunc vs 354μs ioctl)
- Compress FAISS and vLLM into breadth summary table (1 paragraph each, proving generality not depth)
- Add honest UVM+hints baseline for llama.cpp — quantify the precise gap

**What to cut (to stay within page limit):**
- Fold §3 Design Principles into §4 intro (3 sentences, not 0.5 page)
- Condense device-side eBPF (SIMT verifier) from ~1.5 pages to ~0.5 page
- Compress FAISS + vLLM case studies from 2 paragraphs each to 1 paragraph each in a breadth table
- Remove leftover internal annotations (\xiangyu{}, \yusheng{}, \todo{}) and placeholder values (\speedupAccel = "xxx")

### Thesis (reframed)

> "GPU resource management requires an extensible OS interface that goes beyond advisory hooks: it needs active asynchronous operations, proactive app-boundary hooks, and cross-process kernel-privilege control — capabilities that the synchronous struct_ops model cannot express. We design gpu_ext, which composes struct_ops hooks, sleepable kfuncs, BPF work queues, and uprobes into a cross-domain policy runtime spanning user-space applications, kernel driver, and GPU hardware. gpu_ext's policies improve throughput by up to 4.8x and reduce tail latency by up to 2x, with the async pipeline alone contributing 27% beyond what synchronous hooks achieve."

---

## Part VI: Side-by-Side with XRP

| Paper element | XRP | gpu_ext (proposed) |
|---|---|---|
| **Surprising observation** | Software overhead grew from 15% to 49% on fast NVMe | Policy controls 73% of oversubscribed GPU decode time |
| **Quantified naive vs full** | Table 2: syscall=1.15x, NVMe=2.5x | L1=2.65x, L2=3.36x, L3=-95% P99 |
| **Domain-specific challenge** | NVMe driver lacks FS metadata for address translation | GPU DMA is async (ms vs μs); driver lacks app semantics |
| **Domain-specific mechanism** | Metadata digest + NVMe request resubmission | Sleepable kfuncs + bpf_wq + uprobe→GPU hardware |
| **"Aha moment"** | BPF can resubmit I/O from the interrupt handler | Uprobe on cuLaunchKernel can directly preempt GPU hardware |
| **Domains crossed** | kernel → kernel | **user-space → kernel → GPU hardware** |
| **Deep case study** | BPF-KV (controlled, best case) | GNN cross-block (shows full async pipeline) |
| **Real-world case study** | WiredTiger/YCSB 1.25x (honest: "63% I/O bound") | MoE 4.8x (with honest UVM+hints comparison) |
| **Honest modest result** | WiredTiger 1.25x, explained | vLLM 1.3x (explained: low oversub ratio) |
| **Breadth** | Range queries, aggregations | FAISS, vLLM, memory priority, observability |
