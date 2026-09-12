# Buster compiler-throughput priorities — 2026-09-12

## Status and scope

This is a prioritized synthesis of retained evidence and proposed experiments,
not a new performance measurement or implementation audit. Repository snapshot:
`5dab18af0208cfa7fa65673b6884d5c253fda85b`. The objective is to make Buster
produce artifacts faster, not to spend additional compilation time optimizing
the generated programs. No compiler build, new profile, benchmark, sanitizer,
self-host or platform-matrix run was performed while preparing this note.

**Recommendation:** remove unnecessary C-to-IR construction and memory touching
first; pursue function-local parallelism for larger single-translation-unit
improvements; apply SIMD to the remaining measured dense work. Do not initiate a
replacement IR or another benchmark framework. Preserve canonical IR, supported
C behavior, diagnostics, deterministic output and the existing validation gates.

The current implementation must be distinguished from historical issue premises:
PR #497's empty-block SSA retirement is merged; compact opcode metadata already
serves the FAST prepass; native multi-TU link cohorts already exist. None of those
facts alone establishes accepted end-to-end performance on the filing revision.

## Historical evidence and the size of the opportunity

The [September 11 native profile][native-profile] sampled source
`44a90fcbff44988624878aff9db3346a8bc6550b` on a Ryzen 7 9700X, with a Clang
22.1.8 Release producer and Superluminal sampling at 8 kHz. It is one diagnostic
capture, not a repeated A/B benchmark or a measurement of the snapshot above.
Its compiler lane was 708.685 ms:

| Phase | Sampled inclusive time | Share of compiler lane |
| --- | ---: | ---: |
| C-to-IR lowering | 334.064 ms | 47.14% |
| Canonical/native code generation | 183.999 ms | 25.96% |
| Semantic analysis | 62.504 ms | 8.82% |
| Preprocessing | 47.244 ms | 6.67% |
| IR preparation/validation | 41.748 ms | 5.89% |
| AST parsing | 21.623 ms | 3.05% |
| Object/DWARF construction | 17.501 ms | 2.47% |

A simple model gives useful prioritization, not a forecast. For an assumed
fraction `p` accelerated by `s`, with all other work unchanged and no added
cost, `speedup = 1 / (1 - p + p/s)`:

| Hypothetical change to that historical workload | Modeled lane-time reduction | Modeled lane-throughput multiplier |
| --- | ---: | ---: |
| All lowering runs twice as fast | 23.6% | 1.31x |
| Lowering and codegen both run twice as fast | 36.6% | 1.58x |
| Codegen alone runs eight times as fast | 22.7% | 1.29x |
| All lowering and codegen run eight times as fast | 64.0% | 2.78x |

The last row does **not** establish that the entire fraction is parallelizable,
that eight workers supply an eightfold phase speedup, or that Buster can obtain
2.78x. It excludes scheduling, imbalance, additional memory traffic, preparation,
merging and all other added overhead. These are not current measured bounds or
acceptance targets. Re-profile integrated main before assigning effort from
this historical partition.

## 1. Remove remaining lowering work: #447 and #306

### Preserve the merged SSA improvement; measure what remains

[PR #497][ssa-pr] merged as `9666ce1c468b8a2484e47bb6a71dce366c6a40e9`.
Its stable list retires blocks once their parameters disappear. The retained
[construction census and pilot][ssa-audit] report simplification block visits
falling from 1,129,060 to 395,559, including list construction. This is a 65%
reduction in this work count, **not** a 65% SSA-time or compiler-time improvement.

The retained shared-Xeon pilot did not establish a speedup: stage-1 wall medians
were 3.211331 s for the baseline and 3.329474 s for the candidate, with broad
paired uncertainty. It is not qualified 9700X acceptance; fewer visits must not
be substituted for that result. #447 remains the owner of the remaining work
and performance disposition.

Important unchanged populations in that diagnostic workload were:

| Work/request population | Count or bytes |
| --- | ---: |
| Sparse-map slot probes | 3,126,949 |
| Finish-time slot growths | 1,572 |
| Parameter visits | 882,059 |
| Incoming-value visits | 1,753,581 |
| Value-sized scratch requested | 54,542,756 bytes |
| Explicit value-table clears | 34,929,292 bytes |

Investigate one attributable cost at a time. Compare affected-parameter/block
worklists with the existing stable active-block sweeps, including index and queue
cost. Evaluate a measured sparse-map reservation bound or valid lookup reuse,
not a dense block-by-local replacement. Compare scratch reuse, touched-entry
reset or generations only where their extra per-access cost is justified.
Preserve representative selection, canonical IDs, cyclic/trivial parameters,
source metadata, uninitialized-read behavior and structured failures.

Do not let SSA completion absorb the whole lowering campaign. In the older
native capture, `c_ir_ssa_finish` was 75.951 ms inclusive within 334.064 ms of
lowering. Approximately 258.113 ms lay outside that subtree. Halving that subtree
alone corresponds to about 5.4% less compiler-lane time in the simple model.
The remaining lowering needs attribution rather than a presumed explanation.

### Avoid constructing and retracting the wrong expression form

[#306][place-value] already owns explicit place-versus-value demand. Its source
lead is `c_ir_recover_place`, `c_ir_retract_last_load` and
`c_ir_require_addressable`: some paths emit a load and subsequently recover its
addressable place, retracting instructions and repairing state.

Census these paths, then test a bounded expression family whose consumer can
request the required form from the start. Include the complete producer/consumer
cost, not just the retraction helper. Preserve volatile/atomic accesses,
bit-fields, aggregates, evaluation order, diagnostics and provenance. This is
an unmeasured candidate, not a request for another frontend representation or
an unconditional whole-lowerer rewrite.

## 2. Reduce actual memory touching: #52 and #525

The historical native profile attributes 62.867 ms of exclusive executing time
to kernel `clear_page_erms`, about 8.17% of executing time. This is cross-cutting
work inside the phase costs above, **not another independent phase to add**.
Only some attribution lies under IR values, lexer allocation and lowering setup;
no single allocation-site change is credited with the entire total.

The historical 343 MiB of unused logical capacity in #52 is not demonstrated
recoverable resident memory or a throughput estimate. Keep the current capacity
policy until a representative experiment justifies changing it. Distinguish:

| Ledger | Meaning |
| --- | --- |
| Logical reserved capacity | Representation capacity, including unused slots |
| Explicit initialized/copied bytes | Requested memory traffic in the implementation |
| Page faults and touched pages | Operating-system interaction of actual accesses |
| Peak live allocations and RSS | Retained logical storage and measured residency |

[#525][zeroing] owns a narrow source-range growth experiment. At the inspected
source, `ir_function_add_instruction` allocates `canonical_sources`, clears the
full new capacity and then copies the old live prefix over part of that clear.
The existing arena zeroed-allocation API can avoid explicit clearing of known
fresh zero memory while still clearing potentially dirty rewind/reuse storage.

First screen growth frequency, byte volume and the actual arena lifetime. Test
`arena_allocate_zeroed(arena, IrSourceRange, capacity)` in place of the ordinary
allocation plus unconditional clear; retain prefix copying and initialization
semantics. Preserve or explicitly revise counter meanings, especially bytes
versus rows at partial dirty-watermark boundaries. Exercise fresh storage,
poisoned reuse, partial overlap, repeated growth and allocation failures.

Fewer explicit fill bytes do not establish lower CPU cost or RSS. A smaller
capacity estimate can also lose if its preliminary traversal or additional
copies cost more than it saves. Do not mix growth policy, arena pooling and IR
layout changes into the first bounded experiment.

## 3. Reuse current backend facts; vectorize only a measured residual

[#131][fast-prepass] must start from the current implementation, not the old
large-descriptor proposal. The FAST prepass already consumes a 16-byte
`MachineOpcodeRow` and operand-kind masks. The compact metadata work also serves
other backend consumers. Do not assign another task to implement that same
conversion, infer L1 residency from record size, or assume classification is
the bottleneck without attribution.

In the historical native capture, selection was 71.727 ms inclusive and complete
FAST placement 60.339 ms inclusive. The prepass alone was 21.481 ms inclusive,
about 3.03% of the lane. Its 21.356 ms exclusive attribution is about 3.01%.
Selection and placement together warrant investigation beyond that one prepass.
These sampled values are not a new main measurement or a forecast for #131.

Under existing #122/#132 ownership, identify repeated operand decoding,
prewalks, successor discovery and other facts that can be reused with clear
ownership and invalidation. Count both the removed traversals and any added
sidecar construction/traffic. A second stream is not automatically cheaper.
Keep #124's residual metadata interpretation work separate and recheck what
pre-resolved plans already do before adding more.

For a genuinely dense residual, compare current scalar, batched scalar, AVX2
and AVX-512 where supported. Include extraction, tails, exceptional cases,
compaction, temporary traffic and scalar updates. Preserve the serial allocator
state machine where row-to-row state is load-bearing. Use existing `simd.h`,
correct feature guards and scalar/platform fallbacks; retain the applicable
Zen 4 evidence requirement. A kernel win alone is not whole-compiler acceptance.

## 4. Separate project scaling from single-TU scaling: #53, #54 and #531

### Existing TU cohorts: evaluate before replacing

The [driver contract][driver] already documents `-fcompile-jobs=N` for
consecutive native C inputs in a link invocation. For example:

```sh
build/Release/ide cc -fcompile-jobs=4 a.c b.c c.c -o app
```

The documented bounded feature does not parallelize preprocessing-only,
syntax-only, `-S`, `-c`, non-native paths or single-input fast paths. Default
worker count remains one pending acceptance. Worker-sized cohorts can still be
limited by a large TU. Test balanced/imbalanced multi-file inputs at bounded
1/2/4/8 worker counts and measure total time plus peak memory under #53/#424.
Do not report multi-file scaling as a unity single-TU speedup.

### Per-function backend: continue the existing owner

[#54][function-codegen] owns machine-code-generation parallelism within a TU.
Use private function scratch/results, stable source-indexed fragments,
deterministic layout offsets and subsequent fixups. Include relocations,
diagnostics, unwind/debug contributions and fragment lifetimes in the design.
Serial table prewarm alone does not make shared signature plans, source cursors,
line suppression or inline-assembly symbol mutation thread-safe; the
[parallelism guide][parallelism] explicitly identifies these dependencies.

Use the existing persistent gang. Bound memory and total admitted work, including
nested TU/function scheduling, rather than multiplying whole-TU storage by
worker count. Keep one-lane and single-threaded behavior on the same kernel.
Backend-only parallelism is useful, but its historical fraction explains the
limited whole-lane multiplier in the model above.

### Larger opportunity: prove a function-body lowering boundary first

New [#531][function-lowering] owns feasibility and a bounded experiment for
function-body C-to-IR lowering after necessary declaration/type resolution.
It does not assert that such a boundary is already independent. Inventory
mutable types/caches, symbols, globals/constants, canonical IDs, initializers,
source/debug state, diagnostics, inline assembly and arena ownership. Classify
each as immutable, serially preparable, privately owned, mergeable or genuinely
serial; measure the eligible fraction and its function-size distribution.

Only then attempt an opt-in implementation with existing lanes and deterministic
publication. No alternative IR, whole-TU cloning, new callback pool or global
lock around the entire lowerer. A justified no-go/no-change result is acceptable.
This issue does not expand #54's scope or become a new blocker for #36 retirement.

## 5. Independent experiment: trusted-producer PGO under #55

[#55][pgo] already owns PGO; do not create a duplicate. Compare ordinary trusted
Clang Release Buster with a PGO-built Buster. Training should run representative
actual compilations; evaluate the final uninstrumented compiler on distinct
held-out workloads, including tiny inputs/startup, large unity inputs,
macro-heavy and CFG-heavy code and relevant modes/targets.

Keep source, semantic options, workload/dependency closure and output contracts
matched. PGO changes the host machine code running Buster, not the requested
optimization policy for generated programs. Do not mix PGO, new SIMD algorithms,
LTO and unrelated source changes in the first comparison. Keep any later
interpreter-superinstruction experiment separately attributable; the existing
issue's broader title does not justify assuming an interpreter bottleneck in
the native workload. No gain is promised by this note.

## Execution order and acceptance

| Priority | Owner | Next deliverable |
| --- | --- | --- |
| 1 | #447, with #128 coordination | Current integrated profile, accepted disposition of #497, then one attributed remaining lowering cut |
| 2 | #52/#525 and independently #306 | One measured memory-touch or construction experiment, with counters and real compiler-time/RSS results |
| 3 | #54 | Correct, bounded per-function backend implementation and single-TU scaling evidence |
| 4 | #531 | Ownership inventory, measured eligible lowering fraction and explicit prototype/no-go decision |
| 5 | #122/#131/#132/#124 as attribution warrants | Optimize a remaining backend bottleneck against current code, not historical premises |
| Independent | #55 and #53/#424 | PGO comparison and evaluation of existing multi-TU cohorts |

Reuse #46's harness, #422's host qualification, #426's noise/decision policy,
#437's admitted execution service and #423's frozen real-workload descriptors.
Do not create another throughput epic or measurement loop. Keep exploratory
counts separate from performance acceptance and use existing infrastructure
without waiting for unrelated dashboards or scope expansion.

Each candidate needs a work-count check of its intended mechanism, paired
uninstrumented compiler-time measurements on fixed inputs, and correctness plus
resource checks outside its motivating case. Record exact source/tree/binary,
producer configuration, input/header hashes, options, target, host and raw
samples. Keep intrusive counters/profiling separate from ordinary timed runs.
Preserve fixed points, supported frontend forms, relevant allocator modes,
Debug/Release, sanitizers and applicable platform/ABI gates. Do not disable
required canonical validation or weaken semantic checks to obtain speed.

A smaller work count, a merged PR and a passing broad regression guard are not
interchangeable with a demonstrated speedup. Preserve inconclusive and negative
results. No benchmark result or numeric acceptance budget is approved by this
note. The changes made to publish it are documentation and issue bookkeeping
only; all compiler-execution gates for this publication are **NOT RUN**.

## References

[native-profile]: https://github.com/buster14a/buster/blob/5dab18af0208cfa7fa65673b6884d5c253fda85b/docs/performance-audits/2026-09-11T200453Z.md
[ssa-audit]: https://github.com/buster14a/buster/blob/5dab18af0208cfa7fa65673b6884d5c253fda85b/docs/performance-audits/2026-09-12T173031Z.md
[ssa-pr]: https://github.com/buster14a/buster/pull/497
[place-value]: https://github.com/buster14a/buster/issues/306
[zeroing]: https://github.com/buster14a/buster/issues/525
[fast-prepass]: https://github.com/buster14a/buster/issues/131
[driver]: https://github.com/buster14a/buster/blob/5dab18af0208cfa7fa65673b6884d5c253fda85b/docs/agents/driver.md#opt-in-native-translation-unit-lanes
[parallelism]: https://github.com/buster14a/buster/blob/5dab18af0208cfa7fa65673b6884d5c253fda85b/docs/agents/parallelism.md
[function-codegen]: https://github.com/buster14a/buster/issues/54
[function-lowering]: https://github.com/buster14a/buster/issues/531
[pgo]: https://github.com/buster14a/buster/issues/55
