# Implicit postorder syntax tree

[Frontend index](../frontend.md) · [Issue #3102](https://github.com/buster14a/buster/issues/3102)

`c_ast_build` (`src/buster/lib/compiler/frontend/c/c_ast.c`, contract in
`c_ast.h`) reads a unit's final expanded token stream once, front to back, and
appends a complete syntax tree as it goes. The tree covers declarations and
declarators, statements, expressions, initializers and designators,
attributes, assembly and the supported GNU/C23 forms, function bodies
included. **Status: pilot.** The default pipeline does not build it. The
opt-in [driver hook](#driver-pilot-hook) builds it, and two consumers read it:
- the [declaration split](#declaration-split-from-the-tree) publishes
  `c_parse_ast`'s top-level records from its declaration nodes;
- the [tree expression typer](#tree-expression-typer) answers function-body
  and file-scope initializer expression-type queries during semantic analysis.

Otherwise `c_analyze_semantics_only` and `c_lower_to_ir_with_options` still
rediscover syntax from token ranges, as described in the
[foundations guide](foundations.md). The consumer map below lists which
walkers this tree is meant to retire, and what has to be shown before any of
them move.

## Representation

The tree is stored in postorder. Each subtree is one contiguous interval of
node indices, a node's children come before it, and the node that roots a
subtree is the last one in its interval. Topology needs one field,
`extents[n]`: the number of nodes in n's subtree, n included. A node's
subtree begins at `n + 1 - extents[n]`. Its last child is `n - 1`, and each
earlier child starts just before the previous one, at
`child - extents[child]`. The tree stores no parent or sibling links.

Nodes are structure-of-arrays rows:

| Column | Bytes | Meaning |
|---|---|---|
| `kinds` | 1 | `CAstKind` |
| `extents` | 4 | subtree node count |
| `tokens` | 4 | provenance anchor: final-stream token index (operator, keyword, name or first token, per kind) |
| `data` | 4 | per-kind payload: interned symbol, `CAstWord`, presence bits and flags, or list word |

`C_AST_KIND_LIST` is the only authority for each kind's child contract. It
has five contract classes:

- **leaf:** no children.
- **fixed:** a set number of children, read by position.
- **range:** a minimum and maximum child count; when the trailing child is optional, its absence is the shorter count.
- **presence:** each optional child has a bit in the low byte of `data`, and children appear in source order.
- **list:** fixed leading children, a variadic middle and fixed trailing children. In the implicit layout, `data` holds the middle count.

`c_ast_validate` checks every contract, the extent tiling and the layout
slices.

Facts are produced once and stored with the node:
- Identifier-like nodes carry their interned symbol.
- Keyword specifiers carry a normalized `CAstWord`, so `__const__` and `const` are one word while the token keeps the spelling.
- A string-literal run is one node whose `data` holds the run length.
- Pointer and array qualifiers are flags.

Parentheses, commas and other punctuation get no nodes. A cast and a
compound literal differ by kind, so neither needs a separate marker.

## Construction

The builder is an explicit frame machine. A dispatch loop advances the top
frame, which may push child frames. No frame's step function calls another
step, so the C stack does not grow with nesting depth. Every open construct
records the append position where its subtree began. When the grammar commits
the construct, its node is appended with extent `append + 1 - begin`. Because
the parent is appended last, its kind is decided at that moment: a
declaration that turns out to be a function definition, or a parenthesis that
turns out to be a cast or compound literal, needs no rewrite. The builder
never reads an emitted node back.

Expressions use shunting-yard over an operator stack and an operand-begin
stack. Postfix operators apply immediately, prefix operators and casts are
right-associative, `?:` and the assignment family are right-associative, and
the middle operand of a conditional is a nested full-expression frame.
Declarators keep a stack of pointer prefixes per nesting level and emit them
after that level's suffixes, the `*` nearest the name first, so for any
declarator the chain from its name to the root reads in type-derivation
order.

The cursor reads at most `C_AST_LOOKAHEAD` tokens ahead. Two cases need the
full lookahead: `identifier :` at a statement start, and the decision after
`(` between a type name and an expression. With `CAstOptions.refill_batch` at
zero (the production setting) the window borrows the token array in place.
Any other value copies tokens through a ring that is refilled that many
tokens at a time. Tests build the same input at several batch sizes and
require identical trees, which checks that no grammar rule reads beyond the
window. That property is the precondition for feeding the builder from a
streaming preprocessor instead of a complete token array; see
[Preprocessor fusion](#preprocessor-fusion).

## Preprocessor fusion

The builder runs after `c_preprocess` has finished, over the complete final
token array. Fusion would interleave the two stages: the preprocessor hands
over final-stream tokens in batches as it produces them, and the builder
consumes each batch while it is still in cache. The tree would still refer to
the preprocessing result's tokens by index, so the array stays either way.
**Status: measured, not shipped** (audit
[`2026-10-09T222755Z`](../../performance-audits/2026-10-09T222755Z.md)).

The measured mechanism stays in the history of
[#3102](https://github.com/buster14a/buster/issues/3102)'s fusion branch at
`2eeb846e`, then reverted:
- **The preprocessor.** Its main loop became resumable: begin, run text lines
  until the output reaches a target, then finish. A record holds the state that
  outlives one line.
- **The builder.** Its cursor borrowed the output stream in place, since the
  stream's base never moves. Its refill ran the preprocessor for 2,048 tokens
  past the row it needed. The builder's dispatch loop was the one loop that
  advanced both stages.
- **Correctness.** The tree, its diagnostics and the preprocessing result were
  identical to the array path.
- **Measurement.** Fusion was free in instructions, and in cache simulation it
  removed the builder's last-level misses on the token array. On the host,
  preprocessing plus the parse phase did not get faster, and peak RSS grew,
  so it fails the ship budget below.

A later attempt has to handle what this one met:
- **The final-stream passes.** After the main loop, `c_preprocess` respells
  identifiers (C23 underscore aliases and UCN names), rewrites GNU obsolete
  designators and renames GNU `__label__` labels. Any of them can change rows
  the builder has already read; the pilot rebuilt from the array when one did.
- **Keywords.** A keyword the symbol table has not interned when the build
  begins needs its table entry before the first row that carries it.
- **The symbol table.** The builder must not intern into the preprocessor's
  table mid-stream, or every later id shifts.
- **Memory.** Both phase arenas are alive at once: about 12 MiB more peak RSS
  and 31 GiB more reserved address space, which Valgrind's cap cannot hold
  ([#3305](https://github.com/buster14a/buster/issues/3305)).
- **`__STDC_HOSTED__`.** Its replacement token lives on the stage driver's
  setup stack. That is safe only while setup and the line loop share one frame.

## Names that change the parse

The builder decides typedef names against ordinary identifiers when it reaches
each declaration point. It keeps one meaning byte per symbol (none, typedef,
ordinary) and an undo log with scope marks. A scope is opened for:
- the file;
- each prototype parameter list;
- each function body, which first re-declares the parameter names of the
  innermost function derivation and any K&R identifier list;
- each compound statement, `for` statement and statement-expression body.

A declarator's name becomes visible when its declarator completes (C17
6.2.1p7), so `int T = sizeof(T);` inside a typedef's scope already sees the
object. An enumerator becomes visible after its value. An identifier is a
typedef-name specifier only if no type specifier has appeared earlier in the
same specifier sequence. Labels, tags and members never bind.

The builder decides no types and binds no other identifier uses. Semantic
completion owns those, and will consume nodes instead of token ranges once it
is migrated; the [tree expression typer](#tree-expression-typer) is the first
part of it that reads nodes.

## Storage and lifetime

The builder writes each node once, where the published tree keeps it. Each of
the four columns grows in place in its own private arena (`CAstStorage`):
- The arena reserves address space for `C_AST_COLUMN_CAPACITY` nodes, which
  is 2^28. Across the four columns that is 13 bytes per node, or 3.25 GiB of
  address space, not memory.
- The arena commits `C_AST_COLUMN_STEP` (65,536) nodes at a time as the
  builder appends.

Nothing is staged or sealed. A tree larger than its reservations moves into
reservations of twice the capacity, once per doubling. That move is the only
copy a build can make, and `CAstStatistics.column_copy_bytes` counts it. The
unity self-host tree fills about 1% of the first reservation. The reservation
is not a bound on the tree, so the builder's limit stays `C_AST_NODE_LIMIT`.

Columns allocated once in the caller's arena at an upper bound would need no
arenas of their own, but no bound on nodes per final-stream token is proven.
Wrapper nodes share tokens: `int x;` is five nodes for three tokens, against
0.66 nodes per token on the self-host. A proven bound would need an audit of
every append site, would reserve several times the tree, and would leave gaps
between four columns sized that way in the caller's arena.

The tree owns its column arenas:
- `c_ast_release` retires them into the calling thread's reuse pool. Each
  keeps at most `C_PHASE_ARENA_RETAINED_SIZE` committed, as the preprocessor's
  private arenas do under `c_preprocess_release`.
- It must run on the thread that built the tree.
- It is idempotent through any copy, because copies share one storage record
  in the caller's arena.
- The driver releases the pilot tree at the end of the unit, before the
  preprocessing result.

The caller's arena holds the storage record, the diagnostics and the HYBRID
and EXPLICIT slices. Builder frames, stacks, bindings and the refill ring live
in the phase arena, which is released afterwards, or in a private one that is
retired. `CAstStatistics` reports:
- the retained bytes;
- the phase arena's high-water mark, which holds no node bytes;
- the bytes a move copied.

On the first syntax error, or a refused reservation, the build records one
structured diagnostic, releases its columns and publishes an empty tree, so a
partial tree is never handed on. The tree refers to the preprocessing result's
tokens and symbols by index, so that result must stay alive as long as the
tree does.

## Layouts

`CAstOptions.layout` selects a variant of the same tree:

- **Implicit:** extents only.
- **Hybrid:** in addition, each list node gets a child-root slice; its `data`
  becomes the slice offset, and the count is read from the slice.
- **Explicit:** the compact explicit-index control. Every interior node gets
  a slice through `CAst.slices`.

The forward pass always produces the implicit form. The hybrid and explicit
variants are built afterwards in one finalization pass, and that pass is
included in their measured cost. The accessors `c_ast_child_count`,
`c_ast_list_count`, `c_ast_child_at`, `c_ast_children` and `c_ast_walk_*`
return the same answers for every layout.

A consumer can traverse the tree in three ways:
1. A plain postorder scan over `kinds`. This works where operands are
   evaluated before their operator, as in constant folding and most
   expression typing.
2. `c_ast_walk_next` enter/exit events on an explicit stack. Context that must
   be set before a child is visited goes on the enter event: branch arms,
   short-circuit operands, unevaluated operands, scopes.
3. Direct child access by position.

## Syntax boundary

The builder enforces C's syntax. It rejects a typedef name used as an
expression operand: under C17 6.5.1, a primary-expression identifier must
designate an object or a function. The one exception is a
`__builtin_offsetof` member designator, which may share a typedef's spelling.
It also rejects asm operands not separated by commas. The following are
accepted as syntax and left to semantic checking, which already owns them in
today's pipeline:

- a label, `case` or `default` directly before `}`, and a declaration directly
  after a label, in every dialect (a constraint only before C23);
- GNU nested function definitions, which appear as a `FUNCTION_DEFINITION`
  item of a `COMPOUND_STATEMENT`;
- implicit `int` at file scope, when an identifier opens the declaration.

The tree accepts two attribute placements that need node slots of their own:

- A later declarator in a list may carry an attribute list before and after
  it (`int a, __attribute__((x)) b __attribute__((y));`). `INIT_DECLARATOR`
  and `MEMBER_DECLARATOR` each have a presence bit (bit 3) for the list
  written before the declarator, which is their first child; the trailing
  list keeps its own bit and follows the declarator.
- An attribute list may open a parenthesized declarator that is not a pointer
  (`int (__attribute__((x)) p);`, `int (__attribute__((x)) f)(void);`,
  `int (__attribute__((x)) a)[3];`, and the abstract forms in parameters and
  type names). The group's declarator is wrapped in a `DECLARATOR_ATTRIBUTED`
  with two children: the `ATTRIBUTE_LIST`, then everything inside the
  parentheses. When a `*` follows the list the attributes belong to that
  pointer and stay on `DECLARATOR_POINTER`, as before. The wrapper derives
  nothing, so it does not change which node is the first derivation above a
  name. A list with no declarator after it (`int (__attribute__((x)));`) is a
  syntax error.

## Driver pilot hook

`ide cc -fc-ast-pilot[=implicit|hybrid|explicit]` (see the
[driver guide](../driver.md)) is off by default. When given, the C compile path
calls `c_ast_build` after `c_preprocess` succeeds, inside the existing parse
phase boundary, so the build's time is part of `parse_ns` and of the `parse`
phase in `-fmetrics-out`. The bare flag is the implicit layout. The tree's
storage record lives in the unit's arena and its columns in their own arenas,
which the unit releases when it ends. The driver then takes the unit's
declaration records from `c_parse_ast_from_tree` in place of `c_parse_ast`
([declaration split](#declaration-split-from-the-tree)), and hands the tree to
semantic analysis in `CParserResult.ast`, where the
[tree expression typer](#tree-expression-typer) reads it; nothing else does.
With it the driver sets `CParserResult.type_interning`, which the typer's
answers for casts and `&` need ([interned rows](#interned-rows)). The object,
every diagnostic and every later stage are unchanged. The driver has no phase arena to lend (`c_preprocess` is not
given one either), so the builder creates and retires its own. A build that is
not complete fails the unit with the parse error class; its diagnostic is
published exactly as a `c_parse_ast` diagnostic is. `-E` and assembly inputs
never reach the hook.

Under `-v` the driver prints four rows with the other verbose counters:

- `C_AST nodes=<n> tokens=<parser tokens> build_ns=<c_ast_build wall time>
  retained_bytes=<> transient_high_water=<> column_copy_bytes=<>
  finalize_child_entries=<> layout=<name>`
- `C_AST_WALK walk_ns=<one full c_ast_walk over the root> walk_steps=<events>
  scan_ns=<one linear pass over the kinds column> children_ns=<c_ast_children
  over every node into a scratch buffer> child_entries=<sum of child counts>
  scan_calls=<CALL nodes the scan counted>`
- `C_AST_TYPES bodies=<function bodies typed> initializers=<file-scope
  initializers a query had typed> nodes_typed=<expression nodes the eager pass visited>
  nodes_accepted=<those it gave a type> answers=<type queries answered from
  the tree> declines=<queries that mapped to a node the typer does not vouch
  for> misses=<queries that mapped to no node> gated=<queries met in a machine
  state the typer leaves alone>`
- `C_AST_SPLIT units=<units whose records the tree split published>
  records=<records it published> assertions=<body _Static_assert ranges it
  published> fallbacks=<units it handed to c_parse_ast's walker>
  reason=<CParserTreeFallback of the last fallback> fallback_token=<the first
  token of the declaration that caused it>`

The second row's passes run only under `-v`; they are diagnostic. The third
and fourth rows count what the typer and the split did; they time nothing. Each feeds a
counter that is printed (`walk_steps`, `scan_calls`, `child_entries`), so the
compiler cannot drop the measured loop. Both rows use the driver's own clock
and are summed over the inputs of one invocation; the layout is the
invocation's. Like all hosted numbers they are diagnostic, not acceptance
evidence.

## Tree expression typer

`c_ast_types.c` answers semantic analysis's expression-type queries in function
bodies and in file-scope initializers from the tree, in place of the
speculative type machine (`CTypeParseMachine` in `c_parse.c`). Stage 1 covers
names, literals and postfix chains; stage 2 adds the operators; stage 3 adds
file-scope initializers, and, in bodies, `&` and casts to primitive and
pointer type names over rows the machine interns under the pilot. It runs
only when the caller supplies a tree in `CParserResult.ast`, which today only
the [driver hook](#driver-pilot-hook) does; without one, analysis is
unchanged.

- **When it types.** `c_parse_validate_lowering_constraints` indexes the
  tree's top-level function definitions and declarations once
  (`c_ast_types_bodies_prepare`). Before each body's validator families run,
  `c_ast_types_body_begin` makes one forward pass over the body's node
  interval. Children come before parents, so each expression node's operands
  are already typed when the node is reached. The pass records each node's
  token span and, for the accepted kinds, its type, whether it is safe under
  constraint checks, and the bit-field width of a member. A node is accepted
  only when every operand the machine types for it is accepted. Its arrays
  live in the machine's scratch arena above the body's validation mark and are
  released with the rest of the body's scratch.
- **File-scope initializers.** `c_parse_validate_static_initializers` made
  nearly all of the queries outside a typed body: 77,545 of 79,035 on the unity
  self-host, at about 190 M machine Ir, against at most 3 M for any other
  validator (audit `2026-10-09T213311Z`). Before it validates a file-scope
  object's initializer, `c_ast_types_initializer_begin` reserves the arrays
  for that initializer's subtree, over the initializer's tokens, and they are
  released afterwards. The subtree is typed the same way, but only when the
  first query that the literal fast path does not answer reaches it. Until
  then a lone literal keeps the literal path (`c_ast_types_waiting`). That
  matters: 61 of the self-host's 2,777 initializers are numeric tables that
  are only asked about lone literals, and they hold 551,736 of the 736,333
  expression nodes. Typing them all cost more than the machine runs it
  removed. The binder records no identifier use outside bodies. Where it recorded none, the machine resolves an identifier, and a
  cast's or compound literal's typedef name, by spelling in the query's scope,
  so the pass does the same lookup in the file scope. Each such node keeps the
  entity it found and carries the lookup mark, so the query repeats the lookup
  in its own scope (`c_ast_types_lookups_agree`). The kinds and rules are the
  bodies'. There is no memo at file scope, so an answer publishes only the
  machine state.
- **When it answers.** `c_parse_expression_type_query` reads the per-body memo
  first. On a miss it asks the typer, which maps the range to a node (after
  stripping balanced outer parentheses, as the machine does). It answers only
  when that node is accepted and the machine would take the same path;
  otherwise the literal fast path and the machine run follow, unchanged. A tree
  answer leaves the machine state and the memo entry exactly as the machine's
  valid, constraint-free answer would, so later machine runs read the same
  sub-range results and analysis ends with the same type tables. The typer
  never answers:
  - inside a running machine (a nested query);
  - in a constant-evaluation mode;
  - over a `_Generic` or `__builtin_types_compatible_p` site;
  - while an enumerator list is half parsed;
  - when, in the query's scope, a callee or a cast's typedef name somewhere in
    the subtree resolves to an entity other than the one the binder bound, or,
    in an initializer, an unbound name resolves to an entity other than the one
    the pass found (`c_ast_types_lookups_agree`).
- **What it accepts.** Each answer is a row that already exists: an entity's,
  member's, element's, return or typedef type, an operand's own row, an
  immutable scalar row, or an interned primitive or pointer row (see
  [Interned rows](#interned-rows)). The rules are the machine's own functions, shared
  through `c_internal.h` and applied to operand types the pass already holds:
  `c_parse_expression_arithmetic_type`, the promotion with a bit-field width,
  the operator precedence and the scalar conversion check.
  - Stage 1: identifiers bound to an object, function, parameter, local or
    enumerator; number and character literals; `.` and `->` on an unqualified
    struct or union; `[]` and unary `*` on an array or pointer; and a call
    whose callee is a bound function or function pointer that is not a builtin.
  - Stage 2:
    - the binary arithmetic, shift, comparison, bitwise and logical operators;
    - unary `+ - ~ !`;
    - assignment and every compound assignment;
    - comma;
    - `?:` and GNU `?:` with an omitted operand, over arithmetic, `void`,
      vector and same-row aggregate arms;
    - casts and compound literals whose type name is one typedef name;
    - `sizeof` and `_Alignof`.
  - Stage 3 (`c_ast_types_type_name`, `c_ast_types_cast`,
    `c_ast_types_address`):
    - `&`, whose answer is the interned pointer to the operand's row;
    - casts and compound literals whose type name is a typedef name or a run
      of primitive specifier words (with `const` and `volatile`), then plain
      `*`s. The answer is the typedef's row or the interned primitive row,
      under interned pointer rows. Each is accepted only once every row it
      reads is already interned, so its machine run appends nothing at any
      task level.
    - a call of an unbound void `__builtin_` with no arguments
      (`__builtin_debugtrap()`, `__builtin_unreachable()`), whose answer is
      the published `void` row (`c_ast_types_builtin_call`).
- **What it declines.** Every shape whose machine answer appends a row stays
  with the machine:
  - a qualified member or array element;
  - an array operand of `+` or `-`, which decays;
  - a pointer or `nullptr` conditional;
  - a qualified operand whose unqualified row was never recorded;
  - a cast or compound literal to any other type name: a qualified typedef,
    a tag, a qualified or `restrict` pointer, or an array or function
    declarator. Their readers append rows that are not interned.
  - `&` or a type name whose interned rows do not exist yet when the body is
    typed;
  - a string literal. Its array row is not interned, because lowering gives
    each array row its own IR array type and `-g` describes every IR type.
    A query on one costs the machine little: the unity self-host makes about
    6,650, against 2.19 million queries. A literal path that answered them
    before the machine saved about 1 million instructions there, and its
    check on every query cost more than that, so it was dropped.

  An operand the machine scans but does not type (a cast's operand without
  constraint checks, a `sizeof` expression) must therefore hold no type name
  the tree did not accept. The typer also declines a `?:` whose range holds a
  top-level comma or assignment, which the machine splits at instead.
- **The replay.** With constraint checks the machine types a cast's operand.
  When that operand is one string-literal token, alone or in parentheses
  (`(char8*)("text")`, as `S8()` spells it), its typing appends the literal's
  array row. The tree answers such a query anyway: the answer carries the
  token, and `c_parse_expression_tree_query` makes exactly the machine's
  operand task. That task strips the enclosing parentheses, probes the
  per-body memo for the token under the query's scope and flags and, on a
  miss, calls the machine's string leaf, which appends the same row. The node
  is never constraint-safe for its parents, whose machine runs would type the
  literal too. An operand of several literal tokens is not replayed.
- **Constraint checks.** A checked query is answered only from a node whose
  operands are safe and whose own checked-mode rule cannot fire. For the
  binary operators that is the machine's operand rule. The cases it settles by
  spelling or compatibility pass only for the literal `0` against a pointer and
  for two pointers to one unqualified element row. A cast whose conversion
  rule is clean is safe, and so is a conditional whose condition is a safe
  scalar.
- **Failing operands.** The machine types a few nodes to nothing, with no row
  and no constraint: `__func__`, `__FUNCTION__` and `__PRETTY_FUNCTION__`,
  which name no entity, and an unbound `__builtin_expect(...)` call, which no
  leaf rule types. Such a node is never accepted, but it carries
  `C_AST_TYPE_FLAG_FAILS`. The machine's checked cast tests its conversion
  only when the operand has a type, and its conditional tests the condition
  only when that has a type. So a cast over a failing operand is safe, and so
  is a conditional with a failing condition, such as `BUSTER_CHECK`'s
  `(void)(__builtin_expect(!(ok), 0) ? (…, 0) : 0)`. The query repeats the
  name's lookup in its own scope (`c_ast_types_lookups_agree`), and that
  lookup must find nothing.
- **Authority.** The machine remains the only producer of diagnostics. A query
  the typer declines, misses or leaves alone runs the machine as before.
- **Designator probes.** Nearly every miss was one of two designator probes
  from `c_parse_validate_const_assignments`, 64,905 of 65,070 on the unity
  self-host (about 56 M machine Ir):
  - the lone `{` or `,` that its member walk takes for the base of the
    `.name` designator after it;
  - the `.name` chain that its assignment walk takes for the place before a
    designator's `=`.

  No expression starts with these tokens. The machine's direct reader stops at
  the first one, so it fails without a diagnostic, a constraint, a new row or a
  memo entry, and the walk reads only that failure. The walk therefore no
  longer asks (`c_parse_designator_probe`), with or without a tree, so this is
  a deliberate change to the default path as well. In verify mode the machine
  answers each skipped probe anyway, and the probe must fail and leave the
  diagnostics and table sizes as they were. A `[index]` designator, and a
  probe range that starts at a `{` but runs on (`{ .a` before `.a.b`), still
  run the machine.

### Interned rows

`c_parse_primitive_type`, `c_parse_pointer_chain` and the machine's `&` append
a fresh row for every type name or address they read, and a cast's type name
is read once per operator-scan level and again by its leaf. On the unity
self-host 93.8% of the 96,717 pointer rows and nearly all of the 37,000
primitive rows were copies. Under the pilot, inside
`c_parse_validate_lowering_constraints`' loop over function bodies, where the
per-body queries mint them, those builders go through `c_parse_intern_type`.
It returns the live row equal to the one it would append (`CTypeInterning` in
`c_internal.h`). The tree's answers for casts and `&` read those rows.
File-scope initializers are validated before the window opens, where the
machine appends, so an initializer's answers read no interned row
(`c_ast_types_interned`) and those shapes decline there.

Interning is part of the pilot, not the default path. Semantic analysis keeps
the interning header only when the caller asks for it
(`CParserResult.type_interning`), and the driver asks only with the tree
(`-fc-ast-pilot`). Without it every builder appends as before. The first
version of stage 3 interned on the default path too. Review held it because a
default-path change needs 9700X acceptance, so it was narrowed
([#3321](https://github.com/buster14a/buster/pull/3321)).

A row interned there is observable only as table size:
- It is never mutated in place. Only aggregate and enum rows are completed in
  place.
- No side table is keyed by its index. Alignment records, the definition
  index and the aggregate lookup key rows with a tag or an unqualified link.
  Lowering keys its per-type tables on array rows.
- Lowering maps it to a scalar, qualified-scalar or pointer IR type, and
  lowering interns those itself.
- The window opens after every declaration has its rows, so an interned row
  only ever replaces a later copy of itself. That copy resolves in the same
  lowering pass, and the IR types, and so the `-g` type entries, keep their
  order.

Interning declarations too is not unobservable. Lowering maps rows in passes,
in table order, and a struct resolves only once its members' rows are mapped.
A member's interned `char *` row moved ahead of its struct, the struct resolved
a pass earlier, and the `-g` type entries of the unity self-host came out in a
different order.

The same holds for an aggregate a body query defines. The machine reads most
aggregate definitions written in expressions back from rows the declaration
pass made. One whose type name puts a qualifier before the tag
(`(const struct { char *p; })`) it defines itself, inside the window, with its
row ahead of its members'. CTypeInterning's `suspended` counts the member
segments the machine is reading (`c_type_parse_aggregate_range_step`), and
nothing is interned while any is in flight. The machine's failure path takes
back the segments it discards. Without that, a source of this shape kept its
`-g0` object but its `-g` object changed. The unity self-host defines no
aggregate inside the window. The c_ast corpus and the frontend fixtures define
a few, but in none of them did interning move a member's row ahead of its
aggregate, which is why the self-host checks missed it.
`c_test_type_interning_objects` compiles
both shapes with the window shut (`c_test_set_type_interning_off`) and open,
at `-g0` and `-g`, with and without the tree, and requires identical objects
and diagnostics.

A `restrict`-qualified row is never interned, because
`c_type_parse_root_finish` diagnoses an invalid `restrict` only on rows a
query appends. Array rows are never interned: each lowers to its own IR array
type, which `-g` describes. The interning log's live length,
`CParseResult.interned_type_count`, rolls back with the result, so a row a
rollback removed is never returned. The private constant-query copy, which
must not write shared state, appends instead.

`rederive.tree_type_{answers,declines,misses,nodes}` in the work ledger and the
`C_AST_TYPES` row under `-v` count its work. `c_ast_test_types` probes each
accepted kind, with the C type the standard gives it, and the declines and
misses on a private machine, with and without constraint checks. The
[corpus differential](#corpus-differential) holds every answer to the machine.
The default stays off: hosted measurements are diagnostic, and adoption needs
the Zen 5 route ([measurement plan](#measurement-plan)).

## Declaration split from the tree

`c_parse_ast_from_tree` (`c_parse.c`, after `c_parse_ast`) publishes the
`CParserResult` that `c_parse_ast_run` would publish for the same unit. It
covers every `CParserDeclaration` field, each function body's `_Static_assert`
ranges (`CParserStaticAssert`), the initializer and assertion expression
ranges, and the number facts. It reads them from the tree's declaration nodes
instead of rediscovering them from tokens. The driver uses it under
`-fc-ast-pilot`. Every rule is an explicit loop over a postorder range or a
fixed number of node and token reads; nothing recurses.

- **Split.** The external declarations are the `TRANSLATION_UNIT`'s children,
  and each one's anchor is its first token. So one declaration ends where the
  next begins, and the last ends at the end-of-file anchor. A `DECLARATION`
  gives one record per `INIT_DECLARATOR`, or one record when it has none. Like
  the walker, it splits a list into one segment per declarator, and the
  segments share the specifiers. A `FUNCTION_DEFINITION`, a file-scope
  `STATIC_ASSERT`, an `ASM_TOP_LEVEL` and an `EMPTY_DECLARATION` give one
  record each.
- **Names.** A record's name is its declarator's `DECLARATOR_NAME`. The
  record is a function when the first derivation above that name is a
  `DECLARATOR_FUNCTION`; its parameter list then gives the identifier-list
  range, when the name stands in no group. Group boundaries are where a suffix
  derivation's inner declarator is a pointer. A declaration without a
  declarator takes the last name outside every delimiter, else the first name
  inside one, which is what the walker does.
- **Punctuation.** The `)` before a body's `{`, the `,` before a later
  declarator, the `=` before an initializer and the final `;` have no nodes.
  Each is read as the token next to an anchor, and that read also checks that
  the token is the one the shape requires.
- **Body assertions.** The walker opens a frame at each `{` that is outside
  parentheses and brackets, and notes every statement that begins with
  `_Static_assert`. The tree side notes a `STATIC_ASSERT` that is an item of a
  `COMPOUND_STATEMENT` or `MEMBER_LIST` and is joined to the body only through
  braces. A statement expression, a type name, a parameter or a `for` header
  reaches it through parentheses instead. A sub-statement of a label, `case`
  or control statement begins with its keyword, so it is not noted.

The token walker stays the authority, and the whole unit falls back to it in
two cases:
1. **The walker would report a diagnostic.** A probe runs the walker's own
   validators over every token: integer spelling, type-specifier runs and the
   missing return operand. It reads the shape sidecar in 64-token windows. The
   walker validates a subset of those tokens, so a clean probe means a clean
   walk.
2. **The walker would read a shape differently from the grammar, or no rule
   here states what it reads.** `CParserTreeFallback` names each case:
   - `specifiers`: a parenthesized specifier (`typeof`, `_Atomic(T)`,
     `_Alignas`, `_BitInt`), or an enum's fixed type;
   - `declarator`: redundant parentheses, which is
     [#3215](https://github.com/buster14a/buster/issues/3215)'s family and
     where the walker misreads; attributes on a pointer or opening a group; or
     a function returning a function or an array;
   - `old_style`: an old-style declaration list;
   - `tokens`: a comma operator in an initializer, where the walker splits a
     list, or a token next to an anchor that is not the required one;
   - `assertion`: a file-scope assertion whose condition holds a comma or an
     initializer list, or a body assertion with no message or behind an
     attribute list.

A fallback discards everything derived and returns `c_parse_ast`'s result, so
the records, the diagnostics and #3215's rejections are always the walker's.
Neither #3215 nor #3143 is changed by this split. Where the walker's reading
is wrong but the split can state it, the split reproduces it instead. For
example, a `typedef` or `constexpr` word anywhere outside the body marks the
whole declaration ([#3310](https://github.com/buster14a/buster/issues/3310)).
The `typedef` and `constexpr` words are collected once per unit, so only a
declaration that holds one outside its top-level specifiers is scanned whole.

`c_ast_test_split` runs one shape per fallback reason, both #3215 inputs
included, in every layout. It requires the walker's result and the named
reason. The [corpus differential](#corpus-differential) compares every
record. On the self-host unity input the split publishes all 15,476 records
and 33 body assertions, with no fallback. The fixtures that fall back are the
parenthesized specifiers, the fixed-type enums and a few attributed or
redundant declarators.

## Corpus differential

`c_ast_test_corpus` (`c_ast_tests`; like `c_test.c`'s fixture suites it does not run on Android or iOS, whose test runs carry no repository tree) builds every `tests/**/*.c` file the
preprocessor accepts (with the `-std=c23` fixtures and
`tests/basic_c_dialect.c` in each dialect the driver test uses), a table of
declaration shapes the corpus holds few of, and on Linux the frontend's own
`c_source.c`, `c_parse.c`, `c_gen.c` and `c_ast.c`. Each tree must be complete
(three fixtures are pinned as not valid C input, each with its reason) and pass
`c_ast_validate`. The top-level declaration records the tree implies (one per
declarator; kind, definition, `typedef`, declarator name token, body start,
continuation and token-range tiling) must equal the `CParserDeclaration`
records `c_parse_ast` produces. Inputs on which the two passes legitimately
differ are pinned on both sides in `c_ast_corpus_known`, so a fix to either
pass fails its entry until the input moves to the agreement table
`c_ast_corpus_constructs`. No input is pinned today: the table's last two
entries, the attribute placements under [Syntax boundary](#syntax-boundary),
moved to the agreement table when the tree began to accept them. The four
declaration-split misreads the differential found
([#3142](https://github.com/buster14a/buster/issues/3142)) were fixed by
[#3156](https://github.com/buster14a/buster/pull/3156) and are agreement
constructs now: a file-scope plain `asm("...")`, attribute lists before or
opening a parenthesized pointer declarator, and a C23 opaque `enum E : T;`.
The tree also rejects syntax errors that today's `-fsyntax-only` accepts
([#3143](https://github.com/buster14a/buster/issues/3143)).

Every input whose tree builds also checks the
[declaration split](#declaration-split-from-the-tree) (`c_ast_corpus_split`).
The records `c_parse_ast_from_tree` publishes must equal `c_parse_ast`'s:
the same count, every `CParserDeclaration` field of every record, each
record's body assertion ranges, and the diagnostics. A unit the split hands to
the walker is compared too. To keep a split that always falls back from
passing, there are floors on the records it publishes itself: about 6,100
from the fixtures and constructs, and about 15,900 with the frontend sources.
Each of the four frontend sources must take the split whole. Today the
comparison covers about 16,400 records with 0 differences.

Every input whose tree builds and whose declarations `c_parse_ast` accepts
also checks the [tree expression typer](#tree-expression-typer)
(`c_ast_corpus_types`). Semantic analysis runs three times on it: without the
tree, with it, and with it in verify mode (`c_test_ast_type_verify_set`), where
the type machine also answers every query the tree answered. The first two
runs must end with the same diagnostics and the same type-table sizes. Every
tree answer must match the machine's in validity, structural type, constraint,
nonplace fact, diagnostics and table growth; a replayed answer's rows are
taken back after the replay, and the machine must append the same rows again.
The fixtures give about 56,600 checked answers on Linux x86-64, and the
hosted frontend sources about 282,300 more. Preprocessed for Windows AArch64
the fixtures reach fewer typed bodies: each stage-3 change alone gave about
50,300 and 50,800 there. Verify mode also has the machine answer every
designator probe the const-assignment walk skips (about 1,400 in the
fixtures, 27,600 in all), and each must fail without a diagnostic or a new
table row.

The compiler sources are preprocessed against the host's C library, so the
differential also covers glibc's headers. It caught `__float128`, which glibc
2.43 uses in `typedef __float128 _Float128;` for the compiler identity this
preprocessor reports. `__float128` is now a builtin type word.
`_Float128`, `_Float64x` and `_Float128x` stay identifiers that a header may
typedef.

## Consumer and retirement map

These token walkers currently rediscover structure that the tree records once.
A row may be retired only when its consumer reads nodes and the corresponding
differential tests pass.

| Current owner | Rediscovers | Tree replacement | Status |
|---|---|---|---|
| `c_parse_ast_run`, `c_parser_parse_function_body`, `c_parser_parse_declaration_expression` | declaration split, declarator name, body range, static-assert ranges | `DECLARATION`, `FUNCTION_DEFINITION`, `INIT_DECLARATOR`, `STATIC_ASSERT` | Retired under `-fc-ast-pilot` by the [declaration split](#declaration-split-from-the-tree). The walker stays the default path and the per-unit fallback. |
| `c_parse_statement_end`, `c_parse_bind_block_statements`, `c_parse_local_declarations` | statement boundaries, block declarations | `COMPOUND_STATEMENT` items | Not migrated. |
| `c_type_parse_*` declarator/specifier machine | specifiers, pointer/array/function derivations | `DECL_SPECIFIERS`, `DECLARATOR_*`, `TYPE_NAME` | Not migrated. |
| `c_parse_direct_expression_type`, `c_type_parse_sizeof_step` | operator precedence (top-down lowest-operator rescans) | expression nodes in postorder | Partly read under `-fc-ast-pilot` (tree expression typer); not retired. |
| `c_ir_lower_body_advance`, `c_ir_statement_end*` | first-token statement classification, controlled-body extents | statement nodes | Not migrated. |
| `c_ir_lower_expression_core_step`, `c_ir_has_root_*`, `c_ir_root_conditional` | shunting-yard precedence, root comma/assignment/conditional splits | expression roots | Not migrated. |
| `c_ir_predict_expression_type*`, `c_ir_query_*` | type-name probes, operand types over token ranges | `TYPE_NAME` / `CAST` / `SIZEOF_*` nodes plus retained semantic facts | Not migrated. |
| `c_ir_build_delimiter_index`, `CTokenPositionIndex` matching delimiters | bracket matching for every structural query | subtree extents | Not migrated. |

Under `-fc-ast-pilot`, the first row's three walkers no longer run for a unit
whose records the split publishes. Their consumer, semantic analysis's
declaration loop, reads records that come from `DECLARATION`,
`FUNCTION_DEFINITION`, `INIT_DECLARATOR` and `STATIC_ASSERT` nodes, and the
field-by-field differential passes. The row is not retired by default: the
walker still runs without the flag and for every unit the split hands back.
Deleting it waits on the hook becoming the default.

The [tree expression typer](#tree-expression-typer) is the first consumer of
the fourth row's replacement. Under `-fc-ast-pilot` it answers most of semantic
analysis's body and file-scope initializer expression-type queries from
expression nodes, operators, `&` and casts included. The machine still
answers the rest: shapes whose answer appends a row that is not interned,
and every query outside a typed body or initializer. So that row is not
retired.

## Measurement plan

Comparisons are between matched endpoints only. The pilot's tree is
complete, and its first consumer is the
[tree expression typer](#tree-expression-typer), so `-fc-ast-pilot` on versus
off is now an end-to-end comparison of the same checked frontend that includes
the tree's build cost. Hosted measurements are diagnostic. Acceptance needs the Ryzen 7 9700X route required by
[#2761](https://github.com/buster14a/buster/issues/2761) and described in
[benchmarking](../benchmarking.md).

For the pilot, the budgets for the self-host unity input (about 3.97 M tokens
reach the parser) were declared before any timing was taken:
- the implicit build costs at most 2× `c_parse_ast` on the same token stream,
  median of paired runs;
- the implicit layout retains at most 16 bytes per final-stream token;
- the transient high-water mark is at most one chunk per column plus frames,
  stacks and bindings.

The hybrid and explicit layouts are reported against the implicit one. Their
build and traversal costs are measured, not budgeted.

The first hosted census is
[`2026-10-08T225623Z`](../../performance-audits/2026-10-08T225623Z.md):
- The build and retained budgets pass: the implicit build takes about 1.03–1.06×
  `c_parse_ast`, and the tree retains 8.6 B per token.
- The transient budget fails: the chunks hold the whole tree until the seal
  copies it. The in-place columns, below, fix that.
- The implicit layout is kept. The explicit indices save about 2 ms per full
  traversal of the self-host tree, and cost 16–36 ms of build time and 8–26 MB.

For stage 1 of the tree expression typer, these budgets were declared before
its measured runs, on the self-host unity input with `-g0`:
- correctness: no verify mismatch over the corpus or the self-host input,
  identical diagnostics and type-table sizes with and without the tree, and
  byte-identical objects for `-c` (`-g0` and `-g`);
- the typer's own effect, `-fc-ast-pilot` with the typer against the same
  flag without it: fewer instructions in semantic analysis, with the eager
  pass and the lookups charged;
- adoption of the hook as the default: `-fc-ast-pilot` on against off must cut
  the whole `-fsyntax-only` compile by at least 1% in both instructions and
  paired wall time, without `-c` getting slower, and peak RSS may grow by no
  more than the tree's retained bytes plus 5%. Only the Zen 5 route can
  establish this.

The tree expression typer's first hosted census is
[`2026-10-09T132356Z`](../../performance-audits/2026-10-09T132356Z.md). It was
taken with Callgrind on `-march=x86-64-v3` builds and is diagnostic only:
- The correctness and object-identity budgets pass.
- With the tree built, the typer removes 2.1% of the compile's instructions
  (55% of the machine runs, the cheap ones).
- The tree itself costs 4.4%, so turning the hook on is still a 2.2% loss, and
  the adoption budget fails. The default stays off.
- The default path pays one check per query: +0.085%.

For stage 2, these budgets were declared before its measured runs, on the same
input and flags. Four arms are counted with Callgrind on tests-off
`-march=x86-64-v3` builds:
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

The base is the main revision the candidate branches from, so it already
carries stage 1. The budgets:
- correctness: as for stage 1. No verify mismatch over the corpus or the
  self-host input; identical diagnostics and type-table sizes with and without
  the tree; and byte-identical `-c` objects (`-g0` and `-g`) across the four
  arms.
- stage 2's own effect (C against B): fewer instructions in the whole compile,
  with the larger eager pass and the subtree lookups charged, and fewer machine
  runs from queries.
- the default path (D against A): stage 2 adds no work there, so the
  difference stays within ±0.05% Ir. That is the rebuild noise stage 1
  measured, about 0.03%.
- adoption of the hook as the default: unchanged from stage 1. Hosted
  instruction counts can show only its instruction half; wall time, `-c` and
  RSS acceptance remain with the Zen 5 route.

Stage 2's hosted census is
[`2026-10-09T181122Z`](../../performance-audits/2026-10-09T181122Z.md), taken
the same way and diagnostic only:
- The correctness, object-identity and default-path budgets pass. The default
  path is within +0.011%.
- Stage 2 removes 4.9% of the pilot's instructions (40% of the machine runs
  from queries, the expensive ones).
- Turning the hook on is now a 2.9% instruction gain against default. The
  instruction half of the adoption budget passes on this host. Wall time is
  unresolved by host noise, and acceptance stays with Zen 5 (#2761), so the
  default stays off.
- In bodies the machine still answers mostly shapes that append rows: casts to
  primitive or pointer type names, `&` and string literals. Outside bodies
  and misses are the other large items.

For stage 3, which types file-scope initializers and skips the designator
probes, these budgets were declared before its measured runs. They use the
same input, flags and four arms as stage 2, with the base at main `14544ffe`,
which already carries stage 2:
- correctness: as for stage 2, and every skipped designator probe, over the
  corpus and the self-host input, must also fail on the machine without a
  diagnostic or a new table row.
- the default path (D against A): the probe skip is the one intended change.
  D must run fewer instructions than A, and its machine runs from queries
  must fall by the skipped probes and by nothing else.
- the initializer typer's own effect: C against B, less the default path's
  D against A, must be fewer instructions, with the initializer passes and
  their lookups charged, and fewer machine runs from queries outside typed
  bodies.
- adoption of the hook as the default: unchanged from stage 1. Only its
  instruction half can be checked on this host.

For stage 3 (interned rows, `&`, casts to primitive and pointer type names,
the replay and the string literal path), these budgets were declared before
its measured runs, on the same input, flags and four arms (A base default, B
base with `-fc-ast-pilot`, C candidate with it, D candidate default):
- correctness: as for stage 2. No verify mismatch over the corpus or the
  self-host input; identical diagnostics and type-table sizes with and
  without the tree; and byte-identical `-c` objects (`-g0` and `-g`) across
  the four arms.
- the default path (D against A) changes on purpose here, so it is measured
  as its own arm: the type table must shrink, and neither `-fsyntax-only` nor
  `-c` (`-g0`) may grow by more than the 0.05% rebuild noise in
  instructions. A reduction is the expected result.
- stage 3's own effect on the pilot: C against B must remove more
  instructions than D against A, with the larger eager pass charged, and
  fewer machine runs from queries.
- adoption of the hook as the default: unchanged from stage 1. Hosted
  instruction counts can show only its instruction half; wall time, `-c` and
  RSS acceptance remain with the Zen 5 route.

The first measured runs failed the default-path budget at +0.21% on
`-fsyntax-only`, and the string literal path was the cause. It was dropped,
and the budgets above apply unchanged to what remains.

The row-appending slice's hosted census is
[`2026-10-10T014058Z`](../../performance-audits/2026-10-10T014058Z.md), taken
the same way and diagnostic only:
- That census measured the first version, which interned on the default
  path. Its hosted budgets passed: the default path was −0.064% on
  `-fsyntax-only` and −0.114% on `-c`, and the type table at the end of body
  validation shrank from 190,076 to 133,389 rows.
- Stage 3 removes 0.72% of the pilot's instructions. Machine runs from queries
  fall from 205,248 to 179,293. Most of the gain is the replay of `S8()`'s
  `(char8*)("text")` casts.
- Turning the hook on is now a 3.76% instruction gain against default.
  Acceptance stays with Zen 5 (#2761), so the default stays off.
- In bodies the machine still answers string literals (10,940 queries), `&`
  and `*` levels whose pointer row is minted later in the same body (about
  3,600), and checked casts over a declined or unsafe operand (about 850).

Stage 3 was then narrowed so that interning runs only under the pilot
(above), and the default path no longer changes. For that version, these
budgets were declared before its measured runs, on the same input, flags and
four arms. The base is now main `f01f5523`, which the branch merges.
- correctness: as above, with every run of the corpus gate interning, and with
  byte-identical `-c` objects (`-g0` and `-g`) across the four arms and
  between the pilot with and without interned rows
  (`c_test_type_interning_objects`).
- the default path (D against A): unchanged, so within ±0.05% Ir on
  `-fsyntax-only` and on `-c` (`-g0`).
- stage 3's own effect on the pilot: C against B removes instructions, with
  the larger eager pass charged, and fewer machine runs from queries.
- performance acceptance: the hosted counts are diagnostic and claim nothing
  for production. An exact-head 9700X compiler comparison is requested in the
  PR head. Until it publishes, performance validation is incomplete (#2761),
  and the hook stays opt-in.

The same audit records that version's hosted census, diagnostic only:
- The correctness and object-identity budgets pass.
- The default path interns nothing and its objects are unchanged, but its
  count moved by −0.069% on `-fsyntax-only`, outside the ±0.05% band on the
  cheaper side. The cause is the code around the changed functions, compiled
  differently, not saved work. `-c` is within the band at −0.031%.
- Stage 3 removes 0.80% of the pilot's instructions. Machine runs from queries
  fall from 207,217 to 181,003.
- On the merge with main `f6e8d25f`, the same arms read −0.077% on the
  default path and −0.84% under the pilot.
- The requested 9700X comparison decides the performance disposition (#2761).

For the in-place columns ([storage](#storage-and-lifetime)), these budgets were
declared before the measured runs, on the same input and flags, with stage 2's
four arms (A base default, B base with `-fc-ast-pilot`, C candidate with it,
D candidate default):
- correctness:
  - the self-host tree is identical in B and C, by a one-off digest of the
    node count and the four columns;
  - the tests compare trees built with and without column moves, under every
    refill batch and layout;
  - `-c` objects (`-g0` and `-g`) are byte-identical across the four arms.
- the copy: `column_copy_bytes` is 0 on the self-host.
- the pilot's transient budget, unchanged: the phase arena's peak holds only
  frames, stacks and bindings. On the self-host `transient_high_water` must be
  at most 2 MiB; the base reports the whole tree there, about 35 MB.
- the build's own cost (C against B): fewer instructions in `c_ast_build`
  inclusive, with the column arenas' reservations and commits charged, and
  no more instructions in the whole compile, the release included.
- the default path (D against A): within ±0.05% Ir.
- memory: peak RSS with the pilot does not rise (C against B, within 1%).

Wall time is reported but not budgeted: on this host it cannot resolve an
effect of a few milliseconds.

The in-place columns' hosted census is
[`2026-10-09T214805Z`](../../performance-audits/2026-10-09T214805Z.md), taken
the same way and diagnostic only. Every budget passes:
- The tree digests and the objects of all four arms are identical.
- `column_copy_bytes` is 0, where the base sealed 35.4 MB.
- `transient_high_water` falls from 35.6 MB to 60 KB, so the pilot's transient
  budget now passes.
- The pilot's compile loses 33.1 M instructions (−0.34%), 35.4 M of them the
  seal's copy, and 8.2 MB of peak RSS.
- The default path is within −0.0007%.
- `c_ast_build`'s own hosted time falls from a median of 86.6 ms to 63.5 ms;
  the whole compile's wall time is unresolved by host noise.
- Zen 5 validation stays incomplete (#2761).

For the [declaration split](#declaration-split-from-the-tree), these budgets
were declared before its measured runs. The input and flags are the same as
for stage 2, and the four Callgrind arms are counted on tests-off
`-march=x86-64-v3` builds:
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

The base is main `14544ffe`, which the candidate branches from. The budgets:
- correctness:
  - 0 differences over the corpus and the frontend sources;
  - a one-off field-by-field comparison on the self-host unity input, also
    with 0 differences;
  - identical diagnostics with and without the flag;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms.
- coverage: the self-host unity input takes the split whole, with no
  fallback.
- the split's own effect (C against B): the split, with its probe and scratch
  charged, costs fewer instructions than `c_parse_ast` does on the same unit
  in B. The whole compile also costs fewer instructions.
- the default path (D against A): the split adds no work there, so the
  difference stays within ±0.05% Ir.
- adoption of the hook as the default: unchanged from stage 1. Hosted
  instruction counts can show only its instruction half; wall time, `-c` and
  RSS acceptance remain with the Zen 5 route.

The declaration split's hosted census is
[`2026-10-09T225734Z`](../../performance-audits/2026-10-09T225734Z.md). It was
taken in the same way and is diagnostic only:
- Every budget passes except adoption. Objects are byte-identical across the
  four arms.
- The split's call costs 457.8 M Ir against the walker's 796.1 M. Both
  figures include the 292.7 M of number facts that both build.
- The whole compile drops by 3.47% (C against B). The pilot is now a 6.43%
  instruction gain against default.
- The default path is +0.025%: `c_number_facts_build` is no longer inlined
  into `c_parse_ast_run`.
- The parse phase's paired wall time drops by 41 ms in the median. The whole
  compile's wall time is lost in host noise.
- Acceptance stays with Zen 5 (#2761), so the default stays off.

For [preprocessor fusion](#preprocessor-fusion), these budgets were declared
before its measured runs, on the same input and flags (`-g0 -fsyntax-only`).
The fused mode hands over 2,048 tokens per batch; that size is fixed here,
before any run. Five arms are counted with Callgrind on tests-off
`-march=x86-64-v3` builds:
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- B′: candidate with `-fc-ast-pilot`, the array build;
- C: candidate with fusion;
- D: candidate, default flags.

The base is the main revision the candidate branches from. The budgets:
- **Correctness.** Over the corpus and on the self-host input, the fused tree
  is byte-identical to the array build in every column, with identical
  diagnostics, including on the first syntax error. The `-c` objects (`-g0`
  and `-g`) are byte-identical across A, B, C and D.
- **Default path (D against A).** Within ±0.05% Ir.
- **Array pilot (B′ against B).** Within ±0.05% Ir.
- **Fusion's instruction cost (C against B′).** At most +0.1% of the whole
  compile's Ir. Fusion adds hand-off work and removes none, so this bounds
  its overhead.
- **Locality (diagnostic).** Callgrind's cache simulation, with Zen 5's
  geometry (I1 32 KiB 8-way, D1 48 KiB 12-way, LL 32 MiB 16-way, 64-byte
  lines), counts last-level data read misses for B′ and C. Callgrind models no
  prefetcher, so fewer simulated misses is not a speedup. This row explains a
  time result; it cannot replace one. Batches of 256 and 16,384 tokens are
  simulated too, for sensitivity only.
- **The gain that ships fusion (C against B′).** Hosted paired wall time of
  preprocessing plus the parse phase must fall by at least 1%. The measure is
  `preprocess_ns + parse_ns` from `-fmetrics-out`, which holds the build in
  both modes. The runs are 15 ABBA pairs, and at least 12 of the 15 must
  favour C. An A/A control (B′ against B′, 15 pairs) must have its median
  within ±0.5%. Peak RSS may grow by no more than the builder's transient
  high-water mark.

If the last budget fails, the audit records the negative result and fusion
does not ship. A hosted pass would still leave Zen 5 acceptance (#2761)
incomplete.

Fusion's hosted census is
[`2026-10-09T222755Z`](../../performance-audits/2026-10-09T222755Z.md), taken
the same way and diagnostic only:
- **Passing budgets.** Correctness and object identity pass, and so do the
  three instruction budgets: the default path is +0.028%, the array pilot
  +0.013%, and fusion's own cost −0.0005%. Fusion's arm could not run under
  Valgrind with 31 GiB phase reservations, so its instruction and cache rows
  come from a build that reserves 8 GiB.
- **Cache simulation.** Fusion removes 712,939 last-level data read misses
  (6.2%). 548 K of them are the builder's own reads of the token array, and the
  rest are later reads of rows still in cache. It adds 712 K
  instruction-cache misses.
- **The ship budget fails.** Preprocessing plus the parse phase was 35.5 ms
  (5.2%) slower, with 4 of 15 pairs favouring fusion, and a repeat agreed. The
  A/A control itself exceeded its ±0.5% bound.
- **Decision.** Fusion does not ship. The token array and the array build stay.

For the typer's [failing operands](#tree-expression-typer), these budgets
were declared before the measured runs. The input and flags are those of
stage 2 (`-g0 -fsyntax-only`, plus `-c` for the default path), and four
Callgrind arms are counted on tests-off `-march=x86-64-v3` builds:
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

The base is main `f38a7716`, which the candidate branches from. The budgets:
- correctness: no verify mismatch over the corpus, and identical diagnostics
  and type-table sizes with and without the tree. `-c` objects (`-g0` and
  `-g`) must be byte-identical across the four arms.
- coverage: a throwaway census build, not committed, counts the checked casts
  the typer declines over a declined or unsafe operand. On the base it counts
  577 declined and 296 unsafe, plus 13 with an unclean conversion. On the
  candidate the first two together must fall by at least 600.
- the change's own effect (C against B): fewer instructions in the whole
  compile, with the larger eager pass and the extra lookups charged, and
  fewer machine runs from queries.
- the default path (D against A): the typer runs only with the tree, so the
  difference stays within ±0.05% Ir on `-fsyntax-only` and on `-c`.
- performance acceptance: the hosted counts are diagnostic. Zen 5 validation
  (#2761) stays incomplete, and the hook stays opt-in.

Results are recorded in a performance audit (`tools/new_audit.py`), not here.
