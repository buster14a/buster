# The node machine: one visit per expression node (#2524): design

Status: step 0, the census, is built and its gate holds
([below](#step-0-the-census)). Step 1, the shadow, is built
([below](#step-1-the-shadow)): the machine's entries are the pipeline's on every
corpus, `-g` included, but its sweeps cost 167 cycles a taken node where the
gate asks for 120 (188 with `&&`, `||` and `?:` and 202 with calls, each measured
on its own, and 223 with both, [below](#one-file-a-sweep): their expressions cost
more a node), and it now codes every operator the census takes (calls, `&&`,
`||` and `?:` [below](#calls), the increments and the compound assignments
[below](#increments-and-compound-assignments)). No compile takes it. Its gains are estimates that
its first two steps exist to confirm or refute; a prototype of its three sweeps
has been run over the compile's real nodes
([below](#what-a-prototype-of-the-sweeps-measured)). It is
[#672](https://github.com/buster14a/buster/issues/672)'s one-core target's
design: the change that the measurements below rank first for one core. The
issue is [#2524](https://github.com/buster14a/buster/issues/2524). The names of
existing code are main's at `d96b2e73`, and every figure not attributed
otherwise is that revision's on the 9700X (CPU 2, under the lock), from
[the measurement record](https://github.com/buster14a/buster-performance/tree/5318c1ea0e3a39f22f01b2bcb837db112c2a8fa7/docs/measurements/one-core-design-672-2026-11-12).

**In one paragraph.** `-c sqlite3.c` spends 87% of its user cycles on function
bodies, and most of that, 70% of the whole compile's, in the checker's stages
between the parser and Tier 0: about 1,600 cycles and 3,250 instructions for
each expression node, which five stages visit in turn, each with its own
dispatch and its own unit-sized record of the node. The node machine reads a
full expression, and later a whole body, from the parser's tree and writes
Tier 0's typed entries in one visit per node, on records that stay in the
first-level cache. What it takes, the stages between don't see. Today's
pipeline is its exact twin, its oracle, and the path for everything it doesn't
take. Expected on `-c sqlite3.c`: about 1.3 times faster with full expressions
taken, 1.6 with whole bodies, 2.0 once those bodies stop using the reader's
token columns.

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
`context[code][operand][the operand's code and standing]` gives each operand
its context and `convert`: the operand's own code is in the key because a
discard's cast to void gives a comma the discard itself and any other operand
a conversion to void (the prototype below: 30,375 operands of `sqlite3.c`'s
casts are undecided without it, none with it). The node's own template is
`template[code][context][class…]`, the inventory's deterministic key with its
facts read from the record beside it instead of from the stream's columns and
the type table. C also counts: each node's entries, the calls, labels and
temporaries under it, so that a call's row is numbered in the tasks' preorder.

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
- **They need a cheap key.** A table is cheaper than the branches it replaces
  only where its key is a few bytes the record already holds. Step 1 measured
  C's and X's tables on whole values as records, and lost to the branches
  ([below](#the-baked-tables-measured)).

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
  refused for the unsequenced check (the census's form: written twice, or
  written below the root and named outside it, neither apart from a sequence
  point nor in a conditional's two arms). The test first written here, of a
  system header's macro's tokens in a user's expression, isn't one: in a
  system header's macro the pipeline only suppresses diagnostics, so a taken
  expression is as silent there as its other tests make it, and the census
  measured that it would refuse 6.5% of `sqlite3.c`'s nodes and 7.7% of Lua's,
  more than the silence budget, nearly all `assert`s' `((void)0)` under
  `NDEBUG`.
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
- **The budget leaves little to win.** T and C cost 48 cycles a node in the
  prototype below, with hashed tables, of the 1,600 a node costs now.

What makes the ordered part cheap is that its loads are of records written a
few nodes ago. If T becomes the limit, the lever is several statements'
sweeps interleaved step by step, since statements between two declarations
are independent of each other: more chains at once, not wider vectors.

### What a prototype of the sweeps measured

The sweeps were run over the compile's real nodes before anything is built.
The lowering's inventory (`measure-lowering-templates`), patched to dump every
evaluated node of every lowered body with the lowering's own key for it and
the entries its task wrote, feeds a prototype that makes T's, C's and X's
decisions from tables alone, an expression's tree at a time, and compares the
entries it writes with the stream's (the record's section 9; `sweeps.c`
there). It is not the machine. It binds no name, reads no type table and
tests nothing for diagnostics: a leaf's facts, those of a cast, a member, a
call, a subscript and a dereference, a call's operands' targets and every
entry's value are given to it. Its tables are learned from the unit and
hashed, not baked and indexed. A key that two nodes answer differently
refuses its nodes' expressions, as does a node whose entries its templates
(16 entries in four runs of at most 8) can't hold.

| | `sqlite3.c` | musl, 1,336 units |
| --- | ---: | ---: |
| evaluated nodes, in full expressions | 266,707 in 52,368 | 131,092 in 26,451 |
| leaves; typed by the table; typed by a lookup | 42%; 22%; 36% | 44%; 28%; 28% |
| T's keys, in conflict | 3,075, 0 | 2,835, 0 |
| C's keys, in conflict | 9,742, 0 | 9,476, 14 |
| template keys, in conflict | 2,400, 3 | 2,396, 4 |
| nodes in taken expressions | 96.5% | 88.2% |
| emitted entries that differ from the stream's | 0 of 346,998 | 0 of 154,132 |
| cycles, instructions, mispredicted branches a node | 99.7, 264, 1.16 | 100.6, 268, 1.16 |

- **The tables decide what the design asks of them.** For every operator
  whose type isn't a lookup's, the operands' facts decide the node's: no key
  in conflict in either corpus, and one corpus's tables answer the other's
  nodes without an error wherever they know the key (51,514 and 27,324
  nodes). The lowering's key, rebuilt from the sweeps' state alone, is the
  lowering's own for every node, and every taken expression's entries are the
  stream's.
- **T is mostly lookups.** 42% of `sqlite3.c`'s evaluated nodes are leaves,
  and 36% are casts (37,534 of the 39,998 to void: the discards), member
  accesses, calls, subscripts and dereferences, whose type a declaration or
  the type table gives. The transitions (`scalar`'s rows and the operators'
  own) type 22%. So T's cost is the binding answers and the type table's
  reads, which today's rules make too, more than its transitions.
- **Five keys' entries aren't the key's to decide** in the two corpora: an
  aggregate's assignment (two keys), an atomic builtin's class and, in musl,
  a complex local's conversion and a local array's value (`LOCAL_ADDRESS` for
  1,152 nodes, `LOCAL` for 47). A bit-field's store has two sequences for a
  key too. These refuse, or get the fact they lack.
- **What the prototype refuses is its templates' shape first**: rows of more
  than 8 entries (post-increments, bit-field stores and reads, `va_arg`: 2.4%
  of `sqlite3.c`'s nodes are in such expressions) and entries written inside
  an operand's (compound assignments and increments: 0.9%). The machine's rows
  need both shapes or leave them to the tasks.
- **Learned tables are not this design's.** With the other corpus's tables
  the prototype takes 59% of `sqlite3.c`'s nodes and 42% of musl's, and
  `sqlite3.c`'s tables give musl 23 wrong entries, all the array's key's,
  whose second answer `sqlite3.c` never shows. That is why the tables are
  baked from the rules over every key and the pipeline stays as their check.
- **The cost.** 99.7 cycles, 264 instructions and 1.16 mispredicted branches
  a node on the 9700X (99.5 to 101.7 over three runs): 16 for T, 32 for C, 37
  for the template's choice and 21 for the expansion when each runs over the
  whole unit, which costs 7% more than an expression at a time and mispredicts
  half as much again. That is 47 cycles a body token, with the lookups given
  and nothing vector yet, against the 752 the stages spend.

## The steps and their gates

Each step is measured on the 9700X before the next begins, and none is taken
by a compile unless its gate holds. `--scalar` keeps today's path throughout.

| step | what is built | gate |
| --- | --- | --- |
| 0. Census | `measure-node-machine`: the pipeline as it is, with the machine's set and its silence tests evaluated on the finished graph for every full expression; counts by reason of refusal, per expression and per body, as `measure-lowering-templates` counts. No machine. | The set takes expressions holding at least 85% of `sqlite3.c`'s evaluated nodes and 75% of Lua's and musl's, and the silence tests refuse under 3% of the nodes. |
| 1. Shadow | The tables, baked, and the three sweeps, run after the pipeline on every root, compared with it and timed (`--time-stages` rows). No compile takes it. | No difference on the corpora, with `-g` too. T, C and X together at most 120 cycles a taken node in the real compile (the prototype's are 103, with its lookups given and its tables hashed). |
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
narrowest set, and the prototype's tables, which have calls, selections and
conversions, take expressions holding 96.5% of the nodes before any test for
diagnostics. Step 3 needs the body whole, and `sqlite3.c`'s ten largest
bodies hold 15.5% of its body tokens, so its set has to reach what those hold:
`goto` and labels (in bodies with 27% of the body tokens), `switch` (20%),
brace initializers (18.5%), `static` locals (15.5%).

## Step 0: the census

`measure-node-machine` (`c_check_node_census.c`, [the guide](commands/lowering.md#node-machine-census-2524))
is step 0: the pipeline as it is, every full expression the tasks lowered read
again from the finished graph, stream and parser's tree by the set's tests and
the silence tests, each silence test only where a warning it stands for is
enabled in the unit. No machine runs. On the 9700X at `eb62e20f` (counts, not
times; [the record](https://github.com/buster14a/buster-performance/tree/151aeb6ce57582f20419567675862274a59f5cf1/docs/measurements/node-machine-census-2524-2026-11-12)):

| | `sqlite3.c` | Lua 5.4.8, 34 files | musl, 1,336 units |
| --- | ---: | ---: | ---: |
| evaluated nodes, in full expressions | 266,707 in 52,368 | 67,358 in 11,562 | 131,092 in 26,502 |
| nodes in expressions the set takes (gate: 85%, 75%, 75%) | 97.54% | 90.89% | 93.03% |
| nodes the silence tests then refuse (gate: under 3%) | 0.00% | 0.02% | 1.24% |
| nodes taken | 97.54% | 90.87% | 91.79% |
| bodies taken whole, and their share of the nodes | 87.6%, 60.2% | 78.7%, 55.4% | 60.8%, 49.5% |

**The gate holds** on all three. What the set refuses, by the share of the
nodes in the expressions it refuses:

- **`sqlite3.c`:** bit-fields 1.54%, callees with semantics of their own 0.38%
  (the library folds `strcmp`, `strlen`, `memcmp`, and the printf family),
  builtins and `va_arg` 0.30%, struct copies 0.21%, brace initializers 0.04%,
  `register` variables 0.02%.
- **Lua:** builtins 4.51%, three quarters of their expressions
  `__builtin_expect`'s (`l_likely`, `l_unlikely`, `luaL_argcheck`), the first
  builtin worth a code of the machine's; the library folds 2.70%; struct copies
  0.85%; other controls 1.05%.
- **musl:** nodes popped twice, compound literals' values, 3.23% (as the
  prototype found); controls 1.89%; complex values 1.62%; builtins 0.99%;
  struct copies 0.58%; callees 0.52%.

The silence tests refuse nothing of `sqlite3.c`, which reports no warning, and
next to nothing of Lua. Of musl's 1.24%: a floating-point pragma's bodies
(`fma.c` and its kin, by design) 0.84%, then parentheses 0.43% and three
`-Wstring-plus-int`s, which are warnings the pipeline reports (`if (map =
__map_file(...))`, `s[e]+0U<<24`), then comparisons at a type's bound, which
are conservative. Two things make that low share trustworthy:

- **Every test is checked against the warnings it stands for.** `test-checker`'s
  fixture has one warning a line, each reported by the compiler with `-Wall
  -Wextra`, and the census must refuse each by its test; disabling one test
  fails it. Every warning ID has a class of the census's (a test stands for
  it, it is no expression's, or it reads the events a taken expression keeps),
  and `test-checker` fails on an ID without one: the two warnings main gained
  while the census was written (`-Wshift-sign-overflow`, `-Wassign-enum`) got
  their tests that way. A warning no test models refuses every expression
  where it is enabled.
- **Its first version refused 1.75% of `sqlite3.c`'s nodes**, and each false
  refusal was a test coarser than its warning, not a warning: a discarded
  `memset` (only `const` and `pure` builtins warn), unsigned 64-bit constants,
  a conditional's two arms (never both evaluated), two fields of one struct
  written in one expression, `&a[N]`, same-width sign changes, and comparisons
  not taken in their common type. The machine's tests have to be that tight
  and still cheap: what each reads (the operator, the operands' kinds and
  constants, the parser's parent of an operator token, an object and member
  offset) is in a node's record or the parser's tree.

The census's set leaves calls, `&&`, `||` and `?:` in, as step 2 does. What it
counts as refused but step 2 could take later: `__builtin_expect` (Lua), the
library folds, bit-fields (`sqlite3.c`) and compound literals (musl). With them
out, `sqlite3.c`'s taken expressions hold 97.5% of its nodes, above the
estimate's optimistic 95% for step 2 ([below](#expected-gains)): the coverage
is not what limits step 2's gain. Step 3's whole bodies are another matter:
87.6% of `sqlite3.c`'s bodies have every expression taken, but they hold 60.2%
of its nodes, the largest bodies being the ones with a bit-field or a call with
semantics somewhere.

Step 1 ([below](#step-1-the-shadow)) runs the machine on the roots the census
takes. The census is where its silence tests start: each is written here
first, against a fixture line of the warning it stands for.

### The readers of an expression's inside

A summary node (step 2) hides its expression's nodes from every pass after the
reader; each pass that reads below a root has to take the summary's side record
or be shown not to need it. From the code at `eb62e20f`, by the files that read
the graph's operators and operands after the graph run:

- **The warnings:** `check_driver_common_warnings` (`c_check_common_warnings.c`:
  the enumeration mixes, the parentheses, shifts and chained comparisons,
  string plus int, sizeof's decay, the graph warnings, for loops, returned
  stack addresses, the uninitialized and unused-but-set scans, format and
  type-safety calls), the graph's own (`c_check_warning_graph.c`,
  `c_check_noderef_graph.c`, `c_check_condition_graph.c`,
  `c_check_overload_graph.c`, `c_check_region_graph.c`), `c_check_unsequenced.c`,
  `c_check_ignored_result.c` and `c_check_musttail.c`. A taken expression has
  none of their warnings by its silence tests; the scans that read events (a
  local read, written or addressed; a call that may not return) take them
  from the side record.
- **The program:** `check_program`'s controls, references and return flow
  (`c_check_program.c`, `c_check_return_flow.c`), which the selection reads,
  the target features' checks (`c_check_target_feature.c`), asm operands
  (`c_check_program_asm.c`) and the driver's own walks (`c_checker_driver.c`:
  interrupt ranges, the x86 builtins' diagnostics).
- **The lowering:** the tasks (`c_check_program_lower.c`, with
  `c_check_program_templates.c` and `c_check_program_batch.c`) and their
  helpers for half, atomic and transported values; `-g`'s rows come from the
  lowering's entry tokens.
- **Read only for a diagnostic**, which a taken expression has none of:
  `c_check_semantic_diagnostic.c` and the warnings' wording.

## Step 1: the shadow

`measure-node-machine --shadow` ([the guide](commands/lowering.md#the-shadow-step-1))
is step 1. The machine is `c_check_node_machine.c`, with one file a sweep
(`_type.c`, `_contexts.c`, `_expand.c`), and the shadow is
`c_check_node_census.c`'s. The machine runs after the pipeline on every full
expression the census takes. Each record it makes is compared with the stream's
node, its tape with the lowering's entries, and its sweeps are timed. No compile
takes it.

### What it is

- **The records** are the expression's graph nodes: one range of them, and the
  root, which the reader can make well after the range (a condition's wrapper
  comes after the statement it controls). The machine reads of them what the
  reader answered at their tokens: a leaf's value (its declaration's or its
  literal's), a cast's and a sizeof's type name, a member's name. It never reads
  the graph run's output, the stream or the lowering's state. In step 2 it reads
  the parser's tree in their place. A record's value is the design's columns,
  not a whole 96-byte `CheckValue` ("T's records as columns", below).
- **T** types each record in one of four ways:
  - a leaf, by the reader's leaf;
  - an arithmetic or bitwise operator, a comparison or `-` and `~` of builtin
    arithmetic types, by its row of the transition table;
  - the lookups (a discard, `->` and `.`, `!` and a condition, `=`, a scalar
    initialization and a return's conversion, `&x`, `*p`, a subscript and a
    pointer plus an integer), by direct rules, each its action's own steps for
    the common case behind a guard;
  - everything else, by the action itself, `check_action_compute`, called on the
    machine's records as the graph run calls it on the stream's. That covers
    constant folds, casts but discards, a global's member and pointer
    comparisons.

  The action is the rows' and the rules' exact twin. `--scalar` types every node
  by it, and `--verify` types every row- and rule-typed node by it too and
  compares the values field by field.
- **The table** (`node_machine_rows`, 3,766 rows over 18 operators) is made from
  the actions. For each operator and each pair of builtin arithmetic types it
  keeps the action's answer for plain rvalues where there is no diagnostic or
  warning. Each operand is tried as an lvalue, whose conversion then adds
  `CONVERT_LVALUE`, and as a literal 1 and 0 with the other operand plain. Twelve
  operand flags are tried alone on every row. A flag that passes to the result on
  every row, or changes nothing on every row, is in the operator's mask. Any
  other flag leaves the node to the action, as a bit-field's width and a fault
  do.

  The bake's initializers build the table on a stack type table of the builtin
  types alone (`baked.c`), so a compile starts with it and `test-baked-tables`
  checks it.
- **C and X** are the node templates' rules (#1048, `c_check_program_templates.c`:
  the classification, each node's own entries and its conversion's), read from
  the records instead of the stream's columns, and `check_lower_logical`'s for
  `&&`, `||` and `?:` ([below](#the-logical-operators-and-the-conditional-rows-before-and-between-operands)). T
  keeps each record's operator, operands, class and facts for them, and one word
  per type of the unit holds its class, facts and kind for every other type's
  lookup.

The machine's set is the census's within those rules. It refuses by code what
they don't take: none of the census's operators now. Calls and `&&`, `||` and
`?:` came first ([below](#calls)), then the increments and the compound
assignments ([below](#increments-and-compound-assignments)).

### What it measured

On the 9700X at `c3494a5f`, on main `2db047f3` (CPU 2, under the lock;
[the record](https://github.com/buster14a/buster-performance/tree/f600821bcda26690dac3d240151ea7d18da7d3b7/docs/measurements/node-machine-shadow-2524-2026-11-12)).
Every taken expression's records and entries are compared:

| | `sqlite3.c` | Lua 5.4.8, 34 files | musl, 1,336 units |
| --- | ---: | ---: | ---: |
| census-taken expressions, evaluated nodes | 51,448, 260,139 | 10,841, 61,207 | 23,037, 120,332 |
| machine-taken expressions | 32,676 (63.5%) | 5,612 (51.8%) | 13,488 (58.6%) |
| machine-taken evaluated nodes, of the census-taken | 130,559 (50.2%) | 25,073 (41.0%) | 56,144 (46.7%) |
| entries compared | 166,504 | 28,506 | 68,642 |
| differences: values, entries, twin; with `-g` | 0, 0, 0; 0, 0, 0 | 0, 0, 0; 0, 0, 0 | 0, 0, 0; 0, 0, 0 |
| T's nodes by a row or a rule, by the action | 75,973, 7,238 | 14,416, 5,098 | 30,299, 4,699 |

c-testsuite's 220 programs (63.7% of their census-taken nodes taken) and
`main.c` (24.6%) differ in nothing either. What the machine refuses is its
codes: of `sqlite3.c`'s census-taken nodes, 49.3% are in expressions with an
operator it has no code for (in 12,836 of those 18,651 expressions the first such
operator is a call) and 0.5% in ones with a node the templates don't take (a
pointer difference, an unsigned division by a power of two).

The sweeps' cost on `sqlite3.c`, in cycles and instructions a taken evaluated
node (the least of 11 passes, each sweep set timed on its own, the user-mode
counters). The first six rows are the machine as it was built, at main
`eedbdece`; the last two are at `2db047f3`:

| version | T | C | X | all three | instructions |
| --- | ---: | ---: | ---: | ---: | ---: |
| the rows, and the action for every other node | 116.0 | 52.3 | 35.4 | 203.7 | 932.7 |
| a discard by the graph run's own steps | 111.8 | 48.6 | 33.6 | 194.0 | 841.2 |
| member, truth and store rules; each record's class and facts kept | 100.2 | 47.5 | 34.0 | 181.7 | 746.9 |
| address, dereference and pointer rules; the records' operators and operands kept | 99.7 | 39.5 | 32.4 | 171.6 | 699.9 |
| one word a type | 99.4 | 39.6 | 31.1 | 170.1 | 695.7 |
| each record's value written as two halves | 96.1 | 42.9 | 31.9 | 170.9 | 693.1 |
| the same, at main `2db047f3` | 95.7 | 43.7 | 31.9 | 171.3 | 693.1 |
| the commit, `c3494a5f` | 96.5 | 39.7 | 30.4 | 166.6 | 693.1 |

The last two rows run the same instructions: about 3% is the placement's.
Lua's is 182.9 cycles. `-c sqlite3.c` is unchanged: 123.9 ms against main's
123.9 ms, with the same instructions. The machine's code and table add 458 KB
to the executable.

**The gate's first half holds** (no difference on the corpora, with `-g`
too). **The second doesn't**: 167 cycles a taken node against 120. That is
about 78 cycles a body token, against the estimate's 55 expected and 90
conservative, so the design isn't refuted: its threshold is 250, and step 2's
projected gain is about 1.3 times at this cost and the estimate's coverage. The
coverage is now half the census's, though, and calls are what that lacks.

What the profiles say (`perf record` of 40 passes, the shares of the whole
run, which the sweeps are most of):

- **T is lookups and copies.** The type table's columns, read at random for
  each record's type (`type_kind`'s loads), were the largest single line until
  the per-type word. `check_action_source_complete`, a member's and a pointer's
  completeness at the node's position, is 7% of the run. The 96-byte values
  copied between records come next: written field by field, they failed store
  forwarding at the parents' reads (#1050's lesson). Written as two halves,
  `node_machine_operand_value` fell from 9.5% to 4.0% of the run, but T only
  from 99 to 96 cycles. The 7,238 nodes left to the action take its whole path.
- **C and X are at the prototype's level.** At 75 cycles together, they match
  the prototype's context, template-choice and expansion steps, 90 with its
  hashed tables. C's most sampled instruction is its branch on whether a
  record is evaluated.

Two follow-ups tested what the profiles suggested
([their record](https://github.com/buster14a/buster-performance/tree/793549b08e9be7a32bb36a16adb930c9a1012ba7/docs/measurements/node-machine-shadow-cost-2524-2026-11-12),
at `9d81a231` on main `a7ca9e8e`):

- **The type table's lookups aren't the cost.** Each type's first complete
  position was put in a per-unit table, so a member's and a pointer's
  completeness became one load. Leaves took a path without the operand
  machinery. T then ran 368 instructions a node instead of 391, and
  `check_action_source_complete` left the profile, but T's cycles barely moved
  (96.5 to 95.2), and the three sweeps' read 172.0, within the placement's 3%.
- **Neither are cold inputs.** `--repeat 2` runs each expression's sweeps twice
  in a row, so the second run finds its inputs in the caches, as the reader
  leaves them in step 2. On `sqlite3.c` the second run costs 152.9 cycles a
  node (T 78.5) against 172.0 for one run: the shadow's cold data is 11% of
  the cost. The rest is the sweeps' own work, about 670 instructions a node at
  an IPC of 4.4. (Lua's small units stay in the caches from one pass to the
  next, and its difference is within the noise.)

So the gate asks for fewer instructions a node, about a fifth fewer at the
sweeps' IPC. The levers, in order:

1. **The design's 28-byte records** in place of whole values, with the leaf's
   class and standing read once from its declaration, as step 2 reads them
   (done, below: worth less than this expected).
2. **The context and template tables baked** in place of C's and X's branches.
   Measured on today's records, they cost more than the branches
   ([below](#the-baked-tables-measured)): they wait for the first lever's
   records to carry what C's key reads.

Coverage comes after: calls first ([below](#calls)).

### The logical operators and the conditional: rows before and between operands

`&&`, `||` and `?:` were refused by code: their entries come before and between
their operands' (the risks' "rows' shape"), and X wrote each record's row after
its operands. They are the machine's now (#2569):

- **C** classifies them by `check_lower_logical`'s rules. A condition the
  program knows (`check_program_runtime_truth`'s test, read from the condition's
  record) numbers no label: its untaken operand isn't evaluated, and the
  condition is evaluated for its effects alone. Otherwise the node numbers two
  labels, its alternate and its end. `a ?: b`, whose condition the tasks keep in
  a temporary, stays the tasks'.
- **X** writes their entries that come before or between operands as a segment
  after an operand's row, which a state bit of the operand's record names: the
  condition's test and branch to the alternate after the condition's row, and a
  conditional's jump to its end after its first arm's, with its alternate there.
  The node's own row has the rest: a logical operator's test of its second
  operand, its jump and its value at the alternate, then its end.
- **The labels** are numbered from the expression's first, in the order the
  tasks number them: the order they pop the records, a preorder. A labeled
  record's rank is the number of labeled records that are its ancestors or lie
  before its subtree's first record, so the numbering reads the labeled records
  alone. Each label's place in the tape is kept: step 2's splice numbers them
  from the function's count, and `check_lower_resolve_labels` gives the
  branches their targets as it does today.
- **T** types `&&` and `||` by `check_action_logic`'s steps for operands the
  common conversions take, scalars without a fault, and `?:` by the action,
  each the rule's twin under `--verify`.
- **The comparison.** After `check_lower_resolve_labels` a branch's value is its
  target's entry, not its label, so the inventory's log keeps each pop's next
  label and each body's labels' entries. Each label of a taken expression must
  be the lowering's of the same number counted from the expression's first, at
  the same entry, and each branch's target that entry.

On the 9700X, against main `0af27a30` (`--verify`, CPU 2, under the lock;
[the record](https://github.com/buster14a/buster-performance/tree/18dc758cb1e170014646de5a6ed39dc5b966c65e/docs/measurements/node-machine-logical-2524-2026-11-12)):

| | `sqlite3.c` | Lua 5.4.8, 34 files | musl, 1,336 units |
| --- | ---: | ---: | ---: |
| machine-taken evaluated nodes, of the census-taken: main | 130,559 (50.2%) | 25,073 (41.0%) | 56,144 (46.7%) |
| with `&&`, `\|\|` and `?:` | 154,280 (59.3%) | 29,595 (48.4%) | 65,775 (54.7%) |
| refused by code: main, now | 49.3%, 40.2% | 55.9%, 48.4% | 48.5%, 39.7% |
| labels compared, in taken expressions | 5,438 in 2,100 | 806 in 302 | 2,300 in 924 |
| differences: values, entries, twin; with `-g` | 0, 0, 0; 0, 0, 0 | 0, 0, 0; 0, 0, 0 | 0, 0, 0; 0, 0, 0 |

Numbering the labels from 1 instead of 0 makes every labeled expression differ,
so the labels' comparison isn't vacuous. What the code refusal leaves on
`sqlite3.c` is calls first: in 13,048 of its 16,538 expressions the first
operator without a code is a call, and the increments and compound
assignments are the rest. No `&&`, `||` or `?:` of `sqlite3.c` is refused by its
template.

The sweeps' cost on `sqlite3.c`, in cycles a taken evaluated node (the least of
11 passes, each sweep set timed on its own, as above):

| version | T | C | X | all three | instructions |
| --- | ---: | ---: | ---: | ---: | ---: |
| main `0af27a30` | 95.5 | 42.7 | 32.9 | 171.1 | 669.9 |
| the three typed by the action (`48abdd08`) | 96.2 | 55.3 | 38.0 | 189.5 | 702.9 |
| `&&` and `\|\|` by a rule, labels numbered from the labeled records (`cadbbe12`) | 97.2 | 52.1 | 38.9 | 188.2 | 695.3 |

Lua's are 180.6 on main and 190.4. The average rises because the expressions
newly taken cost more a node, not the old ones: by the difference of the
totals, the 23,721 newly taken nodes cost 282 cycles each (T 107, C 103, X 72),
against 171 for those taken before, and they write 2.06 entries a node against
1.28 (their tests, branches and jumps). Two attempts moved it no more than
placement's 3%:

- numbering the labels from the labeled records alone, in place of a preorder
  walk of the records (C from 55.3 to 52.1);
- keeping the logical operators' rule, classification and numbering out of
  line, so that `node_machine_evaluate` is inlined again (186.6 in all, C
  53.3), which was then left out.

C's most sampled instruction is still its branch on whether a record is
evaluated. The lever for the cost is the records above, not this code; the
baked tables wait for them ([below](#the-baked-tables-measured)). `-c sqlite3.c` is unchanged: 123.978 ms against
main's 123.923, user cycles +0.02%, 5,323 more instructions (the inventory's
log's test, once a body).

### One file a sweep

So that those levers and the new codes can proceed in parallel pull requests,
the machine is split: `c_check_node_machine.c` keeps its state and the record
accessors (`node_machine_record_*`), and T, C and X have a file each. C and X
read every record field through the accessors, which a 28-byte record layout
implements in place of today's columns (the
[guide](commands/lowering.md#the-shadow-step-1) lists them). The split changes no
behavior. On the 9700X at `34748d68`, on main `0af27a30`
([the record](https://github.com/buster14a/buster-performance/tree/bc6af1371b99cedc4e9293eda00aa76c793fbe4d/docs/measurements/node-machine-split-2524-2026-11-12)):

- **The comparison:** `--verify` on `sqlite3.c`, Lua and musl, with and without
  `-g`, has no difference, and main's counts.
- **T:** the same code, 368.1 instructions a node on `sqlite3.c`.
- **T and C together:** 557.1, as before.
- **All three:** 659.9 against 669.9. Main's X formed and spilled each record's
  value address before its template's branch; the accessors form it only where
  a value field is read.

The split merged as `f5b919f2`, after #2569's `&&`, `||` and `?:` were ported into
its files and #2575's calls (below) merged with them. Calls take a template of
their own after the logical operators' three, and a label's subtree starts at
T's `lowest` record, which counts a call's arguments. On the 9700X
([the record's "The merged head"](https://github.com/buster14a/buster-performance/tree/9a7d2b01a26dfe47410eebac3e5d993f50b503a2/docs/measurements/node-machine-split-2524-2026-11-12)):

- **The port costs nothing.** Ported over main `f2faf255` (`3878feb1`), the
  machine takes main's set with no difference, at 371.5 instructions a node for
  T (main 371.5), 575.4 for T and C (575.3) and 687.9 for all three (695.3).
- **With calls too,** it takes 93.0% of `sqlite3.c`'s census-taken nodes (Lua
  87.2%, musl 82.1%), with no difference, `-g` included. It compares 7,346
  labels on `sqlite3.c` against main's 5,438, because the expressions that mix
  calls with the logical operators are taken now.
- **Its cost** is 222.6 cycles and 764.1 instructions a taken node on
  `sqlite3.c` (T 446.7, T and C 651.1). As #2569 and #2575 each found, the
  newly taken expressions cost more a node than those taken before.

### Calls

The machine's first new code, built on the split's files and accessors
(#2575, [its record](https://github.com/buster14a/buster-performance/tree/d4eb313476e3f3229f6c9b44f434c7976d0809a2/docs/measurements/node-machine-calls-2524-2026-11-12)).
A call was the first operator without a code in 12,836 of `sqlite3.c`'s
18,651 refused expressions.

- **T** types a call by its callee's prototype (`node_machine_call_rule`), as
  "T: bind and type" says. Each argument converts as an assignment would: an
  arithmetic type to another (an enumeration's too), a null pointer constant
  or a pointer to the same pointee, or to a `void` one, with at least its
  qualifiers. A variadic argument takes the default promotions. The action
  takes the rest and is the rule's twin under `--verify`, which compares the
  arguments' conversions too.
- **T refuses as `callee`** a callee with semantics of its own:
  - a library builtin's call that the catalogue folds;
  - a unit with overload sets;
  - a callee without a prototype, or of a convention other than System V's;
  - a callee that returns twice.

  The census refuses before it the callees whose attributes or names a warning
  reads, and in step 2 that test becomes the machine's.
- **C** takes the lowering's plain call (`check_lower_call_body`'s): a result,
  parameters and arguments of plain scalars. It evaluates the callee only where
  the call goes through a pointer, then each argument to its conversion's type.
  Any other call is a `template` refusal: a struct, union, long double, 128-bit
  or half-precision argument, parameter or result.
- **X** writes a `CALL` whose value is the call's row of the function's calls
  table. Rows are numbered as the lowering numbers them, when a call's task
  pops (the tasks' preorder), but relative to the expression's first call.
  X writes in postorder, so a call's row is the evaluated calls written
  before its subtree's least record, plus the evaluated calls that contain it
  (C's call depth). Step 2's splice adds the rows before the expression.
- **The shadow** compares three things:
  - each call's arguments' conversions, with the stream's;
  - its `CALL`'s row, with the stream's by one base for the whole expression;
  - the row's contents, with Tier 0's calls table: the callee's symbol, the
    argument count, classes and sizes, the variadic flag, and no result
    object.

  Numbering the rows in postorder instead made 282 of `sqlite3.c`'s
  expressions differ, the ones with nested calls.

On the 9700X at `4b0e3cb5`, on the split's `27ee009a`, the machine-taken
evaluated nodes as shares of the census-taken ones, with no difference in
values, entries or the twin. The three corpora also ran with `-g`:

| | main `0af27a30` | with calls | calls compared |
| --- | ---: | ---: | ---: |
| `sqlite3.c` | 50.19% | 80.23% | 12,548 |
| Lua 5.4.8, 34 files | 40.96% | 73.27% | 3,708 |
| musl, 1,336 units | 46.66% | 68.59% | 4,596 |
| c-testsuite's 220 programs | 63.67% | 81.15% | 276 |
| `main.c` | 24.55% | 43.52% | 4,034 |

- **What `sqlite3.c` still refuses** is the other codes: 19.2% of its nodes,
  by the first such operator in each expression. The increments come first
  (2,173 expressions), then `&&` (1,600), the compound assignments (1,422),
  `||` (625) and `?:` (588).
- **musl** refuses 59 calls by template, most with a long double.
- **Lua** refuses one call as `callee`, `ldo.c`'s `setjmp`.
- **T types 3, 0 and 11 calls by the action** on the three corpora (volatile
  arguments).

**The cost:** the sweeps take 201.5 cycles and 749.9 instructions a taken node
on `sqlite3.c`, against the split's 171.0 and 659.9. The nodes calls add cost
252 cycles each at the margin, 159 of them in T:

- **More records than nodes.** A direct call's callee is a record T types but
  no node the lowering evaluates: 1.23 records an evaluated node at the
  margin, against 1.09.
- **Two costs of T's own.** The profiles show interning the callee's
  function-to-pointer type (`type_index_find` and `type_find_or_add`, 2.4% of
  the run) and the arguments' conversions (`check_action_convert_plain`, 3.6%
  against 1.9%). Each is a per-unit table's work: a function type's pointer,
  and a pair of types' conversion, as the design's memo has it.

`-c sqlite3.c` is unchanged: 124.1 ms against 124.0 ms, with the same
instructions.

### T's records as columns

T's records are the design's columns now, not a whole `CheckValue` a record
([the guide](commands/lowering.md#the-shadow-step-1) lists them). A record's
value is its type, flags and object, a standing byte (its category, its
constant kind and which field its payload holds) and a 64-bit payload (its
integer, its offset or its floating value's significand, beside a 16-bit
exponent). A value the columns can't hold whole goes whole to a side array:
88 of `sqlite3.c`'s 142,196 records (two payload fields, mostly an
integer-to-pointer cast's).

- **The rows and the rules** read their operands' columns and write their
  records'. The discard, the store, the address, the dereference and the truth
  test convert on the columns in the common case. They make a value whole only
  for the action's own conversion: a floating constant's, a null pointer's, a
  `_Bool` constant's.
- **The action is still the twin.** A value is made whole only where a node
  goes to it, where `--verify` compares it, and where the shadow compares a
  record with the stream.
- **Nothing else changed.** The same nodes go by a row or a rule (75,973) and
  by the action (7,238). `--verify` shows no difference on `sqlite3.c`, Lua,
  musl and `main.c`, with `-g` too.
- **The columns are one block** (`NodeMachineColumns`): every field of a
  record is one load from one base. A pointer a column in `NodeMachine` had to
  be loaded again after every store, since the build's `-fno-strict-aliasing`
  lets any store change it: three instructions a field, which cost C 19 a node.
- **The columns are staggered:** each array starts 128 bytes past the one
  before, modulo 4 KB. Arrays of 4,096 elements one after another had each begun
  on a 4 KB boundary. That put every array's first records, all an expression
  of five nodes touches, in one set of the first-level cache, more lines than
  its 12 ways, and each record's fields 4 KB apart, which the load-store unit
  takes for aliases. Main's whole values had the same layout.

On the 9700X at `2044025c`, on #2571's split (`27ee009a`) and main `0af27a30`
(`sqlite3.c`, CPU 2, under the lock, the least of 11 passes, a taken evaluated
node;
[the record](https://github.com/buster14a/buster-performance/tree/6eb0a054b6f07132d12569ad1d275422084594f1/docs/measurements/node-machine-records-2524-2026-11-12)):

| | T: cycles, instructions | all three: cycles, instructions | all three, warm |
| --- | ---: | ---: | ---: |
| main | 94.8, 368.1 | 170.1, 669.9 | 159.8 |
| the split | 95.2, 368.1 | 169.4, 659.9 | 150.3 |
| the split, staggered | 92.5, 368.1 | 152.9, 659.9 | 130.5 |
| T's columns, a pointer each, not staggered (on main) | 135.1, 347.1 | 285.0, 680.6 | 342.3 |
| T's columns, a pointer each, staggered (`a7554651`) | 88.1, 347.0 | 156.7, 658.9 | 124.9 |
| T's columns in one block, staggered (`2044025c`) | **87.3, 344.0** | **150.2, 627.1** | **123.4** |

"Warm" is the second of two runs in a row (`--repeat 2`). Warm, T costs 68.2
cycles a node, C 35.1 and X 20.1 (main: 79.9, 49.5 and 30.4). Lua's three sweeps
go from 178.6 cycles a node to 169.8, and to 162.2 with the stagger alone.

- **The stagger is most of the gain:** 16.5 cycles of all three sweeps' 169.4
  in one run and 19.8 of 150.3 warm, with no instruction fewer. Without it the
  columns were slower than the whole values: 285 cycles, at an IPC of 2.4.
- **The columns are worth 24 of T's 368 instructions and 33 of all three
  sweeps' 660:** 2.7 cycles in one run and 7.1 warm, against the staggered
  whole values. On Lua they cost 7.6 (169.8 against 162.2): a quarter of its
  typed operators go to the action, which takes and gives whole values. A
  value's copies were never where T's instructions go. By the
  profile of retired instructions they spread over the loop and its context
  test, the leaves' records, the operands' search, the type words and the
  rules' type-table queries, none of them a tenth of T. Holding T's loop inputs
  in locals, and `restrict`, changed nothing measurable.
- **The gate: 150.2 cycles for one run and 123.4 warm, against 120.** The warm
  figure is the reader's position in step 2, and it is 3% over.
- **What T has left is the action and its inputs.** The 7,238 nodes the action
  types are about a fifth of T's cycles (`node_machine_action` and its callees,
  11% of the profile against `node_machine_type`'s 39%). They are casts but
  discards (2,870), a global's member (2,110), constant folds (`-x` 1,316, `|`
  755), pointer comparisons (1,056) and `sizeof` (912). Rules of their own would
  take them off the action. The rest of T is its inputs, cold in one run: the
  leaves' records, the per-type words and the graph's columns, which step 2
  replaces with the parser's tree.

**With the new codes** (calls, `&&`, `||` and `?:`, at `04abc91b`, which merges
#2571's `f5b919f2`), the machine takes 93.0% of `sqlite3.c`'s evaluated nodes,
not 50.2%, and the figures move:

| | T: cycles, instructions | all three: cycles, instructions | all three, warm |
| --- | ---: | ---: | ---: |
| #2571's `f5b919f2` | 119.1, 446.7 | 212.4, 764.1 | 167.1 |
| the same, staggered alone | 119.0, 446.7 | 209.6, 764.1 | 159.3 |
| T's columns (`04abc91b`) | **116.2, 434.5** | **211.3, 733.1** | **160.5** |

- **The stagger is worth 2.8 cycles in one run and 7.8 warm**, a sixth of what
  it was. The calls' 43 cycles a node more are mostly elsewhere.
- **The columns still cut 31 of the 764 instructions, but no cycles:** 1.7
  more in one run and 1.2 warm than the staggered whole values, and 4.0 on Lua.
  The action types more nodes now (12,942 of 147,250), and a call's arguments
  are made whole for the action's conversions.
- **The gate is further off:** 211.3 cycles in one run and 160.5 warm against
  120. `--verify` at `04abc91b` shows no difference on the corpora, with `-g`
  too.

### Increments and compound assignments

`++x`, `--x`, `x++`, `x--` and the ten compound assignments are the
machine's codes. Their entries aren't one row after their operands': the
lowering's `check_lower_update` reads the old value between the destination's
entries and the value's, and computes and stores after the value's. So an
update writes two rows, each of at most 8 entries:

- **The first, after its destination's entries:** the old value, read by name
  (`LOCAL` or `GLOBAL`, and a `DUP` for a postfix one) or through the address
  the destination left (a `DUP` of it and a `LOAD`; a postfix one keeps the
  address and the old value in two temporaries), then converted to the
  computation's type. A destination read by name isn't evaluated, and writes
  nothing of its own.
- **The second, its own:** the operation (an increment's one, or an unsigned
  division's or remainder's shift or mask by a power of two, as the tasks write
  them), the result converted back to the destination's type, and the store.

C marks the destination's record and keeps the update's plan, which X reads at
both rows. A temporary is its number in the expression on the tape; the shadow
matches it with a local the body hasn't met. In step 2 the splice gives it a
local, as `check_lower_temporary` does. T types an update by
`check_action_update`'s own steps: an arithmetic destination's by its
computation's row of the transition table, a pointer's by
`check_action_pointer_arithmetic`'s, and the rest by the action, which
`--verify` checks the rules against. The machine refuses an update of a
`long double`, a 128-bit integer, a vector, an atomic, a register variable or
an object in a segment's space; the census refuses a bit-field's.

On the 9700X at `6196395c`, on main `0af27a30` before #2569 had merged (CPU 2,
under the lock;
[the record](https://github.com/buster14a/buster-performance/tree/722bc1ea492efa797596744b847cdb34c5053803/docs/measurements/node-machine-updates-2524-2026-11-12)):

| | `sqlite3.c` | Lua 5.4.8, 34 files | musl, 1,336 units |
| --- | ---: | ---: | ---: |
| machine-taken evaluated nodes, main | 130,559 (50.2%) | 25,073 (41.0%) | 56,144 (46.7%) |
| machine-taken evaluated nodes, with the updates | 145,386 (55.9%) | 27,971 (45.7%) | 65,409 (54.4%) |
| machine-taken expressions, main and with the updates | 63.5%, 70.2% | 51.8%, 58.6% | 58.6%, 68.5% |
| entries compared | 199,540 | 34,955 | 87,335 |
| differences: values, entries, twin; with `-g` | 0, 0, 0; 0, 0, 0 | 0, 0, 0; 0, 0, 0 | 0, 0, 0; 0, 0, 0 |

- **What the machine refused by code** was then calls, `&&`, `||` and `?:`.
  On `sqlite3.c` the first such operator is a call in 12,856 of 15,214
  expressions. At main, `x++` came second.
- **T's rules type all but 2 of `sqlite3.c`'s updates,** and all of Lua's.
- **The sweeps run 690.4 instructions a taken node on `sqlite3.c`** against
  main's 669.9, at 173.4 to 178.9 cycles against main's 169.8 (Lua: 861.0
  against 838.6).
  - X's instructions an entry are main's: the new expressions write 1.37
    entries a node against 1.28.
  - On main's own expressions the code costs nothing. A build with the update
    codes refused runs 669.2 instructions a node.
- **Two things that cost every node.** They were found on the way, and the
  record has the series:
  - `node_machine_convert` as a call of its own in X, once the updates' rows
    called it too: 11 instructions a node;
  - C's and X's tests for the updates ahead of the common cases: 9.8.

  Both are fixed. **A machine code that adds a caller of a function X or C
  inlines should check the symbols** (`nm build/compiler | grep node_machine_`).
- **Cycles move without the instructions,** by up to about 4% between
  processes running one binary. Compare instructions.

With the logical operators (#2569) merged, an update's first row is one more
segment after an operand's row, by its own state bit beside theirs. At
`58ca4ea7`, against main `f2faf255`:

| | `sqlite3.c` | Lua 5.4.8, 34 files | musl, 1,336 units |
| --- | ---: | ---: | ---: |
| machine-taken evaluated nodes, main | 154,280 (59.3%) | 29,595 (48.4%) | 65,775 (54.7%) |
| machine-taken evaluated nodes, with the updates | 169,496 (65.2%) | 32,547 (53.2%) | 75,541 (62.8%) |
| machine-taken expressions, main and with the updates | 67.6%, 74.3% | 54.6%, 61.5% | 62.6%, 72.7% |
| entries compared | 249,280 | 42,862 | 107,086 |
| differences: values, entries, twin; with `-g` | 0, 0, 0; 0, 0, 0 | 0, 0, 0; 0, 0, 0 | 0, 0, 0; 0, 0, 0 |

- **Calls were then the only code the machine lacked.** They were every
  expression it refused by code: 13,069 of `sqlite3.c`'s, 3,885 of Lua's and
  5,141 of musl's.
- **Instructions a taken node on `sqlite3.c`:** 714.8 against main's 695.3.
  On main's own expressions the code costs 0.4.

With the split files (#2571) and calls (#2575) merged too, the updates live in
the sweeps' files, reading the records through the accessors. At `5200426e`,
against main `5f75ea52`:

| | `sqlite3.c` | Lua 5.4.8, 34 files | musl, 1,336 units |
| --- | ---: | ---: | ---: |
| machine-taken evaluated nodes, main | 242,002 (93.0%) | 53,385 (87.2%) | 98,763 (82.1%) |
| machine-taken evaluated nodes, with the updates | 258,466 (99.4%) | 57,954 (94.7%) | 110,248 (91.6%) |
| refused by code, main and with the updates | 3,616, 0 | 837, 0 | 2,630, 0 |
| entries compared, calls compared | 353,246, 13,473 | 70,723, 4,064 | 149,651, 5,357 |
| differences: values, entries, twin; with `-g` | 0, 0, 0; 0, 0, 0 | 0, 0, 0; 0, 0, 0 | 0, 0, 0; 0, 0, 0 |

- **The machine now has a code for every operator the census takes.** What it
  refuses is templates (on `sqlite3.c`, 159 expressions, most at a `-` or a
  `/`), a few callees and rows. `main.c` and c-testsuite, run locally, take
  98.4% and 99.8% of their census-taken nodes, with no difference.
- **Instructions a taken node on `sqlite3.c`:** 779.5 against main's 764.1.
  On main's own expressions the code costs 2.8. Of those, 2.0 are X's tests
  for an update's destination, which one test of a state past `EVALUATED`
  would fold.

**X now makes that one test** after each record's row
(`node_machine_after_row`), for its consumer's segment, an update's first row
and an update's destination read by name. It also adds 1 to its record every
time, in place of the refusal's bool. The tape and the refusals are the same:
`--verify` reports main's lines on all three corpora, with `-g` too. On the
9700X, against main `f006224e`
([record](https://github.com/buster14a/buster-performance/tree/755230610b031fdbfc782943836ae32ed02802bc/docs/measurements/node-machine-x-state-test-2524-2026-11-12)):

| a taken node | `sqlite3.c` | Lua 5.4.8 |
| --- | ---: | ---: |
| X's instructions, main → one test → and the increment | 117.3 → 115.6 → **110.6** | 115.1 → 113.5 → **108.2** |
| X's cycles, main → one test and the increment | 40.3 → **37.9** | 28.6 → **25.7** |

- **The one test alone saves 1.7 instructions.** Clang spends most of it on
  register copies and stack accesses elsewhere in X, a count on a baseline
  build shows.
- **The increment saves 5.0 more.** The record's end is one test of the
  refusal, and the loop steps two of the records' column pointers with the
  record.

### The baked tables, measured

The second lever was built and measured on today's records, a whole 96-byte
`CheckValue` each. With those records, **neither table beats the branches it
replaces.** Every variant was exact: its entries were the lowering's, and
`--verify` compared it with the branches, its twin, on every expression. None
merged. The variants are kept as diffs in
[the record](https://github.com/buster14a/buster-performance/tree/f942b79e49e55c1d92beabcbc91623443c20032f/docs/measurements/node-machine-baked-tables-2524-2026-11-12),
each against its base: main `0af27a30`, and the split's head `27ee009a`.

- **C by tables.** Each record's facts go into a word, and a per-operator
  mask's `pext` indexes a baked table of rows: the template, the operands'
  contexts and targets, and X's row.
- **X by rows.** Each record's row is written by three stores of eight entries:
  its kinds; its classes, a byte shuffle; its values, a permute. Its conversion's
  row is baked by running the twin's `node_machine_conversion` at every key.

In instructions a taken node on `sqlite3.c`, the least of 11 passes, on the
9700X:

| | C | X |
| --- | ---: | ---: |
| the branches (main; the split) | 189.0 | 112.8; 102.8 |
| C and X by tables, C choosing X's rows (4 variants) | 436.4 to 647.7 | 54.8 to 65.4 |
| X by rows choosing its own, C's branches unchanged (3 variants) | 189.1 | 124.2 to 138.4 |

- **C's cost is its key, not its lookup.** The table reads about 25 bits a
  record: from its value, its type and its operands' types. Each bit is a load
  or two and a compare, and together they cost more than the branches they
  replace, which mispredict 0.19 times a node. Three remedies narrowed the gap
  without closing it:
  - a pass that reads each record's context-free facts before the walk;
  - the columns copied into locals against `-fno-strict-aliasing`'s reloads;
  - fewer bits a record.
- **X's rows win only where C has chosen them.** Choosing a record's row reads
  what the branches read, and costs about what they cost. The three stores save
  little at 1.28 entries a node.

**What would make them pay:** a key of a few byte loads. C's rows read about 11
context-free facts of a record, beside its class and its type's facts:
- its category;
- its constant's kind, where it is pure;
- its object's kind (none, a local, a global, past the unit's, a compound
  literal that isn't static);
- an overload set, a nonzero offset, the type exactly `void`.

The 28-byte records ([#2580](https://github.com/buster14a/buster/pull/2580))
carry the category, the constant's kind and a nonzero offset in their standing
byte. The rest would take a second byte, written where T writes a value. With
it, C's row is one lookup on (code, context, standing, class), and X's row is
C's choice at no extra cost. The record is the baseline that version has to
beat.

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
- **The machine's cost** is the estimate that matters most: 60 to 150 cycles
  a body token against today's 752. Its sweeps are measured in the prototype
  at 47, with hashed tables and the lookups given, so the expected case's 55
  leaves 8 for the lookups, the records and the silence tests: a first
  machine is likelier between the expected and the conservative cases. The
  gain depends little on it. With 90% of the nodes in taken expressions,
  step 2 gives 1.34 times at 55 cycles a body token, 1.29 at 90 and 1.22 at
  150. The other references are the emission, which expands a template an
  entry with registers and frames and encodes it for 76, the typed stream's
  validation, a stack of classes over every entry, for 9, and tcc's whole
  parse and code generation for 131 a token.
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
  and the prototype name (an aggregate's copy, an unsigned division by a
  power of two, a bit-field's store, an atomic builtin's class, a complex
  value's conversion, musl's array locals): the tables then don't hold a
  node's treatment.
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
- **The rows' shape.** A row of 8 entries after the operands holds most nodes,
  not all: 2.2% of `sqlite3.c`'s write before or between their operands (`&&`,
  `||`, `?:` and the compound assignments), and the prototype's four runs
  still left 3.3% of the nodes in refused expressions. Each further shape is a
  row format X has to write without a branch a node, or a refusal. `&&`, `||`
  and `?:` take one: a segment after an operand's row, its consumer's test,
  branch or jump, read from a state bit X already tests
  ([step 1](#the-logical-operators-and-the-conditional-rows-before-and-between-operands)).
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
