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

## Conditions as branches (#2883)

A statement's condition (`if`, `while`, `for`, `do`) and a conditional's
condition whose expression, under its `!`s, is an `&&` or `||` are written as
branches to the condition's own targets (`check_lower_branch`), not as a value
that a join makes 0 or 1 and the branch tests again. A statement's `CONDITION`,
popped with its control's branch under it, takes that branch off the stack and
pushes a `CHECK_LOWER_BRANCH` task with its target in its place
(`check_lower_condition_branches`); `check_lower_logical` pushes one in place of
a conditional's test of its condition. Popped, an `&&` where false and an `||`
where true branch to the target on either operand, an `&&` where true and an
`||` where false branch on their first operand to a label of their own past the
second, and a `!` swaps the targets. Each operand that is no `!`, `&&` or `||`
is its value, its truth and the branch, as `check_lower_logical` tests its
operands, so that the order and the evaluation of the operands are C's.
`check_lower_branch_operator` takes an operator of an `int` value that the
expansion would expand (no constant), and an `&&` or `||` whose first operand's
truth the program doesn't know; a condition without an `&&` or `||` (`if (!p)`)
and GNU's `a ?: b` are written as before. The `CONDITION` stays the popped root
of the statement's expression, which owns the branches; its `&&`, `||` and `!`
are never popped, and its leaves are popped under it.

An `&&` or `||` used as a value keeps its join: its second operand's truth jumps
to it, and its first operand's branch goes to the constant there. Where that
first operand is itself branches (under its `!`s, an `&&` or `||`: `a && b &&
c`, `(a && b) || c`), `check_lower_logical` pushes the `CHECK_LOWER_BRANCH` task
for it, as for a conditional's condition, to the value's own constant label:
where false for an `&&`, where true for an `||` (#2891). No value of the first
operand is made, joined and tested again; the value's outermost join is the one
left. The node machine writes the same branches (#2890): its C sweep gives such
an operator a target and a sense where a value has a context, and X writes each
operand's test and branch after its row ([the design](../node-machine.md), "The
logical operators and the conditional").

`tests/checker/branch-conditions` holds the regressions, which run natively
against clang (`c_check_branch_condition_test.c`): conditions written as
branches, whose functions have no `JUMP_VALUE` but their conditionals' and no
jump to a test of a joined value, and values (returned, assigned, an expression
statement's, chains under `!`s) that keep their outermost operator's join alone;
and the operands' order and effects, traced by calls, nested three deep and
more, in every statement's condition, a conditional's, a statement expression's
and values whose first operand is an `&&` or `||`, of every scalar type (NaN
among the doubles), a first operand the program knows, and chains of 17
operands. The node templates' entries are the tasks' there.

A third program is for a condition the program must not know (#3028): an
lvalue at an absolute address (`if (*(int *)4096)`), whose truth is its
value's, which the program loads. Its address says nothing of it
(`check_action_truth`). Twenty forms each keep the lvalue's load in their
entries: an `if`'s condition, the first operand of `&&` and of `||`, a
conditional's condition, of each scalar class, through a member and a
subscript, and the forms that read before the fix too. Two keep none: an
array at an absolute address decays to it, and `&` of the lvalue is it.

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
  common type, a null pointer constant that is no literal, by the compiler's own
  test of an assignment's, a conversion's and an initializer's source and of a
  call's arguments), parentheses (an assignment as a condition,
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
by default refusing nothing; what warns without an option, read with the
default warnings alone (`node_census_test_defaults`); and a system header's
macros, taken and counted apart. A new warning reported inside an expression needs its test here and a
line in that fixture: the census is where the machine's silence tests are
written first. [The design](../node-machine.md)'s
"Step 0: the census" has the results on `sqlite3.c`, Lua and musl.

### The shadow (step 1)

`./build/compiler measure-node-machine --shadow [--scalar] [--verify] [--fused] [--interleave N] [--front|--front-first] [--front-lean] [--front-warm] [--front-types] [--front-flat|--front-flat-first] [--front-sweeps] [--front-tests] [--front-tests-last] [--front-facts] [--front-window] [--window-tests] [--window-contexts] [--window-rows] [--passes N] [--repeat N] [--list N] [preprocessor options] file...`
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
  body hasn't met, the same one every time the expression names it. A named
  result's address (#1375: a returned local built where the hidden pointer
  points) is the hidden pointer's `LOCAL` in the stream, which
  `check_lower_named_rewrite` made of the local's `LOCAL_ADDRESS` after the
  tasks wrote it; the log keeps each body's named local and hidden pointer's
  local (`CheckLowerOwners.named`), and the tape's address of the body's one
  object there is that entry (#2882).
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
  - **Its structural bytes** are what #2850's layout reads of a record
    beside its facts: the operator, the operand count (a call's callee and
    arguments), the operands' distances back, and, written into an operand's
    record when its consumer's is made, the consumer's distance forward and
    the operand's place among the consumer's operands (a callee 0, the
    arguments from 1). A root, a value a left token took and one nothing
    takes have no consumer's distance.
  - **`--front-lean`** is the front that writes only those, the token and a
    leaf's payload: an identifier's declaration, a literal's type and
    integer, a string's type and object. An identifier's facts are then a
    load by its declaration (#2850's fact words), and a member's name, a
    requested type and a call's library mark are loads by the token. It
    decides an identifier from its token's own columns and reads none of the
    chunk's identifier entries; it still reads the literals'. It runs after
    the walk, or before it with `--front-first`.
  - **`--front-warm`** touches the front's arrays before its first chunk.
    They are fresh memory in every unit, and on a host without transparent
    huge pages their first touch is a cost a unit (half of the front's ticks
    on musl in a cloud VM, under 2% on the 9700X): with it the ticks are the
    front's own work.
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
    lists have given theirs. The structural bytes are checked from both
    sides: the consumer's count and distances against the walk's node's
    operands, and each operand's own distance forward and place against its
    consumer; a consumer's distance no compared consumer vouches for is a
    difference, and so is one on a value still waiting. The lean front's
    operands are found by its bytes alone (the record that many back; a
    call's arguments by their places), and its leaves by their payloads.
    `--front` runs the front after the walk acted at a chunk, `--front-first`
    before it, where the machine would run: the same records, and the caches'
    first touches the front's. The front's calls, timers and counts in the
    coded walk are two functions out of line (`node_front_prepared`,
    `node_front_acted`), called only where the front runs: inline and never
    run they cost `-c sqlite3.c` 0.76 M instructions.
  - **The report:** `node-shadow: front after-the-walk|before-the-walk
    tokens=N left=N records=N compared=N differences=0 resets=N
    waiting-at-the-end=0` (no value may wait when a unit ends; `lean` and
    `warm` after the order where asked), the acting tokens and those left by
    class (`front class=NAME tokens=N left=N`) and the `sizeof` and
    `_Alignof` ones among those left, `front taken-expressions=N (x%)
    evaluated-nodes=N (y%)` (the machine-taken
    expressions whose every record the front made), the operands by distance
    (`within-64`, `in-a-byte`, `further`, a call's arguments and the roots'
    apart: a condition's or a discard's operand is a body away where the
    tree puts the control's closing after it) with the taken expressions that
    have an operand past 64 records back, what the layout's byte columns cap
    (`front structure records-with-more-than-63-operands=N
    operands-at-a-place-past-3=N`), `front ticks=N`, and the walk's
    preparation by its parts where the front ran (`walk prepare ticks
    compact=N identifiers=N literals=N`: the lean front reads the first and
    the last). `--list N` lists the first differences and the taken
    expressions the front missed. A difference fails the command.
  - **Where it runs:** beside the coded walk over a whole unit. A unit that is
    read by parts has no front, and the report says so: `node-shadow:
    front not run in N of M units`. `test-checker` then expects it to have
    made nothing.
  - Every shadow run prints `node-shadow: walk ticks classes=N prepare=N
    actions=N visits=N`, the coded walk's parts in the files' one reading
    (TSC ticks; the batch's flushes are the actions'): with no front option,
    the walk as it is.
- `--front-types` runs **T where it sits**, at reader time
  (`c_check_node_machine_reader.c`, `PPOptions.node_front`'s bit 16): after
  the walk acted at a chunk and the front's records were compared, T types
  each full expression the front made whole, on the walk's nodes, before the
  unit's program exists. It runs with the front after the walk, or as
  `--front-first` and `--front-lean` ask. [The
  design](../node-machine.md#t-where-it-sits) has what it measured.
  - **What it reads in place of the program's:** each object's last static
    declaration, returns_twice and asm label, made from the reader's columns
    as the program makes its own; a static declaration for a symbol; a library
    call's "computed inline?" from T's own record of its size; the contexts
    the front gave its records; and the words of the types made since the
    machine began (the walk's, and T's own pointer types, each settled where
    it is made).
  - **An expression** is a root's operand's subtree and the root; an
    initialization's is its value alone, whose root the lowering pops.
  - **The comparison** is after the pipeline. Each expression of the census's
    taken set that reader time ran on is typed again by the shadow's own T,
    and the two must agree in their range, their refusal and their records
    (a digest of every column C, X and the shadow read). The objects' columns
    and every library call's answer are compared with the program's own.
  - **The report:** `node-shadow: reader-time T expressions=N records=N
    typed=N taken=N roots-not-run=N late-types=N ticks=N setup-ticks=N` (its
    ticks are T's alone, a chunk's expressions a run; the digests are a second
    run), `reader-time T refused=REASON expressions=N`, `reader-time T
    lowered=N (x%) never-lowered=N (taken N, context N; of them in a lowered
    body: taken N, context N) declarations'-discards-not-run=N compared=N
    (records N) differences=0 (range 0, refusal 0)
    taken-expressions-the-front-left=N the-front's-not-run=N` and
    `reader-time T objects=N differences=0 library-calls=N differences=0`.
    `never-lowered` are the expressions it typed that the lowering never
    popped: in a body the lowering lowers (a constant's context, a condition
    the program folds) or in one the selection dropped.
    `declarations'-discards-not-run` are the front's roots T is not run on: a
    declaration's discard, its initializer's value cast to void at its `;`,
    which nothing evaluates (by the parser's tree: the cast's operand is an
    initializer's `=` or below one). `the-front's-not-run` are taken
    expressions whose nodes the front made without a root for them (an
    initializer beside a variable-length array's bound). `--list N` lists the
    first differences. A difference fails the command.
- `--front-flat` runs **the flat front** beside the lean one, its twin
  (`c_check_node_machine_flat.c`, `PPOptions.node_front`'s bit 32): the lean
  front's records made by passes over a chunk, with no stack and no walk a
  token at a time. `--front-flat-first` (bit 64) runs it before the walk acts
  at the chunk, where the machine would run; the lean front stays where the
  other options put it. [The design](../node-machine.md#the-front-by-passes)
  has what it measured.
  - **Operands by the tree's shape.** A token that takes values hops back
    over its children by their lengths (`CheckInputReader.subtree_sizes`) and
    reads one word a child from a ring by position: the place of the record
    that is the child's value, an unknown value's mark, or nothing. The hop
    over a child is in a second ring, the steps, which the front's own
    compaction writes: the child's length, or 1 for a token that only passes
    values on (a list, a separator, a group's closing), which is never
    visited.
  - **The passes** (`node_front_chunk_flat`): the compaction
    (`node_flat_compact`, 16 positions a step); the classes
    (`node_flat_classify`, 16 tokens a step: each token's class, its byte,
    its record's first form and its class's list); the leaves
    (`node_flat_leaves`, a class at a time); the places (`node_flat_places`:
    a running sum of the records each token makes, each token's word, and a
    terminator's discards); the records, a class's tokens together
    (`node_flat_unaries`, `node_flat_binaries`, `node_flat_ternaries`,
    `node_flat_calls`); and the empty records (`node_flat_empties`).
  - **An empty record.** A token the front leaves keeps its place, with
    `BUSTER_NODE_FLAT_EMPTY` for its operator, where the lean front makes no
    record: a chunk's places are then known before any operand is read. Its
    consumers are made empty in turn, a statement's discards together.
  - **The comparison** (`node_flat_compare`), after both fronts made a chunk:
    the compaction and the classes against their scalar twins, then every
    record both fronts made, by token, field for field, an empty record being
    none and the distances less the empty records between. The lean front's
    records are compared with the walk's nodes in the same run.
  - **The report:** `node-shadow: flat front after-the-walk tokens=N (the
    lean front's less the N that pass values on) left=N records=N
    empty-places=N hops=N (x a token) past-the-rings'-reach=N`, `flat front
    against the lean front: compared=N differences=0 only-the-flat-front's=N
    only-the-lean-front's=N past-the-records'-ring=N
    distances-past-a-byte-by-empty-places=N`, `flat front ticks=N
    compact-ticks=N` and `flat front stage ticks classify=N leaves=N places=N
    records=N empties=N`. `only-the-lean-front's` are values past the rings'
    reach (`BUSTER_NODE_FLAT_WINDOW` positions: a condition before a very long
    body); `only-the-flat-front's` are records the lean front's full stack
    lost. `--list N` lists the first differences by field. A difference fails
    the command.
- `--front-sweeps` runs **C and X where they sit** too
  (`PPOptions.node_front`'s bit 128, with `--front-types`' bit): after T types
  an expression at reader time and takes it, C and X run on the same records.
  [The design](../node-machine.md#c-and-x-where-they-sit) has what it
  measured.
  - **What they read beside T's records** is the machine's own:
    `NodeMachine.object_symbols`, `declaration_bits` and `object_count`. After
    the pipeline those are the program's symbols and its input's bits. At
    reader time no symbol is numbered: an object with a static declaration,
    and a string's object, has itself in its symbol's place, so X writes the
    object where it writes a symbol.
  - **The root's consumer** is derived from the front's root: no context, and
    a conversion's target for an initialization alone, its object's type
    without its qualifiers where the conversion writes entries, with the
    object its declaration names.
  - **The comparison** is after the pipeline, on each expression of the
    census's taken set that T took both times. Reader time's target and
    object must be the lowering's pop's, its refusal the shadow's own C's and
    X's, and its tape theirs entry for entry once a global's entries
    (`GLOBAL`, `GLOBAL_ADDRESS`, `STORE_GLOBAL`, `SET_GLOBAL`) are patched
    with the program's symbols. Its labels' entries, its calls' records and
    its temporaries are compared by a digest.
  - **The report:** `node-shadow: reader-time C and X expressions=N taken=N
    entries=N tapes-not-kept=0`, `reader-time C and X refused=REASON
    expressions=N` and `reader-time C and X compared=N (entries N)
    differences=0 (root 0, refusal 0, tape 0, labels-and-calls 0) patched:
    globals'-entries=N direct-calls=N`. With this option `reader-time T`'s
    `ticks` are of T, C and X together, a chunk's expressions a run.
    `patched` counts what a splice would patch in the compared expressions:
    the globals' entries, and the direct calls, whose rows hold the callee's
    symbol. `--list N` lists the first differences, and a difference fails the
    command.
- `--front-tests` runs **the tests where they sit** (`PPOptions.node_reader`'s
  bit 1, with `--front-sweeps`' bits): after T takes an expression at reader
  time and before C and X, the census's set and its silence tests run on T's
  records. [The design](../node-machine.md#the-tests-where-they-sit) has what
  it measured.
  - **The tests are the census's own code** (`node_census_expression` and what
    it calls), reading a node's value, its conversions and a member's offset
    through a view (`node_view_value` and its kin): the stream's after the
    pipeline, T's record at reader time (`NodeCensusScratch.records`).
  - **What stands for the rest** at reader time
    (`node_census_reader_expression`): the root's kind is the front's root's
    operator, a nested control's inside a statement expression; an
    initializer's object's type is its declaration's; the enabled warnings
    are read before the walk; a body's pragmas are found by the outermost
    braces around each; the events are the reader's own so far; T keeps its
    actions' warnings an expression (`NodeMachine.action_warnings`); and any
    address taken in a unit with a packed record or member stands for a
    packed member's witness.
  - **C and X run on the expressions no test refuses** in the timed run. The
    second run sweeps the others too, so that the report can say what the
    tests add to C's and X's own refusals.
  - **The comparison** is after the pipeline, on every root the tests ran on:
    one the lowering popped has the census's own verdict, and one it never
    popped is read by the census again from the stream, with the kind and the
    object's type reader time gave. No expression reader time takes may be
    one a test of the census's refuses, and no test's bit may differ but
    reader time's actions' warnings (at least the graph run's), a packed
    unit's addresses, and the events of the passes after the reader. The
    lowering's own facts (a body that tracks its stack, a node popped twice)
    are counted apart: a splice reads them where it pops the root. The
    comparison reads bodies that are never lowered too, where the census's
    tests can't lean on the selection: a string's object has no symbol
    there, so the test of a returned address takes a static object by its
    value's flag (the compiler's own source has two such returns).
  - **The report:** `node-shadow: reader-time tests expressions=N refused=N
    nodes-outside-the-records=0 units-whose-enabled-warnings-differ=0`,
    `reader-time tests refused test=TEST expressions=N first=N`, `reader-time
    tests compared=N never-lowered=N taken-there=N (nodes N)
    taken-there-and-refused-by-the-census=0 (for the lowering's facts alone
    N) differences=0 root-kind-differences=0 refused-by-the-census=N (of them
    past C and X N, nodes N)` and `reader-time tests test=TEST reader-alone=N
    census-alone=N`. With this option `reader-time T`'s `ticks` are of T, the
    tests, C and X together. `past C and X` are the refused expressions C and
    X take: what the tests add to the sweeps' own refusals. `--list N` lists
    the first differences, and a difference fails the command.
  - **With `--verify`** T's rows and rules are checked against the action at
    reader time too, on every expression it types there (`reader-time verify
    nodes-whose-value-is-not-the-action's=0 nodes-whose-action-warns=0`): a
    row or a rule knows no warning, so the action must give none where one
    typed the node. A value that differs fails the command.
  - **`--front-tests-last`** (bit 16) runs the tests after C and X in the
    timed run, on the expressions those take.
- `--front-facts` runs **the fact words' kernel where the machine sits**
  (`PPOptions.node_reader`'s bit 32, with `--front-types`' and
  `--front-lean`'s bits): #2850's kernel on each expression T takes at reader
  time, its windows' inputs made from the lean front's records.
  [Its section](#the-kernel-where-the-machine-sits-2850s-step-2) has it.
- `--front-window` has **the lean front write the kernel's window** as it
  makes each record, and the kernel read it where it is written
  (`PPOptions.node_reader`'s bit 64, with `--front-facts`' bits).
  [Its section](#the-front-writes-the-window-2850) has it.
- `--window-tests` runs the tests in **the machine's own form, on that
  window** (`PPOptions.node_reader`'s bit 128, with `--front-window`'s and
  `--front-tests`' bits): their loop 64 records a step over the records the
  kernel has just made. [Its section](#the-tests-on-the-window-2850) has it.
- `--window-contexts` makes **C's answers on that window** too
  (`PPOptions.node_reader`'s bit 4, with `--window-tests`' bits): each
  record's template, context and target, 64 records a step, beside C on T's
  columns at reader time. [Its section](#c-on-the-window-2850) has it, and
  [a call's](#a-calls-arguments-in-the-kernel-and-cs-call-2850).
- `--window-rows` makes **X's rows on that window** too
  (`PPOptions.node_reader`'s bit 512, with `--window-contexts`' bits): each
  evaluated record's entries' kinds and classes, 64 records a step, beside X
  on T's columns at reader time. [Its section](#xs-rows-on-the-window-2850)
  has it.

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
several postfix updates through addresses in one expression among them. Its
conditions that are branches (#2890) are there by shape: an `&&` and an `||`
where true and where false, under an if, a while and a do statement, under a
`!` and two, three deep, as a conditional's condition and as a value's first
operand, with a call, an update and a conditional as operands; and what stays
a value. Reader time sweeps what the census leaves too, so its refusals there
are counted: four operands a truth's entries don't test (a cast to
`__float128`) must be C's refusals, not X's. The
front runs over the fixture too (after the walk in the `--verify` run, before
it in the `--scalar` one, lean in the `-funsigned-char` one): every record the
walk's node, no value left waiting, and each taken line's expressions the
front's but those of the lines marked `// front: left`. Two generated texts
take it to its limits, whole and lean (`node_front_test_limits`): a body whose
chunk ends fall at every place within a library call by its name and through
its address, and an initializer with more values waiting than the stack holds,
with a call astride its last entry. T runs at reader time in those runs too
(`BUSTER_NODE_FRONT_TYPES`): the shadow's records on every expression both
type (by the rows there against the action alone in the `--scalar` run), the
objects' columns and the library calls' answers the program's, T run on every
expression the front made, and the fixture's six callees refused there (four
of them calls the census refuses before the shadow's T, which no comparison
reads). The chunk-boundary text ends with the address of an array no
declaration points to, whose pointer type T makes itself. The flat front runs
beside the lean one on the fixture, after the walk and before it, and on the
two generated texts: every record the lean front's, the places of the tokens
the fronts leave empty, and, where the lean front's stack is full, the records
the stack lost made by the flat front alone. A third text takes it past its
rings' reach (`node_flat_test_reach`): a condition whose body is 13,500
tokens, whose record the flat front alone lacks, then a call, a sum and a
conditional of a `sizeof`, whose consumers keep their places empty; and two
declarations whose `;` the flat front leaves because its first value is past
the reach, a braced initializer of four values (one's consumer over 255
records after it, one's four records after it) and 9,000 adjacent strings.
Their near values have a consumer in the lean front alone, which the
comparison takes for no difference where that consumer is a record only the
lean front made. And the fronts run over the twelfth tests' fixture, a unit
with clang's `no_builtin` on two functions (`node_front_test_builtins`; whole
after the walk and before it, lean with the flat front beside it, the flat
one first): each makes the unit's calls (their 38 arguments), every record is
the walk's node, the whole front's call with the walk's library mark, which
is asked by the function that holds the call, and the flat front's records
are the lean one's.
C and X run at
reader time too (`BUSTER_NODE_FRONT_SWEEPS`) in the `--scalar` and the
`-funsigned-char` runs, in a fourth run of the fixture without warnings (where
the census takes a `for` statement's clauses, whose condition's wrapper has a
type that is no target of its root's) and in the lean chunk-boundary run:
every tape kept, each compared expression's target, object, refusal, tape,
labels and calls the shadow's, and the fixture's globals' entries and direct
calls patched. Its lines for them are a `const` object's initializer, one that
converts nothing, one whose conversion is empty, a static local, a global's
updates, and the strings.
The census's twelve fixtures run again with its tests at reader time
(`node_tests_test_fixture`: `--front-tests` with `--verify`, then the same
with the lean front's window and `--window-tests`, the machine's own form
beside the census's code, whose bits, root's kinds and spans must be its;
after the walk, before it, and lean): no expression taken there is one the
census refuses, no
test differs, every root the tests ran on is compared, T's rows and rules are
the action's without a warning, each fixture's count of expressions a test
refuses there is exact, and so is its count of declarations' discards left
alone. A silence test's line has an expression that test refuses there. The
lines added for them: a conditional whose third operand's conversion changes
a constant, a call's argument whose conversion does, two members of one
record written in one expression, and a body with a diagnostic pragma. For
the own form: the seventh fixture, whose lines the own form's conditions
decide where every warning would send them to the census's code, and a `for`
statement in the first (its clauses under `-Wfor-loop-analysis`). For its
loop on the window: the seventh's last function (a name's leaf without a
line, a register object's member, an ordering under a bitwise operator, an
operator listed for an address constant the record doesn't hold whole, and
four expressions of some 140 records, whose operands are further back than
the block before), and each fixture's counts of what the loop lists and
sends (`NodeTestsWindowed`), which no verdict shows. For the loop's rule of
the constants with no test to make: the seventh's functions `q` to `q3` (a
line for each class of operator the rule leaves out, and for what it must
keep listed: a constant past a bound, zero against an unsigned operand, a
shift by 32, a division by zero, a bool, a bit-field, an enumeration, a
128-bit operand, a floating one, a constant first, a return whose constant
doesn't fit and one whose own type is no integer's), the eighth fixture (no
warning of a mix of enumerations and `-Wassign-enum`, where an enumeration's
constant is listed for itself alone), and each fixture's counts of the
operators left out, each checked, and of the constants whose record says
its value. For the tree's tests listed by the operator above and the
expressions with nothing listed: the seventh's function `t` (a comparison
under each bitwise operator, first and second, and in parentheses; `!`
before a comparison, in parentheses, after one; `=` as each statement's
condition and a `for`'s, in parentheses, as a `switch`'s, in a `for`'s
other clauses; a comparison discarded whose operand writes, and under a
cast to void; `!` and `=` further back than the block before the operator
above them, and in the block before it; an initializer whose top is listed,
in each list; a warning event in an expression with nothing listed, in its
tokens' first word, their last and one between, and at its first token; a
pointer compared with an integer, which T's action warns of; an address
taken outside a return), the ninth fixture (the warnings that list an
expression, off: what they would refuse is taken, and two writes or a
returned address list nothing; its first function's return has nothing
listed and ends at the unit's first warning event, the events' lower bound
at its equality), the tenth (`-Wc++-compat`, a warning no test stands for: every
expression refused, one with nothing listed too), the seventh's function
`calls` and the eleventh fixture (the calls with no test to make: a call
left out with no constant, with zero and 1 to 126, as a statement, under an
assignment, as a condition, in a return and as a `for`'s last clause,
through a pointer's name, a dereference (a pointer's, and a function's
designator's, once and twice) and a member, with six arguments,
under a cast to another type and as a comma's first operand; a call kept
for a constant past 126 or under zero, for one a cast or a conditional
makes, for a floating, a narrow or an enumeration's parameter, the first of
two too, for a variable argument, an aggregate, a printf-family name, a
pointer whose declaration has a format, a null callee, a callee before the
block before; a statement whose top is a cast to void, a cast to another
type, a call whose callee asks for its result, at its declaration's first
such statement and at a later one, and a `for`'s last clause whose discard
is in its block, in the one before and further back; seventy statements
that are a call left out and seventy whose callee asks, a cast to void at
each place of a block; and, with `-Wtype-safety`, every call kept), the
twelfth (two functions with clang's `no_builtin`, one naming `memset` and one
naming nothing, between three without it: the unit's calls are made, and a
plain library builtin is its declaration's call in the function whose
attribute covers it and has its own semantics in the others), the seventh's
function `v` (the loop's rules of a pointer: its cast to void, as a
statement's discard and written, to a pointer and to a pointer to void,
left out; to a 64-bit integer, listed with nothing; to a 32-bit one, a bool
and an enumeration, which fire; an enumeration's cast, which nothing sends;
its subscript by zero, a small, a large and a negative constant, with the
constant first and of an address constant, left out; an array's, a null
constant pointer's on each side and one by a bool's constant, listed; its
sum with a constant on each side, each compound assignment and one whose
pointer is the constant, left out; a string's, two pointers' difference and
one whose constant is further back than the block before, listed) with a
pointer's cast to an integer in the ninth under `-Wno-pointer-to-int-cast`
(nothing sends it), and
each fixture's counts of the
operators listed for the tree's tests, of the records those tests read that
no listed operator has as an operand, each checked, of the calls left out,
each checked, of the casts in the asks' list and the declarations asked
whether they want their result, of the casts left out, each checked, of
the expressions with nothing listed, each run through the tests all the
same, and of the roots' records apart from their operand's.
The fact words' kernel runs at reader time on the fixture twice
(`node_shadow_test_kernel`: after T alone, and in the `--scalar` run with the
census's tests and C and X beside it) and on the two generated texts: no
record differs from T's over poison or over decoys, no list from its twin's,
no input made from the front's records from the one made from T's columns,
as each expression came and made again; the second making makes as many
records' as the first; and the counts are exact: the expressions the front's
records don't give (3 of the fixture's, cut by a chunk's end; 5 of the
chunk-boundary text's, four cut and one out of reach), the records the
kernel leaves to T (24 and 3), the literals' leaves that are a line's (125,
and one in each generated text), and in the second run the whole machine's
records and those of them made (1,223 and 1,088). What the corpora showed
the kernel needs there has a line each:

- an enumerator whose value is past 64 bits beside one that isn't: the
  reader's own column has both before the input's list is made;
- a sum whose first operand, a call, is 260 records before it, past a byte's
  distance: its inputs are the other making's;
- `*&narrow`, twice, in the fixture's last body: T derives the pointer type
  there, and its row has to be made before the expression is appended (Lua's
  `lua_numbertointeger` showed it);
- a third generated text, one body of 16,500 statements `x += 1;`
  (`node_facts_test_reader_lines`): more literals than a batch's lines hold,
  so each has a line only if a batch's lines are taken away with it.

Where the front writes the window (`BUSTER_NODE_READER_WINDOW`) the same
texts run again: the fixture after T alone and with the census's tests and C
and X beside it, the two generated texts, and the 16,500 literals. No input
the front wrote differs from either other making's; the kernel in place makes
the records it makes of the batch, each of them T's; the names' pass's scalar
path gives every leaf the same kind and line; and the same front without the
window makes the same records of every chunk. The rows kept at the chunks'
ends give the expressions the copy alone lost: none of the fixture's is cut,
and of the chunk-boundary text's five the window gives all and the copy four
(the sum whose operand is past a byte's distance stays the other making's for
the copy; the window's distances are whole). Four more generated texts are
the window's own, each with its counts of values kept whole, of their
records and of values cut pinned:

- `node_facts_test_window_room`: an initializer of 3,000 sums, whose values
  wait for the list's end and leave the arrays room for the first few rows
  alone, then 7,280 statements of seven kinds in turn, each with a sum of
  five terms (nine records) as one operand of what follows it: an if
  statement's condition, an operator's first and second operand, a
  conditional's three, a call's argument. The seven's tokens are 115, an odd
  count, so the chunks' ends, every 1,024 tokens, fall at each of the 115
  places in turn. The room left is a few records, by how many of the
  statement's values wait, and never nine: a whole sum that waits at an end
  keeps its place alone and is cut, with every record made above it. 64
  expressions are neither the window's nor the copy's, and nothing resets
  the stack;
- `node_facts_test_window_wait`: an if statement's condition, a call of two
  calls with a sum of a literal among their arguments, that waits through
  six chunks' ends. Its ten records are moved once and then kept where they
  are, with its literal's line and its calls' arguments, and the calls made
  meanwhile take the arguments' places after theirs. What it is moved over
  and what is made after it show a column, a line or an argument that did
  not move: twelve long's literals where its leaves go, fifteen arguments
  before its own in the list, and in the body sixty literals' lines in a
  row, over its `1`'s first number. Three more lines before it are for the
  writers: a call whose first argument asks a later round than its last, an
  imaginary literal (no line's leaf), and `__extension__` over a sum (an
  operator the kernel makes no record of);
- `node_facts_test_window_calls`: an initializer of 2,000 calls of three
  arguments. A cut call's arguments are not kept, or the list would pass
  its room at the 1,366th waiting call; the most it held is read;
- `node_facts_test_window_again`: a sum of 599 records waits as a call's
  first argument while a compound literal's 2,300 elements wait above it.
  It is kept whole at four ends and cut at the fifth; the literal's end then
  frees the room, and at the next end the sum stays cut, its records being
  gone.

[The design](../node-machine.md)'s "Step 1: the shadow" has the results.

### The fact words (#2850's step 1)

`./build/compiler measure-node-machine --shadow --facts | --facts-lean | --facts-loads [--facts-operators] [--facts-reads] [--facts-packed] [--facts-window N] [--passes N] [--list N] [preprocessor options] file...`
(`c_check_node_machine_facts.c`) is step 1 of [the one-core
path](../one-core-path.md): T's identifier leaves and lookups made from fact
words, with T's records as the oracle. With `--facts-operators` (step 2's
first piece) it makes T's other records too, and `--facts-reads` (its second
piece, [below](#what-reads-ts-record-2850s-step-2)) counts what reads them.
It runs in the shadow's own pass,
after the pipeline and the shadow's sweeps; no compile takes any of it.

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
  distances back, a made operator's class, and T's record of it. A window is a
  body's expressions, cut where the next expression would take it past 384
  records (`BUSTER_NODE_FACTS_CUT`). `--facts-packed` lets a window hold
  several bodies' expressions; `--facts-window N` cuts at N records (16 to
  4,096). Either implies `--facts`.
- **The classes.** A made operator's class is its round and its code
  (`node_facts_class`). The codes are the operators' loops, in an order that
  puts an operator's operands mostly before it (`BUSTER_NODE_FACTS_CODE_*`).
  An operator's round is the highest of its operands' rounds, and one more
  than an operand's whose code is after its own (`node_facts_round`): a round
  makes its classes in the codes' order, each class's operators in the
  window's order, so an operator finds its operands made, those of its own
  code before it in its class. Four rounds have classes
  (`BUSTER_NODE_FACTS_ROUNDS`); what would be of a later one is made after
  them by its rule.
- **The kernel** (`node_facts_window`): the window's leaves and its made
  operators listed sixteen records a step (`node_facts_list`); the operators
  appended each to its class's list (`node_facts_distribute`); each leaf one
  load of its line and one store of its record (`node_facts_leaves`); then
  the classes, a round at a time, one loop an operator (`node_facts_arrows`,
  `_dots`, `_dereferences`, `_subscripts`, `_calls`, `_discards`). Each
  lookup has a short path (`node_facts_*_fast`) and behind it its rule
  (`node_facts_slow`), T's own rule's steps on rows and slots; what neither
  takes is T's record, counted as left. A loop lists the operators its short
  path didn't take and makes them after it (`node_facts_slow_each`), so that
  no call is in the loop; it marks each one's record as a wide value's, so
  that a later operator of the class that reads it is listed too. A short
  path's choices are between registers: what a choice may need is loaded
  whichever way it goes, since clang compiles a choice with a load behind it
  as a branch. The kernel's record is one 64-byte line (`NodeFactsRecord`),
  T's fields together.
- **The operators' kernel** (`--facts-operators`, with `--facts` or
  `--facts-lean`). A literal's leaf is a line of its own, the reader's record
  of it (`node_facts_literal`), copied as a declaration's. The operators that
  are no lookup are made by T's rows' and rules' steps on the kernel's
  records, a loop a code: the rows' operators (`node_facts_rows_made`, T's
  baked table through `node_facts_row_made`, and behind it
  `node_facts_rows_rest`: the constants' fold, a pointer plus or minus an
  integer, a pointer's equality with a null pointer constant); `=` and a
  conversion or an initialization (`node_facts_stores`, `_converts`); a
  control's condition and `!` (`_truths`); `&x` (`_addresses_made`); a cast
  (`_casts`); `&&` and `||` (`_logics`); the increments and the compound
  assignments (`_updates`); and a comma, sizeof, `_Alignof`, `?:` and a unary
  plus, each by its rule (`node_facts_slow_list`). A store, a truth and a
  logic operator have short paths (`node_facts_store_fast`, `_truth_fast`,
  `_logic_fast`); the others run their rule. Beyond a type's row the rules
  read a side row a type (`NodeFactsSide`: the pointer types to it, from the
  machine's memo as T fills it, and its size and alignment) and a slot a pair
  of pointer types (`NodeFactsPair`: whether one converts to the other, and
  whether they point at the same type), a memo as the members' slots.
- **The variants.** `--facts` is the whole kernel: a leaf's and a lookup's
  record with every field T writes. `--facts-lean` makes what later rules
  decide on: a leaf is its declaration's line alone, a lookup writes its
  record's first half (type and word, flags and object, payload, standing) and
  a member's offset, no conversion's entry is written, and a discard is not
  made. `--facts-loads` makes of a lookup only what its load gives (its type,
  that type's word, a member's offset) and tests only what the load tests;
  its standing, flags, object and payload are given, and so is whether its
  operand is fit for the short path, which is the lean kernel's own answer.
  The loads' kernel is a measurement of the load, not a kernel a compile
  could run alone.
- **The comparison.** A checked pass gives the kernel T's records of the given
  nodes and poisoned bytes for what it is to make, then compares what the
  variant makes of each record with T's (`node_facts_same`: every field for
  the whole kernel) and the kernel's lists and classes with a
  record-at-a-time twin (`node_facts_lists_same`), which also checks the
  order itself: an operand the kernel makes by class is on a list no later
  than its operator's. What a lean kernel leaves
  out is made in the checked pass by the record's rule from the kernel's own
  records (`node_facts_derived`): each taken lookup's two conversions' entries
  must be T's, or the command fails, and each discard's whole record is
  counted as its rule's or not. The loads' kernel is checked after the lean
  one, on records whose given fields are T's and whose loaded fields are
  poisoned. A second checked pass gives the kernel decoys for what it is to
  make: T's record of each node with one inherited flag the other way. Poison
  reads as no value a path takes, so an operator made before its operand would
  only be left there, to its rule or to T; a decoy reads as the operand's own
  value, as T's record does in a timed pass, and what is made from it
  differs (but for `=`, a call and an update, which set that flag
  themselves: the twin's order check is what sees those). The timed
  passes follow: `--passes N` of the windows whole, by the cycle, instruction
  and mispredicted-branch counters; as many of the given records put in place
  alone, which is taken off; and as many with the two parts' TSC ticks apart,
  each window run and run again at once.

The report's `node-facts:` lines give the variant, the records by kind, the
identifiers by what they name, the lookups by operator with those left, those
a short path took and those their rule took, a lean kernel's conversions and
discards with those derived, the loads' kernel's lookups by their load, the
given records by operator, the differences, the words made (rows, lines, lines
made again, member slots), the windows, and the times: the kernel's cycles, instructions
and mispredicted branches a taken leaf or lookup, the two passes apart, and a
leaf's and a lookup's ticks. With `--facts-operators` they add the operators' records, those made,
those a class's loop made and those their rules made; each operator's records
with those left; the literals' lines; the share of the records that are no
literal's leaf that the kernel made; and the kernel's cycles, instructions and
mispredicted branches a unit, a record it made, a record of the windows and
an evaluated node (the sweeps' own count, as `node-shadow: time sweeps=T` has
main's T). `--list N` prints the first N records that differ, with both records.
The command exits 1 on a difference. On a host without the vector paths the
listing and the leaves run a record at a time and give the same records.

**The kernel's figures are a difference of two passes**
([#2872](https://github.com/buster14a/buster/issues/2872)): a pass over a
unit's windows (each window's given records put in place, then the kernel)
less a second pass that only puts the given records in place, each the least
of `--passes N`, summed over the units. The second pass alone is not what the
copying costs inside the first: on a unit of a hundred records it carries
some hundred cycles a unit's pass that a build fixes, and two builds of one
kernel read 13% to 30% apart on musl by the difference. The line
`node-facts: the two passes, a record of the windows` has each pass's own
cycles, instructions and TSC ticks. Compare two builds by the first pass's
figure there: a change of the kernel moves it, and where the units are short
nothing else measured does. `sqlite3.c` is one unit and its difference
carries no such constant.

`test-checker` runs `node_facts_test_text` (each kind of leaf and each lookup,
taken and left) by the whole kernel at three window settings and by the lean
and the loads' kernels at two each: every record T's in what the variant
makes, the lists the twin's, the counts of leaves, lookups and lookups left
the fixture's, and every conversion and discard a lean kernel left out its
rule's. It runs `node_facts_test_operators_text` (each operator the
operators' kernel makes, by each case of its rule, the shapes each rule
leaves to T, a store, a call and an update of an operand of a later code,
and chains whose first link is T's) by the operators' kernel with whole
records at three window settings and with lean ones at two: every record
T's, the lists the twin's, each operator's records, those made and those a
class's loop made the fixture's, and every conversion a lean kernel left out
its rule's. A
rule changed in T needs its step changed in the facts' rule and short path:
the fixtures and the corpora's comparison say where. [The
design](../one-core-path.md#step-1-the-fact-words) has the results, and
[step 2's first piece](../one-core-path.md#step-2s-first-piece-ts-other-operators-on-the-fact-words-records)
the operators' kernel's.

### What reads T's record (#2850's step 2)

`./build/compiler measure-node-machine --shadow --facts-reads [--facts-window N] [--passes 0] [--list N] [preprocessor options] file...`
(`c_check_node_machine_reads.c`) is step 2's second piece of [the one-core
path](../one-core-path.md): for every record of the fact words' windows, which
of its wide fields a later reader reads, and where each field's value comes
from. It implies `--facts --facts-operators` with whole records and runs
after the kernel's passes (`--passes 0` skips the timed ones). It is a count:
it times nothing and no compile takes it.

- **A read is found by changing the field and running its reader again**,
  not by reading the reader's code. The readers are a later rule of T (the
  record's consumer's rule, the kernel's own `node_facts_slow`, on the
  window's records: `node_reads_consumers`; a call's is the machine's
  `node_machine_call_rule`, which converts each argument: `node_reads_call`)
  and C and X (the machine's `node_machine_contexts` and
  `node_machine_expand` on the expression's columns: `node_reads_sweeps`). X
  runs on C's unchanged columns, so what it reads itself is apart from what
  reaches it through C.
- **The changes** (`node_reads_changes`), one at a time: the record's type to
  another type of the unit with the same word and the same row's facts (its
  identity alone differs) and to a type of another word; each of its flags'
  25 bits flipped; its object to another of its kind (a local, a global) and
  to one of the other kind; its payload to the values its type holds of six
  (its own with the low bit flipped, 0, 3, 8, 70,000 and 2^40 + 5); a
  member's offset to two. For C and X also each operand's conversion's type,
  the same two ways, and its flags' 12 bits. A type's other self is, of the
  types with its word, one whose element and whose size or stride differ
  from its own where there is one (`node_reads_begin`): `int*` becomes
  `long*` before `volatile int*`, which no reader of a pointee's size tells
  from it.
- **What changed says how the field was read.** A byte-level answer (a
  consumer's standing, flags, type's word or conversions' flags; C's
  context, template or target's word; the tape's kinds, classes or length; a
  refusal): the field *decides*. A wide value alone (a type's identity, an
  object, a payload, the tape's value): it is *carried*. A flag a consumer
  only hands on, the same bit of its own flags, is no read. A carried value
  is a read only where it arrives: what a rule carries is kept as an edge
  from the operand's field to the consumer's fields that changed, and
  counted where one of those is itself read (`node_reads_classify`, from the
  last record to the first); where C alone carries a value, X runs on C's
  changed columns and the tape says.
- **Where a read field comes from** (`node_reads_sources`): none (no object,
  no payload); a leaf's line; a builtin type, which a code names; an
  operand's same field; a type an operand's type's row gives (its value
  type, promotion, element, pointer types, return type) or that type's size;
  the node's own type name; or new at the record. A payload that equals an
  operand's is that operand's only where its rule was seen to copy it, over
  the changes of the operand's payload that leave the rule's byte-level
  answer as it was: one that is computed (`1 << 0`) is new. So is one a
  conversion copies only where it fits its type, and one whose rule is T's
  own and didn't run again; the report counts both apart.
- **A record's class** is what has to be made at it: nothing wide is read; a
  code names what is read; it is a copy a selector could name; or something
  read is new at the record, a step no lane makes.
- **The census checks itself.** Before a consumer's or an expression's
  changes and after them all, the reader runs with nothing changed and must
  make what it made (`same differences`, which must be 0), and each record's
  place in the windows must be its place in the machine's expressions
  (`misplaced`).
- **What it leaves.** The operands of a consumer the kernel left to T, and of
  a call T types by its action; an expression past 256 records
  (`BUSTER_NODE_READS_MOST`), which C and X aren't run again on. Each is
  counted, and its records aren't among the measured ones.

The report's `node-reads:` lines give the expressions, the records and those
measured; the runs; by kind (a leaf; a lookup: a member, a dereference, a
subscript, a call, a discard; another operator) the records by class, the
records C or X decide on an identity of, those whose type has no other of
its word in the unit (its identity can't be changed), the records by which
wide field is read and by which of those is new, and by reader; then for
each operator its records by class, each reader's reads by field, the flags'
bits read, the payloads' values that decide for C or X, and each read
field's source. `--list N` prints the first N records C or X decide on an
identity of: the file and line, the record's operator and its consumer's,
each reader's bits, and the indexes of its type and of the type it was
changed to. The command exits 1 if a run with nothing changed differs or a
record is misplaced.

What the counts can and can't say:

- A type's identity is changed to one other type. Two types that give a
  reader the same answer hide a read: two records with the same member at
  the same offset, or two function types whose parameters are all plain
  scalars, which is how C's question of every callee shows for few of them.
- A change that takes a record out of its rule's reach counts as a decision,
  and a wide value that a type of another word changes counts as that
  type's identity's read: the counts of wide reads are upper bounds there.
- A byte-level answer of a consumer's record counts whether a later reader
  reads that byte or not.
- The readers past the expression (a statement's own checks) and the
  silence tests are not run.

`test-checker` runs `node_reads_test_text` by the census at two window
settings: a read of each kind the census has to find, beside records nobody
reads a wide field of, and the kinds of record it leaves. The counts by
kind and class, by field and by reader, the reads the fixture is there for,
their sources, and the records left are pinned. [The
design](../one-core-path.md#step-2s-second-piece-what-reads-ts-record) has
the results.

The census gives a reader records no program does: an operand whose type,
flags, object or payload was changed alone. So under the sanitizers its
tests also test the rules and the sweeps on those. The merge queue's
MemorySanitizer row found one such read: the fact words' pointer rule
(`node_facts_pointer`) read its second operand's type where its first
operand wasn't taken and the second had never been read into its place (the
rule's answer didn't depend on it). That rule's operands and the cast's
source are zeroed for it. A pull request's checks don't run the sanitizer
rows: before queueing a change to the census or to a reader, build with
`BUSTER_HOST_ARCH=x86-64 BUSTER_SANITIZER=memory` (then `address` and
`thread`) and run the part of `test-checker` that has the census's tests.

### The kernel where the machine sits (#2850's step 2)

`./build/compiler measure-node-machine --shadow --front-facts [--front-tests] [--verify] [--passes 0] [--list N] [preprocessor options] file...`
is step 2's third piece of [the one-core path](../one-core-path.md): the fact
words' kernel with the operators' ([above](#the-fact-words-2850s-step-1)) run
at reader time, after T types a chunk's roots there, on windows whose inputs
come from the lean front's records. It implies `--front-types` and
`--front-lean`, after the walk unless an order is given. No compile takes any
of it, and on a host without the vector paths, where no front runs, it runs
nothing. Its code is the last part of `c_check_node_machine_facts.c`; the
hook is in `node_machine_reader_chunk`, after the tests and C and X.

- **A batch** is the expressions T takes among a chunk's roots
  (`node_facts_reader_expression` appends each; `node_facts_reader_flush`
  runs the batch after the chunk's last root, or where it would pass
  `BUSTER_NODE_FACTS_READER_RECORDS` records or
  `BUSTER_NODE_FACTS_READER_EXPRESSIONS` expressions). Its windows are cut as
  after the pipeline.
- **The words at reader time.** `node_facts_settle` makes the rows of the
  types made since the last expression, before the next is appended. A
  declaration's line is made at its first use (`node_facts_front_line`, which
  takes a line at hand whose type is still the declaration's, and else
  `node_facts_declaration`). An enumerator's value is read from the reader's
  own column (`NodeFacts.enum_values`; the input's list, which the kernel
  reads after the pipeline, is made when the reader ends), and one past 64
  bits makes its line's leaf T's (`BUSTER_NODE_FACTS_HIGH`). A literal's line
  is a batch's own (`node_facts_front_integer`, `node_facts_front_literal`).
- **The inputs from the front's records** (`node_facts_front_expression`):
  each record's operator, kind, class, operands' distances, its entry (a
  leaf's line, a member's name, a cast's or a return's type, a call's first
  argument's place), its count of arguments and its source position, from
  `NodeFront`'s columns and the reader's by token. It returns why an
  expression isn't given whole (`BUSTER_NODE_FACTS_WHY_*`): a chunk's end cut
  its records, an operand is past a byte's distance, a literal isn't decoded
  again, a call has 255 operands or more.
- **Their twin** is `node_facts_append`'s making from T's columns and the
  walk's nodes. `node_facts_front_same` compares the two a record: every
  field, a literal's line by its content, a call's arguments one by one. An
  expression the front's records don't give takes the twin's inputs and is
  counted.
- **A batch's runs:** the kernel as it finds the words, timed (`the kernel's
  first run`); the front's inputs made again, the batch's expressions in one
  loop, timed and compared again; the kernel over poisoned records from the
  front's inputs, every record compared with T's and the lists with their
  twin's; the kernel over decoys; and a timed turn, the kernel on the
  batch's windows and T again on its expressions, the kernel first in every
  other batch. With `--verify` T isn't run again for its time: it checks
  itself against the action there, which counts what the action warns of.
- **With `--front-tests`** the report adds the share
  of the records the kernel makes in the expressions the whole machine takes
  at reader time (T, the tests, C and X), which is the set nearest to the
  kernel's windows after the pipeline.

The report's `node-shadow: reader-time fact words` lines give the expressions,
their records and windows, the literals, the records to make, those made
(with those a short path or a class's loop made) and left, the differences
(`differences`, `over decoys`, `list-differences`) and the batches that
didn't fit (`failures`); with the tests and the sweeps, the same share `in
the expressions the whole machine takes there`; each operator's records left
(`left op=OP records=N of N`); the words made (`rows`, `declarations'-lines`
with those made again, `member-slots`, `pointer-conversion-slots`) and their
ticks each; the ticks a record of the windows (`the kernel's first run`, `a
later run`, `T again`, `the rows' and the lines' making`, `the windows'
making from T's columns`) and their totals; `inputs from the front's
records` (the expressions and records given, the `differences` from the
twin's, the `expressions-not-given-whole`, the ticks as each expression came
and `again-ticks`, the second making's); and `not given by the front's
records`, by why. T's own first run is `reader-time T`'s `ticks`, as without
the option. `--list N` prints the first N differences (`reader-time fact
words difference`, with the file, the line, the operator and what differs:
a record, a record over decoys, a window's lists, the front's inputs) and,
on a count of their own, the first N expressions not given, with why. The
command exits 1 on a difference or a failure.

`test-checker` runs it on the node machine's fixture and on three generated
texts, as "The shadow" above has. Of the 52 faults put into this part one at a
time, 50 fail those tests. The two that pass change nothing a text can show
at reader time: a line at hand taken without its type compared (no
declaration is read there with two types), and a return's type read from its
token's query type (the walk's action at a return has copied it there). The
record's campaign has the three runs and what each changed. [The
design](../one-core-path.md#step-2s-third-piece-the-kernel-where-the-machine-sits)
has what it measured.

### The front writes the window (#2850)

`./build/compiler measure-node-machine --shadow --front-window [--front-tests] [--front-warm] [--verify] [--passes 0] [--list N] [preprocessor options] file...`
is the first part of step 3 of [the one-core path](../one-core-path.md): the
lean front writes each record's inputs of the fact words' kernel as it makes
the record, and the kernel reads them in the front's arrays. It implies
`--front-facts`, whose batch and copy run beside it as its twins. No compile
takes any of it, and on a host without the vector paths, where no front runs,
it runs nothing. The front's part is in `c_check_node_machine_front.c` (the
columns and the chunk's end); the writers, the names' pass and the kernel's
runs in place are the last part of `c_check_node_machine_facts.c`.

- **The columns** (`NodeFront.window_kinds`, `window_classes`, `window_left`,
  `window_right`, `window_counts`, `window_entries`, `window_positions`, and
  the calls' `window_arguments`) are the kernel's `NodeFactsInputs`, one of
  each a record of the front's. `node_front_chunk_window` is the lean
  front's pass with the writers' calls (`node_front_chunk_by`'s `window`):
  without the option the lean front is the code it was.
- **The writers.** `node_facts_window_leaf`: a literal's line is written at
  its record's number (`record + 1`; a batch's first
  `BUSTER_NODE_FRONT_RECORDS` lines are the front's records'), and a name
  gets its declaration and a prefetch of its line's number; a float and a
  string go through `node_facts_window_rare_leaf`.
  `node_facts_window_operator`: the kind and code by the operator
  (`window_op_kinds`, `window_op_codes`), the class from the operands'
  classes, the distances whole, the entry. `node_facts_window_call`: the
  arguments' distances, appended to the front's list.
- **The names' pass** (`node_facts_window_names`, after each chunk's pass)
  is three sweeps over the chunk's records: the names' places by a compare
  and a compress of sixteen kinds a step; a prefetch of each one's line; the
  read, where a first use makes the line and an enumerator past 64 bits makes
  the leaf T's. Its scalar path runs on a copy of the kinds and entries, and
  the two are compared.
- **A chunk's end** (`node_front_keep`): a waiting value keeps its records as
  a row where they fit in what a record a waiting value and a chunk's
  records leave of the arrays (`spare`). A row already in place is not
  moved. `node_facts_window_kept` moves a kept literal's line to its
  record's new number and a kept call's arguments to the list's start. A
  value without room keeps its place alone and takes the class
  `BUSTER_NODE_FACTS_CLASS_CUT`, which every record made above it takes from
  it; it is cut for good, and its calls' arguments are not kept. The list
  then holds no more arguments than the arrays hold records, which is its
  room.
- **The kernel in place** (`node_facts_window_run`, from
  `node_facts_reader_flush`): the batch's inputs, T's expected records and
  the windows' bounds are exchanged for the front's arrays and one window of
  the records the front holds. The kernel runs over poison with every record
  of the taken expressions compared with T's and the lists with their
  twin's, over decoys, and once more for its time. The records of the
  expressions T didn't take read as given (`NodeFactsReader.place_kinds`).
- **Its twins.** `node_facts_window_expression` compares each record's
  inputs with those made from T's columns and, where the copy gives the
  expression, with the copy's (`node_facts_window_same`: a leaf's line by
  its number or its 32 bytes, a call's arguments one by one, a root's
  operand by its place). An expression with a cut record is counted and not
  read in place.
- **The front without the window** (`node_front_chunk_windowed`): a second
  lean front runs on the same chunks, first or second by the chunk's
  parity. Its ticks are what the window's are read against, and its records
  and waiting values must be the window front's.

The report adds six `node-shadow: reader-time fact words` lines:

- `windows the front wrote`: the expressions and records read in place, the
  inputs' `differences` from either twin's, and the
  `expressions-not-there-in-a-row`;
- `the kernel in place`: the records to make, made and left, the
  `differences` from T's records, and its ticks;
- `the front with the window`: its ticks and records, `the same front
  without it` on the same chunks with the `chunks-that-differ`, and `the
  rise`;
- `the names' pass`: the names, its ticks, of which `the declarations' lines
  it made`, `the rest`, and the `leaves-its-scalar-path-gives-otherwise`;
- `chunks' ends`: the waiting values `kept whole` with their records, those
  `kept as their place alone, cut`, and the most `the calls' arguments'
  list` held at an end, beside its room;
- `ticks a record of the windows`: the kernel in place, the names' pass less
  the lines it made, the front's rise, and their sum, which the piece's gate
  reads.

`--list N` prints the first N differences (`a record's inputs as the front
wrote them`, `a record the kernel made of the front's window in place`) and
the first N expressions `not given by the front's window`, with why. The
command exits 1 on a difference of the inputs, of the kernel's records in
place, of the two fronts' records or of the names' two paths, and where the
calls' arguments' list held more than its room.

`test-checker` runs it on the fixture, on the three generated texts and on
four of its own, as "The shadow" above has. Each of the 122 faults put into
this part one at a time fails those tests. The record's campaign has the
three runs and what the first two changed: a cut call's arguments, which a
chunk's end kept and the list had no room for; three tests no text can
fail, removed; and the four texts above, made so that each operand's cut, a
kept leaf's columns, a kept line and a kept call's arguments show. [The
design](../one-core-path.md#the-front-writes-the-window) has what it
measured.

### The tests on the window (#2850)

`./build/compiler measure-node-machine --shadow --window-tests [--verify] [--passes 0] [--list N] [preprocessor options] file...`
is the next part of step 2 of [the one-core path](../one-core-path.md): the
set's and the silence tests in the machine's own form, made from the window
the front wrote once the kernel has made a chunk's records in place. It
implies `--front-window` and `--front-tests`, whose verdict (the census's
code on T's records) is its reference. No compile takes any of it. The code
is the last part of the tests in `c_check_node_census.c`
(`node_tests_window_chunk` and what it calls), behind a hook after the
kernel's run in place (`node_facts_window_run`).

- **One form.** `node_tests_expression` has no loop of its own over T's
  records and no leaves' pass: it takes what the window's loop made of the
  expression (`NodeTestsLoop`). `--front-own-tests` and its mode bits are
  gone.
- **A type's bits ride in its word.** `node_machine_make_word` puts the six
  bits the tests ask of a value's type at every record in the type's word
  (`BUSTER_NODE_MACHINE_WORD_TESTS_SHIFT`: a type the machine has no kind
  for, a struct or union, an enumeration, a pointer, a pointer whose pointee
  may be void, an array). A record has its type's word whichever path wrote
  it.
- **The records' bytes** (`node_tests_window_bytes`), sixteen records a
  step: four columns of the kernel's 64-byte records (the word, the flags,
  the object, the standing) by sixteen loads and twelve permutes ([the
  columns](#the-records-bytes-columns-2850)), and one gather of a name's
  declaration's line's byte
  (`NodeCensusReader.line_bytes`, which the census's code makes at the
  line's first leaf). Two byte columns come out: the type's byte and the
  value's (a constant a test may read, a bit-field, a declaration the tests
  refuse, and the record's kind for the rule below).
- **The loop** (`node_tests_window_loop`), 64 records a step. Each of the
  nine tables by operator is one byte permute of two registers. An operand's
  byte comes by its distance, from this block's bytes and the block
  before's, one permute a side; an operand further back is read by a load,
  and counted. It writes what each record is as an operand, its operands'
  bytes together, its part of the set's tests, and five masks a block, one
  line of the cache: the operators listed for their operands' tree's tests,
  the operators with tests of their own, the writes, the operators the two
  rules below left out, and the records that as a statement's top leave its
  test nothing.
- **The tree's tests, by the operator above.** A test of the parser's tree
  (`node_tests_tree`) reads its record's token's parent's role, and fires
  only where that parent is the record's consumer's token. So the loop
  lists the consumer: a comparison whose first operand is `!`, a condition
  whose first operand is `=` (`own_tree_firsts` against the operand's
  `own_tree_marks`), and `&`, `|`, `^` or a cast with a comparison for an
  operand (`own_tree_sends` against the operands' properties). The test
  then runs on the listed operator's operands, and decides. With
  `--verify`, each record such a test reads (a comparison's token, `!`,
  `=`) that no listed operator has as an operand has its test run all the
  same and must have none.
- **The constants with no test to make.** An operator listed for a constant
  operand alone is left out of the list where its operands' kinds say its
  tests have nothing to say (`BUSTER_NODE_TESTS_QUIET_*`, the classes;
  `node_tests_quiet_pairs`, the rule, a 64-byte table by the two operands'
  kinds). A record's kind as an operand is three bits of its value's byte,
  which the records' bytes' pass makes (`BUSTER_NODE_TESTS_KIND_*`: none, a
  plain pointer, a plain integer signed or unsigned, an integer constant
  zero, 1 to 31, 32 to 126 or other). A constant's value is what its
  record's exponent says (`node_facts_small` in
  `c_check_node_machine_facts.c`: the payload and one, 0xffff past 0xfffd,
  0 for none), which the standing's gather has; the kernel writes it where
  it makes a constant's record, and T's own record says none. With
  `--verify`, each operator left out has its tests run all the same and
  must have none, and each value said is compared with its payload.
- **The calls with no test to make.** A call is left out of the list where
  its own type is no aggregate's, its callee gives a call no semantics of
  its own, its operands' properties send it nothing, and its arguments are
  quiet. The last is the kernel's: its call rule's short path
  (`node_facts_call_fast` in `c_check_node_machine_facts.c`) gathers, where
  it reads each argument's record, the kinds of constant among them, whether
  an integer constant is not zero or 1 to 126, and their types' words
  (`node_facts_argument`), and the call's record's exponent says 1
  (`BUSTER_NODE_FACTS_QUIET_CALL`; a call the rule itself makes says 0) where
  they have no aggregate and no constant but an address's or such an
  integer's, which the callee's type's row says each parameter takes
  unchanged (`BUSTER_NODE_FACTS_TAKES`: no enumeration's type, no floating
  one, no integer's but a plain one of 8 to 64 bits). The calls' second pass
  takes the 1 away where an argument may be a null pointer constant that is
  no literal: an integer constant without the literal's flag whose own
  parameter's conversion's type is a pointer's (the arguments' rule's
  answer's `BUSTER_NODE_FACTS_ARGUMENT_QUIET`). The callee's part is a
  bit of its declaration's line's byte (`BUSTER_NODE_TESTS_LINE_LOUD`, the
  census's code's fact of the declaration, made at the line's first leaf
  where the leaf's type's row says it calls), which a name's leaf has in its
  value's byte and the call reads of its first operand (a leaf without a
  line is loud whatever it names). A unit with `-Wtype-safety` keeps every
  call. With `--verify`, each call left out has its test run all the same
  and must have none, and has no other constant among its arguments by T's
  records and its own list (`node_tests_call_loud`).
- **The casts, a pointer's subscripts and its sums.** A cast is sent to its
  test by a pointer operand alone (the table's entry sends no enumeration:
  the enumerations' test reads two operands' types), and is left out where
  its own type's byte says void or a pointer: the pointer cast's test fires
  for an integer under 64 bits or an enumeration. A subscript's operator is
  of the class of `+`, whose pairs of kinds take a plain pointer with a
  plain integer (`node_tests_quiet_pairs`): the integer is promoted, which
  narrows none. A subscript whose pointer is a constant a test may read
  stays listed for its null dereference's test, by one mask of each side's
  value's byte. With `--verify`, each cast left out has its test run all
  the same and must have none, counted apart from the constants'.
- **A statement's top, and the asks.** A statement is listed for its own
  test where its top is no write, has a type that is not void, and is no
  call left out whose callee asks for no result: a bit a record of a block's
  mask. Whether a callee asks for its result is its declaration's other
  fact (`BUSTER_NODE_TESTS_LINE_RESULT`), made only where a statement may
  discard a call of it. A statement's discard is a cast to void over its
  top: the loop compresses the numbers of the casts to void whose first
  operand is a call left out, in its block or the one before, into a list
  (`NodeCensusReader.window_asks`), and `node_tests_window_asks` reads each
  one's callee's line's byte, asks the census's code at the line's first ask
  (`node_tests_window_wants`), and clears from the mask a call whose callee
  asks. A call whose discard is further back is cleared from it where the
  loop reads that operand, and its statement stays listed.
- **An expression's part** (`node_tests_window_chunk`'s first sweep). The
  set is an OR of its records' bytes. Its lists are the masks' words aligned
  at its first record (`node_tests_window_word`), its root's bits last. Its
  tokens' span is a masked minimum and maximum.
- **An expression with nothing listed** has its verdict there, from the
  window alone, and neither T nor the tests below run on it. It is listed
  for them where it has an operator in a list, two writes or one under its
  top (`-Wunsequenced`), an initialization's root, a statement whose top
  discards something (above), a return of an address
  (`-Wreturn-stack-address`), or a warning
  of T's actions (`BUSTER_NODE_FACTS_PLACED_WARNED`, a bit of the kernel's
  reader's byte an expression). Its verdict is its set's bits, the
  control's bit of a root inside a statement expression, the bit of a
  warning no test stands for, a `for`'s clause under
  `-Wfor-loop-analysis`, a pragma's body and a warning event at its tokens
  (`node_census_reader_marks`, asked where its span meets the bounds of the
  unit's events so far). With `--verify`, each such expression is run
  through the tests all the same and must have the same bits, root's kind
  and span.
- **The tests that read T's records still** (the tree's, the operators', the
  expression's own, and the census's code for a record sent to it) run in a
  second sweep, on the expressions listed for them
  (`NodeCensusReader.called`), each one's after T has made its records
  again. A third sweep runs T alone on the same expressions, and its ticks
  are taken off the second's.
- **The comparison:** each expression's bits, its root's kind and its
  tokens' span, against what the census's code gave at reader time.

The report adds:

- `node-shadow: window tests expressions=N records=N differences=0
  operands-before-the-block-before=N lines'-bytes-made=N records-swept=N
  roots-apart-from-their-operand=N chunks=N`;
- `node-shadow: window tests ticks=N (N a record)`, then by sweep: `the
  records' bytes`, `the loop`, `an expression's part`, `the tests on T's
  records`, and `T again for them, not counted`;
- `node-shadow: own tests`: the expressions with a record sent to the
  census's code, the records sent and the walks; what the loop listed (the
  tree's records, the operators, and of those the calls and the ones with a
  constant or a third operand); and the records sent, by what sent them;
- `node-shadow: own tests operators-left-out-for-quiet-constants=N
  checked=N of-them-with-a-test=0 constants'-said-values-checked=N
  not-their-payloads'=0`: the operators the rule left out, of every record
  the loop swept; with `--verify`, those of them in an expression the tests
  ran on, each checked (a swept record of an expression without tests has
  none to check), and the integer constants whose record says its value, including ordinary integers folded from floating
  constants;
- `node-shadow: own tests calls-left-out=N checked=N of-them-with-a-test=0
  of-them-with-a-loud-argument=0 casts-over-one=N
  callees-asked-for-their-result=N
  initializers-asked-for-a-null-constant=N`: the calls the rule left out, of
  every record the loop swept; with `--verify`, those of them in an
  expression the tests ran on, each checked twice; the casts to void in the
  asks' list; the declarations asked whether they want their result; and
  the initializers' roots asked of the census's code for a null pointer
  constant that is no literal (an integer constant without the literal's
  flag whose object's type is a pointer's);
- `node-shadow: own tests casts-left-out=N checked=N of-them-with-a-test=0`:
  the casts the loop left out for their own type, of every record it swept;
  with `--verify`, those of them in an expression the tests ran on, each
  checked;
- `node-shadow: own tests tree's-tests'-records-no-listed-operator-has
  checked=N of-them-with-a-test=0
  lists'-words-with-a-bit-past-their-expression=0`: with `--verify`, the
  records the tree's tests read that no listed operator has as an operand,
  each checked;
- `node-shadow: own tests expressions-with-nothing-listed=N (P%) checked=N
  of-them-with-another-verdict=0`: the expressions whose verdict is the
  window's alone; with `--verify`, each run through the tests that read T's
  records and compared.

`--list N` lists the first differences (`node-tests: window difference`),
and a difference, an operator left out with a test, a call left out with a
test or with a loud argument, a cast left out with a test, a value said that
is not its payload's, a
record left to no listed operator with a tree's test, a list's bit past its
expression, or an expression with nothing listed and another verdict fails
the command.

`test-checker` runs it on the census's twelve fixtures, as "The shadow" above
has, and on the window's test of values without room at a chunk's end: the
tests run on the 8,259 expressions the kernel makes in place there, and on
none of the 64 that the ends cut. [The
design](../node-machine.md#the-tests-on-the-window) has what it measured.

### C on the window (#2850)

`./build/compiler measure-node-machine --shadow --window-contexts [--passes 0] [--list N] [preprocessor options] file...`
is the next part of step 2 of [the one-core path](../one-core-path.md): C's
answers made from the window once the kernel has made a chunk's records in
place and the tests' sweep has made their bytes. C gives every evaluated
record its template, its context and its target, and refuses an expression
with an evaluated record that has no template. The option implies
`--window-tests`. C on T's columns at reader time (`node_machine_contexts`, a
record at a time from each root down) is its reference. No compile takes any
of it. The code is `c_check_node_machine_window.c`
(`node_contexts_window_chunk` and what it calls), behind a hook after the
tests' chunk (`node_facts_window_run`).

- **What it decides.** An expression the kernel placed and the reference's C
  ran on is decided on the window unless one of its records is listed: a
  call whose record doesn't say every argument's conversion is made, or
  whose callee is no name's leaf and has an object ([a call's
  arguments](#a-calls-arguments-in-the-kernel-and-cs-call-2850)); `&&`, `||`
  or `?:`; an update (`++`, `--`, a compound assignment); a
  comma or a selection; a division or a remainder by an integer constant
  (the tasks' rule for a power of two reads the divisor's value); a record
  whose value the columns don't hold whole (a bit-field's, a faulted
  constant's and an overload set's are); a cast to a union; a leaf with an
  effect, of void's type, or read for its address without being an address
  constant; and any operator the loop has no case for. C on T's records
  decides a listed one, as before. Listing is always safe; a refusal must be
  C's own, so each of C's terms is in the loop unless what makes the
  records implies it (the file's comments name what does).
- **A consumer's distance.** The front writes each operand's record its
  consumer's distance among the window's records (`NodeFront.window_ups`,
  beside `ups` in `node_front_add`: one store an operand, with the window
  alone). `ups` is the distance in the unit's postorder, which is another
  where a chunk's end took records from between a control's root and its
  operand; it says whether a record has a consumer yet.
- **The records' bytes.** Where C runs, the tests' sweep
  (`node_tests_window_bytes`) makes five byte columns more from its columns:
  the type's word's low three bytes (its class, its facts and its kind), the
  standing's byte, and a byte of what C's rules read of the flags and the
  object (`BUSTER_NODE_CONTEXTS_*`: an effect, a vector's element, whether
  the record has an object, whether a name's leaf's object has a symbol of
  its own, and whether the kernel placed the record). The symbol's bit is
  the declaration's line's byte's (`BUSTER_NODE_TESTS_LINE_SYMBOL`), made at
  the line's first leaf.
- **The loop** (`node_contexts_window_loop`), 64 records a step, from the
  chunk's last block to its first, so that a consumer's template is made
  before its operands read it:
  - a record's context by its consumer's operator's class and its own place
    among the operands (`NodeContextsTables.classes`, one byte permute of two
    registers by the operator's code; the consumer's bytes through its
    distance forward, from this block and the block after);
  - its template by masks, C's rules (`node_machine_classify`'s) term by
    term: what every operator shares first (a type no template takes, an
    atomic lvalue's read, a folded constant), then a case an operator's
    class, over the record's own bytes, its context and its two operands'
    bytes. C reads T's conversion of each operand; an operand's own type
    stands for it where the two say the same (an array's and a function's
    are a pointer's; two operands of a number's class convert to a
    number's). The template's number is four masks, one a bit;
  - whether its consumer evaluates it and where its target comes from, a
    table of 64 rows by the consumer's template and the place
    (`NodeContextsTables.rows`, one byte permute): T's conversion of the operand, the
    consumer's own type (a wrapper's operand and a store's value: a cast to
    an atomic type converts its operand to the value type and has the atomic
    one), long (a pointer's index), or none;
  - the records under one that isn't evaluated, taken out by its subtree's
    size (the front's `sizes`), a record at a time: few have any.
- **Three things a record at a time.** A pointer's stride, for a pointer's
  sum and a subscript: its type's row's (`node_contexts_stride`), or the
  type table's by a call where the row doesn't hold it. C's rule on T's two
  conversions themselves (`node_contexts_converted`), where the usual
  arithmetic conversions make an operand's class and its conversion's
  differ: a sum or a binary operator of a number's class with an operand
  whose class is no number's and no pointer on either side (a 128-bit
  integer beside a double is converted to it). And a consumer past the block after or a first
  operand before the block before, read by loads into a block of the loop's
  frame and blended in by one masked load a column (a second operand is its
  operator's record's neighbour for every operator with a case).
- **The targets** (`node_contexts_window_targets`), 16 records a step: the
  type a source names, one gather from the consumer's record's line, at its
  conversion of the operand (by the operand's place) or at its own type; or,
  for a call's argument, from the record's own line, at its conversion's
  type (`BUSTER_NODE_CONTEXTS_SOURCE_OWN`).
- **An expression's part** (`node_contexts_window_part`): whether any of its
  records is listed and whether any evaluated one has no template, each a
  range of a block's mask read as one word, and its root's target, which is
  the one the expression is given.
- **The comparison** (`node_contexts_window_compare`): for each decided
  expression, its verdict and each of its records: whether it is evaluated,
  and of an evaluated one its template, its context and its target.
- **A second run.** The shadow runs the three sweeps again over the same
  chunk and counts those ticks apart: what the first run's are over them is
  the chunk's cold start.

The report adds:

- `node-shadow: window contexts expressions=N records=N decided=N (P%)
  their-records=N (P%) of-them-refused=N differences=0 records-that-differ=0
  strides-read=N of-them-asked-of-the-type-table=N
  rules-that-read-the-conversions=N out-of-a-block's-reach=N
  records-swept=N`: the expressions the kernel placed and the reference ran
  on; those decided on the window, and of them those C refuses;
- `node-shadow: window contexts listed, by an expression's first record's
  reason:` a count a reason (`node_contexts_reason_names`);
- `node-shadow: window contexts ticks=N (N a record swept, N a record of the
  decided)`, then by sweep: `the loop`, `the targets`, `an expression's
  part`, and `the reference's C on the decided expressions`, a record of
  theirs;
- `node-shadow: window contexts a second run over the same chunk, not
  counted`, the same three sweeps' ticks.

`--list N` lists the first differences (`node-shadow: window contexts
difference`, with the file, the line and the root's token), and a difference
fails the command.

`test-checker` runs it on the census's twelve fixtures in their window's
form, each with its count of decided and refused expressions pinned, and on a
fixture of its own (`node_contexts_test_fixture`): two functions of 250
expressions with each rule's cases on both sides, whose decided and refused
expressions, strides read, rules that read the conversions, loads out of a
block's reach and listed expressions by reason are pinned. The fixture then
runs again with `BUSTER_NODE_READER_WINDOW_CONTEXTS_FAULT`
(`PPOptions.node_reader`'s bit 8, the tests' alone): one of the reference's
answers is changed for each expression C takes, and the comparison has to
count every one of them. [The design](../node-machine.md#c-on-the-window)
has what it measured.

### A call's arguments in the kernel, and C's call (#2850)

The same command, `measure-node-machine --shadow --window-contexts`. The
kernel in place makes each argument's conversion with its call's record, and
C's answers for a call are made on the window from them. No compile takes
any of it. [The design](../node-machine.md#a-call-on-the-window) has what it
measured.

- **The arguments' rule** (`node_facts_argument_made` in
  `c_check_node_machine_facts.c`) is T's own (`node_machine_argument`) as
  words of 0 or 1, with one branch (a pointer to another pointer type asks
  that pair's slot, `node_facts_pair`). It asks the type table nothing: a
  type's answers are made once with its row (`node_facts_argued`:
  `NodeFacts.argued` by a value's type's index, with the type its value has
  past the parameters above them; `NodeFacts.targets` by a parameter's place,
  the conversion's target with its answers above it). Three answers
  (`BUSTER_NODE_FACTS_ARGUMENT_*`): made (the conversion, T's type and flags,
  in the argument's record's last quadword); kept (the conversion is T's and
  changes nothing the call inherits, so the call's record is the kernel's);
  neither (the call is T's).
- **The answers' slots** (`NodeFactsArgumentSlot`, 4,096 of half a line, by
  a hash of the key): the key is everything the rule reads of the record
  (`node_facts_argument_standing`: its type, the target, the flags of
  `BUSTER_NODE_FACTS_ARGUMENT_FLAGS`, its standing, and whether it is an
  integer constant whose payload is zero). The calls' loop
  (`node_facts_call_arguments`, a second pass over a batch's calls in
  `node_facts_calls_of`) reads each argument's slot, and where the slot has
  another key's answer or none the rule answers there and then
  (`node_facts_argument_fill`, out of line).
- **A call's record** says two bits more in its exponent, beside the tests'
  quiet bit: every argument's conversion is made
  (`BUSTER_NODE_FACTS_CONVERTED_CALL`), and the call is a plain one as C asks
  (`BUSTER_NODE_FACTS_PLAIN_CALL`: its prototype's result, and its arguments'
  conversions' types, are plain scalars). A call with an argument that is
  neither made nor kept goes to the call's rule, which leaves it to T. The
  loop marks it as a value the record doesn't hold whole first: a call of
  its batch may have it as an argument (a call's code is not after a
  call's), and that one then follows it to the rule, which makes it from
  T's record of the first.
- **C's call** (`node_contexts_window_loop`): the call's template
  (`BUSTER_NODE_MACHINE_TEMPLATE_CALL`) where the record says a plain call
  and its context is no address's; its callee evaluated unless the lowering
  calls it directly, by its own bytes (no lvalue, an address constant with
  an object that has a symbol, no offset, no effect); its arguments
  evaluated, each to its own record's conversion's type. C sets a call's
  depth too, which the window does not make.
- **The comparison.** The shadow's check of the kernel in place
  (`node_facts_same`) compares, for a call whose record says its arguments'
  conversions are made, each one with T's (`argument_conversions`, type and
  flags). Before each run every record's last quadword is poisoned
  (`node_facts_given`), so that a reader of one that is no argument's reads
  no conversion.

The report adds:

- `node-shadow: reader-time fact words the kernel in place's calls:
  to-make=N made=N with-every-argument's-conversion=N (P% of the calls to
  make) of-them-plain=N left-for-an-argument=N; the arguments' rule's
  answers=N of-them-in-the-timed-run=N ticks=N (N an answer)`: the calls the
  kernel is to make and those it made; those whose record says every
  argument's conversion is made, and the plain ones among them; those the
  rule left to T for an argument alone; and the rule's answers (the slots'
  misses) over the unit's runs, with their ticks. The timed run has its
  slots filled by the runs before it, so its answers are the keys that were
  not at their slots; a unit's first answers are in no timed figure.

`test-checker`'s fixture (`node_contexts_test_fixture`) has a call for each
term of the rule and pins the calls' counts. Its control changes T's
conversion of each call's arguments too, and the kernel's comparison has to
count every converted call that has an argument, once over poison and once
over decoys. And it runs once more with
`BUSTER_NODE_READER_ONE_SLOT` (`PPOptions.node_reader`'s bit 256, the tests'
alone): an answer is written to every slot, so a key's probe meets another
key's answer wherever the last answer was another's; the records, the
counts and C's answers have to be the same, and the rule's answers and
those of the timed run are pinned.

### An update on the window (#2850)

The same command, `measure-node-machine --shadow --window-contexts`. C's
answers for an update (`++`, `--` and the compound assignments) are made on
the window by the loop's masks. No compile takes any of it. [The
design](../node-machine.md#an-update-on-the-window) has what it measured.

- **The masks** (`node_contexts_window_loop` in
  `c_check_node_machine_window.c`) take what C's plan takes
  (`node_machine_update_plan`): a record of a number's class that is no `/=`
  or `%=` of an integer by an integer constant (listed, as `/` and `%` are),
  whose destination is no atomic's and, where it is a name's, no register
  variable's, and whose second operand is of a number's class unless the
  update is `++`, `--` or a pointer's. The comment there says, for each term
  of the plan, which byte gives it or why none is needed.
- **Three bits are new:** two of an operator's class
  (`BUSTER_NODE_CONTEXTS_CLASS_INCREMENT` and `_QUOTIENT`, the classes'
  table's) and one of a record's flags' byte
  (`BUSTER_NODE_CONTEXTS_REGISTER`, the bytes' sweep's in
  `c_check_node_census.c`, from the use's `BUSTER_CHECK_REGISTER`).
- **A pointer's update** asks its stride in the loop the pointers' sums ask
  theirs in (`node_contexts_stride`, a record at a time): the update's
  record holds T's conversion of its destination as a sum's does its first
  operand's.
- **A unit with a type in a space of its own** (`TypeTable.extended`:
  `__seg_gs`): the loop asks each masked update's destination's type's space
  (`check_lower_segment_override`) a record at a time, and one in a
  segment's lists its expression.
- **An update the masks don't take lists its expression**, with the reason
  `update`, which a `/=` and a `%=` by a constant have too.
- **The operands' rows** are by the update's key: its template's number
  (`BUSTER_NODE_MACHINE_TEMPLATE_UPDATE`) and two bits
  (`BUSTER_NODE_CONTEXTS_KEY_ADDRESS` and `_POINTER`). A destination that is
  no name's is evaluated for its address; the second operand is evaluated to
  T's conversion of it, or to long where the destination is a pointer. The
  loop keeps the keys in a register for the block below, and the column it
  stores has C's templates.

The report adds:

- `node-shadow: window contexts updates=N (P% of the records swept) taken=N
  listed=N`: the updates C asks its plan of, those of them the masks took,
  and the rest.

`test-checker` (part 5 of 8) reads two fixtures for it, in
`c_check_node_census_test.c`: the lines from "An update" on in C on the
window's (`node_contexts_test_source`), a line a case of a term, and a unit
with a segment's space (`node_contexts_test_spaced_source`). The updates
asked and taken are pinned (`NodeContextsExpected.updates` and
`.updates_taken`).

### A condition's branches on the window (#2850)

The same command, `measure-node-machine --shadow --window-contexts`. C's
answers for a statement's CONDITION over `&&`, `||` and `!`, which C
evaluates as branches and no value (`NODE_MACHINE_TEMPLATE_BRANCHES`), are
made on the window by the loop's masks. No compile takes any of it. [The
design](../node-machine.md#a-conditions-branches-on-the-window) has what it
measured.

- **Each record says it of itself** (`node_contexts_window_loop` in
  `c_check_node_machine_window.c`), where C branches a record from the root
  down: from its own bytes, its first operand's and its consumer's class.
  The comment there says, for each term of C's rule
  (`node_machine_branch_operator`, `node_machine_branch_condition`,
  `node_machine_classify_branches`), which byte gives it or why none is
  needed.
- **A branch operator** is a `!`, an `&&` or an `||` that is no folded
  constant, and for `&&` and `||` one whose first operand's truth is not
  known: no constant, or an object's lvalue, whose constant is its address.
- **A CONDITION is branched** where its operand is `&&` or `||`; `&&` and
  `||` are under a CONDITION, an `&&`, an `||` or a `!`; `!` is under an
  `&&` or an `||`.
- **What one level cannot say lists the expression:** an `&&` or an `||`
  that is not branched (a value, or one whose first operand's truth is
  known); a `!` that is a value over `&&` or `||`, which a CONDITION over it
  is too; and a branched `!` over a `!`. The reason is
  `logical-or-conditional`.
- **A branched record's operands** are each evaluated: one branched in its
  turn with no target, a value to T's conversion of it, which C then tests.
  An operand of a type no truth's entries test leaves the record no
  template, which refuses the expression.
- **A class and a bit are new** in the classes' table: a conditional's own
  class (`BUSTER_NODE_CONTEXTS_CLASS_CONDITIONAL`; it had `&&`'s and `||`'s,
  and is listed still), and a statement's CONDITION's bit
  (`BUSTER_NODE_CONTEXTS_CLASS_CONDITION`), which `!` has not. A far first
  operand's standing is a row more of the far lanes'.

The report adds:

- `node-shadow: window contexts branched=N (P% of the records swept)
  of-them-a-condition=N`: the records the loop evaluates as branches, and
  the statements' CONDITIONs among them.

`test-checker` (part 5 of 8) reads the lines from "A condition's branches"
on in C on the window's fixture (`node_contexts_test_source`), a line a case
of a term, and six statements 64 times each for the blocks' ends. The
branched records and the CONDITIONs among them are pinned
(`NodeContextsExpected.branched` and `.branched_roots`).

### The records' bytes' columns (#2850)

No command of its own: the sweep that `--window-tests` and
`--window-contexts` both run (`node_tests_window_bytes_by` in
`c_check_node_census.c`). [The
design](../node-machine.md#the-records-bytes-without-their-four-gathers) has
what it measured.

- **Four columns, no gather.** A step's sixteen records' words, flags,
  objects and standings come from sixteen loads of the lines' first halves,
  two records a register, one permute of two registers for each four
  records, and eight lane shuffles. The last four are masked by the placed
  records: a record the kernel didn't place reads as zeros.
- **The columns' places** are `BUSTER_NODE_TESTS_COLUMN_*`, dwords of a
  record's line; an assert beside them ties them to `NodeFactsRecord` and
  the window's size to a whole number of steps, since a step reads its
  sixteen lines whether placed or not.
- **What is a gather still:** a name's declaration's line's byte, and the
  reader's verify's two.

`test-checker` (part 5 of 8) reads the sweep through the tests on the
window's fixtures and C on the window's: a column that is another record's
fails their pins, and one that is not masked fails the reader's verify, which
counts a record the kernel didn't place that has a byte with the values it
finds wrong (`own_values_wrong`, pinned at zero).

### X's rows on the window (#2850)

`./build/compiler measure-node-machine --shadow --window-rows [--passes 0] [--list N] [preprocessor options] file...`

`--window-rows` adds X's rows to `--window-contexts`' run. For each
expression C takes on the window, the kinds and the classes of each evaluated
record's own entries and of its conversion's are made from C's answers there,
64 records a step, and compared entry by entry with X's tape on T's columns
at reader time (`node_machine_expand`), their reference. An entry's value is
not made yet. No compile takes any of it. [The
design](../node-machine.md#xs-rows-on-the-window-first-their-shapes) has what
it measured.

- **A shape a part** (`c_check_node_machine_rows.c`): a byte for a record's
  own entries and one for its conversion's. Below 64 it is a typed kind's
  number, one entry of the record's class. From 64 up
  (`BUSTER_NODE_ROWS_ADDRESS` to `_FOLDED`) it is a row of `NodeRowsTables`:
  for each of a part's four slots the entry's kind and its class, or where
  the class comes from (`BUSTER_NODE_ROWS_CLASS_OWN`, `_TARGET`,
  `_OPERAND`). `BUSTER_NODE_ROWS_LATER` is the mark of a template whose
  rows are not made here (`NodeRowsTables.templates`).
- **The reads** (`node_rows_window_reads`): masks, 64 records a step,
  compress the numbers of the records to read for into two lists, and a loop
  a list reads a record at a time: a target's type's word where a record
  converts (its class and facts; none for void), and a record's line for a
  member's offset, a truth's operand's conversion's class and whether an
  address constant's object is a local's.
- **The shapes** (`node_rows_window_shapes`), 64 records a step:
  `node_machine_own`'s case of a record's template and
  `node_machine_convert`'s cases as masks over the window's bytes and the
  reads' columns. A term of the rules that no record on the window separates
  is no mask, and the comments there say of each what makes it: the
  discarded context, a complex's and a `_BitInt`'s source, the facts of a
  complex's and an aggregate's conversion, an address constant's offset's
  value.
- **The rows** (`node_rows_window_write`), 64 records a step: a slot's kinds
  and classes by one lookup each of the block's shapes, the classes made from
  their sources, the eight slots transposed into eight records' rows a
  register, and the entries compressed into the chunk's two streams
  (`x_kinds`, `x_classes`). A row's place is `node_rows_window_place`.
- **An expression's part** (`node_rows_window_part`): it is listed for X
  where one of its operand's records is; else its tape is its operand's
  records' rows, in a row in the streams, and its root's. A root's own record
  never lists it alone (the comment there).
- **What is listed**, and left to X on T's columns: an update's template and
  an operator's evaluated as branches; a conversion from or to a class past
  double's (a long double's, a 128-bit integer's, a half-precision one's, an
  aggregate's, a complex type's), which X's rules refuse; and an integer
  constant converted to float or double, whose fold reads its value.
- **A record's type is a column** with `--window-rows`: the records' bytes'
  sweep makes it with its four (`node_tests_window_bytes_by`,
  `window_x_types`), so a target and a type are compared in registers.

The report adds:

- `node-shadow: window rows expressions=N records=N taken=N (P%)
  their-records=N (P%) their-entries=N differences=N
  not-kept-by-the-reference=N second-runs-that-differ=N records-swept=N
  entries-written=N`: the expressions C takes on the window; those whose tape
  is made here; the tapes that differ from X's on T's columns in their
  number of entries, a kind or a class; the taken ones the reference kept no
  tape for, for want of room; and the chunks whose second run wrote another
  number of entries.
- `node-shadow: window rows listed, by an expression's first record's
  reason: update=N branches=N refused-conversion=N
  constant-to-a-floating-class=N`.
- `node-shadow: window rows records read a record at a time: a target's
  word=N (P% of the records swept) a line or a table=N (P%)`: the two lists'
  records.
- `node-shadow: window rows ticks=N (T a record swept, T a record of the
  taken): the reads=N (T) the shapes=N (T) the rows=N (T) an expression's
  part=N (T); the reference's X on the taken expressions=N (T a record of
  theirs)`: TSC ticks. The reference's are with its entries' values, which
  the rows here don't make.
- `node-shadow: window rows a second run over the same chunk, not counted:
  ...`: the same four parts over the chunk once more, their code and tables
  at hand.
- `node-shadow: window rows difference file=F line=N token=N`, with `--list
  N`: where a tape that differs is.

The command fails where a tape differs or a chunk's second run wrote another
number of entries.

`test-checker` (part 5 of 8) reads the rows' fixture
(`node_rows_test_source`): a line a shape, a line a term's case (a `_BitInt`'s
target, the targets past double's, an aggregate converted to an aggregate, a
store of a constant, records that are not evaluated), and two groups of
statements 64 times each for the blocks' ends. It reads C on the window's
fixture through the rows too. Every tape made is X's on T's columns; the
reader's verify finds no byte of the window wrong, the types' column's among
them; the expressions, the tapes, their entries, the listed ones by reason,
the entries written and the records read for are pinned
(`NodeRowsExpected`); and with one
of the reference's answers changed for each tape
(`BUSTER_NODE_READER_WINDOW_ROWS_FAULT`: its first entry's kind, its last's
class and its length in turn) the comparison counts every tape.
`node_rows_test_tables` checks the tables: each shape's entries and length,
the classes' sources the write resolves, each template's shape with the mark
of those whose rows are not made here, the order's permutation, and void's
word, which the reads don't read.
