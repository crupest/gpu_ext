# What Makes a Top Venue (SOSP/OSDI) Paper Attractive

## The one thing that matters

A paper must **change how people think about a problem.** Not solve it faster, not solve it more completely — change the mental model.

## Four types that get accepted

### 1. New abstraction
A new way to think about an old problem. The abstraction becomes the contribution, not the system.

- MapReduce: data processing as map + reduce
- Exokernel: separate protection from management
- sched_ext: kernel scheduling as loadable BPF programs
- Borg: the cluster as a single computer

The test: after reading the paper, does the reader think about the domain differently?

### 2. Surprising result
Contradicts conventional wisdom. "Everyone thought X, but actually Y."

- "Flash is fast but FTL is the bottleneck"
- "Microservices don't need fast networks, they need tail-latency control"
- "Hardware isolation is cheap enough for user-space"

The test: does the reader say "I didn't expect that"?

### 3. Previously impossible capability
Not "10% faster" but "now you can do X that was impossible before."

- XDP: packet processing at line rate in software
- Dune: user-space access to hardware privilege
- DPDK: bypass the kernel for networking

The test: could you do this before this paper existed?

### 4. The right solution to a hacked-around problem
The field has been using workarounds. You provide the principled answer.

- RCU: the right primitive for read-mostly concurrent data
- BPF: the right way to extend the kernel safely
- NVMe: the right storage interface for flash

The test: will people stop using the workarounds after reading this?

## What does NOT get accepted

- "We applied technique X to domain Y" — engineering report, not research
- "We identify N challenges and solve each" — taxonomy, not insight
- "Our system has these features" — feature list, not contribution
- "10% improvement over baseline" — incremental
- "We built a faster version of X" — no new understanding
- "Three composable layers" — complexity without a unifying idea

## What reviewers actually evaluate

### The champion test
Can ONE reviewer stand up in the PC meeting and say in one sentence why this paper matters? If the contribution takes a paragraph to explain, no one will champion it.

### The "so what" test
After describing the system, ask: why should I care? "It's faster" is not enough. "It changes how we think about X" is enough.

### The lasting value test
Will this paper be cited in 10 years? Papers that introduce abstractions last. Papers that optimize a specific system don't.

### The simplicity test
Is the core idea simple enough to explain to a colleague in 2 minutes? The best papers have simple core ideas with complex implications, not complex core ideas with simple implications.

## How to frame a paper for acceptance

1. Lead with the **insight**, not the system
2. The system **demonstrates** the insight, not the other way around
3. Make the reader feel they **learned something**, not just that they saw a system
4. Show the insight is **general** (applies beyond this specific system)
5. Be **honest** about limitations — this builds trust for the positive claims
