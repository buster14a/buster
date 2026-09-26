# Two-witness certificates for shared promotion cleanup

Research record: 2026-09-26 UTC. **Result: a valid bounded transfer with conditional work savings, not an established compiler optimization. Do not enable it unconditionally.**

The C prototype preserves the source-derived cleanup decisions in the tested model. An unfavorable case nevertheless reduces incoming visits while increasing root lookups. A fresh Buster census also shows that most of the apparent opportunity belongs to the explicit memory-form reference path, not the default frontend. No compiler speedup, runtime regression, or global novelty is claimed.

## Pinned problem, before the papers

The investigation started at main commit `ade6ac4b6ecb21f30b61b656439bac476c145e2f`, tree `4c5306221fdb22fccc929b55e333163742de17d0`. The relevant implementation is [ir_promote_compact](https://github.com/buster14a/buster/blob/ade6ac4b6ecb21f30b61b656439bac476c145e2f/src/buster/lib/compiler/ir/ir_promote.c#L649-L707), with source SHA-256 `8a02397d698438caab077b05c55c6b01c8ecdb66a365d37ad4235b12a63eb874`.

This is shared local-promotion cleanup, **not** direct frontend SSA simplification and **not** the capped FAST transform. It repeatedly visits blocks and surviving parameters. For each newly inserted parameter, it resolves incoming values until finding a second distinct non-self root, or scans the whole list to establish a single root. Removing a parameter immediately changes the replacement forest and requests another sweep. The final dense rewrite remains mandatory.

The initial workload hypothesis was specific: a cascading elimination can force repeated visits to a still-nontrivial parameter, repeatedly rediscovering the same late conflicting input. This can be wasteful even though the implementation already stops at the first conflict. The existing cascade regression associated with #981 supplied a source-backed nonempty case; the fresh census below reproduces three sweeps in memory form. It did not establish that this is hot on the default path.

Before outside research, the review covered AGENTS.md; frontend foundations, testing, build, SIMD, parallelism and benchmarking guidance; the [middle-end pass map](../middle-end-pass-map.md); matching open/closed discussions; and the identified recent [2026-09-25 audit](../performance-audits/2026-09-25T003323Z.md). That audit's negative population result concerned a different optimization and is not used as promotion workload evidence.

Relevant history was reconciled rather than re-reported: [#623](https://github.com/buster14a/buster/issues/623) is closed by merged [#981](https://github.com/buster14a/buster/pull/981), already in this pin; its empty-population skip and mandatory tail/remap repair are preserved. Merged [#371](https://github.com/buster14a/buster/pull/371) provides work counters. The research and comments in [#590](https://github.com/buster14a/buster/pull/590) identify scheduling and alias-invalidation hazards in a different FAST scope. Open [#1331](https://github.com/buster14a/buster/pull/1331) concerns frontend row-stream work; no owned frontend or test files were edited. Searches for the exact cleanup and watched techniques found no matching implementation in the inspected issue/PR scope. This is not an exhaustive novelty search or a claim to own neighboring research.

## Primary research and the actual transfer

Moskewicz, Madigan, Zhao, Zhang and Malik, [*Chaff: Engineering an Efficient SAT Solver*](https://www.princeton.edu/~chaff/publication/DAC2001v56.pdf), DAC 2001, section 2, supply the connection: two non-false literals certify that a clause is not unit, allowing larger scans to be deferred until a witness fails. Only this small-certificate principle transfers here; SAT search, backtracking and its reported speedups do not.

Ian P. Gent, [*Optimal Implementation of Watched Literals and More General Techniques*](https://research-repository.st-andrews.ac.uk/handle/10023/4132), JAIR 48 (2013), 231–252, published October 2013, gives a stronger circular-scanning analysis. Its monotone acceptability, notification and search-order assumptions must be established, not presumed. It also reports a propagation improvement that did not produce a statistically significant whole-solver improvement. This prototype does not implement circular scanning or claim that theorem's bound.

The author-maintained [Princeton zChaff distribution page](https://www.princeton.edu/~chaff/zchaff.html) was checked as an implementation reference: its listed 2007.3.12 release is a C++ distribution with its own usage conditions. Only the distribution metadata was inspected; no external implementation source was audited, copied or vendored.

### Mathematical objects mapped to Buster

For a live promotion-created parameter with value ID `p`, define

```
D(p) = { root(incoming.value) : incoming belongs to p,
                               root(incoming.value) != p }
```

The source removes `p` exactly when `D(p)` contains **one** value. Zero values, including self-only input, must not be treated as trivial. Preexisting parameters are outside this simplification's candidate set.

| Research object | Actual Buster object or operation |
| --- | --- |
| Population being classified | `IrBlockParameter.first_incoming` linked list |
| Current equivalence state | `replacements[]` and `ir_promote_root` |
| Small certificate that classification cannot trigger | Two incoming-derived roots in different classes, neither equal to `p` |
| Invalidated witness | The cached IDs now resolve to the same root, or one resolves to `p` |
| Recheck action | The original ordered incoming scan, including its representative selection |
| State lifetime | One `ir_promote_compact` invocation, before mandatory dense rewrite |

When an ordinary scan sees two conflicting roots, cache their IDs. On a subsequent visit, resolve both again:

```c
first = ir_promote_root(replacements, witness.first);
second = ir_promote_root(replacements, witness.second);
if (first != second && first != p && second != p)
{
    /* A valid certificate: retain p without an incoming-list scan. */
}
else
{
    /* Run the unchanged scan and refresh its certificate if nontrivial. */
}
```

This is a **polled certificate**, not an event-driven watched-literal scheduler. It retains the existing sweep, block and linked-list order and all immediate updates. It does not introduce a work queue, reverse-use index, SCC algorithm, parallel scheduler, new IR or canonical field.

### Transfer argument

Incoming lists and value identities remain stable during this cleanup. Replacement updates merge classes; they do not split them. Consequently, a cached representative remains in the class of the incoming value from which it was obtained. Re-resolving two cached IDs therefore recovers two current incoming classes. If they remain distinct and non-self, `|D(p)| >= 2`, so the original scan would retain the parameter.

A failed certificate does **not** authorize removal. It falls back to the original scan, which applies the unchanged nonempty singleton rule and picks the same root. Inducting over the original visitation order preserves removal decisions, replacement equivalence classes, removal trace and surviving list order. Additional path compression may change intermediate replacement-array entries; raw intermediate arrays are not claimed identical. Dense remapping is still required after cleanup.

## Baseline, slice and assumption matrix

The conventional baseline is the exact pinned **early-conflict** scan, not an artificially exhaustive scan. The prototype is generated from that source and adds only a per-invocation two-ID witness array and the checks above. Logical additional storage is `8 * A` bytes, where `A = value_count - old_value_count`, excluding alignment and allocator overhead. The first sweep initializes entries on first encounter. The zero-appended-value case allocates no witness array and still performs the common rewrite.

The stronger dirty-worklist alternative would need complete alias-class invalidation and a carefully preserved schedule. It is outside this slice and has no performance or correctness claim here. The simpler original scan remains the adoption baseline.

| Assumption or cost | Source/model status and consequence |
| --- | --- |
| Stable incoming membership and IDs | Holds during the selected source loop. A cache spanning incoming edits or dense renumbering would be invalid. |
| Monotone replacement classes | Required. Re-resolve both IDs; cached numeric inequality is insufficient. |
| Self exclusion | Required on every certificate use. A previously non-self root can become `p`. |
| Ordering and representatives | Existing block/list order and immediate changes retained; no FIFO or snapshot substitution. |
| Revisit frequency | One-sweep cases cannot repay cache construction. Early-conflict cases can add root calls. |
| Sparse versus dense inputs | Long stable conflict prefixes can save work. Dense but immediately trivial inputs cannot. |
| Construction and memory | One extra scratch allocation, `8A` logical bytes and witness stores; no reverse graph. Peak RSS and real allocator costs unmeasured. |
| Root lookup cost | A call is not a constant-time hardware event. Path length/compression and cache behavior are not captured by call counts. |
| SIMD and instruction set | Scalar C, no ISA requirement or new SIMD dispatch/fallback contract. No vector speed claim. |
| Parallelism and arena lifetime | Invocation-local scratch; no new shared state or lane changes. Production arena integration remains untested. |
| Invalid inputs and allocation failure | The reduced harness bounds its valid forest and storage. It does not establish production overflow, allocator-failure or diagnostic behavior. |
| Canonical validation | Never proposed for removal. The model substitutes a narrow remap seam and cannot certify real IR publication. |
| Self-hosting and portability | Clang/GCC x86-64 model results only. No Buster fixed-point, MSVC or AArch64 acceptance result. |
| Gent's stronger complexity bound | No proven correspondence for changing partner/root predicates and their costs; not imported. |

## Executed model: correctness and hostile cases

[prepare.py](../../tools/research/promotion-witness/prepare.py) refuses any source SHA-256 mismatch and extracts the production root finder and cleanup. It generates the conventional baseline, bounded candidate, uninstrumented candidate text and two deliberately broken controls. [probe.c](../../tools/research/promotion-witness/probe.c) supplies a C17 reduced-state harness. Python is a standard-library research extraction tool, not a new production/runtime dependency.

A separate classifier scans all incoming roots without path compression or early conflict termination, forms the complete distinct-root set, and checks every removal/retention decision. Differential checks compare flattened replacement maps, ordered removal traces with sweep numbers, surviving links, heads, tails, counts and unchanged incoming values. This classifier is algorithmically separate but was written by the same integrator; it is not independent agent review.

On GitHub-hosted Ubuntu x86-64, **25,892 cases passed each** under Clang 21.1.8 `-O2`, Clang ASan/UBSan with fatal errors, and GCC 15.2.0 `-O2`. The three stdout files compared byte-identical. The corpus comprises ten named cases, 882 exhaustive two-parameter cases, 5,000 exploration cases and 20,000 held-out-seed cases. Both random seeds were fixed before execution; the held-out cases use the same generator, not independent production programs. Parameter dependency cycles, aliases, preexisting parameters, varied ordering, duplicate inputs, empty input, self-only input and dense cases are included. Not every generated graph is a realizable CFG.

Two small counterexamples reject incomplete transfers:

* `p = phi(q, a); q = phi(a)`, visited in that order. Initially cached `q` and `a` become equivalent. Comparing stale IDs over-retains `p`.
* `p = phi(q, a); q = phi(p)`. Eliminating `q` makes one witness self. Omitting self exclusion over-retains `p`.

Both broken variants fail the separate classifier and the ordered cleanup comparison. These controls demonstrate missed cleanup, not a claimed machine-code miscompilation.

### Work counts that falsify a blanket optimization claim

Counts below are measured logical operations in the model, **not timing, instructions, RSS or compiler speedups**. Root calls exclude the independent oracle and common final flattening. Extra payload excludes harness storage and alignment.

| Model shape | Sweeps, unchanged | Incoming visits, baseline → candidate | Root calls, baseline → candidate | Extra payload |
| --- | ---: | ---: | ---: | ---: |
| No inserted parameters | 0 | 0 → 0 | 0 → 0 | 0 B |
| Two inputs, no revisit | 1 | 2 → 2 | 2 → 2 | 8 B |
| Early conflict plus reverse-order cascade | 17 | 306 → 64 | **306 → 336** | 136 B |
| Late conflict after a long equal prefix | 17 | 4,624 → 318 | 4,624 → 590 | 136 B |
| Dense, immediately trivial | 2 | 4,096 → 4,096 | 4,096 → 4,096 | 256 B |

The two cascade cases each perform 136 certificate checks, 121 successful skips and 64 witness stores. A lower `parameter_incoming_visits` count alone would misleadingly call both improvements. The early-conflict case instead adds root calls and scratch costs. The single-visit case adds three witness stores with no saved lookups; the dense trivial case adds 32 stores with no saved lookups.

Let `Q` be certificate checks and `I_fallback` the remaining incoming visits. The implemented lookup accounting is

```
R_candidate = I_fallback + 2 * Q
```

A lookup-count improvement requires `I_baseline - I_fallback > 2 * Q`. Even that is insufficient for a timing win because memory, stores, branches and find-path costs remain. There is no asymptotic sweep-count improvement in this slice.

## Fresh Buster workload census

An unmodified pinned Buster compiler was built on an authorized GitHub-hosted executor, then compiled four subjects in default and explicit `-fno-frontend-ssa` forms. All eight compile-only invocations completed successfully. The table reports **shared promotion** counters aggregated across each translation unit; sweep totals are not per-function maxima.

| Subject | Frontend form | Parameters inserted | Sweep total | Parameter visits | Incoming visits |
| --- | --- | ---: | ---: | ---: | ---: |
| `basic_c_operations.c` | default | 2 | 2 | 2 | 4 |
| `basic_c_operations.c` | memory reference | 30 | 18 | 35 | 70 |
| `basic_c_frontend_ssa.c` | default | 0 | 0 | 0 | 0 |
| `basic_c_frontend_ssa.c` | memory reference | 129 | 36 | 209 | 420 |
| Temporary cascade | default | 0 | 0 | 0 | 0 |
| Temporary cascade | memory reference | 6 | 3 | 13 | 26 |
| Compiler unity source | default | 1,374 | 14 | 2,538 | 5,795 |
| Compiler unity source | memory reference | 370,401 | 11,568 | 811,100 | 1,791,741 |

The default unity compile also reports 459,450 parameters created by the **different frontend SSA algorithm**. Assigning that population to this shared cleanup would manufacture the opportunity. The reference path's much larger incoming count likewise cannot be presented as default-path work.

The nonempty shared cleanup is live, but these results do not establish a consequential default-path bottleneck. Existing counters do not reveal conflict-prefix lengths, failed certificates or candidate hits. The near-two-input average is not a per-parameter distribution or a cache profitability proof. No candidate compiler was built, so the real-workload table contains no candidate correctness or improvement verdict. Compiling the unity source is not a self-hosting fixed point.

## Reproduction and evidence boundaries

The model execution head is `65778fc849dd3dcc98812056bf5e1eac75c19833`, tree `37eb6d5a14db312917cd5bc8e1039f7c2155a276`. [Model job 108302813100](https://github.com/buster14a/buster/actions/runs/36206085627/job/108302813100) succeeded. The overall first workflow failed because its separate baseline census selected unavailable mold during configuration; it is not a fully green run.

[Census retry job 108303227939](https://github.com/buster14a/buster/actions/runs/36206222943/job/108303227939) succeeded at workflow head `ae176667890dae46d8402b06881a0fe18090862b`, using the documented hosted Clang bootstrap exception and existing `--linker DEFAULT` option. The source worktree remained unchanged. No dependency was installed to run the experiment.

The exact temporary [model workflow](https://github.com/buster14a/buster/blob/65778fc849dd3dcc98812056bf5e1eac75c19833/.github/workflows/research-promotion-witness.yml) and [census retry workflow](https://github.com/buster14a/buster/blob/ae176667890dae46d8402b06881a0fe18090862b/.github/workflows/research-promotion-witness.yml) remain available at their execution commits. They created detached worktrees and recorded compiler identities. Their active workflow is removed from the final research diff rather than added as a permanent gate.

[evidence.txt](../../tools/research/promotion-witness/evidence.txt) preserves model commands, versions, raw results, source/generated-file hashes and [artifact 10893469576](https://github.com/buster14a/buster/actions/runs/36206085627/artifacts/10893469576). [census.txt](../../tools/research/promotion-witness/census.txt) preserves every selected counter row, compilation recipe, source/object/stdout identities and [artifact 10894340547](https://github.com/buster14a/buster/actions/runs/36206222943/artifacts/10894340547). Artifacts expire October 10, 2026; these tracked text records retain the essential evidence. Run the recorded recipes only on authorized hosted executors, not a desktop benchmark environment.

The model's remap substitute checks root flattening and list-tail maintenance, not actual dense value numbering, types, dominance, use-def consistency or machine code. Full canonical validation, registered compiler tests, production allocator/sanitizer integration, cross-target portability, self-hosting and real-workload candidate equivalence remain untested. The census is a developer Release work-count observation, not a frozen matched-production build. No timing result was interpreted as performance evidence; no 9700X lease or run occurred.

## Decision and handoff

Keep the conventional scan in production. The research establishes a usable certificate and two necessary invalidation rules, but rejects the incoming-visit metric as a sufficient decision criterion and does not justify default enablement. The favorable long-prefix result is a model condition, not a demonstrated workload win.

A future integration decision should first establish substantial **revisited late-conflict prefixes in the intended real path**, including failed-check and allocation costs. Only a positive result warrants an explicit production-owner handoff, a narrowly integrated candidate with canonical/registered/self-host correctness evidence, and the approved exclusively leased 9700X matched-build comparison. Until then, this is a preserved conditional/negative research result rather than an optimization waiting for automatic merge.

The sole integrator was GPT-6 Astra Pro. Actual tools were connected GitHub, GitHub Actions hosted executors, primary-source web/PDF inspection, and local text/archive/hash processing. No callable research subagents or independent reviewing agent were available. All compiler/model execution was hosted; the local staging worktree was not represented as a full Buster checkout. No owned production branch was modified or implicitly incorporated. No new runtime dependency, generated-binding edit, lane/SIMD change, acceptance-policy change, excluded retirement/service/admission/deployment work or merge is included.
