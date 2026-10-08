# Implicit postorder syntax tree

[Frontend index](../frontend.md) · [Issue #3102](https://github.com/buster14a/buster/issues/3102)

`c_ast_build` (`src/buster/lib/compiler/frontend/c/c_ast.c`, contract in
`c_ast.h`) reads a unit's final expanded token stream once, front to back, and
appends a complete syntax tree as it goes. The tree covers declarations and
declarators, statements, expressions, initializers and designators,
attributes, assembly and the supported GNU/C23 forms, function bodies
included. **Status: pilot.** Nothing in the production pipeline consumes it; the opt-in
[driver hook](#driver-pilot-hook) builds it for measurement.
`c_parse_ast`, `c_analyze_semantics_only` and `c_lower_to_ir_with_options`
still rediscover syntax from token ranges, as described in the
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
is migrated.

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

Known gaps, both rejected with a diagnostic and both pinned by the
[corpus differential](#corpus-differential); no corpus input uses either form:

- a later declarator in a list that carries both a leading and a trailing
  attribute list (`int a, __attribute__((x)) b __attribute__((y));`), because
  `INIT_DECLARATOR` has one attribute-list slot;
- an attribute list that opens a parenthesized declarator which is not a
  pointer (`int (__attribute__((x)) p);`, accepted by clang, gcc and
  `c_parse_ast`), because only `DECLARATOR_POINTER` has a slot for it.

## Driver pilot hook

`ide cc -fc-ast-pilot[=implicit|hybrid|explicit]` (see the
[driver guide](../driver.md)) is off by default. When given, the C compile path
calls `c_ast_build` after `c_preprocess` succeeds and before `c_parse_ast`,
inside the existing parse phase boundary, so the build's time is part of
`parse_ns` and of the `parse` phase in `-fmetrics-out`. The bare flag is the
implicit layout. The tree lives in the unit's arena and nothing reads it
afterwards; the object, the diagnostics of valid input and every later stage
are unchanged. The driver has no phase arena to lend (`c_preprocess` is not
given one either), so the builder creates and retires its own. A build that is
not complete fails the unit with the parse error class; its diagnostic is
published exactly as a `c_parse_ast` diagnostic is. `-E` and assembly inputs
never reach the hook.

Under `-v` the driver prints two rows with the other verbose counters:

- `C_AST nodes=<n> tokens=<parser tokens> build_ns=<c_ast_build wall time>
  retained_bytes=<> transient_high_water=<> sealed_copy_bytes=<>
  finalize_child_entries=<> layout=<name>`
- `C_AST_WALK walk_ns=<one full c_ast_walk over the root> walk_steps=<events>
  scan_ns=<one linear pass over the kinds column> children_ns=<c_ast_children
  over every node into a scratch buffer> child_entries=<sum of child counts>
  scan_calls=<CALL nodes the scan counted>`

The second row's passes run only under `-v`; they are diagnostic. Each feeds a
counter that is printed (`walk_steps`, `scan_calls`, `child_entries`), so the
compiler cannot drop the measured loop. Both rows use the driver's own clock
and are summed over the inputs of one invocation; the layout is the
invocation's. Like all hosted numbers they are diagnostic, not acceptance
evidence.

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
records `c_parse_ast` produces. The differences that exist are pinned on both
sides in `c_ast_corpus_known`:

- `c_parse_ast` classifies a file-scope plain `asm("...")` as a function named
  `asm`, and `int __attribute__((x)) (*p)(void);` as a function named `x`.
- It takes `__attribute__` as the name in `int (__attribute__((x)) *p);` and
  classifies a C23 opaque `enum E : T;` as an object.

The compiler sources are preprocessed against the host's C library, so the
differential also covers glibc's headers. It caught `__float128`, which glibc
2.43 uses in `typedef __float128 _Float128;` for the compiler identity this
preprocessor reports. `__float128` is now a builtin type word.
`_Float128`, `_Float64x` and `_Float128x` stay identifiers that a header may
typedef.

These are defects of the current declaration split
([#3142](https://github.com/buster14a/buster/issues/3142)). The tree also
rejects syntax errors that today's `-fsyntax-only` accepts
([#3143](https://github.com/buster14a/buster/issues/3143)).
- The tree's two known gaps above are pinned as rejected.

## Consumer and retirement map

These token walkers currently rediscover structure that the tree records once.
A row may be retired only when its consumer reads nodes and the corresponding
differential tests pass.

| Current owner | Rediscovers | Tree replacement |
|---|---|---|
| `c_parse_ast_run`, `c_parser_parse_function_body`, `c_parser_parse_declaration_expression` | declaration split, declarator name, body range, static-assert ranges | `DECLARATION`, `FUNCTION_DEFINITION`, `INIT_DECLARATOR`, `STATIC_ASSERT` |
| `c_parse_statement_end`, `c_parse_bind_block_statements`, `c_parse_local_declarations` | statement boundaries, block declarations | `COMPOUND_STATEMENT` items |
| `c_type_parse_*` declarator/specifier machine | specifiers, pointer/array/function derivations | `DECL_SPECIFIERS`, `DECLARATOR_*`, `TYPE_NAME` |
| `c_parse_direct_expression_type`, `c_type_parse_sizeof_step` | operator precedence (top-down lowest-operator rescans) | expression nodes in postorder |
| `c_ir_lower_body_advance`, `c_ir_statement_end*` | first-token statement classification, controlled-body extents | statement nodes |
| `c_ir_lower_expression_core_step`, `c_ir_has_root_*`, `c_ir_root_conditional` | shunting-yard precedence, root comma/assignment/conditional splits | expression roots |
| `c_ir_predict_expression_type*`, `c_ir_query_*` | type-name probes, operand types over token ranges | `TYPE_NAME` / `CAST` / `SIZEOF_*` nodes plus retained semantic facts |
| `c_ir_build_delimiter_index`, `CTokenPositionIndex` matching delimiters | bracket matching for every structural query | subtree extents |

## Measurement plan

Comparisons are between matched endpoints only. The pilot's tree is
complete, but nothing consumes it yet, so it cannot be compared against
today's checked frontend as an end-to-end result. Hosted measurements are
diagnostic. Acceptance needs the Ryzen 7 9700X route required by
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

Results are recorded in a performance audit (`tools/new_audit.py`), not here.
