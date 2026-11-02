# driver

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/main.c` is the executable entry point: its commands, and
  for anything else the semantic driver (`run_check_command`, in
  `c_checker_driver.c`): `./build/compiler [--scalar] -fsyntax-only [-E's
  options] file.c` preprocesses, parses and checks the file and prints the
  diagnostics as clang does, with exit status 0 for a unit without errors and
  1 otherwise; `-c [-o file.o]` also writes the object, its functions through
  Tier 0 ([the checker guide](../../docs/c-checker.md)); and with neither option it
  links an executable (`-o`, `a.out` by default) by the direct path, bound
  against the C runtime's snapshot and the shared libraries `-lNAME` names
  (found through `-LDIR`, as needed: `runtime_libraries_import`), `--verify`
  checking every function's records with `x86_validate`, `--lower-all`
  lowering every definition, referenced or not (#129's test option; `-c` and
  the direct path lower only what the unit's roots reach,
  `check_program_select`), `--tier1` compiling each function Tier 1's pipeline
  takes by Tier 1 and the others by Tier 0 (`c_check_tier1.c`, both tiers'
  code the emission's `EmittedCode`, #1056, their unwind tables alike; with `-g`
  Tier 1's rows at the function's granularity, the Tier 1 guide's "Debug
  information";
  `--tier1-strict` fails qualification on an unsupported body rather than
  falling back, while resource and internal failures fail either mode;
  `--tier1-report` and `--tier1-refusals` say which step refused the others,
  and `--tier1-no-rematerialize` compiles them without rematerialization and
  `--tier1-no-lifetimes` without their objects' lifetimes: [the Tier 1
  guide](../../docs/tier1.md)'s "The C driver's `--tier1`" and [Tier 1 failure
  outcomes](../../docs/tier1-outcomes.md)), `--id-order N` handing the unit's identifiers their
  ids in another order, on which nothing it writes may depend (#153's test
  option, `check_driver_id_order`), and `-g` attaching DWARF of its functions'
  source lines (`c_check_debug.c`, #395), to the executable and with `-c` to the
  object (`ObjectOptions.debug`). Either output has its functions' unwind
  tables when clang's would (by default, unless `-ffreestanding` or
  `-fno-asynchronous-unwind-tables`: `predefines_unwind_tables`, the
  [unwind guide](../../docs/unwind.md)'s "The C driver's unwind tables"). Either
  output's functions compile on lanes (#151, `check_program_emit_lanes`, the
  [lanes guide](../../docs/function-lanes.md)): by default as many as the bodies'
  tokens pay for, up to one per core of the CPUs the process may run on, and
  with `--lanes N` N of them, at most one per CPU; the output is one lane's,
  byte for byte. Those lanes are the command's session's (#905,
  `lane_session_run`, `check_driver_run`): started once, after the options and
  before any source is read, as many as the command may take (one per core of
  its CPUs, or `--lanes N`, at most `--worker-budget N`), none for one source
  without code to emit (`-fsyntax-only`), lane 1 counting the cores in sysfs
  (`BUSTER_LANE_SESSION_PER_CORE`, `lane_core_count`) and creating the others
  while lane 0 starts the command (`BUSTER_LANE_SESSION_LAZY`; lane 0 learns
  how many it asks for at its first wide part, `lane_session_asked`, and how
  they started, `lane_session_settle`), and kept parked until the process exits;
  the jobserver's slots are taken per wide region, never at the start, and
  lanes that can't start leave the command on one. An executable carries the program's symbols in `.symtab` (but `.L` labels; with
  `-g`, its DWARF units' functions'). With several source files (#152,
  `c_checker_units.c`, the [translation units guide](../../docs/translation-units.md))
  the units compile on lanes instead, one unit a lane at a time, with lanes
  past the units as their sources pay for and the lanes without a unit left
  taking the units' functions' parts (#777; `--lanes N` then counts those
  lanes), each from preprocessing to code as alone
  (`check_driver_unit`), and their diagnostics are printed in the command
  line's order: `-fsyntax-only` checks each, `-c` writes each one's object in
  the current directory as clang names it (`-o` refused), and the link merges
  the units' links (`check_units_merge`, on the units' lanes by the vector path:
  `check_units_merge_lanes`, #778) into one executable, with `-g` of their DWARF
  units, one each (#779).
  `./build/compiler parse
  [-fsyntax-only] [-E's options] [--scalar] file.c` is the parser's driver
  alone (`run_parse_command`, in `c_parser.c`). With no file each prints its
  usage and exits with status 1. It
  includes most of the compiler and all of its tests, and `build.sh` compiles
  it as eleven translation units: `main.c` itself, the driver's unit, and each
  `main_unit_*.c`, which names a unit and includes `main.c`. Every unit reads
  all of `main.c`, so each has the whole file's diagnostics, but emits only its
  own commands' code: each branch of `main_command` first tests its unit, and
  clang emits no static function that only other units' branches reach. A new
  command's branch names its unit. The modules that several units' commands
  run compile in one unit alone, their home, which the others call (#270): the
  lexer in the driver's unit, the preprocessor and the parser in the front
  end's, and the linker's modules (`import.c`, `runtime.c`,
  `bind.c`, `dynamic.c`, `executable.c` and `snapshot.c`) in the dynamic PIEs'
  unit; and `ir_source_test.c`'s `ir_source_prepare`, the check and emission
  that `test-ir-source` and `benchmark-ir-source` run before the IR, in the
  checker's unit, whose commands run in the backend's (#784). Their entry
  points that other units call take the module's macro (`BUSTER_LEXER_ENTRY`,
  `BUSTER_PREPROCESSOR_ENTRY`, `BUSTER_PARSER_ENTRY`, `BUSTER_LINKER_ENTRY`,
  `BUSTER_IR_SOURCE_ENTRY`) instead of `static`: `main.c` defines it as nothing
  in the home, and elsewhere as an inline definition that is never inlined,
  which clang doesn't emit, so that neither the entry point nor what only it
  reaches compiles there; a unit without `main.c` (a benchmark's) gets
  `static`. A command then runs in two units' copies of the code, so a home
  must share no static variable with its callers' units: `main.c`'s units share
  one lane context (`lane.c`) and the x86 encoder's templates
  (`x86_64_encoder.c`'s `BUSTER_X86_SHARED`), and `./build.sh check-call-graph`
  reports any variable a command reaches in two units ([Call graph check](call-graph.md#call-graph-check)). A
  function to home is a module's entry point that several units compile, coarse enough
  that a call from another unit costs nothing, which keeps its state in its
  arguments. On the 9700X the checker's unit, the longest, took 27.0 s alone
  and all of the build's 40.0 s before the homes, which removed 13% of the
  functions the units compile. The build waits for its largest unit, so keep
  the units balanced: the
  types unit, which had grown to hold the checker, took 2.5 times as long as
  any other until it was split into the types, checker and checker builtins
  units. A test runner that calls each section once (`run_checker_tests`)
  marks the sections `noinline`, which keeps clang from inlining them all into
  one function that its optimizer takes superlinear time over. The checker's
  own sources (not its tests or the builtins' actions) are included between
  `#pragma clang section text = ".text.buster_checker"` and its reset, a
  section that starts on a page, so that code compiled before them doesn't
  move the driver's hot loops in the instruction caches (#502,
  `docs/profiling.md`'s "Placement"): a new one goes there too. The checker
  unit's own tests (`c_checker_test.c` with the sections it includes, and its
  commands' other `*_test.c`) are included between `#pragma clang optimize
  off` and `on` (#681). On the 9700X, the measured `716c980d` to `4ce693ad`
  pair's checker-unit mean fell from 33.7 to 27.35 s; compilation-phase wall
  time fell from 33.7 to 28.5 s (two builds per role, excluding final linking
  and remaining script overhead). One SQLite compile hold, 21 runs per role,
  observed +0.10% median wall, +48,704 instructions (+0.00353%) and +380 KiB
  peak RSS. Four added eager libm imports and layout changes were not isolated
  from each other; this is no general runtime-neutrality claim or timing of
  later heads. [The frozen record](https://github.com/buster14a/buster-performance/tree/db74207d9a1a5bd6b59742afc25d53d5e2e03c68/docs/measurements/checker-unit-build-681-2026-10-28)
  retains the original controller's limitations and correctness scope.
  No benchmark's timed code calls these tests.
  A new test of the checker's goes inside them; a test whose own code is its
  time (a fuzzer's loop, an interpreter) doesn't.
