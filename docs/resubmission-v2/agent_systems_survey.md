# Survey: AI Agents for System-Level Optimization

**Date**: 2026-03-29
**Purpose**: Literature survey to support the claim that practitioners increasingly rely on AI agents to explore and optimize system-level decisions, and to contextualize our work on BPF-based GPU memory management within this broader trend.

---

## 1. LLM/AI Agents for OS-Level and Kernel-Level Optimization

This is the most directly relevant category. A surge of papers appeared in 2025, establishing OS kernel tuning as a first-class application of LLM agents.

### 1.1 OS-R1: Agentic OS Kernel Tuning with Reinforcement Learning
- **Authors**: Hongyu Lin, Yuchen Li, Haoran Luo, Kaichun Yao, Libo Zhang, Mingjie Xing, Yanjun Wu
- **Venue**: arXiv (2508.12551), August 2025
- **What it does**: Abstracts the Linux kernel configuration space (18,000+ options) as an RL environment for LLM exploration. Uses rule-based reward functions for reasoning standardization and performance awareness. Two-phase training for convergence.
- **Results**: Up to 5.6% performance improvement over heuristic tuning.
- **LLM/Agent**: Yes -- LLM agent with RL training loop.
- **Relevance**: HIGH. Same spirit as our work: agent-driven exploration of kernel-level policies. They tune kernel configs; we explore GPU memory eviction/prefetch policies via BPF.

### 1.2 SchedCP / "Towards Agentic OS" -- LLM Agent Framework for Linux Schedulers
- **Authors**: Yusheng Zheng et al. (eunomia-bpf)
- **Venue**: arXiv (2509.01245), September 2025
- **What it does**: First framework enabling fully autonomous LLM agents to optimize Linux schedulers. Uses MCP (Model Context Protocol) server architecture with three services: Workload Analysis Engine, Scheduler Policy Repository, Execution Verifier. LLM agents analyze workloads, synthesize custom eBPF scheduling policies, deploy via sched_ext.
- **Results**: 1.79x performance improvement, 2.11x P99 latency improvement, 1.60x throughput gain, 13x cost reduction vs. naive agentic approaches.
- **LLM/Agent**: Yes -- multi-agent system generating eBPF code.
- **Relevance**: VERY HIGH. Directly analogous to our work. They use LLM agents to generate sched_ext BPF schedulers; we use agents to explore BPF struct_ops for GPU memory management. Same mechanism (eBPF), same kernel interface pattern (struct_ops), different subsystem (CPU scheduling vs. GPU memory).

### 1.3 "An Expert in Residence": LLM Agents for Always-On OS Tuning
- **Authors**: Liargkovas et al.
- **Venue**: NeurIPS 2025 Workshop on ML for Systems (ML4Sys)
- **What it does**: Online LLM-driven agent that emulates expert reasoning for continuous OS optimization. Studies CFS scheduler tuning (latency_ns, min_granularity_ns) for p99 tail latency minimization. Uses MCP for tool discovery and invocation.
- **Results**: Converges faster and more stably than Bayesian Optimization, RL, and human experts under dynamic workloads.
- **LLM/Agent**: Yes -- always-on LLM agent with tool access (iostat, perf, knob writes).
- **Relevance**: HIGH. Demonstrates that LLM agents can outperform traditional ML for online systems tuning. Validates the agent-driven exploration paradigm.

### 1.4 BYOS: Knowledge-Driven LLMs for OS Kernel Tuning
- **Authors**: (same group as OS-R1)
- **Venue**: arXiv (2503.09663), March 2025
- **What it does**: Dual-layer Knowledge Graph (OD-KG) bridges tuning objectives with config options. Mitigates LLM hallucinations through graph-guided reasoning. Incremental knowledge maintenance for kernel evolution.
- **Results**: 7.1%-155.4% improvement over defaults (up to 155.4% on UnixBench, 42.7% latency reduction on Nginx).
- **LLM/Agent**: Yes -- LLM with structured knowledge graph.
- **Relevance**: HIGH. Demonstrates the knowledge-grounding challenge for LLM-based system tuning.

### 1.5 ICMOS: Incremental Concept Mining for OS Kernel Configuration
- **Authors**: (submitted to OpenReview)
- **Venue**: Under review, 2025
- **What it does**: Integrates LLMs with heterogeneous knowledge graph (OSKC-KG) for kernel configuration mining. Uses DeepSeek-V3 as core LLM, Neo4j for knowledge storage.
- **Results**: Tail latency reduced up to 58.1%, throughput improved up to 11.0%. Optimization time reduced by 50%, configuration success rate from 29% to 76%.
- **LLM/Agent**: Yes -- agentic LLM reasoning with knowledge graph.
- **Relevance**: HIGH.

### 1.6 AIOS: LLM Agent Operating System
- **Authors**: Mingyu Jin et al. (Rutgers University)
- **Venue**: COLM 2025
- **What it does**: Proposes an OS architecture where LLMs act as the kernel, with agents as applications. AIOS kernel provides scheduling, context management, memory management, storage management, access control for runtime agents.
- **Results**: Up to 2.1x faster execution for serving agents across frameworks (AutoGen, LangChain, ReAct, etc.).
- **LLM/Agent**: Yes -- LLM-as-kernel architecture.
- **Relevance**: MEDIUM. Different focus (OS for agents, not agents for OS), but shows the OS-AI convergence trend.

---

## 2. LLM/AI Agents for GPU-Specific Optimization

This category has exploded in 2025-2026, but almost entirely focused on **CUDA kernel generation and optimization** rather than GPU resource management.

### 2.1 Astra: Multi-Agent System for GPU Kernel Performance Optimization
- **Authors**: Anjiang Wei, Tianran Sun, Yogesh Seenichamy, Hang Song, Anne Ouyang, Azalia Mirhoseini, Ke Wang, Alex Aiken (Stanford)
- **Venue**: NeurIPS 2025
- **What it does**: First LLM-based multi-agent system for GPU kernel optimization. Four specialized agents (testing, profiling, planning, coding). Starts from existing CUDA implementations in SGLang.
- **Results**: Average 1.32x speedup using zero-shot o4-mini.
- **LLM/Agent**: Yes -- multi-agent LLM system.
- **Relevance**: MEDIUM. GPU optimization via agents, but at kernel code level, not memory management.

### 2.2 CUDA Agent: Large-Scale Agentic RL for CUDA Kernel Generation
- **Authors**: ByteDance/Tsinghua
- **Venue**: arXiv (2602.24286), February 2026
- **What it does**: RL-trained agent for CUDA kernel optimization. Three-stage pipeline: seed crawling, combinatorial synthesis, execution-driven filtering. Skill-augmented execution environment.
- **Results**: State-of-the-art on KernelBench: 100%, 100%, 92% faster rate on L1/L2/L3. Outperforms Claude Opus 4.5 and Gemini 3 Pro by ~40% on L3.
- **LLM/Agent**: Yes -- RL-trained LLM agent.
- **Relevance**: MEDIUM. Demonstrates agents as "active systems optimizers" for GPU, but kernel code, not resource management.

### 2.3 AI CUDA Engineer (Sakana AI)
- **Authors**: Sakana AI
- **Venue**: arXiv (2509.14279), February/September 2025
- **What it does**: Agentic framework for CUDA kernel discovery, verification, and optimization. Uses evolutionary search + RAG + profiling feedback. Released 17,000+ verified CUDA kernels.
- **Results**: 10-100x speedup over common PyTorch operations.
- **LLM/Agent**: Yes -- agentic LLM with evolutionary search.
- **Relevance**: MEDIUM.

### 2.4 CudaForge: Agent Framework with Hardware Feedback for CUDA Kernel Optimization
- **Authors**: (published October 2025)
- **Venue**: arXiv (2511.01884)
- **What it does**: Two LLM agents (Coder + Judge) iteratively generate, correct, optimize CUDA kernels with Nsight Compute feedback.
- **Results**: 97.6% correctness, 1.68x average speedup over PyTorch. ~$0.3 API cost per kernel.
- **LLM/Agent**: Yes -- multi-agent with hardware profiling feedback.
- **Relevance**: MEDIUM.

### 2.5 KernelAgent (Meta/PyTorch)
- **Authors**: Meta PyTorch team
- **Venue**: Blog post + GitHub, 2025
- **What it does**: Hardware-guided GPU kernel optimization via multi-agent orchestration. Profile -> Diagnose -> Prescribe -> Orchestrate -> Explore -> Measure loop.
- **Results**: 100% correctness on KernelBench L1/L2/L3. 1.56x speedup over torch.compile, 89% of H100 roofline.
- **LLM/Agent**: Yes -- multi-agent system.
- **Relevance**: MEDIUM.

### 2.6 KernelSkill: Multi-Agent Framework for GPU Kernel Optimization
- **Venue**: arXiv (2603.10085), March 2026
- **What it does**: Dual-level memory (long-term expert skills + short-term trajectory). Replaces implicit heuristics with knowledge-driven optimization.
- **Results**: 5.44x, 2.82x, 1.92x speedups on KernelBench L1/L2/L3.
- **LLM/Agent**: Yes -- multi-agent with structured memory.
- **Relevance**: MEDIUM.

### 2.7 KernelCraft: Benchmarking Agentic Kernel Generation on Emerging Hardware
- **Venue**: arXiv (2603.08721), March 2026
- **What it does**: First benchmark for agentic kernel generation on customized accelerators (PLENA, AMD NPU, Coral NPU, RISC-V). Agents refine kernels under ISA and hardware constraints.
- **Results**: Agents produce functionally valid kernels for unseen ISAs within a few refinement steps.
- **LLM/Agent**: Yes -- agentic with feedback loop.
- **Relevance**: MEDIUM.

### 2.8 KernelBench: Can LLMs Write GPU Kernels?
- **Authors**: Stanford Scaling Intelligence Lab
- **Venue**: ICML 2025
- **What it does**: Benchmark of 250 PyTorch operations for LLM-driven CUDA kernel generation.
- **Results**: Best models match PyTorch in <20% of cases (zero-shot); agents significantly improve this.
- **LLM/Agent**: Benchmark for agent evaluation.
- **Relevance**: LOW-MEDIUM. Benchmark, not a system.

### 2.9 CuAsmRL: Optimizing GPU SASS Schedules via Deep RL
- **Authors**: (Cambridge)
- **Venue**: CGO 2025
- **What it does**: RL agent that plays an "assembly game" to find optimal GPU SASS schedules. Operates on compiled Triton code.
- **Results**: Up to 26% improvement over -O3 optimized schedules, 9% average.
- **LLM/Agent**: Traditional RL (not LLM-based).
- **Relevance**: LOW-MEDIUM. RL for GPU optimization but not LLM-based.

### 2.10 Gap: GPU Resource Management
- **No papers found** that use LLM/AI agents specifically for GPU memory management, eviction policies, prefetch policies, or UVM/unified memory optimization. The closest is traditional ML work on page fault prediction (Transformer-based models for prefetching), but these are not agent-based systems.
- This represents a clear gap in the literature that our work addresses.

---

## 3. LLM/AI Agents for Database Tuning

Database knob tuning is arguably the most mature application area for LLM agents in systems.

### 3.1 GPTuner: Manual-Reading Database Tuning via GPT-Guided Bayesian Optimization
- **Authors**: Jiale Lao et al. (Sichuan University)
- **Venue**: VLDB 2024 (pvldb 17(8), 2024) + SIGMOD 2024 demo
- **What it does**: LLM pipeline to collect/refine knowledge from database manuals. Prompt ensemble for structured knowledge views. Workload-aware knob selection + Coarse-to-Fine Bayesian Optimization.
- **Results**: 16x less tuning time, up to 30% performance improvement over SOTA.
- **LLM/Agent**: Yes -- LLM-guided Bayesian Optimization.
- **Relevance**: MEDIUM. Pioneering LLM-for-systems work. Good citation for establishing the trend.

### 3.2 lambda-Tune: LLMs for Automated Database System Tuning
- **Authors**: (University of Michigan / Cornell)
- **Venue**: SIGMOD 2025
- **What it does**: Generates entire configuration scripts using LLMs (GPT-4, Claude 3). Treats prompt generation as cost-based optimization. Optimizes knob settings + physical design.
- **Results**: Significantly more robust than prior approaches across PostgreSQL and MySQL.
- **LLM/Agent**: Yes -- LLM-driven configuration generation.
- **Relevance**: MEDIUM.

### 3.3 AgentTune: Agent-Based LLM Framework for Database Knob Tuning
- **Venue**: Proceedings of the ACM on Management of Data, February 2025
- **What it does**: First agent-based knob tuning framework. Agents collaborate via structured prompt chaining. Tree-based search to explore configuration space. Analyzes workloads to select impactful knobs.
- **Results**: Superior configurations with significantly fewer workload replays. Rarely generates invalid configurations.
- **LLM/Agent**: Yes -- multi-agent LLM system.
- **Relevance**: MEDIUM. Good example of agent-based exploration of configuration spaces.

### 3.4 MCTuner: Spatial Decomposition-Enhanced DB Tuning via LLM-Guided Exploration
- **Venue**: Proceedings of ACM on Management of Data, December 2025
- **What it does**: Mixture-of-Experts framework with LLM-based experts for knob importance. MCTS-based spatial decomposition of configuration space.
- **Results**: Up to 19.2% performance gains, 1.4x faster discovery.
- **LLM/Agent**: Yes -- LLM-guided MCTS exploration.
- **Relevance**: MEDIUM.

### 3.5 LLMTune: Accelerate Database Knob Tuning with LLMs
- **Venue**: arXiv (2404.11581), April 2024
- **What it does**: Uses GPT-4 to craft workloads; HEBO algorithm optimizes knobs for GPT-4-generated workloads to create training labels.
- **LLM/Agent**: Hybrid (LLM for workload generation, BO for optimization).
- **Relevance**: LOW-MEDIUM.

### 3.6 LGTune: LLM-Guided Database Knob RL Tuning System
- **Venue**: Springer, 2025
- **What it does**: Combines LLM guidance with reinforcement learning for knob tuning.
- **LLM/Agent**: Hybrid LLM+RL.
- **Relevance**: LOW-MEDIUM.

---

## 4. LLM/AI Agents for Compiler and Runtime Optimization

### 4.1 Meta LLM Compiler: Foundation Models of Compiler Optimization
- **Authors**: Meta AI
- **Venue**: CC 2025 (also arXiv 2407.02524, July 2024)
- **What it does**: Foundation model trained on 546B tokens of LLVM-IR and assembly. 7B and 13B parameter models. Fine-tuned for code size optimization and disassembly.
- **Results**: 77% of autotuning potential without compilations. Perfectly emulates compiler 20% of the time.
- **LLM/Agent**: Yes -- foundation LLM for compiler tasks.
- **Relevance**: MEDIUM. Establishes LLMs as viable for low-level system optimization.

### 4.2 Reasoning Compiler: LLM-Guided Optimizations for Model Serving
- **Venue**: NeurIPS 2025
- **What it does**: LLM as proposal mechanism for hardware-informed compiler transformations + MCTS for structured search. Integrated with TVM.
- **Results**: 5.0x average speedup with 5.8x fewer samples than TVM. Up to 2.5x speedup with 36 samples vs. 16x more for evolutionary search.
- **LLM/Agent**: Yes -- LLM-guided MCTS.
- **Relevance**: MEDIUM-HIGH. Shows LLM agents dramatically improve sample efficiency for systems optimization.

### 4.3 AlphaEvolve / Magellan: Autonomous Compiler Heuristic Discovery
- **Authors**: Google DeepMind
- **Venue**: arXiv (2506.13131 / 2601.21096), 2025
- **What it does**: Gemini-powered evolutionary coding agent. Magellan evolves compiler passes by synthesizing C++ heuristics. Applied to LLVM function inlining, register allocation, XLA.
- **Results**: 32.5% speedup on FlashAttention. Borg heuristic in production for 1+ year, recovering 0.7% of Google's worldwide compute.
- **LLM/Agent**: Yes -- evolutionary LLM agent.
- **Relevance**: HIGH. Production deployment of AI agent for systems optimization at global scale. Strong citation.

### 4.4 ComPilot / Agentic Auto-Scheduling: LLM-Guided Loop Optimization
- **Venue**: IEEE (2511.00592), November 2025
- **What it does**: LLM as interactive optimization agent for loop nests. Closed-loop with compiler feedback (legality, speedup). No fine-tuning required.
- **Results**: 2.66x (single run), 3.54x (best-of-5) geometric mean speedup. Competitive with Pluto polyhedral optimizer.
- **LLM/Agent**: Yes -- agentic LLM in feedback loop.
- **Relevance**: MEDIUM.

### 4.5 DeCOS: Data-Efficient RL for Compiler Optimization Selection Ignited by LLM
- **Venue**: ICS 2025 (ACM International Conference on Supercomputing)
- **What it does**: RL engine for compiler optimization sequences, bootstrapped by LLM knowledge for initial training. Performance counter feedback.
- **Results**: Matches or outperforms Opentuner. Portable across applications and hardware.
- **LLM/Agent**: Hybrid (LLM bootstraps RL agent).
- **Relevance**: MEDIUM.

### 4.6 Improving Parallel Program Performance with LLM Optimizers via Agent-System Interfaces
- **Authors**: Anjiang Wei et al. (Stanford)
- **Venue**: ICML 2025
- **What it does**: LLM-powered agent generates and refines mappers (task-to-processor, data-to-memory mappings) for parallel programs via a DSL interface.
- **Results**: Up to 1.34x speedup, reducing tuning time from days to minutes. Outperforms expert-written mappers.
- **LLM/Agent**: Yes -- LLM agent with DSL interface.
- **Relevance**: HIGH. Directly demonstrates agent-driven exploration of resource mapping policies for parallel systems.

---

## 5. LLM/AI Agents for Systems Configuration (Cloud, Networking, Storage)

### 5.1 StorageXTuner: LLM Agent-Driven Automatic Tuning for Heterogeneous Storage
- **Venue**: arXiv (2510.25017), October 2025
- **What it does**: Four-agent architecture (Executor, Extractor, Searcher, Reflector). Insight-driven tree search with layered memory. Cross-system reuse.
- **Results**: Up to 575% higher throughput, 88% lower p99 latency on RocksDB/LevelDB/CacheLib/MySQL.
- **LLM/Agent**: Yes -- multi-agent LLM system.
- **Relevance**: MEDIUM-HIGH. Shows agent-driven exploration of storage system configuration spaces.

### 5.2 STELLAR: Storage Tuning Engine Leveraging LLM Autonomous Reasoning
- **Venue**: SC 2025 (Supercomputing) -- ACM/IEEE
- **What it does**: LLM agent that reads software manuals, analyzes I/O traces, selects tuning strategies, reruns applications, and reflects on experience. Multi-agent design with RAG.
- **Results**: Up to 7.8x speedup over defaults. Near-optimal configurations within 5 attempts (vs. hundreds of thousands for autotuners).
- **LLM/Agent**: Yes -- autonomous LLM agent.
- **Relevance**: HIGH. Demonstrates human-expert-level autonomous systems tuning. Strong citation for the agent paradigm.

### 5.3 Confucius: Intent-Driven Network Management with Multi-Agent LLMs
- **Authors**: Meta, Harvard, Johns Hopkins, Stony Brook
- **Venue**: ACM SIGCOMM 2025
- **What it does**: Production-ready multi-agent LLM framework for network management at Meta. DAG-based workflow modeling, RAG for memory, integration with validation tools. Operational for 2+ years with 60+ applications.
- **Results**: Production deployment at hyperscale.
- **LLM/Agent**: Yes -- multi-agent LLM system.
- **Relevance**: HIGH. Production-deployed agent system for infrastructure management at major company. Very strong citation.

### 5.4 NetConfEval: Can LLMs Facilitate Network Configuration?
- **Venue**: CoNEXT 2024 (ACM Proceedings on Networking)
- **What it does**: First model-agnostic benchmark for LLMs in network configuration. Four tasks: formal specification generation, API call generation, routing algorithm development, low-level configuration generation.
- **Results**: GPT-4-Turbo can generate P4 configurations from natural language.
- **LLM/Agent**: Benchmark/evaluation.
- **Relevance**: LOW-MEDIUM.

### 5.5 ORACL: LLM-Based Autoscaling for Microservices
- **Venue**: arXiv (2602.05292), February 2026
- **What it does**: Transforms runtime telemetry into semantic descriptions, uses LLM chain-of-thought reasoning for root-cause diagnosis and resource allocation.
- **Results**: 15% better root-cause accuracy, 24x training speedup, 6% QoS improvement.
- **LLM/Agent**: Yes -- LLM agent with chain-of-thought.
- **Relevance**: MEDIUM.

---

## 6. General Autonomous Agent Frameworks Applied to Systems Tasks

### 6.1 SWE-agent: Agent-Computer Interfaces for Automated Software Engineering
- **Authors**: Princeton/Stanford
- **Venue**: NeurIPS 2024
- **What it does**: Autonomous agent that takes GitHub issues and fixes them. Custom agent-computer interface for file navigation, code editing, test execution.
- **Results**: 12.5% on SWE-bench (SOTA at release). Subsequent agents reached 80%+ in 2025.
- **LLM/Agent**: Yes -- autonomous coding agent.
- **Relevance**: LOW-MEDIUM for our specific claim, but important context for agentic AI trend.

### 6.2 AlphaEvolve (Google DeepMind)
- (Listed under Compiler section, but also general framework)
- **Relevance**: HIGH. The Borg datacenter scheduling heuristic is a direct example of AI agents optimizing production systems.

### 6.3 Harvard CS249r: "Agentic AI for Computer Systems Design"
- **Venue**: Harvard graduate course, Fall 2025
- **What it does**: Entire course dedicated to how agentic AI transforms systems design across the stack -- compilers, processors, accelerators, chip placement. Taught by Vijay Janapa Reddi.
- **Relevance**: MEDIUM. Signals that the academic community considers this a first-class research direction.

---

## 7. AI for Systems: Surveys and Position Papers

### 7.1 ML for Systems Workshop (NeurIPS, running since 2018)
- **Venue**: NeurIPS Workshop, annual
- **Focus (2025)**: Using ML for challenges in large-scale ML systems, training/serving of emerging models, agentic workflows, power/carbon challenges.
- **Relevance**: Establishes the research community and long-running interest.

### 7.2 "The New Compiler Stack: A Survey on the Synergy of LLMs and Compilers"
- **Venue**: arXiv (2601.02045), January 2026
- **What it does**: Comprehensive survey of LLM-compiler integration.
- **Relevance**: MEDIUM. Survey that documents the trend.

### 7.3 "A Comprehensive Survey on LLM-Based Network Management and Operations"
- **Venue**: International Journal of Network Management (Wiley), 2025
- **What it does**: Surveys LLM applications in network management.
- **Relevance**: LOW-MEDIUM.

### 7.4 "Autotuning Systems: Techniques, Challenges, and Opportunities"
- **Authors**: Brian Kroth et al. (Microsoft Research)
- **Venue**: SIGMOD 2025 Tutorial
- **What it does**: Tutorial covering autotuning landscape including LLM-based methods.
- **Relevance**: MEDIUM. Authoritative overview from industry.

---

## 8. Analysis: Evidence Strength for Our Claim

### The Claim
"Practitioners increasingly rely on AI agents to explore and optimize system-level decisions."

### Evidence Assessment

**STRONG evidence for:**
- **OS kernel tuning**: 5+ papers in 2025 alone (OS-R1, SchedCP, Expert-in-Residence, BYOS, ICMOS). This is a genuine emerging subfield.
- **Database tuning**: 6+ papers (GPTuner, lambda-Tune, AgentTune, MCTuner, LLMTune, LGTune). Most mature area.
- **Compiler optimization**: Rich body of work (Meta LLM Compiler, Reasoning Compiler, AlphaEvolve, ComPilot, DeCOS). Includes production deployment at Google.
- **CUDA kernel optimization**: Explosion of work in 2025-2026 (Astra, CUDA Agent, AI CUDA Engineer, CudaForge, KernelAgent, KernelSkill, KernelCraft). At least 7 major systems.
- **Storage/infrastructure**: STELLAR (SC'25), StorageXTuner, Confucius (SIGCOMM'25 -- in production at Meta).

**THIN evidence for:**
- **GPU resource management (memory, eviction, prefetch)**: NO papers found using LLM/AI agents for GPU memory management policies, UVM optimization, or eviction/prefetch strategy exploration. The closest work is traditional ML (Transformer-based page fault prediction) but this is not agent-based.
- **GPU scheduling policies**: CuAsmRL uses RL for instruction scheduling but not for resource management.

### Gap Analysis

The literature reveals a clear pattern:
1. AI agents are successfully optimizing systems across **every major subsystem** -- CPU scheduling, database knobs, compiler passes, network configuration, storage tuning, CUDA kernel code.
2. GPU optimization has received massive agent attention, but **exclusively for kernel code generation** (translating PyTorch to CUDA, optimizing CUDA kernels).
3. **GPU resource management** (memory allocation, eviction policies, prefetch policies, oversubscription management) remains **untouched by agent-based approaches**.

This gap is notable because:
- GPU memory management involves the same type of policy exploration that agents excel at in other domains (kernel configs, scheduler policies, database knobs).
- The BPF struct_ops interface for GPU memory management (our contribution) provides exactly the kind of programmable, safe, hot-swappable policy interface that SchedCP/sched_ext provides for CPU scheduling.
- The configuration space (eviction policy, prefetch scope, cross-block strategy, phase detection) is complex enough to benefit from agent-driven exploration, as demonstrated by the 20+ configurations we tested manually.

### Recommended Citations for P1

**Tier 1 (directly supports "agents for kernel/system policies"):**
1. SchedCP (arXiv 2509.01245) -- LLM agents generating BPF scheduling policies via sched_ext
2. OS-R1 (arXiv 2508.12551) -- RL-based LLM agent for kernel tuning
3. "Expert in Residence" (NeurIPS ML4Sys 2025) -- Always-on LLM agent for OS tuning
4. AlphaEvolve/Magellan -- Production agent for compiler/datacenter heuristics at Google
5. Confucius (SIGCOMM 2025) -- Production multi-agent system at Meta for network management

**Tier 2 (supports "agent-driven exploration of system configurations"):**
6. STELLAR (SC 2025) -- Agent-driven storage tuning
7. AgentTune (ACM 2025) -- Agent-based database configuration exploration
8. Reasoning Compiler (NeurIPS 2025) -- LLM-guided compiler optimization search
9. CUDA Agent (arXiv 2602.24286) -- RL agent for GPU kernel optimization

**Tier 3 (establishes the broader trend):**
10. AIOS (COLM 2025) -- LLM-as-OS-kernel paradigm
11. SWE-agent (NeurIPS 2024) -- Agent-computer interfaces for code
12. Harvard CS249r course -- Academic recognition of the field
13. ML4Sys Workshop (NeurIPS, since 2018) -- Long-running research community

### Framing Recommendation for the Paper

The strongest framing is:

> "AI agents are now actively deployed for system-level policy optimization across the computing stack -- from CPU scheduling [SchedCP, Expert-in-Residence] and kernel configuration [OS-R1, BYOS] to database tuning [GPTuner, AgentTune], compiler optimization [AlphaEvolve, Reasoning Compiler], and even production network management [Confucius]. GPU optimization has seen particular attention for kernel code generation [CUDA Agent, Astra, KernelAgent], yet GPU **resource management** -- the policies governing memory eviction, prefetch, and oversubscription -- remains unexplored by agent-based approaches. Our BPF struct_ops interface for GPU memory management fills this gap, providing the same kind of programmable, safe, hot-swappable policy mechanism for GPU memory that sched_ext provides for CPU scheduling, and that SchedCP has shown to be amenable to agent-driven exploration."

This is honest: we can cite a rich body of agent-for-systems work, acknowledge the GPU kernel optimization boom, and clearly identify GPU resource management as the open frontier our work enables.

---

## Appendix: Summary Table

| Paper | Year | Venue | System Aspect | LLM/Agent? | GPU-Related? |
|-------|------|-------|--------------|------------|--------------|
| OS-R1 | 2025 | arXiv | Kernel config | LLM+RL agent | No |
| SchedCP | 2025 | arXiv | CPU scheduling (BPF) | Multi-agent LLM | No |
| Expert-in-Residence | 2025 | NeurIPS ML4Sys | OS tuning | LLM agent | No |
| BYOS | 2025 | arXiv | Kernel config | LLM+KG | No |
| ICMOS | 2025 | Under review | Kernel config | LLM+KG | No |
| AIOS | 2025 | COLM | OS for agents | LLM-as-kernel | No |
| Astra | 2025 | NeurIPS | CUDA kernels | Multi-agent LLM | Yes (kernel code) |
| CUDA Agent | 2026 | arXiv | CUDA kernels | RL LLM agent | Yes (kernel code) |
| AI CUDA Engineer | 2025 | arXiv | CUDA kernels | Evolutionary LLM | Yes (kernel code) |
| CudaForge | 2025 | arXiv | CUDA kernels | Multi-agent LLM | Yes (kernel code) |
| KernelAgent | 2025 | Meta/PyTorch | CUDA kernels | Multi-agent LLM | Yes (kernel code) |
| KernelSkill | 2026 | arXiv | CUDA kernels | Multi-agent LLM | Yes (kernel code) |
| KernelCraft | 2026 | arXiv | Accelerator kernels | Agentic LLM | Yes (kernel code) |
| CuAsmRL | 2025 | CGO | GPU assembly | RL (not LLM) | Yes (scheduling) |
| GPTuner | 2024 | VLDB | Database knobs | LLM+BO | No |
| lambda-Tune | 2025 | SIGMOD | Database config | LLM | No |
| AgentTune | 2025 | ACM | Database knobs | Multi-agent LLM | No |
| MCTuner | 2025 | ACM | Database knobs | LLM+MCTS | No |
| Meta LLM Compiler | 2024/2025 | CC | Compiler | Foundation LLM | No |
| Reasoning Compiler | 2025 | NeurIPS | Compiler (TVM) | LLM+MCTS | No |
| AlphaEvolve | 2025 | arXiv | Compiler/datacenter | Evolutionary LLM | No |
| ComPilot | 2025 | IEEE | Loop optimization | Agentic LLM | No |
| DeCOS | 2025 | ICS | Compiler passes | LLM+RL | No |
| Agent-System Interfaces | 2025 | ICML | Parallel mapping | LLM agent | No |
| StorageXTuner | 2025 | arXiv | Storage systems | Multi-agent LLM | No |
| STELLAR | 2025 | SC | Parallel file sys | Multi-agent LLM | No |
| Confucius | 2025 | SIGCOMM | Network mgmt | Multi-agent LLM | No |
| NetConfEval | 2024 | CoNEXT | Network config | Benchmark | No |
| ORACL | 2026 | arXiv | Cloud autoscaling | LLM agent | No |
| SWE-agent | 2024 | NeurIPS | Code repair | Coding agent | No |
| **Our work (gpu_ext)** | **2026** | **--** | **GPU memory mgmt** | **Agent-amenable BPF** | **Yes (resource mgmt)** |
