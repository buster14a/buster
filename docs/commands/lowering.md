# lowering

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Selective lowering

`./build/compiler test-lowering --clang` compares selective function/object
emission planning with an independent graph oracle and Clang object symbols
on original, manually modeled C fixtures. `benchmark-lowering` measures
synthetic graph preparation and vector/scalar selection; use the shared 9700X
lock and pin timings to CPU 2.

On the source path, `check_program_select` (`c_check_program_select.c`) gives
the planner the checked program's definitions, roots and evaluated references,
and `-c` and the direct path lower what it selects. A constant `if`'s dead arm
is never written, as clang -O0 doesn't emit it (#1108, the lowering guide's
"Constant `if`s"): what only it reaches, a block-scope static it declares
among them, is neither selected nor checked for target features, so
`main.c`'s units emit no other unit's commands and a baseline build no
`BUSTER_VECTOR_PATH` kernel. `c_lowering_symbols.c`
compares an object's functions and objects with `clang -O0 -c`'s: `test-checker`
on fixtures of each kind of definition (functions, objects and string literals),
`test-c-testsuite --cross-check` on c-testsuite and `test-checker-corpus
--symbols` on any corpus. Every correctness run compiles the compiler's own
`main.c` with `build.sh`'s flags for a plain `-march=znver5` build and
`-DBUSTER_COMPLETE_DRIVER=1` (all of the compiler's code; build guide), by `-c` and
by `--lower-all` (`--verify`, and the scalar path's `--lower-all` object the
vector path's byte for byte), and compares its symbols with clang's
(`test-checker-corpus --symbols-minimum 1`), so that it keeps compiling both
ways. `benchmark-lowering-source [--passes N]
[preprocessor options] file...` measures what lowering only the referenced
definitions saves against `--lower-all`, after checking, with each file's
functions and objects defined, lowered and skipped. It times warm selection,
lowering and object construction in memory, with unwind generation and
verification disabled; it excludes checking and file writing. A file the checker rejects,
or that is refused by name (`CHECK_PROGRAM_UNSUPPORTED`,
`CHECK_DATA_UNSUPPORTED`), is left out; any other failure, a timed run that
lowers other than its warm-up, and a run that measures no file fail the command
(`test-checker` checks each, `c_lowering_source_benchmark_test.c`). Run it under
the 9700X's lock with `taskset -c 2`. [The lowering guide](../../docs/c-lowering.md) records the
contract, the comparisons' results and the completed `main.c` measurement.

## Jump-table address reuse (#1217)

Each switch dispatch emits one `GLOBAL_ADDRESS` followed by `DUP`. The indexed
signed-offset load and target addition therefore share the same table value in
both the typed stream and its IR lowering; value numbering need not rediscover
that equality. `CheckLowerTable.address` records the one provisional-symbol
entry that final lowering patches. Table entries, labels, bounds checks and
relative fixups keep their existing format.

The producer also publishes `Tier0Function.table_addresses`, one typed entry
index per table in dispatch order. Both Tier 0 validators require strictly
increasing indices before their matching jumps, naming `GLOBAL_ADDRESS` of
class U64 and a nonzero, representable direct symbol. GOT and TLS accesses are
refused. A null column preserves ordinary entry lowering. Tier 0's emission
does not move these entries; `ir_lower` emits the listed symbol addresses and
their U64 conversions in its genuine entry preheader, then reuses those values
at the original entries. Source backedges, including those to source entry
zero, cannot reexecute the preheader. This uses the existing count/fill replay
and a monotone cursor, with no per-entry map or general loop-motion pass.

`test-checker` checks one correctly patched table-address entry in every
jump-table fixture, including nested tables and signed/unsigned bounds, and
its matching metadata. `test-ir-lowering` checks optional metadata, two tables,
a mixed computed goto and table, an unreachable table before a live one,
source-entry-zero backedges, SSA dominance and transactional refusal of
malformed address metadata through both validator and lowering paths.
`test-checker-driver`'s Tier 1 labels fixture executes two switches with holes,
defaults and nonzero signed/unsigned lower bounds against Clang and Tier 0.
Both selector paths, with rematerialization enabled and disabled, must emit one
relocated address per table in that fixture. A separate low-pressure loop in
the same source must decode to adjacent `movsxd [base + index*4]`, `add base`
and `jmp` records, with the loaded offset, addition destination and jump target
in the same register. Its single relocated table base must precede the decoded
backedge target.

Hoisting lengthens table-base live ranges and computes the bases even when a
switch is not reached. Allocation can spill or rematerialize a base under
pressure; the small loop's three-record check does not establish the same shape
in SQLite or Lua. Whole-corpus instruction counts and runtime acceptance still
require the 9700X measurements. General LICM remains disabled.

## Sparse-switch search trees (#1371)

Tier 1's emission asks `check_lower_prepare` to retain a balanced dispatch plan
for a sparse switch with at least 128 nonempty case intervals. Dense jump tables
keep their existing admission rule and take priority. Smaller sparse switches,
Tier 0, and selectors outside the exact signed/unsigned 32- and 64-bit classes
keep the existing comparisons. The threshold preserves the issue's competitive
100-case chain; it is a conservative policy boundary, not a measured crossover
for every workload. Current-revision runtime and compile-time qualification on
the 9700X is still required before making a speedup claim.

The checked controls already retain the selector, converted case bounds and
original labels. Table preparation sorts those intervals once and, when a tree
is selected, retains an immutable slice in shared scratch for the lowering
lanes. Its records retain the original case index separately from the signed
ordering key. GNU ranges stay intervals, empty ranges are omitted, and wider or
nonstandard-width `_BitInt` selectors retain their existing lowering.

`c_check_program_sparse_switch.c` emits the tree at the first case, after the
selector has been evaluated once into its local. Each split compares against
the median interval's lower bound; leaves check at most four intervals using
the ordinary case rules, then branch to the original default or switch exit.
An explicit stack of 32 frames bounds traversal for a `uint32_t` interval count.
Planning remains O(K log K) for K intervals, with O(K) storage and emitted code;
each dispatch has O(log K) comparisons. The new ordinary branches enter IR and
SSA through their existing construction, without a new opcode or indirect
target metadata. Case bodies, fallthrough, cleanup exits and source labels
retain their order and identity.

The consumed dispatch controls are numbered without placing new reachable
labels. This preserves a statement expression's pending stack values after
the tree's terminating edges. Looking up the plan by its original control
boundary also survives a nested switch inside the selector expression.

`test-checker` checks plan selection and dispatch against an independent linear
interval oracle. The standalone `tests/driver/tier1_sparse_switch.c` fixture,
run in the existing Tier 1 pattern correctness group, compiles both host paths
with `--tier1-strict --verify`, checks identical objects, and executes them
against a Clang-built oracle. It covers signed/unsigned boundaries, exhaustive
16-bit inputs, shuffled cases, ranges and holes, volatile selector side effects,
nested statement expressions, fallthrough, goto and cleanup. A native CFG walk
requires a bounded comparison path, so a correct linear chain fails the test.

## Node templates (#1048)

`./build/compiler measure-lowering-templates [--top N] [--keys N] [--abstracts N]
[--ignore-tracking] [--allow CATEGORY]... [preprocessor options] file...`
(`c_check_lower_inventory.c`) is #1048's inventory of node templates: each
file's selected definitions lowered by the task loop with every entry's owner
(`check_program_lower_owners`, the `-g` path's tokens replaced by owners through
`CheckLowerWriter.owners`, which no compile sets) and again without, which must
give the same entries; each node's key from the columns and its pattern; the
exceptions by category; the coverage of the deterministic keys and of their
abstract patterns; the bodies without an exception; and the maximal subtrees of
template nodes. It counts rather than times, so it needs no lock beyond the
build's. `test-checker` checks it on a fixture (`c_check_lower_inventory_test.c`).
The lowering guide's "Node templates over a body: the inventory (#1048)" has
`sqlite3.c`'s, Lua's and `main.c`'s results and what the design takes from them.

`c_check_program_templates.c` is #1048's template path in scalar form: before a
body's statements, a scatter gives each node's operands their contexts and
targets by its op's rule (`check_lower_template_rules`), then each node on its
own is classified, by the fast twin of the common shapes
(`check_lower_template_fast`) or the general rules, writes its entries into a
slot, and is marked closed or not, whose slots the task loop splices when it
pops their roots in those contexts (`check_lower_template_splice`). The task
loop stays the exact rules. No compile takes it (`CheckLowerShared.templates`):
on the 9700X, a compile that took it spent about three times in these scalar
steps what the splices saved, so they wait for data-parallel ones.
`./build/compiler test-lowering-templates [--passes N] [preprocessor
options] file...` (`c_check_program_templates_test.c`) lowers each file by the
tasks and with the templates forced (`check_program_lower_templates`), on any
host, requires every function's entries, locals, calls and arguments to be the
same and the fast twin to classify every node it takes as the general rules do
(its `check`), and with `--passes N` times both; `test-checker` runs it on
fixtures. `--time-stages`' `lowering-nodes` line reports the nodes and entries
the templates took. A change to `check_lower_expand`'s rules for what the table
takes changes the templates too: run the command on `sqlite3.c`, Lua and
c-testsuite. [The lowering guide](../../docs/c-lowering.md)'s "Node templates
over a body: the first path (#1048)" has the coverage and the measurements.

Division and remainder by a proven constant power of two (#1386,
`check_lower_power`) normalize the constant from its source type before testing
the arithmetic-width value. Unsigned U32/U64 by 2^k, and signed by 1, become a
shift or a mask. Signed I32/I64 by a positive 2^k become the bias sequence
`(x + ((x >> (W - 1)) & (2^k - 1))) >> k`, or `x - (that sum & -2^k)` for the
remainder, whose DUPs keep the dividend (`check_lower_signed_power_entries`). A
pointer difference by a power-of-two element size is an arithmetic shift, exact
since the difference is a multiple of the size, and by a size of 1 nothing. The
batch fragments, the node templates and the node machine's expressions leave a
rewritten root to the tasks; the node machine's updates write a shift or a mask
as the tasks do and leave a signed sequence, past their row's 8 entries, to
them. The regressions (`c_check_power_divmod_test.c`) cover negative signed
char/short/int constants widened to U32/U64, their compound assignments,
positive narrow constants that still select shifts or masks, and signed
quotients and remainders at both ends of I32 and I64 against independent
expectations. They also cover promoted narrow operands, pointer differences of
1- to 32-byte elements, and controls that keep their division: negative
divisors, the sign bit's, a `_BitInt`'s, divisors with effects or that aren't
constants, and element sizes of 3, 12 and a variable-length array's. Source
`_BitInt` values retain ordinary lowering in this optimization.

An integer constant's own conversion folds into its CONST (#2678,
`check_lower_fold_conversion`), so that Tier 0 takes it as an immediate where
it moved it to a register and converted it every time the code ran (`mov $1;
movslq; add`, `mov $1; cvtsi2sd`). The conversion is the one
`check_lower_expression` or `check_lower_operand` pushes under the operand's
EXPRESSION task, whose `value` is the operand's node plus one; the constant's
expansion records its CONST's end and node (`CheckLowerWriter.constant_end`
and `constant_node`), and a label placed since takes the end away, since a
jump there joins another value. `check_lower_constant_conversion` is the rule
the three writers share: an integer class takes the value modulo its width,
`_Bool` whether it is nonzero, and `float` and `double` the value only where
it is exact, so that a conversion that rounds keeps its run-time rounding and
inexact flag; nothing folds to or from a `_BitInt`, a complex value, an
aggregate or a memory class (`long double`, the 128-bit integers, the
half-precision ones, vectors). The node templates fold it in the constant's
slot (`BUSTER_CHECK_LOWER_TEMPLATE_FOLDED`, and leave a folded constant popped
alone to the tasks, which fold it there), and the node machine in its row
(`node_machine_fold_conversion`).

A conditional's or a logical operator's value converted by its consumer meets
its arms at a label, where no conversion folds, since a jump there brings the
other arm's value. Where an arm is a constant (a conditional's arm by
`check_lower_integer_constant`, a logical operator's 0 or 1) and the conversion
is one that folds to an integer or `_Bool` (`check_lower_conditional_sinks`),
the conversion goes into the arms instead (`check_lower_conditional_target`,
asked out of line by `check_lower_join` only where the task under the node's
EXPRESSION is that conversion): its task, under the node's
EXPRESSION, comes off the stack; a conditional pushes it under each arm's own
conversion, tagged with the arm's node, so that a constant arm folds both (`c ?
-1 : 2u` to `unsigned long` is -1 to `unsigned`, then to `unsigned long`); a
logical operator writes its 0 or 1 in the target's class and converts its second
operand's truth before the jump to the join. The join's JUMP_VALUE takes the
target's class, and the other arm takes the CONVERT the join had. A known
condition's single arm does the same; GNU's `x ?: y`, whose condition is its
arm, keeps the join's. A floating target keeps it too: Tier 1 has no select of
floating values, so the arms' floating constants would take a branch where the
join's integer select is a `cmov` (on the 9700X, a `?:` of 3 and 1 summed into
a `double` on a coin's condition took 223.4 ms for 40.5 with the conversion
after the join, in `constant-arms-2678-2026-11-15`), and Tier 0 writes each
arm's floating constant as a `movabs` and a `movq`, no fewer than its constant
and the join's `cvtsi2sd`. The node machine leaves such a node to the tasks
(`node_machine_sinks`), as it writes a record's conversion once, after its row.

`tests/checker/constant-conversions` holds the regressions, which run natively
against clang (`c_check_constant_conversion_test.c`): the issue's four functions,
whose Tier 0 code has no `movslq` or `cvtsi2s` of a constant; every kind of
conversion that folds, conditionals' and logical operators' arms among them,
after which no integer CONST precedes a CONVERT on either lowering, and the node
templates' entries are the tasks'; and the conversions that don't fold, a
join's to `double` among them.

## Node machine census (#2524)

`./build/compiler measure-node-machine [--list N] [preprocessor options] file...`
(`c_check_node_census.c`) is step 0 of [the node machine](../node-machine.md):
what the machine's set and its silence tests would take, counted on the pipeline
as it is, with no machine. Each file is prepared and lowered as `-c` does, with
the tasks' log of pops (the inventory's, above); each full expression (a tree of
pops under a root a statement's control pushed: an expression statement's, a
condition's, a return's, a switch's, a scalar object's initializer's) is read
again from the finished graph, stream and parser's tree by the tests the machine
would make before it takes it. Its tests read every node of the root's subtree,
unevaluated operands too; its counts are the evaluated nodes, the inventory's.

- **The set** refuses a root of another control (a brace initializer's part, a
  statement expression's), an operator without a code (the builtins, `va_arg`,
  statement expressions, variable-length arrays, `_Generic`'s selection,
  `__real__`, `__imag__`), a complex, `_BitInt`, atomic, vector, half-precision
  or `__float128` value, a bit-field, a struct's or union's value copied, a
  compound literal, a name whose declaration has a flag the tables don't model
  (`register`, `overloadable`, `deprecated`, `unavailable`, `error`, `warning`,
  `diagnose_if`, `enable_if`, `noderef`), a callee with semantics of its own (a
  library builtin the catalogue folds, a printf-family name, `format`,
  `format_arg`, `sentinel`, `returns_twice`, `pass_object_size` and the like, no
  prototype, a `musttail` return), more than 4,096 nodes, a body that tracks its
  stack and a node the tasks popped twice.
- **The silence tests** refuse only where a warning they stand for is enabled in
  the unit (`check_common_enabled`, each warning with its producer's default):
  the actions' own warning bits (the baked rows', the folds' and the pointer
  pairs' memo's answers), a kept warning event of the reader's or the graph's at
  a token of the expression, two enumeration types meeting, a sign comparison, a
  pointer cast to a narrower integer, a void pointer dereferenced, a string
  literal plus an integer, an address of a packed member, sizeof's or alignof's
  operand with an effect, constants (a narrowing conversion that changes one, a
  division by zero, a shift out of range, a null dereference, a subscript past
  its array, a comparison outside its operand's range in the comparison's
  common type, a non-literal null), parentheses (an assignment as a condition,
  a comparison in a bitwise operator, `!` before a comparison, a shift of a sum,
  a chained comparison, a shift in a truth context), a discarded value without
  an effect or a discarded call of a `warn_unused_result`, `const` or `pure`
  callee, a self-assignment, a return of a local's address, a for statement's
  clauses with `-Wfor-loop-analysis` on, an object written and named again
  unsequenced, a signed left shift by a constant with `-Wshift-sign-overflow`
  on, an integer constant converted to an enumeration with `-Wassign-enum` on,
  and a body with a diagnostic or floating-point pragma. A system header's macro
  in a user's expression is counted apart, not refused (the design's "Silence"
  says why).
- **Every warning has a class** (`node_census_warning_classes`): a test stands
  for it (by its own reading or by the set's refusal of what it fires on), it
  fires on no expression the census reads, or its scans read the events a
  taken expression records. One no test models (`-Wc++-compat`) refuses every
  expression where it is enabled, and so does one without a class, with its
  default taken to be on; `test-checker` fails until a new warning ID has a
  class. A new diagnostic lands with its refusal.

The report gives the gate's two shares (the set's of the evaluated nodes, and
the silence tests' refusals of all of them), the nodes taken by root kind, the
bodies taken whole, and each test's expressions and nodes: any it refuses, those
it alone refuses and those it refuses first in the list's order. `--list N`
prints the first N expressions each test refuses, by file and line. It counts
rather than times, so it needs no lock beyond the build's. `test-checker` runs
`c_check_node_census_test.c`: a fixture of one warning a line, which the
compiler reports with `-Wall -Wextra` and the census must refuse by the test that
stands for it; clean idioms it must take (a library call's discarded result, two
fields written in one expression, a conditional's arms, `&a[N]`, a same-width
sign change, `size_t` against `(size_t)-1`); the set's refusals; warnings off
by default refusing nothing; and a system header's macros, taken and counted
apart. A new warning reported inside an expression needs its test here and a
line in that fixture: the census is where the machine's silence tests are
written first. [The design](../node-machine.md)'s
"Step 0: the census" has the results on `sqlite3.c`, Lua and musl.

### The shadow (step 1)

`./build/compiler measure-node-machine --shadow [--scalar] [--verify] [--fused] [--interleave N] [--front|--front-first] [--passes N] [--repeat N] [--list N] [preprocessor options] file...`
is step 1: the machine itself, run after the pipeline on every full expression
the census takes, compared with what the pipeline made and timed. No compile
takes its entries.

- **Its files:** `c_check_node_machine.c` is its state, its records'
  accessors, `node_machine_begin` and `node_machine_run`; then one file a
  sweep, T `c_check_node_machine_type.c`, C `c_check_node_machine_contexts.c`
  and X `c_check_node_machine_expand.c`, and C and X as one sweep,
  `c_check_node_machine_lower.c` (below), which `main.c` includes after it in
  the checker unit, before `baked.c`. C, X and the shadow's comparison read a
  record only through the accessors (`node_machine_record_*`, below), so that
  a new record layout changes the machine's file and T alone.
- **The record accessors,** each `(machine, r)`, and all but `_third` and
  `_truth` one load or store of a column; `_integer`, `_offset`, `_floating`,
  `_fault` and `_overloaded` test the record's standing first and read its
  wide value where it has one (`_wide`), and `_truth` makes the value whole:
  - what T wrote: `_op`, `_left`, `_right`, `_third` (operand records,
    `UINT32_MAX` for none; `_third` is a conditional's second arm, from the
    graph), `_type`, `_category`, `_flags`, `_constant_kind`, `_integer` (the
    low 64 bits), `_floating`, `_object`, `_offset`, `_truth(machine, r,
    &truth)` (whether `check_action_truth` knows the value's truth),
    `_fault`, `_overloaded`, `_facts`, `_class`, `_converted(machine, r, k)`,
    `_conversion_flags(machine, r, k)`, `_member_offset`, `_lowest` (its
    subtree's least record, a call's arguments included), and a call's
    `_argument_count`, `_argument(machine, r, i)` (a record),
    `_argument_converted(machine, r, i)` and
    `_argument_conversion_flags(machine, r, i)`;
  - C's own: `_evaluated`, `_state` (with `BUSTER_NODE_MACHINE_AFTER_CONDITION`
    and `_AFTER_ARM`, a consumer's segment after the record's row), `_context`,
    `_target`, `_template`, `_hook` (that consumer), `_label_base` and
    `_call_depth` (the evaluated calls among its ancestors), written by
    `node_machine_records_set_unevaluated(machine, count)` and
    `node_machine_record_set_evaluated`, `_set_context`, `_set_target`,
    `_set_template`, `_set_hook(machine, r, consumer, hook)`,
    `_set_label_base` and `_set_call_depth`;
  - a call's, beside them: `node_machine_callee_function` and
    `node_machine_direct_callee` of its callee record, and
    `node_machine_call_target` and `node_machine_call_variadic`, its row's;
  - the shadow's: `node_machine_record_value(machine, r, &value)`, the whole
    value, which its comparison with the stream reads with `_converted`,
    `_conversion_flags` and `_member_offset`.

  They cost nothing on the 9700X: T's code is the same, C runs the same
  instructions a node and X ten fewer ([the design](../node-machine.md#step-1-the-shadow)).
  C's common cases name their operands (`NodeMachineUse`) and its loop
  evaluates them inline (`node_machine_evaluate_at`): an out-of-line
  `node_machine_evaluate`, a call, between a case's reads would make every
  accessor after it load its column's pointer again. Calls, `&&`, `||`, `?:`
  and updates evaluate theirs by `node_machine_evaluate`, which takes the
  consumer. C classifies by one case an operator and X writes a record's row
  by one case a template ([the design](../node-machine.md#cs-and-xs-dispatch-one-case-an-operator-one-a-template)).
- **T's records** are the design's columns, not a whole `CheckValue` a record:
  a value's type, flags and object, a standing byte (its category, its
  constant kind and which field its 64-bit payload holds: its integer, its
  offset or its floating value's significand, beside a 16-bit exponent) and
  the payload. A value they can't hold whole (a fault, an overload set, a
  bit-field's width, an integer past 64 bits, two payload fields) goes whole to
  a side array, `wides`, which its payload indexes. T writes them with
  `node_machine_record` and `node_machine_record_from` (an operand's value with
  its type, category, flags and constant kind replaced) and makes a value whole
  only for the action, `--verify` and the rare conversions. T's and C's
  columns are the arrays of one struct, `NodeMachineColumns`, so that every
  field is one load from one base, each array 128 bytes past the one before
  modulo 4 KB: [the design](../node-machine.md#ts-records-as-columns) has why.

- **Its records** are the expression's graph nodes: one range of them and the
  root, which the reader may make later (a condition's wrapper follows the
  statement it controls). The machine reads of them what the reader answered at
  their tokens (a leaf's value, a cast's and a sizeof's type name, a member's
  name), never the graph run's output, the stream or the lowering's state.
- **T** types each record: a leaf by the reader's leaf, an arithmetic operator
  of builtin types by its row of the baked transition table
  (`node_machine_rows`), the lookups by direct rules that are their actions'
  own steps for the common case, and the rest by the action itself on the
  machine's records. The direct rules also take the integer constant folds of
  builtin types of at most 64 bits (`node_machine_fold`, by the operator's row
  and `check_action_fold_integer`'s steps where the fold faults nothing), the
  casts between integers, pointers and void (`node_machine_cast`), a static
  object's member and its array's subscripts (address constants), a pointer's
  equality with a null pointer constant (`node_machine_null_compare`),
  `sizeof` and `_Alignof` (`node_machine_query`), the comma
  (`node_machine_comma`) and stores of a null pointer constant or of a pointer
  `node_machine_pointer_assignable` takes. A plain char constant converts by
  the unit's signedness (`node_machine_integer_sign`), not the bake's.
  **C** and **X** are the node templates' rules
  (`c_check_program_templates.c`) read from the records, and
  `check_lower_logical`'s for `&&`, `||` and `?:`, whose test, branch and jump
  X writes as a segment after an operand's row. Their labels are numbered from
  the expression's first, in the order the tasks number them, with each
  label's place in the tape. For the increments and the compound assignments
  they are the lowering's own (`check_lower_update`): an update's first row
  (the old value read, by name or through its destination's kept address, and
  converted) follows its destination's entries, and its second (the operation,
  the conversion back and the store) follows its value's. T types an update of
  an arithmetic destination by its computation's row and of a pointer by
  `check_action_pointer_arithmetic`'s steps, the action taking the rest. The
  machine's set is the census's within those rules: `a ?: b` and an update of
  a `long double`, a 128-bit integer or a vector are refused by template (the
  census refuses a bit-field's and an atomic's).
- **A call** is typed by its callee's prototype (`node_machine_call_rule`):
  each argument converted as an assignment converts it, a variadic one by the
  default promotions, the action taking the rest. A callee with semantics of
  its own (a library builtin's call the catalogue folds or the lowering
  computes, `check_program_call_library`, a unit with overload
  sets, no prototype, another convention than System V's, a callee that
  returns twice) is refused as `callee`; a call the lowering doesn't make
  plainly (`check_lower_call_body`'s plain call: a struct's, a union's, a long
  double's, a 128-bit or a half-precision argument, parameter or result) as
  `template`. X writes the arguments, each converted, and a `CALL` whose value
  is the call's row numbered in the tasks' preorder, as the lowering allocates
  its rows, relative to the expression's first call.
- **A call's per-unit answers** (`NodeMachine`, filled as T meets them and
  forgotten by `node_machine_forget`):
  - `pointers` and `const_pointers`, a type's pointer type by type index,
    unqualified and const (`node_machine_pointer`), `type_pointer` their twin:
    a callee's and a function argument's conversion, an array's decay and an
    address are one load once the type's first has filled it.
  - `argument_memo`, 1,024 entries of 16 bytes, direct-mapped
    (`node_machine_argument_memo`): `node_machine_argument`'s answer (taken or
    refused, and the conversion) by the argument record's type, its standing
    (an lvalue, an integer constant, or neither) and the parameter's type (0
    for a variadic argument). Only an argument the rule reads nothing else of
    is a key: no constant but an address or an integer, none of a null
    pointer's, a bit-field's, a register array's or a volatile part's flags,
    and no enumeration's lvalue. A hit answers where the conversion changes
    nothing `check_action_inherit` reads (an integer, floating, pointer or
    enumeration type's, from those or an array or a function), and inherits
    the record's value; an integer constant's only where every integer of its
    type converts alike (`node_machine_memo_integers`: not to a pointer, where
    a null pointer constant is taken and another isn't). The rule fills a miss
    and takes every other argument, and `--verify` checks the call against
    the action either way.
- **C and X as one sweep** (`node_machine_lower`,
  `c_check_node_machine_lower.c`; [the design](../node-machine.md#c-and-x-as-one-sweep)):
  down from the root as C goes, each evaluated record classified by C's own
  functions and its row written at once, since its context and target are
  all its row waits for. The records come in reverse postorder, so the tape
  is written from its columns' ends and starts at `NodeMachine.entry_first`:
  within a record, the segment after its row first, then its conversion's
  entries, then its own, each row from its last entry (`node_machine_back_*`,
  X's rows mirrored). What X numbers by the order it writes in is numbered
  after the loop, for the expressions that have any
  (`node_machine_lower_number`): the labels by C's ranks, the calls' rows by
  X's rule over the calls in X's order, and the temporaries in X's order.
  Where a row refuses, C's own sweep says whether a template refuses first,
  so that the refusal is the two sweeps'. C and X are its exact twin:
  `--fused` runs it after them on every expression T typed and compares its
  refusal, its tape, its labels' entries and its calls' records with theirs
  (`node-shadow: fused expressions=N differences=0`), and times T with it as
  a set of its own (`T+CX`). `node_machine_run` still runs C and X.
- **The comparison**, for every expression the machine takes: each record's
  value (every field), its operands' conversions, a call's arguments'
  conversions and a member's offset against the stream's, and its tape against
  the lowering's entries that the expression's tasks own (the inventory's
  log), a local's numbered by a map of the body's that must stay one to one
  and a call's row by one base, the stream's row for the tape's 0, each row's
  contents (its callee's symbol, its arguments' count, classes and sizes,
  whether it is variadic, no result object) the machine's. A branch's target,
  an entry after `check_lower_resolve_labels`, is compared by label: the log
  keeps each pop's next label and each body's labels' entries, so each of the
  expression's labels must be the lowering's of the same number counted from
  the expression's first, at the same entry. A temporary (a postfix update's
  through an address keeps the address and the old value in two) is numbered
  on the tape in the order the tape first names it, and must be a local the
  body hasn't met, the same one every time the expression names it.
- `--verify` types every node a row or a rule types by the action too and
  compares the two, field by field (the twin); `--scalar` types every node by
  the action alone. `--repeat N` (1 by default) runs each expression's sweeps
  N times in a row in the timing passes, so that a run after the first finds
  its inputs in the caches, as the reader leaves them in step 2; the
  difference of two runs' figures is that warm cost. `--passes N` (5 by
  default; 0 for none) times each file's
  taken expressions N times a sweep set, T alone, T and C, and all three, one
  set's N passes after another's, and keeps each set's least cycles and
  instructions (this thread's user-mode counters) and TSC ticks, per evaluated
  node: on the 9700X only, under its lock, as `taskset -c 2`. A set's passes
  are in a row so that its figure doesn't move with the sets timed beside it:
  taken in turn inside a pass, a Lua file's T read 79 cycles a node after its
  own pass and 89 after the walk's sets. Each pass starts with the unit's call
  answers forgotten, so that it pays their fills as one compile of the unit
  does.
- `--interleave N` times the sweeps where other code runs between two of the
  machine's runs, as the reader's does in a compile ([the
  design](../node-machine.md#where-the-machine-runs-other-code-between-its-runs)).
  Three more sets take the file's expressions by chunks of N (a body's
  expressions for 0): the census's own walk of each of a chunk's expressions
  (`node_census_expression`, other code over the same nodes) alone (`walk`),
  then that walk and the three sweeps of each (`walk;T+C+X`), and with
  `--fused` the walk, T and the one sweep (`walk;T+CX`). A set's figure less
  `walk`'s is what the sweeps add there, the walk's own slowdown included;
  the line `node-shadow: interleaved` gives the sweeps' own TSC ticks alone,
  to compare with the same set's ticks in the tight loop.
- `--front` and `--front-first` run **the machine's own front** (step 1b,
  [the design](../node-machine.md#step-1b-the-machines-own-front)) in the
  files' one reading. The sweeps above read their inputs from the graph the
  reader's coded walk writes; the front (`c_check_node_machine_front.c`, which
  `c_check_expression_input.c` includes; `PPOptions.node_front`) makes the
  machine's records itself, from each chunk's acting tokens and classes and
  the chunk's identifier and literal entries, without the walk's value chains
  or the graph.
  - **A record** is a node's operator, token, operands (as earlier records,
    and each as a byte, its distance back in the postorder, 255 for that or
    more), a leaf's value, a member's name, a call's arguments and library
    mark, the requested type and the context.
  - **Values wait on a stack** in groups that share a consumer, a value's
    consumer being its token's parent. A token that only passes values on (a
    list, a separator, a group's closing, a braced initializer's `=`) hands
    its group to its parent in one step (`node_front_pass`). A token the front
    doesn't model is **left** to the walk: its value is unknown, so is every
    record that would take it, and the context of what it took. A stack with
    no room (4,096 values: a static table's elements wait for its end) makes
    every waiting value unknown, once (`node_front_forget`).
  - **Between chunks** only the waiting values' records are kept
    (`node_front_keep`): a condition's value waits for its statement's body,
    whose tokens the tree puts before the control's closing.
  - **The comparison** (`node_front_compare`): each record against the walk's
    node at the same token (a discard's by a search from the record before),
    field by field, operator, operands' nodes, leaf value, member, arguments,
    type, and each distance against the front's own records between the two
    nodes; contexts at the walk's end (`node_front_finish`), where the walk's
    lists have given theirs. `--front` runs the front after the walk acted at
    a chunk, `--front-first` before it, where the machine would run: the same
    records, and the caches' first touches the front's.
  - **The report:** `node-shadow: front after-the-walk|before-the-walk
    tokens=N left=N records=N compared=N differences=0 resets=N
    waiting-at-the-end=0` (no value may wait when a unit ends), the tokens
    left by class and the `sizeof` and `_Alignof` ones among them, `front
    taken-expressions=N (x%) evaluated-nodes=N (y%)` (the machine-taken
    expressions whose every record the front made), the operands by distance
    (`within-64`, `in-a-byte`, `further`, a call's arguments and the roots'
    apart: a condition's or a discard's operand is a body away where the
    tree puts the control's closing after it) with the taken expressions that
    have an operand past 64 records back, and `front ticks=N`. `--list N`
    lists the first differences and the taken expressions the front missed.
    A difference fails the command.
  - **Where it runs:** beside the coded walk over a whole unit, which is the
    reader's vector path. A unit the scalar walk reads (a build without the
    vector paths, `BUSTER_HOST_ARCH=x86-64`; the parser's scalar option) or
    that is read by parts has no front, and the report says so: `node-shadow:
    front not run in N of M units`. `test-checker` then expects it to have
    made nothing.
  - Every shadow run prints `node-shadow: walk ticks classes=N prepare=N
    actions=N visits=N`, the coded walk's parts in the files' one reading
    (TSC ticks; the batch's flushes are the actions'): with no front option,
    the walk as it is.

The report gives the expressions, evaluated nodes and calls taken, each
refusal's (shape, code, leaf, context, diagnostic, template, row, root,
callee), the operators the code and template refusals stopped at, the
differences (values, entries, twin), the labels compared, T's nodes by row or
rule and by the action, by operator, the argument memo's entries filled and the
arguments the rule took instead (over every expression T typed, beside the taken
calls' arguments), the fused sweep's census (`node-shadow: fusion`: what a T that
classified each record's operands at its own visit would settle later; [the
design](../node-machine.md#a-fused-sweep-what-it-would-settle-later)), and the
times.
`--list N` prints the first N differences by file and line. The command exits 1
on a difference. `test-checker` runs its fixture (`node_shadow_test_text`, one
construct a line, each taken or refused by a named reason), with `--verify`
and with `--scalar`, each time with `--fused` and `--interleave 3`: the one
sweep must give C's and X's refusal, tape, labels and calls for every
expression of the fixture, nested calls, labels under and around calls and
several postfix updates through addresses in one expression among them. The
front runs over the fixture too (after the walk in the `--verify` run, before
it in the `--scalar` one): every record the walk's node, no value left
waiting, and each taken line's expressions the front's but those of the lines
marked `// front: left`. Two generated texts take it to its limits
(`node_front_test_limits`): a body whose chunk ends fall at every place within
a library call by its name and through its address, and an initializer with
more values waiting than the stack holds, with a call astride its last entry.
[The design](../node-machine.md)'s "Step 1: the shadow" has the results.

### The fact words (#2850's step 1)

`./build/compiler measure-node-machine --shadow --facts [--facts-packed] [--facts-window N] [--passes N] [--list N] [preprocessor options] file...`
(`c_check_node_machine_facts.c`) is step 1 of [the one-core
path](../one-core-path.md): T's identifier leaves and lookups made from fact
words, with T's records as the oracle. It runs in the shadow's own pass, after
the pipeline and the shadow's sweeps; no compile takes any of it.

- **The words.** A declaration's line (32 bytes: its leaf's record's first
  half), a type's row (64 bytes, by type index: its word, value type,
  promotion, element, pointer types, return type, and as flags what each
  lookup's rule asks of the type) and a member's slot (32 bytes, by a hash of
  the record type and the name). `node_facts_begin` makes the rows;
  `node_facts_declaration` makes a line the first time its declaration is
  named and again where the line would change (a type completed since, a weak
  pragma since); `node_facts_member_fill` fills a slot at its first access.
- **The windows.** As the shadow takes each expression, `node_facts_append`
  adds its records to the window at hand: each record's operator, kind (a
  given record, a literal's leaf, an identifier's leaf, a lookup), operands as
  distances back, a lookup's class, and T's record of it. A window is a
  body's expressions, cut where the next expression would take it past 384
  records (`BUSTER_NODE_FACTS_CUT`). `--facts-packed` lets a window hold
  several bodies' expressions; `--facts-window N` cuts at N records (16 to
  4,096). Either implies `--facts`.
- **The kernel** (`node_facts_window`): the window's identifiers' leaves and
  its lookups listed sixteen records a step (`node_facts_list`); each leaf one
  load of its declaration's line and one store of its record
  (`node_facts_leaves`); then the lookups a class at a time, the lower levels
  first (`node_facts_batch`, and one loop an operator: `node_facts_arrows`,
  `_dots`, `_subscripts`, `_calls`, `_discards`). Each lookup has a short
  path (`node_facts_*_fast`) and behind it its rule (`node_facts_slow`), T's
  own rule's steps on rows and slots; what neither takes is T's record,
  counted as left. The kernel's record is one 64-byte line
  (`NodeFactsRecord`), T's fields together.
- **The comparison.** A checked pass gives the kernel T's records of the given
  nodes and poisoned bytes for the leaves and lookups, then compares every
  field T writes of a record (`node_facts_same`) and the kernel's lists and
  classes with a record-at-a-time twin (`node_facts_lists_same`). The timed
  passes follow: `--passes N` of the windows whole, by the cycle, instruction
  and mispredicted-branch counters; as many of the given records put in place
  alone, which is taken off; and as many with the two parts' TSC ticks apart,
  each window run and run again at once.

The report's `node-facts:` lines give the records by kind, the identifiers by
what they name, the lookups by operator with those left, the given records by
operator, the differences, the words made (rows, lines, lines made again,
member slots), the windows, and the times: the kernel's cycles, instructions
and mispredicted branches a taken leaf or lookup, and a leaf's and a lookup's
ticks. `--list N` prints the first N records that differ, with both records.
The command exits 1 on a difference. On a host without the vector paths the
listing and the leaves run a record at a time and give the same records.

`test-checker` runs `node_facts_test_text` (each kind of leaf and each lookup,
taken and left) at three window settings: every record T's, the lists the
twin's, and the counts of leaves, lookups and lookups left the fixture's. A
rule changed in T needs its step changed in the facts' rule and short path:
the fixture and the corpora's comparison say where. [The
design](../one-core-path.md#step-1-the-fact-words) has the results.
