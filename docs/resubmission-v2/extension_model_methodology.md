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

**Device-side execution (co-equal contribution)**: device-side BPF with SIMT-aware verifier provides both observation of the remote processor's internal state AND policy execution on the GPU (e.g., work-stealing thread-block scheduler, combined host-device prefetch). Device-side BPF is a headline contribution with e2e evaluated policies, not auxiliary instrumentation.

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

---

## Unified Abstraction Candidates (2026-03-31)

Context: gpu_ext has multiple hook points (struct_ops in fault handler, uprobes on CUDA APIs, device-side BPF, kprobes), effects via kfuncs (sync and async), coordination via BPF maps. sched_ext is NOT part of our model. The question: what is the ONE unifying abstraction that covers memory (eviction, prefetch, placement), scheduling (preemption, priority, work-stealing), AND observability (device monitoring, phase detection)?

Constraint: Syrup (SOSP'21) unified NIC/kernel/thread scheduling under one abstraction: `schedule(input) -> executor` (matching). We need something similarly clean. "All effects through kfuncs" is implementation, not abstraction. "Three layers" is a feature list.

### Candidate A: "Programmable GPU Resource Lifecycle"

**One-liner**: "gpu_ext makes GPU resource lifecycle transitions programmable."

**Idea**: Every GPU resource has a lifecycle:
- Memory chunk: allocated → activated → accessed → evict_prepare → evicted → re-activated
- Compute context: submitted → scheduled → executing → preempted → completed
- Channel: created → active → idle → destroyed

Policy hooks attach at lifecycle transitions. The BPF program at each transition decides what happens: eviction ordering at evict_prepare, prefetch scope at activate, preemption at context_switch. This is exactly what sched_ext does for CPU scheduling (hooks at enqueue, dispatch, running, stopping), extended to ALL GPU resource types.

**How it addresses three challenges**:
- Timescale: some lifecycle transitions cross the device boundary → async kfuncs for transitions that take ms
- Coupling: memory lifecycle and compute lifecycle share transitions (page fault is both a memory event and a scheduling stall) → hooks at shared transitions see both domains
- Information: device-side lifecycle transitions (warp execution, SM scheduling) are invisible from host → device-side BPF exposes them

**Strengths**: Natural extension of sched_ext pattern. "Lifecycle" is concrete and familiar. Each hook has a clear trigger (a lifecycle transition). Covers memory + scheduling + observability.
**Weaknesses**: "Lifecycle" may feel too generic. Not clear what makes GPU resource lifecycles fundamentally different from CPU ones (beyond crossing device boundary). Risk of sounding like "we applied the sched_ext pattern to more stuff."

### Candidate B: "Cross-Boundary Policy Composition"

**One-liner**: "A gpu_ext policy is a set of BPF program fragments at different boundary-crossing points, composed into a coherent policy through shared state."

**Idea**: The key insight is that GPU resource management inherently crosses boundaries: host-device boundary (data movement), driver-application boundary (application intent), memory-scheduling boundary (coupling). No single hook point can see everything. A policy is therefore COMPOSED of fragments:
- A fragment at the fault handler (sees memory + scheduling state)
- A fragment at the uprobe (sees application intent)
- A fragment on the device (sees GPU internal state)
- All fragments share state through BPF maps

The composition model is the abstraction. How fragments coordinate (maps), how effects propagate (kfuncs), how safety is ensured across fragments (verifier per side) — this is what the design section explains.

**How it addresses three challenges**:
- Timescale: fragments at different timescales (sync fault handler + async bpf_wq) compose into one policy
- Coupling: fragments at the memory-scheduling intersection (fault handler) naturally see both domains
- Information: fragments on the device side feed observations into shared maps

**Strengths**: Naturally explains why the system has multiple hooks (not a zoo — it's composition). Novel: no prior BPF system composes across a device boundary. Syrup composes across layers on one host; we compose across host and device.
**Weaknesses**: "Composition" is abstract. Hard to give a one-sentence explanation a reviewer can immediately grasp. Risk of sounding like distributed systems.

### Candidate C: "Advisory Decisions with Deferred Cross-Device Effects"

**One-liner**: "BPF programs make advisory decisions at GPU management events and defer cross-device effects through async kfuncs."

**Idea**: The fundamental difference from sched_ext/Syrup: effects cross a device boundary and therefore can't be immediate. The model is:
1. Event occurs (fault, API call, device event)
2. BPF program makes an advisory decision (sync, fast — eviction ordering, prefetch hint)
3. BPF program optionally defers an effect (async kfunc → DMA migration, GPU preemption)
4. Deferred effects execute asynchronously, results visible in maps for future decisions

The "advisory + deferred" pattern is the unifying abstraction. Every hook follows it. The novelty is the "deferred" part — effects that cross the interconnect, take ms, and must be tracked.

**How it addresses three challenges**:
- Timescale: deferred effects handle the ms gap (the core of the model)
- Coupling: advisory decisions at the fault handler see both memory and scheduling state; deferred effects in one domain trigger events in another
- Information: device-side BPF programs are deferred observers — they feed information back to the host via maps, enabling future advisory decisions to be better informed

**Strengths**: Sharp contrast with sched_ext (immediate vs deferred). Concrete: every hook is "advisory + optional deferred effect." Easy to explain.
**Weaknesses**: "Advisory + deferred" might sound like an implementation pattern, not an abstraction. Doesn't clearly cover pure observability (device-side BPF is not really "advisory + deferred"). Timescale-centric — coupling and information feel secondary.

### Candidate D: "Programmable Event-Driven Orchestration Across the Device Boundary"

**One-liner**: "gpu_ext policies are event-driven BPF programs that orchestrate GPU resource management across the host-device boundary through events, shared state, and cross-device effects."

**Idea**: Like a reactive/event-driven system. Events (faults, API calls, device execution) trigger BPF programs. Programs read/write shared state (maps), produce effects (kfuncs). The event-driven model naturally spans multiple hook points, multiple timescales, and both host and device. The orchestration is the policy.

**How it addresses three challenges**:
- Timescale: events are async by nature; effects via kfuncs can be deferred
- Coupling: shared state (maps) connects events across memory and scheduling domains
- Information: device-side events feed into the shared state

**Strengths**: "Event-driven" is well-understood. Naturally accommodates multiple hooks without feeling like a zoo. Generalizable.
**Weaknesses**: Too generic — every BPF system is "event-driven programs with maps." Not clear what's novel. "Orchestration" is vague.

### Candidate E: "Programmable Arbitration of Shared Device Resources"

**One-liner**: "gpu_ext policies arbitrate access to constrained device resources (memory capacity, bandwidth, compute time) across competing workloads."

**Idea**: Device memory, bandwidth, and compute time are shared, constrained resources. Every policy decision is arbitration: who gets device memory (eviction priority), who gets bandwidth (prefetch scope), who gets compute time (preemption, scheduling). The arbitration runs on the host but governs the device.

**How it addresses three challenges**:
- Timescale: arbitration decisions must be enacted across the interconnect → async
- Coupling: memory capacity and compute time are the SAME constrained pool from the device's perspective (memory for data = memory not available for other workloads' data = scheduling impact)
- Information: better arbitration requires knowing device state → device-side BPF

**Strengths**: "Arbitration" captures the multi-tenant, resource-constrained nature of GPU management. Explains coupling naturally (resources are shared). More specific than "event-driven."
**Weaknesses**: "Arbitration" may sound narrow — covers multi-tenant well but not single-tenant optimization (prefetch policy for one workload isn't really "arbitration"). Doesn't capture observability well.

### Candidate F: "Policy State Machine Spanning Host and Device"

**One-liner**: "A gpu_ext policy defines a state machine over GPU resource states, with transitions triggered by events on both host and device."

**Idea**: Policy is a state machine. States represent resource conditions (chunk: resident/evicted/migrating; workload: active/stalled/preempted; system: memory-pressure/balanced). Transitions are triggered by events (faults, API calls, device metrics). BPF programs at each transition define the policy logic. Maps store the state machine's state. The state machine spans host and device — some transitions happen on host (fault handler), some on device (execution events), connected by cross-layer maps.

**How it addresses three challenges**:
- Timescale: transitions that cross the device boundary are async (migrating state takes ms)
- Coupling: memory states and scheduling states are part of the same state machine → naturally coupled
- Information: device-side states are only observable via device-side hooks → the state machine must extend to the device

**Strengths**: "State machine" is formal and precise. Naturally explains why you need hooks at multiple points (transitions happen at different places). The state machine IS the policy — not a list of hooks.
**Weaknesses**: May feel over-formal for a systems paper. State machine implies exhaustive state enumeration, which is hard for complex policies. Risk: reviewers say "so it's a fancy FSM framework?"

### Candidate G: "Reactive Dataflow Across Host-Device Boundary"

**One-liner**: "A gpu_ext policy is a dataflow graph of BPF programs connected by maps, spanning host and device."

**Idea**: Policy as a directed graph. Nodes are BPF programs at different hooks. Edges are BPF maps carrying state between hooks. Events trigger nodes. Outputs flow through maps to downstream nodes. Some nodes are on host (fault handler, uprobes), some on device (device-side BPF). The graph naturally spans the boundary.

Example: device-side BPF (node 1) observes SM utilization → writes to map → fault handler BPF (node 2) reads utilization → decides eviction priority → writes to map → bpf_wq BPF (node 3) reads decision → executes async DMA.

**How it addresses three challenges**:
- Timescale: async edges in the graph (bpf_wq nodes execute after sync nodes return)
- Coupling: memory and scheduling nodes connected in the same graph → naturally coordinated
- Information: device-side nodes feed data into the graph

**Strengths**: Visual, intuitive. Easy to draw architecture diagrams. Naturally accommodates composition.
**Weaknesses**: "Dataflow" is a PL/compiler term that may confuse systems reviewers. Overhead of the abstraction may seem unjustified for what is basically "hooks + maps." Risk: reviewers say "you just wired BPF programs together with maps, every BPF program already does this."

### Summary Comparison

| Candidate | One-liner | Novelty | Covers all 3 domains? | Clean? | SOSP risk |
|-----------|-----------|---------|----------------------|--------|-----------|
| A: Resource lifecycle | Programmable lifecycle transitions | Extension of sched_ext | Yes (each resource type has lifecycle) | High | "Just sched_ext for more stuff" |
| B: Cross-boundary composition | Fragments at boundary points, composed via maps | Composition across device boundary | Yes | Medium | "Too abstract" |
| C: Advisory + deferred | Advisory decisions + deferred cross-device effects | Deferred effects are novel | Weak on observability | High | "Implementation pattern, not abstraction" |
| D: Event-driven orchestration | Event-driven BPF across host-device | Generic | Yes | Medium | "Every BPF system is event-driven" |
| E: Resource arbitration | Arbitrate constrained device resources | Arbitration framing | Weak on single-tenant, observability | Medium | "Too narrow" |
| F: State machine | Policy as state machine spanning host/device | Formal model | Yes | Medium | "Over-formal, fancy FSM" |
| G: Reactive dataflow | Dataflow graph of BPF programs across boundary | Visual, composable | Yes | Medium | "Just hooks + maps" |

### Open questions for review
1. Is there a candidate that is clearly stronger?
2. Can candidates be combined? (e.g., A's concreteness + B's composition story)
3. Are we missing an obvious framing?
4. Does the abstraction need to be as clean as Syrup's `schedule()`, or is gpu_ext inherently more complex (managing multiple resource types, not just scheduling)?
5. Is the real novelty in the abstraction itself, or in the CROSSING (host-device boundary) regardless of abstraction?

---

## Current Best Candidate: Effect-Based Cross-Device Extensibility (2026-03-31, revised)

### One-sentence model

**BPF programs respond to GPU management events and produce all effects exclusively through kfuncs.**

(Expanded: gpu_ext policies are triggered by events across the GPU management stack — driver lifecycle transitions, application API calls, device execution events — and produce all effects through a shared kfunc API, coordinating via BPF maps that span the host-device boundary.)

### Key design principle: all effects via kfuncs, no return-value decisions

In the gpu_ext model, **every policy effect is a kfunc call**. There are no return-value decisions. This is a deliberate departure from the sched_ext/XDP pattern and is the core of the unified abstraction.

Implementation note: the actual implementation uses struct_ops with return values for some hooks (evict_prepare returns an ordering value) as a convenience. But conceptually, this is equivalent to calling `bpf_kfunc_set_eviction_priority()` — the return value is a shorthand for a kfunc call. The paper presents the pure kfunc model; struct_ops is an implementation detail.

### Why this matters: the unified hook shape

With all effects via kfuncs, **every hook has the same shape**, regardless of layer or trigger type:

```
event → BPF program → kfunc calls + map operations
```

No exceptions. No "some hooks are decision points, some are event triggers." ALL hooks are event-triggered effect injectors.

```c
// Driver hook: eviction — effect via kfunc, not return value
SEC("fentry/uvm_evict_prepare")
int on_evict(struct gpu_chunk *chunk) {
    bpf_kfunc_set_eviction_priority(chunk, compute_priority(chunk));
    bpf_kfunc_prefetch_range_async(next_va, CHUNK_SIZE);
    return 0;
}

// Application hook: proactive prefetch — same shape
SEC("uprobe/cudaLaunchKernel")
int on_launch(struct pt_regs *ctx) {
    bpf_kfunc_prefetch_range_async(va, size);
    return 0;
}

// Device hook: observation — same shape
SEC("gpu/thread_block_start")
int on_tb(struct gpu_tb_ctx *ctx) {
    bpf_kfunc_record_access(ctx->va, ctx->sm_id);
    return 0;
}
```

All three: event triggers BPF, BPF calls kfuncs, BPF reads/writes maps. The context differs (chunk state, CUDA args, warp state). The effect interface is identical.

### The execution model in detail

#### 1. Events provide context (OBSERVE)

GPU resources follow well-defined lifecycles managed by the driver:

**Memory chunk lifecycle:**
```
                    fault (reactive)
            ┌──────────────────────────┐
            ▼                          │
allocated ──▶ activated ──▶ accessed ──▶ evicted
            ▲                              │
            └──────────────────────────────┘
                   prefetch (proactive)
```
Note: the evicted → activated transition can be triggered reactively (by a page fault) or proactively (by a prefetch kfunc). Both are valid lifecycle transitions — prefetch does not create a new transition, it triggers an existing one proactively.

**Compute context lifecycle:**
```
submitted ──▶ scheduled ──▶ running ──┐
    ▲                                 │
    └──── preempted ◀─────────────────┘
```

BPF programs are triggered by events across three layers. Not all events are lifecycle decision points — some are notifications, some are application-level triggers, some are device-side observations. The common element is: each event provides context that the BPF program uses to decide what kfuncs to call.

- **Driver layer** (struct_ops / fentry): fault handler events — evict_prepare, chunk_activate, prefetch. These include lifecycle decision points AND event notifications.
- **Application layer** (uprobes): CUDA API calls — cudaLaunchKernel, cudaStreamSynchronize. These are proactive triggers: not lifecycle decisions, but events where BPF injects effects (e.g., prefetch before a kernel launch).
- **Device layer** (device-side BPF): GPU execution events — thread_block_start, warp_exec, should_try_steal. These provide device-internal context AND enable device-local policy execution.
- **Deferred execution** (bpf_wq): async continuations from earlier hooks. Not triggered by an external event, but by a previously scheduled work item.

Each layer provides context that the others cannot:

| Layer | What it sees | Example |
|-------|-------------|---------|
| Driver | Memory state + scheduling state at the fault handler | Which chunks are resident, fault counts, process ownership |
| Application | Application-level intent and phase | Which model layer is executing, whether this is prefill or decode |
| Device | GPU-internal execution state | Per-SM utilization, warp-level access patterns, compute phases |

#### 2. kfuncs deliver ALL effects (ACT) — the unified abstraction

**The fundamental model difference from prior BPF extensibility:**

In sched_ext/XDP, the primary effect mechanism is the **return value**: the BPF program returns a decision and the kernel acts on it.

```c
// sched_ext: decision via return
int select_cpu(struct task_struct *p, ...) { return cpu_id; }

// XDP: decision via return
int xdp_prog(struct xdp_md *ctx) { return XDP_DROP; }
```

(Note: sched_ext also uses helper calls like `scx_bpf_dispatch()` in `enqueue()` and `dispatch()`, so it is not purely return-value. But the return-value decision is the primary pattern and the one presented in the model.)

In gpu_ext, **ALL effects are kfunc calls**. There is no return-value decision path. Every hook is a pure effect injector:

```c
// gpu_ext: ALL effects via kfuncs — no meaningful return value
int on_evict(struct gpu_chunk *chunk) {
    bpf_kfunc_set_eviction_priority(chunk, priority);  // effect: ordering
    bpf_kfunc_prefetch_range_async(va, size);           // effect: async DMA
    bpf_map_update_elem(&state, &k, &v, 0);            // effect: state sharing
    return 0;  // no decision here — all effects already delivered
}
```

**Why this is necessary (not just a design choice):**

1. GPU policy requires **multiple effects per hook** — setting priority, triggering async prefetch, AND updating state, all in one invocation. A single return value cannot express this.
2. GPU effects **cross the device boundary** — DMA takes ms, must be async. Return values are synchronous by definition.
3. GPU policies **span multiple hooks** — effects in one hook (uprobe writes phase to map) enable effects in another (fault handler reads phase, adjusts prefetch). The kfunc + map pattern makes this composition natural.

| | sched_ext / XDP / Syrup | gpu_ext |
|--|------------------------|---------|
| Primary effect mechanism | **Return value** | **kfunc calls** |
| Effects per invocation | Typically 1 | Multiple |
| Effect timing | Sync, immediate | Sync + async |
| Effect scope | Local processor | Cross-device |
| Hook shape | Decision point (kernel asks, BPF answers) | **Effect injection (event triggers, BPF acts)** |
| Unified element | Decision format (`schedule() → executor`) | **Effect API (kfunc set)** |

**The kfunc API is gpu_ext's unifying abstraction — analogous to Syrup's `schedule()`.** Syrup unifies the DECISION FORMAT: every layer does matching with the same function signature. gpu_ext unifies the EFFECT INTERFACE: every hook — regardless of layer, trigger type, or execution environment — produces effects through the same kfunc API and shares state through the same maps.

This is a different kind of unification than Syrup's, and it is the RIGHT kind for cross-device extensibility: you CANNOT have a unified function signature when hooks span host CPU interrupt context, user process context, and GPU SIMT execution. These are fundamentally different execution environments. But you CAN have a unified effect interface — the kfunc API is callable from all environments, and maps are accessible from all environments.

Effects have different timescales:

- **Sync effects**: complete immediately. Examples: set eviction priority, set prefetch scope.
- **Async effects**: initiate cross-device operations that complete later (via bpf_wq + sleepable kfuncs). Examples: DMA page migration, GPU context preemption, cross-block prefetch.
- **Observation effects**: write metrics/state to BPF maps, feeding information to future hook invocations on either host or device.

The kfunc API is the policy's capability surface — it defines exactly what a policy can and cannot do.

#### 3. BPF maps coordinate across hooks (CONNECT)

BPF maps are shared key-value stores accessible from all hooks. They serve three roles:

- **Cross-hook coordination**: an uprobe writes application phase info; the fault handler reads it to adjust prefetch strategy.
- **Cross-time coordination**: a fault handler records access patterns; a later bpf_wq task uses them to decide what to prefetch.
- **Cross-device coordination**: device-side BPF writes SM utilization; host-side fault handler reads it to make eviction decisions. (Cross-layer maps with snapshot-based consistency.)

#### 4. The complete picture

```
 CONTEXT (lifecycle events)          POLICY              EFFECTS (kfuncs)

 ┌─────────────────────┐                            ┌──────────────────┐
 │ Application Layer   │        ┌──────────┐        │                  │
 │  • cudaLaunchKernel │───────▶│          │        │  Sync            │
 │  • cudaSync         │        │          │──────▶ │  • set evict     │
 │    (uprobes)        │  ┌────▶│   BPF    │        │    ordering      │
 ├─────────────────────┤  │     │ Programs │        │  • set prefetch  │
 │ Driver Layer        │  │     │          │──────▶ │    scope         │
 │  • evict_prepare    │──┘     │          │        │                  │
 │  • chunk_activate   │───────▶│          │        │  Async           │
 │  • prefetch         │        └─┬──────┬─┘        │  • migrate pages │
 │    (struct_ops)     │          │      │    ┌───▶ │  • preempt ctx   │
 │                     │          │ Maps │    │     │  • prefetch DMA  │
 ╞═════════════════════╡          │      │    │     │    (bpf_wq +     │
 │    PCIe / NVLink    │        ┌─▼──────▼─┐  │     │     kfuncs)      │
 ╞═════════════════════╡        │          │──┘     │                  │
 │ Device Layer        │        │  Shared  │        │  Observation     │
 │  • thread_block     │───────▶│  State   │        │  • write metrics │
 │  • warp_exec        │        │          │        │    to maps       │
 │    (device-side BPF)│        └──────────┘        └──────────────────┘
 └─────────────────────┘                                     │
                                                             ▼
                                                        GPU Resources
                                                     (memory, compute)
```

### Safety model: lifecycle IS the safety boundary

The execution model and the safety model are unified through one principle: **policies can only influence lifecycle transitions — their ordering, timing, and selection — but cannot create transitions outside the lifecycle or violate resource invariants.**

**What policies CAN do (influence transitions):**
- Change eviction **ordering**: evict chunk A before chunk B
- Change prefetch **timing**: prefetch now vs. wait for fault
- Change preemption **selection**: preempt workload X to make room for Y
- **Observe** lifecycle state: read fault counts, access patterns, residency

**What policies CANNOT do (outside lifecycle):**
- Create or destroy resources (allocate GPU memory, free pages, create channels)
- Skip lifecycle states (move a chunk from allocated directly to evicted, bypassing activation)
- Violate invariants (evict a pinned chunk, preempt a non-existent context, corrupt page tables)
- Directly access hardware (write GPU registers, modify page tables, send raw DMA commands)

**Why this holds structurally (not just by runtime checks):**

1. **Lifecycle defines the capability surface**: the driver defines which lifecycle transitions exist and exposes each as a kfunc. The BPF program can only call kfuncs — therefore it can only trigger valid lifecycle operations. There is no kfunc for "corrupt page table" or "skip migration."

2. **Verifier ensures program safety**: the BPF verifier (host-side) and SIMT-aware verifier (device-side) guarantee at load time that programs terminate, stay within memory bounds, only call valid kfuncs with valid arguments, and (on device) maintain warp uniformity.

3. **Driver mediates every effect**: each kfunc implementation validates parameters and enforces invariants before executing. The policy cannot bypass the driver — there is no raw hardware access path.

4. **Automatic fallback**: if a policy program returns an error or is unloaded, the driver reverts to its default lifecycle behavior. The worst case is no policy, not a broken system.

**The principle**: the lifecycle state machine is defined and enforced by the driver. Policies influence which edges are taken (ordering, timing, selection of transitions) but cannot add new states or invalid edges. This is structural safety — it holds by construction, not by testing.

This unification means: the same concept (lifecycle) explains both what the system does (execution model: observe lifecycle events, produce lifecycle effects) and why it's safe (safety model: effects are valid lifecycle operations). Agent-generated policies get the same guarantee — they can explore arbitrarily within the lifecycle surface, but cannot escape it.

**Scope of safety guarantee — correctness, not performance isolation:**

Lifecycle safety guarantees correctness: no invalid state transitions, no kernel crash, no data corruption. It does NOT guarantee performance isolation — a policy can flood async prefetch requests and saturate PCIe, just as a sched_ext policy can starve processes or an XDP program can drop all packets. These are structurally valid but performantly bad decisions. The remedy is the same across all BPF extensibility systems: unload the policy, fallback to default. Driver-level rate limiting on async kfuncs can further bound resource consumption, but this is an implementation safeguard, not a model property.

**Cross-device map consistency model:**

Cross-layer maps (host ↔ device) provide eventual consistency with periodic snapshots: device-side writes are batched and synced to host at configurable intervals. Host-side reads may see stale values. Stale information can lead to suboptimal policy decisions but cannot violate lifecycle invariants — because the driver validates every kfunc invocation regardless of input data. A prefetch request based on stale SM utilization may prefetch the wrong data (wasteful), but cannot corrupt page tables or crash the system (unsafe).

### How this addresses the three challenges

| Challenge | How lifecycle + effects addresses it |
|-----------|--------------------------------------|
| **Timescale** | Some lifecycle transitions cross the device boundary and take ms (migration, preemption). The model handles this via async kfuncs: the BPF program initiates the transition, the effect completes asynchronously. The lifecycle tracks in-flight transitions. |
| **Coupling** | Memory lifecycle and compute lifecycle share transitions: a page fault is both a memory event (data not resident) and a scheduling event (context stalls). Hooks at shared transitions naturally see both domains. Maps propagate state between memory and scheduling hooks. |
| **Information** | Device-side lifecycle events (warp execution, thread block scheduling) are invisible from the host. Device-side BPF extends both lifecycle observation AND policy execution to the GPU (e.g., work-stealing thread-block scheduler), feeding device state into shared maps and enabling device-local policy decisions. |

### Comparison to prior work

| System | Lifecycle scope | Effect mechanism | Effect scope | Cross-device? |
|--------|----------------|-----------------|--------------|---------------|
| **sched_ext** | CPU scheduling transitions | **Return value** (1 per hook, sync) | Local | No |
| **XDP** | Packet arrival | **Return value** (1 per hook, sync) | Local | No |
| **Syrup** | NIC + kernel + thread (multi-layer) | **Return value** (matching, sync) | Local | No |
| **cache_ext** | Page cache eviction | **Return value** (1 per hook, sync) | Local | No |
| **XRP** | NVMe I/O completion | **I/O resubmission** (chained, stateless) | Local device | No |
| **gpu_ext** | GPU lifecycles (host + app + device) | **kfunc calls** (multiple per hook, sync + async) | **Cross-device** | **Yes** |

**What prior work shares**: lifecycle hooks + verifier + kernel mediates effects. This is the proven BPF extensibility pattern.

**What gpu_ext changes at the model level**:
1. **Effect model: kfunc calls instead of return values.** Prior systems produce one effect per hook via a return value (sync, immediate, local). gpu_ext produces multiple effects per hook via kfunc calls (sync + async, cross-device). This is the fundamental execution model difference — it is necessary because GPU policy requires multiple concurrent effects (eviction + prefetch + state update) that span the device boundary.

**What gpu_ext adds on top**:
2. **Cross-device lifecycle observation**: hooks span host AND device (device-side BPF is genuinely new — no prior system runs verified BPF on a GPU)
3. **Cross-device effects**: async kfuncs that initiate operations on a separate device (no prior BPF system has effects that cross a device boundary)
4. **Cross-device state**: maps shared between host and device BPF programs (cross-layer maps with consistency model)
5. **SIMT-aware verification**: device-side verifier that enforces warp uniformity constraints (new verification domain)

The lifecycle-extensibility pattern is proven for CPU subsystems. gpu_ext shows that extending it across the device boundary requires — and enables — mechanisms that prior work did not address: async effects for cross-device operations, device-side verified programs for opaque-device observation and policy execution, and cross-device shared state for policy coordination. The boundary crossing is not incremental — it forces fundamentally new execution semantics (deferred effects, SIMT verification, cross-device maps) that have no counterpart in host-only BPF systems.

### One-paragraph paper summary

"Linux has made CPU scheduling (sched_ext), networking (XDP), and storage (XRP) extensible via verified BPF programs that hook into resource lifecycle transitions. We extend this model to GPU resource management — a domain where lifecycle events and effects must cross the host-device boundary. gpu_ext attaches BPF programs at GPU resource lifecycle transitions spanning the host driver, application boundary, and GPU device. Policies observe lifecycle events for context and produce effects through kfuncs, with both observation and effects bridging the host-device gap via async execution and cross-device shared state. The lifecycle itself is the safety boundary: policies can influence the ordering, timing, and selection of transitions, but cannot create invalid transitions or violate resource invariants. This model enables agent-generated policies that explore the GPU policy space with guaranteed safety."

### Acid test (from methodology section)

Can a reader who has never seen the system write a simple policy after reading the model description?

**Test**: Write an eviction policy that keeps pages for the most recently active process.

```c
SEC("struct_ops")
int evict_prepare(struct gpu_chunk *chunk) {
    u64 last_active = bpf_kfunc_get_last_access_time(chunk);
    return last_active;  // lower value = evict first
}
```

**Test**: Write a proactive prefetch policy triggered by application kernel launch. (Uprobe can directly call async kfuncs — no map indirection needed for the effect.)

```c
SEC("uprobe/cudaLaunchKernel")
int on_launch(struct pt_regs *ctx) {
    u64 va_start = PT_REGS_PARM1(ctx);  // kernel's data region
    u64 size = PT_REGS_PARM2(ctx);
    bpf_kfunc_prefetch_range_async(va_start, size);  // direct async effect
    return 0;
}
```

Both follow the model: observe lifecycle event → produce effect via kfunc. The reader can write these from the model description alone.

**Test**: Write a composed multi-hook policy: multi-tenant priority eviction with proactive prefetch. (Demonstrates cross-hook coordination via maps, sync + async effects, application + driver layers.)

```c
// === Application layer: detect tenant priority via uprobe ===
SEC("uprobe/cudaLaunchKernel")
int on_launch(struct pt_regs *ctx) {
    u32 pid = bpf_get_current_pid_tgid() >> 32;
    u32 priority = HIGH;
    bpf_map_update_elem(&tenant_priority, &pid, &priority, 0);
    return 0;
}

// === Driver layer: priority-aware eviction (sync effect) ===
SEC("struct_ops")
int evict_prepare(struct gpu_chunk *chunk) {
    u32 pid = bpf_kfunc_get_chunk_owner(chunk);
    u32 *prio = bpf_map_lookup_elem(&tenant_priority, &pid);
    if (prio && *prio == HIGH)
        return KEEP;  // protect high-priority tenant's pages
    return bpf_kfunc_get_last_access_time(chunk);  // LRU for the rest
}

// === Driver layer: proactive cross-block prefetch (async effect) ===
SEC("struct_ops")
int chunk_activate(struct gpu_chunk *chunk) {
    u64 next_va = bpf_kfunc_get_chunk_va(chunk) + CHUNK_SIZE;
    bpf_kfunc_prefetch_range_async(next_va, CHUNK_SIZE);  // async DMA
    return 0;
}
```

This example uses three hooks across two layers (application + driver), both sync effects (eviction ordering) and async effects (cross-block prefetch), connected by a shared map (`tenant_priority`). A reader can write this from the model description.

**Test**: Write a cross-device feedback policy: device-side GPU fault observation feeds into host-side scheduling decisions. (Demonstrates device→map→host feedback loop — the core novelty of spanning the device boundary.)

```c
// === Device layer: observe GPU fault pressure, write to map ===
SEC("struct_ops")
int gpu_block_activate(struct gpu_chunk *chunk) {
    u32 pid = bpf_kfunc_get_chunk_owner(chunk);
    struct gpu_state *state = bpf_map_lookup_elem(&gpu_state_map, &pid);
    if (state)
        __sync_fetch_and_add(&state->fault_count, 1);  // atomic update
    return 0;
}

// === Host driver layer: read device pressure, adjust eviction ===
SEC("struct_ops")
int evict_prepare(struct gpu_chunk *chunk) {
    u32 pid = bpf_kfunc_get_chunk_owner(chunk);
    struct gpu_state *state = bpf_map_lookup_elem(&gpu_state_map, &pid);
    if (state && state->fault_count > THRASHING_THRESHOLD)
        return KEEP;  // protect thrashing workload's pages
    return bpf_kfunc_get_last_access_time(chunk);
}
```

This shows the device→map→host feedback loop: device-side hooks observe GPU fault pressure (via struct_ops on the fault path), write to a shared map, and host-side eviction policy reads that map to make better decisions. The same `gpu_state_map` can also be read by host-side CPU scheduling policies (e.g., throttle best-effort tenants when LC workload is thrashing). This pattern is used in the evaluated xCoord policy.

**Summary of acid test coverage:**

| Test | Layers | Effect type | Maps | Novel feature demonstrated |
|------|--------|-------------|------|---------------------------|
| Simple eviction | Driver | Sync | No | Lifecycle hook + kfunc |
| Proactive prefetch | Application | Async | No | Uprobe → async kfunc (cross-device) |
| Multi-tenant priority | App + Driver | Sync + Async | Yes (cross-hook) | Multi-hook composition |
| **Device feedback** | **Device + Driver** | **Observation + Sync** | **Yes (cross-device)** | **Device→map→host feedback loop** |

### Addressing the "always_max" misconception

A potential PC concern: "If the simplest policy (always_max prefetch) is optimal, why is a programmable framework needed?"

This is based on a misreading of results. always_max is a good BASE policy for prefetch scope, but it is NOT the optimal end-to-end policy:

| Workload | always_max alone | Best policy (requires programmability) | Improvement over always_max |
|----------|-----------------|----------------------------------------|-----------------------------|
| GNN (1.34x oversub) | 2.60x | + async cross-block prefetch | **3.36x (+27%)** |
| FAISS (1.5x oversub) | ~48s add | + phase-adaptive gating (uprobe) | **-31.8%** |
| vLLM multi-tenant | ~baseline | + priority eviction + preemption | **P99 -9.5%, tput +9.8%** |
| llama.cpp (1.84x) | ≈ baseline | (PCIe saturated — all policies hit ceiling) | — |

The policies that surpass always_max require capabilities that only the programmable framework provides:
- **Cross-block prefetch**: async kfuncs (bpf_wq → DMA) — cannot be expressed as a sync advisory decision
- **Phase-adaptive gating**: uprobe on CUDA APIs — driver cannot see application phases
- **Priority eviction**: multi-hook coordination (uprobe writes priority → fault handler reads it) — no single hook can express this
- **Device-side work-stealing**: device-side BPF — host-only hooks cannot schedule GPU thread blocks

The framework's value is precisely in enabling policies BEYOND what any single hook or fixed configuration can express.

---

## Opus SOSP Review Results (2026-03-31)

### Round 1 verdict: BORDERLINE → Round 2 verdict: WEAK ACCEPT

### Issue tracking

| Issue | Round 1 | Round 2 | Notes |
|-------|---------|---------|-------|
| One-liner too long (23 words) | OPEN | **FIXED** | 11 words, at parity with XDP |
| Lifecycle diagram missing prefetch edge | OPEN | **FIXED** | Prefetch triggers existing transition proactively |
| Async DoS hole in safety model | OPEN | **FIXED** | Scoped to correctness, not performance isolation; parallels sched_ext/XDP |
| Cross-device map consistency undefined | OPEN | **FIXED** | Eventual consistency + stale reads can't violate safety |
| "Not a new abstraction" self-undermining | OPEN | **FIXED** | Reframed positively: boundary crossing forces new semantics |
| Acid test too simple (no composition) | OPEN | **FIXED** | 4 tests: simple → proactive → multi-hook → device feedback |
| Information challenge weak (no e2e) | OPEN | **FIXED** | Work-stealing scheduler + xCoord feedback + combined prefetch |

### Head-to-head comparison with prior work (from opus review)

**vs sched_ext:**
- Borrows: struct_ops callback pattern, verifier-as-safety, automatic fallback
- Adds: async kfuncs (sched_ext effects are immediate), device-side BPF (sched_ext is host-only), cross-device maps
- sched_ext does better: cleaner single-subsystem story (one resource type, one lifecycle, one decision domain)

**vs XDP:**
- Borrows: "event triggers BPF, return action" pattern, kfunc-as-capability-surface
- Adds: multi-hook composition (XDP is single-attachment), async effects (XDP is sync), cross-device operation
- XDP does better: simplicity — one hook, one return value, zero state

**vs Syrup (SOSP'21):**
- Borrows: multi-layer hooks under one policy framework, different layers provide different context
- Adds: crosses physical device boundary (Syrup is all on same host), async effects (Syrup is sync matching), device-side execution
- Syrup does better: cleaner unified abstraction — `schedule(input) → executor` applies uniformly to every layer. gpu_ext hooks have heterogeneous signatures across layers.

**vs cache_ext:**
- Borrows: folio lifecycle hook model for eviction is directly analogous to chunk lifecycle
- Adds: prefetch as proactive lifecycle trigger (cache_ext is purely reactive), async effects, cross-device scope
- cache_ext does better: simpler resource (page cache) with mature baselines

**vs XRP (OSDI'22):**
- Borrows: BPF initiating I/O from within driver path — XRP chains NVMe I/Os, gpu_ext chains DMA migrations
- Adds: manages stateful, long-lived resource (page residency) vs stateless I/O chains; device-side BPF entirely new
- XRP does better: tighter, more elegant contribution — one mechanism with clear latency wins

**vs Bento (SOSP'21):**
- Borrows: safely replacing kernel subsystem policy with user-written code, kernel mediates all effects
- Adds: BPF (vs Rust), cross-device (vs single-host VFS), runtime hot-loading (vs compile-time safety)
- Bento does better: richer safety guarantees via Rust type system; can express full subsystem logic (not just advisory)

### Prepared rebuttal: "Just sched_ext for GPU"

> sched_ext hooks CPU transitions with immediate local effects; XRP chains storage I/Os at a single completion point. gpu_ext must handle something neither confronts: policy effects that cross a physical device boundary, take ms, and operate on a processor whose internal state is opaque — forcing async kfuncs with deferred completion, cross-device shared state with explicit consistency, and a second verified execution environment on the GPU. The contribution is not applying BPF to a new target but showing that lifecycle-extensibility breaks at device boundaries and what new semantics are necessary to restore it.

### Remaining issues to address in paper

**Model / design section:**
1. **Heterogeneous hook signatures**: Syrup's hooks all share `schedule(input) → executor`. gpu_ext hooks have different signatures at each layer (struct_ops returns ordering, uprobe calls kfuncs, device-side writes maps). Need to justify why heterogeneous hooks still constitute a unified model — the unifying element is the lifecycle + effects + maps pattern, not the function signature.
2. **vs XRP distinction**: XRP already does "BPF initiates async I/O." Must clearly articulate: gpu_ext manages **stateful long-lived resources** (page residency persists across invocations) vs XRP's **stateless I/O chains** (each chain is independent). This is a qualitative difference in what the model must handle (state tracking, consistency, lifecycle).

**Paper sections not yet updated:**
3. **Intro P4b** (mechanism mapping): still has old jargon (sleepable kfuncs, bpf_wq, uprobes, SIMT verifier). Needs rewriting with lifecycle + effects framing.
4. **Intro P5** (system summary + results): arq said "unconventional to preview ablation." Needs compression.
5. **Intro Contributions**: arq said "#1 is not strong." Rethink based on lifecycle model — potential: (1) lifecycle-based cross-device extensibility model, (2) gpu_ext implementation spanning host+device with SIMT verifier, (3) evaluation across 4 workloads showing each mechanism layer adds value.
6. **Abstract**: needs complete rewrite to match new framing.
7. **Design section**: needs restructuring around the execution model (§4.1 model overview → §4.2 hook points → §4.3 kfunc API → §4.4 maps → §4.5 device-side BPF → §4.6 safety). Current design.tex has misaligned challenges (C1-C3 don't match intro's three challenges).
8. **Eval section**: needs connecting sentences mapping results to the three challenges. Current RQ1/RQ2/RQ3 organization is fine but doesn't explicitly connect to intro's story.

**Missing experiments:**
9. **No controlled experiment for coupling**: no comparison of unified hooks vs separate memory/scheduling hooks. This is the direct evidence for the coupling challenge. Difficult to add but its absence weakens the claim.

### Device-side BPF: three e2e usage patterns (confirmed from codebase)

1. **Observation feedback**: `eviction_lfu_xcoord.bpf.c` — GPU fault stats → `gpu_state_map` → host CPU scheduler (`sched_gpu_coord.bpf.c`) reads fault_rate → PID controller adjusts BE throttle. Full device→map→host loop.
2. **Combined host-device policy**: Device-side L2 prefetch + host-side callback stride-based prefetch → 1.77x speedup (eval.tex).
3. **Device-local policy**: Work-stealing thread-block scheduler — `should_try_steal` handler executes directly on GPU SMs.

These three patterns demonstrate that device-side BPF is not "vaporware-adjacent" (opus round 1 concern) but is used across observation, combined policy, and device-local policy execution.

---

## Model Evolution Log (2026-03-31 discussion)

This section records the key insights and course corrections from the yunwei37 + opus discussion that led to the current model. Included so future revisions can trace the reasoning.

### Evolution 1: From "three composable layers" to "lifecycle + effects"

**Problem**: M1 ("three composable mechanism layers") was a feature list, not a model. Opus anti-pattern: "layer cake — three separate models pretending to be one."

**Fix**: A+C combination — lifecycle transitions provide structure (where hooks are), kfunc effects provide execution semantics (what hooks do). This passed the first opus review as BORDERLINE → WEAK ACCEPT.

### Evolution 2: From "decisions at lifecycle points" to "all effects via kfuncs"

**Problem** (yunwei37): "struct_ops 本质是什么？是每个生命周期的关键节点做出决策？我们实际上现在要的是什么？好像不太一样？wq 和 uprobe 也都不是关键节点，effect 也不是决策？"

**Analysis**: struct_ops hooks are decision points — kernel asks, BPF answers. But gpu_ext hooks are a MIX:

| Hook type | Is it a decision point? | What it actually does |
|-----------|------------------------|----------------------|
| evict_prepare | Yes — kernel asks "eviction ordering?" | Returns ordering (decision) |
| chunk_activate | No — kernel notifies "chunk activated" | BPF injects async prefetch (effect) |
| uprobe on cudaLaunch | No — not a kernel question at all | BPF proactively prefetches (effect) |
| bpf_wq | No — deferred continuation | BPF executes async DMA (effect) |
| device-side should_steal | Yes — runtime asks "steal?" | Returns bool (decision) |
| device-side observation | No — not a decision | BPF writes metrics to map (effect) |

Only SOME hooks are decision points. Most are event-triggered effect injectors. Framing everything as "decisions at lifecycle transitions" is inaccurate.

**Insight** (yunwei37): "我们是不是换成非 struct_ops 更好？" — if we frame the model as "all effects via kfuncs" (no return-value decisions), every hook has the same shape: `event → BPF → kfunc calls + map ops`. No exceptions. Implementation can still use struct_ops return values as a convenience, but the conceptual model is pure kfunc effects.

**Fix**: Reframed the model as effect-based. Every hook is an event-triggered effect injector, not a decision point. The kfunc API (not the function signature) is the unified abstraction.

### Evolution 3: From "heterogeneous hooks are a problem" to "kfunc API is the unification"

**Problem**: Three opus reviewers all said "gpu_ext fails Syrup's unified-abstraction test" because hooks have different signatures across layers.

**Analysis** (yunwei37 + discussion):

Syrup CAN unify signatures because all layers do the SAME operation (matching: input → executor) on the SAME hardware (CPU). gpu_ext hooks do DIFFERENT operations (eviction ordering, proactive prefetch, warp observation) on DIFFERENT hardware (host CPU interrupt context, user process context, GPU SIMT). A unified function signature is impossible and would be artificial.

But a unified EFFECT INTERFACE is possible: the kfunc API is callable from ALL hooks in ALL execution environments. Maps are accessible from ALL hooks.

**The comparison**:

| System | What's unified | Type of unification |
|--------|---------------|-------------------|
| Syrup | `schedule(input) → executor` | Signature-level (same function everywhere) |
| sched_ext | struct sched_ext_ops | Struct-level (one struct, different callback signatures) |
| gpu_ext | kfunc API + BPF maps | Interface-level (shared effect API + shared state) |

Note: sched_ext ALSO has heterogeneous callback signatures (select_cpu returns s32, enqueue is void, dispatch takes different args). sched_ext's unification is the struct_ops, not the individual signatures. Syrup is the exception, not the norm.

**Fix**: Stopped trying to find a unified signature. Instead, articulated kfunc API as the unified abstraction — analogous to how syscalls unify what user programs can do, kfuncs unify what BPF policies can do. Syrup unifies the decision format; gpu_ext unifies the effect interface.

### Evolution 4: Correcting "return value vs kfunc call" oversimplification

**Problem**: Initial framing said "sched_ext/XDP use return values, gpu_ext uses kfunc calls." But sched_ext's `enqueue()` also calls `scx_bpf_dispatch()` (a helper/kfunc), and XDP can call `bpf_redirect()`. So the distinction is not clean.

**Correction**: The difference is not "return values vs kfuncs" in absolute terms, but in PRIMARY effect mechanism and what the model REQUIRES:
- sched_ext: return value is the primary decision mechanism. Helper calls exist but are secondary.
- gpu_ext: kfunc calls are the ONLY effect mechanism (in the conceptual model). Return values are eliminated.

And the deeper reason: gpu_ext REQUIRES the kfunc model because:
1. Multiple effects per hook (can't express with one return value)
2. Async effects (return values are synchronous)
3. Cross-device effects (return values are local)

### Evolution 5: Device-side BPF from auxiliary to co-equal

**Problem**: Internal analysis docs kept saying "auxiliary," "instrumentation only," "weak evidence." Three opus reviewers all said device-side BPF is the MOST novel part.

Opus reviewer 3: "If the paper is remembered, it will be for proving that the BPF verification model can be exported to a fundamentally different processor architecture. Everything else is context for that result."

**Analysis**: Device-side BPF is NOT just instrumentation. It has three e2e usage patterns:
1. Observation feedback (fault stats → map → host scheduler)
2. Combined host-device policy (device-side prefetch + host-side prefetch → 1.77x)
3. Device-local policy execution (work-stealing scheduler on GPU)

**Fix**: All docs updated. "Auxiliary" → "co-equal contribution." The paper already presents device-side BPF as headline; the analysis docs were lagging behind.

### Evolution 6: From "all effects via kfuncs" to "decoupled lifecycle direction" (2026-03-31, latest)

**Problem** (opus reviewer 4): "'All effects via kfuncs' is like saying 'all effects via function calls' — it describes how BPF works, not a new execution model." Also: sched_ext's `enqueue()` also calls helpers (`scx_bpf_dispatch`), so "return value vs kfunc" is not a clean distinction. And: the acid test example uses `return last_active` — contradicting "no return-value decisions."

**Problem** (yunwei37): "模型本身不需要是 lifecycle hook；我们只需要 effect 在 lifecycle 上面生效" and "struct_ops 本质是什么？是每个生命周期的关键节点做出决策？我们实际上现在要的是什么？好像不太一样？wq 和 uprobe 也都不是关键节点" and "effect 这个词准确吗？" → need a more precise verb for what BPF programs do to lifecycles.

**Key insight** (yunwei37): The real difference from sched_ext is NOT "kfunc calls vs return values." It is:
- sched_ext: one sync decision at one transition point (kernel calls BPF at the transition, BPF decides, done)
- gpu_ext: many decisions from many trigger points, sync + async, all directing lifecycle transitions from anywhere

**sched_ext couples decisions to transition points. gpu_ext decouples them.**

But both are lifecycle-bounded — decisions can only affect valid transitions.

**Word choice**: "effect" is too generic (any side effect). "operate on" too vague. The precise description: BPF programs **direct** lifecycle transitions — their ordering, timing, and selection. "Direct" = active guidance without full control (driver still executes), common in systems ("direct I/O", "packet steering").

---

## Current Model: Decoupled Lifecycle Direction (2026-03-31, latest)

### One-liner

**"gpu_ext lets BPF programs direct GPU resource lifecycle transitions from anywhere in the management stack, decoupling policy decisions from the transition points they affect."**

### Core distinction from sched_ext / cache_ext

In sched_ext and cache_ext, the kernel invokes a BPF callback **at** each lifecycle transition and the program makes a single synchronous decision **at that point** — which CPU to dispatch to, which page to evict. One transition, one decision, at the point of transition.

The device boundary **forces** gpu_ext to invert this: BPF programs can be triggered from **many points** across the GPU stack — driver fault events, application API calls (uprobes), device execution events (device-side BPF), deferred async work (bpf_wq) — and from any of these points, **direct multiple lifecycle transitions asynchronously**. A single uprobe on `cudaLaunchKernel` can initiate prefetch of data (directing a future evicted→activated transition), adjust eviction priority for an unrelated chunk (directing future eviction ordering), and write phase state to a map that will influence transitions later. The decisions need not be synchronous, need not be one-per-hook, and need not originate from the transition they affect. This decoupling is not a design choice — sched_ext *could* decouple but does not need to (CPU transitions are local and immediate); gpu_ext *must* decouple or it cannot express the policies at all.

But all decisions remain **lifecycle-bounded**: they can only direct the ordering, timing, and selection of valid transitions.

```
sched_ext:  decision ←→ transition point    (coupled, 1:1, sync)
gpu_ext:    decision ←— any trigger point   (decoupled, many:many, sync+async)
                 └——→ lifecycle transitions  (bounded)
```

### Why GPU requires decoupled direction

This departure is necessary because GPU lifecycle transitions cross the device boundary:

1. **Async**: A migration (evicted→activated) takes milliseconds of DMA — it cannot complete within a synchronous callback. BPF must initiate the transition and let it complete after the hook returns.
2. **Proactive**: A prefetch decision is best made from an application event (uprobe on `cudaLaunchKernel`), not reactively at the fault that triggers the transition. The decision originates outside the lifecycle.
3. **Cross-device observation**: Device-internal state that should inform eviction ordering is only visible from GPU-side BPF, not from the host-side transition point. The information originates on a different device than where the transition executes.

The sched_ext model — one sync decision at the transition point — cannot express these policies. gpu_ext decouples the decision from the transition point, allowing decisions to be made anywhere and take effect on the lifecycle asynchronously.

### How this maps to the three challenges

| Challenge | What it requires | How decoupled direction addresses it |
|-----------|-----------------|--------------------------------------|
| **Timescale** | Transitions cross the interconnect, take ms | Decisions can initiate transitions asynchronously (bpf_wq + async kfuncs). The transition completes later; the decision returns immediately. |
| **Coupling** | Memory placement and compute scheduling share transitions (page fault = both) | Decisions from any trigger can direct transitions in both domains. A uprobe can direct both a prefetch (memory) and an eviction priority (scheduling) simultaneously. |
| **Information** | Device-internal state is opaque to host | Device-side BPF programs observe GPU state and direct device-local transitions (work-stealing), or feed observations into maps that inform host-side direction. |

### Safety: lifecycle bounds all direction

Safety is preserved because the lifecycle constrains what decisions can direct, not where they originate:

- Every kfunc corresponds to a valid lifecycle operation. A prefetch initiated from a uprobe and a prefetch initiated from a fault handler produce the same lifecycle transition (evicted→activated) through the same validated kfunc.
- The BPF verifier guarantees program safety (termination, memory bounds). The lifecycle guarantees that all directed transitions are valid.
- **Triggers are open; operations are lifecycle-bounded.**
- If a policy fails, the driver reverts to default lifecycle behavior.

Lifecycle safety guarantees correctness (no invalid transitions, no crashes), not performance isolation. A policy that floods async prefetches is directing valid transitions wastefully — like a sched_ext policy that dispatches to the wrong CPU. The remedy is the same: unload and fallback.

### Comparison to prior work (revised)

| System | Primary decision origin | Decoupling degree | Sync/async | Cross-device? |
|--------|------------------------|-------------------|------------|---------------|
| **sched_ext** | At transition point (`select_cpu`, `dispatch`) | Low — helpers can affect other DSQs, but from the same transition, synchronously | Sync | No |
| **XDP** | At packet arrival | None — one decision per packet | Sync | No |
| **Syrup** | At scheduling event per layer | Low — maps share state across layers, but each decision is sync at its trigger | Sync | No |
| **cache_ext** | At eviction point | None — one decision per eviction | Sync | No |
| **XRP** | At I/O completion | Medium — chains multiple I/Os, but stateless and at the same attachment point | Chained | No |
| **gpu_ext** | **From any trigger (fault, uprobe, device, async wq)** | **High — different trigger, different transition, async execution, cross-device** | **Sync + async** | **Yes** |

**What's shared**: All use BPF programs, verifier, kernel mediation, fallback. The lifecycle hook pattern is the common foundation.

**What gpu_ext changes**: The device boundary **forces** decoupling decisions from transition points. sched_ext *could* decouple (helpers in `enqueue()` can affect other DSQs) but does not *need* to — CPU transitions are local and immediate. gpu_ext *must* decouple or it cannot express the policies at all: DMA takes 6.6ms, application intent is invisible at the fault point, device state is opaque from the host. This causal necessity — not design taste — is the model-level difference.

**Two co-equal contributions built on this model**:
1. **Host-side decoupled direction**: async kfuncs + bpf_wq for cross-device transitions, uprobes for proactive triggers, shared maps for cross-hook coordination
2. **Device-side BPF**: verified programs on GPU SIMT hardware with warp-uniformity verifier, enabling device-local direction (work-stealing) and observation feedback

### Acid test (revised — no return-value decisions)

```c
// Uprobe: proactive prefetch — decision DECOUPLED from the transition
SEC("uprobe/cudaLaunchKernel")
int on_launch(struct pt_regs *ctx) {
    u64 va = PT_REGS_PARM1(ctx);
    u64 size = PT_REGS_PARM2(ctx);
    // Direct a future evicted→activated transition from an app event
    bpf_kfunc_prefetch_range_async(va, size);
    // Direct future eviction ordering from an app event
    u32 pid = bpf_get_current_pid_tgid() >> 32;
    u32 priority = HIGH;
    bpf_map_update_elem(&tenant_priority, &pid, &priority, 0);
    return 0;
}

// Fault handler: ordering — direction at the transition point
SEC("struct_ops")
int evict_prepare(struct gpu_chunk *chunk) {
    u32 pid = bpf_kfunc_get_chunk_owner(chunk);
    u32 *prio = bpf_map_lookup_elem(&tenant_priority, &pid);
    // Priority was set by the uprobe (decoupled decision)
    bpf_kfunc_set_eviction_priority(chunk, prio ? *prio : DEFAULT);
    // Also initiate async prefetch for next chunk (async direction)
    u64 next = bpf_kfunc_get_chunk_va(chunk) + CHUNK_SIZE;
    bpf_kfunc_prefetch_range_async(next, CHUNK_SIZE);
    return 0;  // no decision in return value
}

// Device-side: observation feeds into host direction
SEC("gpu/thread_block_start")
int on_tb(struct gpu_tb_ctx *ctx) {
    u32 sm_id = ctx->sm_id;
    struct sm_state *s = bpf_map_lookup_elem(&sm_utilization, &sm_id);
    if (s) __sync_fetch_and_add(&s->active_blocks, 1);
    return 0;
}
```

Three hooks, three layers, all kfunc-based (no return-value decisions). The uprobe **directs** transitions it does not trigger. The fault handler uses **decoupled state** (priority set by uprobe). Device-side BPF **feeds observation** into shared maps.

### Paper design section — target paragraphs (revised after opus review 5)

**Paragraph 1 — Model (lead with sched_ext contrast, then lifecycle):**

In sched\_ext and cache\_ext, the kernel invokes a BPF callback at each lifecycle transition and the program makes one synchronous decision at that point. \sys{} decouples decisions from transition points: BPF programs triggered by any event across the GPU management stack --- driver faults, application API calls, device execution events, deferred async work --- can direct multiple lifecycle transitions asynchronously. The lifecycle defines what transitions exist; BPF programs direct which ones happen, when, and in what order. For example, a single uprobe on \texttt{cudaLaunchKernel} can call \texttt{bpf\_kfunc\_prefetch\_range\_async()} to initiate a future migration (directing an evicted$\to$activated transition) while simultaneously updating a map that influences eviction ordering at a later fault. GPU resources --- memory chunks and compute contexts --- follow driver-defined lifecycles (allocated$\to$activated$\to$accessed$\to$evicted; submitted$\to$scheduled$\to$running$\to$preempted), and all kfunc operations are scoped to valid transitions within these lifecycles.

**Paragraph 2 — Why decoupling is forced (not chosen):**

This decoupling is not a design preference --- it is forced by the device boundary. A page migration takes 6.6\,ms of DMA across PCIe; it cannot complete within a synchronous callback, so BPF must initiate transitions asynchronously. Optimal prefetch decisions are best made proactively from application events, not reactively at the faults that trigger transitions. Device-internal execution state that should inform eviction ordering --- per-SM utilization, warp-level access patterns --- is only visible from GPU-side BPF, not from any host-side transition point. sched\_ext's helpers can affect other dispatch queues synchronously from the same transition; gpu\_ext's decoupling is qualitatively stronger: different trigger source, different target transition, asynchronous execution across the device boundary. sched\_ext does not need this because CPU transitions are local and immediate; gpu\_ext cannot avoid it.

**Paragraph 3 — Safety (structural syllogism):**

Safety follows from three facts. First, kfuncs are the \emph{sole} interface between BPF policy programs and the GPU driver --- there is no alternative path to hardware resources (no raw page-table writes, no direct register access, no unmediated DMA). Second, every kfunc implementation validates its parameters and enforces lifecycle invariants before executing. Third, the BPF verifier guarantees that programs terminate, stay within memory bounds, and call only valid kfuncs. Together, these ensure that the policy capability surface is \emph{exactly} the kfunc set --- regardless of trigger source. A prefetch initiated from a uprobe and one initiated from a fault handler produce the same validated transition (evicted$\to$activated) through the same driver-mediated kfunc. Triggers are open; operations are lifecycle-bounded. If a policy errs, the driver reverts to default behavior.

**Implementation note (one sentence, for paper):**

For driver-level hooks where the lifecycle naturally presents a decision point (e.g., eviction ordering), the implementation uses struct\_ops return values as a convenience. Conceptually, returning an ordering value is equivalent to calling \texttt{bpf\_kfunc\_set\_eviction\_priority()} --- both direct the same lifecycle transition through driver-mediated validation.

### Key insights from discussion (preserved for reference)

**Insight 1 — Trigger vs effect distinction (yunwei37)**:
"模型本身不需要是 lifecycle hook；我们只需要 effect 在 lifecycle 上面生效。" The model is not "hooks at lifecycle points" — the model is "operations bounded by the lifecycle, triggered from anywhere." This separates WHERE you hook (open) from WHAT you can do (lifecycle-bounded).

**Insight 2 — Not decisions, not effects, but direction (discussion)**:
"Effect 这个词准确吗？" → "effect" is too generic (any side effect). What BPF programs do is **direct** lifecycle transitions — their ordering, timing, and selection. "Direct" = active guidance without full control. The driver still executes; BPF directs which transitions happen.

**Insight 3 — The core model contrast (yunwei37)**:
"我们不是在关键转换的点做一个决策，而是允许在很多点做很多 async 的决策，但这些决策都只会在 lifecycle 内影响转换的时机和方向。" This is the single sentence that captures the model.

**Insight 4 — Coupling is not merging (discussion)**:
Memory and scheduling are NOT the same lifecycle — data can be resident while compute is preempted; eviction for workload A is a positive for workload B. Coupling means "decisions in one domain affect the other" because they share device memory and bandwidth. A BPF program at the fault handler sees BOTH domains and can direct transitions in both simultaneously.

**Insight 5 — Forced not chosen (opus review)**:
sched_ext *could* decouple but doesn't need to. gpu_ext *must* decouple or it cannot express the policies at all. The causal necessity — not design taste — is the insight.

### Candidate paper figures

**Figure A — Decoupled direction (model diagram)**:
Shows the core model: many triggers → all direct the same lifecycle transitions.

```
    Trigger Points                                Lifecycle Transitions
    (open)                    kfuncs              (bounded)
                             (direct)
                                              ┌─────────────────────────┐
 ┌─ Host ──────────────────┐                  │  Memory Chunk           │
 │                          │                  │                         │
 │  App: cudaLaunch      ──┤                  │  alloc ──▶ active ──┐  │
 │       cudaSync        ──┤                  │    ▲                 │  │
 │       (uprobe)           │                  │    │  prefetch       ▼  │
 │                          │  ┌──────────┐   │    └─(async,6.6ms)─evi │
 │  Driver: fault        ──┼─▶│   BPF    │──▶│                         │
 │          evict        ──┤  │ programs │   ├─────────────────────────┤
 │          activate     ──┤  │    +     │   │  Compute Context        │
 │                          │  │   maps   │──▶│  submit──▶run──▶prempt │
 │  Async: bpf_wq        ──┤  └──────────┘   │    ▲              │    │
 │                          │                  │    └──────────────┘    │
 ╞═══ PCIe / NVLink ════════╡                  │                         │
 │                          │                  │  Both domains directed  │
 │  Device: warp_exec    ──┤                  │  by same BPF programs   │
 │          TB_start     ──┤                  │  via same kfuncs        │
 │      (device BPF)        │                  │                         │
 └──────────────────────────┘                  └─────────────────────────┘
   any event can trigger                        only valid transitions
   a BPF program                                can be directed
```

**Figure B — Coupling (why memory + scheduling can't be separated)**:
Shows that one hook directs both domains simultaneously through shared resources.

```
                       ┌───────────────────────────────────────┐
                       │      GPU Resource Management           │
  Triggers             │    (memory + scheduling coupled)       │
                       │                                        │
                       │  memory decisions   sched decisions    │
  uprobe ─────────────▶│  ┌──────────────┐  ┌──────────────┐  │
  (cudaLaunch)         │  │ evict order  │◀▶│ preempt who  │  │
                  ┌───▶│  │ prefetch what│◀▶│ resume who   │  │
  struct_ops ─────┤    │  │ prefetch when│◀▶│ priority     │  │
  (fault/evict)   │    │  └──────────────┘  └──────────────┘  │
                  │    │         │    coupled     │             │
  bpf_wq ────────┘    │         │  (shared       │             │
  (async)              │         │ device memory  │             │
                       │         │ + bandwidth)   │             │
  ═══ PCIe ════════════╡         └───────┬────────┘             │
                       │                 │                      │
  device BPF ─────────▶│     one BPF program                   │
  (warp/TB)            │     directs BOTH domains              │
                       │     from ANY trigger                  │
                       └───────────────────────────────────────┘
```

**Figure C — sched_ext vs gpu_ext (contrast diagram)**:
The simplest visual showing the model difference.

```
  sched_ext (coupled):

    transition ──▶ BPF ──▶ decision ──▶ immediate effect
    point           │         │              (same processor,
                    │         │               same moment)
                    └─────────┘
                    1:1, sync


  gpu_ext (decoupled):

    trigger A ──▶ BPF ──┬──▶ direct transition X (async, 6.6ms DMA)
                   +    ├──▶ direct transition Y (ordering)
    trigger B ──▶ maps  └──▶ write state for future trigger C
                   │
    trigger C ──▶ BPF ──▶ read state from A, direct transition Z
                   │
    ═══ PCIe ══════╪═══
                   │
    trigger D ──▶ BPF ──▶ observe device state, feed back to maps
    (device)

    many:many, sync+async, cross-device
    all transitions lifecycle-bounded
```

### Evolution 7: Terminology refinement — "effects", "triggers", "direct" (2026-03-31, latest)

**"kfuncs" in the intro**: Early versions said "via kfuncs" in P4b. This is implementation detail for the intro. Changed to "via effects" — abstract enough for intro, design section explains that effects = kfunc calls.

**"side effects" considered and rejected**: "Side effects" is technically accurate (BPF programs produce effects during execution, not via return values). But in systems context "side effect" implies "unintended/accidental," which contradicts "intentional policy action." "Via effects" is cleaner, no PL baggage.

**"decouple decisions from the transitions they direct" → "decoupling triggers from their effects"**: Original phrase was circular — "decisions direct transitions but are decoupled from them?" Reader confusion. "Triggers from effects" is cleaner: trigger = where BPF runs, effect = what lifecycle transition is directed. No circularity.

**"To ensure safety, policies can..." → "To ensure safety, policies only..."**: "Can" sounds like permission; "only" sounds like constraint. The safety point is about what policies CANNOT do, framed positively.

**Current P4b in intro.tex (after all refinements):**

> \sys{} models GPU resource management as programmable lifecycles, where verified BPF programs direct transitions such as memory eviction, prefetch, and compute preemption via effects. Unlike current policy interfaces such as sched_ext, cache_ext and XDP, where each callback makes one synchronous decision at one transition point, \sys{} programs can direct multiple transitions asynchronously from any trigger in the management stack, decoupling triggers from their effects. To ensure safety, policies only influence which transitions occur and when, but cannot create invalid transitions or violate resource invariants. \sys{} extends this model to the GPU itself, where a SIMT-aware verifier enables verified BPF to run on GPU hardware for observation and policy execution.

**Key terminology settled:**
- **"direct"** — what BPF programs do to lifecycle transitions (ordering, timing, selection)
- **"effects"** — the lifecycle operations BPF programs produce (eviction, prefetch, preemption). In implementation = kfunc calls, but intro uses abstract "effects"
- **"triggers"** — where/when BPF programs are invoked (fault, uprobe, device event, bpf_wq)
- **"decoupling triggers from effects"** — the core model property distinguishing gpu_ext from sched_ext
- **"lifecycle-bounded"** — effects can only direct valid transitions (safety)

### Open questions (latest)
1. Is "direct" the right verb? Alternatives: steer, guide, influence. "Direct" chosen for precision — active guidance without full control, common in systems terminology.
2. Does "decoupled from transition points" accurately describe what sched_ext CANNOT do? sched_ext's `enqueue()` can call helpers that affect other parts of the scheduler — is that also "decoupled"? Answer: partially. sched_ext helpers affect the same lifecycle synchronously from the same transition point. gpu_ext's decoupling is stronger: different trigger, different transition, async execution.
3. The acid test examples now use `bpf_kfunc_set_eviction_priority()` instead of return values. Is this consistent with the actual implementation? Implementation still uses struct_ops return values. Paper should note: "implementation uses struct_ops return values as a convenience; conceptually equivalent to the kfunc pattern."
4. Which figure(s) to use in paper? Figure A (model), B (coupling), and/or C (contrast)?
5. Implementation can be changed to match the model (make eviction ordering also go through kfuncs instead of return values). Not a fundamental concern.
