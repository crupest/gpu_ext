# Paper Writing Skill (SOSP/OSDI Systems Papers)

When writing or reviewing LaTeX paper text, apply these rules. Sources: arq (co-author andi), yunwei37 (first author), methodology docs.

## Hard Rules (always do / never do)

### Double-Blind
- NEVER use "our" when referring to prior work. Use "a prior" / "the prior" / third-person. (arq)
- NEVER use author names in rendered text. Comment macros (\arq{}, \xiangyu{}) are OK.
- Check ALL \cite{} keys against the .bib — if any author overlaps with this paper's authors, it's a self-citation risk.

### Language
- NEVER use em-dashes (---). Use commas, semicolons, or separate sentences. (yunwei37)
- NEVER use "we argue" — weak for SOSP. Use "we propose" / "we show" / "we demonstrate". (yunwei37)
- Keep sentences short. If a sentence has 3+ clauses, split it. (arq: "This sentence is trying to do too much")
- Define acronyms on first use (e.g., UVM = Unified Virtual Memory). (arq)
- No absolute language without evidence ("always", "never", "fundamentally", "impossible"). (yunwei37)

### Factual Accuracy
- GPU hardware HAS policies (warp scheduler, timeslice scheduler, TLB). It delegates CERTAIN policy decisions to software. Don't say "GPU has no hardware policy." (yunwei37)
- Policy is scattered across the stack (user-space, CUDA runtime, driver, hardware). Driver is the coordination point, not the only policy location. (yunwei37)
- ALL 59 evaluated policies are agent-generated BPF programs. There are NO hand-written baselines. Never say "hand-written policies." (yunwei37)

### Structure
- One idea per paragraph. Don't mix different concepts. (arq: "The driver discussion is a different idea than where this paragraph starts")
- Establish context before using concepts. Architecture before workload diversity. Goals before goal-dependent language ("safe", "dynamic"). (arq)
- Evidence before explanation. Don't assert insights before showing data. (methodology)
- Intro paragraphs: lead with the topic sentence, not background.

## Anti-Patterns to Avoid

### Mechanism Zoo
Listing many mechanisms (kfuncs, uprobes, struct_ops, bpf_wq, device-side BPF, SIMT verifier) without a unifying model. Reviewer sees complexity, not insight. Each mechanism is a feature, not a contribution. (extension_model_methodology.md)

### Layer Cake
"Three composable layers, each crossing a boundary." Sounds clean but raises: Do they compose? In what order? Can you use layer 2 without layer 1? Actually three separate models pretending to be one. (extension_model_methodology.md)

### Taxonomy as Contribution
"We identify three challenges and address each with a mechanism." This is a design methodology, not an execution model. Reviewers want runtime behavior, not design process. (extension_model_methodology.md)

### Checklist
"Our model provides safety, programmability, deployability, and observability." Properties, not a model. A model is HOW you achieve properties. (extension_model_methodology.md)

### Bolted-On Trends
Adding trendy topics (agents, AI) without integrating them into the core story. If agent disappears after P1, it feels bolted on. (arq: "It reads as though we wanted to make the paper 'hot' so we chucked on some agent stuff")

### Editorializing in Background
Background should describe what things ARE, not what they do wrong. Save criticism for the motivation/gap section. (arq: "we probably really just want to focus on what the Kernel driver *is* rather than what it does wrong")

### Previewing Results in Intro
Ablation study details (2.60x -> 3.36x breakdown) in the intro is unconventional. Keep intro results high-level (up to 4.8x). (arq)

### Overclaiming Contribution
Don't claim a contribution that isn't fully supported by evaluation. Device-side BPF is observability-only — don't position it as equal to host-side policy mechanisms. (opus review)

## Self-Review Questions (ask before outputting paper text)

1. **One-sentence test**: Can I summarize this paragraph in one sentence? If not, it's trying to do too much.
2. **Reader knowledge test**: What does the reader know at this point? Am I using undefined terms?
3. **Evidence test**: Is every claim in this paragraph supported by data or citation? If not, weaken the claim.
4. **Removal test**: If I delete this sentence, does the paper get worse? If not, delete it.
5. **Attack test**: How would a hostile reviewer attack this paragraph? Preempt the attack.
6. **Champion test**: Could a sympathetic PC member quote this paragraph to advocate for the paper? If not, sharpen it.
7. **Double-blind test**: Does any phrase reveal author identity? Check "our", tool names, self-citations.
8. **Consistency test**: Does this claim match what we say elsewhere in the paper? Check intro vs eval vs abstract.

## SOSP/OSDI-Specific Principles

### What Gets Accepted (what_makes_top_venue_paper.md)
1. New abstraction that changes how people think
2. Surprising empirical result
3. Previously impossible capability
4. The right solution to a widely hacked-around problem

### What Gets Rejected
- "We applied X to Y" without new insight
- Taxonomy papers ("we identify N challenges")
- Engineering-only papers ("we made it work") without understanding WHY
- Papers where the framing promises more than the evaluation delivers

### The Champion Test
One PC member must be able to say in 30 seconds: "This paper does X, the insight is Y, and it matters because Z." If your intro doesn't lead to that sentence, rewrite it.

### Extension Model Clarity (extension_model_methodology.md)
A good extension model answers four questions:
1. WHERE does extension code run? (execution context)
2. WHEN does it run? (trigger / attachment point)
3. WHAT can it do? (capabilities and effects)
4. WHAT CAN'T it do? (safety boundary)

The model should be explainable in ONE sentence. If you need a paragraph, it's too complex.

### Good Questions yunwei37 Asks
- "This is implementation detail, not intro material" — intro = WHAT and WHY, sections = HOW
- "Is this really a mismatch or a design implication?" — don't inflate observations into contributions
- "Does this claim hold as hardware evolves?" — distinguish current-generation from permanent contributions
- "Are we overclaiming? What's the honest version?" — always prefer the weaker but defensible claim
