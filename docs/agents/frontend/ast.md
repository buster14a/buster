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
  expression-type queries during semantic analysis.

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
[Remaining migration](#remaining-migration).

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

During construction, columns grow in fixed-size chunks in the phase arena.
They are then sealed into the caller's arena as exact-sized contiguous arrays,
one copy per column. The phase arena is released afterwards, or a private one
is retired. `CAstStatistics` reports the retained bytes, the transient high
water mark and the bytes copied by sealing. On the first syntax error the
build records one structured diagnostic and publishes an empty tree, so a
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
phase in `-fmetrics-out`. The bare flag is the implicit layout. The tree lives
in the unit's arena. The driver then takes the unit's declaration records from
`c_parse_ast_from_tree` in place of `c_parse_ast`
([declaration split](#declaration-split-from-the-tree)), and hands the tree to
semantic analysis in `CParserResult.ast`, where the
[tree expression typer](#tree-expression-typer) reads it; nothing else does.
The object, every diagnostic and every later stage are unchanged. The driver has no phase arena to lend (`c_preprocess` is not
given one either), so the builder creates and retires its own. A build that is
not complete fails the unit with the parse error class; its diagnostic is
published exactly as a `c_parse_ast` diagnostic is. `-E` and assembly inputs
never reach the hook.

Under `-v` the driver prints four rows with the other verbose counters:

- `C_AST nodes=<n> tokens=<parser tokens> build_ns=<c_ast_build wall time>
  retained_bytes=<> transient_high_water=<> sealed_copy_bytes=<>
  finalize_child_entries=<> layout=<name>`
- `C_AST_WALK walk_ns=<one full c_ast_walk over the root> walk_steps=<events>
  scan_ns=<one linear pass over the kinds column> children_ns=<c_ast_children
  over every node into a scratch buffer> child_entries=<sum of child counts>
  scan_calls=<CALL nodes the scan counted>`
- `C_AST_TYPES bodies=<function bodies typed> nodes_typed=<expression nodes
  the eager pass visited> nodes_accepted=<those it gave a type> answers=<type
  queries answered from the tree> declines=<queries that mapped to a node the
  typer does not vouch for> misses=<queries that mapped to no node>
  gated=<queries met in a machine state the typer leaves alone>`
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
bodies from the tree, in place of the speculative type machine
(`CTypeParseMachine` in `c_parse.c`). Stage 1 covers names, literals and
postfix chains; stage 2 adds the operators. It runs only when the caller
supplies a tree in `CParserResult.ast`, which today only the
[driver hook](#driver-pilot-hook) does; without one, analysis is unchanged.

- **When it types.** `c_parse_validate_lowering_constraints` indexes the
  tree's top-level function definitions once (`c_ast_types_bodies_prepare`).
  Before each body's validator families run, `c_ast_types_body_begin` makes one
  forward pass over the body's node interval. Children come before parents, so
  each expression node's operands are already typed when the node is reached.
  The pass records each node's token span and, for the accepted kinds, its
  type, whether it is safe under constraint checks, and the bit-field width of
  a member. A node is accepted only when every operand the machine types for
  it is accepted. Its arrays live in the machine's scratch arena above the
  body's validation mark and are released with the rest of the body's scratch.
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
    the subtree resolves to an entity other than the one the binder bound
    (`c_ast_types_lookups_agree`).
- **What it accepts.** Each answer is a row that already exists: an entity's,
  member's, element's, return or typedef type, an operand's own row, or an
  immutable scalar row. The rules are the machine's own functions, shared
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
- **What it declines.** Every shape whose machine answer appends a row stays
  with the machine:
  - a qualified member or array element;
  - `&` and string literals;
  - an array operand of `+` or `-`, which decays;
  - a pointer or `nullptr` conditional;
  - a qualified operand whose unqualified row was never recorded;
  - a cast or compound literal to any other type name. The machine's operator
    scan reads a parenthesized type name through a reader that appends
    primitive and pointer rows, and its leaf reads it again, so the number of
    rows depends on how many of its task levels see the group.

  An operand the machine scans but does not type (a cast's operand without
  constraint checks, a `sizeof` expression) must therefore hold no type name.
  The typer also declines a `?:` whose range holds a top-level comma or
  assignment, which the machine splits at instead.
- **Constraint checks.** A checked query is answered only from a node whose
  operands are safe and whose own checked-mode rule cannot fire. For the
  binary operators that is the machine's operand rule. The cases it settles by
  spelling or compatibility pass only for the literal `0` against a pointer and
  for two pointers to one unqualified element row. A cast whose conversion
  rule is clean is safe, and so is a conditional whose condition is a safe
  scalar.
- **Authority.** The machine remains the only producer of diagnostics. A query
  the typer declines, misses or leaves alone runs the machine as before.

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
   missing return operand. The walker validates a subset of those tokens, so
   a clean probe means a clean walk.
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
Neither #3215 nor #3143 is changed by this split.

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
nonplace fact, diagnostics and table growth. The fixtures give about 56,000
checked answers, and the hosted frontend sources about 246,600 more.

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
analysis's body expression-type queries from expression nodes, operators
included. The machine still answers the rest: every shape whose answer
appends a type row, and every query outside a typed body. So that row is not
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
  copies it.
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

Results are recorded in a performance audit (`tools/new_audit.py`), not here.
