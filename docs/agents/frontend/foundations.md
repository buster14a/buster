# C frontend, canonical IR, and places

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

Read the matching sections; [the frontend index](../frontend.md) lists these notes in their original order. Cross-references such as “above” and “below” follow that order.

## Direct local SSA (GitHub #34)

`c_ir_ssa_*` in `c_gen.c` constructs pruned canonical block-argument SSA for
supported scalar parameters, automatic locals and compiler-generated scalar
expression temporaries. The shared `ir_local_type_promotable` predicate keeps
representation and narrow-integer normalization rules identical to canonical
promotion. Ordinary pointers, supported integers/floats and fixed vectors are
eligible. Narrow-normalizing, volatile/atomic, static, thread-local and
cleanup-managed owners retain memory form.

Signed integer vector comparisons retain their operand's lane type for the
all-ones/zero mask. `c_ir_vector_mask_type` must not substitute another
same-width C type such as plain `char`; mask lookup for unsigned or floating
lanes excludes qualified integer types. `basic_c_vector_lane_edges.c` checks
narrow signed masks alongside arithmetic in all native backend modes.
Compatible vector aliases can still have distinct canonical type IDs.
`c_ir_emit_representation_alias_conversion` preserves their equal lane representation
through typed views of a private slot, with ordinary validated memory
operations; do not mutate an existing value's type or weaken cast validation.
Explicit GNU vector casts (`C_CONDITIONAL_CAST`) use the same path when both
layouts are resolved and their total byte sizes match, even when lane count,
width, signedness or floating format differs. Implicit assignment, argument
and return conversions retain the compatible-lane rules in `c_ir_emit_cast`.
`compiler_driver_test_vector_casts` checks the LLVM 4x32/2x64 arithmetic shape,
floating bit patterns, compatible aliases and rejected conversions through
both frontend modes and every native allocator.

Eligibility is per owner, not a function-wide token blacklist. Normal calls,
address-taking, aggregates beside scalar locals, adjusted array parameters,
field/index expressions, scalar compound literals, statement expressions,
logical/conditional expressions, switch and ordinary goto do not disqualify
unrelated locals. Actual lowered place uses determine escapes and subobject or
mismatched-width accesses. A worklist detects missing reaching definitions;
a declaration without an initializer is supported when all reads are defined.
No uninitialized value is invented. Entry-initialized owners need only one
initialization proof; every journaled store's RHS is an independent root so
copying another uninitialized owner is still caught.

Each owner is wholly SSA or wholly memory at publication. A compact operation
journal records elided declaration/load/store sites, source ranges and original
instruction anchors, not canonical instruction rows or another frontend IR.
An escaping or may-uninitialized owner is restored at **all** original sites,
including writes before the escape and across blocks. Restored loads cease to
be aliases before parameter simplification. Existing instruction IDs, extras,
source ranges, value alignment, qualifiers and canonical-local IDs remain
intact. Private temporary owners do not pollute the named debug-local table.

Indirect calls, known non-local-jump calls, dynamic-stack operations, label
addresses/computed goto and inline assembly retain the conservative shared
promotion barrier contract. Label-address and assembly syntax are declined
before lowering where the existing provenance machinery requires memory;
other barriers restore all journaled owners. Shared promotion remains the
independent fallback for every retained memory owner.

The current-value table is sparse `(block, owner)` state, not a blocks × locals
matrix. Unresolved reads create provisional block parameters. Sealing waits
until all backedges and goto predecessors are known; an iterative queue fills
incoming values, forwarding through single-predecessor chains. Trivial
parameters and unused parameter cycles are removed. Disconnected empty label
blocks have no outgoing edge. Publication includes **every** predecessor edge,
including parameter-free destinations; selectors must never see a partial CFG.
Condition lowering resolves a literal left operand of `||` or `&&` before
allocating a block for its right operand. A short-circuited arm must not
become a disconnected source block that joins a value defined only on another
path; selected MIR enforces dominance in unreachable code too.
Nested GNU statement-expression body walks reuse the function's label block at
the same source token. Allocating a second block leaves the predeclared label
unterminated and separates ordinary goto from label-address provenance. The
strict `basic_c_statement_expression_value.c` corpus checks both goto arms.

A named label can re-enter a token range after control skipped an ordinary
automatic declaration. Fixed-size objects in a labeled function therefore
receive their canonical local/place rows before the entry block terminates;
the declaration still owns its initializer, cleanup activation and source
location. Block-scope `extern`, static/thread storage and VLA allocation remain
on their existing declaration paths. A missing mapping for an automatic local
fails lowering instead of being reinterpreted as a global symbol. Target-local
promotion keeps a zero-store object in its frame slot, because a mutable
virtual register would have no defining store.

Existing current-value queries do not grow the sparse table. A missing-key
insertion owns capacity growth, and parameter creation reuses the slot its
caller already resolved. Once the journal contains every write, an owner with
exactly one initializing entry store forwards reachable pending reads straight
to that definition. A later store revokes this shortcut, and escaped owners
still follow memory recovery. Every store RHS remains an initialization root
before alias substitution, including a single entry definition that copies an
uninitialized owner; disconnected reads keep the ordinary predecessor path.
The dependency walk is unnecessary when every retained owner already has entry
initialization; restored loads still become independent definitions first.

After predecessor propagation finishes, parameter simplification reuses its
block cursor for a stable list of blocks that still own parameters. Empty
blocks leave the list after each sweep. Simplification never adds parameters,
so they cannot become active again. Retain ascending block order and each
block's parameter order: changing elimination order can change replacement
representatives and canonical value IDs. The allocation diagnostic census
counts initial list construction as well as subsequent block visits.

Temporary places and read aliases preserve C lvalue/qualifier checks without
emitting `LOCAL`, `LOAD` or `STORE` rows for promoted owners. Finalization
resolves aliases and compacts values/operand slices. Debug-local names, types,
IDs, scopes and source ranges are preserved; frontend entity IDs do not escape.
The existing conservative opcode summary also tracks `LOCAL`, so shared
promotion skips its discovery scan for certified functions with no memory
locals. Unknown summaries still scan and the shared algorithm stays independent.

`c_lower_to_ir_with_options` and `c_analyze_with_options` expose the memory-form
reference through `CIRLowerOptions.disable_direct_ssa`. `ide cc
-fno-frontend-ssa` selects it; `-ffrontend-ssa` restores the default. Neither
changes canonical or target-local promotion switches. `-v` reports
`IR_FRONTEND_SSA` functions, promoted owners (including temporaries), eliminated
reads/writes, provisional/removed parameters, temporary-owner count and
restored-owner count. Source locals excluded before journaling are not included
in `fallback_locals`. Shared-promotion tests explicitly select the reference,
so direct construction cannot make their coverage vacuous.

`c_test_direct_ssa` checks raw output on x86-64, AArch64, Wasm64 and eBPF layouts,
complete CFG publication, opcode census, initialization/escape ownership,
alignment, diagnostics and debug provenance. `tests/basic_c_frontend_ssa.c` is
registered in the existing native driver matrix for both frontend paths, with
target-local promotion disabled. It covers irreducible joins, switch fallthrough,
late escape, scalar temporaries, bitfields and dynamic-stack fallback. Native
machine tests exercise vector block parameters as well as preserving the
independent legacy mutable-register and pressure-census contracts.

`ir_prepare_canonical_module` consumes an input-only producer certificate.
Changed output is checked with the existing canonical verifier in debug,
test and sanitizer builds; optimized production retains the pass-contract
fast path. `BUSTER_VERIFY_IR_TRANSFORMS=1` also enables this check in an
optimized production build. `IrValidationResult.boundary` distinguishes
canonical input from local-promotion output, including successful hook calls;
it is not a durable certificate. `local_promotion_complete` is only a pass
completion marker. A later mutator must invalidate its own certificate and
request validation again. See [the boundary inventory](../../ir-validation-boundaries.md).

## Published canonical CFG

After canonical transforms, `ir_prepare_canonical_module` publishes immutable
block/edge/parameter/argument slices shared by native, Wasm, eBPF and LLVM
consumers. Terminators own topology, including parameter-free destinations and
duplicate-target suppression. `ir_function_cfg_edge` replaces incoming-list
searches. Mutation must invalidate `IrFunction.published_cfg`; it is not a
semantic certificate. See [publication and lifetime details](../../canonical-cfg-publication.md).

## C frontend and canonical IR rules

- [Vector semantics](../../ir-vector-semantics.md) classifies every dedicated
  vector opcode. Generic lane operations can legalize without changing their
  semantics; exact SIMD uses a shared feature gate and explicit refusal.
  `IrSimdShape` owns integer/internal-predicate boundaries, consumed by C
  result typing and canonical validation. C masks remain integer values.

- `c_parse_binding_bind` publishes a previously unbound enclosing-scope name
  without scanning unrelated undo records. A live undo record implies a valid
  current binding: bind installs the new entity, and unwind removes its record
  before restoring the previous one. Bound names retain the oldest-record
  search and shadow restoration; the authoritative scope/symbol chains and
  type-parser rollback contract are unchanged. The private test seam observes
  the existing search cursor and verifies geometric work counts without adding
  a production counter, allocation, or mutable cache.

- The public frontend API is `compiler/frontend/c/c.h`. In non-unity builds the
  implementation is split across `c_source.c`, `c_parse.c`, and `c_gen.c`;
  `c.c` preserves the unity include order and diagnostic mapping.
- Keep the frontend pipeline explicit: source loading and preprocessing,
  parsing and semantic construction, then canonical-IR lowering. Do not add a
  parallel frontend-specific IR or route code generation around canonical IR.
- Macro expansion uses one growable LIFO task array per expansion call. Each
  argument context records a task-index floor; lookahead and argument collection
  must not pop below it into suspended parent work. Store indices, never pointers
  across batch pushes that may grow the array. An ENABLE marker remains below
  its replacement batch, and refused identifiers retain `no_expand` on rescans.
  Output nodes and source-stamp ownership are independent of task storage.
  A non-builtin definition without `#` or `##` is written straight into its
  reserved batch by `c_macro_produce_plain_tasks` (exact size from
  `plain_count` and per-parameter use counts); builtins, stringify and paste
  still stage a `CPpToken` list in `c_macro_replacement_tokens` and push it
  with `c_macro_expansion_tasks_push`. Both orders must stay identical:
  `c_test_macro_plain_production` compares them token for token.
- Macro placemarkers survive the entire `##` sequence. The replacement loop
  compacts into its existing materialized buffer and removes placemarkers only
  when emitting the rescan tokens. Only the explicitly marked GNU
  `, ## __VA_ARGS__` operator may delete a comma for an empty argument;
  named parameters and ordinary macros retain it.
  `tests/basic_c_macro_empty_paste.c` covers empty operands, chained pastes,
  surrounding tokens, rescanning, and GNU comma behavior (GitHub #220).
- Source `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, and `#endif` lines may
  cross an in-progress function-like macro invocation as the GCC/Clang
  compatibility extension. The source driver processes each conditional once
  and gives only active source-token segments to the existing iterative macro
  argument collector; inactive delimiters do not affect argument shape. Other
  directive categories retain their ordinary line-level contract and still do
  not execute inside an invocation. C17 and GNU17 share this compatibility
  behavior; no separate strict-mode diagnostic is added. The registered
  `c_macro_conditional_tests` module compares semantic token sequences with
  independent Clang/GCC preprocessors and runs a Buster-built selected-branch
  fixture through both C lowering modes (GitHub #76).
- `c_conditional_number` admits the complete bounded integer spelling, checks
  overflow before accumulation, and leaves its output unchanged on failure.
  Ordinary constants and the x87 initializer folder share it; do not restore a
  second integer parser. U/L/LL (with same-case LL), the MS i8/i16/i32/i64 suffixes (including unsigned forms),
  binary digits and between-digit separators retain their existing admission
  policy. Fixed-width Microsoft suffixes retain their signed/unsigned literal type, including the Windows SDK limits. This does not add C23 bit-precise suffixes.
  The syntax pass validates integer tokens in its existing declaration/body
  walks, including unused functions and unevaluated operands. Inactive macro
  definitions and stringized tokens are not C integer tokens and remain valid.
  Language diagnostics name the offending token with
  `C_DIAGNOSTIC_INVALID_INTEGER_LITERAL`; preprocessing keeps the conditional
  directive diagnostic. `c_test_integer_spelling_consistency` and
  `tests/basic_c_integer_literals.c` cover these contracts (GitHub #148).
- Enumerator integer evaluation uses the typed semantic constant evaluator
  directly over the original preprocessed token stream. `_Generic` selects its
  association by token range without flattening or copying the translation
  unit, and unselected associations are never evaluated. The nested
  generic-constant cases cover this path (GitHub #797).
- Legacy integer constant ranges and static assertions share the private
  `c_parse_constant_expression_evaluate` walker over original token indices.
  The shape sidecar and parse position index describe that stream; copying a
  range into a synthesized token view while retaining either derived index
  gives the wrong classification. Spelling, source recovery, and pack changes
  still belong to the original preprocess result (GitHub #629).
- Preprocessing integer-expression reductions carry signedness and a deferred
  arithmetic-fault bit in the same byte. Division by zero and signed
  `INT64_MIN / -1` (including remainder) never execute as host arithmetic.
  `&&`, `||` and `?:` propagate faults only from evaluated operands; the
  conditional's common unsigned type still depends on both arms. Syntax
  validation remains unconditional. Character constants obtain their
  preprocessing signedness from the decoded target scalar type, including
  target-dependent `L` and C23 `u8` literals. Parse-side constant folds retain
  ordinary C promotions and do not inherit this `intmax_t`/`uintmax_t` widening.
  `c_macro_conditional_tests` covers those character types alongside ordinary C
  controls; `c_test_preprocessor_short_circuit` covers generated `#if`/`#elif`,
  live-fault and malformed-dead-operand controls;
  `tests/basic_c_preprocessor_short_circuit.c` runs in the existing native
  allocator matrix (GitHub #147, #258).
- A folded conditional expression converts its selected value to the common
  type of both arms before any enclosing operator consumes it. Constant and
  runtime typing share `c_ir_conditional_pointer_type`; arithmetic uses the
  usual conversion helper. `tests/basic_c_constant_conditional_type.c` pins
  signed/unsigned widening, mixed floating/integer arithmetic, nested folds,
  and pointer/null selections under every allocator (GitHub #219).
- Invalid user input must produce structured C diagnostics and a failed driver
  result. Assertions and `BUSTER_TODO()` are for violated internal invariants,
  never ordinary syntax or semantic errors.
- Identifier and preprocessing-number spellings require well-formed UTF-8:
  shortest encodings of Unicode scalar values, with no surrogate or value above
  U+10FFFF. This validates encoding without adding Unicode identifier-category
  or normalization restrictions. `c_lex_validate_word_utf8` reports one
  `C_DIAGNOSTIC_INVALID_UTF8` per affected word, at the first byte of its first
  malformed sequence (including a truncated leader). Translation checkpoints
  retain its original file, byte offset, line and byte column across CRLF and
  splices, including splices inside a sequence. Lexing precedes preprocessing,
  so discarded branches, unused macro bodies and stringized arguments are checked.
  The compact lexer reuses its high-byte mask to escape at the whole word's
  start; ASCII words retain batch emission. Comments, quoted header names and
  literal payloads retain their existing byte/literal-conversion policies;
  angle-header words use the ordinary token encoding rule. This contract does
  not change shared `utf8_decode` or OS path/argument handling. The independent
  validity oracle, guard-page/window differential cases and driver failures are
  registered in the frontend/driver suites; `basic_c_utf8_identifiers.c` checks
  valid source through every native allocator (GitHub #253).
- Arena ownership is part of the API contract. Returned source, syntax,
  semantic, and IR structures may reference earlier-stage storage; callers must
  retain the translation-unit arena until every downstream consumer finishes.
- Source-map regions retain append order for equal `start` keys. Finalization
  uses an allocation-free ordered scan or four stable byte-wise radix passes
  over the 32-bit key. The one temporary row buffer is rewound before origin
  recovery and publication; the original region array remains authoritative.
  Do not restore displacement-dependent insertion sorting for `#line` splits.
  Publish lookup keys before C23 respelling so its origin queries can read the
  existing prefix. `c_source_map_publish_appended` rebuilds keys afterwards only
  if that append-only phase added regions; without an append, keep the original
  keys and sentinel. Count equality is not a general mutation-cache contract.
  Respelling may move the region array, but must not invalidate the published
  key prefix while querying it. Neither publication rewinds the TU arena:
  canonical lowering copies the map's pointers into `IrProgram.source_map`,
  whose diagnostic, DWARF and CodeView consumers still borrow their storage.
- Zero-initialize aggregate tables before publishing a partially resolved type.
  Recursive and mutually dependent declarations can expose an aggregate while
  later members are still unresolved; an uninitialized `IrField` must never be
  mistaken for a valid type or source reference.
- Canonical IR owns format-neutral functions, blocks, instructions, values,
  symbols, source ranges, types, and relocations. Shared layers must not contain
  frontend entity IDs, parser AST pointers, or language-specific invalid
  sentinels.
- Validate IR before machine selection or Wasm emission. A diagnosed frontend
  failure must not publish an apparently valid partial function to codegen.
- Constant initialization must preserve the source type and every value limb.
  The direct aggregate leaf paths in `c_gen.c` use a nonzero test for `_Bool`
  and `c_ir_constant_integer_to_float` for unsigned integers, with rounding at
  the destination precision. The general aggregate writer stores both limbs
  of a 128-bit integer. `c_ir_constant_float_to_integer` decodes binary64 bits,
  truncates fractions before checking the destination range, and refuses
  nonfinite or unrepresentable conversions without executing an undefined host
  cast. Automatic nested initializers recognize a string as its whole array
  subobject before brace elision. Pin these paths with independent object bytes
  as well as runtime comparisons: the initializer fixtures also exposed a
  selected x86-64 float-to-u64 conversion whose binary32 threshold encoded
  2^31 instead of 2^63. Runtime float-to-128-bit conversion on x86-64 remains
  unsupported; constant conversion supports both integer limbs.
- Automatic chained designators in `c_ir_lower_nested_compound_literal_step`
  retain a continuation cursor for every selected aggregate container. A
  following positional item resumes at the innermost remaining sibling and
  advances outward only when that container is exhausted. Named members may
  cross anonymous structs or unions; `c_ir_nested_initializer_field_cursors`
  records each emitted field edge, while array steps record their selected
  index directly. The driver's `c_designator_continuation_source` checks
  automatic and file-static values, compound literals, and outward
  continuation across both frontend forms and all native allocators (GitHub
  #1206).
- `c_parse_index_scope_children` stores siblings in token-interval order.
  Source-ordered rows keep a linear construction path; synthesized rows use
  iterative merging with the finished CSR cursor storage as scratch.
  `c_parse_scope_for_token` binary-searches the last child starting at or
  before the query, checks its exclusive end, and descends iteratively.
  Sibling intervals must remain disjoint; equal-range nesting resolves to the
  deepest child. Empty siblings sort before nonempty siblings at the same
  start. Queries before index construction retain the unindexed fallback.
- `CAggregateLookup` doubles its slot array at half occupancy. Its stable
  header and every rehashed slot survive speculative rollback; live type IDs
  are revalidated, qualified aliases cannot own tags, and duplicate scoped
  tags retain the scope-aware fallback. Only arena exhaustion or count overflow
  makes the index incomplete. `c_test_aggregate_lookup_growth` covers both
  8,192 and 16,384 tag boundaries and rollback across growth. With
  `BUSTER_BENCH_ALLOCATIONS=ON`, it also bounds production probes/rehash work
  and requires zero fallback type visits for unique tags;
  `BUSTER_AGGREGATE_CENSUS=1` prints these diagnostic-only counts.
- Each aggregate initializer context retains a `CIrInitializerRelocationExtent`.
  Before clearing a subobject, it incorporates only relocation records appended
  since the preceding query. Clears wholly outside the occupied extent skip
  relocation compaction. Overlapping clears preserve stable record order and
  recompute the surviving bounds during that same compaction. The extent is a
  conservative overlap test: holes inside it and arbitrary repeated overwrites
  still take the full compaction path. GNU range copies use their parent
  context's extent; separately materialized range values own a fresh context.
- `c_parse_validate_constexpr_declaration` validates a leaf root from one local
  work entry, without acquiring scratch or clearing the translation-unit type
  universe. Arrays, structs and unions retain the explicit private graph walk.
  Both paths use the same qualifier and complete-object checks, in the same
  diagnostic order. Pointees are not subobjects. No result is cached across
  mutation or rollback. `c_test_constexpr_leaf_storage` checks reused scratch,
  unrelated type populations, qualifier mutation/restoration and diagnostics;
  `tests/basic_c_constexpr_leaf.c` runs in C23 under every native allocator.
  Composite-query universe-sized scratch remains tracked in GitHub #259.
- **GNU's `__alignof__` takes an expression; `_Alignof` takes only a type
  name.** Both spellings reach the same fold in `c_gen.c`, and it resolved an
  expression operand only for a compound literal until libc-test's
  `tls_align_dso.c` reached it: the file fills a table with `__alignof__(x)`
  over four `__thread` objects, and every other shape refused with "could not
  lower logical expression core". The operand now goes through the resolver
  `sizeof v` already used, which answers the alignment of the operand's **own
  type** -- `__alignof__(arr)` over a `char[7]` is 1, not the 8 the expression
  type prediction would give the pointer it decays to in any other context.
  The prediction is still the last resort for both words, under the same
  guards: an inline aggregate definition and an object whose array type never
  mapped are refused rather than guessed at. `tests/basic_c_alignof_expression.c`
  is the fixture, and every value in it was compared against clang.
- **`void` is one byte, and an object of it is still refused.** GNU gives
  `void` a size and an alignment of one so that arithmetic on a `void *` steps
  by bytes, and clang and gcc both fold `sizeof(void)`, `sizeof(const void)`
  and `_Alignof(void)` to 1. This compiler folded 0, and the index that `p + 3`
  becomes is scaled by the pointee's layout size, so the pointer did not move
  at all -- a silently wrong address rather than a diagnostic, and `q - p` was
  refused outright because the divide by the element size would have been a
  divide by zero (#743). The answer lives in `c_parse_builtin_type_layout`,
  which is the one table **both layout engines** read: `c_parse_type_layout`
  folds `sizeof` through it during the parse and `c_ir_scalar_type` builds the
  `IrType` from it, so there is no second place to keep in step. Nothing
  downstream had to change, because every backend already scales an
  `IR_OPCODE_INDEX` by the element type's size and every question about `void`
  that is *not* its size is asked of `IR_TYPE_VOID` rather than of a zero.
  The size is an extension for that arithmetic and for `sizeof`, **not a
  licence to declare a `void` object**: C 6.7p7 wants a complete type and both
  reference compilers refuse `void v;`, `void a[4];` and a `void` member. Those
  refusals used to fall out of the zero size -- an aggregate whose layout never
  resolved, a local whose alignment was zero, a file-scope object that reached
  code generation and failed there naming `main` rather than the object -- so
  they are asked of the kind now, through `c_ir_type_is_void_object` in
  `c_gen.c`, at the two local-declaration sites, the global definition walk,
  and the aggregate member layout, which reports through the same
  one-per-type slot a rejected alignment specifier uses. The predicate
  descends array elements alone: a qualified copy keeps the base's kind, so
  `const void` answers the same, and a `void *` is a pointer and answers no.
  `tests/basic_c_void_size.c` pins every runtime answer under all four
  register allocators -- both orders of the addition, the two subtractions,
  `++`/`--`/`+=`/`-=`, and the qualified pointees -- reading each stepped
  pointer back through a live object so an address that folds correctly and
  lowers wrongly still fails; `c_test_void_object_refusals` pins both layout
  engines' number and the four refusals.
- An integer converted to a pointer reaches pointer width in the frontend,
  before `IR_CONVERSION_INTEGER_TO_POINTER`, sign-extending when the operand is
  signed. All four backends lower that conversion as a plain register copy and
  LLVM's own `inttoptr` zero-extends, so a narrower operand that arrives
  un-widened silently loses its top half: `(void *)-1` — musl's `MAP_FAILED` —
  came out as `0x00000000ffffffff` and hung four libc-test units for as long as
  it was expressible. `c_ir_emit_integer_to_pointer` is the one place that
  performs it, a null pointer constant is built at pointer width so it costs no
  instruction there, and `ir_canonical_conversion_valid` rejects a
  non-pointer-width operand outright. `tests/basic_c_integer_to_pointer.c`
  pins the answers under every allocator.
- `main` is the one function whose fall-off is defined: reaching the `}` that
  terminates it returns 0 (C 5.1.2.2.3), where every other non-void function's
  is undefined (C 6.9.1p12) and terminates with `IR_OPCODE_UNREACHABLE` — the
  `ud2` Clang and GCC also emit. `CIrSignature.returns_zero_at_end` decides it
  once, with the signature, from a file-scope declaration named `main` whose
  return type is `int`; the terminator does not match a name. Getting it wrong
  is silent until the program ends: `regression/sem_close-unmap` and
  `functional/mntent` in libc-test have no return statement anywhere and both
  ran correctly to their last statement before dying on the brace with SIGILL.
  `tests/basic_c_main_implicit_return.c` pins it under every allocator, and
  exit zero is reachable there only through the closing brace.
- `builder->size_type` and `builder->ptrdiff_type` are chosen against the width
  of the scalar type the lowering built, not against `program->data_layout`'s
  own `unsigned long` entry. The two can disagree: the layout comes from the
  *preprocess* result and the scalar types come from `c_lower_to_ir`'s `target`
  argument, so a caller that preprocesses with one target and lowers with
  another — every frontend test does, preprocessing with a default target —
  otherwise gets a `size_t` narrower than a pointer on an LLP64 host. Nothing
  noticed while that only reached arithmetic; the widening above notices
  immediately, which is how a platform-independent defect first showed up as
  one platform's CI failure.
  `c_test_pointer_width_integer_conversion` lowers the same source for an LLP64
  target and an LP64 one and checks both.
- Parse-side builtin-kind answers keyed to `preprocess.target` come from one
  immutable `CTargetBuiltinFacts` record, which `c_preprocess` resolves for the
  target it stamps on its result and hangs off `CPreprocessDetail`. It holds
  each kind's `c_parse_builtin_type_layout` answer and integer-literal limit,
  and is built by calling those helpers, so it cannot disagree with them. Read
  it through `c_builtin_facts_from_preprocess` only at a call site whose target
  argument is that result's own `target`: lowering's `target` can differ and
  keeps its own `CIrTypeContext` tables, a caller-supplied `data_layout` never
  feeds it, and a hand-built result has no detail block and falls back to the
  helpers. Non-optimized builds check every record read against the helper.
  `c_test_builtin_facts_target_alternation` alternates LP64, LLP64 and
  AArch64 units in one process and compares the record, the hand-built
  fallback and a failing assertion's diagnostic on each.
- `IrFunction.opcode_summary` answers *may this function contain opcode X*
  without a scan, and it answers only for the `IR_OPCODE_SUMMARY_TRACKED`
  list: an opcode outside that list is never recorded, so
  `ir_function_may_contain_opcodes` rejects a query naming one rather than
  reporting a confident absence. Adding a query means adding its opcode to
  the list in the same change. The summary is exact only for functions
  created by `ir_module_add_function` and filled by
  `ir_function_add_instruction`; IR whose rows were written straight into
  `instructions` reads as unknown and every consumer keeps a scan for it.
  Frontend SSA and shared compaction already sum operand slots for their dense
  pools. They publish `operand_total` with `operand_total_rows`; FAST reuses
  the total only when that population still equals `instruction_count` and the
  opcode summary is known. Appends change the population and reopening a
  published CFG clears it. Construction retracts rows before frontend SSA publishes this fact.
  Missing/stale totals and unknown summaries retain the conservative row scan.
  There is no per-append operand accounting.
- Source diagnostics in shared layers use canonical `IrSourceRange` and
  `IrSourcePosition`. Do not reintroduce parser-specific source-range APIs into
  codegen, debug information, object writing, or the linker.
- A value defined by `IR_OPCODE_GLOBAL` is a *place*: its frame slot holds the
  object's address, so it is an eightbyte whether or not the object's type has
  a resolved layout. That is what lets C's `extern struct opaque object;` be
  addressed without ever being completed, and musl's `src/include/stdio.h`
  depends on it — it suppresses the definition of `struct _IO_FILE` and then
  declares `extern FILE __stderr_FILE`, so every `stderr` in the tree takes
  the address of an incomplete object. Codegen's two value-sizing loops
  therefore require a resolved layout of every value *except* a global place;
  requiring it of all of them cost eight musl units with an
  `INVALID_IR` blamed on whichever function happened to be first in the
  module.
- **An array lvalue is never loaded**, whichever expression names it. A named
  array keeps its place, and so does `*p` on a pointer to an array: C 6.5.3.2p4
  says the dereference designates the array object, and what follows either
  decays it or indexes it in place, neither of which reads anything. Loading
  one instead emits an `IR_OPCODE_LOAD` of the array type, which the code
  generator honours by copying the whole object into a frame temporary — so
  `(*p)[1] = v` indexed the copy and the store was dropped with no diagnostic
  (#719), while `p[0][1] = v` next to it worked. The expression walk in
  `c_gen.c` returns the place from its dereference arm for the same reason its
  address-of arm accepts one; `tests/basic_c_packed_layout.c` runs all three
  spellings of the store and `c_test_frontend_global_types` pins that no lowered
  function holds a load of array type. The decay is the half a libc trips
  over: musl's strftime writes into `*s` through `snprintf` and returns it from
  a `char (*s)[100]` parameter, so the copy took every formatted specifier with
  it and libc-test's `functional/strftime` failed all 64 of its checks.
  `tests/basic_c_pointer_to_array_place.c` pins that shape under all four
  register allocators.
- Variably modified arrays use flattened scalar pointers in canonical IR.
  `CIrVlaValue` in `c_gen.c` keeps the frontend's saved element type, bound
  arrays, remaining dimension and array-lvalue/pointer-rvalue distinction in
  a lazy per-function hash table. A partial subscript retains an array-lvalue
  witness: ordinary decay advances one dimension, while `&` preserves it.
  Indexing, pointer offsets/differences and dereferencing use that saved shape;
  pointer-local reads restore their declaration's bounds. Explicit scalar
  pointer casts discard it, even when canonical pointer types match. `sizeof`
  of a named VLA pointer's dereference reads the cached suffix size.
  A saved size does not suppress evaluation of a VLA-typed operand. The
  sizeof continuation evaluates its operand once through the existing
  expression machine, discards the row address, and retains that size.
  The remaining original C type decides evaluation: a variable outer
  bound does not make a fixed-size row or scalar operand evaluated.
  Declaration bounds are not reevaluated and array data is not read.
  `c_test_sizeof_vla_evaluation` checks raw calls/volatile stores and
  runtime results across contexts, saved bounds and outer sizeof.
  Compatible conditional pointer results retain their shape through the
  result slot and remain rvalues. Concrete consumers normalize any pointer
  shell retained by unevaluated type prediction before applying scalar element
  scaling. The table dies with the frontend builder; no frontend IDs or shape
  entries enter canonical IR. Every value-ID retraction invalidates its entry
  while retaining the hash key, including bulk bit-field recovery rewinds.
  `tests/basic_c_vla_row_address.c` runs byte/wider-element rows, multidimensional
  partial subscripts, captured bounds, pointer-slot addresses and joins across
  both frontend forms and all native allocator modes. Frontend tests separately
  reject address-of pointer rvalues and check that volatile rows are not read.
- **An aggregate member the next `.` walks through is a place, not a value.**
  `((T *)p)->a.b` names b, and C 6.5.2.3 gives the route to it no read of its
  own. The expression walk in `c_gen.c` loaded `a` anyway and then recovered
  the place it needed from that load's own operand, so every answer was right
  and the copy was dead — but a dead copy is still a read of memory, and
  offsetof is written on the null pointer. A compiler that does not advertise
  `__builtin_offsetof` gets musl's other spelling,
  `((size_t)( (char *)&(((type *)0)->member) - (char *)0 ))`, so a member named
  through two accesses copied a whole object out of address zero. musl's
  `__pthread_exit` takes exactly that offset for every mutex on the robust list
  — `_m_next` is `__u.__p[4]` — and died there with SIGSEGV, which is what
  libc-test's `functional/pthread_robust` and `regression/pthread-robust-detach`
  reported as #737. Predefining `__GNUC__` in every dialect moved musl to
  `__builtin_offsetof` and closed both units before this rule landed, so the
  spelling that reaches the walk is now a program's own rather than a libc's:
  measured 2026-08-30, the libc-test classification is identical with and
  without this rule. The member arm keeps the place when the following token
  is a `.`, which is the same rule as the array arm beside it; a chain that
  ends at the aggregate still loads it, so a by-value read is unchanged.
  `tests/basic_c_member_chain_place.c` pins the offsets against a live object
  under all four register allocators, spelling the pointer form directly so it
  does not depend on which offsetof a header picks, and faults the way musl did
  if the copy comes back. The peek only sees the token after the member
  identifier, so a group hides the `.` that follows from it — `(*o).a.b` and
  `&(((T *)0)->a).b[i]` reach the next access with the load already emitted,
  the dereference arm having emitted the first one and the member arm the
  second (#741). The place `c_ir_emit_field_place_from_value` recovers from
  such a load therefore *drops* it, through the same
  `c_ir_recover_place_from_value` the address-of arm of `c_ir_apply_operation`
  uses for `&E`: the load must still be the last instruction emitted, which it
  is because the access that recovers from it is the next thing the walk does.
  A group is not a frame of its own — an ordinary one is a `C_CONDITIONAL_OPEN`
  marker on the same expression frame's operator stack — so nothing about this
  needs a place-or-value request threaded through the lowering machines.
  `c_test_frontend_global_types` pins that no lowered function holds a load of
  a struct or union type nothing reads, and that the by-value read beside it
  keeps the one it needs.
- **Member-search scratch follows the visited anonymous graph.**
  `c_ir_emit_field_place_from_value` keeps 32 type/parent/field/depth rows on
  the C stack and grows all four arrays together in its temporal arena only
  when the visited prefix fills them. Parent indices and breadth-first order
  survive growth; the type-table count remains a validity and cycle bound,
  not an initial allocation size. The reversed result path needs only
  `found_depth + 1` fields, with the same 32-entry local allowance. Emitted
  places, operands, immediates and missing-member diagnostics remain in the
  persistent builder arena. The private `c_gen_internal.h` test seam checks
  these lifetimes by poisoning released scratch, including broad/deep,
  ambiguous, missing, cyclic and qualified-place cases.
- **A value never carries a qualifier.** The frontend builds a qualified copy
  of a type wherever a qualifier is written, because a place, a pointee or a
  member has to carry it, and that copy keeps the base's kind and layout: it is
  one representation under two ids. Both load emitters and the store emitter
  therefore unqualify a `volatile` place the way they already unqualified
  `_Atomic` -- an lvalue conversion yields the unqualified type of the object
  (C 6.3.2.1p2) and an assignment converts to the unqualified type of the left
  operand (C 6.5.16.1p2) -- and `c_ir_emit_cast` treats a difference of only
  `volatile` as no conversion at all. The value ladder there spans the scalar
  kinds, so while a struct crossing the qualifier had to find an arm in it,
  `volatile sigset_t oldset = set2` was refused outright and every unit written
  around `setjmp` failed to compile (#735). Volatility itself never travelled on
  the type: a load and a store carry `volatile_access`, taken from the place's
  own flag, which is why none of this changes which accesses are volatile.
  `ir_types_differ_only_in_volatile` is the one predicate, and
  `ir_validate_canonical_module` admits exactly that difference between a plain
  `IR_OPCODE_LOAD` or `IR_OPCODE_STORE` and its place -- the pairing the atomic
  opcodes were always validated with. `tests/basic_c_volatile_aggregate.c` pins
  both directions of the qualifier under all four register allocators.
- Canonical block IDs are graph identities, not an execution order. A valid
  `IrFunction.entry` may name any block. Native canonical and eBPF emission
  place that entry first, then retain ID order for the remaining blocks; branch
  fixups continue to use original block IDs. Incoming argument capture belongs
  to the declared entry, and debug-location endpoints follow emitted layout,
  not the next numeric ID. `canonical_entry_test_internal.h` renumbers valid
  source-derived graphs before emission and checks arguments, joins, loops,
  native bytes/execution, eBPF execution, and debug ranges. The ordinary C
  frontend still creates entry ID zero; these regressions protect the shared
  canonical-IR API rather than claiming it currently emits nonzero entries.
- Native lowering is `canonical IR -> machine IR -> scheduling/register
  allocation -> encoding`. Selection patterns and scheduling classes remain
  separate metadata domains even when they share instruction-form IDs.
- Every primitive type-specifier scan validates the collected word set through
  `c_parse_primitive_specifiers_valid`, and `c_parse_ast`'s token walk calls
  `c_parser_validate_type_specifiers` on each contiguous type-word run so
  unused bodies, `_Static_assert`/`sizeof` operands, and other machineless
  type queries diagnose before semantic analysis. A contradictory or
  unsupported set
  (`signed unsigned`, `long float`, `_Complex int`, any `_Imaginary`, repeated
  type words, more than two `long`s) is a `C_DIAGNOSTIC_INVALID_TYPE_SPECIFIERS`
  error at the first specifier word, never a silent drop that lets an
  implicit-int or guessed-int answer continue. Complex-integer and imaginary
  types are deliberately unsupported, not partially accepted; `_Complex` alone
  and its `__complex`/`__complex__` aliases on the three C99 real floating kinds
  remain valid. `_Float16` and its complex extension use the same validity
  gate in both scanners, retaining the binary16 specifier contract while
  rejecting contradictory or repeated half-type words with a diagnostic.
  A type name refused this way pins `sizeof`/`_Alignof` to the
  recorded constraint instead of falling back to a guessed `int`.
- A `struct`, `union` or `enum` specifier names a type exactly as a primitive
  word does, so a set that spells both (`int struct S`, `struct S unsigned`)
  or two tags (`struct S enum E`) is the same error at the same first
  specifier word. `c_parse_type_specifier_token` is the one question behind
  it -- it runs `c_ir_primitive_type_kind` over a single token, so no second
  list of spellings can drift from the scans -- and it is asked of the words
  the aggregate scan stepped over to reach the tag keyword, of a type word
  left standing where a declarator belongs (which is also how a second
  specifier after a typedef name is caught), and of each word in a token-walk
  run that carries a tag. That run spans the tag's name, so `struct S int` is
  one run and not two; a definition body ends it instead, leaving the
  declarations inside the braces to be walked on their own. Before this,
  `int struct S v` took the aggregate's layout under a spelling it never had
  and `struct S int v` was accepted with no definition emitted at all.
  Regressions:
  `c_test_type_specifier_diagnostics` and `compiler_driver_test_type_specifiers`,
  which also verify a refused compilation preserves or never creates the output.

## Immutable aggregate and complex construction

`IR_OPCODE_AGGREGATE` captures already-evaluated `IR_VALUE_VALUE` operands
at their field types and produces another `IR_VALUE_VALUE`. It never loads a
place implicitly and never creates an addressable result. The canonical
validator rejects either a place operand or a place result. A consumer that
needs object identity must explicitly materialize storage.

Complex rvalues use this same operation through `c_ir_complex_compose`, not a
`LOCAL`/`FIELD`/`STORE`/`LOAD` construction sequence. Arithmetic consumers in
`c_ir_complex_split` project a known constructor's two scalar operands directly.
This bounded projection does not apply to the `__real__`/`__imag__` lvalue path:
component assignments must continue to designate their original object.
Qualifiers, volatile memory, and non-constructor values retain their existing
explicit load/store path. No whole-function cleanup pass is required. Raw IR
tests cover both direct frontend SSA and its memory-form reference, so scalar
parameter promotion cannot conceal complex construction temporaries.

`c_ir_emit_initializer_capture` keeps the exact constructor operand contract.
When a volatile-qualified field and its converted value have distinct types,
it captures the already-evaluated operands through explicit subobject stores
and loads the completed aggregate. This also handles omitted zero initializers;
it does not retag an existing value or change the field's layout/qualification.
The ordinary exact-type constructor stays immutable. The strict
`basic_c_ir_validation_values.c` corpus covers lowered/raised alignment,
volatile loads/stores, nested records, unions, arrays and single evaluation.

Complex comparisons/truth conversion and floating classification combine
Boolean comparisons with `IR_BINARY_BOOLEAN_AND`/`IR_BINARY_BOOLEAN_OR`.
Their canonical verifier case requires matching Boolean value operands and a
Boolean value result. Integer bitwise opcodes still require integer operands.
Both native canonical emitters implement these Boolean operations as well as
the existing machine selectors, including canonical fallback for x87 functions.

## ABI decomposition ownership

`IrType` holds language identity and layout only. Each `IrAbiContext` owns one
calling convention's decomposition cache, keyed by canonical type id and ABI
use. `IrProgram.abi_contexts` creates contexts only for conventions requested
by the frontend or a target consumer. Independent contexts may share the same
immutable language types. `ir_type_abi_value` remains the shared call-lowering
query used by native consumers and the frontend; explicit contexts use
`ir_abi_context_value`. Wasm, eBPF and LLVM do not acquire a native cache merely
by existing; only an actual ABI query creates it.

`IrAbiContext.sysv_unnamed_bitfields_integer` selects the narrow SysV unnamed
bit-field policy. It defaults to false, preserving historical Buster behavior.
`CIRLowerOptions` sets the requested context before lowering can make an ABI
query. Independent contexts can classify the same immutable types with different
policies. Changing a context's policy after a query requires invalidating that
context, just as changing layout does; neither selection mutates `IrType`.

Cache pages contain 64 types for one use, with a resolution mask; values are
initialized before their bit is published. Variadic arguments reuse argument
classification except on Windows AArch64, whose convention distinguishes them.
Unresolved layouts are not cached. Adding a type under a fresh id is supported;
changing an existing layout requires `ir_program_invalidate_abi` (or invalidating
every independent context), because dependent aggregate classifications change
too. Neither cloning a type nor querying an ABI mutates the type record.

`ir_prepare_program_abi` reserves the requested/default, explicit function, and
already-used contexts before native code generation opens its retry checkpoint.
It does not classify unused types. Queries during an attempt fill resident pages
without retaining arena allocations that a code-buffer retry could discard.
The type table must not grow beyond the reserved count inside such a checkpoint.
`allocated_bytes` counts context page/directory bytes; `classified_values` counts
completed cache misses cumulatively, including misses after invalidation.

## Large aggregate initialization

Plain aggregates of at least 4 KiB use a bounded canonical byte loop for their
initial zero image, then the existing nested initializer walker applies explicit
values and designators once. The type worklist checks every member and rejects
volatile or atomic storage from this representation shortcut. Existing integer,
floating, boolean and null-pointer zero representations are retained; no libc
helper is introduced. Small or qualified aggregates retain typed construction.
The large-frame fixture checks zeroed nested storage, explicit values, copies
and odd byte tails under both frontend lowering modes. This prevents initializer
expansion from exceeding Windows ARM64's unwind function-size limit.

- C lowering records every named and temporary local in the canonical
  `IrFunction.local_places` projection at creation. The frontend-private
  capacity grows monotonically; unused capacity is never read. Frontend SSA
  and canonical compaction remap or invalidate its place IDs before later
  promotion and selection consume it. No frontend entity IDs enter the map.

## Enumerator types and declaration order (#900)

An enumerator retains three distinct facts: the original typed initializer ICE
(`integer_constant`), the immutable `declaration_type` visible to later
initializers in its own list, and `type`, finalized at the closing brace. The
owning enum records its compatible integer type in `element_type`; the separate
`has_fixed_underlying_type` bit distinguishes an explicit base from a resolved
ordinary enum. Completing the enum never rewrites the original ICE or its
recorded declaration-point type.

The supported contracts are explicit, not selected by the host compiler:

- GNU17 follows the historical Clang extension: an explicit int-representable
  initializer declares an `int` enumerator; otherwise it uses the initializer's
  integer type. On completion, each int-representable member remains `int`;
  larger members use the completed enum type.
- C23/GNU23 follows WG14 N3029: declaration-point typing is the same for those
  explicit initializers, but an ordinary completed enum containing any value
  outside `int` gives **all** members its enum type. An all-small list keeps
  `int` enumerators. Fixed-underlying enum members have the enum type both
  during the list and after completion, including narrow bases.

Ordinary compatible-type selection considers both full-width signed-magnitude
limits. It chooses an unsigned type for an entirely nonnegative range, or a
signed type when negative values occur, trying int, long, long long and the
supported 128-bit extension in rank order. Widths come from the target, not the
host: the same 2^32 value therefore selects unsigned long on LP64 and unsigned
long long on LLP64. A range no available integer type represents is diagnosed.
Implicit successors retain the predecessor's **declaration-point** type, not
its original initializer ICE type or its eventual completed symbol type. This
applies even when a negative wide value increments back into the int range;
only an explicit int-representable initializer resets the declaration type to
int. Both the supported GNU17 extension and C23 use the same transition rule.
On overflow of that type, the existing range policy selects a suitably sized
integer with the same signedness: INT_MAX advances to signed long on LP64 or
signed long long on LLP64, UINT_MAX to the corresponding unsigned type,
LLONG_MAX to signed 128-bit, and UINT64_MAX to unsigned 128-bit. Completion
still chooses one compatible type from the entire enum's range, independently
of these intermediate types.

`c_parse_enum_successor` uses two unsigned limbs and signed magnitude. It
handles low-limb carry, negative borrow and normalization of negative zero,
and diagnoses the signed/unsigned 128-bit terminal boundaries instead of
switching signedness or wrapping to zero. An invalid predecessor stays invalid
through following implicit members; an explicit initializer can reset the
sequence. Explicit initializers never speculatively compute a successor, so an
explicit reset immediately after the largest supported constant is valid.
Fixed-base successors use the same arithmetic but must fit their declared base
and never widen. Explicit fixed-base initializers pass the same signed-magnitude
representability check after typed evaluation, including all casts in the ICE,
and before the member's fixed declaration type is published (#903). A negative
value cannot fit an unsigned base; an explicit cast can change that value before
checking. Range failures diagnose the enumerator's name and invalidate its ICE,
so following implicit members stay invalid until an explicit reset. The declared
base and the original ICE's magnitude/type are never widened or narrowed to make
an invalid value fit. GNU17's fixed-base extension and GNU23 use this same rule.

Pending lookup respects lexical scope and declaration order, including a nearer
ordinary identifier shadowing an outer enumerator. Published names use ordinary
lookup; only the current incomplete list can shadow them before publication.
File- and block-scope entities publish the same member type, independently of
pointer/typedef/object declarators sharing the enum definition. `typeof`, sizeof,
constant folding and runtime operands consume that type. Static assertions using
enumerators defer to typed semantic evaluation instead of replacing names with
untyped decimal spellings. Full-width runtime constants use ordinary canonical
shift/or operations; the one-immediate integer-constant contract is unchanged.

`c_test_enumerator_types` pins both contracts across Linux x86-64/AArch64 and
Windows x86-64, and validates canonical IR in both frontend SSA forms.
`c_test_fixed_and_wide_enumerator_types` covers narrow fixed bases, preserved
128-bit references and signed magnitudes. On Linux x86-64,
`c_test_enumerator_type_differential` executes the same assertion-bearing source
with Clang GNU17 and GCC GNU2x at O0/O2, then with Buster GNU17/GNU23 in both
frontend forms with strict codegen verification. The independently verified
reference versions are Clang 17.0.0/21.1.8 and GCC 14.2.0/15.2.0. Clang 20's
[N3029 implementation](https://github.com/llvm/llvm-project/pull/103917) also
changed its pre-C23 extension behavior: Clang 20 and later use the C23 completion
profile in GNU17 mode; earlier Clang uses the historical profile. The external
Clang source selects these two hardcoded profiles using `__clang_major__`, not
observed probe results. GCC GNU2x always uses the C23 profile. GCC 14's GNU17 mode
already applies its C23 completion rule, whereas Clang 17's GNU2x mode still uses
its older rule; dialect names alone do not identify an external oracle.

Buster receives a separate source file with an unconditional `ENUM_C23=0` or
`ENUM_C23=1`, selected only by its requested dialect. The external prefix never
reaches Buster, so a reference upgrade cannot weaken its GNU17 checks or silently
change its semantics. Every reference must compile and execute the full
assertion-bearing fixture. A compiler failure reports the executable, dialect,
optimization, native status, timeout and captured stdout/stderr; failed source
writes never launch a compiler against an earlier temporary file.

Specification: [WG14 N3029](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3029.htm)
and [N3030 fixed enums](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3030.htm).

## Implicit successor boundary regressions (#901)

`c_test_enum_successors` checks exact low/high limbs, signed magnitude, width,
rank, immutable declaration type, later initializer observations and completed
types. It parses and validates canonical IR in GNU17/GNU23, both frontend SSA
forms, and Linux/Windows x86-64/AArch64 target data models. Its assertion-bearing
sources cover chained crossings at INT_MAX, UINT_MAX, LLONG_MAX and UINT64_MAX,
negative wide values entering the int range, a 128-bit borrow, explicit resets,
and constants observed through globals and runtime functions.
`c_test_enum_successor_limits` checks diagnostic kind, message and source
location for consecutive invalid successors at both 128-bit terminal limits
and signed/unsigned fixed 8/64-bit limits, without changing the declared base.

`c_test_fixed_enum_ranges` and `c_test_fixed_enum_range_diagnostics` extend that
contract to signed/unsigned 8/16/32/64/128-bit bases, target-sized long, typedef
bases and bool. They cover exact endpoints, out-of-range explicit values,
signedness-changing casts, implicit overflow and recovery, with declaration-site
diagnostics and unchanged base types on Linux/Windows x86-64/AArch64 in GNU17
and GNU23. Positive inputs also validate both frontend IR forms. The existing
enumerator differential harness compiles and executes the fixed-base source
with Clang in both dialects at O0/O2 and with Buster in both frontend forms.

The Linux x86-64 `c_test_enumerator_type_differential` also executes the new
sources at O0/O2: Clang in GNU17/GNU2x for the 32-bit transitions and negative
int-range reentry, and GCC in both modes for the 64-to-128-bit transitions.
Local Clang 17.0.0 preserves the negative declaration type, while GCC 14.2.0
narrows that case to int; conversely Clang 17 does not support the required
implicit 128-bit transitions, which GCC 14.2.0 does. These are deliberately
separate, fixed oracle contracts, not a probe that adapts Buster's expected
values to whichever compiler happens to be installed. The existing versioned
Clang completion prefix stays reference-only; Buster always receives the
unconditional completion contract selected by its requested dialect.

The implicit declaration rule is specified by C23 draft N3096, 6.7.2.2p11
([WG14 draft](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3096.pdf)).

## Resolved enum lowering (#904)

`c_parse_enum_complete` is the authority for an ordinary enum's compatible
integer type. Lowering reads its stored `element_type` through `c_type_ir_map`;
it never rescans the enumerators or substitutes signed int. Enumerator runtime
values (`c_ir_emit_enumerator`) and constant identifiers use the completed
symbol's mapped semantic type, which can still be int for an individually small
GNU17 enumerator. `c_ir_emit_integer_value` is only the synthetic-int helper.
An unavailable constant type remains unresolved instead of folding through s32.

The existing type worklist also resolves enum bases that depend on a pending
typedef mapping. This matters for fixed bases with an alignment attribute:
their enum objects, return values and static initializers must all consume the
same resolved base. An ordinary forward enum tag has an incomplete canonical
enum type so pointers can name it without inventing a four-byte object layout.

`c_test_enum_lowering` checks the semantic compatible kind and canonical return,
parameter and global types on Linux, Windows and macOS, each on x86-64/AArch64,
in GNU17/GNU23 and both frontend forms. The assertion-bearing source covers
all-small signed/unsigned ranges, bit 31, 2^32, mixed negative/large-positive
values, implicit successors across UINT_MAX, UINT64_MAX, sizeof, comparisons,
widening, static initializers and volatile runtime values. The existing external
differential harness runs it with Clang/GCC at O0/O2; expected values are literal
constants, not inferred from Buster. The fixed-range fixture additionally checks
the aligned-base case against Clang. `c_test_enum_runtime` runs these two sources
and the bit-field source in all four native allocator modes with strict codegen
verification, rejecting machine fallback outside NONE.
