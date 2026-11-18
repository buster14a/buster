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
| a constant its conversion changes, a division by zero, a shift's count, a null dereferenced, a zero that is no literal made a pointer | at an operator with a constant operand, exact in place | the constant's value in T's columns, the types' widths |
| a pointer cast to a narrow integer, a string plus an integer, a comma's discard | at the operator, exact in place | its operands' bytes |
| a comparison's bound, a mix of enumerations, signs compared, a void dereferenced, a shift of a sum, a chained comparison, a shift or a product read as a truth, a name assigned to itself, an atomic record's member, a subscript at its array's end or past it, a constant the columns don't hold whole | by the census's code, on that record alone | a condition each of them needs, from the operator's bytes and its operands' |
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
  adds is a record's work a turn.
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
| its arguments | have no aggregate among them, and no constant but an address's or an integer's of zero or 1 to 126 |
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
  evaluated records): the kernel's call rule makes those conversions and
  keeps none. A call is listed in this piece.
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
  2,267 for `&&`, `||` or `?:`. The kernel's call rule makes a call's
  arguments' conversions and keeps none: keeping them is the piece after
  this one.
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
  first piece there: a call, the updates and the logical operators are
  listed still, and X is not on its answers.
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
