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
  its expression graph's walk has a vector path, the coded walk
  (`c_check_expression_input.c`, #1038), which acts at the compacted tokens by
  each one's class, with the walk (`--scalar`) its twin (`docs/c-checker.md`'s
  "The coded walk"); `c_check_binding.c` publishes each use's retained answer from those events
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
  semantic mutants with `--semantic-mutations` ("Checker corpus and semantic
  mutations" below), and `benchmark-checker-source` measures checking of real
  source bodies by the scalar schedule, the vector path's postorder batches and its level order
  (`levels`, `CheckOptions.level_order`): #133's question of which order wins, which the postorder
  batches did on the 9700X (`docs/c-checker.md`, "Level order or postorder").
  `test-checker [--seed N] [--clang PATH] [--part K/N] [--program-initializers|--program-vectors|--program-library|--embed-initializers]` compares fresh scalar/vector tables,
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
  `--embed-initializers` selects real C23 resource files, independent and Clang object bytes, automatic native
  initialization and an 8 MiB resource whose parser tokens, graph nodes and initializer parts stay bounded. It also
  exercises the source driver and three deferred/function lanes; grant it at least three cores where `cores` is used.
  `c_check_data.c` and `c_check_data_input.c` turn selected static scalar definitions into real
  Link atoms/fixups; their tests compare emitted ELF bytes with Clang objects.
  `benchmark-checker --shape wide|chain` measures synthetic graph checking,
  including scheduling and output materialization. `docs/c-checker.md`
  records the actual contract and the remaining declaration/initializer,
  control-flow, corpus and downstream integration still needed
  for #133; this module does not establish whole-compiler semantic coverage.
