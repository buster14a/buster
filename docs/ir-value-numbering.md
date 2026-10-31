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
and rewriting. Target acceptance still requires locked 9700X baseline/candidate
measurements across repeated processes and effective placements, tiny and cyclic
controls, and real-source IR qualification. Record raw evidence in
`buster14a/buster-performance`, not this repository.
