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
  reaches, and a loop that runs keep both arms. Some shapes that clang folds
  keep both arms here, which runs alike: a statement expression with a jump in
  code its evaluation skips (`if (0) return 0;`, a `return` or `goto` in a
  loop whose condition is false or in a constant `switch`'s body before any
  label) or that initializes a `volatile` object, which the checker counts as
  side effects of the statement expression; a loop that a `break` leaves
  (`while (1) { break; }`, `do { break; } while (0)`) and a constant `switch`
  with a `case` or `default` label, which the walk doesn't model; a `const`
  local's read as its value, which the checker gives no constant; and an
  address the walk doesn't take (an element's, a variable-length array's
  decay). The
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
  a constant condition never evaluates is never allocated, nor read. A constant
  `switch`'s cases that clang skips (`CodeGenFunction::EmitSwitchStmt`'s
  `FindCaseStatementsForValue`) are still written and selected.

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
scalar expression expansion remains the oracle (`--scalar`).

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
evaluated expression occurrences, those handled by vector operation fragments,
their percentage, and records classified; then the evaluated nodes and the
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

It is still slower than the tasks. On this session's host (an Intel Xeon whose
build is the scalar host path), `sqlite3.c`'s lowering took 54 to 58 ms by the
tasks and 67 to 72 ms with the templates, 1.24 to 1.25 times (21 passes, 3
runs, pinned). The first version of the pass, with its two passes and the
general rules for every node, took 1.30 to 1.37 times. callgrind on Lua's units
(a baseline build) counts the pass's instructions:

| pass | instructions | per node of the ranges |
| --- | ---: | ---: |
| the first version | 36.9 M | about 450 |
| the three steps, the general rules for every node | 40.2 M | about 495 |
| the three steps with the fast twin | 32.0 M | about 395 |

The splices save about 12.4 M of the tasks' 111.8 M there. The pass's cost is
spread over its steps (the scatter about a fifth, the fast twin a third, the
classification step's bookkeeping a quarter) with no one line more than a few
percent: the scalar steps can't pay by trimming. So no compile takes the
templates (`CheckLowerShared.templates` is set only by
`check_program_lower_templates`), and the default compile's objects are the
same as before (`sqlite3.c`'s byte for byte). What remains is the issue's
point: the three steps 64 nodes at a time. The scatter's table is by op
alone. The fast twin's shapes read the stream's columns and #570's classes and
facts. The closure can run over prefix sums of the slots' counts. The vector
pass's results are to be checked against these scalar ones, and the lowering
against the tasks, by the same command.

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
  checking comparison, whose 6,323,682-token unit exceeds their hosted
  runner's memory (docs/ci.md).
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
