# Agent Code Generation vs Configuration Search: Evidence (2025-2026)

## Summary

The claim "AI agents are evolving from configuration tuners to policy code writers" is **well-supported** by evidence. At least 5 systems generate actual executable code for systems-level policy (not just tune parameters).

## Per-System Classification

### Genuine Code Generation (systems-level policy)

**AlphaEvolve / Magellan (Google DeepMind, 2025)** — CODE GENERATION
- Generates actual executable C++ functions for LLVM compiler heuristics (143-line C++ heuristic vs LLVM's 2,115-line baseline)
- Produced Verilog rewrites for arithmetic circuits
- For Borg datacenter scheduling: discovered "simple yet remarkably effective heuristic" as human-readable code — in production 1+ year, recovering ~0.7% of Google's worldwide compute
- Blog: "proposes computer programs that implement algorithmic solutions as code"

**Barbarians at the Gate / ADRS (UC Berkeley, SIGOPS 2025)** — CODE GENERATION
- Explicitly treats systems as "white boxes" where AI "can rewrite system code itself"
- Concrete examples with code figures:
  - Spot instance scheduling: complete function with window tracking, dynamic parameters
  - MoE load balancing: LLM replaced greedy Python loop with PyTorch tensor ops
  - LLM-SQL reordering: generated code with caching logic, recursion thresholds
  - Transaction scheduling: two complete functions with greedy insertion + hill climb
- Contrasts with "prior work that treated systems as black boxes, leveraging AI to tune configuration knobs"

**PolicySmith (UT Austin / LDOS, HotNets 2025)** — CODE GENERATION
- Generates C++ caching heuristics for web cache replacement policies
- Experimental BPF congestion control code targeting Linux kernel integration
- Tagline: "Man-Made Heuristics Are Dead. Long Live Code Generators!"
- "All of the code in the generated heuristics, except the function prototype, was generated completely by the LLM"

**NECC (arXiv 2601.22461)** — CODE GENERATION
- LLM generates "refined CCA code" that compiles and "can be attached to the BPF interface"
- Output is deployable C code for BPF-based congestion control
- Manipulates kernel CCA variables like `snd_cwnd` and `sk_pacing_rate`

**Glia (MIT, SIGOPS 2025)** — CODE GENERATION (likely)
- Synthesized a novel Head-Room Admission (HRA) global scheduler
- Discovered ordering requests by prefill length reduces delay by 25%
- "Produces new algorithms for request routing, scheduling, and auto-scaling" and "performs hands-on engineering to implement them in code"
- No code listings shown, but described as new algorithmic implementations, not parameter tuning

**KernelEvolve (Meta, arXiv 2512.23236)** — CODE GENERATION (CUDA kernels, not resource mgmt)
- Generates GPU kernel code in Triton, CuTe DSL
- 100% pass rate on KernelBench (250 problems)
- Deployed on NVIDIA GPUs, AMD GPUs, Meta AI accelerators for production recommendation models
- Note: generates compute kernel code, not resource management policy

### Configuration Search (not code generation)

**ASAP (NeurIPS ML4Sys 2025)** — CONFIG SEARCH
- Outputs configuration parameter tuples: `{'model': 8, 'data': 16, 'seq': 4}`
- "Automates the diagnosis of sharding issues and generation of optimized configurations"
- No source code modified

**SchedCP (NeurIPS ML4Sys 2025)** — CONFIG SELECTION (code gen aspirational)
- In actual experiments, **selected existing sched_ext schedulers** (scx_rusty, scx_bpfland)
- Architecture supports code generation, but no LLM-generated scheduler demonstrated
- A motivation test: Claude Code wrote a FIFO BPF scheduler but needed 33 min, 221 API calls, $6

### Aspirational (proposed but not demonstrated)

**LDOS (UT Austin, SIGOPS 2026)** — ASPIRATIONAL
- Proposes LLM-generated kernel modules selected by RL controller
- "Existing LLMs are not sufficient to generate data with the fidelity and diversity required"
- Concrete results from RL controller selecting among modules, not from LLM-generated code

## Key Takeaway for gpu_ext Paper

| Claim | Evidence |
|-------|----------|
| "Agents write code, not just tune configs" | **Strong**: AlphaEvolve, ADRS, PolicySmith, NECC, Glia all generate code |
| "Agents write BPF code specifically" | **Moderate**: PolicySmith (BPF CC), NECC (BPF CCA), SchedCP (aspirational BPF sched) |
| "Agents write GPU resource management policy code" | **No direct evidence** — none target GPU memory/scheduling policy |

### Implication for P2-J

The opus reviewer's criticism #1 ("agent evolving to code writers is your wish, not reality") is **factually wrong** — there is strong evidence of agents writing systems code.

The opus reviewer's criticism #3 ("our eval is config search, not code generation") is **also wrong**:
- The 59 policies are **BPF programs** — actual kernel-level code loaded into the GPU driver via struct_ops, not parameter tuples
- The agent generates/modifies BPF code (eviction policies, prefetch strategies, scheduling logic), compiles it, loads it, and tests it
- This is directly analogous to PolicySmith (BPF CC code) and NECC (BPF CCA code) — all generate BPF programs for kernel subsystems
- gpu_ext is the **first system where an agent generates BPF code for GPU resource management policy**

Correct framing: "ALL evaluated policies were generated by an AI agent as BPF programs for GPU resource management (eviction ordering, prefetch strategies, scheduling logic). The 2.60x, 3.36x, and P99 -9.5% results are all from agent-written code. 59 policies explored, 0 kernel panics, all safety violations contained by the eBPF verifier."

### Citations to add

- PolicySmith: `\cite{policysmith2025}` — most relevant (BPF policy code generation)
- NECC: `\cite{necc2026}` — BPF congestion control code generation
- ADRS/Barbarians: already cited as `\cite{cheng2025barbarians}`
- AlphaEvolve: already cited as `\cite{alphaevolve2025}`
- KernelEvolve: `\cite{kernelevolve2025}` — GPU code generation (different domain but shows GPU+agent trend)
