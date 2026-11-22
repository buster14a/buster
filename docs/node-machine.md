# The node machine: one visit per expression node (#2524): design

Status: step 0, the census, is built and its gate holds
([below](#step-0-the-census)). Step 1, the shadow, is built
([below](#step-1-the-shadow)): the machine's entries are the pipeline's on every
corpus, `-g` included, but its sweeps cost 167 cycles a taken node where the
gate asks for 120 (188 with `&&`, `||` and `?:` and 202 with calls, each measured
on its own, and 223 with both, [below](#one-file-a-sweep): their expressions cost
more a node; T's per-unit answers for calls then take 13 to 15 off the three,
[below](#calls-cost-in-t-two-per-unit-answers)), and it now codes every operator
the census takes (calls, `&&`, `||` and `?:` [below](#calls), the increments and
the compound assignments [below](#increments-and-compound-assignments)). T's
direct rules now type nearly every node it sent to the action: a twentieth of
`sqlite3.c`'s typed operators and a quarter of Lua's
([below](#ts-rules-for-the-actions-nodes)). The sweeps are bound by fetching
their code; with T's rare paths out of its loop's line, the three cost 181
cycles a taken node on `sqlite3.c`
([below](#the-sweeps-front-end-ts-rare-paths-out-of-line)), and with C and X
choosing a record's case by one `switch` each, 170
([below](#cs-and-xs-dispatch-one-case-an-operator-one-a-template)); a warm,
repeated run of an expression costs 124
([below](#ts-loop-without-its-rules-and-what-a-first-run-costs)). Running the
sweeps over batches of expressions keeps their code warm but loses more on
`sqlite3.c` than it gains: a sweep's branches are predicted from the sweep
before it on the same expression ([below](#sweeps-by-batches-what-the-predictor-loses)),
and a profile-guided layout gains it 0.6%
([below](#the-sweeps-code-what-a-profile-guided-build-finds)). C and X as one
sweep, exact, takes `sqlite3.c`'s three sweeps from 174 cycles a taken node to
164 ([below](#c-and-x-as-one-sweep)). Where the machine runs is worth more than
its sweeps' design: with other code between two expressions' runs the three
sweeps add 273 cycles a node, with 256 expressions a run 187
([below](#where-the-machine-runs-other-code-between-its-runs)). An expression's
shape as a memo and lanes inside an expression don't pay
([below](#a-memo-of-shapes-and-lanes-what-didnt-pay)). No compile takes it. Its gains are estimates that
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

The machine is the reader's second way to read an expression, beside the
coded walk, and it runs where it runs: after the reader's
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
- **Measured on the machine itself**
  ([below](#a-memo-of-shapes-and-lanes-what-didnt-pay)): an expression's
  records, grouped by level and class, fill 9% of 16 lanes, and 64
  expressions' 45%; T's per-record prologue by lanes saves 17 instructions a
  node and no cycle.

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
`main.c` (24.6%) differ in nothing either. (A function's named result reads
differently in the two since #1375: the lowering builds a returned local where
the hidden pointer points and, after the tasks wrote the entries, makes each
`LOCAL_ADDRESS` of it a `LOCAL` of that pointer, where the tape has the
address. The log gives the comparison each body's named local and hidden
pointer's local, as it gives the labels' entries; before #2882 it didn't, and
192 expressions of `main.c` read as different.) What the machine refuses is its
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
- **A condition's `&&` and `||` (#2883).** The lowering writes a statement's
  condition whose expression, under its `!`s, is an `&&` or `||` as branches to
  the statement's own targets, and a conditional's condition so too, to its arms
  (`check_lower_branch`): no 0 or 1 is made, joined and tested again. The
  `CONDITION` stays the root the census reads; its `&&`, `||` and `!` aren't
  popped. A value's `&&` or `||` whose first operand is such an operator takes
  that operand as branches too, to the value's own label (#2891). The machine
  writes the same entries (#2890), by the lowering's rule read from the records
  (`node_machine_branch_condition`):
  - **C gives such an operator a target and a sense in place of a value's
    context** (`node_machine_classify_branches`), the two things a
    `CHECK_LOWER_BRANCH` task carries: the record whose label the branch goes
    to, or the control's own, and the truth it is taken at. An `&&` where false
    and an `||` where true hand both operands theirs. An `&&` where true and an
    `||` where false send their first operand the other way, to a label of
    their own past their second operand. A `!` swaps the sense. A statement's
    `CONDITION` starts with the control's label and the control's sense, which
    the root's context carries: a do statement's control branches back where
    its condition holds, the others' away where it doesn't.
  - **An operand that is no such operator is a value,** converted to the
    operator's conversion's type, with a segment after its row as a logical
    operator's condition has: its test and the branch
    (`node_machine_branch_hook`). A type a truth's entries don't test (a
    `__float128`'s) refuses the expression, as under a value's `&&`.
  - **X writes no entry for the operator itself.** One with a label of its own
    places it at its row, which is past its second operand's branch. It is a
    labeled record of one label, so the numbering counts labels, two a value's
    and one such an operator's, where it counted labeled records.
  - **The control's label is outside the expression.** On the tape it is one
    value (`BUSTER_NODE_MACHINE_CONTROL_LABEL`), which step 2's splice gives the
    control's. The comparison takes the stream's branch for it where its target
    is the entry of the root's control's target: the inventory's log keeps each
    body's first label (`CheckLowerOwnerPop.labels`), and a control's label is
    its index from there.
  - **At reader time** the sense is the parser's: a do statement's condition is
    a child of its statement's terminator, and any other is its statement's
    own token. The one sweep (`--fused`) writes the same segments from the
    tape's end.

  With it the machine takes what it took before #2889. Machine-taken
  evaluated nodes, of the census-taken (main `178c9e2` and the same with
  #2890; before #2889 they were 98.85%, 94.32% and 91.13%):

  | | `sqlite3.c` | Lua | musl |
  | --- | ---: | ---: | ---: |
  | main: expressions; evaluated nodes | 48,877; 89.76% | 10,128; 86.29% | 20,626; 82.73% |
  | with #2890 | 51,047; 98.84% | 10,433; 94.29% | 21,561; 91.07% |

  On the 9700X (Release, CPU 2, under the lock; main `178c9e2` with the front
  by passes, and the same with #2890;
  [the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/node-machine-condition-branches-2890-2026-11-17)),
  a machine-taken evaluated node of the three sweeps after the pipeline, and a
  record of T, C and X at reader time (medians of three readings):

  | | `sqlite3.c` | Lua | musl |
  | --- | ---: | ---: | ---: |
  | cycles a node: main; with #2890 | 176.0; 183.8 | 154.2; 155.2 | 140.1; 141.6 |
  | instructions a node: main; with #2890 | 603.6; 606.4 | 688.1; 691.2 | 655.8; 661.6 |
  | at reader time, TSC ticks a record: main; with #2890 | 156.8; 168.6 | 180.8; 189.0 | 275.6; 290.5 |

  The nodes it takes back cost more than the others. `sqlite3.c`'s 23,251 take
  6.07 M cycles, 261 each, and write 1.89 entries each where the others write
  1.24: a test and a branch an operand. No compile runs the machine, and
  `-c sqlite3.c` takes the time it took (+0.14% and -0.00% in two sets of 21
  runs a build; the base against itself -0.04% and +0.09%).

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
  - a library builtin's call that the catalogue folds or the lowering computes
    in its place (`check_program_call_library`: `strcmp`'s fold, `fmax`, and
    `memcpy` of a small constant size, #1373), but not one that stays a plain
    call (`memcpy` of a size that is no constant);
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

### Calls' cost in T: two per-unit answers

The two costs of T's own that calls brought are now answered from per-unit state
([the record](https://github.com/buster14a/buster-performance/tree/1e57bdc259d8b9ddcb6b791440d719e7f722eed4/docs/measurements/node-machine-call-cost-2524-2026-11-12)):

- **The callee's pointer type.** `NodeMachine.function_pointers` (now
  `pointers`, for every type: [below](#ts-rules-for-the-actions-nodes)) holds a
  function type's pointer type by type index, filled at the type's first
  designator by `type_pointer`, its twin. A callee's conversion, and a function
  argument's, is then one load.
- **The arguments' conversions.** `NodeMachine.argument_memo` is 1,024
  direct-mapped entries of 16 bytes (`node_machine_argument_memo`). Each holds
  `node_machine_argument`'s answer, whether it took the argument and the
  conversion, by three keys: the argument record's type, its standing (an
  lvalue, an integer constant, or neither) and the parameter's type.
  - **Only an argument the rule reads nothing else of is a key:** no constant
    but an address or an integer; none of a null pointer's, a bit-field's, a
    register array's or a volatile part's flags; no enumeration's lvalue, whose
    completeness at the node the rule asks.
  - **A hit inherits the record's value as it is.** The conversion changes
    nothing that `check_action_inherit` reads for an integer, floating, pointer
    or enumeration type, nor for an array or a function decayed to a pointer.
  - **An integer constant's entry answers only where every integer constant of
    its type converts alike** (`node_machine_memo_integers`). For a pointer
    parameter it doesn't: a null pointer constant is taken and another integer
    isn't.
  - **The rule fills a miss and takes every other argument,** and `--verify`
    checks each call it types against the action either way.

The shadow forgets both before each timed sweep set, so each pass pays their
fills as one compile of the unit would.

On the 9700X, against main `f006224e`, with the updates (medians of ten
alternating runs, `--passes 11`; the comparison has 0 differences on
`sqlite3.c`, Lua and musl, `-g` too, with main's counts):

| T a taken node | main | with the answers |
| --- | ---: | ---: |
| `sqlite3.c`: cycles, instructions | 121.8, 453.3 | 109.5, 410.4 |
| `sqlite3.c`, warm (`--repeat 2`, both runs) | 220.9, 904.2 | 198.4, 814.3 |
| Lua: cycles, instructions | 126.2, 601.0 | 117.8, 551.5 |

- **All three sweeps on `sqlite3.c`:** 220.0 cycles a node become 206.7. C and X
  run the same instructions as before, so the change is T's. Against main
  `afcb185e`, before the updates, it was 215.1 to 200.0.
- **The profiles:** the interning falls from 2.4% to 1.1% of the run.
  `check_action_convert_plain` falls from 3.2% to 2.6%; what remains of it is
  mostly the stores' conversions, whose converted values are their records'.
- **The memo on `sqlite3.c`:** 1,900 entries filled and 690 arguments left to
  the rule, against the 33,565 arguments of its taken calls.

Integer constants were most of what the first version left to the rule. Sharing
their type's entry ran fewer instructions but more cycles on Lua; as keys of
their own, a hit tests one key and one bit, and that version is the one kept.
`-c sqlite3.c` is unchanged: 124.1 ms against 123.8 ms, with the same
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
9700X
([record](https://github.com/buster14a/buster-performance/tree/fcee0f08ba82766dea4b9f1a94192645fd777c24/docs/measurements/node-machine-x-state-test-2524-2026-11-12)),
per taken node:

| X's | `sqlite3.c` | Lua 5.4.8 |
| --- | ---: | ---: |
| instructions on main `f006224e`: main → one test → and the increment | 117.3 → 115.6 → **110.6** | 115.1 → 113.5 → **108.2** |
| cycles on main `f006224e`: main → one test and the increment | 40.3 → **37.9** | 28.6 → **25.7** |
| instructions on main `c3eb40fc`, with #2580: main → both | 113.9 → **103.9** | 111.9 → **101.5** |
| cycles on main `c3eb40fc`: main → both | 43.2 → 42.9 | 30.0 → 30.4 |

- **The one test alone saves 1.7 instructions.** Clang spends most of it on
  register copies and stack accesses elsewhere in X, a count on a baseline
  build shows.
- **The increment saves 5.0 more.** The record's end is one test of the
  refusal, and the loop steps two of the records' column pointers with the
  record.
- **With #2580's records, X's cycles no longer follow its instructions.** Its
  branch misses are the same with and without the change (45.2 M and 45.4 M
  in 40 passes on `sqlite3.c`), and its hottest samples follow its
  data-dependent branches: the record's state, its template and whether it
  has a conversion. The change removes only branches X predicts well. Those
  three are the next thing to take out of X's loop.

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

### T's rules for the action's nodes

T typed 13,987 of `sqlite3.c`'s evaluated nodes and 9,370 of Lua's by the
action, on whole values. They were a few shapes, counted on main `df85213e` by a
diagnostic in T's action branch (#2619's record):

| what went to the action | `sqlite3.c` | Lua 5.4.8 |
| --- | ---: | ---: |
| integer constant folds (`-1`, `A \| B`, `1 << 4`, `~0`) | 3,417 | 4,947 |
| casts | 3,325 | 2,800 |
| a static object's member (`.` of an address constant) | 2,144 | 213 |
| stores (of a null pointer constant, of another pointee's pointer) | 1,276 | 417 |
| a pointer and a null pointer constant (`p == 0`, `p == NULL`) | 1,266 | 187 |
| `sizeof`, `_Alignof` | 928 | 373 |
| `?:` | 641 | 376 |
| a static array's subscripts | 471 | 61 |
| the comma | 361 | 576 |
| other | 524 | 370 |

(The diagnostic counts a few nodes twice, which the shadow's own report
doesn't.) Each now has a direct rule in T (`c_check_node_machine_type.c`), its
action's own steps behind a guard, with the action as its twin under
`--verify`:

- **The folds** (`node_machine_fold`) take an operator's row for its
  conversions and result type, and `check_action_fold_integer`'s steps for the
  value, on rvalue integer constants of builtin types up to 64 bits. A fold
  that faults stays the action's.
- **The casts** (`node_machine_cast`), to void, an integer or a pointer, of an
  integer or a pointer: on the columns where the operand has no constant, and
  made whole for `check_action_conversion` where it has one.
- **A member and a subscript of an address constant** keep it: the base's
  offset plus the member's bytes, or the index's elements.
- **A pointer's equality with a null pointer constant**
  (`node_machine_null_compare`), **`sizeof` and `_Alignof`**
  (`node_machine_query`, of a type name too), **the comma**
  (`node_machine_comma`) and **`?:`** (`node_machine_conditional`, arms of
  builtin arithmetic types or a void one).
- **Stores** take a null pointer constant and a pointer
  `node_machine_pointer_assignable` takes, converted whole.

Two more changes came with them:

- **`NodeMachine.pointers` and `const_pointers`** (`function_pointers`
  before) hold any type's pointer type, unqualified and const. An array's decay
  and an address are now one load, as a function designator's conversion was.
  The const types' column comes after the machine's other arrays, which stay
  where the function types' column left them.
- **A call's value** goes onto the columns without being made whole.

**A latent bug, found on the way.** The store rule converted a plain `char`
constant by the bake's signedness, which knows nothing of `-funsigned-char`:
`y = (char)200` gave −56 where the action gives 200. The corpora don't use the
option, so their runs didn't show it. A char's signedness is now the unit's
(`node_machine_integer_sign`), and the shadow's fixture runs once more with
`-funsigned-char`.

The action now types 716 of `sqlite3.c`'s nodes, 626 of Lua's and 887 of
musl's. `--verify` shows no difference on the three, with `-g` too, nor on
`main.c`.

On the 9700X
([the record](https://github.com/buster14a/buster-performance/tree/0dcf998f0cf53a7c54fd7e0c61e12ffd1fabcf66/docs/measurements/node-machine-action-rules-2524-2026-11-13)),
against main `df85213e` (CPU 2, under the lock, `--passes 11`, medians of runs
taken in turn), in cycles and instructions a taken node. `v1` is the folds,
casts, members, null comparisons, `sizeof`, comma, subscripts and stores; `v3`
adds `?:`, the pointer column and a call's value; the pull request's code is
`v5`, below:

| | T | T, C and X | the three, two runs (`--repeat 2`) |
| --- | ---: | ---: | ---: |
| `sqlite3.c`, main | 107.2, 391.2 | 203.1, 694.8 | 351.6 |
| `sqlite3.c`, `v1` | 102.0, 376.4 | 198.9, 680.1 | 346.3 |
| `sqlite3.c`, `v3` | 99.7, 365.3 | 198.8, 668.9 | 347.1 |
| `sqlite3.c`, `v5` | **102.0, 369.2** | **196.5, 672.8** | **340.8** |
| Lua, main | 116.9, 555.1 | 191.4, 855.6 | 363.2 |
| Lua, `v5` | **96.3, 464.4** | **170.1, 765.0** | **318.8** |

- **The rules take 26 of T's instructions a node on `sqlite3.c` and 94 on
  Lua.** A quarter of Lua's typed operators went to the action (its constant
  shifts, `+`, `~` and casts), against a twentieth of `sqlite3.c`'s. All three
  sweeps are 3.2% cheaper on `sqlite3.c` and 11.1% on Lua.
- **X's cycles followed T's code, not its own.** At `v3` X ran 43.0 cycles a
  node on `sqlite3.c` against main's 39.5, with the same 103.9 instructions.
  The pointer column's size wasn't the cause: `v4`, which put the machine's
  other arrays back where main had them, measured as `v3`. The new rules had
  grown T past clang's threshold, and it had taken the member, truth and logic
  rules out of T's loop (the member rule alone was 6% of the profile). `v5`
  inlines them again, as main had them: T runs 4 instructions more than at
  `v3`, X is back at 39.4 cycles, and the three sweeps cost the least of every
  version. **A change to T should compare its executable's
  `nm | grep node_machine_` with main's**, as #2577 found for C and X.
- **What T has left is its inputs.** `node_machine_action` and the
  `check_action_*` functions fall from 9.5% of the profile to 1.9%, and the
  type table's interning from 1.2% to 0.5%. T's heaviest lines are the leaves'
  records, the graph's op, context and operand columns, and the type words.
  Step 2's parser tree replaces them.
- **`-c sqlite3.c` is unchanged:** 123.6 ms against main's 123.9, with the
  same instructions.

Step 1's gate asks for 120 cycles a node for the three sweeps; this is 196.5.
What remains of T's own work is the action's last nodes (`?:` of other arms,
pointers' comparisons and differences, floating casts, unary `+` of a
variable) and a call's per-call type queries; most of its cycles are its
inputs.

### T's loop: its column stores and its operands, and two levers that didn't pay

In the profile of T's loop by retired instructions and by cycles, each line's
shares match (which doesn't make it instruction-bound: stalls spread over the
loop's lines match too, [below](#the-sweeps-front-end-ts-rare-paths-out-of-line)).
At main `26f0bb0e` its own bookkeeping was a large part of them:
- the node index, recomputed for each operand (`node_machine_node`, 6% of T's
  instructions);
- the operands' search (`node_machine_operand`, 6%);
- the column stores, through `machine->columns`.

Under `-fno-strict-aliasing` every store to a column could change the machine's
fields, so each was loaded again after it. The change that merged takes two of
these out ([the record](https://github.com/buster14a/buster-performance/tree/50a6e350e97b7cab786116fea110c2bd9e1de3f1/docs/measurements/node-machine-t-loop-2524-2026-11-13)):

- **Each of T's functions takes the column base into a local first**, where it
  stored five to nine columns through `machine->columns` in a row.
- **T's loop finds a record's operands from the node it found**
  (`node_machine_record_of`), where `node_machine_operand` computed the node
  again for each.

On the 9700X, against main `26f0bb0e` (CPU 2, under the lock, `--passes 11`,
medians of runs taken in turn), in cycles and instructions a taken node:

| | T | T, C and X | the three, two runs |
| --- | ---: | ---: | ---: |
| `sqlite3.c`, main | 101.7, 369.2 | 196.7, 672.8 | 339.2 |
| `sqlite3.c`, the operands from the node alone (v8a) | 100.3, 360.1 | 195.6, 663.7 | 337.3 |
| `sqlite3.c`, the local column base alone (v8b) | 100.5, 358.3 | 193.1, 661.9 | 333.5 |
| `sqlite3.c`, both (v11) | **99.4, 345.9** | **193.0, 649.6** | **331.4** |
| Lua, main | 96.3, 464.4 | 171.1, 765.0 | 319.7 |
| Lua, both (v11) | **92.3, 437.7** | **164.9, 738.3** | **310.1** |

Two levers that looked as good in the profile didn't pay, and aren't in it:

- **All of the loop's inputs in locals** (v6): the graph's columns, the type
  words, the node, `first`, `root` and `count` across the loop. T ran 19
  instructions a node fewer but 7.6 cycles more on `sqlite3.c`. Its
  instructions without a source line, the spills' and their reloads, went from
  7% of T's to 23%: the loop inlines most rules, and fifteen live pointers
  overflowed the registers. #2580's record found the same of an earlier
  version.
- **A memo of the callee's type's answers** (v9), and of a direct callee's
  returning twice. In the profile they were 4% of the loop's instructions and
  6.8% of its cycles. The memo, 1,024 entries direct-mapped by the callee
  record's type, filled 2,161 times for `sqlite3.c`'s 13,478 calls: at 4,096
  entries it still filled 1,632, the unit's distinct callee types. T ran 1.6
  cycles slower with it, and the two changes above measured the same with it
  as without. Its twin under `--verify` counted 84 differences on the fixture
  when either answer was corrupted on its fill: the check works, the memo just
  doesn't pay.

### The sweeps' front end: T's rare paths out of line

The sweeps are bound by fetching their code, not by their instructions. A
local patch to `node_shadow_time` times one sweep set alone (T; T and C; all
three), and `perf stat` counted each set's 40 passes less a run of none, per
taken node, at main `b7a95eed` on the 9700X, three times. The five events
share the core's counters by turns, so a figure moves by up to a tenth between
runs; these are the medians:

| `sqlite3.c` | cycles | instructions | dispatch slots the front end left empty | L1i misses | mispredicted branches |
| --- | ---: | ---: | ---: | ---: | ---: |
| T | 100.5 | 324.7 | 29% | 7.2 | 0.8 |
| C | 53.4 | 191.9 | 52% | 5.9 | 0.4 |
| X | 37.4 | 105.3 | 59% | 5.1 | 0.4 |
| all three | 190.4 | 621.5 | 41% | 18.4 | 1.6 |

Lua's sets look the same: 21%, 53% and 56% of their slots empty, and 20.5
misses a node. Over a whole run of 40 passes at `e412d48e`, `perf stat`'s
pipeline metrics put 40% of the dispatch slots on the front end (22% waiting
on its latency, 18% on its bandwidth), 10% on mispredicted branches and 19% on
the back end; 35% retire. Half of T's instruction-cache misses fell on 12 of
the 141 64-byte lines of its code that missed, those of its loop's common
path: its leaves, its node's op and context, its operands and the type words.
The loop had its rare paths
inlined among them: the action, the twin's comparison, a leaf's whole value
and the refusals.

**The bound.** A build with profile-guided optimization trained on the shadow
of `sqlite3.c` alone (`BUSTER_PGO=1`) took the three sweeps from 190.4 to 170.8
cycles a node on `sqlite3.c`, and from 161.6 to 135.4 on Lua, which it wasn't
trained on (one run each). It cut the instruction-cache misses from 18.4 to
7.5 a node by inlining every sweep into `node_shadow_sweep` and laying the hot
blocks out together. That is a bound, not the change: it lays the whole
compiler out by one input's profile.

**The change.** T's rare paths are out of line and marked cold, and its tests
for them are expected false, so that the loop's common blocks lie together:
- the nodes T's rules leave to the action (`node_machine_type_action`), 716 of
  `sqlite3.c`'s 258,466 taken nodes;
- `--verify`'s twin of a node a row or a rule typed
  (`node_machine_same_as_action`);
- a leaf with a payload row, an overload set or another category, made whole
  first (`node_machine_leaf_whole`);
- T's refusals, which reach none of `sqlite3.c`'s expressions, one of Lua's
  and 39 of musl's 23,036.

C's label numbering, which runs for one taken expression in 15 to 19 (2,824 of
`sqlite3.c`'s 51,289), is kept out of line too (`node_machine_number_labels`). `nm` shows nothing else moved:
`node_machine_type` goes from 13,880 bytes to 11,410 and
`node_machine_contexts` from 8,631 to 7,484. The other functions are the same.

On the 9700X, against main `b7a95eed` (CPU 2, under the lock, `--passes 11`,
medians of six runs each taken in turn), in cycles and instructions a taken
node ([the record](https://github.com/buster14a/buster-performance/tree/c5d45b327d90f5187af4e74df00dc227f26e57b4/docs/measurements/node-machine-front-end-2524-2026-11-13)):

| | T | T, C and X | the three, two runs |
| --- | ---: | ---: | ---: |
| `sqlite3.c`, main | 100.5, 345.9 | 194.6, 649.6 | 340.3 |
| `sqlite3.c`, the change | **87.6, 344.1** | **180.9, 647.7** | **313.9** |
| Lua, main | 93.7, 437.7 | 168.6, 738.3 | 318.6 |
| Lua, the change | **80.9, 436.7** | **154.1, 737.2** | **292.6** |

- **T is 13 cycles a node faster on the same instructions**, 13% on both
  inputs, and the three sweeps 7.0% on `sqlite3.c` and 8.6% on Lua. In the
  counts above (two runs), T's misses fall from 7.2 a node to 6.3, and its
  empty slots from 232 to 213.
- **The placement doesn't make it.** With the checker unit's code shifted by 0,
  1,024, 2,112 and 3,136 bytes (`./build.sh shifts`), T ran 88.3 to 88.8
  cycles on `sqlite3.c` with the change against 98.9 to 100.4 without, and 80.5
  to 81.8 on Lua against 91.7 to 92.8.
- **C and X are unchanged.** Half and three fifths of their slots are still
  empty. The profile's layout took C from 53 to 47 cycles a node on
  `sqlite3.c`, but keeping its label numbering out of line didn't. Both choose
  each record's case by branches on its facts, and those cases are what they
  fetch: fewer, denser cases, or the row on a key of a few bytes that
  [the baked tables](#the-baked-tables-measured) describe, are the levers left
  to try.
- **The comparison:** `--verify` on `sqlite3.c`, Lua and musl, with and
  without `-g`, has no difference, and main's counts. `-c sqlite3.c` doesn't
  run the shadow and is unchanged: 124.6 ms against main's 124.8, with the
  same instructions.

The three now cost 180.9 cycles a node on `sqlite3.c`, against the gate's 120.

### C's and X's dispatch: one case an operator, one a template

C chose each record's template through one chain of tests, the same for every
operator, and X wrote each record's row through another. A record reached its
own case only after failing the tests of the cases before it. C also called an
out-of-line function for each operand it evaluated, and stored the record it
was classifying into the machine first. After each such call and store, the
build's `-fno-strict-aliasing` made C load the columns' pointer again.
The change gives each sweep one case an operator or a template, which runs only
its own tests:

- **C's operands, in one place.** A common case no longer evaluates its
  operands itself. It names at most two (`NodeMachineUse`), and C's loop
  evaluates them inline, below the record's call depth, which it reads once
  (`node_machine_evaluate_at`). Calls, `&&`, `||`, `?:` and updates still
  evaluate their own operands, out of line (`node_machine_evaluate`, which now
  takes the consumer). The machine no longer keeps the record C is classifying
  (`classified`), and C stores the record it stopped at once, after its loop.
- **C's classification, one case an operator.** The refusals every operator
  shares come first, then the folded constant, then a `switch` on the operator.
  Each case holds its own tests: a cast's class, a member's record, the
  pointer arithmetic's operands. The order of `check_lower_template_classify`'s
  rules is kept where it decides: an aggregate or a non-scalar value refuses
  every operator that rule's chain reached after the leaves, `&&`, `||`, `?:`,
  the selections, `&`, `*`, `.`, `->` and pointer arithmetic, except a cast, a
  conversion and a unary plus. A division's and a remainder's power-of-two
  test (`node_machine_power`) is asked for those two alone: since #2679 clang
  had left it out of line, a call for every binary operator. The update's case
  is out of line too (`node_machine_classify_update`).
- **X's rows, one case a template.** `node_machine_own` is a `switch` on the
  template, and each case reads only the fields it writes. A `PASS`, a `VOID`
  and a discarded constant write nothing. Before, they fell through every test.
- **A call's row from the calls written.** X stored, at every record, the
  evaluated calls before it (`calls_at`), for the calls' rows alone. X now
  keeps the calls' records in the order it writes them (`calls_written`,
  increasing). A call's row counts those below its subtree's least record,
  from the last.

What C classifies, by operator and template, over the first 250,000 records it
classified on `sqlite3.c` (a local patch to C's loop, in the record):

| operator, template | share |
| --- | ---: |
| a leaf: a local's read | 24.6% |
| a cast: its operand alone (`PASS`) | 15.3% |
| a leaf: a constant | 14.3% |
| `->` and `.` | 11.5% |
| a call | 5.2% |
| a condition's test (`TRUTH`) | 4.7% |
| a store to a named object; through an address | 3.7%; 2.6% |
| the rest | 18.1% |

On the 9700X, against main `ca1ede6a` (CPU 2, under the lock, `--passes 11`,
each run its own hold, in turn; [the record](https://github.com/buster14a/buster-performance/tree/569d3e36c443c992b6a899fb913c4519e4888e5f/docs/measurements/node-machine-dispatch-2524-2026-11-14)),
in cycles and instructions a taken node, medians of six runs:

| | T | C | X | T, C and X | the three, two runs |
| --- | ---: | ---: | ---: | ---: | ---: |
| `sqlite3.c`, main | 90.2, 350.9 | 57.6, 209.9 | 35.9, 104.0 | 183.8, 664.8 | 320.4 |
| `sqlite3.c`, the change | 90.4, 350.9 | **44.7, 169.3** | 35.1, 78.6 | **170.2, 598.8** | **288.2** |
| Lua, main | 83.2, 444.5 | 49.6, 208.9 | 24.2, 101.4 | 157.1, 754.8 | 297.2 |
| Lua, the change | 83.2, 444.5 | **37.1, 171.2** | 24.6, 76.1 | **144.9, 691.8** | **271.3** |

- **C** runs 22% fewer cycles on `sqlite3.c` and 25% fewer on Lua, and 41 and
  38 fewer instructions a node. Most of it is one case an operator: with
  the operands in one place alone, an intermediate version, C ran 53.6 cycles
  on `sqlite3.c` against main's 56.9 in the same rounds, on main's
  instructions. `nm`: `node_machine_contexts` goes from 7,681 bytes to 5,945,
  and `node_machine_classify_update` (213) is new. Nothing else changes size.
- **X** runs a quarter fewer instructions, but its cycles barely move: it
  retires about 2.2 instructions a cycle. Its tape's count, which each entry
  reads and writes in the machine, isn't what holds it: kept in a local, it
  didn't move X's cycles either (below). `node_machine_expand` goes from 5,879
  bytes to 5,714.
- **The three sweeps** cost 7.4% fewer cycles on `sqlite3.c` and 7.8% fewer on
  Lua, and 10.0% and 8.7% fewer in two runs, where the second finds its inputs
  in the caches.
- **Three levers didn't pay.** Each is a patch in the record:
  - `restrict` on C's machine took the column reloads out of main's C (16
    instructions and 6 cycles a node). On top of the operands in one place,
    which removed those reloads' stores, it gained nothing more.
  - X's tape in a local, passed by address to its updates' rows (out of line),
    added 6.6 instructions a node: the updates' copies kept it on the stack.
    Passed to them by value, it added 6.1 and left X's cycles as they were.
  - A path for a leaf's read ahead of C's shared refusals added 14
    instructions a node.
- **The comparison:** `--verify` on `sqlite3.c`, Lua and musl, with and
  without `-g`, has no difference, and the same counts of taken expressions
  and nodes as main. `test-checker` passes. `-c sqlite3.c` doesn't run the
  shadow and is unchanged: 122.81 ms against main's 122.90, user cycles
  -0.16%, 229 fewer instructions of 1.36 G.

The three now cost 170.2 cycles a node on `sqlite3.c`, against the gate's 120.
T is about half of it, C a quarter and X a fifth. C and X stay one record at a
time: C follows the tree down ([data-parallel first](#data-parallel-first-what-is-vector-and-what-is-ordered)),
and X's rows written by three vector stores lost to its branches when they
were measured ([the baked tables](#the-baked-tables-measured)).

### T's loop without its rules, and what a first run costs

The two levers that cut C in #2727, applied to T (`c_check_node_machine_type.c`):

- **T's rules, one case an operator, out of line.** `node_machine_rule` used to
  reach a node's rule through a chain of tests on the operator. It is now a
  `switch`, kept out of T's loop (`noinline`), with the rules it calls inlined
  into it. T's loop, the leaves' and the rows', is 4.4 KB where it was 11.8
  KB. The rows still come first, and their `+` and `-` go on to the pointer
  rule as before.
- **T's range from its arguments.** T's loop reads the expression's first
  node, root and count from its own arguments, not from the machine after each
  of its column stores. It keeps its count of typed nodes and the record it
  stopped at in locals, and stores them once after the loop.

T runs 14 fewer instructions a node (350.9 to 336.8 on `sqlite3.c`, 444.5 to
427.4 on Lua), on the same machine results. Its cycles move less than a copy of
the executable can move them:

- **A copy moves the sweeps.** The exploration's executable and the commit's
  have byte-identical `.text`, every symbol at the same address, yet ran the
  three sweeps at 169.4 and 172.9 cycles a node on `sqlite3.c`. Fresh copies of
  the commit's file ran 169.9, 172.8 and 171.3, one run each, most of the spread
  in X. Main's four copies agreed within half a cycle (173.2 to 173.7). A
  difference of a few cycles needs several fresh copies of each side.
- **Over four copies of each** (the commit's build, six runs, and three fresh
  copies, one run each; main's the same), against main `c471f32`, medians in
  cycles a taken node:

  | | T | all three |
  | --- | ---: | ---: |
  | `sqlite3.c`, main | 90.6 | 173.3 |
  | `sqlite3.c`, the change | 90.4 | **172.1** |
  | Lua, main | 83.9 | 148.4 |
  | Lua, the change | **82.7** | **146.2** |

  The three sweeps are 1.2 and 2.2 cycles a node faster, and T on Lua 1.2
  faster at every copy. Most of the rest is C, whose code is unchanged: with
  T's loop smaller, C finds more of its code in the caches (C 47.2 to 45.4 on
  `sqlite3.c` in the six runs). At four placements of the checker unit's code
  (`./build.sh shifts`), the exploration's build ran the three 4.0 to 6.4
  cycles faster on `sqlite3.c` and 1.0 to 4.5 on Lua.
- **Neither lever pays alone:**
  - The `switch` without the range in locals, in the same out-of-line shape,
    ran T's instructions unchanged and T 1.6 cycles slower.
  - The range in locals on main's shape cut 6.5 instructions and made T 13
    cycles slower, with no function's inlining changed: T's code is sensitive
    to its layout.
  Each is a patch in [the record](https://github.com/buster14a/buster-performance/tree/f1fdc7e99a21f93ea05209beb96886010ea19b99/docs/measurements/node-machine-t-dispatch-2524-2026-11-14).
- **The comparison:** `--verify` on `sqlite3.c`, Lua and musl, with and
  without `-g`, has no difference, and main's reports. `test-checker` passes.
  `-c sqlite3.c` doesn't run the shadow and is unchanged: 122.68 ms against
  main's 122.66, the same instructions within 300 of 1.36 G.

**What a first run costs.** Each sweep set timed alone (the front-end record's
`only.patch`, `perf stat`, 40 passes less a run of none) on `sqlite3.c` at main
`c471f32`, a taken node:

| | cycles | instructions | dispatch slots the front end left empty | L1i misses | op-cache misses |
| --- | ---: | ---: | ---: | ---: | ---: |
| T | 88.5 | 323.9 | 30% | 5.2 | 8.8 |
| C | 47.6 | 164.9 | 47% | 5.1 | 7.3 |
| X | 32.1 | 81.2 | 68% | 4.0 | 6.5 |

X is the most front-end bound of the three: two thirds of its slots are
empty. `--repeat 2` runs each expression's three sweeps twice in a row. The
second run costs **123.5 cycles a node** against the first's 168.2, on the same
570 instructions:
- its front end leaves 267 slots empty against 570;
- it misses the instruction cache 6.4 times against 14.2, and the op cache 9.1
  times against 22.6;
- it mispredicts 0.73 branches against 1.56.

T alone repeated costs 75.6 against 88.5. So about 45 of a first run's 168
cycles are the expression's own paths through the three sweeps' code, and its
inputs, coming back into the caches: the operators differ from one expression
to the next, and the sweeps' code together is larger than the 32 KB
instruction cache. The gate's 120 is about what a warm run costs now. The lever
this points to is the sweeps' code staying warm: each sweep run over a batch of
expressions before the next sweep. The next section measures it.

### Sweeps by batches: what the predictor loses

Each sweep run over a batch of expressions, a statement list's or a body's,
before the next sweep starts, so that its code stays in the caches across the
batch. Built and measured, not merged; the code is two patches in
[the record](https://github.com/buster14a/buster-performance/tree/c2b332bc83caae6572c66947379a5c7686ee74be/docs/measurements/node-machine-batches-2524-2026-11-14).

- **The machine by batches.** An expression's records are a range of the
  batch's, from a base: T types a batch's expressions one after another in the
  columns, and C and X take each one afterwards. The wide values and the call
  arguments continue from the batch's earlier expressions', since C and X read
  them after T has gone past. T's loop keeps one more value live, the base, and
  spills: the shadow at batch 1 costs 2 to 5 cycles a node more than main.
- **The check.** Under `--verify`, every taken expression's result by batches
  (its refusal, its tape, its labels' entries and its calls' records) is
  compared with its result alone. Starting T's argument list at every
  expression made 9,857 of `sqlite3.c`'s expressions differ; the code as
  measured has none, on `sqlite3.c`, Lua and musl, with and without `-g`.

**All three sweeps by batches.** Cycles a taken node, medians of three rounds
(each within 2.4 of its median):

| batch | `sqlite3.c` | Lua |
| ---: | ---: | ---: |
| main | 169.4 | 146.1 |
| 1 | 177.6 | 154.3 |
| 4 | 208.0 | 155.5 |
| 16 | 195.1 | 145.2 |
| 256 | 186.5 | 136.4 |
| 1,024 | 186.7 | 135.8 |

The code does stay warm: at batches of 256, the three sweeps miss the
instruction cache 11.4 times a node on `sqlite3.c` against 18.5 at batch 1, and
the op cache 16.5 times against 26.1. But their mispredictions rise from 1.57 to
2.19 a node, nearly all in C and X (each from about 0.4 to 0.7), and the batch's
records outgrow the first-level data cache (fills from L2 2.3 to 6.0 a node).

**Which handoff matters.** Batching only some of the sweeps (medians of two
rounds, batch 256):

| | `sqlite3.c` C | X | all three | Lua all three |
| --- | ---: | ---: | ---: | ---: |
| all three by batches | 51.7 | 43.8 | 187.9 | 137.5 |
| T by batches, then C and X an expression at a time | 51.6 | **31.5** | 175.0 | 136.9 |
| T and C an expression at a time, then X by batches | **47.2** | 43.8 | 183.3 | 144.8 |

A sweep keeps its cost when it runs right after the sweep before it went over
the same expression: C right after T costs 47 to 51 a node on `sqlite3.c`, C
after other expressions' C 51 to 63; X right after C 31 to 37, X after other
expressions' X 44 to 52. Each sweep's branches follow the expression's
operators, which the sweep before it has just walked, and the predictor's
history keeps them. That history, not only the code, is much of what a warm
second run gets back.

**T by batches, C and X an expression at a time,** the best of the shapes, against
main, medians of three rounds (two for musl):

| | main | batch 1 | batch 256 | batch 1,024 |
| --- | ---: | ---: | ---: | ---: |
| `sqlite3.c` | 170.8 | 173.0 | 176.5 | 176.3 |
| Lua | 146.9 | 149.8 | 137.5 | 136.8 |
| musl | 131.2 | 136.6 | 125.7 | 125.5 |

Lua and musl gain (6.4% and 4.2% at 256), `sqlite3.c`, the gate's file, loses
3.3%: C costs it 6.7 cycles a node more without T right before it, where C on
Lua and musl gains 8.7 and 9.2 from its code staying warm. Weighted by each
corpus's taken nodes the three come out about even (+0.75 cycles a node), and
the batch-1 path's own cost stays. So the sweeps stay one expression at a time.
What it leaves:

- **Keeping the sweeps' handoffs.** A first run's cost is partly the code and
  partly the branches: a smaller sweep is the code's lever, since batching
  trades one for the other.
- **The corpora differ.** `sqlite3.c`'s C keeps more from T's history than
  Lua's and musl's does; a lever measured on one of them alone can mislead.

### The sweeps' code: what a profile-guided build finds

The sweeps' hot code is larger than the 32 KB instruction cache: at main
`e62535f`, 99% of their cycles on `sqlite3.c` fall on 605 64-byte lines (38.7
KB), and their instruction-cache and op-cache misses fall across the same lines,
not on a few in conflict. T's rules (`node_machine_rule`, 26.4 KB) touch 292 of
their lines, 90% of their cycles on 137; T's loop, C and X touch 60, 85 and 61.

A profile-guided build (`BUSTER_PGO=1`, trained on the shadow of `sqlite3.c`
alone) bounds what a layout gives, in cycles a taken node, medians of three
rounds (two for musl), each within 1.4 of its median:

| | main | profile-guided |
| --- | ---: | ---: |
| `sqlite3.c` | 175.3 | 174.3 |
| Lua | 143.7 | 137.4 |
| musl | 128.5 | 120.8 |

On `sqlite3.c`, the file it was trained on, it gains 0.6%: it inlines the three
sweeps into the shadow's timing loop and grows T's rules to 106.8 KB, so the hot
code gets larger (99% of the cycles on 669 lines) and the misses don't fall.
`minsize` on T's rules shrinks them only to 23.4 KB. So a layout has no measured
headroom on the gate's file; Lua and musl gain from it, as they did from
batching ([above](#sweeps-by-batches-what-the-predictor-loses)). The record is
[`node-machine-layout-2524-2026-11-15`](https://github.com/buster14a/buster-performance/tree/8c00e4f5572a3e29e12c96b32c05ba23c4a2993d/docs/measurements/node-machine-layout-2524-2026-11-15).

### A fused sweep: what it would settle later

Neither batching nor a layout leaves headroom on `sqlite3.c`: what stands
between the sweeps and the gate is instructions, about 585 a taken node where
120 cycles at today's rate are about 440. About 170 of the 585 are C re-reading
the records T has just written, and C costs least right after T on the same
expression ([above](#sweeps-by-batches-what-the-predictor-loses)). A
[recovered design exploration](https://github.com/buster14a/buster/issues/2524#issuecomment-6892875893)
fuses C into T: as T types a record, it classifies the record's operands, as C
does, since the record's operator decides their contexts and targets. What T's
visit can't know waits for a later step:

- a context that is the consumer's own (a `SELECT`'s operand and a comma's second
  operand), which its consumer's consumer gives;
- a record classified at its consumer's visit whose consumer turns out
  unevaluated (under a folded constant, `sizeof`, a decided condition);
- a call's depth among the evaluated calls, which numbers its row and is its
  ancestors'.

The shadow's census (`node-shadow: fusion`, `node_shadow_fusion`) counts them
over the taken expressions, at main `d356f725`:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| evaluated records | 257,263 | 57,779 | 110,119 |
| inherited contexts | 340 (0.13%) | 242 (0.42%) | 194 (0.18%) |
| of them not a value | 0 | 0 | 0 |
| unevaluated records | 34,829 | 16,384 | 20,708 |
| of them under an unevaluated consumer | 2,893 (1.1% of the evaluated) | 5,780 (10.0%) | 3,057 (2.8%) |
| evaluated calls, below another call | 13,351, 325 | 4,049, 176 | 5,357, 318 |
| expressions with any of them | 1,554 of 51,135 (3.0%) | 1,281 of 10,444 (12.3%) | 1,550 of 21,604 (7.2%) |

An inherited context was a plain value every time, as T's visit would assume
it. A fused T would settle later 3.0% of `sqlite3.c`'s expressions, and its
wasted classifications would be 1.1% of its records. The census shapes the
design but doesn't measure it: what a fused T's code size and its predictions
cost is the prototype's measurement, behind the shadow's `--verify`. The record is
[`node-machine-fusion-census-2524-2026-11-15`](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/node-machine-fusion-census-2524-2026-11-15).

### C and X as one sweep

The census above is of a T that classifies as it types. The part of that fusion
with nothing to settle later is C with X: a record's row waits only for its
context and target, which C gives it on its way down, so one sweep can classify
a record and write its row at once. `node_machine_lower`
(`c_check_node_machine_lower.c`, [the guide](commands/lowering.md#the-shadow-step-1))
is that sweep:

- **Down from the root, as C goes.** Each evaluated record is classified by C's
  own functions, its operands get their contexts and targets, and its row is
  written before the sweep steps to the record before it.
- **The tape from its end.** The records come in reverse postorder, so the
  entries are written back from the columns' ends: within a record, the segment
  after its row first, then its conversion's entries, then its own, each row
  from its last entry. The tape starts at `NodeMachine.entry_first`.
- **What X numbers by the order it writes in** is numbered after the loop, for
  the expressions that have any (`node_machine_lower_number`): the labels by C's
  ranks, the calls' rows by X's rule over the calls in X's order, a postfix
  update's temporaries in X's order.
- **A refusal is the two sweeps':** where a row refuses, C's own sweep says
  whether a template refuses first.

C and X are its exact twin. `--fused` runs it after them on every expression T
typed and compares its refusal, its tape, its labels' entries and its calls'
records: no difference on `sqlite3.c` (51,338 expressions), Lua (10,828) and
musl (22,998), with `-g` too. Four deliberate breaks each show: the calls' rows
left in the sweep's order (291 of `sqlite3.c`'s expressions differ), the labels'
numbers (73), a member's load written before its offset (11,343), and the
temporaries' order (2 of a fixture's 3 expressions with several postfix updates
through addresses; `sqlite3.c` has none with two, so `test-checker`'s fixture
has them).

Cycles and instructions a taken node in the tight loop, against main
`1b6461b7`, medians of three rounds (two for musl):

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| main's three sweeps | 173.8, 594.8 | 146.0, 683.9 | 130.7, 645.1 |
| T and the one sweep | 164.2, 600.5 | 141.1, 689.5 | 130.2, 651.0 |
| | −5.5% | −3.4% | −0.4% |

- **It runs C's and X's instructions, not fewer.** After T's 89 cycles, C and X
  take 86 for 259 instructions and the one sweep 74 for 264. What it saves is
  X's own walk of the records, the most front-end bound of the three
  ([above](#ts-loop-without-its-rules-and-what-a-first-run-costs)): X's 88
  instructions took 39 cycles.
- **It holds with other code between the machine's runs**
  ([below](#where-the-machine-runs-other-code-between-its-runs)): 6 to 8% on
  `sqlite3.c`, 3 to 5% on Lua, and on musl 3% with one expression a run and
  under 1% by bodies.
- **musl's units are short** (16 taken expressions, 5 nodes each): the one
  sweep's setup and its numbering after the loop are a larger share there.

`node_machine_run` still runs C and X; the one sweep is the shadow's until step
2 takes one of them. With it the sweeps cost 164 on `sqlite3.c`, T 89 of them
and the one sweep 74.

### Where the machine runs: other code between its runs

The gate's figure is the shadow's tight loop: one expression's sweeps right
after another's, their code and their branches' history never leaving the
caches and the predictor. In a compile the reader's code runs between two of
the machine's runs. `--interleave N` times that position: a file's taken
expressions by chunks of N (a body's for 0), the census's own walk of each of a
chunk's expressions (`node_census_expression`: its tests over the same nodes,
904 to 909 instructions and 227 to 232 cycles a node on `sqlite3.c`), then the
sweeps of each. A set less the walk alone is what the sweeps add there, the
walk's own slowdown included; the sweeps' own time is their TSC ticks inside
the set.

Cycles a taken node, medians of three rounds:

| `sqlite3.c`: a run takes | three sweeps, less the walk | T and the one sweep, less the walk | three sweeps' own | the one sweep's own |
| --- | ---: | ---: | ---: | ---: |
| (the tight loop, no walk) | 174.9 | 163.3 | | |
| 1 expression | 273.4 | 255.9 | 207.1 | 190.9 |
| 16 | 243.5 | 225.3 | 196.1 | 182.9 |
| a body's (19 on average) | 216.5 | 199.3 | 189.6 | 176.7 |
| 256 | 186.7 | 172.9 | 181.7 | 170.9 |

| Lua: a run takes | three sweeps, less the walk | T and the one sweep, less the walk | three sweeps' own | the one sweep's own |
| --- | ---: | ---: | ---: | ---: |
| (the tight loop, no walk) | 145.9 | 141.0 | | |
| 1 expression | 206.5 | 197.8 | 169.9 | 162.0 |
| 16 | 167.5 | 161.0 | 154.9 | 149.7 |
| a body's | 168.6 | 163.2 | 155.4 | 150.6 |
| 256 | 150.7 | 145.9 | 149.0 | 144.7 |

musl's three sweeps add 188.7 with one expression a run and 144.7 by bodies,
130.2 in the tight loop.

- **One expression a run costs the sweeps 18% of their own time** on
  `sqlite3.c` (207.1 against 174.9), **and the code between them twice as many
  cycles** (273.4 less 207.1 is 66 a node, on the walk's 229): each puts the
  other's code and branches out of the caches and the predictor. The pair's
  extra is 56% over the tight loop there, 42% on Lua and 45% on musl.
- **By 256 expressions a run little is left of it:** 7% on `sqlite3.c`, 3% on
  Lua.
- **A body's expressions are worth chunks of 64** on `sqlite3.c`, whose bodies
  hold 19 taken expressions on average, the long ones most of them; on Lua,
  chunks of 16.

What it decides:

- **Step 2's range holds a body's expressions, or a statement list's, never
  one.** Run after each expression, the sweeps would add `sqlite3.c` 273 cycles
  a node where the gate's figure says 175. Step 3 has it by what it is.
- **The gate is read where the machine will run:** `--interleave 0` is the
  closest position the shadow has to step 2's.
- **A sweep's design is worth less than its position:** with one expression a
  run the one sweep saves 17 cycles a node; a body's expressions a run save 57.

**The shadow's own sets showed it too.** It took a file's sets in turn inside
each pass (T, T and C, all three) and kept each set's least. The sets this round
added joined the turn, so a file's T then ran after the walk's sets: on Lua, 300
expressions a file, T read 79 cycles a node after its own pass, 82 after the
three sweeps' and 89 after the walk's, on the same code; `sqlite3.c`, one file,
read the same. The shadow now runs each set's passes one after another, and a
set's figure no longer moves with the sets timed beside it. Main's own figures
drop 1.1% on Lua and musl by it, none on `sqlite3.c`.

### A memo of shapes, and lanes: what didn't pay

Two more designs, each built as an exact prototype behind the shadow and
measured on the 9700X, not merged; their patches are in the record.

**An expression's shape as a memo.** A taken expression's entries are a function
of its shape: each record's operator and operands' places, its cast's type or
its member's name, a leaf's type, flags, category, constant and object's kind,
the leaves' objects only up to renaming (which leaves name the same object,
which the same symbol), and the root's context, target and initialized object.
A unit's later expression of the same key takes the first one's tape with its
own objects and symbols in place.

- **It is exact:** every hit was instantiated and compared with the sweeps'
  tape, labels and calls on the same expression, with no difference on the
  three corpora.
- **A third of the nodes hit:** 55% of `sqlite3.c`'s taken expressions and 39%
  of their nodes, 39% and 31% of Lua's, 23% and 16% of musl's. They are the
  short ones: 86 to 90% of the expressions of 1 to 3 records, 14% of those of 9
  to 16.
- **The key reads what T reads of every record:** 41 cycles and 134 instructions
  a node on `sqlite3.c`, 7 more for the lookup, paid by the misses on top of
  their sweeps.
- **It is slower:** 222 cycles a node on `sqlite3.c` against the three sweeps'
  180 in the same build, 207 with the one sweep for the misses. A hit run right
  after its own first run still costs 55 a node, where the sweeps' second run
  costs 118.
- **No looser key helps enough:** without the leaves' integers, which an exact
  memo could parametrize, 44% of `sqlite3.c`'s nodes; without casts' types and
  members' names too, 49%. The leaves' types are the rest (68% without them),
  and they decide the entries' classes.

**Lanes.** The shadow's census groups a batch's records by level (a leaf's 0,
any other's one past its operands' greatest) and class: a group is what one
vector step could take.

| batch of | `sqlite3.c`: records a group, of 16 lanes | Lua |
| --- | ---: | ---: |
| 1 expression | 1.46, 9.1% | 1.58, 9.9% |
| 8 | 3.14, 18.6% | 3.28, 19.3% |
| 64 | 9.77, 45.4% | 10.08, 46.3% |
| 512 | 45.9, 82.5% | 30.8, 74.7% |
| a body's | 6.62, 33.9% | 4.61, 25.6% |

- **An expression has 5.7 records, at most one or two of a kind and level.**
  Leaves, 46% of the records, fill 96% of their lanes at 64 expressions;
  members 59%, discards 44%, the rest 10 to 34%. musl's units hold 107 records.
- **T's per-record prologue by lanes is exact and saves no cycle.** An
  expression's nodes are one range of the reader's columns, so one 16-lane step
  reads every record's operator, its first two operands' records and what
  refuses the expression before a record is typed; the leaves and the other
  records are then two loops over the masks' bits. On `sqlite3.c` T runs 332.2
  instructions a node for 349.5, and 92.9 cycles for 92.2. T isn't held by the
  instructions of its bookkeeping.
- **Lanes over batches sorted by level and class weren't built.** They would
  fill half their lanes at 64 expressions, with each record's operands
  elsewhere in the batch (a gather, at what 16 loads cost), on sweeps by
  batches, which lose the predictor's handoff
  ([above](#sweeps-by-batches-what-the-predictor-loses)).

The record of this section and the two before it is
[`node-machine-step1-designs-2524-2026-11-15`](https://github.com/buster14a/buster-performance/tree/0c8faee435d3b64a7ed293375db3ff6d548efe63/docs/measurements/node-machine-step1-designs-2524-2026-11-15).

## Step 1b: the machine's own front

Step 1's sweeps read their inputs from the graph the reader's coded walk
writes (`c_check_expression_input.c`, #1038): the machine was timed on
records another stage made. Step 2 removes that stage for a taken expression,
so the machine must make its records itself. `--front` and `--front-first`
([the guide](commands/lowering.md#the-shadow-step-1)) run that in the shadow:
a front of the machine's own, beside the walk, which is its oracle. No compile
takes it. [The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/node-machine-own-front-2524-2026-11-15)
has the runs.

### What the walk costs, by its parts

`-c sqlite3.c --lanes 1` on the 9700X (CPU 2, seven runs, main at `b9ebe041`,
TSC ticks at 3,792.9 MHz):

| the coded walk | ms |
| --- | ---: |
| the chunks' acting tokens and classes | 0.40 |
| the chunks' identifiers' and literals' entries (`prepare`) | 2.32 |
| the classes' actions | 9.03 |
| of which the batch's flushes into the graph | 2.62 |
| the visits the actions leave to the scalar path | 0.21 |
| **all** | **11.97** |

That is the 99 cycles a body token of "Expected gains" (the graph's
construction). The flushes write 322,432 nodes in 2,413 batches, 74% of the
nodes in batches of over 256. A machine that took the walk's batch for its
records would skip the flushes alone, 22 of the 99: the walk's chains of
values through the token columns, which the batch is numbered from, are most
of its actions.

### The front

`c_check_node_machine_front.c` reads what the walk's actions read (a chunk's
acting tokens and classes, its identifiers' and literals' entries) and makes
records, not nodes: an operator, a token, operands as earlier records, a
leaf's value, a member, a call's arguments and library mark, the requested
type and the context.

- **A stack of values, in groups.** A value's consumer is its token's parent.
  The values not yet taken wait on a stack in groups that share a consumer,
  and a token's operands are the top group's where the token is its consumer.
  A token that only passes values on (a list, a separator, a group's closing,
  a braced initializer's `=`) hands its group to its parent in one store,
  however many values it holds: walking each value up its chain of separators
  is quadratic in a table's length, and musl has tables of thousands.
- **Left tokens.** A token the front has no case for (the walk's own visits,
  `sizeof` and `_Alignof`, a literal whose decoding reports) is left to the
  walk: its value is unknown, and so are a record that would take it and the
  context of what it took. An expression with a left token is not the
  front's.
- **Its limits.** 4,096 records and as many waiting values. A static table's
  thousands of elements wait for the list's end: a full stack makes every
  waiting value unknown, once. Between chunks only the waiting values'
  records are kept, since the tree puts a control's closing token after its
  body and a condition's value waits through it.
- **The comparison.** Each record against the walk's node at the same token,
  field by field; contexts at the walk's end, where the lists have given
  theirs; no value may be left waiting when a unit ends.

### Every record the walk's node

| | acting tokens | left | records | differ | machine-taken expressions the front makes whole | their evaluated nodes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `sqlite3.c` | 466,925 | 7,069 | 315,966 | 0 | 50,276 of 51,042 (98.50%) | 250,549 (97.76%) |
| Lua | 282,317 | 3,944 | 90,801 | 0 | 10,165 of 10,433 (97.43%) | 55,400 (96.14%) |
| musl | 2,828,420 | 304,173 | 618,853 | 0 | 20,961 of 21,561 (97.22%) | 103,895 (94.76%) |

With `-g` and with the front run before the walk the records are the same.
`sqlite3.c`'s left tokens are the scalar path's 2,697, 1,356 binary
operators, 971 prefix ones (945 of them `sizeof` and `_Alignof`), 914
terminators and 580 calls. Most of the taken expressions the front misses
that `--list` shows are an operator or a call above a `sizeof` (`memset(p, 0,
sizeof(*p))`); the others have a character constant with an escape. musl's
left tokens are mostly its file scope's.

**Mutation checks.** Thirty-three single changes to the front (an operator, an
operand's order, each leaf field, a member, an argument, a type, a context, a
distance, each column a chunk boundary keeps, the full stack's two paths, a
group's hand-over) each show as differences or as values left waiting in
`test-checker`'s fixtures. Five of them show in none of the three corpora:
the fixture's omitted conditional, the generated body whose chunk ends fall
at every place within a call, and the initializer with more values than the
stack holds are what catch them.

### What it costs

The shadow's one reading of each corpus (CPU 2, medians of seven runs, musl's
of three; ms):

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| the walk as it is: tokens and classes | 0.43 | 0.19 | 1.55 |
| its entries (`prepare`) | 2.36 | 0.71 | 4.43 |
| its actions, with the flushes | 8.83 | 3.17 | 26.27 |
| its visits | 0.22 | 0.19 | 5.19 |
| the walk, all | 11.85 | 4.26 | 37.48 |
| the front, after the walk acted at a chunk | 4.17 | 1.72 | 15.22 |
| the front, before it | 4.97 | 1.84 | 16.62 |
| the walk's actions where the front ran first | 8.02 | 3.07 | 25.72 |

- **The front is 55% of what it would replace** (the actions and the visits)
  where it runs first and takes the caches' first touches: 4.97 ms of 9.05 on
  `sqlite3.c`, 53% on musl. With the tokens, classes and entries, which it
  reads too, the machine's reading is 7.76 ms against the walk's 11.85: 65%,
  so 65 cycles a body token where the walk is 99.
- **Its columns' stagger was 30% of its time.** The front's arrays are one
  block, and each was a multiple of 4 KB long: a record's twenty-odd fields
  lay 4 KB apart, in one set of the first-level cache. With 128 bytes more an
  array (as `BUSTER_NODE_MACHINE_STAGGER`) the front before the walk went
  from 7.11 to 4.97 ms and after it from 6.67 to 4.17; its function's cycles
  from 34.6 M to 23.4 M at 54.4 M and 57.2 M instructions.
- **Counters in registers didn't pay.** The stack's and the records' counts
  as locals, with each token's parent read for the whole chunk first, retired
  68 M instructions where 57 M did, at the same time or worse (5.03 ms with
  the stagger, 7.53 without): the function holds more live values than
  registers, so a local is a stack slot.
- **What is left is diffuse:** 122 instructions and 50 cycles an acting
  token, 0.5 mispredicted branches, no line above a tenth. A record is twenty
  stores and a leaf twenty-seven; a leaner record comes with T reading the
  front's records, which fixes what a record must hold.

`-c sqlite3.c` itself is unchanged: 123.37 and 122.80 ms from fresh copies of
main's and the branch's executables (-0.46%, the copies' spread -1.12% to
+0.03%), the same peak.

### Operands as distances

[#2850](https://github.com/buster14a/buster/issues/2850)'s layout asks of the
front an operand as a byte, its distance back in the postorder, so that 64
records' operand facts are a permute over two blocks. The front has both
records when it makes one, and keeps the distance beside the index (255 for
that or more); the comparison checks it against the front's own records
between the walk's two nodes.

| | operands | within 64 records | in the byte | further | calls' arguments past 64 | taken expressions with one past 64 | roots' operands past 64 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `sqlite3.c` | 165,576 | 165,574 | 2 | 0 | 0 of 32,581 | 2 (0.004%) | 1,623 of 56,681 |
| Lua | 52,751 | 52,746 | 5 | 0 | 0 of 10,425 | 3 (0.029%) | 251 of 13,184 |
| musl | 314,578 | 314,573 | 5 | 0 | 12 of 51,040 | 4 (0.019%) | 809 of 57,888 |

An operator's operand is all but never out of a block's reach. A root's is
(a condition's, a discard's, a return's conversion): 1.4% to 2.9% of them are
a body away, because the tree puts a control's closing token after its body.
The front could make such a root where its value is given, since it knows the
consumer's class there, at the price of an order that is no longer the walk's;
it doesn't.

### The structural bytes, and a lean front

[#2850's step 0](https://github.com/buster14a/buster/issues/2850#issuecomment-6898510406)
ran the sweeps on byte columns at 16.7 and 20.6 cycles a taken node, where the
shadow's best is 164. Its layout reads of a record, beside the facts the
sweeps make, its **structure**: the operator, the operand count, the operands'
distances back, the parent's distance forward and the place among the
parent's operands. The front has all of it when it makes a record, so it now
keeps each as a byte: a consumer's distance and a place are written into an
operand's record when its consumer's is made. The comparison checks them from
both sides, and a consumer's distance that no compared consumer vouches for is
a difference. [The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/node-machine-lean-front-2524-2026-11-15)
has the runs.

**The lean front** (`--front-lean`) writes only that, the token and a leaf's
payload (an identifier's declaration, a literal's type and integer, a string's
type and object). An identifier's facts are then one load by its declaration,
which is #2850's step 1; a member's name, a requested type and a call's
library mark are loads by the token. It decides an identifier from its token's
own columns, so it reads none of the walk's identifier entries. Its records
are the same on every corpus, found by their bytes alone: no difference, the
same expressions made whole.

Before the walk on the 9700X (CPU 2, medians of seven readings in turn, musl's
of three; ms):

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| the walk as it is | 11.91 | 4.24 | 37.59 |
| #2857's front | 4.94 | 1.85 | 17.32 |
| the front with the structural bytes, passing tokens first | 4.83 | 1.66 | 15.60 |
| the lean front | 4.39 | 1.41 | 13.50 |
| the walk's preparation: its tokens' compaction | 0.47 | 0.30 | 1.87 |
| its identifiers' entries, which the lean front doesn't read | 0.98 | 0.16 | 1.31 |
| its literals' entries | 0.86 | 0.23 | 1.40 |
| tokens, classes, what it reads of the preparation and the front: #2857's | 7.71 | 2.74 | 23.4 |
| the lean front's | 6.13 | 2.13 | 18.5 |

- **The record's width was not the front's cost.** The lean front makes about
  half the stores (sixteen a leaf where the whole front makes thirty) and is
  9% faster than the whole front on `sqlite3.c` (15% on Lua, 13% on musl).
  The front's time is its walk over the acting tokens: the waiting values'
  groups, each value's consumer, the class dispatch. By its events the lean
  front is 48 cycles, 111 instructions and 0.4 mispredicted branches an acting
  token, where #2857's is 52, 122 and 0.5.
- **Passing tokens first.** 27% of `sqlite3.c`'s acting tokens and 57% to 59%
  of Lua's and musl's are lists, separators and groups, and most lists are
  declarations', with no value below them. Testing whether the top group of
  waiting values is the token's before the class dispatch, and reading
  nothing more at a terminator without a value, took 3% off `sqlite3.c`, 12%
  off Lua and 10% off musl.
- **What the machine reads is now 51% of the walk** on `sqlite3.c` (65% with
  #2857's front), 50% on Lua and 49% on musl: about 51 cycles a body token.
- **What the layout's bytes cap** (step 0 keeps a place up to 3 and a count
  up to 63): no record has more than 63 operands; 5,098 of `sqlite3.c`'s
  32,581 arguments are a call's fourth or later (822 of Lua's 10,425, 6,615
  of musl's 51,040).
- **First touch is a host's matter.** The front's arrays are fresh memory in
  every unit. In a cloud VM without transparent huge pages their first touch
  was half of the front's ticks on musl; on the 9700X it is under 2%
  (`--front-warm` touches them first).
- **The hooks stay out of the walk's function.** With the front's calls, its
  timers and its class counts inline in the coded walk and never run,
  `-c sqlite3.c` retired 0.76 M more instructions than main and read 0.26%
  slower from fresh copies of each executable. They are now two functions out
  of line, called only where the front runs: 0.41 M more instructions (0.03%),
  and 0.00%, +0.47% and -0.10% in three comparisons. Step 2's splice is in the
  same loop.

### T where it sits

Step 2 puts the machine in the reader, and every figure above has its sweeps
run after the pipeline: on the finished graph, with the program's columns.
`measure-node-machine --shadow --front-types` runs T where step 2 would
(`c_check_node_machine_reader.c`): right after the walk acted at a chunk, on
each full expression the front made whole, before the unit's program exists.
After the pipeline the shadow's own T types the same expression, and the two
must agree in every column that C, X and the shadow read. [The
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/node-machine-reader-types-2524-2026-11-15)
has the runs.

The coded walk runs once the reader has bound the unit's names and made its
declarations' types, so the unit's types and its tags' history are whole by
then. What T reads that the graph run or the program makes, and what stands
for it at reader time:

| what T reads | after the pipeline | at reader time |
| --- | --- | --- |
| an object's last static declaration, returns_twice, asm label | the program's columns | the same three, made from the reader's columns the program's way |
| whether a called object has a symbol | the selection's numbering | whether it has a static declaration |
| a library call's "computed inline?" | the stream's value of its size | T's own record of the size |
| a node's context | the graph's | the front's own, which is the walk's at the unit's end |
| a type's word | every type's, made at once | the unit's, then each later type's as it is made |

On the 9700X (CPU 2, medians of seven readings in turn, musl's of three):

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions T ran on at reader time | 56,681 | 13,184 | 57,888 |
| of them, taken, that the lowering never pops | 3,941 (7.0%) | 1,722 (13.1%) | 26,597 (45.9%) |
| of those, a declaration's discard | 3,828 | 1,708 | 5,557 |
| of those, in a body that is never lowered | 22 | 9 | 20,745 |
| compared with the shadow's T: expressions, differences | 50,555, 0 | 10,545, 0 | 22,363, 0 |
| T at reader time, TSC ticks a record | 67.5 | 84.3 | 134.5 |
| the shadow's T, its first pass in the tight loop | 61.8 | 59.8 | 61.7 |
| its best of seven passes | 54.1 | 43.2 | |
| acting tokens an expression | 8.2 | 21.4 | 48.9 |
| the machine's state as the shadow makes it, ms (T's own) | 0.96 (5.54) | 3.39 (2.04) | 35.09 (8.82) |

- **Exact.** No expression differs in its range, its refusal or its records,
  in any of 248 readings. The objects' columns (5,201, 13,118 and 255,903
  objects with a static declaration) and the library calls' answers (332, 53
  and, under musl's `-ffreestanding`, none) are the program's.
- **Almost nothing was missing.** The types made after the machine begins are
  1,264, 1,081 and 4,786, nearly all the walk's own (a string literal's array
  type); T's derivations make a handful, and two expressions of `sqlite3.c`
  read such a word. A static declaration stands for a symbol everywhere it is
  asked.
- **What a reader types for nothing.** Of the expressions T runs on at reader
  time, 7% on `sqlite3.c`, 13% on Lua and 46% on musl are ones it takes that
  the lowering never pops. On `sqlite3.c` and Lua nearly all of them are a
  declaration's discard: at a declaration's `;` the walk casts each
  initializer's value to void, as it does an expression statement's value,
  and the front makes that cast a root. Nothing evaluates it (the program
  takes an initializer's nodes for data, and the initialization's value is a
  root of its own), so T types each such value once more under the cast. 22
  and 9 are in bodies the selection drops. On musl 20,745 are, most of them
  headers' inline functions, and 5,557 are discards. The pipeline reads and
  types all of those nodes today and lowers none. A machine in the reader
  must leave a declaration's discard alone, and it runs C and X on a dropped
  body's expressions too unless the selection comes first. The split was
  counted after this section's runs, with [the tests where they
  sit](https://github.com/buster14a/buster/issues/2524#issuecomment-6908043343):
  this section's first reading had all of them as dropped bodies', which
  holds for musl alone.
- **Where it sits, T costs what a body's expressions a run cost.** On
  `sqlite3.c` it is 9% over the tight loop's first pass and 25% over its
  best, which is what "Where the machine runs" found for a body a run (199
  cycles a node against 164). Step 2's sum took the sweeps at 75 to 91 cycles
  a body token: if C and X pay in place as T does, they are about 94, and the
  sum gives 1.25 with the lean front.
- **Sparse expressions cost more.** Lua has an expression every 21 acting
  tokens and musl one every 49, where `sqlite3.c` has one every 8, and there
  T at reader time is 1.4 and 2.2 times its tight loop's first pass: a chunk
  holds a few expressions, and by the next chunk T's code and tables have
  been pushed out, as the same section measured with other code between runs.
  This record has no counters for it. Step 2's gate has a line for those
  corpora (no unit slower by more than 1%), so the machine has to run more
  than a chunk's expressions at a time there, or be small enough to stay warm.
- **Its state is a scan a unit as the shadow makes it:** every type's word,
  the unit's declarations read for the three columns, the caches cleared. It
  is 26 µs a unit, four times T's own time on musl's 1,336 units. Step 2 would
  make it as the declarations are read.
- **What no comparison reads.** The census refuses a library builtin's call
  and a returns_twice callee before the shadow's T, so T's refusal of them at
  reader time is compared with nothing: the answers behind it are compared
  with the program's directly, and the fixture counts the callees refused.
  Of 19 faults put into the reader-time path one at a time, the tests show 16.
  The other three show on no input here: T reading the graph's own contexts
  as they stand after the chunk (the walk has marked a taken expression's
  nodes by then), a later type's completion position, and the words of a type
  the action makes.
- **A compile is unchanged** (+0.07% and -0.18% from fresh copies; 26,000
  more instructions of 1,363 M). The shadow's own T retires 1.6 to 2.2 more
  instructions a node (0.5%) for the tests of where its callee's facts come
  from.

### The front by passes

The lean front spends 48 cycles, 111 instructions and 0.40 mispredicted
branches an acting token, a token at a time over a stack of waiting values.
`measure-node-machine --shadow --front-flat` (`c_check_node_machine_flat.c`)
makes the same records another way, and was built in two steps, each measured
on the 9700X against the lean front as its twin: every record both make equal,
field for field, by token.

**Without the stack.** In postorder a token's children are the runs just
before it, and the reader has each run's length. A token that takes values
hops back over its children by those lengths and reads one word a child from
a ring by position: the place of the record that is the child's value, an
unknown value's mark, or nothing. No value is pushed or matched and no parent
is loaded. The tokens that only pass values on (a quarter of `sqlite3.c`'s
acting tokens, over half of Lua's and musl's) are never visited: the front's
own compaction writes their step, 1, into a second ring.

**By passes.** How many records a token makes is known from its class and its
own columns before any operand is read, if a token the front leaves keeps its
place as an empty record. A chunk is then taken apart by what each step needs:
the classes and each class's list 16 tokens a step; the leaves a class at a
time; one pass in order for the places, the words and each record's first
form; then each class's records in its own loop, with no branch on the class;
then each empty record's consumers made empty in turn.

Before the walk acts at a chunk, where the machine would run, ms (CPU 2, the
lock, Release, medians of seven readings, musl's of three; each front with the
compaction it reads):

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| the lean front, and the walk's compaction | 4.41 + 0.46 | 1.42 + 0.30 | 13.47 + 1.82 |
| no stack, a token at a time | 3.90 + 0.27 | 1.19 + 0.22 | 9.43 + 1.86 |
| by passes | 3.01 + 0.33 | 1.29 + 0.23 | 11.08 + 1.93 |
| by passes against the lean front | 1.46 times | 1.13 | 1.18 |

By passes on `sqlite3.c`, by stage, before the walk and after it (its columns
warm): the classes 0.16 and 0.16 ms, the leaves 0.50 and 0.37, the places 0.88
and 0.84, the records 1.44 and 1.11, the empty records 0.03.

- **Exact.** No record differs on the three corpora. On `sqlite3.c` the lean
  front makes four records the flat one doesn't: values further back than the
  rings' 8,192 positions (a condition before a very long body). The flat front
  leaves 4,656, 2,255 and 95,511 places empty.
- **On the compiler's own source too, once the comparison knew the reach's
  other face.** A declaration's `;` takes every value of its initializer.
  Where the first is past the rings' reach the flat front leaves the `;`, and
  the lean front, whose stack holds a value however far its consumer is,
  makes its discards. A value near that `;` then has a consumer in the lean
  front and none in the flat one. `main.c` has two: the 808 KB string of
  `c_unicode_names.inc`, and `branch_sizing.c`'s table of sixteen long
  constant expressions. The comparison read each as a difference; it now
  takes a consumer only the lean front made as it took one only the flat
  front made. On `main.c` the lean front makes 18 records the flat one
  doesn't, 17 of them those two declarations' discards, and the flat front
  35,340 the lean one doesn't: discards of tables with more values than the
  lean front's stack holds.
- **The stack was not the cost.** Without it the front is 14% faster on
  `sqlite3.c` (its pass 52.9 cycles, 113 instructions and 0.48 mispredicted
  branches a token it visits). Making the hops' positions independent of the
  pass's own stores, and moving the two rings off a 4 KB multiple apart,
  changed nothing.
- **The dispatch was a part of it.** By passes a token's class is no branch,
  and `sqlite3.c` is 20% faster again. Lua is 8% slower and musl 15% slower
  than a token at a time: most of their tokens are declarations', which make
  nothing, and each pays every pass.
- **What is left is each class's loads and a hundred instructions a record.**
  Before the walk a condition, a return and a cast cost 100 to 140 cycles
  each (over three builds' profiles): each reads a column of the reader's by
  token (the condition's keyword, the return's type, the cast's type), a unit
  long and not in the cache. An identifier's three loads are 19 cycles a
  leaf, 14 after the walk. A binary operator is about 30 cycles and 120
  instructions: two hops, its record, and the two bytes it writes into each
  operand's record. The places' pass is 14 cycles a token it visits, 0.18 of
  a mispredicted branch: the branch on a statement's terminator, whose
  discards are made there.
- **Against the line this was claimed with:** 15 cycles an acting token on
  `sqlite3.c`. By passes it is 36, and 4 for the compaction (the lean front 52
  and 5): not met.
- **A compile is unchanged** (-0.09% twice from fresh copies). It retires
  344,000 more instructions of 1,363 M: the Release build's parts fall
  elsewhere with the new file, and the coded walk's function is 41 bytes
  longer. No hook is in its loop.

What would take it further is outside a front that reads the reader's columns
after the fact: an identifier's three loads as one word a token; a discard as
a byte on its operand's record, as [#2850's
follow-up](https://github.com/buster14a/buster/issues/2850#issuecomment-6900715849)
measures it, so that every token's count is its class's and the places are a
prefix sum 16 tokens a step; and the consumer's two bytes left to the sweep
that reads them. Of 24 faults put into the flat front one at a time, the tests
show 21. The other three show on no input here: an identifier with no
declared type that the walk acts at, a member operator without its name, and
the hops' loop ending a child early, which the general loop behind it then
answers.

### C and X where they sit

"T where it sits" left the other two sweeps after the pipeline.
`measure-node-machine --shadow --front-sweeps` runs them at reader time too:
C and X follow T on each expression it takes there, before the unit's program
exists. After the pipeline the shadow's own C and X run on the same
expression, and reader time's tape must be theirs entry for entry. [The
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/node-machine-reader-sweeps-2524-2026-11-16)
has the runs.

C and X read T's records and little else:

| what C and X read | after the pipeline | at reader time |
| --- | --- | --- |
| whether an object is a local or a global | whether it has a symbol, the selection's numbering | whether it has a static declaration, or is a string's |
| a global's symbol, in its entries and in a direct call's row | the selection's | the object itself, which a splice patches |
| a declaration's bits (register, compound, static) | the program's input's | the reader's input's, the same column |
| the root's context, its conversion's target, its object | the lowering's pop's | none, but an initialization's: its object's type without its qualifiers where the conversion writes entries, and the object |

- **The machine holds the objects' columns itself now**
  (`NodeMachine.object_symbols`, `declaration_bits`, `object_count`), set
  where it begins, so C and X read no program in either place. T's test of a
  direct callee reads the same column, and its test of where the answer comes
  from is gone.
- **What a splice patches is told by an entry's kind:** `GLOBAL`,
  `GLOBAL_ADDRESS`, `STORE_GLOBAL` and `SET_GLOBAL`, and a direct call's row.
  Locals, labels, calls' rows and temporaries were relative on the tape
  already.
- **A string's object is static without a declaration.** Reader time gives the
  strings' objects their place before each chunk's expressions. Without it
  1,054 of `sqlite3.c`'s tapes had a local's address where the shadow has a
  global's: the one thing the first run lacked, with the qualifiers of 14
  `const` objects' targets.

On the 9700X (CPU 2, medians of seven readings in turn, musl's of three; TSC
ticks a record T typed):

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions C and X ran on at reader time (those T took) | 55,063 | 12,568 | 49,115 |
| of them taken; refused for a template or a row | 50,393; 4,670 | 10,387; 2,181 | 41,209; 7,906 |
| compared with the shadow's C and X: expressions, entries, differences | 50,555, 331,155, 0 | 10,544, 65,963, 0 | 22,324, 133,898, 0 |
| patched in them: globals' entries, direct calls | 2,199, 12,555 | 668, 3,845 | 2,396, 4,871 |
| T alone at reader time | 69.1 | 85.6 | 138.7 |
| T, C and X at reader time | 127.7 | 139.9 | 235.9 |
| C and X in place (the difference) | 58.7 | 54.4 | 97.2 |
| C and X in the tight loop, its first pass | 44.0 | 39.8 | 42.8 |
| the three in the tight loop, its first pass | 106.9 | 99.8 | 104.5 |

These are with the whole front before T. After the lean front the three read
152.8, 160.7 and 264.4, and T alone 93.8, 105.0 and 170.3: the shadow's search
for a root's operand's record there is in both.

- **Exact.** No expression differs in its root's target or object, its
  refusal, its tape, its labels or its calls, in any of 34 readings, whole
  front and lean.
- **A splice patches little.** In `sqlite3.c`'s compared expressions 2,199
  entries of 331,155 hold a global, and there are 12,555 direct calls: a
  quarter of a call an expression. Everything else on a tape was the
  expression's own already.
- **Where they sit, the three cost what a body's expressions a run cost.** On
  `sqlite3.c` they are 1.19 times their tight loop's first pass: about 212
  cycles a node, where "Where the machine runs" has 216.5 for the three sweeps
  with a body's expressions a run and 174.9 in the tight loop. C and X pay a
  third over their tight loop there and T a tenth, so the guess that C and X
  pay as T does was low for them and right for the sum.
- **Step 2's sum stands.** It took T and the one sweep at 199 cycles a node, a
  body's expressions a run, where the three sweeps are 216.5. Reader time ran
  the three as they are, not the one sweep, and reads them at 212: the sum's
  upper figure, 91 cycles a body token, holds if the one sweep keeps its
  twelfth there, and the sum is 1.25 on `sqlite3.c`.
- **Sparse expressions cost C and X as they cost T:** 1.4 times the tight
  loop's first pass on Lua for the three, and 2.3 times on musl.
- **The reader runs them on a declaration's discard too.** Of the 55,063
  expressions C and X run on at reader time on `sqlite3.c`, 3,941 are ones
  the lowering never pops (7%); of Lua's 12,568, 1,722 (14%); of musl's
  49,115, 26,597 (54%). Nearly all of `sqlite3.c`'s and Lua's are a
  declaration's discard, the walk's cast to void of an initializer's value,
  which nothing evaluates ("T where it sits"); 20,745 of musl's are in bodies
  the selection drops. The discards are no roots of the machine's, and the
  selection before the sweeps matters for musl's headers.
- **A compile is unchanged** (-0.47% and -0.15% from fresh copies, the same
  1,363.464 M instructions).
- **The shadow's own sweeps retire 0.5 to 0.7 fewer instructions a node,**
  T's test of where a callee's symbol comes from being gone, and read 1.3%
  more cycles on `sqlite3.c` as built (173.3 to 175.5), 1.7% on Lua, 0.2% on
  musl.
- **That is inside what the stack alone does to them.** The machine's
  structure is a local of the census's function, and the sweeps' frames are
  below it at a distance the build fixes. With that distance moved at run
  time over 32 places a page wide, the baseline's three sweeps read from
  172.8 to 182.4 cycles a node (5.6%) and this build's from 174.5 to 189.1
  (8.4%); a third and a quarter of the places are the slow ones. Store-to-load
  interference in the machine's functions is 44% up as built, with its
  mispredicted branches and its cache misses within 2%. At their best places
  the two builds are 1.0% apart, all of it X's, whose functions the Release
  build's parts now place beside T's. A figure of these sweeps is one place
  of the 32: two builds of the machine compare over the places, or with the
  structure off the stack.
- **Faults.** Of 13 faults put into the reader-time path one at a time, the
  tests show all 13. Two of them needed a fixture line first: a conversion
  that is empty between two types, and a `for` statement's condition, whose
  wrapper has a type of its own.

### The tests where they sit

"C and X where they sit" left the decision after the pipeline. At reader time
the machine took what T, C and X didn't refuse, and the census's set and its
silence tests, which say what it may take, read the stream.
`measure-node-machine --shadow --front-tests` runs them at reader time: after
T takes an expression and before C and X, the census's own test code on T's
records. Their bits are the machine's verdict before the pipeline. After the
pipeline the census's verdict for the same root is compared with it, test by
test. [The
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/node-machine-reader-tests-2524-2026-11-16)
has the runs.

The tests are one code with two views of a node: the stream's after the
pipeline, T's record at reader time. What else they read, and what stands for
it before the pipeline:

| what the tests read | after the pipeline | at reader time |
| --- | --- | --- |
| a node's value, its operands' and arguments' conversions, a member's offset | the stream's | T's record of the node |
| an action's warnings at a node | the stream's, less an unevaluated operand's | T's own actions', for the whole expression |
| the root's kind | the control that pushed it | the front's root's operator; a root inside a statement expression is a nested control's |
| an initializer's object's type | its control's declaration's | the declaration's |
| the unit's enabled warnings | the options and the diagnostic pragmas | the same, whole before the coded walk |
| a body's diagnostic and floating-point pragmas | by the body's braces | by the outermost braces around each pragma |
| the warning events at the expression's tokens | the reader's, the graph run's and the later passes' | the reader's own so far |
| a packed member's address | the reader's witnesses, a later pass's | any address taken in a unit with a packed record or member |
| a body that tracks its stack, a node popped twice | the lowering's | nothing: a splice reads them where it pops the root |

On the 9700X (counts; `-Wall -Wextra` turns on the tests that are off by
default):

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions the tests ran on (those T took) | 51,185 | 10,859 | 43,481 |
| compared with the census: lowered, never lowered | 51,122, 63 | 10,846, 13 | 22,518, 20,963 |
| taken there that the census refuses; tests that differ | 0; 0 | 0; 0 | 0; 0 |
| refused by a test there | 567 | 302 | 435 |
| of them, ones C and X take | 4 | 208 | 298 |
| with `-Wall -Wextra`: refused by a test; of them, ones C and X take | 3,821; 3,256 | 824; 728 | 2,408; 2,243 |
| with `-Wall -Wextra`: taken there that the census refuses; tests that differ | 0; 0 | 0; 0 | 0; 0 |

- **Exact.** No expression reader time takes is one the census refuses, with
  the default warnings or with all of them, on a lowered body's root or on
  one the lowering never pops (the census reads those from the stream again
  for the comparison). No test's bit differs but in the three ways it may:
  reader time's actions' warnings are at least the graph run's, an address in
  a unit with a packed record stands for a witness, and its events are the
  reader's own.
- **What the tests add to the sweeps' own refusals.** T, C and X refuse what
  they have no row or template for, which is most of the set. They know no
  warning: with the default warnings they would take 4 expressions of
  `sqlite3.c` that the census refuses, 208 of Lua's and 298 of musl's, and
  with `-Wall -Wextra` 3,256, 728 and 2,243: a for statement's clauses under
  `-Wfor-loop-analysis` most of all (3,244, 523 and 1,857 of the refusals),
  then musl's parentheses and sign comparisons.
- **A declaration's discard is no root.** At a declaration's `;` the walk
  casts each initializer's value to void, and the front made that cast a
  root: 5,355 of the 56,681 roots T ran on at reader time on `sqlite3.c`,
  2,275 of Lua's 13,184 and 13,991 of musl's 57,888. Nothing evaluates them.
  The comparison found them: the census refuses each as a value without an
  effect, and the lowering pops none. Reader time leaves them alone now, by
  the parser's tree: the cast's operand is an initializer's `=` or below one.
- **What the fixtures found.** Each warning has a line in `test-checker`'s
  fixture, and reader time must refuse it as the census does. Three did not
  at first: an enumeration's constant under `-Wassign-enum` (the test read a
  flag the census sets after the pipeline), a statement expression's inner
  roots (a nested control's, which the front gives as a statement's or an
  initialization's), and a packed member's address (its witness is made by a
  pass after the walk).
- **A conditional's third operand.** T's columns hold two operands'
  conversions, and a constant second arm's conversion can change its value
  (`i ? 1.5f : 16777217`). T keeps a conditional's third now, beside them.
- **T's rows and rules know no warning, and none is missed.** With `--verify`
  reader time checks each node a row or a rule typed against the action,
  on every expression it types: its value and conversions are the action's,
  and the action gives no warning there, on the three corpora with every
  warning on.
- **The lowering's own facts are a splice's to read.** A body that tracks its
  stack (one with a statement expression, or any body of a unit that calls a
  terminating helper) and a node popped twice are known where the lowering
  pops the root, not before. On the corpora no expression reader time takes
  has one; a fixture's body with a statement expression has three.

What they cost in place (TSC ticks a record T typed, medians of seven readings
in turn, musl's of three; the discards left alone in every row):

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| T, C and X at reader time | 134.3 | 158.5 | 254.1 |
| with the tests between T and C | 299.8 | 338.4 | 528.0 |
| the tests alone (the difference) | 165.5 | 179.9 | 274.0 |
| the tests alone as the census had them | 206.1 | 221.7 | 292.8 |

- **As the census writes them, the tests cost more than the three sweeps.**
  They are 165.5 ticks a typed record on `sqlite3.c` beside 134.3 for T, C
  and X: about 115 cycles a body token, where ten move step 2's sum by a
  hundredth. In the machine as they are they would take the sum from 1.25 to
  about 1.13, under its gate of 1.2.
- **There is no one thing to fix.** As the census had them the tests read a
  node's whole value, asked the type table and a declaration's attributes at
  every node, and ran the constants' tests on every operator. A byte a type
  and a byte a declaration, kept once, and three tests asked only where the
  operator can fail them (the constants', the tree's parent, a truth
  context) took a fifth off. What is left is spread, by the profile: the walk
  that lists an expression's nodes (a stack and three columns a node), the
  forty conditions a node passes one after another whatever its operator, and
  the view's step from a node to its record at every read. Three quarters of
  the added cycles are in the tests' code and a seventh in the sweeps' own,
  which run colder with that code between an expression's T and its C. The
  walk's columns kept by record, an expression's alone, made it slower (181
  ticks a record), and are not kept.
- **What the machine needs is the same tests in its own form:** over T's
  records in their order, with no walk; one case an operator, so that a node
  runs its operator's tests alone; a leaf's declaration's flags as a byte the
  reader makes; and the whole after T as a loop of its own. The census's
  code on the view is then its oracle at reader time, bit for bit, as the
  action is T's rows'.
- **Without the discards the three sweeps read 134.3 ticks a record** on
  `sqlite3.c` where "C and X where they sit" has 127.7, 158.5 on Lua (139.9)
  and 254.1 on musl (235.9): each scalar initialization's second typing was
  cheaper than a first. That is 223 cycles a node on `sqlite3.c` where the
  sum's upper figure took 216.5.
- **Its state is a unit's:** the enabled warnings, a bit a token for the
  reader's events and for the statement expressions, and the bytes' columns
  cleared: 0.19 ms on `sqlite3.c`, 61 µs a unit on Lua's 34 and 6.7 µs a unit
  on musl's 1,336, beside the machine's own.
- **A compile is unchanged** (-0.11% and +0.22% from fresh copies, the same
  1,363.464 M instructions), and the shadow's own sweeps retire the same
  instructions a node (597.6 on `sqlite3.c`) and read 1% more cycles as
  built, inside what the stack's place moves them. buster#2850's fact words'
  pass reads the same a record on the three corpora. Its harness's figure, a
  pass less a second pass that only puts the given records in place, is 13%
  to 30% lower on musl in this build, and that is the second pass, which
  reads a tenth slower alone as built (the record's section 5).
- **Faults.** Of 26 faults put into the reader-time path one at a time (the
  view's fields, the root's kind, the object's type, each stand-in, the
  discards), the tests show 25. Two needed a fixture line first: a call's
  argument whose conversion changes a constant, and two members of one record
  written in one expression, which their offsets tell apart. The one that
  shows on no input reads a conditional's third conversion as its second's:
  both arms convert to the conditional's common type.

### The tests in the machine's own form

"The tests where they sit" made the verdict at reader time with the census's
own code, at more than the three sweeps' cost.
`measure-node-machine --shadow --front-own-tests` made the same bits as the
machine would: from T's records in their order, with no walk of the graph and
no step from a node to its record. With `--verify` the census's code ran
beside it on every expression, and a bit, a span of tokens or a count of
nodes that differed failed the command. [The
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/node-machine-own-tests-2524-2026-11-16)
has the runs.

**This section is what that form measured as a pass over T's columns.** Its
loop and its leaves' tests are now the window's, 64 records a step ("The
tests on the window", below), and there is one form: the option and the pass
over T's columns are gone. The tables of what each test reads, and of what is
decided before the form runs, are the form's still.

One loop reads every record once, without a branch on what it is: its
operator's bytes in the unit's tables (by its enabled warnings), its type's
byte, its standing, its flags, its token. It keeps a byte of what the record
is as an operand, gathers the set's tests as bits over all records, and lists
three kinds of record. Each test is then decided where what it reads is:

| the tests | decided | by |
| --- | --- | --- |
| an operator outside the set, a special type, a bit-field, an aggregate copied, a packed member's address | in the loop, over all records | the operator's class, the type's byte, the flags |
| a register object, a declaration the tables don't model | at each leaf | its flags and the name's declaration's byte |
| a comparison under a bitwise operator or as a statement, a `!` under a comparison, an assignment as a condition | at a comparison's token, a `!` and an `=` | the parser's tree |
| a call's aggregates and its callee's own semantics | at each call | the arguments' bytes, the callee's declaration's byte |
| a constant its conversion changes, a division by zero, a shift's count, a null dereferenced | at an operator with a constant operand, exact in place | the constant's value in T's columns, the types' widths |
| a pointer cast to a narrow integer, a string plus an integer, a comma's discard | at the operator, exact in place | its operands' bytes |
| a comparison's bound, a mix of enumerations, signs compared, a void dereferenced, a shift of a sum, a chained comparison, a shift or a product read as a truth, a name assigned to itself, an atomic record's member, a subscript at its array's end or past it, a constant the columns don't hold whole, an integer constant without the literal's flag made a pointer (which may be a null pointer constant that is no literal: "A null pointer constant that is no literal", below) | by the census's code, on that record alone | a condition each of them needs, from the operator's bytes and its operands' |
| a statement's discard, a for statement's clause, an initializer's constant, a returned local's address, T's actions' warnings | once an expression | the root, and the loop's bits |
| two writes not apart from a sequence point | by the census's code after its walk | two writes in the expression, or one below its top |

The loop's own conditions are never a silence test's verdict but where the
test is exact in place: elsewhere they only say where none can fire, and the
census's code decides.

**What is decided before it, it doesn't ask.** It runs where it sits, on an
expression the front made whole and T took:

| the census's condition | decided before the form runs, by |
| --- | --- |
| a floating-point pragma's region | T's context refusal, by the same bits |
| a callee whose type has no prototype | T's callee refusal |
| an overload set's leaf, for which a wide leaf went to the census's code | T's leaf refusal; a wide leaf's flags are in its columns, as any leaf's |
| a sizeof's or an `_Alignof`'s operand, a compound literal's object | the front, which leaves the three to the walk |
| a register object by its object's bit | the reader: the leaf's flags have it from the same declaration |
| a condition's root that is a shift or a product | the root's kind: a condition's root is its wrapper, whose own send takes its operand |

- **Each is the census's code's condition still,** and that code runs beside
  the own form with `--verify`.
- **The fixtures have a line for each that could come back:** a pragma's
  region, a callee without a prototype, a compound literal and
  `sizeof(x++)`. The day T takes one or the front makes one, `test-checker`
  shows what the form lacks.

On the 9700X (`sqlite3.c`, Lua, musl):

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| records the loop lists: leaves | 45.6% | 44.5% | 48.7% |
| at a comparison's token, a `!` or an `=` | 9.6% | 6.4% | 8.0% |
| operators with tests of their own | 16.8% | 21.7% | 16.2% |
| records sent to the census's code | 0.31% | 1.18% | 0.81% |
| expressions walked for it | 2.1% | 4.1% | 3.0% |
| with `-Wall -Wextra`: records sent | 5.2% | 3.9% | 4.7% |
| expressions whose bits are not the census's code's, either way | 0 | 0 | 0 |

TSC ticks a record T typed at reader time (medians of seven readings in
turn, musl's of three), at main `cc1c47d62`, the form as it is now ([the
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/node-machine-own-tests-main-2524-2026-11-16)):

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| T, C and X | 129.8 | 154.8 | 259.2 |
| with the census's code between T and C | 308.0 | 346.4 | 521.6 |
| with the own form | 216.1 | 270.8 | 411.6 |
| the census's code alone (the difference) | 178.2 | 191.5 | 262.5 |
| the own form alone | 86.3 | 115.9 | 152.4 |

What follows was read at main `be6515fba`, before the conditions decided
before the form were taken out of it: there the own form alone was 90.0,
118.8 and 157.3 ticks, and the census's code 173.5, 186.1 and 248.0. Taking
them out changed no count of the table above and a few ticks.

The own form by part (`sqlite3.c`; each part switched off by a knob of a
build made for it, so the parts overlap by a few ticks):

| | ticks a typed record |
| --- | ---: |
| the loop | 27.0 |
| the operators' tests: calls 6.6, a constant operand's 9.5, the census's code and its walks 4.5, the rest 1.8 | 27.1 |
| the leaves' tests | 14.7 |
| the expression's own | 6.2 |
| the tree's | 5.3 |
| what is around it: the root's kind, the pragmas' bodies, the events | 8.9 |

- **Exact.** No expression's bits differ from the census's code's, on the
  three corpora with the default warnings and with every warning, and on the
  seven fixtures.
- **Half the cost, not a tenth.** The own form is 90 ticks a typed record on
  `sqlite3.c` where the census's code is 174: about 62 cycles a body token.
  Step 2's sum is 1.19 with it, where 1.13 stood: a hair under the gate
  still. The aim was 30 ticks.
- **The cost is instructions, not misses.** The first version sent 1.8% of
  the records to the census's code and still cost two thirds of it: it
  retired some 470 instructions a record where the census's code retires
  780. Each later version cut what a part read (the calls' callee by a bit
  of its type's word, the constants' tests in place, a declaration's three
  facts each made when first asked, the walk only where a parent is read)
  and took 114 ticks to 90. No part is a tenth of the rest.
- **The loop is not its lookups.** Of its 27 ticks, the type's byte, the
  token's role and the operands' bytes, each looked up by an index, are 3.4
  together. The other 23.6 are contiguous columns read once, five tables by
  operator and five stores a record: what 64 records a vector step would
  make (a table is one byte permute of two registers, a list a compressed
  store). That form is not built.
- **Three things are not a pass's to do.** A leaf's two bytes are its
  declaration's and its object's, 15 ticks here: #2850's fact word has the
  declaration in hand when it makes the leaf, and a bit there is free. A
  constant operand's tests read its conversion's type, which the row that
  typed the operator chose: 9.5 ticks here. And the loop reads again what T
  had in registers a moment before. The machine's own form of these is in
  the leaf's maker and in T's rows, not after them.
- **The order doesn't matter.** With the tests after C and X
  (`--front-tests-last`), on what those take, the whole reads 3 to 4 ticks
  less: the tests refuse 1% of what T takes, so C and X run on almost the
  same expressions either way.
- **With #2871's kernel.** This work merged with that pull request's head
  builds and passes both sides' tests, and the operators' kernel reads the
  same there (within 2%, the same instructions).
- **Every fault fails the tests.** Of 98 faults put into the own form one at
  a time, the tests first showed 63 and the three corpora 5 more; 30 showed
  on no input. Sorted, they were:
  - **27 fixture lines owed.** A line of C showed each of 19 at once (a
    comparison under a bitwise operator, a shift by too large a count, a
    chained comparison, a mix of enumerations, a constant outside its
    operand's type). Three more needed the right line: the front's one
    operator outside the census's set (`__extension__`, a selection there),
    a conditional whose third operand alone is a constant its conversion
    changes, and a comparison as a statement whose operand writes. The
    corpora's 5 had no line either.
  - **Why the fixtures missed them:** with `-Wall -Wextra` a comparison and
    a constant's shift go to the census's code whatever their operands are,
    which hides the form's other conditions on them. So the lines are a
    seventh fixture, read with the default warnings alone, and a `for`
    statement in the first.
  - **7 conditions decided before the form runs** (the table above), now
    taken out.
  - **1 that could not fire where it stood:** a register object's flag,
    behind the census's code, whose own register test comes before it asks a
    declaration's attributes, so that such a leaf always went there. It is
    the leaf's first branch now.
  - **Of the 91 left, each fails `test-checker`.**

### The tests on the window

The own form read T's columns in a pass after T: 86 ticks a typed record,
where its aim was 30. Three of its inputs were not a pass's to look up: a
leaf's declaration's bit, a type's byte, and its own loop a record at a
time. #2850's window has them where the record is made ("The front writes
the window" in [the one-core path](one-core-path.md)): the front writes a
record's inputs, and the kernel makes T's record from them in place.
`measure-node-machine --shadow --window-tests` makes the tests there, once
the kernel has made a chunk's records. [The
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/window-tests-2850-2026-11-17)
has the runs, the variants and their patches.

**What changed, part by part:**

| the form's part | as a pass over T's columns | on the window |
| --- | --- | --- |
| a type's byte | a table by the type's index, at every record | six bits of the type's word, which every record has whichever path wrote it |
| a leaf's declaration | its declaration's byte, by the leaf's token, at every leaf | a byte a declaration's line, made at the line's first leaf; a register object by its record's flags |
| the loop | a record a turn: five tables by operator, the operands' bytes by their indexes | 64 records a step: a table is one byte permute of two registers, an operand's byte comes by its distance from this block's bytes and the block before's |
| the lists | three lists of records, a store each a record | a mask a block for the tree's records, the operators with tests and the writes; no list of leaves |
| the set's tests | bits gathered in the loop | an OR of one byte column over the expression's records |
| the tree's tests, the operators', the expression's own | on T's records | the same code, on T's records still |

- **The records' bytes** are read from the kernel's records sixteen at a
  step: a gather each of the word, the flags, the object and the standing,
  one of a name's line's byte, one of the token's role.
- **An expression's part** is its records' range in the window: the set's
  OR, the masks' words aligned at its first record, its tokens' lowest and
  highest by a masked minimum and maximum.
- **One form.** `node_tests_expression` takes what the loop made. The pass
  over T's columns, the leaves' pass and `--front-own-tests` are removed
  (#2924).
- **The reference is the census's code** on T's records at reader time: each
  expression's bits, its root's kind and its tokens' span.

**What still reads T's records.** The tree's tests, the operators' tests and
the expression's own are the own form's code, and T's records are gone by
the time the kernel has run. The shadow has T make each expression's records
again before them and times T alone on the same expressions to take it off.
That second run is scaffolding: it goes when those tests read the kernel's
records, the piece after this one.

On the 9700X (`sqlite3.c`, Lua, musl):

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions the tests ran on | 50,993 | 10,839 | 43,415 |
| their records | 289,838 | 76,304 | 224,326 |
| operands further back than the block before | 970 | 129 | 386 |
| declarations' lines whose byte was made | 18,295 | 6,687 | 36,782 |
| roots whose record is apart from their operand's | 21.2% | 14.8% | 14.8% |
| records sent to the census's code | 0.31% | 1.18% | 0.80% |
| expressions walked for it | 2.06% | 4.11% | 3.01% |
| expressions that differ from the census's code's, with the default warnings, with every warning, and beside `--verify` | 0 | 0 | 0 |

TSC ticks a record of the expressions the tests ran on (medians of seven
readings in turn, musl's of three; the own form at the base in the same
turns, by its sets as before: on `sqlite3.c` it read 86 at its own piece's
build, and 82 to 87 in this piece's four jobs):

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| the own form, a pass over T's columns (main `9a1422eab`) | 83.0 | 112.4 | 158.4 |
| **the tests on the window** | **54.1** | **69.5** | **111.8** |
| of which the records' bytes | 8.9 | 9.8 | 16.6 |
| the loop | 0.7 | 0.9 | 1.9 |
| an expression's part | 7.2 | 7.7 | 13.6 |
| the tests on T's records | 37.2 | 51.1 | 78.8 |
| with every warning (one reading) | 72.6 | 81.1 | 136.4 |
| beside `--verify` (one reading) | 61.3 | 78.8 | 136.2 |

- **Exact.** No expression's bits, root's kind or span differ from the
  census's code's: on the three corpora and the compiler's own source, with
  the default warnings, with every warning and beside `--verify`, and on the
  seven fixtures.
- **No walk for the loop.** The expressions walked and the records sent are
  the own form's, to the expression.
- **The loop is 0.7 ticks a record where it was 27.0.** What it reads is in
  byte columns by then, and nothing in it is a branch on what a record is.
- **Its inputs cost more than it does.** 8.9 ticks a record for the records'
  bytes, of which 5.5 are the gathers (a second run over the same chunk) and
  the rest a first run's: each declaration's line's byte made once by the
  census's code, and the records' lines read where the kernel left them.
- **A third less, not a half.** 54.1 ticks a record where the own form reads
  83.0: 65% of it on `sqlite3.c`, 62% on Lua, 71% on musl. 37.2 of the 54.1
  are tests this piece didn't move.
- **Between the gate and its refutation.** The piece's gate was 40 ticks a
  record on `sqlite3.c` and its refutation 65: 54.1 is neither.
- **musl pays a unit's first uses again.** Its 1,336 units make a
  declaration's line's byte at 16% of the records, and touch the tests'
  arrays for the first time in every unit.
- **Nothing else moved.** Without the tests the window's kernel in place
  reads 15.7 and 15.7 ticks a record, base and piece (Lua 20.2 and 20.2,
  musl 18.7 and 19.0). `-c sqlite3.c`, read twice, is +0.1% and 0.0% in
  wall time and 0.0% both times in user cycles; the base against itself is
  +0.1% in wall time.
- **The front reads more with the same instructions.** 52.4 and 53.3 ticks
  a record on `sqlite3.c` (+1.7%), 69.6 and 71.3 on Lua (+2.5%), base and
  piece; musl's three readings spread 5%. Its function is the same 1,705
  instructions in both builds, not a line of the disassembly apart. Every
  build of the piece has it at one address, 6,064 bytes before the base's
  and 16 bytes further into its cache line.

**What moved it, and what didn't** (`sqlite3.c`, ticks a record; three
readings each):

| the records' bytes | |
| --- | ---: |
| six gathers a step of sixteen records | 9.4 |
| the same, out of line | 9.3 |
| the same, a second run over the chunk | 5.5 |
| a loop a record, its choices written `?:` and `&&` | 22.4 |
| a loop a record written without branches, where clang made one mask a branch on the leaf | 21.0 |
| that loop with the mask opaque to the compiler: no branch on a record's kind | 14.2 |
| the same, out of line | 15.9 |

- **The gathers stay.** A loop a record is slower on the 9700X in every form
  tried. Its four loads of a record's line are the gathers' loads; what it
  adds is a record's work a turn. (Until "The records' bytes without their
  four gathers", below: sixteen records a step still, their columns by loads
  and permutes.)
- **Lists as bits cost the same as lists.** With the tree's records and the
  operators compressed into lists, an expression's part read 9.1 and the
  tests on T's records 36.8. As words of the blocks' masks, read bit by bit
  by the tests, 7.4 and 38.9: the same sum. The words are less code, and no
  store a listed record.
- **A leaf needs no test of its operator.** A name's leaf and a leaf without
  a line are kinds the front gives leaves alone, and a register object's
  flags are on its leaf: the pass reads no operator. It took the bytes from
  9.4 to 8.9.

**What stays scalar, and why:**

- **The tests on T's records:** 37.2 of the 54.1. They are the next piece's,
  on the kernel's records.
- **A line's byte's first making:** once a declaration's line, by the
  census's code's reading of the declaration's attributes: 18,295 on
  `sqlite3.c`.
- **An operand further back than the block before:** a load each, 970 of
  289,838 records on `sqlite3.c` (0.33%).
- **An expression's part:** a few vector steps an expression, 5.7 records
  each on `sqlite3.c`. It is a loop over expressions, 7.2 ticks a record.

**Faults.** 87 were put into the new code, one at a time, in a copy of the
tree: 85 fail the node machine's tests. The other two change no bit a test
can read:

- **Every record's bytes made, a tested expression's or not.** No test reads
  the bytes of a record that is not a tested expression's.
- **A token past the unit's count taken into an expression's span.** The
  front gives every record a token of the unit's. The bound is on an index
  into the events' prefix, and is kept.

The campaign's earlier runs found what the fixtures didn't hold and what the
code did for no result:

- **New fixture lines:** a name's leaf without a line, a register object's
  member, an ordering under a bitwise operator, an address constant the
  record doesn't hold whole, expressions of more than two blocks with their
  test in the last words, an initializer of as many, an aggregate only a
  root's record copies, and an expression a chunk's end cuts.
- **Removed:** a leaf's test of its operator, a root's record's bits for the
  tree's tests and the writes, and the undoing of a root's bytes after its
  tests. Nothing differed without them on the four corpora.

### The constants with no test to make

"The tests on the window" left 37 of its 54 ticks a record to tests that
read T's records. This piece takes those apart, then drops the largest part
that asks nothing new of T: the operators a constant operand sends to tests
that have nothing to say. [The
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/window-tests-second-half-2850-2026-11-17)
has the experiments, the variants and their patches.

**What the tests on T's records are.** An experiment build with switches
that leave one part out; a part is the difference of two settings of the
same executable (ticks a record):

| the tests on T's records | `sqlite3.c` | Lua |
| --- | ---: | ---: |
| all | 38.0 | 52.4 |
| the operators' tests | 19.2 | 30.3 |
| an expression's fixed part: its root's kind, the pragmas' bodies, the events, the lists' frame | 6.3 | 6.5 |
| the tree's tests | 4.0 | 4.0 |
| the root's own | 3.1 | 4.5 |
| the unsequenced test | 1.9 | 3.0 |
| what is left with no call at all: T between other code, the root's bytes | 3.5 | 4.2 |

- **Half is the operators' tests.** Of `sqlite3.c`'s 48,645 listed
  operators, 27,241 are listed for a constant operand alone, 12,918 are
  calls and 8,486 the rest.
- **97% of the constants' tests conclude nothing.** 26,466 of those 27,241
  have no test that fires and send nothing to the census's code; on Lua,
  8,582 of 8,742.

**The rule.** The loop leaves an operator out of its list where it is listed
for a constant operand alone and its operands are of kinds its tests have
nothing to say of:

| the operator | left out where |
| --- | --- |
| `+ - * & \| ^` and their assignments | both operands are plain integers |
| unary `+ - ~` | its operand is a plain integer |
| `== !=` | both are plain integers, and a constant against no constant is zero or 1 to 126; or one is a plain pointer and the other a constant zero |
| `< <= > >=` | both are plain integers, and a constant against no constant is 1 to 126, or zero against a signed operand |
| `<< >>`, but under `-Wshift-sign-overflow` | both are plain integers, a constant count is 0 to 31 and a constant shifted is zero or 1 to 126 |
| `/ %` | both are plain integers, and a constant divisor is 1 to 126 |
| `=` | both are plain integers, and the constant stored is zero or 1 to 126 |
| a return's conversion | its operand is a constant zero or 1 to 126, and its own type is a plain integer's |

- **A plain integer** has an integer's type of 8 to 64 bits (no bool,
  enumeration, `_BitInt` or atomic one) and is no bit-field's value.
- **A plain pointer** points to no void.
- **What stays listed:** a subscript by a constant (its array's length is
  asked), pointer arithmetic, a null stored in a pointer, a constant of any
  other value or type, and every operator another warning lists whatever its
  operands are (a comparison under `-Wsign-compare`, an assignment under
  `-Wself-assign`). "The casts, a pointer's subscripts and its sums" later
  takes a plain pointer's subscript and its sum out.

**How it is made:**

- **A record says its constant's value.** The rule asks whether a constant
  is zero, 1 to 31 or 1 to 126, and the value is the record's payload: one
  more gather. A record's exponent, which only a floating value used, now
  says it: the payload and one, 0xffff for a payload past 0xfffd. It is
  written where the record is made (`node_facts_small`: a fold's, a truth's,
  a size's, a store's and a conversion's, a literal's line and an
  enumerator's; a plain copy of an operand's value says what the operand
  says). The standing's gather has it, in the same four bytes. T's own
  record says none, and reads as a constant of any value.
- **A kind a record.** The records' bytes' pass puts the record's kind as an
  operand in three spare bits of its value's byte: none; a plain pointer; a
  plain integer that is no constant, signed or unsigned; an integer constant
  that is zero, 1 to 31, 32 to 126, or any other. A plain integer's type is
  one range of its word's low three bytes: an integer's kind, no fact, a
  class of 8 to 64 bits. No column is added.
- **One table by the pair.** The rule is a table of 64 bytes by the first
  operand's kind and eight times the second's: the classes of operator that
  pair is left out for (`node_tests_quiet_pairs`). The loop reads each
  side's value's byte by its distance, as it reads the operands' other
  bytes, and the table by one byte permute. An operator's class is a bit,
  from one more table by operator. A return's conversion's class is the bit
  that says its own type is a plain integer's.
- **Checked where the reader verifies.** With `--verify` each operator left
  out has its tests run all the same, and they must find nothing and send
  nothing. Each value a record says is compared with its payload, by the
  gather the rule does without. Either fails the command.

The counts, on the 9700X (the compiler's own source in the cloud
container):

| | `sqlite3.c` | Lua | musl | the compiler's own source |
| --- | ---: | ---: | ---: | ---: |
| operators the loop lists, main | 48,645 | 16,513 | 36,260 | 35,075 |
| with the rule | 27,750 | 9,486 | 21,783 | 12,412 |
| with every warning, main | 62,602 | 19,138 | 45,924 | 45,734 |
| with the rule | 52,451 | 13,420 | 35,327 | 34,745 |
| operators left out with a test that fires or a record sent, beside `--verify` | 0 | 0 | 0 | 0 |
| integer constants whose record says its value | 46,532 | 16,700 | 30,664 | 41,190 |
| of them not their payload's | 0 | 0 | 0 | 0 |

TSC ticks a record of the expressions the tests ran on, main `aa09d1a04`
and the piece in turn on the 9700X (medians of seven readings, musl's of
fifteen; with every warning of five, musl's of fifteen):

| | `sqlite3.c` | | Lua | | musl | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| | main | the piece | main | the piece | main | the piece |
| **the tests on the window** | 54.7 | **48.7** | 70.0 | **63.2** | 111.9 | **108.7** |
| of which the records' bytes | 9.0 | 9.3 | 9.7 | 10.4 | 16.9 | 17.9 |
| the loop | 0.7 | 0.9 | 1.0 | 1.1 | 2.2 | 2.4 |
| an expression's part | 7.2 | 7.4 | 7.6 | 7.7 | 13.1 | 12.7 |
| the tests on T's records | 37.8 | 31.0 | 51.6 | 43.9 | 80.3 | 75.4 |
| with every warning | 73.0 | 70.1 | 81.7 | 76.9 | 131.1 | 130.4 |
| beside `--verify` (one reading) | 61.7 | 71.2 | 79.1 | 87.2 | 127.6 | 139.5 |

- **Exact.** No expression's bits, root's kind or span differ from the
  census's code's, no operator left out has a test, and no value said is
  another than its payload: on the three corpora and the compiler's own
  source, with the default warnings and with every warning, beside
  `--verify`, and on the eight fixtures.
- **6.0 ticks a record less on `sqlite3.c`,** 6.8 on Lua, 3.2 on musl: 11%,
  10% and 3% of the tests.
- **The rule's inputs cost a tenth of what it saves on `sqlite3.c`.** The
  tests on T's records fall by 6.8; the bytes rise by 0.4, the loop by 0.1
  and an expression's part by 0.2, its sweep's source unchanged.
- **With every warning: 2.9 less on `sqlite3.c`,** 4.8 on Lua, 0.6 on musl.
  `-Wsign-compare` and `-Wself-assign` list every comparison and assignment
  whatever its operands are, and on `sqlite3.c` the rule then leaves out
  half as many operators.
- **musl is read with its spread.** Fifteen readings of one build spread
  11% to 16% there, and one executable's reading moves by as much with the
  size of the environment alone, which moves the stack: over twelve sizes
  it read 108.0 to 121.2. Three builds of this form read 0%, 1% and 3%
  under main with the default warnings, and 1%, 3% and 0% with every one.
- **The kernel reads the same.** Without the tests the window's kernel in
  place reads 15.6 and 15.7 ticks a record on `sqlite3.c`, main and piece
  (Lua 20.2 and 20.2, musl 18.9 and 18.7). `-c sqlite3.c`, read twice, is
  -0.07% and -0.01% in wall time and +0.15% and -0.01% in user cycles; main
  against itself is -0.08% and +0.04%, +0.05% and +0.03%.
- **The gate is still ahead.** 48.7 where the gate is 40.

**What moved it, and what didn't** (`sqlite3.c`, ticks a record; each form
in turn with main, which read 54.3 to 54.7):

| the rule's inputs, and its form in the loop | the tests | the records' bytes | the loop | an expression's part |
| --- | ---: | ---: | ---: | ---: |
| main, without the rule | 54.3 to 54.7 | 8.9 | 0.8 | 7.2 |
| a constant's payload by two masked gathers; the rule as masks | 53.1 | 11.9 | 1.2 | 7.8 |
| one gather, of the constants of types of 32 bits at most | 51.4 | 10.8 | 1.2 | 7.8 |
| a literal's value from the front's column, a gather for the others | 51.9 | 11.1 | 1.1 | 7.9 |
| the kernel writes three tests' bytes a record, one gather for them | 48.3 | 6.9 | 1.2 | 7.9 |
| the value said in the record's exponent, no gather | 50.3 | 9.7 | 1.1 | 7.9 |
| the same, the rule one table by the pair, the verify's words out of the sweep | 50.2 | 9.3 | 0.8 | 7.3 |
| the same, the kind in the value's byte and no column of its own | 48.7 | 9.3 | 0.9 | 7.4 |

- **A masked gather costs what a gather costs:** 1.3 to 1.5 ticks a record,
  for a field 7% of the records have.
- **The kernel can't write the bytes for less.** With a record's three
  tests' bytes made as the kernel makes the record, the bytes' pass read 6.9
  for 8.9, and the kernel in place 19.5 for 15.6: some forty operations a
  record on its critical path, where the pass does the same for sixteen
  records at once. The names' pass rose by 2.2 ticks a name too.
- **The value said costs the kernel nothing it can read:** a compare, a
  select and an add where it writes a constant's record, in the quadword it
  writes anyway.
- **The rule as masks doubled the loop on musl** (2.0 to 4.2 ticks a
  record): fourteen byte tests and some forty mask operations a step, on a
  corpus whose chunks are short. As one permute by the pair it reads 2.4.
- **A flag tested in the sweep cost 0.6 ticks a record.** The verify's two
  branches in an expression's part, never taken in a timed run, read +0.6 to
  +0.7 in every build that had them. In a loop of their own after the sweep
  they read +0.1 to +0.2.
- **A column of its own cost musl 2 ticks a record.** One more byte column
  is one more array cleared a unit and one more stream stored and loaded,
  cold in each of musl's 1,336 units: with every warning its fifteen
  readings read 134.0 with the column and 128.1 without, where main read
  129.7.

**What stays scalar, and why:**

- **The pairs' table's making:** 64 entries a unit, the rule written once as
  conditions on two kinds.
- **The check of an operator left out:** the tests' own code, under
  `--verify` alone.

**Faults.** 131 were put into the new code, one at a time, in a copy of the
tree: 129 fail the node machine's tests. The other two change no result a
test can read:

- **The second operand's kind read only where the first one's is near.** A
  second operand of an operator the rule takes is the record before it, and
  is never before the block before.
- **The store's slow rule's converted constant saying its operand's value.**
  The window's kernel makes such a record by the store's fast path, which
  says the converted value. The slow rule makes one in the reads' census
  alone, which reads no value said.

The campaign's three earlier rounds found what the fixtures didn't hold and
what the code did for no result:

- **New fixture lines,** one for each class the rule leaves out and for what
  it must keep listed: a constant past a bound (126 and 127, 200, a 64-bit
  one), zero against an unsigned operand and against a signed one, a shift
  by 0, by 3 and by 32, a division by 2 and by zero, a constant first,
  pointer arithmetic, a pointer against 0 and against 1, a bool, a
  bit-field, an enumeration, a 128-bit operand, a floating one, a store of
  0, of 5 and of 300 into a char, and returns of 0, 1 and 300 into an int, a
  char, a float and a pointer.
- **An eighth fixture:** no warning of a mix of enumerations, and
  `-Wassign-enum`: an enumeration's constant is then listed for itself
  alone, and must stay listed.
- **Removed:** an initialization's class (its root is no record of the
  window's); the six type bits beside the type's kind, a constant's kind
  beside its type's, and the wide standing's test beside the value said; a
  unary class's test of a second operand; a pointer's constant kind; and
  where a value is said, the test that its record is a constant's. Nothing
  differed without them on the four corpora.

### The operator above, and an expression with nothing listed

"The constants with no test to make" left 31 of the window's 48.7 ticks a
record on `sqlite3.c` to the tests that read T's records, and those ran on
every expression. This piece takes two things out of them: the tree's tests'
list, and the tests themselves where an expression has nothing for them.
[The
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/window-tests-listing-2850-2026-11-18)
has each form's patch and readings.

**The tree's tests, counted.** A test of the parser's tree reads its
record's token's parent's role, and nearly every one ends there. The loop
listed every record such a test reads: a comparison's token, `!`, `=`.

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| records listed for the tree's tests | 27,794 | 4,844 | 17,871 |
| of them, the parent's role lets a test fire | 1,890 | 318 | 951 |
| operators listed by the operator above, below | 6 | 0 | 34 |

Of `sqlite3.c`'s 1,890, 1,182 are an `=` in a `for`'s head and 708 a
comparison under a `;`: the role lets the test go on, and its next question
ends it.

**By the parents' roles: exact, and no gain.** The first form kept the list
and asked the role in the records' bytes' pass: the listed records
compressed, sixteen a step, a gather of their tokens' parents and one of
those parents' roles, and two more for an `=` under a terminator. It read,
in turn with the piece before:

| ticks a record | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| the tests on the window, before | 48.8 | 63.2 | 109.7 |
| with the parents' roles | 48.5 | 63.1 | 112.0 |
| of which the records' bytes, before | 9.3 | 10.4 | 18.3 |
| with the parents' roles | 11.3 | 12.5 | 23.8 |
| the tests that read T's records, before | 31.2 | 44.1 | 75.0 |
| with the parents' roles | 29.1 | 41.9 | 71.1 |

- **The pass costs what the tests it saves did:** 2.0 ticks a record more in
  the bytes for 2.1 less on T's records, on `sqlite3.c`.
- **A chunk has ten of them.** Of some hundred records a chunk, ten are
  listed: a step of sixteen is never full at a chunk's end, and what each
  costs is its two loads, which the scalar test paid too.

**By the operator above.** A test fires in four places, and in each the
parent that the role is asked of is the token of the record's consumer:

| the test | the record | its token's parent | its consumer |
| --- | --- | --- | --- |
| a comparison under a bitwise operator, no parentheses | the comparison | `&`, `\|`, `^` | the bitwise operator, either operand |
| `!` before a comparison | `!` | the comparison | the comparison, its first operand |
| `=` as a condition | `=` | a condition's `)`, a `for`'s condition's `;` | the condition's root, which the reader puts at that token |
| a comparison discarded | the comparison | an expression statement's `;` | the discard, the reader's cast to void at that `;` |

So the loop lists the consumer, by what its operands are, and no token's
parent is read to make the list:

- **Three tables by operator, one permute.** What an operator is as a first
  operand to these tests, `!` or `=` (`own_tree_marks`); what a first
  operand must be to list its consumer, `!` for a comparison and `=` for a
  condition (`own_tree_firsts`); and the operand properties that list it, a
  comparison for `&`, `|`, `^` and for a cast (`own_tree_sends`). The first
  operand's mark comes by its distance as its other bytes do, and the
  comparison's property was the loop's already.
- **The list is a superset, and the test decides.** A comparison in
  parentheses under `&`, `(void)` of one, `!` in parentheses: the consumer
  is listed, the record's token's parent is then read
  (`node_tests_expression`, for the listed operator's operands), and the
  test has nothing. The operators are listed whatever warnings the unit
  enables: they are few, and the test reads the switches.
- **An operand further back than the block before** has its mark by a load,
  as its other bytes have.
- **A root is listed too.** The discard and the condition are roots'
  records: an expression's lists take their bit, as they took the root's
  operators' bit.
- **The role's gather is gone.** The records' bytes' pass read each record's
  token's role for this list alone: 0.8 ticks a record on `sqlite3.c`.

**An expression with nothing listed.** The tests that read T's records had
a call an expression, whatever it held. An expression is now listed for
them only where one can have something:

- an operator in one of its two lists;
- two writes, or one under its top, under `-Wunsequenced`;
- an initialization, whose root's tests are its object's type's;
- a statement whose top is no write: it may be a value without an effect.
  Its root is the front's discard, the one root it makes with a cast's
  operator, so no token's role is read to tell it from an explicit cast;
- a return of an address, by its set's bits, under
  `-Wreturn-stack-address`;
- a warning of T's actions, which the kernel's reader keeps a bit an
  expression for.

One with nothing listed has its verdict from the window alone: its set's
bits; the control's bit of a root inside a statement expression; the bit of
a warning no test stands for; under `-Wfor-loop-analysis`, a `for`'s
clause; a pragma's body around its root; and a warning event at its tokens,
asked only where its span meets the bounds of the unit's events so far.
Neither T nor the tests run on it.

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions the tests ran on | 50,993 | 10,839 | 43,415 |
| with nothing listed | 21,157 | 3,069 | 21,925 |
| share | 41% | 28% | 51% |
| with every warning | 8,793 | 1,302 | 15,146 |
| share | 17% | 12% | 35% |

**Checked where the reader verifies.** With `--verify`:

- each record the tree's tests read that no listed operator has as an
  operand has its test run all the same, and must have none (27,788, 4,844
  and 17,837 checked on the three corpora);
- each expression with nothing listed is run through the tests that read
  T's records all the same, and must have the same bits, root's kind and
  span;
- no list has a bit past its expression's records.

Any of the three fails the command.

**Measured.** The 9700X, CPU 2, under the lock a run, Release; the piece
before (#2955's sources on main `aa09d1a04`) and this one in turn. TSC ticks
a record of the expressions the tests ran on; medians of seven readings,
musl's of fifteen; with every warning of five, musl's of fifteen.

| | `sqlite3.c` | | Lua | | musl | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| | before | the piece | before | the piece | before | the piece |
| **the tests on the window** | 48.9 | **42.0** | 63.3 | **59.1** | 111.3 | **100.0** |
| of which the records' bytes | 9.3 | 8.5 | 10.4 | 9.4 | 18.0 | 18.1 |
| the loop | 0.8 | 0.9 | 1.1 | 1.2 | 2.6 | 2.5 |
| an expression's part | 7.4 | 9.5 | 7.7 | 9.9 | 13.9 | 16.7 |
| the tests on T's records | 31.3 | 23.2 | 43.9 | 38.6 | 76.1 | 61.9 |
| with every warning | 70.2 | 67.3 | 76.6 | 74.3 | 128.8 | 122.3 |
| beside `--verify` (one reading) | 71.2 | 80.2 | 87.2 | 98.0 | 139.5 | 170.7 |

- **6.9 ticks a record less on `sqlite3.c`, 4.2 on Lua, 11.4 on musl:** 14%,
  7% and 10% of the tests.
- **musl is read with its spread.** Its fifteen readings run from 97.6 to
  105.2, and the piece before's from 107.3 to 120.4. This form's build
  before its last change read 102.4 there, for 109.3.
- **The listing costs every expression 12 ticks, and saves some 80 at each
  one with nothing listed** (`sqlite3.c`): an expression's part rises by 2.1
  ticks a record, and the tests on T's records fall by 8.1, of which 2.1
  were the tree's tests' calls (what the form by the parents' roles took
  off them).
- **With every warning: 2.9, 2.4 and 6.5 less.** `-Wsign-compare` and
  `-Wself-assign` list every comparison and assignment, and a sixth of
  `sqlite3.c`'s expressions are then left with nothing listed.
- **The verify checks more, and reads more:** it runs the tests on every
  expression, and the tree's test of every record that has one.

**What moved it, and what didn't.** `sqlite3.c`, ticks a record; each form
in turn with the piece before, which read 48.5 to 48.9:

| the form | the tests | the records' bytes | an expression's part | the tests on T's records |
| --- | ---: | ---: | ---: | ---: |
| the piece before | 48.5 to 48.9 | 9.3 | 7.3 to 7.4 | 31.0 to 31.3 |
| the tree's list by the parents' roles | 48.5 | 11.3 | 7.5 | 29.1 |
| by the operator above; nothing listed, no call: the listing with branches | 46.6 | 8.4 | 12.9 | 24.2 |
| the same with no branch, the expressions to call as a list | 45.2 | 8.5 | 13.0 | 22.7 |
| the listing trimmed | 43.5 | 8.5 | 10.0 | 24.1 |
| a `for`'s clause asked of the unlisted alone | 42.7 | 8.5 | 9.8 | 23.5 |
| T's warning a bit of the kernel's reader's byte, the events' intake out of line | 42.3 | 8.5 | 9.7 | 23.1 |
| a statement's root's token's role not read: this piece | 42.0 | 8.5 | 9.5 | 23.2 |

- **It was not the branches.** The listing first cost 5.6 ticks a record,
  with a branch on each condition (12.9) or with none (13.0).
- **What trimmed it.** An expression's events are asked only where its span
  meets the bounds of the unit's events so far, two compares for two words
  read and masked; its lists are OR-ed in the loop that makes them; what
  the report counts is counted after the timed sweeps; and a statement
  expression's bit is read only in a unit that has one.
- **The tests on T's records move by a tick between builds** that list the
  same expressions (24.2, 22.7, 24.1, 23.5, 23.1, 23.2): T's own time is
  taken off them, and it read 32.1, 30.8, 29.9, 30.6, 30.5 and 30.6.
- **Under `-Wfor-loop-analysis` a clause's test is two loads,** a token's
  parent and its role: asked of every expression they cost what the piece
  saved with every warning (69.9 before, 70.2 with), and asked of the
  unlisted alone they read 67.9.
- **A statement's root's token's role was read for nothing.** The listing
  told the reader's discard from an explicit cast by that role, as the
  census's code does on T's records. The front makes a root with a cast's
  operator at a terminator alone, and the three terms changed no result
  (the faults below): without the load an expression's part reads 9.5 for
  9.7.

Not this piece's to change, and read beside it (before, then the piece):

- **The window's kernel in place, without the tests:** 15.7 and 15.8 ticks
  a record on `sqlite3.c`, 20.2 and 20.2 on Lua, 18.8 and 18.9 on musl.
- **The front with the window: 53.0 and 53.0** on `sqlite3.c`, 70.5 and
  70.4 on Lua, 87.2 and 89.3 on musl (its three readings 88.0, 89.3 and
  93.1, for 87.2, 87.2 and 90.3), once a hook of the tests was out of its
  code. The reader's intake of a chunk's warning events was inlined into
  the front's own function. Ten instructions more there (the events'
  bounds) moved that function's stack slots and the code after them, and in
  six builds the front's ticks read 53.4 to 53.8 where the piece before
  read 52.7 to 52.8 on `sqlite3.c`, and 70.9 to 71.8 for 70.0 to 70.5 on
  Lua, the same front without the window unchanged. The intake is now a
  function of its own (`node_census_reader_chunk`).
- **`-c sqlite3.c` as whole processes** (21 runs of each of three copies,
  twice): wall -0.07% and -0.05%, user cycles -0.04% and -0.08%, where the
  piece before against itself reads -0.24% and -0.14%, -0.04% and -0.15%.

**What stays scalar, and why:**

- **An expression's listing and verdict,** in the sweep that makes its set
  and its lists, one expression at a time: 12 ticks an expression on
  `sqlite3.c`. Its inputs are four loads at its root (the statement
  expression's bit, the operator's kind, T's warnings' byte, the root's two
  masks' bits). As gathers of sixteen expressions they are 1.3 to 1.5 ticks
  an element each, what "The constants with no test to make" measured: five
  to six ticks an expression for the loads alone.
- **The tree's tests of a listed operator's operands:** 6, 0 and 34
  operators on the three corpora.
- **The three tables' making,** 128 entries each a unit.
- **The check of what is left out:** the tests' own code, under `--verify`
  alone.

**Faults.** 119 were put into the new code, one at a time, in a copy of the
tree: 117 fail the node machine's tests. The other two change no result a
test can read:

- **The events' lower bound started at zero.** More expressions then ask
  their events, and find the same.
- **An empty span's guard in the events' words.** A span with no token of
  the unit reads the first word's top bit and its lowest, which no mask
  shares.

The campaign's three earlier rounds found what the fixtures didn't hold,
what the verify didn't check and what the code did for no result:

- **New fixture lines** (the seventh fixture's function `t`): the other
  operator's mark where an operator asks for one (`(a = b) == c`,
  `if (!a)`), an initializer whose top is listed, in each list, a warning
  event in each word of an expression's span, and an address taken outside
  a return.
- **A ninth and a tenth fixture:** the warnings that list an expression,
  off; and a warning no test stands for.
- **The events' lower bound at its equality.** A return with nothing listed
  can end at the unit's first warning event; a statement can't, whose last
  token is its terminator. The ninth fixture's first function is one.
- **A list's range,** which the verify now checks: a bit past an
  expression's records.
- **A statement's root's token's role, read for nothing.** The listing told
  the reader's discard from an explicit cast by that role, as the census's
  code does on T's records, and three faults in those terms changed no
  result. The front makes a root with a cast's operator at a terminator
  alone (`CHECK_INPUT_FORM_DISCARD`): the terms and their load are gone.

**Against its gates.** The pieces were specified on the issue before their
code:

- **The tree's tests under 1.0 tick a record on `sqlite3.c`, for a pass of
  its own under 1.0: missed by the form specified, met by the other.** The
  parents' roles' pass read 2.0. By the operator above there is no pass: the
  loop reads under 0.1 more, the bytes 0.8 less, and the tree's tests are 6
  calls.
- **Both pieces, 40 on `sqlite3.c`, refuted at 44: neither.** 42.0.
- **An expression with nothing listed: fewer than counted for.** 57%, 39%
  and 62% of the expressions have no listed operator and no write out of
  sequence; 41%, 28% and 51% have nothing listed, once an initialization's
  root, a statement whose top is no write and a return of an address keep
  their call.
- **What is left on T's records** is 23.2: the calls' tests first, 12,918
  of the 27,750 operators still listed.

### The calls with no test to make, and a statement that discards nothing

"The operator above, and an expression with nothing listed" left 23.2 of the
window's 42.0 ticks a record on `sqlite3.c` to the tests that read T's
records, on the 59% of its expressions that were listed for them. This piece
takes out the calls, and the statements listed for their top alone. [The
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/window-tests-calls-2850-2026-11-18)
has each form's patch and readings.

**What was listed, counted.** Before any code, on the piece before's sources
(default warnings; with every warning in parentheses):

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions the tests run on | 50,996 | 10,839 | 43,415 |
| listed for the tests that read T's records | 29,839 (42,203) | 7,770 (9,537) | 21,490 (28,269) |
| listed calls | 12,921 | 4,182 | 8,586 |
| of them, with a test that finds nothing and sends nothing | 12,904 | 3,971 | 8,541 |
| statements listed because their top is no write | 12,769 | 3,233 | 2,746 |
| of them, the top a cast to void | 5,667 | 573 | 31 |
| of them, the top a call | 6,619 | 2,186 | 2,069 |
| of those calls, the callee asks for its result | 0 | 1 | 0 |

A call was listed whatever it was, and `sqlite3.c`'s `assert`s are
`((void)0)`, three records listed because a statement whose top is no write
may be a value without an effect.

**The rule.** A call's test has four things to find: an aggregate result or
argument, its callee's own semantics, a constant argument its parameter's
type changes, and an enumeration under the mix's warnings. The loop leaves a
call out of its list where:

| | the call is left out where |
| --- | --- |
| its own type | is no aggregate's |
| its callee | is a name whose declaration gives a call no semantics of its own (no printf-family name, none of the attributes the call's test asks about), or no name at all |
| its arguments | have no aggregate among them, no constant but an address's or an integer's of zero or 1 to 126, and none that may be a null pointer constant that is no literal: an integer constant without the literal's flag whose own parameter's conversion's type is a pointer's |
| its callee's type | takes such an integer unchanged in each parameter: none is an enumeration's, a floating type's, or an integer's but a plain one of 8 to 64 bits |
| its operands' properties | send it nothing |

A unit with `-Wtype-safety` keeps every call listed: its test has something
for each. And a statement is listed for its own test where its top is no
write, has a type that is not void, and is no call left out whose callee asks
for no result. The census's code asks the same of a discarded value: one of
type void is none, a cast or not.

**Where its inputs are.** Each is made where its record is read already:

- **The arguments, in the kernel's call rule's short path.** It reads each
  argument's record for its flags and standing, and gathers three more
  words there (`node_facts_argument`): the kinds of constant among them, a
  bit each; whether an integer constant among them is not zero or 1 to 126,
  by what its record says of its value; and their types' words together,
  for an aggregate's bit. The call's record then says 1 in its exponent
  (`BUSTER_NODE_FACTS_QUIET_CALL`); a call's record says 0 or 1 there. The
  first four arguments are read with no branch, as they were. A call the
  rule makes after its short path refused it says nothing, and stays
  listed: seven on Lua, none on the two others.
- **An argument against its own parameter, in the calls' second pass.**
  Whether an argument may be a null pointer constant that is no literal is
  a pair's to say, the argument's record and its parameter's conversion's
  type, and the arguments' rule has both: its answer has a bit for it
  (`BUSTER_NODE_FACTS_ARGUMENT_QUIET`), the answers' key has the literal's
  flag, and the loop takes the call's 1 away where an argument's answer
  lacks the bit.
- **What each parameter takes, a bit of the callee's type's row**
  (`BUSTER_NODE_FACTS_TAKES`), made with the row, once a function type.
- **The callee's own semantics, a bit of its declaration's line's byte,**
  made at the line's first leaf where the leaf's type's row says it calls,
  from the census's code's fact of the declaration. A name's leaf has it in
  its value's byte, and a call reads its first operand's byte, as the quiet
  rule reads its operands' kinds.
- **The calls' rule in the loop** is then masks of a block's bytes: the
  call's operator, its type's byte's two bits, its first operand's byte's
  bit, and what its operands send it.
- **A statement's top, a mask a block:** the records that as a statement's
  top leave its test nothing, a write, a record of type void and a call
  left out. An expression's listing reads its top's bit.
- **Whether a callee asks for its result: the asks.** That is its
  declaration's fact too (`warn_unused_result`, `const`, `pure`, or a
  builtin's name), and it is asked only of a call a statement may discard. A
  statement's discard is a cast to void over its top: the loop compresses
  the numbers of the casts to void whose first operand is a call left out,
  in its block or the one before, into a list, a half block a step, and one
  counted pass reads each one's callee's line's byte
  (`node_tests_window_asks`). The fact is made at the line's first ask and
  kept in the byte. A call whose callee asks is cleared from the top's
  mask: one call in the three corpora. So is a call whose discard is
  further back than the block before, a `for`'s last clause's after a long
  body: no ask reaches it, and its statement stays listed.

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| calls the loop leaves out | 12,064 | 3,509 | 7,227 |
| calls still listed | 854 | 673 | 1,359 |
| casts to void over a call left out, the asks' list | 6,145 | 1,721 | 1,974 |
| declarations asked whether they want their result | 1,144 | 740 | 1,361 |
| operators listed, before | 27,750 | 9,486 | 21,783 |
| with the piece | 15,686 | 5,977 | 14,556 |
| expressions with nothing listed, before | 21,157 | 3,069 | 21,925 |
| with the piece | 35,069 | 5,731 | 26,760 |
| share | 69% | 53% | 62% |
| with every warning, before | 8,793 | 1,302 | 15,146 |
| with the piece | 20,333 | 3,594 | 17,622 |
| share | 40% | 33% | 41% |

**Checked where the reader verifies.** With `--verify` a call left out has
its test run all the same, and must find nothing and send nothing; and it is
asked again, from T's records and its own list of arguments, whether an
argument is a constant the rule doesn't take (`node_tests_call_loud`: the
test finds an aggregate, and finds nothing at 127), and must have none. An
expression with nothing listed is run through the tests all the same, as
before, which checks a statement's top. Either fails the command.

**Measured.** The 9700X, CPU 2, under the lock a run, Release; main
`8d4025fbf` and this piece on it, in turn. TSC ticks a record of the
expressions the tests ran on; medians of seven readings, musl's of fifteen;
with every warning of five, musl's of fifteen.

| | `sqlite3.c` | | Lua | | musl | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| | before | the piece | before | the piece | before | the piece |
| **the tests on the window** | 41.8 | **35.8** | 58.0 | **53.0** | 98.0 | **96.4** |
| of which the records' bytes | 8.5 | 9.3 | 9.4 | 10.7 | 16.2 | 18.6 |
| the loop, with the asks | 0.9 | 1.7 | 1.1 | 2.3 | 2.3 | 4.4 |
| an expression's part | 9.3 | 9.7 | 9.6 | 9.7 | 16.6 | 16.6 |
| the tests on T's records | 23.1 | 15.1 | 37.8 | 30.2 | 63.1 | 55.9 |
| with every warning | 67.7 | 63.8 | 73.9 | 70.8 | 120.6 | 121.1 |
| beside `--verify` (one reading) | | 85.5 | | 103.4 | | 172.5 |

- **6.0 ticks a record less on `sqlite3.c`, 5.0 on Lua, 1.6 on musl:** 14%,
  9% and 2% of the tests.
- **A second build of the same sources** (with one string more) read 35.6
  for 42.0, 53.2 for 58.0 and 96.3 for 100.0.
- **A third, on the main the pull request was opened on** (`5fa4e76c3`, two
  merges of Tier 1's certificate later; that main and the piece in turn):
  35.6 for 42.2, 53.0 for 58.1 and 97.7 for 104.5; with every warning 63.0
  for 67.4, 70.6 for 74.2 and 124.9 for 128.3.
- **musl is read with its spread, and by its chunks.** Its fifteen readings
  run from 92.6 to 105.5, and main's from 95.6 to 106.6. Its 43,415
  expressions come in 2,975 chunks of two blocks, each after another unit's
  compile, and each sweep reads twice `sqlite3.c`'s ticks a record there
  (the records' bytes 18.6 for 9.3). In one executable, with the rule on
  and off and its inputs made either way, the tests read 35.8 for 44.5 on
  `sqlite3.c`, 54.3 for 62.5 on Lua and 102.5 for 111.4 on musl.
- **With every warning: 3.9 and 3.1 less, and nothing on musl.**
  `-Wsign-compare` and `-Wself-assign` list every comparison and
  assignment: two fifths of `sqlite3.c`'s expressions are then left with
  nothing listed, for one sixth before.

Not this piece's to change, and read beside it (main, then the piece):

- **The window's kernel in place, without the tests: 15.6 and 16.4** on
  `sqlite3.c`, 20.2 and 20.9 on Lua, 18.7 and 19.2 on musl: the arguments'
  part of its call rule's short path. An earlier build of this form read
  16.0 for 15.7.
- **The names' pass:** 22.6 and 22.6 ticks a name, 93.1 and 94.5, 68.3 and
  67.8.
- **`-c sqlite3.c` as whole processes** (21 runs of each of three copies,
  twice): wall -0.08% and -0.10%, user cycles -0.05% and -0.03%, where main
  against itself reads -0.10% and -0.02%, +0.01% and +0.04%.
- **The front with the window read 71.7 for 52.8 from this build's file,**
  91.9 for 71.2 on Lua and 102.0 for 90.8 on musl, **and that was the
  file's, not the build's.** Four fresh copies of the same executable read
  52.8, and so did the file itself once its pages were dropped from the
  page cache and read back; through a hard link, the same pages, it read
  71.7. Its code is main's at main's addresses and the stack's place
  doesn't move it. In the slow file's runs the function's ops are the
  same, miss no more and retire sooner after they enter (27.0 cycles for
  30.3), so they arrive more slowly, and its fetches miss the op cache at
  102 of 3,282 for 59 of 3,647; no counter of the whole process tells the
  two files apart. The second and the third build read 52.8, as any other
  file did. This section first put it down to where read-only data sat:
  wrong, and corrected with
  [#2967](https://github.com/buster14a/buster/issues/2967), which has the
  runs. A job that reads the shadow now runs each build from a fresh copy
  a reading.

**What moved it, and what didn't.** Ticks a record; each form in turn with
the piece before (#2960's sources on main `aa09d1a04`), `sqlite3.c` by its
sweeps:

| the form | `sqlite3.c` | the records' bytes | the loop | an expression's part | the tests on T's records | Lua | musl |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| the piece before | 41.7 to 42.1 | 8.5 | 0.8 | 9.4 | 23.0 to 23.2 | 58.4 to 58.7 | 96.3 to 99.9 |
| as specified: the callee's facts with its parameters a line's bit, a loud argument marking its consumer | 37.8 | 11.6 | 1.8 | 9.4 | 15.0 | 59.7 | 112.5 |
| the arguments in the kernel's call rule; a callee's result asked in the loop, a call from it | 36.5 | 9.3 | 3.2 | 9.7 | 14.3 | 55.7 | 104.2 |
| asked in the listing, of every expression's top, with a branch on the callee's kind | 37.5 | 9.1 | 1.0 | 12.1 | 15.1 | 55.8 | 99.2 |
| the same with no branch | 37.2 | 9.2 | 1.0 | 12.0 | 14.9 | 54.3 | 94.4 |
| the loop marks the casts over a call left out; a pass over each block's marks | 36.0 | 9.3 | 2.2 | 9.4 | 15.2 | 54.0 | 102.4 |
| the marks compressed into a list, one counted pass | 35.2 | 9.2 | 1.7 | 9.4 | 15.0 | 53.4 | 94.1 |
| its mask written for a callee that asks alone | 35.4 | 9.2 | 1.6 | 9.6 | 14.8 | 53.4 | 95.8 |
| a record of type void discards nothing; the asks' list is the casts to void near their call; the rule's own path says nothing: this piece, against main `8d4025fbf`, which read 41.8, 58.0 and 98.0 | 35.8 | 9.3 | 1.7 | 9.7 | 15.1 | 53.0 | 96.4 |

- **As specified, its inputs cost more than the rule saved on Lua and
  musl.** The specification put the callee's facts and its parameters'
  types in its declaration's line's byte, and had each loud argument mark
  its consumer by the front's distances. One executable with each part
  behind a switch read, in ticks a record on `sqlite3.c`, Lua and musl: the
  fact of a result asked for, made for every function's line, 0.8, 3.6 and
  7.8 in the records' bytes; the parameters' scan 0.6, 1.8 and 4.0; the
  line's type's row 0.5, 1.1 and 2.0; the marking 0.8, 1.1 and 2.3 in the
  loop. The rule itself took the tests on T's records from 22.3, 37.5 and
  61.5 to 14.8, 30.3 and 52.1.
- **So each input moved to where its record is read.** The arguments to the
  kernel's call rule, which reads each one's record; the parameters to the
  callee's type's row, once a type; and the result's fact to the statements
  that may discard a call, made when one asks.
- **A call from inside the loop cost every block.** With the ask called
  from the loop, at the calls a cast follows, the loop read 3.2 ticks a
  record on `sqlite3.c`, 5.5 on Lua and 10.5 on musl, for 1.0, 1.3 and 3.1
  without it.
- **Asked of every expression, it cost every expression.** Four loads at
  each expression's top's first operand, with a branch on what it is or
  with none: 2.7 ticks a record more in an expression's part, where the
  facts' making was 0.1 (1,130 asks at 28 ticks each on `sqlite3.c`, 31 on
  Lua, 60 on musl).
- **A pass over each block's marks mispredicts its own loops:** 47 ticks a
  cast on `sqlite3.c`, 6,471 casts in 5,000 blocks. The same casts as one
  list read 27, and 0.6 ticks a record.
- **musl moves between builds that list the same expressions.** The four
  forms before the last list the same ones, and their tests on T's records
  read 49.6, 60.0, 52.3 and 52.5 there, where the piece before read 96.9 to
  99.9 in all in the same jobs.
- **The campaign's faults took code out** (below): the rule's own path's
  verdict, a cast's test of a top's type, the asks of a cast further back
  than the block before, and the masks' sixth word. The form before them
  read 35.4 against the piece before; with the first of them 34.9 against
  main, and this one 35.8. The last ones changed the asks and the far
  path alone, and what reads more is an expression's part (9.7 for 9.3)
  and the tests on T's records (15.1 for 14.6): the tests' functions are
  the same bytes 576 further on, and the function the sweeps are inlined
  in is 62 bytes longer.

**What stays scalar, and why:**

- **The asks,** a cast at a time: its first operand, that call's first
  operand, its kind, its line's number and the line's byte, five loads in a
  chain. 6,145 casts on `sqlite3.c`, 2% of its records; a form with 6,471
  read them at 27 ticks each with the facts' making, 0.6 ticks a record. As
  gathers of sixteen casts, at the 1.3 to 1.5 ticks an element "The
  constants with no test to make" measured, the five loads are 7 ticks a
  cast in a full step; a chunk has ten casts on `sqlite3.c` and fewer than
  one on musl.
- **A declaration's fact of its result,** the census's code, once a
  declaration's line: 1,144, 740 and 1,361 asks; a form with 1,130, 741 and
  1,361 read them at 28, 31 and 60 ticks.
- **A call whose discard is further back than the block before:** one load
  and one store where the loop reads that operand, as it reads the others
  that far.
- **The arguments' part of the kernel's call rule,** three ORs an argument
  where the rule reads its record, the first four with no branch.
- **A row's parameters,** once a function's type.
- **The check of what is left out:** the tests' own code and a call's own
  list of arguments, under `--verify` alone.

**Faults.** 155 were put into the first complete form, one at a time, in a
copy of the tree: 98 failed the node machine's tests, 32 changed no result,
and 25 of the texts didn't compile. What the 32 showed, each from the code
that makes the value it reads:

- **Code that did nothing, taken out.**
  - The rule's own path's verdict: it makes seven of Lua's calls and no
    fixture's, and they stay listed.
  - A parameter's type's word's valid bit: a prototype's parameters' types
    are valid.
  - A cast's operator beside a top's void type: the census's code asks the
    type of any discarded value.
  - A record's standing and the rest of its exponent, where the records'
    bytes read a call's bit: a call's record says 0 or 1.
  - A leaf without a line's declaration's fact: such a leaf is loud as a
    callee without asking.
  - A word of a block's masks for the calls left out, and the asks' reads
    of it.
  - The asks of a cast further back than the block before: its call stays
    listed.
  - An aggregate, a missing record and a wide value in the verify's own
    list of arguments: the call's test finds the first, and a call left out
    has neither of the others.
- **New fixture lines** (the seventh fixture's function `calls`): the first
  of two parameters that takes no constant; a constant a cast or a
  conditional makes, whose record says no value; a static object's address;
  six arguments, the fifth and the sixth loud; a pointer whose declaration
  has a format; a null callee; a cast to another type and a comma over a
  call left out; a `for`'s last clause whose discard is in the block
  before, and further back, with a callee that asks, one that doesn't and a
  write; and seventy statements of each kind, a cast to void at each place
  of a block.
- **A pinned count:** the casts in the asks' list.

139 were then put into the final code, the 25 texts corrected: 137 fail the
tests. The other two change no result a test can read:

- **The far path's test of a record's place in its chunk.** A record past
  the chunk's count is read there as the others that far are, and without
  the test a stale one could keep a call listed: another listing, the same
  verdicts.
- **A leaf without a line, loud as a callee.** No call has one for a
  callee: a name's leaf has no line where its value is past the columns, an
  enumerator's, or where the lines are full, and their room is half the
  unit's tokens and 64.

The verify's own two counts of a call left out are not among the faults: a
fault there shows only beside one in the rule.

**Against its gates.** The piece was specified on the issue before its code:

- **Exact: met.** No expression differs on the three corpora and the
  compiler's own source, with the default warnings and with every warning;
  beside `--verify` no call left out has a test or a loud argument, and no
  expression with nothing listed has another verdict.
- **The tests on the window under 40.0 on `sqlite3.c`, refuted at 41.0:
  met.** 35.8, and 35.6 in the second build.
- **The rule's inputs under 1.0 tick a record (the records' bytes and the
  loop together, 9.4 before): missed.** They read 10.9: 0.8 more in the
  bytes, where a line's byte has the callee's fact, and 0.7 in the loop, of
  which the asks were 0.6 where they were timed.
- **What is not this piece's reads the same: the names' pass and
  `-c sqlite3.c` do; the kernel in place does not,** by the form that
  replaced the specification's: its call rule gathers the arguments' part,
  and it reads 16.4 for 15.6 on `sqlite3.c`. The front with the window
  read a third more from this build's first file and main's from every
  other file of it, above: the file's pages, not the piece.
- **Where it left the specification.** The arguments' part is the kernel's
  call rule's, not a mark by the front's distances; the parameters' part is
  the callee's type's row's, not its declaration's line's; a callee's result
  is asked where a cast is over its call, not at its line's first leaf; and
  a constant argument is quiet where its record says a value of zero or 1
  to 126, so a constant a cast or a conditional makes, whose record says
  none, keeps its call listed.

**What is left, counted.** With the piece, by what its tests find (default
warnings):

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions listed for the tests that read T's records | 15,926 | 5,108 | 16,655 |
| for a listed operator alone | 10,453 | 2,150 | 8,640 |
| for an initialization's root alone | 3,310 | 1,155 | 5,780 |
| for writes out of sequence alone | 600 | 137 | 420 |
| for a returned address alone | 76 | 129 | 316 |
| for a statement's top alone | 0 | 0 | 0 |
| operators listed | 15,688 | 5,970 | 14,556 |
| of them, with a test that finds nothing and sends nothing | 14,770 | 4,858 | 12,734 |
| casts | 7,370 | 1,995 | 3,849 |
| of them, with nothing | 7,370 | 1,919 | 3,824 |
| subscripts, by a constant | 3,181 | 87 | 1,586 |
| assignments of a constant | 1,250 | 223 | 1,112 |
| calls | 854 | 666 | 1,359 |
| conditionals and commas, listed whatever their operands | 988 | 925 | 816 |
| comparisons with a constant | 1,041 | 466 | 2,153 |
| shifts by a constant | 282 | 1,094 | 823 |

- **No statement is listed for its top alone any more.**
- **Half of what is listed on `sqlite3.c` is a cast,** and every one of its
  casts' tests finds nothing: a third of Lua's listed operators and a
  quarter of musl's.
- **An initialization is listed for its root alone** at a fifth of
  `sqlite3.c`'s listed expressions and a third of musl's, 5,235 of musl's
  5,780 with a plain name's value for an initializer.
- **With every warning** `-Wself-assign` and `-Wsign-compare` list every
  assignment and comparison: 16,165 assignments on `sqlite3.c`, 12,136 with
  nothing, and 10,830 comparisons, each sent to the census's code.

### A unit with `no_builtin`, and `*f` of a function's designator

Two things the pieces above never read, found while counting the calls' rule
on the compiler's own source
([#2968](https://github.com/buster14a/buster/issues/2968),
[#2970](https://github.com/buster14a/buster/issues/2970)). [The
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/fronts-calls-2968-2026-11-18)
has the probes and the readings.

- **One `no_builtin` function left every call of its unit unmade.** The
  fronts left a call where its unit had clang's attribute anywhere, and the
  compiler's own source has one such function since 2026-10-21
  (`dataflow_liveness_vector_one`). No call of it was made at reader time,
  and each piece of the tests on the window was exact there on expressions
  without a call. The attribute matters to a plain library builtin inside
  the function that has it (#1958): the walk asks by the function that
  holds the call (`check_input_no_builtin`), the whole front's library mark
  asks the same, and the lean and the flat fronts read no mark. So the
  fronts make the calls, whatever the unit has.
- **`*f` of a function's name had no type.** The kernel's dereference rule
  converts its operand as T does, a function's designator to a pointer to
  it, and read what that pointer points at from the operand's own row. Only
  a pointer's and an array's row have an element: the record got no type
  and an lvalue's category, and the shadow exits 1 on `(*f)(a)`. The record
  is the designator's own type now, a function's designator again. No
  corpus has one among the records the kernel makes.

The compiler's own source, in the cloud container, with `--verify`:

| | before | with both |
| --- | ---: | ---: |
| expressions the tests on the window run on | 38,155 | 52,015 |
| their records | 226,623 | 371,332 |
| calls listed | 0 | 4,393 |
| calls the rule leaves out, each checked | 0 | 12,484 |
| of them with a test, or with a constant argument the rule doesn't take | | 0 |
| casts in the asks' list | 0 | 4,140 |
| declarations asked whether they want their result | 0 | 723 |
| expressions with nothing listed, each checked | 18,255 (48%) | 24,063 (46%) |
| with every warning | 7,931 (21%) | 11,812 (23%) |
| of them with another verdict | 0 | 0 |
| library calls T asks about at reader time | 0 | 1,171 |
| expressions C and X compared | 13,831 | 19,450 |
| records the kernel makes in place | 185,936 | 310,953 |
| that differ, in any of them | 0 | 0 |

- **No expression differs in any of the shadow's twelve modes on the four
  corpora,** and the three other corpora's counts are what they were: none
  of their units has the attribute.
- **On the 9700X the three corpora read the same.** Two builds of the
  fixes, each in turn with the piece before's: the tests on the window 35.6
  for 35.7 and 35.8 for 35.4 on `sqlite3.c`, 52.9 for 53.2 and 53.5 for
  53.1 on Lua, 95.8 for 95.2 and 100.3 for 99.7 on musl; the kernel in
  place 16.5 for 16.4 in both.
- **Tests.** A twelfth fixture, two functions with the attribute (one
  naming `memset`, one naming nothing) between three without it: a library
  call is its declaration's in the function whose attribute covers it and
  has its own semantics in the others. The fronts run over it, whole, lean
  and flat (`node_front_test_builtins`). `(*f)(a)`, `(**f)(a)` and a
  pointer's `(*fp)(a)` are in the facts' fixture, and the first two in the
  tests' function `calls`.
- **Faults.** Nine were put into the two fixes, one at a time, in a copy of
  the tree: eight fail the tests, the fronts' unit-wide term put back in
  either front among them, and the whole front's mark asked of no function.
  The ninth showed code that did nothing: the designator's type was
  stripped of its qualifiers, and the type table gives a function type none
  (`type_qualify`). The strip is gone.

### The casts, a pointer's subscripts and its sums

"The calls with no test to make, and a statement that discards nothing" left
15,926 of `sqlite3.c`'s 50,996 expressions listed for the tests that read T's
records, and half of their listed operators were casts. This piece takes out
three kinds of operator whose test reads a pointer and finds nothing. [The
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/window-tests-casts-2850-2026-11-18)
has the census, its scripts and every reading.

**What was listed, counted.** Before any code: the shadow with a line for
each listed expression and each of its listed operators, on the piece
before's sources with #2968's and #2970's fixes (default warnings):

| | `sqlite3.c` | Lua | musl | the compiler's own source |
| --- | ---: | ---: | ---: | ---: |
| listed operators | 15,688 | 5,977 | 14,556 | 21,223 |
| casts | 7,370 | 1,995 | 3,849 | 2,639 |
| a pointer's, to void | 6,256 | 1,446 | 2,093 | 1,520 |
| a pointer's, to a pointer | 1,026 | 464 | 609 | 373 |
| a pointer's, to a 64-bit integer | 88 | 9 | 1,122 | 133 |
| an enumeration's | 0 | 76 | 25 | 613 |
| of them, with a test that finds anything | 0 | 0 | 0 | 0 |
| subscripts with a constant operand | 3,181 | 78 | 1,582 | 2,061 |
| of them, of a plain pointer | 2,288 | 57 | 655 | 938 |
| sums of a plain pointer and a constant | 174 | 305 | 254 | 335 |

The casts to void are statements for the most part. A statement's discard
is a cast to void over its top, and a top of a pointer's type (`p = q;`,
`p++;`, a call that returns one) sent the cast to the pointer cast's test,
which asks whether the cast's own type is an integer narrower than a
pointer.

**The three rules.**

| the operator | is left out where |
| --- | --- |
| a cast | a pointer operand sent it, and its own type is void or a pointer |
| a subscript | one operand is a plain pointer that is no constant a test may read, and the other a plain integer |
| `+ -` and their assignments | one operand is a plain pointer and the other a plain integer |

- **A cast's one test is the pointer cast's.** It fires where a pointer is
  cast to an integer of under 64 bits or to an enumeration. A constant
  operand's conversion has no test at a cast. The enumerations' test reads
  two operands' types, a conditional's arms' or a conversion's, never a
  cast's one operand, so the table's entry for a cast sends no enumeration
  any more.
- **A cast to an integer stays listed, whatever its width.** The type's
  byte says none, and a 64-bit one's test finds nothing: 35, 1, 90 and 17
  expressions would follow.
- **A subscript's index and a sum's integer are promoted**
  (`check_action_pointer_arithmetic`), which narrows no integer: the
  conversion's test has nothing to find. A subscript has two tests more. One
  is a null constant pointer's dereference: a subscript whose pointer is a
  constant a test may read stays listed. The other is an array's bound, and
  an array is no plain pointer.
- **A string's sum stays listed** by its array's bit, as before.

**How it is made.** No new byte, no new sweep, nothing scalar:

- **The sums and the subscripts are the quiet rule's table's.** The pairs of
  a plain pointer and a plain integer are in the class of `+`
  (`node_tests_quiet_pairs`), which a subscript's operator joins. No other
  operator of that class takes a pointer.
- **A subscript's constant pointer is one mask:** each side's value's byte,
  which the loop has for the table already, against a plain pointer's kind
  with the constant's bit.
- **A cast is one mask more:** sent by its operand, its own type's byte void
  or a pointer. It joins the mask of the operators left out, whose tests
  the reader's verify runs all the same, counted apart.

The counts, in the cloud container beside `--verify` (default warnings; the
share before in parentheses):

| | `sqlite3.c` | Lua | musl | the compiler's own source |
| --- | ---: | ---: | ---: | ---: |
| casts left out, each checked | 7,282 | 1,910 | 2,702 | 1,893 |
| casts of an enumeration, sent no more | 0 | 76 | 25 | 613 |
| subscripts and sums left out, each checked | 2,462 | 362 | 909 | 1,273 |
| of all those, with a test that fires or a record sent | 0 | 0 | 0 | 0 |
| operators listed, before | 15,688 | 5,977 | 14,556 | 21,223 |
| with the piece | 5,944 | 3,629 | 10,920 | 17,444 |
| expressions listed, before | 15,926 | 5,108 | 16,655 | 27,952 |
| with the piece | 9,540 | 3,932 | 14,546 | 26,250 |
| expressions with nothing listed | 81% (69%) | 64% (53%) | 66% (62%) | 50% (46%) |
| with every warning | 42% (40%) | 37% (33%) | 42% (41%) | 24% (23%) |

The census's lines put through the three rules gave the same counts before
the code was written, to the expression.

TSC ticks a record of the expressions the tests ran on, #2973's head
`cc9c84a54` and the piece on it in turn on the 9700X (medians of seven
readings, musl's of fifteen; with every warning of five, musl's of fifteen;
the kernel and the front of seven):

| | `sqlite3.c` | | Lua | | musl | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| | before | the piece | before | the piece | before | the piece |
| **the tests on the window** | 35.5 | **31.2** | 53.4 | **49.4** | 99.9 | **95.8** |
| of which the records' bytes | 9.3 | 9.3 | 10.8 | 10.7 | 19.1 | 18.8 |
| the loop | 1.6 | 1.6 | 2.3 | 2.5 | 4.9 | 5.6 |
| an expression's part | 9.5 | 9.5 | 9.7 | 9.6 | 17.2 | 17.2 |
| the tests on T's records | 15.0 | 10.8 | 30.6 | 26.5 | 56.7 | 52.7 |
| with every warning | 63.4 | 59.7 | 71.1 | 67.2 | 126.9 | 123.0 |
| beside `--verify` (one reading) | | 85.1 | | 102.7 | | 173.4 |
| the kernel in place, without the tests | 16.4 | 16.0 | 20.9 | 20.5 | 19.1 | 19.1 |
| the front with the window | 53.1 | 53.3 | 71.0 | 70.7 | 89.9 | 92.5 |

- **Exact.** No expression's bits, root's kind or span differ from the
  census's code's, and no operator left out has a test or sends a record:
  on the three corpora and the compiler's own source, with the default
  warnings and with every warning, beside `--verify`, and on the twelve
  fixtures.
- **4.4 ticks a record less on `sqlite3.c`,** 4.0 on Lua and 4.1 on musl:
  12%, 8% and 4% of the tests. A first build of the same rules read 31.2
  for 35.9, 49.0 for 53.4 and 92.7 for 99.0 against the same base.
- **The tests on T's records fell by less than their records.** 45% of
  `sqlite3.c`'s listed records left the list (61,457 of 111,130 stay) and
  28% of that sweep's ticks: what stays is costlier a record. 3,890 of the
  9,540 expressions still listed are initializations, of three records each
  on average (11,513 of the 61,457), under a root with tests of its own.
- **The loop reads the same on `sqlite3.c`, 0.2 more on Lua and 0.5 to 0.7
  more on musl:** some 50 ticks a chunk on both (244 and 2,975 chunks),
  which the rules' dozen operations a block don't account for. No counter
  was read for it.
- **What is not the piece's reads the same.** The kernel in place read 16.0
  for 16.4 in this build and 16.8 for 16.3 in the first (three readings
  there: 16.5, 16.8 and 18.7), the front with the window within its spread,
  and `-c sqlite3.c` as whole processes -0.05% and -0.06% in wall time
  where the base against itself reads -0.08% and -0.16%.

**Against its gates.** The piece was specified on the issue before its
code:

- **Exact: met.**
- **The counts (no more than 9,600, 3,950, 14,600 and 26,300 expressions
  listed): met,** at 9,540, 3,932, 14,546 and 26,250.
- **The tests on the window under 31.0 on `sqlite3.c`, refuted at 32.0:
  missed by 0.2,** at 31.15 and 31.18 in the two builds. **Under 50.0 on
  Lua, refuted at 51.0: met,** at 49.4 and 49.0. musl reads no more.
- **The records' bytes and the loop together no more than 0.3 above: met on
  `sqlite3.c` and Lua** (the same, and 0.1 to 0.2 more). On musl the two
  builds read 0.2 less and 0.4 more.
- **What is not this piece's reads the same: met.**

**Tests.**

- **A function of the seventh fixture for the three rules,** each on both
  sides: a pointer's cast to void (a statement's discard and a written
  one), to a pointer and to a pointer to void, left out; to a 64-bit
  integer, listed with nothing; to a 32-bit one, a bool and an enumeration,
  which fire; an enumeration's cast. A subscript of a pointer by zero, a
  small, a large and a negative constant, with the constant first and of an
  address constant, left out; an array's, a null constant pointer's on each
  side and one by a bool's constant, listed. A sum on each side, each
  compound assignment and one whose pointer is the constant, left out; a
  string's, two pointers' difference and one whose constant is further back
  than the block before, listed. The casts left out are a pinned count of
  their own.
- **A pointer's cast to an integer in the fixture of warnings that are
  off:** with `-Wno-pointer-to-int-cast` nothing sends it.
- **Faults.** 32 were put into the piece's code, one at a time, in a copy
  of the tree: each fails the tests (29 as written, and three whose first
  text didn't compile, corrected). One edit went back before them for doing
  nothing: the enumerations' trigger asked of what the operator sends, where
  a cast that reaches its test has a pointer for an operand.

**What is left, counted.** With the piece (default warnings):

| | `sqlite3.c` | Lua | musl | the compiler's own source |
| --- | ---: | ---: | ---: | ---: |
| expressions listed for the tests that read T's records | 9,540 | 3,932 | 14,546 | 26,250 |
| for an initialization's root alone | 3,439 | 1,278 | 5,828 | 13,027 |
| for a listed operator alone | 4,273 | 1,440 | 6,567 | 8,669 |
| for writes out of sequence alone | 698 | 150 | 490 | 163 |
| operators listed | 5,944 | 3,629 | 10,920 | 17,444 |
| assignments | 1,250 | 223 | 1,112 | 1,535 |
| of them, with a test that finds nothing and sends nothing | 5,026 | 2,593 | 9,123 | 10,793 |
| subscripts of an array by a constant | 893 | 21 | 927 | 1,003 |
| calls | 854 | 673 | 1,359 | 4,393 |
| conditionals and commas, listed whatever their operands | 988 | 925 | 816 | 5,728 |
| casts of a pointer to a 64-bit integer | 88 | 9 | 1,122 | 133 |

- **An initialization's root is the largest part now:** a third of
  `sqlite3.c`'s listed expressions and half of the compiler's own.
- **An assignment listed on `sqlite3.c` stores a constant in each case,**
  and none of the 1,250 has a test that finds anything: a zero in a plain
  pointer for 592.
- **Two thirds of Lua's calls that an argument keeps listed have a negative
  constant among their arguments,** which the kinds don't tell from any
  other constant past 126: 2,079 of the 3,199 the kernel's call rule finds
  loud over its runs there, 1,869 of 5,712 on `sqlite3.c` and 854 of 9,422
  on musl.

### C on the window

The tests stop at their fourth piece for now: what step 2 lacked next was no
test. C and X still read T's columns, 25 of them, a case an operator. C
(`c_check_node_machine_contexts.c`) goes from each root down and gives every
evaluated record its template and its operands their contexts and targets, a
record at a time. This piece makes its answers on the window, 64 records a
step, for the expressions with no call, no `&&`, `||` or `?:`, no update and
no comma. [The
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/c-on-the-window-2850-2026-11-18)
has each build's patch, every reading, the probes and the faults; [the
census's](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/c-answers-census-2850-2026-11-18)
has what the design rests on.

**What C's answers depend on, counted.** Before any kernel: a line for each
record of each expression T takes at reader time, with C run on it as today
(158,121 expressions, 968,051 records, 763,283 of them evaluated with a
template, on the three corpora and the compiler's own source), and C's rules
restated over a record's own bytes and its consumer's:

- **Every answer but one is a consumer's bytes and a record's own.** The
  restated rules gave C's context, its target's source and whether a record
  is evaluated on all 858,545 operand answers.
- **A template needed none of T's conversions' words there.** An operand's
  type as its operator reads it was its own record's, an array's and a
  function's as a pointer's: 0 of the 763,283 templates differed for it.
  The 1,978 that the restated rules missed read a value: a division by a
  power of two (505), a call that is not plain (1,466), a stride (5) and an
  update (2).
- **Only whether a record is evaluated chains down a tree,** and the chain
  breaks at a leaf nearly always.
- **A call's arguments' targets are in no line** (10.8% to 16.8% of the
  evaluated records): an argument's target is its conversion's type, and
  the kernel's call rule makes none of a call's arguments' conversions,
  which are T's still (`node_machine_argument`). A call is listed in this
  piece.
- **A learned table of a few bytes is not enough for the template:** 83.5%
  to 88.5% of the records by the consumer's template, the operator and the
  place alone. The rules are masks here, not a table.

**What it is.** Four sweeps over a chunk's records, after the kernel has made
them in place:

- **The records' bytes.** The tests' sweep makes five byte columns more from
  the gathers it has: the type's word's low three bytes (class, facts,
  kind), the standing's byte, and a byte of what the record's flags and
  object are. Whether a name's leaf's object has a symbol is its
  declaration's line's byte's. The sweep is two, each its own code: the
  tests' bytes alone as before, and with C's.
- **The loop, 64 records a step, from the chunk's last block to its first,**
  so that a consumer's template is made before its operands read it. A
  record's context is its consumer's operator's class and its own place. Its
  template is C's rules as masks, four of them the template's number's
  bits. Whether its consumer evaluates it and where its target comes from
  are a table of 64 rows by the consumer's template and the place, one byte
  permute.
- **The targets, 16 records a step:** one gather from the consumer's
  record's line, at T's conversion of the operand or at the consumer's own
  type.
- **An expression's part:** whether any of its records is listed, whether an
  evaluated one has no template (C refuses the expression), and its root's
  target, given.

A consumer's bytes come through its distance forward among the window's
records, which the front writes beside its distance in the unit's postorder:
the two differ where a chunk's end took records from between a control's
root and its operand. One store an operand.

Four things are a record at a time: a pointer's stride, its type's row's; C's
rule on T's two conversions themselves, for a sum or a binary operator whose
operands' own classes don't decide (below); a consumer past the block after
or a first operand before the block before, read by loads; and the records
under one that isn't evaluated, taken out by its subtree's size.

**What is listed,** for C on T's records as before: a call; `&&`, `||` and
`?:`; an update; a comma and a selection; a division or a remainder by an
integer constant; a record whose value the columns don't hold whole (a
bit-field's, a faulted constant's and an overload set's are); a cast to a
union; a leaf with an effect, of void's type, or read for its address
without being an address constant; and any operator without a case.

**Listing is always safe; a refusal has to be C's own.** The census's
restated rules were exact on 763,283 templates and wrong on programs none of
the corpora has. The first fault campaign's survivors read as dead terms, and
the build without them (the sixth below) was still exact on the three corpora,
the compiler's own source and the fixture. Reading C's rule against the code
that makes each record, and one statement at a time through the shadow, found
what the corpora could not:

- **T's record of an aggregate has an address's class** (`node_machine_type`),
  where its type's word has none: a wrapper of a struct passes, and one of a
  half-precision value does not. Without the wrapper's class's test
  `(int)(_Float16)d` differed; with it and no more, `return *o` of a struct.
- **A cast to an atomic type** converts its operand to the value type and
  has the atomic one: its operand's target is the cast's own type, not T's
  conversion, and C's atomic rule is an lvalue's read alone.
- **A 128-bit integer beside a double or a float is converted to it:** C's
  rule reads the conversion's class, a number's, where the operand's own is
  none. Two 128-bit integers, and a long double beside a double, stay.

So the loop has each of C's terms, but where what makes the records implies
one (a bit-field's flag comes with its width, which makes its value one the
columns don't hold whole; at reader time every object is one the unit
knows). And an operand's own bytes stand for T's conversion of it only
where the two say the same. A value is converted as a value alone, an
lvalue to its value and an array or a function to a pointer, and a type's
class and facts are its value's already: a truth's operand, a subscript's
and a pointer's sum's read the same either way. The usual arithmetic
conversions are where the two differ: for a sum or a binary operator of a
number's class whose operands' own classes are not both a number's, the
record's rule reads T's two conversions, a record at a time: none on
`sqlite3.c` and Lua, 128 records on musl, 140 on the compiler's own source.

The counts, in the cloud container (default warnings):

| | `sqlite3.c` | Lua | musl | the compiler's own source |
| --- | ---: | ---: | ---: | ---: |
| expressions the kernel placed and C ran on | 50,996 | 10,839 | 43,415 | 52,015 |
| **decided on the window** | **32,300 (63%)** | **5,644 (52%)** | **30,863 (71%)** | **24,659 (47%)** |
| their records | 138,609 (48%) | 32,054 (42%) | 111,599 (50%) | 98,719 (27%) |
| of them, refused for a template, as C refuses them | 124 | 144 | 1,631 | 817 |
| expressions and records that differ from C's | 0 | 0 | 0 | 0 |
| listed for a call | 12,179 | 3,741 | 7,288 | 12,841 |
| for `&&`, `\|\|` or `?:` | 2,267 | 334 | 1,034 | 8,866 |
| for an update | 3,415 | 758 | 3,873 | 5,191 |
| for a comma or a selection | 149 | 320 | 39 | 0 |
| for a division by a constant | 160 | 38 | 243 | 405 |
| for a value the columns don't hold whole | 526 | 4 | 75 | 53 |
| records swept | 321,488 | 91,270 | 377,187 | 557,987 |
| strides read, a record at a time | 6,965 | 1,074 | 3,850 | 12,897 |
| of them, asked of the type table | 0 | 0 | 5 | 0 |
| rules that read T's conversions | 0 | 0 | 128 | 140 |
| consumers and first operands read by loads | 1,356 | 192 | 351 | 2,485 |

An expression counts for the first reason its records give, its root's last.

The 9700X, CPU 2, under the lock, Release; main `521d5b63d` and the piece on
it in turn; medians of seven readings, musl's of fifteen. TSC ticks:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| **C on the window, a record its sweeps ran over** | **5.44** | **6.77** | **10.96** |
| of which the loop | 2.44 | 3.82 | 6.75 |
| the targets | 1.24 | 1.43 | 2.20 |
| an expression's part | 1.75 | 1.51 | 2.05 |
| the same three sweeps again over the chunk | 4.60 | 4.52 | 5.09 |
| of which the loop | 1.83 | 2.06 | 2.12 |
| the records' bytes with C's five columns, a record of the tests' | 9.95 | 11.86 | 21.81 |
| the tests' bytes alone: main | 9.30 | 10.79 | 19.03 |
| with the piece, C off | 9.19 | 10.81 | 19.97 |
| **C's three sweeps, a record of the decided expressions** | **12.6** | **19.3** | **37.0** |
| with the bytes' five columns: the gate's figure | 13.9 | 22.0 | 43.9 |
| the reference's C on the same expressions | 40.2 | 36.2 | 43.0 |
| the tests on the window: main | 31.1 | 49.8 | 93.8 |
| with the piece, C off | 31.2 | 49.5 | 97.7 |
| the kernel in place: main | 16.0 | 20.6 | 18.9 |
| with the piece, C off | 16.0 | 20.6 | 19.0 |
| with C on | 16.3 | 20.6 | 18.9 |
| the front with the window: main | 53.2 | 70.7 | 89.4 |
| with the piece | 53.6 | 70.6 | 90.3 |

- **A record of the decided is 2.3 to 3.4 records swept:** the sweeps run
  over every record of a chunk, and half of them or fewer are a decided
  expression's. The loop's 2.4 ticks a record swept are 5.7 a record of the
  decided on `sqlite3.c`.
- **A chunk's first run is not its second:** the loop reads 2.44, 3.82 and
  6.75 where a second run over the same chunk reads 1.83, 2.06 and 2.12.
  The three corpora's second runs are within a tenth of each other; their
  first runs are not.
- **C on T's columns costs 3.2 times as much on `sqlite3.c`** (40.2 a record
  of the same expressions), 1.9 times on Lua, and 1.2 times on musl, where
  it is the first run that costs.
- **`-c sqlite3.c` reads the same,** as it must with nothing in a compile:
  wall -0.03% and -0.01% in two runs of 21 processes a build (the base
  again -0.04% and -0.08%).
- **The kernel in place reads 16.3 on `sqlite3.c` where C runs beside it**
  and 16.0 where it doesn't, in the same executable (16.3 to 16.8 for 16.0
  to 16.4 over the builds).
- **musl's readings scatter:** its tests on the window read 97.7 with the
  piece for main's 93.8 here, and 94.2 for 95.8 and 91.9 for 95.3 in the
  two builds before.

**What moved it, and what didn't.** Ten builds on the 9700X, each against
the same main in turn; C's three sweeps and the bytes' five columns, a record
of the decided expressions on `sqlite3.c`:

| build | what changed | ticks |
| --- | --- | ---: |
| 1 | the first | 17.2 |
| 2 | the forward distance as one store an operand in the front, where the window's writers had stored it a record; the template's number as four masks | 16.0 |
| 3 | the loop a function of its own, its frame aligned to 64 bytes | 15.3 |
| 4 | the fact words' kernel back at a cache line's first byte | 15.2 |
| 5 | the bytes' sweep as two, the tests' alone as before | 15.4 |
| 6 | the terms the first fault campaign's survivors read as dead, removed: not C's rule (above) | 13.7 |
| 7 | each of C's terms, the rule on T's conversions, the vector's element's flag | 14.2 |
| 8 | what the third fault campaign found unneeded, removed: the rows' table 64 bytes read as one register, a name's template one mask less | 14.2 |
| 9 | the conversions' rule without two arguments that were always true | 14.2 |
| 10 | the conversions' rule for the arithmetic conversions alone; a range's reset that did nothing, removed: the pull request's code | 13.9 |

- **A 512-bit loop inside a large function spilled its registers across
  cache lines.** As part of `node_facts_reader_flush`, whose frame is
  aligned to 16 bytes, the loop kept twenty registers in the frame: 78 of a
  profile's 427 samples were on those moves and 112 right after one. A
  function of its own with an aligned frame took the loop from 3.15 to 2.86
  ticks a record swept, a tenth: the spills were not most of its cost.
- **A function's place in its cache line moved the fact words' kernel.** The
  third build's layout put `node_facts_window` 48 bytes into a line, and
  the kernel in place read 16.4 ticks a record for 16.0 with the same 736
  instructions. Aligned to 64 bytes it reads 16.0 again.
- **One bytes' sweep with a test a step for C's columns cost the tests'
  bytes 0.2, 0.6 and 2 ticks a record** with C off (builds 2 to 4). Two
  sweeps from one inlined body read main's on `sqlite3.c` and Lua, and 0.1
  to 1.1 more on musl, whose own readings are 0.9 apart from job to job.
- **Exactness cost a fifth to a half of a tick:** 14.2, 14.2, 14.2 and 13.9
  in the last four builds for the sixth's 13.7.

**Against its gates.** The piece was [specified on the
issue](https://github.com/buster14a/buster/issues/2850#issuecomment-6983596165)
before its code:

1. **Exact: met.** No expression's verdict and no record's template,
   context or target differs from C's on T's columns, on the three corpora
   and the compiler's own source, with the default warnings and with every
   warning, on `test-checker`'s fixtures, and on 266 statements run one at a
   time. The corpora alone would have passed the sixth build, which was not
   C's rule.
2. **The counts: met.** 32,300, 5,644, 30,863 and 24,659 expressions decided
   (`sqlite3.c`, Lua, musl, the compiler's own source), where the gate asked
   for 32,000, 5,500, 30,000 and 24,000.
3. **Under 10.0 ticks a record of the decided on `sqlite3.c`: not met.**
   13.9 (13.8 to 14.5 over seven readings; 14.2 in the three builds
   before). It was to be refuted at 15.0, and the first five builds read
   15.2 to 17.2.
4. **What is not the piece's reads the same: met but for the front.** The
   tests on the window (31.2 for 31.1 on `sqlite3.c`), the kernel in place
   with C off (16.0 for 16.0) and `-c sqlite3.c` read main's. The front with
   the window reads 0.4, -0.1 and 0.9 ticks a record more (0.0 to 0.4, -0.1
   to 1.2 and 0.5 to 3.9 over the builds): the forward distance's store.

**Tests.**

- **A fixture of its own** (`node_contexts_test_fixture`): two functions of
  250 expressions, each rule's cases on both sides and each kind it lists,
  and 128 statements of seven and of five records, which put a block's end
  between every two of a statement's records in turn. Pinned: 240 decided
  and 30 of them refused, 20 strides read (3 of the type table), 6 rules
  that read T's conversions, 16 loads out of a block's reach, and the listed
  by reason; no verdict and no record differs.
- **The comparison's control.** The fixture runs again with one of the
  reference's answers changed for each expression C takes: its root's
  template, context, target or whether it is evaluated, or its verdict, in
  turn (`BUSTER_NODE_READER_WINDOW_CONTEXTS_FAULT`). The comparison has to
  count every decided expression C takes, 210. Before it, a fault in the
  comparison itself passed every test.
- **The census's twelve fixtures** in the window's form, each with its
  decided and refused expressions pinned, and the room's fixture, where a
  chunk's end cuts expressions.
- **Faults,** one at a time in a copy of the tree, each built and run
  against the node machine's tests, seven rounds as the code changed. Of the
  380 whose text is in the pull request's sources, 347 are caught. A corpus
  tells none of the 33 others from the code: five change how many records
  are read by loads and no answer; two choose between two right readings;
  seven are the guard that lists a leaf the loop makes no template for,
  which no source reaches; nineteen are terms of C's own that what makes the
  records never separates (the record names what does, for each).
- **What the rounds changed.** The first round's survivors were read as dead
  and removed, wrongly (above). The later rounds' gave the fixture its
  lines for what the corpora told apart and it did not (what a block
  carries to the block before, an operand's second conversion, a root that
  alone has no template, a range under a folded constant over more than two
  blocks), the comparison its control, and the code four removals: the
  rows' third and fourth places, a local's template's own mask, the
  conversions' rule's branches for a truth and a subscript, and a range's
  reset that did nothing.
- **The sanitizers:** memory, address and thread on the native host, the
  rows a pull request's checks skip: the node machine's tests' part, and
  under memory and address C on the window on `sqlite3.c`, Lua and the
  compiler's own source. No report.

**What is left, counted.**

- **Two thirds of what it lists is a call's.** Of `sqlite3.c`'s 18,696
  listed expressions 12,179 are listed for a call, 3,415 for an update and
  2,267 for `&&`, `||` or `?:`. An argument's target is its conversion's
  type, and the kernel's call rule makes the call's own record and none of
  its arguments' conversions, which are T's still
  (`node_machine_argument`): making them in the kernel is the piece after
  this one, [a call on the window](#a-call-on-the-window).
- **X reads none of it, and no compile takes any.** The answers are
  compared with C's and dropped. The sum of step 2 moves when X runs on
  them.
- **A chunk's first run:** 0.8, 2.3 and 5.9 ticks a record swept over its
  second.
- **An expression's part is a third of the sweeps on `sqlite3.c`** (1.75 of
  5.44): two ranges of a block's masks an expression, and its root's
  target.
- **The five columns:** 0.7, 1.1 and 2.8 ticks a record of the tests' over
  the tests' bytes alone.

### A call on the window

Two thirds of what C on the window listed was a call's. This piece takes
them: the kernel's call rule makes each argument's conversion, and C's
answers for a call are made on the window from them. [The
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/a-call-on-the-window-2850-2026-11-19)
has the census, each build's patch and readings, the probes and the faults.

**A correction came first.** This piece's first specification, and the
result note before it, said the kernel's call rule made a call's arguments'
conversions and kept none. It made none: `node_facts_call`,
`node_facts_call_fast` and `node_facts_call_load` checked the callee's
type's row and the arguments' count, gathered the arguments' flags, and
wrote the call's own record. Each argument's conversion was T's
(`node_machine_argument` through its memo, or the action's). The sentence
was written from the rule's comment, not from its body. An argument's target
in C is its conversion's type, so nothing on the kernel's side had it.

**And the kernel's call record was not T's where an argument's conversion
changes its flags.** A call inherits its flags from its converted arguments
(`check_action_call`), and the kernel's call rules gathered the arguments'
own. Two statements, on main before this piece, each ended the shadow with
a difference:

```c
fi(1e100);    /* void fi(int): the constant saturates, with two inherited flags */
fvec(a = b);  /* a vector's conversion keeps three flags and drops the others */
```

No corpus has such a call, and no fixture had one: 102 calls run one
statement at a time found the first, and reading each conversion's code
after it found the second. No compile was affected (the kernel in place is
in the shadow alone).

**What a call's arguments are, counted.** A line an argument of every call
T types at reader time:

| | `sqlite3.c` | Lua | musl | the compiler's own source |
| --- | ---: | ---: | ---: | ---: |
| calls | 16,016 | 4,213 | 7,967 | 16,985 |
| arguments | 42,623 | 10,017 | 21,027 | 43,628 |
| already the parameter's type, no constant | 66.4% | 67.7% | 63.6% | 59.6% |
| an integer constant of the parameter's type | 11.9% | 12.6% | 8.2% | 2.7% |
| an integer constant converted (a null pointer constant among them) | 4.8% | 2.4% | 7.1% | 12.5% |
| a pointer to another pointer type | 7.7% | 3.5% | 5.8% | 6.3% |
| an array or a function, to a pointer | 3.2% | 7.3% | 3.0% | 11.5% |
| an arithmetic conversion, no constant | 2.8% | 1.9% | 11.1% | 3.1% |
| past the parameters (the default promotions) | 3.0% | 4.6% | 0.8% | 4.2% |

**The arguments' rule** (`node_facts_argument_made`) is T's own
(`node_machine_argument`), as words of 0 or 1 with one branch (a pointer to
another pointer type asks that pair's slot), and it asks the type table
nothing: what it needs of a type is that type's answers, made once with its
row (`node_facts_argued`: whether it is arithmetic, a scalar, bool, real, a
pointer, an aggregate, a vector, complex; whether an integer constant folds
to it; its width; and the type its value has past the parameters). Three
answers an argument:

- **Made:** its conversion, T's type and flags, written to the argument's
  record's last quadword. To a pointer parameter a null pointer constant, a
  pointer of its type or one its pair's slot says is assignable; to another
  parameter what `node_machine_arithmetic_assignable` takes; past the
  parameters the default argument promotions. And only where the conversion
  changes nothing a call inherits: with no constant or an address's, an
  integer constant to an integer's or an enumeration's type or to float,
  double and long double, a floating constant among those three.
- **Kept:** the conversion is T's, and the call's record the kernel's all
  the same, because the conversion changes nothing the call inherits (a
  struct by value to a parameter of its type, an enumeration's value, a
  volatile or an atomic object's value, a pointer whose conversion drops a
  qualifier, a vector with none of the flags its conversion drops).
- **Neither:** the call is T's (a floating constant to an integer's type or
  of a type that is not one of the three, a complex number, a vector with
  such a flag, a value of another type to a struct's or a union's
  parameter, a parameter with no target).

A call's record says two things more, beside the tests' quiet bit: every
argument's conversion is made, and the call is a plain one as C asks (its
prototype's result and its arguments' conversions' types are plain
scalars).

**A call the loop leaves is marked, because a call of its batch may have it
as an argument.** The kernel's batches are by an operator's code and its
operands' rounds, and a call's code is not after a call's: `f(g(x))` has
both calls in one batch. The short path makes `f` from `g`'s record as it
stood. Where `g`'s arguments then don't keep `g`'s record, `g` is marked as
a value the record doesn't hold whole, `f` is neither made nor kept for it,
and both go to the rule in the window's order, which makes `f` from T's
record of `g`. A build without the mark was exact on `sqlite3.c`, Lua, musl
and the fixture of the time, and wrong on `fi(two(1e100, b));`: the fault
round that removed the mark as a dead term had no nested call to tell it.

**The rule's answers are kept by key.** An argument's answer is some 300
instructions, and a unit's arguments have few keys: 1,146 on `sqlite3.c`
for its 33,150 arguments (491 of them an argument of its parameter's type,
265 a pointer to another pointer type, 150 an array or a function). The key
is everything the rule reads of the record (its type, the target, eleven of
its flags, its standing, and whether it is an integer constant whose
payload is zero), and what the rule reads beside is the unit's words, which
once made don't change. 4,096 slots of half a line, one place a key. The
calls' loop reads each argument's slot; where the slot has another key's
answer or none, the rule answers there and then and the slot takes it.

**What was tried, each measured on the 9700X** (the kernel in place on
`sqlite3.c`, ticks a record of the window over the build before the piece:
the first four over C on the window's first piece on main `521d5b63d`, the
first of them with C off as its job read it, and the last over main
`bc66290c5`):

| | the kernel in place |
| --- | ---: |
| four arguments' slots read in the call's short path, unrolled | +2.2 |
| the slots read in a second pass over the batch's calls | +1.07 |
| the same with one multiply for the key's hash | +1.30 |
| the second pass, the rule as words of 0 or 1, a miss answered in the loop | +0.99 |
| the same as pushed (the rule's dead terms out, a left call marked) | +1.21 |

- **The short path lost its registers.** Four slots read beside the call's
  own arguments' gather doubled the loop (1,267 bytes to 2,722, 158 reads
  and writes of the stack).
- **The cheaper hash cost more than it saved.** One multiply of the two key
  words folded together sent 286 calls a pass to the rule for another key's
  answer at their slot (59 with a multiply each).
- **A missed branch is not the cost.** Samples of mispredicted branches in
  the calls' loop: 0.13 a call more than before the piece.
- **The rule on every argument, with no slots, was the slower.** Exact, and
  some 300 instructions an argument in the loop: 24 to 29 ticks a record in
  the cloud container, where the slots read 21 and the build before 19.

**What the timed run does not count.** The shadow times the kernel's third
run of a chunk, with the slots filled: it makes 58, 6 and 10 answers there
(a key whose slot another key took). A unit's first answers are made in the
runs before: about 1,150 on `sqlite3.c`, 1,330 over Lua's 35 units and
8,000 over musl's 1,500, where a unit has few calls and nearly every call a
key of its own. An answer reads 102, 103 and 128 ticks with its own two
clock reads: 0.4, 1.8 and 4.6 ticks a record of the window that no timed
figure here has. Most of an answer is the first touch of code and
words the loop leaves seldom, not the rule's length: an earlier rule that
asked the type table read 250 to 300 ticks an answer in the cloud
container, and this one 200 to 290 there.

**C's answers for a call** (`node_contexts_window_loop`):

- **The call:** the call's template where its record says a plain call and
  its context is no address's; none otherwise, which refuses the expression
  as C does.
- **Its callee:** evaluated, unless the lowering calls it directly, by its
  own bytes (no lvalue, an address constant with an object that has a
  symbol, no offset, no effect).
- **Its arguments:** evaluated, each to its conversion's type, read from
  its own record: the targets' gather's third source.
- **Listed:** a call whose record doesn't say every argument's conversion
  is made, and a callee that is no name's leaf and is such a constant (a
  symbol's bit is a leaf's line's).

C sets a call's depth too (`call_depths`), which the window does not make:
X's piece.

The counts, in the cloud container (default warnings; the same with `-Wall
-Wextra`):

| | `sqlite3.c` | Lua | musl | the compiler's own source |
| --- | ---: | ---: | ---: | ---: |
| calls the kernel in place is to make | 12,921 | 4,182 | 8,586 | 16,877 |
| made | 12,883 | 4,182 | 8,556 | 16,876 |
| **with every argument's conversion** | **12,865 (99.57%)** | **4,131 (98.78%)** | **8,415 (98.01%)** | **14,150 (83.84%)** |
| of them plain | 12,865 | 4,131 | 8,179 | 12,815 |
| left to T for an argument | 0 | 0 | 25 | 0 |
| records and conversions that differ from T's | 0 | 0 | 0 | 0 |
| **expressions decided on the window** | **43,876 (86.04%)** | **9,135 (84.28%)** | **37,304 (85.92%)** | **32,853 (63.16%)** |
| of them refused, as C refuses them | 147 | 165 | 1,899 | 1,213 |
| differ from C | 0 | 0 | 0 | 0 |
| listed for a call | 18 | 47 | 157 | 2,107 |

On the compiler's own source a sixth of the calls pass a vector or a struct
by value, whose conversion is the action's.

The 9700X, CPU 2, under the lock, Release; main `bc66290c5` and the piece
on it, in turn in each reading; medians of seven readings, musl's of
fifteen. TSC ticks:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| **the kernel in place: main** | **16.31** | **20.69** | **18.89** |
| **with the piece** | **17.52** | **22.36** | **20.36** |
| the same two builds with C off | 15.94, 17.63 | 20.63, 22.36 | 19.00, 20.36 |
| **C's three sweeps, a record of the decided: main** | **12.47** | **18.88** | **34.09** |
| **with the piece** | **8.17** | **11.68** | **26.55** |
| with the bytes' columns: the gate's figure | 9.18 | 13.42 | 31.20 |
| the reference's C on the same expressions: main, with | 37.7, 38.5 | 34.2, 36.6 | 41.2, 40.6 |
| C's sweeps, a record they ran over: main, with | 5.38, 5.79 | 6.63, 7.14 | 10.09, 12.16 |
| of which the loop | 2.42, 2.60 | 3.69, 4.03 | 5.89, 7.78 |
| the targets | 1.23, 1.38 | 1.42, 1.58 | 1.99, 2.54 |
| an expression's part | 1.73, 1.73 | 1.52, 1.50 | 1.95, 2.02 |
| the tests on the window, C off: main, with | 31.4, 31.1 | 49.7, 49.0 | 91.6, 96.6 |
| the front with the window: main, with | 53.1, 53.4 | 70.8, 70.9 | 90.9, 89.9 |

`-c sqlite3.c` (nothing of the piece runs in it): wall +0.05% and -0.30%
in two runs of 21 processes a build (the base again -0.02% and -0.45%).

Against [its
gates](https://github.com/buster14a/buster/issues/2850#issuecomment-6995139458):

- **Exact (A1, B1): met.** No record, no argument's conversion and none of
  C's answers differs, on the four corpora with the default warnings and
  with `-Wall -Wextra`, the fixtures, and 608 statements one at a time (44
  of them the checker rejects).
- **The kernel keeps its calls (A2): met.** Every argument's conversion is
  made for 99.57%, 98.78%, 98.01% and 83.84% of the calls (98, 95, 97 and 80
  asked), and it leaves 0, 0, 25 and 0 to T for an argument.
- **The kernel's cost (A3): not met.** 1.0 tick a record over main was
  asked on `sqlite3.c`, to be refuted at 2.0: it reads 17.52 for main's
  16.31, 1.21 over (each reading's own difference 0.96 to 1.27), and 1.69
  over with C off (17.63 for 15.94).
- **Decided (B2): met** (43,876, 9,135, 37,304 and 32,853 for 43,000,
  8,800, 36,500 and 32,000).
- **C's cost (B3): met.** C's sweeps with the records' bytes read 9.18 ticks
  a record of the decided on `sqlite3.c` (8.94 to 9.32 over seven readings;
  10.0 asked, refuted at 12.0; 13.9 before the piece).
- **Nothing else moves (B4): met on `sqlite3.c` and Lua, not on musl.** The
  tests on the window read 31.4 and 31.1, and 49.7 and 49.0; the front
  with the window 53.1 and 53.4, 70.8 and 70.9, 90.9 and 89.9. On musl the
  tests on the window read 91.6 for main and 96.6 with the piece; in a turn
  of main, the piece, the piece and main again, 91.9, 97.4, 98.3 and 90.3.
  **It is the executable's, not what the piece runs:** one build with a
  switch reads 98.8 and 94.9 as pushed (the same setting twice), and 95.8,
  97.5 and 96.4 with the arguments' pass off, then its tables' clearing
  too, then the types' answers too. Every part of the tests reads more,
  the census's code on T's records among them, which is main's source.
  What in the executable is not established.

**What stays scalar, and why.** An argument's key is four loads at computed
addresses (its record's type, flags and standing, and its target), its slot
three more and a store, one argument after another: about 10 ticks
an argument (1.2 a record of the window's 289,838 over its 33,150
arguments). It was not built with gathers, for what two measured figures
say: on this machine a gather's lane costs about what a scalar load does
(C's targets' sweep, one 16-lane gather and a store, reads 1.2 to 1.4 ticks
a record), and eight arguments by gathers are nine 8-lane gathers and a
scatter, no fewer loads than the loop's. The rule itself runs on a miss
alone.

**Tests.**

- **The fixture** (`test-checker`, C on the window's own) has 263 calls, by
  what the rule makes of their arguments: made and plain, made and refused
  by C, kept, left, left inside another call (once, twice over, past the
  parameters, beside one the first pass leaves), and calls five deep that
  the rule alone makes. Pinned: the calls to make, made, with every
  conversion, plain and left for an argument, and C's decided, refused and
  listed by reason.
- **The comparison's control** changes T's conversion of each converted
  call's arguments too, and the kernel's comparison has to count each, once
  over poison and once over decoys: 426 for the 213 such calls.
- **The slots as one:** the fixture runs again with each answer written to
  every slot, so a key's probe meets another key's answer wherever the last
  answer was another's: 2,307 answers where the slots apart make 105, with
  the same records, counts and answers of C.
- **An unwritten conversion is poison:** every record's last quadword,
  before each run.
- **Faults,** one at a time in a copy of the tree, seven rounds as the code
  changed: of the 278 whose text is in these sources, **241 are caught**. Of
  the 37 others, 13 guard a program the checker rejects (the kernel has to
  leave it to T, and the shadow compares no rejected program), 19 are terms
  of T's rule or of C's that what makes the records never separates, 4 give
  the same answer without them (a spared probe, a tripwire nothing trips),
  and 1 waits for #3014. The record names each with what makes it so.
- **What the rounds changed:** dead terms out, each with what makes it dead
  named where it was; fixture lines for what the survivors showed untested
  (a const struct's member array, a callee with an offset, a vector with a
  GNU constant's flag, a floating constant of another type, the two passes'
  order); and the mark of a left call, taken out as dead and put back.
- **Before pushing,** in the cloud container on these sources: a native
  Release build; `test-checker`'s eight parts; C on the window on the four
  corpora with the default warnings and with `-Wall -Wextra`; the shadow's
  eleven other modes on `sqlite3.c` and Lua; the self-check, the self-parse
  and the call graph; the build's ownership check; the complete driver's
  lists (one item fails in this container as in every run before the
  piece: its clang spells its include directory another way); and the
  sanitizer rows a pull request's checks skip (memory, address and thread,
  native): the node machine's tests' part, and under memory and address C
  on the window on `sqlite3.c`, Lua and the compiler's own source. No
  report. After the rebase on main `041cfd9ed`, which changed none of the
  piece's files: the build, the node machine's tests' part and the four
  corpora again.

**What is left, counted.**

- **Listed still:** an update (3,535 expressions on `sqlite3.c`), `&&`,
  `||` and `?:` (2,721), a value the columns don't hold whole (529), a
  division by a constant (167), a comma or a selection (150).
- **A unit's first answers** are not in the timed run (above).
- **A constant the fold leaves two fields in** is a value the columns don't
  hold whole, and its call is T's: `(char*)5` and `(int)1.5`. What the
  probes found beside it, `(int)0.5` read by its exponent and a zero that is
  no literal taken as a null pointer constant, is the next section's.
- **X reads none of it, and no compile takes any.** A call's depth is X's
  piece's.

### A null pointer constant that is no literal

Three defects in the tests, found by running statements one at a time for the
call piece and fixed together
([#3012](https://github.com/buster14a/buster/issues/3012),
[#3014](https://github.com/buster14a/buster/issues/3014),
[#3017](https://github.com/buster14a/buster/issues/3017)). Each was in the
shadow alone: no compile takes the tests' verdicts yet. No corpus has a case
of any of them, and no fixture had one.

**What was wrong.**

- **The warning's test covered a part of it (#3012).** The compiler warns of
  a zero that is no literal `0` made a pointer
  (`-Wnon-literal-null-conversion`, on by default): `ip = RED;`,
  `int* np = 1 - 1;`, `fp((char)0);`, `return (char)0;`. The census's test
  had `=`, an initialization's node and a conversion, and only where the
  source was neither a leaf nor a cast. So the tests took an enumerator and
  every cast in `=` and `return`; every form as a scalar's initializer, whose
  conversion is in its plan and at no node; and every form as an argument.
  Of 253 such statements the checker accepts, 136 ended with a take the
  census refuses.
- **The reference took some of them too (#3017).** The census after the
  pipeline refuses an expression with a warning's event between its nodes'
  tokens. The compiler puts this warning at its source's first token where
  that is a parenthesis (a cast's, a group's) or `_Generic` or
  `__builtin_choose_expr`, which is no node's token; and a scalar's
  initializer's root is the bare expression, with no node before it as an
  assignment's, a call's or a return's has. So of 42 initializer forms the
  compiler warns of, the reference saw the event of 18, and the shadow
  showed nothing for the other 24.
- **A constant's value was read from a field that isn't its own (#3014).**
  The fold of a floating constant to an integer leaves the floating storage,
  and T's record keeps the one storage that is not zero: `(int)0.5` is an
  integer constant whose payload and exponent are the floating value's, its
  integer being zero. The standing's field bits say so. Two readers didn't
  look at them: the tests' sweep of the window, which reads a constant's
  value from its exponent (`(int)1e-4900L` read as 104, a quiet divisor,
  where the compiler warns of a division by zero); and the kernel's operand,
  from which a store made a null pointer anew where the action's keeps the
  floating storage (`ip = (int)0.5;` differed from T's record). (The
  kernel's call rule gathers its arguments' said values too, but no call
  with such an argument is its own: the arguments' rule leaves it to T's.)

**The rule is the compiler's own, called.** `node_census_null_warned` asks
`check_warning_null_integer`, which is what raises the warning (the
destination a pointer; the source an integer constant expression of an
integer type that is no bool, of value zero, with no fault), and that the
value has no literal's flag. The warning's own test of the tokens, by which a
lone number in parentheses is a literal, is not copied: a number's leaf has
the flag and parentheses make no node. It is asked of `=`, an
initialization's node and a conversion, with the type the node converts its
source to; of each argument of a call, with its own conversion's type; and of
a scalar's initializer's root, with its object's type.

**Where each form asks it.**

| | what it does |
| --- | --- |
| the census's code, the reference at both times | the call above |
| the tests in the machine's own form | send the record to the census's code where the conversion's type is a pointer's and the source an integer constant without the literal's flag; no second statement of the test |
| the loop on the window | lists `=` and a return's conversion to a pointer already: no quiet rule takes a pointer's destination |
| the kernel's call rule | the arguments' rule's answer says whether the argument is such a constant to a pointer's parameter, and a call with one is not quiet |

An argument is asked against its own parameter: `f(RED, 0)` to an integer
and a pointer is quiet, and `f(0, RED)` is listed. A call's quiet bit is the
short path's and the answers are the second pass's, which takes the bit away.

**An integer constant whose storage is not its integer is no record of the
columns'.** T writes a value into its columns where they hold it whole: at
most one of its integer, its offset and its floating value no zero. It asks
now that an integer constant's be its integer (`node_machine_whole`): one
folded from a floating constant goes to the wide values, as a value with two
storages does. So no reader of the columns meets it. The sweep reads no said
value of it, for a wide record says none and its operator stays listed; the
kernel leaves what consumes it to the rule, which is T's action; and C on the
window lists its expression as a value the columns don't hold whole. Neither
loop has a term for it.

A first form of the fix had two: a mask in the sweep, over the records whose
payload is a floating value's, and a test of each operand in the kernel's
rule and in its updates' loop. With one executable and a switch that turned
them off, on the 9700X, the sweep's bytes read 0.02 ticks a record more with
them and the kernel in place 0.27 on Lua (0.06 to 3.15 over seven readings;
0.02 and 0.03 on `sqlite3.c` and musl): some eight instructions an operand,
and the rule alone makes 12% of Lua's records, 3% of `sqlite3.c`'s. The
writer's test is in a path T takes for a value the action made, some 7,000
times on `sqlite3.c`.

The fold itself is as it was: clearing the storage it leaves is a change to
every compile, with its own check, and
[#3014](https://github.com/buster14a/buster/issues/3014) has the counts
(66 addresses and 11 floating constants with a stale integer on `sqlite3.c`,
each a value the columns don't hold whole for that alone).

**An initializer's span after the pipeline** starts at the token after its
object's name, so the events before its first node's token are its own. At
reader time the span stays the nodes': the events the reader has when it pops
a root are at tokens it has read as nodes, and the warning the pipeline puts
before them has its own test there, the one above. A warning's event there
with no test of its own would show in the shadow as a take the census
refuses.

**Counts.** On `sqlite3.c`, Lua and musl, with the default warnings and with
`-Wall -Wextra`, every count of the shadow with C on the window is what it
was: no call lost its quiet bit, no constant its said value, no initializer
an event. The arguments' answers are 1,726, 1,393 and 8,250 for 1,669, 1,393
and 8,209: the key has one flag more.

**One statement at a time.** 59 sources of a zero, each alone in a function
in five places (`ip = X;`, `int* np = X;`, `fp(X);`, `f2(a, X);`,
`return X;`), through the shadow with `--verify`: 295 statements, of which
the checker rejects 42. The compiler warns of 42 of the sources, in every
place, and clang 23 warns of the same 210 statements.

| of the 253 accepted | before | now |
| --- | ---: | ---: |
| end with a take the census refuses | 136 | 0 |
| end with a said value that is not its payload's | 2 | 0 |
| warned initializers whose event the reference has | 18 of 42 | 42 |
| warned statements the reader-time tests refuse | 32 | 190 |

Before, the tests refused the 16 sources that are neither a leaf nor a cast,
under `=` and `return` alone. Now they refuse the same 38 sources in each
place, and nothing the compiler doesn't warn of. The other four are no
expression of the reader's: its front leaves a character constant and a
`sizeof`, and the set has no `_Generic` and no `__builtin_choose_expr`.

And 72 statements with a constant folded from a floating one under each
operator (an operand of the arithmetic, shift, bitwise, comparison, logical
and conditional operators and of their compound forms, a pointer's addend
and index, a cast's operand, an argument, and a null pointer assigned,
compared and passed on), of which the checker rejects one:

| of the 71 accepted | before | now |
| --- | ---: | ---: |
| end with a status: a said value that is not its payload's | 71 | 0 |
| of them, a record the kernel made otherwise than T | 8 | 0 |
| of them, an operator the sweep left out where the census has a test | 2 | 0 |
| of them, a take the census refuses | 9 | 0 |

**On the 9700X,** the piece against the build before it, in turn (ticks a
record, medians of 7 readings on `sqlite3.c` and Lua, 15 on musl):

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| the kernel in place: before, with | 17.39, 17.38 | 22.35, 22.41 | 20.49, 20.38 |
| each reading's own difference | -0.02 (-0.15 to 0.16) | 0.03 (-0.24 to 0.18) | 0.02 (-1.80 to 3.54) |
| C's three sweeps, a record of the decided: before, with | 8.27, 8.07 | 11.62, 11.62 | 25.77, 25.96 |
| the tests on the window, C off: before, with | 31.3, 31.3 | 50.1, 49.8 | 99.9, 100.3 |
| the tests' bytes alone: before, with | 9.16, 9.25 | 10.82, 10.88 | 20.42, 19.17 |
| the front with the window: before, with | 53.2, 53.1 | 71.7, 70.7 | 89.6, 91.5 |

`-c sqlite3.c`, which runs none of it, read 0.16% and 0.09% more wall time
with the piece in two comparisons where the base again read 0.08% less and
0.05% more.

**Tests.**

- **The fixtures** have 51 lines for it with the default warnings, each with
  what the census must do with it at both times: the four places, and the
  forms that warn beside those that don't (a literal, in parentheses too, a
  bool's zero, a pointer's); an argument and its own parameter both ways
  round; an atomic pointer assigned; an initializer that starts with a
  parenthesis, which the event refuses after the pipeline and the constant's
  test at reader time (a `// reader: TEST` marker says so); a for
  statement's clauses a line each, whose first one's event is no other's;
  and the constant folded from a floating one as a divisor, a shift's count,
  an argument and a null pointer. Five lines with the warning off take
  everything. Ten lines in C on the window's fixture have the folded
  constant as an argument and as a null pointer assigned, and a null that is
  no literal as an argument.
- **Faults.** Put into the new code one at a time, in a copy of the tree, in
  four rounds as the code changed, the last on these sources with every
  earlier fault whose text is still in them. Of its 43, 37 are caught. Of
  the other 6: 3 are terms no statement separates (an assignment's own type
  is its source's conversion's on every statement, an atomic destination's
  too; no initialization's node is in an expression the tests read, the
  reader's root for one being its operand; and the front's line of a leaf's
  value asks the columns' rule of a literal's or a string's value, which has
  its own storage alone); 2 give the same answer (the span's low end a token
  either way: no kept event is at an object's name or at its `=`); and 1
  guards a program the checker rejects (an lvalue's constant that is no
  integer constant expression, to a pointer's parameter). The record names
  each with what makes it so. With the writer's rule taken out, the fixture
  shows what the issue found: 2 operators left out where the census has a
  test, and 14 said values that are not their payloads'.
- **What the rounds changed:** five terms out as dead, each a guard the
  compiler's own test has or one no record reaches (among them a guard of
  the kernel's gathered arguments: no call with such an argument is the
  short path's); the count of the initializers' roots asked, which made two
  pre-filters' faults caught; the warning of the event that refuses an
  expression, in the census's record, so that a fixture's
  `// refuse: warning-event` line asks for an event; lines for an atomic
  pointer, a pointer's first parameter and the for statement; and, after
  the 9700X's switch, the writer's rule for the two readers' terms.
- **Before pushing,** in the cloud container on these sources: a native
  Release build; `test-checker`'s eight parts; C on the window on the four
  corpora with the default warnings and with `-Wall -Wextra`; the shadow's
  eleven other modes on `sqlite3.c` and Lua; the self-check, the self-parse
  and the call graph; the build's ownership check; the complete driver's
  lists (one item fails in this container as in every run before the piece:
  its clang spells its include directory another way); and the sanitizer
  rows a pull request's checks skip (memory, address and thread, native):
  the node machine's tests' part, and under memory and address C on the
  window on `sqlite3.c`, Lua and the compiler's own source. No report.
  After the rebase on main `a847fd0e6`, which changed none of the piece's
  files: the build, the node machine's tests' part, the ownership check and
  C on the window on `sqlite3.c`, Lua and musl again.

### An update on the window

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/an-update-on-the-window-2850-2026-11-19).
`measure-node-machine --shadow --window-contexts`
([the guide](commands/lowering.md#an-update-on-the-window-2850)).

Of what C on the window still listed after the calls, the largest part was
an update's (`++`, `--` and the compound assignments): 3,535 of 7,120
expressions on `sqlite3.c`. This piece makes C's answers for them on the
window, by the loop's masks. No compile takes any of
it. Its [specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7011006204)
had four gates: exact (U1), what it decides (U2), what C's sweeps cost (U3)
and that nothing else moves (U4). The form it specified was refuted on U3 and
is not the one here; [the note of that](https://github.com/buster14a/buster/issues/2850#issuecomment-7013427495)
is on the issue, and "The form that was refuted" below has its readings.

**C's plan of an update, term by term.** C asks its plan (`node_machine_update_plan`) of an update whose record is of a
number's class and is not read for its address, and takes the update where
the plan does. On the window each term is a byte's, or needs none:

| The plan's term | On the window |
| --- | --- |
| the record's class is a number's | its type's class's byte (`numeric`); a record the kernel didn't place has none |
| the record is not read for its address | no mask: an update's value is no lvalue and no struct's, and each operator that takes its first operand's address asks for one |
| its shape: a destination, and a second operand unless it is `++` or `--` | no mask: the front's |
| the destination is an lvalue | no mask: the checker takes no update of anything else |
| the destination's class is a number's, with no aggregate's, `_BitInt`'s or complex fact | no mask: the update's own record has its destination's value's type, and the loop asks only of a record of such a class with none of those facts |
| the destination is no atomic's | its facts' byte's bit |
| it is no bit-field | no mask: a bit-field's record is a value the columns don't hold whole, which lists the expression |
| it is no vector's element | no mask: a vector's element has no template of its own, which refuses the expression |
| a destination that is a name's is an object's the unit knows, and no register variable's | a new bit of the flags' byte, the use's `BUSTER_CHECK_REGISTER`; at reader time the unit knows every object |
| the computation's type and the second operand's conversion are of a number's class with none of those facts, unless the update is `++`, `--` or a pointer's | the second operand's own class's byte: two operands of a number's class convert to one |
| the destination is in no segment's space | a record at a time, in a unit whose type table has an extended qualifier at all |
| a pointer's stride is not zero | a record at a time, in the loop the pointers' sums ask theirs in |
| a division or a remainder by a power of two is no signed one's | `/=` and `%=` of an integer by an integer constant are listed, as `/` and `%` are |

**A mask only takes.** An update the masks don't take lists its expression,
and what C's plan makes of it is the reference's to say. So a term the masks
read more strictly than the plan costs a listed expression and nothing else.

**What is new in the loop.**

- **Three bits.** Two of an operator's class, in the classes' table the loop
  already reads by a permute: `++` and `--` (no second operand), and `/=` and
  `%=` (the plan asks their divisor's power). One of a record's flags' byte,
  made in the bytes' sweep: a register object's use.
- **An update's key.** Its operands' rows are four where its template is
  one, by whether its destination is evaluated for its address (it is, unless
  the update names it) and whether it is a pointer's (its second operand is
  then made a long). The key is the template's number and those two bits, by
  two masked adds; the loop keeps the keys in a register for the block below,
  and the column it stores has C's templates. A consumer out of the two
  registers' reach is asked its key again from the bytes, and only its
  address's bit: the pointer's is read by the second operand, the record
  right before the update's own.
- **A pointer's update's stride** is asked with the pointers' sums', in their
  loop, its body unchanged: the update's record holds T's conversion of its
  destination, the pointer's type without its qualifiers, as a sum's holds
  its first operand's.
- **A segment's space.** In a unit with `__seg_gs` or another extended
  qualifier the loop asks `check_lower_segment_override` of each masked
  update's destination's type, a record at a time. No unit of the corpora has
  one.

Eight vector operations of a step of 64 records are the update's, where the
first masks had nineteen: two compares (an update's class; a destination that
is a name's, which a store's mask compares too), three tests of a bit (the
class's two and the register's), a move of the template's number and the two
adds of the key.

**What it decides.** `sqlite3.c`, Lua and musl, the same with the default warnings and with
`-Wall -Wextra`:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| updates C asks its plan of | 3,736 | 861 | 3,969 |
| of them a pointer's | 527 | 369 | 846 |
| **taken by the masks** | **3,734** | **861** | **3,969** |
| **expressions decided: before, with** | **43,876, 47,266** | **9,135, 9,877** | **37,304, 40,937** |
| of the expressions the kernel places | 86.04%, 92.69% | 84.28%, 91.12% | 85.92%, 94.29% |
| listed still: `&&`, `\|\|`, `?:` | 2,744 | 493 | 1,787 |
| a comma or a selection | 263 | 375 | 169 |
| a division by a constant | 170 | 40 | 261 |
| a value the columns don't hold whole | 529 | 6 | 77 |
| an update | 6 | 1 | 27 |
| a call | 18 | 47 | 157 |

The two updates of `sqlite3.c` the masks leave are two the plan refuses. The
other listed updates are `/=` and `%=` by a constant. The compiler's own
source has 6,103 updates, all taken, and 38,020 of its 51,842 expressions
decided (73.34%): 11,222 of the rest are listed for `&&`, `||` or `?:`.

**The form that was refuted.** The specification asked the plan of each update a record at a time: a rule
that read the kernel's records, the types' words and the destination's
declaration's bits. It was exact and decided the same expressions. On the
9700X, against the build before in the same job:

| C's sweeps with the records' bytes, a record of the decided | `sqlite3.c` | Lua |
| --- | ---: | ---: |
| before | 9.10 | 13.19 |
| **with a rule a record** | **10.14** | **14.60** |
| the gate's limit, and where it is refuted | 9.1, 9.6 | 13.3, 13.8 |

The rule alone, run again over each chunk's updates, read 75.4 ticks an
update on `sqlite3.c` and 82.2 on Lua, an update being 1.16% and 0.94% of the
records swept. What in the rule cost that was not measured.

**What it measured.** The 9700X, CPU 2, under the lock, Release; the build before (#3023's code)
and the piece in turn; medians of seven readings, musl's of fifteen. TSC
ticks:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| **C's sweeps with the records' bytes, a record of the decided (U3's figure): before, with** | **9.12, 8.90** | **12.93, 13.39** | 30.42, 27.54 |
| C's three sweeps alone, a record of the decided: before, with | 8.28, 7.94 | 11.52, 11.91 | 27.49, 25.54 |
| the reference's C on the same expressions | 38.4 | 36.8 | 41.0 |
| the loop, a record swept: before, with | 2.66, 2.97 | 3.98, 4.77 | 8.03, 8.78 |
| the same in a second run over the same chunk | 1.95, 2.16 | 2.17, 2.38 | 2.27, 2.62 |
| the kernel in place, a record: before, with | 17.56, 17.41 | 22.43, 22.43 | 20.49, 20.37 |
| the tests on the window, C off: before, with | 31.5, 31.2 | 50.3, 49.6 | 99.0, 94.7 |

**One executable, the update's block on and off.** Two builds differ in more
than the code one of them adds, so an experiment's build has a switch that
skips the update's block by a branch and lists every update's expression, as
before the piece. On, off, off, on in each of seven readings; each reading's
own difference:

| the block on, less off | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| the loop, a record swept | +0.19 (0.09 to 0.27) | +0.26 (0.13 to 0.35) | +0.14 (-1.22 to 2.22) |
| the same in a second run | +0.15 (0.13 to 0.25) | +0.17 (0.16 to 0.21) | +0.17 (0.12 to 0.21) |
| **C's three sweeps, a record of the decided** | **-0.36 (-0.46 to -0.16)** | **-0.52 (-0.70 to -0.35)** | -2.70 (-5.58 to 1.81) |

With the block off the experiment's executable reads the loop at 2.85 and
4.53 ticks a record swept, where the build before read 2.66 to 2.73 and 3.98
to 4.06 in four jobs: more than half of what the two builds differ by on Lua
is there with the update's block skipped. What that part is was not
established. The loop's function is 6,140 bytes in the piece's build and
4,920 in the one before.

**Against its gates.**

- **U1, exact: met.** The fixtures, 250 statements run one at a time in a
  unit with a type in a segment's space and in one with none, and the three
  corpora and the compiler's own source with the default warnings and with
  every warning: every expression C on the window decides has C's verdict on
  T's columns, and each record of one it takes C's template, context, target
  and whether it is evaluated.
- **U2, decided: met.** 47,266, 9,877, 40,937 and 38,020 expressions on
  `sqlite3.c`, Lua, musl and the compiler's own source, for 47,100, 9,800,
  40,700 and 38,000 asked.
- **U3, C's cost: met on `sqlite3.c`, at its limit on Lua.** 8.90 ticks a
  record of the decided for 9.1 asked; 13.39 for 13.3, below where it is
  refuted (13.8). In one executable the piece lowers the sweeps' cost a record
  of the decided on both, by 0.36 and 0.52.
- **U4, nothing else moves: met.** The kernel in place and the tests on the
  window read what they did.

**What stays a record at a time, and why.**

- **A pointer's stride:** a lane of the loop the pointers' sums already have.
  Its answer is its type's row's, a load at a computed address a lane; 7,491
  lanes on `sqlite3.c`'s 321,488 records swept, 527 of them an update's.
- **A destination's space:** only in a unit that has an extended qualifier,
  which none of the corpora's is.

**Tests.**

- **The fixtures** (`test-checker`, part 5 of 8). C on the window's has a
  line for each case of each term: by name, through an address, a pointer's
  both ways, each class, what the plan refuses, a register variable in
  parentheses and as a struct's and a pointer's base, a qualified pointer, a
  second operand that is a wrapper's record of a long double (which has a
  template where the plan refuses) and of a 128-bit integer under a pointer
  (taken), a consumer past two blocks, and each update's records with a
  block's end between every two. A second fixture is a unit with a segment's
  space, its updates in the flat space and in a segment's in turn. The
  updates asked and taken are pinned.
- **Statements one at a time:** 251, each alone in a function, with the
  shadow's comparison on: 250 the checker accepts, none with a difference,
  in either kind of unit.
- **Faults,** one at a time in copies of the tree, three rounds as the code
  changed. The last, on these sources: 60 of 61 caught. The other is the
  test of whether the unit has an extended qualifier, made always true: the
  same answers, a loop a step more.
- **What the rounds removed,** each a term no accepted program separates and
  so not a mask's: the destination's standing (a permute and a far lane's
  row, for "an lvalue"); the object's flag beside the register's; `placed`
  and the address's context in what the loop asks of; the pointer's fact's
  own mask where `left_pointer` says the same of an update; and a far
  consumer's key's pointer's bit.

**What is listed still.** `&&`, `||` and `?:` are most of it: 2,744 of the 3,730 expressions listed on
`sqlite3.c`, 493 of 962 on Lua, 1,787 of 2,478 on musl. Then a comma and a
selection, a division by a constant, and a value the columns don't hold
whole. Those, then X on these answers.

### A condition's branches on the window

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/a-conditions-branches-on-the-window-2850-2026-11-19).
`measure-node-machine --shadow --window-contexts`
([the guide](commands/lowering.md#a-conditions-branches-on-the-window-2850)).

After the updates, `&&`, `||` and `?:` were most of what C on the window
still listed: 2,744 of the 3,730 expressions listed on `sqlite3.c`. They are
two cases in C. This piece is the first: a statement's CONDITION over `&&`,
`||` and `!`, which C evaluates as branches and no value
(`NODE_MACHINE_TEMPLATE_BRANCHES`). No compile takes any of it. Its
[specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7016320819)
had four gates: exact (N1), what it decides (N2), what it costs by one
executable with a switch (N3) and that nothing else moves (N4).

**C's rule, term by term.** C branches a record from the root down
(`node_machine_root`, `node_machine_branch_condition`,
`node_machine_classify_branches`, `node_machine_branch_operator`). On the
window each record says it of itself, from its own bytes, its first operand's
and its consumer's class, and lists what one level cannot say:

| C's term | On the window |
| --- | --- |
| a branch operator is a `!`, an `&&` or an `\|\|` | its operator's class's byte |
| that is no constant without effects | `free`: its standing's and its flags' bytes |
| whose value is an int's and no lvalue | no mask: the checker's, which takes no vector's `!`, `&&` or `\|\|` and gives any other an int |
| and, for `&&` and `\|\|`, whose first operand's truth is not known | the first operand's standing (no constant, or an lvalue whose constant is an address) and its flags' object's bit (an lvalue at an absolute address has none: C knows its truth only where it is an array's, since [#3028](https://github.com/buster14a/buster/issues/3028)'s fix, and the loop lists its expression either way) |
| a statement's CONDITION is branches where, under its `!`s, there is a branch `&&` or `\|\|` | where its operand's code is `&&`'s or `\|\|`'s. One that is no branch operator lists the expression at its own record, and with a `!` between, the `!` does |
| a branched operator branches each operand that is a branch operator | `&&` and `\|\|` under a CONDITION, an `&&`, an `\|\|` or a `!`, and `!` under an `&&` or an `\|\|`: by the consumer's class alone. A consumer that is not branched is a value: an `&&` or an `\|\|` lists the expression there, and a `!` does where its operand is `&&` or `\|\|` |
| a `!` under a branched `!` is branched too | two levels away: the branched `!` over a `!` lists the expression |
| any other operand is its value, converted to T's conversion of it and tested: of a type a truth's entries test | the aggregate's fact of each operand's own type: a `__float128` has it, and its record a template all the same. Its class asks no mask: a scalar of no class a truth tests is a half-precision float, of which no record has a template |
| a branched record has no target | by its own branched bit |
| its labels, its hook and its sense | not made here: X's |

**A mask only takes.** An `&&` or an `||` that is not branched is a value,
and a value lists its expression in this piece. So a consumer may take its
operand for branched from the operand's code alone, and the reference says of
every listed expression.

**Against the specification.** It read a consumer's two bits (a branch `&&`
or `||`; a branched `!`) by two permutes of bits made in the same step. The
loop reads the consumer's class instead, which it has already: a consumer of
that class that is not branched lists the expression at its own lane, and its
operand's answer is then read by no one.

**What is new in the loop.**

- **A class and a bit** of the classes' table: a conditional has a class of
  its own (it had `&&`'s and `||`'s, and is listed still), and a statement's
  CONDITION a bit where `!` has none.
- **A first operand's standing** is read through its distance back as its
  other bytes are: one more permute, and a row more of the far lanes'.
- **A branched record's template** is one masked move, and its operands' rows
  two of the rows' table: each evaluated, a value's to T's conversion of it.
- **A truth's operand's test** is the aggregate's fact alone, where it was
  three operations: the class's two bounds are implied (the table's row), for
  a truth that is no branch too.

Twelve vector operations a step more: fourteen for the piece (the permute, a
compare for `&&`'s and `||`'s class, two of the consumer's class, a test of
the CONDITION's bit, three for the first operand's code, three for its
standing, a test of its object's bit, a test of the second operand's fact and
the template's move), and two fewer in a truth's operand's test.

**What it decides.** `sqlite3.c`, Lua, musl and the compiler's own source,
the same with the default warnings and with `-Wall -Wextra`:

| | `sqlite3.c` | Lua | musl | own source |
| --- | ---: | ---: | ---: | ---: |
| expressions the kernel places | 50,996 | 10,839 | 43,415 | 51,908 |
| records evaluated as branches | 5,613 | 570 | 2,364 | 13,975 |
| of them a statement's CONDITION | 2,198 | 230 | 877 | 3,366 |
| **expressions decided: before, with** | **47,266, 49,313** | **9,877, 10,079** | **40,937, 41,759** | **38,064, 41,314** |
| of the expressions placed | 92.69%, 96.70% | 91.12%, 92.99% | 94.29%, 96.19% | 73.33%, 79.59% |
| listed still: `&&` and `\|\|` as values, `?:` | 674 | 290 | 964 | 7,924 |
| a comma or a selection | 264 | 376 | 169 | 0 |
| a division by a constant | 170 | 40 | 262 | 435 |
| a value the columns don't hold whole | 551 | 6 | 77 | 66 |
| an update | 6 | 1 | 27 | 9 |
| a call | 18 | 47 | 157 | 2,160 |

**One executable, the piece's block on and off** (N3's measure). Two builds
differ in more than the code one of them adds, so an experiment's build has a
switch that skips the block by a branch and lists every `&&` and `||`, as
before the piece: with it off the executable decides what the build before
does (47,263, 9,877 and 40,937 expressions on the 9700X's copies). The 9700X,
CPU 2, under the lock, Release, TSC ticks; on, off, off, on in each of seven
readings, and each reading's own difference:

| the block on, less off | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| **the loop, a record swept** | **+0.24 (0.10 to 0.35)** | **+0.27 (0.05 to 0.50)** | -0.12 (-1.11 to 1.93) |
| the same in a second run over the same chunk | +0.21 (0.19 to 0.26) | +0.23 (0.22 to 0.46) | +0.24 (-1.38 to 0.30) |
| **C's three sweeps, a record of the decided** | **-0.36 (-0.66 to -0.31)** | **-0.20 (-0.56 to 0.15)** | -1.91 (-3.06 to 2.76) |
| the kernel in place, a record | +0.09 (-0.19 to 0.21) | -0.04 (-3.59 to 2.00) | -0.02 (-0.12 to 0.08) |
| the tests on the window, a record | +0.15 (-0.23 to 0.41) | -0.91 (-2.53 to 0.61) | -3.46 (-12.06 to 1.15) |

musl's first runs don't resolve the block: an earlier build's switch read the
loop +1.59 there (-0.60 to 3.10) in this order and -0.29 (-1.34 to 3.74) with
the settings in the other order (off, on, on, off).

**Build against build**, the build before (#3025's code) and the piece in
turn, medians of seven readings, musl's of fifteen:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| C's sweeps with the records' bytes, a record of the decided: before, with | 8.86, 8.38 | 13.55, 13.78 | 29.58, 30.72 |
| C's three sweeps alone, a record of the decided: before, with | 7.92, 7.46 | 11.97, 11.92 | 26.42, 26.66 |
| the reference's C on the same expressions | 38.7 | 37.2 | 41.4 |
| the loop, a record swept: before, with | 2.97, 3.19 | 4.82, 5.17 | 9.05, 9.55 |
| the kernel in place, a record: before, with | 17.66, 17.37 | 22.40, 22.40 | 20.50, 20.39 |
| the tests on the window, C off: before, with | 31.4, 31.3 | 49.7, 49.6 | 94.7, 94.7 |

`-c sqlite3.c`, which runs none of it: wall -0.08% and +0.10% in two
comparisons, where the base again read -0.09% and +0.15%.

**The loop's frame.** By its layout in the 9700X's builds, the loop's main
body is 819 instructions a step with the piece and 746 before, and 256 and
219 of them have an operand in the frame: of the 73 instructions the piece's
build has more, 37 are frame traffic, where the block's own operations are
fourteen. A first build of the piece, whole runs' user instructions counted:
the switch's executable with the block off retired 1.76 instructions a record
swept more than the build before on `sqlite3.c` and 1.73 on Lua. What a step
keeps in the frame, and why, is the next thing to measure in this loop.

**Against its gates.**

- **N1, exact: met.** The fixtures, 257 statements run one at a time in a
  unit with a type in a segment's space and in one with none, and the four
  corpora with the default warnings and with every warning: every expression
  C on the window decides has C's verdict on T's columns, and each record of
  one it takes C's template, context, target and whether it is evaluated.
- **N2, decided: met.** 49,313, 10,079 and 41,759 expressions on `sqlite3.c`,
  Lua and musl, for 49,200, 10,050 and 41,650 asked; 41,314 on the
  compiler's own source, where the text that read 38,020 at the specification
  reads 38,064 before the piece now, for 40,900 asked.
- **N3, cost: met.** In one executable the block lowers C's three sweeps a
  record of the decided on `sqlite3.c` and Lua, by 0.36 and 0.20 (refuted at
  0.3 over); the loop reads 0.24 and 0.27 ticks a record swept more (0.5
  asked, refuted at 0.8).
- **N4, nothing else moves: met.** The kernel in place reads within 0.1 of
  itself on and off. The tests on the window, which the switch doesn't touch,
  read +0.15 and -0.91 a record on `sqlite3.c` and Lua, each range across
  zero.

**Tests.**

- **The fixture** (`test-checker`, part 5 of 8): 82 statements, a line a case
  of each term, each run alone too with its counts summed to the fixture's
  pins: each operator under each and a loop's three conditions; tested values
  of each class; operands no truth tests; a first operand that is an object's
  lvalue, and one whose truth is known, of each constant's kind; two levels
  away; values; the operator whose code is before `&&`'s; consumers and first
  operands past two blocks. And six statements 64 times each, a block's end
  between every two of their records in turn. The branched records and the
  CONDITIONs among them are pinned.
- **Statements one at a time:** 271, each alone in a function with the
  shadow's comparison on, in both kinds of unit: the checker accepts 257 (it
  takes no vector's `!`, `&&` or `||`, and no `constexpr` at the default
  standard), and none has a difference. They have each kind of record of a
  half-precision float, a `__float128`, a long double, a 128-bit integer, a
  complex value and a `_BitInt` as an operand.
- **Faults,** one at a time in copies of the tree, two rounds. The first: 96
  of 116 caught, 12 texts that did not build, 8 survivors. Two of the
  survivors were terms another record says, and went: a branched truth's
  second operand's test (it has none, and its bytes read zero) and a truth's
  own class (an int's, the checker's). Two were cases with no line, and got
  one: a complex constant first, and a bit-field of an object first. The
  second round, on these sources: **112 of 116 caught**. The four others give
  the same answers: a CONDITION taken for a `!` under a logical consumer (a
  CONDITION has no consumer); an lvalue first whose constant is of any kind
  (a placed record's lvalue has an address's or none); and a branched
  record's target left to its row where the record has no template (its
  expression is refused) or is a root (which has no consumer's row).
- **Before the rounds,** two terms the first build had as masks went on
  reading the code that makes the values: the int's class of a `!`, an `&&`
  and an `||` (the checker's), and a branched CONDITION's `free` (a folded
  CONDITION has a folded operand, which lists).

**What the probes found beside it.** A condition on an lvalue at an absolute
address (`if (*(int*)4096)`) was compiled as true, its load dropped:
[#3028](https://github.com/buster14a/buster/issues/3028), a fault of the
checker's truth rule, which C reads. Its fix makes such an lvalue's truth
unknown unless it is an array's. This piece's mask takes a first operand
that is an lvalue only where it has an object, and lists the expression of
one at an absolute address before the fix and after it.

**What is listed still.** `&&` and `||` as values and `?:` are 674 of the
1,683 expressions listed on `sqlite3.c`, 290 of 760 on Lua, 964 of 1,656 on
musl; then a value the columns don't hold whole, a comma and a selection, and
a division by a constant. The values are the piece after, with the loop's
frame.

### The records' bytes without their four gathers

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/the-records-bytes-without-gathers-2850-2026-11-19).
Its [specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7020847483)
had four gates: the same bytes (S1), what it costs (S2), that nothing else
moves (S3) and faults (S4).

The tests' loop and C's read a chunk's records as byte columns, which one
sweep makes sixteen records a step (`node_tests_window_bytes_by`). It read
four columns of the kernel's 64-byte records (a record's word, flags, object
and standing) by four masked gathers of sixteen dwords. A gather of sixteen
is 64 uops on Zen 5 ("SIMD and Zen 5" in [the coding style](coding-style.md)):
256 a step, for what sixteen loads hold.

**Loads and permutes.** A record's line's first half is its type, word,
flags and object, its payload, and its standing with its exponent: eight
dwords. The sweep reads that half whole, two records a register. One permute
of two registers puts each column's four dwords of four records in one
128-bit lane, and eight lane shuffles make the four columns, the last four
masked by the placed records as the gathers were. Sixteen loads and twelve
permutes a step. A step reads its sixteen lines whether placed or not: they
are the window's own array, 4,096 lines.

**What "The gathers stay" had measured** ("The tests on the window", above)
was a loop a record, which lost to the gathers in every form. This is no loop
a record: the same sixteen records a step, their columns by permutes.

**What stays a gather:** the names' declarations' lines' bytes, whose indices
are entries' numbers and no column, and the two of the reader's verify.

**Measured.** The 9700X, CPU 2, Release, each run its own hold of the lock,
TSC ticks a record, seven readings; a reading's own difference, its median
and range. Build against build is the build before (#3030's code) and the
piece in turn. The switch is one executable, an experiment's build, that
reads the columns by the gathers where its environment says so: loads,
gathers, gathers, loads in each reading.

| the records' bytes | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| build against build: before, with | 9.16, 7.58 | 10.78, 9.33 | 19.67, 19.48 |
| with less before | -1.65 (-1.72 to -1.35) | -1.49 (-1.86 to -0.64) | -0.48 (-3.57 to 6.54) |
| **the switch: loads less gathers** | **-2.41 (-2.51 to -2.32)** | **-2.35 (-2.40 to -2.27)** | **-3.00 (-3.61 to -2.14)** |
| the same where the sweep makes C's bytes too | -2.44 (-2.52 to -2.36) | -2.55 (-2.79 to -2.43) | -3.68 (-5.80 to -2.69) |

| by the switch, loads less gathers | `sqlite3.c` | Lua |
| --- | ---: | ---: |
| the tests on the window, their sum | -2.52 (-2.66 to -2.35) | -2.26 (-2.48 to 0.84) |
| the tests' loop | -0.03 (-0.15 to 0.04) | +0.07 (-0.03 to 0.10) |
| an expression's part | -0.03 (-0.12 to 0.08) | +0.03 (0.02 to 2.86) |
| the tests on T's records | -0.03 (-0.14 to 0.06) | +0.03 (-0.25 to 0.49) |
| C's three sweeps, a record swept | -0.04 (-0.16 to 0.11) | +0.04 (-0.08 to 0.19) |

The two measures are 0.8 apart on `sqlite3.c`. The switch's executable reads
the gathers at 9.54 where the build before reads 9.16, and the loads at 7.15
where their own build reads 7.58. By the switch the tests on the window are
29.11 ticks a record on `sqlite3.c` for 31.56.

**Against its gates.**

- **S1, the same bytes: met.** No pin of `test-checker`'s fixtures moves.
  On the 9700X's three corpora the shadow's count lines of the piece's build
  are the build before's in each of 42 readings; in the cloud container the
  three corpora and the compiler's own source, with the default warnings and
  with `-Wall -Wextra`, read the counts of "A condition's branches on the
  window" with no difference.
- **S2, cost: met.** 1.65 and 1.49 ticks a record lower on `sqlite3.c` and
  Lua build against build (1.2 asked, refuted under 1.0), 2.41 and 2.35 by
  the switch (2.0 asked, refuted under 1.6); musl lower in both medians.
- **S3, nothing else moves: met.** By the switch each other part reads
  within 0.1 of itself in the median on `sqlite3.c` and Lua (0.3 asked).
  Build against build an expression's part reads 0.27 and 0.34 higher in the
  piece's build (0.05 to 0.37, and 0.27 to 0.60), which one executable's two
  settings do not show: the tests' sum is 1.23 and 0.50 lower between the
  two builds for the 2.52 and 2.26 of the switch.
- **S4, faults: met.** 37 faults, one at a time in the piece's tree on the
  9700X (each index of the permute, each shuffle's selection, each column's
  mask, each load's place, the permute's registers swapped), each built and
  run on `test-checker`'s part 5. The first round caught 34. The three it
  passed dropped the placed records' mask of the flags', the objects' and the
  standings' columns: a record the kernel didn't place then has bytes, which
  no test read. The reader's verify now counts such a record
  (`own_values_wrong`), and a second round with it caught those three and the
  words' mask again.

**What else the sweep spends** is not measured here: its one gather, and
the declarations' lines' bytes' first making (18,295 lines on `sqlite3.c`).

### X's rows on the window, first their shapes

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/x-rows-on-the-window-2850-2026-11-19).
Its [specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7023470094)
had five gates: exact (R1), how much (R2), cost (R3), that nothing else moves
(R4) and faults (R5). [The census of X's
rows](https://github.com/buster14a/buster/issues/2850#issuecomment-7022665047)
came before it.

X goes up an expression's postorder and writes each evaluated record's row:
its own entries by its template, then its conversion's to its target, then
what follows its row. An entry is a kind, a class and a value. This piece
makes the kinds and the classes of the first two parts on the window, from
C's answers there, for the expressions with no update and no operator
evaluated as branches. `c_check_node_machine_rows.c` has it; no compile takes
any of it.

**A shape a part.** A record's own entries and its conversion's are each a
shape, a byte:

| shape | entries |
| --- | --- |
| 0 | none |
| a typed kind's number, below 64 | one entry of that kind, of the record's class: a constant's CONST, a local's and a global's read, the three stores, a binary operator's kind, NEG and NOT, a LOAD, a CALL, a DISCARD |
| an address constant's, 64 to 73 | a CONST with no object, or a local's or a global's address; a CONST and a PTR_ADD more with an offset; a CONVERT more for an rvalue of an integer's class |
| a member's, 74 and 75 | its offset's CONST and PTR_ADD; and its LOAD |
| a truth's, 76 to 79 | a CONST of its operand's conversion's class and NE, or EQ for `!`; a narrow class's after a CONVERT to I32 |
| a pointer's, 80 to 82 | its sum's PTR_ADD; a NEG before it for a difference; a LOAD after it for a subscript that loads |
| a conversion's, 83 to 85 | to bool, a CONST, NE and a CONVERT to U8, a narrow class's after a CONVERT to I32; and a CONVERT to the target's class |
| a folded constant's, 86 | its CONST of its target's class |
| 127 | none: the mark of a template whose rows are not made here, which lists its expression |

A shape from 64 up is a row of a table, which has each entry's kind and
where its class comes from: the class itself, the record's, its target's or
its first operand's conversion's.

**Three sweeps over a chunk's records.**

- **The reads.** What the window's bytes don't hold: a target's type's class
  and facts where a record converts (it is in a value's context and its
  target is not its type; none are read for void, whose class and facts are
  zeros); a member's offset; a truth's operand's conversion's class; and
  whether an address constant's object is a local's. A sweep of masks, 64
  records a step, compresses the numbers of the records to read for into two
  lists, and a loop a list reads a record at a time. A record's type is a
  column the tests' sweep makes with the records' bytes, so target and type
  are compared in registers.
- **The shapes**, 64 records a step: X's rules as masks over the window's
  bytes and the reads' columns (`node_machine_own`'s case of a record's
  template, `node_machine_convert`'s cases, and the fold of an integer
  constant's conversion into its CONST).
- **The rows**, 64 records a step. A part has four slots, an entry each. A
  slot's kinds, and its classes' sources, are one lookup of the block's
  shapes each (a table of 128 bytes a slot); the classes are made from their
  sources by masked moves; the eight slots' registers are transposed, by
  three rounds of unpacks, into eight records' rows a register; and each
  register's entries, the bytes whose kind is not zero, are compressed into
  the chunk's two streams. The compress joins a record's two parts and the
  eight records' rows. A row's place is the entries before it.

An expression's tape is its operand's records' rows, in a row in the streams,
and its root's.

**What is listed for X** and left to the reference: an expression with an
update or with an operator evaluated as branches; a conversion from or to a
class past double's (a long double's, a 128-bit integer's, a half-precision
one's, and an aggregate's and a complex type's, which have no class's
number), which the templates refuse; and an integer constant converted to a
floating class, whose fold reads its value.

**The terms that are no mask.** X's rules ask more than a record on the
window can separate. The fault rounds' survivors pointed at such terms; 241
one-statement programs, each a file of its own, and the three corpora, run
with the term changed, showed which a C program reaches there; and each term
that none reaches is out of the sweeps, with what makes it in the code's
comments:

- **The discarded context**, which writes a constant no entry: C gives it to
  a comma's value, a bit-field's assignment and an aggregate's store under a
  discard. C on the window lists or refuses each, and its contexts' byte has
  an address's bit alone.
- **A value's context**, in which alone the rule converts: a record read for
  its address has no target but a `.`'s base, whose target is T's conversion
  of it, its own type.
- **An empty conversion**, two types of one class with the target no bool:
  nothing asks whether a conversion is one. A DISCARD or a CONVERT is to
  another class's type alone, and an integer constant's fold to its own
  class is its own CONST again.
- **A complex's and a `_BitInt`'s source:** C gives a record of either no
  template.
- **A complex target, and an aggregate's on either side:** neither has a
  class's number, so one compare of the two classes' greater with a long
  double's refuses them with the long doubles; the rule's two tests of the
  facts, and its reading of an aggregate's target as an address's, ask
  nothing more.
- **An address constant's offset** is its standing's bit, which whatever
  writes a value sets for an offset that is not zero and for no other: the
  payload is not read.
- **A member's offset** is not zero, in bits: a bit-field's record lists its
  expression in C's loop.
- **A constant's class** is an integer's where its kind is: C's rule for the
  template.
- **The templates whose rows are not made here** are a mark in the
  templates' table, one compare, where a range of numbers with a call's
  excepted was three operations.
- **A constant folded to bool** is the folded constant's row: its CONST is of
  its target's class, and bool's is a U8's.
- **Whether a record that lists its expression is evaluated.** A record that
  is not evaluated converts nothing (the reads' mask), so only its template
  could list. The records C leaves unevaluated in an expression it takes are
  a store's or an update's named destination, a callee called directly and a
  folded constant's operands, and none of them is an update or an operator
  evaluated as branches: C lists a folded `&&`, `||` and `?:` itself. A mask
  that only takes would at worst list one more expression.
- **An expression's root's own record**, in its part: a statement's discard,
  a control's CONDITION, a `switch`'s value or a `return`'s conversion has no
  target, so no conversion of its own lists it; and its template is one whose
  rows are not made here only as a CONDITION over `&&` or `||`, whose operand
  C then takes with that template too, or does not take the expression. The
  operand's records' bits say it.

What a program does reach got a line in the fixture, and its term stayed: a
`_BitInt`'s target, which is an initialized object's alone (of its value's
class with no entry, of another with a CONVERT and no fold); a complex, a
long double's, a 128-bit integer's and a half-precision target; an aggregate
converted to an aggregate; and a store of a constant, `a = b = 0`, which has
its value's constant kind and is no fold. That last one no probe had: with
the fold's test of the template out, five of musl's tapes differed, and the
term went back with its line.

**How it came to its cost.** Builds on the 9700X, TSC ticks a record swept on
`sqlite3.c` and Lua, three readings each and seven for the last three:

| | `sqlite3.c` | Lua |
| --- | ---: | ---: |
| first: one sweep with two loops a block, the rows eight records a step | 7.47 | 8.21 |
| the reads a sweep of their own, a record's type a column, the rows by slots | 5.66 | 6.52 |
| the reads' two lists, a loop a list | 4.88 | 6.04 |
| no word read for void, and the expression's part with no branch | 5.00 | 5.90 |
| the part with its branch again | 4.84 | 5.80 |
| the terms that are no mask out | 4.38 | 5.37 |
| the part without its root's test, and the last terms out | 4.11 | 5.17 |

- **A register read of bytes just stored a byte at a time waits for the
  stores.** The first sweep's loops wrote a block's bytes and its vector part
  read them back at once: the shapes read 4.35 ticks a record there and 0.80
  once the reads were a sweep before them.
- **A loop a block ends on a branch its count mispredicts.** The reads read
  2.71 as two loops a block and 1.81 as two loops a chunk over compressed
  lists.
- **The rows by slots and a compress read less than half of the rows a
  quadword a record.** Eight records a step, a quadword a record joined by a
  shift, read 1.85; 64 records a step, joined by the compress, 0.84.
- **Half the conversions are to void**, a discarded value's: the reads' word
  loop runs for 9.95% of `sqlite3.c`'s records where it ran for 18.11%. Its
  time did not move for it.
- **The expression's part with no branch read more**, 1.45 for 1.26: it is
  back to its branch.
- **The terms that are no mask** were a third of the shapes' sweep on
  `sqlite3.c`: it read 0.88 ticks a record with them and 0.59 without; and
  the reads read 1.83 and 1.65.
- **The part without its test of the root's record** reads 1.02 ticks a
  record swept on `sqlite3.c` and 1.05 on Lua, where the build before, with
  the test, read 1.29 and 1.19.

**Measured.** The 9700X, CPU 2, Release, each run its own hold of the lock,
TSC ticks, seven readings: one executable, the rows on (`--window-rows`), off
(`--window-contexts`), off, on in each reading.

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions C takes on the window | 49,155 | 9,909 | 39,712 |
| their tapes made here | 43,719 (88.94%) | 8,961 (90.43%) | 35,194 (88.62%) |
| of the expressions C decides | 88.66% | 88.91% | 84.28% |
| listed for an update | 3,394 | 742 | 3,530 |
| listed for branches | 2,033 | 196 | 775 |
| listed for a refused conversion | 1 | 0 | 20 |
| listed for a constant to a floating class | 8 | 10 | 193 |
| tapes that differ from X's on T's columns | 0 | 0 | 0 |
| **the three sweeps and the part, a record swept** | **4.11 (4.02 to 4.23)** | **5.17 (5.09 to 5.33)** | **10.54 (9.22 to 13.20)** |
| the reads | 1.62 | 1.83 | 3.46 |
| the shapes | 0.62 | 1.00 | 2.60 |
| the rows | 0.86 | 1.29 | 2.66 |
| an expression's part | 1.02 | 1.05 | 1.54 |
| a second run over the same chunk | 3.29 | 3.14 | 4.04 |
| a record of the taken expressions | 5.84 | 8.66 | 24.68 |
| X on T's columns, a record of theirs, with its values | 24.34 | 22.99 | 27.54 |
| records whose target's word is read | 31,998 (9.95%) | 6,919 (7.58%) | 34,220 (9.07%) |
| records whose line is read | 47,178 (14.68%) | 11,584 (12.69%) | 19,496 (5.17%) |

**Against its gates.**

- **R1, exact: met.** No tape made differs from X's on T's columns in its
  number of entries, a kind or a class: on the three corpora on the 9700X,
  and in the cloud container on them and the compiler's own source (31,607
  tapes there) with the default warnings and with `-Wall -Wextra`. The
  fixture has a line a shape and a line a term's case (356 tapes), and
  C's fixture 679 more. With one of the reference's answers changed for each
  tape (its first entry's kind, its last's class and its length in turn) the
  comparison counts every tape of both.
- **R2, how much: met on `sqlite3.c` and Lua, not on musl.** 88.66% and
  88.91% of the expressions C decides (85% asked, refuted under 80%), and
  84.28% on musl: C refuses 2,047 of the 41,759 it decides there for a
  template, which have no tape to make, and the rows make 88.62% of those
  it takes.
- **R3, cost: met on `sqlite3.c`, not on Lua.** 4.11 and 5.17 ticks a record
  swept (5.0 asked, refuted over 8.0); 4.26 and 5.52 with what the types'
  column adds to the records' bytes' sweep (R4). A second run over the same
  chunk, right after the first, reads 3.29 and 3.14. What a chunk's first run
  reads more is not taken apart here; Lua's chunks are 379 records for
  `sqlite3.c`'s 526.
- **R4, nothing else moves: met, but for the records' bytes on Lua.** By the
  switch on `sqlite3.c` and Lua, on less off: the kernel in place -0.12 and
  +0.06; the tests' loop -0.04 and +0.05, their expression's part +0.06 and
  -0.01, their sweep on T's records +0.04 and +0.08; C's three sweeps +0.00
  and +0.01 (0.3 asked). The records' bytes' sweep makes the types' column
  with the rows on, the rows' own work, and reads +0.15 and +0.35 for it
  (+0.08 and -0.07 in the build before, whose code there is the same); the
  tests' sum with it +0.21 and +0.56.
- **R5, faults: met.** Five rounds: each term of a mask, each row of the
  tables and each of the write's permutes and shifts changed alone, a build
  and the fixture's tests a fault. A round stopped where its survivors had
  changed the code, and the next ran on the changed code: 12 survivors of
  the first round's 55 faults run, 4 of the second's 47, 2 of the third's 44
  and 4 of the fourth's 157. Each one's term went out as one no record on
  the window separates (the corpora's tapes are the same without it), or
  its case got a line in the fixture; one, a column's mask of the records
  the kernel placed, got the reader's verify asserted. The fifth round's 167
  faults, on the code as it is, are all caught.

**What stays a record at a time, and why.** The reads' two loops: 24.6% of
`sqlite3.c`'s records swept, 20.3% of Lua's and 14.2% of musl's. Each
iteration is a load by a type's or an object's number, or of a record's
line, and a store of bytes, with no branch but the loop's. A gather of
sixteen such loads is 64 uops on Zen 5 ("SIMD and Zen 5" in [the coding
style](coding-style.md)), what sixteen loads are, and its results would be
stored to the same bytes. The lists, not the loads, are what the vector does
here. With a clock read between the reads' stages (an experiment's build of
the round before's code, whose reads are these, three runs), the masks and
the two lists are 0.50 ticks a record swept on `sqlite3.c` and 0.57 on Lua,
the loop over the targets' words 0.35 and 0.32, and the loop over the lines
0.67 and 0.65.

**What is next:** an update's rows (its plan on the window), the branches'
(the states, the senses and the labels), and the entries' values.

### An update's rows on the window

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/x-update-rows-2850-2026-11-20).
Its [specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7030649938)
had five gates: exact (U1), how much (U2), cost (U3), that nothing else moves
(U4) and faults (U5).

The piece before listed every expression with an update: 3,394 of the
49,155 `sqlite3.c` has that C takes on the window, 742 of Lua's 9,909 and
3,530 of musl's 39,712. X writes an update two rows, by
`node_machine_update_plan`'s choices: the first after its destination's row
(`node_machine_update_first`: the old value's read, by name or through the
address its destination's entries left, and its CONVERT to the class the
operation is computed in), the last where its own record is
(`node_machine_update_last`: the step's CONST for `++` and `--`, the
operation, the result's CONVERT back and the store). This piece makes the
kinds and the classes of both on the window. No compile takes any of it.

**Each row where its record has free slots.** A record's row is two parts of
four slots, its own entries' and its conversion's (the piece before). An
update's two rows are shapes of the same table, in the slots its two records
don't use:

| row | where | entries |
| --- | --- | --- |
| its own, 87 to 90 | the update's record's own part | a CONST of the computation's class for `++` and `--`; the operation; a CONVERT back to the destination's class where the two differ; the store |
| a pointer's own, 91 to 94 | the same | its step's CONST for `++` and `--`; a NEG for `--` and `-=`; the PTR_ADD; the store |
| its first by name, 95 to 98, and 99 | its destination's record's two parts: a destination by name is not evaluated, and has no entry | the read, a local's or a global's, and a postfix update's DUP in the own part; the CONVERT in the conversion's |
| its first through an address, 100 and 101 | its destination's record's conversion's part: a record read for its address converts nothing | a DUP of the address, the LOAD, the CONVERT |
| a postfix update's first through an address, 102 to 111 | both parts of its destination's record, after the destination's own entries | six entries (the address kept in a temporary and read back, the old value loaded and kept in another, and the two read for the store and for the value) and the CONVERT |

A row's slots are fixed, with none where an entry is not written: the write's
compress drops an empty slot as it joins the parts. Two of the own row's
entries have a kind that is the record's to say, the operation's (by the
operator's code) and the store's (a local's, a global's or through its
address; a SET for a postfix update): each is a mark in the table, which the
write resolves with one masked move, as it does a class's source. A first
row's CONVERT is its part's last slot's in each row, so one slot of a
conversion's part reads the computation's class.

**What it lists.** A bool's update, whose result is tested (the conversion
back is the test's entries, which the four slots have no room for); and a
postfix update through an address whose destination's own entries leave the
six no room: an address and its offset's three, or a member's offset's two
with the CONVERT. 73 of `sqlite3.c`'s expressions, 10 of Lua's and
4 of musl's.

**A sweep and a loop of their own**, after the shapes' sweep and before the
rows' write (`node_rows_window_updates`), in a chunk where C's loop gave a
record the update's template. A sweep of masks, 64 records a step, compresses
the chunk's updates' numbers into a list: their places in the block as bytes,
one compress, the first 16 widened onto the list, and another turn for a
block with more. A loop over the list then writes each update's two rows'
shapes over what the shapes' sweep made of its record and of its
destination's, with their lengths; its store's kind and the class its
operation is computed in (T's conversion of its first operand, one read of
the update's line), where the write reads them; and, where its rows are not
made, the bit that lists its expression. Each choice is a bit and each shape
a sum or a table's byte, with no branch on an update's bits. The reads' and
the shapes' sweeps have no term of an update, and an update has no column of
its own.

**The terms that are no term here.** The plan asks more than this reads; for
what C on the window takes an update by, each is said by a byte the loop has
already (the comments there):

- **The stored class and facts**, the destination's type's, are the update's
  own record's and its destination's record's alike: an update's type is its
  destination's value's.
- **The class the operation is computed in** is its computation's: the plan's
  other case is a narrow class's, a `_BitInt`'s alone (C promotes the others),
  and C takes no update of a `_BitInt`.
- **A pointer's computation** is its own class's: no pointer's update has the
  CONVERT.
- **A destination by name** has an object, and one through its address
  converts nothing.
- **A division or a remainder by a power of two**, a shift or a mask in the
  plan: C on the window lists every compound division and remainder by an
  integer constant.

**How it came to its cost.** Builds on the 9700X, TSC ticks a record swept,
three readings each: the rows' sweeps with the update's rows on, and what
they read more than the piece before's executable in the same readings.

| build | `sqlite3.c`, with the update's rows | more than the piece before's | Lua, with them | more |
| --- | ---: | ---: | ---: | ---: |
| first: the update's loop in the reads' sweep, each shape chosen by branches; four columns of its own | 5.16 | +1.02 | 6.52 | +1.30 |
| each shape a sum of bits, with no branch | 5.21 | +1.11 | 6.98 | +1.89 |
| no column of its own: the shapes into the two the sweeps write | 4.84 | +0.62 | 6.18 | +1.06 |
| both rows' shapes as masks in the shapes' sweep; the loop carries what crosses the two records | 4.75 | +0.57 | 6.40 | +1.27 |
| a sweep and a loop of their own, after the shapes' sweep | 4.75 | +0.61 | 6.15 | +0.99 |
| that loop with no table of its own (not kept) | 4.79 | +0.70 | 6.10 | +0.95 |
| the fifth's, in a chunk with an update alone, and a row's entries' number one byte (seven readings) | **4.71** | **+0.67** | **6.11** | **+0.94** |

- **A shape chosen by branches is mispredicted.** The first build's loop chose each shape by branches: the reads
  read 0.57 ticks a record more with the update's rows on than off on `sqlite3.c`, 49.1 an update; as sums of bits,
  0.43 and 37.0.
- **A column of its own is one more a block, in every chunk.** With four (the two shapes', the operation's kind and
  the store's), the reads read 0.34 ticks a record more than the piece before's on `sqlite3.c` and 1.01 on Lua with
  the update's rows off; with none, 0.14 and 0.44.
- **A term in a big sweep is every block's.** The fourth build made both rows' shapes of masks in the shapes' sweep,
  its loop carrying only what crosses the two records. The shapes' sweep read 0.22 ticks a record more than the
  piece before's on `sqlite3.c` and 0.49 on Lua, 0.18 and 0.47 of it with the update's rows off; and the reads, with
  the list and the loop in them, 0.29 and 0.67 more.
- **A sweep and a loop of their own leave the others theirs.** In the fifth build the reads read +0.00 and +0.02 against
  the piece before's and the shapes +0.01 and +0.01. The updates' own counter reads 0.53 and 0.89, of which 0.11 and 0.30
  are the list's sweep with no update to make (the switch), and 0.35 and 0.34 on a chunk's second run.
- **The loop with no table of its own read no less**, 0.58 and 0.96: a sum of bits for each shape and each length
  is more instructions than the lookups it replaces.

**Measured.** The 9700X, CPU 2, Release, each run its own hold of the lock,
TSC ticks, seven readings; in each reading one executable with the update's
rows on, off (an experiment's switch: an update's template lists its
expression, as the piece before had it), the piece before's executable
twice, then off and on again.

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions C takes on the window | 49,155 | 9,909 | 39,712 |
| their tapes made here | 47,027 (95.67%) | 9,689 (97.78%) | 38,705 (97.46%) |
| the piece before's | 43,719 (88.94%) | 8,961 (90.43%) | 35,194 (88.62%) |
| of the expressions C decides | 95.37% | 96.13% | 92.69% |
| listed for an update | 73 | 10 | 4 |
| listed for branches | 2,046 | 200 | 790 |
| listed for a refused conversion | 1 | 0 | 20 |
| listed for a constant to a floating class | 8 | 10 | 193 |
| tapes that differ from X's on T's columns | 0 | 0 | 0 |
| updates | 3,734 (1.16% of the records swept) | 861 (0.94% of the records swept) | 3,969 (1.05% of the records swept) |
| **the rows' sweeps and the part, a record swept** | **4.71 (4.61 to 4.93)** | **6.11 (5.99 to 8.12)** | **11.97 (9.87 to 14.39)** |
| the piece before's executable, in the same readings | 4.12 | 5.15 | 10.59 |
| **more than the piece before's** | **+0.67 (+0.47 to +0.81)** | **+0.94 (+0.87 to +1.99)** | **+1.27 (-1.11 to +4.23)** |
| with the update's rows off | 4.37 | 5.54 | 11.68 |
| the reads | 1.62 | 1.81 | 3.58 |
| the shapes | 0.62 | 1.02 | 2.10 |
| the updates; with none to make | 0.52; 0.11 | 0.81; 0.27 | 1.55; 0.69 |
| the rows | 0.98 | 1.44 | 3.16 |
| an expression's part | 0.99 | 0.99 | 1.51 |
| a second run over the same chunk; the piece before's | 3.81; 3.29 | 3.54; 3.15 | 4.70; 4.04 |
| the updates in it; with none to make | 0.36; 0.09 | 0.33; 0.12 | 0.53; 0.30 |
| the updates, ticks an update: a first run; a second | 45; 31 | 86; 35 | 147; 50 |
| a record of the taken expressions | 6.22 | 9.51 | 25.20 |
| X on T's columns, a record of theirs, with its values | 26.64 | 24.78 | 29.25 |
| records a chunk; updates a chunk | 526; 6.1 | 379; 3.6 | 127; 1.3 |

**Against its gates.**

- **U1, exact: met.** No tape made differs from X's on T's columns in its number of entries, a kind or a class, on
  the three corpora on the 9700X, with the update's rows on and off. In the cloud
  container the same on the three and on the compiler's own source (36,721 tapes there), with the default warnings and
  with `-Wall -Wextra`. The rows' fixture has a line an update's case (629 tapes with the first piece's lines), a
  unit of one chunk with one update has 3, and C's fixture 1,253 more; with one of the reference's answers changed
  for each tape the comparison counts every tape of the three. And 618 one-statement programs, each a file of its
  own, 369 of them an update's cases, read as X on T's columns does.
- **U2, how much: met.** 3,308 tapes more on `sqlite3.c`, 728 on Lua and 3,511 on musl (2,900, 600 and 3,400 asked;
  refuted under 2,500, 500 and 3,000): 95.67%, 97.78% and 97.46% of the expressions C takes, and 95.37%, 96.13% and 92.69%
  of those it decides. Of the 438, 115 and 80 expressions with a postfix update
  through an address (the count before the specification), 73, 10 and 4 are listed still, for the entries of their
  destination's own.
- **U3, cost: not met.** The rows' sweeps and the part read +0.67 ticks a record swept more
  than the piece before's executable on `sqlite3.c` (+0.47 to +0.81) and +0.94 on Lua (+0.87 to +1.99), where 0.5 was asked
  and 1.0 refutes; +1.27 on musl (-1.11 to +4.23), which the gate did not name. Of it the updates' sweep and loop are 0.52,
  0.81 and 1.55, and the rows' write, with the update's marks and sources in it, +0.12, +0.17 and +0.22. On a chunk's second
  run the whole reads +0.52, +0.40 and +0.67 more.
- **U4, nothing else moves: met.** By the switch, on less off, on `sqlite3.c` and Lua: the kernel in place -0.10 and
  -0.04; the records' bytes -0.01 and +0.00, the tests' loop +0.03 and +0.00, their expression's part -0.02 and +0.00, their sweep on
  T's records -0.06 and -0.12; C's three sweeps +0.01 and -0.07 (0.3 asked).
- **U5, faults: met.** Four rounds: each term of the updates' loop's bits and shapes, each of its stores, the list's sweep, the
  write's new resolutions and marks and each new row of the tables changed alone, a build and the fixture's tests a
  fault. The first round's 174 faults left 3: one that gives a pointer's row a CONVERT, which no pointer's update
  has, so it changes nothing (dropped); the two parts' lengths of a row swapped, which no one read apart (they became
  a row's entries' number, one byte); and the list taking records that are not evaluated, whose case, a folded
  `&&`'s dead operand, got its lines. The second's 181, on the changed code, left 3: a row's entries' number read by
  its low bits, and two that skip a chunk with one update (a unit of one chunk with one update). The third's 181 left
  the first of those still: the line it had got, an address constant's cast converted to bool, makes rows of four
  and of five entries and none of eight. Run against the fault's build, a narrow destination's `++` whose value is
  converted to bool does (its own four and the conversion's four, an expression's last record's), and that is the
  fixture's line now. The fourth's 181, on the code as it is, are all caught.

**What stays a record at a time, and why.** The updates' loop, for 1.16% of
`sqlite3.c`'s records swept, 0.94% of Lua's and 1.05% of musl's. An update's
two records are apart by a distance only its own record has; its
computation's class is a load by its line's type's number; and its shapes
are bytes stored at the two records. As masks in the shapes' sweep (the
fourth build), an update's terms are every block's, for one record in some
ninety: the shapes' sweep read 0.22 ticks a record more on `sqlite3.c` and 0.49
on Lua with them, 0.18 and 0.47 of it with no update to make; the loop of its own reads 0.52 and 0.81, and leaves
the sweep where it was.

**What is next:** the branches' rows (the states, the senses and the
labels), then the entries' values.

### The branches' rows on the window

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/x-branch-rows-2850-2026-11-20).
Its [specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7037879405)
had five gates: exact (B1), how much (B2), cost (B3), that nothing else moves
(B4) and faults (B5).

The piece before listed every expression with an operator evaluated as
branches: 2,046 of the 49,155 `sqlite3.c` has that C takes on
the window, 200 of Lua's 9,909 and 790 of musl's 39,712. X
writes such an operator no entry (`node_machine_classify_branches`: a
statement's CONDITION over `&&` or `||`, and the `!`, `&&` and `||` below
it). Each of its operands that is no such operator is a value it tests
(`node_machine_branch_operand`): after the value's row, converted to the type
the operator converts it to, come its test (`node_machine_put_truth`: a
narrow class's CONVERT to I32, a CONST 0 of the class tested and NE) and a
branch to a label where its truth is its sense, BRANCH_TRUE or BRANCH_FALSE
(`node_machine_branch_hook`). The sense comes from the root down. A
CONDITION's is its statement's: a `do`'s branches back where the condition
holds, an `if`'s, a `while`'s and a `for`'s away where it doesn't
(`node_machine_reader_root`). An `&&`'s first operand's is false and an
`||`'s true, whatever the operator's own (an `&&` taken where true and an
`||` where false branch on their first operand the other way, to a label of
their own past their second); a second operand's is its operator's; and a
`!`'s operand's is the other. This piece makes the kinds and the classes of a
tested value's test and branch on the window. The label is the branch's
entry's value, which is not made yet. No compile takes any of it.

**The test is the tested value's conversion's part.** A record's row is two
parts of four slots, its own entries' and its conversion's (the first piece).
The checker converts a logical operator's operand to its own type without its
qualifiers (`check_action_logic`'s operands), and of the values the count
before the specification found tested (4,860 of `sqlite3.c`'s, 428 of
Lua's, 1,814 of musl's and 7,941 of the compiler's own source's), none of
the three corpora's has a conversion that writes an entry. So the part is
free, and the test and the branch are its row, four rows of the table:

| row | the tested value's class | entries |
| --- | --- | --- |
| 112 and 113 | not narrow; branching where it doesn't hold, and where it holds | a CONST of the record's class, NE, BRANCH_FALSE or BRANCH_TRUE |
| 114 and 115 | narrow (a bool's, a char's or a short's) | a CONVERT to I32, a CONST of I32, NE, the branch |

The class tested is the record's own, where it converts nothing. An operator
evaluated as branches has no shape: its template's row in
`NodeRowsTables.templates` is no entry, where the piece before had the mark
that lists its expression.

**What it lists.** A tested value with a conversion of its own, whose part
has no room for the test. One conversion does that: a qualified `_Bool`'s
read, which the lowering converts to bool again by four entries
([#3185](https://github.com/buster14a/buster/issues/3185)): nine with its own
entry and the test's four, and a record has eight slots. Its row stays as it
was and its expression is listed: none of the three corpora's, and 560 of
the compiler's own source's 40,195 (759 such values in the count
before the specification), which declares its flags `const bool`.

**One sweep makes two lists, and a loop goes down the second.** The update's
sweep of masks, 64 records a step, compresses a chunk's updates' numbers into
a list. In a chunk where C's loop evaluated a record as branches it
compresses the chunk's `!`, `&&` and `||` of that template into a second list
in the same pass: a compare with the template's number, one with a
CONDITION's code and a compress more a block. A statement's CONDITION takes
no turn: its one operand's sense is its own, which the reader writes at the
operand's record when it registers the expression
(`node_facts_reader_expression`). A loop then goes down the second list from
its end (`node_rows_window_branch`). A record's consumer is a later record,
so an operator's turn finds its sense in the senses' column (a byte a record,
`window_x_senses`), where its consumer's turn or the reader left it. Its turn
gives each of its one or two operands its sense (`node_rows_window_tested`):
a byte for an operand that is such an operator in its turn; and for a value
the test's shape in its conversion's part (112, two more for a narrow class
and one more for the sense), three or four entries more in its row's length
and, where the part is not free, the bit that lists its expression. A `!`'s
one operand is given twice with the same sense, and the second time changes
nothing, so the turn has no branch on its operator. Each choice is a bit and
each shape a sum. The one branch is the listing's, which none of the three
corpora's values takes. An update that is a tested value (`if (n-- && p)`)
has its own row from the updates' loop and its test from this one: the one
writes its record's own part and the other its conversion's, and each adds
its entries to the row's length, so the two loops' order is no one's to say
(the specification had the updates' first; a fault ran them the other way,
and nothing changed).

**How it came to its cost.** Builds on the 9700X, TSC ticks a record swept,
three readings each and seven for the last: the rows' sweeps with the
branches' rows on, and what they read more than the piece before's
executable in the same readings.

| build | `sqlite3.c`, with the branches' rows | more than the piece before's | Lua, with them | more | the lists' and the loops' code, bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| first: the second list in every chunk, a statement's CONDITION a turn of its own, the loop a function and a clock's read of its own | 5.40 | +0.60 | 6.83 | +0.70 | 2,648 |
| one timed region; the bit that lists written where it is set; the second list in a chunk that branches | 5.41 | +0.58 | 6.85 | +0.71 | 2,623 |
| a CONDITION takes no turn (the reader writes its sense at its operand); an operand's entries counted once | 5.40 | +0.59 | 7.02 | +0.94 | 2,662 |
| the lists' sweep's step in one copy, where the compiler had unrolled it by four | 5.29 | +0.47 | 6.58 | +0.51 | 1,740 |
| the branches' loop in the lists' function, its mask in a chunk that branches alone | 5.36 | +0.53 | 6.41 | +0.35 | 1,806 |
| the fifth's after the fault rounds' changes, the code as it is: an operand's rule with one mask for its shape and its length, the narrow test by its upper bound alone, the chunk's guard on the operators with a turn (seven readings) | **5.10** | **+0.32** | **6.39** | **+0.34** | 1,736 |

- **A statement's CONDITION needs no turn.** The first two builds gave every record evaluated as branches a turn:
  5,613 on `sqlite3.c`, 570 on Lua and 2,364 on musl. A CONDITION's one operand has the CONDITION's own sense, which the
  reader knows when it registers the expression, so the third build has it write the sense there: 3,415, 340 and 1,487 turns.
- **The step in one copy.** The compiler had unrolled the lists' sweep's block loop by four, with a remainder's
  copies: 2,145 bytes of function for 1,223 with `unroll(disable)`, the same work a block. A chunk's first run read
  +0.59 ticks a record swept more than the piece before's on `sqlite3.c` and +0.94 on Lua unrolled, and +0.47 and +0.51 in one
  copy; its second run, +0.40 and +0.32, then +0.34 and +0.30. A chunk is some 8 blocks of `sqlite3.c`'s and 6 of
  Lua's. What a first run pays for there, the function's bytes or its loops' ends, which a second run over the same
  chunk has just seen, these readings don't separate.
- **The loop where the lists are made.** The fourth build's branches' loop was a function of its own, 517 bytes
  beside the lists' 1,223; in the lists' function, with its mask computed in a chunk that branches alone, the two are
  1,806 bytes, and the rows read +0.53 and +0.35 more than the piece before's (+0.47 and +0.51 before).
- **`sqlite3.c` moved little, Lua by half.** Across the five builds `sqlite3.c` read from +0.47 to +0.60 more than the
  piece before's, their readings' ranges overlapping; Lua from +0.94 down to +0.35.
- **The fault rounds' changes read less on `sqlite3.c`.** They touched the timed code after the fifth build: an
  operand's rule (one mask for its shape and its length), the narrow test (its upper bound alone) and the chunk's
  guard (the operators with a turn). By the switch, on less off, the lists and the two loops read +0.48 ticks a record
  swept on `sqlite3.c` and +0.23 on Lua in the fifth build's three readings, and +0.29 and +0.22 in the final code's seven; the
  rows +0.32 and +0.34 more than the piece before's, for +0.53 and +0.35. Which of the three changes it is was not measured.

**Measured.** The 9700X, CPU 2, Release, each run its own hold of the lock,
TSC ticks, seven readings; in each reading one executable with the branches'
rows on, off (an experiment's switch: the branches' template lists its
expression and the sweep makes no second list, as the piece before had it),
the piece before's executable twice, then off and on again.

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions C takes on the window | 49,155 | 9,909 | 39,712 |
| their tapes made here | 49,073 (99.83%) | 9,888 (99.79%) | 39,495 (99.45%) |
| the piece before's | 47,027 (95.67%) | 9,689 (97.78%) | 38,705 (97.46%) |
| of the expressions C decides | 99.52% | 98.10% | 94.58% |
| listed for an update | 73 | 10 | 4 |
| listed for branches | 0 | 0 | 0 |
| listed for a refused conversion | 1 | 0 | 20 |
| listed for a constant to a floating class | 8 | 11 | 193 |
| tapes that differ from X's on T's columns | 0 | 0 | 0 |
| operators with a turn | 3,415 (1.06% of the records swept) | 340 (0.37% of the records swept) | 1,487 (0.39% of the records swept) |
| **the rows' sweeps and the part, a record swept** | **5.10 (5.01 to 5.28)** | **6.39 (6.31 to 9.88)** | **12.46 (10.37 to 14.48)** |
| the piece before's executable, in the same readings | 4.80 | 6.08 | 13.34 |
| **more than the piece before's** | **+0.32 (+0.17 to +0.40)** | **+0.34 (+0.07 to +2.13)** | **-0.45 (-1.82 to +0.17)** |
| with the branches' rows off; on less off | 4.80; +0.30 | 6.13; +0.25 | 11.50; +0.52 |
| the reads | 1.67 | 1.83 | 3.21 |
| the shapes | 0.67 | 1.02 | 1.96 |
| the lists and the two loops; with the branches' rows off | 0.83; 0.53 | 1.05; 0.83 | 1.94; 1.79 |
| the rows | 0.95 | 1.47 | 3.38 |
| an expression's part | 0.98 | 0.99 | 1.52 |
| a second run over the same chunk; the piece before's | 4.08; 3.79 | 3.83; 3.54 | 5.12; 4.71 |
| the lists and the two loops in it; with the branches' rows off | 0.62; 0.43 | 0.56; 0.46 | 0.91; 0.83 |
| the branches' rows, ticks a turn (on less off): a first run; a second | 27; 18 | 59; 30 | 38; 20 |
| a record of the taken expressions | 6.09 | 9.48 | 24.98 |
| X on T's columns, a record of theirs, with its values | 25.53 | 23.55 | 28.34 |
| records a chunk; turns a chunk | 526; 5.6 | 379; 1.4 | 127; 0.5 |

**Against its gates.**

- **B1, exact: met.** No tape made differs from X's on T's columns in its number of entries, a kind or a class, on
  the three corpora on the 9700X, with the branches' rows on and off. In the cloud container the same on the three
  and on the compiler's own source (39,453 tapes of its 40,195 expressions, 98.15%), with the default warnings and with
  `-Wall -Wextra`. The rows' fixture has a line a
  pattern (1,166 tapes with the pieces' before), a unit of one chunk with one condition and no update has 3, one with
  one update 3, and C's fixture 1,479 more; with one of the reference's answers changed for each tape the comparison
  counts every tape of the four. And 1,134 one-statement programs, each a file of its own: 286 conditions (`&&`, `||` and `!`
  under an `if`, a `while`, a `do` and a `for`, nested in either operand three deep, a tested value of each class and
  kind, and conditions C lists), 230 tested values that might have a conversion of their own (a decayed array's and
  function's, a qualified, an atomic and a bit-field's read, a class past double's), and the 618 of the pieces before.
- **B2, how much: met.** 2,046 tapes more on `sqlite3.c`, 199 on Lua and 790 on musl (2,046, 199 and 790 asked: every
  expression the count had with no other record that lists it; refuted under 2,000, 190 and 770): 99.83%, 99.79% and
  99.45% of the expressions C takes, and 99.52%, 98.10% and 94.58% of those it decides. No expression is listed for
  branches on the three.
- **B3, cost: met.** The rows' sweeps and the part read +0.32 ticks a record swept more than the piece before's
  executable on `sqlite3.c` (+0.17 to +0.40; 0.6 asked, 1.0 refutes) and +0.34 on Lua (+0.07 to +2.13; 0.4 asked, 0.8 refutes);
  -0.45 on musl (-1.82 to +0.17), which the gate did not name. Two of Lua's 14 runs with the branches' rows on read 9.44 and 9.88, each with one part some 300,000 ticks over its
  other readings (the lists and the loops in the one, the rows' write in the other); the other 12 read
  6.31 to 6.47. By the switch, on less off, the lists and the two loops read
  +0.29, +0.22 and +0.15: 27, 59 and 38 ticks a turn. On a chunk's second run the whole reads +0.29, +0.29 and +0.40 more than
  the piece before's.
- **B4, nothing else moves: met.** By the switch, on less off, on `sqlite3.c` and Lua: the kernel in place +0.02 and
  -0.03; the records' bytes -0.06 and +0.07, the tests' loop -0.02 and +0.02, their expression's part +0.01 and -0.07, their sweep on
  T's records -0.02 and -0.07; C's three sweeps -0.03 and +0.05 (0.3 asked).
- **B5, faults: met.** Three rounds: each term of an operand's rule and of an operator's turn, each of their stores, the second
  list's sweep, the chunk's guard, the reader's sense, each new row of the tables and the listed expression's reason
  changed alone, a build and the fixture's tests a fault. The first round's 160 faults left 7. Four change nothing
  a program reaches, and are no faults: a count set to the lesser of itself and its block's end, which the loop
  before it leaves no less; the two loops in the other order (an update that is a tested value has its own part from
  the one and its conversion's from the other); a second operand given its sense before the first; and void's class
  taken for a narrow one, which no tested value has (the test is by its upper bound alone now). Three found code to
  change. The chunk's guard counted the CONDITIONs, which take no turn, so a guard of two for one and a count without
  them both passed: the count is the turns' now, and a unit of one chunk with one turn separates them (run against
  the first fault's build before the next round). And the listed expression's reason read its record's consumer
  behind a test that the record has one, which a fault removed unseen: a build with a line for each listed record
  with no consumer printed none on the three corpora and the fixture, so the test was a term no record asked. The
  reason is by the record's own shape now, with no consumer's read. The second round's 159 faults left 2, both in
  that rewritten reason: one changes nothing (the tables give a destination with no room the mark in both of its
  parts), and one swapped the two reasons a record with the bit alone can have, which the fixture hid, its counts
  for the two being the same. A fourth `const` bool condition makes them differ, and that fault's build fails on it,
  run before the third round. The third round's 158 faults, on the code as it is, are all caught.

**What stays a record at a time, and why.** The branches' loop: a turn for
each `!`, `&&` and `||` evaluated as branches, 1.06% of `sqlite3.c`'s
records swept, 0.37% of Lua's and 0.39% of musl's. An operator's first
operand is at a distance only its own record has, and an operand's sense is
its consumer's, which the `!`s above it change: down a nest of operators, a
turn after its consumer's. The specification named what to measure against
the loop if the loop read over its gate: the senses as a segmented scan of
bits a block, and the shapes by a permute of the consumers' templates. The loop reads +0.29
ticks a record swept on `sqlite3.c` and +0.22 on Lua by the switch (27 and 59 a turn), and the rows with it +0.32 and
+0.34 over the piece before's: within its gate, so that build was not made. What the update's piece read of a rare
case's terms in a big sweep stands against it: as masks in the shapes' sweep an update's terms made that sweep
read 0.22 ticks a record more on `sqlite3.c` and 0.49 on Lua, 0.18 and 0.47 of it with no update to make, and an
operator with a turn is one record in 94 of `sqlite3.c`'s and one in 268 of Lua's.

**What is next:** the entries' values.

### The entries' values on the window

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/x-values-2850-2026-11-20).
Its [specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7043553775)
had five gates: exact (V1), how much (V2), cost (V3), that nothing else moves
(V4) and faults (V5). The cost gate is refuted as it was specified.

An entry of X's tape is a kind, a class and a value (`node_machine_expand`'s
three columns), and the three pieces before made the first two on the
window. This piece makes the third: a quadword an entry, a stream beside the
kinds' and the classes'. With it a tape the rows make is whole, and it is
compared with X's on T's columns entry by entry, its value too. No compile
takes any of it.

**What a value is** (`node_machine_own`, `node_machine_address`,
`node_machine_fold_conversion`, `node_machine_update_first` and `_last`,
`node_machine_branch_hook`, `node_machine_call_own`):

- an object's number, for a LOCAL, a GLOBAL and their addresses; a store's
  and an update's by name are their destination's. At reader time a global's
  entry holds its object, which stands for its symbol until a splice patches
  it;
- an integer constant's bits, masked to its class's width and extended by
  its sign; a folded one's by its own class and then its target's, and
  whether it is not zero for a bool's; a floating one's as a float's or a
  double's;
- a member's offset in bytes and its sum's 1; an address constant's offset
  and its sum's 1;
- a pointer's stride, its sum's, its subscript's and its update's;
- an update's step: 1, or one's bits of a floating class;
- a branch's label: the control's own, or the label of the operator that
  owns the tested value (an `&&` branched where true or an `||` where false,
  for its first operand), its preorder number among the expression's
  operators with a label;
- a call's row, its preorder number among the expression's evaluated calls;
- a postfix update through an address's two temporaries, numbered in the
  order their first rows are written;
- and zero for every other entry: an operation's, a conversion's, a test's,
  a discard's, and an access's (a vector's bytes, and C takes no vector's
  access).

**What the count before the specification found.** A line a record of each
expression the rows take, with its row's entries as the reference's tape has
them, and a candidate rule for each value run over the lines:
796,051 entries of the three corpora's and the compiler's own
source's, none wrong. Half the values are zero (50.59%, 54.95%, 52.65% and 50.59% are not),
and a record has about half a value that is not (0.568, 0.485, 0.515 and 0.628). What
is no field of a line and no fixed number is a numbering, and nearly every
one is its first: a call's row is zero for 30,216 of
31,842 calls, an operator's own label for 1,962 of
2,103, and no expression of the four has two postfix updates
through an address. An expression with two or more of an evaluated call, an
operator with a label and such an update is about one in eighty:
474 (0.97%) of `sqlite3.c`'s, 163 (1.65%) of Lua's, 635 (1.61%) of musl's and 479 (1.21%) of the compiler's own source's.

**The stream starts as zeros and its sums' ones.** After the rows' write,
which gives each row its place: 64 entries a step, a compare of their kinds
with PTR_ADD's and eight masked stores of a zero or a one. A member's
offset's sum and an address constant's have that 1, which no loop then
writes; a pointer's stride is written over its own.

**Two columns hold what a loop would read of a line.** A record's line is 64
bytes, and the first build's loops read one a record, a line of the cache a
value. The tests' sweep loads every record's line for its bytes already, so
it stores a quadword a record too, the record's value (`window_x_values`):
its object's number where it has one and its constant is an address's,
else its payload (a constant that is no address's may have an object all the
same, `(_Bool)&x`: "A summary node's side facts on the window" found it, in
none of the corpora). The rows' reads'
loop, which reads a member's line for its offset, leaves the offset in bytes
in that place. And C's loop, which asks a type's row for a pointer's stride
to choose its template, keeps the stride it asked for (`window_c_strides`).
The loops below read these two columns and write the stream.

**One sweep of masks makes the lists**, 64 records a step over each record's
own shape, once the updates' and the branches' loops are done. A byte of a
table by the shape (`NodeRowsTables.value_owns`) has a bit a list, and a list
is the block's records' numbers under its bit, compressed. A constant's list
is by its class and its fold too: an integer's is the constants', a floating
one's and one folded to bool the rest's.

| list | the value | where in its row |
| --- | --- | --- |
| a name's read, an address constant, an update's read by name, a member's offset | its record's value | its first entry |
| a store by name | its first operand's record's value | its one entry |
| an integer constant, plain or folded | its record's value, extended from its own class's width and then its entry's | its first |
| a pointer's sum, subscript and update | its stride; and a 1 for an update's step | the PTR_ADD's place, its shape's; its first |
| a tested value the control owns | the control's label | its last |
| the rest: a floating constant, one folded to bool, an address constant's offset after its object's address | by its shape, from its line | |
| a record with a number of its expression's that is no first such record of it | the numbering's, below | |

The fifth list is the branches' loop's, made as it gives a value its test,
and the chunk's updates are the updates' loop's list already: a loop over
them writes each one's store by name's object, its step (one of the class it
is computed in) and, of a postfix update through an address, its
destination's two temporaries, the expression's first pair.

**A loop a list:** each value a load or two and a store at its row's place
(the entries before its eight records' and before it among them, two
columns' loads), none waiting for another. Each loop's step is in one copy
(`unroll(disable)`): a list is some tens of records a chunk.

**The numbers of an expression.** A call's row, an operator's own label and
a postfix update's temporaries are numbers in their expression, and its
first of each is what the loops wrote: a zero, and the pair 0 and 1. The
sweep finds every record with such a value that is no first one of its
expression by one subtraction a block, from two masks: those records (a bit
of the table's byte), and the expressions' operands' last records, which the
reader marks in the senses' column as it registers an expression. A borrow
from an expression's first record stops at its first marked bit, the one bit
that then differs; the other marked bits are the list. The expression that
goes on from the block before with a marked record there has its first in
this block on the list too, a word of state a block. Each listed record's
expression is then numbered once, a record at a time, by the reference's own
rules (`node_rows_window_number`): its calls' rows, its labels down a walk
from its last operator with a label, and its temporaries.

**How it came to its cost.** Builds on the 9700X, TSC ticks a record swept,
two or three readings each: the rows' sweeps with the values, and the
values' own part of them, a chunk's first run and its second. The piece
before's executable read 5.07 (5.03 to 5.21) on `sqlite3.c` and
6.43 (6.32 to 6.68) on Lua beside the last of them.

| build | `sqlite3.c`, the rows' sweeps with the values | the values' part: a first run; a second | Lua, the rows' sweeps | the values' part | the values' sweep's code, bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| first: seven lists, each loop reading its records' lines, and a pointer's type's row for its stride; an expression's marked records counted in its part | 9.51 (9.45 to 9.62) | 3.30; 2.13 | 11.84 (11.76 to 12.49) | 4.19; 1.89 | 4,290 |
| a record's value and a pointer's stride as two columns; the tested values' list from the branches' loop and the updates' from their own; an expression's later marked records by a subtraction a block; a member's offset, an update's step and its store's object written under a condition | 10.19 (10.09 to 10.63) | 4.04; 2.30 | 12.61 (12.61 to 13.00) | 5.42; 2.36 | 5,145 |
| those three written at a place chosen without a branch | 9.32 (9.24 to 9.39) | 4.06; 2.28 | 12.35 (12.34 to 12.36) | 5.56; 2.34 | 5,142 |
| the stream's start with its sums' ones, by the entries' kinds, and no list for the members; each loop's step in one copy, its columns' addresses loaded once; the numbering's expression from a column the reader writes, and its labels' walk over the operators' records alone | 8.25 (8.22 to 8.67) | 3.14; 1.86 | 11.01 (10.91 to 11.45) | 4.23; 1.89 | 3,537 |
| the fourth's with the columns' and the stream's lines asked for from the rows' write | 8.59 (8.48 to 8.87) | 3.11; 1.86 | 11.24 (11.16 to 11.45) | 4.24; 1.86 | 3,537 |
| the fifth's with the sweep compiled for size | 8.89 (8.82 to 8.97) | 3.43; 2.08 | 11.06 (11.03 to 11.13) | 4.20; 2.08 | 2,373 |

- **A value written where its record has one was a branch.** The second build's reads' loop left a member's
  offset in the record's value's place as `*value = member ? offset : *value`, and the updates' loop wrote an
  update's step and its store's object the same way. The compiler made each a store behind a branch on the
  record's kind, which no predictor learns: the reads read 2.65 to 2.76 ticks a record swept on `sqlite3.c` where the
  first build's read 1.67 to 1.76. The third build writes each at a place chosen by arithmetic, the record's own
  or one past the column's records that no one reads: 1.77 to 1.80.
- **The two columns and the lists moved read no better by themselves.** The third build's rows' sweeps read 9.32
  ticks a record swept on `sqlite3.c` for the first's 9.51, and 12.35 for 11.84 on Lua; its values' part more, 4.06 and
  5.56 for 3.30 and 4.19. Its stream started as zeros, with a list of the members that wrote each one's offset and its
  sum's 1, and its loops reached each column through the reader's state. The fourth starts the
  stream with the sums' ones by the entries' kinds, has a member's offset on the first list, and each loop's step
  in one copy (`unroll(disable)`): the values' part read 3.14 and 4.23, the rows' sweeps 8.25 and 11.01, and the
  sweep's code is 3,537 bytes for 5,142. The builds changed more than one thing at a time, and no reading is of the
  columns alone.
- **A chunk's first run pays a cold start, more than work a record.** A build that reads the processor's counters
  around the sweep (user mode, a chunk's average; `sqlite3.c` has 526 records a chunk and Lua 379): the sweep
  executes 4,313 to 4,429 instructions a chunk on `sqlite3.c` and 1,974 to 2,075 on Lua, the same in a chunk's two runs,
  in 1,898 to 2,284 cycles the first run and 1,262 to 1,323 the second (Lua: 1,795 to 2,316 and 931 to 961). The first run has
  18.9 to 22.7 mispredicted branches a chunk for the second's 10.6 to 12.0, 18.4 to 73.9 instruction-cache misses for
  2.7 to 27.4, and 631 to 1,594 cycles with the front end stalled for 231 to 364. No run of either corpus has more than
  0.1 TLB misses a chunk, data or instructions. The fourth build's ticks a chunk are nearly the same on the two
  (1,652 and 1,602) though `sqlite3.c`'s chunk has 39% more records. The rows' write, measured the same way,
  takes 835 to 843 cycles a chunk its first run and 683 to 686 its second on `sqlite3.c`.
- **Two builds that went after the cold start changed nothing.** Asking for the columns' and the stream's lines
  from the rows' write, the sweep before: the write read 1.19 and 1.79 for 0.92 and 1.50, and the values' part
  3.11 and 4.24 for 3.14 and 4.23. Compiling the sweep for size on top of that (2,373 bytes for 3,537): 3.43 and 4.20.
  Neither is in the code.

**Measured.** The 9700X, CPU 2, Release, each run its own hold of the lock,
TSC ticks, seven readings; in each reading one executable with the
values on, off (an experiment's switch: no record's value's column, no list,
loop or numbering, and no value compared), the piece before's executable
twice, then off and on again.

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions C takes on the window | 49,155 | 9,909 | 39,712 |
| their tapes made here, whole | 49,073 (99.83%) | 9,888 (99.79%) | 39,495 (99.45%) |
| tapes that differ from X's on T's columns, in a value too | 0 | 0 | 0 |
| listed for a value | 0 | 0 | 0 |
| their entries | 302,047 | 54,341 | 184,146 |
| records on a list, a record swept | 0.482 | 0.363 | 0.288 |
| expressions numbered | 474 (0.97% of the tapes) | 163 (1.65% of the tapes) | 635 (1.61% of the tapes) |
| **the rows' sweeps and the part, a record swept** | **8.50 (8.27 to 8.66)** | **11.09 (10.98 to 11.58)** | **21.26 (19.89 to 22.95)** |
| the piece before's executable, in the same readings | 5.05 | 6.41 | 11.29 |
| **more than the piece before's** | **+3.48 (+3.13 to +3.69)** | **+4.78 (+4.28 to +4.91)** | **+10.16 (+8.10 to +12.23)** |
| with the values off; on less off | 5.15; +3.38 | 6.94; +4.19 | 13.81; +7.63 |
| with the values off, more than the piece before's | +0.12 (-0.04 to +0.21) | +0.49 (+0.20 to +0.75) | +2.53 (+0.98 to +3.72) |
| the values' part | 3.20 | 4.11 | 7.48 |
| the reads; on less off | 1.83; +0.20 | 2.26; +0.17 | 4.77; +0.44 |
| the shapes | 0.60 | 1.05 | 2.38 |
| the updates' and the branches' lists and loops; on less off | 0.97; +0.01 | 1.12; +0.00 | 1.98; +0.01 |
| the rows; on less off | 0.95; +0.02 | 1.52; +0.02 | 3.12; +0.01 |
| an expression's part | 0.96 | 1.06 | 1.46 |
| a second run over the same chunk; the piece before's | 6.17; 4.05 | 6.17; 3.72 | 8.04; 4.83 |
| the values' part in it | 1.93 | 2.05 | 2.91 |
| the values' part, ticks a chunk: a first run; a second | 1,683; 1,015 | 1,556; 776 | 948; 369 |
| the records' bytes, which make a record's value; on less off | 9.33; +0.30 | 11.88; +0.26 | 23.36; +0.42 |
| C's loop, which keeps a stride; on less off | 3.37; +0.04 | 5.21; +0.05 | 9.03; +0.54 |
| a record of the taken expressions | 10.16 | 16.45 | 42.59 |
| X on T's columns, a record of theirs | 25.61 | 23.69 | 28.64 |
| records a chunk | 526 | 379 | 127 |

**Against its gates.**

- **V1, exact: met.** No tape made differs from X's on T's columns in its number of entries, a kind, a class or a
  value, on the three corpora on the 9700X (49,073, 9,888 and 39,495 tapes), and no chunk's second run differs from its
  first. In the cloud container the same on the three and on the compiler's own source (39,473 tapes of its 40,215
  expressions, 98.15%), with the default warnings and with `-Wall -Wextra`. The values' fixture has a line a source of a
  value (1,057 tapes), the rows' 1,166, C's 1,479 and two units of one chunk 3 each: with one of the reference's answers
  changed for each tape the comparison counts every one, and with one of a chunk's first run's answers changed every
  chunk's second run differs. And 5,447 one-statement programs, each a file of its own: 4,313 written for the values (a
  constant of each class, plain and folded; an address constant; a member; a pointer's sum, subscript and update; a
  store and an update by name and through an address; calls nested and side by side; conditions whose operators own
  a label; postfix updates through an address), 1,476 of the 5,447 with an expression numbered, and the 1,134 of the pieces
  before.
- **V2, how much: met.** Every expression whose rows the piece before made has its tape whole: none is listed for a
  value on the three corpora or the compiler's own source (0.5% refutes). The numbered expressions are the count's: 474,
  163, 635 and 479.
- **V3, cost: refuted.** The rows' sweeps and the part read +3.48 ticks a record swept more than the piece before's
  executable on `sqlite3.c` (+3.13 to +3.69) and +4.78 on Lua (+4.28 to +4.91); 1.5 was asked and 2.5 refutes. On musl, which the gate
  did not name, +10.16 (+8.10 to +12.23): its chunks are a quarter the size. The values' own part is 3.20, 4.11 and 7.48 of it;
  by the switch, on less off, the reads, which leave a member's offset, read +0.20 and +0.17 more on the first two, and the
  rows' write +0.02 and +0.02. With the values off the same executable reads +0.12 and +0.49 more than the piece before's, its
  two clock's reads around the sweep among them. A chunk's
  second run reads the whole +2.14 and +2.46 more than the piece before's, the values' part 1.93 and 2.05: the work a
  record is over what was asked with its code and its tables at hand, and a first run adds a cold start a chunk to it
  (1,683 ticks a chunk for 1,015 on `sqlite3.c`, 1,556 for 776 on Lua).
- **V4, nothing else moves: met.** By the switch, on less off, on `sqlite3.c` and Lua: the kernel in place -0.02 and -0.09;
  the tests' loop +0.01 and +0.02, their expression's part -0.01 and +0.01, their sweep on T's records +0.05 and +0.18; C's targets +0.03
  and +0.00, its expression's part +0.01 and -0.01. The two sweeps the piece gives work to: the records' bytes, which make a
  record's value, +0.30 and +0.26 a record of the tests', at the gate's bound, and C's loop, which keeps a stride, +0.04 and +0.05
  (0.3 asked).
- **V5, faults: met.** Two rounds: each entry of the values' tables, each term of the sweep's masks and of the later
  records' arithmetic, each loop's load, shift and store, the two lists made before the sweep, the columns' makers, each
  term of the numbering, and the shadow's comparisons and their control, changed alone, a build and the fixture's
  tests a fault. The first round's 352 faults, run from the list's two ends at once (the 9700X and the cloud container:
  51 faults by both, with the same verdict each), left 41.
  - Eight were terms no program separates, and are out of the code: an address's range beside the two variants that
    are within it; a record's constant's kind beside its object (X's own test is the object's, and a count found no
    record of the three corpora with an object and a constant that is no address's); a fold beside a floating
    class and the reads' mask beside a fold (a folded constant is an integer's with a target the reads read); the
    expressions' ends in the subtraction (a borrow that runs on stops at the next expression's first marked record,
    which no start is); and the numbering's test for two marked records (a count of its calls found none with fewer).
  - Five change nothing a program reaches, and are no faults: an object's number sign-extended (it is a token's
    index and one); an update's step's class read from the operation after it, which is of the same class; a
    destination's shape taken through the kept rows' rest's, which are a conversion's part's; an expression's marked
    records taken from its second record on, its first being a leaf; and an owned record with any conversion's part
    taken for a tested value, which a numbered expression has none of.
  - Seven were the shadow's own comparisons, which no test could fail. The control now changes a value's lowest bit and
    its highest, a tape's first entry's and its last's, and a value beside a class; and one of a chunk's first run's
    answers in thirteen turns, so that the second run's comparison counts every chunk.
  - Fourteen were in the later records' arithmetic, where a superfluous record on the list numbered nothing and changed
    no answer: the list's records are counted and pinned now.
  - Six got a line in the fixture, each run against its fault's build before the next round: an expression with two
    calls beside one C leaves; an operator with no label between a tested value and its owner; a member past 4 GB;
    a constant before an entry of a narrower class; a listed expression with two calls; and a call with no
    argument right after a condition. And one comparison had slack: a call's holder is one whose subtree starts no
    later than its own, where the code asked no later than its record.
  The second round's 89 faults, every one whose text changed, the new ones and the first's survivors, on the code as
  it is, are all caught.

**What stays a record at a time, and why.**

- **The lists' loops:** 0.48, 0.36 and 0.29 listed records a record swept. A value is a load from a column and a store at
  its row's place, which is the sum of two columns' bytes for its record: the places are no register's eight in a
  row, and a scatter is 88 micro-operations on Zen 5, one every 17 cycles ("SIMD and Zen 5" in the coding style).
  The vector makes the lists. The specification named two things to measure against the loops if they read over
  their gate. The first is in the code since the second build: a column of each record's one value (the rows'
  sweeps read 9.51 and 11.84 in the first build and 8.25 and 11.01 in the fourth). The second is not: the values
  written by slots as the kinds are, eight quadwords a register through a compress. What the counters say of the
  loops is that their work is not the cost a chunk's first run pays: the sweep executes the same instructions in
  both runs, and takes 1,898 to 2,284 cycles a chunk the first and 1,262 to 1,323 the second on `sqlite3.c`.
  [#3196](https://github.com/buster14a/buster/issues/3196) has that cold start, for every sweep of the
  window, and the by-slots build as what to measure there.
- **The numbering:** 0.97%, 1.65% and 1.61% of the tapes. A call's row and a label are preorder numbers in the
  expression's tree, and the reference numbers them a record at a time too. The sweep finds those expressions with
  no test an expression: one subtraction a block.

**What is next:** `&&` and `||` as values, `?:` and a comma, in C and in X on
the window together (C lists them still); then a taken expression's
references and events, and the splice and the summary node, #2524's step 2.

### An operator's value and a comma on the window

`&&` and `||` as values, `?:` and a comma were what C on the window listed
most after the entries' values: C decided no expression with one, and X made
no row of theirs. This piece makes them in both, in the sweeps that exist:
masks in C's loop, turns in the branches' loop, and the numbering by labels
([#2850's specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7049301495);
`measure-node-machine --shadow --window-rows`,
[the guide](commands/lowering.md#an-operators-value-and-a-comma-on-the-window-2850);
[the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/logical-values-2850-2026-11-20)).
No compile takes any of it.

**What the reference makes of them** (`node_machine_logical`,
`node_machine_classify`'s comma, `node_machine_hook`, `node_machine_own`).
An `&&`, an `||` or a conditional whose condition's truth is not known has two
labels, its alternate and its end. Its condition is a value converted to T's
conversion of it and tested after its row, with a branch to the alternate, or,
where an `&&` or an `||` is under its `!`s, it is evaluated as branches to
the alternate. A conditional's first arm is converted to the conditional's
type and then jumps to the end, where the alternate is placed; its second arm
follows. A logical operator's own row tests its second operand, jumps to the
end, and has its value at the alternate, 0 for `&&` and 1 for `||`. A record
whose consumer's conversion the tasks put into its arms (`node_machine_sinks`,
#2678) is left to them. A comma of a number's class passes its second
operand: its first is converted to void, and its second to the comma's type,
or to void where the comma's own target is void, which gives it the discard's
context and no conversion of its own.

**What the count before the specification found**
([the census](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/logical-values-census-2850-2026-11-20):
a line a record of every expression with such an operator, the reference's
answers and rows beside the window's bytes):

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions with an `&&`, an `\|\|`, a `?:`, a comma or a selection | 3,133 | 876 | 2,002 |
| the window took already (a statement's condition as branches) | 2,047 | 200 | 792 |
| listed for one of these alone, and taken by the reference | 825 | 387 | 1,033 |
| candidate rules, simulated over the lines, take of them | 793 | 366 | 970 |

Of the listed records, 1,343 were conditionals, 743 commas and 434 an `||`'s
value. A conditional's condition is a tested value in 515 of `sqlite3.c`'s
525; its first arm is converted to another class in 44; 55 of Lua's and 26 of
musl's are of void's class. 368 of musl's `||`s are discarded,
`(void)(a || b)`.

**C: a comma.** A mask in the loop: a free record of a comma's class and a
number's has a wrapper's template. Its operands' rows are a key's of its own,
since a wrapper's give its operand the wrapper's type: its first operand to
void, a new source the targets' sweep writes; its second to the comma's type,
or to void where the comma is discarded. A comma is discarded where its
target is void, which is its consumer's to say with no chain: a comma's first
operand is; a wrapper's operand and a conditional's arm are where that
record's class is void's, a byte's load a comma. A comma that is a comma's
second operand has that comma's discard, a chain: it lists its expression, as
do a comma read for its address and a folded one, whose constant's template
has the discard's context.

**C: which operators are branches.** C says it from the root down. The loop
said it of a record by its consumer's class alone, and listed every `&&` and
`||` that was not under a branched operator or a statement's CONDITION; with
values to take, a record under an `&&` has to know whether that `&&` is
branches or a value. A record says it of itself:

- A **branch operator** is a `!`, an `&&` or an `||` that is no folded
  constant, and for the last two one whose first operand's truth is not
  known.
- It is **entered**, branched whatever its consumer is, where it is the first
  operand of an `&&` or an `||`: under a branched one it is its operand, and
  under a value's it is the value's condition, which is branches where an
  `&&` or an `||` is under its `!`s. A value whose first operand is a `!`
  lists its expression, since a `!` alone is no such condition.
- A `!` is **deep** where a branch operator `&&` or `||` is under it with
  `!`s alone between: the runs of `!` records right after such an operator's
  record, by one sum's carries (a `!`'s operand is the record before it). A
  deep record is entered too as a statement's CONDITION's operand, where the
  `!` lists (the CONDITION is branched itself, two levels away), and as a
  conditional's condition.
- It is **chained** where it is an `&&`'s or an `||`'s second operand or a
  `!`'s operand: it has its consumer's answer, and its consumer is the next
  record. The answers go down each run of chained records by two doubling
  steps of two masks, three records up, with a bit carried from the block
  after; a longer run lists its expression, and so does a block's last record
  that is a branch operator under a `!`, whose run the block after did not
  see.
- Any other is a **value**.

**C: an operator's value.** An `&&` or an `||` that is a branch operator and
a value, and a conditional of a number's class or of void's whose condition's
truth is not known, where the condition's conversion's type is one a truth's
entries test (and a logical operator's second operand's too), have the
template with two labels. A conditional's operands' rows are a key of its
own: its condition to T's conversion of it, its arms to its own type. Three
of C's terms are a record at a time, for these records alone, one record in
450, 220 and 401 of those swept: a conditional of one operand
twice (`a ?: b`), the conversion its consumer puts into its arms
(`node_contexts_sinks`), and the same for a root, whose target its
expression's part gives. Each lists. The sinks' rule here is not the
reference's term by term. It is three of its terms: a target of another type
than the record's own, of an integer's class, and an arm that is a constant
(a logical operator's 0 or 1 is one; a conditional's arm is where it is an
integer constant). The reference asks more, each a term that takes: that the
conversion write entries, that it fold for these classes, and that the arm be
no lvalue, of an integer's class and with no effect. What those would take is
listed here: 16 expressions of `sqlite3.c`, none of Lua
and 6 of musl, by the build before, which had the rule term by term
(the faults, below).

**Listed still:** a condition whose truth may be known (one operand is not
evaluated) and a folded conditional; a value whose first operand is a `!`; a
`!` over an `&&` or an `||` under a statement's CONDITION; a run of more than
three chained records, and a block's last record that is a branch operator
under a `!`; `a ?: b`; what the tasks sink and the little more the sinks'
rule lists with it; a comma read for its address, a folded one and a comma's
second operand that is a comma; and a selection.

**X: the turns.** The branches' loop goes down a list of the chunk's
operators evaluated as branches, and an operator's value is on it too
(`node_rows_window_held`):

- Its **condition** takes a tested value's segment, the shape that exists,
  with the sense of its operator (a branch where it holds for `||`, where it
  doesn't for the others) and its operator's label; a condition that is
  branches takes the sense for its own turn.
- A conditional's **first arm** has its jump after its conversion's entries,
  in that part's slots: a row of the table by whether the conditional is
  void's, whether the reads read the arm's target's class and the arm's
  conversion (none, a CONVERT, a DISCARD). An arm converted to bool leaves no
  slot: listed.
- A logical operator's **own row** is four entries, its own part: the test of
  its second operand's conversion's class, its JUMP_VALUE, and its CONST. A
  narrow class's test is one entry more: listed.
- A conditional's own part has no entry: its end's label is placed there.

**The labels.** An operator's value has two where an operator evaluated as
branches has one or none, so the numbering counts labels, and a holder's jump
has its first label's number and one. A call's row and a label are counted
apart: an expression with one operator with labels has the labels 0 and 1
whatever calls it holds, which the lists' loops write (a first arm is on the
list of the rows whose last entry's value is a fixed one, with a bit for its
1), and the numbering runs where an expression has two of one kind:
428, 318 and 1,071 of the tapes on the three corpora
(0.86%, 3.10% and 2.65%), where the piece before's ran on
474, 163 and 635.

**How it came to its cost.** Three builds on the 9700X, CPU 2, Release, seven
readings each, against the piece before's executable in the same readings
(TSC ticks; the first two against #3197's head `8a7427df2`, the third against
main with it merged, `549613156`):

| build | expressions more taken: `sqlite3.c`; Lua; musl | C's loop, a record swept, more, a chunk's first run: `sqlite3.c`; Lua | its second run: `sqlite3.c`; Lua | C's sweeps' first run less their second, ticks a chunk, more: `sqlite3.c`; Lua |
| --- | ---: | ---: | ---: | ---: |
| the first: two more permutes a block, of the block's answers by the consumer's lane (a `!` that is not deep has its consumer's answer) and of its classes (a comma's consumer's, before the loop that reads its type); a comma's and a conditional's keys three blends in a row; the discard's context a blend | +789; +360; +958 | +1.34 (+1.04 to +1.49); +1.97 (+1.63 to +2.01) | +0.61 (+0.59 to +0.62); +0.65 (+0.62 to +0.83) | +495 (+342 to +602); +632 (+504 to +683) |
| the second: neither permute (a `!` under an operator is entered or chained as an `&&` is, and a value whose first operand is a `!` lists; every comma under a wrapper or an arm loads its consumer's byte), the three keys made beside the templates and placed in one step, a discarded comma's context a byte's store each, a block's edge listed from the block below | +785; +360; +955 | +0.91 (+0.62 to +1.06); +1.34 (+1.20 to +1.58) | +0.52 (+0.47 to +0.53); +0.54 (+0.51 to +0.65) | +302 (+158 to +395); +443 (+382 to +538) |
| the code: the second, with the terms the first fault round found implied taken out and the sinks' rule its three terms | +769; +360; +949 | +1.02 (+0.94 to +1.49); +1.94 (+1.44 to +2.94) | +0.44 (+0.35 to +0.48); +0.53 (+0.51 to +0.57) | +410 (+379 to +668); +630 (+324 to +1051) |

A second run's readings fell with each build. The first runs' are too wide to
order the three builds: the third build's ranges hold the first's on Lua.

**Measured.** The 9700X, CPU 2, Release, each run its own hold of the lock,
TSC ticks, seven readings; in each reading the build, the piece before's
executable twice, and the build again:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions C decides on the window; the piece before's | 50,109; 49,310 | 10,715; 10,079 | 42,776; 41,759 |
| **expressions whose tape X makes, whole; the piece before's** | **49,842**; 49,073 | **10,248**; 9,888 | **40,444**; 39,495 |
| more of them | +769 (1.51% of the expressions) | +360 (3.32% of the expressions) | +949 (2.19% of the expressions) |
| their records; the piece before's | 278,867; 268,942 | 70,873; 61,509 | 204,846; 188,250 |
| answers of C's; tapes of X's that differ from the reference's | 0; 0 | 0; 0 | 0; 0 |
| operators' values C takes; of them listed for their target; commas | 715; 98; 330 | 414; 7; 283 | 940; 49; 184 |
| listed for a logical operator or a conditional; the piece before's | 135; 674 | 22; 290 | 115; 964 |
| listed for a comma; the piece before's (a selection among them) | 0; 264 | 5; 376 | 0; 169 |
| listed for X by a turn (an arm with no room, a narrow second operand) | 3 | 0 | 0 |
| expressions numbered; the piece before's | 428; 474 | 318; 163 | 1,071; 635 |
| **C's sweeps, a record of the decided, a chunk's second run; the piece before's; more** | 6.79; 6.40; **+0.36 (+0.34 to +0.44)** | 7.25; 7.61; **-0.35 (-0.38 to -0.30)** | 12.31; 10.95; **+1.53 (+1.09 to +2.51)** |
| **the rows' sweeps, a record of the taken, a second run; the piece before's; more** | 7.30; 7.32; **-0.04 (-0.52 to +0.31)** | 8.83; 9.14; **-0.29 (-0.44 to +0.09)** | 15.74; 16.05; **-0.20 (-0.96 to +5.21)** |
| C's sweeps, a record of the decided, a first run; the piece before's; more | 9.02; 7.67; +1.22 (+1.12 to +1.83) | 13.09; 12.01; +1.02 (-0.14 to +2.36) | 34.48; 29.55; +4.93 (-0.79 to +17.16) |
| the rows' sweeps, a record of the taken, a first run; the piece before's; more | 10.15; 10.00; +0.15 (-0.33 to +1.44) | 16.00; 16.36; -0.46 (-0.99 to +5.09) | 43.61; 44.22; -2.18 (-7.98 to +21.59) |
| C's sweeps, a record swept: a first run, more; a second, more | +1.32 (+1.24 to +1.84); +0.52 (+0.50 to +0.59) | +2.41 (+1.59 to +3.50); +0.71 (+0.68 to +0.75) | +4.12 (+1.06 to +11.21); +1.38 (+1.15 to +2.04) |
| the rows' sweeps, a record swept: a first run, more; a second, more | +0.44 (+0.03 to +1.55); +0.19 (-0.21 to +0.50) | +1.34 (+0.95 to +5.60); +0.71 (+0.65 to +1.01) | +0.81 (-2.07 to +13.62); +0.59 (+0.18 to +3.54) |
| **the rows' sweeps, a first run less a second, ticks a chunk; the piece before's; more** | 1299; 1171; **+116 (-63 to +558)** | 2065; 1859; **+237 (+74 to +1876)** | 1884; 1778; **+63 (-505 to +1653)** |
| C's sweeps, a first run less a second, ticks a chunk; the piece before's; more | 1021; 552; +410 (+379 to +668) | 1800; 1159; +630 (+324 to +1051) | 1635; 1246; +373 (-42 to +1162) |
| C's loop, a record swept: a first run; the piece before's; more | 4.41; 3.30; +1.02 (+0.94 to +1.49) | 7.20; 5.21; +1.94 (+1.44 to +2.94) | 13.99; 11.21; +3.49 (+1.58 to +11.31) |
| C's loop, a second run; the piece before's; more | 2.87; 2.42; +0.44 (+0.35 to +0.48) | 3.20; 2.67; +0.53 (+0.51 to +0.57) | 3.99; 2.79; +1.24 (+1.05 to +1.38) |
| C's targets; its expression's part: more | -0.01; +0.33 | -0.08; +0.56 | -0.13; +0.60 |
| the rows' reads; shapes; write: more | -0.05; +0.01; +0.08 | +0.06; +0.03; +0.08 | -0.28; -0.31; +0.31 |
| the updates' and the branches' lists and loops: a first run; the piece before's; more | 1.11; 0.87; +0.25 (+0.21 to +0.37) | 1.50; 1.06; +0.41 (+0.21 to +1.41) | 2.45; 1.91; +0.60 (+0.29 to +2.28) |
| the values: a first run; the piece before's; more | 3.35; 3.15; +0.22 (-0.12 to +0.26) | 5.18; 4.14; +1.04 (+0.95 to +1.07) | 10.05; 8.76; +0.88 (-1.73 to +3.63) |
| the values, a second run; the piece before's; more | 1.93; 1.85; +0.10 (+0.03 to +0.14) | 2.37; 1.90; +0.47 (+0.44 to +0.55) | 2.56; 2.44; +0.13 (+0.11 to +1.04) |
| the kernel in place, more; the tests on the window, more | -0.33 (-0.80 to -0.24); +0.21 (-0.17 to +0.39) | -0.36 (-2.67 to -0.12); -0.59 (-2.58 to +1.80) | -0.17 (-3.27 to +3.05); -4.12 (-5.91 to +5.21) |
| the reference's C, a record of the decided; its X, a record of the taken | 40.50; 25.80 | 36.84; 23.50 | 43.70; 28.97 |
| records a chunk; chunks | 526; 611 | 379; 241 | 127; 2,975 |

**Against its gates.**

- **L1, exact: met.** No answer of C's and no entry of X's tape differs from
  the reference's on the three corpora on the 9700X
  (49,842, 10,248 and 40,444 tapes), and no chunk's second run
  differs from its first. In the cloud container the same on the three and
  on the compiler's own source (41,890 tapes of the 43,614 expressions C
  takes there, of 52,163), with the default warnings and with `-Wall -Wextra`,
  14 runs. C's fixture has a function of these operators, a line a
  term (470 operators' values, 18 of them listed for their target, and
  224 commas, pinned). And 6,414 one-statement programs, each a file of its
  own: 966 written for this piece (a comma under each consumer, discarded
  and not; a conditional with each arm's conversion and each kind of
  condition; `&&` and `||` as values with each operand; and nine statements
  whose answer is another record's, the next one's or a consumer's past a
  long operand, at each lane of a block), of which the window takes 857 whole, lists 70 in C and
  23 in X and refuses 16 as the reference does, and the 5,447 of the pieces
  before.
- **L2, how much: met on Lua, not on the other two.** 769, 360 and
  949 expressions more are taken, C's answers and X's tape both, where
  780, 355 and 950 of the census's 825, 387 and 1,033 were asked: 11 short
  on `sqlite3.c` and 1 on musl. The second build took 785, 360 and 955: what
  is short is what the sinks' rule lists since the first fault round (L6).
  On the compiler's own source, which the gate did not name, 2,417
  expressions more have their tape whole (41,890 for 39,473, the same source
  through main's build and the piece's), 2,712 of its 9,606 operators' values
  are listed for their target, and X lists 1,448 expressions at a turn for 560
  (an arm converted to bool and a second operand of a narrow class are what
  the piece lists there, and this source has `bool` where the corpora have
  `int`).
- **L3, work a record: met for the rows; C's reads +0.36 on `sqlite3.c`
  where 0.35 was asked.** A chunk's second run of the rows' sweeps reads
  -0.04 (-0.52 to +0.31) ticks a record of the taken more than the piece
  before's on `sqlite3.c` (0.30 asked) and -0.29 (-0.44 to +0.09) on Lua (0.40). C's
  sweeps read +0.36 (+0.34 to +0.44) a record of the decided on `sqlite3.c` and
  -0.35 (-0.38 to -0.30) on Lua (0.35 asked on either; 0.70 refutes). On musl,
  which the gate did not name, -0.20 (-0.96 to +5.21) and +1.53 (+1.09 to +2.51).
- **L4, a chunk: met on `sqlite3.c` and musl by their medians, not on Lua.** The rows' sweeps' first
  run less their second rose by +116 (-63 to +558) ticks a chunk on `sqlite3.c`,
  +237 (+74 to +1876) on Lua and +63 (-505 to +1653) on musl (150 asked, 300
  refutes): by the medians it is met on `sqlite3.c` and musl, Lua's is
  between the two, and each corpus's range reaches past 300. **The gate named the rows' sweeps alone, and C's loop is where
  the piece's cost a chunk is:** C's sweeps' first run less their second
  rose by +410 (+379 to +668), +630 (+324 to +1051) and +373 (-42 to +1162).
- **L5, nothing else moves: not as written.** The rows' reads are within 0.2
  on `sqlite3.c` and Lua (-0.05 and +0.06; -0.28 on musl). The kernel in place,
  whose source the piece does not touch, reads -0.33 (-0.80 to -0.24), -0.36 (-2.67 to -0.12) and
  -0.17 (-3.27 to +3.05): less in every reading of `sqlite3.c`, and less in each of the
  three builds (-0.29 and -0.45 in the first two). The tests' sweeps read
  +0.21 (-0.17 to +0.39), -0.59 (-2.58 to +1.80) and -4.12 (-5.91 to +5.21), on both sides of
  zero. A sweep with no change of its source reads a third of a tick or more
  apart between two builds here, which is the size of L3's margins: C's
  +0.36 against 0.35 is nearer than the readings can tell.
- **L6, faults: met, with five faults no test separates whose terms stay.**
  Two rounds: each entry of C's tables' new rows, each term of the loop's new
  masks and of the doubling steps, the comma's and the value's loops, the
  sinks' rule, the targets' source, each entry of the rows' new shapes and of
  the arms' table, each line of the turn, the write's second slot and the
  values' and the numbering's new terms, changed alone, a build and the
  fixture's tests a fault. The first round's 308 faults, run from the list's
  two ends at once (the 9700X and the cloud container), left 46.
  - 15 were terms that what makes the records implies, and are out of the
    code or in the fault's simpler form: the `!`s' mask on the seed of a
    deep `!`'s run; the last lane's mask on the chained records and the
    branched records' on the unreached runs; the address's bit in the
    conditional's mask, the value's and the comma's; a number's class in the
    logical value's (an int's, the checker's); the made commas' mask in the
    discard's and the placed records' in the commas' count; the void source's
    target in the sinks' step, which reads as none; the conditional's test
    beside `a ?: b`'s; the reads' test for the discard's context alone, where
    any context converts nothing; an arm's sense's other bits; the upper
    bound of a tested value's shapes in the numbering; and a comma's
    consumer's type, where its class's byte says the same.
  - 18 were terms of the sinks' rule as the first two builds had it, the
    reference's term by term on the window's bytes. The rule is three terms
    now, each with a fixture's line, and lists what the others would take
    (L2).
  - 10 found a line of the fixture or a pin to add, each caught in the
    second round: a folded conditional, a conditional and a value with a
    condition a truth's entries don't test, a value with such a second
    operand, a comma that is a conditional's condition, an `&&` the kernel
    didn't place, a target given by the second operand's conversion and one
    by the record's type, a narrow second operand of sixteen bits, and a
    turn of an operator that is not evaluated.
  - 3 change nothing a program reaches.
  The second round's 55 faults, every one whose text changed, the new ones
  and the first's survivors, on the code as it is: 50 caught, and 5 that
  change nothing a program reaches, whose terms stay as the reference has
  them. A conditional's arms' target read from T's conversion of its second
  operand in place of its own type, by the table's row or by its template's
  key: T converts both arms to the conditional's type. The conditional's key
  given to every operator's value in the inner of two blends, whose outer
  mask is the conditional's and the comma's. The sinks' rule asked with no
  target, whose class is no integer's. And an arm's constant's kind read with
  its category's bits, an integer constant being no lvalue.

Beside the gates, as the specification asked, what the reference's C and X
read on the newly taken records against what the piece adds over all the
records swept:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| records newly taken; of those taken before | 9,925; 3.7% | 9,364; 15.2% | 16,596; 8.8% |
| the reference's C and X, ticks a record of theirs | 66.30 | 60.34 | 72.67 |
| **the reference on the newly taken records, ticks** | **658,028** | **565,024** | **1,206,031** |
| the piece, C's sweeps and the rows', more a record swept: a chunk's first run; its second | +1.76; +0.71 | +3.75; +1.42 | +4.93; +1.97 |
| records swept | 321,434 | 91,252 | 377,187 |
| **the piece over all the records swept, a first run: ticks; of the reference's** | **565,724; 86%** | **342,195; 61%** | **1,859,532; 154%** |
| the same, a second run | 228,218; 35% | 129,578; 23% | 743,058; 62% |

**What stays a record at a time, and why.**

- **In C's loop, one record in 308, 131 and 336 of those swept** (an operator's
  value or a comma): whether a comma's consumer is of void's class, a byte at
  its consumer's place; a discarded comma's context, a byte's store; and an
  operator's value's target's rule, which reads its target's type's class and
  two operands' standings. The first build had the vector's forms of the
  first two: a permute of the block's classes by the consumer's lane, in
  every block, before the commas' loop, and the discard's context as a blend
  (and a second permute, of the block's answers, for a `!`'s consumer's).
  The second, with neither permute and a byte's store a discarded comma,
  reads -0.09 and -0.11 ticks a record swept against it on a chunk's second
  run of C's loop (`sqlite3.c` and Lua; the two builds differ in their keys'
  blends too).
- **X's turn,** one an operator's value: its condition's segment, its arm's
  shape or its own row, each a byte or two at its operands' places. The
  updates' and the branches' lists and loops read +0.25 (+0.21 to +0.37) ticks a
  record swept more on `sqlite3.c` a first run and +0.08 (+0.06 to +0.17) a second.
- **The numbering:** 0.86%, 3.10% and 2.65% of the tapes. A label is a preorder
  number in the expression's tree, as the reference numbers it.

**What it means.**

- **The operators' values and the commas are exact on the window,** and X's
  tapes are whole for 49,842, 10,248 and 40,444 expressions of the three corpora's
  50,993, 10,839 and 43,415.
- **Their work a record is small on `sqlite3.c` and Lua:** a chunk's second
  run reads +0.36 and -0.35 ticks a record of the decided more than the piece
  before's in C, and -0.04 and -0.29 a record of the taken in the rows. On musl C's
  reads +1.53.
- **Their cost is by the chunk, and it is C's loop's.** The loop's step has
  the new masks in every block, and its first run over a chunk reads
  +1.02, +1.94 and +3.49 ticks a record swept more where its second reads
  +0.44, +0.53 and +1.24. Over all the records swept, a first run of the two
  sweeps adds 86%, 61% and 154% of what the reference's C and X read on the
  newly taken records, and a second run 35%, 23% and 62%. **On musl, whose
  chunks are 127 records, a first run costs more than the reference's work
  it replaces.** What a chunk's first run pays is the window's cold start
  ([#3196](https://github.com/buster14a/buster/issues/3196)), which grew
  here with the masks the loop's step took on; the specification gated it
  for the rows' sweeps and not for C's.

**What is next:** a summary node's side facts, the flip's first piece
(#2524's step 2), below. The window's sweeps run in a compile after it,
where their cost by the chunk is settled.

### A summary node's side facts on the window

Step 2 ends with a flip: one summary node in the graph where a taken
expression's nodes were, and its tape spliced. Every pass after the reader
that reads below a full expression's root today then needs its answer from
that node's value or from a side record of it, or has to be shown not to
read there. This piece makes the side record's facts on the window, for each
expression whose tape X makes whole, and compares them after the pipeline
with the program's own walks on that expression's root ([#2850's
specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7053609091);
`measure-node-machine --shadow --window-rows`, [the
guide](commands/lowering.md#a-summary-nodes-side-facts-on-the-window-2850);
[the
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/summary-facts-2850-2026-11-21)).
No compile takes any of it.

**What reads below a root today** ([the survey and its
census](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/summary-facts-census-2850-2026-11-20):
the passes, a file and a line each, and what the program's walks say of each
taken root beside the window's bytes). In a compile with the default
warnings:

| reader | what it reads below a root | a summary node's answer |
| --- | --- | --- |
| `check_program_references`, every body | the static objects its leaves name, a set | **the side record: its references** |
| the return flow (`check_program_return_facts`; `-Wreturn-type` is on by default) | each call's callee, joined up the tree | **the side record: its root's fact** |
| `check_program_branch_truth` (the return flow, the warnings' reach) | a branch's condition's truth, through `!`, casts and the operands of `&&` and `\|\|` | its value's: the window lists a logical operator decided by its second operand |
| the controls' fold (`check_control_fold_truth`) | the root's constant, and below it where it is one | its value's: a taken constant folds |
| `-Wswitch` | the selector's type before the switch promotes it | its operand's window type, kept after the references |
| the initializer planner (`check_initializers_run_events`) | the greatest node of the source's tokens' | the flip's: the summary node is its tokens' |
| the lowering and its rooms | everything | the flip's: the tape is spliced |
| the default-on warning passes | subtrees | none: a taken expression has none of their warnings, by its tests |

Only with an option or a construct a unit may have: the uninitialized scan
and `-Wunused-but-set-parameter` (`-Wall`, `-Wextra`),
`-Wfor-loop-analysis`, the target features' check, the symbol pragmas' walk,
the candidates' scan, `musttail`, and `-g`'s rows. Those are the piece after
the first compile's.

**What the count before the specification found**, on the expressions the
window took whole in the cloud container (the census's own table's rows):

| | `sqlite3.c` | Lua | musl | the compiler's own source |
| --- | ---: | ---: | ---: | ---: |
| expressions the window takes whole; their records | 49,845; 278,892 | 10,248; 70,873 | 40,444; 204,846 | 41,890; 238,831 |
| **the references the program's walk makes of them** | **14,506** | **4,920** | **9,396** | **13,365** |
| expressions with none; one; two; three or more | 36,878; 11,618; 1,204; 145 | 6,365; 3,044; 717; 122 | 32,311; 7,231; 750; 152 | 31,897; 7,532; 1,812; 649 |
| calls; through a value (no declared function's name) | 12,724; 252 | 4,022; 15 | 7,943; 65 | 9,637; 9 |
| expressions with a call of a function declared noreturn | 0 | 112 | 21 | 30 |
| conditions; with a known truth; of them by a constant value; by one operand of an `&&` or an `\|\|` alone | 11,905; 53; 53; 0 | 1,537; 0; 0; 0 | 7,206; 239; 237; 2 | 8,218; 1; 1; 0 |
| roots with a constant integer value and no effect; of them with a comma | 3,028; 0 | 550; 0 | 1,643; 0 | 5,631; 0 |

A reference is exactly a leaf with an object that is static or has a name's
symbol, evaluated or not: none missing and none extra on the four. The
return fact is the flow's own rule run over the window's records. A
condition's truth is its value's but for two of musl's.

**The references.** `check_program_references` takes a VALUE's leaf's object
where its value is a static object's or its declaration is static, whatever
is above the leaf (it leaves out a `sizeof`'s operand, whose token the front
leaves to the walk). On the window that is two bits of a record's flags'
byte, by its kind: a name's leaf with a symbol of its own, or a literal's
leaf with an object, a string's. The walk has other sources of a reference
(a compound literal's initializer, a statement expression's region, an
overloaded call's function, a constant string's builtin); none adds one to a
taken expression on the four inputs or in the probes. The sweep
(`node_rows_window_summary`) takes 64 records a step:

- **the mask**: the flags tested against a table of what each kind of record
  needs, one shuffle and one test;
- **the list**: the block's references' records' numbers, one compression,
  and after the sweep a gather writes each one's object, its record's value,
  eight a step;
- **the counts**: the references through each record of the block, in a
  register. A multiplication of the mask's bytes by `0x0101010101010101`
  gives each byte the sum of its quadword's bytes up to it; the quadwords'
  totals are summed up the register in three steps; and the two are added.
  The chunk's count before the block is added and the 64 counts stored.

An expression's references are then the list's between two of those counts:
the one through the record before its operand's first and the one through
its last. That is two loads where the expression's summary's record is
written, and no step of the sweep is an expression's own. The first form of
the sweep was not so: it asked each expression's two counts of each list by
its block's mask, an expression at a time, and was replaced before any
reading on the 9700X. So was a form that took the n-th end mark for the n-th
expression: a batch's expressions are in the walk's order, an `if`'s
condition after its body's, and not in the window's.

**The return fact.** The flow gives a call its own fact
(`check_program_return_call`): none where its callee, through `*` and `&`,
is the name of a function, a stop where that function is declared noreturn,
and an unknown for a call through a value or of a library's name the flow
leaves unknown. Facts go up the tree; an `&&`, an `||` or a `?:` keeps its
condition's, an unknown of either arm, and a stop where both arms of a `?:`
stop. On the window:

- **A plain call has none.** A function's declaration's line has two bits,
  made with the line: a plain function's (not noreturn, no such name), and a
  noreturn one's. The tests' sweep copies a name's leaf's line's byte beside
  the records' bytes, and the tests' loop, which has each record's first
  operand's place at hand, gives each block a mask of the calls whose callee
  is no plain function's leaf in the block or the block before.
- **The others are asked a record at a time** (`node_rows_summary_call`, by
  the callee's record's byte; C lists an expression that calls `*f` or `&f`
  of a function's name, so the window asks no such call): 255, 127 and 99 of
  the three corpora's 321,434, 91,252 and 377,187 records swept, one record
  in 1,261, 719 and 3,810.
- **An expression's fact is its calls' together**, since an unknown goes up
  through every operator and a stop through every one that selects no
  operand. Where a call stops and the expression has an `&&`, an `||` or a
  `?:`, the flow's own reduction runs over its records
  (`node_rows_summary_reduce`): no expression of the three corpora and 18 of
  the compiler's own source.
- **A unit with a function unknown from a declaration on** (an
  `analyzer_noreturn`, an ignored `noreturn`:
  `check_program_return_unknowns`' table, made at the reader's start) has no
  plain call: each is asked, with its use's place.

**A condition's truth and a constant's fold** need nothing in the side
record. The original side-facts measurement listed two cases where the
walk and the root's value could disagree:

- **An `&&` or an `||` whose second operand's truth is known and decides**
  (`x && 0`, `x || 1`; one whose first operand's is known is C's to list
  already). The turns list an `&&` or an `||` whose second operand's truth
  is known, as branches or as a value: its standing's constant's kind, but
  an object's lvalue's. That leaves out 3, 2, 3 and 0 expressions the piece
  before took.
- **A cast in the original side-facts measurement.** The walk gave a
  cast its operand's truth where the cast's value has none. An address
  constant is not null; `(char)(long)&x` is no constant, and its low byte
  may be zero ([#3204](https://github.com/buster14a/buster/issues/3204):
  clang keeps both edges, and the old walk did not). C's loop listed the
  expression of a cast that is no constant, over an operand whose truth may
  be known, where its consumer may ask its truth: a CONDITION, a `!`, an
  `&&` or an `||`, or another wrapper (`passes_truth`). A `!` and a
  CONDITION are a constant wherever their operand's truth is known
  (`check_action_logic`), so only a cast passes one. On the four inputs it
  listed no expression: the specification did not have this case, and a
  one-statement program found it (below).
  With #3204 a cast of unknown value can propagate an inferred logical
  result. A symbolic address can no longer decide a branch when the
  converted value is unknown. Numeric
  conversions retain their existing treatment, including under
  `FENV_ACCESS`. The type machine already lists that context, so the
  window no longer needs the separate cast exclusion.

The controls' fold does not fold a comma whose left operand is no constant,
and C lists a folded comma: every taken root with an integer constant folds
as a leaf of its value would.

**A switch's selector type.** The original run left this side fact open on
70 SQLite, 85 Lua, 115 musl and 27 compiler-source expressions. The warning
then read the first token's node. After [#3209](https://github.com/buster14a/buster/pull/3209),
it reads the operand of the switch's promotion and unwraps implicit conversions.
The window already has the operand's type. It keeps one extra word after the
references of each switch whose tape it takes. The front only creates an implicit
conversion at a return, so a taken switch's operand is already the value the
warning inspects. The shadow compares its full type identity with the warning
pass's graph walk, including qualifiers and the distinction between two enum
types. A fixture covers arithmetic promotions and casts alongside enum names,
members, subscripts and calls. The fault control changes each saved selector
type and the comparison counts it.

**What the corpora did not have.** The four inputs, the fixtures and the
piece's own one-statement programs read no difference when the code was
first frozen. Two things run after that found two in it, and three in the
earlier pieces' code:

- **The earlier pieces' one-statement programs**, run again with the facts'
  comparison (6,414 programs; 7,957 with this piece's 1,543): one of them,
  `if ((char)(long)&x)`, has a truth the walk knows and its value has not.
  The cast's term above is its fix.
- **A generator of random one-statement programs** through the shadow, whose
  exit status is the oracle (integer, pointer and floating operands of each
  operator, weighted toward constants, addresses and casts): 18,000 programs
  on the first frozen code gave 82 that differ, 68 by that cast and 14 by an
  entry's value on the window, an earlier piece's: `(_Bool)&x` is an
  integer's constant, 1, whose record still has x for its object, and the
  value's column took a record's object wherever it had one. It takes it
  where the record's constant is an address's now. The repository's own
  regression programs have that statement (`tests/checker/sources/0013.c`),
  and no corpus has.
- **The earlier pieces' three:** with a share of operands of the wrong kind
  the generator finds what the reader-time tests take with a warning (an
  ordered comparison of a pointer with an integer, a cast of an array or a
  function to a narrower integer:
  [#3201](https://github.com/buster14a/buster/issues/3201)), and the
  regression programs a `return` under a pending cleanup
  ([#3202](https://github.com/buster14a/buster/issues/3202)) and a
  multiversioned function's object
  ([#3203](https://github.com/buster14a/buster/issues/3203)). Those are
  filed and not this piece's; each is the first compile's to have closed.

On the code here the generator read 60,000 programs with operands of the
right kind and 30,000 with a share of the wrong kind: 1 and 211 differ,
every one a take of the reader-time tests' with one of #3201's two warnings
(the one of the right kind by a cast the generator wrote without its
parentheses). Of the 1,229 regression programs 408 run through the shadow
with no option, and 9 end with status 1, #3202's 2 and #3203's 7; on main 13
do, the 4 more by that constant's value.

**How it came to its cost.** Builds on the 9700X, CPU 2, Release, one
executable with the facts and without them in each reading
(`--no-window-summaries`; TSC ticks, the timed region's own two timer reads
taken off):

| build | the sweep, a record swept, a chunk's second run | a first run less a second, ticks a chunk | the tests' loop, more | the rows' sweeps with the facts, a second run, more | readings |
| --- | ---: | ---: | ---: | ---: | ---: |
| the masks, the lists and the counts' column; the references' mask by two compares of the kinds and two tests under a mask of the chunk's records; both lists compressed in every block; the reduction for every expression with a call that stops; the tests' loop's mask of plain callees from three columns (the operators, the standings, the lines' bytes) | 0.33, 0.49, 0.49 | 75, 217, 190 | +0.16, +0.16, +0.16 | +0.33, +0.54, +0.60 | 3 |
| the references' mask by a table's shuffle and one test, with no mask of the chunk's records (the tests' sweep writes a block's bytes whole); the calls' list only in a block that has one; the counts' stores aligned; the reduction only where the expression has an `&&`, an `||` or a `?:` | 0.26, 0.35, 0.34 | 109, 192, 219 | +0.09, +0.14, +0.47 | +0.29, +0.40, +0.43 | 5 |
| a line's byte's two bits a plain function's and a noreturn one's (they were a noreturn one's and an unknown name's): the tests' loop's test is one AND of the lines' bytes, with no standing read | 0.28, 0.35, 0.35 | 99, 184, 193 | +0.05, +0.00, +0.07 | +0.30, +0.44, +0.44 | 7 |
| the code as first frozen: the terms the first three fault rounds took out | 0.29, 0.36, 0.37 | 94, 179, 212 | +0.05, +0.08, -0.15 | +0.34, +0.46, +0.41 | 7 |
| a cast's term in C's loop and the value's column's choice, both in a run with the facts and without them alike | 0.27, 0.36, 0.39 | 86, 184, 196 | +0.14, +0.09, +0.05 | +0.31, +0.43, +0.48 | 7 |
| **the code here**: the objects by a gather of eight where a loop wrote them one a step | 0.27, 0.31, 0.31 | 68, 185, 167 | +0.12, +0.06, +0.67 | +0.34, +0.39, +0.36 | 7 |

(`sqlite3.c`, Lua and musl in each cell.)

**Measured** (the 9700X, the code here, 7 readings, each the facts on, off
twice and on again; a median and its least and most):

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| records swept; chunks; records a chunk | 321,434; 611; 526 | 91,252; 241; 379 | 377,187; 2,975; 127 |
| expressions whose facts are kept; their references; the program's (a set an expression) | 49,839; 14,634; 14,502 | 10,246; 4,959; 4,918 | 40,441; 10,247; 9,391 |
| of them with an unknown for a return fact; with a stop | 250; 0 | 15; 112 | 64; 21 |
| records listed for a reference; calls asked a record at a time; expressions reduced | 14,911; 255; 0 | 5,128; 127; 0 | 11,522; 99; 0 |
| **the sweep, a record swept, a chunk's second run** | **0.27 (0.27 to 0.28)** | **0.31 (0.31 to 0.32)** | **0.31 (0.31 to 1.19)** |
| the sweep, a record swept, a first run | 0.41 (0.39 to 0.43) | 0.80 (0.78 to 0.81) | 1.70 (1.57 to 2.28) |
| the tests' records' bytes, a record swept, more with the facts (their one run) | -0.01 (-0.12 to +0.06) | -0.01 (-0.24 to +0.98) | -0.81 (-6.69 to +3.09) |
| the tests' loop, more (its one run) | +0.12 (-0.07 to +0.32) | +0.06 (+0.01 to +1.22) | +0.67 (-1.20 to +4.47) |
| **the three together, the sweep by its second run** | **+0.40 (+0.17 to +0.47)** | **+0.32 (+0.18 to +2.52)** | **+1.01 (-5.70 to +5.54)** |
| the three together, the sweep by its first run | +0.55 (+0.29 to +0.61) | +0.81 (+0.68 to +3.00) | +2.33 (-3.74 to +7.00) |
| **the sweep, a first run less a second, ticks a chunk** | **68 (63 to 80)** | **185 (178 to 186)** | **167 (86 to 249)** |
| the sweep, ticks a chunk: a first run; a second | 215 (207 to 226); 144 (143 to 147) | 304 (295 to 306); 119 (116 to 122) | 216 (199 to 290); 40 (39 to 151) |
| the tests' bytes and loop, ticks a chunk, more | +66 (-60 to +104) | +2 (-46 to +834) | +89 (-763 to +663) |
| **the rows' sweeps, the facts' among them, a record swept, a second run, more** | **+0.34 (+0.31 to +0.38)** | **+0.39 (-0.18 to +0.40)** | **+0.36 (-0.39 to +1.14)** |
| **the rows' sweeps, the facts' among them, a first run less a second, ticks a chunk, more** | **+23 (-44 to +64)** | **+115 (-11 to +372)** | **+232 (-1 to +248)** |
| the rows' other sweeps, a second run, more | +0.06 (+0.03 to +0.10) | +0.07 (-0.49 to +0.07) | +0.03 (-0.70 to +0.26) |
| of them the turns' lists and loops, where the listing is | +0.01 (-0.01 to +0.02) | +0.00 (-0.02 to +0.02) | -0.01 (-0.03 to +0.00) |
| the rows' other sweeps, a first run less a second, ticks a chunk, more | -47 (-117 to +0) | -70 (-189 to +188) | +60 (-156 to +81) |
| C's sweeps, a second run, more; a first run less a second, ticks a chunk, more | +0.01 (-0.03 to +0.06); +20 (-86 to +111) | +0.01 (-0.43 to +0.05); +7 (-37 to +61) | -0.06 (-1.40 to +0.28); +54 (-185 to +229) |
| the kernel in place, more | +0.01 (-0.05 to +0.14) | +0.01 (-0.41 to +0.40) | +0.01 (-0.78 to +0.10) |
| the tests' other parts, more | -0.02 (-0.27 to +0.20) | -0.43 (-2.81 to +0.71) | +2.14 (-1.49 to +2.94) |
| the rows' sweeps without the facts, a record swept: a first run; a second | 8.82 (8.58 to 9.11); 6.27 (6.25 to 6.30) | 12.42 (12.20 to 13.06); 6.71 (6.65 to 7.23) | 24.27 (23.53 to 25.94); 8.14 (8.03 to 8.77) |
| the timed region with no sweep in it, ticks a chunk | 27.0 (26.6 to 30.1) | 26.9 (25.6 to 28.6) | 27.4 (27.1 to 27.8) |

A second set, 8 readings of the same executable (the repeat, below), reads
the sweep at 0.28, 0.32 and 0.31 a record on a second run, the rows' sweeps
with the facts +0.34, +0.39 and +0.34 more, and the tests' other parts
+0.04, -0.13 and -0.14.

**Against its gates**

- **F1, exact: met for what the piece makes, with two differences from the
  gate's letter.** On `sqlite3.c`, Lua, musl and the compiler's own source,
  in the cloud container, every kept expression's references and return fact
  are the program's own walks' (49,842, 10,246, 40,441 and 41,890
  expressions, 14,506, 4,918, 9,391 and 13,365 references, 250, 15, 64 and
  15 facts with an unknown and 0, 112, 21 and 30 with a stop), every kept
  branch's condition's truth is its value's (11,902, 1,536, 7,204 and 8,218
  asked, 53, 0, 237 and 1 known) and every kept root with an integer
  constant folds (3,028, 550, 1,643 and 5,631); the same with `-Wall
  -Wextra` (49,842, 10,246, 40,441 and 41,890 expressions). The 7,957
  one-statement programs agree: 1,543 written for this piece (a reference of
  each kind in each place, a call of each form, a stop in each place an
  operator puts it, conditions an operand or a cast decides, the walks'
  other sources of a reference, a `constexpr` object, and statements at each
  lane of a block) and the 6,414 of the pieces before. The differences: a
  selector's type was left out of that measurement (its follow-up is above); and the control changes what reader
  time kept, not the program's answers. With it each kept expression's facts
  are kept with one changed (a reference more, a reference's object, its
  fact's stop and its unknown in turn), and the comparison must count every
  one by its turn; one of a chunk's first run's facts is changed, and every
  chunk's second run must differ; and the truths and the folds are asked of
  every root reader time typed, where they must count the fixture's lines
  the window lists for them.
- **F2, how much: met.** The turns' listing leaves out 3, 2, 3 and 0
  expressions the piece before took (5 a corpus asked), and the cast's term
  lists no expression there.
- **F3, work a record: met for the tests' sweep; the rows' sweeps read +0.34
  on `sqlite3.c` and +0.39 on Lua where 0.30 was asked.** A chunk's second
  run of the rows' sweeps, the facts' among them, reads +0.34 (+0.31 to
  +0.38) ticks a record swept more with the facts on `sqlite3.c` and +0.39
  (-0.18 to +0.40) on Lua (0.30 asked, 0.60 refutes), and +0.34 and +0.39 in
  the repeat; the sweep itself is 0.27 and 0.31 of it. The tests' sweep that
  makes the records' bytes reads -0.01 (-0.12 to +0.06) and -0.01 (-0.24 to
  +0.98) more (0.20 asked), +0.05 and -0.15 in the repeat. The tests' sweeps
  have one run a chunk: theirs is a first run's reading, and the gate's
  "second run" is the rows' alone. On musl, which the gate did not name,
  +0.36 (-0.39 to +1.14) and -0.81 (-6.69 to +3.09).
- **F4, a chunk: refuted on musl for the rows' sweeps, not met on Lua, met
  on `sqlite3.c`; and the tests' sweep reads by the build.** The rows'
  sweeps' first run less their second rose by +23 (-44 to +64) ticks a chunk
  on `sqlite3.c`, +115 (-11 to +372) on Lua and +232 (-1 to +248) on musl
  (100 asked, 200 refutes), and by +46, +144 and +259 in the repeat: musl's
  is past what refutes in both sets. The sweep is a function of its own and
  pays a chunk's cold start of its own, 68, 185 and 167 ticks a chunk (70,
  192 and 208 in the repeat), and musl's chunks are 127 records. The two
  frozen builds before read +62, +109 and +157 and +19, +102 and +165: over
  the gate on Lua and musl in each. **The tests' sweep reads by the build.**
  Its bytes and loop read +66 (-60 to +104) ticks a chunk more with the
  facts on `sqlite3.c` here and +87 (+21 to +142) in the repeat (50 asked,
  100 refutes); the two frozen builds before read +41 and +38, then +181 and
  +140, in two sets each. The first and the second of the three builds
  differ by one mask's operation in the bytes' sweep, and the second and
  this one by nothing in either sweep. With the facts and without them each
  of the tests' sweeps is another copy of its code
  (`node_tests_window_bytes_by`'s and `node_tests_window_loop_by`'s forms),
  so the switch reads one copy's place in the executable against the
  other's, which is the build's: by the gate's letter it is met in one
  build, over in one and refuted in one. What the facts add to the two is a
  narrowing and a store a step of sixteen records, and a load, an AND, a
  permute, two tests and a quadword's store a step of sixty-four. On Lua +2
  (-46 to +834) and -33, on musl +89 (-763 to +663) and -274.
- **F5, nothing else moves: met for the kernel and C's sweeps; the tests'
  other parts are within it on `sqlite3.c` and not told apart on Lua and
  musl.** By the switch, the kernel in place reads +0.01 (-0.05 to +0.14),
  +0.01 (-0.41 to +0.40) and +0.01 (-0.78 to +0.10) more and C's sweeps'
  second run +0.01 (-0.03 to +0.06), +0.01 (-0.43 to +0.05) and -0.06 (-1.40
  to +0.28) (0.2 asked). The tests' other parts read -0.02 (-0.27 to +0.20),
  -0.43 (-2.81 to +0.71) and +2.14 (-1.49 to +2.94), and +0.04, -0.13 and
  -0.14 in the repeat: one run a chunk, with ranges ten times the gate on
  the two corpora of small chunks. Five earlier builds' medians of the same
  parts on musl are +0.91, -0.36, -0.94, +0.62 and +1.18.
- **F6, faults: met, with one fault no test separates whose term stays.**
  Each term of the piece changed alone, a build and the node machine's tests
  a fault: the turns' listing, a call's own fact, the selection's test, the
  reduction, the sweep's masks, counts and lists, the gather, the lines'
  column and the loop's mask, the unit's table, the comparison after the
  pipeline and a chunk's second run's, the cast's term and the value's
  column's choice. A round is run from the list's two ends at once, the
  9700X and the cloud container, and the two agree on every fault both ran.
  - The first round's 126 faults, on the third build's code, left 12, and 12
    that did not build (a name left unused), worded again for the second.
    Five of the survivors were terms the window's own rules imply, and are
    out of the code: the listing's test for a `!` above the operator; the
    call's walk through `*` and through `&`, whose expression C lists; the
    test that a call is inside the expression its end mark names; and the
    reduction's test that a record has a consumer. Two were a table's
    entries no record reaches, and the table is simpler. Three found a line
    of the fixture or a turn of the control: an arm that stops beside one
    that is unknown, the reduction's bytes of the chunk before, and a second
    run's objects. Two change nothing a program reaches.
  - The second round's 26, every one whose text changed and the survivors:
    22 caught, 1 that did not build and 3 that survived, one of them a gap
    of the control's (the reference more it kept could be the expression's
    own object's), closed for the third round, whose 13 are all caught.
  - After the probes' finds, the 17 faults of the cast's term and the
    value's column: 15 caught, and 2 whose terms are out of the code: the
    count's mask of the placed records, and the value's column's "or no
    constant at all" (the reader makes every object's leaf with an address's
    constant).
  - **The whole list on the code here, 142 faults: 141 caught and one that
    changes nothing a program reaches**, the reduction started at its
    operand's second record: an operand's first record is a leaf, which has
    no fact of its own and joins none to its consumer's. Both machines ran 6
    of them, with one verdict each.

**What stays a record at a time, and why**

The objects were a loop, and are a gather. Until this build the code wrote
each reference's object by a load and a store, one a step (14,911, 5,128 and
11,522 records listed, one in 22, 18 and 33 of those swept). An experiment
build (the build before's code with a switch read from the environment, not
for a commit; 7 readings, each setting in turn in one executable) read its
two vector's forms against it:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| records listed for a reference; a chunk | 14,911; 24.4 | 5,128; 21.3 | 11,522; 3.9 |
| the sweep with the loop, a record swept, a chunk's second run (a region's two timer reads in it) | 0.33 (0.32 to 0.34) | 0.44 (0.44 to 0.45) | 0.68 (0.68 to 0.73) |
| the same executable and setting twice in a reading, the second less the first | +0.01 (-0.01 to +0.02) | -0.00 (-0.01 to +0.02) | -0.01 (-0.09 to +0.01) |
| **with a gather of eight quadwords, more** | **+0.00 (-0.00 to +0.09)** | **-0.07 (-0.08 to -0.05)** | **-0.14 (-0.19 to -0.13)** |
| with a gather of sixteen doublewords, more | +0.06 (+0.04 to +0.08) | -0.06 (-0.07 to -0.05) | -0.15 (-0.20 to -0.14) |
| a first run: with the loop | 0.64 (0.59 to 0.65) | 1.29 (1.27 to 1.32) | 2.71 (2.35 to 3.60) |
| **a first run: with a gather of eight, more** | **-0.04 (-0.07 to +0.02)** | **-0.06 (-0.10 to +0.01)** | **-0.40 (-0.84 to -0.27)** |
| a first run: with a gather of sixteen, more | -0.01 (-0.07 to +0.02) | -0.11 (-0.14 to +0.86) | -0.60 (-1.13 to -0.49) |

A gather of eight is the code: it reads the same on `sqlite3.c` and less
where a chunk has few references, which end the loop on a branch their count
mispredicts. A gather of sixteen doublewords reads more on `sqlite3.c`. An
earlier experiment read the sweep with no objects written at all -0.09
(-0.35 to -0.09) ticks a record swept on `sqlite3.c` and -0.11 (-0.12 to
-0.11) on Lua: the loop was a third of the sweep.

What stays a record at a time:

- **A call asked** (`node_rows_summary_call`, a byte of its callee's and, in
  a unit with a function unknown from a declaration on, a table's entry):
  255, 127 and 99 calls, one record in 1,261, 719 and 3,810 of those swept.
  With none asked that experiment reads -0.02 (-0.29 to -0.01) and -0.04
  (-0.05 to -0.03). A call that says a fact (an unknown in 250, 15 and 64
  expressions, a stop in 0, 112 and 21) then scans to its expression's end
  mark, some records on.
- **The reduction** (`node_rows_summary_reduce`), up one expression's
  postorder: no expression of the three corpora and 18 of the compiler's own
  source, those with a call that stops under an `&&`, an `||` or a `?:`. It
  is the flow's own rule in its own order, for expressions no reading here
  has enough of to time.

**What it means**

- **The side facts a compile with the default warnings reads are exact on
  the window**: an expression's references and its root's return fact as a
  side record, and a condition's truth and a constant's fold by its root's
  value, with two listings. A `switch`'s condition stays the walk's.
- **Their work a record is a third of a tick, and over its gate:** the sweep
  reads 0.27, 0.31 and 0.31 ticks a record swept on a chunk's second run,
  4%, 5% and 4% of what the rows' sweeps read without it (6.27, 6.71 and
  8.14). What the consumer adds, two loads an expression where its summary's
  record is written, is the first compile's to read.
- **By the chunk it pays a cold start of its own, and on musl that is past
  what its gate allows:** the sweep's first run reads 0.41, 0.80 and 1.70 a
  record swept, and its first run less its second 68, 185 and 167 ticks a
  chunk, beside the 1342, 2163 and 2044 the rows' other sweeps pay there
  already ([#3196](https://github.com/buster14a/buster/issues/3196)). In the
  shadow the reference's sweeps run between a chunk's and the next's; a
  compile that takes expressions runs none of them there, and is where a
  chunk's first run is to be read.
- **Four inputs and a fixture are not what "exact" asks.** Five differences
  were in none of them: a truth through a cast and a constant of an object's
  record here, and in earlier pieces' code what the reader-time tests take
  with a warning, a `return` under a cleanup and a multiversioned function's
  object. A generator of programs and the repository's own regression
  programs found them in minutes. Both are in the record, and a piece's
  freeze runs them from here on.

**What is next:** the earlier pieces' three holes
([#3201](https://github.com/buster14a/buster/issues/3201),
[#3202](https://github.com/buster14a/buster/issues/3202),
[#3203](https://github.com/buster14a/buster/issues/3203)), which a compile
that takes expressions cannot have, with a trigger of every warning the
tests stand for run through the shadow; then that compile, behind an option,
where the sweeps' cost by the chunk is read.

### What the compiler's own tests had and no corpus

The pieces before were gated on four inputs: `sqlite3.c`, Lua, musl and the
compiler's own source, with each piece's fixture and its one-statement
programs. All of it is code that warns of nothing and spells few attributes.
The side facts' freeze found five differences in three other inputs, and
three of them were the earlier pieces'
([#3201](https://github.com/buster14a/buster/issues/3201),
[#3202](https://github.com/buster14a/buster/issues/3202),
[#3203](https://github.com/buster14a/buster/issues/3203)). A compile that
takes expressions cannot have one. This piece looks for the rest where the
compiler's own tests keep them, and closes what it finds ([the tests'
specification](https://github.com/buster14a/buster/issues/3201#issuecomment-7058791784),
[the
objects'](https://github.com/buster14a/buster/issues/3203#issuecomment-7059489314);
[the guide](commands/lowering.md#the-tests-holes-and-the-objects-side-2850);
[the
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/tests-holes-2850-2026-11-21)).
No compile takes any of it.

**The input.** Every C string literal of `src/buster/compiler/*_test.c` that
is a program (13,866, of which the compiler accepts 6,620 with no option), a
file each, and every file under `tests/` (1,229; 408), through
`measure-node-machine --shadow --window-rows --passes 0`, whose status is
the oracle, under three warning sets: none, `-Wall -Wextra`, and
`-Weverything -Wno-c++-compat` (`-Wc++-compat` is a warning no test stands
for, which refuses every expression). The three sets are 1 minutes of four
cores with a Develop build. And one program for each warning the tests stand
for, written to warn (93 warnings: 60 written, 32 found in the programs
above, 1 with none: `-Wavailability`, which no Linux target raises).

**What it found** on the code before, main `e77b3eba4`: 150 programs with
status 1 under each of the three sets, in fifteen kinds (a program can have
several).

| what differs | programs | where | the issue |
| --- | ---: | --- | --- |
| a multiversioned function's object, which the program makes for itself; in 5 of them a call reader time takes and the program refuses, and its summary | 74 | T's objects and calls | [#3203](https://github.com/buster14a/buster/issues/3203) |
| a call in a function with x86's `interrupt` (`-Wexcessive-regsave`): **taken with a warning** | 26 | the tests | [#3201](https://github.com/buster14a/buster/issues/3201) |
| a `return` under a cleanup, whose value's root is no return's | 18 | the tests' root's kind | [#3202](https://github.com/buster14a/buster/issues/3202) |
| an asm label on a declaration after the object's definition, which the asm pass ignores after the bodies | 7 | T's objects | [#3207](https://github.com/buster14a/buster/issues/3207) |
| **a load or a store in a segment's address space, whose override the tape loses: wrong code if taken** | 6 | X's tape, after the pipeline too | [#3205](https://github.com/buster14a/buster/issues/3205) |
| a weak name's value, where another declaration or a `weakref` makes it weak | 5 | the kernel | [#3206](https://github.com/buster14a/buster/issues/3206) |
| a pointer's ordered comparison with an integer: **taken with a warning** | 4 | the tests | #3201 |
| a dereference of a `noderef` pointer through a typedef: **taken with a warning** | 4 | the tests | #3201 |
| a declarator's warning among its initializer's tokens | 3 | the tests' span | #3201 |
| a call of a function whose middle redeclaration has `diagnose_if`: **taken with a warning** | 1 | the tests | #3201 |
| an initializer whose cast's type has braces, read as a brace initializer | 1 | the tests' root's kind | #3202 |
| a label a rename pragma makes when the reader is done | 1 | T's objects | #3207 |
| the returned-address test on a sum of more than 64 operands, refused after the pipeline and taken on the window | 1 | the tests | #3201 |

The generator's programs with operands of the wrong kind had found the
pointer's cast and comparison and a function designator's (212 of 90,000
programs on the code before).

**Where a test goes.** Each unsound take was a warning that some pass raises
after the graph's run, by a rule of its own that no test of the census's
mirrored. A test's verdict is the census's code's, at both times
(`node_census_node`, `node_census_root_tests`). The machine's own form and
the window's loop only list the records sent to that code, by an operator's
class and its operands' bytes, and a type's byte is made once a type and
read by all three. So a new test is a rule in one place, a bit in the tables
for what sends a record to it, and, where the loop can say more from the
bytes it has, a mask that leaves a record out.

- **A pointer's ordered comparison with an integer**
  (`check_warning_ordered_event`) is a test of its own: exactly one operand
  a pointer as the action converts it, the other an integer that is no null
  pointer constant. The tables send `<`, `<=`, `>` and `>=` for an operand
  that converts to a pointer. That would list every loop's `p < end`, so the
  loop leaves out a comparison whose two operands both convert to one, where
  nothing else sends it and its operator has no test whatever its operands:
  76, 36, 47 and 32 comparisons of the four corpora, each checked (0, 0, 0
  and 0 with `-Wall -Wextra`, where the sign comparison's test keeps every
  ordered comparison listed).
- **What converts to a pointer** is a pointer, an array or a function. A
  function's type's byte has the pointer's bit and the void pointee's, as
  `nullptr_t`'s has: `(int)function` and `(char)array` are sent to the
  cast's test, and `*function` no more reads as a pointer's dereference with
  nothing to ask. musl's expressions with nothing listed fall by 8 (28,847
  to 28,839): calls through `(*f)`.
- **A list of conditions** is a declaration's own or one it inherits from an
  earlier redeclaration (`CheckInput.condition_declarations`), which no
  attribute of the declaration at hand says: such a declaration is a callee
  to ask.
- **Two attributes are a unit's.** x86's `interrupt` on any declaration
  makes every other call in such a body warn, and `noderef` warns by a
  declaration's type's annotation, which a typedef, a member or a cast may
  carry and no node's type says. A unit with either has every expression
  refused, as one with a warning no test stands for has.
- **An initializer's tokens** begin after its object's name after the
  pipeline ([#3017](https://github.com/buster14a/buster/issues/3017)) and
  began at its value at reader time: a declarator's warning that the reader
  has raised by then refuses it at both times now. One the reader raises
  after the body, that it ignores an automatic object's asm label, is not
  there yet when the walk reads the initializer: the declaration itself
  refuses its initializer (in a body, with no linkage, neither `static` nor
  `register`; musl's system calls' `register long r10 __asm__("r10") = a4;`
  are taken as before).
- **A `return` under a cleanup** has another control's root after the
  pipeline, where the program computes the value, runs the cleanups and then
  returns. Reader time says the same by a search of the unit's cleanups,
  which keep the furthest end of the scopes up to each.
- **A value in a segment's address space** is of a type the machine has no
  kind for, at both times and after the pipeline: its loads and stores have
  an override the rows have no entry for. Refused, not made: no corpus has
  one, and x86's segments are a kernel's and a runtime's few lines.

**The objects' side**: what T and the kernel read of an object that the
reader settles only after the bodies. The machine at reader time begins
before them.

- **Overloads.** T refuses every call of a unit whose graph has an
  overload's candidates, and a function multiversioned by target strings
  gets its calls' candidates after the last body. One answer both times have
  before the bodies: the candidates made so far (an overloadable function's
  declarations) or the unit declaring a function multiversioned so. The
  program's own object for a multiversioned function (by target strings, by
  `target_clones` or by `cpu_dispatch`), its versions' reference, is
  compared apart: reader time must have nothing for it.
- **An asm label** decides one answer of T's, whether a plain library
  builtin's call is computed in place or called by its label. The asm pass,
  after the bodies, ignores a label on a declaration that follows the
  object's definition; the machine applies that rule to the declarations in
  their order where it makes the objects' facts. A rename pragma's label is
  chosen when the reader is done, by the labels the asm pass kept and the
  uses the bodies made: in a unit with such a pragma T refuses every library
  builtin's call at both times and asks no label. No corpus has the pragma.
- **A weak name** is the name's from a token on, not a declaration's: the
  reader notes it at a `weak`, `weakref` or `weak_import` attribute on any
  declaration of the name. The kernel asked the use's own declaration for a
  `weak` attribute, and missed a function declared weak and then defined, a
  `weakref` and a block's weak declaration of a name another declaration
  has. The reader's list is in its output now, and the reader and the kernel
  ask one search of it, eight records a step. A declaration whose name is
  noted anywhere can change its line at another declaration's token, so its
  line is made again at every use; any other's last line is taken by one
  load and one test as before.

**Measured.** In the cloud container, a Develop build of the code here.

| | the code before | the code here |
| --- | ---: | ---: |
| the tables' programs with status 1: no option; `-Wall -Wextra`; `-Weverything -Wno-c++-compat` | 141; 141; 141 | 0; 0; 0 |
| `tests/`'s files with status 1, the same three | 9; 9; 9 | 0; 0; 0 |
| the generator's programs with status 1, of 90,000 | 212 | 0 |
| the one-statement programs that do not agree, of 8,009 | 33 | 0 |
| the warnings' triggers with a run that differs, of 92 (three runs each) | 2 | 0 |

How much is taken, on the four corpora (the same counts with `-Wall -Wextra`
but the tests', which refuse more there):

| | `sqlite3.c` | Lua | musl | the compiler's own source |
| --- | ---: | ---: | ---: | ---: |
| expressions T takes at reader time; before | 50,996; 50,996 | 10,839; 10,839 | 43,415; 43,415 | 52,163; 52,163 |
| expressions the tests take there; before | 50,158; 50,158 | 10,158; 10,158 | 40,770; 40,770 | 39,756; 39,756 |
| the same with `-Wall -Wextra`; before | 46,949; 46,949 | 9,640; 9,640 | 38,836; 38,836 | 34,398; 34,398 |
| expressions C decides on the window; before | 50,112; 50,112 | 10,715; 10,715 | 42,776; 42,776 | 45,074; 45,074 |
| tapes X makes whole; before | 49,842; 49,842 | 10,246; 10,246 | 40,441; 40,441 | 41,890; 41,890 |
| records the kernel makes; before | 247,906; 247,906 | 62,855; 62,855 | 191,502; 191,502 | 311,487; 311,487 |
| calls T refuses for their callee; before | 133; 133 | 50; 50 | 124; 124 | 890; 890 |
| pointers' ordered comparisons left out | 76 | 36 | 47 | 32 |
| the program's own objects; objects a pragma labelled | 0; 0 | 0; 0 | 0; 0 | 0; 0 |
| declarations' lines made again | 0 | 0 | 0 | 0 |

Nothing a corpus took is lost, and nothing more is taken: the holes were in
what the corpora do not have.

The completed 9700X measurement on the original change put the tests' loop
above H4's target on both gated inputs. Packing the operator classes, counting
the block's masks together, and using one converted-pointer bit brought the
loop under the target. The front now checks all three operand distances
against the same bound before reading a record. The measurements below are
medians of seven paired Release readings. Each number is the difference from
the compiler before the holes were fixed, in ticks per record.

| Shadow work | SQLite | Lua | musl |
| --- | ---: | ---: | ---: |
| Tests' loop and result asks, per record swept | −0.10403 | +0.12243 | +0.42228 |
| Front inputs made again, per input record | −2.63026 | −2.13058 | −0.57860 |
| Kernel in place, per record made | +0.02146 | +0.04716 | +0.05437 |

H4 allows +0.15 on SQLite and Lua. The musl tests' loop measurement is
informational. O3 allows +0.20 on all three inputs for both the repeated
front and the kernel. These are measurements of the compiler's shadow
command. The record includes each paired reading and both native loop
disassemblies. [Original readings](https://github.com/buster14a/buster-performance/tree/fac20eb7f9d56f2faf208ae114c1f278bc2af61d/docs/measurements/tests-holes-2850-2026-11-21/9700x/observed)
and [follow-up readings and checks](https://github.com/buster14a/buster-performance/tree/3fcddc9068499483cfb86adbbb53d73dcc19dc90/docs/measurements/window-cost-2850-2026-11-21/5c0cf0020).

**Against its gates** (H1 to H6 the tests' specification's, O1 to O5 the
objects')

- **H1 and O1, exact: met.** No program of the tables or of `tests/` has
  status 1 under any of the three sets (150 under each on the code before).
  The four corpora with no option and with `-Wall -Wextra`, 8 runs: no
  difference and no unsound take. The generator's 90,000 programs, six runs:
  none with status 1 (212 on the code before). The 8,009 one-statement
  programs agree: the 7,957 of the pieces before and 52 units written for
  the objects' side, 33 of which did not agree on the code before.
- **H6, a trigger a warning: met.** Of the 93 warnings the tests stand for,
  60 have a program written to warn and 32 one found in the tables; each
  warns, and none of its three runs (its own options, with `-Wall -Wextra`,
  and with `-Weverything -Wno-c++-compat`) differs or takes an expression
  with a warning. On the code before 2 warnings' runs did: the ordered
  comparison's and `-Wexcessive-regsave`'s. `-Wavailability` has no trigger.
- **H3 and O2, how much: met.** The four corpora take what they took, to the
  expression, with no option and with `-Wall -Wextra`: T, the tests, C, the
  rows' tapes and their entries, the kernel's records and the summaries (the
  table above). No corpus has a rename pragma, and T's refusals for a callee
  are the code before's (133, 50, 124 and 890): no unit of theirs loses a
  call to the overloads' answer.
- **H4 and O3, work.** The follow-up meets both targets in the seven paired
  readings above.
- **H5 and O4, faults: met for the first two rounds; the third was cut
  short.** Each term of the two parts changed alone, a build and the node
  machine's tests a fault. The first round's 104 faults, from the list's two
  ends at once (the 9700X and the cloud container), left 30 (7 did not build
  as written). Each of them got a fixture's line, a fixture, a direct test
  of the shadow's own comparisons, or its term out of the code where what
  makes the records implies it. The second round's 117 left 11: a fixture's
  line each for two (a plain direct call beside a conditioned one, seen
  failing on its mutant; the cleanup's ask after the pipeline is out, the
  program's own control says it), and nine whose terms stay: the reference's
  own (the ordered comparison's "exactly one a pointer" and "the other an
  integer", an automatic object's label's "in a body", "with no linkage" and
  "an object"), a rename pragma the reader did not take (an error unit)
  twice, and three of work with the same answer. On the code here the 9700X
  ran 40 of the 116 before the hand-off: 35 caught and
  `ordered-any-other-operand`, `ordered-two-pointers-too`,
  `orders-any-operator`, `own-order-unasked` and `segment-unextended-asked`
  survived, each one of those nine.
- **O5, the fixtures: met.** Each kind has a line in a fixture, in a unit of
  its own where its answer is a unit's. Of the 13 new fixtures 10 have
  status 1 on the code before (20 of the holes' 47 annotated lines do not
  hold there, 7 of the segments' 11); the 3 others hold on both and pin what
  must stay as it was: nothing refused with every warning off, a labelled
  builtin's call taken beside a weak pragma, and an overloadable function's
  unit.

**What stays a record at a time, and why**

- **A record sent to the census's code** is asked there, one at a time, as
  before: the new test is a rule in that code. What the loop adds is a mask
  of six tests a block of 64 records, of bytes it has.
- **The cleanups' search** is a binary search a `return`, in the units that
  have a cleanup at all (none of the four corpora). The first form walked
  every record before the token, a unit's returns times its cleanups; each
  record keeps the furthest end up to it, made as the reader appends it.
- **The asm labels' rule** is a bit an object in the loop over the
  declarations that makes the objects' facts already, at the machine's
  begin: no pass more.
- **The weak names' search** is eight records a step, asked once a
  declaration's first use in a unit that notes a weak name, and again at
  every use of a declaration whose name is noted. The reader's own asks, an
  identifier each in such a unit, are the same search now, where they were a
  loop.

**What it means**

- **The tests at reader time take nothing the compiler's own tests' programs
  warn of**, by the rule that a test's verdict is one piece of code at both
  times and the faster forms only list for it.
- **What the machine reads of an object is what the reader has before the
  bodies**, or it refuses: a unit with a rename pragma has its library
  builtins' calls refused, and one that declares a function multiversioned
  by target strings its calls.
- **The inputs that say "exact" are five now**: the corpora, the fixtures,
  the one-statement programs, the generator's, and the compiler's own tests'
  programs with a trigger a warning. The last is the cheapest and found the
  most. In the shadow it is a campaign's sweep, in the record; with the
  first compile behind its option the tables' own drivers run through the
  machine, and it stands by itself.
- **A hole of another kind came with them:** the lowering computes a renamed
  library builtin in place where clang calls the new name, in a unit with no
  other asm label
  ([#3208](https://github.com/buster14a/buster/issues/3208)).

**What is next:** the walk's truth through a cast
([#3204](https://github.com/buster14a/buster/issues/3204)), then the first
compile that takes expressions, behind an option, where the sweeps' cost by
the chunk is read.

### What it says of step 2

"Expected gains" took the machine at 55 cycles a body token for a taken
expression (90 in its conservative case). Measured, it is 65 for its own
reading with #2857's front, 51 with the lean one, and 75 for T and the one
sweep in the tight loop, 91 with a body's expressions a run: 126 to 142 with
the lean front. With the front's coverage (97.76% of the nodes of the taken
expressions, which hold 96% of all) the same sum gives step 2 1.25 to 1.27
times on `-c sqlite3.c` (1.23 to 1.25 with #2857's front): above its gate of
1.2, under the 1.3 expected. T where it sits costs what the upper figure took,
and C and X where they sit cost what a body's expressions a run cost them, so
on `sqlite3.c` the sum is at its lower end, 1.25. With #2850's sweeps in
place of the shadow's, if they carry into the compiler, the reading is most of
the machine and the sum is 1.34 to 1.35: its step 0 ran them at 17 cycles a
node with the leaves and the lookups given, and [its step
1](https://github.com/buster14a/buster/issues/2850#issuecomment-6900508040)
measured those at about 5 cycles a leaf and 25 to 40 a lookup, 29 to 33 a node
in all, over its own line of 20 a lookup. Ten cycles a body token of the
machine move the sum by a hundredth, a point of coverage by a third of that.

The sum had no term for the set's and the silence tests, which decide what
the machine takes. "The tests where they sit" ran them at reader time as the
census writes them: about 115 cycles a body token on `sqlite3.c`, more than
the three sweeps, which would take 1.25 to about 1.13, under the gate. "The
tests in the machine's own form" made the same bits from T's records at 62:
1.19, a hair under it. "The tests on the window" made its loop 64 records a
step and its leaves' bits a byte a declaration's line: 65% of the own form's
ticks on `sqlite3.c`, which at the same share is about 40 cycles a body
token and 1.21. "The constants with no test to make" took it to 59%, about
36 cycles and the same 1.21. "The operator above, and an expression with
nothing listed" and "The calls with no test to make, and a statement that
discards nothing" took it to 51% and 43%: about 31 and 27 cycles,
and 1.22. "The casts, a pointer's subscripts and its sums" took it to 38%:
about 23 cycles, and 1.22 to 1.23. The sum stands only with the tests at a
third of the own form's.

"C on the window" is the first of the sweeps where the records are made: C's
answers for 63% of `sqlite3.c`'s placed expressions at 12.6 ticks a record of
theirs, where C on T's columns reads 40.2, in the shadow. The sum does not
move for it: X reads T's columns still, and no compile takes either.
What would move it:

- **The rest of the tests where their inputs are:** an initialization's
  root's, an assignment's of a constant, an array's subscript by a constant,
  and a comparison's signs under `-Wsign-compare`: 11 of the window's 31
  ticks a record are tests on T's records still, after a pointer's casts,
  subscripts and sums left the list. "The casts, a pointer's subscripts and
  its sums" counts what is listed still. The census's code is their oracle.
  They are the sum's largest open term.

- **The front's walk:** over a hundred instructions an acting token for a
  stack walk, lean or not. The structure it derives (counts, distances,
  places) is the tree's, which the parser's columns hold: "The front by
  passes" makes it from them without the stack, 1.1 to 1.5 times faster, and
  what is left there is the reader's columns' loads. Writing these bytes in
  the parse itself is #2850's step 3.
- **The sweeps on these records where they run:** #2850's step 2. T on the
  front's records as the shadow's scalar T would read them was this step's
  plan before #2850's step 0, and is dropped. "C on the window" is C's
  first piece there, "A call on the window" and "An update on the window" the
  next two. X's rows are on its answers since "X's rows on the window, first
  their shapes", with their values since "The entries' values on the window",
  and "An operator's value and a comma on the window" takes the logical
  operators' values, the conditionals and the commas in both, and "A summary
  node's side facts on the window" makes what the passes after the reader
  read below a root. No compile takes any of it: three holes of the earlier
  pieces' that a generator of programs found
  ([#3201](https://github.com/buster14a/buster/issues/3201),
  [#3202](https://github.com/buster14a/buster/issues/3202),
  [#3203](https://github.com/buster14a/buster/issues/3203)) and then the
  first compile that takes expressions are next.
- **What a compile gains, measured from the other side:** with each expression
  whose tape the window makes whole replaced by one leaf in the source, main's
  stages from `check-input` to `lower` fall from 87.6 ms to 56.7 on `sqlite3.c`,
  and to 76.3 where only the tapes with no call, label or temporary are
  replaced ([the one-core path](one-core-path.md#step-2s-ceiling-the-middle-without-its-taken-expressions)).
  Against the 10 ms the window's sweeps cost a compile, that is the sum above,
  1.23 times, and nothing for a first compile that takes no call.
- **Step 3's boundary, measured apart from the machine:** a body's lowered
  function supplied at the lowering with its tokens masked from the reader,
  the supplier a recording of an ordinary compile. Exact by default flags on
  four corpora, and `sqlite3.c` with 84% of its bodies' tokens supplied
  compiles in 64.9 ms where the ordinary compile takes 110.9: the host alone
  costs about 56 with every body free
  ([the one-core path](one-core-path.md#the-redesigns-first-piece-the-body-boundary)).
- **`sizeof` and `_Alignof`,** which the front leaves: 1.5% of `sqlite3.c`'s
  taken expressions and most of musl's missing 5% of nodes.
- **Where the sweeps run:** 164 cycles a node in the tight loop, 199 a body's
  expressions a run, and at reader time, a chunk's expressions a run, T alone
  2.2 times its tight loop on musl.
- **The selection before C and X:** 20,745 of the 57,888 expressions a reader
  types on musl (36%) are ones it takes in bodies that are never lowered, its
  headers' inline functions; 22 of `sqlite3.c`'s and 9 of Lua's are.
- **A declaration's discard:** 9%, 17% and 24% of the roots the front gives T
  (5,355, 2,275 and 13,991) are an initializer's value cast to void at the
  declaration's `;`, which nothing evaluates.

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
- [#2850](https://github.com/buster14a/buster/issues/2850): [the one-core
  path](one-core-path.md), whose step 1 makes T's identifier leaves and
  lookups from fact words in this shadow (`--shadow --facts`).
