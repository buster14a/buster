# Implicit postorder syntax tree

[Frontend index](../frontend.md) · [Issue #3102](https://github.com/buster14a/buster/issues/3102)

`c_ast_build` (`src/buster/lib/compiler/frontend/c/c_ast.c`, contract in
`c_ast.h`) reads a unit's final expanded token stream once, front to back, and
appends a complete syntax tree as it goes. The tree covers declarations and
declarators, statements, expressions, initializers and designators,
attributes, assembly and the supported GNU/C23 forms, function bodies
included. **Status: default since 2026-10-13.** The
[driver hook](#driver-pilot-hook) builds it on every C compile, by the
[owner decision](https://github.com/buster14a/buster/issues/3102#issuecomment-6146782845)
that put the switch ahead of steps 3 and 4 of the
[lowering direction](https://github.com/buster14a/buster/issues/3102#issuecomment-6144374034).
Every ordinary C compile prepares the tree before semantic analysis and
retains the completed facts needed by lowering. The production
`-fno-c-ast-pilot` switch is retired; selecting it is an unsupported-option
error. `-fc-ast-pilot=implicit|hybrid|explicit` remains a layout selector.
The explicit `c_parse_ast_reference` and model-only `c_parse` APIs preserve
independent parser and semantic controls. They do not provide a lowering
fallback: direct callers use `c_parse_for_lowering`, and lowering refuses an
unvalidated model or missing completed tree/facts.

The declaration split, semantic expression typer and statement plan consume
this syntax during analysis. Lowering reads completed nodes, operands,
declaration and initializer ownership, selections and numeric facts. Tokens
remain available to preprocessing and semantics; lowering receives source
spelling and diagnostic sites through `CSourceAnchor`.

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
compound literal differ by kind, so neither needs a separate marker. A
`TYPE_NAME` has one specifier child and an optional abstract declarator
(`RANGE`, one or two children); its `data` retains the builder's cursor at
its end. The shared span index reads that producer boundary directly,
including nested `typeof`, function pointers and VLA bounds.

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

### Retained facts

**Each phase retains its finished output for the next** (owner direction of
2026-10-13 on [#3102](https://github.com/buster14a/buster/issues/3102#issuecomment-6144374034),
implemented by [#4062](https://github.com/buster14a/buster/issues/4062)).
This replaces the #3474 decision that the
[tree expression typer](#tree-expression-typer)'s per-node types are neither
retained nor recomputed for lowering. The [inventory](#facts-lowering-needs)
lists what lowering re-derives today and which facts are retained so far.

**The record.** `CAstFacts` (`c.h`) is three dense columns indexed by node,
each `node_count` long, about 9 bytes per node:

| Column | Bytes | Meaning |
|---|---|---|
| `types` | 4 | the node's C type row (`CTypeId` value), or `C_AST_FACT_NONE` |
| `payloads` | 4 | per flag: the entity an `IDENTIFIER` or `DECLARATOR_NAME` names, a bit-field member's width, or a string literal's length |
| `flags` | 1 | `CAstFactFlag`: candidate, typed, from the machine, nonplace, bound, bit-field, string |

The columns are dense because the tree is complete before analysis starts,
so the node count is known and the record never grows. Analysis prepares the
unit's shared `CAstSpans` range-to-node index before its first query
(`c_ast_facts_prepare`), using the parse result's retained delimiter rows.
Every semantic producer keys its finished answer through
`c_ast_facts_locate`; declaration-name leaves are spanned as well as
expressions. The index lives in `CAstFacts.index_arena`, follows the facts'
lifetime, and its bytes are included in `retained_bytes`. A required index
reservation failure produces a structured semantic diagnostic. The tree
typer's smaller per-region scratch index is still released at its region's end.

Declaration owners are published with the final semantic arrays by
`c_parse_publish_declaration_facts`: `CDeclaration.declaration_node_plus_one`
selects the translation-unit child, `init_declarator_plus_one` selects its
own declarator, and `initializer_node_plus_one` selects its optional last
child. Zero denotes absence. `CEntity.init_declarator_plus_one` keeps the
init-declarator at its finalized declaration anchor, including a local's
`ASM_LABEL`; `CEntity.declaration_node_plus_one` retains its containing
declaration for shared local symbol attributes. Compatible earlier
declarations do not replace these owners. The existing semantic array arena
allocations account their sizes: twelve bytes per declaration capacity row
and eight bytes per entity capacity row. Source-order
extent cursors publish these links without a parent table or punctuation
classification. `c_ast_declarator_name` follows only the inner-child chain,
so function parameter names cannot substitute for a declaration's owner.
The token-reference semantic model has no node provenance; independent owner
fixtures check these links while model comparisons check the existing fields.

Deferred static assertions are finished semantic verdicts. Canonical lowering
accepts only retained `C_STATIC_ASSERT_HOLDS` rows and rejects missing or
non-passing rows at their recorded declaration location before allocating IR.
It has no unchecked assertion evaluator or late source-message parser.

Unit lowering assigns each function definition to its finished semantic scope
and propagates that owner to child scopes. Parameter-bound uses belong to the
definition; prototype-only uses do not become unconditional external roots.
Global initializer liveness reads bound identifier leaves in the retained
initializer subtree, excluding member and designator names by node kind.
Prepared capacities count call/index/array nodes and sparse expression groups;
assembly and label-address nodes control SSA barriers. Statement-expression
jump enclosures are subtree intervals around known label and goto nodes.
Intrinsic expansion budgets walk the known definition interval and consume
finished builtin identities, result types and storage-half admission facts.
Unevaluated operands are skipped by node kind and retained variable-bound
answers; vector lane counts use finished type/recipe references and scalar
layout. Budgeting performs no syntax scan or semantic type query.
The finished semantic index supplies the remaining delimiter-provenance
column. Lowering rejects a missing column for a nonempty stream before IR
allocation; it performs no late position-index build. Label tables enumerate
nodes and need no identifier-colon candidate positions.

Finished symbol properties are sparse `CDeclarationBinding` rows in the
result's arena. `CDeclaration.binding_plus_one` and a static or extern local's
`CEntity.binding_plus_one` select them; zero means the empty property set.
The producer preserves declaration-level visibility, alias and weakref names,
constructor/destructor priorities, section and assembler link names, noreturn
and GNU inline requests, and GNU error text with its recorded attribute site.
All decoded strings live in the result arena; the growth vector is scratch,
compacted once and rewound. `C_SYMBOL_BINDINGS` reports the exact retained
arena bytes and growth/final-copy bytes. Alias validation reads the finished
rows before lowering, while the explicit token model keeps its source reader.
Those source readers live in `c_parse.c`; `c_gen.c` consumes the finished
properties and reads source tokens only at recorded spelling/location sites.
Global initializer callers likewise pass the retained initializer root, with
zero owner ids denoting implicit zero initialization.

**Candidates.** A fact candidate is an expression node (`IDENTIFIER` through
`COMMA`, without braced lists and `_Generic` associations) of a *typed
region*: a function body or a file-scope object's initializer that the
typer indexes. Expression nodes elsewhere are counted, not typed:
- `uncovered` counts those inside a function definition no region covered,
  because the body was refused or its scratch ran out;
- `outside` counts those at file scope outside any initializer: array bounds,
  enumerators, attribute arguments and static assertions. Lowering reads
  their constants from `CParseResult` already (see the inventory).

**Publication.** Analysis is the only writer. `c_parse.c` keeps its method;
it publishes and does not parse differently.
1. **A query's answer.** A top-level query (machine idle, normal evaluation
   mode) whose valid answer covers exactly a candidate's span, after
   balanced outer parentheses are stripped, keeps that answer
   (`c_ast_types_facts_answer` at the end of `c_parse_expression_type_query`).
   Its rows stay: only a failing root query rolls back, and only to its own
   checkpoint.
2. **The typer's rule.** At the region's end (`c_ast_types_facts_begin`), every
   accepted node's type, every identifier's binding (the binder's use, or an
   initializer's lookup) and every bit-field member's width are copied out.
   An initializer no query typed is typed first. An accepted row existed
   when the region was typed, before any of the region's checkpoints, so no
   rollback in the region can take it.
3. **The completion run.** Every candidate still without a type is asked of
   the machine over its span, unchecked and in the region's mode, operands
   before operators, so each operator's run reads its operands' answers from
   the body's memo (`c_parse_region_facts`). The typer is set aside for these
   queries. Nothing a run appends or mutates is kept. A run that appended a
   row, bound, alignment record or interned-log entry, or mutated a row, is
   rolled back, including the mutations a valid root commits
   (`CTypeParseMachine.committed_mutation_count`), and the memo slot it may
   have overwritten is restored. Its answer is still the node's fact when the
   run added no member, entity, scope or diagnostic and either:
   - the answer's row is older than the run, because the appended rows were an
     operand's (a string literal under `[]`); or
   - the node is a string literal. Its array row is kept in structural form
     (`C_AST_FACT_STRING`): `types` holds the element row and `payloads` the
     length with its terminator (`c_ast_types_facts_string`). A validator's
     query that answers a string literal is kept in the same form.

   A sound answer whose own row is temporary keeps the entire appended row
   run in sparse `CAstTypeRecipe` descriptions before rollback. Each describes
   a CType, its copied array bound and bound-expression node, and inherited
   type-alignment record. References into the temporary run become recipe
   references; older references name existing published rows. The type table,
   bound table and query memo still return to their checkpoints.

   Two forms without ordinary C rows are explicit: a recognised builtin
   identifier retains its semantic builtin identity (its call retains its own
   result type separately), and a function-name identifier retains its
   enclosing declaration and the implicit `const char[name length + 1]`
   object. The counters report those forms separately from ordinary type
   recipes. Every remaining failure is a counted gap.

**Why a completion may not append.** Lowering maps every C type row to an IR
type up front (`c_type_ir_map`), and `-g` describes every IR type. A row that
only the completion run appended would therefore change the `-g` object.
The sparse descriptions retain these answers without appending any row to
that inventory. `C_AST_FACT_RECIPE` makes the distinction explicit; `types`
is then a recipe index, and consumers must never interpret it as a CTypeId.
The recipe arena follows the facts' lifetime, and its committed bytes are
included in `retained_bytes`. Failed recipe reservations produce a structured
source-size diagnostic.

**Lifetime.**
- The driver reserves the record (`c_ast_facts_create`) in a private arena
  once the pilot tree is built, unless the action is `-fsyntax-only`, which
  does not lower.
- Analysis receives it in `CParserResult.ast_facts` and lowering in
  `CIRLowerOptions.syntax_facts`.
- The driver retires the arena (`c_ast_facts_release`) as soon as
  `c_analyze_with_options` returns, so no later phase holds it at peak, and
  again, idempotently, at the unit's end.
- When analysis finishes (`c_ast_types_facts_finish`), any published row past
  the final type table is dropped and counted as `stale`. Zero is expected.

**Never in IR.** No node id, column index or `CAst` pointer reaches canonical
IR. Lowering reads the columns by node and emits only what it emits today.

## Retained facts for lowering

Step 1 of the [owner direction](https://github.com/buster14a/buster/issues/3102#issuecomment-6144374034)
on #3102: each phase keeps its finished output for the next, and `c_gen.c`
lowers from nodes and facts.

- **The tree.** Parsing and semantic analysis retain the completed syntax tree
  and facts before lowering. Direct callers use `c_parse_for_lowering`; a
  model-only result cannot lower. `c_lower_to_ir_tree_run` borrows the retained
  tree when no override is supplied and rejects missing authority. It never
  builds syntax or reruns semantics. `-fsyntax-only` prepares syntax without
  allocating lowering-only fact/literal storage.
- **Spans.** Semantic analysis computes `CAstSpans` and seals delimiter
  provenance before publication. Its private range index serves semantic
  producers; lowering borrows the columns and selects them only by an exact
  carried node. A missing span/delimiter column refuses the lowering boundary.
  `c_ir_syntax_spans_create` owns only the lowering hoist bytes and walk bits;
  no lower-time span or matching-delimiter build remains.
- **Types and constants.** Completed C model references, sparse constructor
  recipes, bindings, selections, numeric answers and initializer plans live
  with `CAstFacts`. The checked node scheduler materializes the canonical
  type/value projection in the established order, without changing C rows,
  recovering nodes from ranges or calling a private grammar reader.
- **Counts.** `CAstLowerStatistics` records borrowed spans (`span_nodes`,
  `span_bytes`) and actual hoist/evaluator work. `-v` prints
  `C_AST_RETAINED span_nodes=<> span_bytes=<> hoist_steps=<> evaluator_steps=<>`.
  Syntax construction counts remain in the driver's `C_AST` row. The
  always-zero lower-time tree-build and range-adapter fields are removed;
  historical receipts retain their original fields.

Retained bytes are measured and reported, not budgeted per slice. The
end-to-end measurement waits until lowering is complete (step 5).

Expression type queries have distinct node views in `c_ir_expression_type_node`:
`STRICT` keeps the operand type, `PROMOTED` uses the existing bit-field promotion
rules, and `PREDICTED` applies value-context array/function decay and the existing
conditional prediction method. These views remain part of each checked query
key. Their child tasks follow AST topology and the finished facts; sparse type
recipes are materialized without appending a semantic model row. Private graph
and projection state survives a child yield and is rewound only after its root
finishes. Canonical IR stores the resulting IR types, never the node or state.
`c_parse_for_lowering` selects the production driver's type-interning policy
for its full semantic queries. `c_parse` keeps its historical model-only
policy; explicit semantic callers continue to select their parser option.
Independent full-authority comparisons match the interning option explicitly.

Compact direct-call publication preflights the exact aligned AST and fact
arrays, including sparse recipes, constants and source-owner sidecars. It
returns `C_AST_RETAIN_FAILED` before copying or retiring an owner when the
unit arena cannot hold the whole output. Entrypoints reserve a refusal row
before publication, return `SOURCE_TOO_LARGE`, and retire their private owners
once; an earlier semantic diagnostic retains its precedence. The ordinary
borrowed driver takes no copies or refusal-row reservation. Successful compact
outputs retain their existing layout and measured copy bytes.

Negative direct-call fixtures preserve the independent model-only assertions,
then compare required full semantics with the explicit token-reference semantic
authority. Label return/call refusals precede lowering; each aggregate escape
is checked separately so an earlier refusal cannot hide a later lowering-only
address refusal. A malformed assembly body retains the following declaration
in both models, and its valid neighbor lowers independently; failed full
semantics never publishes a partial canonical program.

The remaining range boundaries locate one retained node before querying it.
Operand, prediction, conditional-type and null-constant range frame families
and the operand precedence/postfix walkers have been removed. Conditional
selection merges its already projected branches in private node-root state;
it requests only the promoted or null-constant children the merge needs.
Local identifiers use their retained entity binding and the existing local
VLA source view, preserving the parameter's adjusted ABI pointer. A bound
incomplete extern array keeps its bare STRICT/PROMOTED refusal; PREDICTED and
conditional, arithmetic or subscript value operands use its existing model
element pointer. That contextual projection does not mint an unsized array row
or change the retained semantic type.

`c_test_expression_type_projection` checks all three views against verified
legacy shapes, including qualifiers, nested pointer/function types, bit-field
conditional prediction and the leading-compound postfix method. Its test-owned
provenance snapshot poisons every token kind and punctuator before the node
query. The old expression walkers are absent from test builds too.

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

Every ordinary C compile builds its retained tree inside the parse phase,
after preprocessing. `ide cc -fc-ast-pilot[=implicit|hybrid|explicit]` selects
the layout; the bare flag and an invocation with no flag use `implicit`.
`-fno-c-ast-pilot` is retired and rejected. `-E` and assembly inputs never
reach the hook.

The driver lends the completed tree to `c_parse_ast_from_tree` and semantic
analysis, creates facts for units that will lower, and retires the private
owners after all consumers finish. A syntax rejection retains the builder's
diagnostics while the independent semantic parser preserves existing
precedence. Reservation failure is an immediate parse error. No incomplete
syntax reaches lowering, and lowering never rebuilds a missing tree.

The model-only `c_parse` and explicit `c_parse_ast_reference` oracle APIs
remain separate controls for parser/semantic differentials. Production
invocations always prepare syntax, including `-fsyntax-only`; that action
avoids retained lowering facts and decoded literal bytes.

Under `-v` the driver prints these rows with the other verbose counters, plus
`C_AST_REJECTED units=<n>` when the builder refused a unit:

- `C_AST nodes=<n> tokens=<parser tokens> build_ns=<c_ast_build wall time>
  retained_bytes=<> transient_high_water=<> column_copy_bytes=<>
  parenthesis_copy_bytes=<> parenthesis_committed_bytes=<> finalize_child_entries=<> layout=<name>`
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
- `C_AST_STATEMENTS bodies=<function bodies whose plan the binder followed>
  segments=<plain segments it bound in one pass> segment_tokens=<their tokens>
  uses=<identifier uses they bound> bails=<segments it handed back to the
  token loop partway> declaration_hints=<block declarations whose end the plan
  gave> loop_hints=<for statements whose boundaries the plan gave>
  declines=<statements the plan left to the token loop>
  declarator_hints=<block declarators the split took from their hints>
  initializer_hints=<plain initializers among them> auto_skips=<declarations
  whose auto-inference scans were skipped> declarator_declines=<declarator
  hints the split refused> braced=<statements planned around their
  initializer lists' braces> fallbacks=<bodies left wholly to the token loop>
  reason=<CAstStatementFallback of the last fallback> fallback_token=<that
  body's `{`>`

- `C_AST_LOWER steps=<nodes the root-conditional descents visited>
  answers=<root-conditional queries answered true> negatives=<queries
  answered false> statement_bodies=<bodies the statement walker lowered from
  their nodes> statements=<statement nodes it lowered>`

- `C_AST_FACTS nodes=<tree nodes> retained_bytes=<the facts' committed
  columns> regions=<typed regions published> candidates=<their expression
  nodes> tree=<typed by the typer's rules> machine=<typed by a validator's
  query> completed=<typed by the completion run> missing=<candidates left
  without a type> unspanned= invalid= appends= busy=<those, by CAstFactGap>
  bound=<identifiers with an entity> bit_fields=<bit-field members>
  uncovered=<expression nodes in a definition no region covered>
  outside=<file-scope expression nodes outside every initializer>
  stale=<rows dropped as past the type table> completion_queries=<machine runs
  the completion made>` ([retained facts](#retained-facts)).


The second row's passes run only under `-v`; they are diagnostic. The third to
sixth rows count what the typer, the split, the statement plan and lowering
did; they time nothing. `C_AST_LOWER` and `C_AST_FACTS` are zero under
`-fsyntax-only`, which does not lower. Each feeds a
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
file-scope initializers, and `&` and casts to primitive and pointer type
names over rows the machine interns under the pilot. It runs
only when the caller supplies a tree in `CParserResult.ast`, which today only
the [driver hook](#driver-pilot-hook) does; without one, analysis is
unchanged.

- **When it types.** `c_parse_validate_lowering_constraints` indexes the
  tree's top-level function definitions and declarations once
  (`c_ast_types_bodies_prepare`). Before each body's validator families run,
  `c_ast_types_body_begin` makes one forward pass over the body's node
  interval, in two loops: one types the single-token leaves and lists the other
  expression nodes, and the other runs the operator rules over that list in
  index order. Children come before parents, so each expression node's
  operands are already typed when the node is reached. The pass records each node's
  token span and, for the accepted kinds, its type, whether it is safe under
  constraint checks, and the bit-field width of a member. A node is accepted
  only when every operand the machine types for it is accepted. Its arrays
  live in the machine's scratch arena above the body's validation mark and are
  released with the rest of the body's scratch.
- **Why bodies are typed eagerly.** Initializers wait for their first query
  (below), but bodies gain nothing from that. On the unity self-host, 6,531 of
  6,612 bodies receive a query that the literal fast path does not answer, and
  90% of the typed body expression nodes lie inside some queried node's
  subtree. The pass is made cheaper per node instead (audit
  [`2026-10-10T203657Z`](../../performance-audits/2026-10-10T203657Z.md)):
  - one table load classifies a node's kind;
  - only the arrays that are read before they are written are cleared;
  - `c_ast_types_member_lookup` remembers, for the length of one pass, a member
    found directly in its aggregate's row. Nothing the lookup reads changes
    during a pass. A promoted search into anonymous members is never
    remembered, because it takes guarded body scratch (#1256).

  A second cut keeps every rule:
  - the leaves and the operators are typed in separate loops, which keeps
    each loop's working values in registers;
  - an integer literal whose kind the syntax pass cached is typed from that
    fact without calling the machine's leaf;
  - `c_ast_types_arithmetic` remembers the usual arithmetic conversions for
    one pass;
  - two integer operands pass the checked-mode operator rule without its
    other tests.
- **File-scope initializers.** `c_parse_validate_static_initializers` made
  nearly all of the queries outside a typed body: 77,545 of 79,035 on the unity
  self-host, at about 190 M machine Ir, against at most 3 M for any other
  validator (audit `2026-10-09T213311Z`). The other phases are left to the
  machine: together they run about 3 M machine Ir, and almost none of it
  could be answered (audit
  [`2026-10-11T093053Z`](../../performance-audits/2026-10-11T093053Z.md)).
  Before it validates a file-scope
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
- **How the name check stays cheap.** Repeating every lookup per query cost
  about 50 M Ir on the unity self-host. Almost every marked name there has a
  symbol that no parameter, local or other non-file-scope entity shares. Such
  a name finds nothing in any scope before the file scope, so from any query
  scope whose chain reaches the file scope it resolves as it does from the
  file scope.
  - A region settles its marks once, at its first query that needs the check
    (`c_ast_types_lookups_settled`). Each mark is checked at the file scope
    against its binding, which must be a file-scope entity or none. The file
    scope's answer for a symbol is looked up once per lookup generation and
    kept beside the symbol table (`CAstTypeSymbol`). The scan skips flag
    words without a mark in a loop of its own and visits only the mark bytes
    of the rest. The settlement runs out of line
    (`c_ast_types_lookups_settle`), so the per-query check
    (`c_ast_types_lookups_hold`) saves one register, not five.
  - Up to 16 marks per region that a non-file-scope entity also spells are
    recorded. A query over one of them walks its subtree as before. A region
    with more walks for every query.
  - The settlement holds while the lookup generation
    (`c_parse_lookup_generation`) and the identifier use count are unchanged.
    The generation lives on the scope cursor. It moves with every entity added
    and every rollback that takes entities, scopes or identifier uses away.
    After that, queries walk.
  - The symbol table and a table of the scopes whose chain reaches the file
    scope are built once per generation, and are rebuilt only once the nodes
    settled since the last build reach the entity count.
- **What it accepts.** Each answer is a row that already exists: an entity's,
  member's, element's, return or typedef type, an operand's own row, an
  immutable scalar row, or an interned primitive or pointer row (see
  [Interned rows](#interned-rows)). The rules are the machine's own functions, shared
  through `c_internal.h` and applied to operand types the pass already holds:
  `c_parse_expression_arithmetic_type`, the promotion with a bit-field width,
  the operator precedence and the scalar conversion check.
  - Stage 1: identifiers bound to an object, function, parameter, local or
    enumerator; number and character literals; `.` and `->` on an unqualified
    struct or union (and on a const or volatile one once its member's
    [qualified copy](#qualified-member-copies) is shared); `[]` and unary `*`
    on an array or pointer; and a call whose callee is a bound function or
    function pointer that is not a builtin.
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
  - `++` and `--`, prefix and postfix (`c_ast_types_update`): the operand's
    own row, unpromoted, which is what the machine's `UPDATE` operation and
    its leaf's postfix update answer. Neither appends a row. With constraint
    checks the node is safe only where `c_parse_update_operand_modifiable`
    holds and the operand is not a struct, union or `nullptr`. For a lone
    bound identifier the pass reads that test from the binding and the row;
    any other operand runs it over the operand's tokens.
- **What it declines.** Every shape whose machine answer appends a row stays
  with the machine:
  - a qualified array element, and a member of a const or volatile struct or
    union whose qualified copy is not shared yet or may not be
    ([qualified member copies](#qualified-member-copies)), except such a
    member access queried alone, which is replayed (below);
  - an array operand of `+` or `-`, which decays;
  - a pointer or `nullptr` conditional;
  - a qualified operand whose unqualified row was never recorded;
  - a cast or compound literal to any other type name: a qualified typedef,
    a tag, a qualified or `restrict` pointer, or an array or function
    declarator. Their readers append rows that are not interned.
  - `&` or a type name whose interned rows do not exist yet when the body or
    initializer is typed;
  - a string literal as an operand. Its array row is not interned, because
    lowering gives each array row its own IR array type and `-g` describes
    every IR type. A query of one string-literal token alone is answered by
    replaying the machine's leaf instead (below), and a run of several tokens
    stays with the machine.

  An operand the machine scans but does not type (a cast's operand without
  constraint checks, a `sizeof` expression) must therefore hold no type name
  the tree did not accept. The typer also declines a `?:` whose range holds a
  top-level comma or assignment, which the machine splits at instead.

  It also declines shapes whose machine answers append few or no rows, but
  for which it has no rule:
  - an arithmetic, shift or `?:` operand that ends in `. name` or `-> name`
    without being a member node, such as `(T)s.f` or `x, s.f`, unless the
    machine's bit-field probe on those tokens provably fails (see
    member-tail widths below);
  - a member access over any other base kind, such as a cast or a compound
    literal;
  - a call whose callee is not a plain identifier, and a call of an unbound
    builtin other than a void one with no arguments;
  - an identifier with no binding in a body;
  - `_Generic`, a statement expression, a braced initializer list,
    `__real__` and `__imag__`, `true`, `false` and `nullptr`, and `&&label`.

  The census in audit
  [`2026-10-11T093336Z`](../../performance-audits/2026-10-11T093336Z.md)
  charges each declined shape its machine Ir on the unity self-host. It was
  taken before the `++` and `--` rule above, which answers its third-ranked
  shape.
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

  A query of one string-literal token alone, perhaps parenthesized, is
  replayed the same way. `S8()`'s length spells it `(("text"))[0]` inside
  `BUSTER_ARRAY_LENGTH`. The eager pass flags the node
  (`C_AST_TYPE_FLAG_STRING`) without accepting it, so every parent declines.
  `c_ast_types_answer` reports it from its decline branch as
  `C_AST_TYPE_STRING`, off the path ordinary answers take.
  `c_parse_expression_tree_string_answer` then makes the machine's root task:
  - it strips the parentheses;
  - it probes the memo for the token and, on a miss, calls the string leaf;
  - it rewinds the machine's scratch arena, which the leaf uses here, as in
    the cast replay.

  The replayed row is the answer. A leaf that fails is undone, and the query
  runs the machine. The leaf's element count already comes through
  `CParseResult.string_literals`, which decodes each narrow token once per
  unit. A count fast path in front of it measured as a loss (see the
  [measurement plan](#measurement-plan)).

  A query of a `.` or `->` alone, perhaps parenthesized, over a base the pass
  accepts (and safe, with constraint checks) and a const or volatile struct or
  union that the member rule did not accept is replayed too: its qualified
  copy was not shared when the body was typed, or may not be shared
  ([qualified member copies](#qualified-member-copies)). The machine types the
  base as the tree does, then makes two calls
  (`c_parse_direct_expression_postfix`):
  - `c_parse_member_type` on the qualified aggregate;
  - `c_parse_intern_qualified_type` on the member it finds, which shares or
    appends the member's qualified copy.

  The decline branch reports it (`c_ast_types_decline`) as
  `C_AST_TYPE_STRING` with the member node's kind, so the query function's
  code is unchanged. The replay probes the memo for the access's tokens, as
  the machine's root task does after stripping the parentheses. On a miss,
  `c_parse_expression_tree_member` makes the same two calls, and the row they
  return is the answer. The node is never accepted, so its parents still decline.
  Verify mode holds the replayed row, and any alignment record it inherits,
  to the machine's.
- **Constraint checks.** A checked query is answered only from a node whose
  operands are safe and whose own checked-mode rule cannot fire. For the
  binary operators that is the machine's operand rule. The cases it settles by
  spelling or compatibility pass only for the literal `0` against a pointer and
  for two pointers to one unqualified element row. A cast whose conversion
  rule is clean is safe, and so is a conditional whose condition is a safe
  scalar.
- **Member-tail widths.** For an operand that ends in `. name` or `-> name`
  but is not a member node (`a + s.f`, `c ? x : s.f`), the machine re-types
  the tokens before the final `.` with `c_parse_direct_expression_type` to
  read a bit-field width. The pass answers width 0 only where that probe
  provably fails without a new row (`c_ast_types_probe_fails`):
  - its base, before the first postfix step, types to nothing before any row
    is made;
  - or the base is one identifier whose postfix steps span an accepted
    member or index node, which reads no qualified aggregate or array.

  A parent over any other such operand (a cast, a comma, a qualified chain)
  declines.
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
`c_parse_validate_lowering_constraints` from the file-scope initializer walk
(`c_parse_validate_static_initializers`) through the loop over function
bodies, where the queries mint them, those builders go through
`c_parse_intern_type`. It returns the live row equal to the one it would
append (`CTypeInterning` in `c_internal.h`). The tree's answers for casts and
`&` read those rows, in initializers as in bodies (`c_ast_types_interned`), so
`S8()`'s `(char8*)("text")` at file scope answers with the replay. Until
[#3102](https://github.com/buster14a/buster/issues/3102)'s initializer slice,
the window opened only for the bodies, after the initializer walk, and every
such initializer shape declined.

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
- The window opens after every declaration has its rows, and the initializer
  walk and the bodies rewrite no declaration's row, so an interned row only
  ever replaces a later copy of itself. That copy resolves in the same
  lowering pass, and the IR types, and so the `-g` type entries, keep their
  order.

Interning declarations too is not unobservable. Lowering maps rows in passes,
in table order, and a struct resolves only once its members' rows are mapped.
A member's interned `char *` row moved ahead of its struct, the struct resolved
a pass earlier, and the `-g` type entries of the unity self-host came out in a
different order.

The same holds for an aggregate a type name defines. The declaration pass
registers every aggregate that a body or file-scope initializer type name
defines, qualified ones such as `(const struct { char *p; })` included, before
the window opens, and the machine reads its row back, so no member segment is
read inside the window ([#3380](https://github.com/buster14a/buster/pull/3380)).
A trap in `c_parse_type_interning_suspend` while the window is open never
fires on `c_test_type_interning_objects`' sources or on the unity self-host at
`-g0` and `-g`. CTypeInterning's `suspended` stays as a guard should a later
change define an aggregate inside the window: it counts the member segments
the machine is reading (`c_type_parse_aggregate_range_step`), nothing is
interned while any is in flight, and the machine's failure path takes back the
segments it discards. The history of this guard is in audit
[`2026-10-10T014058Z`](../../performance-audits/2026-10-10T014058Z.md).
`c_test_type_interning_objects` compiles three sources: body casts and `&`,
aggregates defined in body type names, and the same in file-scope
initializers (`S8()`-style casts, `&` of objects, aggregates defined in
initializer type names). It compiles each with the window shut (`c_test_set_type_interning_off`) and open,
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

### Qualified member copies

**Status: implemented under `-fc-ast-pilot` as option (b) below, at the
owner's request on [#3102](https://github.com/buster14a/buster/issues/3102).**
The machine side is `c_parse_intern_qualified_type`, the typer's lookup is
`c_parse_interned_qualified_type`, and the rules are in CTypeInterning's
comment (`c_internal.h`). The design and its prototype evidence follow as
they were accepted; the implementation's measurements are in the
[measurement plan](#measurement-plan).

`.` and `->` on a const or volatile struct or union, such as
`preprocess->tokens` with `preprocess` a pointer to const, is the largest
shape the typer declines: about 57 M machine Ir in the decline census
(audit `2026-10-11T093336Z`).
- Each such step in `c_parse_direct_expression_postfix` ends with
  `c_parse_add_qualified_type`. That appends a qualified copy of the member's
  row, and a copy of the base's alignment record when it has one.
- `c_ast_types_member` declines the node, because the row the machine would
  answer does not exist yet.

The measurements below are hosted Callgrind counts, diagnostic only, from
audit [`2026-10-11T101621Z`](../../performance-audits/2026-10-11T101621Z.md),
on main `4e5d563f`:
- **Copies.** Inside the interning window the machine appends 13,889
  qualified copies.
  - 12,946 come from the member step, and 150 more from it inside
    bit-field-width probes.
  - They copy only 609 distinct rows. 11,926 repeat a row first made in an
    earlier body or initializer.
  - None would inherit an alignment record, and none is restrict or atomic.
- **The memo.** The number of copies a run appends is not a function of the
  node.
  - A sub-range the per-body memo already holds appends nothing, and 881 runs
    read one partway through.
  - A bit-field-width probe re-types a member prefix outside the memo.
  - A checked query cannot read an unchecked query's entry. Of the 1,326
    ranges queried both ways in one body, 413 appended a different number of
    copies on the two runs.

The options:

| | Exact answer by | Rows | Cost and risk |
| --- | --- | --- | --- |
| (a) replay | re-walking the subtree in the machine's task order, probing the memo per sub-range under the query's flags, re-running the width probes and checked-mode extras, and appending each copy | unchanged | It re-implements the machine's traversal for one shape, and any drift is a different row index. It removes machine Ir only (ceiling about 49.9 M), and every copy, about 392 Ir each, is still made. |
| (b) intern under the pilot | sharing the copy, so a run over the subtree appends nothing whatever the memo holds | 13,836 copies become 609 shared rows | It adds new interned rows beside the primitive and pointer ones. Prototype: −74 M Ir (−0.80%) on `-fsyntax-only`, and −86 M on `-c`. |
| (c) decline | — | unchanged | No change. |

**Proposal: (b).** It is the only option that is exact by construction: once
every copy a subtree needs exists, no memo state changes what a run appends.
Its rules:
- **Scope.**
  - Only the member step's copy in `c_parse_direct_expression_postfix` is
    shared, and only while `CTypeInterning.enabled` holds and `suspended` is
    zero.
  - Every other caller of `c_parse_add_qualified_type` still appends:
    declarations, conditionals, `__real__`/`__imag__`, and subscripts of
    qualified arrays.
  - Outside the pilot nothing changes, because the interning header exists
    only with the tree.
- **Exclusions.** A copy that would inherit an alignment record, or that is
  restrict or atomic, is appended and never shared.
  - Lowering builds one aligned alias IR type per alignment record
    (`aligned_alias_types`), so sharing a copy that carries one could change
    the IR types.
  - `c_type_parse_root_finish` checks `restrict` only on appended rows.
- **Matching.** Shared copies keep their own log and slot table beside the
  primitive and pointer ones.
  - `c_parse_interned_row_matches` compares only kind, element and
    qualifiers, so it would take a qualified pointer copy for a plain pointer
    row.
  - A copy matches only a row equal in every `CType` field.
  - Its live count sits in `CParseResult` beside `interned_type_count`, so a
    rollback forgets what it removes.
- **The typer.** The member rule accepts a const or volatile aggregate when
  the exact copy already exists. It looks the copy up without minting it.
  Otherwise the node declines.
  - A copy first made in its own body (1,679 runs, 9.7 M) stays with the
    machine.
  - That family is below the code-layout noise #3384 measured
    ([PR #3384](https://github.com/buster14a/buster/pull/3384)), so the
    proposal adds no re-check for it.

**Why sharing stays unobservable.** The argument is the
[interned rows](#interned-rows) argument, extended to the
`has_unqualified_type` rows:
- A copy is never mutated in place, and the exclusions above give it no
  alignment record.
- Lowering maps every row with `has_unqualified_type` through its unqualified
  row:
  - const maps to the same IR type;
  - volatile goes through `c_ir_add_qualified_type`, which returns an
    existing qualified IR type.

  So a copy has no IR type, and no `-g` entry, of its own. Aggregate copies
  are not laid out separately, and array copies share their base's bound
  index.
- The window opens after every declaration has its rows. So a shared copy
  only ever replaces a later copy of itself, which maps in the same pass as
  its unqualified row.

**Prototype evidence.** The prototype is a throwaway patch, measured in the
audit:
- **Objects.** `-c` objects of the unity self-host are byte-identical at
  `-g0` and at `-g`, base and prototype, with and without the pilot.
- **Tests.**
  - `c_test_type_interning_objects` passes.
  - The corpus differential passes in verify mode.
  - The only failures are the two forms of the `CAstTypeCase` probe that
    expects `p->a` over `struct S const*` to decline.
- **Counters.** Declines fall from 24,919 to 17,448.
- **Where the saving comes from.** About half is the machine runs the tree
  now answers (−39.4 M). The other half is fewer whole-table layout solves
  over the smaller table (−39.0 M), and interning alone, with no typer
  change, already gets that half.
- **The default path** moves −0.0055%.

**What an implementation must show.** These budgets are declared in the
[measurement plan](#measurement-plan) for the implementation. The four arms
are those of stage 2:
- **Correctness.**
  - byte-identical `-c` objects at `-g0` and `-g` across the four arms, and
    with the window shut and open;
  - `c_test_type_interning_objects` extended with sources that read members
    through pointers to const and to volatile aggregates, nested aggregates,
    array members and an aligned member typedef;
  - no verify mismatch over the corpus, with a floor on qualified-member
    answers;
  - the decline probe for `struct S const*` turned into an answer probe,
    with a late-row decline probe beside it;
  - a mutation that shares a copy carrying an alignment record must fail a
    test.
- **The default path (D against A):** within ±0.05% Ir on `-fsyntax-only`
  and on `-c` at `-g0`.
- **The slice's effect (C against B):** fewer instructions on both
  workloads, with the lookups and the eager pass charged, and fewer machine
  runs from queries.
- **Acceptance:** Zen 5 (#2761) stays incomplete unless the owner asks for a
  9700X comparison, and the hook stays opt-in.

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
  range. That holds when the list sits inside a redundant group,
  `int (k(a))`, or after one, `int (k)(a)`, because the walker records the
  name's own list either way
  ([#3368](https://github.com/buster14a/buster/issues/3368)). Group boundaries are where a
  suffix derivation's inner declarator is a pointer. Redundant parentheses
  leave no node, so the split counts the `(` written before the name; more
  than the boundaries is a redundant group, which the walker reads the same
  way (below). A declaration without a declarator takes the last name outside
  every delimiter, else the first name inside one, which is what the walker
  does.
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
   walk. An identifier lane is classified as a type word or `return` from its
   interned id, with one bound compare and one `word_bits` read. A specifier
   run of at most four interned type words, with no tag keyword, is checked
   only of the run's own tokens, so its verdict depends on its ids alone. A
   run spelled like one that already validated clean is skipped. The
   walker's unmatched-`?` check (#4072) is not repeated: the split runs
   only over a tree that built, and the tree builds no `?` without its `:`.
2. **The walker would read a shape differently from the grammar, or no rule
   here states what it reads.** `CParserTreeFallback` names each case:
   - `specifiers`: a parenthesized specifier (`typeof`, `_Atomic(T)`,
     `_Alignas`, `_BitInt`), or an enum's fixed type;
   - `declarator`: a redundant group after a keyword other than a type or
     qualifier word, where the walker reads the group as a parameter list;
     attributes on a pointer or
     opening a group; or a function returning a function or an array;
   - `old_style`: an old-style declaration list, including one after a
     parenthesized name (`int (k(a)) int a; {...}`);
   - `tokens`: a comma operator in an initializer, where the walker splits a
     list, a token next to an anchor that is not the required one, or a
     variadic list inside a redundant group (`int (gv(int, ...))`), since the
     walker reads `...` only before the declarator's last `)`;
   - `assertion`: a file-scope assertion whose condition holds a comma or an
     initializer list, or a body assertion with no message or behind an
     attribute list.

A fallback discards everything derived and returns `c_parse_ast`'s result, so
the records and the diagnostics are always the walker's, and #3143 is not
changed by this split. Where the walker's reading is wrong but the split can
state it, the split reproduces it instead.
A declaration is a typedef or constexpr only through its own top-level
specifiers. The walker counts a `typedef` or `constexpr` word only outside
every delimiter and before the first top-level `=` or `,`. The split counts a
`SPECIFIER_WORD` item of the declaration's `DECL_SPECIFIERS`. A word inside an
initializer, such as a statement expression's `typedef` or a C23 constexpr
compound literal, marks neither the declaration nor its later declarators
([#3310](https://github.com/buster14a/buster/issues/3310)).

**Redundant groups ([#3215](https://github.com/buster14a/buster/issues/3215)).**
The walker used to classify a parenthesized declarator by the token after its
outermost group and to require a `*` before a nested one. So it read
`int ((*pq))(int) = f;` as a function, merged `int (gd(int a)) { ... } int zz;`
into one object, and dropped `int ((x))[3];` and `void ((h))(void) {}`.
Now it walks out from the name instead
(`c_parse_parenthesized_declarator_is_function`): a `(` makes a function, a
`[` an array, and a `)` closes a group that is a pointer when it holds a `*`
and is redundant otherwise. A name whose own list sits inside the group,
`(gd(int a))`, is a function, but only when the group follows a type or
qualifier word, a punctuator or nothing (`c_parse_declarator_group_may_open`).
After a typedef or tag name it is a group too
([#3368](https://github.com/buster14a/buster/issues/3368)):
`c_parser_type_name_before_group` answers for a tag after `struct`, `union`
or `enum` (`c_parse_tag_name_before_group`), and for a typedef name in the
type's position (`c_parse_typedef_name_position`: first in the declaration,
or after storage-class, function-specifier and qualifier words and
declaration decorations only). A decoration is recognizable only from its
head, so when the backward walk over those words stops at a `)` or `]`, the
prefix is walked again forwards from the declaration's start, stepping over
`__attribute__((...))`, `__declspec(...)`, `[[...]]` and `__extension__`
(`c_parse_skip_declaration_decoration`); `int f(void)` stops at `int` and
never walks forwards. The
scans have no binding table, so `c_parse_ast_run` records each file-scope
typedef record's name in `CParserTypedefNames`, a set it asks only in that
position; at file scope a typedef name cannot be redeclared, and block-scope
typedefs never reach these scans. `T (x);`, `struct S (s2);`,
`T (tg(int a)) { ... }` and `struct P (pg(void)) { ... }` therefore declare
`x`, `s2`, `tg` and `pg`, and so do decorated forms such as
`static __attribute__((a)) T (y);` and `[[maybe_unused]] T (z);`. The split reads the
same position, with the grammar's `TYPEDEF_NAME` in place of the set, so its
records match. The type parser
(`c_type_parse_parenthesized_step`) nests a group that opens a group holding
the name. The split publishes every redundant group the walker reads this way,
and refuses the rest as above.

`c_ast_test_split` runs one shape per fallback reason in every layout, and
both #3215 inputs, the #3368 shapes and the other redundant shapes on the
published side. It
requires the walker's result and the named reason. The [corpus differential](#corpus-differential) compares every
record. On the self-host unity input the split publishes all 15,593 records
and 33 body assertions, with no fallback. The fixtures that fall back are the
parenthesized specifiers, the fixed-type enums and a few attributed
declarators.

## Statement plan from the tree

The body binder (`c_parse_bind_function_body` in `c_parse.c`) walks each
function body token by token: `c_parse_bind_block_statements` opens and closes
scopes, finds where each block declaration ends and hands it to
`c_parse_local_declarations`, finds a `for` header's first `;`, its `)` and
the loop's end (`c_parse_statement_end`), and binds every identifier use.
Under `-fc-ast-pilot` it follows the body's **statement plan**
(`c_parse_statement_plan_build`), read from the tree's `COMPOUND_STATEMENT`
items before the walk. The token loop stays the engine and the authority; at
given tokens the plan tells it what it would otherwise scan for:

- **Segments.** An expression, `return`, `goto`, `break`, `continue` or null
  statement, an `if`, `while` or `switch` header, a `case` or `default` label,
  a do-while tail, a `for` header after its first clause, and an `else` or
  `do` keyword, when its subtree holds only plain syntax: expressions, and
  type names without a member or enumerator list, braces or attributes.
  Adjacent segments merge. The loop binds a segment's identifiers in one pass
  (`c_parse_bind_plan_segment`) with its own predicates: the type-start probe
  at a statement start, the label test and the use filter. Nothing in a
  segment can open a scope, declare, or reach any other path of the loop, so
  the loop's per-token chain of tests (attributes, asm ranges, aggregate
  definitions, braces, `for`) is skipped. A segment is handed back to the loop
  at the token where the loop would leave plain expression handling: a type
  start, which the loop reads as a declaration, or `__builtin_offsetof`. One
  is not entered while a state the plan cannot see reaches into it: a pending
  GNU typeof declaration, a live asm range, an attribute resume point, or the
  end of a statement scope.
- **Braced segments.** An expression statement, `return`, `if`, `while` or
  `switch` header, `case` value or do-while tail whose subtree also holds
  compound literals, initializer lists and designators, and at most 16 lists,
  is planned as segments split at each list's `{` and `}`
  (`c_parse_plan_braced`). The loop gives a compound literal's `{` a block
  scope and starts a statement after each brace, so it still reads every
  brace itself and the segments between them carry its state as before. A
  list's `}` follows its last item's last token, that token's closers and an
  optional `,` (an empty list's follows its `{`); lists come in node order,
  so an inner list's `}` is known before the outer one's, and the last-token
  rule stops at a list on the rightmost chain. A `{` or `}` the tokens do
  not confirm leaves the statement to the loop. These compound literals,
  mostly the codebase's `S8("...")`, were 9,629 of the 10,378 statements the
  plan declined on the self-host unity input.
- **Declaration hints.** A block `DECLARATION` gives its `;`, so the loop's
  scan for it and its search for a GNU typeof statement expression are
  skipped. It also carries a **declarator hint** per `INIT_DECLARATOR`
  (`c_parse_plan_declarators`): the `,` or `;` past the declarator's greatest
  anchor and closers, the `=` past the closers of the child before its
  initializer, and whether that initializer is plain (expressions, type names
  as above, initializer lists, designators and compound literals). It also
  carries two specifier facts:
  - the storage-class words its `DECL_SPECIFIERS` items spell, folded by the
    split's own test. In a declaration the tree accepts, no such word stands
    outside those items and outside every delimiter.
  - whether an `auto` or `__auto_type` word stands where the auto-inference
    scan reads: any node of the specifiers, or of the first declarator up to
    its initializer. A parameter's specifier counts, as in
    `int (*f)(__auto_type x);` ([#3550](https://github.com/buster14a/buster/issues/3550)).

  The split (`c_parse_planned_local_declarations`, the hinted caller of the
  always-inline `c_parse_local_declarations_split`) then skips:
  - the storage-class scan of the whole declaration;
  - the two auto-inference scans of a declaration that cannot infer;
  - the `,` and `=` scans of each declarator whose hinted tokens it confirms;
  - in a plain initializer, the statement-expression, type-definition,
    attributed definition and attribute tests. `__builtin_offsetof` is a call
    in the tree, so its test stays, as does the use filter.

  A hint the tokens refuse (`declarator_declines`) returns that declarator
  and the rest of the declaration to the scans. The specifiers and
  declarators themselves are still read by the type machine from tokens;
  that is the next row's.
- **Loop hints.** A `FOR` gives its header's first `;` and `)` and one past
  its body, in place of the loop's scans and `c_parse_statement_end`.

Every boundary comes from node anchors and is checked against the token the
grammar puts there:
- an expression statement's anchor is its `;`, and its first token is the
  least anchor of its expression, widened over wrapping parentheses (the token
  before a statement ends the previous one and is never `(`);
- an expression's last token is the greatest anchor on its rightmost chain
  (children come in source order and a node's last child is the node before
  it), followed only by closers, then the `;`, `:` or `)` the statement needs;
- a declaration's or `return`'s `;` is found that way only when its subtree
  holds no member list, enumerator list or statement expression, whose own
  `;` could come first;
- a statement's last token follows the last-child chain through compound
  statements (each adds its `}`) and the statements that end in a
  sub-statement, memoized per node so nested statements share one descent.

A statement whose rule cannot state its boundaries, and every label, asm
statement, attributed statement, static assertion, nested function and
statement whose subtree is neither plain nor braced, gets no entry, and the
loop reads it as it always has. Labels, asm statements and static
assertions were 47 of the declined statements on the self-host unity input,
too few to repay a rule. A token the grammar fixes next to an anchor that is not that
token sends the whole body to the loop, as does a body the tree does not have;
`CAstStatementFallback` names the two cases (`tokens`, `input`). The plan is
built by one preorder walk over statements on an explicit stack, so entries
come out in token order; each expression is scanned once, by the statement
that owns it. The loop consumes them with one cursor, through a single
next-event test that replaces its pending-typeof test, so a walk without a
plan does the same work per token as before.

The scopes, entities, identifier uses and diagnostics are the loop's own,
because the plan only skips work whose outcome it states: the
[corpus differential](#corpus-differential) compares every identifier use,
the per-token use map, every scope and every entity of the analysis with the
plan against the analysis without the tree. `c_ast_test_statements` runs one
body that takes every kind of entry, one whose statements the plan leaves to
the loop (with an `__builtin_offsetof` bail), and one tampered tree per
fallback reason, in every layout, a body of block declarations whose
declarators take every hint, with a tampered declarator whose `=` hint the
split refuses, and a body with a compound literal in every braced position
(each designator kind, nested, empty and over-cap lists, a trailing comma, a
bail inside a list), with a tampered list anchor that leaves its statement to
the loop. On the self-host unity input every body follows its plan, with
no fallback, no bail and no refused declarator hint.

## Declaration specifiers from the tree

Semantic analysis types a declaration's specifier run with the type machine
(`c_parse_scalar_type_in_scope_context`). It validates the storage classes word
by word, scans the run for `typeof` and `_Atomic(T)`, pushes a scalar frame and
a core frame, scans the run again for a tag and a typedef name, and only then
builds the one row the run names. Under `-fc-ast-pilot`, three callers read the
run from the declaration's `DECL_SPECIFIERS` node instead
(`c_parse_declaration_specifiers_type` in `c_parse.c`):
- block declarations (`c_parse_local_declarations`);
- file-scope declarations (`c_parse_declaration_type`);
- parameters (`c_parse_parameter_segment`).

The tree read applies only to a **plain list**. Every child is a
`SPECIFIER_WORD` or `TYPEDEF_NAME` leaf on its own token, one after another
from the run's first token. The list names either primitive type words or one
typedef name, never both. The only other words allowed beside them are
`const`, `volatile`, `inline`, `_Noreturn` and the storage classes `typedef`,
`extern`, `static`, `register` and thread-local. For such a list:
- **Storage.** The machine's verdict (`c_parse_storage_classes_valid`) is
  computed from the set of words. A repeated or conflicting class falls back,
  so the diagnostic stays the machine's.
- **Primitive words.** The row is the one `c_parse_primitive_type` interns. It
  comes from the machine's own primitive scan
  (`c_parse_primitive_type_read`) run over the list's words alone, so every
  spelling combines as the machine combines it.
- **Typedef name.** The row is the name's entity, looked up as
  `c_parse_qualified_typedef_type` looks it up. When a `const` or `volatile`
  word is in the list, it is qualified with `c_parse_add_qualified_type`, as
  there.
- **Declarator start.** The declarator starts one past the list's last word.

Rows are appended only by the machine's own calls, in the machine's order:
`c_parse_type_identity_prepare`, then `c_parse_intern_type` or
`c_parse_add_qualified_type`. So the type table matches the machine's row for
row.

The lists are found through an index, built once per analysis, of the first
token of every **word-only** `DECL_SPECIFIERS` node: a list whose children are
`SPECIFIER_WORD` and `TYPEDEF_NAME` leaves on consecutive tokens. Only such a
list can be read, and no other is indexed. A word-only list holds no other
list and lies in no other word-only list, so postorder reaches these lists in
token order. The index is therefore appended in one pass, with no sorting. A
list out of that order, which a valid tree never has, is left out rather than
moved. So the build reads each list's children at most once, whatever the
input. Indexing every list and sorting as it went was quadratic in tag
nesting: postorder closes an enclosing declaration's list after every list
its members hold. The kind column is scanned eight nodes at a time. A lookup
gallops forward from the previous answer, because each of analysis's passes
types declarations in token order.

Everything else goes to the machine unchanged. `CAstSpecifierFallback` names
each case:
- `input`: no word-only list begins at the run's first token, because the list
  there holds a tag, `typeof`, `_Atomic(T)`, `_Alignas`, `_BitInt` or an
  attribute, or because there is no list;
- `specifiers`: a word outside the plain set (`restrict`, `_Atomic`,
  `__extension__`, ...), no type at all, both a type word and a name, or a word
  right after the list;
- `storage`: a repeated or conflicting storage class;
- `primitive`: words the machine's scan combines into no row, which it
  diagnoses. The syntax pass rejects such combinations first, so only a tree
  that classifies a word differently from the scan reaches this;
- `typedef`: a name that is not a typedef in the run's scope, or a
  restrict-qualified typedef under a qualifier, whose copy
  `c_type_parse_root_finish` checks again.

The machine remains the default path and the fallback for every run that is
not read from the tree. A census of the root runs it hands back found no
shape a read clears the gate with ([root specifier fallbacks](#root-specifier-fallbacks)).
Members' plain runs are read too ([members](#members));
the rest of a tag definition, declarator derivations and the type names typed
inside machine frames (casts among them) still go through the machine.

### Type names

`c_parse_type_name_specifiers` reads the specifier run of a type name for
the validators: every grouped `sizeof` and `_Alignof` operand
(`c_parse_validate_sizeof_operands`), every `( ... )` written before a `{`
(`c_parse_validate_compound_literals`), and the smaller callers'
type names. Many of those runs are not type names at all: `sizeof (x)`,
`if (c) {`, `switch (x) {`. The machine attempts each one, fails, rolls back
and keeps no row. Under `-fc-ast-pilot`, when the machine has no frame open,
the tree settles the run first (`c_parse_tree_type_name_type`):
- **Skip.** No `DECL_SPECIFIERS` node of any kind begins at the run's first
  token, so no type name begins there. The specifier index's pass sets one bit
  per list's first token for this. The machine's attempt is not run; its
  failure path is, in the order `c_parse_scalar_type_in_scope_context` runs
  it. `c_parse_type_identity_prepare` settles the range's identity answers,
  and `c_parse_define_typeof_aggregates` defines the bodies written in a
  `typeof` operand there. If it defines one, the machine's retry
  (`c_parse_scalar_type_in_scope_attempt`) reads the run, as there.
- **Read.** A plain list begins there, and `c_parse_tree_specifiers_type`
  reads it exactly as it reads a declaration's.

Every other run goes to the machine. A run with a frame open, which includes
the type names typed under a type-mode constant evaluation, is counted under
`input`, and the other fallbacks keep their `CAstSpecifierFallback` reasons.
A failed attempt keeps the diagnostics it reported and rolls everything else
back. The member index's generation is the one exception: the next member
lookup rebuilds that index, and no answer changes. So the skip is exact when
the attempt reports nothing on such a run. Verify mode
(`c_test_ast_type_names_verify_set`) runs the attempt on every skipped run and
counts any that answered, kept a row or reported a diagnostic. The
[corpus differential](#corpus-differential) requires none, over the fixtures
and the frontend sources. No input tried has the failure path define a
`typeof` body, because earlier passes have defined it; no input tried
has its identity answers settled first by a skipped run either. Both calls
stay, so that the skip is the machine's failure path, not a guess about it.

`c_ast_test_type_names` runs, in every layout, these units:
- one unit whose type names are all read;
- one whose runs are skipped, including one that holds a `typeof` aggregate;
- one shape per fallback reason, the `storage` and `primitive` shapes on
  tampered trees;
- a tree whose type name's list is moved off its first token. Verify mode must
  catch the machine answering that skipped run. This is the mutation check.

`-v` prints `C_AST_TYPE_NAMES`.

`c_ast_test_specifiers` runs, in every layout, one unit whose block,
file-scope and parameter runs are all read from the tree, and one shape per
fallback reason. One `input` shape and the `primitive` shape use a tampered
tree. A unit with tags nested a thousand deep must index its one word-only
list, reading no more list children than the tree has nodes. The
[corpus differential](#corpus-differential) compares the whole analysis model
field by field.

### Members

A tag definition's body is typed by the machine one member declaration at a
time: each aggregate segment frame pushes an alignment frame, then a scalar
frame and its core frame for the member's specifier run, then reads the
declarators. Under `-fc-ast-pilot` the segment frame reads that run from the
member's `DECL_SPECIFIERS` node instead (`c_parse_member_specifiers_type`)
and pushes no scalar frame. The read is `c_parse_tree_specifiers_type`, the
declaration read above, on the same plain lists with the same rows and
fallback reasons. Two things differ:
- it runs inside the segment frame, in the tag's scope, rather than with no
  frame open. A type-mode constant evaluation still hands every run back as
  `input`;
- like the scalar frame it replaces, it settles no identity answers. The
  answer is left in the machine's result, where the scalar frame's
  completion leaves it, and the segment frame resumes at its next stage.

Everything else in the body stays with the machine: the segment frames and
their checkpoints, alignment frames, declarators, bit-field widths and layout
attributes, and every member run that is not a plain list (a nested tag,
`typeof`, `_Atomic`, an attribute). The
[census of that remainder](#tag-definition-machine-work) found no shape a
tree read could take above the gate. Those runs get the scalar frame, counted
under their `CAstSpecifierFallback`. Plain casts, typed by a scalar frame
inside a parenthesized expression leaf, stay with the machine: the
[census](#member-specifier-runs) put them below the gate.

Verify mode (`c_test_ast_members_verify_set`) has the scalar frame read every
run first. It rolls that read back, then compares the type row, its fields
and the declarator start with the tree's answer. The
[corpus differential](#corpus-differential) requires 0 mismatches, as well
as the field-by-field model comparison.

`c_ast_test_members` runs, in every layout:
- a unit whose members are all read: primitive words, typedef names,
  qualified ones, a member list holding several declarators, and members of
  a nested definition;
- one shape per fallback reason a member reaches, the `primitive` shape on a
  tampered tree;
- a mutation check: a tree whose member word names another type must make
  verify mode report the mismatch.

`-v` prints `C_AST_MEMBERS`.

## Statements from nodes in lowering

Lowering's statement walker, `c_ir_lower_body_advance` (`c_gen.c`), reads a
function body from its statement nodes (#4060, step 2 of the
[owner direction](https://github.com/buster14a/buster/issues/3102#issuecomment-6144374034)).
It has no token path and no fallback: a body without a COMPOUND_STATEMENT
node is refused with `function body has no syntax tree node`.

**The tree.** The walker consumes the tree retained by completed semantic
analysis ([retained facts for lowering](#retained-facts-for-lowering), #3102
step 1). Every production compile builds that tree before analysis; lowering
does not build a missing tree or recover a body from a source range.
Because the walker has no token path, a unit with a function body whose
syntax the builder rejects fails with the builder's diagnostics: a malformed
GNU case range (`case 1 ... 2 ... 3:`) is the builder's syntax error rather
than lowering's. A model-only result has not established lowering authority.

Lowering every body from the tree surfaced two builder gaps, which also
failed `-fc-ast-pilot` and are closed with it:
- a vendor attribute's argument clause is a balanced token sequence (C23
  6.7.13.2). Under a namespace other than `gnu` or `clang` it is stepped
  over to its closing parenthesis, and the ATTRIBUTE_SCOPED node keeps its
  namespace alone (`[[vendor::tag({...})]]`);
- `__fp16`, Arm's storage-only half, is a type word (`C_AST_WORD_FP16`). The
  tree's specifier readers leave it to the type machine, as they leave
  `__float128`.

Each finished declaration names its exact FUNCTION_DEFINITION owner. Lowering
enters that definition's last COMPOUND_STATEMENT child and checks its brace
anchor against the declaration's provenance. A missing, mismatched or
non-definition owner refuses; source positions never choose a replacement
node. A statement-expression frame similarly enters its known block child.
The `c_ast_test_function_body_owners` fixture covers ordinary, inline, extern,
attributed and function-pointer-return definitions, local/nested record
definitions, all tree layouts and missing or tampered owners.

**Tasks.** The body is a stack of `CIrBodyTask`s, as before. Each task now
holds a node cursor: the comma operands of a split expression statement,
then one statement (`head_plus_one`), then the items of a block
(`CIrNodeList`, `item` to `item_count`), stopping at the next case label of
its switch that owns statements (`stop_plus_one`). The walker takes a
statement from the cursor and dispatches on its kind:
- **Blocks, `if`, loops and `switch`** split the task exactly as the token
  walker did, in the same block-creation and push order, so the IR is
  unchanged. A controlled substatement that is a bare block hands its items
  over; anything else, a block under labels or `[[...]]` included, is lowered
  as one statement.
- **Labels and case labels** are prefixes: the walker enters the label's
  block and then lowers the statement it carries.
- **`switch`** collects its case labels from the CASE, CASE_RANGE and DEFAULT
  nodes of its body, nested switches excluded (`c_ir_lower_switch_cases`). A
  label reached from a body item through label prefixes alone owns the
  statements up to the next such label; any other is a dispatch target inside
  them (`if (c) case 1: ...`, Duff's device). When the first label stands
  inside a statement (`switch (x) { if (y) case 1: ...`), that statement and
  the items after it, up to the next label that owns statements, lower from
  a block no edge enters, and the dispatch reaches the label inside them. The
  token walker flattened that shape. A first label inside a block is still
  refused, by semantic analysis's lowering-constraint mirror (`c_parse.c`).
- **Dead code** after a terminator is skipped statement by statement unless
  the statement holds a label (`c_ir_node_holds_label`). Such a statement, a
  block included, lowers whole from a block nothing branches to, as main's
  token walker came to do (`dc44eeb7`); before that, the token walker jumped
  into the middle of the statement, which miscompiled a `goto` into a loop
  body after dead code.
- The label table, the direct-SSA jump targets, the statement-expression
  enclosures and the address-taken labels come from LABELED, GOTO, SWITCH,
  CASE and LABEL_ADDRESS nodes of the body's span.

**Provenance.** Tokens are read only as provenance: an anchor's source
location, a label's spelling, and the task tokens that cleanup and loop
targets ask the parse result's scopes about. A statement's extent comes from
its siblings: the items of a block tile its tokens, so an item ends where the
next starts (`c_ir_node_list_end`), and the last at the block's `}`.

**Child handoff.** Expressions, declarations, type constructors and assembly
carry exact nodes through their frames. Source spans retain the original debug
and diagnostic extents; they never select a structural node. Cleanup and loop
targets query finished scope intervals at the recorded source positions.
`-v` reports `statement_bodies` and `statements` in `C_AST_LOWER`; the retired
range-adapter counter has no remaining producer.

Tests: `c_ast_test_statement_nodes` lowers one body with every
controlled-substatement shape alike in every layout and with or without a
passed tree, and refuses a tree whose body brace or `if` anchor is moved. `c_test_statement_nodes_kinds`
and `c_test_statement_nodes_deep_nesting` (blocks, `if` blocks, an `else if`
chain, a braceless loop nest, a run of labels and nested statement
expressions, thousands deep) lower to certified IR, and the driver fixture
`basic_c_statement_nodes.c` (`src/buster/tests/compiler/driver/fixtures/`)
runs every statement kind under both register allocators.

The extent table that answered controlled-body ranges from the tree before
this (#3495) is deleted with the token walkers it fronted.

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
The redundant-parenthesis misreads it found
([#3215](https://github.com/buster14a/buster/issues/3215)) are agreement
constructs too, beside the other redundant shapes: objects and functions in
one or more groups, a definition whose list sits inside its group, groups in a
declarator list, after a typedef name and in parameters. The single groups
after a typedef or tag name
([#3368](https://github.com/buster14a/buster/issues/3368)) joined them, and
so did the old-style definitions whose name sits in a redundant group, with
and without a declaration list.
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
runs must end with the same diagnostics and the same type-table sizes. The
second run also follows the [statement plans](#statement-plan-from-the-tree),
so the two must also bind alike, field by field
(`c_ast_corpus_bindings_differ`): every identifier use in order (token, entity,
scope), the per-token use map, every scope and every entity. There are floors
on the segments the plans bound and the uses in them: about 6,800 segments and
25,700 uses from the fixtures, and about 34,300 and 129,400 with the frontend
sources. No corpus body falls back. The second run also reads declaration
specifier runs from the tree ([declaration specifiers](#declaration-specifiers-from-the-tree)),
so the two analyses must also build the same model, field by field
(`c_ast_corpus_model_differ`): every type row, array bound, alignment
specifier, type alignment, member, parameter, entity and declaration type,
in table order. There are floors on the runs read from the tree: about
19,800 from the fixtures, and about 60,000 with the frontend sources. Every
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

Beside the specifier model comparison, the runs without and with the tree
must also end with the same whole analysis model, field by field
(`c_ast_corpus_models_differ`). It compares every row
of the type, entity, scope, array-bound, alignment, declaration,
identifier-use, member, enum-member and parameter tables, with names compared
by content; entities include their merged inline hints, and enumerators
their declaration-point integer constant field by field. That is about
125,700 rows from the fixtures and 497,400 with the
frontend sources, and floors guard both counts. `c_ast_test_declaration_hints`
holds the declaration hints' specifier facts to the walker in every layout:
storage classes in each position the walker folds, and auto words in the
specifiers and the first declarator. It also mutates a copy of one analysis in
an entity's inline hints and in each field of an enumerator's constant, and
the comparator must name each mutated field.

The compiler sources are preprocessed against the host's C library, so the
differential also covers glibc's headers. It caught `__float128`, which glibc
2.43 uses in `typedef __float128 _Float128;` for the compiler identity this
preprocessor reports. `__float128` is now a builtin type word.
`_Float128`, `_Float64x` and `_Float128x` stay identifiers that a header may
typedef.

## Lowering from the tree

Under `-fc-ast-pilot` the driver hands its tree to lowering, in
`CIRLowerOptions.syntax_tree`, and releases it at the end of the unit, after
the backend. Without the pilot, lowering builds and releases the unit's tree
itself ([retained facts](#retained-facts-for-lowering)). Either way lowering
reads nodes only while `c_lower_to_ir_with_options` runs, and no IR record,
type or source range refers to a node.

**Root conditionals.** `c_ir_root_conditional` decides whether a token range,
after its outer parentheses are stripped, is a `?:` at the root. If it is, it
reports the `?` and the `:`. The expression, condition and logical-value steps
and the query machine ask this about 800 k times on the self-host.

Since step 3 (c) it answers from the range's node (`c_ir_tree_range_node`) and
its `CIrHoistFact` byte, which encode the token scan's own rules:
- **False** unless the node's root region holds a `?` (`ROOT_QUESTION`) and no
  assignment comes before the first root `?` (`ROOT_ASSIGNMENT`). An
  assignment there makes the conditional its right operand (Lua's
  `luaV_fastgeti` relies on this precedence).
- **True** otherwise, with the stripped range, and the `?` and `:` of the first
  conditional of the root region. The descent follows the one child that holds
  the first root `?`, so it costs the root region's depth. As before, a
  conditional after a root comma counts, and an empty middle operand counts
  only in a GNU dialect.

The `:` is the token after the middle operand's widened span, or the one after
the `?` of a `CONDITIONAL_OMITTED`. `CAstLowerStatistics` counts the answers,
the negatives and the descent steps, and `-v` prints them as a `C_AST_LOWER`
row. `c_test_tree_root_conditional_chains` holds the steps linear as chains
double.

The first slice's per-body index of conditional records
(`c_ir_tree_conditionals_build`, its first-token memo and its three-way
verdict), the token scan it fell back to (`c_ir_root_conditional_scan`) and the
`?` count of the token prefix table are deleted.

## Expression lowering from the tree

**Status: (a) to (c), CALL record identity and control group identity are
implemented. Statement-expression, place, `EXPRESSION`, evaluator-core,
condition, logical-value and conditional-value owners retain their exact nodes
through their child handoffs and continuations. Type, constant, bound and
initializer owners likewise consume exact child nodes and finished facts;
the temporary range adapters are deleted.** This is
step 3 of the [owner direction](https://github.com/buster14a/buster/issues/3102#issuecomment-6144374034)
([#4059](https://github.com/buster14a/buster/issues/4059)). It lands as a
short stack, each part byte-identical on its own:
- **(a) hoisting from the tree**, below in [Hoisting from the tree](#hoisting-from-the-tree):
  both prepasses read nodes, and their token scans are deleted;
- **(b) the evaluator's walk**, below in [The evaluator's walk](#the-evaluators-walk):
  the postorder walk replaces the evaluator's shunting-yard loop and
  `c_ir_expression_core_capacity`;
- **(c) root splits and operand ends from the node**, below in
  [Root splits and operand ends](#root-splits-and-operand-ends): the
  `EXPRESSION`, condition, logical- and conditional-value frames split at
  their node's root operator, calls split their arguments from the `CALL`
  node, and the root-split, group-fact and operand-end token walkers are
  deleted.

Prepared calls now use their `CALL` or `GENERIC_SELECTION` node as their
identity. `c_ir_prepared_call_find` reads one zero-initialized row slot per
node of the function-definition subtree, including parameter VLA bounds
before the body. Discovery rejects an already prepared node, the evaluator
consumes that exact node's result, and a `typeof` replay releases its slots
with its preparation rows. Source tokens still supply names, source ranges
and the unchanged call emission order. Returned-call chains no longer need
callee-token chains or a reference scan: their inner and outer calls have
distinct nodes even when their source starts coincide.

Place and type-query owners retain their exact subtree and prepared CALL
identity. Argument queries use cached CALL children. The range-only entry
boundaries and standalone argument-separator query adapter are deleted.

The rest of this section is the design that (a) to (c) follow. It describes
the token path as it was before (a).

### What the token path does

The expression machine in `c_gen.c` lowers one token range in two stages.

1. An `EXPRESSION` frame (`c_ir_lower_expression_step`) splits the range at
   its root comma, assignment, conditional or logical operator. It pushes the
   frames that own those operators and their operands.
2. What is left goes to an `EXPRESSION_CORE` frame
   (`c_ir_lower_expression_core_step`). Before it evaluates anything, it runs
   two prepasses over its own range:
   - `c_ir_prepare_control_expressions_step` finds parenthesized `?:`, `&&`,
     `||`, assignment and comma groups, and evaluates each of them first;
   - `c_ir_prepare_calls_step` finds calls and hoists them, so the evaluator
     sees each call as one finished value.

   It then sizes its stacks (`c_ir_expression_core_capacity`) and walks the
   tokens shunting-yard style. At each operand it asks structure questions:
   - `c_ir_unary_expression_end`, for a postfix `++`/`--`;
   - `c_ir_matching_delimiter_cached`, for each `(`;
   - `c_ir_group_type_name`, for a cast;
   - `c_ir_group_fact` and `c_ir_has_root_comma`, for a nested control group.

   Every call argument and every nested control group becomes another core,
   which prepares its own range again.

The census is audit
[`2026-10-11T101741Z`](../../performance-audits/2026-10-11T101741Z.md): a
hosted Callgrind count on the unity self-host, `-c`, with the pilot on. It is
diagnostic only. It splits the evaluator's 1.88 G inclusive Ir three ways:

| Part | Ir | What it is |
|---|---:|---|
| Structure questions | 676 M | Prepasses 465 M, postfix probe 102 M, capacity 39 M, operator and group classification about 70 M. They decide what is hoisted and where an operand ends, but append no row themselves. |
| Emission and its lookups | 835 M | Places, operations, loads, constants, literals, enumerators, source ranges. A tree walk must do this too. |
| The evaluator's own body | 371 M | Per-core setup and stacks, about 130 M; the per-token arms. This part is mixed. |

About 296 M of the structure cost is a range that holds nothing to find:
- 93.6% of control prepasses;
- 72.4% of call prepasses;
- 99.9% of postfix probes.

An exact negative answer for those needs no tree: per-body prefix counts of
the candidate tokens give it. That default-path change is
[#3445](https://github.com/buster14a/buster/issues/3445). The tree slices
below are budgeted net of it, so the tree is not credited with that saving.

### Emission order

Objects must stay byte-identical at `-g0` and `-g`. A walk over nodes must
therefore append the same IR rows, with the same source tokens, in the same
order as the token path. The token path's order for one core is:

1. **Hoisted control groups**, in the token order of their `(`. These are
   the parenthesized `?:`, `&&`, `||`, assignment and comma groups that the
   control prepass records.
2. **Hoisted calls**, in the order the call prepass returns (then `c_ir_prepare_calls_discover`).
   That order is the postorder of the hoisted calls' nesting forest: a call
   nested in another call's callee or arguments comes first, and siblings
   come in source order. That is the order in which `CALL` nodes occur in
   the tree's postorder, restricted to the hoisted ones.
3. **Everything else in postorder.** Shunting-yard appends each operand when
   it reaches its first token, and each operator when it reduces it. That is
   the postorder of the core's operator nodes: an operator follows its
   operands, and operands come in source order. A hoisted node in that
   postorder contributes its finished value and is skipped whole: the walk
   jumps over its interval by its extent.

Several constructs are lowered as one unit by an owner frame, which keeps
its own internal order:
- a postfix chain on a name (`a.b->c[i]`), through the place machine;
- a cast, `sizeof`, a compound literal, a statement expression;
- an operand of `++`/`--`.

In the tree each of these is one subtree. The walk hands the owner frame
that subtree instead of a token range.

Postorder storage is therefore not eager evaluation. The walk decides what
to hoist on enter events, before it visits any operand, and it emits in
the order above.

### Traversal frames and contexts

The walk is one explicit stack of frames (`c_ast_walk_next` enter and exit
events, or an equivalent private stack). Each frame holds:
- the node;
- its child cursor;
- a context word.

A frame for each kind sets its context on the enter event, before its first
child is visited:

- **Lazy.** These operands run only when a branch selects them:
  - the right operand of `LOGICAL_AND` and `LOGICAL_OR`;
  - the second and third operands of `CONDITIONAL`, and the last operand of
    `CONDITIONAL_OMITTED`;
  - the expressions of `GENERIC_ASSOCIATION` and `GENERIC_DEFAULT`;
  - the arms of `__builtin_choose_expr`;
  - a `STATEMENT_EXPRESSION` body.

  Nothing inside a lazy operand is hoisted. Its owner frame
  (`LOGICAL_VALUE`, `CONDITIONAL_VALUE`, the selection frames) lowers it
  inside the block the branch selects, as today
  (`tests/basic_c_lazy_operand_argument.c`).
- **Unevaluated.** These operands emit nothing and hoist nothing:
  - the operand of `SIZEOF_EXPRESSION` and `ALIGNOF_EXPRESSION`. A variably
    modified operand is the exception: the `sizeof` owner lowers it in a
    child frame;
  - `TYPEOF` and `TYPEOF_UNQUAL`;
  - the controlling expression of `GENERIC_SELECTION`;
  - the type operands of `__builtin_types_compatible_p`.
- **Sequenced.** A call to the right of a comma is lowered with its
  expression, not hoisted past the left side. Today the prepass's
  `comma_sequence_depth` rule does this. It records the depth of the first
  comma it meets, counting parentheses, brackets and braces, and defers
  every later call at that depth or deeper until the group closes. In the
  tree the rule reads: every child after the first of a `COMMA`, of a
  `CALL`'s argument list or of an `INITIALIZER_LIST`, with its whole
  subtree. The differential must confirm that this matches the token rule's
  depth form on every input, including a `;` inside a statement expression,
  which also ends the rule.
- **Scope.** A `STATEMENT_EXPRESSION` opens its block scope, and a
  `COMPOUND_LITERAL` its temporary object, before their children are visited.

**Exactly once.** Each node has one owner: the hoisting phase, an owner
frame, or the postorder phase. The walk marks the nodes it hoisted in a bit
array, private to lowering and offset from the core's first node. A nested
core consumes a node hoisted by an enclosing core, and does not lower it
again. Today `c_ir_prepared_control_expression_find` and
`c_ir_prepared_call_find` record the same fact: calls key it by their exact
node, and control records by their node and wrapping level.

**No recursion and no callbacks.** A nested core, a call argument or a lazy
operand becomes a frame on the existing `CIrLowerMachine` with today's frame
kinds. The tree walk is a loop with a `switch` over `CAstKind`.

**No frontend identity in IR.** Node indices live only in the frames and in
lowering's private scratch. Each row's source range comes from a token
through `c_ir_token_source_range`, as today. Under `-g` that token must be
the one the token path uses for the same row. For a binary operator that is
the operator token, and for a cast its `(`: both are the nodes' anchors.
Every other kind is listed when its slice is implemented, and the
byte-for-byte comparison of `-g` objects checks it.

### Retained expression parentheses

`CAst.parentheses` retains sparse `(node, open, close, flags)` records when the
forward expression builder consumes a `C_AST_OP_PAREN` marker's closing
parenthesis. These groups add no nodes and preserve implicit, hybrid and
explicit topology. Records are append-built in node order, then innermost to
outermost for a shared root. `c_ast_parentheses` finds a node's sparse slice;
`c_ast_parenthesis_at` reads a wrapping level, with level 1 innermost.
Owned conditions, `for` headers and compound blocks also retain their exact
opening and closing tokens in flagged rows. Conditions commit at their closer;
blocks and `for` headers publish their saved closer when the owner node is
appended. `c_ast_owned_delimiter` reads these roles separately;
`c_ast_parentheses` and `c_ast_parenthesis_at` exclude them from true wrapping
levels. Call argument delimiters remain owned by their `CALL` node.

The rows live in a private retained arena owned by `CAstStorage`, with checked
reservation growth and independent step commits. A failed build retires it;
`c_ast_release` releases it once through any copy of the tree. Logical rows
are charged to `retained_bytes`, unused committed capacity is visible as
`parenthesis_committed_bytes`, and reservation moves charge
`parenthesis_copy_bytes`. The producer does no token reparse or sorting. The flags retain the parser's
opening context when a group follows an identifier, including a keyword
spelled as an identifier. Lowering's wrapper navigation and control hoisting
read these rows; they inspect no token punctuators or matching delimiters to
find an expression group's owner or level. The hoist facts make one linear
pass over rows and nodes with a temporary presence bitset, released for the
normal visited-node role before the table is published.
`c_ast_test_parentheses` specifies exact roots and delimiters independently,
checks every layout and refill batch, forces both column and group moves,
and covers deep nesting, release through copies and failed-build rollback.
`c_ast_test_owned_delimiters` specifies exact condition, header and block
boundaries through every layout, refill and column-growth mode, and verifies
that owned delimiters contribute no true wrapping level. The token-free
lowering probe reads both roles with no token stream or matching index.

### Delimiters (map row 8)

In the B arm's census, lowering asks `c_ir_matching_delimiter_cached` 2.86 M
times, for 68.6 M Ir:

| Asking class | Ir | Owner |
|---|---:|---|
| root splits: `c_ir_root_conditional`, `c_ir_lower_expression_step`, `c_ir_lower_logical_value_step`, `c_ir_has_root_comma`, `c_ir_group_fact` | 37.7 M | slices 1–2; slice 1 has since answered `c_ir_root_conditional` from the tree under the pilot |
| evaluator and prepasses: the core step, both prepasses, `c_ir_unary_expression_end`, `c_ir_postfix_suffix_end`, `c_ir_unevaluated_operand_end`, `c_ir_call_argument_separator`, the `sizeof` operand scans | 21.9 M | slice 5 |
| query machine: `c_ir_query_execute`, `c_ir_sizeof_operand_*` and the static postfix and type-name bounds | 6.1 M | slice 4 |
| statements: `c_ir_builder_controlled_body_range`, `c_ir_lower_body_advance`, `c_ir_lower_condition_step`, `c_ir_statement_span`, the assignment statement, jump targets | 2.9 M | slice 3; the walker now reads [statement nodes](#statements-from-nodes-in-lowering) (#4060) |

The stream index these lookups read (`stream_matching_delimiters_plus_one`)
is built during semantic analysis, for the parser as much as for lowering.
Retiring the lowering lookups therefore removes at most their own 68.6 M,
and not the index build.

Parentheses have no nodes. A walk that moves from node to node never needs a
`)`: where the token path jumps to the closing delimiter, the walk skips an
interval by its extent. Token ranges are still needed wherever a node walk
hands work to a token-range consumer: the place machine, the query machine
and the initializer walkers. There the end comes from nodes, as in the
[statement plan](#statement-plan-from-the-tree):
- a node's last token is the greatest anchor on its rightmost chain;
- a `CALL` or `INDEX` adds its closer;
- expression groups read their parser-committed sparse records;
- statement headers and block closers read owned delimiter provenance.

`c_ir_node_expression_range`, its explicit edge stack and `c_ir_node_close`
are deleted. `c_ir_node_expression_provenance` reads a known root's retained
semantic span and true groups. `DO_WHILE` reads its separate owned condition;
no token scan, delimiter matching or token fallback remains in these reads.

Row 8 retires from lowering only when none of the four classes asks a
delimiter question. That happens after slices 1–5, so it is the last step.
If the outward checks prove too costly, the measured alternative is a sparse
side table, written by the builder when it commits a node: for each node
that ends at a closer, the closer's token. It must be charged to the tree's
retained bytes. The builder already knows that token when it appends the
node.

### Hoisting from the tree

`c_ir_prepare_control_expressions_step` and `c_ir_prepare_calls_step` keep their
frames, but each takes its list from the core's node
(`c_ir_tree_range_node`, through [the retained spans](#retained-facts-for-lowering)),
not from a token scan:
- **Control groups.** `c_ir_tree_control_groups` walks the subtree in source
  order. The groups it can hoist are:
  - a parenthesized node whose interior has a root `?:`, `&&`, `||` or
    assignment, or a root comma with any of those anywhere;
  - a subscript or array size with a root `?:`, `&&`, `||` or assignment;
  - a call's whole argument list, when its callee does not end in a name.

  A parenthesis directly after a name (a call's own, `__extension__ (...)`)
  opens no group unless it is the range's first token. The walk records a
  group without entering it. It does not enter:
  - a lazy operand (the right side of `&&`/`||`, a conditional's arms, a
    statement expression);
  - an unevaluated operand (`sizeof`, `_Alignof`);
  - `_Generic` or `__builtin_choose_expr`;
  - a group already recorded or being lowered.

  The list is exact before the first group is lowered, because lowering a
  group records only groups inside it.
- **Calls.** `c_ir_tree_calls_discover` classifies each call at its `(`,
  after its callee and before its arguments. Rows therefore come out in the
  order of their `(` tokens, each row's parent is the innermost recorded call
  whose arguments hold it, and a chain call (`get()(3)`) follows the call it
  continues. A call is deferred when it is:
  - in a lazy operand;
  - after a comma: any operand after the first of a comma, an argument list
    or an initializer list.

  When it defers a call, the innermost active call notes `deferred_calls`.
  The builtins that own their operands keep them.
- **Facts.** One byte per node (`CIrHoistFact`, `c_ir_hoist_facts_build`), in
  one pass in index order beside the spans, holds three things:
  - the node's root region: the root `?`, `?:`/`&&`/`||`, comma, and an
    assignment before the first root `?`;
  - whether any assignment or control operator appears anywhere in it;
  - whether its subtree holds a call or a hoistable group at all, so the walks
    skip subtrees with neither.
- **Source provenance.** Prepared control records now carry their structural
  identity (see [prepared control identities](#prepared-control-identities)).
  `open_index` and `close_index` supply the temporary expression-frame range
  and debug locations. Prepared call records still use `token_index`,
  `open_index` and `close_index`; the walks compute those tokens from spans:
  - the callee's first token (`c_ir_tree_callee_shape`, which follows member
    chains and `[ ]` suffixes to their name);
  - the group's delimiters.

  The remaining call-record adapter is retired with prepared call identities.
- **Work stays linear on skewed expressions** (`c_test_tree_hoisting_skew`
  counts `CAstLowerStatistics.hoist_steps`):
  - A call already emitted is not entered again, so the arguments of
    `f(g(h(...)))` do not walk the nesting again.
  - When a control prepass has recorded every group it found, the nodes it
    visited off its leftmost path are marked walked
    (`CIrSyntaxSpans.walked`). A later walk skips them. A `typeof` replay that
    releases records clears the marks.
  - Stacks and lists double on demand, and the walk's scratch is released
    when it ends.
- **Deleted:** the token prepass loops, `c_ir_prepare_calls_discover`'s scan,
  the lazy-operand scan (`CIrLazyOperandScan`), `c_ir_group_may_hold_preparable`
  and its `PREPARABLE_INSIDE` group fact, `c_ir_subscript_callee_base` and
  `c_ir_abstract_pointer_declarator`.
- **No tree, no hoisting.** A core whose range no node spans fails with "no
  syntax tree node spans this expression". A unit whose tree the builder
  rejects does not lower at all (see below).

  The builder now accepts the vendor standard attributes that tripped this
  (`[[vendor::tag(gnu::error("m"))]]`): their argument clause is any balanced
  token sequence, and the item keeps no argument nodes. The inputs the
  builder still rejects in the test suite are invalid programs that semantic
  analysis accepts and lowering diagnoses: `[1 ... 2 ... 3]` designators and
  stray initializer separators in static initializers, invalid `asm`
  operands, and an invalid `case` range. None of them reaches an expression
  core, so their diagnostics are unchanged.

### The evaluator's walk

`c_ir_lower_expression_core_step` runs both prepasses, then evaluates its
node (`c_ir_tree_range_node`) in postorder over an explicit stack of
`CIrCoreWalkEntry` (`c_ir_lower_expression_core_walk`). The value stack and
the walk stack start small and double in the temporary arena, so a core
reserves what it holds rather than one slot per token.
`c_ir_expression_core_capacity`, the operator stack, the precedence
reductions and the postfix probe are deleted.

- **Operators.** A unary, binary or cast node pushes its operands and
  applies itself after them (`c_ir_apply_operation`). An `INDEX` that no
  prepass hoisted, and a member access whose base is not a name chain,
  apply to their base's value the same way. `EXTENSION` stands for its
  operand.
- **Units.** These nodes append one value, through a child frame where they
  need one:
  - literals;
  - a name, with the member accesses and variable-length array subscripts
    the place machine takes with it (`c_ir_core_chain_node_unit`);
  - a prepared call, whose record ends where the node does;
  - `sizeof` and `_Alignof`;
  - `__builtin_offsetof`;
  - `++` and `--`, prefix and postfix;
  - `&&label`;
  - a compound literal;
  - a statement expression.
- **Parentheses.** The parentheses around a node are met outermost first, as
  the token path met each `(`. A group the control prepass hoisted
  contributes its value. A group with a root conditional or logical operator
  (innermost group only), a root comma, or an assignment anywhere lowers in
  an `EXPRESSION` frame of its own. Any other group is transparent.
- **Name chains.** The one-entry memo records the known chain node and its
  unit. It reuses the unit only for that same node or its exact unwrapped
  first child, then advances to the child. Source starts never select a
  cached node. `c_ast_test_lowering_chain_work` counts evaluator steps at
  128, 256 and 512 terms on member/subscript chains, grouping barriers, call
  bases and local VLA bases, with both SSA forms and unchanged C type rows.
- **Source tokens under `-g`.** A unary or binary operator's row takes its
  anchor, the operator token. A cast's takes its `(`. An `INDEX` takes its
  closing `]` (a hoisted or separately lowered subscript, its `[`). A member
  access takes its `.` or `->`, which is the token before its anchor, the
  member name. A failed operator reports the token the walk visited last, as
  the token path reported the token whose arrival reduced it.
- **Retained child handoffs.** Compound literals, `sizeof`, casts and
  `__builtin_offsetof` carry exact child nodes and finished semantic facts.
  Their temporary token-range query adapters are deleted.
- **Linear on skewed expressions.** `c_test_tree_evaluator_skew` counts
  `CAstLowerStatistics.evaluator_steps` (walk steps, parenthesis levels and
  name-chain descents) on left- and right-nested arithmetic, prefix chains,
  casts, member chains, subscripts and parentheses.

### Root splits and operand ends

Part (c) moved the questions the frames around the evaluator asked of token
ranges onto each carried node and its `CIrHoistFact` byte. The temporary
`c_ir_tree_range_node` recovery facade is deleted.

- **Calls.** A prepared call keeps its `CALL` node
  (`CIrPreparedCall.node`), and `c_ir_call_arguments_counted` splits the
  arguments from the node's children. A sole argument does not widen over
  the call's own parentheses. The preparation table and evaluator consume
  that same node, with one dense per-function node index; the callee-token
  lookup, chained-call table and Debug reference scan are deleted.
  `c_ir_call_argument_separator` is deleted. `c_test_call_result_callees`
  checks call-result member/index callbacks, grouped designators, sequencing,
  lazy branches, unevaluated chains, `typeof` and parameter VLA bounds with
  exact call counts and native effects. A returned function-pointer table
  (`get_table()[index_once()](argument_once())`, #4195) keeps its producing
  CALL as the indexed base, including a grouped producer. The regression
  checks that the factory, index, argument and callback each execute once,
  and that a short-circuit branch skips all four. Callee preparation reads
  node kinds and retained parentheses; member bases use exact children.
  Indirect callee requests carry the callee node (or a redundant function
  dereference's operand) without a chained token-to-call search. Their
  provenance ranges preserve the previous debug anchors. Ordinary and builtin
  argument requests take the exact CALL child at their retained ordinal;
  _Generic and choose-expression continuations use the retained selected node.
  The range-only request wrappers are deleted, and a missing node fails the
  handoff. Argument counts own iteration; ranges preserve source provenance.
  The existing per-call end cache also keeps one node word per actual
  argument, so the default implicit tree needs one sibling walk per call.
  This adds 4 bytes per cached argument, removes the temporary start cache
  used to create the ends, and sizes prediction scratch by actual arguments.
  Identity, prefetch and clear-cache builtins use those retained argument
  ends for their separators. Object-size and string-length shortcuts inspect
  the exact argument's node kind and intrinsic span, including grouped and
  concatenated strings; they do not strip token parentheses or classify a
  token sequence as an argument.
  CALL payloads retain the normalized semantic builtin family, independently
  of the CALL result and callee type. This includes intrinsics declared by
  headers, such as `_mm_pause`; standalone function designators keep their
  ordinary CType. Member and arrow-member callback names are ordinary calls
  even when their field spelling matches a builtin (#4227); their callee,
  arguments and callback keep their exact-once effects.
  `c_ast_facts_call_builtin` returns that family, zero for an
  ordinary call, and `C_AST_FACT_NONE` when no CALL fact was published. The
  call prepass refuses a missing publication. The classifier reads pointer
  and function types from the retained graph without materializing type rows;
  spelling only selects an operation within the retained builtin family.
  Core source-location calls use the exact CALL arity, C23 constants use their
  node kinds, and function-name objects use their semantic recipe. Explicit
  postfix-update nodes own their effects, without looking beyond a consumed
  place's source span for an update token.
  Runtime `va_arg` takes its list boundary from the argument cache and its
  result from the exact second-child `TYPE_NAME` query. The separator scanner
  remains only in the temporary F2 query adapter. String-length function-name
  shortcuts use the argument's semantic function-name recipe.
  Runtime builtin constant probes query the exact cached argument node:
  atomic orders, object-size modes, alignment and offsets, lock-free sizes,
  immediate operands, constant probes and frame levels. Shuffle selectors
  retain their signed `-1` sentinel and validate both integer limbs before
  emitting the existing target operand. Conditional leaf-zero conversion
  queries its retained leaf node. These continuations do not recover a
  constant operand from a token range.
- **The `EXPRESSION` frame** splits at the node's root comma, assignment,
  conditional or logical operator. `c_ir_root_first_token` walks the root
  region in order over an explicit stack for the first root `+`, assignment
  or `&&`/`||`. A hoisted argument list has no node of its own: its group
  carries the call node (`C_IR_TREE_GROUP_STRIDE`), and the frame splits the
  arguments as a comma expression would (`argument_starts`).
  Its root is retained in `node_plus_one`: comma and assignment continuations
  carry their exact children, and an argument-list continuation takes its
  next argument from the CALL's children. The assignment's place route reads
  the operand's node kinds and wrapper facts. Declaration, query and assembly
  boundaries hand over their exact child nodes; missing nodes are rejected at
  frame entry without range recovery.
- **Conditions.** `c_ir_lower_condition_step` strips parentheses, takes a
  `!` over a chain, reads the `__builtin_expect` hint, and splits the
  `&&`/`||` spine from the node. A spine is split once, into an array of its
  left-nested operators (`CIrConditionTask.spine`), so a long chain costs its
  length. Each ordinary task retains its exact node; a spine suffix has no
  single subtree and retains its spine/cursor instead. Its next left operand
  is the right child of the preceding deeper operator, and its final right
  operand is the right child of the current operator. Negation and hint
  unwrapping retain their exact child, with the original range kept for
  prepared-group reuse and source provenance. A condition whose root is an assignment of a logical value,
  `if (x = a || b)`, is a leaf: the token path split it at the `||` and
  lowered `x = a` as its left operand
  ([#4162](https://github.com/buster14a/buster/issues/4162),
  `c_test_condition_assignment_of_logical`).
- **Logical and conditional values** ask their retained node's root-assignment
  and `ANY_ASSIGNMENT` facts. Conditional type tasks retain the actual
  conditional node and hand its true/false child nodes over in the same
  pre-emission query order. Omitted middles capture their first operand once.
- **The evaluator core** retains its original node and a separate preparation
  node. Leading EXTENSION nodes are stripped for preparation alone; control
  preparation, call preparation and the original-node walk keep their order.
  The two prepasses and their filters take that preparation node directly.
- **Places.** The owner keeps its exact subtree. `c_ir_place_node_begin`
  reads prefix dereferences, groups, casts and prepared-call bases from the
  node kinds; `c_ir_place_path` collects member and INDEX suffixes into a
  source-order path. A grouped place or computed base becomes a child frame,
  preserving the former base emission and load-recovery order. The VLA child
  consumes consecutive INDEX nodes from that path. Subscript expressions and
  prefix/postfix update operands enter their expression/place frames with
  their exact child node. The member node anchors its name, so the emitter
  takes the preceding access token as provenance. The member-chain token
  probe and place/VLA delimiter walks are deleted. Type-name queries retain
  their marked step-4 boundary and their original row-creation order.
- **Assignment statements.** `BODY_ASSIGNMENT_STATEMENT` retains the
  statement expression's node, and a discarded void-cast assignment takes
  its exact operand. Assignment, direct index and prefix/postfix update
  routes read kinds and retained groups; child requests carry the exact left,
  right or subscript node. The direct index base is emitted in its previous
  order even when another suffix follows it. The statement delimiter scan,
  duplicate field/identifier/index classifiers and range-only place push
  adapter are deleted.
- **Prepass filters.** `c_ir_node_may_prepare_control` and
  `c_ir_node_may_prepare_calls` read the node's `ANY` and `HOLDS_CALL` bits.
  The per-body token prefix table (`c_ir_token_prefix_build`) is deleted.
- **Deleted token walkers.** `c_ir_group_fact` and its classifying pass,
  `c_ir_group_is_logical_chain`, `c_ir_has_root_assignment`,
  `c_ir_has_root_comma`, `c_ir_has_root_control_operator`,
  `c_ir_has_root_question`, `c_ir_has_assignment_anywhere`,
  `c_ir_has_control_operator_anywhere`, `c_ir_has_top_level_comma`,
  `c_ir_unary_expression_end`, `c_ir_postfix_suffix_end` and
  `c_ir_unevaluated_operand_end`.
- **Step 4 retirement.** Type, constant, static-initializer, array-bound and
  vendor-budget consumers carry exact nodes and finished semantic facts.
  `c_ir_tree_unevaluated_operand_end`, `c_ir_tree_range_is_cast`,
  `c_ir_tree_anchored_node`, range type prediction and sizeof/group recovery
  adapters are deleted. Runtime sizeof and compound literals use their exact
  operand/type/list children; declaration and assembly child handoffs likewise
  carry explicit nodes. Prepared controls use node and wrapping-level identity.
- **A unit the builder rejects.** The tree is mandatory: such a unit fails
  with the builder's diagnostics (`tree_rejections`), and lowering does not
  run. These are inputs the parser accepts: invalid `__builtin_offsetof`
  designators, an omitted `?:` operand outside a GNU dialect,
  `[a ... b ... c]` designators and case ranges, empty initializer
  elements, a second global assembly string. Through the driver, analysis
  reports the case ranges and initializer elements first, with its own
  messages; the designator and the global assembly now report the builder's
  (`expected ']' after the array range designator`, `expected ')' after the
  assembler text`). `c_test_tree_rejection` checks each. No corpus input is
  rejected.

### Prepared control identities

`CIrPreparedControlExpression` keeps an owner node and a wrapping level.
Level 1 names the innermost parenthesis around that node, with larger levels
naming successive outer parentheses. Level 0 names the owner node's bracket
or whole call argument list. A call argument list has no AST node of its own,
so its identity is the `CALL` node with level 0. This distinguishes an owned
delimiter group from parentheses around that same owner.

The control walk records these identities directly. The evaluator and the
condition and expression frames already hold the node they visit and pass
its wrapping level to `c_ir_prepared_control_expression_find`; lookup never
resolves a token to choose a record. Opening and closing tokens remain source
provenance and the temporary expression-frame range.

Containment reads postorder node intervals. A parenthesis covers its whole
subtree, a bracket covers only the last child's subtree, and an argument list
covers the sibling interval after the callee. Consequently emitting arguments
does not suppress the call itself or its callee. Active-group containment also
orders redundant parentheses on a shared node by their wrapping levels.

Per-node heads and emitted marks are sized to the function definition's
subtree, including parameter declarators. A `typeof` replay restores heads in
reverse creation order before records can be reused; stale emitted marks are
verified against current node intervals and fall back to the kept records.
An emitted group marks its new nodes and skips live covered intervals. Its
own recycled row cannot certify an already marked interval while the fill
runs. Its first mark then names the enclosing interval, allowing the next
parent to skip it without rewriting a skewed nest's descendants.

The runtime cases in `c_test_tree_hoisting_node_identity` exercise redundant
parentheses,
subscripts, synthetic argument groups and VLA `typeof` replay through both
frontend SSA forms at `-g0` and `-g`. Its token-free private replay probe
widens a recycled record's interval, checks all newly covered nodes, restores
per-node heads and verifies that bracket and argument groups exclude their
base and callee. The existing skew fixture keeps the navigation bound.

## Initializers, declarations and type queries from the tree

**Status: local declaration nodes and retained type/constant/initializer
producers are implemented; their remaining consumers are in progress.**
This is step 4 of the
[owner direction](https://github.com/buster14a/buster/issues/3102#issuecomment-6144374034)
of 2026-10-13, tracked in [#4061](https://github.com/buster14a/buster/issues/4061).
Local declarations and their bindings use nodes and retained facts. Under the
direction, `c_gen.c` lowers from nodes and retained facts, uses tokens only for
provenance (source ranges and
spellings), keeps no token fallback, and deletes each walker it migrates.
The plan:

| Step | Issue | Scope |
|---|---|---|
| 1 | [#4062](https://github.com/buster14a/buster/issues/4062) | retain the tree and semantic facts through lowering |
| 2 | [#4060](https://github.com/buster14a/buster/issues/4060) | statements |
| 3 | [#4059](https://github.com/buster14a/buster/issues/4059) | expressions |
| 4 | [#4061](https://github.com/buster14a/buster/issues/4061) | this section |
| 5 | [#4058](https://github.com/buster14a/buster/issues/4058) | remaining token-structure code, and a guard |

No code from this step lands before step 1. Step 1
([retained facts](#retained-facts)) retains F1's types, the bindings and
bit-field widths per node, under `-fc-ast-pilot` (the default since
2026-10-13). Beyond that:
- direct lowering receives a completed tree from `c_parse_for_lowering`
  (`c_lower_to_ir_tree_run`), but the facts are not retained;
- semantic analysis's initializer, declaration and `offsetof` answers are
  not published to lowering yet; F3's existing queried roots now have a
  separate retained sidecar, with node consumption still pending.

Deleting a walker earlier would break the default path, and keeping one as
a fallback is what the direction rules out.

**Boundary with step 3.** The expression walk
([Expression lowering from the tree](#expression-lowering-from-the-tree))
hands an owner frame a subtree: a cast, `sizeof`, `_Alignof`, a compound
literal, `__builtin_offsetof`. This step owns what those frames read: types,
constants, initializer layout and member offsets. It also owns the
declarations and initializers that statements reach, where step 2 hands it
a `DECLARATION` node.

### What the token path does

A static census of `c_gen.c` at main `fca2ed90` counts these families. The
counts come from a throwaway script, the families overlap slightly, and the
figures are approximate:

| Family | Functions | Lines | Token, delimiter or position reads |
|---|---:|---:|---:|
| Initializers, compound literals, string objects, array-count inference | 129 | 8,819 | 298 |
| The query machine and its type-name, `sizeof`, prediction and `offsetof` attempts | 84 | 4,418 | 244 |
| The constant evaluator and array bounds | 14 | 1,993 | 54 |
| Declarations, declarators and labeled-local predeclaration | 31 | 1,873 | 103 |

At that baseline, `c_ir_query_execute` ran a frame stack over twelve kinds,
each keyed by a token range. A frame that needs a sub-answer raises a request, suspends
through `CIrQueryResume` and resumes past its finished prefix. Answers are
memoized by key (`c_ir_query_key_hash`). Every kind rediscovers C grammar
the semantic phase has already parsed:
- **Declarators:** `c_ir_type_name_prefix`, `_declarator` and
  `_suffix_bounds` re-read a type name's specifiers and derivations.
- **Precedence:** `c_ir_sizeof_operand_type_classify` and
  `c_ir_constant_evaluate_impl` each run a shunting yard over the operand.
- **Casts:** the constant evaluator asks `c_ir_query_type_name` for every
  parenthesized group ([#3473](https://github.com/buster14a/buster/issues/3473)
  measures this).
- **Initializers:** `c_ir_constant_initializer_designator`,
  `c_ir_initializer_inference_designator` and the compound-literal steps walk
  braces and designators and count elements to find each item's subobject.
  Semantic analysis did the same walk in `c_parse_initializer_designator`
  and `c_parse_initializer_type_slots`.

On the unity self-host, audit
[`2026-10-11T104652Z`](../../performance-audits/2026-10-11T104652Z.md)
counts 129,110 `TYPE_NAME` attempts and 254,484 group probes. That figure is
hosted and diagnostic.

### Facts this step reads

Step 1 publishes these. Each is keyed by node id and kept in a side table
retained through lowering. No node id reaches IR. Where the semantic phase
computes a fact over a token range, step 1 keys it to the node. This step
does not rebuild that key. The shapes are a proposal until step 1 settles
them.

| Fact | Read by | Produced today by |
|---|---|---|
| F1. The C type, qualifiers and value category of each expression node in a body or initializer | `sizeof`/`alignof` operands, type prediction, the conditional result type, compound literals, null-pointer constants | the [tree expression typer](#tree-expression-typer) (body scratch) and `c_parse_direct_expression_type` |
| F2. The C type of each `TYPE_NAME` node, including `typeof` and variably modified types | casts, `sizeof(type)`, `_Alignof`, compound literals, `va_arg` | `c_type_parse_*` and the [specifier reads](#declaration-specifiers-from-the-tree) |
| F3. The folded value of each integer-constant-expression root, and the symbol and offset of each address-constant root | array bounds, `case`, enumerators, `_Static_assert`, static initializers | `c_parse` constant folding |
| F4. The entity, storage class and linkage of each declarator | local and file-scope declaration lowering | the binder and the [declaration split](#declaration-split-from-the-tree) |
| F5. For each initializer element: the subobject it initializes (offset, member and index path, bit-field) and that subobject's type; for an incomplete array, the inferred count | brace, designated and string initializers; compound literals | `c_parse_initializer_designator`, `c_parse_initializer_type_slots`, `c_parse_infer_initializer_array_count_core` |
| F6. The member path and offset of each `offsetof` designator chain | `__builtin_offsetof` | `c_parse` |

F3's first publication slice retains the roots the existing normal
`c_parse_typed_constant` has evaluated. `CAstFacts.constant_slots` indexes
sparse `CAstConstantFact` rows in a separate owned arena; those rows keep
both integer limbs, narrow float values or wide float target images, and
the primitive semantic kind. A CTypeId is retained
only for an existing row of a top-level query. Protected type queries never
publish a row. The sidecar is released with the facts and its committed
bytes are included in `CAstFactStatistics.retained_bytes`.

The states are `UNQUERIED`, certified `INTEGER` or `FLOAT`, proven `NONCONSTANT`,
`UNSUPPORTED`, and `INVALID`. An existing bound ordinary object can prove a
nonconstant identifier. A fold that declined a shape is `UNSUPPORTED`,
including a supported source value outside the folder's model;
`valid == false` alone cannot prove nonconstancy. `INVALID` records an
evaluated C fault. A lazy operand that is not entered does not fault the
selected root. This publication slice adds no type inventory and no
lowering fallback. The constant grammar walker remains until the node
consumer replaces it in the next slice.

Committed C array-bound indices have a separate four-byte node slot each.
Semantic finish publishes that correspondence once from the completed model,
after protected queries have rolled back. The table keeps no C row or value;
its exact expression owner leads to the existing constant fact or node task.
Its checked private arena is charged to retained bytes, released with the
facts, and compact-copied to the unit arena for convenience/model entrypoints.
Repeated analysis keeps the model inventory and owner correspondence stable.

Sparse annotations share a constant root's slot. An `offsetof` plan records
ordered member offsets and index operand nodes with their element sizes;
an absent plan differs from a recorded invalid designator. The semantic
validator publishes the path from its already parsed type, including runtime
index operands, anonymous promoted members and the one parse of an inline
type definition. The numeric descriptor may publish during a protected TYPE
query: its offsets, element sizes and operand nodes survive semantic model
rollback without retaining a CTypeId or binding. Constant value rows keep
their separate checkpoint guard. Repeated publication keeps one descriptor
per call and preserves the facts-disabled model inventory. F6 lowering
still consumes the legacy walker until its node
consumer replaces it. An x87 initializer annotation retains the
semantic destination significand and exponent independently of the source
arithmetic fact. `c_parse_validate_static_scalar` publishes that annotation
after its existing successful fold; aggregate leaf producers can publish
the same annotation. Neither annotation adds a dense node column.
The same sparse root also retains a generic/choose expression's selected
child. A final semantic publication pass normalizes the committed identity
answers to nodes; compatibility/classification answers become integer
facts. Protected temporary records never reach that pass, and discarded
arms are not evaluated to publish the selected root.

The x87 token expression folder (`c_semantic_ext80_fold_initializer`) and its
private value, cast, arithmetic-frame and conditional-frame machinery belong
to `c_parse.c`. Semantic validation publishes its destination image before
lowering. Exact rational arithmetic and literal conversion remain shared
numeric helpers in `c_gen.c`; their private contract in `c_internal.h` exposes
no source structure. `c_ir_ext80_store_bytes` stores the finished image in
lowering. The old byte initializer's `c_ir_ext80_fold_initializer` adapter
remains a migration residual until that initializer family is retired.

A C type becomes an IR type the way it does today: through
`builder->c_type_ir_map`, or `c_ir_vla_c_type` for a variably modified type.
F1 is the token-to-node map's design C, about 34 MB on the unity self-host
([Token-to-node map](#token-to-node-map-for-lowering)). The owner direction
accepts that size in principle. Its bytes are still reported with the
tree's retained bytes.

Semantic `sizeof` array dependencies use the existing typed-constant task
stack in protected TYPE queries. A failed NORMAL array-layout leaf keeps its
already parsed source-point type and asks that numeric query; only source
start/end and the computed size return before the private model is released.
After full success, the NORMAL scalar publisher certifies those descendants.
Speculative TYPE projections can then read their exact certified size_t fact,
while lowering still schedules source constructor tasks explicitly. No
speculative CType or binding escapes. Array suffixes reuse the existing
indexed delimiter and interned qualifier reader with the same model flags.
The regression checks 64/128/256/1024 nesting, retention on/off tables at the
same interning policy, scoped enum bounds, width/sign controls and unevaluated
calls. Its counters measure dependency tasks, bound requests, numeric receipts
and certified reads; dependency/receipt work is bounded linearly in depth.

The reference comparisons explicitly set `reference_tokens` and verify that
facts stay absent; omitting a supplied sidecar otherwise requests automatic
retention. An earlier array typedef can move a pending bound outside the
query's source range, so constant tasks keep their small initial allocation
and grow toward the unit token bound only as needed. Dimension layouts settle
from inner to outer, with checked target size_t counts and object-size products;
a zero outer dimension cannot conceal an oversized inner array.

Pending array dimensions also mark their exact bound source identity in a
checked sparse active set. Reentering an active bound refuses the cycle;
settled siblings can reuse that source after the owning dimension resumes.
The constant stack refuses its hard capacity before another child can write
past it. A completed semantic model can contain a local declaration before
its declarator was visible: a lone sizeof operand inside that declaration's
own bound selects an earlier outer binding when present. The NORMAL fold
publishes that committed operand identity with its answer; protected TYPE
queries return only numeric receipts or cycle blame, never temporary rows.

The shared constant dispatcher assigns named, disjoint continuation states
to integer-transform operands and array dimensions. Their union payloads
must never share a state value: a transform's token ordinals are not an array
task's private type-list pointer. Mixed transform/array constant expressions
run through the same permanent source-point and reference-parity fixture.

The array dependency route also admits the existing valid enum alignment
case `_Alignas(A)` followed by an array bound `_Alignof(g)`. Its prior
NOT_CONSTANT expectation is now a positive size/count check. A reference
analysis with no AST or sidecar agrees with retained analysis under matched
interning on all supported data layouts and i386; native controls observe
the aligned global address and write/read the array's final element.

### Type queries

The node task scheduler checkpoint uses exact `(kind, node, view)` keys for
`TYPE_NAME`, `COMPOUND_TYPE`, `EXPRESSION_TYPE` and `CONSTANT`. Its explicit
frames and completed-child index reuse the checked frontend query arena;
the canonical IR contains none of these identities. A missing child queues
one task and resumes its parent through the dispatch loop. Private graph
state belongs to the standalone task root and does not escape its scratch
lifetime. A settled declined child is distinct from a pending child.

The scalar and complex constant tasks read numeric, literal and selected
children from nodes and share the existing arithmetic, cast and byte helpers.
Type attempts materialize retained semantic recipes and exact source children.
The old CONSTANT, SIZEOF and OFFSETOF range frame families, shunting-yard
parser, saved token/operator resumes and their scratch arrays are removed.
Condition literal shortcuts and switch label constants pass exact nodes.
The semantic offsetof designator grammar is private to c_parse.c; lowering
consumes its retained numeric path. The remaining legacy initializer closure
and VLA C-type projection frame are retired by their respective owners before
the final source-grammar guard is green.

The remaining expression-type callers now pass exact nodes for arity ranking,
vendor results, variadic operands, indirect callees, sequences, selections,
conditional arms and returns. Arity ranking uses the prepared CALL argument
cache, so wide calls retain linear sibling traversal. The common lowering
frame push no longer finds nodes from source spans: both single and multiple
declaration continuations carry the initializer root, and scalar braces pass
their known child. The unused atomic-place and variadic-expression frame kinds
are removed. Source ranges remain diagnostic and emission provenance only.

Each query kind becomes a fact read on the node its owner frame was handed.
The machine, its keys, its memo and its resumes are deleted:

Runtime alignment queries retain the semantic producer's nonzero declared
object or member alignment in F3, including packed placement. Lowering still
projects the operand's type before applying that answer and rejects a
bit-field through extension wrappers from its retained member flag, retaining
the query's owned group as the diagnostic anchor. A plain `typeof(type-name)` wrapper
projects its completed child F2 graph, so prototype parameter names remain
scoped to that child without a second scalar-specifier parse.
Statement-expression tails use their value context, including an incomplete
array's decay. An unlowered variably modified object follows only its retained
name, dereference and index nodes to the existing scalar or pointer layout;
an arithmetic tail that the original resolver refused keeps its diagnostic.
Storage-half layout queries retain the numeric layout of their complete
pointer, array or vector graph. Lowering consumes that certified F3 answer for
type-name and expression operands without constructing a runtime half type;
an unresolved variably modified layout keeps its original refusal.

| Query frame | Replacement |
|---|---|
| `TYPE_NAME`, and `c_ir_group_type_name` / `c_ir_query_group_type_name` | The node kind decides: a `CAST`, `SIZEOF_TYPE`, `ALIGNOF_TYPE` or `COMPOUND_LITERAL` has a `TYPE_NAME` child, and an expression does not. The type is F2. No probe and no memo. |
| `TYPE_PREDICTION`, `CONDITIONAL_TYPE` | F1 on the node |
| `OPERAND_TYPE` (the `sizeof` operand typer) | F1 on the operand, before decay |
| `SIZEOF` | F3 when the operand is not variably modified. Otherwise lowering evaluates the VLA bound nodes the F2 type names, in a child frame, as today. |
| `CONSTANT`, `ARRAY_BOUND` | F3. The constant evaluator, its cast probe and its resumes are deleted. |
| `VLA_C_TYPE` | Unchanged: it already starts from a `CTypeId`. Its bounds become nodes instead of token ranges. |
| `COMPOUND_TYPE`, `COMPOUND_ELEMENT_COUNT` | F2 on the literal's `TYPE_NAME`, completed by F5's inferred count |
| `NULL_POINTER_CONSTANT` | F3 is zero and F1 is an integer type, or a `void *` cast of such a constant |
| `OFFSETOF` | F6. A runtime index (`c_ir_offsetof_runtime_index`) is lowered from its `DESIGNATOR_INDEX` expression node. |

The constant evaluator's 128-bit and floating folds (`c_ir_constant_apply_*`,
`c_ir_constant_float_*`) stay, as F3's consumers in static initializers
need them. They take their operands from F3 values instead of from a token
walk. If step 1 publishes F3 only for integer constant expressions, a static
initializer's arithmetic constant becomes a postorder fold over its
expression nodes, with no recursion.

### Initializer plan publication

Semantic validation retains F5 as `CInitializerPlan` records indexed by the
initializer value node. `CInitializerPath` rows name root-relative member
and array edges, including anonymous promoted members and GNU range
endpoints. Ordered `CInitializerItem` rows retain values and explicit list
entry/exit events with matching event indices. Complex brace components name
member 0/1 and retain their parent CType plus the `COMPLEX_COMPONENT` flag;
the primitive real kind follows from that CType without adding a model row.
An array's already inferred count is copied from its
semantic bound. Plans and their checked sparse node index belong to the
result arena and survive body-scratch rewinds.

An unsuccessful designator walk retains its integer-constant, signed-negative
or terminal-index refusal and diagnostic token on its plan row. Lowering reads
that finished cause when the initializer has no complete plan, preserving the
original incomplete-array and static-initializer diagnostics without evaluating
the designator again. A later complete plan takes precedence over an earlier
declined inference row. Refusal publication does not change the semantic
model's inference answer.

`c_parse_infer_initializer_array_count_core` records the selections it
already makes. File-array inference publishes its plan before the replayed
validation. Compound-literal publication uses the same semantic walker,
sets aside the tree typer and rolls back any query-only rows, preserving
the semantic model and debug type inventory. The plan's `source_token`,
`range_source_token`, `value_start` and `value_end`, and an edge's
`source_token`, are emission provenance; consumers may not use them to
rediscover grammar. `{0}` retains a producer flag because a trailing comma
changes the existing zero-constructor emission.

The sole-string count shortcut can decline a record or an array of records
before visiting its first member. Publication retains that positional target
through the completed type graph, including list markers and any omitted
outer count, while preserving the original inference answer and model.
No expression query or shared-bound mutation is added by that publication.

Anonymous promoted-member paths can exceed the initializer's token span.
Publication grows the reached frame/continuation workspace within the
by-value type bound. Beyond the original inference capacity it retains the
plan and its canonical count, while leaving the semantic answer, array-bound
row and subsequent validation authority unchanged.

The accepted block-static single-statement-expression shortcut publishes its
already-selected tail node and exact emission provenance in the scalar item.
The original initializer node remains the plan key. Lowering does not inspect
the statement-expression grammar to repeat that choice.

### Initializers

The ordered retained items supply all three consumers:
- runtime stores (`c_ir_emit_initializer_capture` and the compound-literal
  steps);
- static bytes and relocations (`c_ir_constant_initializer_bytes*`,
  `c_ir_global_initializer_impl`);
- incomplete-array canonical counts (`c_ir_infer_incomplete_array_bounds`).

Each item is an expression, a nested `INITIALIZER_LIST` or a `DESIGNATION`
whose last child is the value. F5 names the subobject each one initializes,
so the walk has no designator cursor, slot table or brace-elision search of
its own. Semantic analysis keeps those choices in `c_parse`; the production consumers
read the resulting paths directly.

A `DESIGNATOR_RANGE` keeps its GNU meaning: the value is evaluated once and
copied over the range (`c_ir_nested_initializer_copy_range`). A string
initializer reads its bytes from the #1460 memo
(`c_string_literal_memo_create`) through the value's anchor token, as now.

**Emission order.** Objects must stay byte-identical. The walk therefore
emits stores, zero fills and relocations in the token path's order: item
order, with a later designator overriding an earlier one as
`c_ir_constant_initializer_union_state_*` records today. The `-g` source
token of each store is the anchor of the item's value node. The
byte-for-byte comparison of `-g` objects must confirm this for every item
kind. Any kind where it differs is listed here when it is implemented.

### Static initializer values from retained selections

`c_ir_global_initializer` receives the caller's retained initializer node.
Scalar global kinds use the node constant query; aggregate bytes, strings,
complex components, exact x87 annotations and label relocations use
`c_ir_constant_initializer_bytes_node`. The F5 task iterates list events and
canonical path projections, tracks union replacement state, and materializes
a ranged value once before importing its bytes and relocations into each
selected destination. Explicit braces clear their selected object before
subsequent values. Bit-fields retain the canonical access unit and deposit
only their selected bits.

A source compound constructor is requested through the common node scheduler
before its plan runs. `c_ir_static_compound_literal_node_attempt` keeps the
private context arena in its owning constant frame across child requests;
publication of the writable compound object occurs only after every store
finishes. Publication uses the compacted persistent relocation table rather
than private override tombstones. The state-release hook also releases that
arena on query abort. Only the actual root empty list selects a whole-object
zero initializer. Scalar member/object wrappers retain the existing empty,
nested-brace and bit-field refusals, and a scalar ranged value retains the
existing nonempty-brace refusal; complex root braces remain aggregate events.
Source scalar and lvalue views have distinct keys, preserving the occurrence
identity of addressed scalar literals.

Incomplete-array materialization reads the declaration/entity initializer
root and `plan.inferred_count`. It completes a private bound-table copy so
repeated lowering leaves semantic output unchanged. Flexible-tail synthesis
reads the retained tail count and preserves the existing array-then-record
constructor order. The old token byte folders, compound-address adapters,
designator/brace-elision cursors, slot projection tables and scalar literal
classifiers are deleted. Static and runtime initializer consumers require
the semantic plan; no token grammar fallback remains. Their shared by-value
record-depth memo, union replacement state, relocation index and checked
private context arenas remain canonical projection machinery.

### Unit capacity facts

Direct SSA keeps the existing conservative admission for a short-circuit
right operand beginning with an unevaluated layout query. The node census
checks `LOGICAL_AND` right operands with `c_ir_ssa_leading_sizeof`, following
only higher-precedence binary left operands and postfix bases to
`SIZEOF_EXPRESSION`/`SIZEOF_TYPE`. Retained expression groups, casts and all
prefix operators, including GNU extension, end that spine. Nonleading queries
do not change admission. This carries forward the migration's source-shape
policy; it is not a claim of a semantic hazard or a performance improvement.
Spines of distinct logical right operands do not overlap without crossing a
group or logical boundary, so total visited nodes are linear; group membership
uses the existing indexed lookup. No token lookup, new fact array or pass is
added.

The existing semantic token census publishes `identifier_token_count` and
`string_literal_fragment_count` in `CParseResult`. The lower-capacity plan
reads these finished counts, retaining its original allocation bounds without
classifying source tokens again. The scalar and SIMD census paths publish the
same ten counters. `c_test_ir_lower_capacity_plan` checks that erasing source
kind bytes leaves the accepted plan unchanged.

### Literal contents from owned nodes

`c_ir_decode_string_node_for_target` and `c_ir_count_string_node_for_target`
read a `STRING` node's first token and `data` fragment count. The byte codec
checks that owned run's bounds and encoding spellings; it does no token-kind
classification, parenthesis trimming or adjacent-fragment discovery. The
fragment memo keeps its original token keys and byte ownership. Runtime
initializer string values and the expression core pass their known nodes.
Detached synthesized function-name spellings use the same checked run codec.

`CHARACTER` decoding similarly receives a node and retrieves only its source
spelling. The core selects numeric literals from `NUMBER` nodes and their
retained number facts. Shared raw-range validation belongs to semantic
analysis in `c_parse.c`, including the legacy semantic parenthesis rules;
remaining legacy lowering callers must migrate with their owner walkers.
The literal test gate poisons every token-kind byte after building real syntax
nodes, and verifies the byte, count, encoding and memo results remain exact.

### Runtime initializer values from retained selections

The runtime compound-literal frames consume `CInitializerPlan` by initializer
node. Semantic analysis retains each value node, its destination path,
explicit-list begin/end markers, and source provenance. Path edges name the
canonical member index or array range selected by analysis; lowering does no
designator lookup, separator scan, brace-elision search or constant-index fold.

The flat owner preserves ordered captures, later replacements, final zero
values and `c_ir_emit_initializer_capture`. The storage owner projects each
unique path once over an explicit pending stack. Brace-elided continuations
reuse their projected prefix, while a fresh designation has fresh path ids.
An explicit nested list zeroes its subobject before its values, and its end
copies a GNU range after the first element is initialized. Scalar ranges
copy immediately after the single evaluated value is stored. The range
preflight tracks each explicit list owner and the nearest ranged path edge.
An outer range prefix therefore does not make a fresh nested list's first
range a chained designation. Closing that list restores the enclosing range
context; further-designator and bare aggregate refusals remain unchanged.

Each expression child carries its value node directly. The retained value
and path tokens supply the original debug anchors for casts, stores,
`FIELD`, `INDEX`, zero fills and range copies. The semantic `SINGLE_ZERO`
flag keeps the original bare `{0}` constructor choice, including vector
objects; `{0,}` retains the ordinary item route. A string fills a whole array
only when its element type is compatible with that string representation.

`c_test_initializer_plan_runtime` runs nested, promoted, overridden,
ranged, bit-field, union, string and compound-literal cases in both SSA forms,
at `-g0` and `-g`, under FAST and QUALITY. Missing semantic plans are a
structured lowering failure; every direct API and opt-out caller must retain
the producer's tree and plans before this consumer migration is complete.
The declaration-to-compound-literal frame boundary still has one marked
range adapter until those callers pass the initializer node.

### Declarations

Local statement dispatch takes its `DECLARATION` node. A list visits its
`INIT_DECLARATOR` children in source order, and each preparation frame keeps
its declarator node. `c_ir_declarator_name` follows the declarator's inner
child to its `DECLARATOR_NAME`, never its function parameter siblings.
Semantic analysis publishes that node's entity in `CAstFacts.payloads` with
`C_AST_FACT_BOUND` (`c_parse_publish_declaration_facts`); its type and storage
flags come from the retained `CEntity`. The binder also retains the
initializer's assignment anchor in `CEntity.initializer_token_plus_one`, so
stores keep their original `-g` source locations without rediscovering `=`.
No type row is added by this publication.

`c_ir_prepare_automatic_declaration` and the single-declarator body path
read initializer presence and shape from the `INIT_DECLARATOR`'s last child.
They keep their existing emission order. Initializer and VLA owners still
accept marked range adapters until their slices migrate; `asm` labels and
attribute payloads still have their own lowering readers. Model-only callers
and the explicit pilot opt-out build their tree after analysis; their
binding adapter reads the finalized semantic declaration-anchor index,
without parsing tokens or looking up source names again.

`c_ir_predeclare_labeled_automatic_locals` reserves fixed-size locals in the
entry block of a body with labels. It now visits the body's declaration-name
nodes in source order and reads their retained entity bindings. Initializers,
cleanup activation and VLA allocation remain at their declarations. File-scope objects
(`c_ir_global_initializer`) take their declaration from the split's record
and their initializer from its node.

### Deletion list

When this step is complete, these leave `c_gen.c`, and step 5's guard
covers them:
- `CIrQueryMachine`, `CIrQueryFrame*`, `CIrQueryResume`, `c_ir_query_*`, and
  `c_ir_group_type_name*`;
- the token-reading `c_ir_type_name*` functions: the prefix, declarator,
  suffix, function-type and vector-attribute readers;
- `c_ir_predict_*`, `c_ir_conditional_result_type_attempt`, and
  `c_ir_range_is_null_pointer_constant_attempt`;
- the `sizeof` operand typer (`CIrSizeofOp`, `c_ir_sizeof_operand_*`,
  `c_ir_sizeof_operator_scan`, `c_ir_sizeof_expression_attempt`);
- `c_ir_constant_evaluate*` and `c_ir_constant_cast`, and the token form of
  `c_ir_constant_offsetof_attempt`;
- the designator, slot-search and inference walkers in the initializer
  family, including `c_ir_constant_initializer_bytes_legacy*`;
- the token scan in `c_ir_predeclare_labeled_automatic_locals`, and
  lowering's calls to `c_ir_declaration_initializer_range`.

`c_ir_declaration_initializer_range` and `c_offsetof_walk_next` are shared
with `c_parse.c`, which keeps its method. Only lowering stops calling them.

`c_declaration_binding_scan`, which reads attributes in file-scope
declarations, moves to attribute nodes in the same step.

### Validation

- **Objects:** byte-identical `-g0` and `-g` objects against the step-1
  base, on the self-host and on every `tests/**/*.c`.
- **Focused tests** for:
  - nested and designated initializers, including GNU ranges, overrides and
    union members;
  - string arrays, with inferred, exact and too-long bounds;
  - compound literals at file and block scope;
  - VLA and `typeof` types;
  - constant expressions with casts, `sizeof` and `offsetof`.
- **Gates:** `test_all`, `test_self_host`, a sanitized build, and
  `source_size`.
- **Measurement:** per the owner direction, interim PRs report correctness
  and diagnostic counters only, with no per-slice budget and no benchmark
  request. The end-to-end measurement follows step 5.

**Open questions:**
- whether `c_parse` keeps F5's subobject paths in a form step 1 can publish
  unchanged;
- whether F3 covers arithmetic and address constants, or only integer
  constant expressions;
- how much of the 8,819 initializer lines is shared emission that stays, as
  opposed to walking that goes.

## Token-to-node map for lowering

**Status: superseded for `c_gen.c`.** Under the owner direction of
2026-10-13, lowering will walk nodes and read retained facts instead of
mapping its token ranges back to nodes, so no map is planned. Design C
(below) is accepted in principle: what lowering needs from the typer's
per-body results is retained in the [retained facts](#retained-facts), its
types, bindings and widths. The spans and start chains, which only a map
would need, are not retained. The census below is kept as the record that
led there. No map is built. Lowering receives the tree in
`CIRLowerOptions.syntax_tree`, but its queries arrive as token ranges. Each
tree slice so far has built its own per-body index:
- the [root-conditional records](#lowering-from-the-tree);
- the extent table, since replaced by the
  [statement walker's node reads](#statements-from-nodes-in-lowering).

This section asks whether one shared map, from a token range to the node
whose span it is, would let more queries answer from the tree. Its census is
audit [`2026-10-12T221903Z`](../../performance-audits/2026-10-12T221903Z.md),
which is hosted and diagnostic only.

**The map's contract.** A range maps to the node whose span it is exactly,
after balanced outer parentheses are stripped. The span rules are the tree
typer's (`c_ast_types_span`, `c_ast_types_locate`): a span is widened over
the parentheses that wrap it exactly. Each node also needs two masks:
- **The root region:** the `,`, assignment, `?:` and `&&`/`||` operators
  outside every parenthesis, bracket and brace of the node. These are what
  the token scans see at depth 0. An assignment in a conditional's second or
  third operand is dropped, as the scans drop it.
- **Anywhere:** the same operators anywhere in the subtree. A statement
  expression, compound literal, initializer list, `_Generic` or a type's
  `sizeof`/`alignof` makes it opaque, and the query then scans.

The map holds token and node indices only, in a private per-body arena. No
node id reaches the IR, and the tree is not written.

**Coverage.** On the unity self-host under `-fc-ast-pilot`, the map resolves:
- 98.2% of `c_ir_root_conditional`'s ranges;
- 79 to 100% of the root-token predicates (`c_ir_has_root_comma`,
  `_assignment`, `_question` and `_control_operator`), and 36% of
  `c_ir_call_argument_separator`'s ranges;
- every evaluator core, expression and logical frame, and every in-body
  condition frame (52,607 of 52,629; the other 22 lie outside a body);
- 44% of `c_ir_group_type_name`'s groups as an exact "no type name": the
  group's interior is one expression node.

Over about 1.97 M compared answers, the tree agreed with the scan every time.
The range map does not fit two queries, and two more need facts lowering does
not have:
- `c_ir_unevaluated_operand_end` and `c_ir_unary_expression_end` take an
  open-ended range;
- `c_ir_query_execute` and the `sizeof` operand typer need C types, which
  lowering does not keep.

**The three designs.** All are lazy per body.

| Design | Build | Lookup | Memory | Verdict |
| --- | ---: | ---: | --- | --- |
| A: dense start chain, built in lowering | 189.5 M Ir over 6,389 bodies, 109 Ir per body node | 48 Ir | at most 0.6 MB, per body | answers every query above; loses to its build |
| B: sparse table of the nodes whose region holds an operator | 188.6 M | 108 Ir | 2.3 MB for the whole unit | dominated: the same pass, and it cannot prove a negative, which is almost every predicate answer |
| C: keep the typer's per-body spans and chains for lowering | already paid by the typer's eager pass; masks not measured | about A's | about 34 MB for the whole unit, 4.7% of peak RSS | the only design with no new build; a lifetime change |

**The net for each consumer of A.** Each query pays a lookup on every call,
plus the scan on a miss:

| Consumer | Net |
| --- | ---: |
| The five root-token predicates | ≈ 28.5 M |
| Type-name negatives | ≈ 8 to 16 M |
| The root conditional, replacing the records | ≤ 31.8 M |

Together they net about 68 to 76 M, against a 189.5 M build. No single
consumer clears a 5 M net gate, so nothing was built and no budgets were
declared.

**What needs no tree.** A per-body token count refuses most predicate ranges
without a tree:
- 703,748 calls holding no candidate token cost 71.8 M Ir;
- after such a gate, the tree's share of the predicates is 9.6 M, less than
  the map's lookups would cost.

The same table also already counts `?`. Under the pilot,
`c_ir_root_conditional` skips that count and takes the records' path for
every call: 79.1 M, against 18.1 M of scan behind the count on the default
path. Both leads are on the token path and are tracked in
[#3924](https://github.com/buster14a/buster/issues/3924).

**Recommendation:**
- Land the token gates first. Their saving belongs to the token path, not
  to the tree.
- Build a map only together with [slice 5a](#order-of-slices), the first
  consumer large enough to carry one, and budget 5a net of the map's build.
- Prefer design C if the owner accepts about 34 MB kept for the unit. It
  leaves 5a's ceiling of about 280 M nearly whole. Design A leaves at most
  about 70 M before the walk's own cost.
- Do not pursue design B.
- Zen 5 validation stays incomplete (#2761).

## Facts lowering needs

The inventory for [#4062](https://github.com/buster14a/buster/issues/4062), at
main `2b190a7b`.
- **Left column:** each fact `c_gen.c` re-derives from tokens today.
- **Middle column:** where semantic analysis already computes the same fact.
- **Right column:** whether it reaches lowering.

"Retained" means the fact is in the [retained facts](#retained-facts) or
already in `CParseResult`. Lowering's type queries all take token ranges and
answer an `IrTypeId`. They run on the resumable `CIrQueryMachine`
(`c_ir_query_execute`), whose completed frames are rewound after every root
query.

| Fact | Lowering re-derives it in | Analysis computes it in | Reaches lowering |
|---|---|---|---|
| Expression type | `c_ir_query_prediction`, `c_ir_predict_expression_type*` | `c_parse_expression_type_query` (the machine), `c_ast_types.c` (the typer) | **Retained** per candidate node, as a C row (`types`). Lowering maps a row through `c_type_ir_map`. |
| `?:` result type, null-pointer constant | `c_ir_query_conditional_type`, `c_ir_query_null_pointer_constant` | `c_parse_conditional_expression_type`, `c_parse_range_is_null_pointer_constant` | The conditional node's type is retained. The null-pointer-constant test is not. |
| Type name (cast, compound literal, `sizeof`, `_Alignof`, `va_arg`) | `c_ir_query_type_name`, `c_ir_type_name`, `c_ir_group_type_name`, `c_ir_query_compound_type` | `c_parse_type_name_specifiers` plus the declarator machine; the typer's interned rows | A cast's or compound literal's *expression* node carries its type. `TYPE_NAME` nodes get no column yet (step 4). |
| Identifier → entity | `c_ir_identifier_entity` (lowering's own per-token table rebuilt from `identifier_uses`), with lookups by spelling as a fallback | the binder (`identifier_uses`); an initializer's lookups in the typer | **Retained** per `IDENTIFIER` (`payloads`, `C_AST_FACT_BOUND`). `identifier_uses` was already retained. |
| Bit-field-ness of a member access | `c_ir_bit_field_from_place` (from IR places) | the typer's member widths | **Retained** per accepted member node (`C_AST_FACT_BIT_FIELD`). |
| Value category (place or value), modifiability | `c_ir_lower_place_step`, `c_ir_recover_place_from_value`, `c_ir_entity_is_read_only` | `c_parse_expression_place_shape`, `c_parse_update_operand_modifiable`; the nonplace projection | The nonplace fact only (`C_AST_FACT_NONPLACE`). The rest follows from a node's kind and its operands' facts; it gets a column only if step 3 needs one. |
| Integer, float and address constants | `c_ir_query_constant`, `c_ir_constant_evaluate`, `c_ir_integer_constant_evaluate` | `c_parse_typed_constant`, `c_parse_constant_expression_evaluate` | Enumerator values, `constexpr` values, inferred array counts and static-assertion verdicts were already retained in `CParseResult`. Per-node constant values are not; that is step 4. |
| Array bounds | `c_ir_query_array_bound` (re-evaluates the bound unless it was inferred) | `c_parse_infer_file_array_bounds`, `c_parse_validate_array_bound_values` | Only the inferred counts. |
| `sizeof`, `_Alignof`, `offsetof`, layout | `c_ir_query_sizeof`, `c_ir_query_offsetof`, `c_ir_alignof_object_alignment` | `c_parse_type_layout_core` (the machine's layout cache), `c_parse_constant_offsetof` | No. Lowering lays types out from IR types. The operand's type is retained wherever the operand is an expression node. |
| `_Generic`, `__builtin_choose_expr` selection | `c_ir_selection_answer` → `c_semantic_type_identity_query` | `type_identity_queries` | Already retained in `CParseResult`. Lowering runs the machine on demand when a row is missing. |
| String literals | `c_ir_decode_string_literal_range_for_target` | the literal memo (`string_literals`) | The bytes were already retained per token. A string operand's array row is a gap whenever no validator's query appended it (see below). |
| Number literals | `c_number_fact` | `number_facts` | Already retained. |
| Labels, `&&label`, computed goto | `c_ir_lower_body_initialize_labels_run`, `c_ir_label_find`, `c_ir_label_address_prefix` | `c_parse_validate_labels`, `c_parse_validate_label_values` (body scratch) | No. A label is a statement node, and `&&label` is an expression node the typer declines. Step 2 decides whether labels need a column. |

**Measured** at the candidate on main `2b190a7b`. The self-host column is the
`C_AST_FACTS` row of
`ide cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -target x86_64-unknown-linux -march=znver5 -g0 -c -fc-ast-pilot -v src/buster/apps/ide/ide.c`.
The corpus column is the tally of `c_ast_test_corpus` on Linux x86-64, which
includes the compiler's own frontend sources. Both are hosted and diagnostic
only.

| Count | Unity self-host | Corpus (`c_ast_tests`) |
|---|---:|---:|
| Tree nodes | 2,928,061 | |
| Retained bytes (9 per node) | 26,352,549 | |
| Typed regions | 9,817 | |
| Candidates | 1,779,329 | 587,028 |
| Typed by the typer's rules | 1,694,370 | |
| Typed by a validator's query | 23,797 | |
| Typed by the completion run (61,162 runs) | 58,533 | |
| …of them string literals in structural form | 54,212 | |
| **Missing** | **2,629** (0.15%) | **3,474** (0.59%) |
| …`invalid` | 2,446 | 3,213 |
| …`appends` | 183 | 261 |
| …`unspanned`, `busy` | 0, 0 | 0, 0 |
| Stale | 0 | 0 |
| Uncovered (in a definition), outside (file scope) | 29, 6,052 | |

Peak RSS for the `-g0` unity compile under the pilot rises from about 735 MB
to 754 MB (three runs each, hosted, diagnostic only). The facts are retired
before code generation, so this is the analysis-to-lowering peak.

A throwaway census on the unity self-host attributes the missing nodes:
- `invalid`: 872 `__func__` uses, which the machine types to nothing;
  about 1,522 callee names of unbound builtins (`__builtin_strlen`,
  `__builtin_debugtrap`, `__builtin_expect`, the `__builtin_buster_simd_*`
  family); and about 52 calls and member accesses over those.
- `appends`: operators whose answer is itself a row the run appended. These
  are a qualified array element under `[]`, pointer arithmetic over a
  decayed array, and a pointer `?:`. They are the shapes the typer declines
  for the same reason ([what it declines](#tree-expression-typer)).
- Before the structural string form, 56,267 nodes were `appends`: 39,142
  string literals, and 16,925 `[]` over a string literal, mostly `S8()`'s
  `(("text"))[0]`.

**Current row-free publication.** The type-query takeover keeps sparse
structural recipes rather than accepting extra C rows. The historical census
above predates that extension; it is retained as the baseline of the gap
classes. The implementation resolves the appended-type class, recognised
builtin identities and implicit function-name objects. Unsupported machine
answers remain visible as gaps. The original alternatives were:
- **New fact forms that name no new row.** These follow the string literal's
  form: `__func__` as an array of `const char` of its function name's length;
  a builtin callee as a builtin identity (its spelling, which provenance
  allows); and a qualified element, decayed pointer or pointer `?:` as a
  derivation (qualifiers, pointer-to or array-of) over an existing row.
- **Accepting the rows.** Keep the rows the completion run appends, and
  accept, after review, the `-g` difference their extra IR types make.

The row-free route is implemented for completion queries that append only
type rows, bounds, alignment metadata and interning entries. It copies the
complete temporary row run before rollback, while every reference into an
older table remains a published row. Fresh-completion fixtures cover qualified
array elements, decayed pointers, pointer conditionals and transitive string
arrays, and compare the semantic model with publication disabled. Builtin
callee identities and implicit function-name objects have explicit forms.
Type-name publication retains the final abstract-declarator type and its
DECL_SPECIFIERS base. TYPE_NAME payload bit
`C_AST_TYPE_NAME_VARIABLY_MODIFIED` comes from the semantic producer. A fresh
function type retains its parameter rows, including each parameter
`parameter_source_type` before array/function adjustment, so the written
constructor can still be materialized before its parameter value type.
Recipes carry `recipe_begin`/`recipe_end`, numeric constant array counts, and
node references for nonconstant bounds. Completion runs use the protected TYPE
constant-evaluation mode and restore the full semantic checkpoint after
publication. No new C row or IR type is emitted by this producer.

The node type-name consumer now reads this graph with an explicit resumable
stack and asks the shared node scheduler for bound constants. Source typedef
identity is retained on TYPEDEF_NAME separately from the completed qualified
TYPE_NAME. It preserves the established layout projection of already atomic,
over-aligned aliases. Source function parameter tasks are memoized separately
from return-declarator constructor effects, preserving the prior IR type
inventory and order. Compound literal array counts are published even under
unevaluated `sizeof`/`typeof` regions where no initializer execution plan is
required. These producers still restore their semantic checkpoints completely.

The obsolete token TYPE_NAME, COMPOUND_TYPE and COMPOUND_ELEMENT_COUNT frame
families and declarator walkers are removed. Expression/operand and VLA
consumers likewise carry exact nodes; the transitional range callers are
deleted. TYPEOF and atomic-specifier operands now request their exact TYPE_NAME or
STRICT EXPRESSION_TYPE child through the shared scheduler. Stable source
TYPE_NAME constructor projections have one owner per function subtree, shared
by constant and runtime readers. The producer marks those body source owners
with TYPE_NAME payload `C_AST_TYPE_NAME_BODY_SOURCE`; lowering does not derive
ownership from a source token range. TYPEOF retains its local projection effects.
The body token opener cache and its negative probe states are removed. VLA
TYPEOF shells retain the operand node alongside diagnostic provenance. This
checkpoint does not claim complete query-machine retirement or a green full
object comparison.

Required expression operands outside typed regions now use the same final
checkpoint completion: sizeof/alignof/typeof expression subtrees and retained
array-bound owner subtrees. This includes file-scope `sizeof(call())` bounds and
parameter-bound calls. Their existing semantic callable result and bindings
are retained before lowering asks F1; the consumer never reparses a callee
signature. Temporary marks are one bit per node, checked and released after
publication. Existing region candidates are not queried a second time, and
already completed outside compound facts are counted when their required
subtree becomes a candidate region.

On frozen main `2c2c46fc9` unity input, the current producer census describes
all 1,787,510 typed-region expression candidates with zero missing, appended
state or stale gaps, and all 60,387 TYPE_NAME nodes. The 29 uncovered function
expression nodes and 6,096 outside-region nodes remain explicitly counted;
this is not a claim that every expression in the unit has a fact. The
lowering consumer migration remains open and this producer checkpoint does
not claim retirement of the query machine.

Semantic array layout binds each identifier at the bound's own source site.
Ordinary constants use lexical scope and declaration-point visibility; inline
enum definitions use their unpublished suffix until the enclosing declaration
publishes it. Completed nested enum lists can therefore shadow a global enum
without exposing later declarations or sibling scopes. Retained offsetof paths
carry the resulting numeric offsets, with no lower-time scope reconstruction.
Typed semantic constant leaves use the same declaration-point lookup when
late validation revisits a bound. A later enum in an inline record must not
replace an earlier reference's value while its BOUND fact still names the
original declaration; direct, arithmetic and casted bound facts retain that
source-order distinction.

Exact-node constant tasks distinguish ordinary values from addressed compound
objects with `C_IR_AST_CONSTANT_LVALUE`. A direct whole initializer's literal
address route uses `C_IR_AST_CONSTANT_WHOLE_ADDRESS`, preserving the legacy
nested-object admission; general conditional/arithmetic probes retain their
nested-object refusal. Extension and member/index bases
retain that view through the task key. Numeric complex compound literals
keep two component values; addressed scalar literals request static objects.
Identifier and callable builtin identities come from BOUND facts and semantic
recipes. Integer builtin operands remain child node tasks even when the call
has an integer fact, preserving nested cast and sizeof constructor effects.
The retired token constant identifier fallback and oldest-name index have no
lowering or test facade. The scope regression checks static initializer images
from a finished semantic unit, including a nearer enum shadow among 256 names.
Static compound objects suspend on retained initializer-plan leaves and are
published only when their task completes. The scheduler releases any active
object context arenas on failure, restoring nested guards from the inside out.

A certified integer subtree may skip arithmetic only when its node kinds
contain no type or object constructors. That property is propagated by exact
child tasks and cached in two bits per AST node in checked unit query scratch;
it does not consume a full completed numeric-answer row. The cache survives
query-root rewinds because it depends only on immutable node kinds. A geometric
array-bound regression checks the actual task attempts for 128, 256 and 512
terms, including a nested cast whose constructor must still be materialized.

Scalar pointer-plan leaves retain the legacy integer-cast probe as a distinct
node task keyed by the destination IR type. Its child queries carry a private
probe context, separate from normal value queries and source-type projection
memoization. A settled noninteger result keeps the constructors and string
object produced by that abandoned attempt; the ordinary value fold then runs
once in its own context. Integer results use the shared signedness and pointer
width conversion. Empty numeric scalar compound literals produce typed zero;
addressed empty scalar objects keep their existing refusal.

Source-location constant CALLs use the normalized retained builtin identity
and zero-argument child count. Their call anchor supplies the presumed line
or mapped file name, and function names reuse the existing shared object.

The runtime offsetof core consumes the ordered numeric path and a CONSTANT
task result. That task evaluates each index once for folding, retaining only
nonconstant step ordinals in private checked query scratch. The root copies
those ordinals to function scratch before releasing query state. Runtime
expression frames carry each step's exact index node and scale by its retained
element size. No token designator, bracket or member walk remains in this
runtime core, and the private node metadata never reaches canonical IR. The
old constant range evaluator and its offsetof query family remain migration
residuals until their remaining callers move.

Committed array-bound and alignment indices retain exact operand nodes in one
private owner arena. Alignment spelling is retained as a packed standard-form
bitset; GNU `aligned` arguments have ordinary expression children. Width-bearing
members retain sorted sparse member/node pairs. Publication follows protected
query rollback, without adding semantic rows. The sidecar is charged at its
actual retained size, compact-copied with the unit, and sealed before lowering.

The ARRAY_BOUND token query and retokenizer are retired. Initial type mapping
retains the old unresolved projection for bracket-qualified parameter bounds;
the later runtime layout fold consumes the retained expression node. VLA
shells, layout dimensions, and saved bound values carry that exact owner.
Numeric alignment and unresolved width queries use their committed source
owners; type-valued alignment projects the finished mapped type without new
constructors. Certified integer subtrees with no type constructors use their
semantic value; constructor-bearing expressions keep ordered node tasks.
Actual settled sizeof tasks feed the existing semantic-answer comparison.

The scalar node API reads a certified literal/enumerator leaf directly when
it has no constructor effects. A pointer-valued scalar place can read its
matching read-only global initializer; array/string places stay addresses.
A half-complex arithmetic carrier keeps its valid component type until the
destination cast, following the legacy path without creating a new aggregate
IR type. Math-special CALLs inspect their known payload child without emitting
a string object. The sizeof/alignof STRING case counts its retained literal run
and terminator directly, creating no array or pointer constructor.


## Consumer and retirement map

The production driver always builds the retained tree. Semantic analysis owns
source grammar and publishes completed ownership, bindings, type recipes,
constants and initializer plans. Lowering consumes those results and exact
nodes; it cannot opt out of the syntax boundary or recover a node from a
source range. Independent reference/model APIs remain test controls.

| Current owner | Input and responsibility | Retirement status |
|---|---|---|
| `c_parse_ast_from_tree`, declaration analysis | `DECLARATION`, `FUNCTION_DEFINITION`, `INIT_DECLARATOR`, `STATIC_ASSERT` records | Production declaration ownership comes from the mandatory tree. `c_parse_ast_reference` retains the independent token reference. |
| `c_parse_bind_block_statements`, `c_parse_local_declarations` | `COMPOUND_STATEMENT` plan and declaration children | Semantic producers use the statement plan and publish local bindings; private semantic grammar remains owned by `c_parse.c`. |
| `c_type_parse_*` declarator/specifier machine | Source type grammar, layout and retained type recipes | Remains a semantic producer. No type-parser machine, mutation or token-query entry is available to lowering. |
| Tree expression typer and semantic type queries | Completed source type/category, binding, selection and numeric facts | Producer coverage and refusal are explicit; missing authority is rejected rather than reconstructed by lowering. |
| `c_ir_lower_body_advance` | Exact statement nodes and declaration owners | First-token classification, statement-end scans, switch label scans and token extent tables are deleted. |
| `c_ir_lower_expression_core_step` and runtime place owners | Postorder expression nodes, explicit child handoffs and AST operator/member kinds | Shunting-yard precedence, root splits and member/operator token classifiers are deleted. |
| `c_ir_node_query_attempt`, initializer-plan consumers | Exact `TYPE_NAME`, compound, expression projection, constant, bound and initializer child tasks | The checked node scheduler preserves constructor effects and ordered publication. All token query families, range recovery wrappers and initializer/designator grammar walkers are deleted. |
| `c_ir_syntax_spans_create`, source-anchor getters | Borrowed completed spans/delimiter provenance and opaque spelling/site handles | The lower-time delimiter-index builder and fallback are deleted. No raw token classification is exposed; the [source guard](#lowering-provenance-boundary) covers every included lowering implementation. |

The following notes describe the earlier optional-hook migration, before the
mandatory boundary and lowering retirement. Their pilot/default distinction is
historical; current behavior is the map above.

Under `-fc-ast-pilot`, the first row's three walkers no longer run for a unit
whose records the split publishes. Their consumer, semantic analysis's
declaration loop, reads records that come from `DECLARATION`,
`FUNCTION_DEFINITION`, `INIT_DECLARATOR` and `STATIC_ASSERT` nodes, and the
field-by-field differential passes. The row is not retired by default: the
walker still runs without the flag and for every unit the split hands back.
Deleting it waits on the hook becoming the default.

Under `-fc-ast-pilot`, the second row's boundary scans no longer run for a
planned statement: the body binder takes block declaration ends, `for` header
boundaries and loop ends from the tree, binds plain segments without its
per-token loop, and `c_parse_local_declarations` splits a planned block
declaration at the declarators' hinted `,` and `=` and binds a plain
initializer with the use filter alone. Its consumer is the binder itself, and
the field-by-field binding differential passes. The row is not retired: the
loop and the token split still run without the flag, for every statement the
plan declines and for every body that falls back; the declaration's
specifiers and declarators are still read by the type machine (the next row);
and `c_parse_validate_one_switch` and `c_parse_validate_control_statements`
still find statement ends with `c_parse_statement_end`, deliberately: those
calls cost 1.05 M instructions on the self-host unity input, less than a tree
lookup would.

The fifth row is retired: lowering reads every body's statements from their
nodes (#4060), by default and under the pilot, with no token fallback. The
census below found that a tree read in the token walker cost more than it
replaced for the first-token classification and the switch label scan; the
[keyword gate](#statement-keyword-gate-in-lowerings-classification) has the
numbers. That measured the tree beside the token path. With the token path
deleted the comparison no longer applies, and end-to-end measurement waits
until the migrated path is complete, per the owner direction.

Under `-fc-ast-pilot`, the third row's specifier machine no longer runs for a
plain specifier run of a block declaration, a file-scope declaration, a
parameter or a member. Its consumers, the declaration typers and the
aggregate segment frames, take the base type and the declarator start from
`DECL_SPECIFIERS`, and the field-by-field model differential passes. The row
is not retired. The machine still runs without the flag, for every run the
read hands back, for type names typed in its own frames, for the rest of a
tag definition, and for every pointer, array and function derivation.

The [tree expression typer](#tree-expression-typer) is the first consumer of
the fourth row's replacement. Under `-fc-ast-pilot` it answers most of semantic
analysis's body and file-scope initializer expression-type queries from
expression nodes, operators, `&` and casts included. The machine still
answers the rest: shapes whose answer appends a row that is not interned,
and every query outside a typed body or initializer. So that row is not
retired.

## Measurement plan

Comparisons are between matched endpoints only. The final migrated frontend
is compared with the frozen original compiler and workload, including syntax
construction, retained semantic facts, all lowering work, diagnostics, object
identity at `-g0` and `-g`, and the self-host fixed point. Hosted measurements
are diagnostic. Acceptance needs the Ryzen 7 9700X route required by
[#2761](https://github.com/buster14a/buster/issues/2761) and described in
[benchmarking](../benchmarking.md); correctness and source-guard success alone
do not establish that performance result.

The sections below preserve the budgets and measurements of the earlier
optional pilot. Since the default switch (2026-10-13), their "default path"
and "D against A" refer to the historical tree-free driver path. That
production opt-out is now retired; a frozen original binary supplies the
reference endpoint. Under the
[owner direction](https://github.com/buster14a/buster/issues/3102#issuecomment-6144374034),
lowering slices carry no per-slice budget. The final end-to-end measurement
and Zen 5 acceptance follow
[#4058](https://github.com/buster14a/buster/issues/4058). The older
`ast-pilot-acceptance-v1` profile compares no flag against `-fc-ast-pilot`;
after the default switch those arms select the same layout and do not measure
the migration.

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
  establish this. That route is the owner-requested `ast-pilot-acceptance-v1`
  profile (#3397) in [benchmarking.md](../benchmarking.md), which reads "plus
  5%" as 1.05 times the retained bytes.

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

For the shared [qualified member copies](#qualified-member-copies), these
budgets were declared before their measured runs. The base is main
`680e16d3`. The input, flags and four Callgrind arms are stage 2's, on
tests-off `-march=x86-64-v3` builds, run on `-fsyntax-only` and on `-c`
(`-g0`):
- **Correctness.**
  - byte-identical `-c` objects at `-g0` and `-g` across the four arms;
  - `c_test_type_interning_objects` passes with the window shut and open,
    over new sources:
    - members read through pointers to const and to volatile aggregates;
    - nested aggregates and array members;
    - a member of an aligned typedef type;
  - no verify mismatch over the corpus, and a floor on shared-copy member
    answers;
  - the `struct S const*` member probe answers, and a late-row probe beside
    it declines;
  - a mutation that shares a copy carrying an alignment record fails a test.
- **Default path (D against A):** within ±0.05% Ir on both workloads. Outside
  the pilot the step makes one extra test for the interning header.
- **The slice (C against B):**
  - at least 40 M fewer instructions on `-fsyntax-only`, about half of the
    prototype's −74 M, with the lookups, the eager pass and code layout
    charged;
  - fewer instructions on `-c`;
  - at least 5,000 fewer machine runs from queries.
- **Acceptance:** hosted counts are diagnostic. No 9700X comparison is
  requested, so Zen 5 validation stays incomplete (#2761), and the hook stays
  opt-in.

The shared copies' hosted census is
[`2026-10-11T152407Z`](../../performance-audits/2026-10-11T152407Z.md), taken
the same way and diagnostic only:
- Every hosted budget passes. Objects are identical across the four arms at
  `-g0` and `-g`, and the corpus verify gate has 0 mismatches.
- **The pilot (C against B):** −74.9 M Ir (−0.81%) on `-fsyntax-only` and
  −86.5 M (−0.38%) on `-c`.
  - Machine runs from queries fall from 28,360 to 20,742.
  - About half of the saving is those runs, and half is fewer whole-table
    layout solves over the smaller table (91 to 82).
- **The default path (D against A):** +0.0034% and +0.0029%.
- Zen 5 validation stays incomplete (#2761).

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

For the [statement plan](#statement-plan-from-the-tree), these budgets were
declared before its measured runs. The input and flags are the same as for
the declaration split (`-g0 -fsyntax-only`, the self-host unity input), and
four arms are counted with Callgrind on tests-off `-march=x86-64-v3` builds:
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

The base is main `f86d65f9`, which the candidate branches from. The budgets:
- **Correctness.**
  - 0 binding differences over the corpus and the frontend sources: every
    identifier use, the per-token use map, every scope and every entity, with
    the diagnostics and type-table sizes;
  - a one-off comparison of the same fields on the self-host unity input, also
    with 0 differences;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms.
- **Coverage.** Every function body of the self-host unity input follows its
  plan: no fallback.
- **The plan's own effect (C against B).** `c_parse_bind_function_body`
  inclusive costs fewer instructions, with the plans' construction charged,
  and the whole compile costs fewer instructions.
- **The default path (D against A).** The plan adds no work there, so the
  difference stays within ±0.05% Ir.
- **Adoption of the hook as the default.** Unchanged from stage 1. Hosted
  instruction counts show only its instruction half; wall time, `-c` and RSS
  acceptance remain with the Zen 5 route (#2761).

The statement plan's hosted census is
[`2026-10-10T195257Z`](../../performance-audits/2026-10-10T195257Z.md), taken
the same way and diagnostic only:
- Correctness, object identity and coverage pass: every one of the self-host's
  6,584 bodies follows its plan, and the four arms' `-c` objects are identical.
- The binder's own cost (`c_parse_bind_function_body`) falls by 147.8 M
  (15.4%), with about 55 M of plan construction charged, and the whole compile
  by 145.7 M (1.53%, C against B).
- **The default-path budget fails on the cheaper side:** D against A is
  −0.34%. The planless walk runs no plan code and makes the same calls; the
  difference is the walk's code generation, now an always-inline worker with
  a planless and a planned caller. The band was not widened.
- Acceptance stays with Zen 5 (#2761), so the default stays off.

**Owner decision, 2026-10-11: the default-path criterion for the body walk.**
The ±0.05% Ir budget above stays recorded as failed (−0.34%); it is not
relabelled. It is replaced, for changes to the body walk
(`c_parse_bind_block_walk` and its two callers), by a work-equivalence
criterion:
- The planless walk's callees and their call counts on the self-host unity
  input equal the base's, apart from helpers the compiler inlines differently.
- `-c` objects (`-g0` and `-g`) are byte-identical across the four arms.
- Callgrind instruction counts are still measured and reported, but they do
  not gate when the work is equivalent.
- Wall-time neutrality of the default path belongs to the Ryzen 7 9700X route
  (#2761), which remains incomplete here.

The reason is that for this function the instruction band is below
code-generation noise. Six variants whose planless walks make the same calls
measured D against A at +0.088%, +0.125%, +0.146%, −0.105%, −0.22% and
−0.34% (the census above and
[PR #3370](https://github.com/buster14a/buster/pull/3370)): register
allocation in one 500-line loop moves with its surroundings. The statement
plan meets the replacement criterion. Every other budget in this plan keeps
its own declared criterion.

For the statement plan's [declarator hints](#statement-plan-from-the-tree),
these budgets were declared before their measured runs. The input, flags and
four Callgrind arms are the statement plan's (`-g0 -fsyntax-only`, the
self-host unity input, `--cache-sim=no --branch-sim=no`, tests-off
`-march=x86-64-v3` builds). The base is main `4317e0dd`, which carries the
statement plan (#3370). A throwaway no-inline census of that base charged
`c_parse_local_declarations` 337.1 M inclusive, of which the initializer
loop's per-token tests are about 47 M, the `,` and `=` scans about 7 M, and
the two auto-inference scans per declaration 36.4 M. The budgets:
- **Correctness.**
  - 0 binding differences over the corpus and the frontend sources (the
    statement plan's differential, unchanged);
  - a one-off comparison of the same fields on the self-host unity input,
    also with 0 differences;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms.
- **Coverage** on the self-host unity input: no body falls back; no
  declarator hint is refused (`declarator_declines=0`); at least 95% of
  planned declarations skip the auto-inference scans and at least 90% of
  hinted declarators' initializers are plain.
- **The hints' own effect (C against B).** The whole compile costs at least
  40 M fewer instructions (about 0.4%), with the hints' construction charged.
- **The default path (D against A).** The split is one always-inline worker
  with a hintless and a hinted caller, so the default path runs no hint code.
  The difference stays within ±0.05% Ir.
- **Adoption of the hook as the default.** Unchanged, with acceptance on the
  Zen 5 route (#2761).

The declarator hints' hosted census is
[`2026-10-11T104257Z`](../../performance-audits/2026-10-11T104257Z.md), taken
the same way and diagnostic only:
- Correctness, object identity and coverage pass: 0 binding differences on
  the corpus, the frontend sources and the self-host unity input; identical
  `-c` objects in the four arms; 36,366 hinted declarators on the self-host,
  none refused, and every planned declaration skips the inference scans.
- The block-declaration split falls by 84.2 M (25%), and the whole compile by
  63.9 M (0.69%, C against B).
- **The default-path budget fails by 0.006 points:** D against A is +0.056%.
  The hintless split is 2.6 M cheaper than the base's; the body walk's own
  code is 7.9 M dearer, with identical callees and call counts. The band was
  not widened. The walk's default-path criterion is the owner decision above
  (work equivalence), which this change meets.
- Acceptance stays with Zen 5 (#2761), so the default stays off.

The census also found that the switch and control-statement validators'
`c_parse_statement_end` calls cost 1.05 M in total (7,586 calls) on that input.
Reading their ends from the tree cannot repay its own lookup, so they stay
with tokens; this is recorded as a negative result, not a budget.

For the [declaration specifier read](#declaration-specifiers-from-the-tree),
these budgets were declared before its measured runs. The input and flags are
the same as for the statement plan (`-g0 -fsyntax-only`, the self-host unity
input of the base revision). Four arms are counted with Callgrind
(`--cache-sim=no --branch-sim=no`) on tests-off `-march=x86-64-v3` builds:
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

The base is main `4e5d563f`, which the candidate branches from. The budgets:
- **Correctness.**
  - 0 model differences over the corpus and the frontend sources: every type
    row, array bound, alignment specifier, type alignment, member, parameter,
    entity and declaration type, field by field and in table order, with the
    diagnostics;
  - a one-off comparison of the same fields on the self-host unity input, also
    with 0 differences;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms.
- **Coverage (reported, not gated).** The runs read from the tree and the runs
  handed back, by reason, from the `-v` counters.
- **The read's own effect (C against B).** The whole compile costs fewer
  instructions, with the index build charged. The machine's root specifier
  runs (`c_parse_scalar_type_in_scope_attempt` calls) fall by exactly the runs
  read from the tree.
- **The default path (D against A).** The read adds one test of a null index
  per run there, so the difference stays within ±0.05% Ir. The
  work-equivalence criterion for the body walk does not apply to this change.
- **Adoption of the hook as the default.** Unchanged from stage 1. Hosted
  instruction counts show only its instruction half. Wall time, `-c` and RSS
  acceptance remain with the Zen 5 route (#2761), and no 9700X comparison is
  requested.

The declaration specifier read's hosted census is
[`2026-10-11T105317Z`](../../performance-audits/2026-10-11T105317Z.md), taken
the same way and diagnostic only:
- Every hosted budget passes. The model matches field by field over the corpus,
  the frontend sources and the self-host unity input. The objects of all four
  arms are identical.
- On the self-host the read answers 79,095 runs: 13,470 primitive and 65,625
  typedef. It hands 2,113 back, 2,091 of them `input` and 22 `specifiers`. The
  machine's root runs fall by exactly 79,095.
- The pilot's compile falls by 141.8 M Ir (−1.53%), with the 17.7 M index
  charged. The index holds the 144,693 word-only lists, built in one pass.
- The default path is +0.026%.
- Zen 5 validation stays incomplete (#2761), so the hook stays opt-in.

For the statement plan's [braced segments](#statement-plan-from-the-tree),
these budgets were declared before their measured runs. The input, flags and
four Callgrind arms are the statement plan's (`-g0 -fsyntax-only`, the
self-host unity input, `--cache-sim=no --branch-sim=no`, tests-off
`-march=x86-64-v3` builds). The base is main `680e16d3`.

A throwaway census of that base recorded every declined statement's reason.
It switched Callgrind collection on only while the walk was at a declined
statement's own tokens. The 10,378 declined statements cost 188.3 M Ir, of
which:
- compound literals were 9,629 statements and 156.1 M (7,187 expression
  statements, 2,442 `if`, `return` and `while` headers);
- block declarations holding a member or enumerator list were 702 and
  31.8 M;
- labels (12), asm statements (2) and static assertions (33) were too few to
  matter.

Of the compound literals' cost, about 9 M is the census's own toggling, and
roughly 120 M is the walk's per-token chain and tests, which a segment skips;
the binding calls stay. The budgets:
- **Correctness.**
  - 0 binding differences over the corpus and the frontend sources (the
    statement plan's differential, unchanged);
  - a one-off comparison of the same fields on the self-host unity input,
    also with 0 differences;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms.
- **Coverage** on the self-host unity input:
  - no body falls back, and no segment bails;
  - at least 9,000 statements are planned braced, and the declines fall by at
    least 9,000.
- **The braced segments' own effect (C against B).** The whole compile costs
  at least 50 M fewer instructions (about 0.5%), with their construction
  charged.
- **The default path (D against A).** The walk and the segment binder are
  unchanged, and the plan builder runs only under the pilot. The criterion is
  the owner's work equivalence for the body walk (above): identical callees
  and call counts, and identical objects. Instruction counts are reported.
- **Adoption of the hook as the default.** Unchanged, with acceptance on the
  Zen 5 route (#2761).

The braced segments' hosted census is
[`2026-10-11T151745Z`](../../performance-audits/2026-10-11T151745Z.md), taken
the same way and diagnostic only:
- Correctness, object identity and coverage pass:
  - 0 binding differences on the corpus, the frontend sources and the
    self-host unity input;
  - identical `-c` objects in the four arms;
  - 9,627 statements planned braced, with the declines falling from 10,378 to
    751, and no fallback or bail.
- The body binder falls by 73.3 M (9.5%), and the whole compile by 73.2 M
  (0.79%, C against B), with about 11 M of plan construction charged.
- The default path meets work equivalence: the planless walk makes identical
  calls, and D against A is +0.003%, all of it libc string comparison with
  identical call counts.
- Acceptance stays with Zen 5 (#2761), so the default stays off.

For the type-name specifier runs (`c_parse_type_name_specifiers`: `sizeof`
and `_Alignof` operands, compound literals, and the smaller callers), these
budgets were declared before their measured runs. The census came first. It
was a throwaway build of the base with one no-inline wrapper per shape, with
Callgrind's callers separated, on the same input and flags as the
declaration specifier read. It found 70.2 K root runs costing 169.8 M:

| Caller | Shape | Runs | Ir |
| --- | --- | ---: | ---: |
| `c_parse_validate_sizeof_operands` | no type name begins at the run | 36,710 | 68.8 M |
| `c_parse_validate_compound_literals` | plain typedef name | 15,503 | 30.5 M |
| `c_parse_validate_compound_literals` | no type name (`if (c) {`, ...) | 10,575 | 24.5 M |
| `c_parse_validate_sizeof_operands` | plain typedef name | 5,544 | 10.5 M |
| `c_parse_typed_constant` | no type name, typedef name | 1,701 | 3.9 M |
| every other caller and shape | | < 1,000 | < 1 M each |

A (caller, shape) row is taken when its removable Ir clears **5 M**, about
0.05% of the pilot compile. The removable Ir is the row's cost less the work
that remains. A plain typedef name keeps the declaration read's cost, about
556 Ir a run. A run where no type name begins keeps the machine's failure
path, about 110 Ir a run: the identity prepare and the outermost `typeof`
aggregate scan. The first four rows clear the gate, for about 117 M. Both
mechanisms sit in the shared entry, so the rows below the gate take them at
no extra code.

The input, flags and four Callgrind arms are the declaration specifier
read's. The base is `b9c17db2`, the head of
[#3483](https://github.com/buster14a/buster/pull/3483), which the candidate
branches from. The budgets:
- **Correctness.**
  - 0 model differences over the corpus and the frontend sources: the
    analysis model field by field and the bindings, with the diagnostics;
  - a one-off comparison of the same fields on the self-host unity input, also
    with 0 differences;
  - in verify mode, over the same inputs, the machine answers, appends a row
    or reports a diagnostic for none of the runs skipped as no type name;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms.
- **Coverage (reported, not gated).** The runs skipped, the runs read and the
  runs handed back, by reason, from the `-v` counters.
- **The change's own effect (C against B).** The whole compile falls by at
  least 60 M Ir, about half the estimate, with the first-token bitmap charged.
  The machine's root runs fall by exactly the runs skipped and read.
- **The default path (D against A).** One test of a null index per type-name
  run, so the difference stays within ±0.05% Ir.
- **Adoption of the hook as the default.** Unchanged: acceptance on the Zen 5
  route (#2761), with no 9700X comparison requested.

The type-name runs' hosted census is
[`2026-10-11T145510Z`](../../performance-audits/2026-10-11T145510Z.md), taken
the same way and diagnostic only:
- Every hosted budget passes:
  - the model and the bindings match over the corpus, the frontend sources
    and the self-host unity input;
  - verify mode finds the machine silent on all 45,547 skipped runs there;
  - the objects of all four arms are identical.
- On the self-host, 44,295 runs are skipped as no type name and 26,885 are read
  (140 primitive, 26,745 typedef). Another 26 go to the machine as `input`. The
  machine's root runs fall by exactly the 71,180 runs skipped and read.
- The pilot's compile falls by 117.4 M Ir (−1.29%), with the 1.76 M bitmap
  charged.
- The default path is −0.0011%.
- The skip's identity prepare and `typeof` definition never change a result on
  any input tried. They stay, so the skip remains the machine's failure path.
- Zen 5 validation stays incomplete (#2761), so the hook stays opt-in.

For the split's [diagnostic probe](#declaration-split-from-the-tree)
(`c_parser_tree_probe`), these budgets were declared before its measured runs.
The probe classifies identifier lanes from their interned ids instead of
asking the word table and the `return` id through the token predicates. The
input and flags are the same as for the declaration split, and the four
Callgrind arms (`--cache-sim=no --branch-sim=no`) are counted on tests-off
`-march=x86-64-v3` builds:
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

The base is main `f86d65f9`, which the candidate branches from. The budgets:
- correctness:
  - the probe stays a superset of the tokens the walker validates, so
    `c_ast_split_cases` still reaches its `diagnostic` fallbacks (`0x`,
    `long long long`) and the corpus differential keeps 0 differences above
    its floors;
  - the self-host unity input still takes the split whole, with no fallback;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms.
- the probe's own effect (C against B): `c_parser_tree_probe`'s call-site
  inclusive cost falls by at least 25%, and the whole compile costs fewer
  instructions.
- the default path (D against A): the probe does not run there, so the
  difference stays within ±0.05% Ir.
- adoption of the hook as the default: unchanged from stage 1, with
  acceptance on the Zen 5 route (#2761).

The probe's hosted census is
[`2026-10-10T175428Z`](../../performance-audits/2026-10-10T175428Z.md), taken
the same way and diagnostic only:
- Every budget passes except adoption. Objects are byte-identical across the
  four arms, and the self-host unit takes the split whole.
- The probe falls from 125.5 M to 81.6 M Ir (−35.0%), and the whole compile by
  44.1 M (−0.46%, C against B). The id classification alone gives −14.0%; the
  memo cuts the validated specifier runs from 29,649 to 3,299.
- The default path is −0.0037%.
- Acceptance stays with Zen 5 (#2761), so the default stays off.

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

The failing-operand rule's hosted census is
[`2026-10-10T193402Z`](../../performance-audits/2026-10-10T193402Z.md). It
was taken the same way and is diagnostic only. Every hosted budget passes:
- 0 verify mismatches, and byte-identical objects across the four arms.
- The declined and unsafe checked casts fall from 873 to 236. `__func__`
  casts and `os.h`'s failure macros made up 637 of them.
- The pilot's instructions fall by 0.076%, with 719 fewer machine runs from
  queries.
- The default path moves by at most +0.0011%.
- Most of what remains is declined in the operand itself: members, string
  runs and `&`.

For the typer's string-literal queries (a bare string literal or run of them,
perhaps parenthesized, that the tree declines today), these budgets were
declared before any measured run. The input and flags are stage 3's; the four
Callgrind arms (A base default, B base with `-fc-ast-pilot`, C candidate with
it, D candidate default) are counted on tests-off `-march=x86-64-v3` builds,
with the base at main `f38a7716`, which carries the `S8()` replay.
- **Census first.** A throwaway build of the base counts the declined
  string-literal queries on the self-host by shape (one token, parenthesized,
  a run of several tokens) and charges their machine runs' inclusive Ir. If
  that total is below 1 M Ir, no candidate is built and the negative result
  is recorded.
- **Correctness.** No verify mismatch over the corpus; every replayed answer's
  rows are taken back and the machine appends the same rows again. Identical
  diagnostics and type-table sizes with and without the tree, and
  byte-identical `-c` objects (`-g0` and `-g`) across the four arms.
- **Default path (D against A).** Within ±0.05% Ir on `-fsyntax-only` and on
  `-c` (`-g0`). The answer must add no check to queries outside the typer.
- **The gain that ships it (C against B).** At least 1 M fewer instructions
  (about 0.01% of the pilot compile), with the eager pass, the answer and the
  replay charged, and fewer machine runs from queries. A smaller gain, or a
  loss, is recorded as a negative result and the code does not ship.
- **Acceptance.** Hosted counts are diagnostic; Zen 5 validation (#2761)
  stays incomplete and the hook stays opt-in.

The string-literal queries' hosted census is
[`2026-10-10T200424Z`](../../performance-audits/2026-10-10T200424Z.md),
diagnostic only:
- **Census.** 17,765 queries at 43.2 M Ir, not the 6,650 and 1 M the old
  literal path saw. 10,230 are `S8()`'s parenthesized length operand, and
  26 M of the 43.2 M is the literal's decode, which a replay must repeat.
- **Correctness.** 0 verify mismatches. Objects are byte-identical at `-g0`
  and `-g` across the four arms.
- **Ship.** C against B is −11.17 M (−0.118%). Machine runs from validation
  queries fall from 55,593 to 38,724.
- **Default path.** −0.041% on `-fsyntax-only` and −0.015% on `-c`. A version
  that added a parameter to the query's tree turn missed the band at −0.062%;
  the work was unchanged, but the query function compiled differently, so the
  shipped version keeps base's signature.
- **History.** The first two versions lost instructions: tests on the path
  every answer takes cost more than the replay saves. The shipped version
  keeps the string case off that path.

For the initializer slice of stage 3, which opens the interning window at the
file-scope initializer walk instead of after it, these budgets were declared
before its measured runs. The input and flags are stage 2's, and the four
Callgrind arms are counted on tests-off `-march=x86-64-v3` builds:
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

The base is main `f38a7716`, which the candidate branches from. The budgets:
- correctness:
  - no verify mismatch over the corpus, with every run of the gate interning;
  - identical diagnostics and type-table sizes with and without the tree;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms;
  - byte-identical objects between the default, the pilot, and the pilot with
    the window shut, including the file-scope source
    (`c_test_type_interning_objects`).
- the default path (D against A): no interning header exists there, so the
  change adds no work. The difference stays within ±0.05% Ir on
  `-fsyntax-only` and on `-c` (`-g0`).
- the slice's own effect on the pilot (C against B):
  - fewer instructions in the whole compile, with the interning lookups and
    the larger accepted set charged;
  - fewer machine runs from queries;
  - fewer tree declines.
- performance acceptance: the hosted counts are diagnostic and claim nothing
  for production. Zen 5 validation stays incomplete (#2761), and the hook
  stays opt-in.

The initializer slice's hosted census is
[`2026-10-10T190424Z`](../../performance-audits/2026-10-10T190424Z.md), taken
the same way and diagnostic only:
- Every budget passes. Objects are byte-identical across the four arms at
  `-g0` and `-g`.
- The default path is −0.00007% on `-fsyntax-only` and +0.0024% on `-c`.
- On the pilot the slice removes 44.98 M instructions (−0.47%):
  - machine runs from queries fall from 55,593 to 45,297, about the
    `S8()` casts at file scope;
  - tree declines fall from 52,535 to 42,239.
- The hook is now 8.76% below the default in instructions on this input.
  Zen 5 validation is incomplete (#2761), and the default stays off.

For a cheaper eager pass (`c_ast_types_type_body`, the tree typer's
per-body and per-initializer typing), these budgets were declared before its
measured runs. They use the stage-2 input, flags and four Callgrind arms on
tests-off `-march=x86-64-v3` builds:
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

The base is main `f38a7716`, which the candidate branches from. The budgets:
- **Correctness.** No answer changes:
  - no verify mismatch over the corpus;
  - the typer's `-v` counters on the self-host are identical in B and C: bodies,
    initializers, nodes typed and accepted, answers, declines, misses and gated
    queries;
  - identical diagnostics with and without the flag;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms.
- **The pass's own cost (C against B).** `c_ast_types_type_body`'s inclusive
  instructions fall by at least 5%, and the whole compile falls by at least
  the pass's saving less 1 M, so the saving is not moved elsewhere.
- **Default path (D against A).** The change is confined to the typer, so
  the default path stays within ±0.05% Ir on `-fsyntax-only` and on `-c`
  (`-g0`).
- **Performance acceptance.** Hosted counts are diagnostic. No 9700X
  comparison is requested for this change, so Zen 5 validation stays
  incomplete (#2761) and the hook stays opt-in.

The cheaper pass's hosted census is
[`2026-10-10T203657Z`](../../performance-audits/2026-10-10T203657Z.md), taken
the same way and diagnostic only:
- Every hosted budget passes. The typer's counters are identical in B and C,
  and the objects of all four arms are identical.
- `c_ast_types_type_body` falls from 359.7 M to 322.4 M Ir (−10.4%), and the
  pilot's compile by 36.5 M (−0.39%).
- The memo answers 58% of the pass's member lookups.
- The default path is within +0.006%.
- Zen 5 validation stays incomplete (#2761).

For the typer's `++` and `--` rule (`c_ast_types_update`), proposed by the
decline census in audit
[`2026-10-11T093336Z`](../../performance-audits/2026-10-11T093336Z.md), these
budgets were declared before its measured runs. They use the stage-2 input,
flags and four Callgrind arms (`--cache-sim=no --branch-sim=no`) on tests-off
`-march=x86-64-v3` Release builds. The base is main `4e5d563f`, which the
candidate branches from. The budgets:
- **Correctness.**
  - no verify mismatch over the corpus, so no accepted update node's machine
    run appends a row or raises a constraint the tree did not predict;
  - every row that a machine run touching `++` or `--` still appends under
    the pilot is explained by a shape the rule declines;
  - identical diagnostics with and without the flag;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms.
- **The gain that ships it (C against B).** At least 1 M fewer instructions
  in the whole `-fsyntax-only` compile, with the larger eager pass charged.
  Machine runs from queries and tree declines must both fall.
- **Default path (D against A).** The rule runs only with the tree, so the
  default path stays within ±0.05% Ir on `-fsyntax-only` and on `-c`
  (`-g0`).
- **Performance acceptance.** Hosted counts are diagnostic. No 9700X
  comparison is requested, so Zen 5 validation stays incomplete (#2761) and
  the hook stays opt-in.

If the gain budget fails, no code ships, and the audit and #3102 record the
negative result.

The `++` and `--` rule's hosted census is
[`2026-10-11T101413Z`](../../performance-audits/2026-10-11T101413Z.md), taken
the same way and diagnostic only:
- Every hosted budget passes. Objects are byte-identical across the four
  arms at `-g0` and `-g`.
- On the pilot the rule removes 14.99 M instructions (−0.162%): machine runs
  from queries fall from 28,054 to 23,760, and tree declines from 24,919 to
  20,625. The eager pass grows by 0.83 M.
- The default path is −0.0019% on `-fsyntax-only` and −0.0015% on `-c`.
- The 17 machine runs touching `++` or `--` that append rows (23 rows) are
  the same with and without the rule; each declines for a qualified member
  or an array operand that decays.
- Zen 5 validation stays incomplete (#2761).

The stage-1 adoption budget **passes** on the Ryzen 7 9700X. The evidence is
audit [`2026-10-11T142146Z`](../../performance-audits/2026-10-11T142146Z.md):
the first `ast-pilot-acceptance-v1` run (#3397), on candidate main `298713ae`,
through request-only PR #3536. One binary was timed with and without
`-fc-ast-pilot`, 20 ABBA pairs per cell:
- `-fsyntax-only`: instructions B/A 0.8503 and wall time 0.8930
  [0.8902, 0.8944]. Peak RSS does not move (−0.12 MB).
- `-c`: wall time 0.9546 [0.9533, 0.9571] and instructions 0.9382. Peak RSS
  grows by 31.7 MB, within 1.05 × the tree's 36.25 MB retained bytes.
- Objects and diagnostics are identical in both cells.

The RSS check also passes under the other reading of "plus 5%" (retained
bytes plus 5% of the default's peak), so that open owner decision (#3397) does
not change the verdict. The profile is report-only. The pass holds for that
candidate's exact binary, and making the hook the default is a separate,
owner-decided change.

For the typer's remaining out-of-body phases, these budgets were declared
before any measured run. These are the queries outside a typed body or
file-scope initializer that still run the machine under `-fc-ast-pilot`: file
`_Static_assert`, deferred assertions, type alignments, variably modified
checks, signatures, the binder and the like. The input and flags are stage
2's. Four Callgrind arms (`--cache-sim=no --branch-sim=no`) are counted on
tests-off `-march=x86-64-v3` builds:
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

The base is main `a6705764`, which the candidate branches from. The budgets:
- **Census first.** A throwaway build of the base, not committed, routes each
  machine run from a query through one noinline wrapper per analysis phase,
  and Callgrind charges each phase's inclusive machine Ir under the pilot. A
  phase is a candidate only if its machine Ir is at least 1 M and the typer can
  answer its queries: it must run in the normal constant-evaluation mode, on
  an idle machine. If the candidates total less than 1 M, no candidate is
  built, and the audit records the negative result.
- **Correctness.** No verify mismatch over the corpus, or in a one-off verify
  run over the self-host input. Identical diagnostics and type-table sizes
  with and without the tree. Byte-identical `-c` objects (`-g0` and `-g`)
  across the four arms.
- **Default path (D against A).** The new scopes exist only with the tree, so
  the difference stays within ±0.05% Ir on `-fsyntax-only` and on `-c`
  (`-g0`).
- **The gain that ships it (C against B).** At least 1 M fewer instructions in
  the whole compile, with the new scopes' passes and lookups charged, and
  fewer machine runs from queries outside a typed region. A smaller gain, or
  a loss, is recorded as a negative result, and the code does not ship.
- **Acceptance.** Hosted counts are diagnostic. No 9700X comparison is
  requested, so Zen 5 validation stays incomplete (#2761), and the hook stays
  opt-in.

The out-of-body census is
[`2026-10-11T093053Z`](../../performance-audits/2026-10-11T093053Z.md),
diagnostic only, and its result is negative. No candidate was built:
- The phases outside a constant-evaluation mode run 3.00 M machine Ir in
  all, not the 7 M listed earlier. File `_Static_assert` runs 0.90 M, and
  deferred assertions, the validators after the body loop and variably
  modified checks run 0.33 M or less each.
- Only the binder reaches 1 M, at 1.41 M. 1.17 M of that is `S8()` compound
  literals initializing locals. They are queried before the scalar rows are
  published and while the interning window is shut, so the tree declines
  them. The rest is 0.24 M.
- The 17 constant-evaluation runs (3.22 M) are never the typer's.
- 92% of the machine Ir still left under the pilot is declines and misses
  inside typed bodies.

For the member-tail width (`c_ast_types_probe_fails`, row 2 of the decline
census in audit
[`2026-10-11T093336Z`](../../performance-audits/2026-10-11T093336Z.md)),
these budgets were declared before its measured runs. They use the stage-2
input and flags (`-g0 -fsyntax-only` and `-g0 -c`) and the four Callgrind
arms (`--cache-sim=no --branch-sim=no`) on tests-off `-march=x86-64-v3`
Release builds:
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

The base is main `4e5d563f`, which the candidate branches from. The budgets:
- **Proof first.** A throwaway census build of the base must show, over the
  whole self-host compile, that every bit-field probe the new rule calls
  failing does fail and appends no row, and must account for the 120 rows
  that the census's 1,424 runs append. Without both, no code ships.
- **Correctness.**
  - no verify mismatch over the corpus, with the existing answer floors;
  - a mutation that hands the parent a nonzero width where the rule says 0
    must fail the corpus gate;
  - identical diagnostics with and without the flag;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms.
- **The slice's own effect (C against B).** Fewer instructions in the whole
  `-fsyntax-only` compile, with the token test charged, together with fewer
  machine runs from queries and fewer tree declines. The census ceiling is
  about 18.6 M. If C is not below B, nothing ships, and the result is recorded
  on #3102 as a negative one.
- **Default path (D against A).** The change is confined to the typer, so the
  default path stays within ±0.05% Ir on `-fsyntax-only` and on `-c` (`-g0`).
- **Performance acceptance.** Hosted counts are diagnostic. No 9700X
  comparison is requested, so Zen 5 validation stays incomplete (#2761) and
  the hook stays opt-in.

The member-tail width's hosted census is
[`2026-10-11T103427Z`](../../performance-audits/2026-10-11T103427Z.md), taken
the same way and diagnostic only:
- **Proof.** On the base, all 546 probes the rule calls failing did fail,
  and none appended a row. The 1,440 runs the width rule refused first
  append 162 rows, all const-qualified copies:
  - 158 come from a qualified member elsewhere in the same query;
  - 4 come from probes the rule leaves to the machine.
- **Budgets.** Every budget passes:
  - the corpus verify gate has 0 mismatches, and its tree answers rise from
    58,322 to 58,365;
  - the mutation (width 3 instead of 0) fails the gate;
  - objects are byte-identical across the four arms at `-g0` and `-g`.
- **Effect.** The pilot runs 5.60 M fewer instructions (−0.060%). Declines
  fall by 508, and the pass costs 1.18 M more.
- **Default path.** −0.002% on `-fsyntax-only` and −0.0015% on `-c`.
- **What stays.** The other 908 runs keep their machine run: their probe
  starts at a cast or a comma, or walks a qualified chain.
- Zen 5 validation stays incomplete (#2761).

The count cache's audit, below, leaves a second lead: under `-fsyntax-only`
nothing reads the bytes a first sight records. The candidate counts a narrow
fragment's first sight (`c_ir_count_quoted`) and records only its length, but
only when the compile will not lower. The driver says so with a flag on `CParserResult`, set for
`-fsyntax-only` alone, because `c_analyze_semantics_only` also runs before
lowering. With the flag clear, which covers `-c` and every other caller, a
first sight decodes and records bytes exactly as before. A length-only slot is
a miss for `c_ir_decode_string_literal_range_for_target`, which then decodes
without recording. These budgets were declared before the measured runs. The
base is main `680e16d3`, with the count cache's input, flags and four
Callgrind arms, below (`--cache-sim=no --branch-sim=no`):
- **Correctness:**
  - no verify mismatch over the corpus;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms;
  - the same exit status and no diagnostics on `-fsyntax-only` in every arm;
  - focused tests: the same element counts and types with and without the
    flag, and the same bytes from a decode that meets a length-only slot;
  - the literal-memo census expectations in `c_test.c` are unchanged.
- **`-fsyntax-only` (D against A, and C against B).** The whole compile must
  run fewer instructions, by more than the 0.05% rebuild noise. Each arm must
  report its calls to `c_ir_decode_quoted` and `c_ir_count_quoted`. Any
  decode that a length-only slot sends back is charged.
- **`-c` (D against A, `-g0`).** It must not grow by more than the 0.05% noise.
  `c_ir_decode_quoted` and `c_ir_count_quoted` must be called exactly as often
  as in A, so no count pass is added.
- **Acceptance.** Hosted counts are diagnostic. No 9700X comparison is
  requested, so Zen 5 validation stays incomplete (#2761).

The count-only first sight's hosted census is
[`2026-10-11T142104Z`](../../performance-audits/2026-10-11T142104Z.md),
diagnostic only. Every budget passes, and the change ships:
- `-fsyntax-only` falls by 183.9 M Ir: −1.755% default and −1.990% under the
  pilot. The 30,943 first-sight decodes (310.9 M) become counts (127.2 M), and
  no length-only slot sends a decode back.
- `-c` moves by +0.0001%, with the same decode calls and no count calls.
  Objects are byte-identical at `-g0` and `-g`.

For a per-token cache of string literals' element counts in the machine's
leaf (`c_parse_string_literal_expression_type`), these budgets were declared
before the measured runs. Exploratory Callgrind runs of the base came first;
the audit reports them as such. The base is main `a6705764`. The input and
flags are stage 2's, and the four Callgrind arms are counted on tests-off
`-march=x86-64-v3` builds:
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

What the leaf already reuses decides the bound. `CParseResult.string_literals`
records each narrow fragment's decoded bytes per token, so a call that decodes
is the first time any consumer sees that token, and lowering reads the same
bytes. A count cache can remove only the repeat calls' range shape (trim, kind
check, encoding scan) and memo probes. The budgets:
- **Census first.** A throwaway build of the base splits the leaf's calls into
  repeats, whose every fragment the memo already holds, and first sights. Each
  half's count is charged inclusively. The repeat half bounds what a cache can
  remove; the cache's own probe and insert are not yet charged. If that bound
  is below 1 M Ir, the floor the string-literal replay used, no candidate is
  built and the negative result is recorded.
- **Correctness**, if a candidate is built:
  - no verify mismatch over the corpus;
  - the same leaf calls and the same appended array bounds and rows;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms;
  - wide literals, per-target element kinds and concatenated runs covered by
    focused tests.
- **Default path (D against A).** It changes on purpose. The leaf's inclusive
  instructions, with the cache charged, must fall by at least 1 M. The whole
  compile may not grow by more than the 0.05% rebuild noise on
  `-fsyntax-only` or on `-c` (`-g0`).
- **Pilot (C against B).** The same leaf criterion, and the same 0.05% bound
  on the whole compile.
- **Acceptance.** Hosted counts are diagnostic. No 9700X comparison is
  requested, so Zen 5 validation stays incomplete (#2761).

The count cache's hosted census is
[`2026-10-11T095929Z`](../../performance-audits/2026-10-11T095929Z.md), a
negative result, diagnostic only:
- **Census.** Of the leaf's 32,707 calls, 83% are the first time any consumer
  sees the token. Their decode (38.4 M Ir) is the one lowering reuses. So
  the 26 M decode in the string-literal replay's census above is first-sight
  work that every design keeps, not a repeat. The 5,477 repeats cost 1.38 M,
  so a candidate was built.
- **Candidates.** A fast path answers a lone, recorded, unprefixed fragment
  from the existing memo, with no second table. Objects are byte-identical at
  `-g0` and `-g`, and the whole compile moves by at most +0.0035%.
- **Leaf budget fails.** The leaf grows by 0.53 M, or by 0.25 M when a first
  sight is probed only once. The code does not ship.

For the first lowering slice, [root conditionals from the
tree](#lowering-from-the-tree), these budgets were declared before its
measured runs. Lowering runs only when an object is produced, so the input is
the base revision's unity self-host source compiled with `-g0 -c`. Four
Callgrind arms are counted on tests-off `-march=x86-64-v3` builds:
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

The base is main `a6705764`, which the candidate branches from. The budgets:
- **Correctness.**
  - In Debug and sanitized builds, every root-conditional answer the tree
    gives, true or false, is checked against the token scan
    (`BUSTER_REFERENCE_CHECKS`). The pilot's `-c` tests run under that check.
  - Diagnostics are identical with and without the flag.
  - `-c` objects (`-g0` and `-g`) are byte-identical across the four arms.
- **Coverage.** On the self-host, the tree answers at least 80% of the
  root-conditional queries made inside top-level function bodies. Answers
  count both the true and the false ones. The rest go to the scan.
- **The slice's own effect (C against B).**
  - `c_ir_root_conditional`'s inclusive instructions, with the tree's answers
    included, fall by at least 50%.
  - The whole compile costs fewer instructions, with the index build charged.
- **Default path (D against A).** No index exists without the tree, so the
  difference stays within ±0.05% Ir on `-c` (`-g0`) and on `-fsyntax-only`.
- **Performance acceptance.** Hosted counts are diagnostic. No 9700X
  comparison is requested, so Zen 5 validation stays incomplete (#2761), and
  the hook stays opt-in.

The root-conditional slice's hosted census is
[`2026-10-11T111308Z`](../../performance-audits/2026-10-11T111308Z.md), taken
the same way and diagnostic only:
- **Base.** The measured candidate, `4bdecbd3`, was counted against main
  `0924dd69`, the merge base after the branch took main. The budgets did not
  change.
- **Earlier versions.** Three earlier versions were counted against the first
  base, main `a6705764`, and stay recorded in the audit.
  - The first cut the function by only 34%, because it stripped parentheses
    before the `?` test.
  - The second put the default path at +0.073%, because of an out-of-line
    dispatcher.
  - The third passed every budget, but review found its index build
    quadratic on nested conditional chains.
- **Correctness.** Every budget passes for the measured version. Objects are
  byte-identical across the four arms at `-g0` and `-g`. A reference-checked
  Debug compile of the self-host input checked all 823,431 tree answers.
- **Coverage.** The tree answers 99.05% of in-body queries, and the function
  falls by 52.94%.
- **The pilot's compile (C against B).** It drops by 51.40 M (−0.225%), with
  the index build charged.
- **The default path.** It is +0.0032% on `-c` and −0.0031% on
  `-fsyntax-only`.
- **Nested chains.** At depth 32,768 the reviewer's left-nested chain now
  compiles in 0.37 s under the pilot (4.44 s before), with 163,834 build
  steps.
- **Zen 5.** Validation is incomplete (#2761), and the default stays off.

For slice 5a of [expression lowering](#expression-lowering-from-the-tree),
the hoist set from one walk of the core's subtree, these budgets are
declared before any measured run of a candidate. That candidate does not
exist yet. Its base is the main revision where slice 2's range-to-node index
has landed; slice 1's hand-off is already on main. If
[#3445](https://github.com/buster14a/buster/issues/3445) has landed by then,
the base carries it, so the tree is measured net of the tree-free negative
tests. Inputs and arms: the unity self-host, `-g0 -c`, with four Callgrind
arms (`--cache-sim=no --branch-sim=no`) on tests-off `-march=x86-64-v3`
builds: A base default, B base with `-fc-ast-pilot`, C candidate with
`-fc-ast-pilot`, D candidate default. The budgets:
- **Correctness.**
  - A test-only verify mode computes every core's hoist set both ways and
    compares the ordered lists: each hoisted group's `(` and `)`, and each
    hoisted call's `(`, `)`, `deferred_calls` and emission position. It
    must show 0 differences over the corpus, the frontend sources and the
    self-host input.
  - Mutation checks must each fail the differential: dropping the lazy
    context of a `LOGICAL_OR` right operand, dropping the sequenced context
    of a second argument, and reversing two sibling calls.
  - `-c` objects (`-g0` and `-g`) must be byte-identical across the four
    arms.
  - Diagnostics must be identical with and without the flag.
- **Coverage.** Every core that falls back is counted under `-v` with a
  named reason (no tree, no node for the range, a shape the walk declines),
  and the corpus has one test per reason. On the self-host input at most 5%
  of cores may fall back.
- **The slice's own effect (C against B).**
  - `c_ir_lower_expression_core_step`'s inclusive instructions fall by at
    least 100 M, with the walk charged.
  - The whole compile falls by at least that saving less 5 M, so the saving
    is not moved elsewhere.

  The ceiling is about 280 M, the non-empty prepasses in audit
  [`2026-10-11T101741Z`](../../performance-audits/2026-10-11T101741Z.md),
  and the gate leaves room for the walk's own cost. If the measured saving
  is below the gate, the audit records a negative result and the code does
  not ship.
- **The default path (D against A).** The slice adds one pointer test per
  core there. The difference must stay within ±0.05% Ir. If it misses that
  band, the
  [owner's work-equivalence criterion](#measurement-plan) may replace it,
  but only when the evaluator's callees and their call counts are identical
  apart from inlining. That criterion was set for the body walk, and this
  budget adopts it only on those terms.
- **Performance acceptance.** Hosted counts are diagnostic. No 9700X
  comparison is requested, so Zen 5 validation stays incomplete (#2761) and
  the hook stays opt-in.

The census behind these budgets is
[`2026-10-11T101741Z`](../../performance-audits/2026-10-11T101741Z.md),
taken on base `4e5d563f` with the base binary and a throwaway counter build,
and diagnostic only:
- Of the evaluator's 1.88 G inclusive Ir, 676 M answers structure
  questions, 835 M is emission and its lookups, and 371 M is its own body.
- About 296 M of the structure cost is a range with nothing to find, which
  [#3445](https://github.com/buster14a/buster/issues/3445) can answer without
  a tree.
- No tree slice was built. At the census base, slices 1–2, which this one
  consumes, had not landed, and building their hand-off and index here would
  have duplicated claimed work. Slice 1's hand-off has since landed; slice
  2's range-to-node index has not.

For the checked casts the typer still declines in the operand (236 on the
unity self-host after the failing-operand rule: members, string runs, builtin
calls and `&` over a declined member), these budgets were declared before any
measured run. They use the stage-2 input and flags and four Callgrind arms on
tests-off `-march=x86-64-v3` builds:
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

The base is main `a6705764`, which the candidate branches from. The budgets:
- **Census first.** A throwaway build of the base, not committed, records
  each of those checked casts by exact operand shape: which member rule
  refused it, the length of a string run, the builtin's name, and the operand
  of `&`. It charges the inclusive Ir of each one's machine run. A shape is a
  candidate only when the machine provably appends no row and raises nothing
  for it, or when an exact replay exists. If the candidate shapes' machine Ir
  totals below 1 M, no candidate is built, and the negative result is
  recorded.
- **Correctness.**
  - no verify mismatch over the corpus;
  - identical diagnostics and type-table sizes with and without the tree;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms.
- **The gain that ships it (C against B).** At least 1 M fewer instructions
  in the whole compile, with the eager pass, the query function and any wider
  node flags charged, and fewer machine runs from queries. A smaller gain, or
  a loss, is recorded as a negative result, and the code does not ship.
- **Default path (D against A).** Within ±0.05% Ir on `-fsyntax-only` and on
  `-c` (`-g0`).
- **Acceptance.** Hosted counts are diagnostic. Zen 5 validation (#2761)
  stays incomplete, and the hook stays opt-in.

The census, taken on a tests-off build of the base under Callgrind, counted
254 such checked casts. It fails the 1 M gate for them:
- 141 members of a qualified aggregate, each appending 1 to 3 rows;
- 28 string runs and 23 `&` over a member, which append rows;
- 31 builtin calls, 30 of them appending nothing;
- 31 others, 26 of them appending nothing.

The 56 shapes that append nothing cost 0.71 M machine Ir together. No
candidate is built for them. The same census showed that the member rule
declines 3,650 other queries whose node is itself a member of a const
aggregate. Their base is accepted, and their machine run appends exactly one
row, the member's qualified copy, at 7.8 M machine Ir. For that shape, these
budgets were declared before the candidate's measured runs, on the same arms
and base:
- **The replay.** A declined query whose node is `.` or `->` over an accepted
  base (safe, with constraint checks) and a const or volatile struct or union
  is answered from the decline branch. The answer makes the machine's two calls
  on the query's tokens: `c_parse_member_type` on the qualified aggregate, then
  `c_parse_add_qualified_type`. The row it appends is the answer. Nothing
  changes on the path that ordinary answers take, and the node flags stay
  `u8`.
- **Correctness.** No verify mismatch over the corpus, with the replayed row
  held to the machine's. Identical diagnostics and type-table sizes with and
  without the tree, and byte-identical `-c` objects (`-g0` and `-g`) across
  the four arms. New `CAstTypeCase` probes cover the replay.
- **The gain that ships it (C against B).** At least 1 M fewer instructions
  in the whole compile, and fewer machine runs from queries. Otherwise the
  negative result is recorded, and the code does not ship.
- **Default path (D against A).** Within ±0.05% Ir on `-fsyntax-only` and on
  `-c` (`-g0`).

The census and the replay's hosted measurements are in audit
[`2026-10-11T111859Z`](../../performance-audits/2026-10-11T111859Z.md),
diagnostic only:
- **Census.** The gate fails for the declined checked casts (0.71 M).
- **Correctness.** 0 verify mismatches. Objects are byte-identical at `-g0`
  and `-g` across the four arms.
- **Ship.** C against B is −1.01 M (−0.011%), with 3,650 fewer machine runs
  from queries. That passes the gate by 11.7 K Ir.
- **Default path.** +4 Ir on `-fsyntax-only` and +5 on `-c`.
- **Code layout.** Four earlier versions with the same work measured from
  +21 M to +6 M on the pilot, and up to +0.147% on the default path, because
  the code inlined into `c_parse_expression_type_query` changed. The shipped
  version keeps the whole decline tail in one out-of-line function that
  returns its answer by value.

For a cheaper query-time name check (`c_ast_types_lookups_agree`, the
repeat of the machine's by-spelling lookups that the
[tree expression typer](#tree-expression-typer) makes before it answers),
these budgets were declared before its measured runs. They use the stage-2
input, flags and four Callgrind arms on tests-off `-march=x86-64-v3` builds:
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

The base is main `a6705764`, which the candidate branches from. A throwaway
census build of the base (not committed) found 138,804 checks, 712,144 nodes
walked, 191,184 lookups and no disagreement on the self-host. The budgets:
- **Correctness.** The check may decline more, never less, and on the
  self-host no answer changes:
  - no verify mismatch over the corpus, with probes for any new rule;
  - the typer's `-v` counters on the self-host are identical in B and C;
  - identical diagnostics with and without the flag;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms.
- **The check's own cost (C against B).** `c_ast_types_lookups_agree`'s
  inclusive instructions, plus any increase in `c_ast_types_type_body`'s,
  fall by at least 30%, and the whole compile falls by at least 10 M Ir.
  Otherwise the result is recorded as negative and the code does not ship.
- **Default path (D against A).** Within ±0.05% Ir on `-fsyntax-only` and on
  `-c` (`-g0`). A change outside that band whose callees and call counts are
  identical, with byte-identical objects, passes on work equivalence, as for
  the statement plan (#3370).
- **Performance acceptance.** Hosted counts are diagnostic. No 9700X
  comparison is requested, so Zen 5 validation stays incomplete (#2761) and
  the hook stays opt-in.

The settled name checks' hosted census is
[`2026-10-11T114326Z`](../../performance-audits/2026-10-11T114326Z.md), taken
the same way and diagnostic only:
- Every hosted budget passes. The typer's counters are identical in B and C,
  and the objects of all four arms are identical at `-g0` and `-g`.
- The check falls from 52.48 M to 33.02 M Ir (−37.1%), and the pilot's
  compile by 19.04 M (−0.203%). 138,650 checks settle, and 148 walk.
- The default path is +0.0081% on `-fsyntax-only` and +0.0047% on `-c`.
- Zen 5 validation stays incomplete (#2761).

### Member specifier runs

For the specifier runs of members inside tag definitions
([members](#members)), these budgets were declared before their measured
runs. The census came first. It was a throwaway build of base main `b71dfc89`
on the same input and flags as the declaration specifier read. Each scalar
frame that an aggregate segment or a parenthesized expression leaf pushes ran
as a nested machine run under one no-inline wrapper per shape, under
`-fc-ast-pilot`. The shape was read from the run's `DECL_SPECIFIERS` list as
the read would classify it, appending nothing:

| Frame | Shape | Runs | Ir | Ir per run |
| --- | --- | ---: | ---: | ---: |
| member (aggregate segment) | plain typedef name | 8,445 | 15.1 M | 1,789 |
| member (aggregate segment) | plain primitive words | 1,246 | 4.5 M | 3,591 |
| member (aggregate segment) | tag definition, tag reference, other | 121 | 0.5 M | |
| parenthesized expression leaf (casts) | plain typedef name or words | 3,445 | 5.4 M | 1,565 |
| parenthesized expression leaf (casts) | other | 111 | 0.1 M | |

Scalar frames pushed under any other frame were 74 runs in all. The gate is
the type-name runs' **5 M** of removable Ir per row, where a plain run keeps
the declaration read's cost of about 550 Ir:
- plain member typedef names clear it, at about 10.5 M; primitive members
  (about 3.8 M) take the same mechanism at no extra code;
- plain casts, at about 3.5 M, do not, and would need a read of their own
  inside the expression leaf; they stay with the machine.

The 494 file-scope tag definitions that reach the declaration specifier
read's fallback cost 10.4 M as root runs, members included. Everything in a
tag definition but its members' specifier runs (the segment frames and their
checkpoints, alignment frames, declarators and layout attributes) stays
with the machine.

The input, flags and four Callgrind arms are the declaration specifier
read's. The base is main `b71dfc89`, which the candidate branches from. The
budgets:
- **Correctness.**
  - 0 model differences over the corpus and the frontend sources: the
    analysis model field by field and the bindings, with the diagnostics;
  - a one-off comparison of the same fields on the self-host unity input, also
    with 0 differences;
  - in verify mode, over the same inputs, every member run read from the tree
    is also read by the machine's scalar frame, rolled back, and the two
    answers match: type row, its fields and the declarator start, 0
    mismatches. A tree whose member word is changed must make verify mode
    report a mismatch;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms.
- **Coverage (reported, not gated).** The member runs read and handed back,
  by reason, from the `-v` counters.
- **The read's own effect (C against B).** The whole compile falls by at
  least 7 M Ir, about half the estimate.
- **The default path (D against A).** One test of a null index per member
  run, so the difference stays within ±0.05% Ir.
- **Adoption of the hook as the default.** Unchanged and on hold: acceptance
  on the Zen 5 route (#2761), with no 9700X comparison requested.

The member read's hosted census is
[`2026-10-12T134037Z`](../../performance-audits/2026-10-12T134037Z.md), taken
the same way and diagnostic only:
- Every hosted budget passes:
  - the model, the bindings and the diagnostics match over the corpus, the
    frontend sources and the self-host unity input;
  - verify mode finds every read equal to the machine's scalar frame: 1,790
    reads from the fixtures, 11,151 with the frontend sources and 9,695 on the
    unity input;
  - the objects of all four arms are identical.
- On the self-host 9,691 member runs are read (1,246 primitive, 8,445
  typedef), and 121 go to the scalar frame as `input`.
- The pilot's compile falls by 13.80 M Ir (−0.154%).
- The default path is +0.046%, all of it self cost in
  `c_type_parse_machine_run`, where the new branch is inlined; the calls are
  A's.
- Zen 5 validation stays incomplete (#2761), so the hook stays opt-in.

### Initializer value typing

This section has its own anchor because other open AST branches append to
the plan's final paragraph. After [#3552](https://github.com/buster14a/buster/pull/3552),
`c_parse_infer_initializer_array_count_core` was still the largest
semantic-analysis cost in audit
[`2026-10-11T142104Z`](../../performance-audits/2026-10-11T142104Z.md)'s
census. A Callgrind caller/callee census of main `b504c4e9` on
`-fsyntax-only` (exploratory, base only) splits it:
- **One cost, not two.** `c_parse_validate_initializer_shape` (1,346.8 M Ir
  inclusive) spends 1,200.1 M of that in the infer core's 3,156 calls that
  carry a diagnostic. The core is 1,567.9 M in all.
- **Per-element typing dominates.** `c_parse_initializer_expression_constraint`
  costs 780.7 M under the core over 572,857 calls (605,695 in all, 922.5 M).
  Each call types the element with `c_parse_checked_expression_type`
  (458.6 M). Then `c_parse_incompatible_aggregate_value` (408.2 M) types the
  same range again, unchecked, before it decides the conversion.
- **The walk.** The rest is the walk itself: designators 209.2 M, element
  ends 95.5 M, the core's own 247.1 M, and 45.7 M of
  `c_ir_tokens_are_string_literals` over 968,227 elements. Only 2,154 of
  those elements are aggregates, the one case that reads the answer.
- **Strings.** The literal shape work left after #3552 is 112.4 M under the
  core, and it stays.

A throwaway instrumented build (not committed) compared the two typings.
- Of the 605,678 elements whose checked typing succeeds, 5,848 get a
  different row from the unchecked query (397 under `-fc-ast-pilot`). Every
  one is a cast, whose type name is parsed afresh, as a new row, on every
  query.
- The verdict and message of the conversion check computed from the checked
  row agree with the base's in all 605,678.
- The local scalar initializers in `c_parse_validate_lowering_constraints`
  repeat the same pair 38,959 times, with no row difference.

The candidate:
- reuses the checked type at those two sites when the checked typing
  succeeded (`c_parse_incompatible_checked_value`), and types the range again
  only when it did not;
- asks `c_ir_tokens_are_string_literals` only for aggregate elements.

Debug and sanitized builds (`BUSTER_REFERENCE_CHECKS`) also run the base's
re-typing for every reused value, and require the same verdict and message.

These budgets were declared before the measured runs. The base is main
`b504c4e9`. The input, flags and four Callgrind arms are the count-only first
sight's, above (`--cache-sim=no --branch-sim=no`):
- **Correctness:**
  - no verify mismatch over the corpus;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms;
  - the same exit status and no diagnostics on `-fsyntax-only` in every arm;
  - the reference check passes in a Debug run of the C frontend and AST
    tests;
  - the census expectations in `c_test.c` are unchanged.
- **`-fsyntax-only` (D against A, and C against B).** The whole compile must
  run at least 1 M fewer instructions, and more than the 0.05% rebuild noise
  fewer. Each arm reports its calls to `c_parse_expression_type_query` and
  `c_ir_tokens_are_string_literals`.
- **`-c` (D against A, `-g0`).** The same validation runs before lowering, so
  the compile must also fall by at least 1 M. It may in no case grow by more
  than the 0.05% noise. If it does not fall, the result is recorded as
  negative, and the code does not ship.
- **Acceptance.** Hosted counts are diagnostic. No 9700X comparison is
  requested, so Zen 5 validation stays incomplete (#2761).

The initializer value typing census is
[`2026-10-11T170541Z`](../../performance-audits/2026-10-11T170541Z.md),
diagnostic only. The second candidate passes every budget and ships:
- `-fsyntax-only` falls by 278.1 M Ir on the default path (−2.663%) and by
  220.1 M under the pilot (−2.395%). `-c` falls by 278.2 M (−1.130%).
- 633,657 typings and 966,073 string tests are no longer made.
- Objects are byte-identical across the four arms at `-g0` and `-g`.
- The first candidate reused every valid checked type, which saved 32 M
  more. It dropped 397 unreferenced `char[N]` DWARF types from the `-g`
  object, so it failed the identity budget. The base's re-query had
  created them as type rows for string operands in casts. The candidate
  that ships therefore reuses a type only when its typing appended no row.

For lowering's extent table (#3495, deleted by #4060; see
[statements from nodes](#statements-from-nodes-in-lowering)), these budgets
were declared before its measured runs. The input is the self-host unity
input, at `-g0 -c` (the instruction counts) and at `-g -c` (objects). The
four Callgrind arms (`--cache-sim=no --branch-sim=no`) are counted on
tests-off `-march=x86-64-v3` Release builds, with the driver built by Clang
(no TCC on the host):
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

The base is main `4e5d563f`, which the candidate branches from. The slice is
a migration step, not a speedup. The walkers it replaces cost about 20 M Ir
(0.09%) on this input, so it is gated on exactness and work equivalence,
not on an instruction gain. The budgets:
- **Correctness.**
  - 0 differences over the corpus and the hosted frontend sources, above the
    floors;
  - a one-off verify-mode run on the self-host unity input, also with 0
    differences;
  - both mutations reported by the differential;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms.
- **Coverage.** No self-host body's table is refused (`input` and `anchor`
  are 0). The queries the walker answers for `shape` are reported.
- **The default path (D against A), by work equivalence.** The table is
  never built without a tree. The callees of `c_ir_builder_controlled_body_range`
  (`c_ir_statement_end`, `c_ir_statement_span`, `c_ir_statement_labels_end`,
  `c_ir_statement_do_tail_end`, `c_ir_matching_delimiter_cached`) and their
  call counts must equal the base's, apart from helpers the compiler inlines
  differently. The objects must be byte-identical. The whole compile's Ir is
  reported against the ±0.05% band, but it does not gate.
- **The slice's own effect (C against B).** Reported, not gated. The
  report gives the inclusive Ir of `c_ir_builder_controlled_body_range` with
  the table's construction charged, the walkers' Ir in B, and the whole
  compile. A loss is recorded as one, and the slice still ships on exactness,
  because the owner wants the walker retired.
- **Acceptance.** Hosted counts are diagnostic. Zen 5 validation (#2761)
  stays incomplete, no benchmark request is made, and the hook stays opt-in.

The extent table's hosted census is
[`2026-10-11T115005Z`](../../performance-audits/2026-10-11T115005Z.md), taken
the same way and diagnostic only:
- Correctness, object identity and coverage pass. The corpus, the hosted
  sources and the self-host show 0 differences, and both mutations are seen.
  The four arms' objects are identical, and no self-host body is refused.
  12 queries are walked for `shape`.
- The default path passes by work equivalence: the same callees and call
  counts. It is +0.0025% Ir.
- **The table costs more than the walkers it replaces.** C against B is
  +13.8 M (+0.060%). `c_ir_builder_controlled_body_range` rises from
  15.7 M to 29.3 M inclusive, with the table's construction charged. A
  first candidate cost +19.2 M. The slice ships on exactness, as declared,
  and is not a speedup.
- Acceptance stays with Zen 5 (#2761), so the default stays off.

For map row 7 in lowering (`c_ir_group_type_name`, `c_ir_query_type_name` and
the query machine's `TYPE_NAME` frames), a census came first:
[`2026-10-11T104652Z`](../../performance-audits/2026-10-11T104652Z.md), on
main `4e5d563f`, hosted and diagnostic. No candidate was built:
- Positive answers still parse the type, so only negative answers are
  removable. On the unity self-host there are 70,007 negative `TYPE_NAME`
  attempts and 10,448 negative group probes.
- Most of them begin with a token that can never begin a type name: a
  literal, a number, a punctuator other than `[`, or `sizeof`. The constant
  evaluator's cast probe asks without the existing token gate and memo. A
  throwaway build that routes it through `c_ir_query_group_type_name` runs
  32.7 M fewer instructions (−0.136%) on the default path, with byte-identical
  objects. That is a default-path change in its own right, measured on its
  own ([#3473](https://github.com/buster14a/buster/issues/3473)).
- After that gate, a tree answer ("no `TYPE_NAME` anchored at the first
  token, so negative") could still remove at most about 26 M (0.11%), almost
  all of it unbound identifiers whose scope lookup the probe runs.

If that tree slice is built, after the gate has landed, these budgets apply.
It reads the tree through the hand-off to lowering
([lowering from the tree](#lowering-from-the-tree)), which landed after this
census.
They use the unity self-host, `-g0 -c`, and the usual four Callgrind arms on
tests-off `-march=x86-64-v3` builds (A base default, B base pilot, C candidate
pilot, D candidate default):
- **Correctness.** A test-only verify mode answers every negative both ways
  over the corpus and the hosted frontend sources, with 0 disagreements and a
  floor on tree answers. A probe whose first token has a `TYPE_NAME` node
  always runs. The `-c` objects are byte-identical at `-g0` and `-g` across
  the four arms. No node id or `CAst*` reaches canonical IR.
- **The slice's own effect (C against B).** Fewer instructions in the whole
  compile, with the anchor index charged, by at least 10 M; below that the
  slice is recorded as a negative result and not shipped.
- **Default path (D against A).** Within ±0.05% Ir.
- **Performance acceptance.** Hosted counts are diagnostic; Zen 5 validation
  stays incomplete (#2761) and the hook stays opt-in.

For a further cut in the eager pass (#3102), these budgets were declared
before its measured runs. The change keeps every rule. It splits the pass into
a leaf loop and an operator loop over a list, reads cached integer-literal
facts without the leaf call, memoizes the usual arithmetic conversions per
pass, and settles the checked-mode rule for two integer operands at once. The
budgets use the stage-2 input, flags and four Callgrind arms
(`--cache-sim=no --branch-sim=no`) on tests-off `-march=x86-64-v3` builds,
with base main `a6705764`:
- **Correctness.** No answer changes:
  - no verify mismatch over the corpus;
  - the typer's `-v` counters on the self-host are identical in B and C;
  - identical diagnostics with and without the flag;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms.
- **The pass's own cost (C against B).** The inclusive cost of
  `c_ast_types_type_body`, which is the cost charged at its call sites, falls
  by at least 12%. The whole compile falls by at least the pass's saving less
  3 M, which covers the region's two extra scratch arrays, so the saving is
  not moved elsewhere.
- **Default path (D against A).** The change is confined to the typer, so the
  default path stays within ±0.05% Ir on `-fsyntax-only` and on `-c` (`-g0`).
- **Performance acceptance.** Hosted counts are diagnostic. No 9700X
  comparison is requested, so Zen 5 validation stays incomplete (#2761) and
  the hook stays opt-in.

The further cut's hosted census is
[`2026-10-11T110050Z`](../../performance-audits/2026-10-11T110050Z.md), taken
the same way and diagnostic only:
- Every hosted budget passes. The typer's counters are identical in B and C,
  and the objects of all four arms are identical.
- `c_ast_types_type_body` falls from 329.2 M to 274.2 M Ir (−16.7%), and the
  pilot's compile by 54.2 M (−0.58%).
- The default path is within −0.001% on `-fsyntax-only` and +0.001% on `-c`.

### Call-argument value typing

This section has its own anchor, because other open AST branches append to
the plan's final paragraph. Audit
[`2026-10-11T170541Z`](../../performance-audits/2026-10-11T170541Z.md) left
one lead from [initializer value typing](#initializer-value-typing): call
arguments get the same double typing. In
`c_parse_validate_const_assignments`, each argument is typed with
`c_parse_checked_expression_type`. When the callee has a parameter for it,
`c_parse_incompatible_aggregate_value` then types the same range again,
unchecked, before it decides the conversion.

A Callgrind census of main `b71dfc89` (base only, exploratory, not reused as
an arm) on the unity self-host at `-fsyntax-only`, default flags, gives the
size of the lead:
- The checked typing of arguments is 145,245 calls (284.4 M Ir inclusive).
- `c_parse_incompatible_aggregate_value` is inlined into the validator. At
  its two sites there (call arguments and assignments) it makes 189,839
  unchecked queries, which cost 28.4 M, and the conversion checks cost
  72.9 M.
- The second query is cheap, about 150 Ir, because the per-body memo
  answers an unchecked probe from the checked answer. What it still pays is
  `c_parse_type_identity_prepare`, the lookup, and a full run where the
  memo holds nothing, such as after a constraint message. The expected
  saving is therefore about 20 M (0.2%), not the 82.9 M the earlier audit
  charged to the whole wrapper. That figure included the conversion check,
  which stays.

The candidate applies the shipped rule from
[initializer value typing](#initializer-value-typing) at the call-argument
site. `c_parse_incompatible_checked_value` reuses the checked type when the
checked typing succeeded and appended no type row. Otherwise it types the
range again. Reference-check builds (`BUSTER_REFERENCE_CHECKS`) also run the
base's re-typing and require the same verdict and message. The assignment
site is not part of this slice. The same query feeds both the default path
and `-fc-ast-pilot`, because the tree typer answers through
`c_parse_expression_type_query`.

These budgets were declared before the measured runs. The base is main
`b71dfc89`. The input and flags are the initializer slice's, with four
Callgrind arms (`--cache-sim=no --branch-sim=no`) on tests-off
`-march=x86-64-v3` Release builds whose driver is built by Clang (no TCC on
the host). A is the base with default flags, B the base with `-fc-ast-pilot`,
C the candidate with `-fc-ast-pilot`, and D the candidate with default flags.
- **Correctness:**
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms;
  - the same exit status and no diagnostics on `-fsyntax-only` in every arm;
  - the reference check passes in a Debug run of the C frontend and AST
    tests, and over the unity self-host input in a Debug `ide`;
  - the census expectations in `c_test.c` are unchanged, or any change is
    explained by the removed query;
  - `test_all` passes, and `test_self_host` keeps its fixed point.
- **The floor, `-fsyntax-only` (D against A, and C against B).** The whole
  compile must run at least 5 M fewer instructions, and more than the 0.05%
  rebuild noise fewer. Each arm reports its calls to
  `c_parse_expression_type_query`.
- **`-c` (D against A, `-g0`).** The same validation runs before lowering, so
  the compile must also fall by at least 5 M. It may in no case grow by more
  than the 0.05% noise.
- **Below the floor.** The audit records a negative result, and no code
  ships.
- **Acceptance.** Hosted counts are diagnostic. No 9700X comparison is
  requested, so Zen 5 validation stays incomplete (#2761).

The call-argument census is
[`2026-10-12T131052Z`](../../performance-audits/2026-10-12T131052Z.md),
diagnostic only. The candidate passes every hosted budget and ships:
- `-fsyntax-only` falls by 27.4 M Ir on the default path (−0.254%) and by
  30.2 M under the pilot (−0.336%). `-c` falls by the same amounts (−0.108%
  and −0.127%).
- 126,768 queries (141,840 under the pilot) are no longer made.
- Objects are byte-identical across the four arms at `-g0` and `-g`.
- The gain is small because the memo already answered most second typings.
  The assignment site in the same validator, which pairs a checked typing
  with the same wrapper, is left as a lead.

### Assignment value typing

This section has its own anchor, because other open AST branches append to
the plan's final paragraph. [Call-argument value typing](#call-argument-value-typing)
left the assignment site in `c_parse_validate_const_assignments` as a lead.
A simple or compound assignment types its value with
`c_parse_checked_expression_type`. Unless the assignment conversion message
already refused it, `c_parse_incompatible_aggregate_value` then types the same
range again, unchecked, before the aggregate and scalar conversion check.

A census of main `4ed35b59` (a throwaway build that routes the site through
noinline wrappers, exploratory, not reused as an arm) on the unity self-host
at `-fsyntax-only` gives the size of the lead:
- The site makes 46,529 unchecked queries. They cost 8.27 M Ir inclusive,
  about 178 Ir each, under both default flags and `-fc-ast-pilot`.
- Under #3806's rule, 45,303 of them could reuse the checked type on the
  default path and 46,301 under the pilot. The rest follow a typing that
  appended a type row or did not succeed.
- The removable ceiling is therefore about 8 M (0.07%), plus whatever setup
  Clang inlined around the call. The conversion check stays.

The candidate routes the site through `c_parse_incompatible_checked_value`,
which gains a `simple_value_conversion` argument so a compound assignment
keeps its scalar conversion rule. The type is reused only when the checked
typing succeeded and appended no type row; otherwise the range is typed
again. Reference-check builds (`BUSTER_REFERENCE_CHECKS`) also run the base's
re-typing and require the same verdict and message. The change is on the
token path, so it applies to the default path and to `-fc-ast-pilot`.

These budgets were declared before the measured runs. The base is main
`4ed35b59`. The input, flags and arms are the call-argument slice's: four
Callgrind arms (`--cache-sim=no --branch-sim=no`) on tests-off
`-march=x86-64-v3` Release builds whose driver is built by Clang (no TCC on
the host). A is the base with default flags, B the base with `-fc-ast-pilot`,
C the candidate with `-fc-ast-pilot`, and D the candidate with default flags.
- **Correctness:**
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms;
  - the same exit status and no diagnostics on `-fsyntax-only` in every arm;
  - the reference check passes in a Debug run of the C frontend and AST
    tests;
  - `test_all` passes, and `test_self_host` keeps its fixed point.
- **The floor, `-fsyntax-only` (D against A, and C against B).** The whole
  compile must run at least 5 M fewer instructions, and more than the 0.05%
  rebuild noise fewer. Each arm reports its calls to
  `c_parse_expression_type_query`; the candidate should make about 45,000
  fewer.
- **`-c` (D against A, `-g0`).** The validation runs before lowering, so the
  compile must also fall by at least 5 M. It may in no case grow by more
  than the 0.05% noise.
- **Below the floor.** The audit records a negative result, and no code
  ships.
- **Acceptance.** Hosted counts are diagnostic. No 9700X comparison is
  requested, so Zen 5 validation stays incomplete (#2761).

The assignment census is
[`2026-10-12T173747Z`](../../performance-audits/2026-10-12T173747Z.md),
diagnostic only. The candidate passes every hosted budget and ships:
- `-fsyntax-only` falls by 8.17 M Ir on the default path (−0.073%) and by
  8.36 M under the pilot (−0.089%). `-c` falls by the same amounts (−0.032%
  and −0.035%).
- 45,303 queries (46,301 under the pilot) are no longer made, exactly the
  census's reusable count.
- Objects are byte-identical across the four arms at `-g0` and `-g`.
- The validator's remaining unchecked queries follow a typing that appended
  a row or failed, and are kept for `-g` identity.
  `c_parse_type_identity_prepare`, which every query pays first, is still a
  lead.

### Initializer walk

This section has its own anchor because other open AST branches append to
the plan's final paragraph. After [#3624](https://github.com/buster14a/buster/pull/3624)
[reused each element's checked type](#initializer-value-typing), the walk in
`c_parse_infer_initializer_array_count_core` was left unclaimed at about
550 M Ir. Designators (`c_parse_initializer_designator`, about 216 M) are a
separate claim and are not touched here.

A census came first. It used Callgrind caller/callee and per-line self costs
on main `b71dfc89` (tests-off `-march=x86-64-v3` Release, unity self-host
input), plus a throwaway counting build that was not committed. It splits the
rest of the walk, which costs the same on `-fsyntax-only` and on `-c`:
- **The core's own instructions, 262.2 M.** About 31.5 M of them is the
  brace-only `c_parse_matching_delimiter` scan, inlined at the list's outer
  braces and at each brace-enclosed element (166,362 elements,
  2,661,886 tokens).
- **Element ends, 95.8 M** (`c_parse_initializer_value_end`, 1,135,463
  calls, 4,300,283 tokens). Brace-enclosed aggregate elements account for
  2,828,248 of those tokens, but they never read the end: that branch uses
  the matching `}`. Scalar elements scan 1,430,619 tokens, and brace-elided
  aggregate elements scan 41,416.
- **Repeat walks.** 579 shape validations walk again, with their element
  checks, the 1,197,089 tokens that array-bound inference walked first. The
  two walks run at different times and do different checks, so merging them
  is a design change and not part of this slice. The elided elements'
  own re-walk is 637 tokens, which is under the floor.
- **Designator call overhead.** 29.5 M of the core's own instructions are at
  the `c_parse_initializer_designator` call: its arguments and the zeroed
  result. They go with the designator claim.

The candidate is an exact cut on the default path, with no tree:
- the brace-enclosed element takes its `}` before, and instead of, its end
  scan;
- that `}`, and the list's own, come from `c_parse_type_delimiter_close`:
  the unit's pair table when its delimiters nest, and the same-kind scan
  otherwise, as [#3667](https://github.com/buster14a/buster/pull/3667) does
  for tag bodies;
- the end scan tests each token against one punctuator set before it looks
  at which delimiter it is.

Debug and sanitized builds (`BUSTER_REFERENCE_CHECKS`) recompute the scans
the candidate skips, and check that they agree.

These budgets were declared before the measured runs. The base is main
`b71dfc89`. The input is the base checkout's unity self-host source, compiled
with `-target x86_64-unknown-linux -march=znver5 -g0`, on `-fsyntax-only` and
on `-c`. The four Callgrind arms (`--cache-sim=no --branch-sim=no`) are
counted on tests-off `-march=x86-64-v3` Release builds, with the driver built
by Clang (no TCC on the host):
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

The budgets:
- **Correctness:**
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms;
  - the same exit status and no diagnostics on `-fsyntax-only` in every arm;
  - the reference checks pass in Debug runs of the C frontend, AST and
    driver tests;
  - Release `test_all` passes, and `test_self_host` keeps its fixed point.
- **The floor (D against A, and C against B).** Each endpoint, `-fsyntax-only`
  and `-c` (`-g0`), must run at least 1 M fewer instructions, and more than
  the 0.05% rebuild noise fewer. If either endpoint misses, the result is
  recorded as negative and the code does not ship.
- **Each part.** Each of the three parts must remove at least 1 M Ir on both
  endpoints, judged from the Callgrind line and callee costs of D against A.
  A part that does not is dropped and recorded.
- **Reported, not gated:** the inclusive and own Ir of the core, and the
  calls and Ir of `c_parse_initializer_value_end`.
- **Acceptance.** Hosted counts are diagnostic. No 9700X comparison is
  requested, so Zen 5 validation stays incomplete (#2761).

The initializer walk's census is
[`2026-10-12T134729Z`](../../performance-audits/2026-10-12T134729Z.md),
diagnostic only. Every budget passes, and the candidate ships:
- `-fsyntax-only` falls by 90.5 M Ir on the default path (−0.839%) and by
  90.4 M under the pilot (−1.005%). `-c` falls by 90.3 M (−0.354%).
- The parts: the skipped end scans −59.5 M, the pair table −26.7 M with its
  helper charged, and the set test −4.4 M (−3.7 M on `-c`).
- Objects are byte-identical across the four arms at `-g0` and `-g`.
- Still open: designators, the 579 shape validations that re-walk an inferred
  list, and the scalar end scans.

### Initializer element replay

This section has its own anchor because other open AST branches append to
the plan's final paragraph. [The initializer walk](#initializer-walk) left
two items open: the shape validations that walk a list again after
array-bound inference walked it, and the scalar element end scans.
Designators stay with [#3579](https://github.com/buster14a/buster/issues/3579)
and are not touched here.

**Census.** It was taken on main `f0f84944` (tests-off `-march=x86-64-v3`
Release, unity self-host input) from one Callgrind run per endpoint and a
throwaway counting build, neither committed. Inference and validation are
`c_parse_infer_file_array_bounds` and `c_parse_validate_initializer_shape`,
both with `c_parse_infer_initializer_array_count_core` as the walk:
- **The walks.** Inference reaches the walk's frame loop for 206 lists
  (1,124,520 tokens). Its walks cost 305.4 M Ir inclusive, of which 112.4 M
  is the string-literal count of `char x[] = "..."` lists, which never
  reach the loop. Validation's walks cost 914.8 M on `-fsyntax-only`,
  569.9 M of it in the element checks
  (`c_parse_initializer_expression_constraint`).
- **The repeat walks.** 580 validations walk a list whose bound was
  inferred (1,197,700 tokens, 394,823 element checks). 374 of them have no
  inference walk to reuse: their bound came from a string or from another
  inference site.
- **The exact share.** 94 lists (784,791 tokens, 326,329 element checks)
  were walked by inference with no designator, no brace elision, no vector
  and no array bound evaluated. The other 112 lists have designators (109)
  or evaluate an array bound (3).
- **Scalar ends.** `c_parse_initializer_value_end` runs for 966,972 scalar
  elements (1,431,095 tokens). 913,180 of those elements are one token long.
  The scan costs 48.1 M Ir, about 50 a call, plus 6.8 M at the call site.

**Why there are two walks.** Inference runs in `c_analyze_semantics_core`
before the function bodies are bound, with diagnostics off and, in 205 of
the 206 walks, with `semantic_constant_queries` off. Its count fixes the
array's type, and everything after it reads that type. Validation runs
much later, in `c_parse_validate_static_initializers`: after every bound is
final (`inferred_bounds_final`), with type interning on, and under
`-fc-ast-pilot` inside the tree typer's initializer region. Its element
checks type each value, append type rows and make the first diagnostic.
Moving the checks into inference would type them before the bounds and
rows they read are final, and in a different order. So the checks stay where
they are.

**One structural walk.** The design keeps one structural walk and moves
nothing else:
- **Inference records.** While inference walks a file-scope list, it writes
  each element check it would make, as a destination type and a token
  range, in walk order. It writes into the walk's scratch, at most one
  entry for every two tokens. After a successful walk, the log is copied to
  the phase arena with the list's range and inferred count, and the array
  bound indexes it.
- **Validation replays.** Validation looks the bound up. When the log is
  there and its range and count still match, validation makes the same
  element checks in the same order, with the same scope, destination and
  diagnostic sink. It stops at the first failure, as the walk does. The
  count and the verdict then come from the same structural walk.
- **Where a log is refused.** The log is kept only when the walk's path
  depends on nothing but tokens, the delimiter pair table, the member lists
  of complete types and the bound being inferred. A walk that meets a
  designator, brace elision, a vector, an array bound to evaluate, the
  nested string branch or an aggregate-expression query keeps no log. So
  does a walk whose log does not fit the scratch. Those lists, local
  arrays and compound literals walk again, exactly as before.

**What stays exact.**
- **Same structure.** With the log's conditions, the validation walk takes
  the same path. Its root has a bound equal to the inferred count, where
  inference's root was unbounded. Every root element index is below that
  count, and the root's element type is the same row, so no element changes.
- **No structural diagnostics.** Inference succeeded on the same structure,
  so the separator, slot and excess-element failures that only validation
  can report cannot occur. The first diagnostic is the first failing
  element check in either version, with the same token and container.
- **Same queries.** The skipped walk makes no type query of its own on these
  lists, so the typer, the type table and the pilot's region see the same
  sequence of queries.
- **Reference checks.** Debug and sanitized builds (`BUSTER_REFERENCE_CHECKS`)
  run the skipped walk in a record-only mode behind every replay. They check
  that it succeeds and that it records the same elements.

**Scalar ends.** A scalar element of one token ends at the next token when
that token is a comma or the list's limit, and the element's own token is
not one of the scan's seven punctuators. The walk takes that end inline and
calls the scan for every other element. The scan's answer is the same, so
this part is exact by construction. Reference builds compare the two.

**Budgets.** These budgets were declared before the measured runs. The base
is main `f0f84944`. The input is the base checkout's unity self-host source,
compiled with `-target x86_64-unknown-linux -march=znver5 -g0`, on
`-fsyntax-only` and on `-c`. The four Callgrind arms (`--cache-sim=no
--branch-sim=no`) are counted on tests-off `-march=x86-64-v3` Release builds,
with the driver built by Clang (no TCC on the host):
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

The budgets:
- **Correctness:**
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms;
  - the same exit status and no diagnostics on `-fsyntax-only` in every arm;
  - the reference checks pass in Debug runs of the C frontend, AST and
    driver tests, and in Debug compiles of the unity input with and without
    `-fc-ast-pilot`;
  - Release `test_all` passes, and `test_self_host` keeps its fixed point.
- **The floor (D against A, and C against B).** Each endpoint,
  `-fsyntax-only` and `-c` (`-g0`), must run at least 5 M fewer
  instructions, and more than the 0.05% rebuild noise fewer. If either
  endpoint misses, the result is recorded as negative and the code does not
  ship.
- **Each part.** The replay and the inline scalar end must each remove at
  least 5 M Ir net on both endpoints, judged from D against a throwaway
  variant without that part. A part that does not is dropped and recorded.
- **Memory.** The logs live in the phase arena, which the analysis
  releases when it returns. They take at most 12 bytes per logged element
  and one word per array bound. Their size is reported.
- **Reported, not gated:** the inclusive Ir of the walk and of
  `c_parse_validate_initializer_shape`, the calls and Ir of
  `c_parse_initializer_value_end`, and the replayed lists and elements.
- **Acceptance.** Hosted counts are diagnostic. No 9700X comparison is
  requested, so Zen 5 validation stays incomplete (#2761).

The replay's census is
[`2026-10-12T210727Z`](../../performance-audits/2026-10-12T210727Z.md),
diagnostic only. Its raw Callgrind outputs, patches and logs are kept under
`evidence/2026-10-12T210727Z/`. The measuring session reported them, and
nobody has reproduced them independently. By that evidence, every budget
passes, and the candidate ships:
- `-fsyntax-only` falls by 140.9 M Ir on the default path (−1.264%) and by
  141.3 M under the pilot (−1.522%). `-c` falls by 139.8 M (−0.555%) and
  140.2 M (−0.600%).
- The parts: the replay −128.1 M, and the inline end −20.4 M (−19.7 M on
  `-c`). Validation falls by 149.2 M, and inference pays 7.8 M for its
  logs.
- 94 validation walks become replays of 326,329 element checks. The logs
  take 3.97 MB of phase arena.
- Objects are byte-identical across the four arms at `-g0` and `-g`.
- Still open: designated lists, which walk twice; lists that evaluate an
  array bound; and the 45,232 end scans of longer elements.

### Root-conditional `?` refusal without the tree

For the tree-free negative test in `c_ir_root_conditional` (a per-body prefix
count of `?` tokens, built in lowering scratch the first time a query asks),
these budgets were declared before its measured runs. It is a default-path
change and needs no tree, so it is measured as its own arm, apart from any
tree slice that answers the same predicate. Two arms are counted with
Callgrind (`--cache-sim=no --branch-sim=no`) on tests-off `-march=x86-64-v3`
Release builds, on the self-host unity input:
- A: base, default flags;
- D: candidate, default flags.

The base is main `4e5d563f`, which the candidate branches from. The budgets:
- **Correctness.** The answer is exact by construction: the scan can return
  true only at a `?` in its range, and the count uses the scan's own token
  test. Reference-check builds run the scan behind every refused range and
  check it agrees. `-c` objects (`-g0` and `-g`) are byte-identical between
  A and D, `test_self_host` keeps its fixed point, and `test_all` passes.
- **The predicate's own cost (D against A, `-c` `-g0`).**
  `c_ir_root_conditional`'s inclusive instructions, with the prefix build
  charged, fall by at least 50%.
- **The whole compile (D against A).** `-c` (`-g0`) falls by at least 0.2%
  Ir. `-fsyntax-only` runs no lowering, so it must stay within the ±0.05%
  rebuild noise.
- **The same pattern elsewhere.** `c_ir_has_root_comma` and the scanning
  arm of `c_ir_group_fact` ship a count only if each one alone meets the
  same 50% inclusive budget and moves `-c` by at least 0.05%. Otherwise the
  audit records the negative result.
- **Performance acceptance.** Hosted counts are diagnostic. No 9700X
  comparison is requested. The report-only main comparison runs after the
  merge, and Zen 5 validation stays incomplete (#2761).

The negative test's hosted census is
[`2026-10-11T102547Z`](../../performance-audits/2026-10-11T102547Z.md), taken
the same way and diagnostic only:
- The objects are byte-identical between A and D, at `-g0` and at `-g`.
- `-c` (`-g0`) falls by 96.5 M Ir (−0.40%). That is about 62% of the
  predicate's 154.6 M, with the 16.4 M prefix build charged.
- `-fsyntax-only` moves by −0.002%.
- A `,` count for `c_ir_has_root_comma` measured −0.007% and did not ship.
- `c_ir_group_fact`'s scanning arm is below the bar by census.
- The tree slice ([lowering from the tree](#lowering-from-the-tree)) landed
  first and was measured without this refusal. When lowering has a tree for a
  body it takes the tree's answers, so the count is built only on the
  default path and for bodies the tree does not index.
- Zen 5 validation stays incomplete (#2761).

### Identity prepare gate

This section has its own anchor because other open AST branches append to
the plan's final paragraph. `c_parse_type_identity_prepare` settles the
`_Generic`, `__builtin_types_compatible_p` and `__builtin_classify_type`
answers in a range before a query reads them. Every expression-type query,
every scalar specifier attempt, every typed constant and both pilot
specifier reads pay one out-of-line call to it before anything else, and
audit [`2026-10-12T131052Z`](../../performance-audits/2026-10-12T131052Z.md)
counted 1.78 M calls. #3570 recorded that the call never changed a result on
any input it tried, and left open whether it was needed.

A throwaway census build of main `4ed35b59` (not committed) counted each
call site's work on the unity self-host at `-fsyntax-only`, using the input
and flags of [call-argument value typing](#call-argument-value-typing):
- 1,658,911 calls with default flags and 1,640,181 under `-fc-ast-pilot`.
  1,458,569 and 1,439,920 of them are `c_parse_expression_type_query`'s. The
  rest are the scalar specifier attempt, the pilot's two specifier reads,
  `c_parse_typed_constant` and `c_parse_validate_generic_duplicates`.
- The input holds no identity site. The position index lists none, so
  every call finds its range empty. None visits a site, records a row,
  appends a type or reports a diagnostic. No call finds the index missing,
  and no call is nested inside another.
- The base's Callgrind profile (default flags) puts 53.1 M Ir inclusive in
  the function. Almost none of it is lookup, because both binary searches run
  over an empty list. The rest is the call itself, the guard and the save
  and restore of a large frame. The callers also copy the by-value
  `CPreprocessResult` argument, which that figure does not include.

The candidate splits the function. An inline gate answers from the position
index alone and returns when the index lists no site in `[start, end)`: an
empty list, a last site before `start`, or a first site at or after `end`.
Those are the ranges whose two lower bounds are equal, where the base's loop
runs no iteration, so the skipped call is exactly a no-op. Every other range,
and a missing index (which makes the base scan tokens), still calls the
out-of-line body, unchanged. Reference-check builds (`BUSTER_REFERENCE_CHECKS`)
check that the two lower bounds are equal for every skipped range. The gate
is on the default path, because it needs nothing from the tree.

These budgets were declared before the measured runs. The base is main
`4ed35b59`. The four Callgrind arms are those of
[call-argument value typing](#call-argument-value-typing): tests-off
`-march=x86-64-v3` Release builds with a Clang-built driver, A the base with
default flags, B the base with `-fc-ast-pilot`, C the candidate with
`-fc-ast-pilot`, and D the candidate with default flags.
- **Correctness:**
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms;
  - the same exit status and no diagnostics on `-fsyntax-only` in every arm;
  - the reference check passes in a Debug run of the C frontend and AST
    tests, and over the unity self-host input in a Debug `ide`;
  - a focused regression shows the gate still settles a `_Generic`, a
    `__builtin_types_compatible_p` and a `__builtin_classify_type` site
    inside a query's range, and skips a range beside one;
  - `test_all` passes, and `test_self_host` keeps its fixed point.
- **The floor, `-fsyntax-only` (D against A, and C against B).** The whole
  compile must run at least 5 M fewer instructions, and more than the 0.05%
  rebuild noise fewer. Each arm reports its calls to the out-of-line body.
- **`-c` (D against A, `-g0`).** The same validation runs before lowering,
  so the compile must also fall by at least 5 M. It may in no case grow by
  more than the 0.05% noise.
- **Below the floor.** The audit records a negative result, and no code
  ships.
- **Acceptance.** Hosted counts are diagnostic. No 9700X comparison is
  requested, so Zen 5 validation stays incomplete (#2761).

The gate's hosted census is
[`2026-10-12T175829Z`](../../performance-audits/2026-10-12T175829Z.md),
diagnostic only. The candidate passes every hosted budget and ships:
- `-fsyntax-only` falls by 62.6 M Ir on the default path (−0.559%) and by
  69.4 M under the pilot (−0.741%). `-c` falls by the same amounts (−0.245%
  and −0.292%).
- No call reaches the walk on the self-host. 53.1 M of the saving is the
  function itself, and the rest is the callers' argument copy and call setup.
- Objects are byte-identical across the four arms at `-g0` and `-g`.
- The walk is still needed where a range holds a site. A gate that always
  skips fails 45 fixtures in `c_frontend_tests`.

### Settling the name checks

This section has its own anchor because other open AST branches append to
the plan's final paragraph. After the settled name checks above,
`c_ast_types_lookups_settled` is most of what the
[tree expression typer](#tree-expression-typer)'s name checks still cost.
A throwaway census build of main `b71dfc89` (not committed), on the stage-2
input under `-fc-ast-pilot`, found:
- 132,295 settled checks and 179 walks, over 6,022 settlements;
- 1.93 M flag bytes scanned, for 125,907 lookup marks, 106 of them "other";
- one table build per compile;
- every one of the 125,801 file-scope lookups answered by the binding array,
  so no lookup walks a chain.

Its Callgrind split of the settlement, with noinline census wrappers:
- 40.0 M inclusive in all;
- 6.5 M of query-time checks;
- 11.9 M in the flag scan;
- 19.4 M in the per-mark checks, of which 6.8 M is the file-scope lookup
  call and 3.8 M is finding a call's callee through
  `c_ast_types_first_child`;
- 2.3 M in the one table build.

The candidate keeps every rule. It changes how the facts are found:
- it answers each symbol's file-scope lookup once per lookup generation, from
  the table the build already clears;
- it reads a call's callee token beside its `(`, where the call rule
  requires the callee to be;
- it scans only the mark bytes of each flag word.

These budgets were declared before its measured runs. They use the same
stage-2 input, flags and four Callgrind arms (A to D) on tests-off
`-march=x86-64-v3` builds, with main `b71dfc89` as the base:
- **Correctness.** No answer changes:
  - no verify mismatch over the corpus, with the existing
    `c_ast_test_type_lookups` probes plus any new case;
  - the typer's `-v` counters on the self-host, `lookups_settled` and
    `lookups_walked` included, are identical in B and C;
  - identical diagnostics with and without the flag;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms.
- **The settlement's own cost (C against B).**
  `c_ast_types_lookups_settled`'s inclusive instructions, plus any increase in
  `c_ast_types_type_body`'s, fall by at least 25%, and the whole compile falls
  by at least 6 M Ir. Otherwise the result is recorded as negative and the
  code does not ship.
- **Default path (D against A).** Within ±0.05% Ir on `-fsyntax-only` and on
  `-c` (`-g0`).
- **Performance acceptance.** Hosted counts are diagnostic. No 9700X
  comparison is requested, so Zen 5 validation stays incomplete (#2761) and
  the hook stays opt-in.

The settlement's hosted census is
[`2026-10-12T134656Z`](../../performance-audits/2026-10-12T134656Z.md), taken
the same way and diagnostic only:
- Every hosted budget passes. The typer's counters are identical in B and C,
  and the objects of all four arms are identical at `-g0` and `-g`.
- The settlement falls from 34.96 M to 24.28 M Ir (−30.5% with the eager
  pass's +35 K charged), and the pilot's compile by 10.78 M (−0.120%). Lookups
  in the settlement fall from 125,801 to 11,435, one per symbol.
- The default path is −0.0027% on `-fsyntax-only` and −0.0001% on `-c`.
- About 14 M remains, spread over the scan, the per-mark binding loads and the
  query-time checks.
- Zen 5 validation stays incomplete (#2761).

### Tag definition machine work

This covers the rest of a tag definition after [members](#members): the
aggregate segment frames and their checkpoints, the alignment frames, member
declarators and layout attributes. These rules were declared before any
candidate. They keep the [member specifier runs](#member-specifier-runs)
gate: a shape is built only if it removes at least **5 M Ir** on the
self-host unity input. Two things are charged against it:
- **What any read could remove.** This is an oracle's drop: a throwaway build
  that skips the machine's work on the members the shape covers, with
  byte-identical objects.
- **What the read must spend to be exact.** This is the measured cost of the
  tree test that admits those members.

A shape that clears the gate would take the member read's budgets:
- 0 model differences;
- a verify mode with a mutation check;
- byte-identical `-g0` and `-g` objects across the four arms;
- C − B of at least the gate;
- D − A within ±0.05%.

The census is
[`2026-10-12T175251Z`](../../performance-audits/2026-10-12T175251Z.md), a
throwaway build of base main `4ed35b59` under `-fc-ast-pilot`, hosted and
diagnostic only:
- **Frame costs.** Inside tag definitions, the 9,868 segment frames cost
  18.35 M Ir, 4.21 M of it the member read, which stays. The alignment frames
  cost 1.90 M and the 1,300 range frames 1.74 M.
- **Plain members.** 98.2% of segments are plain: only words, `*`, `[...]` and
  `,`, with no decoration word. On them, every decoration scan finds nothing:
  - the attribute skips;
  - the alignment frame;
  - the layout and vector scans;
  - the `_Static_assert` test.
- **The oracle.** Skipping those scans removes **7.71 M** with identical
  objects. Split by the rows above, no row reaches 2.5 M.
- **The tree test.** An exact test walks the member's nodes and checks its
  names and words. It answers 97.7% of members and costs 3.32 M; #3812's
  member read gets 0.18 M cheaper.
- **Net:** 4.57 M, **below the gate**. A lean test without the specifier
  list's checks costs 2.44 M, but it is not exact. A `__extension__` word, or
  a typedef named `aligned`, changes what the machine does.

No code was written. #3812's inlined branch already uses +0.046% of the
default path's ±0.05% band in the same frame step. The segment frames,
alignment frames, declarators and layout attributes stay with the machine.
Zen 5 validation stays incomplete (#2761).

**The one disagreement is benign.** The census left one member unidentified:
the tree test admitted it and the token predicate rejected it. A later rerun
of both instruments, audit
[`2026-10-12T205005Z`](../../performance-audits/2026-10-12T205005Z.md), logged
every segment frame where they disagree. It ran on main `f0f84944` over three
inputs only: the unity self-host input under `-fc-ast-pilot`, `c_ast_tests` and
`c_frontend_tests`. The audit keeps the instrument and the raw rows. The member is
`u32 fallback_opcode_counts[IR_OPCODE_COUNT + 1];` in `CodegenStatistics`
(`codegen.h`). Its tree is `(declarator_array (declarator_name ...) (add
(identifier IR_OPCODE_COUNT) (number 1)))`. The member carries no decoration,
so the tree is right. The token predicate is stricter than it needs to be: it
admits only words, numbers, `*`, `[`, `]` and `,`, so an operator other than
`*` in an array size fails it. So do the parentheses that the tree does not
keep as nodes. The test corpora add only `char c[(68) - 4];`, which is the
same case. On those inputs every other disagreement goes the other way,
where the tree declines and the token predicate admits:
- **Tagged specifiers.** These are `struct`, `union`, `enum` and
  `_Atomic struct` heads. They have no word-only list, so the tree test does
  not look at them.
- **One fixture, `int first [[maybe_unused]];`.** The tree holds an
  `ATTRIBUTE_LIST` there. The token predicate calls the member plain, because
  a C23 `[[...]]` attribute is spelled only with tokens it admits. A read
  gated on that predicate would skip a real attribute. The self-host rerun
  logged no such row, so the census and the oracle's object identity stand.
  The tree test is the one to ship.

No AST construction defect was found on these inputs.

### Statement-keyword gate in lowering's classification

Two map-row follow-ups were left open after the extent table (#3495, deleted
by #4060; see [statements from nodes](#statements-from-nodes-in-lowering)):
- `c_ir_lower_body_advance`'s first-token classification;
- `c_ir_control_substatement_position`, the switch label scan.

A throwaway census build with noinline wrappers and counters ran on base main
`b71dfc89` (unity self-host, `-g0 -c -fc-ast-pilot`, tests-off
`-march=x86-64-v3` Release, Callgrind):
- **The switch label scan's call:** 0.22 M Ir over 5,224 calls, none true.
- **The classification:** 155,771 statements.
  - The typedef probe costs 38.5 M. 65,611 first tokens take its scope
    lookup, and 53,835 of them are statement keywords: `if`, `return` and
    the like.
  - `c_parse_type_word_for_dialect` costs 9.0 M.
  - The label proof costs 5.7 M and finds 12 labels.

A keyword is never a type word and never names an entity, so the probe's
answer for one is always false. A token gate can skip the lookup for these
keywords on both paths, with no tree.

These budgets were declared before the measured runs. The input is the
self-host unity input, at `-g0 -c` for the instruction counts and at
`-g -c` for the objects. Four arms are counted with Callgrind
(`--cache-sim=no --branch-sim=no`) on tests-off `-march=x86-64-v3` Release
builds, with the driver built by Clang (no TCC on the host):
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

The base is main `b71dfc89`, which the candidate branches from. The budgets:
- **Correctness.** The gate is exact by construction: the gated set
  (`break`, `case`, `continue`, `default`, `do`, `for`, `goto`, `if`,
  `return`, `switch`, `while`) is keywords in every dialect. Reference-check
  builds (Debug and sanitized) run the skipped lookup behind every gated
  token and check that it answers false. The `-c` objects (`-g0` and `-g`)
  must be byte-identical across the four arms. `test_self_host` must keep
  its fixed point, and `test_all` must pass.
- **The probe (D against A, and C against B).** `c_ir_lower_body_advance`'s
  calls to `c_parse_scope_for_token_near` fall from 65,611 to at most
  11,776, the unbound first tokens that are not gated keywords.
- **The whole compile.** `-c` (`-g0`) must fall by at least 0.05% Ir, both D
  against A and C against B. `-fsyntax-only` runs no lowering, so it is not
  measured.
- **The tree reads.** Each is built only if its census ceiling, net of the
  gate, clears 1 M and the cost of the per-body table that answers it. The
  extent table measured that cost at about
  400 to 700 Ir per entry. A read that does not clear the bar is recorded as
  a negative result, with no code.
- **Acceptance.** Hosted counts are diagnostic. No 9700X comparison is
  requested. The report-only main comparison runs after the merge, and Zen 5
  validation stays incomplete (#2761).

The keyword gate's hosted census is
[`2026-10-12T135905Z`](../../performance-audits/2026-10-12T135905Z.md), for
the first candidate, and its follow-up
[`2026-10-12T150330Z`](../../performance-audits/2026-10-12T150330Z.md), for
the shipped one. Both are diagnostic only. There were two candidates:
- **The first, `51036154`, missed the lookup budget by 77.** It gated the
  eleven words declared above, and its lookups fell to 11,853, not 11,776.
  The census had counted the reserved `_Static_assert` (75) and `__asm__`
  (2) among the keywords. The first candidate left them out on a wrong
  rationale: that some dialect spells them as ordinary identifiers. That is
  true only of plain `asm`. `_Static_assert` is the spelling C23's
  `static_assert` is respelled to, and an older dialect's `static_assert`
  keeps its own symbol. `__asm` and `__asm__` are always asm keywords.
- **The second, `028ad7e0`, ships.** It also gates those three reserved
  spellings, and plain `asm` stays probed.
  - **Correctness.** The objects are byte-identical across the four arms,
    at `-g0` and at `-g`. The reference-check builds found no disagreement.
  - **The lookup budget passes.** Lookups fall from 65,611 to exactly
    11,776.
  - **The compile falls by 23.62 M Ir on both paths.** That is −0.093% on
    the default path and −0.100% under the pilot, which more than recovers
    the extent table's +13.8 M. The gain is on the token path, not from the
    tree.
- **Neither tree read is built.**
  - The switch label scan's call costs 0.22 M, which is under the 1 M bar.
  - The classification's ceiling, net of the gate, is under 30 M. A
    first-token table over its 155,771 statements would cost at least about
    62 M at the extent table's per-entry cost.
- Zen 5 validation stays incomplete (#2761).

For the storage-class hint (the plan's declaration hint also carries the
storage-class set its `DECL_SPECIFIERS` items spell, and the hinted split
skips its scan of the whole declaration for it), these budgets were declared
before the measured runs. The input, flags and four Callgrind arms are the
declarator hints' (`-g0 -fsyntax-only`, the self-host unity input,
`--cache-sim=no --branch-sim=no`, tests-off `-march=x86-64-v3` builds). The
base is main `680e16d3`, which carries the declarator hints (#3499). On the
superseded block-declaration split's base (#3503, audit
[`2026-10-11T104857Z`](../../performance-audits/2026-10-11T104857Z.md)) that
scan was about 7 M Ir over 41.6 K declarations. The budgets:
- **Correctness.**
  - A new field-by-field model differential in the corpus gate
    (`c_ast_corpus_models_differ`) shows 0 differences. It runs beside the
    binding differential and compares every row of the type, entity, scope,
    array-bound, alignment, declaration, identifier-use, member, enum-member
    and parameter tables with and without the tree, over the corpus and the
    frontend sources.
  - A one-off run of the same comparison on the self-host unity input also
    shows 0 differences.
  - `-c` objects (`-g0` and `-g`) are byte-identical across the four arms.
- **The hint's own effect (C against B).** The whole compile costs at least
  2 M fewer instructions, with the hint's construction charged. A smaller
  gain, or a loss, is recorded as a negative result. The code change then
  does not ship, and only the differential does.
- **The default path (D against A).** The split stays one always-inline
  worker with a hintless and a hinted caller, so the default path runs no
  hint code. The difference must stay within ±0.05% Ir, or else meet the
  walk's work-equivalence criterion: identical callees and call counts, and
  identical objects.
- **Acceptance.** Hosted counts are diagnostic. No 9700X comparison is
  requested, so Zen 5 validation stays incomplete (#2761).

The storage-class hint's hosted census is
[`2026-10-11T144221Z`](../../performance-audits/2026-10-11T144221Z.md),
diagnostic only:
- **Correctness.** 0 model differences over the corpus and the self-host
  unity input, and objects byte-identical at `-g0` and `-g` across the four
  arms. The `-v` counters are identical in B and C, so the wider auto check
  skips no fewer self-host declarations.
- **Ship.** C against B is −8.46 M Ir (−0.092%). The hinted split falls by
  11.3 M, with the plan's added folding charged.
- **Default path.** −76 Ir on `-fsyntax-only` and +28 Ir on `-c`.
- **Acceptance.** Zen 5 validation is incomplete (#2761).

The same four arms and budgets were re-taken on main `cc48e7cb`, because the
default path had changed since `680e16d3`. The re-measurement is audit
[`2026-10-13T113236Z`](../../performance-audits/2026-10-13T113236Z.md), also
diagnostic only. Every budget still passes:
- **Objects.** Byte-identical at `-g0` and `-g` across the four arms.
- **Counters.** The `-v` counters are identical in B and C.
- **Ship.** C against B is −6.87 M Ir on `-fsyntax-only` (−0.074%) and on
  `-c` (−0.030%).
- **Default path.** −4,417 Ir on both workloads.

Results are recorded in a performance audit (`tools/new_audit.py`), not here.

### Type-word test and label proof in lowering's classification

After the [statement-keyword gate](#statement-keyword-gate-in-lowerings-classification),
two pieces of `c_ir_lower_body_advance`'s first-token classification were
left open: the type-word test (about 9.0 M Ir) and the label proof (about
5.7 M). Those figures came from noinline census wrappers, so they include the
wrappers' own calls.

A throwaway census build of main `7bc92b2d` counted both on the unity
self-host (`-g0 -c`, default flags, tests-off `-march=x86-64-v3` Release,
Callgrind):
- **The type-word test.** 103,554 calls spend 8.05 M Ir in
  `c_parse_type_word_for_dialect`, about 78 Ir a call, matching the spelling
  against a ladder of string compares.
  - The declaration branch makes 98,569 calls. 86,724 are identifiers bound
    to an entity, 6,990 are predefined symbols, and 6,876 answer true.
  - The `declaration_statement` test makes 4,985 calls. All are bound
    identifiers, and none answers true.
  - `c_parse_type_word_for_dialect_token` answers every one of the 103,554
    the same way. It reads the interned symbol's `word_bits`, which are
    computed from the same spelling predicates, and every specifier spelling
    is interned in the predefined range.
- **The label proof.** Of 156,929 first tokens, 12 have the shape an
  identifier followed by `:`. Only those 12 reach the out-of-line proof, and
  all 12 are labels. The shape test is already inline and needs about two
  token loads a statement, so there is nothing separate to gate. The
  dead-code hop after a terminator, in the same function, calls
  `c_ir_named_label_proven_at` 18,882 times (2.19 M). That is a different
  question, and it is under the floor below.

These budgets were declared before the measured runs. The input is the
self-host unity input, at `-g0 -c` for the instruction counts and at
`-g -c` for the objects. Four arms are counted with Callgrind
(`--cache-sim=no --branch-sim=no`) on tests-off `-march=x86-64-v3` Release
builds, with the driver built by Clang (no TCC on the host):
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

The base is main `7bc92b2d`, which the candidate branches from. The budgets:
- **Correctness.** Both type-word sites read the token form instead of the
  spelling. Reference-check builds (Debug and sanitized) compare the two
  answers for every identifier first token. The `-c` objects (`-g0` and
  `-g`) must be byte-identical across the four arms. `test_self_host` must
  keep its fixed point, and `test_all` must pass.
- **The test (D against A).** `c_ir_lower_body_advance` makes no call to
  `c_parse_type_word_for_dialect`.
- **The whole compile.** `-c` (`-g0`) must fall by at least 3 M Ir, both D
  against A and C against B. Below that floor the result is recorded as
  negative and no code ships. `-fsyntax-only` runs no lowering, so it is not
  measured.
- **The label proof.** A cut is built only if it removes at least 3 M. The
  census above leaves none, so the result is recorded as negative, with no
  code.
- **Acceptance.** Hosted counts are diagnostic. No 9700X comparison is
  requested. The report-only main comparison runs after the merge, and Zen 5
  validation stays incomplete (#2761).

The hosted census is
[`2026-10-12T220456Z`](../../performance-audits/2026-10-12T220456Z.md),
diagnostic only:
- **The type-word test ships on the token path.** Both arms fall by 8.98 M Ir
  (−0.036% default, −0.039% pilot). `c_ir_lower_body_advance` makes no
  spelling-ladder call, and the objects are byte-identical across the four
  arms at `-g0` and `-g`.
- **The label proof is negative.** No code ships for it.
- **Leads, unclaimed:** the dead-code hop's proofs (2.19 M) and
  `c_ir_query_execute`'s 48,392 spelling-ladder calls (4.98 M).
- Zen 5 validation stays incomplete (#2761).

Results are recorded in a performance audit (`tools/new_audit.py`), not here.

### Type-word tests in lowering's type queries

The [type-word section](#type-word-test-and-label-proof-in-lowerings-classification)
left one lead: `c_ir_query_execute` still called the spelling ladder
`c_parse_type_word_for_dialect` 48,392 times (4.98 M Ir) on the base it
measured. After that change, `c_gen.c` has two spelling-form type-word
tests left:
- **Site 1, `c_ir_type_name_prefix`.** It tests the first specifier token
  of a type name that no entity binds, before looking the name up.
- **Site 2, `c_ir_statement_expression_tail`.** It tests the first token of
  a statement expression's trailing statement.

A throwaway census build of main `446f742e` counted both on the unity
self-host (`-g0 -c`, default flags, tests-off `-march=x86-64-v3` Release,
Callgrind). Each site went through a noinline wrapper with noinline
markers:
- **Site 1** makes 48,315 calls, all from `c_ir_query_execute`, where
  `c_ir_type_name_prefix` is inlined; 9 more come from its recursive frame.
  - 3,571 calls answer true.
  - 24,855 tokens are predefined symbols, and none lacks a symbol.
  - `c_parse_type_word_for_dialect_token` answers every call the same way.
- **Site 2** makes no call. The input has no statement expression, as the
  [statement-expression census](#statement-expression-interiors-in-lowering)
  found.

These budgets were declared before the measured runs. The input is the
self-host unity input, at `-g0 -c` for the instruction counts and at
`-g -c` for the objects. Four arms are counted with Callgrind
(`--cache-sim=no --branch-sim=no`) on tests-off `-march=x86-64-v3` Release
builds, with the driver built by Clang (no TCC on the host):
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

The base is main `446f742e`, which the candidate branches from. The budgets:
- **Correctness.** Both sites read the token form instead of the spelling.
  Site 2 is swapped for exactness, not for its count. Reference-check
  builds (Debug and sanitized) compare the two answers for every identifier
  token each site tests. The `-c` objects (`-g0` and `-g`) must be
  byte-identical across the four arms. `test_self_host` must keep its fixed
  point, and `test_all` must pass.
- **The test (D against A).** No function makes a call to
  `c_parse_type_word_for_dialect` from `c_gen.c`'s lowering on this input.
- **The whole compile.** `-c` (`-g0`) must fall by at least 3 M Ir, both D
  against A and C against B. Below that floor the result is recorded as
  negative and no code ships. `-fsyntax-only` runs no lowering, so it is not
  measured.
- **Not in scope.** The dead-code hop's 18,882 label proofs (2.19 M) stay
  under the floor; this census does not change them.
- **Acceptance.** Hosted counts are diagnostic. No 9700X comparison is
  requested. The report-only main comparison runs after the merge, and Zen 5
  validation stays incomplete (#2761).

The hosted census is
[`2026-10-13T084504Z`](../../performance-audits/2026-10-13T084504Z.md),
diagnostic only:
- **Both sites ship on the token path.** Both arms fall by 4.09 M Ir
  (−0.016% default, −0.018% pilot). `c_ir_query_execute` makes no
  spelling-ladder call, and no caller of the ladder remains in either
  candidate arm. The objects are byte-identical across the four arms at
  `-g0` and `-g`.
- **Leads, unclaimed:** the dead-code hop's proofs (2.19 M), still under the
  floor.
- Zen 5 validation stays incomplete (#2761).

Results are recorded in a performance audit (`tools/new_audit.py`), not here.

### Settling the name checks, second cut

This section has its own anchor because other open AST branches append to
the plan's final paragraph. After [the first cut](#settling-the-name-checks),
a census of main `4ed35b59` under `-fc-ast-pilot`, on the same stage-2 input,
attributed `c_ast_types_lookups_settled`'s 24.35 M inclusive Ir line by line
from one Callgrind run (no census wrappers):
- 132,669 calls, 6,047 settlements, 126,249 marks, one table build;
- 6.5 M of query-time checks, about 49 Ir a call. 2.1 M of it is the
  prologue and epilogue: every call saves and restores five callee-saved
  registers and a frame that only the settlement loop needs;
- 5.5 M in the flag scan, about 21 Ir per eight-byte group. Each group
  recomputes its width and tests for a tail;
- 8.1 M in the per-mark checks, including the binding loads and the 11,474
  file-scope lookups;
- 2.3 M in the table build, and 1.7 M of spills spread over the loops.

The floor for a code change is 1 M Ir removable. Two items clear it:
- the query path pays the settlement's frame on every call, though only
  6,047 of 132,669 calls settle;
- the scan pays per-group bookkeeping that a whole-word loop with one tail
  does not.

The candidate keeps every rule. It moves the settlement into its own
out-of-line function, so the query path saves one register, and scans
whole flag words with the tail assembled once. These budgets are declared
before its measured runs. They use the same stage-2 input, flags and four
Callgrind arms (A to D) on tests-off `-march=x86-64-v3` builds, with main
`4ed35b59` as the base:
- **Correctness.** No answer changes:
  - no verify mismatch over the corpus, with the existing
    `c_ast_test_type_lookups` probes;
  - the typer's `-v` counters on the self-host, `lookups_settled` and
    `lookups_walked` included, are identical in B and C;
  - identical diagnostics with and without the flag;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms.
- **The settlement's own cost (C against B).** The inclusive instructions of
  `c_ast_types_lookups_settled` and the new settlement function, plus any
  increase in `c_ast_types_answer`'s and `c_ast_types_type_body`'s own
  instructions, fall by at least 12%, and the whole compile falls by at least
  2 M Ir. Otherwise the result is recorded as negative and the code does not
  ship.
- **Default path (D against A).** Within ±0.05% Ir on `-fsyntax-only` and on
  `-c` (`-g0`).
- **Performance acceptance.** Hosted counts are diagnostic. No 9700X
  comparison is requested, so Zen 5 validation stays incomplete (#2761) and
  the hook stays opt-in.

The second cut's hosted census is
[`2026-10-12T174307Z`](../../performance-audits/2026-10-12T174307Z.md), taken
the same way and diagnostic only:
- Every hosted budget passes. The typer's counters are identical in B and C,
  and the objects of all four arms are identical at `-g0` and `-g`.
- The settlement falls from 24.35 M to 20.43 M Ir (−16.1%), and the pilot's
  compile by 3.92 M (−0.042%). The query path's own cost falls from about
  6.5 M to 4.1 M, and the scan's from 5.5 M to about 3.8 M.
- The default path moves +54 Ir on `-fsyntax-only` and +20,431 on `-c`.
- What remains is mostly the per-mark checks (about 8 M) and the query-time
  checks themselves. Neither has a lead above the 1 M floor that avoids
  recording marks in the eager pass.
- Zen 5 validation stays incomplete (#2761).

### Root specifier fallbacks

This section has its own anchor because other open AST branches append to
the plan's final paragraph. After [members](#members), the
[declaration specifier read](#declaration-specifiers-from-the-tree) still
hands 2,259 root runs back to the machine on the self-host unity input under
`-fc-ast-pilot` (2,237 `input`, 22 `specifiers`). The census came first. It
was a throwaway build of base main `f0f84944`, on the same input and flags as
the [tag definition census](#tag-definition-machine-work). Each fallback ran
the machine under one no-inline wrapper per caller and shape. The shape was
read from the run's tokens, and the first match in the table's order wins:

| Caller | Shape | Runs | Ir | Ir per run |
| --- | --- | ---: | ---: | ---: |
| file scope | tag reference (`struct S`, plain words beside it) | 1,160 | 6.90 M | 5,946 |
| file scope | tag definition | 437 | 8.96 M | |
| file scope | attribute | 342 | 1.22 M | 3,559 |
| file scope | `__extension__`, `_Atomic`, other words | 22 | 0.17 M | |
| parameter | tag reference | 132 | 0.70 M | 5,313 |
| block | tag definition | 62 | 0.95 M | |
| block | `typeof` | 40 | 0.24 M | |
| block | tag reference | 42 | 0.22 M | 5,186 |
| block | other words | 22 | 0.06 M | |

Every tag reference is plain: its other specifiers are words the read already
takes, with no attribute on the tag. The 19.4 M total splits as follows:
- **Tag references.** 1,334 runs cost 7.82 M. Inside them, the work a read
  must keep is the machine's own calls: `c_parse_tag_lookup` (0.67 M),
  `c_parse_add_type` for a tag seen first (0.27 M),
  `c_parse_type_identity_prepare` (0.04 M) and
  `c_parse_apply_trailing_qualifiers` (0.05 M). The rest is the frames and the
  scans: `c_parse_storage_classes_valid` alone is 2.28 M, and the attribute
  and alignment skips are 2.13 M. With the declaration read's cost of about
  550 Ir a run added, about **6 M** is removable. This is an estimate.
- **Tag definitions.** 499 runs cost 9.91 M, most of it the body, which a
  root read cannot take. At the tag reference's cost a run, the work outside
  the body is at most about 3 M (estimate).
- **Everything else.** Each row is below 1.3 M.

Members' tag references (54 runs) and type-name fallbacks (28 runs) are
smaller still ([member census](#member-specifier-runs)).

The gate is the [member specifier runs](#member-specifier-runs)' **5 M** of
Ir. Only tag references can clear it. The candidate reads a root run whose
`DECL_SPECIFIERS` list is plain words followed by one `struct`, `union` or
`enum` specifier with only a tag name. It makes the machine's calls in the
machine's order, and does not apply to members. These budgets are declared
before its measured runs. They use the same input, flags and four Callgrind
arms (A to D) on tests-off `-march=x86-64-v3` builds, with main `f0f84944` as
the base:
- **Gate (C against B).** The whole compile falls by at least **5 M Ir** on
  `-fsyntax-only`, with the wider index charged. Otherwise the result is
  recorded as negative and no code ships.
- **Correctness.**
  - 0 model differences over the corpus and the frontend sources: the
    analysis model field by field and the bindings, with the diagnostics;
  - a one-off comparison of the same fields on the self-host unity input,
    also with 0 differences;
  - in verify mode, every tag reference read from the tree is first read by
    the machine and rolled back. The two answers must match (type row, its
    fields, the declarator start and the rows appended), with 0 mismatches.
    A tree whose tag name is changed must make verify mode report a mismatch;
  - byte-identical `-c` objects (`-g0` and `-g`) across the four arms.
- **Coverage (reported, not gated).** Tag references read, and the runs still
  handed back by reason, from the `-v` counters.
- **Default path (D against A).** Within ±0.05% Ir on `-fsyntax-only` and on
  `-c` (`-g0`).
- **Performance acceptance.** Hosted counts are diagnostic. No 9700X
  comparison is requested, so Zen 5 validation stays incomplete (#2761). The
  pilot-default switch stays on hold.

The result is
[`2026-10-12T210633Z`](../../performance-audits/2026-10-12T210633Z.md),
hosted and diagnostic only. It is **negative**, and no code ships:
- **Coverage.** Each of six layouts read 1,333 of the 1,334 tag references,
  with `-c` objects identical to the base's at `-g0` and `-g` where checked.
- **What the read removes.** About 6.9 M of machine self cost disappears.
- **What it costs.** Every layout pays at least 4.4 M back in code generation:
  around the 124,934 plain reads, in their callers, or in the index pass.
  - The best layout, with the tag path out of line, reached C − B of
    **−2.54 M**, against the **5 M** gate.
  - Keeping the plain read's source unchanged was worse: −1.48 M and −1.75 M.
    One layout rose by 1.80 M once the index's append stopped being inlined.
- **What was not run.** Verify mode and the model differential were not run,
  since no candidate passed the gate.
- **Evidence.** The raw material is in
  [`evidence/2026-10-12T210633Z`](../../performance-audits/evidence/2026-10-12T210633Z/PROVENANCE.md),
  pinned by `SHA256SUMS`. It holds every Callgrind output, the census and
  variant 6 patches, which rebuild the measured binaries byte for byte, and
  variant 3's `c_parse.c`. The sources of variants 1, 2, 4 and 5 were not
  kept. Their figures rest on the retained Callgrind outputs and binary
  hashes alone, and their code cannot be replayed.

Tag definitions, attributes, `typeof` and the other rows stay below the gate,
so all 2,259 runs stay with the machine.

Results are recorded in a performance audit (`tools/new_audit.py`), not here.

### Statement-expression interiors in lowering

This section has its own anchor because other open AST branches append to
the plan's end. This census predates #4060, which reads statement-expression
bodies from their [statement nodes](#statements-from-nodes-in-lowering).
The extent table did not enter expressions. So a `({ ... })` body lowered by
`c_ir_lower_statement_expression_step` asks the token walkers
(`c_ir_lower_body_advance`, `c_ir_controlled_body_range`,
`c_ir_statement_end*`), and its controlled substatements are walked as
`shape`.

The floor for extending the table to those bodies was declared on #3102
before any measured run: 5 M Ir of walker work inside statement expressions,
on the self-host unity input at `-g0 -c`, with and without `-fc-ast-pilot`.

A census on main `7bc92b2d` reported **no statement expression** in that
input, on either arm. It reported 0 statement-expression frames out of 6,389
body frames lowered, and no `({` in the base's `-E` output. These counts are
the census session's own observations. Its logs were not retained, and the
counts have not been independently replayed. The 0 Ir of walker work is
inferred from the zero frames, not measured with Callgrind. On that report
the item is negative, and no code ships. The census is
[`2026-10-12T213925Z`](../../performance-audits/2026-10-12T213925Z.md),
diagnostic only. Its appended note lists what is retained: the full
instrument patch, the positive control and the artifact hashes. A later attempt needs a workload that has statement
expressions, such as a GNU-heavy compatibility input, and a floor declared
on that workload. Zen 5 validation stays incomplete (#2761).

### Root-token predicate refusals and the pilot's `?` gate

This section has its own anchor because other open AST branches append to
the plan's end. The [token-to-node map census](#token-to-node-map-for-lowering)
left two tree-free leads on the token path, tracked in
[#3924](https://github.com/buster14a/buster/issues/3924):
- **The predicates.** `c_ir_has_root_comma`, `c_ir_has_root_assignment`,
  `c_ir_has_root_control_operator`, `c_ir_has_root_question` and
  `c_ir_call_argument_separator` scan 703,748 ranges (71.8 M Ir) that hold
  no candidate token. On such a range each answer is fixed.
- **The pilot's root conditional.** Under `-fc-ast-pilot`,
  `c_ir_root_conditional` sends every range to the tree's records (79.1 M)
  without the `?` count the default path tests first.

The change adds a `,` count and an assignment-operator count to the body's
token prefix table (`CIrTokenPrefix`). Each predicate refuses an in-body range
in O(1) when its candidate count does not change across it. The control
operators' candidates are the `controls` count less the assignment count.
The pilot tests the `?` count before the records. Both are token-path changes
and are not credited to the tree; tree slices are budgeted net of them.

These budgets were declared before any measured run. The input is the
self-host unity input, at `-g0 -c` for the instruction counts and at `-g -c`
for the objects. Four arms are counted with Callgrind
(`--cache-sim=no --branch-sim=no`) on tests-off `-march=x86-64-v3` Release
builds, with the driver built by Clang (no TCC on the host):
- A: base, default flags;
- B: base with `-fc-ast-pilot`;
- C: candidate with `-fc-ast-pilot`;
- D: candidate, default flags.

The base is main `56b6fca3`, which the candidate branches from. The budgets:
- **Correctness.** Each refusal is exact by construction: every scan can
  answer other than its fixed answer only at a candidate token in its range,
  and the counts use the scans' own punctuator tests. Reference-check builds
  run the skipped scan behind every refusal and check that it agrees, and
  check each new class against its token test as the table is built. The
  pilot's refusal keeps its statistics: a refused non-empty range counts as
  the records' negative, which is the records' answer for a range with no
  `?`. The `-c` objects (`-g0` and `-g`) must be byte-identical across the
  four arms. `test_self_host` must keep its fixed point, and `test_all` must
  pass.
- **The predicates (D against A).** `-c` (`-g0`) must fall by at least 30 M
  Ir, with the new columns' work in `c_ir_token_prefix_build` charged.
  Below that floor the result is recorded as negative and the predicate
  gates do not ship.
- **The pilot gate (C against B).** `-c` (`-g0`) must fall by at least
  30 M Ir more than D against A. Below that floor the pilot gate does not
  ship.
- **Not measured.** `-fsyntax-only` runs no lowering.
- **Acceptance.** Hosted counts are diagnostic. No 9700X comparison is
  requested. The report-only main comparison runs after the merge, and Zen 5
  validation stays incomplete (#2761).

The hosted census is
[`2026-10-13T092848Z`](../../performance-audits/2026-10-13T092848Z.md),
diagnostic only:
- **The predicates are negative; no code ships.** With all five gates, the
  default path rose by 21.6 M Ir. The two new columns cost +40.9 M in
  `c_ir_token_prefix_build`, and the inline gates cost about 32 M in their
  callers, against about 47 M of scans saved. A per-token column is paid for
  every body token, so it does not pay for these predicates.
- **The pilot's `?` gate ships.** `c_ir_root_conditional` stays as on main:
  any change to that inline function moved `c_ir_lower_expression_step` by
  about +13 M. Instead `c_ir_tree_root_conditional` is a small gate. It
  refuses a non-empty body range with no `?` and counts it as the records'
  negative. Only the 61,998 ranges that may hold a `?` reach
  `c_ir_tree_root_conditional_records`. C against B is −38.6 M Ir (−0.17%)
  and D against A is +1.3 M (+0.005%).
- **Correctness.** The objects are byte-identical across the arms and on the
  `tests/**/*.c` corpus, and the `-v` pilot statistics are unchanged.
- Zen 5 validation stays incomplete (#2761).

Results are recorded in a performance audit (`tools/new_audit.py`), not here.

## Assembly lowering from nodes

Inline assembly lowering consumes its `ASM` root, qualifier flags, ordered
`ASM_OUTPUTS`/`ASM_INPUTS` children, `ASM_OPERAND` expression roots and symbolic
names, and `ASM_CLOBBERS`/`ASM_LABELS` lists. A `STRING` node's retained run count
selects template fragments; lowering only fetches their spelling. The same
codec reads the `ASM_TOP_LEVEL` string child. The assembly template language
and constraint/register validation retain their existing implementation.

Operand continuations carry the exact expression node and retained source
provenance, preserving evaluation order and memory-place recovery. Semantic
analysis owns the token-based GNU dialect preflight; lowering no longer
repeats it. Local register labels use the published entity-to-`INIT_DECLARATOR` link;
the declaration-link adapter is deleted.

Runtime casts and compound literals now read their exact TYPE_NAME/list
children. Runtime sizeof/_Alignof reads its operand node, uses saved VLA
suffix dimensions by bound entity, and asks the shared constant task for fixed
layouts. A materialized VLA type retains its element alignment even while its
total size is unresolved: `_Alignof(T[n])` reads that completed alignment and
does not evaluate the bound. `sizeof(T[n])` still requires the saved or
computed runtime dimensions. VLA operand evaluation hands the same node to call preparation and
expression lowering. The previous prefix/subscript token walk, type-name probe,
statement-tail scans and core fallback classifier are removed. The legacy
whole SIZEOF callback and its constant-parser callers are also removed.

## Lowering provenance boundary

`CSourceAnchor` in `c_source_internal.h` contains a resolved byte offset and
length. Lowering can obtain the spelling, mapped source site or location
through `c_source_anchor_*`; oversized spelling lengths retain their exact
value. The API exposes no token kind, punctuator or symbol classification.
Owned AST token indices may select source anchors, but source ranges never
recover a structural node.

`python3 -B tools/check_c_lowering_provenance.py` checks `c_gen.c` and every
implementation it includes, following nested quoted paths and angle paths
relative to `src/`. Missing implementation files, paths outside the frontend
boundary and nonliteral include directives fail the scan. The required CI lint
and queue-lint jobs run that guard and `check_c_lowering_provenance_test.py`.
The regression plants direct, aliased and included-unit token grammar reads
and requires command failure; comments and string literals remain prose.
The guard rejects token types and
getters, raw preprocessing token columns, range-to-node recovery and parser
grammar helpers. Its named model-helper allowlist consists only of audited
target layouts and finished semantic tables. It has no test-build exception
inside lowering.

The driver regression `compiler_driver_test_lowering_token_poison` poisons the
expanded stream's kind, punctuator and symbol columns after semantic analysis,
while preserving spelling ranges and numeric-cache identity. Its FAST/QUALITY,
implicit/hybrid and `-g0`/`-g` rows require byte-identical objects; native rows
check the member, bit-field, atomic and update results. The source-anchor fixture
also checks macro mapping, `#line`, oversized strings and foreign numeric-cache
identity. The compiler and source API remain C-only; Python is a CI test tool.

Leading constructor projections preserve the original order while a comma or
CALL-list continuation keeps the same prefix. The expression frame reuses its
settled answer only for the same known node, or that node's exact first child
at the same recorded start. Assignment, branch and statement-tail transitions
recheck identity. The geometric regression counts actual prefix visits for
comma, CALL-list, assignment, branch, statement-tail and pointer-constructor
shapes and checks the C type rows remain immutable; no unit cache or extra
syntax pass is introduced.
