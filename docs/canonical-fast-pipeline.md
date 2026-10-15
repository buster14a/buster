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

No level selects the opt-in `cse` experiment described in
[opt-in block-local CSE](#opt-in-block-local-cse-3011).

These aliases do not promise a size-specific recipe or relaxed floating-point
semantics. Positive levels deliberately share the accepted local optimizer;
they do not claim Clang/GCC-equivalent optimization effort. Extra transforms
need their own implementation and evidence before enablement.

Explicit `-fcanonical-fast` / `-fno-canonical-fast` override all pass bits:
the first selects the four-pass recipe and the second nothing, and both clear
`cse`. Per-pass controls override only their named bit. For each bit the last explicit
control wins, independently of whether it precedes or follows `-O`. An explicit
`-fregister-allocator=fast|quality` is likewise independent of `-O`; the last
allocator option wins. Timing requests never enable a pass.

No level changes canonical inlining. The inliner from
[#48 / PR #3111](https://github.com/buster14a/buster/pull/3111) runs before
shared promotion and FAST cleanup: mandatory `always_inline` sites are always
expanded, and tiny-leaf inlining stays off unless `-fcanonical-inline` is given.
Explicit inliner controls are independent of `-O`; see
[bounded direct-call inlining](#bounded-direct-call-inlining-48).

Direct SSA, shared local promotion and target placement remain independent
throughput mechanisms at every level. `-O0` skips optional cleanup and its
scratch/validation work. Debug information remains opt-in with `-g`;
`-fpinned-debug-locals` retains readable named scalar locals as documented in
[the driver guide](agents/driver.md#source-debug-information).

`ide cc -v` reports each `IR_FAST_PASS` in execution order, including
`enabled=0` at `-O0`. Work/change counters distinguish selected passes from
actual rewrites or budget/validation skips. `-ftime-canonical-fast -v` adds
pass timings for diagnostic use.

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
| `cse` (opt-in) | Block-local value numbering; see [opt-in block-local CSE](#opt-in-block-local-cse-3011). Runs only when `-fcanonical-fast-cse` selects it. |
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
For producer-certified input, a strict function validation failure declines
optional FAST only for that function (#1602). Every pass reads and rewrites
only its admitted function; invalid shared globals, aliases or initializers
still decline the whole module. `validation_skips` counts declined lowered
functions. Declined bodies remain unchanged by FAST and are excluded from its
output check; the shared scope and every admitted body are still checked.
Uncertified input must pass the full strict validator before any transform.
Optional transformation never turns an accepted legacy shape into a diagnostic.

Strict value validation permits unresolved struct/union layouts only for
`PLACE` results of `GLOBAL` or `DEREFERENCE` consumed by `ADDRESS_OF`.
These rows form addresses without reading object bytes. Every other consumer
of an incomplete place rejects, as do incomplete values, local allocations,
field/index results and explicit alignment claims. This rule applies equally
with and without label metadata; opaque extern address-taking does not decline
FAST (#3777). The [validation report](canonical-incomplete-address-validation.md)
records the focused controls and pinned compatibility census.

`IrFastStatistics.declines` retains every declining function and its first
strict error, block and instruction, in function order. `ide cc -v` prints
one `IR_FAST_DECLINE source=... function=... scope=function|module error=...`
record per declined body, with numeric `IrValidationError` values and invalid
IDs for unavailable locations. Multi-input records retain source order and
own their names after per-unit arenas are released. Ordinary valid input
allocates no decline records; a previously validated module needs no new scan.
The [pinned decline census](canonical-fast-decline-census.md) records the
observed source shapes, compile failures and follow-up owners for #1602.

When preparation has already run that validator over the module in exactly
its current state -- the promotion-output scan of a Debug/test/sanitizer/
explicit transform-check build, or the input scan of an uncertified caller --
the FAST input guard reuses that result instead of scanning the unchanged
module a second time. Any mutation between the two points clears the reuse,
and the guard still scans whenever nothing has proven the current
representation. In the default tests-enabled Release `ide`, a translation
unit in which promotion changes any function therefore pays one strict scan
before FAST, not two. The promotion-output check also reuses a strict input
proof for each body promotion left unchanged; changed bodies still receive
their independent output check. The allocation diagnostic counter
`ir_construction.preparation_promotion_input_proofs_reused` counts these
avoided body traversals. When that scan excludes a legacy-invalid body, its
successful neighbors retain individual post-promotion proofs in temporary
preparation storage. FAST reuses those exact-state proofs while still checking
shared scope and every unproven body. The proofs expire at the end of the
preparation call, before later caller mutations; transformed FAST output keeps
its existing independent check. The allocation diagnostic counter
`ir_construction.preparation_promotion_proofs_reused` counts this work reduction.

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

All five names accept `-fcanonical-fast-NAME` and
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

### Construction-time fold census (#3010)

`BUSTER_BENCH_ALLOCATIONS` builds report under `ir_construction` in
`-fsource-metrics` whether FAST's scalar folds could have happened when a row
was first built. `ir_fast_fold_rule` is the only rule authority. The fold pass
calls it, and `ir_construction_fold_census` also calls it with no replacement
map on every row the C builder commits through `c_ir_append_instruction`:

- `fold_birth_scalar_rows`: committed `CAST`, `UNARY` and `BINARY` rows probed.
- `fold_birth_<rule>`: rows a rule already folds from the operands committed
  before them (direct). `fold_birth_metadata_mismatches` counts identities
  rejected by the replacement metadata check. `fold_birth_avoidable_bytes` is
  logical row/value/operand/source storage for direct birth folds: an identity
  needs none of it, while a constant still needs its row and immediate. This
  is not an arena-call or RSS figure.
- `fold_birth_cascade_<rule>`: rows no rule folds directly but one does once
  earlier birth folds are applied: identity birth folds are followed to their
  replacement, and constant birth folds supply their folded bits.
  `fold_birth_cascade_uncached` counts rows that stayed unfoldable while an
  operand's birth-constant bits had left the bounded cache, so it bounds the
  cascades the census may have missed.
- `fold_late_<rule>`: folds FAST actually applied. `fold_late_replaced_roots`
  counts the folds whose operand had to be followed through the replacement map
  (an earlier identity or a parameter sweep).

The rules are `cast_identity`, `cast_constant`, `unary_constant`,
`binary_constant`, `binary_right_identity` and `binary_left_identity`. Probes
suppress `ir_semantics` recording, so those counters keep measuring only the
compiler's own semantic work, and they allocate nothing, so `allocation.*` is
unchanged.

### Matched birth/late association

Taken alone, `fold_birth_*` and `fold_late_*` count independent populations.
Birth counts include functions that FAST later skips or does not run
(`-fno-canonical-fast`, budget and provenance skips) and rows that later passes
delete. Late counts include rows that SSA insertion, promotion and inlining add
after commit. Neither a birth/late ratio nor late minus birth attributes or
bounds anything.

The `fold_match_*` counters join them per row. The census stores its answer in
the committed row: the diagnostic-only `IrInstruction.fold_birth` byte, which
exists only under `BUSTER_BENCH_ALLOCATIONS` and occupies the row's existing
padding, so `sizeof(IrInstruction)` stays 64. Whole-row copies carry it through
promotion, `ir_rewrite_compact` renumbering, CFG publication and inlining; rows
created after commit start zeroed. Each fold `ir_fast_fold` applies is counted
once, by rule, in exactly one class:

- `fold_match_direct_<rule>`: the row's birth tag names the same rule, decided
  from its committed operands.
- `fold_match_cascade_<rule>`: the birth tag names the same rule, decided
  through earlier birth folds.
- `fold_match_post_<rule>`: anything else, so the fold needed a fact
  construction did not have: SSA insertion, promotion, inlining, or a root the
  late scan supplied. The causes split as `fold_match_post_unprobed` (a row the
  C builder never committed, such as SSA or promotion output),
  `fold_match_post_unfoldable_at_birth` (committed, but no rule folded it) and
  `fold_match_post_rule_changed` (committed and foldable, but late facts chose
  another rule).

For each rule, `fold_late_<rule>` equals the sum of the three
`fold_match_*_<rule>` counters. An inlined copy keeps its callee row's tag
and the inliner adds a clone bit: a callee row foldable at its own commit
stays direct or cascade, while one that folds only with the caller's
arguments is post. `fold_match_clone_{direct,cascade,post}` count the clone
share of each class; they are a split, not further folds.

Survival follows each committed, non-clone birth-foldable row (direct or
cascade) into FAST. `fold_survival_reached` counts those in functions FAST
admits and `fold_survival_skipped` those in functions it declines (budget or
provenance). `fold_survival_unfolded` counts reached rows that the fold pass
did not fold in that run, for example because `fold` was not among the
selected passes. Birth folds in functions that never reach
`ir_fast_function` (`-fno-canonical-fast`, validation skips) or in rows
deleted before FAST appear in neither; they stay in `fold_birth_*` only.
These are hosted diagnostic counts, not performance evidence.

#### End-of-body breakdown and net account

`c_ir_fold_census_body` takes a second census of each C function after its body
lowers and before `c_ir_ssa_finish`. At that point the rows are still the
committed ones, minus retractions, and values are not yet renumbered. It asks
the same rule of every row (an operand defined after its reader does not count,
as at commit) and walks the rows twice more. Direct SSA still keeps stored
values in its `STORE` events and resolved reads, so those are counted as
readers (`fold_net_ssa_uses`). This population is close to the commit-time one
but not identical: retracted rows, functions whose body failed to lower, and
rows committed outside a function body are absent.

- **Form.** `fold_form_<shape>_<consumer>` crosses the folded row's IR shape
  with the construct that first reads its result. Shapes: `reinterpret`,
  `sign_extend`, `zero_extend`, `truncate` (literal casts), `bool_compare` (a
  two-literal binary producing `bool`), `other` (unary, non-boolean binary,
  identities). Consumers: `store` (a store row or an SSA store), `aggregate`
  (array/aggregate construction, which includes initializers), `return`,
  `call`, `branch` (conditional branch or switch), `operator` (cast, unary or
  binary), `other`, `unused`.
- **Function size.** `fold_size_<bucket>_{functions,rows,folds,dead_literals}`
  buckets functions by committed rows: `rows_lt_64`, `rows_lt_256`,
  `rows_lt_1024`, `rows_lt_4096`, `rows_ge_4096`.
- **Net account.** These are object counts, not bytes multiplied by a presumed
  size.
  - `fold_net_constant_rows` and `fold_net_identity_rows` partition the folded
    rows.
  - `fold_net_operand_slots` counts the operand slots those rows filled. Each
    row's slice is its own arena request in the C builder.
  - `fold_net_literal_uses` counts the literal reads that folding removes; an
    identity's forwarded operand is not counted.
  - `fold_net_dead_literals` counts literals left with no reader. Each would
    need no row, value or immediate request.
  - `fold_net_adjacent_dead_literals` counts the dead literals committed directly
    before their folded reader in the same block. Those could be rewritten in
    place.

  Rows, values and source ranges come from per-unit streams trimmed to the
  final count (`c_ir_row_streams_trim`). So the rows and values a birth fold
  saves free stream slots, not arena requests. In that model:
  - Rows and values not built: `identity_rows + dead_literals`.
  - Operand-slice requests saved: `constant_rows + identity_rows`.
  - Immediate requests at construction: `constant_rows - dead_literals` more,
    since each folded constant needs one.
- **Late side.** Each `fold_late_*_constant` is one FAST immediate request, and
  `fold_late_dce_constants` counts `CONSTANT_INTEGER` rows FAST DCE deleted
  (literals and folded rows alike). Promotion compacts rows before FAST runs,
  so this census cannot tell which late requests or deletions belong to
  birth-foldable rows. They only bound what a birth fold would also save:
  - FAST immediates spared: at most `min(fold_net_constant_rows, Σ fold_late_*_constant)`.
  - DCE deletions spared: at most `min(fold_net_dead_literals, fold_late_dce_constants)`.

  The late folds themselves are attributed per row by the
  [matched association](#matched-birthlate-association):
  `fold_match_direct_*` plus `fold_match_cascade_*` are exactly the late folds
  a birth fold would replace. The immediates and DCE deletions remain bounds.
  Whether FAST would skip a whole function's compaction is not measured.

Scratch is a per-thread arena created on first use and carved with the raw
arena primitive. The census therefore adds no `allocation.*` request and does
not move any other arena's dirty prefix. The arena's reservation does appear
among OS events in the exit census, and its pages in minor-fault counts. A
function with more than 2^25 values is skipped and counted in
`fold_net_skipped_functions`.

## Opt-in block-local CSE (#3011)

`ir_fast_cse` is an experiment, outside every optimization level and outside
`IR_FAST_ALL`. It walks each block's linked instruction chain once, after
`fold`/`address` and before `dce`, and keys two row shapes:

- `BINARY` add, subtract, multiply, and, or, xor whose result and both resolved
  operands share one unsigned integer type of 1..64 bits. Add, multiply and
  the bitwise operations order their operands; subtract does not.
- Nonnegative single-limb `CONSTANT_INTEGER` rows of such a type, keyed by
  their immediate bits.

The key is the operation, canonical type and resolved operand roots (or
immediate bits); a match compares the whole key, then requires the same
category, alignment and read-only/volatile facts as the folding replacement.
Signed, Boolean, enum, wider, pointer, floating, place, volatile and atomic
values, loads, calls, division/remainder, shifts and SIMD are never keyed.
`ir_instruction_is_pure` is not consulted as a CSE certificate.

A hit records a replacement in the shared map and marks the row removed; the
existing single compaction deletes it. Nothing moves between blocks: only an
earlier row of the same block can be a representative. One 256-slot table of
compact integer entries serves the whole module preparation. Each block takes
a new generation tag instead of clearing it, and a wrapped counter clears the
table and restarts at generation 1. A block admits at most 192 entries and a
lookup probes at most eight slots. A full table or exhausted probe sequence
declines insertion and leaves the row unchanged. A replacement only joins
equal values, so an operand replaced by a block walked later leaves earlier
hits sound; that block's entries have already expired. The table adds a constant 7 KiB to
scratch, recorded as `table_bytes`, not to the per-function bound.

`-fcanonical-fast-cse -v` prints an `IR_FAST_CSE` record. It reports blocks,
candidates, hits, commuted, constant and window hits, probes, collisions,
metadata and saturation declines, initialized table bytes and the largest
block candidate count. A window hit is a hit at most eight candidates after
its matched entry's recorded ordinal. The two strategies record that ordinal
differently. A table entry keeps the representative's first occurrence. A
window slot is refreshed by every matching occurrence, so its distance runs
from the previous occurrence, and the representative can be older; for ten
repeats of one key, the last hit has distance 1. Neither count bounds a
representative's age or live range.

`-fcanonical-fast-cse-strategy=window` replaces the table lookup with the
issue's linear arm. Each candidate is compared, newest first, with the
previous eight keyed candidates of its block, kept in a ring in the table's
first slots. Every candidate, hit or miss, then takes the oldest slot. After a
hit the slot records the representative, so repeated hits age the window
like any other row. The arm never saturates; `probes` counts its comparisons. `=table` (the default) restores the hash table. The strategy
changes nothing unless `cse` is selected.

### Three-arm census (diagnostic, not performance evidence)

These historical results were measured at `c29b794b`, before the merge of
main. They come from a hosted 4-vCPU container, using a Clang-built Release
`ide` with the default four passes, alone or plus `cse` in either strategy.
The self-host input is `ide.c` with tests off and `-g -march=native`, using
`-ftime-canonical-fast -v`. Each arm ran 7 times, interleaved; the table
shows medians and the wall range.

| Self-host arm | Wall s (min-max) | Peak RSS KiB | `cse` ms | Compact ms | Hits (constant) | Probes | Rows after FAST | `.text` bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| No CSE | 6.050 (5.689-7.470) | 846,064 | 0 | 56.8 | - | - | 1,810,089 | 28,401,706 |
| Linear window (8) | 5.961 (5.586-6.679) | 844,200 | 20.7 | 55.7 | 90,459 (90,424) | 451,184 | 1,721,599 | 28,148,631 |
| Table (256 slots) | 6.005 (5.716-6.404) | 844,552 | 20.6 | 57.3 | 94,824 (94,787) | 277,792 | 1,717,421 | 28,149,050 |

| Smaller workload | Candidates | Table hits | Window hits | Arithmetic hits | Table saturation declines |
| --- | ---: | ---: | ---: | ---: | ---: |
| `tests/basic_c_operations.c` | 506 | 98 | 85 | 0 | 0 |
| `tests/c_abi_cfuncs.c` | 1,224 | 116 | 90 | 0 | 1 |
| `tests/basic_c_machine_alias.c` | 645 | 3 | 3 | 0 | 42 |

Both arms remove about 5% of canonical rows after FAST and 0.9% of `.text` on
self-host. A compiler built with either arm recompiles itself with the same
arm to byte-identical output. What the data show:

- **Arithmetic arm as framed:** after frontend SSA and folding, repeated
  unsigned arithmetic in one block almost never survives. Self-host has 37
  arithmetic hits under the table and 35 under the window; the other
  workloads have none.
- **Constants:** almost every hit is a repeated constant.
- **Window versus table:** the window catches 95% of the table's self-host
  hits and 78-100% elsewhere, with no table-sizing or saturation state, but
  makes 1.6x the probes. Both arms report about the same pass time.
- **Compile time and memory:** hosted wall-time differences are inside the
  run-to-run range. Peak RSS falls by about 1.5-1.9 MiB in both arms. Neither
  effect is established here.

So this census answers the recurrence question and makes the cost comparison
diagnostically. It does not complete #3011's first gate, which needs the
matched Zen 5 A/A and A/B comparison (#2761). Because hits concentrate in
constants, the next section measures a construction-time constant cache
against the late pass.

## Construction-time constant interning (#3011)

C lowering reuses an integer constant already built earlier in the current
block instead of building another one. This is on by default;
`-fno-canonical-intern-constants` turns it off and
`-fcanonical-intern-constants` turns it back on, the last one given winning.
It is independent of every `-O` level and FAST control. It was opt-in until
the Zen 5 measurement below justified making it the default.
`c_ir_emit_integer_value_at` looks up a 64-slot direct-mapped table shared by
one lowering. Each function builder takes a fresh generation, so no entry
crosses a function and none is cleared. The key is the canonical type, the
canonicalized immediate bits and the sign flag.

An entry is only a hint. Lowering can retract a row
(`ir_block_retract_tail`) or truncate a block's tail
(`ir_block_truncate_after`) and then reuse the freed row and value IDs. So a
hit must re-prove all of the following:

- the cached value is still defined by its cached row;
- the row is still the same `CONSTANT_INTEGER`;
- the row lies in the builder's current block.

A hit allocates no row, value or immediate. A failed proof builds the row as
before and is counted as `stale`. Global initializers and the test hooks have
no table. `-v` prints `C_CONSTANT_INTERN lookups hits stale`.

### Four-arm comparison (diagnostic, not performance evidence)

Measured on a hosted 4-vCPU container with a Clang-built Release `ide`
(production code as in the commit that adds this section). The compile is
self-host `ide.c` with tests off, `-g -march=native`, the default four FAST
passes and `-v`. Compile wall time and peak RSS are medians of 7 interleaved
runs per arm. Spill, reload and push counts come from `-v` and
`llvm-objdump` of each object.

| Arm | Wall s (min-max) | Peak RSS KiB | Rows into FAST | Rows after FAST | `.text` bytes | Spills | Reloads | Remats | `pushq` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| None | 5.299 (5.177-5.603) | 854,084 | 2,026,447 | 1,822,939 | 28,520,268 | 30,135 | 53,991 | 49,886 | 116,050 |
| Interning | 5.292 (5.118-5.911) | 835,708 | 1,859,956 | 1,740,671 | 28,298,232 | 30,040 | 53,191 | 29,619 | 116,504 |
| CSE table | 5.302 (5.130-5.692) | 852,412 | 2,026,447 | 1,729,373 | 28,265,508 | 31,786 | 54,829 | 35,302 | 116,720 |
| Interning + table | 5.298 (5.143-5.396) | 835,256 | 1,859,956 | 1,716,648 | 28,259,145 | 31,938 | 54,973 | 33,319 | 116,811 |

Interning reports 421,116 lookups, 166,402 hits and 75 stale entries. With
interning, CSE still finds 25,384 hits, 25,347 of them constants that only
become equal after folding. The CSE pass costs about 20 ms per compile;
interning has no separate pass.

Generated-code runtime: each arm's object was linked into a compiler. Each
compiler then compiled the same input with default flags, 5 interleaved
runs per arm.

| Compiler built with | Wall s (min-max) | Peak RSS KiB |
| --- | ---: | ---: |
| None | 24.308 (23.678-25.540) | 860,524 |
| Interning | 24.103 (22.971-25.238) | 860,248 |
| CSE table | 23.551 (23.546-25.524) | 860,080 |
| Interning + table | 24.330 (23.656-25.816) | 859,976 |

All four compilers produced byte-identical objects. The compilers built with
interning, and with interning plus CSE, each recompile themselves with the
same flags to byte-identical output.

What the data show:

- **Rows and memory:** interning removes 8.2% of the rows entering FAST
  before they are allocated. Peak RSS falls by about 18 MiB (2.2%), against
  about 2 MiB for the late CSE pass.
- **Code size:** after FAST, interning leaves 4.5% fewer rows and 0.78% less
  `.text`. The late table gets 5.1% and 0.89%, because folding exposes about
  25k further equal constants.
- **Spills:** the late table raises spills by 5.5% and reloads by 1.6%.
  Interning leaves both slightly lower. Both extend constant live ranges
  across calls in the same block. That shows as more callee-saved pushes:
  +0.4% with interning and +0.6% with the table. In a small Windows module
  that change shows up directly in hand-checked frames.
- **Time:** compile-time and generated-runtime differences are inside the
  hosted run-to-run range. Neither is established here.

Interning is therefore the cheaper way to obtain most of the constant reuse,
with less memory and without the spill growth. The fixed report-only profile
`profile: constant-reuse-acceptance-v1` measures both flags on the 9700X; its
method is in [benchmarking.md](agents/benchmarking.md).

### Zen 5 measurement (9700X)

The profile ran on main `446f742e`, check
[115128610735](https://github.com/buster14a/buster/runs/115128610735). It used
one binary on the `-g0` unity `-c` self-host compile, with 20 ABBA pairs per
cell. Ratios are B (flag) over A (default):

| Flag | Wall [95% CI] | Retired instructions | Peak RSS | Code bytes |
| --- | --- | --- | --- | --- |
| `-fcanonical-intern-constants` | 0.9932 [0.9912, 0.9954] | 0.9868 | −1.81% | −1.62% |
| `-fcanonical-fast-cse` | 1.0024 [1.0007, 1.0034] | 0.9957 | −0.47% | −1.96% |

Both flags left diagnostics identical to the default. Interning is a
detectable compile-time win, while the late table is a detectable cost that
buys 0.34 points more code reduction. Interning is the only adoption candidate
under the throughput priority. On that measurement it became the default; the
late CSE table stays opt-in. Making interning the default also updated the C
frontend's row-capacity, codegen-shape and CSE-count tests, which had
expected the old default. The constant-reuse profile's `intern` cell passes
`-fcanonical-intern-constants` to B, so after this change both of its
variants intern and the cell no longer measures the arm.

## Opt-in construction-time folding (#3010)

`-fcanonical-construction-fold` (cleared by
`-fno-canonical-construction-fold`, the last one given winning) is the bounded
experiment that the [fold census](#construction-time-fold-census-3010) pointed
to. It is off by default and independent of every `-O` level and FAST control.
It touches only the two C-lowering helpers where the census found birth folds:

- `c_ir_emit_cast`, for an integer sign-extend, zero-extend, truncate or
  reinterpret;
- `c_ir_truth_value`, for the `_Bool` conversion of an integer.

When the operand is a committed single-limb `CONSTANT_INTEGER`, the helper
describes the row it would build and asks `ir_fast_fold_candidate`, which
calls `ir_fast_fold_rule` exactly as `ir_fast_fold` does. If that is a
constant rule, the helper builds that constant through
`c_ir_emit_integer_value_at` instead of the `CAST` or `BINARY` row. A truth
test is the candidate `value != 0`: the zero is supplied as bits, and the
operand supplies its type, as the converted zero would. So a folded truth
also builds no zero literal and no conversion of it. Anything else builds
the rows as before:

- an identity rule;
- an unfoldable row;
- a type wider than 64 bits;
- a volatile or atomic type;
- floating or pointer operands;
- an operand whose `points_to_read_only` or label provenance the row's result
  would have inherited.

The constant's value then has the metadata the late fold keeps, and
`ir_fast_fold` still folds every row this leaves.

The operand's own literal row is still built and stays for FAST DCE when it
has no other reader. Operands are evaluated as before, since only an
already-built constant is read. `-fno-canonical-fast` keeps its meaning:
FAST's admission, validation and late folding are unchanged, and the
construction fold runs whether or not FAST does. `-v` prints
`C_CONSTRUCTION_FOLD casts truths`. In `BUSTER_BENCH_ALLOCATIONS` builds, a
row this experiment never builds is never seen by the commit census. So the
`fold_birth_*` and `fold_match_direct_*` counters drop for the rows it folds.

## Tests

`ir_test_inline_required_work_budget` exercises 400 required sites and 16
assignment statements. Its actual copied-row floor for the old repeated-
projection formula exceeds the unchanged 4 Mi work allowance, while the full
retained-storage estimate remains subject to its unchanged cap. The explicit
399-site case confirms that the configured site limit still refuses expansion.
Its 5000-site flat caller uses a leaf with no body statements and derives a
prefix-search lower bound above twice the base allowance before confirming
refusal at the existing per-caller work cap.
`ir_test_canonical_inline` continues to cover mandatory cycles and the
explicit growth, storage, shape and site-limit refusals.

`ir_fast_test.c` checks all 16 subsets structurally, transitive dead work,
retained calls/memory effects, existing trivial parameters, repeat preparation,
classification exclusions and budget refusal before value-storage access.
`ir_fast_intern_tests` checks that interning shares a repeated constant
within a block, never across blocks, types or signs, and that each hit is
one row and one value never built. `ir_fast_construction_fold_tests` checks, with
construction folding on and off, the widths, signedness, wrapping and `_Bool`
cases, an effectful comma operand, and unchanged non-constant, 128-bit and
floating operands. Each folded case returns the constant FAST folds to
without its row; diagnostic builds also check that no birth fold remains. A
direct check rejects a volatile result type through `ir_fast_fold_candidate`. The driver test runs the shared fixture
with interning and CSE, and with construction folding and no FAST pass, on
every backend.
`ir_fast_cse_tests` runs each case under both strategies and checks the
window boundary for distinct rows and for repeated hits. The cases cover
commuted and ordered pairs, duplicate operands, block boundaries, excluded
signed, load, division and shift rows, and roots resolved by folding. Other
tests cover constants, table saturation and collisions, metadata declines,
volatile rows and generation rollover.
`driver_fast_test.c` executes the fixed-result C fixture for all 16 subsets in
FAST/QUALITY with zero fallback on desktop hosts, plus the full recipe with
`cse`. The native-retirement census freezes every tracked file under `tests/`,
so the executable CSE control is source that `driver_fast_test.c` writes to a
temporary file. It runs natively under both allocators and must report exactly
three commuted arithmetic hits and one constant hit. Android/iOS retain all 32
native object-generation checks; their application process cannot launch
generated executables. The registered native-level regression checks all
accepted spellings, explicit override ordering, invalid levels, all four
numeric levels under both allocators, skipped pass work at `-O0`, distinct
native text sections, and fixed-result execution on desktop hosts. Each subset also compiles to
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

Tiny-call expansion is opt-in: `-fcanonical-inline` selects it and
`-fno-canonical-inline` clears it, the last of the two winning; optimization
levels never change it (see `docs/agents/driver.md` for the measurement behind
that default). Source `always_inline` requests remain independent of that
selection.

Optional candidates are same-module, noninterposable single-block leaves with
compatible fixed-prototype signatures. Target-aware C lowering permits strong
external definitions on Windows COFF, while default-visible ELF definitions
remain interposable and weak/bodyless symbols remain ineligible. Canonical IR
embedders start with the conservative interposition policy.
Mandatory candidates can contain
multiple blocks, local storage, ordinary calls and aggregate returns.
The iterative mandatory-call graph processes callees before callers and
rejects cycles. Caller-owned computed-goto label values and destinations retain
their IDs across a splice; computed labels inside the callee remain refused
until their provenance has a cloning contract (#3995). Dynamic stack lifetime operations, callee-owned computed labels,
variadic bodies, returns-twice calls and unsupported ABI shapes are refused.
Unsupported candidates remain calls when optional and diagnose when mandatory.
Arguments are evaluated once before the splice. Expansion remaps canonical
values, blocks, locals, debug scopes, source ranges, instruction extras and incoming edges.
Expansion retains the original out-of-line definition, including when its
address is observed. Cloned instruction ranges preserve supported source line
information; the debug model does not gain inline call-stack records.

Positive decimal limits are configurable through
`-fcanonical-inline-max-callee=N` (default 16 canonical instructions),
`-fcanonical-inline-function-growth=N` (optional default 2048 copied rows per caller),
`-fcanonical-inline-module-growth=N` (optional default: the larger of 4096 copied rows
and 1/32 of the module's lowered canonical instruction rows), and
`-fcanonical-inline-call-sites=N` (optional default 64 sites per caller).
The fixed 4096-row module default stopped optional expansion of the unity
self-host source after 243 sites, because callers are visited in module
order; per-caller limits refused only 54 further sites once the module limit
was raised (#48). An explicit module limit is absolute.
A caller keeps its optional splices only when their planned row growth,
shifted left by `IR_INLINE_CALLER_GROWTH_SHIFT` (5), reaches its instruction
rows; otherwise every optional site in it stays a call. The first splice
relocates the caller's exactly sized tables and the arena keeps the old copy,
so one tiny site in a large caller costs more memory than it saves. The pass
hands the complete pages of every abandoned table back to the OS
(`ir_inline_cfg_discard_record`, `os_discard`); the range stays mapped and
writable. It holds one open run per table kind: because the C frontend carves
each kind out of one row stream in module order and callers are visited in that
order, a relocated caller's range usually extends the run (freeing the page two
neighbours share), and any other range first releases the run. The last runs
are released after the final splice; the record takes no planning scratch.
The verbose record reports the released bytes (#3854). The shift measurements
below predate this release. On the
unity self-host source with `-fcanonical-inline`, shifts 2 through 6 kept 801,
1643, 2639, 3462 and 3739 of 3984 optional sites for +5, +11, +25, +42 and
+52 MB of peak RSS over tiny-off (unbounded: +80 MB). Generated stage-2
self-host instructions under cachegrind fell 0.47%, 1.00% and 1.32% at shifts 3,
4 and 5 (unbounded: 1.31%), so 5 keeps the measured benefit at about half the
memory. Diagnostic only; 9700X qualification is separate. Mandatory sites are never
dropped. `-fcanonical-inline-caller-growth-shift=N` and
`IrInlineOptions.caller_growth_shift` override the shift (a zero API field
selects the default, 32 or more keeps every planned site).
The optional caller default stays 2048 rows, twice the 64-site times 16-row
tiny-body budget it was sized from.
Required intrinsic wrappers can exceed both optional row caps (the RAD AVX-512
probe copies 6318 rows), so zero function/module row and call-site limits leave
required expansion to the independent work and storage guards. Explicit
row/site limits cap both phases and still produce a source-mapped refusal.
Zero row/site limits remain zero in embedding options across modules;
other zero numeric fields normalize to their named defaults.
Mandatory calls are processed before optional tiny candidates across the module.
Their growth and sites count against any subsequent optional allowance.
Mandatory graph planning retains its module-wide work and scratch bounds.
Required splicing keeps the fixed 4 Mi work-unit and 16 MiB retained-storage
limits per caller, plus one input-proportional relocation of the caller's
existing tables and local/debug snapshots. The required work charge follows
the splice loops: two call-block prefix walks (three for an aggregate result),
the moved-tail target walk and its successor phi repairs, the output proof's
caller-phi scans once per caller and once more for each spliced original block,
the caller's debug-row preflight once per caller, the marginal caller reservation,
and each callee clone/output payload once. Callee debug locals are charged for
their two support checks and clone, and callee debug scopes for their planning
and splice checks.
The work estimate covers the production splice and proof path; builds with
`BUSTER_IR_TRANSFORM_CHECKS` also run their existing full canonical validation
after each splice batch, outside that admission estimate.
Earlier planned calls
also walk the one-edge branch to the prior callee entry. The pass does not
recharge every existing caller block, unrelated phi list or prior clone at each
required site. Planning scratch remains bounded at 16 MiB, copied rows retain
the independent 4 Mi module-wide ceiling, and explicit row/site limits and
cycle/shape refusals still apply.

A per-callee census of the unity self-host source (#3854, before the syntax
tree was built on every compile) found the following. Most optional callees are one-line field predicates
that lower to 10-16 promoted rows (arguments, address, load, casts, compare,
return); a limit of 10 or less keeps almost none of them. One 11-row callee,
`c_token_is_punctuator`, took 2072 of 3586 spliced sites and 55% of the row
growth, and 409 M of the generated compiler's 1.65 G calls to optional
callees when it self-hosts. A 13-row shape test added 25 M calls from 4 sites.
The 12-row band (184 sites) and the 14-16-row band (337 sites, 4224 rows)
together took about 3 M calls: mostly backends an x86-64 self-host never
runs. Row shape does not separate those from the hot predicates, so no
sharper static rule was found. Under cachegrind at `-march=x86-64-v3`
(diagnostic only; 9700X qualification is separate), against tiny-off:

| Callee limit, caller shift | Compiler instructions with the flags | Generated stage-2 instructions | `.text` growth |
|---|---|---|---|
| 16, 5 (default) | 1.0106 | 0.9853 | +0.76% |
| 13, 5 | 1.0097 | 0.9845 | +0.65% |
| 12, 5 | 1.0096 | 0.9861 | +0.63% |
| 11, 5 | 1.0090 | 0.9855 | +0.59% |
| 11, 4 | 1.0061 | 0.9878 | +0.43% |
| 11, 6 | 1.0103 | 0.9862 | +0.63% |
| 13, 4 | 1.0067 | 0.9869 | +0.48% |
| 13, 6 | 1.0111 | 0.9852 | +0.69% |

The first column is a fixed no-inline compiler compiling the unity source with
each setting; the second runs that setting's own stage-1 compiler with the
same flags. On that tree 13 rows improved on 16 in every column. At 13 rows,
shift 4 cut the compiler's added instructions by a third but kept only 89% of
the stage-2 saving; shift 6 was worse than 5 in every column.

The 13-row default (#4044) was reverted. Once #4089 built the syntax tree on
every compile, its 14-row `c_ast_is_punctuator` (165 sites) and the 16-row
`c_punctuator_in_set` became hot in the self-host, so the "dead 14-16-row
band" premise no longer held. The 9700X `canonical-inline-self-host-v1`
receipt for 13/5 at `e4d15cbc` (run 38434056637) measured stage-1 wall
1.0251, instructions 1.0115 and peak RSS 1.0136; stage-2 wall 1.0011 (no
detectable difference), instructions 0.9924 and peak RSS 1.0155; code bytes
1.0071. The #3877 receipt for 16/5 measured stage-1 wall 1.0260, but on an
older tree, so the two 9700X runs are not a controlled comparison of callee
limits. On the `e4d15cbc` tree, a diagnostic cachegrind sweep gave stage-2
instructions of 0.9925 at 13 rows, 0.9919 at 14 and 0.9909 at 16 (`.text`
+0.71%, +0.76%, +0.82%). So 16 rows executes about 0.16% fewer stage-2
instructions than 13, at slightly larger code size. Stage-1 wall time and peak
RSS for the restored 16-row setting remain unmeasured on the current tree. A
static size cut is also tuned to one workload: the 14-16-row band went from
never executed to hot when the workload changed.

Optional selection, like FAST, bounds work and retained storage per caller:
splicing into a caller copies its exactly sized tables, so module-wide byte or
work totals let the first few large callers starve every later one. Optional
expansion across the module stays bounded by the module row-growth limit.
The mandatory graph captures
call sites in one bounded scan rather than
rescanning every instruction to build reverse edges. That scan skips a
function whose producer opcode summary records no `CALL`
(`IR_OPCODE_SUMMARY_TRACKED`) and streams the dense rows of the rest. Planning
classifies every call row with one byte of per-symbol callee facts
(`ir_inline_plan_facts`: required, `noinline`, linkage-allowed, tiny leaf),
filled once per pass, instead of re-deriving the symbol and callee predicates
at every site. The table adds one byte per symbol to the planning scratch
charge; work charges and statistics are unchanged. Planning storage includes
function metadata, captured edge chunks and reverse-edge arrays. Caller planning counts block tails and phi metadata, with instruction prefixes
recorded during the candidate scan. Splices preserve existing IDs and repair block
chains directly, so no full caller compaction is required. Each caller has one
splice reservation: the first splice that adds locals, debug locals or debug scopes
widens every block snapshot and the caller's place/debug arrays once to the
planned final counts, later splices write in place, and predecessor lists are
rebuilt once after the caller's last splice. Planning charges a site the increase
in that reservation's bytes and work, plus the callee payload, instead of the
caller-wide widening per site. Other repeated splice work stays charged per site. Predecessor work uses original and projected target
counts rather than unrelated instruction/value payload counts; call membership
checks stop when they reach the call. Growing table storage is charged from the caller's actual
capacities through projected appends, including geometric growth floors and
alignment; that charge is an upper bound, because before a caller's first splice
`ir_inline_cfg_reserve_tables` grows its block, instruction, value and extra
tables once to the planned final counts instead of doubling them. Relocating a
touched caller's exactly sized frontend tables still retains the old copy in the
arena: on the unity self-host source, tiny inlining touches 809 callers whose
tables total 65.5 MB, which is most of its peak-RSS cost (#48). At the default
shift, 639 table relocations abandon 34.6 MB, of which 29.3 MB lies in complete
pages that the pass now releases; a local stage-1 tiny-on/off peak-RSS ratio
fell from 1.049 to 1.019 (diagnostic, #3854). Callee payloads
and clone maps remain charged per splice.
Tiny bodies are screened before payload scans. Mandatory expansion is still
subject to resource guards,
including bounded planning work and retained cloning storage. Expansion checks
strict canonical validity per function, never by a module-wide scan: each
candidate's callee once before it is read, each mandatory candidate's caller
likewise, an optional candidate's caller once its planned sites survive the
caller relocation bound and before its first splice (so a large caller that
the bound keeps as calls is never checked by this pass, #3854), and each
spliced caller again after its last splice (an output failure reports the
`inlining output` boundary). The strict validator does not check debug rows,
so the planner checks a caller's debug-local scopes and scope parents once
rather than once per site. A mandatory call whose caller or callee is a legacy producer-certified
shape that fails that check is diagnosed; an optional one stays a call (all of
an invalid caller's planned optional sites count as shape skips).

Every spliced caller was proven valid just before its splices, so its output
check, `ir_validate_canonical_splice`, walks only what they rewrote (#3854).
Before the first splice the pass records the original blocks that hold
planned sites, their rows and the values those rows and the blocks'
parameters define (`IrValidationSplice`). The check then reuses the full
validator's per-block step on those blocks and every new block, marking every
other row as already owned and every other old value as already defined, so a
rewritten chain that reaches an untouched row, or a rewritten row that also
defines an untouched value, is refused. It requires the rewritten chains to
reach exactly the recorded rows plus every new row, rechecks the parameters
of every block (predecessor lists are rebuilt for all of them) and sweeps
only recorded and new values. Rows outside the rewritten blocks keep their
verdict because a row check reads only its own row, its operands' records,
counts that only grow, and three facts about an operand's defining row; the
argument is spelled out above `ir_validate_canonical_splice`. Label metadata
or a published CFG makes the check refuse, as does a spliced call result of
pointer-to-function type left without a defining row, the one rewrite that
changes an outside row's verdict (#3988).

The proof's markers also follow that region (#4251). Small old regions use
immutable radix-sorted ID indexes, binary lookup and marker bytes for those IDs
plus the appended suffix. Missing old IDs implicitly retain their owned or
parameter-defined state. A dense prefix is used only when it is at most four
times the recorded old region, so setup remains bounded by relevant IDs. The
inliner's existing pre-splice scan records all original parameter-bearing
blocks; the proof checks that list instead of rediscovering it across every
old block. Splices add parameters only on new blocks, which the row walk checks.

The check may refuse a valid caller but never accepts one the full validator
refuses, provided the splices wrote only what was recorded. A production
build trusts that write set. When the check refuses, the full validator
decides and names the location, and a checked build
(`BUSTER_IR_TRANSFORM_CHECKS`) also runs the full validator after every splice
batch, so a write outside the recorded set still fails there. `IR_INLINE`
reports `splice_proofs` and `splice_fallbacks`. Since every rewritten caller
is checked at once, a module the input check had already proven stays proven.

When FAST will run, the pass publishes one byte per function in
`module->inline_proven` for every function it proved and left in that state:
callees, callers that kept all their sites as calls, and spliced callers.
Promotion clears the byte of a function it rewrites. The FAST input guard
skips the per-function check of a marked function (`reused_proofs`) but still
checks the shared scope, and preparation drops the table before it returns.
A legacy caller that failed its check is never marked, so the guard checks it
and keeps its FAST decline. On the unity self-host source, under local
callgrind with a tests-off `-march=x86-64-v3` build (diagnostic, not a
performance result), the tiny-on stage-1 compile proves 661 spliced callers
with no fallback, reuses 721 proofs and retires 85.1 M fewer instructions
(26.154 G to 26.069 G, -0.33%). The default compile, whose 70 mandatory
sites take the same path, retires 30.1 M fewer (25.768 G to 25.738 G). The
object bytes are unchanged in both. Both checks share one walk
(`ir_validate_function_walk`): two walks over shared helpers cost the default
compile 11.6 M to 16.8 M more instructions, because the leaf validators then
had two callers and were no longer inlined. The verbose
`IR_INLINE` record reports candidates, accepted/mandatory calls, copied rows,
growth, rejection categories, visits, released relocation bytes, splice proofs
and fallbacks, and reused proofs. These counts are diagnostics, not a
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
