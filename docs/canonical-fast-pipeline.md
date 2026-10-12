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
  before them. `fold_birth_metadata_mismatches` counts identities rejected by
  the replacement metadata check. `fold_birth_avoidable_bytes` is logical
  row/value/operand/source storage: an identity needs none of it, while a
  constant still needs its row and immediate. This is not an arena-call or RSS
  figure.
- `fold_late_<rule>`: folds FAST actually applied. `fold_late_replaced_roots`
  counts the folds whose operand had to be followed through the replacement map
  (an earlier identity or a parameter sweep).

The rules are `cast_identity`, `cast_constant`, `unary_constant`,
`binary_constant`, `binary_right_identity` and `binary_left_identity`. Probes
suppress `ir_semantics` recording, so those counters keep measuring only the
compiler's own semantic work. Rows that SSA construction inserts behind a commit
(`ir_block_insert_instruction_after`) are not probed, and neither is
inlined-callee output.

`fold_birth_*` and `fold_late_*` count independent populations. Birth counts
include functions that FAST later skips or does not run (`-fno-canonical-fast`,
budget and provenance skips) and rows that later passes delete. Late counts
include rows that SSA insertion, promotion and inlining add after commit. No
per-row birth classification reaches FAST, so neither a birth/late ratio nor
late minus birth attributes or bounds the late folds that were available at
commit; that needs a matched per-row association.

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
constants, a construction-time constant cache (#3010) remains a cheaper
candidate that neither arm measures. The pass stays opt-in. Adopting either
arm, or that alternative, also needs allocation, spill and runtime data.

## Tests

`ir_fast_test.c` checks all 16 subsets structurally, transitive dead work,
retained calls/memory effects, existing trivial parameters, repeat preparation,
classification exclusions and budget refusal before value-storage access.
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
rejects cycles. Dynamic stack lifetime operations, computed labels,
variadic bodies, returns-twice calls and unsupported ABI shapes are refused.
Unsupported candidates remain calls when optional and diagnose when mandatory.
Arguments are evaluated once before the splice. Expansion remaps canonical
values, blocks, locals, debug scopes, source ranges, instruction extras and incoming edges.
Expansion retains the original out-of-line definition, including when its
address is observed. Cloned instruction ranges preserve supported source line
information; the debug model does not gain inline call-stack records.

Positive decimal limits are configurable through
`-fcanonical-inline-max-callee=N` (default 16 canonical instructions),
`-fcanonical-inline-function-growth=N` (default 2048 copied rows per caller),
`-fcanonical-inline-module-growth=N` (default: the larger of 4096 copied rows
and 1/32 of the module's lowered canonical instruction rows), and
`-fcanonical-inline-call-sites=N` (default 64 sites per caller).
The fixed 4096-row module default stopped optional expansion of the unity
self-host source after 243 sites, because callers are visited in module
order; per-caller limits refused only 54 further sites once the module limit
was raised (#48). An explicit module limit is absolute.
A caller keeps its optional splices only when their planned row growth,
shifted left by `IR_INLINE_CALLER_GROWTH_SHIFT` (5), reaches its instruction
rows; otherwise every optional site in it stays a call. The first splice
relocates the caller's exactly sized tables and the arena keeps the old copy,
so one tiny site in a large caller costs more memory than it saves. On the
unity self-host source with `-fcanonical-inline`, shifts 2 through 6 kept 801,
1643, 2639, 3462 and 3739 of 3984 optional sites for +5, +11, +25, +42 and
+52 MB of peak RSS over tiny-off (unbounded: +80 MB). Generated stage-2
self-host instructions under cachegrind fell 0.47%, 1.00% and 1.32% at shifts 3,
4 and 5 (unbounded: 1.31%), so 5 keeps the measured benefit at about half the
memory. Diagnostic only; 9700X qualification is separate. Mandatory sites are never
dropped. `-fcanonical-inline-caller-growth-shift=N` and
`IrInlineOptions.caller_growth_shift` override the shift (a zero API field
selects the default, 32 or more keeps every planned site).
The caller default is twice the 64-site times 16-row tiny-body budget. This leaves
headroom for larger mandatory checked-arithmetic expansions; the module, work
and storage guards still independently bound expansion.
Zero numeric fields in embedding options normalize to those defaults.
Mandatory calls are processed before optional tiny candidates across the module;
both phases share the same row-growth, call-site and per-caller limits. Mandatory
planning and splicing share one module-wide work and retained-storage budget.
Optional selection, like FAST, bounds work and retained storage per caller:
splicing into a caller copies its exactly sized tables, so module-wide byte or
work totals let the first few large callers starve every later one. Optional
expansion across the module stays bounded by the module row-growth limit.
The mandatory graph captures
call sites in one bounded scan rather than
rescanning every instruction to build reverse edges. Planning storage includes
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
tables total 65.5 MB, which is most of its peak-RSS cost (#48). Callee payloads
and clone maps remain charged per splice.
Tiny bodies are screened before payload scans. Mandatory expansion is still
subject to resource guards,
including bounded planning work and retained cloning storage. Expansion checks
strict canonical validity per function, never by a module-wide scan: each
candidate's caller and callee once before they are read, and each spliced caller
again after its last splice (an output failure reports the `inlining output`
boundary). A mandatory call whose caller or callee is a legacy producer-certified
shape that fails that check is diagnosed; an optional one stays a call. When FAST
is selected, a caller whose splices were all optional skips that output check:
it is listed in `inline_unchecked_callers` and proven by the validation that
follows anyway (the promotion output check of a checked build, else the FAST
input guard's per-function check). A listed caller that fails reports the
`inlining output` boundary instead of a FAST decline; a shared-scope failure is
attributed to it only if its own rows also fail, and failures outside the list
keep their legacy decline. This removed 0.87 G of 3.4 G inliner instructions
from an uncapped tiny self-host compile under cachegrind (#48). Any other
rewritten caller is checked at once, so the FAST input guard still checks all
functions on a producer-certified default compile. The verbose
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
