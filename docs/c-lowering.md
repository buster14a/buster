# Selecting definitions for lowering

`c_lowering.c` supplies the emission planner for #129, part of parser tracker
#126. It consumes resolved definitions and references, finds the definitions
reachable from the unit's roots, and returns them in source order. It has a
vector path and an independent scalar worklist. Neither path recurses.

This is a graph consumer. On the source path, `check_program_select`
(`c_check_program_select.c`) gives it the checked program's definitions, roots
and evaluated references, and `-c` and the direct path lower what it selects
("The source path" below). `c_lowering_symbols.c` compares what they emit with
`clang -O0 -c`'s objects on fixtures and corpora ("Against clang -O0's
objects"), and `benchmark-lowering-source` measures what it saves ("What the
selection saves").

## Producer and consumer

The semantic producer numbers definitions in source order after resolving
redeclarations and tentative definitions. One definition identity represents
one possible emitted function body or object, even when several declarations
refer to it. Its flags describe the already resolved emission policy; this
module does not reinterpret storage-class tokens or look up identifier names.

Roots include externally emitted definitions and definitions retained by
`used`, `constructor`, `destructor`, or an alias. An alias-target root means
the target body or object; publishing the alias symbol remains the link
producer's responsibility. Plain C99 inline-only definitions have no local
out-of-line emission. Their calls and addresses refer to the external symbol,
and their bodies do not add local dependencies merely by existing. The
producer must account for all declarations of a function and GNU inline
semantics before setting the flags. Clang documents the distinction in its
[C99 inline compatibility guide](https://clang.llvm.org/compatibility.html#inline)
and [language-mode guide](https://clang.llvm.org/docs/UsersManual.html#differences-between-various-standard-modes).

References record dependencies of emitted bodies and initializers. Local
evaluated references can reach another definition; unevaluated references and
references resolved to an external symbol do not. A blanket rule that drops
everything written inside `sizeof` would be wrong: evaluated variable-length
array bounds still contribute dependencies. The semantic producer must select
`_Generic` associations, handle constant-folded expressions, and resolve
calls and addresses before producing these columns. Static objects reachable
only from an unreferenced function remain unselected.

The output is a reached bitset and a compact array of selected definition
IDs in source order. A downstream function producer maps those IDs to checked
bodies for Tier 0 or IR lowering; an object producer maps them to checked
initializers and link definitions. Discovery order never becomes emission
order. Diagnostic checking is independent: integration must preserve the
requirement to check every body by default, as #155 requires.

Checked inline-only bodies must also remain available to a future Tier 1
inliner. Attribute any emission dependencies introduced by inlining to the
emitted caller, then prepare and compute reachability before emission. A changed
inline plan requires recomputing that closure; edges whose source is an
inline-only body are deliberately discarded during preparation. This out-of-line
selection is not a list of checked bodies that may be discarded.

## Graph preparation and traversal

`c_lowering_prepare` validates the bounded input columns and builds a stable
counting-sort adjacency index. Preparation belongs to the cost of using the
planner. The caller owns the arena and the prepared workspace; selections
reuse that workspace. No input column needs readable padding. Empty inputs
and partial vector batches are valid, and allocation failure is explicit.

`CLoweringInput` contains `definition_count` flags and `reference_count`
entries in each of the source, target and kind columns. All four columns use
`uint32_t` elements with their natural alignment. Source and local target IDs
are below `definition_count`; an external or unevaluated target may instead be
`BUSTER_C_LOWERING_NONE`. Neither count may be `UINT32_MAX`. Zero-length
columns may be null. Preparation copies the retained information, so the
producer can release its input columns after success. Failure preserves both
the arena length and the previous result.

For a nonempty input, the prepared `CLowering` owns a flags column,
`definition_count + 1` adjacency offsets, retained targets, a queue, a scalar
visited column, the reached bitset
and selected IDs. An empty plan has null buffers, including adjacency offsets.
Each solver reuses these buffers without allocating. The bitset has
`ceil(definition_count / 64)` words, with high unused bits zero;
only the first `selected_count` output IDs are valid. A workspace belongs to
one worker and must outlive every consumer of its selected IDs. The reported
`workspace_bytes` includes both paths' reusable storage and alignment;
`scratch_bytes` identifies its mutable traversal/output portion. These are
allocated capacities, not a count of bytes touched by every invocation.

`c_lowering_vector` keeps a compact frontier and a visited bitset. It batches
target membership checks and compacts the final selection in source order.
Sparse bitset writes serialize conflicts so duplicate targets cannot lose a
bit. A long dependency chain does not cause a full-edge scan at every round.
`c_lowering_scalar` uses an independent byte-visited worklist and supplies the
exact reference result. A select-all diagnostic path includes every eligible
local definition; it still excludes inline-only definitions.

## Validation and measurement

`./build/compiler test-lowering --clang` checks graph selection against an
independent oracle and checks manually modeled C emission fixtures against
Clang's object symbols. It exercises source ordering, cycles, duplicate
edges, empty inputs, vector tails, invalid contracts and resource limits.
The 9700X CI test list runs the Clang comparison.

`./build/compiler benchmark-lowering` measures deterministic synthetic graphs,
including an intrinsic-shaped small live set, broad fanout, chains and cycles.
Run each build, test and measurement through the shared 9700X lock; pin timings
to CPU 2. Selection-only measurements reuse the prepared graph; the complete
planner measurement additionally charges graph preparation and first touches.
Conditioned runs use a separate prepared plan immediately before a vector
sample, either running the scalar path or warming the vector path explicitly.

Options are `--shape all|intrinsic|fanout|chain|cycle|duplicates|large`,
`--definitions N`, `--only vector|scalar|prepare` and either `--before-vector MS`
or `--after-scalar MS`.
Each path has one warm-up and 21 samples. `prepare` means arena reservation,
first touches, validation, copying, adjacency preparation and vector selection;
result verification and arena teardown are excluded. Nanoseconds time the
operation, while the perf cycles/instructions also include the two clock reads.
Unavailable or multiplexed counters are reported as unavailable. Every measured
result is compared to a separate oracle outside the timed interval.
The default samples are unconditioned single selections; the checks between
samples do not establish sustained vector execution. Use separate processes
with `--before-vector 100` and `--after-scalar 100` for the two conditioned
states. There is no prepare-plus-scalar timing path, so the prepare result does
not establish a whole-planner improvement over scalar selection.

These measurements describe the planner, not backend lowering or a complete
compiler. The source path below compares emitted symbols on the fixtures and
corpora, and measures `main.c`'s counts and time saved.

[The qualification record](https://github.com/buster14a/buster-performance/blob/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/c-lowering-2026-10-23/README.md)
retains the exact compiler revision, host and binary identities, raw test
outputs and measurement conditions.

## The source path

`check_program_select` (`c_check_program_select.c`) runs after checking, on the
checked program (`check_program`, every body's control stream and references)
and the static data (`check_data_prepare`). Its definitions are the bodies and
the data objects, string literals and compound literals among them, numbered in
source order: a canonical object joins its redeclarations, and a prototype is no
definition. Its flags and edges:

- **Roots:** a definition with external linkage, unless it is inline-only; and
  one marked `used`, `retain`, `constructor` or `destructor`. A C99 inline
  definition without an `extern` declaration in the unit is inline-only. GNU
  inline semantics apply in C89 modes, under `-fgnu89-inline`, or for a function
  carrying `gnu_inline`. An `extern inline` definition is inline-only unless a
  declaration of that function is plain `inline`; a plain inline definition is
  emitted. Inline-only calls and addresses go to the external symbol. Like Clang,
  `-fno-gnu89-inline` turns the option off in C99 and later modes but does not
  change C89's GNU inline semantics. A GNU `extern inline` definition followed
  by an ordinary definition of the same function in one translation unit is
  GNU89's idiom: the `extern inline` definition stays inline-only and the
  ordinary one is the external definition, as Clang accepts it. Where a plain
  inline declaration makes the `extern inline` body external, Clang emits that
  body and ignores the later ordinary one; this selection does the same.
- **Edges:** each body's evaluated references (calls, addresses and reads of
  static objects, from the checked expression walk: not `sizeof`, `alignof` or a
  `_Generic` association it doesn't select, and not the operands of `&&`, `||`
  and `?:` that a constant condition never evaluates, nor what a constant
  `if`'s dead arm holds, below), and each initializer's (its address
  constants, strings and compound literals). A body also selects its own
  block-scope statics, referenced or not, as clang emits every static local of
  a function it emits, but those that a dead arm declares.
- **Constant `if`s (#1108):** `check_program` writes only the arm that an `if`
  whose condition is an integer constant without side effects runs, without the
  condition, as clang -O0's code has it (`CodeGenFunction::EmitIfStmt`): unless
  a label, or a `case` of a `switch` outside it, can enter the other arm, or the
  condition holds a label itself (`ContainsLabel`, which sees one in `sizeof`'s
  operand too). A condition whose `&&` or `||` leaves an operand unevaluated is
  a constant, a call in that operand among them: `main.c`'s
  `BUSTER_IN_UNIT(X) && argc >= 2 && strcmp(...) == 0` branches. The
  condition is its source value, where clang's constant evaluation would fold
  it outside a constant context too (`check_control_foldable`): not where it
  evaluates `__builtin_constant_p` of a variable (`__builtin_constant_p(5)`
  folds), nor a comma whose left operand is no constant (`(n, 0)` keeps both
  arms; `(1, 0)` and `(sizeof(n), 0)` fold), nor a statement expression with a
  statement before its value that the evaluator (`EvaluateStmt`) refuses. Such
  a statement expression keeps both arms; one folds whose statements declare
  automatic objects initialized with constants, address constants among them
  (`&n`, an array's or a string literal's decay), or not at all (a
  variable-length array among them),
  declare extern objects, or are constant expressions, blocks of such
  statements, an `if` of a constant condition by its taken arm, a `while`, a
  `for` (its first clause such a statement) or a `do` whose constant condition
  is false, or a `switch` of a constant condition without a `case`. A read of
  an object (`n;`), a static declaration, a `return` or `goto` the evaluation
  reaches, and a loop that reaches its backedge keep both arms. An emission-only
  fallback (`c_check_program_fold.c`, #1194) follows statements in source order:
  a reached `break` leaves only its innermost loop or switch, and skips the
  remaining body, a for's update and a do's condition. A constant switch starts
  at its matching case (including signed ranges) or default and follows
  fallthrough. Code before that label is not evaluated. Jumps in untaken arms
  and local volatile initialization therefore do not prevent this emission
  proof. The checker stream, diagnostics and constant-expression results are
  unchanged. A direct read of a nonvolatile const integer local can supply the
  result when its constant initializer actually executed; ordinary and volatile
  reads remain unknown. Constant in-bounds element addresses and VLA decay are
  accepted without reading their elements.
  The fallback does not interpret mutable locals, repeat loops, follow
  `continue`, enter switch cases through other control statements, or bypass
  cleanup, atomic-initialization or effectful VLA-bound obligations. Such
  paths, nested regions requiring the fallback, and exhaustion of its bounded
  128-entry statement stack remain conservative. Existing ordinary expression
  and VLA proofs still apply first. All listed #1194 shapes compare their actual
  object symbols with clang and execute against clang's build; there is no
  wider-emission exemption for them. A separate VM-bound safety regression
  asserts conservative selection and byte-identical emission, not clang parity:
  clang 23 drops the bound updates in that fixture, whereas this fallback
  refuses to discard them. The
  condition and the dead arm are left out with their
  references, their block-scope statics (a `used` one is no root either) and
  their statement expressions, whose regions are written empty
  (`CheckProgram.dropped_ranges`, their token ranges), and the lowering gives
  their locals no place, so a variable-length array declared there needs no
  allocation. A block-scope static declared in a statement expression that its
  body's references never reach, in an operand a constant condition never
  evaluates or in `sizeof`'s, isn't selected either
  (`CheckProgram.region_reached`). So a function that only a dead arm calls is neither selected nor
  lowered, nor checked by code generation's target features (#775):
  `if (BUSTER_VECTOR_PATH(vector))` in a baseline build no longer emits the
  AVX-512 kernel it calls. A variable-length array's address is read only after
  its allocation, which the lowering checks at each read: one in an operand that
  a constant condition never evaluates is never allocated, nor read. Constant
  `switch` statements use the same dropped ranges (#1192), before reference
  selection: a foldable selector keeps the matching case or default and its
  fallthrough statements through the first direct break, or nothing without a
  match/default. The nonrecursive collector in `c_check_program_switch.c`
  retains the whole switch when skipped labels or declarations, an escaping
  inner break, a partially entered scope's fallthrough declarations, or a case
  range encountered before the reverse-order match prevent Clang's
  `FindCaseStatementsForValue` shortcut. Nonconstant/effectful selectors and
  the outside-constant-context refusals (`__builtin_constant_p(n)`, `(n,1)`)
  keep normal dispatch. Complete retained blocks keep their scope exits;
  terminal breaks use the ordinary cleanup, VLA-restore and lifetime machinery.
  Dropped regions and block-scope statics introduce no selection roots.
- **Bound sections (#1122):** a `__start_NAME`/`__stop_NAME` reference names a
  section, not a member, so no edge reaches the members nothing else does — a
  static registry entry first — but GNU ld's bounds still cover them. After
  each reachability pass the members of the input sections that a selected
  reference's bound names are rooted as `used`, iterated to a fixpoint. A
  reference's bound name is its linkage name: an asm label from any of its
  declarations (`extern int begin[] __asm__("__start_reg")`), else its
  identifier. Each object's bound section is found once before the fixpoint,
  and each pass compares the placements against the needed objects' few. A unit
  whose link is kept for a multi-unit command roots all of its named-section
  members, since a bound of another unit's may name them, as a `-c` object's
  members stand for any link's.

The selected bodies, data and the symbols they need become the emission's view,
in source order whatever order the traversal found them in. `--lower-all`
(`check_program_select_mode` with `C_LOWERING_ALL`) selects every definition that
may be emitted instead, for measurement: an inline-only definition is never one.
Every body is checked either way; only lowering depends on the selection (#155).

The emitter names the local symbols it makes as clang does (`function.name` for
a block-scope static, `.compoundliteral` for a file-scope compound literal, and
`.N` for a name that another symbol has already, made before it or given by the
source, an identifier or an asm label, as LLVM makes a name unique in its
module), so that tools and the comparisons below read them alike.

## Straight-line expression templates (#624)

`c_check_program_batch.c` handles reached operation fragments before the general
task loop expands them. The original
scalar expression expansion remains the oracle (`--scalar`). Since #1048 no
compile takes the fragments (`CheckLowerWriter.fragments`, which only their
test sets): on the 9700X they cost `sqlite3.c`'s statements more than they
saved ("The vector path's fast paths" below), and none takes the leaf path
either ("The leaf path and the runtimes' names"). The wrappers' and truth
tests' path described here still runs. Larger fragments didn't pay either
("Nested fragments (#868)" below).

Ordinary unary and binary operations can join their immediately preceding leaf
operands when their consuming edges need no conversion. Bounded gathers and
byte permutes classify the partial vector of operator/type records, up to 16
rows per invocation. A fragment of at most four rows uses scalar type loads and
a 256-bit two-table byte permute; larger vectors use bounded gathers. Each 32-bit result word carries kind, class and type
facts in registers. Reached leaves supply their payloads. Occupied slots form
a mask, whose prefix popcounts give output positions; register compression
and masked stores expand the slots into the kind, class and value columns. A wrapper has no final slot.
The join proves evaluation order, entry room and a conservative bound on the
original task stack before publishing a bulk write. There is no graph-sized
workspace, persistent plan or recursive traversal.

Standalone `VALUE` nodes retain the original expression handler, including
addresses, storage flags and VLA indirection. Proven integer constants use a
scalar template and a packed type lookup. Casts, selections and truth tests
with no constant annotation directly schedule their checked operand through
the original conversion and truth helpers. These scalar paths retain tracked
emission and are excluded from the reported vector share. Empty conversion
queries use the packed class/fact lookup too; the separate columns remain its
scalar twin.

Other shapes keep the scalar task rules, including their conversions and
resource failures. Constant proofs skip the same children. Calls, builtins,
bit-fields, pointer arithmetic, short-circuit and conditional expressions,
memory classes, floating constants and automatic compound initialization retain
their existing handling. Operations in tracked functions and source-row
emission for `-g` also keep scalar expression expansion.

The pending stack uses two words per task instead of a padded 32-byte C record.
The control word holds the full node identity, kind and flags, and an
instruction's class. Instructions, address addends, copies and complex
operations retain all 64 payload bits; other tasks keep their full TypeId and
32-bit local identity. Dispatch reads those words directly, with expressions
and instructions separated from the less common task kinds. Expression
expansion takes its node and flags directly. The codec has an independent
field-for-field boundary test. Up to 4080 bytes of padding keep the columns
after the task stack at the same relative offset modulo 4 KB as 32-byte tasks,
while each active task touches 16 bytes.

Classification reads only immutable checked types and operators. A neighboring
body's mutable local map is never inspected speculatively. Payloads belong only
to reached expressions, and counters belong to each writer's part and are
merged in source order, without shared counters.

With `--time-stages`, `-c` and the executable driver print `lowering-nodes`:
evaluated expression occurrences, those handled by vector operation fragments
(0 while no compile takes them), their percentage, and records classified; then the evaluated nodes and the
entries #1048's node templates spliced (`templates` and `template-entries`)
with their shares of the evaluated nodes and the entries (`template-share` and
`entry-share`), which are 0 while no compile takes the templates. This counts
repeated graph uses and excludes unevaluated operands and unselected
definitions. Scalar leaf templates
are excluded from the vector expression-node share. The optional packed type table costs four bytes per type; failure keeps
the original expression rules available.

`test-checker` compares every column, failure and diagnostic token with the
original expression expansion. Guarded tests cover partial vectors, folds,
promotions, shared and reordered uses, numeric and boolean conversion chains,
missing optional workspace, addresses, VLA indirection, compound initialization
and a neighboring body's local map. Guarded pending stacks check mixed tasks,
labels, every entry-capacity limit, invalid classes and untouched
output tails. Source fixtures also compare native effects with Clang.

## Nested fragments (#868)

#868 asked whether #624's fragments could take more than a node with adjacent
leaf operands. On `sqlite3.c` (main at `586c2ae2`), 13,784 of 259,895
evaluated expression occurrences (5.30%) took them. In `((a + b) + c) + d`,
only `a + b` did: a parent's operands had to be leaves or constants next to it.

#883 tried closed postorder fragments of up to 16 integer nodes. Each root
proved its fragment's order, types, conversions and room before one bulk
write. #624's adjacent path stayed, and the expansion was the oracle. On the
9700X, against main at `c1c5ed02`, with the checker's code at six placements
that all gave the same objects
([the record](https://github.com/buster14a/buster-performance/tree/101a0b1b18f1d4eb0fb68c8c6ba92e575e56689c/docs/measurements/pr883-fragment-placement-2026-10-31)):

- coverage rose from 13,784 to 14,425 occurrences (5.30% to 5.55%);
- 11,290 roots tried a fragment and 275 closed one (2.4%); 7,978 stopped at
  an operator outside the table and 1,268 at the order check
  ([the inventory](https://github.com/buster14a/buster-performance/tree/101a0b1b18f1d4eb0fb68c8c6ba92e575e56689c/docs/measurements/nested-fragments-883-2026-10-30));
- the records classified rose from 13,885 to 17,442;
- `sqlite3.c`'s `lower` on one lane rose by 0.196 ms (95% CI +0.165 to
  +0.233), and by 0.220 ms from fresh copies of the executables;
- the 8-lane whole compile stayed within its A/A spread, and Lua's `lower`
  rose by 0.0035 ms.

A second version (`5aebc491`) read the values only for structurally closed
runs; single-lane emission was still 2.7% higher. #883 was closed without
merging.

Two later results explain why:

- **Too little to amortize.** #1048's inventory (below) found that a maximal
  subtree of nodes a template table covers holds 2.83 nodes on `sqlite3.c`,
  so a proof made root by root has almost nothing to spread its cost over.
- **The base path stopped paying.** #624's own fragments cost more than they
  save: #1235 measured `lower` 0.27 ms lower without them, and no compile has
  taken them since ("The vector path's fast paths").

To see where that stands now, main at `ec5f0f6b` ran on the 9700X against the
same executable with the fragments forced on: a measurement-only toggle,
`CheckLowerWriter.fragments` set from the environment, which main doesn't
have. The method:

- two builds, with the checker's code 2,048 bytes apart in its page
  (`BUSTER_CHECKER_OFFSET` 0 and 2048);
- the frozen `sqlite3.c` and Lua 5.4.8's 34 units as inputs;
- ten rounds, each of `measure-compile --runs 11` for every input, build and
  variant, pinned to CPU 2, with the variants' order alternating by round.

Every build and variant gave the same objects. Forced on, the fragments took
14,564 of `sqlite3.c`'s 266,735 evaluated nodes (5.46%) and 3,068 of Lua's
67,586 (4.54%). Medians of the ten rounds, both builds; each difference pairs
the variants of one round and build
([the record](https://github.com/buster14a/buster-performance/tree/1ebd8a1b0dd67749c9f921787afe99c437a1a8b7/docs/measurements/lowering-fragments-868-2026-11-06)):

| row | off (main) | on | on − off, median (range) | pairs above 0 |
| --- | ---: | ---: | ---: | ---: |
| `sqlite3.c` `lower` | 10.305 ms | 10.728 ms | +0.431 (+0.352 to +0.513) | 20 of 20 |
| `sqlite3.c` `lower.statements` | 6.700 ms | 7.119 ms | +0.429 (+0.302 to +0.529) | 20 of 20 |
| `sqlite3.c` default wall | 140.548 ms | 140.758 ms | +0.390 (−0.228 to +1.113) | 19 of 20 |
| Lua's units' `lower.statements`, summed | 2.301 ms | 2.407 ms | +0.105 (+0.059 to +0.138) | 20 of 20 |

The fragments added 4.05 M of the compile's 1,636 M user instructions and
saved no cycles (perf's counters of 11 compiles of each variant). Their cost
has grown from #1235's 0.27 ms, as the tasks' loop got cheaper per node and
the fragments' proof didn't.

So a larger fragment would extend a path that already costs more than it
saves, by a proof that #883 measured slower again. #868 is closed with that
answer: batching root by root doesn't pay at any fragment size measured. The
lowering's speed is #1048's question, a pass over a body's nodes as a whole,
which the sections below measure.

## Node templates over a body: the inventory (#1048)

#1048 proposes to lower each body's postorder by node templates, data-parallel,
instead of the task loop's push, pop and dispatch per node: each node's context
from its parent, a template id per node, a prefix sum of the templates' entry
counts, and an expansion of the templates into the columns, with the existing
rules for the exceptions. Its first step is an inventory: how much of the
lowering a table of templates covers. `measure-lowering-templates`
(`c_check_lower_inventory.c`) takes it:

```sh
./build/compiler measure-lowering-templates [--top N] [--keys N] [--abstracts N] \
    [--ignore-tracking] [--allow CATEGORY]... [-E's options] file...
```

Each file is checked and its definitions selected as `-c` selects them, then
lowered by the task loop with each entry's owner
(`check_program_lower_owners`): the lowering with `-g`'s source tokens, whose
tokens are the owners instead (`CheckLowerWriter.owners`, which only that path
reads). An entry's owner is the expression node whose task wrote it, or, marked,
the node whose value a conversion task converts for its consumer
(`check_lower_expression`'s, which the inventory moves from the consumer to the
operand, as the design puts a conversion in the operand's context), or a control
or a token for what a body writes outside the expressions. Every popped
`EXPRESSION` task is logged with its context (the task's address, initialized,
discard and `_BitInt` flags), its conversion's target and the owner that pushed
it, which gives each node its children. The run lowers the file again without
the owners and fails it unless every function's entries are the same.

A node's key is what a classifier can read from the columns: its op and
context, its type's class and facts (#570's table), whether it is a folded
constant and of which kind, its category, a bit-field, vector-element or
overload flag, a `VALUE`'s or constant address's object's kind (a symbol, a
compound literal, static) and whether the constant's offset is past the
object's start, whether a member's offset is past its record's start, its first
two operands' types, its conversion's target, and a store's destination's
kind. Its pattern is its own entries' kinds and classes, its conversion's
marked apart, interleaved with its children's subtrees. A key is deterministic
where every node of it has one pattern. A node is an exception by the issue's
list: a body that tracks its stack, a node popped twice (a compound literal's
initialization), a call, a builtin or `va_arg`, `&&`, `||` and `?:`, a
statement expression, a variable-length array's bound, extent or size, a
bit-field's access, or a complex, `_BitInt`, atomic, vector or half-precision
type. `--allow CATEGORY` counts a category as ordinary, and `--ignore-tracking`
classifies a tracked body's nodes by the other rules. "Ordinary" means none of
those categories, an upper bound for a table that takes everything else: it
counts `long double` and `__int128` values, aggregate values and their copies,
updates (`+=`, `++`) that aren't a bit-field's, `__real__` and `__imag__`, and
run-time `sizeof` and `alignof`, which the first path's table below takes none
of. So its 93% of `sqlite3.c`'s nodes is not that table's coverage, which is
75% (the first path's results); on a file of `long double` and `__int128`
arithmetic the inventory counted 50 of 71 nodes ordinary where the first path
took 6. An abstract pattern writes
each class as what gives it, the node's own, an operand's or its conversion's
target: the table a classifier would hold, each node taking its classes from
its own columns.

On `sqlite3.c` 3.53.2 (the frozen corpus's bytes, with #518's `-c` options and
this container's system headers), 2,668 bodies:

| | |
| --- | ---: |
| node occurrences (`lowering-nodes`' evaluated) | 266,853 |
| entries | 407,396 |
| entries owned by nodes / their conversions / statements and frames | 79.3% / 13.1% / 7.6% |
| ordinary nodes, of nodes / of expression entries | 93.0% / 85.6% |
| calls / `&&` `\|\|` `?:` / bit-fields / builtins, of nodes | 5.1% / 1.5% / 0.27% / 0.07% |
| the same, of expression entries | 4.3% / 7.1% / 1.7% / 1.3% |
| ordinary keys, deterministic | 2,056, 2,054 |
| their nodes / expression entries / all entries | 92.99% / 85.54% / 79.0% |
| abstract patterns | 187 |
| the first 32 of them: nodes / expression entries | 88.1% / 76.1% |
| the first 64 | 91.9% / 82.1% |
| the first 128 | 92.9% / 85.3% |
| bodies without an exception: of bodies / of entries | 12.4% / 2.8% |
| maximal covered subtrees, nodes each | 80,071, 2.83 |
| statements' expressions covered whole | 69.4% |

The two undetermined keys are aggregate assignments, a `COPY` or a `memcpy`
call by the size. Every pattern is postfix (its own entries after its
children's) but 1,522 nodes' (updates through memory, which `DUP` the address
or take a temporary). With `--allow '&&-||-?:'` the short-circuit and
conditional operators are deterministic templates too (their own entries
between their operands', their labels the payloads): 225 abstract patterns, and
93.4% of the nodes and 87.4% of the expression entries.

Lua 5.4.8's 34 units (`-std=gnu99 -DLUA_COMPAT_5_3 -DLUA_USE_LINUX`): 1,104
bodies, 67,586 nodes, 91,790 entries; 91.3% of the nodes and 85.3% of the
expression entries ordinary, all 944 keys deterministic, 145 abstract patterns
(the first 64: 90.4% of the nodes, 81.7% of the expression entries), 8.8% of
the bodies without an exception, maximal covered subtrees of 2.72 nodes. Both
corpora together have 197 abstract patterns: Lua adds 10 to `sqlite3.c`'s.

This compiler's `main.c` (`-march=znver5`, `build.sh`'s other flags): 11,512
bodies, 2.04 million nodes, 4.81 million entries, every body tracked, since a
program with a terminal helper (`__builtin_trap` or `__builtin_unreachable`
lowered to a helper, `CheckLowerShared.terminal`) tracks every body's stack.
With `--ignore-tracking`: 87.7% of the nodes ordinary but 66.5% of the
expression entries, as `&&`, `||` and `?:` hold 18.7% of them (5.0% of the
nodes); 3,339 of 3,353 keys deterministic, 219 abstract patterns (the first 64:
84.9% of the nodes, 64.6% of the expression entries), 19.0% of the entries a
body's statements' and frame's, 5.4% of the bodies without an exception, and
maximal covered subtrees of 2.23 nodes.

What the design takes from it:

- A table of reasonable size covers most nodes: 64 to 128 abstract templates,
  whose key the columns give, cover 90% of the nodes of `sqlite3.c` and Lua and
  85% of `main.c`'s.
- The exceptions are in nearly every body: a body without one holds under 5% of
  the entries. Falling back per body would leave the task loop nearly all of
  them, so the exceptions take their places within a body's expansion.
- A maximal subtree of template nodes has 2.2 to 2.9 nodes: expanding one
  subtree at a time can't amortize its setup (#868's fragments failed so). The
  classification, the counts' prefix sums and the expansion run over a whole
  body or program's nodes at once, and what stays scalar (the controls and the
  exceptions) places the expanded ranges.
- `&&`, `||` and `?:` are 7% of `sqlite3.c`'s expression entries and 19% of
  `main.c`'s: worth templates whose labels a prefix sum over the body's label
  allocations numbers.
- Stack tracking is decided per program: a program with a terminal helper
  tracks every body, so its templates must keep the tracked stack, or tracking
  be decided per body.

## Node templates over a body: the first path (#1048)

`c_check_program_templates.c` is the template path in scalar form, with the
task loop as its exact rules. Before a body's statements, three steps run over
its range of nodes, each shaped as a data-parallel pass will take it:

1. **The scatter**, from the last node down: each node's rule, by its op alone
   (`check_lower_template_rules`, two operands each), gives its operands their
   contexts (value or address) and their conversions' targets. An operand two
   nodes name is shared, and a node the tasks fold gives its operands, and
   theirs, no evaluation.
2. **The classification**, each node on its own from the first up: its context
   with its discard, its template, and its entries and its conversion's in a
   slot of eight. The fast twin (`check_lower_template_fast`) takes the common
   shapes, each by its own rule alone: a `VALUE`'s read and address, integer
   and address constants, `&x`, the casts, `*p`, member accesses, pointer
   arithmetic and subscripts, the unary, binary and truth operators, and scalar
   stores. The general rules (`check_lower_template_classify`) take the rest.
   A template takes its operands only where the scatter gave each the context
   and the target its rule gives it.
3. **The closure**, in the same step once its operands are done: its entries'
   offset in node order, and whether its subtree is closed. A subtree is closed
   when its node is a template whose evaluated operands are closed and
   converted, their entries in turn, and the last ending where the node's own
   start.

When the task loop pops a closed node in the context the pass gave it, it
copies the slots of the node's subtree in node order (but the node's own
conversion, which the task under it writes) instead of expanding the node and
its operands task by task. It leaves the writer as its tasks would (the locals
whose addresses it takes, the token, the counts). The table takes a `VALUE`'s
read and address, folded constants, `&x`, the casts and conversions, `*p`,
member accesses, the unary and binary operators of the classes I8 to F64, the
truth tests, the comma, pointer arithmetic and subscripts, and scalar
assignments and initializations. It never takes an atomic, complex, bit-field or
`_BitInt` value, an overload, a compound literal, a variable-length array, a
body with `-g`'s tokens or a tracked stack.

`test-lowering-templates [--passes N] [-E's options] file...` lowers each
file's selected program by the tasks alone and with the templates
(`check_program_lower_templates`, on any host), and requires the same entries,
locals, calls and arguments in every function. Its lowering with the templates
also checks the fast twin: each node it takes is classified by the general rules
too, which must give the same template, entries and operands, with the contexts
and targets the scatter gave them. `--passes N` times both lowerings, without
that check. `test-checker` runs the command on original fixtures of every
template (`c_check_program_templates_test.c`).

| | functions | evaluated nodes taken | entries taken | fast twin's nodes |
| --- | ---: | ---: | ---: | ---: |
| `sqlite3.c` (the system's headers) | 2,602, all the same | 75.3% | 60.8% | 259,992 |
| Lua 5.4.8 (34 units) | 1,104, all the same | 68.9% | 55.1% | 63,182 |
| c-testsuite's subset (220) | 430, all the same | 65.7% | 43.2% | 8,652 |

The fast twin and the general rules disagreed on none of those nodes.
`main.c` (`-march=znver5`, `build.sh`'s other flags) lowers the same too,
11,582 functions and 2.06 million nodes, with none taken: every body tracks its
stack. The command fails such a run, which proves nothing about the templates,
and a file that neither lowering lowers (with `-march=native` on a host without
AVX-512 VBMI, `main.c` is refused for its byte shuffles).

It is still slower than the tasks. On this session's host (an Intel Xeon),
`test-lowering-templates --passes`, whose two lowerings both take the scalar
path (`check_program_lower`'s and `check_program_lower_templates`'), timed
`sqlite3.c`'s lowering at 54 to 58 ms by the tasks and 67 to 72 ms with the
templates, 1.24 to 1.25 times (21 passes, 3 runs, pinned). The first version of
the pass, with its two passes and the general rules for every node, took 1.30
to 1.37 times. callgrind on Lua's units (a baseline build) counts the pass's
instructions:

| pass | instructions | per node of the ranges |
| --- | ---: | ---: |
| the first version | 36.9 M | about 450 |
| the three steps, the general rules for every node | 40.2 M | about 495 |
| the three steps with the fast twin | 32.0 M | about 395 |

The splices save about 12.4 M of the tasks' 111.8 M there. The pass's cost is
spread over its steps (the scatter about a fifth, the fast twin a third, the
classification step's bookkeeping a quarter) with no one line more than a few
percent: the scalar steps can't pay by trimming. Those numbers are the scalar
path's, without #624's fragments, so #1197 had the vector path's compile take
the templates (`check_lower_prepare`), for `--time-stages`' `lower.statements`
and `lower.bodies` rows to weigh the splices against the pass there. Both paths'
objects were the same, since the templates write what the tasks write:
`sqlite3.c`, Lua's 34 units and c-testsuite's subset of 220 compiled to the same
255 objects by both. On the 9700X, `measure-compile`'s medians of 11 runs on
`sqlite3.c` ([run at `adfbe904`](https://github.com/buster14a/buster/actions/runs/41559073005/job/125670065912);
the tasks' column is #1181's runs at `4216e4ec`, the next section's, and at
`09f3c8ff`):

| row | the tasks | with the templates |
| --- | ---: | ---: |
| `lower` | 12.485 to 12.684 ms | 18.306 ms (18.198 to 18.358) |
| `lower.prepare` | 1.356 to 1.374 ms | 1.246 ms |
| `lower.statements` | 8.339 to 8.490 ms | 5.281 ms |
| `lower.bodies` | 1.563 to 1.573 ms | 10.453 ms |
| `lower.validate` | 1.184 to 1.194 ms | 1.228 ms |

The templates took 201,411 of the 266,773 evaluated nodes (75.5%) and 60.9% of
the entries, most of the nodes #624's fragments took among them: those fell
from 14,573 to 466. The splices saved about 3.1 ms of the statements, 37%, but
the pass cost about 8.9 ms in the bodies' row: 44 ns for each node it took,
against the 15 ns or so its splice saved. `check_lower_template_body` was 6.5% of
the whole compile's perf samples, its third symbol. This session's Xeon (whose
default build is the vector path too: it has AVX-512 VBMI) gave the same
shape, `sqlite3.c` with `-c --time-stages --lanes 1`, pinned, 11 runs of each
build alternating: `lower` 37.2 against 57.9 ms, the statements 25.8 against
17.5 ms and the bodies 4.6 against 33.2 ms. A sampling profile there puts the
pass's time in the fast twin (45%), the scatter (29%) and the classification
step's bookkeeping and closure (26%).

So no compile takes the templates (`CheckLowerShared.templates` is set only by
`check_program_lower_templates`), and the default compile's objects are the
same as before. A pass that pays costs less than a third of this one, below
about 15 ns a node on the 9700X: what remains is the issue's point, the three
steps 64 nodes at a time. The scatter's table is by op alone. The fast twin's
shapes read the stream's columns and #570's classes and facts. The closure can
run over prefix sums of the slots' counts. The vector pass's results are to be
checked against these scalar ones, and the lowering against the tasks, by the
same command. Even a free pass leaves the statements at 5.3 ms of a 9.4 ms
stage, though, against Tier 0's 4.8: the tasks still expand the quarter of the
nodes no template takes (calls, `&&`, `||`, `?:`, increments and compound
assignments among them), so the templates' coverage has to grow as well.

## Node templates over a body: the vector steps (#1048)

#1201 made two of the pass's three steps data-parallel on the vector host path
(its commit `43d47466`), with the scalar steps as their twins, and measured
them on the 9700X:

- **The scatter** (`check_lower_template_scatter_vector`). Each operator
  linked each operand its rule evaluates, and gave it its target, by scatters
  in node order; the links read back marked an operand another operator linked
  shared; each operand's context was its link's mode. Small scalar steps from
  the last node down took the inherited contexts (a selection's and a comma's
  right operand), a comma's right operand's target and the operands under each
  fold.
- **The classification of the fast twin's common shapes**
  (`check_lower_template_vector`): a VALUE's read of a local or a global, `&x`,
  `*p`, the wrappers, the negation and the complement, the truth tests and the
  scalar binary operators but pointer arithmetic, for nodes of no payload row,
  with each lane's conversion (from #570's packed classes and facts of its type
  and its target) and its values in one 64-byte row store. It took 138,821 of
  the 259,987 nodes the fast twin and it classified on `sqlite3.c` (53%).

`test-lowering-templates`' check compared them with the scalar steps node by
node: no mismatch on `sqlite3.c`, Lua's 34 units and c-testsuite's subset, and
every function's entries the same as the tasks'.

The 9700X, `measure-compile`'s medians of 11 runs on `sqlite3.c`, the vector
path's compile taking the templates
([run at `89f39ff2`](https://github.com/buster14a/buster/actions/runs/41603617509/job/125815498424)),
against the scalar steps' run (#1197, the section above):

| row | the scalar steps | the vector steps |
| --- | ---: | ---: |
| `lower` | 18.306 ms | 19.683 ms (19.536 to 20.271) |
| `lower.statements` | 5.281 ms | 5.211 ms |
| `lower.bodies` | 10.453 ms | 11.888 ms |

The coverage was the same (201,411 nodes, 60.9% of the entries). In perf's
profile, the vector classification took 2.45% of the compile's samples (about
2.9 ms) and the vector scatter 2.22% (about 2.65 ms), against the scalar
scatter's 2.6 ms and the 2.1 ms of the fast twin on the lanes the
classification took. Each 16 nodes of the range, the classification runs
about 430 instructions with 9 gathers and 95 mask-register operations, and the
scatter up to four scatters and eight gathers, many of them chained (a node's
type, then its type's classes and facts; its object, then its local). On the
9700X that costs more than the scalar steps' branches and loads, so the steps
stay scalar and the vector ones are not kept.

The measurements of this section and the one above set what the templates
need: a pass that touches every node of the range at well under 15 ns a node,
and coverage well past three quarters of the nodes, since the pass's cost is
the range's and the splices' savings only the covered nodes'. Even a free pass
leaves `lower` at about 9.4 ms against Tier 0's 4.8.

## VALUE tasks on the leaf path (#1048)

On the vector path, the tasks' loop has #624's fast paths for an expression
task: the leaf path (`check_lower_value`), the wrappers and truth tests, and
the two- and three-node fragments. A standalone VALUE task takes none of them,
but the expansion. #1205 sent it to the leaf path, which takes a VALUE's read
of a local or a global, its address and a proven integer constant, under the
rules #1048's templates were checked against the tasks with. The 9700X's
correctness job passed with it. Two separate runs of the 9700X job, on the same
base and about 35 minutes apart, each with `measure-compile`'s medians of 11 on
`sqlite3.c` and the same options, gave the stage's aggregate rows; neither
times the VALUE tasks alone:

| row | the expansion (`92081c4a`, [run](https://github.com/buster14a/buster/actions/runs/41619659912/job/125870648949)) | the leaf path (`e33b7cc5`, [run](https://github.com/buster14a/buster/actions/runs/41609944231/job/125837465257)) |
| --- | ---: | ---: |
| `lower` | 12.535 ms (12.454 to 12.565) | 12.676 ms (12.574 to 12.830) |
| `lower.statements` | 8.525 ms (8.351 to 8.561) | 8.615 ms (8.436 to 8.727) |

These show no improvement in `sqlite3.c`'s lowering, though not a regression
either, and say nothing of other inputs. So VALUE tasks keep the expansion, which
the guard's comment in `check_lower_drain_body` records.

## The preparation's node counts and the branches' targets (#1048)

Before the bodies, `check_lower_prepare` bounds the writers' room by counts
over every node of the unit's graph: its builtins (the integer ones too, which
share a recipe pool), the nodes that may lower an atomic access (an atomic
builtin, or a node whose type or whose update's destination is `_Atomic`), and
the C library's calls of a vector's libm builtins and strict rounds
(`__builtin_elementwise_round` under `FENV_ACCESS`), one a lane. Its loop read
both types' kinds through `type_strip_aligned` for every node. callgrind on
`sqlite3.c` (#136's options, a baseline build of main at `171d6301`) counts
49.1 M instructions in `check_lower_prepare`, 19.6 M of them that loop's,
about 30 for each of the graph's 647,114 nodes.

`check_lower_node_counts` takes the counts from the ops alone, 16 at a time:
the masks of their compares, which population counts add up. A node's type,
or its update's destination, is `_Atomic` only where the type table has an
`_Atomic` kind (an aligned typedef's base is a type of the table too), so a
unit without one reads no node's type, and a unit with one takes the rules
for every node. A vector builtin's node takes the rules for its lane calls.
The loop is the scalar twin (`check_lower_node_counts_scalar`, which
`--scalar` takes), and `test-checker` compares the two on every boundary
fixture of its program tests, among them a vector's libm builtins, an
`_Atomic` global (the rules) and the atomic builtins on plain objects (the
masks, whose fixture's table the test requires to have no `_Atomic` kind).
A unit whose type table has a transport vector type (#137, a vector of `bool`,
of 1 or 2 bytes or of more than 64 bytes: only a unit with a vector kind is
scanned for one) takes the rules for every node too: such a node's operation,
cast or element writes entries a piece, an element or a cleared byte at a
time past a node's bounds (`check_lower_transport_room`: 48 entries for each
16 bytes, including logical masks, blends and private operand copies, 8 for
each of a `bool` vector's bytes, and for an element-wise conversion, shuffle,
builtin, reduction or logical operation 32 an element, which a `bool` vector's
maximum or minimum nearly fills). The node also reserves twelve temporaries
for sources, results and scalar or vector intermediates. Ordinary extended
vectors' logical operators and vector conditions use these counts too; the
vector counting pass selects those nodes only in a unit with an extended-vector
type. The intrinsics' headers have a transport type, AMX's 1024-byte
`_tile1024i`, whose nodes take the same bounded accounting.

After its statements, each body's branches (`BRANCH_FALSE` to `JUMP_VALUE`)
take their targets: their labels' entries. The loop compared every entry's
kind and branched on it, a branch whose outcome follows the branches' places
among the entries: 4.7 M instructions over `sqlite3.c`'s 407,396 entries.
`check_lower_resolve_labels` compares the kinds 64 at a time, and only the
branches among them take their targets, in entry order, up to the first label
without an entry, as its scalar twin (`check_lower_resolve_labels_scalar`)
does. `test-checker` compares the two on random kinds at a page's end,
across the masks' tails, with labels that all have entries and with some that
don't.

On the 9700X (`measure-compile`'s medians of 11 runs on `sqlite3.c`), the
`lower` stage went from 13.541 ms (13.472 to 13.603, #1143's last run at
`425f57c7`) to 12.485 ms (12.399 to 12.708) with both, and perf's share of
`check_lower_prepare`'s own samples from 1.46% to 0.75% of the compile's.
`--time-stages`' rows (#1048) then split the stage, on one lane (the step pins
the compile to one CPU; on several lanes the parts' time is the `lanes`
stage's), each row its own median, so that they add up to 12.508 ms, 0.023 ms
more than the stage's median. That run's `--time-stages` costs 0.588 ms
(0.51%) of the default compile's median wall time, against 0.196 ms (0.10%)
in the compile-time guide's older figure, which later splits compare with:

| row | ms | of `lower` |
| --- | ---: | ---: |
| `lower.prepare` | 1.356 | 10.9% |
| `lower.statements` | 8.339 | 66.8% |
| `lower.bodies` | 1.573 | 12.6% |
| `lower.validate` | 1.194 | 9.6% |
| `lower.rest` | 0.046 | 0.4% |

The statements, the controls and the tasks' loops, are two thirds of the
stage, and alone 1.7 times Tier 0's 4.874 ms in the same run: the node
templates' target.

## Lean steps in the tasks' loop (#1048)

The measurements above rule out a separate pass over every node: #1197's and
#1201's, scalar and AVX-512, cost more per node than the expansion they would
replace. #1235 tried the other way around: make the loop's one visit of each
node cheaper. Its commit `3828a06c` added `c_check_program_lean.c`:

- **The step.** When the vector path's loop popped an EXPRESSION task of one of
  the expansion's ordinary shapes, a lean step took the node: folded constants,
  `VALUE` reads and addresses, casts, conversions, selections, `&x`, `*p`,
  member accesses but bit-fields', the unary, truth and binary operators below
  VECTOR, the comma, scalar assignment, and pointer arithmetic and subscripts.
- **Exactness.** It pushed and wrote what `check_lower_expand` would, through
  the same helpers and in the same order, testing only the expansion's cases
  before its own.
- **What it left out.** It read only the columns its case needed, never the
  node's 96-byte `CheckValue`. It pushed operands inline. It continued with the
  operand the node evaluates first instead of pushing and popping it.
- **Everything else** went to the expansion, node by node.

It took 91.35% of `sqlite3.c`'s evaluated nodes. The default compile's objects
were the same as `--scalar`'s, which keeps the expansion, on all of these:

- `sqlite3.c`, with #136's options and with the frozen corpus's pinned ones;
- Lua 5.4.8's 34 units;
- c-testsuite's subset of 220.

callgrind counted the tasks' loop with what it calls (`check_lower_drain`) on
`sqlite3.c`, on a baseline build with the steps forced on for the count. It
fell from 124.4 M instructions to 110.2 M. This session's Xeon put the vector
path's `lower.statements` 5% lower, at medians of 15 alternating runs.

The 9700X ran the same base twice, main at `a4a2182a` merged with each head, in
two separate runs of `measure-compile`'s medians of 11 on `sqlite3.c` with
perf's counters of 11 compiles. The steps were on at `3fee64da`
([run](https://github.com/buster14a/buster/actions/runs/41650771613/job/125972558973))
and off at `2309a558`, which is the base's tree plus a source comment
([run](https://github.com/buster14a/buster/actions/runs/41655565084/job/125989536872)):

| row | steps on | steps off |
| --- | ---: | ---: |
| `lower` | 12.233 ms (12.172 to 12.307) | 12.378 ms (12.286 to 12.456) |
| `lower.statements` | 8.217 ms (8.114 to 8.292) | 8.358 ms (8.228 to 8.465) |
| `lower.bodies` | 1.522 ms | 1.490 ms |
| Tier 0 | 4.876 ms | 4.797 ms |
| the compile's user instructions | 1,181.1 M | 1,200.2 M |
| the compile's user cycles | 525.7 M | 526.1 M |
| the compile's user branch misses | 3.07 M | 3.09 M |

The steps removed 19.2 M of the compile's instructions, 1.6%. The runs show no
reliable reduction in its cycles (0.08%) or its branch misses (0.6%). The
steps' `lower.statements` median was 0.14 ms lower. That is within the spread
of main's own separate runs on earlier bases: 8.34, 8.49 and 8.53 ms in the two
sections above. These are two runs on one input, not an interleaved A/B. So the
steps aren't kept, and their code stays in #1235's history (`3828a06c`,
`3fee64da`).

Why fewer instructions didn't give fewer cycles, these counters don't say. The
hypothesis that #1048's next step tests is that the loop on Zen 5 waits on each
node's chain of dependent loads, which the steps kept:

- the node's op and payload row;
- its type, then its type's class and facts;
- its object, then its local;
- its operand, then the operand's type.

The instructions the steps removed would then have run beside that chain.
Samples of the lowering's load misses and their latency by line on the 9700X
would confirm it or rule it out. If it holds, a step that pays has to shorten
the chain each node waits on, or overlap the chains of several nodes.

## The helper report outside the compile (#1048)

#1188 had every compile count its lowered code's direct atomic forms for
`--helper-report` (`check_helper_report_add`), a pass over every entry of every
lowered function, timed in the lowering's stage. It took `lower.rest` on
`sqlite3.c` from 0.07 to 0.87 ms on the 9700X (#1235's diagnostic run on main at
`747ab617`) and 1.25 ms on this session's Xeon, where bisecting main found it.
The emission now makes the report only where the command asks for it
(`CheckProgramLanes.helper_reporting`, set by `--helper-report`). The objects
are the same, and `--helper-report`'s counts too. `lower.rest` is 0.07 ms again
on the Xeon. On the 9700X, #1235's run at `b20fcdbf` (main at `2fef25b0` merged
with it, [run](https://github.com/buster14a/buster/actions/runs/41750312371/job/126317031501))
gave 0.042 ms (0.034 to 0.050) in its `measure-compile` of the compile with
`--check-transitions` and 0.048 ms (0.041 to 0.051) in the compile-time step's.

The same run measured a change to the preparation: its first pass over the
declaring tokens keeps each one's body and tests it for a local once, where
it had tested it twice, and its reverse pass links the locals and keeps
their `local_map` entries. On the Xeon, `lower.prepare` went from 2.138 to
1.931 ms (15 alternating runs against main). The 9700X's two medians, 1.227 and
1.206 ms, against 1.254 ms in the diagnostic run below, are separate runs and
don't resolve a change of that size.

## Where the lowering's cycles go on Zen 5 (#1048)

A temporary step of #1235's 9700X job (`d72188fb`,
[run](https://github.com/buster14a/buster/actions/runs/41743889750/job/126295817133))
profiled `benchmark-lowering-source --passes 40` on `sqlite3.c`, a build with
debug information, each pass a warm selection, lowering, Tier 0 and emission,
the lowering about half of each pass's cycles. perf's top-down metrics put the
slots at 29.5% retiring, 30.4% frontend-bound (17.8% latency, 12.3%
bandwidth), 30.4% backend-bound (25.8% memory) and 9.3% bad speculation, at
2.52 instructions a cycle. `check_lower_drain` had 19% of the cycles, 19% of
the branch misses and 13% of the L1D misses; `check_lower_part` 7%, and 10% of
the L1D misses. Inside the tasks' loop the three events fell on the same lines,
where a node's data is first touched: the `ops[node]` test,
`check_stream_value`'s columns, the expansion's first use of the value and the
task's dispatch. That is skid's resolution, not a load's own address. No one
limit dominates, and the warm passes weren't much faster than `-c`'s cold
stage.

## The statements' prefetch (#1048)

#531 had the lowering prefetch, before each statement, the next statement's
checked values and conversions, up to its last 64 nodes (`check_lower_prefetch`),
when each value was a 96-byte record. #628 made the values columns. The
statements row moves by about 0.4 ms between separate 9700X runs, more than
the prefetch could be worth, so #1235's run at `c2e691e0`
([run](https://github.com/buster14a/buster/actions/runs/41755115082/job/126335142288))
ran variants of one build in turn, chosen by an environment variable the
lowering read only for that run: three rounds, each a `measure-compile`
(medians of 11) of every variant, on `sqlite3.c` with the compile-time step's
options. Each cell is the median of the three rounds' medians, the rounds in
brackets:

| row | main's | no prefetch | the graph's columns too | every node | a sweep of loads |
| --- | ---: | ---: | ---: | ---: | ---: |
| `lower` | 12.345 | 11.988 [11.933 to 12.025] | 12.345 | 12.280 | 13.206 |
| `lower.statements` | 8.427 [8.375 to 8.443] | 8.049 [8.005 to 8.103] | 8.407 | 8.352 | 7.662 |
| `lower.bodies` | 1.425 | 1.416 | 1.431 | 1.446 | 3.045 |
| Tier 0 | 4.804 | 4.818 | 4.806 | 4.809 | 4.805 |

- **No prefetch.** Every round's statements row was lower without it than every
  round's with it, by 0.38 ms in the medians. The prefetch's own work, two calls
  a statement of a loop over seven ranges, now costs more than the misses it
  hides, so the lowering no longer prefetches.
- **More prefetching.** Adding the graph's `ops`, operands, tokens and member
  offsets, two statements ahead, changed nothing; every node of a statement
  instead of its last 64 gained 0.07 ms, less than leaving it out.
- **A sweep of loads.** Before each statement's tasks, a loop loaded every
  column the expansion reads of each of its nodes, and what they name: the
  type's class and facts, the object's local, the payload row's type. It took
  0.77 ms off the statements and cost 1.62 ms in the bodies' row, over
  304,321 nodes: 5.3 ns a node. So the tasks' loop waits on its first touches
  for about 9% of its time, and a pass that only touches the nodes in order
  costs a fifth of what the loop costs per node (8.4 ms over about 290,000
  nodes, 29 ns a node).

On this session's container, every variant's object of `sqlite3.c` was main's,
and with the sweep of loads the default compile's objects of `sqlite3.c`, Lua's
units and c-testsuite's subset were `--scalar`'s; the run itself compared none.

## The operands' overload tests (#1048)

#1235's next run ([run at `cf254b58`](https://github.com/buster14a/buster/actions/runs/41760729792/job/126350645649))
measured four cuts of the work the lowering does for every statement and
operand in the same way, three rounds of each variant of one build, on the
base without the statements' prefetch (sqlite3.c, the compile-time step's
options; every variant's object was the base's, which the run checked too):

| row | the base | one tasks' loop a body | no overload tests | common controls first | the three | the statement sweep |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `lower` | 12.114 | 12.266 | 11.755 | 12.013 | 11.790 | 12.943 |
| `lower.statements` | 8.189 [8.177 to 8.228] | 8.343 | 7.809 [7.806 to 7.869] | 8.052 [8.048 to 8.131] | 7.849 | 9.016 |

- **The operands' overload tests.** Every operand's push read its value's
  overload head (`check_stream_overload_head`: its payload row's index, and
  the row where it has one) and looked its operand up among the selected
  overloads' addresses (`check_program_overload_address`). An overload set's
  head comes only from the overloadable attribute's sets
  (`CheckInput.overload_heads`), so a unit without them
  (`CheckLowerShared.overloads`) skips both. That took 0.38 ms off the
  statements, every round.
- **The common controls first.** An expression statement and a branch, most
  of the controls, were the last case of `check_lower_control`'s chain of
  kinds. Testing them first took 0.14 ms.
- **One tasks' loop a body**, its controls pushed as tasks rather than a loop
  entered for each statement, cost 0.15 ms, and isn't kept.
- **The statement sweep** lowered a statement whose nodes were all of the
  plainest shapes (reads of locals and globals, integer constants, casts,
  the unary, truth and binary operators of the scalar classes, and stores to
  a local or a global) by two passes over its nodes in postorder rather than
  by the tasks: the first, from its root down, gave each operand its context
  and its conversion's target, the second wrote each node's entries and then
  its conversion. It took 12,267 statements and 33,836 nodes of `sqlite3.c`,
  and cost 0.83 ms: each statement it didn't take was walked until a node of
  another shape before the tasks lowered it, the duplicated work this issue's
  pre-passes paid. Its code stays in #1235's history (`cf254b58`).

## Plain calls and compact locals (#1048)

The next run ([run at `7513c6f0`](https://github.com/buster14a/buster/actions/runs/41764265315/job/126362024774)),
on the base with the overload and control cuts, measured two more, the same
way (every variant's object was the base's):

| row | the base | plain calls | compact locals | both |
| --- | ---: | ---: | ---: | ---: |
| `lower` | 11.639 [11.616 to 11.767] | 11.590 | 11.572 | 11.503 [11.490 to 11.521] |
| `lower.prepare` | 1.187 | 1.189 | 1.244 | 1.245 |
| `lower.statements` | 7.735 | 7.631 | 7.699 | 7.652 |
| `lower.bodies` | 1.435 [1.434 to 1.477] | 1.446 | 1.330 [1.308 to 1.344] | 1.335 |

- **Plain calls.** `check_lower_call_body` tested each parameter's and
  argument's type with its helpers (`check_lower_call_passed`, the
  aggregate's, the wide value's, the vector's and the atomic's tests), in
  four loops. A call whose parameters and arguments are all plain scalars
  (a class from I8 to F64 and none of the facts whose rules go elsewhere,
  `check_lower_plain_scalar`), none a transparent union's, with such a
  result or void, now passes those checks and records its arguments by their
  packed classes and facts alone, on the vector path, which has the table;
  the scalar path keeps the helpers.
- **Compact locals.** Each body's locals were a list linked through every
  token (`local_first`, `local_next`), which the body's walks followed a
  dependent load at a time, twice. They are now a slice of one array in
  token order (`CheckLowerShared.local_tokens`, with their objects in
  `local_objects`), which the preparation builds in the passes over the
  declaring tokens it made anyway: the first tests each for a local and
  counts its body's, the second, in reverse, places each at its body's end.
  The run's variant built the slices beside the lists, with a third pass,
  which the preparation's row shows; the change leaves the lists out.

Every round of the two together was below every round of the base: `lower`
0.136 ms lower.

## The vector path's fast paths (#1048)

On the vector path, an expression task of a node other than a `VALUE` tried
#624's leaf path (`check_lower_value`), its wrappers' and truth tests' path
(`check_lower_plain`) and its fragments' path (`check_lower_batch_expression`)
before the expansion, each of them reading its node's value again. #1235's
run at `cc624eef`
([run](https://github.com/buster14a/buster/actions/runs/41767977102/job/126373552471))
left each out in turn, all three, and ran 3828a06c's lean steps (which leave
the three out) interleaved this time, on the base with the plain calls and
compact locals (every variant's object was the base's):

| row | the base | no leaf path | no wrappers' path | no fragments | none of the three | the lean steps |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `lower` | 11.780 [11.762 to 11.832] | 11.660 | 12.275 | 11.510 [11.418 to 11.579] | 11.576 | 11.799 |
| `lower.statements` | 7.938 | 7.784 | 8.389 | 7.616 | 7.730 | 7.926 |

- **The fragments** cost more than they save: without them every round was
  lower, `lower` by 0.27 ms. No compile takes them now
  (`CheckLowerWriter.fragments`); their test (`c_check_program_batch_test.c`)
  still compares them with the expansion.
- **The wrappers' and truth tests' path** saves 0.45 ms, and stays.
- **The leaf path** cost 0.12 ms, every round; its own run on the base without
  the fragments decided it (the next section).
- **The lean steps**, interleaved with the base, didn't pay either: 7.926 ms of
  statements against 7.938, and against 7.730 with the three paths left out,
  which they also leave out. The separate runs' reading in "Lean steps in the
  tasks' loop" stands.

## The leaf path and the runtimes' names (#1048)

The next run ([run at `c9a61be9`](https://github.com/buster14a/buster/actions/runs/41771038578/job/126383658074)),
on the base without the fragments (every variant's object was the base's):

| row | the base | no leaf path | the runtimes' names in the locals' pass | both |
| --- | ---: | ---: | ---: | ---: |
| `lower` | 11.452 [11.393 to 11.477] | 11.251 [11.244 to 11.273] | 11.422 | 11.158 [11.113 to 11.170] |
| `lower.prepare` | 1.290 [1.288 to 1.291] | 1.282 | 1.269 [1.264 to 1.272] | 1.266 |
| `lower.statements` | 7.522 | 7.456 | 7.547 | 7.294 |

- **The leaf path** (`check_lower_value`) took a node other than a `VALUE`
  whose value is a proven integer constant before the expansion, at the cost
  of reading its value for every node it didn't take. No compile tries it now;
  its test (`c_check_program_batch_test.c`) still calls it.
- **The runtimes' names.** The preparation matched the declarations' names
  with the runtime functions the unit spells in a pass of its own over the
  declaring tokens (`check_lower_declared_runtimes`). The vector path now
  matches them in the locals' first pass over the same tokens
  (`check_lower_declared_runtime`, the per-token test both use), which loads
  the same columns; the scalar path keeps its pass over every token, its
  twin.

Every round of the two together was below every round of the base: `lower`
0.29 ms lower.

A later run ([run at `1cb13f8a`](https://github.com/buster14a/buster/actions/runs/41779070832/job/126409052295))
measured the runtimes' test with the linkage and the declaration's own token
first, which the locals' test loads too, and the object and the name only for
a declaration with external linkage: `lower.prepare` 1.192 ms against 1.260
(every one of four rounds lower), and so it tests them. The same run's
integer constants' fast path, which wrote a constant of a plain class first in
`check_lower_constant` by its packed facts, didn't pay (`lower.statements`
6.936 ms against 6.899), and isn't kept.

## The tasks' loop's code (#1048)

The loop's copies (`check_lower_drain_body`) inline the whole expansion, so
the plain copy was 17 KB of code, of which the common nodes' cases are a
fraction; #1235's diagnostic found the lowering a third frontend-bound. The
expansion's cases of the ops past `CHECK_OP_SELECT` (the builtins,
statement values, `va_arg`, VLAs' extents, the complex parts and bit casts)
now run out of line, through a copy of the whole expansion of their own
(`check_lower_expand_rare`), and every copy of the loop keeps the other ops'
cases alone (`check_lower_expand_split`, whose assumption on the op lets
clang leave those cases out): the plain copy is 14 KB.

#1235's run at `e5b79666`
([run](https://github.com/buster14a/buster/actions/runs/41774131348/job/126393324952))
ran a split copy of the plain loop beside the plain one, in one build, five
rounds in turn on `sqlite3.c` (the variant's object was the base's):

| row | the base | the split copy |
| --- | ---: | ---: |
| `lower` | 10.920 [10.867 to 10.932] | 10.733 [10.714 to 10.767] |
| `lower.statements` | 7.149 [6.997 to 7.162] | 6.878 [6.832 to 6.918] |

Every round of the split copy was below every round of the base. On this
session's Xeon, a build with the split alone took 4% off the statements in 7
of 8 alternating rounds.

The next run ([run at `0ce575ef`](https://github.com/buster14a/buster/actions/runs/41777114635/job/126402983237))
took the plain copy's other cold paths out of line in a copy of its own, 10.4
KB: an unsupported feature's report at each of the expansion's 11 refusals, a
compound literal's first evaluation, and the handlers of the task kinds past
`CONTROL` (the builtins', copies, fills, bit-fields' reads and stores,
`va_arg`'s, VLA sizes, atomics, complex values). Five rounds in turn gave
`lower.statements` 7.007 ms against the base's 6.970 (6.912 to 7.047), and
`lower` 10.832 against 10.799: no gain, so it isn't kept.

## The operands' push (#1048)

An operand's push cost about 115 instructions on the baseline host (callgrind
on `sqlite3.c`): `check_lower_operand`'s overload lookup, a tail call to
`check_lower_expression`, its overload and count tests, its void context's
loads (the op, the destination and its flags), `check_lower_convert`, and a
`CheckLowerTask` built and packed. In a unit without overload sets the
operand is now pushed in `check_lower_operand` itself, as
`check_lower_expression` pushes it: the void context's tests only in a void
context, the conversion's test in place, the `EXPRESSION` task packed
directly. Its code is the operand function's, not the tasks' loop's, which
inlines none of it.

#1235's run at `4b9d236a`
([run](https://github.com/buster14a/buster/actions/runs/41782206254/job/126419182825)),
five rounds in turn on `sqlite3.c` (the variant's object was the base's):

| row | the base | the push in place |
| --- | ---: | ---: |
| `lower` | 10.704 [10.658 to 10.764] | 10.543 [10.491 to 10.566] |
| `lower.statements` | 6.963 [6.854 to 7.021] | 6.729 [6.685 to 6.822] |

Every round of the push in place was below every round of the base. Copying
the expansion's value at the two cases that take its address, so that clang
could keep it in registers elsewhere, took 0.8 M of 207.4 M instructions on
the baseline host (callgrind), and wasn't measured further.

A `VALUE` task's read of a local or a global by its columns alone, in a copy
of the loop 223 bytes longer (`check_lower_value_read`: no payload row, a
plain class, none of the facts whose rules go elsewhere, no automatic
compound literal), didn't pay either, interleaved this time
([run at `02e4a889`](https://github.com/buster14a/buster/actions/runs/41784993695/job/126427989003)):
`lower.statements` 6.848 ms against the base's 6.787, as #1205's leaf path's
two separate runs found. A `VALUE` node costs about the same through the
expansion, whose instructions aren't what it waits on.

Two more didn't pay ([run at `0ac86c2e`](https://github.com/buster14a/buster/actions/runs/41787103822/job/126434576373),
four rounds): `check_lower_expression`'s own pushes (the statements' roots
among them) in place too, `lower.statements` 6.720 ms against 6.768 with
rounds overlapping the base's; and a conversion between two plain scalars'
types by their packed classes alone, in a copy of the loop, 6.833 ms.

## The initializers' map, the bodies' copies and the writer's reloads (#1048)

The preparation cleared `initializer_declarations`, a word for each of the
graph's nodes (1.3 MB on `sqlite3.c`'s 323,557), before writing it at the
initializers' nodes, and the expansion reads it only at `INITIALIZE` nodes.
It is no longer cleared: a read (`check_lower_initializer_of`) takes a slot's
token as the node's declaration only where `initializer_nodes` maps the token
back to the node, which a stale slot's can't, since every token whose
initializer is a node is among the declaring tokens that write the map
(`check_program_declaring`). Filling every slot with another node's declaring
token beforehand left `sqlite3.c`'s, Lua's and c-testsuite's objects the same.
#1235's run at `ff461ce9`
([run](https://github.com/buster14a/buster/actions/runs/41791985354/job/126450663310)),
five rounds in turn on `sqlite3.c`:

| row | the base | without the clearing |
| --- | ---: | ---: |
| `lower.prepare` | 1.196 [1.186 to 1.198] | 1.156 [1.154 to 1.167] |
| `lower` | 10.611 [10.601 to 10.657] | 10.587 [10.514 to 10.609] |

Every round of the preparation without it was below every round of the base.
`sqlite3.c` has no variable-length array, so the token-indexed clearings of
the arrays' identities and saved positions, which only such a unit takes,
don't run there.

Three more didn't pay:

- **The bodies' short columns.** Each body's end copies about ten columns from
  the writer to the output arena, most of a few bytes. Copying one of at most
  64 bytes by a masked load and store instead of `memcpy`
  ([run at `c4b2f341`](https://github.com/buster14a/buster/actions/runs/41789197103/job/126441581914))
  put `lower.bodies` at 1.284 ms against 1.311, but its slowest round (1.296)
  was above the base's fastest (1.295), and `lower`'s rounds overlapped.
- **A restrict writer.** The build's `-fno-strict-aliasing` lets every store
  to an entry's byte columns alias the writer, so the loop reloads its depth,
  tasks, count, output and program after each, and reaches a node's op through
  four dependent loads (`writer->program->input->graph.ops`). A copy of the
  loop whose writer is `restrict` compiled to the same code: the writer is
  passed to the out-of-line helpers on the loop's paths, so clang may not
  assume that a pointer loaded from memory doesn't reach it.
- **The value's fields alone.** The expansion builds the node's 96-byte
  `CheckValue` on the stack (five fields and four overlapping zeroing
  stores), as two of its cases take its address. A copy of the loop that read
  only the seven fields it uses, and built the whole value at those two cases,
  was 330 bytes shorter but took `lower.statements` 0.6 ms higher on this
  session's Xeon in each of three alternating rounds: its stack accesses rose
  from 401 to 474, the fields' registers spilling what the value's copy had
  held.

## Plain scalar locals and parameters (#1048)

Each body's setup gives each of its parameters and locals a temporary or a
frame object by about ten queries of the type table: its kind (through
`type_strip_aligned`), whether it is an aggregate, of a memory class or of a
runtime size, its class, a compound literal's vector, its alignment. On the
vector path a plain scalar's (`check_lower_plain_scalar` by its packed class
and facts, and neither an array's nor a function's), of no attribute's
alignment and none past 8 bytes, now takes its temporary of its class
directly, as those rules give it, and a prototype's plain scalar parameter
too, whose type `check_lower_passed` takes. #1235's run at `ccb12c14`
([run](https://github.com/buster14a/buster/actions/runs/41799729636/job/126476270330)),
five rounds in turn on `sqlite3.c`, every variant giving the base's object:

| row | the base | the locals' | the locals' and the parameters' |
| --- | ---: | ---: | ---: |
| `lower.bodies` | 1.292 [1.284 to 1.304] | 1.210 [1.203 to 1.220] | 1.197 [1.194 to 1.207] |
| `lower` | 10.535 [10.519 to 10.553] | 10.482 [10.445 to 10.545] | 10.473 [10.468 to 10.489] |

Every round of both variants' bodies was below every base round, and so were
the second's `lower` rounds. `test-checker` passed with them, whose fixtures
compare the vector path, which takes them, with the scalar one, which doesn't.

The locals' walk still reads each local's declared type by its token, a word
a few tokens apart for each local, which the statements before it have
evicted. Writing the types in the preparation's slice of the bodies' locals
instead, in its pass over their tokens, moved the miss rather than removing
it ([run at `6f5156dc`](https://github.com/buster14a/buster/actions/runs/41805877539/job/126497206766)):
`lower.bodies` 1.184 ms against 1.232, `lower.prepare` 1.258 against 1.157,
`lower` 10.592 against 10.555.

## The expansion's token (#1048)

The expansion and the wrappers' path set the writer's token to their node's
(`writer->token = graph->tokens[node]`) for every node, a read of the tokens'
column, but only a failure (`check_lower_fail`) and the part's end
(`CheckLowerPart.final_token`) read the token. They now keep the node, marked
by the top bit (`BUSTER_CHECK_LOWER_TOKEN_NODE`: no unit has 2^31 nodes, and
`BUSTER_CHECK_NONE` is none), and those two readers take its token
(`check_lower_writer_token`). The fragments' test compares the writers' tokens
so read. #1235's run at `5849a291`
([run](https://github.com/buster14a/buster/actions/runs/41808837919/job/126505321530)),
five rounds in turn on `sqlite3.c`, both giving the base's object:

| row | the base | the node kept |
| --- | ---: | ---: |
| `lower` | 10.440 [10.389 to 10.537] | 10.367 [10.304 to 10.385] |
| `lower.statements` | 6.857 [6.776 to 6.890] | 6.773 [6.649 to 6.807] |

Every round of `lower` was below every base round; one round of the
statements overlapped the base's.

Four more of the operands' and the values' paths didn't pay on this
session's Xeon, each in three alternating rounds against the base of one
build, and weren't run on the 9700X:

- **A branchless conversion push.** Writing an operand's `CONVERSION` task
  whether or not it is needed, and advancing the depth by whether it is,
  took the statements 0.8 to 1.0 ms higher: the next pop's load waits on a
  store whose address the test decides late, where the branch was predicted.
- **The operands' common push in a leaf of its own**, called by a copy of the
  loop's expansion (`check_lower_operand` saves five registers on every
  call; the leaf, three): 0.1 to 0.3 ms higher.
- **A `VALUE`'s `LOCAL` and `GLOBAL` entries appended inline** rather than by
  `check_lower_emit`'s call: 0.03 to 0.11 ms higher.
- **The value's fields read from the stream where the expansion uses them**,
  with no copy of the value: 0.3 to 0.7 ms higher. The copy's stores overlap
  with the node's other work, where each field's second read costs a load.

## Each node's class and facts by node (#1048)

The expansion's head finds its node's class and facts through its value's
type: the node's type, then the type tables (`check_lower_class`,
`check_lower_facts`), a chain of dependent loads on every node. A pass in the
preparation wrote each node's (`type_templates`' low 16 bits) 16 nodes at a
time by a gather from the packed table, and a copy of the tasks' loop read them
by node instead, beside the node's op. The stream writes every node's type in
its column, payload rows' too (`check_stream_set_value`), so the column alone
gives it. #1235's run at `c4aa3882`
([run](https://github.com/buster14a/buster/actions/runs/41796225108/job/126464328680)),
five rounds in turn on `sqlite3.c`, all three giving the base's object, with
the pass gathering a payload row's type from the row (16) and with a payload
row's node left to the table (32):

| row | the base | 16 | 32 |
| --- | ---: | ---: | ---: |
| `lower` | 10.674 [10.577 to 10.715] | 10.939 [10.920 to 10.972] | 10.696 [10.679 to 10.772] |
| `lower.prepare` | 1.170 [1.149 to 1.185] | 1.522 [1.500 to 1.553] | 1.295 [1.284 to 1.310] |
| `lower.statements` | 6.915 [6.824 to 7.027] | 6.776 [6.754 to 6.819] | 6.803 [6.744 to 6.828] |

The shorter chain took 0.11 to 0.14 ms off the statements, and the pass over
the unit's 323,557 nodes cost as much: about 0.4 ns a node, its gather and its
reads of two columns. So neither is kept. Two more of the same kind didn't pay
on this session's Xeon, and weren't run on the 9700X: the operands'
conversion tests taking their operand's class and facts by node too (mixed
rounds), and each node's local resolved by a pass over its body's range of
nodes once the body's locals are set (`lower.bodies` 0.7 ms higher, the
statements unchanged).

Nor did carrying them without a pass. An operand's push reads its type and,
for its conversion's test, the type's packed word, and an `EXPRESSION`
task's payload is otherwise zero: the push put the word there, and a copy of
the loop took it in the expansion's head; a second variant carried the
operand's op too. #1235's run at `38727492`
([run](https://github.com/buster14a/buster/actions/runs/41803179958/job/126487426058))
put `lower.statements` at 7.264 ms and 7.298 against the base's 6.909, every
round above every base round: the push's added lookups and the copy cost more
than the shorter chain saved.

## The statement sweep over evaluated nodes (#1048)

#1235's statement sweep (`cf254b58`) took only statements whose every node had
one of the plainest shapes. It walked each statement it refused before the
tasks lowered it. Commit `83f2cad3` on the branch `opencode/1048-lowering`
tried the full version: every expression statement, branch, plain return and
scalar local's one-value initializer on the vector path, with the nodes it
can't take left to the tasks in place.

1. **From the root down.** Each evaluated node, found by a bitset of distances
   below the root, writes its own entries and its conversion's into a slot. It
   gives its operands their contexts, targets and ancestors' calls.
2. **From the lowest node up.** The slots are copied in node order. A task node
   (`&&`, `||`, `?:`, an update, a bit-field's or an aggregate's access, a
   builtin, among others) has its expression task drained in its place. Each
   call's row goes where the tasks' preorder puts it.

It was exact. `test-lowering-sweep` lowered every function the same as the
tasks: 79.75% of `sqlite3.c`'s evaluated nodes swept, 72.79% of Lua's 34
units' and 84.82% of c-testsuite's subset's. `sqlite3.c`'s object matched
`--scalar`'s. It was slower, though. On the 9700X, seven interleaved rounds of
one build ran three variants:

- **The tasks**, as on main.
- **The sweep.**
- **The walk**: the sweep's first pass with each node's rules replaced by its
  op, its type's class and its first two operands marked. It discards the
  result, and the tasks then lower every statement.

Medians in ms, ranges in brackets
([the record](https://github.com/buster14a/buster-performance/tree/9fce968596150cc884dd311880238185ba162a21/docs/measurements/lowering-sweep-1048-2026-11-07)):

| row | the tasks | the sweep | the walk |
| --- | ---: | ---: | ---: |
| `lower` | 10.611 [10.468 to 10.775] | 12.185 [12.132 to 12.330] | 12.157 |
| `lower.statements` | 6.943 [6.846 to 7.032] | 8.516 [8.452 to 8.600] | 8.460 |

`test-lowering-sweep --passes 25`'s warm lowerings agreed: the sweep took 1.147
to 1.150 times the tasks' time, in three runs.

The walk alone cost about 1.5 ms. The sweep's row is that walk plus the rules
for 80% of the nodes, the second pass and the tasks for the rest. That comes to
about 7 ms, as much as the tasks' whole loop, push, pop and dispatch included.
IBS op sampling on this session's Zen 4 laptop agreed: both designs dispatch
about 400 to 450 macro-ops per evaluated node. So the tasks' loop spends its
time on the expansion's rules, the chain of loads and tests that decides each
node's entries, not on reaching the node. A different order of visiting the
nodes can't take `lower.statements` near Tier 0's time. These changes took
the sweep from 1.30 to between 1.11 and 1.18 times the tasks' time on the
laptop, never below 1:

- a bitset in place of 64-byte loads of the byte states just stored;
- each slot's kinds and classes stored and read as whole words;
- no per-statement copy of the writer's columns;
- a copy of the slots alone for a statement without calls or task nodes;
- every common op's case inline.

So no compile takes the sweep; its code stays in the branch's history.

## Address spaces (#1447)

The checker's types carry clang's address spaces (docs/c-types.md's "Extended
qualifiers"). clang's x86 target maps 256, 257 and 258 (`__seg_gs`, `__seg_fs`)
to the `%gs`, `%fs` and `%ss` segments: its -O0 code loads and stores there with
a segment override (`mov %gs:(%rax),%eax`).

`check_lower_access` gives a LOAD, STORE or SET of an object in `%fs`, `%gs` or
`%ss` the typed stream's override (`BUSTER_TYPED_ACCESS_SEGMENT`, docs/tier0.md's
typed stream), from the accessed lvalue's type: a read's own, an assignment's
target's (the assignment's value has lost the space), an update's stored type
for its load and its store (the postfix one's too). It marks the function
(`Tier0Function.segments`), which Tier 0 compiles with the override on the
access's memory operand, and which Tier 1 compiles with it too (docs/ir.md's
"Segment overrides"; an atomic read/modify/write's stays Tier 0's). A global in one of
those spaces goes through its address (GLOBAL_ADDRESS, then LOAD or STORE), as a
memory class's does: `lea sym(%rip)` and `%gs:(%rax)` reach the byte clang's
`%gs:sym(%rip)` does. What takes the override (`check_lower_segment_carried`):
a read by indirection, member, subscript or name, and an assignment, compound assignment, increment or
decrement, of an integer's, a floating value's (long double's too), a 128-bit
integer's or a vector's class (not a transport vector), which one LOAD or STORE
moves, and a half-precision value's (`_Float16`'s, `__bf16`'s), whose loads and
stores are a U16's (`c_check_program_half.c`). An array's lvalue isn't read: its value is its address. A vector's
element, read, assigned or updated, takes its vector lvalue's override, which the
element's own type has lost (`check_lower_element_segment`): clang reads the
whole vector through it and extracts the element, and writes it by a read, an
insertion and a store of the whole vector, which for a vector that isn't
volatile is the same access as one of the element's bytes (an element of a
volatile vector stays refused by name). A bit-field's
read and store take it on each piece of its unit (`check_lower_bit_unit`,
`check_lower_bit_store`): its place (`CheckLowerBitField.segment`, packed in its
task's bits 17 and 18) has its record's override, from the member's type
(`check_lower_bit_member`). An atomic builtin's
object in `%fs` or `%gs` takes it where a direct plan (#1059) takes the builtin:
its LOAD or SET (a seq_cst store's `xchg`), `ATOMIC_RMW` (`lock xadd`, `xchg`),
`ATOMIC_CAS` (`lock cmpxchg`) and a fetch's loop of a relaxed LOAD and
`ATOMIC_CAS`. Its space is its object type's, or else its pointer argument's
pointee's (`check_lower_atomic_space`: a generic builtin's object type, such as
`__atomic_test_and_set`'s, doesn't carry it).

An object whose value is an address (a struct's, a union's, a complex value's, a
`__float128`'s, a wide `_BitInt`'s limbs, a transport vector's) is read by a
copy of its bytes through the override to a frame object of its own
(`check_lower_segment_read`), whose address its consumers then take as any
flat one: a copy, a call's argument, a register return, a complex operation, a
comma's or a conditional's forwarding. A struct's or union's assignment into a
segment copies through the override (`CheckLowerTask.segments`, the
destination's in bits 2 and 3, `check_lower_copy_segmented`), and its value is
the stored bytes at the source's address. Each copy is pieces of 8, 4, 2 and 1
bytes, a `LOAD` and a `SET` each with its side's override. Clang lowers such a
copy (an `llvm.memcpy` in a segment's address space, which no library
function takes) inline as at most 8 stores of its widest vector (4 at `-Os`)
and stops with "cannot lower memory intrinsic in address space" past them: 128
bytes for SSE, 256 for AVX and 512 for znver5's AVX-512. The lowering takes up to 8
of the function's target's widest vector registers' bytes, never fewer than
clang, and refuses a larger copy by name ("an aggregate copy of more than 8
vector registers in a segment's address space ..., which clang can't lower
either").

A complex value's, a `__float128`'s and a wide `_BitInt`'s update (a compound
assignment, an increment or a decrement), and a real object's update by a
complex value, go through a frame object (`check_lower_segment_update`): the
update task's emitters read and write the destination through its address, so
the object is copied there through the override, updated as any flat object,
and copied back, its value kept in a temporary across the copy. A complex
lvalue's part (`__real__ *p`, `__imag__ *p`) has lost its object's space, as a
vector's element has, and takes the complex object's override on its read, store
and update (`check_lower_element_segment`), as clang's code does. A narrow
`_BitInt`'s read, store and update take it as an integer's do. A bit of a vector of bool, which no address
reaches, is read, and written by a read and a store of the byte that holds it,
through the vector's override (`check_lower_transport_bit_read`,
`check_lower_transport_bit_store`), as clang loads and stores the vector.

An `_Atomic` lvalue's read, store and update go through its plan
(`check_lower_atomic_prepare`), whose direct accesses take the object's override
(`CheckLowerAtomic.segment`): a load's `mov`, a store's `xchg`, an addition's
`lock xadd` and another update's loop of a load and `lock cmpxchg`, the
instructions clang's code has. A library call (`__atomic_load`, `__atomic_store`
of an object no instruction moves atomically) takes the object's address as it
is, as clang's does: the segment's offset, which is the object's own only in
`%ss`.

The rest the lowering refuses by name, rather than compile it as a plain access:
an atomic
builtin another plan takes (a generic one by pointer, `__atomic_clear`,
`__sync_lock_release`, one of 16 bytes); and an atomic builtin's object in one, which its pointer argument reaches
(`check_lower_segment_access`). An asm statement's register output in one is
refused too (`check_lower_asm_segment`), since its value is stored through the
output's address; a memory operand's address goes to the template without the
segment, as clang's does. Only a unit whose type table has a type in a
segment's space tests its nodes (`CheckLowerWriter.segments`, from one `memchr`
of the kinds for a `QUALIFIED` record, then those records' spaces), and such a
unit's expressions take the expansion, not the plain or batched paths, so that
each node meets the test. Any other space's code is the default space's, as
clang's x86 code is.

## Computing with transport vectors (#137)

Tier 0's VECTOR class holds vectors of 4, 8, 16, 32 and 64 bytes. The others,
the transport vectors (`check_lower_transport_type`: a vector of `bool`, whose
extended vectors pack a bit per element, one of 1 or 2 bytes, and one of more
than 64 bytes, AMX's 1024-byte tile among them), are an aggregate's value, their
object's address, which calls, copies and `va_arg` take by their bytes
(docs/tier0.md). `c_check_program_transport.c` computes with them as clang -O0's
code does, each result written into a frame object of its type whose address is
the result, with the least new machinery: every entry it writes is an entry the
lowering already writes for scalars or ordinary vectors.

- **Vectors of 1 or 2 bytes** (one or two `char`s, a `short`, a `_Float16` or a
  `__bf16`): an element at a time (`check_lower_transport_elements`). Each
  element is loaded, promoted (to `int`, or to `float` for a half-precision one),
  computed by C's operator and converted back, as LLVM's element-wise
  operations give it after widening the vector into a register whose other
  lanes no result reads. Wrapping arithmetic, shifts, division and remainder
  are so exact. A compare's result is the negated `int` 0 or 1 in the result's
  signed elements; a half-precision negation flips the sign bit, as `fneg`
  does, and its arithmetic in `float` rounds once to the element as clang's
  single operation does. At two elements, a vector row would cost more entries
  than the elements' own.
- **Vectors of more than 64 bytes**: a piece at a time, each the width of the
  target's widest registers (64 bytes with AVX-512F and VL, 32 with AVX, else 16),
  as LLVM legalizes them, by the ordinary vectors' rows at that width
  (`check_vector_lower_element_row`, which takes an element and a width rather
  than a type): `check_lower_transport_pieces` loads each operand's piece at its
  offset, runs the row and sets the result's piece. A scalar operand is splat
  once into a register-sized temporary that every piece reads. What the rows
  refuse at a level (variable shifts without AVX2 or AVX-512BW, 64-bit compares
  without SSE4.2, integer division, 8-bit multiplication) is refused by the same
  names as the ordinary vectors'. Bf16 arithmetic uses its ordinary rows too,
  in pieces of at most 32 bytes under float excess precision, so the widened
  elements fit one register; native AVX10.2 uses the full register width.
- **Vectors of `bool`**: their bits as clang's `<N x i1>` keeps them in memory,
  element i at bit i % 8 of byte i / 8. Their operators are bitwise operations on
  the bytes (`check_lower_transport_bools`): `&`, `|` and `^` themselves, `~` and
  `!` a complement, and the compares, false below true as `i1`'s unsigned
  compares order them, two steps each (`a < b` is `~a & b`, `a == b` is
  `~(a ^ b)`, ...). Up to 8 bytes they are one integer's (in 32 or 64 bits), past
  them the unsigned chars' vector rows a piece at a time. The bits past the last
  element are cleared, as clang's stores of masks clear them where it doesn't
  keep them in an opmask register: clang's own code leaves them either way (a
  `<4 x i1>` complement through `knotb` sets them), so the tests fold a padded
  vector's elements alone. An element is its bit: read by its byte's load, a
  shift and a mask; written by the byte's other bits kept and its own set to the
  value's truth.
- **Casts, splats and constants** (`check_lower_transport_cast`): between
  vectors of the same bytes, their bits (nothing between two transport vectors;
  an ordinary vector's loaded from or set into an object); between a GNU C
  vector and an integer of its bytes, their bits; a scalar's splat into every
  element, converted to the element's type first (a `bool` vector's elements all
  its truth). A constant the checker folds is an integer's bits (a cast of an
  integer constant), stored into the object.
- **Element-wise conversions, shuffles and reductions**: `__builtin_convertvector`
  and the implicit conversion of an extended integer vector to a `bool` vector
  of as many elements (clang's `icmp ne 0`, which the checker now accepts as
  clang does; an explicit cast stays an error) convert element by element as C
  converts a scalar (`check_lower_transport_convert`), and
  `__builtin_shufflevector` gathers elements by its constant indices, either way
  from or into an ordinary vector through a temporary, and into a `bool` vector a
  byte of bits at a time (`check_lower_transport_gather`). The integer
  reductions (`add`, `mul`, `and`, `or`, `xor`, `max`, `min`) fold the elements
  first to last in their promoted type, a maximum or minimum by a mask without a
  branch; a `bool` vector's sum is an exclusive or and its product a
  conjunction, as `i1`'s arithmetic wraps.
- **Element-wise builtins** use the ordinary rows on disjoint register-sized
  pieces (`check_lower_transport_elementwise`). The `ldexp` exponent can have
  another element width: each argument advances by its own width, and the
  widest one bounds the piece's lane count. Tiny operands are copied into
  private padded storage, and only the result's actual bytes are stored.
  Libm functions, `pdep`, `pext` and scalar `clmul` read each element through
  the same address/value handoff as ordinary vectors. Boolean builtins use
  packed bit operations (`check_lower_transport_boolean_builtin`): max and
  saturating add are OR; min, `pdep`, `pext` and `clmul` are AND; saturating
  subtract is `a & ~b`; popcount and bitreverse keep their operand. A one-bit
  funnel shift has a count modulo one, so the left funnel keeps the first
  operand and the right funnel the second. All arguments are still evaluated
  once, and only defined result bits are retained.
- **Floating reductions** preserve clang's reduction tree across register
  boundaries (`check_lower_transport_reduce_floating`): repeatedly combine the
  low and high halves in register-sized pieces, then use the ordinary
  legal-width reduction. Maximum and minimum preserve their NaN and signed
  zero rules; `max` and `min` fold the final legal vector in source order.
  Associative sums use -0.0 in unused padding lanes and then add the start
  value. Ordered sums visit the defined lanes first to last.
- **Logical operators and vector conditions** (`check_lower_vector_logical`)
  use the same piece-wise compare and selection rows at tiny, ordinary and
  transport widths. Logical truth is nonzero; an integer vector condition
  selects by each lane's sign bit, and a boolean condition by its packed bit.
  Every operand is evaluated once, including both conditional arms. Capturing
  an address-valued operand copies its value before a later operand can
  modify the source object. Half-precision logical truth falls back to scalar
  comparisons where no matching target row exists.
- **Compound assignments** compute into the result's object with the
  destination's address as the left operand, then copy the result into the
  destination (memmove past Tier 0's copy limit, for a tile).

The ordinary rows' element-type and target-feature refusals remain. Floating
reductions of half-precision and bf16 elements and padded floating extrema
remain named refusals, as do bf16 comparisons and excess-precision arithmetic
chains. Clang rejects whole-vector increments and boolean-vector `&&` and `||`;
the checker diagnoses them. Compound assignments and increments of a `bool`
vector's elements remain named lowering refusals: clang 23's code is invalid IR (an
`extractelement` of the vector's `i8`), which crashes clang on some (`^= 1`,
`*= 2`, `--`) and computes no element's value on others: what clang itself
doesn't compile. Boolean elementwise `clzg` and `ctzg` also retain a named
refusal: clang 23.1.4 emits a scalar zero count followed by an invalid
scalar-to-vector `trunc`, crashing or producing incorrect code depending on
the vector's width. Element-wise conversions, shuffles, non-boolean builtins,
reductions and logical operations take at most 1024 elements, bounding their
expanded entry runs; packed bit operations do not unpack those lanes. Past that
(`BUSTER_CHECK_LOWER_TRANSPORT_LANES`) they stay refused by name, where clang
compiles them (#1834's sweep: `ext_vector_type(1025)`): LLVM splits such a
vector into its legal registers' pieces, while these lowerings expand an entry
run per element, a function's entries growing with the lanes. Matching clang
there takes conversions, shuffles and reductions at the registers' width, as
the operators' pieces are (`check_lower_transport_rows`), not wider bounds;
#2216 lists them with the other refusals that hold at every target.

`test-checker --program-vectors` runs a program of all of these against clang
-O0's build of it at x86-64-v4 (`vector-transport-compute`) and at x86-64,
x86-64-v2 and x86-64-v3 (`baseline-transport-compute`), on both paths; the
packed boolean builtins have matching `vector-boolean-builtins` and
`baseline-boolean-builtins` cases covering 1 through 1024 lanes, padding,
neighboring-byte guards and argument evaluation. The refusals' fixtures name
each refusal. The same programs pass by the driver
with `--scalar`, by default and with `--tier1 --verify` at x86-64, x86-64-v3 and
x86-64-v4.

## Against clang -O0's objects

clang emits no unreferenced static or inline function, even at `-O0`, nor an
object that only such a function refers to, and every static local of a
function it emits: `clang -O0 -c`'s object is #129's oracle. `c_lowering_symbols.c`
reads an object with the import path's reader and lists the functions, objects,
thread-locals and IFUNCs it defines, with their bindings, but not the
assembler's local labels (`.L` names: string literals, which clang names
`.L.str`). Two objects' lists compare as multisets.

Names compare exactly but for the uniquing suffix of the ones a compiler makes.
LLVM takes a repeated name's `.N` from one counter for the module, which string
literals' names advance too, and the emitter from its own, so their numbers
differ. The checked source gives the comparison the bases of its made names, a
block-scope static's `function.name` and `.compoundliteral`, and its asm labels
(`lowering_names_of_input`): a local named by a base, `.` and N compares as the
base and the fact that it had a suffix, and every other name exactly, an asm
label above all, which is the user's name even where it ends as a suffix does
(`tag.1`, or `f.x.1` beside `f`'s statics). An identifier has no dot, so only a
label could spell a made name. `test-c-testsuite` compares every name exactly:
none of its subset's programs makes a name twice in clang's objects.

- **Fixtures** (`test-checker`, in `c_check_program_select_test.c`): every
  emitted fixture's object is compared with `clang -std=gnu17 -O0 -c`'s,
  functions, objects and string literals (the strings of mergeable string
  sections). Besides the existing selection fixtures, one for each kind of
  definition: static functions referenced, unreferenced and referenced only from
  an unreferenced one; C99 inline-only, inline made external by an `extern`
  declaration, `extern inline` and static inline ones, used and unused; an
  inline-only definition called through its external symbol; GNU inline in both
  spellings; addresses taken in a body, in a global's initializer, a referenced
  static's and an unreferenced static's; objects referenced, unreferenced,
  tentative and reached through a chain of initializers; block-scope statics of
  emitted and unreferenced functions, two of them of one name; string literals of
  emitted and unreferenced definitions; and `used` functions and objects. And
  the comparison's own check: asm labels `tag.1` and `tested.x.1` beside two
  statics `x` of `tested`, whose object must agree with clang's (`tested.x` and
  `tested.x.2` in both) and differ from it once a name in its string table is
  replaced: `tag.2` for the label, `tested.x` for the made `tested.x.2` (the
  suffix lost) and `tested.x.3` for the label `tested.x.1`. Constant `if`s'
  fixtures leave their dead arms out (#1108), in `-c`'s selection and in
  clang's model alike, and their objects are compared with clang's too, and
  run: a chain of `sizeof` conditions, dead arms with a label, with an outer
  `case` and with their own `switch`, a true condition with an `else`,
  floating, pointer and side-effecting conditions, an `else if` chain, dead
  arms' block-scope statics, a condition whose `&&` leaves a call unevaluated,
  dead arms' statement expressions with a `break` and a `continue` (in the body
  and inside a live statement expression) and a dead arm's `asm goto`. And
  the reasons below on one fixture of each (`c_check_corpus_symbols_test.c`),
  through `check_corpus_measure` as `--symbols` runs it, must all be
  explained, what only constant `if`s' dead arms reach must make no difference
  at all, and a function clang alone sees renamed must not be.
- **c-testsuite** (`test-c-testsuite --cross-check`): each program's `-c`
  object against `clang -O0 -c`'s, functions and objects. All 220 programs
  agree; the backend compiles every one since it lowers `long double`.
- **Corpora** (`test-checker-corpus --symbols`): each file the checker accepts,
  its object built as `-c` builds it, against `clang -O0 -c`'s of the file with
  the corpus's options. A file the backend refuses is counted with its feature,
  and `--symbols-minimum N` fails the run when fewer than N files' objects were
  compared. The 9700X job compares eight of the frozen musl `src` directories
  (`math`, `string`, `stdlib`, `fenv`, `legacy`, `process`, `signal` and
  `multibyte`: 542 files the backend lowers, 354 before it took musl's
  register variables, x87 operands and prefix statements, and 351 before it
  lowered inline assembly at all (#147), with every explained difference of
  musl's) with `--symbols-minimum 542`. `sqlite3.c`, which the backend once refused,
  compares too: at `d12fd583`, with one difference, inlined.
- **`main.c`**, the compiler's own unit, with `build.sh`'s flags for a plain
  `-march=znver5` build, whatever the run's host, sanitizer and fuzzing: every
  correctness run compiles it by `-c` and by `--lower-all`, each with
  `--verify`, the scalar path's `--lower-all` object byte for byte the vector
  path's, and compares its checking, by both paths, and its `-c` object with
  clang's (`test-checker-corpus --symbols-minimum 1`). The TSan and MSan rows
  run the compiles in a separate step after the suite, and leave out only the
  checking comparison, whose unit (4,864,411 tokens on 2026-11-10, with
  `-DBUSTER_COMPLETE_DRIVER=1`) exceeds their hosted runner's memory
  (docs/ci.md).
  At `d12fd583` its 12,016 differences are all explained: 7 folded, 11,845
  dropped and 164 inlined. The dropped ones were the other units' commands,
  each behind a `BUSTER_IN_UNIT` branch whose condition is a constant, which
  `-c` now leaves out as clang does (#1108).

On corpora, clang -O0 decides three things in its code otherwise than the
selection. The harness explains a difference with one of them only from the
checked program, never from a name, and fails on any other:

| Reason | Which side | What clang does |
| --- | --- | --- |
| folded | ours only: an object that a selected body reads, a const and not volatile scalar with an initializer | Reads its value as a constant (`CodeGenFunction::tryEmitAsConstant`), so nothing needs the object. musl's `math` constants (`pS0`, `pio2_hi`) and Lua's `HOOKKEY` and `CLIBS`. |
| unselected | clang only: a definition the selection reaches when it also counts the operands that a constant condition never evaluates (`CheckProgramOptions.unselected_operands`) | Emits a `?:` in a branch's condition as branches without folding a constant condition (`CodeGenFunction::EmitBranchOnBoolExpr`), so the dead arm's calls stay. musl's `isnan` and `signbit` call `__FLOAT_BITS` in the dead arm for a `double`. |
| inlined | ours only: a local function whose definition has `always_inline` | Inlines it at its calls at -O0 too (its always-inliner) and emits none of a local function it inlined everywhere, as SQLite's `SQLITE_INLINE` `allocateSpace` is. Tier 0 doesn't inline: it calls the function, so the object defines it. |

Until #1108 there was a fourth, dropped: a definition only ours had, which
clang's model left out because only a constant `if`'s dead arm reached it
(musl's `FORCE_EVAL` calls `fp_force_evalf` and `fp_force_evall` in the arms a
`double` never runs). `-c` leaves such an arm out now as clang does (the source
path's "Constant `if`s"), so the selection's model is the program with the
unselected operands counted alone, and what a dead arm reaches makes no
difference. The counts below that name dropped differences are from before it.

The model doesn't name string literals, which have no symbols: the corpora
compare no strings.

The harness finds the definitions that may explain a difference by binary
searches (`check_corpus_symbol_index`): once per file it sorts the selections'
symbols, data entries and bodies by the names they are compared by, then by the
order in which a pass over the selection would meet them, which is the order in
which the reasons take them. On the 9700X, a pass per difference made
`main.c`'s comparison take 13.7 s, its 12,016 differences against a selection
of 10,105 functions and 125,262 objects at `d12fd583`; with the index it takes
8.8 s, and musl's eight directories take 6.3 s either way
([the record](https://github.com/buster14a/buster-performance/tree/bcb44f9de7a071a502e4c81ebf9afe52d085c3b5/docs/measurements/main-c-ci-gates-129-2026-10-30)).
Both give the same counts there, and on the Zen 4 laptop on `sqlite3.c` and
c-testsuite too.

Results on the Zen 4 laptop, with clang 23.1.3 (`--target`'s configuration of
each corpus as `test-checker-corpus` runs it):

| Corpus | Accepted | Compared | Explained differences (folded, unselected, dropped before #1108) | Unexplained | Refused by the backend |
| --- | --- | --- | --- | --- | --- |
| c-testsuite (220) | 220 | 220 | 0 | 0 | 0 |
| musl 1.2.5 `src` (Zig 0.14.1's copy), 1,524 files, `-std=c99 -ffreestanding` | 1,484 | 546 | 383 (210, 59, 114) | 0 | 937, and 1 the backend fails on |
| Lua 5.4.8 `src`, `-std=gnu99 -O2` | 33 | 13 | 2 (2, 0, 0) | 0 | 20 (`visibility`) |
| `sqlite3.c` | accepted | 0 | | | 1 (an `__uint128_t` local: values aligned past 8 bytes) |

The backend's refusals are its features, not the selection's: most of musl's
are attributes (aliases, weak symbols, visibility, 480), inline assembly (384)
and `_Complex` (64). Since Tier 0 lowers `long double` (#423), musl's files
that only refused it are compared, and its `FORCE_EVAL`'s dead arms are the
dropped differences. The backend fails on `stdio/vfwprintf.c`: an internal
error lowering it (a last operand of another class than the entry before it),
which main has too, outside the selection.

On the 9700X, with its frozen corpus (musl 1.2.5 with the headers its Makefile
generates, clang 23.1.1, `-march=znver5`, one run over `src` at `ddf3537c`,
merged with main's `4f4a0bfa`), 1,484 of the 1,524 files are accepted, as clang
accepts them; 544 are compared, with no unexplained difference, 939 refused by
the backend, and 1 the backend fails on, `stdio/vfwprintf.c`, as on the laptop.
The explained differences are all in the 9700X job's eight directories, which
give the same counts alone: 210 folded, 59 unselected and 114 dropped. Those
directories, `math` and seven small ones with many files the backend lowers,
compare 348 files.

## What the selection saves

```sh
./build/compiler benchmark-lowering-source [--passes N] [preprocessor options] file...
```

prepares each file as `-c` does (the shared preparation, [the request
guide](compilation-requests.md): its options refused as the driver refuses
them, its program for their target and predefines, `-c`'s target-feature check
of its selection), outside the time, prints its configuration's receipt, then
times in turns the two selections, lowering, Tier 0, branch sizing, encoding, the link's code,
the static data's publication, section/symbol preparation and building the
object in memory. Unwind generation and verification are disabled in this
benchmark; preprocessing, checking, program/static-data preparation, the outer
per-file arena reservations and warm-up, and writing the object to disk are
outside the time. Internal emitter allocations remain measured.
It reports `--passes` (21) medians and each file's functions and objects defined,
lowered and skipped, both medians and the time saved, then the totals over the
files that lower both ways. A file the checker rejects is left out of the
totals, and so is one refused by name, with the reason the report names: a
statement the checked program's preparation marks unsupported
(`CHECK_PROGRAM_UNSUPPORTED`), or a feature that either way's emission or
publication of the static data marks so (`CHECK_PROGRAM_UNSUPPORTED`,
`CHECK_DATA_UNSUPPORTED`). Lowering everything also lowers what nothing calls,
which the backend may refuse so: then the file is reported with both outcomes.
Any other failure leaves its file out and makes the command exit with status 1:
a file that doesn't preprocess or that exhausts the checker's resources, a
preparation or a warm-up that fails otherwise (the backend's, a resource's, an
invalid input's), and a timed run that fails or lowers other than its warm-up
did (the same counts and object size; it does not compare every output byte).
So does a run that measures no file.
`test-checker` checks each outcome, on fixtures of its own and on failures
injected where the real ones arise, with the status each would have
(`c_lowering_source_benchmark_test.c`).

The unchanged `src/buster/compiler/main.c` now lowers both ways at
[`20e01ceb`](https://github.com/buster14a/buster/commit/20e01cebc77a0142260f3e0a6b3e9c66228ef3bf).
This includes its section pragmas and file-scope assembly (#619) and its
header intrinsics, including instructions absent from `znver5`. On the locked
Ryzen 7 9700X, with Clang 23.1.4's headers and `build.sh`'s captured
`-march=znver5` flags, eleven separate benchmark processes each ran 21
alternating paired passes on CPU 2:

| Definitions | Defined | Referenced only | All |
| --- | --- | --- | --- |
| Functions | 15,489 | 10,098 | 15,431 |
| Objects | 161,056 | 124,950 | 161,056 |

All 15,431 emit-capable functions lower in all-definition mode. The other 58
definitions are inline-only external bodies, checked but excluded from
out-of-line emission by the C/GNU inline-linkage policy even with `--lower-all`.
Referenced selection omits 5,333 emit-capable functions and 36,106 objects;
the object count includes pooled strings and compound literals.

| Warm post-checking work | Median across 11 processes |
| --- | --- |
| Referenced only | 278.939083 ms |
| All definitions | 299.961666 ms |
| Paired time saved | 20.882761 ms |
| Paired fraction saved | 6.963378% |

The saving is the median of the eleven paired process results, so it is not
the difference of the two medians in the first rows. Its rank-2-to-rank-10
interval is 20.764750–21.489129 ms (6.919437–7.150007% for the paired fraction),
with nominal 98.828125% coverage under independent, identically distributed
process samples. These are warm lowering-and-object-construction savings,
with unwind generation disabled, rather than whole-compilation savings.

Separate actual `-c --verify` and `-c --verify --lower-all` gates both succeed
with unwind generation enabled. Strict Clang-O0 symbol comparison of `main.c`
also succeeds, with zero unexplained differences, unsupported files or failures.
Every correctness run repeats these gates since (the `main.c` item of "Against
clang -O0's objects"). At `d12fd583` they pass with 15,496 functions defined,
10,105 lowered by `-c` and 15,438 by `--lower-all`, and 161,406 objects, 125,262
of them lowered by `-c`.
Instruction/configuration oracles and native memory/ABI checks cover AMX,
XOP, AVX512-BMM and AVX512-FP16; this does not claim execution of those
instructions on the Ryzen host. The
[measurement record](https://github.com/buster14a/buster-performance/tree/824a03c24f17805217e17065d2004eb541d12aa7/docs/measurements/rescued-126-main-lowering-2026-10-30)
retains the exact source/binary identities, inputs, commands, raw results,
qualification and statistical method. This supersedes the blocked 2026-10-27
[`main.c` survey](https://github.com/buster14a/buster-performance/tree/b2e5db81/docs/measurements/main-c-lowering-survey-136-2026-10-27).

In an earlier 9700X run (`benchmark-lowering-source` on
c-testsuite's programs, 21 passes, run 40179359967), the 219 programs that lowered
both ways defined 341 functions and 682 objects; the selection lowered 322 and 645
and skipped 19 and 37. The skipped functions were glibc's static inline helpers
that `<stdlib.h>` defines (`__bswap_16`, `__bswap_32`, `__bswap_64` and the three
`__uint*_identity`), which lowering only what is referenced saves where a program
includes it:

| Program | Functions (defined, lowered) | Referenced only (µs) | Everything (µs) | Saved |
| --- | --- | --- | --- | --- |
| 00040 | 9, 3 | 26.5 | 31.8 | 16.5% |
| 00181 | 10, 4 | 63.4 | 72.4 | 12.4% |
| 00182 | 11, 5 | 43.2 | 48.6 | 11.2% |
| all 219 | 341, 322 | 1,676 | 1,696 | 1.2% |

A program that skips nothing costs the same either way within this older
run's noise. These small units and their historical source differ from the
`main.c` measurement above.

That comparison is within one tree. Against main, on the same work, a harness of
`benchmark-lowering-source` reduced to the driver's selection
(`check_program_select`, which both trees have) was compiled into main at
`5863eeda` and into this work, and timed c-testsuite's 219 programs in
alternating rounds on the 9700X, each run under the lock and pinned to CPU 2
([the record](https://github.com/buster14a/buster-performance/tree/34d55c24d9d74f452c51c9919bca6d48c0316220/docs/measurements/lowering-emit-418-2026-10-25)).
The first version of this work took 4.1 to 5.1% longer than main, round by
round: its emitter built a table of every symbol's name, and added the source's
names in a pass of their own, in every unit, so that no name it makes is one of
them. Most units make none, and now only a unit that makes a name builds the
table; that took 0.8 to 1.9% longer than main (a median of 1,673,043 ns over the
219 programs against main's 1,653,603), with the same objects as before. The
rest is in this work's other changes on that path, which the runs don't tell
apart: the edges from a body to its block-scope statics, and the scan for a made
name.

[The planner's qualification record](https://github.com/buster14a/buster-performance/blob/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/c-lowering-2026-10-23/README.md)
retains the planner's original synthetic measurements.

### Whole-unit passes after checking (#442)

[#442](https://github.com/buster14a/buster/issues/442) attributed that rest, on
the same harness split by stage, with profiles and ablations of #418
([the record](https://github.com/buster14a/buster-performance/tree/3fdc427d36380adc830f5484cc3cf5a08bd3149c/docs/measurements/lowering-scans-442-2026-10-26)).
The attribution was made in a cloud VM on an Intel Xeon, without counters; the
comparison below is the 9700X's, under its lock. The path's time after
checking went mostly to passes over every token of the unit, most of them its
headers', which predate #418: the selection's loop for declarations and its
numbering of the needed objects over every object, the emission's three loops,
the lowering's loop, and `abi_initialize` clearing the ABI table's columns for
the type table's whole room, which the checker sizes at four times the unit's
tokens. #418's excess grew with the tokens there: its selection's isn't its new
edges (an ablation without them is no faster), but the per-token loop's code,
and about two thirds of its emission's is `c_check_program_emit.c`'s (an
ablation with main's file takes it back).

The lowering visits the tokens that declare or start an initializer, which
`check_program_declaring` gives 16 at a time with a register compress; the
selection counts and numbers its needed objects 64 at a time
(`check_program_needed_count`, `check_program_needed_number`); each has an exact
scalar twin, which `test-checker` compares at `PROT_NONE` pages. The lowering's
ABI table has rows for its type table's types alone (`abi_initialize_types`):
the lowering makes no type, and classifies only the types it has rows for. The
selection now reads the static declaration keys retained by `check_data_prepare`
(#592). The emission finds static-storage declarations 64 tokens at a time before
the bodies compile on lanes, and initializes defined bytes only for symbol objects;
other entries are not readable. After those compiles it finds the tokens with attributes
16 at a time, skipping 256 tokens without any with a load of their directory entries
(`check_input_attributed_next`, #1167), retains those at declaring tokens and gives the list back after
the attribute passes, never holding it through Tier 0. It initializes retained flags only
at those objects, including parameters. Of the
names a unit gives, only asm labels with a `.` go in the made names' table,
since every made name has one and no identifier does.
Every c-testsuite object, by both paths, is byte for byte what main wrote before.

On the 9700X, pinned to CPU 2, c-testsuite's 220 programs, each file's least
run minimum over 6 rounds of 101 passes, summed:

| | Main before #418 | #418's merge | Main at `7e3782a` | #442 (`8a81ea9`) |
| --- | ---: | ---: | ---: | ---: |
| All 220 (ns) | 1,856,242 | +1.8% | +4.6% | −17.3% |
| The 216 whose output #418 kept | 1,718,753 | +1.7% | +4.4% | −17.5% |
| The 4 whose output #418 changed | 137,489 | +2.8% | +6.6% | −14.7% |
| Under 300 tokens (154) | 474,650 | +0.7% | +4.7% | +0.9% |
| 3,000 tokens or more (60) | 1,304,588 | +2.2% | +4.5% | −24.3% |

Every round's sum agreed (#442 −16.9 to −17.9%). Against main at `335424b`,
after the function lanes (#151), the change merged with it is −19.8% in all,
−2.5% under 300 tokens and −26.5% from 3,000, every round; and on the
record's synthetic units of 200 and 2,000 definitions, whose few tokens leave the
whole-unit passes little to cost, −3.7 to −7.9%. The Xeon gave −25.5% in all
against main before #418, and +4.0% under 300 tokens. The smallest units, which
have few tokens to skip, kept a fixed cost: chiefly the ABI classification of
the 42 present builtin types in the 64-index builtin prefix on every lowering.
The later [immutable builtin projection](calling-convention.md#immutable-builtin-facts-442)
reuses those rows without a mutable cross-unit cache; derived classifications
and dependency scheduling remain private to each unit. The historical numbers
above predate that change and do not measure its effect.
These are the post-checking path's times, not the whole compiler's, and not what the selection saves against `--lower-all`.

### A small unit's fixed cost after checking (#442)

A rebaseline on 2026-11-06 compiled the same harness into main before #418 (`3bfb4a72`), #418's merge (`54703dcf`) and
main at `ec5f0f6b`, and ran them on the 9700X under its lock, pinned to CPU 2
([the record](https://github.com/buster14a/buster-performance/tree/73009c83386222d63bc625b657c4c4a52e8c5240/docs/measurements/lowering-fixed-cost-442-2026-11-06)).
#418's merge still cost what it did, +1.6% on c-testsuite and −0.3% under 300 tokens. But main's path after checking
had grown to +54% over main before #418, and to +177% on its 154 programs under 300 tokens: a 9-token unit took 6.8 µs
where main before #418 took 1.7. #418 didn't add that. Three pieces of every unit's setup did, each the same in every
unit:

- **Lane 0's writer's columns.** #531 put them in an arena of base pages, which a `CheckProgramLanes` keeps while it has
  their room. But an emission without lanes reserved one and released it each time, and the C driver released a units'
  lane's lanes after each unit (`check_driver_object`): an `mmap`, `mprotect`s, a `getrlimit` and an `munmap`, and a
  fault for each page the columns' starts touch. The calling thread now keeps its own (`check_program_thread_columns`),
  and a units' lane its lanes' first from one unit to the next (`check_program_lanes_settle`).
- **The runtime functions' names.** The preparation looks up about 185 of them in the unit's identifiers, so that a
  call of one takes the symbol of the unit's declaration of its name, and it measured and hashed each spelling for
  every unit. Their lengths and hashes are baked with the backend's tables (`check_lower_runtime_names_initialize`), so a
  unit's lookups are its table's probes.
- **The builtin types' rows.** The 64 builtin types' classes and facts were classified by the rules for every unit.
  The vector path copies them (`check_lower_builtin_classes` and `check_lower_builtin_facts`, a 64-byte vector each,
  plain char's class by `TypeTable.char_unsigned`), and `test-checker` compares the copy with its twin's rules with
  plain char signed and unsigned.

c-testsuite's 220 programs, each file's least run minimum over 4 rounds of 101 passes, summed:

| | Main before #418 (ns) | #418's merge | Main at `ec5f0f6b` | #442 (#1568) |
| --- | ---: | ---: | ---: | ---: |
| All 220 | 1,865,081 | +1.6% | +54.4% | −23.1% |
| The 216 whose output #418 kept | 1,727,545 | +1.5% | +58.8% | −22.2% |
| The 4 whose output #418 changed | 137,536 | +3.3% | −1.2% | −35.4% |
| Under 300 tokens (154) | 481,282 | −0.3% | +177.3% | +7.6% |
| 3,000 tokens or more (60) | 1,306,044 | +2.4% | +10.1% | −34.2% |

Every round's sum agreed (−22.6 to −23.8%), and against main at `ec5f0f6b` the change is −50.2% in all and −61.1% under
300 tokens, every file faster. Under 300 tokens the kept columns took −52.0% off main's time, the baked names a further
−11.2% and the copied rows a further −10.6%. What the smallest units keep, about 240 ns a file over main before #418, is
setup that main added since #418: in the emission (+5.6%) the names' probes and the rest of the lowering's setup, and in
the object (+36%) the definitions' named sections (#619) among it. The synthetic units of 2,000 definitions, whose few
tokens leave the per-unit setup little to cost, are 3.4 to 7.8% faster than main at `ec5f0f6b`; their emission is at
most 2.6% over main before #418's, and their selection, about twice its, is per-definition work main has added since,
not #418's. The whole compiler gains less: `-c --lanes 1` of all 220 programs at once, through the units' lanes, took 1.51%
less than main at `ec5f0f6b` (8 of 8 paired runs), as a small unit's compile is mostly its checking. Every object, of
c-testsuite's programs by `-c`, `-c -g`, `-funsigned-char`, `--scalar` and the units' path, and `main.c`'s, is byte for
byte main's.

### The definitions' selection and a small unit's rest (#1629)

[#1629](https://github.com/buster14a/buster/issues/1629) took what #442 left: the units under 300 tokens, about 240 ns a
file over main before #418, and the synthetic units of 2,000 definitions, whose selection took about twice main before
#418's ([the record](https://github.com/buster14a/buster-performance/tree/c83998671d62edf619b720c0fe5fa507f26a8684/docs/measurements/lowering-definitions-1629-2026-11-07)).

- **The definitions' selection.** Each definition searched the data's declared objects for its object
  (`check_data_declared_index`, 12 dependent loads for 4,000 static objects), and each block-scope static the bodies for
  the one holding it (`check_program_token_body`). The declared objects' places are now written at their objects in a
  column that the read checks, so nothing clears it (#877's way: `check_program_declared_fill` and
  `check_program_declared_at`), and the statics' bodies are taken by a cursor that walks forward in the definitions'
  token order, searching only for a token before the last one asked (`check_program_token_body_next`).
- **The runtime functions' names.** The preparation tested each of the library's 394 entries against the x86 builtins'
  calls in turn, which took as long as the probes of a unit that calls none; it now steps from one called library
  function to the next by the set bits of `called` (`check_lower_runtime_called_next`).
- **The emission's clears.** One lane's emission cleared its 6.3 KB lane state twice, all lanes' before the run and lane
  0's again for the run alone, which writes every field of it; the lanes' are cleared only for a run on lanes. Each
  writer cleared 3.5 KB of its lane's `runtime_symbols` and `runtime_order`, which only the program's lowering numbers
  (`check_lower_finish`); they are `CheckProgramLowered`'s last fields, which a lane's copy leaves unwritten. And its
  `runtimes` list, which its count bounds, is a column of its scratch.
- **The object's classes.** With descriptors, as every `-c` compile's object has (`check_program_sections`), the object
  writer grouped the atoms of each of its 14 classes, even those without atoms, most of a small unit's; a class without
  atoms now takes no groups.

On the 9700X, pinned to CPU 2, c-testsuite's 220 programs, each file's least run minimum over 4 rounds of 101 passes,
summed:

| | Main before #418 (ns) | Main at `53854a7c` | #1629 |
| --- | ---: | ---: | ---: |
| All 220 | 1,860,108 | −23.5% | −25.4% |
| Under 300 tokens (154) | 475,585 | +5.9% | +0.1% |
| 3,000 tokens or more (60) | 1,307,328 | −33.9% | −34.5% |

Against main, −179 ns a file under 300 tokens (paired rounds −3.9 to −6.3%) and −2.2% in all (−1.6 to −3.1%). Under 300
tokens the selection is −6.8% against main before #418 and the emission −2.8%; what stays over it is the setup main has
added since: the publication, +27% (+1.5 µs over the 154 programs), and the object, +35% (+9.9 µs), the definitions'
named sections (#619) among it. On the synthetic units of 2,000 definitions the selection is now below main before
#418's on every unit (statics 108,123 ns against 113,423; main's 256,481), and their totals +0.5 to +8.0% over it
(main's +6.8 to +17.2%), and literals −15.9%; what keeps them over it is the publication, about +25%, and the object,
+14 to +16%, per-definition work that main had already added and this leaves as it was. The whole compiler shows none
of it: `-c --lanes 1` of all 220 programs took 72.4 ms against main's 72.3 (+0.19%, 3 of 8 paired runs faster), the 35
µs saved being under the runs' spread. Every object, of c-testsuite's programs and the synthetic units by `-c`, `-c -g`,
`--scalar` and `-funsigned-char`, and `main.c`'s, is byte for byte main's.

Three more changes were measured and not kept. A lookup of each runtime name by its home bucket alone, without a
branch, meant to overlap the names' loads, cost +141 ns a file under 300 tokens: the lookups' count of the names found,
which places each result, then waited on every load, where the branchy lookup's predicted absence starts the next
lookups' loads at once. Prefetching the names' home buckets before the lookups, and making the helpers' key table only
for a unit with builtins, showed no gain.

### The publication's and the object's rest (#1629)

What #1658 left of #1629 was the publication's and the object's excess over main before #418
([the record](https://github.com/buster14a/buster-performance/tree/fbb71141383b4c65f471b403bcd3de40f7b2d852/docs/measurements/lowering-object-1629-2026-11-07)).
The harness timed the object stage a second time, warm, with the clock stopped after each of its calls, on the 9700X.
Under 300 tokens (ns a file):

| | Main before #418 | Main after #1658 | Constant names |
| --- | ---: | ---: | ---: |
| Publication | 37.3 | 48.5 | 48.2 |
| Object, as run | 184.8 | 257.9 | 245.8 |
| Warm: `link_check` / `object_prepare` / `object_build` | 6.5 / 38.9 / 68.3 | 10.4 / 54.2 / 100.1 | 10.7 / 54.1 / 104.9 |

- **The removed part:** every object took `strlen` of each class section's name twice (`object_section_name_bytes`,
  `object_fill_headers`). `ObjectSectionFormat.name_length` now holds it, from the literal. The object stage is −12 ns a
  file against main (every round), and the path's total under 300 tokens 1.8% below main before #418's.
- **What stays is work main has added since #418,** each piece a few ns a unit or about one a symbol (the 2,000-definition
  units: each call about 1 ns more a symbol, the publication 8 to 9 ns more a static object):
  - COMDAT groups (#1268): `link_groups_valid`, in `link_check` and `object_prepare`, and `object_build`'s groups
    passes;
  - named sections (#619), and the debug sections' passes;
  - the publication's plan of each object before its bytes (`check_data_plan_object`), and its proof that an initialized
    object is nonzero, for clang's `.bss` placement (`check_data_initializer_witness`).
- **No single hotspot:** profiled alone, no line of the object stage holds more than about 5% of it (the largest is
  `object_count_sections`' loop over its 14 classes).

One opportunity remains outside the path: `link_check` checks every atom's fixups again, as before #418, which `-c`'s
one-lane adds have already checked. Skipping it needs `Link` to record which columns lanes filled unchecked.

`-c --lanes 1` of all 220 programs: −0.84% against main (7 of 8 paired runs faster). Every object of c-testsuite's
programs and the synthetic units, by `-c`, `-c -g`, `--scalar` and `-funsigned-char`, is byte for byte main's.
