# Bounded canonical FAST transformations

The implementation for [#40](https://github.com/buster14a/buster/issues/40)
lives in `ir_fast.c`, included by the existing `compiler_ir` module immediately
after `ir_promote.c`. It adds no second IR and widens neither instruction row.
The transforms are enabled by default after passing paired total-compile-time
and peak-RSS acceptance on the dedicated qualified host. The complete pipeline
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

## Order and ownership

`ir_prepare_canonical_module` receives the frontend's completed CFG. It checks
uncertified input, expands requested canonical direct calls under #48's bounded
policy, then runs the shared local-promotion oracle and selected FAST transforms
on each lowered function in this order:

| Stage | Algorithm and preserved boundary |
| --- | --- |
| Direct-call inlining | `ir_inline_module` consumes the canonical argument values already evaluated by the caller, retains callable callee definitions and source line ranges, and checks transformed IR before FAST cleanup. Optional tiny selection and mandatory source directives are independent of the register allocator. |
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
validator skip optional FAST as a unit and increment `validation_skips`.
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
build/Release/ide cc source.c -fcanonical-fast-fold -o output
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
NONE/MIR_STACK/FAST/QUALITY with zero fallback on desktop hosts. Android/iOS
retain all 64 native object-generation checks; their application process cannot
launch generated executables. Each subset also compiles to
Wasm64, eBPF and LLVM bitcode. The bounded existing eBPF VM executes four inputs
including unsigned wraparound. Wasm and bitcode magic checks establish artifact
production, not engine execution; stronger engine/external-compiler checks are
recorded separately when available.

## Bounded direct-call inlining (#48)

Source `always_inline`/`__always_inline__` and
`noinline`/`__noinline__` directives survive declaration merging as canonical
symbol facts. Windows `__forceinline` retains its force-inline meaning and
ordinary C inline linkage. Contradictory directives are source errors.
A direct mandatory call that cannot be expanded produces a structured
`ir.inline-required` diagnostic rather than silently keeping the call.
Indirect calls preserve their ordinary runtime dispatch.

Tiny-call expansion is selected by positive `-O` levels or
`-fcanonical-inline` and disabled by `-O0` or `-fno-canonical-inline`;
the last selection wins. An invocation without an optimization-level or inline
flag retains the existing optional-inlining default. Source `always_inline`
requests remain independent of that selection.

Optional candidates are same-module, noninterposable single-block leaves with
compatible fixed-prototype signatures. Target-aware C lowering permits strong
external definitions on Windows COFF, while default-visible ELF definitions
remain interposable and weak/bodyless symbols remain ineligible. Canonical IR
embedders start with the conservative interposition policy.
Mandatory candidates can contain
multiple blocks, local storage, ordinary calls and aggregate returns.
The iterative mandatory-call graph processes callees before callers and
rejects cycles. Dynamic stack lifetime operations, computed labels,
variadic bodies, returns-twice calls and unsupported ABI shapes are refused.
Unsupported candidates remain calls when optional and diagnose when mandatory.
Arguments are evaluated once before the splice. Expansion remaps canonical
values, blocks, locals, source ranges, instruction extras and incoming edges.
Expansion retains the original out-of-line definition, including when its
address is observed. Cloned instruction ranges preserve supported source line
information; the debug model does not gain inline call-stack records.

Positive decimal limits are configurable through
`-fcanonical-inline-max-callee=N` (default 16 canonical instructions),
`-fcanonical-inline-function-growth=N` (default 1024 copied rows per caller),
`-fcanonical-inline-module-growth=N` (default 4096 copied rows per module), and
`-fcanonical-inline-call-sites=N` (default 64 sites per caller).
The caller default equals 64 sites times the 16-row tiny-body threshold; it also
accommodates multiple required checked-arithmetic helpers in production callers.
The module, work and storage guards still independently bound expansion.
Zero numeric fields in embedding options normalize to those defaults.
Mandatory calls are processed before optional tiny candidates across the module;
both phases share the same caller and module limits. The mandatory graph captures
call sites in one bounded scan rather than
rescanning every instruction to build reverse edges. Planning storage includes
function metadata, captured edge chunks and reverse-edge arrays. Caller planning counts block tails and phi metadata, with instruction prefixes
recorded during the candidate scan. Splices preserve existing IDs and repair block
chains directly, so no full caller compaction is required. Repeated splice work
stays charged per site. Predecessor work uses original and projected target
counts rather than unrelated instruction/value payload counts; call membership
checks stop when they reach the call. Growing table storage is charged from the caller's actual
capacities through projected appends, including geometric growth floors and
alignment. Per-splice snapshots, debug records, predecessors, payloads and clone
maps remain charged independently.
Tiny bodies are screened before payload scans. Mandatory expansion is still
subject to resource guards,
including bounded planning work and retained cloning storage. A mandatory call in a legacy
producer-certified module that cannot pass strict validation is diagnosed. The verbose
`IR_INLINE` record reports candidates, accepted/mandatory calls, copied rows,
growth, rejection categories and visits. These counts are diagnostics, not a
performance result. Mandatory budget refusals additionally report the first
resource category, requested amount and applicable limit in the source diagnostic.

Performance validation is incomplete until the applicable exact candidate,
workloads and configurations execute on the approved Ryzen 7 9700X. The
existing compiler comparison request measures default-path compile time,
available retired-instruction counters and ELF executable-section bytes.
A matched two-stage tiny-off/on generated-self-host comparison is required to
qualify optional tiny inlining. Each generated compiler executes the same source
workload and must reproduce its corresponding first-stage executable bytes.
Existing competitor
reports are diagnostic and do not isolate an inliner benefit. Default adoption
requires the existing throughput/memory acceptance policy and self-hosting,
platform, canonical-validation and backend correctness gates.
