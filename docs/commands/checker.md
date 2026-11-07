# checker

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

Plain printf-family format inference follows the library builtin eligibility
policy (#1390): `-ffreestanding`, `-fno-builtin` and exact
`-fno-builtin-NAME` exclusions disable the name's inferred contract;
`-fbuiltin` follows the existing last-option policy. Explicit `__builtin_`
spellings and retained `format(printf, ...)` attributes remain checked.
`dprintf` has no builtin catalog row and requires an explicit format attribute.
Bare compatible function redeclarations inherit their visible predecessor's
format records without mutating published metadata; binding-only readers with
no optional attribute column retain its absence. Each call reads the
declaration visible there, so later or closed-block annotations do not leak.
A first block-scope extern remains discoverable by a later file declaration;
a block redeclaration of an existing outer declaration keeps its scope end.
Without a visible predecessor, the canonical first declaration supplies that
contract even when a later block redeclaration has expired.
Explicit library builtins use their retained intrinsic source-call receipt,
including after deferred-body merging.
The original default-warning fixtures cover those boundaries, redeclarations,
embedded NULs, complete Clang diagnostics, warning promotion/suppression and
scalar/vector object twins at one and three requested lanes.

Multi-declarator declarations reuse compressed ancestor paths (#1378):
`check_program_plans` finds initializer statements in a private parent copy
borrowed from its existing `seen` scratch column, then clears it before object
and flow processing. `check_input_unused_events` makes a separate scope-parent
copy only when selected local unused warnings have paid for one. Both callers
first walk original parents, accumulating their work; once that reaches the
unit's token count, subsequent queries use a copy. Cheap queries pay no copy or
extra allocation, and total uncached work is at most twice the token count.
The scope predicate retains the
nearest block/body/compound closing brace and leaves record braces out. Neither
copy changes the parser or outlives its phase; each predicate has its own copy.
When activated, parent-copy initialization touches four bytes per token. The warning phase also
reserves that amount of temporary scratch; program planning reuses existing
storage. These costs and small-unit latency remain part of physical performance
qualification, alongside 4,000/8,000/16,000-declarator scaling.

The reader's random-parent oracle covers both predicates, backward links,
cycles, missing ancestors, empty input and an immutable 4,096-link chain. The
existing poisoned plan tests still check exact statement links and compound
literal handling; a focused one/two/three-query fixture checks that the copy is
delayed until the raw-walk budget is spent. Native scalar/vector source fixtures cover 257 static/local
declarators, nested scopes, for declarations and statement/compound expressions;
unused-warning fixtures also check exact scope-completion tokens.

- `src/buster/compiler/c_check_actions.c` supplies the reader-independent
  expression rules and constant folding for #133. `c_checker.c` checks an
  explicit postorder operand graph either a node at a time or in batches of
  consecutive nodes that read none of each other (or, with
  `CheckOptions.level_order`, in stable dependency levels), with scalar
  barriers for actions that intern types.
  `c_check_input.c` and `c_check_literals.c` are the bounded source adapter;
  `c_check_declspec.c` and `c_check_literal_diagnostic.c` word its refusals of
  declaration specifiers that don't combine and of numeric constants as clang
  does, at clang's places (#1127), and only a refusal runs them; a constant
  before a unit's first syntax error, which only the parse reached, is checked
  first, so that its error is clang's first (`c_checker_driver.c`);
  its expression graph's walk has a vector path, the coded walk
  (`c_check_expression_input.c`, #1038), which acts at the compacted tokens by
  each one's class, numbers their nodes into a batch whose columns it writes 16
  nodes at a time (`check_input_expression_flush`), with the walk (`--scalar`)
  its twin (`docs/c-checker.md`'s "The coded walk"); `c_check_sparse.c` holds the output columns few tokens have
  an entry in, as 16-token blocks behind a directory (`docs/compile-time.md`'s
  "The sparse output columns", #628), and `attributes` as a wide one, of 64-bit
  values ("The attributes column", #1167); `c_check_binding.c` publishes each use's retained answer from those events
  directly (`--scalar`: the independent stack replay); `benchmark-checker-bindings
  [--pipeline] [--only MODE [--after-scalar MS]] [-E's options] file.c` times the whole reader
  including initialization, or fresh parsing through expression checking and cleanup with `--pipeline`
  with each handoff ([the binding handoff](../../docs/c-check-bindings.md), #336). `compiler check [--scalar] [--dump-typed] [--dump-static] file.c`
  and the default C command (`-fsyntax-only`, `-c`) use it; `compiler parse`
  keeps the parser's driver alone. Unsupported syntax is diagnosed.
  `test-checker-driver --executable ./build/compiler [--part K/N]
  [--lazy-system-headers]` and `test-checker-asm`
  check the driver and inline assembly against Clang (but for named UCNs,
  which clang accepts and the driver refuses at their places, #452; with
  `--lazy-system-headers`, only the checks that pass the driver
  `-flazy-system-headers`, since the others run the same either way, and
  `--part K/N` runs the K-th of N parts its checks are dealt to in turn,
  `CheckDriverPart`). Its library-builtin comparisons are pinned to Clang
  23.1.4, CI's reference: it compares `clang --version` with the release,
  prints `expected clang 23.1.4, found …` when another release is found, and
  skips only the plain names that release's table is known to classify
  differently (the named list `cbuiltin_library_table_differences`: 23.1.5's
  `strlcat` and `strlcpy`), still comparing every other name and every
  version-independent check, and reruns them under a fake clang 23.1.5 so the
  skip path is exercised under the reference too; the fake reaches `--clang`'s
  executable through a path with a space, quotes and `$;*`
  (`clang_release_wrapper`), so an awkward `--clang` path stays one word. `test-checker-corpus`
  compares a corpus's acceptance and first diagnostics with Clang, and its
  semantic mutants with `--semantic-mutations` ([Checker corpus and semantic
  mutations](checker-corpus.md#checker-corpus-and-semantic-mutations)), and
  `benchmark-checker-source` measures checking of real
  source bodies by the scalar schedule, the vector path in node order (`vector`, its default), its regions in
  postorder batches (`regions`, `CheckOptions.regions`) and by level (`levels`, `CheckOptions.level_order`), and its
  memoized transitions in node order (`transitions`, `CheckOptions.transitions`, #1050, `c_check_transitions.c`,
  keyed by interned value classes): #133's question of which order the regions take, which the postorder batches
  won on the 9700X (`docs/c-checker.md`, "Level order or postorder"), #1050's of what the regions do ("Node order or
  regions") and of the transitions ("Memoized transitions"), opt-in (the driver's `--check-transitions`) while they
  don't save a large fraction of `check-expressions`. With them, `--time-stages` reports their hits, misses, folds and entries as `time-check
  transition-*` lines, and the driver's `--verify` runs every hit's action too and fails on a difference.
  `test-checker [--seed N] [--clang PATH] [--part K/N] [--program-initializers|--program-vectors|--program-library|--embed-initializers|--requests|--transitions]` compares fresh scalar/vector tables,
  protected tails and original source fixtures against Clang, and checks the constraint catalogue
  (`c_check_constraints_test.c`): for every Constraints paragraph of C17's 6.4 to 6.10 a violation of each rule and
  a valid twin, and the conversions, constant expressions and initializers, each checked as the corpus check checks a
  file and against clang (`docs/c-checker.md`, "The constraint catalogue").
  `--part K/N` runs the K-th of N parts that the default run's sections and `check_test_programs`' programs are dealt
  to in turn (`CheckTestPart`); part 1 also runs the sections that share the run's type table and graph arenas.
  `--program-selection` selects the emission-selection, constant-if and constant-switch regressions alone.
  The switch fixtures (#1192, `c_check_program_switch_test.c`) cover matching/default/no-match dispatch,
  fallthrough, converted values, range ordering, label/declaration/inner-break fallbacks, nonfoldable selectors,
  nested scopes, cleanups, VLAs and dropped statics. The constant-if fixtures include #1194's
  statement-expression paths: scalar/vector and eager/lazy objects must agree byte for byte and match Clang's
  symbols and execution. These checks remain in the default suite too.
  `--direct-initializers` selects #1252's Clang parenthesized-initializer regressions alone: attributes and asm labels,
  first-argument selection with later arguments checked but not evaluated, nested comma expressions, scalar and
  aggregate conversions, static bytes, deduction and invalid forms. These checks remain in the default suite too.
  `--program-initializers` selects the automatic-initializer native regressions alone: threshold and odd tails,
  wide strings, embedded NULs, truncation, partial zeroing, constant lists and ordered runtime/volatile writes; and
  #1376's clears (`c_check_program_clear_test.c`): each path's shape (memset past 128 bytes, no loop, no clear before
  a whole object's copy, compound literals in place, a volatile object's stores) and native runs on a dirtied stack.
  `--program-vectors` selects the vector, x86 builtin and half-precision programs alone (`c_check_program_vector_test.c`,
  `c_check_program_x86_test.c`, `c_check_program_vector_builtins_test.c`, `c_check_program_fp16_test.c`,
  `c_check_program_half_test.c`), run against clang -O0's code where the host has their features.
  Byte funnel shifts of 16-byte vectors require XOP: these tests compile/link everywhere and execute only where
  XOP is present; `test-x86-intrinsics` also checks their emitted expressions against Clang, including `_mm_rot_epi8`.
  Required constant evaluation folds `_Float16` and `__bf16` arithmetic in the source format after each operation
  (#1497); runtime half-precision chains retain their target's excess-precision behavior. The constant-source and
  floating-action fixtures cover that distinction, ties, subnormals and negative zero.
  `--program-library` selects the library builtins' tables, constant folds and programs alone
  (`c_check_program_library_test.c`).
  `--transitions` selects the memoized transitions' fixtures, classes, static words and table limits alone (`c_check_transitions_test.c`).
  `--semantic-diagnostics` selects #1107's common semantic-error catalogue: exact Clang messages and columns,
  valid twins and a later syntax error, through both corpus paths and both driver paths. The default constraint
  suite includes it too. `c_check_semantic_diagnostic.c` spells the retained names, types, converted case values
  and decoded static-assert messages only on failure; corrected carets leave publication order unchanged.
  `--embed-initializers` selects real C23 resource files, independent and Clang object bytes, automatic native
  initialization and an 8 MiB resource whose parser tokens, graph nodes and initializer parts stay bounded. It also
  exercises the source driver and three deferred/function lanes; grant it at least three cores where `cores` is used.
  `--requests` selects #1057's request tests (`c_check_request_test.c`) and the source benchmarks' preparation tests.
  `c_check_self_assign_driver_test.c` checks #1391's macro self-assignment boundary. The warning is suppressed
  when the assignment operator or either variable-name use comes from a macro body or substituted argument;
  macro-produced parentheses or declarations alone do not suppress an ordinary source assignment. Original
  fixtures compare independent counts, severity and complete diagnostic lines with Clang through default,
  scalar and deferred reader routes, including warning flags and source diagnostic pragmas.
  `-Wsign-compare` retains the original unsignedness of both operands before narrow integer promotion (#1391).
  An `unsigned char` or `unsigned short` promoted to `int` cannot become negative; signed casts, arithmetic
  results and genuine signed operands keep their existing diagnostics. `c_check_sign_compare_driver_test.c`
  compares independent counts, severity and complete Clang diagnostic lines through default/scalar compilation
  and a deferred route that requires two admitted reader lanes. Run `test-checker-driver --sign-compare-only`
  with `--executable build/compiler` under a two-CPU grant; the shared correctness inventory
  supplies that grant. This is a type-local fix; general expression ranges and bitfield ranges remain separate.
  `c_check_noderef_driver_test.c`, in the attribute-effects driver section,
  compares original GNU `noderef` placement, propagation, conversion,
  address-cancellation and unevaluated-expression fixtures with pinned Clang
  in GNU C17 and C23. It checks complete diagnostic lines and notes through
  scalar, ordinary and poisoned deferred-reader paths, plus `-Werror=noderef`.
  Aggregate cases cover arrays, nested designators, anonymous records,
  overlapping union alternatives, atomic pointer elements, compound literals
  and GNU range initialization, including preservation and removal of marks.
  Each body has at most one warned dereference because Clang's delayed warning
  order for multiple accesses is unstable across invocations.
  Bitwise/comparison `-Wparentheses` diagnostics distinguish a single bare comparison from eager logical
  comparison pairs and unparenthesized bitwise chains (#1390). Parenthesized operands retain their explicit
  grouping. Original default-warning fixtures compare complete diagnostics and independent counts against
  pinned Clang through scalar/vector and one/three-lane readers, including warning promotion and suppression.
  Explicit void casts on a conditional arm's value path suppress the conditional's unused-result marker (#1390).
  The existing postorder conditional/comma and selected-value facts carry that discard intent through nested
  arms and transparent GNU/generic wrappers, while inner comma and division diagnostics still run.
  `c_check_void_conditional_driver_test.c` covers those boundaries,
  scalar-result controls, for clauses, macros and source policy through default and scalar modes and a requested
  deferred-option route. These single-file syntax-only commands are capped at one lane and use the ordinary-reader
  fallback; they do not establish admitted deferred-reader coverage.
  `c_check_return_flow.c` supplies source return-warning facts for checked graphs. Ordinary direct calls no longer
  suppress a function's `-Wreturn-type` warning; a proven direct `noreturn` call or terminating builtin ends only its
  own warning-flow edge. Short circuit, conditional and unevaluated operands retain their execution rules, and
  emission reachability is unchanged. Indirect calls (including GNU `noreturn` pointer/typedef contracts), overloads,
  analyzer annotations, semantic queries, conditional classifier contracts and library-policy contracts remain
  conservative until their retained facts are available here (#1393 remains open). Indirect calls in automatic
  initializer value parts share that conservative boundary, including ordinary pointers whose Clang warning is
  currently suppressed here; GNU noreturn pointer/typedef initializers need it until callable-type facts are retained.
  `c_check_return_call_driver_test.c` exercises direct-call fall-off,
  path-local termination, initializer/comma operands, warning flags and those conservative boundaries through the
  ordinary, scalar and deferred readers against Clang, with independent warning counts.
  `c_check_data.c` and `c_check_data_input.c` turn selected static scalar definitions into real
  Link atoms/fixups; their tests compare emitted ELF bytes with Clang objects.
  `benchmark-checker --shape wide|chain` measures synthetic graph checking,
  including scheduling and output materialization. `docs/c-checker.md`
  records the checker contract, declaration/initializer and control-flow
  integration, constraint catalogue, corpus qualification and measurements
  completed for #133. [Stage 4 acceptance](../type-checking-ssa.md) connects
  that evidence to the typed-stream and SSA consumers; expression checking
  alone does not establish whole-compiler semantic coverage.
- `src/buster/compiler/c_check_request.c` is one request's configuration
  (#1057): the code generation, instrumentation and layout options the
  driver refuses (`check_request_scan`, `check_request_refusal`, among them
  the hardening families below, and #1124's object and linkage options and
  #1112's sanitizers, coverage and OpenMP when code is emitted), a client's
  reading of its options
  (`check_request_read`, `check_request_parse`), a checked program's target,
  predefines and identifiers from its preprocessor
  (`check_request_program_options`) and a measurement's receipt
  (`check_request_receipt`). `c_check_source_prepare.c` prepares a real file as
  `-c` does for `benchmark-backend-source`, `benchmark-check-lanes`,
  `benchmark-lowering-source` and `ir_source_prepare`. Build a new real-source
  client's options with them, never by copying fields.
  [The request guide](../../docs/compilation-requests.md) has the contract,
  lifetimes, overrides and the series' discontinuities.

The focused `test-checker-fortify --clang clang --executable build/compiler`
suite also checks current glibc's Clang fortify wrappers at `_FORTIFY_SOURCE=2/3`,
including ordinary function addresses and scalar/vector, eager/lazy headers.
The object-size attributes currently use conservative unknown bounds, without
Clang's hidden-parameter ABI ([fortify compatibility](../c-builtins-fortify.md#current-glibcs-clang-wrappers-1361)).

## System-header token bitmap and MSan (#1610)

The source reader maps parser tokens' source indices to system-header bits in
`check_input_system_token_map`. It clamps indices at or beyond the preprocessor
output count to the user-file sentinel, gathers the low level byte in groups of
16 and finishes the tail with the scalar twin. History/run selection, arena
ownership and the graph's final sentinel remain in `check_input_system_tokens`.

The mapping kernel is deliberately `noinline`. In the pinned LLVM 23.1.4
([revision 7a941dfb](https://github.com/llvm/llvm-project/blob/7a941dfb2877a0b8cf30323fefa85ad019e0b0f5/llvm/lib/Transforms/Instrumentation/MemorySanitizer.cpp#L1480-L1508)),
MSan switches large functions to callback checks; wide shadows take a temporary
`alloca` at the check site. Inlining the bitmap loop into the large reader can
therefore make that temporary consume stack on each iteration. The small
separate function retains the vector path and every sanitizer check, and
avoids inheriting the reader's callback threshold. This is a source diagnosis
consistent with the actual native MSan O3 stack-overflow reports; generated
instrumented IR/assembly and post-fix sanitizer execution must be checked
separately. No stack limit or sanitizer suppression changes are involved, and
no performance improvement is claimed.

`test-checker`'s input-scan section runs `c_check_system_tokens_test.c`: 228
scalar/vector pairs at lengths 0 through 65, vector boundaries and 2,097,167
tokens, with output limits 0, 1 and 67. Every byte is compared with independent
level classes, including both system levels and other byte values. Source
columns end at protected pages; level padding ends at a protected page; output
prefixes and sentinels must stay unchanged. This is semantic and memory-boundary
coverage. A small direct caller alone need not reproduce LLVM's large-function
threshold. The existing repository-translation-unit checking command is the
previously failing MSan regression and remains required for crash validation.

## Object and linkage options (#1124)

The C driver refuses an effective `-ffunction-sections`,
`-fdata-sections`, non-default `-fvisibility=hidden|protected|internal`, or
`-fvisibility-ms-compat` when writing an object (`-c`, including `check -c`)
or linking an executable. The diagnostic names the option:
`error: unsupported code generation option '…'`. This is an explicit
unsupported boundary, not implementation of these features. The refusal
precedes input processing and output creation, including multi-file commands,
and preserves an existing destination in both Tier 0 and Tier 1.

`-fcommon` is implemented (#147). When it comes after any `-fno-common`,
every tentative definition of external linkage is an `SHN_COMMON` symbol,
and a multi-unit link merges the units' commons as GNU ld does.

Each family has independent last-option-wins state.
`-fno-function-sections`, `-fno-data-sections` and `-fvisibility=default`
retain the current output and cancel an earlier request in their own family.
For visibility, the last of `-fvisibility=…` and `-fvisibility-ms-compat`
decides, as clang's driver reads them: an earlier malformed value is ignored,
a malformed last value keeps clang's `invalid value` diagnostic, and an
effective `-fvisibility-ms-compat` (clang's hidden default) is refused by name.
Option values consumed by another option (such as `-include -fvisibility=hidden`)
are filenames, not code-generation requests.

Valid requests remain accepted for preprocessing, syntax checking, `check`,
and typed/static dumps, which emit neither an object nor an executable.
Explicit source visibility attributes and pragmas are unaffected. The real-driver
regressions in `c_check_pic_driver_test.c`, included by `test-checker-driver`,
cover these boundaries, both option orders, the effective visibility spelling,
`-fvisibility-ms-compat`, scalar/default paths, both tiers, output preservation
and the original two-unit tentative-definition example.

## Unsupported hardening requests

The semantic C driver refuses the option families `-ftrivial-auto-var-init`,
`-ftrapv`, `-fzero-call-used-regs`, `-fsplit-stack` and `-mcmodel` by name
(#1125). It does not yet implement the requested automatic initialization,
overflow traps, return-register clearing, split-stack prologues or alternative
code models. Family-prefix matching also refuses bare flags, empty or unknown
values and auxiliary hardening options; unknown values never silently select a
weaker default.

Only the complete spellings `-ftrivial-auto-var-init=uninitialized`,
`-fzero-call-used-regs=skip` and `-mcmodel=small` are admitted as the existing
inactive/default behavior.

`-fsanitize-coverage=` isn't one of these families: its features resolve as
clang's do, after every option, a later `-fno-sanitize-coverage=` among them,
and object emission and linking refuse what is left by name, as sanitizers
below (#1138). Non-emitting commands keep its predefines. `-fsanitize-coverage=`
and `-fsanitize-coverage=0` ask for nothing, a value clang doesn't know is
clang's error, and the bare `-fsanitize-coverage`, which clang doesn't take, is
refused by name. The auxiliary `-fsanitize-coverage-*` options (such as
`-fsanitize-coverage-allowlist=` and the cc1-only `-fsanitize-coverage-type=`)
are refused by the semantic option admission boundary below. The resolved
`-fsanitize-coverage=` and `-fno-sanitize-coverage=` grammars are unchanged;
admitting either never admits a `-fsanitize-coverage-*` prefix. Negative flags
are admitted only by an explicit owner or compatibility entry. Within these families, any unsupported request is an error
even when a later option requests an inactive/default form; this is not
Clang's last-option-wins policy. Unlike `-fstack-protector*` and
`-fcf-protection=*`, which are resolved separately and do honor a later
disabling spelling, a later inactive spelling cannot cancel an earlier request.
The diagnostic retains the first unsupported spelling from this refusal table
(`check_request_codegen_option` in `c_check_request.c`, which every client
that reads the driver's options shares).

Refusal happens before reading a translation unit or opening an output, for
both tiers, the scalar and vector paths, `-c`, direct linking and the explicit
`check` command. Syntax-only checks and typed/static dumps use the same refusal
policy. Pure preprocessing (`-E`) remains a separate action and is not a promise
of code-generation support. Option values such as `-include -ftrapv` remain
filenames, not hardening requests.

`c_check_hardening_driver_test.c`, included in `test-checker-driver --executable
./build/compiler`, checks all six reported options, missing and existing output
files, a missing source, option-looking include and output names, both tiers
and paths, non-emitting actions, malformed/auxiliary forms and exact inactive
defaults. The default-deny boundary below closes the remaining generic
`-f`/`-m` acceptance path. The object and linkage options of #1124 are above.


## Code-generation option admission (#1125)

The semantic driver and every shared-request client now refuse an unclassified
`-f` or `-m` argument, including unknown negative forms, with
`error: unsupported code generation option '…'`. The first such argument is
retained. This check runs before source IO or output creation, including
`-fsyntax-only`, `check` and dumps, on both tiers and scalar/vector paths.
Consumed option values and arguments after `--` are not scanned as options.
Pure preprocessing keeps its separate contract: `-E` can model flags without
promising to implement their code generation.

`c_check_options.c` owns admission, not option parsing. Its `handled` and
`values` tables name existing semantic/frontend consumers and resolved refusal
paths; the frontend's `pp_families` inventory and the target's exact plain
feature names are reused. The existing parsers still validate values and
resolve order. Sanitizers, coverage, OpenMP, PIC, visibility, section options,
stack protection and layout retain their existing resolved policies. In
particular, enabling then disabling coverage still compiles, while requesting
an auxiliary coverage allowlist, ignorelist or cc1 coverage type is refused.
The unconditional hardening families above remain conservative.

Being known to `predefines_option` is not sufficient. `predefines_neutral`
means only “does not change macros”; it is not a backend capability table.
Consequently `-mretpoline`, `-mspeculative-load-hardening`, `-mlvi-hardening`,
`-mharden-sls=all`, `-mfunction-return=thunk-extern`, `-mstackrealign` and
`-mms-bitfields` now fail rather than silently weakening code or changing its
advertised ABI. Active LTO and OpenACC requests are also refused. Other
unimplemented options fail by default, without adding another blacklist entry.

The `compatible` table is an exact-spelling list, not a family-prefix escape:

- Inactive/default controls: `-ftrivial-auto-var-init=uninitialized`,
  `-fzero-call-used-regs=skip`, `-mcmodel=small`, `-fno-split-stack`,
  `-fno-trapv`, `-fno-stack-check`,
  `-fno-instrument-functions`, `-fno-profile-arcs`, `-fno-test-coverage`,
  `-fno-profile-generate`, `-fno-profile-instr-generate`, `-fno-lto`,
  `-fno-blocks`, `-fplt`, `-ffp-contract=off`, `-mfpmath=sse`, `-m64`,
  `-mred-zone`, `-mno-ms-bitfields`, `-mno-rtd` and `-mno-iamcu`.
- Existing integer/aliasing behavior and permissions that do not require an
  optimization: `-fwrapv`, `-fno-wrapv`, `-fstrict-overflow`,
  `-fno-strict-overflow`, `-fstrict-aliasing`, `-fno-strict-aliasing` and
  `-fno-semantic-interposition`. These flags do not promise that an
  optimization will be performed. `-mtune=` is a validated scheduling
  preference, not a promise of target-specific scheduling. The frame pointer's
  `-f[no-]omit-frame-pointer` and `-m[no-]omit-leaf-frame-pointer` are
  `handled` (#177): both tiers' frames take them ([the driver
  guide](driver.md)), and so are `-f[no-]stack-clash-protection`, whose probes
  both tiers' frames and allocations take.
- Presentation and C-inapplicable controls: `-frtti`, `-fno-rtti`,
  `-fdwarf2-cfi-asm`, `-fno-dwarf2-cfi-asm`, `-fdiagnostics-color`,
  `-fno-diagnostics-color`, its exact `=always`, `=never`, `=auto` forms,
  `-fcolor-diagnostics` and `-fno-color-diagnostics`. Diagnostics remain
  uncolored; the driver emits objects rather than alternative assembly CFI
  spellings. C `-fexceptions` is handled separately: it requests implemented
  unwind tables, not merely a no-op.

Unknown extensions to these names or values are not accepted. The one open
name suffix is `-fno-builtin-NAME`: the frontend records the nonempty function
name, not a code-generation suboption. Adding another admission requires an
owner or a documented no-op and a regression; do not grant whole `-fno-`,
`-mno-`, `-fdiagnostics-*` or `-fsanitize-*` namespaces.

`c_check_options_driver_test.c` extends `test-checker-driver` with independent
unknown, negative, auxiliary, alias and macro-neutral machine fixtures,
output preservation, first-error ordering, option-looking include values,
compatibility byte comparisons and the separate `-E` behavior.
`test-checker --requests` checks shared-client/driver refusal parity and a
zeroed configuration on failure. The existing instrumentation tests retain
coverage cancellation and non-emitting predefines. This is the `-f`/`-m`
semantic boundary, not a claim to audit every `-g`, `-O`, `-W` or linker option.


## Bit-precise format arguments (#1392)

`_BitInt(N)` is a distinct integer type even when its width equals an ordinary
`char`, `short` or `int`. Variadic argument promotions do not turn it into
those types. The printf integer-class and dynamic width/precision checks
therefore reject bit-precise arguments for ordinary conversions, including
`%n`'s pointed-to class, string pointees and C23 `wN` modifiers. Explicit casts and arithmetic
that produce the required ordinary type remain valid. A fixed parameter of
an attributed nonvariadic function has already converted its argument: for a
bit-precise source or target (including a pointer/array of bit-precise elements),
format checking and its type spelling use that recorded assignment conversion
rather than the source type.

The behavior was checked in the pinned Clang 23.1.4 source, revision
[7a941dfb](https://github.com/llvm/llvm-project/blob/7a941dfb2877a0b8cf30323fefa85ad019e0b0f5/clang/lib/AST/FormatString.cpp#L445-L650),
`ArgType::matchesType`, and `CheckPrintfHandler::HandleAmount` in
[SemaChecking.cpp](https://github.com/llvm/llvm-project/blob/7a941dfb2877a0b8cf30323fefa85ad019e0b0f5/clang/lib/Sema/SemaChecking.cpp).
No upstream implementation or fixture is imported.

`c_check_format_bitint_driver_test.c` adds 25 original fixtures to
`test-checker-driver` through the shared default-warning harness. Independent
warning counts and complete Clang diagnostics cover narrow and equal-width
arguments, one-bit unsigned values, character conversions, string/count pointers,
dynamic width/precision, C23 exact-width formats, explicit casts, usual
arithmetic conversions, fixed-parameter conversions and ordinary integer and
boolean controls. Syntax checks and object emission run under the default
policy, `-Werror` and `-w`, through both kernels and requested lane counts;
promoted errors must preserve existing output. This is the bit-precise
subscope of #1392. Boolean-valued `int` expressions and typedef-sugar spelling
remain separate work; this change does not advertise complete parity.
