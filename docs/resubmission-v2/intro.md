# Intro — Working Draft

Based on draft3 discussion. One paragraph at a time, to be finalized incrementally.

---

## P1: Agent + GPU policy importance

As AI agents increasingly explore and optimizing systems[citations: SWE-agent, AutoGPT, LLM-for-systems, agentic coding], GPU resource management policies — memory placement, page migration, eviction ordering, and compute scheduling — have become a critical target for iterative optimization. No single policy fits all scenarios: recent work shows that default UVM placement policies [Forest, HELM, SUV, DREAM] and scheduling policies [GCAPS, GPREEMPT, XSched] perform poorly across workloads and lack multi-tenant coordination. The right policy depends on workload, hardware, and deployment — and model sizes are growing faster than GPU memory, making memory oversubscription and the policies governing it increasingly performance-critical. Under oversubscription, the choice of eviction and prefetch policy can control up to 73% of execution time.

> **What P1 does:** (1) Agents are exploring GPU policies (with citations to agent systems). (2) GPU policies matter — defaults are bad (with citations to GPU policy papers). (3) Stakes quantified: 73% under oversubscription. All external evidence, no our-own-results.
>
> **What P1 does NOT do:** No 59+/75%/1.3x (our development experience → save for P2 or eval). No problem statement (→ P2).

---

## P2: But the GPU stack prevents safe exploration

(To be written — problem statement: drivers are static, no safe extensibility, tension between high impact and zero programmability.)

## P3: Existing approaches are insufficient

(To be migrated from draft3 — user-space, driver mods, profiling. struct_ops = our starting point.)

## P4: Struct_ops is not enough — three boundaries

(To be migrated from draft3 — 27% gap evidence, memory+scheduling coupling, three boundaries each 1-2 sentences.)

## P5: gpu_ext

(To be migrated from draft3 — three mechanisms, once. Containment.)

## P6: Results

(To be migrated from draft3 — capability progression, overhead, containment stats.)

## P7: Contributions

(To be migrated from draft3 — three bullets.)
