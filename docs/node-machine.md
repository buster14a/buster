# The node machine: one visit per expression node (#2524): design

Status: design only. Nothing below is implemented, and its gains are estimates
that its first two steps exist to confirm or refute. It is
[#672](https://github.com/buster14a/buster/issues/672)'s one-core target's
design: the change that the measurements below rank first for one core. The
issue is [#2524](https://github.com/buster14a/buster/issues/2524). The names of
existing code are main's at `d96b2e73`, and every figure not attributed
otherwise is that revision's on the 9700X (CPU 2, under the lock), from
[the measurement record](https://github.com/buster14a/buster-performance/tree/cf44ba7a6a5058921026f3d46e0fa2be8468eb49/docs/measurements/one-core-design-672-2026-11-12).

**In one paragraph.** `-c sqlite3.c` spends 87% of its user cycles on function
bodies, and most of that, 70% of the whole compile's, in the checker's stages
between the parser and Tier 0: about 1,600 cycles and 3,250 instructions for
each expression node, which five stages visit in turn, each with its own
dispatch and its own unit-sized record of the node. The node machine reads a full expression, and
later a whole body, from the parser's tree and writes Tier 0's typed entries
in one visit per node, on records that stay in the first-level cache. What it
takes, the stages between don't see. Today's pipeline is its exact twin, its
oracle, and the path for everything it doesn't take. Expected on
`-c sqlite3.c`: about 1.3 times faster with full expressions taken, 1.6 with
whole bodies, 2.0 once those bodies stop using the reader's token columns.

## The problem

### Where one core's time goes

`-c` of the frozen `sqlite3.c` (716,136 preprocessed tokens, the 9700X job's
`compile_time` options) takes 123.8 ms (123.0 to 125.4, 11 runs), 2.89 times
tcc's 42.8 ms: 595.2 M user cycles, 86.3 M kernel cycles and 1,338.8 M user
instructions. Per token, by stage (hardware counters sampled with times, each
sample put in the stage it fell in; the emission's stages interleave, so they
are one row):

| stage | ms | cycles a token | instructions a token | IPC | L1D fills | from DRAM | a load not complete |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| preprocess | 9.4 | 60 | 174 | 2.88 | 1.72 | 0.02 | 20% |
| parse | 7.2 | 33 | 68 | 2.08 | 0.95 | 0.02 | 17% |
| check-input | 45.8 | 307 | 630 | 2.05 | 6.74 | 1.06 | 43% |
| check-expressions | 11.5 | 82 | 265 | 3.23 | 1.10 | 0.05 | 35% |
| check-declarations | 2.0 | 15 | 35 | 2.28 | 0.17 | 0.04 | 60% |
| check-program | 23.7 | 183 | 293 | 1.60 | 2.73 | 0.53 | 54% |
| select | 1.1 | 7 | 11 | 1.50 | 0.24 | 0.03 | 51% |
| lower | 11.4 | 88 | 222 | 2.53 | 1.25 | 0.14 | 43% |
| Tier 0 to the code's atoms | 8.7 | 66 | 149 | 2.26 | 1.08 | 0.05 | 28% |
| whole compile | 122.8 | 853 | 1,872 | 2.19 | 16.2 | 1.98 | 41% |

The cycles are the sampled runs', 2.6% above `perf stat`'s 831 a token. The
stages from `check-input` to `lower` are 96 ms, 78% of the span. Of the
compile's dispatch slots 26% retire: 40% wait for the backend, and a load is
the oldest instruction, not yet complete, in 41% of the cycles.

### The bodies are 87% of it

The same unit, preprocessed, compiled whole and with each function body
replaced by `;` (2,674 bodies, 568,641 of the tokens; 11 alternating runs):

| stage, ms | whole | file scope alone | the bodies' |
| --- | ---: | ---: | ---: |
| parse | 7.283 | 1.674 | 5.609 |
| check-input | 45.399 | 11.530 | 33.869 |
| check-expressions | 11.434 | 0.251 | 11.183 |
| check-declarations and static data | 2.472 | 0.360 | 2.112 |
| check-program | 23.701 | 2.051 | 21.650 |
| select | 1.148 | 0.140 | 1.008 |
| lower | 11.413 | 0.137 | 11.276 |
| Tier 0, the emission and the object | 9.894 | 0.153 | 9.741 |
| total | 115.623 | 17.939 | 97.684 |

The bodies are 79.4% of the tokens, 84.5% of the time and 86.5% of the user
cycles (497.5 M of 575.2 M), and 144 MB of the 228.5 MB peak. Their cost is
875 cycles and 1,814 instructions a body token, of which **the stages from
`check-input` to `lower` are 752 cycles and 1,524 instructions**. The bodies
hold 266,707 evaluated nodes (`measure-lowering-templates`' count), one for
2.13 body tokens: those stages spend about **1,600 cycles and 3,250
instructions on each node**. What a node comes to in the end is 1.5 typed
entries of 10 bytes (406,765 in all).

### Five visits to each node

The bodies' cycles a body token, by family of function (the two profiles'
difference):

| family | cycles | instructions |
| --- | ---: | ---: |
| the reader's walks but the expressions' (`check_input_*`): scopes, declarations, binding answers, per-token passes | 152.9 | 287.7 |
| the program and its controls (`check_program*`, `check_control_*`) | 126.2 | 205.2 |
| warnings (`check_common_*`, `check_unseq_*`, `check_driver_common_warnings`, `check_input_warning_graph`) | 121.6 | 180.3 |
| the graph's construction (`check_input_expression*`, the literals) | 98.9 | 157.3 |
| the lowering (`check_lower_*`) | 93.0 | 239.3 |
| the emission (`tier0_*`, `x86_*`, branch sizing, unwinding) | 76.0 | 186.6 |
| the graph run's loop (`check_graph_*`) | 52.3 | 186.7 |
| the parser and the tree's columns (`parse_*`) | 44.4 | 115.7 |
| the C rules for a node (`check_action_*`) | 30.9 | 110.7 |
| libc's `memset`, `memmove`, `memcmp` | 26.7 | 17.1 |
| initializers and static data | 24.6 | 51.5 |
| the rest | 27.5 | 75.7 |

A node is made by the reader's walk, typed by the graph run, read again by the
warning passes, again by the program's walk for its references, and again by
the lowering's tasks. Each of the five reaches it through a dispatch of its
own and reads it from columns as long as the unit (the graph's 17 a node, the
stream's values and conversions, the reader's token columns), which the stage
before wrote and which have left the first-level cache by then: `check-input`
takes 6.7 L1D fills a token, one of them from DRAM. **The C rules themselves,
the actions, are 31 of the 752 cycles.**

### Two references on the same file and core

- **Tier 0** takes these nodes' typed entries to x86 records, with their
  registers and frames: 76 cycles a body token for the whole emission, the
  encoder, branch sizing and the unwind rows included. It is a table of
  templates over a postorder, which is what the stages before it aren't.
- **tcc** parses, checks and generates machine code in one pass: its
  `tccgen.c` and `x86_64-gen.c` take about 131 cycles a token (its lexer and
  preprocessor 147, where buster's take 60). buster's stages from the parse to
  the object take about 790. The split is by address ranges of the package's
  stripped library, so it is approximate.

### What the stages' own redesigns found

Each of the three redesign issues asked the right question of one stage, and
each answer stopped at the stage's boundary:

- **The graph's construction (#1038, closed).** The coded walk gives each token
  a class and acts by it. #1671 then found no part of `check-input` worth more
  than 0.3 ms by a local change.
- **The graph run (#1050).** 85% of the non-leaf nodes repeat an earlier
  node's key (150,819 hits and 26,227 misses at this main), but a hit still
  builds its key from the columns and writes every column of the stream, so it
  costs 123 cycles against an action's 160 (on the VM that measured it), and
  its 4.5 MB table is new misses to a compile that runs once: the real compile
  is slower with it
  ([the checker's design](c-checker.md), "Memoized transitions").
- **The lowering (#1048).** A node's entries are a function of a small key:
  2,050 of 2,055 ordinary keys are deterministic and 189 abstract patterns
  cover 93.0% of the nodes. But a pass that classifies every node costs 44 ns
  a node where its splices save 15, a sweep per statement is 1.15 times
  slower, and leaner steps retire 1.6% fewer instructions for no fewer cycles:
  "the time is in the rules, not in reaching the nodes"
  ([the lowering's design](c-lowering.md), "Node templates over a body").
  Each rule begins by reading the node's facts back through chains of
  dependent loads: its type, then the type's class; its conversions, then
  their types.

They agree on three things this design is built on. A node's treatment is a
function of a small key. The key's facts are in registers when the node is
typed, and expensive to read back afterwards. And a pass that is added beside
the stages loses: the gain is in visits that no longer happen.

## The design

### One visit, one record

For each full expression the machine makes **three sweeps over one small array
of node records**, all of it in the first-level cache:

1. **T, up the postorder: bind and type.** Each node's record is made from its
   operands' records by a table transition: its type, its class, how its value
   stands (a constant, an lvalue in a local, an lvalue at an address, an
   rvalue), and its payload. A transition that could raise a diagnostic, or
   that the tables don't have, **refuses the expression**.
2. **C, down from the root: contexts.** Each node gives its operands their
   context (value, address or discarded) and the class its consumer converts
   them to, and takes its own entry template by its context. This is the
   lowering's top-down part: `a[i]`'s operand is an address or a value by its
   type, so it follows T.
3. **X, up again: expand.** Each node's template row is written to the entry
   tape: kinds, classes and values, in the evaluation order the typed stream
   has.

Nothing else is made for the expression: no graph nodes, no leaf records, no
stream rows, no conversion records, no tasks. Its entries wait in the body's
tape for the place the lowering gives them.

An example, with `struct S { long m; long n; } *p; int a, b;` all locals, of
`p->n = a + b * 2;`:

| node | T: class, standing, payload | C: context, conversion | X: its entries |
| --- | --- | --- | --- |
| `p` | U64, lvalue in a local, local `p` | value | `LOCAL` U64 `p` |
| `->n` | I64, lvalue at an address, offset 8 | address | `CONST` U64 8, `PTR_ADD` U64 |
| `a` | I32, lvalue in a local, local `a` | value | `LOCAL` I32 `a` |
| `b` | I32, lvalue in a local, local `b` | value | `LOCAL` I32 `b` |
| `2` | I32, constant, 2 | value | `CONST` I32 2 |
| `*` | I32, rvalue | value | `MUL` I32 |
| `+` | I32, rvalue | value, to I64 | `ADD` I32, `CONVERT` I64 |
| `=` | I64, rvalue | discarded | `STORE` I64, `DISCARD` I64 |
| `;` | void | | |

Eleven entries, the ones the tasks write today (the inventory's patterns
`local.n`, `C const.a ptr-add.a`, `C C add.n v:convert.t` and
`C C store.n v:discard.n`), from nine records of 28 bytes.

### Where it sits

The machine is the reader's third way to read an expression, beside the
scalar walk and the coded walk, and it runs where they run: after the reader's
walk in source order has bound the range's names and made its declarations'
types (`check_input_resolve_range`, the binding answers, the controls and the
constant contexts), and before `check_input_expressions_coded_range` acts on
the range's tokens.

- **Taken:** the machine writes the expression's entries to the body's native
  tape and one **summary node** to the graph: a leaf of a new operator whose
  value is the root's (its type, category and constant, as any leaf's record
  holds them), with a side record of where its entries, its references and
  its events are. The expression's tokens are marked as tokens the walk
  doesn't act at, as it marks them today for tokens without a class.
- **Refused:** nothing is written, and the coded walk reads the expression as
  it does today.

So the stages after the reader run over a graph that has one node where it had
many, and each learns one thing: what a summary node is. `check_graph_run`
completes it as a leaf.
The warning passes find nothing under it, which is what the machine has
proved (below). `check_program` takes its references and events from its side
record. The lowering's loop splices its entries where it pops the root, as
`check_lower_template_splice` splices a template's slots today.

The roots it reads at first are the run-time full expressions of function
bodies: an expression statement's, a condition's, a `for`'s clauses', a
`return`'s value and a scalar automatic object's initializer. The reader makes
the root's wrapper as a node already (the discard's cast to void, a
`condition`, a `convert` to the return type, an `initialize`), so the wrapper
is a node of the machine's too and the summary node has the type its consumers
expect. Constant contexts, static initializers, brace initializers and
variable-length arrays' bounds stay the graph's.

### The records and the tapes

A full expression's nodes, in postorder, as columns on the reader's scratch
(`sqlite3.c`'s statements' expressions hold 5.1 evaluated nodes on average; an
expression of more than 4,096 nodes is refused):

| column | bits | what |
| --- | ---: | --- |
| `code` | 8 | the node's operator: the coded walk's class (`CheckInputExpressionCode`) with the token's kind |
| `class` | 8 | the value's `BUSTER_TYPED_CLASS_*`, or none for an aggregate, whose value is its address |
| `standing` | 8 | constant, lvalue in a local, lvalue in a global, lvalue at an address, rvalue, function; and its purity |
| `context` | 8 | value, address or discarded: written by its consumer in C |
| `convert` | 8 | the class its consumer converts it to, or none: written by its consumer in C |
| `template` | 8 | its row of the entry table, chosen in C |
| `first` | 16 | where its entries start in the expression's tape: X's running count |
| `type` | 32 | its `TypeId` |
| `token` | 32 | its token: `-g`'s rows, and a refusal's place |
| `left`, `right` | 16 each | its operands' records (a call's arguments are a chain through `right`) |
| `payload` | 64 | a local's or a symbol's object, a constant's bits, a member's offset, an element's size, a call's row, a label |

28 bytes a node. Beside them, per body:

- **The native tape:** Tier 0's own three columns (`kinds`, `classes`,
  `values`), the taken expressions' entries one after another, with an optional
  fourth of tokens under `-g`.
- **The side records,** one a summary node: its entries' range, its range of
  **references** (the declarations it names, evaluated or not: the selection's
  and the unused warnings' input) and its range of **events** in evaluation
  order (a local read, written or its address taken; a call, with whether its
  callee returns): what `check_program`'s walks and the uninitialized scan
  read under a root today.

Payloads that depend on the function's numbering are **relative** on the tape:
a local is its object, a call's row and a label count from the expression's
first, a temporary from its first. The splice rebases them by kind, 64
entries' kinds compared at a time: the function's local for an object
(`local_map`), its call rows, labels and temporaries by the counts before the
root. The lowering's numbering stays its own, so a body's
stream is the same whichever of its expressions the machine took.

### T: bind and type, by transition

T is a loop over the postorder with a stack of operand records, one case a
code, each a few loads from the first-level cache and one table row:

- **An identifier:** its declaration is the reader's binding answer
  (`declarations[token]`), and its record a copy of the declaration's: its
  type, its class (a byte a declaration, made once), its standing and its
  payload. A declaration with a flag the tables don't model (deprecated,
  unavailable, an overload, a builtin's name, a register variable) refuses.
- **A literal:** an integer's type by C's rule for its value and suffix; a
  floating or a string literal by the reader's own functions
  (`check_literal_number`, `check_input_string`), whose answers it copies.
- **A scalar operator** (unary, binary, compare, shift, `!`, the scalar
  conversions): one row of `scalar[code][kind(a)][kind(b)]`, where a kind is
  one of 32 (the integer classes with `bool` and enumerations apart, the
  floating classes, a pointer to an object, to a function, to `void`). The row
  gives the result's kind, each operand's conversion, the fold for constant
  operands and a **diagnostic mask**. A pointer row carries a rule instead of a
  kind (the result is operand a's type, its pointee, `int`, `ptrdiff_t`), with
  the element size read from the type table.
- **A member access:** `type_lookup_member` by the record's type and the name,
  as the action does it; a bit-field refuses until its templates are in the
  set.
- **An assignment, an argument, a return, an initialization:** the scalar
  rows where the two types' kinds decide (arithmetic to arithmetic, the same
  `TypeId`, a null pointer constant). Two different pointer types, or a record,
  ask a memo keyed by the two `TypeId`s, whose miss runs the existing action
  (`check_action_scalar`, a pure function of its operands) and keeps whether
  it converted without a diagnostic.
- **A call:** the callee's type gives the parameters, each argument the rule
  above, a variadic one the default promotions' row. A callee with semantics
  of its own (a builtin, a library builtin the catalogue folds or checks, a
  `format`, `nonnull` or `diagnose_if` attribute, a returns-twice function)
  refuses.
- **`&&`, `||`, `?:`, the comma, `&`, `*`, `[]`, `++`, `--`, compound
  assignment, a cast, `sizeof`:** rows of their own; a cast's and a
  `sizeof`'s type name is the type query the reader's walk already answered
  at that token.

A constant operand's value is payload, not key: `scalar`'s fold is the vector
path's integer fold, and a fold that can warn (a division by zero, a shift
count out of range, an overflow) sets the row's mask for those values. So the
table doesn't grow with the unit's constants, which made #1050's table 26,135
entries.

### C and X: contexts and expansion

C walks the records from the root down. A node's row of
`context[code][operand][standing]` gives each operand its context and
`convert`; its own template is `template[code][context][class…]`, the
inventory's deterministic key with its facts read from the record beside it
instead of from the stream's columns and the type table. C also counts: each
node's entries, the calls, labels and temporaries under it, so that a call's
row is numbered in the tasks' preorder.

X writes each node's row with three stores, whatever its length, and advances
by its count: 8 bytes of kinds from the table; 8 bytes of classes by one byte
shuffle of a vector that holds the node's class, its target's and the fixed
ones (U64 for an address, I64 for an offset); and 64 bytes of values by one
permute of the node's payload, its size and the row's constants. Rows overlap
as `check_lower_template_splice`'s slots do. A selection's branches name
labels the splice numbers, and `check_lower_resolve_labels` gives them their
targets as it does today.

### The tables

- **Made from the oracle.** `scalar`, `context` and `template` are baked
  (`baked.c`): the build runs the existing actions over every pair of kinds
  for each code, and the lowering's rules (`check_lower_template_classify`'s)
  over every key those answers can give, and keeps the rows whose answer has
  no diagnostic and whose entries fit a row. The rest are refusals. The tables
  are then the old code's own answers, tabulated, and a change to a rule
  changes them at the next build. `test-baked-tables` checks them as it
  checks the encoder's.
- **Small.** 64 codes by 32 by 32 kinds of 4 bytes is 256 KB before the codes
  that take one operand or none are cut down, in the second-level cache, and a
  unit touches a few hundred of its rows; the entry table is about 250 rows of
  24 bytes (the inventory's 240 abstract patterns with calls, selections and
  bit-fields).
- **Per unit:** only the pointer-pair memo and the member lookup's own index,
  both of which exist.

### Silence

**The machine reports nothing.** It takes an expression only when it has
proved that the pipeline would report nothing for it, and refuses otherwise,
so that every diagnostic is still made by the code that makes it today, with
its words, its place, its notes, its order and its pragmas. The proof is a
conjunction of cheap, conservative tests:

- **Row masks.** Each row's diagnostic mask names the warning groups that
  could fire for that operator and those kinds (a sign comparison, a constant
  conversion, a pointer-sign mismatch), and is tested against the unit's
  enabled groups: a warning that is off refuses nothing.
- **Shapes.** Tests on the records for what isn't a pair of kinds: a shift
  whose operand is an unparenthesized `+`, an assignment used as a condition,
  a comparison of an object with itself, a discarded value of a pure operator,
  a `return` of a local's address.
- **Effects.** An expression that writes an object and names it twice is
  refused for the unsequenced check; one whose tokens come from a macro of a
  system header, where the warnings' suppression differs, by a bit a token.
- **Regions.** A diagnostic pragma, a floating-point pragma or a statement
  attribute in the body refuses the body's expressions.

A refusal costs only the sweeps done so far, and its expression then costs
what it costs today. `sqlite3.c` reports nothing and a musl unit one
`-Wshift-op-parentheses`, from a header, so refusals come from the tests'
coarseness, which step 0 measures before anything is built.

**A new diagnostic lands with its refusal.** The danger of a second reader is
a diagnostic added to the old path that the machine doesn't know. Two guards:
every fixture that expects a diagnostic inside an expression also asserts that
the machine refused that expression, and a diagnostic's site names the
operators it can fire on, which refuse by default until a tighter test is
written.

### The twin, the oracle and the checks

- **`--scalar` never runs the machine**, as it never runs the coded walk: the
  pipeline as it is today is the twin, and every existing comparison of the
  default path with `--scalar` (objects byte for byte, diagnostics, the typed
  dumps) is a comparison of the machine with its oracle.
- **`--verify` reads each taken expression both ways**: the machine's entries,
  references, events and root value against the graph's, the stream's and the
  tasks' for the same expression, and fails on a difference, naming the node.
- **A shadow mode** (step 1) runs the machine after the pipeline on every
  expression without taking any, compares, and times it.
- **Fixtures** for each code and each refusal, generated expressions over the
  kinds against the actions (`test-checker`), and the corpora
  (`test-checker-corpus`, `sqlite3.c`, Lua, musl, c-testsuite, `main.c`) with
  both paths' objects compared.
- **Type numbers.** T interns a derived type (a pointer for `&x`, a decayed
  array's) earlier than the graph run would have, so `TypeId`s may differ from
  `--scalar`'s. Nothing emitted should read them: the typed stream has
  classes, diagnostics print types by structure, and `-g`'s types are added as
  declarations reference them (`check_debug_type_reference`). Step 1 checks
  that on the corpora with `-g`; if something does read them, the machine
  records its interned types with their positions and replays them in source
  order, as the deferred reader's lanes do (`TypeChanges`).

### Data-parallel first: what is vector and what is ordered

The vector parts, each with the scalar twin it has or gets:

- **Classes:** `check_input_expression_classes` already gives 64 tokens their
  classes at a time; the machine's codes are those.
- **Refusal by spelling:** one compare of an expression's class bytes against
  the set the machine has no code for, 64 tokens at a time, before T starts.
- **X's rows:** a row is three in-register table lookups (a load, a byte
  shuffle, a 64-bit permute) and three stores, 8 entries wide.
- **The splice's rebasing:** kinds compared 64 at a time, the function's bases
  added under their masks.
- **The folds:** the vector path's integer fold, 8 lanes.
- **Around it, unchanged:** the literals' decoding, `abi_assign` across a
  body's calls, the labels' targets, Tier 0.

T and C stay one node at a time, and this is why:

- **The dependency is the tree.** In a postorder a parent follows its last
  child. #1050's regions held 1.4 nodes a batch, #1146's windows 2.97 a
  round, and the inventory's maximal subtrees of template nodes 2.83.
- **On Zen 5 a batch's gathers cost more than the scalar loads.** A
  `vpgatherdd` of 16 lanes is 12.6 cycles and a `vpscatterdd` 17, against three
  first-level loads a cycle. #1201's vector classification ran 430
  instructions with 9 gathers for 16 nodes and was slower than its scalar
  twin on the 9700X; `parse_tree_columns` is scalar for the same reason.
- **The budget leaves little to win.** T and C are estimated at about 40
  cycles a node, of the 1,600 a node costs now.

What makes the ordered part cheap is that its loads are of records written a
few nodes ago. If T becomes the limit, the lever is several statements'
sweeps interleaved step by step, since statements between two declarations
are independent of each other: more chains at once, not wider vectors.

## The steps and their gates

Each step is measured on the 9700X before the next begins, and none is taken
by a compile unless its gate holds. `--scalar` keeps today's path throughout.

| step | what is built | gate |
| --- | --- | --- |
| 0. Census | `measure-node-machine`: the pipeline as it is, with the machine's set and its silence tests evaluated on the finished graph for every full expression; counts by reason of refusal, per expression and per body, as `measure-lowering-templates` counts. No machine. | The set takes expressions holding at least 85% of `sqlite3.c`'s evaluated nodes and 75% of Lua's and musl's, and the silence tests refuse under 3% of the nodes. |
| 1. Shadow | The tables, baked, and the three sweeps, run after the pipeline on every root, compared with it and timed (`--time-stages` rows). No compile takes it. | No difference on the corpora, with `-g` too. T, C and X together at most 120 cycles a taken node in the real compile. |
| 2. Full expressions | Summary nodes, side records, the splice; the stages' walks read them. | `-c sqlite3.c` at least 1.2 times faster across placements (expected 1.3), both paths' objects and diagnostics the same on every corpus, no unit of musl or Lua slower by more than 1%. |
| 3. Whole bodies | Statements, controls, labels, the return flow and the unused and uninitialized events by the machine, for a body whose every expression and statement it takes: `check_program`, the selection's walk and the lowering skip such a body, as they skip a body `-flazy-system-headers` deferred. A body not taken whole keeps step 2's expressions. | At least 1.35 times cumulative (expected 1.6). |
| 4. Bodies without the token columns | Scopes and local declarations by the machine, with file-scope names through `c_environment_lookup` at the body's opening. The order is the deferred reader's: the top level first with every body cut out (`check_deferred_cut`), then each body by the machine, and a body it refuses by the reader's walks, as a deferred body is read today. A taken body touches none of the reader's token columns. | At least 1.6 times cumulative (expected 2.0); peak RSS under 150 MB. |

Step 2's set is what the inventory says a body can't do without: with ordinary
nodes alone, the bodies free of an exception hold 2.8% of `sqlite3.c`'s nodes;
with calls and `&&`, `||` and `?:`, 67.0%; with bit-fields, 92.4%; with
`va_arg` and the builtins, all of them (musl's selected bodies: 7.9%, 73.6%,
75.0% and 80.7%, the rest complex, atomic and vector values and compound
literals). Step 2 takes expressions, not bodies, so it gains from the first
day's coverage: 69% of the statements' expressions are already whole under the
narrowest set. Step 3 needs the body whole, and `sqlite3.c`'s ten largest
bodies hold 15.5% of its body tokens, so its set has to reach what those hold:
`goto` and labels (in bodies with 27% of the body tokens), `switch` (20%),
brace initializers (18.5%), `static` locals (15.5%).

## Expected gains

On `-c sqlite3.c`, of 681.5 M cycles a compile (user and kernel):

| | conservative | expected | optimistic |
| --- | ---: | ---: | ---: |
| nodes in taken expressions (step 2), tokens in taken bodies (steps 3 and 4) | 80%, 70% | 90%, 85% | 95%, 95% |
| the machine's cost, cycles a body token: expressions, statements, the body's reading | 90, 20, 40 | 55, 15, 30 | 45, 10, 5 |
| after step 2 | 1.24 | 1.34 | 1.39 |
| after step 3 | 1.38 | 1.57 | 1.68 |
| after step 4 | 1.6 | 2.0 | 2.4 |

Each cell is 681.5 M over what is left when a step's removed work, at its
coverage, less the machine's own, is taken from it (the removed work below; the
kernel's part counted at 10 M for step 2's rows, 5 M for step 3's and 32 M for
step 4's columns, each at its coverage).

- **The bound.** If the bodies' work from `check-input` to `lower` were free,
  the compile would be 2.9 times faster: 417 M of its user cycles and about
  30 M of its kernel cycles (the pages of the columns that work fills).
- **Step 2** removes, for a taken expression, the graph's construction (99
  cycles a body token), the graph run (89 with its rules), the node-driven
  warning passes (about 90 of 122), the program's reference walk (about 35 of
  126) and the lowering's tasks (about 70 of 93): about 390 at full coverage,
  for a machine of about 55.
- **Step 3** removes the rest of the program, the controls and the lowering
  for a taken body (about 165), and **step 4** the reader's walks and passes
  over its tokens (about 180, with `memset`'s share), for about 20 and 40
  more of the machine.
- **The machine's cost** is the estimate that matters most and is the least
  measured: 60 to 150 cycles a body token against today's 752. Its references
  are the emission, which expands a template an entry with registers and
  frames and encodes it for 76, the typed stream's validation, a stack of
  classes over every entry, for 9, and tcc's whole parse and code generation
  for 131 a token.
- **Memory.** A taken body leaves no graph, stream or program rows; after step
  4, no token columns either. The bodies hold 144 MB of the 228.5 MB peak, 85
  MB of it the reader's.
- **musl** gains less: its tokens are 61% file scope and 28% bodies, and
  `check-input`'s walk of declarations is 47% of its user cycles, the kernel
  22% of all its cycles. Step 4 is estimated at about 1.1 times there. Its
  lever is the file scope's reader, below.

## What would refute it

- **The census (step 0)** finds that the set plus the silence tests take under
  70% of `sqlite3.c`'s nodes after the cheap tightenings: the tail is then
  fatter than the lowering's inventory says, and step 2's gain falls under
  1.2.
- **The shadow (step 1)** measures the sweeps above 250 cycles a taken node,
  or finds keys whose rows aren't deterministic beyond the few the inventory
  names (the aggregate copies by size, an unsigned division by a power of
  two): the tables then don't hold a node's treatment.
- **Step 2's compile** gains less than the stages' removed work predicts: the
  cost was then not the visits, and steps 3 and 4 shouldn't start.

Each is a measurement that ends the work with a record, as #1048's and
#1050's did.

## Risks and open questions

- **A diagnostic the machine doesn't know.** The worst failure is silent: an
  expression taken that the old path would have warned about. The guards are
  the refusal-by-default rule, the fixtures' assertion, `--verify` in the
  qualification rows, and the corpus and mutation campaigns comparing both
  paths' diagnostics (`test-checker-corpus`, the differential suite).
- **Two readers of C.** Every rule the tables can't derive (the shapes, the
  call's refusals) is written twice. The design keeps that list short by
  refusing: the machine implements no diagnostic, no builtin and no extension
  at first.
- **Coverage where it counts.** Steps 3 and 4 gain by whole bodies, and the
  largest bodies have everything in them. Their sets grow by measured share,
  one construct a pull request, each with its fixtures.
- **Summary nodes.** Every reader of an expression's inside has to take a
  summary node's side record or be proved not to need it: the reference walk,
  the return flow, the uninitialized and unused-but-set scans, the musttail
  and cleanup checks, the debug rows. Step 0 lists them from the code.
- **`-g`.** A taken expression's entries need their tokens, which the record
  has; a body with `-g` can be refused until the fourth column is written.
- **Code size.** The compile is 24% frontend-bound and misses the op cache 70
  times a token. The machine's loop is small and replaces large ones, but it
  is one more unit until the old path's share shrinks.
- **Lanes.** #113's and #151's lanes read bodies apart already. The machine
  writes only its own tapes and the summary nodes of its own body's range, so
  it runs on a lane as the coded walk does; steps 3 and 4 make a body's
  compile independent of the unit's columns, which is #672's windows.
- **Open:** whether C and X are one sweep; whether the summary node is a leaf
  or a handle; whether the tables are baked from the actions or written by
  hand with the actions as their test; where the pointer-pair memo lives.

## What it leaves

After step 4 the expected compile is about 340 M cycles, in which the largest
parts are the file scope's reader (about 56 M and its pages), the bodies the
machine refused, Tier 0 and the emission (55 M), the machine itself, the
preprocessor (51 M) and the parse. The next lever is then the same change for
the file scope: declarations read one after another into an environment,
without token columns, which is also musl's first cost. It is not designed
here.

What this design is not:

- **Not #672's windows.** The windows run today's stages on one function's
  columns at a time, for the cache. The experiment on #672 found their gain on
  one core limited to units as large as `sqlite3.c` (marginal cycles a token
  flat to 125,000 tokens), with the instructions unchanged. The machine
  removes the visits; from step 3 on it is also function-major, which is what
  the windows need.
- **Not a memo.** The tables are functions of kinds, baked, not a cache of the
  unit's first answers: #1050 measured what a per-unit table costs a compile
  that runs once.
- **Not a pass beside the stages.** #1048 measured that too. Nothing here
  runs for a node that another stage then visits again, except in the shadow.

## Related issues

- [#672](https://github.com/buster14a/buster/issues/672): the tracker, its
  windows design and their experiment.
- [#1050](https://github.com/buster14a/buster/issues/1050),
  [#1048](https://github.com/buster14a/buster/issues/1048): the stages'
  redesigns, whose inventory, transitions, template rules and splice this
  uses. Their owners' areas (`c_checker.c`'s run, `c_check_program_lower.c`)
  are theirs: step 2's hooks there are small and theirs to review.
- [#1671](https://github.com/buster14a/buster/issues/1671),
  [#1701](https://github.com/buster14a/buster/issues/1701): the reader's
  remaining time and the memory, which steps 2 to 4 take the bodies' part of.
- [#113](https://github.com/buster14a/buster/issues/113),
  [#151](https://github.com/buster14a/buster/issues/151): bodies on lanes.
- [#1340](https://github.com/buster14a/buster/issues/1340): the common
  warnings' cost, 122 cycles a body token here.
