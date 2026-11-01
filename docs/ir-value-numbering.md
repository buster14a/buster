# SSA value numbering

`ir_optimize_gvn.c` is the value-numbering stage for
[#139](https://github.com/buster14a/buster/issues/139). Include it after
`ir_optimize.c`. Call `ir_optimize_gvn(function, output, scratch, options,
result)` with `ir_gvn_options()`. The result is an `IrCheck` and a fresh verified
`IrFunction` in `IrGvnResult.function`. The input, arena independence,
transactional rollback and explicit entry-phi refusal follow the
[optimization contract](ir-optimization.md). Each stage rebuilds dominance
after rewriting; no stale CFG or use list is published.

The vector path starts with opcode, type, flags and semantic metadata classes,
then repeatedly partitions each class by the classes of its operands. An old
class is part of the next key, so refinement only splits. Phis compare their
incoming values in CFG predecessor order, regardless of the side-table order.
They may establish equivalent cyclic recurrences. Refinement ends when no
class splits. After `sweep_limit` rounds (64 by default), scalar hashing and comparison sorting
complete the same fixed point; a limit never authorizes an unfinished
optimistic substitution.

Eight contiguous `uint64_t` key columns feed two eight-lane hash groups per
16-instruction batch. Every column has its exact logical length, partial
loads/stores are masked, and zero length performs no column access. Phi tails
are hashed separately. `ir_gvn_partition_group` interns those already materialized
keys in a bounded scratch hash table. A folded hash selects a bucket; the
original full-key and predecessor-ordered phi comparison establishes equality.
The analysis owns these columns in scratch. They are not persistent IR fields.

## Bounded partition grouping (#866)

Previously every round comparison-sorted every instruction, even when almost
all keys were distinct. Grouping now reuses the two existing `n`-element `u32`
sort columns as bucket heads and representative links. The bucket count is the
largest power of two not exceeding `n`; there is no new scratch allocation,
padding or signed-gather bound. Empty input accesses neither column. Only the
first member of a key class is inserted, so a large duplicate class does not
create a long chain. The existing eight-lane key hashing is retained; table
probes are scalar indexed loads rather than AVX-512 gathers. No host-specific
crossover threshold is introduced.

Each round permits at most `8 * n` representative comparisons. If another probe
would be needed, it abandons the partial grouping and runs the original stable
comparison sort on the same keys. The sort initializes its own index order and
overwrites every tentative class. This bounds hostile collisions by a linear
probe prefix plus the original `O(n log n)` key-comparison bound. The budget is
a worst-case work guard, not a measured performance crossover. Explicit scalar
completion bypasses hashing and takes that sorted oracle directly.

Neither grouping path changes the old class column before the new partition
is complete. Numbering classes in first-occurrence order instead of sorted-key
order only renames the equivalence classes: their members, predecessor-ordered
phi relations and parent-class splits remain the same. By induction the entire
refinement sequence is unchanged up to renaming, including cyclic recurrences
and the number of rounds. The subsequent dominance walk chooses representatives
by instruction traversal and class equality, not by the numeric class order.
Thus this is not an acyclic-only dispatch to the weaker available-expression
oracle, nor does it eliminate whole-function refinement rounds.

`IrGvnResult.partition_probes` counts the bounded lookup comparisons across
rounds. `partition_sort_fallbacks` counts collision-budget completions, separately
from the existing sweep-limit `fallback` flag. A forced sorted round is not a
collision fallback. These counters describe work, not elapsed time or generated
program performance.

A dominator-tree walk chooses a previously executed representative from each
class. Bucket insertions are undone when leaving a dominator subtree. Equal
expressions in sibling blocks therefore remain separate. The scalar oracle
instead hashes available expressions by their already canonical operand IDs
while walking that tree. It does not share partition refinement, and does not
recognize cyclic phi congruences. Its output is semantically equivalent but
need not have the same instruction count. The vector fixed-point solver with
forced scalar hashing does produce the exact same output.

The whitelist includes integer operations, exact bit conversions, floating
sign negation, address calculations, parameters, constants and phis. Replacing
a potentially faulting integer operation requires a dominating execution of
the same operation and operands. The earlier execution remains in place.
Loads, stores, calls, floating arithmetic, numeric floating conversions and
floating comparisons are excluded. `undef` and every value transitively
depending on it are excluded, including copies, to avoid strengthening the
unspecified-value contract. Unreachable copy cycles are never followed as
aliases and are removed with their blocks.

Substitution rebuilds private operand columns and each variable side-table
slice before compaction. This matters because valid input slices can overlap
and have different metadata roles. Constants, address/slot metadata and call
types retain their indices. Congruence is deliberately ordered: commutative
operand canonicalization, memory value numbering and algebraic identities
are not part of this implementation.

`test-ir-gvn [--seed N]` is `run_ir_gvn_tests(argc, argv)`, with the command in
`argv[0]`. Include its file after `ir_optimize_gvn.c` and
`ir_optimize_test.c`. Tests compare the independent bounded interpreter on
the input, partition output and scalar available-expression output. They
cover cyclic congruences and deliberate recurrence splits, reordered phi
inputs, sibling dominance, undef taint, repeated faulting operations,
effects, unreachable copy cycles, random DAGs with planted duplicates,
forced completion, guarded tails, forced hash collisions and partial
allocation rollback. Direct grouping tests check both initial and refined
partitions against the independent full-key relation and the sorted oracle at
empty, tiny, vector and power-of-two boundaries through 257 instructions.
The table, links, output classes and hashes end at protected pages. Distinct
full-hash collisions exercise the exact probe-budget boundary and sort restart;
a single duplicate class must not trigger it. A 4,100-instruction DAG keeps
1,024 planted eliminations, verifies interpreted behavior, and checks that no
sort fallback is needed. Forced sorted completion must preserve the rewritten
function, class count, round count and scratch usage. This is focused local correctness evidence; target-host
compile-time, emitted-code and complete pipeline measurements remain pending.

The implementation is original. The algorithmic reference is Alpern, Wegman
and Zadeck, [Detecting Equality of Variables in Programs (1988)](
https://doi.org/10.1145/73560.73561), whose partition-refinement idea motivated
the monotone congruence solver. No source implementation was imported.


## Performance acceptance

The grouping change targets #866's repeated full comparison sorts. It does not
claim that the partition solver now beats the weaker scalar algorithm, that
all acyclic work is eliminated, or that the default C compilation path gets
faster. Use `benchmark-ir-optimize-passes --pass gvn --instructions 4096
--samples 21` for the existing whole-public-stage fixture, including preparation
and rewriting, and `benchmark-ir-source --only gvn` (or `gvn-scalar`) for
source-derived IR. `benchmark-ir-source` runs both GVN paths once on every
function it prepared, requires both to accept it, and reports their removed
instructions, refinement rounds, key comparisons and collision-budget
completions before timing them in the same 21-pass medians as lowering and
promotion.

On the 9700X, built with `BUSTER_DEBUG_INFO=0` and Clang 23.1.1, pinned to CPU
2 under the shared lock, the synthetic fixture's 21-sample medians over three
processes per code placement were:

| Shape (instructions) | Path | Placement 0 | Placement 256 | Placement 1,024 |
| --- | --- | ---: | ---: | ---: |
| `small-dag` (36) | vector | −21.9% | −22.4% | −23.6% |
| `large-dag` (4,100) | vector | −75.3% | −74.6% | −75.0% |
| `cyclic-phi` (15) | vector | −5.6% | −8.4% | −5.5% |
| `small-dag` (36) | scalar (control) | +2.2% | −0.2% | +1.1% |
| `large-dag` (4,100) | scalar (control) | +3.9% | +2.0% | +2.9% |
| `cyclic-phi` (15) | scalar (control) | +1.4% | −2.4% | +1.3% |

The placements put `main_unit_backend.c`'s code 256 and 1,024 bytes later with
`./build.sh shifts`. The vector path's absolute `large-dag` medians were
1,375,020, 1,353,000 and 1,311,961 ns before the change and 339,513, 344,156
and 328,266 ns after it; `small-dag` went from 4,618.6, 4,698.2 and 4,610.2 ns
to 3,606.3, 3,645.1 and 3,522.2 ns. The scalar control varies within the small
range the placements show; the change leaves that path's algorithm untouched.
The `cyclic-phi` fixture still removes four values with the vector solver and
none with the scalar one, so the cyclic quality is unchanged.

The frozen `sqlite3.c` (2,602 functions, 396,928 typed entries and 598,425
lowered instructions) gave these 21-sample medians of three processes per
variant, with the same source benchmark and options as the 9700X job:

| Variant | Main | #946 | Change |
| --- | ---: | ---: | ---: |
| `gvn` (per typed entry) | 811.61 ns | 320.52 ns | −60.5% |
| `gvn-scalar` (per typed entry) | 199.64 ns | 202.11 ns | +1.2% |

Both paths removed 94,358 instructions over the corpus. The vector path ran
14,998 refinement rounds and made 3,704,792 key comparisons with no
collision-budget sort completion. The baseline rows apply the same
measurement-only GVN adapter to main's unchanged pass, since main has no
source-GVN benchmark; the adapter is part of this change, not of the measured
pass. Raw logs, commands and the placement script are in [the measurement
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/gvn-866-2026-10-31).

The Tier 1 pipeline still does not feed C source through GVN, so no
compile-time, emitted-code or default C-driver effect is claimed here. Those
remain #139's integration work. The source-derived rows qualify the pass on
real IR; the synthetic rows measure the whole public stage.

## Tier 1's measurement (#866's last question)

`--tier1-gvn` (docs/tier1.md, "The C driver's `--tier1`") runs this pass on the
lowered IR before promotion, which is where Tier 1 can exercise it. Measured on
the frozen `sqlite3.c` and on c-testsuite's 430 functions, it should stay off:
the congruence classes it merges lengthen live ranges across blocks, and Tier
1's allocation pays for that in moves and spills. The object's text grows 12.1%
on `sqlite3.c` and its compile 33.0% (525 to 698 ms, five runs each); on
c-testsuite the objects are 1.2% smaller but carry 13.9% more instructions and
25 spill stores and reloads where there were none, with Tier 1's coverage
unchanged (350 functions either way). After `ir_optimize` the same pass is worse
(text +15.8%, instructions +40%, 2.5 times the spill traffic), and a cleanup
pass after it changes nothing, so it is the merge, not dead values, that costs.
Routing C source through GVN by default therefore waits for a variant that
accounts for allocation cost; the option stays as the reproducible measurement.

