# Bounded canonical FAST transformations

The implementation for [#40](https://github.com/buster14a/buster/issues/40)
lives in `ir_fast.c`, included by the existing `compiler_ir` module immediately
after `ir_promote.c`. It adds no second IR and widens neither instruction row.
The transforms are **opt-in** pending paired total-compile-time and peak-RSS
acceptance. Register allocator FAST remains the ordinary default independently.

The [initial paired comparison](performance-audits/2026-09-11T200359Z.md) and
[query-reduction/ablation follow-up](performance-audits/2026-09-11T204614Z.md)
retain the observed total-time cost and inconclusive intervals. They do not
establish performance acceptance; #40 remains open for that criterion.
The [separate production-profile experiment](performance-audits/2026-09-11T211034Z.md)
validates tests-OFF outputs against the tested compiler and retains its full
on/off comparison. Its mixed, inconclusive results also leave acceptance open;
it includes a correction to the earlier optimization-level label.

## Order and ownership

`ir_prepare_canonical_module` receives the frontend's completed CFG. It checks
uncertified input, runs the existing shared local-promotion oracle when needed,
then runs selected FAST transforms on each lowered function in this order:

| Stage | Algorithm and preserved boundary |
| --- | --- |
| Local promotion / frontend SSA | Existing frontend construction and `ir_promote_function`; the existing `-fno-frontend-ssa` and `-fno-canonical-local-promotion` controls remain independent. |
| `fold` | One forward scan; path-compressed value replacements; integer constants up to 64 bits, integer identities, same-type pure casts, integer truncation/extension. No floating-point folding, division/remainder folding, branch deletion or iterative global propagation. |
| `address` | One scan collapses `&*pointer` and `*&place` only when type, category, alignment and all value qualifier facts agree. Shared selector address facts in #44 own deeper offset/index analysis. |
| `dce` | Count surviving instruction and edge-argument uses; queue unused pure definitions, decrement their operands transitively. Each row enters the queue at most once. No CSR use index. |
| `parameters` | Remove a block parameter when all resolved non-self incoming values agree. Up to four full sweeps; remaining nontrivial/cyclic parameters stay valid. No edge order or CFG topology change. |
| Compact | One `ir_rewrite_compact` map shared with local promotion updates values, instruction links, operands, parameters/incomings, source ranges, extras and builder-local maps. Shared input operand slices are copied once into a single new pool. |
| Certify | Any changed FAST result is checked for uncertified callers and Debug/test/sanitizer/explicit transform-check builds. Optimized production trusts the transform's contract under the same policy as promotion; input certification is not extended to changed rows. |

The compaction helper is extracted from existing promotion without changing its
remapping algorithm. Its parameter cleanup remains separate, so counters added
by #371 retain their meaning. FAST counters do not include that cleanup.
Functions with computed-label instructions or sparse label provenance are
conservatively skipped; their ID/provenance relation is left intact.

`fast_complete` records one completed optional transformation run, not a
certificate. Mutating a prepared module requires clearing the relevant
preparation markers and revoking the caller's input certificate. Repeated
untrusted preparation still validates. Published CFG/address facts must be
invalidated before mutation and rebuilt after these transforms (#38/#44).

## Purity contract

`ir_instruction_is_pure` is the shared canonical DCE authority. Scalar integer
arithmetic, integer/pointer conversions and address construction may be removed
when their results have no instruction or incoming-edge uses. Place-producing
FIELD/INDEX rows may be removed; value projections are conservative. Calls,
loads (including nonvolatile loads), stores, atomics, fences, inline assembly,
stack operations, traps, all exact SIMD operations, division/remainder and
floating arithmetic/conversions remain observable. This preserves both their
effects and deliberate backend refusal. New opcode classes are impure until
this contract explicitly classifies and tests them.

## Controls and measurement

```sh
# Select all passes, then independently disable one (last flag wins).
build/Release/ide cc source.c -fcanonical-fast -fno-canonical-fast-dce -o output
# Select only folding; do not collect timing during throughput observations.
build/Release/ide cc source.c -fcanonical-fast-fold -o output
# Diagnostic replay only: clocks and per-pass work counts, plus compaction.
build/Release/ide cc source.c -fcanonical-fast -ftime-canonical-fast -v -o output
```

All four names accept `-fcanonical-fast-NAME` and
`-fno-canonical-fast-NAME`. `-fno-canonical-fast` clears the whole selection.
Timing selection does not enable transformations. Per-pass `IR_FAST_PASS` records
report the enable/timing bits, nanoseconds, row/operand/incoming visits and
changes. `IR_FAST` records instruction counts, skips, parameter-cap hits,
compaction time and memory upper bounds. Multi-input sums additive counters;
the scratch bound is the maximum, not a sum.

The explicit incremental limits per function are:

- 16 MiB maximum scratch payload bound: `16*values + 16*instructions + 256`.
  This covers replacement/use/value maps, removal bytes, deletion queue,
  compaction instruction/link maps and allocation alignment.
- 8 MiB maximum new retained payload bound:
  `4*original_operands + 8*original_instructions`. This includes the compacted
  operand pool and the worst case of one new integer immediate per input row.
  When CFG data is already published, admission also includes reopening its
  builder arrays: `sizeof(IrBlockParameter)*parameters + sizeof(IrIncoming)*arguments
  + sizeof(IrPredecessor)*edges`, plus up to `alignof(T)-1` padding for each of
  those three allocations. Reopening is recorded even when no pass changes a row.
- 4,194,304 maximum work population: instructions + values + operands + blocks
  + parameters + incoming arguments. Each selected folding/address pass scans
  once, DCE is linear, and parameter cleanup has at most four sweeps. Budget
  accumulation saturates before any unsafe multiplication.

An exceeded guard skips all optional passes for that function and increments
`budget_skips`. It does not reject source. Memory records are conservative
payload **bounds**, not measured arena capacity or peak process RSS. They cover
these new passes; frontend construction, existing local promotion, mandatory
validation and backend storage retain their existing costs. Explicitly enabling
FAST after CFG publication admits the combined pass/reopening retained bound
before it restores builder lists; a refused function keeps its published CFG
and allocates nothing. The ordinary driver runs FAST before its first
publication. The initial guard scans flat instruction rows and either linked
builder parameters or published parameter/argument counts without allocating.
Later mandatory CFG republication remains outside the optional FAST payload
bound and must still be included in total peak-memory measurements.

Default adoption requires the existing frozen six-workload throughput corpus,
all four allocator modes, same compiler build/target, all-enabled versus each
pass disabled, raw paired timings and peak RSS, and an A/A noise control on the
exclusive qualified host. Require no confirmed total-time regression and no
peak-RSS regression beyond the accepted bound. A lower instruction count or an
inconclusive hosted result cannot enable the default. Self-hosting fixed point,
all pass subsets, native mode matrix and non-native consumers remain correctness
gates independently of performance acceptance.

## Tests

`ir_fast_test.c` checks all 16 subsets structurally, transitive dead work,
retained calls/memory effects, existing trivial parameters, repeat preparation,
classification exclusions and budget refusal before value-storage access.
`driver_fast_test.c` executes the fixed-result C fixture for all 16 subsets in
NONE/MIR_STACK/FAST/QUALITY with zero fallback on desktop hosts. Android/iOS
retain all 64 native object-generation checks; their application process cannot
launch generated executables. Each subset also compiles to
Wasm64, eBPF and LLVM bitcode. The bounded existing eBPF VM executes four inputs
including unsigned wraparound. Wasm and bitcode magic checks establish artifact
production, not engine execution; stronger engine/external-compiler checks are
recorded separately when available.
