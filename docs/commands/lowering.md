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

Unsigned power-of-two division and remainder normalize proven integer constants
from their source type before testing the arithmetic-width value. The regression
includes negative signed char/short/int constants widened to U32/U64, their
compound assignments, and positive narrow constants that still select shifts or
masks. Source `_BitInt` values retain ordinary lowering in this optimization.

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

`./build/compiler measure-node-machine --shadow [--scalar] [--verify] [--passes N] [--repeat N] [--list N] [preprocessor options] file...`
is step 1: the machine itself, run after the pipeline on every full expression
the census takes, compared with what the pipeline made and timed. No compile
takes its entries.

- **Its files:** `c_check_node_machine.c` is its state, its records'
  accessors, `node_machine_begin` and `node_machine_run`; then one file a
  sweep, T `c_check_node_machine_type.c`, C `c_check_node_machine_contexts.c`
  and X `c_check_node_machine_expand.c`, which `main.c` includes after it in
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
  C reads the comma's and a store's type before it evaluates their operands:
  after `node_machine_evaluate`, a call, an accessor would load its column's
  pointer again.
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
  machine's records. **C** and **X** are the node templates' rules
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
  its own (a library builtin's call the catalogue folds, a unit with overload
  sets, no prototype, another convention than System V's, a callee that
  returns twice) is refused as `callee`; a call the lowering doesn't make
  plainly (`check_lower_call_body`'s plain call: a struct's, a union's, a long
  double's, a 128-bit or a half-precision argument, parameter or result) as
  `template`. X writes the arguments, each converted, and a `CALL` whose value
  is the call's row numbered in the tasks' preorder, as the lowering allocates
  its rows, relative to the expression's first call.
- **A call's per-unit answers** (`NodeMachine`, filled as T meets them and
  forgotten by `node_machine_forget`):
  - `function_pointers`, a function type's pointer type by type index
    (`node_machine_function_pointer`), `type_pointer` its twin: a callee's and
    a function argument's conversion is one load once the type's first
    designator has filled it.
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
  taken expressions N times a sweep set, T alone, T and C, and all three,
  interleaved, and keeps each set's least cycles and instructions (this
  thread's user-mode counters) and TSC ticks, per evaluated node: on the 9700X
  only, under its lock, as `taskset -c 2`. Each sweep set starts with the
  unit's call answers forgotten, so that each pass pays their fills as one
  compile of the unit does.

The report gives the expressions, evaluated nodes and calls taken, each
refusal's (shape, code, leaf, context, diagnostic, template, row, root,
callee), the operators the code and template refusals stopped at, the
differences (values, entries, twin), the labels compared, T's nodes by row or
rule and by the action, by operator, the argument memo's entries filled and the
arguments the rule took instead (over every expression T typed, beside the taken
calls' arguments), and the times.
`--list N` prints the first N differences by file and line. The command exits 1
on a difference. `test-checker` runs its fixture (`node_shadow_test_text`, one
construct a line, each taken or refused by a named reason), with `--verify`
and with `--scalar`. [The design](../node-machine.md)'s "Step 1: the shadow"
has the results.
