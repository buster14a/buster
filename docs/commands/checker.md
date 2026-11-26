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
nearest block/body/compound closing brace or for-header closing parenthesis and
leaves record braces out. A for initializer completes at the whole statement's
subtree end; its retained header anchor bounds error suppression. A private
suffix of unused warnings is stably ordered by scope anchor when a for
initializer can share a completion with an inner scope (#1784). Neither
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
  after a missing statement terminator or a malformed record member, the
  diagnostic reparse also retains completed expression operands, array bounds
  and bit-field widths for lookup in their original scope (#1249). A misplaced
  member bracket keeps its source location and is diagnosed after its bound.
  The original parse failure still prevents object emission;
  its expression graph's walk has a vector path, the coded walk
  (`c_check_expression_input.c`, #1038), which acts at the compacted tokens by
  each one's class, numbers their nodes into a batch whose columns it writes 16
  nodes at a time (`check_input_expression_flush`), with the walk (`--scalar`)
  its twin (`docs/c-checker.md`'s "The coded walk"); `c_check_sparse.c` holds the output columns few tokens have
  an entry in, as 16-token blocks behind a directory (`docs/compile-time.md`'s
  "The sparse output columns", #628), and `attributes` as a wide one, of 64-bit
  values ("The attributes column", #1167); `c_check_binding.c` publishes each use's retained answer from those events
  directly; `benchmark-checker-bindings
  [--pipeline] [--only MODE [--after-scalar MS]] [-E's options] file.c` times the whole reader
  including initialization, or fresh parsing through expression checking and cleanup with `--pipeline`
  with each handoff ([the binding handoff](../../docs/c-check-bindings.md), #336). `compiler check [--scalar] [--dump-typed] [--dump-static] file.c`
  and the default C command (`-fsyntax-only`, `-c`) use it; `compiler parse`
  keeps the parser's driver alone. Unsupported syntax is diagnosed.
  `test-checker-driver --executable ./build/compiler [--part K/N]
  [--lazy-system-headers | --cpu-dispatch-only | --pic-only | --stack-protector-only | --cf-protection-only | --visibility-only |
  --target-versions-only | --type-sugar-only |
  --asm-definitions-only | --tier1-asm-only | --call-counts-only | --fp-contract-only | --imaginary-bit-int-only | --operand-diagnostics-only | --label-attributes-only |
  --attribute-recovery-only | --initializer-diagnostics-only | --malformed-calls-only | --string-plus-only | --assert-bool-only | --assert-diagnostics-only | --query-bool-only | --constant-logical-only | --quiet-roots-only | --unused-comparison-only |
  --float-precision-only | --reach-filter-only | --branch-truth-only |
  --record-extensions-only | --auto-qualifiers-only | --invalid-records-only | --format-matches-only |
  --run-modes-only | --common-warnings-only | --missing-variable-only] [--in-process] [--clang-server PATH]
  [--differential] [--carets | --carets-all]` and `test-checker-asm`
  check the driver and inline assembly against Clang (named UCNs too, #1447,
  `check_test_driver_named_ucns`; a generated unit of about 180,000 tokens as
  a process of its own under an `RLIMIT_AS` of 768 MiB past the executable's
  image, about 1 GiB for Release, which it must compile, and of 32 MiB past
  it, where it must fail with a fatal error that names the limit and no error
  at a line of the unit, #1118, `check_test_driver_address_limit`; with
  `--lazy-system-headers`, only the checks that pass the driver
  `-flazy-system-headers`, including the real system-header lazy-policy
  fixtures and eager/lazy object comparisons, since the others run the same either way, and
  `--part K/N` runs the K-th of N parts its checks are dealt to, each unit
  to the part that weighs the least so far (`CheckDriverPart`, #3399): a unit
  weighs what it takes in hundredths of a second of one CPU on a 4-CPU Xeon
  VM, the usual `BUSTER_CHECK_DRIVER_UNIT_WEIGHT` or, for one that takes 2 s
  or more, its own (`check_test_driver_takes_weighing`), and a sweep that
  would take more than about 20 s deals each of its cases, since a long unit
  late in the order makes its part the longest whatever the weights;
  #2071's run modes, for every run of buster and of the
  oracle clang that goes through the shared helpers
  (`check_test_driver_spawn_stream`, `check_test_driver_run_with`):
  `--in-process` runs buster's source commands as this process's
  `main_source_command_reusable` (main.c's routing by the commands that
  release what they map), with the captured stream in a `memfd`;
  `--clang-server PATH` sends the oracle clang's runs (`--clang`'s name) to
  `tools/clang-server` (`oracle_cache.c`'s client, `OracleServer`; built by
  `tools/clang-server/build.sh` against that clang's libraries with one copy
  of LLVM: a shared-LLVM toolchain's `libclang-cpp`, or the official release's
  static clang and LLVM archives, which the hosted rows install; the build
  checks one request against a spawned clang), which runs each command line through clang's own
  driver and cc1 in its process, in the run's directory and environment, and
  a run the server doesn't answer (it ended, or gave no answer within its
  limit, 60 s) runs as a child (#2263);
  every other run is a child as before: a response file's, a jobserver dry
  run's, and one whose process-start inputs (`BUSTER_HUGE_PAGES`,
  `BUSTER_TEST_HOST_BUSY`, `BUSTER_TEST_MEDIUM_PAGES`, the lanes' test hooks, the transparent huge pages
  setting) differ from the test's own at its start. `--differential` also
  runs each such command as a child and fails on a difference in a complete server reply's status or
  output. A request with no complete reply uses the child's result without a differential comparison (#2421).
  `--carets` (#2376) measures the caret display over the suite: each of buster's `-fsyntax-only` command lines that
  passes `-fno-caret-diagnostics`, and none of buster's own `--` options, `-o` or a `-M` one, runs again without it,
  by buster and by the oracle clang, as children, and their whole errors are compared. A `checker-driver: carets:`
  line counts those the same as clang's, apart only in highlighted ranges' `~`, apart in other source lines (fix-its,
  scratch space, a range's lines) and apart in diagnostics' own lines; the first 64 that differ print both texts
  (`--carets-all`: every one).
  It fails nothing ([the caret display](preprocessor.md#caret-display-2376)).
  `--run-modes-only` checks unavailable, truncated and timed-out replies with a deterministic protocol peer,
  successful and rejected real Clang runs, invalid protocol statuses (#2740), and complete replies that genuinely differ.
  `--time-stages` and `--lane-cpus` runs, whose numeric report
  lines and lane order differ between any two runs, compare their other lines
  in any order. The run reports the counts and the warning sweep's time, and
  a `run costs` line: the served runs' time, the requests past the server's
  limit, and the children's wall and CPU time, each in total and per run. CI
  runs the driver's parts in these modes, with `--differential` in the native
  plain row; `--cpu-dispatch-only` runs the `cpu_specific` and
  `cpu_dispatch` section alone, #1447; `--pic-only` runs `-fPIC`'s, `-fpic`'s
  and `-shared`'s section alone (#2101, `c_check_pic_driver_test.c`: its shared
  libraries against clang's, run by the same programs); `--stack-protector-only`
  runs the stack protector's section alone (#3558, `c_check_stack_protector_driver_test.c`:
  each option's guarded functions against clang `-O0`'s, and a smashed buffer of
  each frame's shape aborting); `--cf-protection-only` runs CET's section alone
  (#3570, `c_check_cf_protection_driver_test.c`: each spelling's `endbr64` and
  `notrack` marks per function and its note against clang `-O0`'s, a program
  through the marks, and the refusals); `--visibility-only` runs visibility's
  section alone (`c_check_visibility_driver_test.c`: the pragmas' symbols and
  diagnostics against clang's, and #3576's `-fvisibility=` objects' symbols and
  `-fPIC` accesses at both tiers and a hidden library's exports); `--target-versions-only` runs GNU multiversioning's `target` and
  `target_clones` section alone, #1838 (inline assembly in `target_clones` copies at both tiers among it, #2247); `--asm-definitions-only` runs what
  inline assembly defines against clang's objects alone, #1848; `--tier1-asm-only` runs the
  Tier 1 asm body suite and `tests/driver/asm-body-results.c` against Clang. It checks
  multiple register outputs at both tiers, repeated inlining, aliasing, flags, scalar
  floating-point values, spills, both schedulers, and emitted code certificates and
  frame accesses (#2144). `--operand-diagnostics-only` runs
  the original runtime sources in `tests/driver/operand_diagnostics/` against Clang's complete
  diagnostics for invalid record and atomic operators, statement conditions, assertions and
  case labels and enumerators (#2441). Enum controls also check implicit successors,
  fixed underlying types, overflow and redefinition ordering. It checks gnu17 and C23,
  error promotion/suppression, scalar/vector
  readers and one/three requested lanes, deferred bodies, existing output preservation,
  typedef and qualifier spelling, and valid scalar/record/atomic-lvalue controls. The
  atomic conditional controls also compile, link and execute selections of integer,
  floating, pointer and aligned values through Clang and all three Buster routes.
  `--invalid-records-only` runs `c_check_invalid_record_driver_test.c` (#1447):
  typed member and bit-field errors, invalid-record propagation, valid sibling
  diagnostics, independent initializer errors, and preserved output after a
  rejected compilation. `--format-matches-only` runs
  `c_check_format_matches_driver_test.c` (#1447): reference contracts,
  declaration visibility, body checking, malformed strings, source locations
  and warning policy. Both use original runtime fixtures and compare complete
  ordered Clang diagnostics in GNU17/C23 through one and three requested lanes
  and deferred bodies.
  `--float-precision-only` runs `c_check_float_precision_driver_test.c` (#1875):
  `-Wimplicit-float-conversion`'s and `-Wliteral-range`'s floating comparisons
  against clang's complete output, with their controls and policies
  ([the c-checker section](../c-checker.md#floating-precision-and-floating-comparisons-1875)).
  `--unused-comparison-only` runs `c_check_unused_comparison_driver_test.c`
  and the original `tests/driver/unused_comparison*.c` sources (#2906).
  It compares complete Clang diagnostics for discarded comparisons, eligible
  lvalue notes, successful assertions, macro operands and warning policy in
  GNU17/C23 through both readers and one/three requested lanes. Ordinary
  fixtures also check object output and its preservation after rejection;
  the vector-swizzle category fixture checks syntax because its unrelated
  object lowering remains unsupported. A failed-assertion fixture checks
  rejection, retained assertion errors and absence of invalid notes without
  pinning missing warnings: full discarded-comma recovery belongs to #2409.
  `--assert-bool-only` runs `c_check_assert_bool_driver_test.c` alone, including
  the original `tests/driver/assert_bool_*.c` sources for root and nested
  boolean-conversion and discarded-comma warnings in failed static assertions
  (#2409). It compares
  complete diagnostics and exit status under warning promotion/suppression,
  error limits, source policy, C17/C23, both reader choices and lane requests,
  and verifies existing object-output preservation for rejected input.
  `--assert-diagnostics-only` runs that suite together with
  `c_check_assert_const_driver_test.c`: failed-assertion requirements, selected
  literals, attributed type spellings, and the rejected member declaration
  `__extension__ _Static_assert` (#2409). The canonical-spelling and recovery
  fixtures compare complete diagnostics and exit status with Clang through the
  shared warning-policy, reader and requested-lane matrix. A separate safety
  fixture checks assertion errors, diagnostic counts and preserved output for
  complex attributed forms that deliberately retain their source spelling.
  `--quiet-roots-only` runs `-Wunsequenced`'s fixtures and the quiet roots' checks (#1340,
  `c_check_quiet_roots_driver_test.c`, [the design](../c-checker.md#the-quiet-roots-1340)): the common warnings' loop
  leaves out the roots its classes call quiet, and `BUSTER_TEST_DEFAULT_PATH_LOG` visits every root all the same. A
  unit of each kind of root, with frozen counts of its roots and its quiet ones and clang's diagnostics, and 48
  generated units compile both ways to the same diagnostics, in the same order, and the same object, with no quiet
  root's visit that acted; the unit and every other generated unit also behind a filler table of 8,400 tokens, past
  which the reader leaves the statements' discard wrappers out. The
  warning sweeps hold every fixture's compile with the switch to clang and to its own object the same way, and
  `test-checker-driver` prints the sweeps' roots and quiet roots.
  `--constant-logical-only` runs the original `constant_logical_*.c` sources for
  `-Wconstant-logical-operand` (#2900). The standalone group is on by default.
  Integer logical operators warn for independently constant right operands other
  than zero or one; boolean/atomic/noninteger left operands, bare enumerators on
  either side and operators originating in macros stay quiet. Parentheses around
  an enumerator remove that bare-reference exemption. `&&` has the bitwise and
  remove-constant notes; `||` has only the bitwise note.
  Both the constant/type-query reader and the checked graph retain sparse events,
  deduplicated by operator token, before pruning or failed-assertion recovery.
  Their construction boundaries retain diagnostic order in unevaluated operands,
  unselected arms, failed assertions and malformed-unit recovery. Runtime const
  expressions use the existing checked actions with a private, lazy dependency
  cache; the original graph and stream remain unchanged. Reached statement-prefix
  effects and undefined arithmetic prevent a warning proof, even when GNU
  required-constant folding supplies a payload. A completed const-object read
  starts a new evaluation; C23 additionally withholds a static object's receipt
  if any live initializer required undefined folding. The source reader retains
  integer bit-cast and complex-component proofs in erased query operands too.
  A completed top-level brace clears stale inner-statement repair cuts, so later
  completed assertions and initializers stay in the checked prefix.
  The suite compares full
  Clang diagnostics and status, source/command-line policy, GNU17/C23, both readers,
  lane requests and syntax/object modes, including rejected-output preservation.
  The C23 invalid-static-receipt row is syntax-only because pinned Clang's backend
  rejects that initializer; its GNU17 row retains object coverage. Existing
  arithmetic diagnostics on newly folded const reads are isolated in that row;
  direct invalid arithmetic and its diagnostic order have a separate full-output
  fixture.
  `--branch-truth-only` runs the check of the bit before a branch condition's
  fold (#1340, `check_cfg_decidable` in `c_check_program_cfg.c`), which the
  default run has as one of its sweeps: a node whose bit is clear is one the
  fold leaves undecided and at which clang's CFG builder's rules fire no event,
  by the driver's `--branch-truth-census` over a unit with a condition for each
  rule the bit is derived from and each shape it rejects and over
  `tests/checker/cfg-pruning`'s units (their counts frozen), over every unit of
  `tests/driver/warnings` that compiles alone, and over generated units of
  random conditions (`c_check_branch_truth_driver_test.c`).
  `--query-bool-only` runs the original `tests/driver/query_bool_*.c` sources for
  boolean conversions in erased query/type expressions and successful selections
  (#2899). Complete Clang diagnostics cover integer and floating conditions,
  nested construction order, GNU omitted-middle and explicit-cast controls,
  logical unevaluated suppression, source pragmas and macro expansion notes.
  Both GNU17 and C23 run the shared warning-policy, reader, lane and output
  preservation matrix; linked execution controls ensure erased operands and
  unselected associations retain their lack of side effects.
  The two original failed-type-query sources also expose an existing assertion
  requirement-spelling gap. Their five exact primary error texts are checked
  against frozen Buster spellings; all locations, warnings, notes, ordering,
  counts and other text remain complete Clang comparisons. This does not claim
  canonical assertion requirement spelling or normalize arbitrary diagnostic text.
  Repaired declaration views and missing-label recovery retain the warning facts.
  A stray `case` outside a switch still triggers the existing whole-body rejection
  before expression checking; recovery of that body's later queries is separate.
  `--record-extensions-only` runs the existing GNU empty-record checks plus the runtime
  `tests/driver/record-extensions/` catalogue for zero-size-record C++ compatibility and
  zero-length-array warnings (#2409). Complete GNU17/C23 diagnostics cover explicit and
  inferred bounds, nested dimensions, typedef and compound-literal reuse, folded/VLA
  controls, direct and nested flexible members, macros, source pragmas and system headers.
  The shared harness checks suppression/promotion, scalar/default readers, one/three
  requested lanes, syntax/object modes, rejected-output preservation and accepted object twins.
  `--initializer-diagnostics-only` runs the runtime `tests/checker/initializer-comma/`
  and `tests/driver/warnings/pointer-initializers/` catalogues in GNU17 and C23 (#2441, #3563).
  The pointer catalogue covers qualifier loss in braced scalar, array and member initializers,
  array arguments, typedef spelling, GNU ranges, compound literals, pointer sign and nested
  qualifiers, with explicit casts, qualification additions and null pointers as quiet controls.
  Both compare complete Clang diagnostics and exit status. The comma catalogue covers
  multiple incompatible elements, independent nested excess
  warnings, invalid compound-literal dependencies and atomic conditional deduction.
  The cases retain comma-warning order, matching conditional-arm typedefs,
  later independent errors and accepted sibling values. Command-line and scoped
  warning policies, both readers, one/three requested lanes, syntax/object modes,
  rejected-output preservation and accepted object twins use the shared warning
  harness; `--in-process --differential` also compares the child route. Compound
  dependency checks operate on the complete retained expression graph; they do
  not extend the reader's recovery of malformed GNU syntax or failed deduction.
  `--semantic-recovery-only` runs
  `c_check_semantic_recovery_driver_test.c`, `c_check_statement_recovery_driver_test.c`,
  `c_check_attribute_call_recovery_driver_test.c`, `c_check_malformed_call_driver_test.c`,
  `c_check_type_attribute_recovery_driver_test.c`, `c_check_attribute_declaration_driver_test.c`,
  `c_check_structural_recovery_driver_test.c`, `c_check_repaired_view_driver_test.c`,
  the weak-alias diagnostics from `c_check_symbol_pragma_driver_test.c` and the initializer-comma
  catalogue from `c_check_unused_result_driver_test.c`, the whole output and status
  of semantic error recovery's fixtures against clang's on the vector, scalar
  and two-lane deferred paths, and `-c`'s refusal of each: `docs/c-checker.md`'s
  "Semantic error recovery", #1447. The weak-alias catalogues also compare GNU17/GNU23,
  warning policies, error limits and one/three requested deferred lanes, with separate
  syntax and object oracles for emission-only undefined-target errors (#2449).
  They retain macro columns and notes while normalizing scratch-space line numbers,
  preserve rejected outputs, and require ELF output for accepted controls. The full
  weak-alias section additionally checks common-target refusal and defined alias
  symbols under `-fcommon`/`-fno-common`, plus linked programs on both readers and
  requested deferred lanes. Its GNU17 initializer catalogue includes the
  former compound-literal boundary as a complete Clang comparison; the focused
  initializer option above also runs that catalogue in C23. Its runtime `suppress_label_order.c`
  cases compare parser errors before label-subject errors under both language
  modes, warning policies and a one-error limit, with macros, split lists,
  valid controls, later errors and old-output preservation. The adjacent
  `.cases` file records case number, expected exit status and whether the first
  error must be the string-literal error. `--label-attributes-only` runs these
  cases plus the existing label and unknown-attribute suites, including the
  label execution controls. Attributed declarations without a type (#2332)
  compare complete GNU99/GNU17/C23 diagnostics on both readers and deferred checking
  with one, two and three lanes. C23 invalid declarations retain their initializer
  checks and the earlier binding in the same scope. The runtime
  `attribute_declarations.cases` records case number, expected status and check
  bits (1: Clang comparison, 2: parser boundary). Typed controls include GNU89/C23 and
  require an ELF object that links and runs. Invalid inputs preserve existing
  output or create none. Prefix/reparse contract checks also retain the complete
  meaning metadata and a separate mutable table for each reparse (#2748).
  `--attribute-recovery-only` runs the balanced string-list, mixed-label,
  missing-type-declaration and attribute-led-call fixtures together. The runtime
  `suppress_recovery.cases` adds mixed GNU/C23 lists, macro delimiter locations
  and unmatched attribute delimiters. Each row names a source, its decimal case
  count, and hexadecimal masks for accepted and executable cases. These cases
  compare complete ordered diagnostics and status in GNU17/C23, both readers,
  syntax/object modes, one/three deferred lanes, warning policies and a one-error
  limit; rejected output is preserved and accepted statement effects execute.
  The string
  lists include malformed string subscripts through parentheses and macros,
  repeated and split lists, valid adjacent-string controls and an assumption
  with a subscript. A malformed C23 suppress separator discards the remaining
  items in that list, while earlier items and later separate lists retain their
  diagnostics and effects. Inner-parenthesis and non-string-argument recovery
  controls keep the following items. Their errors retain later diagnostics and existing object
  output, and accepted controls must produce an ELF object. Its `attribute-recovery-integration.c`
  combines their recovery paths in two functions, preserving the complete
  diagnostic order and later independent errors. The declaration portion runs
  in C99–C23, the call portion before C23, and the label portion in every
  tested standard. This focused section supports sanitizer and in-process/child
  differential checks without running the unrelated recovery fixtures.
  `--malformed-calls-only` runs the ordinary and builtin type-first call recovery
  cases in `tests/checker/parse-recovery/malformed-calls.*` (#2406), comparing
  complete GNU89/GNU17/C23 diagnostics and status, both readers, syntax/object
  output, warning policies, `-fno-builtin` and error limits. Mixed ordinary and
  attribute-led macro calls cover multiple rounds and repaired/rejected views;
  metadata contracts keep each callee source paired with its use flag, sorted and
  unique even when a later error revisits an outer call after a nested one. The diagnostic
  view discards the unread type-first argument and retains earlier and later
  arguments, including their lookup and type errors. The invalid callee
  prevents artificial arity, argument-conversion and noncallable diagnostics while
  keeping independent argument checks and warnings. Type-first repairs after valid
  arguments and nested calls retain their original parse errors through successive
  rounds, without duplicating earlier arguments' diagnostics. Block expression
  calls also recover at their first outer semicolon. The reader checks the other
  arguments up to that boundary, including complete operands before a second syntax
  error (#3372). It then checks subsequent clauses as statements. The C23 keyword-typo
  cases check warnings before, inside and after a corrected loop, plus independent
  errors in the same body and at the corrected keyword (#3369). Builtin controls
  keep lookup/header diagnostics and independent arguments without imposing
  intrinsic constraints on the synthetic call. Boundary controls exclude empty
  later arguments in normally closed calls, mismatched delimiters, member callees and semicolon crossings
  in nested calls, initializers or returns. Rejected output is preserved and valid
  cast/`sizeof` controls must produce an ELF object.
  `--string-plus-only` runs both string-addition warning suites, including the
  runtime sources in `tests/checker/string-plus-char/`: character and integer
  warnings with their notes, their independent option controls and ordering,
  character-pointer and literal variants, rejected units and both readers.
  `--type-sugar-only` runs
  `c_check_type_sugar_driver_test.c` alone, the typedef sugar diagnostics keep
  through typedef chains, members and qualified arrays, and const assignment's
  notes, `docs/c-checker.md`'s "Type sugar" and "Const assignment", #1447;
  `--call-counts-only` runs `c_check_call_count_driver_test.c` alone, calls'
  and builtins' argument count errors and calls without a prototype, worded
  and placed as clang's, #1860, #1921, #1994; `--fp-contract-only` runs
  `c_check_fp_contract_driver_test.c` against Clang 23.1.4 at `-O0`, using
  `tests/driver/fp_contract.c` for float/double result bits, expression
  boundaries, option ordering, scoped pragmas and dynamic rounding. It covers
  both readers, object/direct output and strict Tier 1 without fallback,
  with transition verification enabled). It also runs #2806's
  `tests/ir/vectorize/float-convert.c` and `float-convert-main.c`: packed
  FMA and all signed/unsigned same-width integer/float conversions, at
  x86-64, x86-64-v3 and x86-64-v4, against Clang's scalar loops with the
  same contraction policy. The full driver suite includes that check with
  its loop-vectorization cases. `--imaginary-bit-int-only` runs
  `c_check_imaginary_bit_int_driver_test.c` (#2219):
  `tests/driver/imaginary-bit-int/`'s diagnostics sources (constants in every
  spelling and constant context, refused suffixes, the complex `_BitInt` types
  in errors and failed static assertions' printing) in the standards
  `cases.txt` names, by the shared default-warning harness, and its program
  (arithmetic, comparisons, parts, casts, static and automatic initializers,
  calls, returns and `va_arg` of complex `_BitInt`s of 3 to 256 bits and
  `_Complex __int128`), whose output at Tier 0 and strict Tier 1, by both
  readers, as an object and directly, must be Clang -O0's build's. The Tier 1
  asm-body suite initializes
  and checks its object decoder before inspecting instructions (#2737,
  `check_test_driver_tier1_asm`). An initialization failure is reported directly
  and skips decoding, so partitioned runs do not depend on another suite's setup.
  Its library-builtin comparisons are pinned to Clang
  23.1.4's official build, CI's reference: it compares `clang --version` with
  it, prints `expected clang 23.1.4, found …` when another build is found, and
  skips only the plain names that build's vendor is known to classify
  differently (the named list `cbuiltin_library_table_differences`: Debian's
  and Ubuntu's packages drop `strlcat` and `strlcpy`), still comparing every
  other name and every version-independent check, and reruns them under a fake
  Ubuntu clang 23.1.6 so the
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
  `test-checker [--seed N] [--clang PATH] [--fixtures DIR] [--part K/N] [--program-initializers|--program-vectors|--program-library|--embed-initializers|--requests|--attested|--transitions]` compares fresh scalar/vector tables,
  protected tails and original source fixtures against Clang, and checks the constraint catalogue
  (`c_check_constraints_test.c`): for every Constraints paragraph of C17's 6.4 to 6.10 a violation of each rule and
  a valid twin, and the conversions, constant expressions and initializers, each checked as the corpus check checks a
  file and against clang (`docs/c-checker.md`, "The constraint catalogue").
  `--part K/N` runs the K-th of N parts that the default run's sections and `check_test_programs`' programs are dealt
  to in turn (`CheckTestPart`); part 1 also runs the sections that share the run's type table and graph arenas.
  `--program-selection` selects the emission-selection, constant-if and constant-switch regressions alone.
  Set `BUSTER_TEST_PROFILE_DIR` to an existing writable absolute directory when profiling the native checker
  oracles. `test-checker` leaves each executed `run` ELF at its original path under that directory, so a profiler
  can open it after the child exits. Set the variable in the profiler's launch environment when running the tests
  through `compiler parallel`; the checker shards inherit it. Use a fresh directory per capture and remove it
  after the profiler has processed the capture. Failed fixtures keep their usual diagnostic files.
  The older `BUSTER_CHECKER_PROFILE_DIR` name also works.
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
  a whole object's copy, compound literals in place, a volatile object's stores) and native runs on a dirtied stack;
  #2745's: no store of zeros before a vector's value of the object's size (a struct's, a union's by either member, a
  nested struct's, a compound literal's, the vector object's own, at 16, 32 and 64 bytes), the clears that stay (a
  volatile object or member, a wider object or union member, a union whose designator moves on, a long double's), and
  their native runs.
  The same group covers #1549's hidden-result call initializers: copy, temporary and discarded-address elimination,
  nested and indirect calls, only members and elements, alignment, volatile and self-reference fallbacks, assignments,
  reentry, and which labels keep the copy, at both scan paths' lengths; and #1375's named results
  (`c_check_program_named_result_test.c`): no copy or frame bytes on either side of the inline copy's limit, unions,
  compound literals, escaping addresses, statement expressions and cleanups, every excluded shape's copy, and a native
  run of each against clang -O0's code on a dirtied stack; and #2684's forwarded results (`c_check_program_forward_test.c`):
  a `return f(...);` and a named result's call initializer that pass the hidden pointer on, with no copy, memmove or
  object on either side of the inline copy's limit, through conversions, selections and pointers, every excluded shape's
  copy (kept returns, returns-twice bodies, self-references, conditionals, commas), and a native run of each against
  clang -O0's code into storage the run before poisoned. `tests/interpreter/source.c` also exercises direct, nested and
  indirect aggregate call initializers and forwarded returns through the typed and Tier 1 source checks.
  `--program-vectors` selects the vector, x86 builtin and half-precision programs alone (`c_check_program_vector_test.c`,
  `c_check_program_x86_test.c`, `c_check_program_vector_builtins_test.c`, `c_check_program_fp16_test.c`,
  `c_check_program_half_test.c`), run against clang -O0's code where the host has their features.
  Byte funnel shifts of 16-byte vectors require XOP: these tests compile/link everywhere and execute only where
  XOP is present; `test-x86-intrinsics` also checks their emitted expressions against Clang, including `_mm_rot_epi8`.
  Required constant evaluation folds `_Float16` and `__bf16` arithmetic in the source format after each operation
  (#1497); runtime half-precision chains retain their target's excess-precision behavior. The constant-source and
  floating-action fixtures cover that distinction, ties, subnormals and negative zero.
  `float`, `double`, `long double` and `__float128` folds give APFloat's NaNs, not the host's (#1473, #1250): an invalid operation
  (`inf - inf`, `0 * inf`, `0 / 0`) folds to the positive quiet NaN, and an operation on a NaN keeps its first NaN
  operand's sign and payload, quieted. The floating-action fixtures, the constant-source fixture "real invalid
  constant arithmetic has Clang NaN signs" and `--program-float-constants`' "float-constants-invalid-arithmetic"
  check them against Clang.
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
  `--requests` selects #1057's request tests (`c_check_request_test.c`), with #1444's option IDs and option forms
  (`c_option_table_test.c`, the driver guide's "Option matching"), and the source benchmarks' preparation tests.
  `c_check_self_assign_driver_test.c` checks #1391's macro self-assignment boundary. The warning is suppressed
  when the assignment operator or either variable-name use comes from a macro body or substituted argument;
  macro-produced parentheses or declarations alone do not suppress an ordinary source assignment. Original
  fixtures compare independent counts, severity and complete diagnostic lines with Clang through default,
  scalar and deferred reader routes, including warning flags and source diagnostic pragmas.
  `-Wsign-compare` skips comparisons that are themselves integer constant expressions (#3300). A comparison
  containing an evaluated assignment or comma operator still gets the usual signedness checks.
  `-Wsign-compare` retains both operands' source signs and reads the checked comparison conversion, including
  narrow integers, bit-fields and `_BitInt` promotions (#1391). `c_check_sign_range.c` computes diagnostic ranges
  as a width and a nonnegative flag: constants, implicit conversions, assignment/comma results, ordinary
  conditional arms, arithmetic, masks, shifts, division and bit-fields. These follow Clang's approximate warning
  rules; they are not optimizer facts. Explicit casts retain their target type's range unless the whole cast is
  evaluable, and GNU shared-middle conditionals remain opaque unless evaluable. A modifying condition's folded
  payload cannot select its source arm: both possible arms contribute to the range. Assignment conversions are
  applied before their result's range is used, so narrowing cannot inherit an invalid nonnegative proof.
  Equality is quiet when the unsigned operand's range is narrower than the comparison type; `UINT_MAX` fills
  that type and keeps the warning. The unsigned-zero relational exception requires a source integer constant
  expression. The nonrecursive postorder walk borrows only unused emission frames and preserves every live
  warning frame. Exhausted space or a repeated-DAG work budget returns the operand's opaque type range.
  `c_check_sign_compare_driver_test.c` compares independent counts, severity and complete Clang diagnostic lines
  through default/scalar compilation and a deferred route that requires two admitted reader lanes. Run
  `test-checker-driver --sign-compare-only --executable build/compiler` under a two-CPU grant; the shared correctness
  inventory supplies it. `tests/driver/sign_compare_conditional.c` retains both operand orientations, every
  comparison, nested/selected arms, casts, side-effecting conditions, GNU controls, a balanced 128-leaf tree
  and live sibling diagnostics. The range fixtures extend those controls across assignment/comma wrappers,
  narrowing, arithmetic and equality boundaries without changing the shared constant-evaluation facts.
  `-Wuninitialized`'s graph-order candidates receive a definite-write proof in `c_check_uninitialized_flow.c`
  (#1391). Up to 64 candidate objects share expression read/write masks over their owning body's graph slice.
  Commas and conditions sequence writes; short-circuit and conditional alternatives certify only writes that
  every completion performs. The retained controls supply execution order, including body-to-increment and
  `continue` edges. A monotone worklist joins possibly uninitialized objects by union across predecessors and
  resets objects outside their lexical scope, so a previous block lifetime cannot initialize a later one.
  A reverse expression pass identifies an actual unsafe read, including a post-loop read reached after zero
  iterations. The existing diagnostic publisher retains its warning family, spelling, notes and policy; the
  analysis does not add a general `-Wsometimes-uninitialized` diagnostic engine. Missing control coverage,
  detached statement-expression regions or cleanup chains keep the original warning conservatively. A native
  window summary (#2850) has no operand nodes; its rows are its reads and writes in evaluation order.
  `c_check_uninitialized_driver_test.c` reads the original `tests/driver/uninitialized-for/` fixtures. Quiet and
  definitely uninitialized cases compare complete Clang lines; unsafe joins independently pin Buster's existing
  diagnostic to a marked read. Normal driver parts cover the default, scalar and `--node-window-assignments`
  readers. Run
  `test-checker-driver --uninitialized-only --executable build/compiler` under a two-CPU grant for the additional
  admitted deferred-reader checks; it shares the existing two-CPU diagnostic inventory group with sign comparisons.
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
  `c_check_interrupt_driver_test.c` (#147) compares x86's `interrupt` attribute's handler errors (with those clang
  places nowhere), its direct calls and `-Wexcessive-regsave` with pinned Clang through the vector, scalar and
  two-lane deferred paths, and checks that `-c` of a definition is refused by name (`docs/c-checker.md`).
  The executable interrupt-handler control in `c_check_convention_driver_test.c`
  retains the first failed compile, link or run stage, its status and captured
  diagnostics, with the actual and pinned Clang versions (#2226). A version
  mismatch never turns a failed control into a pass or skip. The original
  `tests/driver/interrupt_probe_control.c` fixture checks all three failure
  stages, empty and wrong output, successful output, and that no later stage
  runs after a failure.
  Bitwise/comparison `-Wparentheses` diagnostics distinguish a single bare comparison from eager logical
  comparison pairs and unparenthesized bitwise chains (#1390). Parenthesized operands retain their explicit
  grouping. Original default-warning fixtures compare complete diagnostics and independent counts against
  pinned Clang through scalar/vector and one/three-lane readers, including warning promotion and suppression.
  An enclosing construct's warning comes after the diagnostics its operands gave as clang built them (#1941): an
  assignment condition's `-Wparentheses` is published at the condition's `)` (a `for`'s at its `;`), as clang's
  CheckBooleanCondition runs once the condition is whole; a bitwise operator's precedence warning and a logical not's
  at their right operand's end (`check_common_operand_end`); and a call's `-Wformat` from its `)` on, so a statement
  expression's own warnings and a nested call's come first. A call argument's `-Wformat` is at its callee, the start of
  the argument (`check_common_argument_start` now reads the node's own `)`). Each costs a warning's climb, nothing on
  a unit without one.
  Explicit void casts on a conditional arm's value path suppress the conditional's unused-result marker (#1390).
  The existing postorder conditional/comma and selected-value facts carry that discard intent through nested
  arms and transparent GNU/generic wrappers, while inner comma and division diagnostics still run.
  `c_check_void_conditional_driver_test.c` covers those boundaries,
  scalar-result controls, for clauses, macros and source policy through default and scalar modes and a requested
  deferred-option route. These single-file syntax-only commands are capped at one lane and use the ordinary-reader
  fallback; they do not establish admitted deferred-reader coverage.
  A GNU statement expression in the expression operand of `sizeof`, `_Alignof`, `__alignof__` or `typeof` is
  unevaluated (#147). Its statements are graph roots of their own, outside the operand's evaluation facts.
  A discarded statement expression whose value is a comparison warns as clang's DiagnoseUnusedExprResult of the
  statement expression does: "expression result unused" at the comparison, `-Wunused-value`'s with no note,
  queued after the statements before it (`check_common_statement_value`, #1447); a statement's own comparison keeps
  `-Wunused-comparison`.
  Clang drops its unused-result, unused-comparison, comma-operand, `void *`-cast, self-assignment, division,
  bounds, sign-comparison and constant-conversion warnings there. `check_common_unevaluated` reads the token's
  parser context from `-Wvoid-ptr-dereference`'s `check_warning_graph_void_context`, only when one of these
  warnings is about to be published. An array declarator's bound is constant-evaluated to clang, which keeps
  its unused-result warnings. So does a bound in the operand's type name or in a declaration inside the
  statement expression. A `sizeof` of a variable-array expression evaluates its operand after all.
  The contexts' memo is pushed at a unit's first such warning, so a unit without one pays nothing. Six
  original default-warning fixtures compare complete output with Clang, among them the evaluated and bound
  controls and a value that decays from a variable array. For `-Wuninitialized`, a statement
  expression whose value is not evaluated has none of its nodes evaluated either (`check_common_region_evaluation`,
  on the analysis's own copy of the facts): clang's CFG holds none of them, so the analysis reads none of their reads
  and writes, while the warnings that read a context keep a bound's there. `sizeof ({ ... })`'s `(` is its statement
  expression's own, as clang's parser reads a parenthesized expression there, on both parser paths. A folded shift's count
  warning there, and every runtime warning in an array bound (clang's ConstantEvaluated context, a variable array's
  too: shift counts, divisions, sign comparisons, constant conversions, array indexes, null dereferences), is
  dropped as clang's DiagRuntimeBehavior drops it (`check_common_bound`, the parser contexts' BOUND bit; a comma's
  unused value stays). Still separate: a variable-array
  `sizeof`'s comma operands, which clang warns of when it evaluates them; and two forms the checker refuses: a statement expression in a variable-array
  `sizeof`'s operand and a statement expression as `_Generic`'s controlling expression, which the constant reader
  reads outside a deferred typeof region. A statement expression in a static selection's chosen operand
  (`_Generic(1, int: ({ u; }))`) is evaluated where the selection is: `check_common_unselected_roots` leaves the
  chosen operand's tokens, between its top-level commas (`check_common_chosen_span`), out of the selection's range, so
  its statements, roots of their own, are read by `-Wuninitialized` and an initializer's `-Wconstant-conversion`
  there is given; in another selection's other operand they stay unselected (#2023).
  `-Wunevaluated-expression` (#1447, clang's CheckUnaryExprOrTypeTraitOperand and CreateGenericSelectionExpr,
  `check_warning_graph_operand`) is on by default, under `-Wunused-value`: the expression operand of a sizeof or an
  alignof of no variable array type, and a `_Generic`'s controlling expression, that has a side effect as
  Expr::HasSideEffects finds one without the possible ones (an assignment, an increment, `__builtin_va_arg`, an atomic
  builtin clang makes an AtomicExpr of; not a call, a volatile read or a declaration), warns at the operand's location,
  a sizeof's own `(` where it has one, once the operation is read. The search climbs the parse tree from each side
  effect in the operand and stops at an expression whose location a macro wrote (an argument's too), a nested sizeof,
  a type, a specifier's operand, a static selection's other operands, and, in a statement expression, an
  unevaluated-argument builtin's call that is a statement's own expression. The scan is part of the warning graph's
  16-node pass, and an operand's tokens are classified 64 at a time. The sizeofs and alignofs the constant reader alone
  reads (array bounds, enumerators' values, bit-field widths, case labels, `_Alignas`), which have no node, are found by
  their keywords 64 tokens at a time (`check_warning_graph_constant_queries`): an expression operand's side effect there
  is an assignment's operator (but a designator's `=`) or an increment's by its role or kind, and a type name's operand
  warns of none. Left: a bound of a pointer to a variable array in a statement expression's declaration, which clang
  leaves out and the scan reads.
  `c_check_unevaluated_driver_test.c` compares it with complete pinned-Clang output at each policy, both paths and one
  or three lanes.
  Member lookup on an atomic struct or union still finds the underlying record's member and keeps its declared type
  (#2444), including arrays and bit-fields; qualifiers on the atomic wrapper do not qualify the member.
  `-Watomic-access` defaults to an error at `.` or `->`, can be demoted or suppressed independently, and joins the
  existing runtime-warning walk: none in unevaluated operands or array bounds, reachable accesses at the function's
  end, and file-scope accesses after the initializer's own diagnostics. Structural errors retain the usual queued
  warnings during recovery, and an atomic error at one body's end flushes subsequent bodies' queues. Under `-w`, a
  valid body skips that queue; erroneous bodies and file-scope initializers retain default-error accesses.
  A monotonic type-table flag adds member candidates to that walk only in units with atomic
  records; ordinary units gain no new graph scan or formatting work. The default-warning driver fixtures compare
  complete Clang output and source/command-line policies through scalar/vector and one/three-lane readers.
  `-Wunsequenced` (#1953, clang's SequenceChecker, `c_check_unsequenced.c`) is on by default, under
  `-Wsequence-point`: two modifications of a variable, or a modification and a read of it, that no sequence point
  orders warn once per variable and full expression, "multiple unsequenced modifications to 'x'" or "unsequenced
  modification and access to 'x'", at the earlier modification's operator (a read's later modification's), after the
  full expression's conversions, as clang's DiagRuntimeBehavior queues them (a function's end, a file-scope
  declarator's). The walk keeps clang's region tree (a comma's, a logical operator's and a conditional's operands
  sequenced; an assignment's, an increment's, a call's arguments' and every other operator's not, C11's), each
  variable's last read and modifications with their regions, sequenced operands' side effects completing when they
  end, and its evaluation of a first operand as clang's constant evaluator finds it, ignoring side effects (`z && 0`
  but not `0 * z`; an assignment's value never); a warning either of whose accesses clang's CFG leaves unreachable is
  dropped (the evaluation facts). Only variables are objects, as clang's getObject in C; unevaluated operands, a
  type's variable array bounds (a cast's or a sizeof's type name's, `CHECK_OP_VLA_BOUND`, #2321), a
  selection's other operands and statement expressions' statements aren't read, a statement expression's value is
  a full expression of its own, checked first, and a brace initializer's list is one, checked once its last value
  is walked (`check_unseq_plans`, a range's value once per element). Candidates come from one vector pass over the
  graph's operators for a modification; a root whose postorder node range names no modified variable twice (counting
  its modifications; `check_unseq_repeats`, 16 nodes at a time, a superset of the walk's usages) can't report and
  isn't walked, as most aren't; the walk is scalar, an ordered tree walk with a union-find. A random
  differential of 21,000 full expressions over three variables, without constant first operands, matches clang
  23.1.6 exactly. A condition whose value a modification gives (`(y = 0)`, `({ y = 0; })`, `!(y = 0)`) is no constant
  to the evaluation facts, as clang's CFG builder's EvaluateAsBooleanCondition fails on it (#2264): the statements'
  edges (`check_program_branch_truth`), the operators' evaluated operands (`check_common_operand_mask`), the stream's
  and the constant reader's unselected arms and a conditional's converted arms all ask `check_graph_evaluable`, which
  walks the `BUSTER_CHECK_MODIFICATION` value path past a comma's left operand, a statement expression's earlier
  statements, a decided logical operator's other operand and a constant conditional's other arm, which only discard
  one. `tests/checker/division-by-zero`'s `assigned-*` and `discarded-modifications` rows compare those with clang.
  Differences left: a brace initializer's constant conversions come after its list's warnings, where
  clang gives them first; and a function holding a statement expression keeps its unreachable code's warnings, as
  every runtime warning does (#1447).
  `c_check_unsequenced_driver_test.c` compares it with complete pinned-Clang output at each policy, both paths and one
  or three lanes.
  A C-style cast from a pointer (an array, a string literal or a function that decays to one) to an integer type
  narrower than it, but `_Bool`, is clang's `-Wpointer-to-int-cast` (#1943, CheckCStyleCast,
  `check_warning_graph_pointer_cast`), on by default: "cast to smaller integer type T from P" at the cast's `(`, as
  soon as the cast is read, in unevaluated operands too; to an enumeration it is `-Wpointer-to-enum-cast`, under it,
  and from a pointer to void `-Wvoid-pointer-to-int-cast` and `-Wvoid-pointer-to-enum-cast`, the last under both of
  the others (`c_check_warning_policy.c`). The cast's type keeps its typedef; the operand's keeps a named object's.
  Differences left: casts only the constant reader reads (an array bound's, an enumerator's or a bit-field width's,
  where clang warns too) give none, as the warning graph has no node for them; and an operand whose type clang keeps
  sugar for through an expression (a conditional's `VP`, a function's `U32 (*)(VP)`) prints its canonical type.
  `c_check_pointer_cast_driver_test.c` compares it with complete pinned-Clang output at each policy, both paths and
  one or three lanes.
  `c_check_unused_result_driver_test.c` (#1447) compares a discarded call's result warnings (`-Wunused-result`'s
  warn_unused_result and nodiscard, a return type's, pure's and const's, the builtin table's) with Clang's whole
  output at each warning policy, both paths and one or three lanes: their spellings and messages, macros, types,
  redeclarations, comma, conditional and for clauses (a parenthesized clause's too, #1447), builtins and `-Wno-unused`'s groups; nested commas,
  conditionals and selections (clang's `isUnusedResultAWarning`, `check_common_unused_of`), calls through pointers,
  unreachable code, keyword builtins' values and logical operators among them.
  Runtime `-Wshift-count-negative` and `-Wshift-count-overflow` warnings inspect an independently constant count
  even when the left operand is not constant (#1394). The ordinary shift uses the promoted left type's value width;
  compound shifts use the original lvalue's underlying integer type, including atomic operands, matching Clang.
  Compound `_Bool` uses its storage width (eight bits on this target); ordinary Boolean shifts use the promoted
  integer width, while `_BitInt` retains its value width in both forms.
  Ordinary unselected conditional arms, short-circuited logical operands and unreachable statements suppress these
  warnings. Unselected `_Generic` associations and `__builtin_choose_expr` arms instead retain Clang's queued count
  diagnostics: those detached expressions have no CFG block, even under an unreachable statement or with an ordinary
  conditional or logical operand nested inside them. Lexical `sizeof`, `typeof` and `_Generic` controlling-expression
  contexts suppress count warnings before queueing, including inside or around those detached arms (#1722).
  Required-constant contexts such as `_Static_assert` and case values suppress their detached-arm count warnings too.
  File-scope C23 `constexpr` initializers also suppress those warnings; function-local `constexpr` initializers retain them.
  The count traversal preserves source order across selected and detached arms and neighboring expressions or
  statements, and visits an omitted-middle conditional's shared condition once. Overlapping statement-expression
  roots share count-publication ownership; detached scan visits and ordinary tree ownership have separate bits,
  so count deduplication cannot mark unrelated warning families as emitted. Other warning families retain
  their existing evaluated-expression rules, including the older unused-value gap for multiple discarded statements
  in an unselected statement expression. Retained runtime constant-fold warnings join the count traversal without being
  rendered or counted twice, preserving their order with new runtime warnings and discarded expressions;
  required-constant warnings and groups collected before a failed program keep their original publication.
  A source comma's "left operand of comma operator has no effect" is queued where clang's `CheckCommaOperands` builds
  it (#1447): the arithmetic scan's post-order visit runs its marker among the divisions and shift counts, ahead of the
  full expression's conversions, and the tree walk skips it. A comma whose operands hold an index's bound
  (`BUSTER_CHECK_REACH_BOUND`) keeps its marker in the tree walk, which orders the bound as clang checks it; bounds still
  follow every division and count of their root. Roots the reader adds after what follows their operand (a
  statement's condition, a switch's, or a `for` increment's discard) are taken where their
  operand stands (`check_common_displaced_roots`), so a condition's warnings precede its body's, as clang analyzes it.
  In a file-scope initializer clang's `DiagRuntimeBehavior` queues the comma's warning to the declarator's end, as it
  does a folded shift count's (`check_driver_file_initializer_end`, #1943): both are published there, ordered by their
  nodes' ends (the records' suborder), so that each comes in the order clang builds it (`int z = (1 << 40, 3);`'s
  count first) and the declarator's constant conversions after them. A comma's static initializer compiles at `-c` as
  clang's does in GNU modes (#2264; clang 23's C23 mode refuses it, so that fixture stays syntax-only). A brace initializer's values walk their roots in initializer order, after
  the plan's literal conversions and before its constant ones (`check_common_initializer_conversions`), as clang
  queues their runtime warnings while it parses them. A comma whose discarded left operand folds undefined behavior
  (`(1 << 44, 3)`, `((void)(2147483647 + 1), 3)`) makes clang's array-bound fold fail as a division's overflow does
  (#2264): `check_action_comma_provenance` passes `BUSTER_CHECK_GNU_DIVISION` on in a constant evaluation, so such a
  bound is variable (refused at file scope, in a member and in a block's static object), while an enumerator, a case
  label, a bit-field width, a static assertion and a static initializer still fold it. A const object's read
  (`GNU_OBJECT_READ`) hides whether its fold also overflowed, so `(c + (1 << 44), 2)` still folds. A variable bound at
  file scope drops the bound's shift and overflow warnings, as clang's constant-evaluated bound does, and its error is
  published after the declarator's last `]` (its report's `sort`), after every bound's comma warning. A file-scope
  `sizeof` of a runtime-sized type name is refused once per initializer element and recovers (#2252): its query reads as
  an `int`'s size, so the initializer's later commas and the unit's later declarations keep their diagnostics. A compound
  literal's bound folds wherever it is (`check_input_vla_compound`), in a body too, its warning at the literal's `(`.
  A brace initializer's or a compound literal's element gets `-Warray-bounds` only from an operator above its subscript
  (`-a[5]`, `f(a[5])`, a comma's right operand): clang's CheckArrayAccess of the full expression sees the initializer
  list and doesn't look into it (`check_common_brace_element`, #2252).
  A statement expression's statements are roots of their own, ahead of its enclosing expression's in node order: the
  arithmetic scan takes each one's whole visit (`check_common_inner_roots`, a `ROOT` frame) where it enters the statement
  expression, so they warn between what the enclosing expression built before it and after it, as clang builds them, a
  displaced condition's first. Its last value, its statement's and the statement expression's, is scanned once. Its
  statements are evaluated as it is (`check_common_statement_roots`): a short-circuited operand's, a constant
  conditional's other arm's and unreachable code's give none of their runtime or unused-value warnings, as clang's CFG
  leaves their blocks unreachable. A body with statement expressions keeps its unreachable-code proof
  (`check_common_unreachable`): the walk starts at each statement expression's first entry too, taken as reached, so
  code after a `return` inside one is dropped as well. Still different: a comparison as its last value warns as a
  comparison where clang says "expression result unused".
  A folded shift's warnings take clang's words (#1447, `check_driver_shift_message`): "shifting a negative signed
  value is undefined", and "signed shift result (0x…) requires N bits to represent, but 'T' only has W bits", the
  result's hexadecimal and its significant bits. Its `-Wshift-negative-value` is a runtime one like its counts: the
  driver stashes both (`CheckCommonShiftWarnings`) rather than reporting them, the common walk reports each at its
  shift and drops one it proves unevaluated (after a return, a `goto` or an endless loop, in a constant condition's
  other branch), so that a dropped `-Werror` one neither counts nor rejects the unit, and whatever no walk reached is
  reported at the end (a unit with errors runs none). `-Wshift-overflow` is no runtime warning, and stays. Under
  `-fwrapv` (or `-fno-strict-overflow` without `-fno-wrapv`) neither warns, as clang's DiagnoseBadShiftValues defines
  signed overflow there (`isSignedOverflowDefined`); the counts' warnings stay. Two
  unevaluated places still warn, as clang's do: an unselected `_Generic` or `__builtin_choose_expr` arm, which has no CFG
  block, whose warnings keep the selection's order, and a reachable statement's condition that folds to a constant,
  whose range the program drops. That statement is reached by its keyword and by the first kept root of its executed arm
  (never a static selection's unselected one) in the evaluation with no range dropped; with an arm of no expression, by
  its place in its function's control program (after its block's last entry before it, by a fall-through or a jump from
  a label before it). Its warnings are reported where its root stands in the roots' order.
  `c_check_int_in_bool_driver_test.c` (#1447) compares clang's `-Wint-in-bool-context` and the
  `-Wtautological-constant-compare` forms of a `<<` or a conditional converted to `_Bool` (`docs/c-checker.md`)
  with complete pinned-Clang output at each policy, both paths and with `-c`.
  A constant a bit-field truncates is clang's `-Wbitfield-constant-conversion` (#1447, AnalyzeBitFieldAssignment,
  `check_common_bitfield_truncates`), and a 1 in a signed one-bit bit-field its
  `-Wsingle-bit-bitfield-constant-conversion`, both under `-Wconstant-conversion`, which `-Wconversion` and `-Wnon-gcc`
  hold. The constant keeps its own width, or a `-` or a `~` operand's significant bits, and warns where the
  bit-field's width and signedness change it. An assignment's warns at its `=` with the full expression's other implicit
  conversions, in unreachable code too, and leaves out the value's constant conversion. An initializer's warns at each
  value's first token in member order, before that initializer's conversions, which it keeps, even in an unevaluated
  operand. `c_check_bitfield_driver_test.c` compares both with complete pinned-Clang output at each policy, both paths
  and with `-c`.
  An operator's usual arithmetic conversions of enumerations of different types, or of an enumeration and a floating
  type, are clang's checkEnumArithmeticConversions (#1447, `check_common_enum_arithmetic`), warned at the operator (a
  conditional's `?`) as it is built, with an enumerator taken as its enumeration: a comparison's `-Wenum-compare`, on by
  default; an arithmetic or bitwise operation's `-Wenum-enum-conversion`, a conditional's `-Wenum-compare-conditional`
  and a floating mix's `-Wenum-float-conversion` (a compound assignment's only from an enumeration to a floating target),
  the three under `-Wenum-conversion` and so `-Wconversion` and `-Wnon-gcc`; and an unnamed enumeration's
  `-Wanon-enum-enum-conversion`. Only a unit where such an operator may mix enumerations is scanned: one where an
  enumerator is named as its operand (`CheckInput.enum_operands`, set where the identifiers' leaves are made, outside
  its own enumeration's definition), or where an arithmetic, comparison or conditional action had an operand of an
  enumeration type in any form, a member, a call's result, a cast's, a subscript's or a dereference's as well as a
  variable (`CHECK_WARNING_ENUM_OPERAND`, which `check_graph_store` moves to `CheckStream.enum_operands` and no warnings
  column keeps); an operator is tested only where an operand may be an enumeration. The constant reader's operators,
  which no graph node holds (an array bound in a declarator or a type name, a member's bound or bit-field width, an
  enumerator's value, a `typeof` operand), leave a `CHECK_WARNING_EVENT_ENUM_MIX` where their operands, as clang
  coerces them, are enumerations of different types or an enumeration and a floating type; the scan reports those that
  no graph operator holds through the same rules (`check_common_enum_mix`), so a `_Static_assert`'s or an
  initializer's is warned once. A comparison's operands are rvalues, as clang's DefaultLvalueConversion makes them: unqualified, a typedef
  that carries a qualifier itself spelled as its type; a GNU `a ?: b`'s omitted operand is its condition promoted, never
  an enumeration.
  `check_common_enum_coerced` first finds the nearest enclosing definition by its ordered opening token. Nested
  enum definitions inside enumerator values can close before a later outer enumerator, so the fallback scans all
  definitions. It takes the maximum one-based matching index (zero for nonmatching SIMD lanes), then loads that type
  once, avoiding Clang 23 MSan checking inactive poison TypeId lanes before `llvm.experimental.vector.extract.last.active`
  (#1829). `c_check_enum_arithmetic_driver_test.c` compares complete pinned-Clang output at each policy, both paths
  and with `-c`, including 9/17-definition vector-chunk cases and nested-definition fallback coverage.
  `-Wfor-loop-analysis` (#1447, clang's CheckForLoopConditionalStatement and CheckForRedundantIteration,
  `check_common_for_loops`) is off by default, under `-Wloop-analysis` and `-Wmost` and so `-Wall`; a unit where nothing
  enables it skips the scan. The condition is its truth test's operand. Its variables, where it is simple (operators,
  casts, literals, local non-volatile variables, a static selection's chosen operand, `__builtin_offsetof` and the
  unevaluated-argument builtins, `check_common_loop_extract`), warn at their first use when neither the condition, the
  increment nor the body may change them (an assignment's target, an increment's operand, an address, a member's object
  or an array's decay, outside sizeof, alignof and a static selection's unchosen operands), in a loop with no return,
  break or goto. A GNU statement expression's statements are graph roots of their own, so a change or a jump inside one
  counts only where the value of each statement expression around it in the loop is evaluated too, none is in a
  specifier's operand (typeof's) or a sizeof's, and each static selection around it chooses the operand that holds it,
  by the parse tree, as the selections' unchosen roots can't tell a statement expression's statements in a chosen arm
  from those in another (#1821, `check_common_loop_reached`); a sizeof's type name's bounds are unvisited as well. Left:
  a bound of a pointer to a variable array in a declaration (`int (*p)[i++];`), which clang's visitor skips and the
  checker counts. A variable's `++` or `--` alone as both the increment and the body block's last statement, with no
  continue of the loop, warns at the body's with a note at the header's. Each check is skipped where the warning is
  ignored at the condition's or the increment's first token, as clang's are. Both are published past the statement's
  last token, after its body's own warnings.
  `c_check_for_loop_driver_test.c` compares them with complete pinned-Clang output at each policy, both paths and with
  `-c`.
  An implicit conversion from one named enumeration to another, an enumerator taken as its enumeration, is clang's
  `-Wimplicit-enum-enum-cast` (#1447, CheckImplicitConversion's last case, `check_common_enum_cast`), on by default under
  `-Wenum-conversion`: an initializer's, an assignment's, a prototype's argument's, a return's and a brace initializer's
  value's, carried to both arms of a conditional (no runtime pruning), through a selection to its selected arm and to a
  root comma's result, in clang's order (CheckConditionalOperand, #1895: the condition's conversions, then each arm's own
  before the arm's, the true arm's first; `-Wconstant-conversion`'s in arms too, an argument's included,
  through the walk's `BUSTER_CHECK_EMIT_ARM` frames), warned at each leaf's expression location (an operator's, a member's name, else its first token
  inside its parentheses) as the full expression completes, in unreachable code too. A leaf a macro's argument writes
  shows that argument's "expanded from macro" note unless the conversion's context (the assignment's operator, the
  declarator, the return, the call, a brace initializer's declaration) is written in the same argument occurrence
  (clang's rangesInsideSameMacroArgExpansion; `check_common_argument_occurrence`, the innermost one). Both enumerations need clang's name
  for linkage: a tag, or the unqualified typedef that declares an anonymous one. Clang's range tests come first
  (`check_common_enum_cast_reached`): a leaf whose likely range (a constant's significant or active bits, a bit-field's
  width, else its type's) is wider than the target's integer type gives no enumeration warning, a constant's
  -Wconstant-conversion instead; nor does a non-negative constant that fills a signed target exactly from a signed type.
  Its candidates are the conversion walk's (`check_common_enum_cast_candidate`: an enumeration's target, from a source
  that may be one), and a brace initializer's plan is read for it where a value part's target is an enumeration
  (`CheckInitializer.enum_values`). They are tested only in a unit that names a named enumeration's enumerator or
  variable or defines a named enumeration (`CheckInput.enum_names`, set where the identifiers' leaves are made and by
  `check_input_enum_records`), so that a source enumeration that is only a member's, a call's result or a cast's is
  looked for too. A selection whose selected arm is an enumerator (`_Generic(c, int: B)`) converts that enumerator's
  enumeration, as clang's fold of it keeps its type (`check_common_enum_cast_source`).
  `c_check_enum_cast_driver_test.c` compares it with complete pinned-Clang output at each policy, both paths and with
  `-c`, with a generated matrix of the C23 underlying types' pairs.
  `c_check_shift_count_driver_test.c` compares complete pinned-Clang diagnostics and independent counts for both
  shift directions, narrow promotions, 128-bit and bit-precise widths, compound assignments, side effects, macros
  and warning policy. The shared default-warning harness covers syntax checking and object publication through
  both kernels and requested one/three-lane options, including `-Werror`, output preservation and `-w`.
  The ordinary vector-shift exclusion control is syntax-only because baseline x86-64 cannot lower its vector shift.
  `c_check_return_flow.c` follows each reachable path for `-Wreturn-type`. An ordinary call returns.
  A direct call can terminate the path through its visible declaration, including a selected overload or an
  inherited analyzer annotation. An indirect call uses the complete callee expression's function type.
  GNU `noreturn` survives typedefs, member selection, casts and pointer results. A conditional pointer
  keeps it when both alternatives carry it. C11 `_Noreturn` and C23 `[[noreturn]]` apply to the declaration.
  Library declarations acquire their builtin type contract according to linkage, compiler options and typedef
  visibility. The warning walk visits `__builtin_fpclassify` selectors and the operand of `__builtin_constant_p`.
  It skips object-size query operands and ends a path at `__builtin_assume(0)`.
  When only one arm of `?:`, `&&` or `||` executes a terminating call, the other can reach
  the end, and Clang's diagnostic includes “in all control paths”.
  The return-call driver tests compare diagnostics and warning policies with Clang through both readers.
  `c_check_data.c` and `c_check_data_input.c` turn selected static scalar definitions into real
  Link atoms/fixups; their tests compare emitted ELF bytes with Clang objects.
  `c_check_string_pool.c` shares exact decoded literal storage after selection, keeping
  symbols and addends intact and named/predefined arrays distinct (#1474).
  The default `test-checker` data tests cover unique atom/byte demand, transaction
  rollback, embedded-NUL storage, guarded invalid descriptors and scalar/vector
  hash/sort boundaries. The program-selection tests include a dead first occurrence
  followed by live duplicates. `test-checker-driver --executable build/compiler`
  runs `c_check_string_pool_driver_test.c` through both tiers and readers, using
  direct linking and object output linked by the system toolchain, with Clang and
  independently stated results as oracles.
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
Its real-header overflows (a local array, an element's offset, a static array, a
structure, `bzero`, `read`) must abort at `--tier1` too, as clang's build does where
glibc's version lets it, since the lowering expands the wrappers with their
callers' bounds ([the wrappers' bounds](../c-builtins-fortify.md#the-wrappers-bounds-2983), #2983).
So must the longer wrappers' (`fgets`, `fread`, `wcscpy`, `wcsncat`, `strlcpy`,
`recv`, `realpath`, `mbstowcs`), as GCC's build does; clang isn't run for them, whose
result depends on glibc's version. One program runs those wrappers within their
objects, against clang too.

## Generic association lookup

Both `_Generic` readers use the same association set. Up to 16 associations fit inline.
Larger sets index enum tags by identity and ordinary function prototypes by return
shape, arity, variadic flag, and the shapes of the first and last parameters.
The integer/enum lists also admit candidates of the other kind. Old-style
functions and functions with a directly passed union at a sampled position keep a
broader list. Each lookup checks candidates in source order with `type_compatible_at`
at the selection's source position, including after an enum completes or a union
acquires its transparent attribute.

`test-checker` counts duplicate and control queries on families of up to 257
associations, checks mixed lists against the exact predicate, and checks both
readers against Clang. Types with indistinguishable shapes can still require
pairwise comparisons. When the directory exceeds the available scratch space,
the readers scan packed keys in source order.

## Runtime fixtures

Complete driver programs, companion headers and expected output live under
`tests/driver/programs/SUITE/`. Each catalog starts with
`buster-fixtures v1 driver-programs` and the suite's input roles, separated
by tabs. Each later row supplies a stable ID and a filename for each role,
in header order. Rows may share files. Add a row to run the same compile,
link, native execution and output checks on another complete case. The
runner stages input under the original logical filenames. Hardware and
toolchain gates, temporary output paths and independent oracles remain in
the specialized C check.

The default-warning driver sweeps read `tests/driver/warnings/SUITE/catalog.tsv`.
Use `test-checker-driver --fixtures DIR` to select the root containing
`driver/` and `checker/`. Each v2 catalog starts with
`buster-fixtures v2 checker-warnings`. Rows have eight tab-separated fields.

```text
ID    FILE.c    WARNINGS    ERRORS    NOTES    FLAGS    OPTIONS    NAME
```

Use a stable ID and an adjacent source file. Several rows may name the same
file to exercise different policies. The runner stages its exact bytes as
`default-warning.c`. The three decimal counts describe the independent Clang
result before promotion or suppression. FLAGS is a comma-separated list of
`c23`, `syntax-only`, `warning-text` and `node-window`. OPTIONS holds up to
four space-separated compiler arguments in order. Use `-` for either empty
list. Lines beginning with `#` are comments. Add ordinary cases here in
execution order. The default, condition and shift-count sweeps deal rows to
the driver's parts. Generated matrices and diagnostic spelling comparisons
run in their original C harnesses.

`test-checker`'s shared source fixtures (`check_test_sources`) are
runtime data (#2053): `tests/checker/sources/` holds `catalog.tsv` and one file
a case, which the test reads when it runs, so that editing a case's source or
flags, or adding a case, compiles, links and bakes nothing. The catalog's first
line is `buster-fixtures v1 checker-sources`; each later line not starting with
`#` is a case, four tab-separated fields:

- **ID**, unique, of letters, digits, `.`, `_` and `-` (the extraction numbered
  them `0001` to `0418` in the table's order);
- **FILE**, in the same directory, unique, ending in `.c`: the source's bytes as
  they are (no NUL; the runner writes a source to its first NUL, as it did);
- **FLAGS**, comma-separated, or `-`: `rejected`, `unsupported`, `extension`,
  one of `gnu89`, `gnu99` and `gnu23` (gnu17 without), `unsigned-char`,
  `undecided`, `overload-tables`, `overload-none`, and `capacity=NAME` for a
  type table `check_test_fixture_capacity` names;
- **NAME**, what the case checks, which a failure prints.

The cases run in the catalog's order, then the 64 generated ones. A missing or
malformed catalog, a missing or unreadable file, a repeated ID or file, a file
in the directory that no line names, or an empty catalog fails the run; no case
is skipped. The fixture root is `tests/` under the working directory, or
`test-checker --fixtures DIR`. A new ordinary case goes there: its file and its
line. The cases' bytes are the table's literals as clang compiled them, compared
byte for byte when they moved (buster-performance's
`fixtures-runtime-2053-2026-11-13`).

Each case `check_test_source` accepts also checks the reader's graph and
enumerations (what its two paths' comparison reads: every node's columns and
leaf, the arguments, the overload calls, the tokens' nodes and the enumeration
records) against `tests/checker/source-digests.tsv`, from the repository's
root, keyed by the case's name and its source's hash (#2924). The digests were
recorded where the reader's vector path and its scalar twin agreed. A case
with no line passes: a new case, or an edited source (a new key), has no such
agreement to record, and clang's acceptance and first error and the checker's
typed streams check it. Each run prints how many cases had a line and how many
had none. A reader change that changes a recorded case's graph records again:
run every part of `test-checker` with `BUSTER_CHECK_RECORD_DIGESTS=FILE`, sort
FILE's lines unique, and put them after the table's three comment lines; the
PR says what vouches for the changed graphs.

The checker's typed streams are recorded the same way, in
`tests/checker/stream-digests.tsv` (#2924, C12c-3): each source fixture's
stream, keyed as above, and each comparison's reference stream (a generated
graph's, a builtin or x86 graph's, the transitions' and the scheduler's), keyed
by its site and the graph (`check_test_graph_key`: its nodes, columns, leaves,
extensions and suppressed warnings). They were recorded where the checker's
scalar schedule and node order agreed; `-` marks a case whose interned types
depend on the tests run before it (a part's against a whole run's). A case with
no line passes, as above. A checker change that changes a recorded stream
records again with `BUSTER_CHECK_RECORD_STREAM_DIGESTS=FILE`, every part and a
whole run, a key with two digests `-`.

Thirteen test files' own source tables, 967 cases, are runtime data in the same
format, a directory a suite under `tests/checker/`, which `check_test_suite`
(`c_checker_test.c`) reads and runs. Each case is written to a file, clang
`-fsyntax-only` checks them a run per dialect (`-std=gnu17` for the cases that
name none, all at once; `gnu89`, `gnu99` or `gnu23`, and `-funsigned-char`,
for those that do), and then each case's acceptance is compared with clang's
and `check_test_source` checks both paths, as the tables' own loops did. There
are no generated cases.

| Suite | Cases | Runner (its test file) |
| --- | ---: | --- |
| `align` | 67 | `check_test_align_sources` (`c_check_program_align_test.c`) |
| `attributes` | 59 | the end of `check_test_attribute_sources` (`c_check_attribute_test.c`) |
| `classify` | 20 | `check_test_classify_sources` (`c_check_program_classify_test.c`) |
| `cleanup` | 78 | `check_test_cleanup_sources` (`c_check_cleanup_test.c`) |
| `const-alias` | 53 | `check_test_const_alias_sources` (`c_check_const_alias_test.c`) |
| `float-constants` | 8 | `check_test_float_constant_sources` (`c_check_program_constants_test.c`) |
| `generic-builtins` | 271 | `check_test_generic_builtin_sources` (`c_check_program_generic_builtins_test.c`) |
| `library` | 21 | `check_test_library_sources` (`c_check_program_library_test.c`) |
| `musttail` | 63 | `check_test_musttail_sources` (`c_check_musttail_test.c`) |
| `overload` | 48 | `check_test_overload_sources` (`c_check_overload_test.c`) |
| `source-extensions` | 254 | the start of `check_test_source_extensions` (`c_check_source_test.c`) |
| `transparent` | 8 | the end of `check_test_transparent_calls` (`c_check_transparent_test.c`) |
| `transparent-types` | 17 | the end of `check_test_transparent_types` (`c_check_transparent_type_test.c`) |

The following private oracles read the same case format at their original
call sites. They retain their temporary filenames, diagnostic policies and
scalar/vector comparisons.

| Suite | Cases | Oracle detail |
| --- | ---: | --- |
| `constant-expressions` | 190 | Includes the full-width Clang/GCC probe and generated nested expressions |
| `enum-sources` | 41 | Each case runs as `enum.c` |
| `vla-sources` | 51 | Includes the generated repeated-size expression |
| `const-objects` | 63 | The sparse-array probe retains its independent smaller Clang input |
| `bit-builtins` | 30 | GNU17 Clang oracle per char signedness, with the recorded checker standard |
| `stdc-bit-builtins` | 10 | Groups by GNU17 or GNU23 |
| `typeof-compound` | 20 | Groups by GNU17 or GNU23 |
| `function-name-semantics` | 10 | Retains the quiet Clang syntax check |
| `integer-extension-sources` | 13 | Stages each source as `oracle.c` |
| `context-selections` | 8 | Stages each source as `selection-N.c` |
| `atomic-vector-constraints` | 2 | Individual Clang and checker runs |
| `function-name-objects` | 20 | `FILE.c.expected` supplies the plain name and pretty signature on two newline-terminated lines |

Add an ordinary regression by adding a catalog row and its files. In suites
with generators or independently modeled inputs, the FILE column can also name
a C-defined `@name`. Each binding must occur exactly once. Generated inputs
stay at the catalog positions the original oracle used. The deep function-name
case still follows the 20 ordinary cases. The integer fold operation/operand
model and its generated expected bits remain in the integer checker.

`tests/checker/overload-placeholders` (#2935) is a warning catalog of
`test-checker-driver`'s (`check_test_driver_warning_catalog`, as
`initializer-comma` is: `buster-fixtures v1 checker-warnings`, then FILE,
WARNINGS, ERRORS, NOTES and NAME a row), each unit's whole output and status
compared with clang's in every policy, mode, lane request and reader.

`tests/checker/constant-conversions` (#2678) is in the same format, three
programs that `check_test_constant_conversions`
(`c_check_constant_conversion_test.c`) runs natively against clang by
`check_test_program_source`, whose catalog names select the checks of their
entries and code ([the lowering guide](lowering.md#node-templates-1048)).
The ordinary native program groups in `tests/checker/programs/` use the
checker-source catalog format and the `test-checker --fixtures` root. A
catalog occupies one call site so its rows inherit the same feature gates,
shard, and position relative to generated and structural checks. Append a
program with its stable name and the flag `-` or `unsigned-char`. Each
program runs through the native harness against Clang. Existing names also
select the original per-program machine-code assertions.

`tests/checker/branch-conditions` (#2883) is too: three programs that
`check_test_branch_conditions` (`c_check_branch_condition_test.c`) runs the same
way ([the lowering guide](lowering.md#conditions-as-branches-2883)), the third
for a condition on an lvalue at an absolute address (#3028).
`tests/checker/asm-label-after-definition` (#2936) is a warning catalog of
`test-checker-driver`'s (`check_test_driver_warning_catalog`, as
`initializer-comma` is: `buster-fixtures v1 checker-warnings`, then FILE,
WARNINGS, ERRORS, NOTES and NAME a row), each unit's whole output and status
compared with clang's in every policy, mode, lane request and reader.

`tests/checker/designator-recovery` (#1842) is a warning catalog of
`test-checker-driver`'s, beside `initializer-comma` (its parts' unused-result
section and `--semantic-recovery-only`), in the same format: the
lists after a designator that names no member, each unit's whole output and
status compared with clang's in every policy, mode, lane request and reader
([the checker guide](../c-checker.md)'s "A designator that names no member
(#1842)").

`tests/checker/cfg-pruning` (#2990) is one too: conditions clang's CFG builder
decides by its heuristics (`x * 0`, `x | 1`, `(x & 8) == 4`, `x == 1 && x == 2`,
`x && !x`, `c_check_program_cfg.c`), whose pruned branches give no runtime
warning and no `-Wreturn-type` fall-off, with the shapes the heuristics leave to
the evaluator; and, under a pragma that enables `-Wtautological-compare`, the
builder's observer's `-Wtautological-bitwise-compare`,
`-Wtautological-overlap-compare` and `-Wtautological-negation-compare` in its
order (`c_check_common_cfg.c`).

`tests/checker/int-float-conversions` (#3339) is one too: `-Wimplicit-const-int-float-conversion`
at the default flags, and `-Wimplicit-int-float-conversion` and `-Wdouble-promotion` under pragmas, whole
([the c-checker section](../c-checker.md#integer-to-floating-conversions-and-floating-promotions-3339)).

`tests/checker/system-macros` (#3338) is one too: common warnings at a system
header's macro expanded in user code, which the expansion location decides as
clang's DiagnosticsEngine does (`check_common_severity`), and the checks whose
clang code tests `isInSystemMacro` itself: a conversion's context
(`check_common_conversion_context`), a discarded value's macro
(`check_common_unused_quiet`), a format call's extra argument and an array
index both a system header spells.

`tests/checker/return-calls` (#1393) checks fall-off after direct and indirect calls, source-visible
library and declaration contracts, and query builtins.

A table's comments are `#` lines of its catalog, above the cases they described.
The other test files' tables stay C for now: they build their sources when they
run, pair each case with data of their own, or pick cases by index (#2053 lists
them). Outside the checker, Tier 1's mask IR (`tests/tier1/masks`,
[the selection guide](../tier1-selection.md)) and the IR parser's canonical
fixtures (`tests/ir/catalog.tsv`, [the IR tools guide](ir-tools.md)) are runtime
data too.

## System-header token bitmap and MSan (#1610)

The source reader maps parser tokens' source indices to system-header bits in
`check_input_system_token_map`. It clamps indices at or beyond the preprocessor
output count to the user-file sentinel, gathers the low level byte in groups of
16 and takes the last tokens as one masked step (#3448). History/run selection, arena
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

The first post-merge native MSan run passed the bitmap's 228 controls, then
reported another stack overflow in `check_input_deprecated_marked` during the
separate main.c compilation. That existing whole-column vector leaf is also
kept `noinline`, preserving its body and `count <= INT32_MAX`/host-vector
admission in `check_input_deprecated_uses`. It still scans the full input;
isolation keeps its instrumentation separate from the large reader. The
scalar warning walk, marked-declaration chains, contexts, ordering, allocation
and scratch lifetime are unchanged. This follows the same pinned-compiler
source inference, with every sanitizer check retained and no stack/flag
adjustment. Actual native MSan main.c compilation and a clean final audit
remain the decisive crash qualification; further wide-check sites must be
judged by their own evidence.

`c_check_deprecated_marked_test.c` adds 456 native-vector cases through the
existing deprecated-walk section. It compares every word with independent
per-token scalar key routing across lengths 0–65, 16/64-token boundaries and
2,097,167 tokens, null/present objects, zero/valid/invalid object-first keys,
invalid declarations, identifier densities and zero/high-bit marks. Exact
input/table ends are protected; output prefixes, suffixes and unused final
bits must be preserved. Baseline hosts retain the kernel's vector admission
and report that this direct kernel control is skipped. Existing scalar/vector
source diagnostic fixtures continue unchanged. A small direct caller is
semantic/bounds coverage and need not reproduce the large-reader threshold.

## Object and linkage options (#1124)

The C driver refuses an effective `-ffunction-sections` or
`-fdata-sections` when writing an object (`-c`, including `check -c`)
or linking an executable. The diagnostic names the option:
`error: unsupported code generation option '…'`. This is an explicit
unsupported boundary, not implementation of these features. The refusal
precedes input processing and output creation, including multi-file commands,
and preserves an existing destination in both Tier 0 and Tier 1.

`-fvisibility=hidden|protected|internal` and `-fvisibility-ms-compat` are
implemented (#3576). As clang's do, they give the unit's definitions of
external linkage the visibility neither an attribute nor a
`#pragma GCC visibility` gives them (`predefines_default_visibility`,
`CheckProgram.visibility`, `check_program_visibility`): functions, objects,
tentative definitions and their `-fcommon` commons, thread-locals, weak
definitions, aliases and ifuncs, multiversioned functions' symbols and
`#pragma weak` aliases. Declarations, weak references and an inline-only
function the unit doesn't emit keep the default. `internal` lowers to hidden,
and `-fvisibility-ms-compat` is hidden in C. Under `-fPIC` such a definition is
no other module's to preempt: it is reached directly, and a thread-local by
local-dynamic accesses. `-shared` exports none of them.

`-fcommon` is implemented (#147). When it comes after any `-fno-common`,
every tentative definition of external linkage is an `SHN_COMMON` symbol,
and a multi-unit link merges the units' commons as GNU ld does.

Each family has independent last-option-wins state.
`-fno-function-sections`, `-fno-data-sections` and `-fvisibility=default`
retain the current output and cancel an earlier request in their own family.
For visibility, the last of `-fvisibility=…` and `-fvisibility-ms-compat`
decides, as clang's driver reads them: an earlier malformed value is ignored,
and a malformed last value keeps clang's `invalid value` diagnostic.
Option values consumed by another option (such as `-include -fvisibility=hidden`)
are filenames, not code-generation requests.

Valid requests remain accepted for preprocessing, syntax checking, `check`,
and typed/static dumps, which emit neither an object nor an executable.
Explicit source visibility attributes and pragmas are unaffected. The real-driver
regressions in `c_check_pic_driver_test.c`, included by `test-checker-driver`,
cover these boundaries, both option orders, the effective visibility spelling,
`-fvisibility-ms-compat`, scalar/default paths, both tiers, output preservation
and the original two-unit tentative-definition example; `--visibility-only`
compares the visibility options' objects and libraries with clang's. It also
checks every global and weak symbol of `target`, `target_clones` and
`cpu_specific`/`cpu_dispatch` groups inside nested hidden, protected and default
pragma scopes (#3578), at both tiers, with and without `-fPIC`, and under each
default visibility. The synthetic objects inherit their source declaration's
visibility; ordinary declarations after each pop keep the restored visibility.
Each resulting object is linked and executes every group.

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
Clang's last-option-wins policy: a later inactive spelling
cannot cancel an earlier request. `-fstack-protector`, `-fstack-protector-strong`,
`-fstack-protector-all` and `-fno-stack-protector` are taken, the last
deciding (#3558, [the checker's guide](../c-checker.md)'s "The stack protector"),
and so are `-fcf-protection` and its `=full`, `=branch`, `=return` and `=none`
(#3570, "CET's protection" there).
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
  `-fno-blocks`, `-fplt`, `-mfpmath=sse`, `-m64`,
  `-mred-zone`, `-mno-ms-bitfields`, `-mno-rtd` and `-mno-iamcu`.
- Floating contraction: `-ffp-contract=off|on|fast|fast-honor-pragmas` is a
  handled value option. The floating option parser validates its value and
  order; the input reader preserves that policy and lexical pragma overrides
  in the source graph. The default is `on`; STDC `FP_CONTRACT DEFAULT`
  restores the command-line policy. See [floating-point pragmas](../c-checker.md).
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

`c_check_address_space_driver_test.c` adds original fixtures for the address
spaces and objc_gc's qualifiers (#1447) to `test-checker-driver` through the same
harness: their declarations, spellings and errors, the conversions' errors and
notes, the conditionals' types (by `_Generic`), and the segments' accesses that
the lowering refuses (`-fsyntax-only` alone). Each one's whole output equals
Clang's on both kernels, at each policy and lane count.
Its `tests/driver/auto-address/` runtime sources cover `__auto_type` with
numeric and OpenCL address spaces (#1844), including the numeric bounds,
duplicate and conflicting attributes, pointer and object levels, cv/atomic
qualifiers, specifier restrict, automatic versus static storage, and compatible
or incompatible redeclarations. The same source runs in GNU17 and C23 modes
with `_Generic` assertions for the deduced types. Both kernels, one/three
requested lanes, syntax checks, object emission or preservation, and
default/`-Werror`/`-w` policies compare complete diagnostics with Clang.
These lane options exercise the existing scheduling requests; they do not by
themselves establish that a particular body is admitted to deferred checking.

`c_check_auto_qualifier_driver_test.c` uses that same harness for the original
`tests/driver/auto-qualifiers/` sources (#1844): the rejected combination of
atomic and restrict specifiers on `__auto_type` diagnoses each declarator,
then drops restrict and continues atomic deduction. Specifier order, GNU
spellings, macro locations, comma groups, missing initializers, recovered types,
later errors and accepted neighboring declarations are checked in GNU17 and
C23, including preservation of output after a rejected unit. A C23 constexpr
comma group guards the continuation flags. The source names and expected
diagnostic counts come from the runtime `cases.txt` catalog.
The same catalog checks `_Atomic __auto_type` pointer initializers: exact
placeholder and source-type spellings, nested and qualified pointers, typedefs,
macro notes, comma declarators, recovered invalid uses, later errors and warning
order. `test-checker-driver --auto-qualifiers-only` runs these sources alone.
Their requested lane settings include the existing source-order fallback for
invalid-binding recovery.

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

## Missing variable declarations (#2222)

`-Wmissing-variable-declarations` diagnoses a file-scope definition with external
linkage when no non-defining declaration precedes that definition. A block's
`extern` declaration counts too. Initialized objects warn after their initializer's
semantic diagnostics and before queued runtime warnings. Tentative objects warn at
the unit's end, in their first tentative definition's order, at their last tentative
definition's name. A later `extern` alone does not suppress an earlier definition.
The attached note points to the written type specifier, including a record or enum
body's opening keyword and the type inside `_Atomic(T)`.

The reader retains this warning's declaration metadata only when command-line
policy or a source pragma can enable it. Its cold pass selects accepted object
declarations from the binding stream in masked 64-event blocks and joins them by
canonical object identity. Two temporary token-indexed `uint32_t` columns retain
the first extern declaration and the accepted binding handle; they remain private
to this reader and its existing tentative-definition pass. Ordinary compilations
allocate neither column. Failed initializer checking allocates a separate byte mask
only for rejected definitions. The driver uses it before warning promotion to suppress
invalid definitions and resolve provisional tentative definitions, while retaining
warnings after nonconstant initializers and recoverable pointer/integer conversions.
Recovery reads preserve the original declaration's warning decision and a type-valid
block extern declaration whose initializer was rejected.

`test-checker-driver --missing-variable-only --in-process` runs the original
`tests/driver/warnings/missing-variable/` catalog through the shared warning harness.
It compares complete Clang diagnostics and exit status, source-policy changes,
system headers, macro notes, syntax/object modes and requested lane counts, including
preservation of an existing output after rejection. The ordinary checker-driver
inventory distributes the suite's individual fixtures across its test parts. Clang
is an external oracle; no implementation or fixture was imported.
