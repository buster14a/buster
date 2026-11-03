# checker

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/c_check_actions.c` supplies the reader-independent
  expression rules and constant folding for #133. `c_checker.c` checks an
  explicit postorder operand graph either a node at a time or in batches of
  consecutive nodes that read none of each other (or, with
  `CheckOptions.level_order`, in stable dependency levels), with scalar
  barriers for actions that intern types.
  `c_check_input.c` and `c_check_literals.c` are the bounded source adapter;
  `c_check_declspec.c` and `c_check_literal_diagnostic.c` word its refusals of
  declaration specifiers that don't combine and of numeric constants as clang
  does, at clang's places (#1127), and only a refusal runs them;
  its expression graph's walk has a vector path, the coded walk
  (`c_check_expression_input.c`, #1038), which acts at the compacted tokens by
  each one's class, with the walk (`--scalar`) its twin (`docs/c-checker.md`'s
  "The coded walk"); `c_check_sparse.c` holds the output columns few tokens have
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
  `CheckDriverPart`); `test-checker-corpus`
  compares a corpus's acceptance and first diagnostics with Clang, and its
  semantic mutants with `--semantic-mutations` ([Checker corpus and semantic
  mutations](checker-corpus.md#checker-corpus-and-semantic-mutations)), and
  `benchmark-checker-source` measures checking of real
  source bodies by the scalar schedule, the vector path's postorder batches (`vector`), its level order
  (`levels`, `CheckOptions.level_order`), its memoized transitions in node order (`transitions`,
  `CheckOptions.transitions`, #1050, `c_check_transitions.c`) and its transitions keyed in 16-lane windows
  (`windows`, `CheckOptions.transition_windows`): #133's question of which order wins, which the postorder
  batches did on the 9700X (`docs/c-checker.md`, "Level order or postorder"), and #1050's ("Memoized
  transitions"), opt-in (the driver's `--check-transitions`) since they saved 4% of `sqlite3.c`'s graph run
  there. With them, `--time-stages` reports their hits, misses, folds and entries as `time-check
  transition-*` lines, and the driver's `--verify` runs every hit's action too and fails on a difference.
  `test-checker [--seed N] [--clang PATH] [--part K/N] [--program-initializers|--program-vectors|--program-library|--embed-initializers|--requests|--transitions]` compares fresh scalar/vector tables,
  protected tails and original source fixtures against Clang, and checks the constraint catalogue
  (`c_check_constraints_test.c`): for every Constraints paragraph of C17's 6.4 to 6.10 a violation of each rule and
  a valid twin, and the conversions, constant expressions and initializers, each checked as the corpus check checks a
  file and against clang (`docs/c-checker.md`, "The constraint catalogue").
  `--part K/N` runs the K-th of N parts that the default run's sections and `check_test_programs`' programs are dealt
  to in turn (`CheckTestPart`); part 1 also runs the sections that share the run's type table and graph arenas.
  `--program-initializers` selects the automatic-initializer native regressions alone: threshold and odd tails,
  wide strings, embedded NULs, truncation, partial zeroing, constant lists and ordered runtime/volatile writes.
  `--program-vectors` selects the vector, x86 builtin and half-precision programs alone (`c_check_program_vector_test.c`,
  `c_check_program_x86_test.c`, `c_check_program_vector_builtins_test.c`, `c_check_program_fp16_test.c`,
  `c_check_program_half_test.c`), run against clang -O0's code where the host has their features.
  Byte funnel shifts of 16-byte vectors require XOP: these tests compile/link everywhere and execute only where
  XOP is present; `test-x86-intrinsics` also checks their emitted expressions against Clang, including `_mm_rot_epi8`.
  `--program-library` selects the library builtins' tables, constant folds and programs alone
  (`c_check_program_library_test.c`).
  `--transitions` selects the memoized transitions' fixtures, keys and table limits alone (`c_check_transitions_test.c`).
  `--embed-initializers` selects real C23 resource files, independent and Clang object bytes, automatic native
  initialization and an 8 MiB resource whose parser tokens, graph nodes and initializer parts stay bounded. It also
  exercises the source driver and three deferred/function lanes; grant it at least three cores where `cores` is used.
  `--requests` selects #1057's request tests (`c_check_request_test.c`) and the source benchmarks' preparation tests.
  `c_check_data.c` and `c_check_data_input.c` turn selected static scalar definitions into real
  Link atoms/fixups; their tests compare emitted ELF bytes with Clang objects.
  `benchmark-checker --shape wide|chain` measures synthetic graph checking,
  including scheduling and output materialization. `docs/c-checker.md`
  records the actual contract and the remaining declaration/initializer,
  control-flow, corpus and downstream integration still needed
  for #133; this module does not establish whole-compiler semantic coverage.
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
are not covered by the "auxiliary hardening options" above: they reach the
generic `-f` handling, which accepts them, and are left to the wider `-f`/`-m`
audit. An active coverage request they refine is still refused where code is
emitted. Negative flags retain their
existing handling. Within these families, any unsupported request is an error
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
defaults. This closes these option families, not the driver's entire unknown
`-f`/`-m` acceptance policy; the wider allowlist audit and #1112 and #1123
remain separate work. The object and linkage options of #1124 are above.
