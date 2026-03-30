# Agentic GPU / Systems Optimization Citations

Search date: 2026-03-29

Scope: papers, blog posts, and workshop systems that use AI/LLM/agentic methods for systems optimization, with emphasis on GPU/accelerator resource management, runtime tuning, kernel optimization, schedulers, and `sched_ext`/eBPF links.

Queries used included the user-specified set plus close variants:

- `barbarian at the gate sigops`
- `barbarians at the gate operating systems AI`
- `SIGOPS blog AI agent systems 2024 2025`
- `AI agent GPU optimization`
- `LLM GPU memory management scheduling`
- `agentic GPU systems`
- `sched_ext AI agent`
- `eBPF AI agent scheduling`
- `ML for Systems GPU optimization agent`
- `Google DeepMind TPU optimization agent`
- `Meta agentic kernel coding GPU`

Notes:

- I prioritized exact URLs and primary sources where possible.
- I split findings into direct agentic GPU/accelerator optimization, adjacent RL/ML optimization, and broader systems-position pieces.
- I did **not** find a strong, well-cited LLM-agent paper specifically for GPU memory eviction/prefetch/oversubscription in this pass. The strongest direct hits were kernel optimization, sharding/configuration, scheduler control, and DVFS/frequency tuning.

## 1. Exact SIGOPS / ACM hit: "Barbarians at The Gate"

## Barbarians at The Gate: How AI is Upending Systems Research

- Authors: Audrey Cheng, Shu Liu, Melissa Pan, Ion Stoica, and the ADRS team
- Venue: ACM SIGOPS Blog; companion arXiv preprint
- Year: 2025
- URL: https://www.sigops.org/2025/barbarians-at-the-gate-how-ai-is-upending-systems-research/
- Companion preprint: https://arxiv.org/abs/2510.06189
- Summary: This is the exact SIGOPS post you asked for. It argues that AI is beginning to replace manually designed heuristics in core systems problems, especially where search spaces are too large for human-only tuning. The piece explicitly claims successful AI-driven redesign of LLM training sharding, inference deployment, cloud resource allocation, and compiler optimization, and frames this as a shift in how systems research itself is done.
- Content highlights: The article argues that a hierarchical tree-search plus code-synthesizing LLM loop can redesign system policies end-to-end. It cites wins including up to 11.5x LLM training speedup, 4.17x inference throughput improvement, and 91.3x faster search over prior methods. It positions "systems intelligence" as especially relevant for heterogeneous clusters and cross-layer optimization.
- Relevance assessment: Very high. This is the exact SIGOPS source, and it directly motivates agentic optimization of GPU/accelerator systems.

## Let the Barbarians In: How AI Can Accelerate Systems Performance Research

- Authors: Audrey Cheng, Shu Liu, Melissa Pan, Zhifei Li, Shubham Agarwal, Mert Cemri, Ion Stoica, and the ADRS team
- Venue: ACM SIGOPS Blog
- Year: 2026
- URL: https://www.sigops.org/2026/let-the-barbarians-in-how-ai-can-accelerate-systems-performance-research/
- Summary: Follow-on SIGOPS post extending the ADRS thesis from "AI upends systems research" to "AI accelerates systems performance research." It focuses on using AI to automate the performance-engineering loop itself rather than only the final policy choice.
- Relevance assessment: High. Not GPU-only, but directly relevant as a position piece about agentic performance research workflows that can subsume GPU tuning.

## Defining System Intelligence

- Authors: Shu Liu, Ion Stoica, Audrey Cheng, Zhifei Li, Bowen Wang, Maaz Bin Safeer
- Venue: ACM SIGOPS Blog
- Year: 2025
- URL: https://www.sigops.org/2025/defining-system-intelligence/
- Summary: Concept paper defining "system intelligence" as autonomous management and optimization of complex systems. The post explicitly gives a GPU-cluster example: a scheduler reasoning over heterogeneous GPU demand across A100, H100, and L40S to improve utilization. It also frames autotuners and AI-driven agents as part of the same design space.
- Relevance assessment: High. Strong framing piece for heterogeneous GPU cluster optimization and agentic control planes.

## Glia: A Human-Inspired AI for Systems Design and Optimization

- Authors: Pouya Hamadanian, Pantea Karimi, Arash Nash-Esfahany, Kimia Noorbakhsh, Joseph Chandler, Ali Parandeh, Mohammad Alizadeh, Hari Balakrishnan
- Venue: ACM SIGOPS Blog
- Year: 2025
- URL: https://www.sigops.org/2025/glia-a-human-inspired-ai-for-systems-design-and-optimization/
- Summary: Presents Glia, a human-inspired AI system for automated systems design. The post emphasizes design memories, intuition/analysis, and self-improvement, then reports that on systems tasks such as vLLM serving and RAG optimization Glia outperforms humans and, in some cases, even the best human-designed solutions by up to 25%.
- Relevance assessment: High. Strong evidence that agentic optimization is already being applied to AI serving systems with GPU implications.

## LDOS: Toward A Learning-Directed Operating System

- Authors: Andrew C. Chen, Adam Dziedzic, Tongxin Li, Jiaye Teng, Noel Lutske, et al.
- Venue: ACM SIGOPS Blog
- Year: 2026
- URL: https://www.sigops.org/2026/ldos-toward-a-learning-directed-operating-system/
- Summary: LDOS proposes a learning-directed OS where LLM agents generate candidate kernel modules and an RL controller selects among them dynamically. The post reports 1.78x scheduling throughput over EEVDF and up to 16.4x over BORE, with the goal of adaptive OS policies rather than static scheduling heuristics.
- Relevance assessment: High. OS-level rather than GPU-specific, but directly relevant to agentic runtime and scheduler optimization.

## 2. Direct agentic GPU / accelerator optimization papers

## SwizzlePerf: Hardware-Aware LLMs for GPU Kernel Performance Optimization

- Authors: Arya Tschand, Kesavan Ramakrishnan, Muhammad A. Awad, Ryan Swann, Jeffrey Jian Ma, Keith Lowery, Vijay Janapa Reddi
- Venue: ML for Systems Workshop at NeurIPS
- Year: 2025
- URL: https://mlforsystems.org/assets/papers/neurips2025/paper23.pdf
- Summary: SwizzlePerf uses LLMs plus explicit hardware-awareness to generate GPU-kernel spatial optimizations, especially swizzling patterns on disaggregated architectures. The workshop summary reports that it can reproduce in under 5 minutes the optimal GEMM swizzle that took expert engineers two weeks to find, and on 10 ML/science kernels it reaches up to 2.06x speedup with 70% higher L2 hit rate.
- Relevance assessment: Very high. One of the cleanest direct examples of LLM-driven GPU kernel performance engineering.

## ASAP: an Agentic Solution to Auto-optimize Performance of Large-Scale LLM Training

- Authors: Yuran Ding, Xinwei Chen, Xiaofan Zhang, Zongwei Zhou
- Venue: ML for Systems Workshop at NeurIPS
- Year: 2025
- URL: https://mlforsystems.org/assets/papers/neurips2025/paper22.pdf
- Summary: ASAP is a multi-agent system with Coordinator, Analyzer, and Proposal agents that combines LLM reasoning, profiler traces, roofline analysis, and a knowledge base of expert optimizations to diagnose bottlenecks and recommend improved sharding configurations for distributed LLM training. The workshop summary reports up to 28% step-time reduction and 1.43x throughput improvement, rising to 2.58x when paired with additional expert optimization.
- Relevance assessment: Very high. This is a direct agentic accelerator-training optimization system centered on configuration and sharding decisions.

## KernelEvolve: Scaling Agentic Kernel Coding for Heterogeneous AI Accelerators at Meta

- Authors: Gang Liao, Hongsen Qin, Ying Wang, Alicia Golden, Michael Kuchnik, Yavuz Yetim, Jia Jiunn Ang, Chunli Fu, Yihan He, Samuel Hsia, Zewei Jiang, Dianshi Li, Uladzimir Pashkevich, Varna Puvvada, Feng Shi, Matt Steiner, Ruichao Xiao, Nathan Yan, Xiayu Yu, Zhou Fang, Roman Levenstein, Kunming Ho, Haishan Zhu, Alec Hammond, Richard Li, et al.
- Venue: arXiv preprint; Meta
- Year: 2025
- URL: https://arxiv.org/abs/2512.23236
- Summary: Meta's KernelEvolve frames GPU/accelerator kernel optimization as an agentic coding loop for heterogeneous AI accelerators. The arXiv metadata classifies it under AI, multi-agent systems, architecture, and performance. The earlier indexed description emphasizes substantial gains over human-expert baselines and a much shorter kernel-development cycle.
- Relevance assessment: Very high. This is one of the strongest company-backed examples of agentic low-level accelerator optimization.

## AKG kernel Agent: A Multi-Agent Framework for Cross-Platform Kernel Synthesis

- Authors: Jinye Du, Quan Yuan, Zuyao Zhang, Yanzhi Yi, Jiahui Hu, Wangyi Chen, Yiyang Zhu, Qishui Zheng, Wenxiang Zou, Xiangyu Chang, Zuohe Zheng, Zichun Ye, Chao Liu, Shanni Li, Renwei Zhang, Yiping Deng, Xinwei Hu, Xuefeng Jin, Jie Zhao
- Venue: arXiv preprint
- Year: 2025
- URL: https://arxiv.org/abs/2512.23424
- Summary: AKG kernel Agent is a multi-agent system for kernel generation, migration, and tuning across multiple DSLs including Triton, TileLang, C++, and CUDA-C. It targets multiple hardware backends and reports an average 1.46x speedup over PyTorch eager baselines on KernelBench across GPU and NPU backends.
- Relevance assessment: Very high. Directly addresses agentic kernel synthesis and cross-platform accelerator tuning at the code-generation layer.

## AlphaEvolve: A coding agent for scientific and algorithmic discovery

- Authors: Google DeepMind
- Venue: Google DeepMind technical report / blog
- Year: 2025
- URL: https://storage.googleapis.com/deepmind-media/DeepMind.com/Blog/alphaevolve-a-coding-agent-for-scientific-and-algorithmic-discovery/AlphaEvolve.pdf
- Blog: https://deepmind.google/discover/blog/alphaevolve-a-gemini-powered-coding-agent-for-designing-advanced-algorithms/
- Summary: AlphaEvolve is not GPU-specific, but it is highly relevant to accelerator systems. Google DeepMind reports that it improved data-center scheduling enough to recover about 0.7% of worldwide Google compute, and also found a TPU matrix-multiplication kernel optimization that sped up a critical Gemini-training kernel by 23% and shortened overall training time by 1%.
- Relevance assessment: Very high for TPU/accelerator optimization. This is the strongest Google DeepMind example of an agentic optimizer materially improving accelerator-system efficiency.

## 3. GPU resource management / configuration / runtime tuning

## Learning to Shard: RL for Co-optimizing the Parallelism Degrees and Per-operator Sharding Dimensions in Distributed LLM Inference

- Authors: Ruokai Yin, Sattwik Deb Mishra, Xuan Zuo, Hokchhay Tann, Preyas Shah, Apala Guha
- Venue: ML for Systems Workshop at NeurIPS
- Year: 2025
- URL: https://mlforsystems.org/assets/papers/neurips2025/paper13.pdf
- Summary: Learn to Shard is not an LLM-agent paper, but it is a very relevant RL-based system for LLM inference resource management. It co-optimizes coarse-grained parallelism degrees and fine-grained operator sharding for large-scale inference, and the workshop summary reports up to 3.5x throughput improvement over metaheuristic baselines and 1.06x over Megatron heuristics on H100 clusters with MoE models up to 1.6T parameters.
- Relevance assessment: High. Strongest hit in this pass for GPU/NPU resource configuration and sharding; adjacent rather than fully agentic.

## AGFT: An Adaptive GPU Frequency Tuner for Real-Time LLM Inference Optimization

- Authors: Zicong Ye, Kunming Zhang, Guoming Tang
- Venue: arXiv preprint
- Year: 2025
- URL: https://arxiv.org/abs/2508.01744
- Summary: AGFT uses online reinforcement learning to tune GPU frequency for real-time LLM inference. It monitors request load and latency, prunes the action space, and autonomously learns DVFS decisions. The abstract reports 44.3% GPU energy savings with under 10% latency overhead and up to 40.3% Energy-Delay Product improvement.
- Relevance assessment: Very high for driver/runtime tuning. It is not an LLM agent, but it is a direct autonomous runtime-level GPU optimization system.

## Leveraging Large Language Models to Enhance Machine-Learning-Driven HPC Job Scheduling

- Authors: Kshitij Bhardwaj, Torrey Wagner, Edgar A. Leon
- Venue: ML for Systems Workshop at NeurIPS
- Year: 2025
- URL: https://mlforsystems.org/assets/papers/neurips2025/paper24.pdf
- Summary: This paper uses LLM embeddings, specifically SBERT variants, to improve ML-driven runtime prediction for Slurm job scheduling. On a 90,000-record, 169-feature dataset it reports an `r2` up to 0.88, about 2.3x better than label-encoding baselines.
- Relevance assessment: Medium-high. Not agentic and not GPU-only, but directly relevant to cluster scheduling for accelerator-heavy HPC environments.

## 4. Agentic OS / scheduler papers relevant to GPU systems and `sched_ext`

## Towards Agentic OS: An LLM Agent Framework for Linux Schedulers

- Authors: Yusheng Zheng, YanPeng Hu, Wei Zhang, Andi Quinn
- Venue: ML for Systems Workshop at NeurIPS
- Year: 2025
- URL: https://mlforsystems.org/assets/papers/neurips2025/paper32.pdf
- Summary: This is the strongest direct `sched_ext` / eBPF + AI-agent hit from the search. The paper introduces SchedCP, an MCP-based framework whose `sched-agent` multi-agent system analyzes workloads, synthesizes custom eBPF scheduling policies, and deploys them via `sched_ext`. The workshop summary reports up to 1.79x performance improvement and 13x cost reduction versus naive agentic baselines.
- Relevance assessment: Very high. Exact hit for the "sched_ext or BPF + AI agents" request, and highly relevant to future GPU-aware OS scheduling.

## LDOS: Toward A Learning-Directed Operating System

- Authors: Andrew C. Chen, Adam Dziedzic, Tongxin Li, Jiaye Teng, Noel Lutske, et al.
- Venue: ACM SIGOPS Blog
- Year: 2026
- URL: https://www.sigops.org/2026/ldos-toward-a-learning-directed-operating-system/
- Summary: LDOS combines LLM-generated scheduling modules with an RL controller that chooses the best module online. It is an OS-level analogue to agentic GPU runtime tuning, where generated control policies are selected based on observed workload behavior.
- Relevance assessment: High. Not explicitly GPU-specific, but very close in mechanism to agentic GPU runtime control.

## 5. Broader systems-position pieces and adjacent systems work

## Sustainable Control of Geo-Distributed Datacenters by Distilling Numerical Experts into Adaptive LLM Agents

- Authors: Antonio Guillen-Perez, Ashwin Ramesh Babu, Sahand Ghorbanpour, Avisek Naug, Vineet Gundecha, Sifat Muhammad Abdullah, Ricardo Luna Gutierrez, Soumyendu Sarkar
- Venue: ML for Systems Workshop at NeurIPS
- Year: 2025
- URL: https://mlforsystems.org/assets/papers/neurips2025/paper8.pdf
- Summary: Distills a numerical expert policy into an adaptive LLM agent for carbon-aware workload orchestration in geo-distributed datacenters. The workshop summary emphasizes better scalability, better adaptability to operator commands, and more transparent control than the original RL/MPC experts.
- Relevance assessment: Medium-high. Not GPU-specific, but squarely in the "agentic systems control" space and relevant for accelerator-cluster operations.

## Automated Multi-Agent Workflows for RTL Design

- Authors: Amulya Bhattaram, Janani Ramamoorthy, Ranit Gupta, Diana Marculescu, Dimitrios Stamoulis
- Venue: ML for Systems Workshop at NeurIPS
- Year: 2025
- URL: https://mlforsystems.org/assets/papers/neurips2025/paper64.pdf
- Summary: Presents VeriMaAS, a multi-agent framework for automatically composing workflows for RTL code generation and optimization, with formal verification feedback in the loop. The workshop summary reports 5-7% pass@k improvement over fine-tuned baselines with much lower supervision cost.
- Relevance assessment: Medium. This is hardware-design automation rather than GPU runtime optimization, but it is useful evidence that agentic workflows are moving into lower-level systems and hardware stacks.

## 6. Company-specific findings

## Google DeepMind

- Strong hit: AlphaEvolve
- Why it matters: Demonstrates that a coding agent can optimize both data-center scheduling and TPU kernels with real production-level gains.
- Assessment: Very high relevance to TPU / accelerator optimization, and a strong precedent for analogous GPU systems work.

## Meta

- Strong hit: KernelEvolve
- Why it matters: Direct evidence that Meta is using agentic methods for low-level heterogeneous accelerator kernel coding.
- Assessment: Very high relevance to GPU / accelerator kernel optimization.

## NVIDIA

- Result in this search pass: I did not find a clearly NVIDIA-authored primary-source paper or blog post that cleanly matched "AI agents for GPU systems optimization" at the same level of specificity as AlphaEvolve or KernelEvolve.
- Closest nearby area: workshop papers such as SwizzlePerf are strongly GPU-kernel-focused, but the primary sources I checked did not clearly establish NVIDIA authorship.
- Assessment: Negative finding for now. Worth a second pass focused specifically on NVIDIA Research, developer blogs, and GTC content if you want a vendor-specific subsection.

## 7. Gaps and takeaways

## What I found strongest

- SIGOPS/ACM now has an explicit "systems intelligence" thread: `Barbarians at The Gate`, `Defining System Intelligence`, `Glia`, `LDOS`, and `Let the Barbarians In`.
- The clearest direct GPU/accelerator-agent papers in this pass are `SwizzlePerf`, `ASAP`, `KernelEvolve`, `AKG kernel Agent`, and `AlphaEvolve`.
- The strongest `sched_ext` / eBPF + AI-agent paper is `Towards Agentic OS`.

## What I did not find strongly substantiated

- A well-established LLM-agent paper specifically for GPU memory eviction or prefetch control.
- A well-established LLM-agent paper explicitly centered on GPU oversubscription policy.
- A clean NVIDIA-authored primary-source equivalent to DeepMind's `AlphaEvolve` or Meta's `KernelEvolve`.

## Practical reading order for a resubmission / related-work section

1. `Barbarians at The Gate` for the high-level framing.
2. `Defining System Intelligence` and `Glia` for systems-specific AI-agent vocabulary.
3. `SwizzlePerf`, `KernelEvolve`, `AKG kernel Agent`, and `AlphaEvolve` for direct accelerator optimization evidence.
4. `ASAP`, `Learning to Shard`, and `AGFT` for training/inference configuration and runtime tuning.
5. `Towards Agentic OS` and `LDOS` for scheduler / OS control-plane parallels.
