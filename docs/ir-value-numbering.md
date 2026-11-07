# SSA value numbering

`ir_optimize_gvn.c` is the value-numbering stage for
[#139](https://github.com/buster14a/buster/issues/139). Include it after
`ir_optimize.c`. Call `ir_optimize_gvn(function, output, scratch, options,
result)` with `ir_gvn_options()`. The result is an `IrCheck` and a fresh verified
`IrFunction` in `IrGvnResult.function`. The input, arena independence,
transactional rollback and explicit entry-phi refusal follow the
[optimization contract](ir-optimization.md). Each stage rebuilds dominance
after rewriting; no stale CFG or use list is published.

The vector path computes the coarsest congruence partition: two values are
congruent when their opcode, type, flags and semantic metadata match and their
operands are congruent position by position. Phis compare their incoming
values in CFG predecessor order, regardless of the side-table order, and may
establish equivalent cyclic recurrences. Values that reach no cycle of operand
dependences are numbered once each in dependency order ("Acyclic numbering"
below). The values that reach a cycle start from opcode, type, flags and
metadata classes and are repeatedly partitioned by the classes of their
operands. An old class is part of the next key, so refinement only splits.
Refinement ends when no class splits. After `sweep_limit` rounds (64 by default), scalar hashing and comparison sorting
complete the same fixed point; a limit never authorizes an unfinished
optimistic substitution. `IrGvnOptions.complete` refines every instruction in
those rounds instead, as the pass did before the acyclic numbering: it is that
numbering's exact oracle.

## Acyclic numbering (#866)

Refinement needs as many rounds as the longest dependency chain whose links
the static keys can't tell apart, and each round rekeyed, hashed and grouped
every instruction. Two identical chains of `L` additions take `L + 1` rounds
over their `2L + 4` instructions (one link splits off per round), and the frozen
`sqlite3.c`'s lowered IR took 15,002 rounds over its 2,602 functions. A value from which no cycle of operand dependences is
reachable is congruent to exactly the values with its static key and congruent
operands, by induction on its depth. It is never congruent to a value that
reaches a cycle: matching operands position by position from both would have
to follow that cycle forever, and the acyclic side ends at leaves.

So `ir_gvn_acyclic` numbers those values once each, in dependency order, by
hashing the static key and the operand classes into a table of
representatives. It is Kahn's algorithm. Instructions are visited in reverse
postorder (unreachable blocks last), and an instruction whose operands all
have classes is numbered at once; otherwise it waits, counting its unnumbered
operands. When a value that something waits for is numbered, the use lists
wake its users, and the users of every copy that resolves to it. The
instructions still waiting at the end are exactly those that reach a cycle.
Only they are refined in rounds, their numbered operands' classes held fixed,
and their classes are numbered after the acyclic ones so the two never
collide. In reachable SSA code every operand except a phi's input along a back
edge dominates its use, so it is numbered first. The use lists are walked only
from the few values such a phi waits for. Impure and undef-tainted
instructions are unique leaves and never wait.

Numbering one value is a dependent chain: its key needs its operands' classes,
which the instructions just before it usually publish. So this numbering is
scalar, one hash, a chain walk and at most one four-word key comparison per
instruction. The refinement rounds keep the eight-lane key hashing. A
representative's key (payload, meta word and operand classes) occupies four of
the eight field words of its own index; the existing hash, link and bucket
columns hold the rest, so the table needs no scratch of its own. The waiting
counts, the awaited flags and the list of members to refine add nine bytes per
instruction, allocated on every path so that `scratch_bytes` doesn't depend on
which one ran. Its probes share
the grouping's budget, `IrGvnOptions.probes` key comparisons per instruction
(8). If the budget runs out, the numbering is abandoned and every instruction
is refined in rounds instead, which counts as a collision-budget completion.
The partition is the same either way; only class labels differ.

Eight contiguous `uint64_t` key columns feed two eight-lane hash groups per
16-instruction batch. Every column has its exact logical length, partial
loads/stores are masked, and zero length performs no column access. Phi tails
and the upper six words of vector constants are hashed separately.
`ir_gvn_partition_group` interns those already materialized
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

Each round permits at most `8 * n` representative comparisons over its `n`
members. If another probe
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
Neither the grouping nor the acyclic numbering dispatches anything to the weaker
available-expression oracle: both compute the same congruence classes.

`IrGvnResult.acyclic` counts the instructions the acyclic numbering classed and
`rounds` the refinement rounds of the rest, their static round included (zero
when nothing reaches a cycle). `partition_probes` counts the bounded lookup
comparisons of both. `partition_sort_fallbacks` counts collision-budget
completions, separately from the existing sweep-limit `fallback` flag. A forced
sorted round is not a collision fallback. These counters describe work, not
elapsed time or generated program performance.

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

SIMD functions are admitted (#1450). The same keys include each operation's
lane flags and each `vcmp` predicate. `vconst` identity covers all 64 canonical
payload bytes, including collision comparisons; identical constants at
different table indices can merge, while different upper lanes cannot.
Integer lane operations, masks, `vselect` and exact-bit scalar splats are pure.
Floating lane arithmetic/comparisons retain the environment-sensitive exclusion.
Tests exercise duplicate payloads, differences confined to the last word,
forced hash collisions, different lane widths and retained floating effects,
with shared-interpreter execution on both transformation paths.

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
a single duplicate class must not trigger it. Odd sizes group the members in
reverse order after a nonzero class base. A 4,100-instruction DAG keeps 1,024
planted eliminations, verifies interpreted behavior, is numbered in no round
within one linear probe budget, and needs no sort fallback either way.

Every fixture runs the vector path three more times. Complete refinement must
give the same class count, rewritten function and scratch usage as the acyclic
numbering. Forced sorted completion of complete refinement must preserve its
rewritten function, class count, round count and scratch usage. A zero probe
budget, which abandons the numbering at its first occupied bucket and sorts
every grouping, must give the same classes and function. Acyclic fixtures must
take no refinement round; recurrent ones must refine something. The acyclic
numbering's own fixtures are:

- a header phi whose back-edge input is computed in the latch, through a copy,
  from loop-invariant values. It must be woken through the copy's users and
  merged with the phi whose matching input is computed before the loop, leaving
  only the counter's three values to refinement;
- two identical chains of 1 to 256 additions, all of whose links merge, where
  complete refinement takes more rounds than links;
- two congruent cycles of additions across two unreachable blocks, which only
  refinement can number and must merge;
- 160 random chains of loops, of 1 to 4 headers with 2 to 5 phis and bodies of
  0 to 9 random operations. Phi back-edge inputs come from anything the body
  sees (recurrences, loop-invariant values computed before or inside the loop,
  copies), and operations and phis repeat on purpose.

This is focused local correctness evidence.

The implementation is original. The algorithmic reference is Alpern, Wegman
and Zadeck, [Detecting Equality of Variables in Programs (1988)](
https://doi.org/10.1145/73560.73561), whose partition-refinement idea motivated
the monotone congruence solver. No source implementation was imported.


## Performance acceptance

#946's grouping change targeted #866's repeated full comparison sorts. It did
not claim that the partition solver beat the weaker scalar algorithm or that
acyclic work was eliminated; the acyclic numbering ("The acyclic numbering on
the 9700X" below) does both on these inputs. Neither claims that the default C
compilation path gets faster. Use `benchmark-ir-optimize-passes --pass gvn --instructions 4096
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

### The acyclic numbering on the 9700X

Main at `ec5f0f6b` against the same commit with the acyclic numbering
(`52003046`, merged by #1560): both built with `BUSTER_DEBUG_INFO=0` and Clang
23.1.1, pinned to CPU 2, each process under the shared lock by itself. The
synthetic fixture ran three processes per tree and code placement (the same
`./build.sh shifts` placements as above), alternating which tree went first;
its 21-sample medians changed by:

| Shape (instructions) | Path | Placement 0 | Placement 256 | Placement 1,024 |
| --- | --- | ---: | ---: | ---: |
| `small-dag` (36) | vector | −16.4% | −15.8% | −15.1% |
| `large-dag` (4,100) | vector | −22.8% | −23.0% | −22.6% |
| `cyclic-phi` (15) | vector | +0.3% | +0.2% | +3.4% |
| `small-dag` (36) | scalar (control) | +0.2% | +1.3% | +3.2% |
| `large-dag` (4,100) | scalar (control) | +0.8% | −1.6% | +1.0% |
| `cyclic-phi` (15) | scalar (control) | +1.1% | −0.2% | +3.1% |

`large-dag`'s vector medians went from 342,084, 338,839 and 343,789 ns to
264,055, 261,021 and 266,256 ns, against a scalar twin of 288,181 to 293,921 ns
in both trees. The vector path's time over its twin's went from 1.15–1.19 to
0.90–0.92 on `large-dag` and from 1.09–1.11 to 0.91–0.93 on `small-dag`. On
`cyclic-phi` it stays 1.02–1.03: eight of its fifteen values reach its
recurrences, so the refinement still runs, and its changes stay within the
scalar control's. Every process removed 1,024 values from `large-dag` and 8
from `small-dag`, and the vector path 4 from `cyclic-phi` against its twin's 0.

On the frozen `sqlite3.c`, `benchmark-ir-source` with all its variants in
each process, three processes per tree, alternating:

| Variant, per typed entry | Main | Acyclic numbering | Change |
| --- | ---: | ---: | ---: |
| `gvn` time | 316.69 ns | 193.72 ns | −38.8% |
| `gvn` cycles | 1,754.0 | 1,073.2 | −38.8% |
| `gvn` instructions | 5,577.1 | 3,525.5 | −36.8% |
| `gvn-scalar` time (control) | 198.79 ns | 199.16 ns | +0.2% |

The processes' medians agreed within 0.2%. The vector path now takes 0.97 of
its twin's time, where it took 1.59. Both trees removed 94,314 instructions
(the corpus's lowering has changed since #946's record, which counted 94,358).
Main's vector path ran 15,002 refinement rounds and 3,717,064 key
comparisons; with the numbering all 599,254 instructions are numbered
acyclic, in no round, with 241,031 key comparisons and no collision-budget
completion. The scalar twin executes 0.6% more instructions per entry
(3,660.6 to 3,680.7): it allocates and zeroes the new scratch columns too, so
that `scratch_bytes` doesn't depend on the path.

The Tier 1 pipeline still does not feed C source through GVN by default, so no
default C-driver or emitted-code effect is claimed here. Those
remain #139's integration work. The source-derived rows qualify the pass on
real IR; the synthetic rows measure the whole public stage.

## Tier 1's measurement (#866's last question)

`--tier1-gvn` (docs/tier1.md, "The C driver's `--tier1`") runs this pass on the
lowered IR before promotion, which is where Tier 1 can exercise it. Measured on
a local `sqlite3.c` of 2,604 functions and on c-testsuite's 430 functions, it
should stay off: the congruence classes it merges lengthen live ranges across
blocks, and Tier 1's allocation pays for that in moves and spills. The object's
text grows by 12.1% on that file and its compile time by 33.0% and 21.2% across
two pairs of five-run sets (525 to 698 ms and 751 to 910 ms, on a Ryzen 9
7940HS); on c-testsuite the objects are
1.2% smaller but carry 13.9% more instructions and 25 spill stores and 25
reloads where there were none, with Tier 1's coverage unchanged (350 functions
either way). On the 9700X, at the reviewed head, the same pair is 1,440,195 to
1,613,155 bytes (+12.0%) and 363 to 487 ms (+34.2%, the median of four clean
compiles). After `ir_optimize` the same pass is worse (text +15.8%,
instructions +40%, 2.5 times the spill traffic), and a cleanup pass after it
does not recover the regression (205,229 against 205,269 instructions), so it
is the merge, not dead values, that costs. Routing C source through GVN by
default therefore waits for a variant that accounts for allocation cost; the
option stays as the reproducible measurement, and [the measurement
record](https://github.com/buster14a/buster-performance/tree/126886e46a23f095709bffb36c3c57aaafac35b3/docs/measurements/gvn-866-tier1-2026-11-01)
has the commands, the host and the raw logs.
