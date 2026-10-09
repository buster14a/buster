# Bounded canonical FAST transformations

The implementation for [#40](https://github.com/buster14a/buster/issues/40)
lives in `ir_fast.c`, included by the existing `compiler_ir` module immediately
after `ir_promote.c`. It adds no second IR and widens neither instruction row.
The no-flag throughput recipe enables these transforms after their paired
total-compile-time and peak-RSS acceptance on the dedicated qualified host.
Explicit optimization levels select the policy below. The complete pipeline
and every pass remain independently selectable. Register allocator FAST remains
the ordinary default independently.

The [initial paired comparison](performance-audits/2026-09-11T200359Z.md) and
[query-reduction/ablation follow-up](performance-audits/2026-09-11T204614Z.md)
retain the observed total-time cost and inconclusive intervals. They do not
establish performance acceptance; #40 remains open for that criterion.
The [separate production-profile experiment](performance-audits/2026-09-11T211034Z.md)
validates tests-OFF outputs against the tested compiler and retains its full
on/off comparison. Its mixed, inconclusive results did not establish acceptance;
it includes a correction to the earlier optimization-level label. The final
dedicated-host A/A, on/off, and leave-one-pass-out acceptance is retained in
[the final integration audit](performance-audits/2026-09-12T001148Z.md).

## Native optimization-level policy

The driver selects existing pass bits, not a second pass manager. Native
compilation carries that mask into `IrProgram.fast_passes`; canonical preparation
runs it once in the fixed order below, within the existing work/storage limits.
The last `-O` option selects the level. Unsupported spellings, including
`-O4` and `-Og`, produce an argument diagnostic before source compilation.

| Spelling | Optional canonical FAST selection | Native allocator default |
| --- | --- | --- |
| No `-O` option | `fold,address,dce,parameters` (retained throughput recipe) | FAST |
| `-O`, `-O0` | None | FAST |
| `-O1`, `-O2` | `fold,address,dce,parameters` | FAST |
| `-O3` | Same bounded recipe; no replay, reordering or larger budget | FAST |
| `-Os`, `-Oz`, `-Ofast` | Level-2 aliases, with the same bounded recipe | FAST |

These aliases do not promise a size-specific recipe or relaxed floating-point
semantics. Positive levels deliberately share the accepted local optimizer;
they do not claim Clang/GCC-equivalent optimization effort. Extra transforms
need their own implementation and evidence before enablement.

Explicit `-fcanonical-fast` / `-fno-canonical-fast` override all pass bits;
per-pass controls override only their named bit. For each bit the last explicit
control wins, independently of whether it precedes or follows `-O`. An explicit
`-fregister-allocator=fast|quality` is likewise independent of `-O`; the last
allocator option wins. Timing requests never enable a pass.

Direct SSA, shared local promotion and target placement remain independent
throughput mechanisms at every level. `-O0` skips optional cleanup and its
scratch/validation work. Debug information remains opt-in with `-g`;
`-fpinned-debug-locals` retains readable named scalar locals as documented in
[the driver guide](agents/driver.md#source-debug-information).

[#48 / PR #3111](https://github.com/buster14a/buster/pull/3111) owns inlining
before shared promotion and FAST cleanup. Its integration contract enables
bounded tiny inlining at positive levels, preserves mandatory source directives
at every level, and keeps explicit inliner controls independent. That PR's
implementation and qualified acceptance are prerequisites for claiming the
inliner part of #47 complete; this policy change introduces no inliner body.

`ide cc -v` reports each `IR_FAST_PASS` in execution order, including
`enabled=0` at `-O0`. Work/change counters distinguish selected passes from
actual rewrites or budget/validation skips. `-ftime-canonical-fast -v` adds
pass timings for diagnostic use.

## Order and ownership

`ir_prepare_canonical_module` receives the frontend's completed CFG. It checks
uncertified input, runs the existing shared local-promotion oracle when needed,
then runs selected FAST transforms on each lowered function in this order:

| Stage | Algorithm and preserved boundary |
| --- | --- |
| Local promotion / frontend SSA | Existing frontend construction and `ir_promote_function`; the existing `-fno-frontend-ssa` and `-fno-canonical-local-promotion` controls remain independent. |
| `fold` | One forward scan; path-compressed value replacements; integer constants up to 64 bits, integer identities, same-type pure casts, integer truncation/extension. No floating-point folding, division/remainder folding, branch deletion or iterative global propagation. Operand decoding and arithmetic are the shared `ir_integer_*` semantics (`ir_integer.c`); a row whose kernel result carries a shift-count, division or unsupported fault is left for run time. |
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
Producer-certified modules that do not yet satisfy the stricter canonical
validator are left unchanged as a unit and increment `validation_skips`.
Optional transformation must never turn an otherwise accepted legacy source
shape into a new diagnostic.

When preparation has already run that validator over the module in exactly
its current state -- the promotion-output scan of a Debug/test/sanitizer/
explicit transform-check build, or the input scan of an uncertified caller --
the FAST input guard reuses that result instead of scanning the unchanged
module a second time. Any mutation between the two points clears the reuse,
and the guard still scans whenever nothing has proven the current
representation. In the default tests-enabled Release `ide`, a translation
unit in which promotion changes any function therefore pays one strict scan
before FAST, not two.

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
# Independently disable one default pass (last flag wins).
build/Release/ide cc source.c -fno-canonical-fast-dce -o output
# Select only folding; do not collect timing during throughput observations.
build/Release/ide cc source.c -O0 -fcanonical-fast-fold -o output
# Diagnostic replay only: clocks and per-pass work counts, plus compaction.
build/Release/ide cc source.c -fcanonical-fast -ftime-canonical-fast -v -o output
```

All four names accept `-fcanonical-fast-NAME` and
`-fno-canonical-fast-NAME`. `-fno-canonical-fast` clears the whole selection.
Timing selection does not enable transformations. Per-pass `IR_FAST_PASS` records
report the enable/timing bits, nanoseconds, row/operand/incoming visits and
changes. `IR_FAST` records instruction counts, validation/budget/provenance
skips, parameter-cap hits,
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
FAST/QUALITY with zero fallback on desktop hosts. Android/iOS retain all 32
native object-generation checks; their application process cannot launch
generated executables. The registered native-level regression checks all
accepted spellings, explicit override ordering, invalid levels, all four
numeric levels under both allocators, skipped pass work at `-O0`, distinct
native text sections, and fixed-result execution on desktop hosts. Each subset also compiles to
Wasm64, eBPF and LLVM bitcode. The bounded existing eBPF VM executes four inputs
including unsigned wraparound. Wasm and bitcode magic checks establish artifact
production, not engine execution; stronger engine/external-compiler checks are
recorded separately when available.
