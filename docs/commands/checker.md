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
  [--lazy-system-headers | --cpu-dispatch-only | --target-versions-only | --type-sugar-only |
  --asm-definitions-only | --call-counts-only | --fp-contract-only] [--in-process] [--clang-server PATH]
  [--differential]` and `test-checker-asm`
  check the driver and inline assembly against Clang (named UCNs too, #1447,
  `check_test_driver_named_ucns`; with
  `--lazy-system-headers`, only the checks that pass the driver
  `-flazy-system-headers`, including the real system-header lazy-policy
  fixtures and eager/lazy object comparisons, since the others run the same either way, and
  `--part K/N` runs the K-th of N parts its checks are dealt to in turn,
  `CheckDriverPart`; #2071's run modes, for every run of buster and of the
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
  `BUSTER_TEST_HOST_BUSY`, the lanes' test hooks, the transparent huge pages
  setting) differ from the test's own at its start. `--differential` also
  runs each such command as a child and fails on a difference in status or
  output, but `--time-stages` and `--lane-cpus` runs, whose numeric report
  lines and lane order differ between any two runs, compare their other lines
  in any order. The run reports the counts and the warning sweep's time, and
  a `run costs` line: the served runs' time, the requests past the server's
  limit, and the children's wall and CPU time, each in total and per run. CI
  runs the driver's parts in these modes, with `--differential` in the native
  plain row; `--cpu-dispatch-only` runs the `cpu_specific` and
  `cpu_dispatch` section alone, #1447; `--target-versions-only` runs GNU multiversioning's `target` and
  `target_clones` section alone, #1838; `--asm-definitions-only` runs what
  inline assembly defines against clang's objects alone, #1848; `--semantic-recovery-only` runs
  `c_check_semantic_recovery_driver_test.c`, `c_check_statement_recovery_driver_test.c`,
  `c_check_type_attribute_recovery_driver_test.c`,
  `c_check_structural_recovery_driver_test.c` and `c_check_repaired_view_driver_test.c` alone, the whole output and status
  of semantic error recovery's fixtures against clang's on the vector, scalar
  and two-lane deferred paths, and `-c`'s refusal of each: `docs/c-checker.md`'s
  "Semantic error recovery", #1447; `--type-sugar-only` runs
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
  with transition verification enabled). Its library-builtin comparisons are pinned to Clang
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
  The same group covers #1549's hidden-result call initializers: copy, temporary and discarded-address elimination,
  nested and indirect calls, only members and elements, alignment, volatile and self-reference fallbacks, assignments,
  reentry, and which labels keep the copy, at both scan paths' lengths; and #1375's named results
  (`c_check_program_named_result_test.c`): no copy or frame bytes on either side of the inline copy's limit, unions,
  compound literals, escaping addresses, statement expressions and cleanups, every excluded shape's copy, and a native
  run of each against clang -O0's code on a dirtied stack. `tests/interpreter/source.c`
  also exercises direct, nested and indirect aggregate call initializers through the typed and Tier 1 source checks.
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
  `c_check_interrupt_driver_test.c` (#147) compares x86's `interrupt` attribute's handler errors (with those clang
  places nowhere), its direct calls and `-Wexcessive-regsave` with pinned Clang through the vector, scalar and
  two-lane deferred paths, and checks that `-c` of a definition is refused by name (`docs/c-checker.md`).
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
  its modifications; `check_unseq_repeats`, vector and scalar twins, a superset of the walk's usages) can't report and
  isn't walked, as most aren't; the walk is scalar, an ordered tree walk with a union-find. A random
  differential of 21,000 full expressions over three variables, without constant first operands, matches clang
  23.1.6 exactly. Differences left: buster's evaluation facts take an assignment's or a statement expression's value
  as a constant condition, which clang's CFG doesn't (`(y = 0) ? 1 / 0 : 2` misses `-Wdivision-by-zero` too), so a
  warning there can be dropped; a brace initializer's constant conversions come after its list's warnings, where
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
  count first) and the declarator's constant conversions after them. A comma's static initializer is syntax-only,
  as `-c` can't compile one yet. A brace initializer's values walk their roots in initializer order, after
  the plan's literal conversions and before its constant ones (`check_common_initializer_conversions`), as clang
  queues their runtime warnings while it parses them. Still separate: a comma in a file-scope array bound, which
  clang refuses as a variable array at file scope and the checker folds.
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

## Runtime fixtures

`test-checker`'s shared source fixtures (`check_test_sources`, 418 cases) are
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
  `gnu23`, `unsigned-char`, `undecided`, `overload-tables`, `overload-none`,
  and `capacity=NAME` for a type table `check_test_fixture_capacity` names;
- **NAME**, what the case checks, which a failure prints.

The cases run in the catalog's order, then the 64 generated ones. A missing or
malformed catalog, a missing or unreadable file, a repeated ID or file, a file
in the directory that no line names, or an empty catalog fails the run; no case
is skipped. The fixture root is `tests/` under the working directory, or
`test-checker --fixtures DIR`. A new ordinary case goes there: its file and its
line. The cases' bytes are the table's literals as clang compiled them, compared
byte for byte when they moved (buster-performance's
`fixtures-runtime-2053-2026-11-13`).

Ten test files' own source tables, 596 cases, are runtime data in the same
format, a directory a suite under `tests/checker/`, which `check_test_suite`
(`c_checker_test.c`) reads and runs. Each case is written to a file, clang
`-fsyntax-only -std=gnu17` checks them all at once, and then each case's
acceptance is compared with clang's and `check_test_source` checks both paths,
as the tables' own loops did. There are no generated cases.

| Suite | Cases | Runner (its test file) |
| --- | ---: | --- |
| `align` | 67 | `check_test_align_sources` (`c_check_program_align_test.c`) |
| `classify` | 20 | `check_test_classify_sources` (`c_check_program_classify_test.c`) |
| `cleanup` | 71 | `check_test_cleanup_sources` (`c_check_cleanup_test.c`) |
| `const-alias` | 53 | `check_test_const_alias_sources` (`c_check_const_alias_test.c`) |
| `float-constants` | 8 | `check_test_float_constant_sources` (`c_check_program_constants_test.c`) |
| `generic-builtins` | 268 | `check_test_generic_builtin_sources` (`c_check_program_generic_builtins_test.c`) |
| `library` | 21 | `check_test_library_sources` (`c_check_program_library_test.c`) |
| `musttail` | 63 | `check_test_musttail_sources` (`c_check_musttail_test.c`) |
| `transparent` | 8 | the end of `check_test_transparent_calls` (`c_check_transparent_test.c`) |
| `transparent-types` | 17 | the end of `check_test_transparent_types` (`c_check_transparent_type_test.c`) |

A table's comments are `#` lines of its catalog, above the cases they described.
The other test files' tables stay C for now: they build their sources when they
run, pair each case with data of their own, or pick cases by index (#2053 lists
them).

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
