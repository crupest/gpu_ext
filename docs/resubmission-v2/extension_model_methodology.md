# Extension Execution Models in Systems Papers (SOSP/OSDI)

## What reviewers want to see

An extension execution model must answer four questions clearly:

1. **Where does extension code run?** (execution context)
2. **When does it run?** (trigger / attachment point)
3. **What can it do?** (capabilities and effects)
4. **What can't it do?** (safety boundary)

The best systems papers present these as a single coherent model, not a feature list.

## Successful models from prior work

### sched_ext: struct_ops callbacks
- **Where**: in the kernel scheduler, replacing specific scheduling decisions
- **When**: at well-defined scheduling events (enqueue, dispatch, runnable, stopping)
- **What**: return scheduling decisions (which CPU, which queue, timeslice)
- **Safety**: BPF verifier guarantees termination and memory safety; fallback to default scheduler on error
- **Model in one sentence**: "User-supplied BPF programs replace scheduling policy decisions at defined hook points, with automatic fallback."

### XDP: packet processing
- **Where**: at the network driver level, before the kernel networking stack
- **When**: on every incoming packet
- **What**: return an action (drop, pass, redirect, tx) + optionally modify packet
- **Safety**: BPF verifier; bounded execution time; no sleeping
- **Model in one sentence**: "A BPF program runs on every packet at the driver level and returns an action."

### XRP: storage
- **Where**: in the NVMe driver, on the I/O completion path
- **When**: on I/O completion, chaining subsequent I/Os without returning to user-space
- **What**: issue follow-up I/O requests (B-tree traversal, log-structured merge)
- **Safety**: BPF verifier; limited to I/O operations
- **Model in one sentence**: "BPF programs chain storage I/Os at the driver level without returning to user-space between operations."

### Bento: file system in user-space with kernel safety
- **Where**: kernel VFS layer, replacing file system operations
- **When**: on VFS calls (read, write, lookup)
- **What**: implement full file system logic in Rust with kernel safety
- **Safety**: Rust type system + kernel API restrictions
- **Model in one sentence**: "Safe user-written file system logic runs at the VFS layer, replacing kernel file system implementations."

## Common patterns

### The "single attachment point" model
Most successful extension models have ONE primary attachment point:
- sched_ext: the scheduler
- XDP: the packet receive path
- XRP: the I/O completion path

The model is simple: extension code runs at ONE point, receives context, returns a decision. Complexity comes from what the code can express, not from how many places it runs.

### The "action return" model
Extension code returns a discrete action (XDP_DROP, SCX_DSQ_LOCAL, etc.) or modifies shared state (BPF maps). It does not have unbounded side effects. The caller (kernel subsystem) acts on the return value. This makes the safety boundary clear: the extension advises, the kernel acts.

### The "fallback" model
If extension code fails (verifier rejects, runtime error, timeout), the system falls back to default behavior. This is critical for production deployment: the extension is an optimization, not a requirement.

## What makes a model strong at SOSP/OSDI

### Clarity
The model should be explainable in one sentence. If you need a paragraph to explain when/where/how extension code runs, the model is too complex.

### Minimality
The fewest concepts needed to express the model. Every additional hook point, trigger type, or mechanism is complexity that must be justified. "Three composable mechanism layers" is three times harder to review than "one hook point with async continuation."

### Clean safety boundary
Reviewer must immediately understand: what CAN'T the extension do? What happens if it goes wrong? The boundary should be structural (enforced by verifier/type system), not policy-based (enforced by convention).

### Abstraction level
The model should be described at the right abstraction level. Too high: "we make the GPU programmable" (meaningless). Too low: "we use sleepable kfuncs with bpf_wq in the UVM fault handler" (implementation detail). Right level: "BPF programs run at fault time and can initiate async effects that outlive the callback."

### Novelty in the model, not just the target
"We applied the XDP model to GPUs" is weak. "We extended the BPF execution model to support async effects and device-side execution" is novel. The model itself should contribute something new, not just port an existing model to a new subsystem.

## Anti-patterns

### The "mechanism zoo"
Listing many mechanisms (kfuncs, uprobes, struct_ops, bpf_wq, device-side BPF, SIMT verifier) without a unifying model. Reviewer sees complexity, not insight. Each mechanism is a feature, not a contribution.

### The "layer cake"
"Three composable layers, each crossing a boundary." Sounds clean but raises questions: Do they compose? In what order? What happens at the interfaces? Can you use layer 2 without layer 1? The model is actually three separate models pretending to be one.

### The "checklist"
"Our model provides safety, programmability, deployability, and observability." These are properties, not a model. A model is HOW you achieve properties, not a list of properties.

### The "taxonomy as contribution"
"We identify three challenges and address each with a mechanism." This is a design methodology, not an execution model. Reviewers want to understand the runtime behavior, not the design process.

## How to present an extension model

### Structure
1. **One sentence**: what the model IS (where extension code runs, what it can do)
2. **Trigger**: when does it run
3. **Capabilities**: what operations are available to extension code
4. **Safety**: what the verifier/runtime guarantees
5. **Example**: one concrete policy expressed in this model (3-5 lines of pseudocode)

### The acid test
Can a reader who has never seen the system write a simple policy after reading the model description? If yes, the model is clear. If no, it is too abstract or too mechanism-heavy.

---

## gpu_ext model: versions discussed (2026-03-30)

### Prior art: Syrup (SOSP 2021)
- "User-Defined Scheduling Across the Stack"
- One BPF policy framework, multiple hook points across the stack (CFS, network queue, etc.)
- Unified scheduling decisions across multiple subsystems
- Key: multiple triggers + unified policy is NOT novel by itself — Syrup already did this

### Version M1: "Three composable mechanism layers" (current tex, old)
- "Three mechanism layers, each crossing one boundary: sync/async, driver/app, host/device"
- **Anti-pattern: layer cake.** Sounds clean but is actually three separate models pretending to be one.
- Raises unanswered questions: do they compose? In what order? Can you use L2 without L1?
- Rejected.

### Version M2: "sched_ext + async continuation" (discussed 2026-03-30)
- "Advisory callbacks with async continuation in the GPU fault handler"
- **Problem: sounds incremental.** "We added async to sched_ext" is not a SOSP contribution.
- The framing misses the fundamental difference: we manage REMOTE resources, not local.
- Rejected.

### Version M3: "Programmable policy proxy" (discussed 2026-03-30)
- "The driver acts as a policy proxy for the GPU. We make this proxy programmable."
- **Problem: "proxy" implies GPU has no policy.** GPU hardware HAS policies (warp scheduler, timeslice scheduler, TLB management). The driver doesn't "proxy" for a policy-less device — it implements ADDITIONAL policies (eviction ordering, migration scope, preemption decisions) that the hardware doesn't handle.
- Also factually wrong about GPU hardware capabilities.
- Rejected.

### Version M4: "Remote resource management via BPF" (current best candidate)

**Positioning**: Like Syrup and sched_ext, gpu_ext is a unified BPF policy framework with multiple trigger points in the driver. Unlike those systems, gpu_ext manages resources on a physically separate processor: policy decisions in the host driver must cross an interconnect to take effect.

**The model**:
- **Where**: in the GPU driver (primarily the UVM fault handler)
- **When**: triggered by GPU page faults (reactive) or application CUDA API calls (proactive)
- **What**: BPF programs make advisory policy decisions (eviction ordering, prefetch scope, preemption) AND can initiate async effects (DMA migration, GPU preemption) that execute after the callback returns
- **Safety**: BPF verifier guarantees termination and memory safety; fallback to default driver policy on error
- **Model in one sentence**: "Verified BPF programs in the GPU driver make resource management decisions and initiate cross-device effects, with the execution model designed for the latency and opacity of managing a remote processor."

**What's novel vs Syrup/sched_ext** (not "we added async" — that's incremental):

| | Syrup / sched_ext | gpu_ext |
|--|---|---|
| Managed resource | Local (same processor) | Remote (separate processor, across interconnect) |
| Effect latency | μs (context switch) | ms (DMA across interconnect) |
| Observability | Host can inspect managed state | Device state architecturally opaque |
| Callback model | Sync return → done | Sync return → async continuation needed |

The contribution is NOT "we added async to sched_ext." The contribution is: **we extended the BPF policy model from local resource management to remote resource management**, which required solving the temporal and informational gaps inherent in managing a physically separate processor.

**Auxiliary**: device-side BPF with SIMT-aware verifier provides observation of the remote processor's internal state. This is a separate facility, not part of the main driver-centric model. Honestly positioned as auxiliary (instrumentation only, no end-to-end policy gain yet).

**Design insight**: the fault handler is the natural hook point because GPU resource management couples memory (which pages to migrate) and scheduling (which context stalls) through data movement. Unlike CPU where sched_ext and cache_ext can be independent, GPU policy hooks must see both memory and scheduling state simultaneously.

**Answers to the four questions**:
1. Where: GPU driver fault handler (host CPU)
2. When: page fault (reactive) or CUDA API call (proactive)
3. What: advisory decisions + async effects (DMA, preemption) via kfuncs
4. What can't: bounded by BPF verifier; effects mediated by driver; fallback to default policy

**One-sentence summary**: "gpu_ext extends the BPF advisory callback model from local to remote resource management: verified programs in the GPU driver make policy decisions and initiate cross-device effects across the host-GPU interconnect."

### Open questions on M4
- Is "remote resource management" a strong enough framing for SOSP? Or does it sound like distributed systems?
- "Cross-device effects" — is this the right term?
- How much of the device-side BPF story belongs in the intro vs Section 3?
- Syrup comparison: do we need to explicitly compare to Syrup, or is sched_ext sufficient as baseline?

### yunwei37 critiques leading to these versions
- M1 "three layers": "也 merge 了很多混乱的东西" — too many mechanisms without unified model
- M2 "sched_ext + async": "听起来像是 incremental 加了一个啥辅助机制" — sounds like a minor addition
- M3 "programmable proxy": "GPU 没有 hardware policy — 谁告诉你的？" — factually wrong about GPU hardware
- M4 "remote resource management": yunwei37 questioned — "这里面真的解决的是 remote 的问题吗？异构或者 co-located 的说法会不会更好？"

---

## Terminology discussion (2026-03-30)

### "Remote" vs "Heterogeneous" vs "Cross-device" vs "Accelerator" vs "Co-location"

These terms describe different aspects of the same hardware situation:

| Term | What it describes | Level |
|------|------------------|-------|
| Remote | Physical distance + latency | Distance |
| Heterogeneous | Different kinds of processors | Hardware architecture |
| Cross-device | Different physical devices | Physical topology |
| Accelerator | Purpose-built processor managed by host | Role/function |
| Co-location (breaking) | Policy executor ≠ managed resource context | Policy design consequence |

**Remote** — implies network distance. GPU is on the same machine, same board, sometimes same package (Grace Hopper). "Remote" sounds like distributed systems. WRONG for GPU.

**Heterogeneous** — means different kinds of processors. Correct (CPU + GPU = heterogeneous), but too broad. ARM big.LITTLE is also heterogeneous, but has same ISA, shared memory, none of our mismatches.

**Cross-device** — means across different hardware devices. More precise than "remote" (no network implication), more specific than "heterogeneous" (implies physical separation). Neutral, descriptive. Captures "different device" but not "different ISA."

**Accelerator** — specialized processor designed for specific workloads, managed by a host CPU. Naturally includes four properties: (1) separate device, (2) specialized ISA, (3) host-managed, (4) interconnect-connected. More specific than "heterogeneous" (big.LITTLE is NOT an accelerator). Good for generalization (GPU, FPGA, TPU, SmartNIC all are accelerators).

**Co-location (breaking)** — not a hardware description. Describes the CONSEQUENCE for extensibility design: sched_ext assumes policy executor and managed resource are on the same processor. GPU breaks this assumption. This is the insight, not the hardware.

### Relationship between terms

Causal chain: GPU is an **accelerator** (role) → it is a **cross-device** separate processor (topology) → it is **heterogeneous** (different ISA) → policy and resource are **not co-located** (design consequence) → CPU extensibility patterns **break**.

"Cross-device" and "heterogeneous" are hardware facts. "Co-location breaks" is the design consequence. "Accelerator" is the role description that encompasses both facts.

### yunwei37's position
- "Remote" is wrong (GPU is not remote)
- Not sure a single term is needed
- Maybe just describe the fact: "GPU is a separate device with its own ISA. Policy runs on host CPU, effects happen on GPU."
- Co-location is not a "层面 (level)" — it's just a fact: sched_ext assumes same processor, GPU isn't.
- "Cross-device" is close to the antonym of "co-located" but doesn't fully capture "different ISA" and "different execution model"

### Conclusion
No single term perfectly captures the situation. Best approach: **describe the fact, don't rely on a term.**

> "GPU is a separate device with its own ISA and execution model. Policy runs on the host CPU, effects happen on the GPU. This is unlike CPU extensibility where policy and resource share the same processor."

Use contrast (unlike CPU...) rather than jargon (breaks the co-location assumption). The reader understands immediately without needing to learn a new term.

If a term IS needed for shorthand, **"accelerator"** is the best single word — it naturally implies separate device + specialized ISA + host-managed. "Accelerator extensibility" as a research area is well-scoped and not confusable with distributed systems or big.LITTLE.

---

## What makes our model a contribution (discussion, 2026-03-30)

### What reviewers want (from `what_makes_top_venue_paper.md`)
1. New abstraction that changes thinking
2. Surprising result
3. Previously impossible capability
4. The right solution to a hacked-around problem

### How our model fits
- **New abstraction**: GPU resource management as a programmable OS subsystem (like CPU scheduling became programmable via sched_ext). Not just "applied BPF to GPU" — the model handles challenges that don't exist for CPU extensibility.
- **Previously impossible**: before gpu_ext, you could not safely load custom GPU resource management policy at runtime. Now you can, and agents can explore the policy space.
- **The right solution**: people have been hacking GPU policy in user-space (Paella, XSched) or modifying driver source (GDEV, GCAPS). BPF in the driver is the principled answer.

### What's NOT the contribution
- "We added async to sched_ext" (incremental)
- "Three composable mechanism layers" (feature list)
- "We identify two mismatches" (taxonomy)

### What IS the contribution
The model itself: verified BPF programs in the GPU driver can express resource management policies that were previously hardcoded, with the execution model handling the fact that policy runs on a different processor than the managed resource. This is a new kind of OS extensibility — extending a kernel subsystem that manages a separate, architecturally distinct device.
