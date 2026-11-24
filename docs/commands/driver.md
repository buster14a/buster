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
  Tier 0 ([the checker guide](../../docs/c-checker.md)), and `-S [-o file.s]` its
  assembly in its place ([-S](#-s-1362)); and with none of them it
  links an executable (`-o`, `a.out` by default), for pure-C inputs by the direct path, bound
  against the C runtime's snapshot and the shared libraries `-lNAME` names
  (found through `-LDIR`, as needed: `runtime_libraries_import`), `--verify`
  checking every function's records with `x86_validate`, `--node-window` consuming the first [reader-time native rows](lowering.md#compiling-with-the-window-2850),
  `--node-window-constants` folding [integer arithmetic inside an ordinary expression](lowering.md#integer-arithmetic-inside-an-ordinary-expression),
  `--node-bodies` taking [the function bodies the node machine's producer takes before the reader's walk](lowering.md#compiling-with-the-bodies-taken-2850), `--check-transitions`
  checking the expressions by #1050's memoized transitions (opt-in, each hit's
  action run too with `--verify`), `--lower-all`
  lowering every definition, referenced or not (#129's test option; `-c` and
  the direct path lower only what the unit's roots reach,
  `check_program_select`), `--selection-census` reporting on stderr, beside
  the unit's exact references, the candidates a selection before #2536's
  windows could take, by a walk from every root, a scan of the value nodes and
  the scan pruned of what no control evaluates, by the writer's dead arms and
  by the pass's own before the bodies' controls (step 2.2a), with their
  selections against the exact one and their cost (step 2.1's test option, no change to the
  object: [the design](../all-core-path.md#step-2-the-selection-after-the-windows)),
  and on lines of their own the builtins that pass reaches against the bodies'
  own, with whether the helpers and the runtime functions' preparation over
  each are the same (`selection-census-builtins`), and its initializers'
  plans, compound literals and asm statements against the controls'
  (`selection-census-statements`, step 2.2c.3's), and its left-out ranges
  against the writer's, with whether the lowering's locals over each are the
  same (`selection-census-ranges`, step 2.2c.4's),
  `--provisional-selection` numbering -c's code by that pass's selection, made
  before the bodies' loop and filled in after it, its extra symbols
  placeholders the object leaves out, its builtins, initializers' plans, asm
  statements and left-out ranges that pass's, the same bytes (step 2.2b's test
  option, which reports on stderr whether it served and its extra symbols;
  executables, `--lower-all`, a unit whose candidates keep an extra body, plan
  other helpers, list other statements or place other locals, and one whose
  target groups' states the candidates' references change take the exact
  selection), `--deferred-lowering` lowering by rooms that read
  none of the controls' totals and deciding each part's jump tables with its
  lowering, on one lane without `--tier1`, the same bytes (step 2.2c.3's test
  option: [the design](../all-core-path.md#step-2-the-selection-after-the-windows)),
  `--windowed-loop` lowering -c's provisional selection as the program's
  loop runs, each window's bodies' loop before its lowering, and writing its
  object after the tail, the diagnostics and the exact selection where the
  provisional selection serves and its emission went, the exact selection's
  emission after the loop otherwise, the same bytes and diagnostics (step
  2.2c.4's test option, implying `--provisional-selection` and
  `--deferred-lowering`, which reports on stderr whether its code served),
  `--parse-tile=N` parsing by tiles of about N tokens (4,096 by default), each
  a run of top-level declarations taken through every parser kernel before the
  next, 0 for the unit as one, the same tree ([the parser's tiles](../c-parser.md#tiles-2536),
  #2536; `--time-stages` reports `parse-tiles` and `parse-tiles-refused`),
  `--tier1` compiling each function Tier 1's pipeline
  takes by Tier 1 and the others by Tier 0 (`c_check_tier1.c`, both tiers'
  code the emission's `EmittedCode`, #1056, their unwind tables alike; with `-g`
  Tier 1's rows at the function's granularity, the Tier 1 guide's "Debug
  information";
  `--tier1-strict` fails qualification on an unsupported body rather than
  falling back, while resource and internal failures fail either mode;
  `--tier1-report` and `--tier1-refusals` say which step refused the others
  (and with `--verify`, how many functions' code its certificate proved,
  `code_certified`, or left for a shape it doesn't decode, `code_uncertified`),
  and `--tier1-no-rematerialize` compiles them without rematerialization,
  `--tier1-no-lifetimes` without their objects' lifetimes and
  `--tier1-no-inline` without expanding the unit's candidates' calls, which
  the report's `inlined` counts, or leaving functions without code, its
  `dead` (#1282, #2541), and `--tier1-no-hold` with every candidate's code
  made on its lane, none held for the last pass (the same objects): [the Tier 1
  guide](../../docs/tier1.md)'s "The C driver's `--tier1`" and "Inlining", and [Tier 1 failure
  outcomes](../../docs/tier1-outcomes.md)); `-O` flags set only preprocessor
  optimization macros, not the tier; Tier 0 is the default, and inlines the same calls: `--tier0-no-inline` keeps them, #1398's control; `--tier0-no-hold` makes each callee's code on its lane, none held for the last pass that leaves the dead ones out (the same objects); and `--tier0-inline-shapes` splices callees with several returns or a result in memory too (step 5, off by default), [the Tier 0 guide](tier0.md#inlining-always_inline-functions-1398)), `--emit-window=N` taking one lane's emission in windows of about N tokens, each window's lowering and code before the next one's, in memory the next one reuses (#2536's step 1; 2,048 by default, 0 one window as long as the unit: [function lanes](function-lanes.md#windows-2536)), `--id-order N` handing the unit's identifiers their
  ids in another order, on which nothing it writes may depend (#153's test
  option, `check_driver_id_order`, for each unit of a multi-file command too), and `-g` attaching DWARF of its functions'
  source lines (`c_check_debug.c`, #395), to the executable and with `-c` to the
  object (`ObjectOptions.debug`). Either output has its functions' unwind
  tables when clang's would (by default, unless `-ffreestanding` or
  `-fno-asynchronous-unwind-tables`: `predefines_unwind_tables`, the
  [unwind guide](../../docs/unwind.md)'s "The C driver's unwind tables").
  `-fno-omit-frame-pointer`, the last of it and `-fomit-frame-pointer`, keeps a
  frame pointer in every function by both tiers (`push rbp; mov rbp, rsp`
  first, rbp no value's register), and with `-momit-leaf-frame-pointer`, the
  last of it and `-mno-omit-leaf-frame-pointer`, in every function that calls
  (`predefines_frame_pointer`, #177: [the frame guide](../../docs/tier1-frame.md)'s
  "The frame pointer on request"); without it neither tier keeps one where its
  frame doesn't need one, at every `-O` (clang keeps it at `-O0`).
  `-fstack-clash-protection`, the last of it and `-fno-stack-clash-protection`,
  makes both tiers' frames of a page or more and their dynamic allocations
  touch each page below rsp before rsp passes it (`predefines_stack_clash`,
  #177: the frame guide's "Stack-clash protection on request"); without it
  neither probes, as clang's default.
  `-fpatchable-function-entry=N[,M[,SECTION]]`, the last one, as clang's
  driver reads and checks it (`predefines_patchable`, with its errors), gives
  every function without a `patchable_function_entry` attribute N bytes of
  NOPs, M of them before its symbol, and an entry in SECTION
  (`__patchable_function_entries` by default), by both tiers (#1447: the
  [object writer guide](../../docs/object-writer-and-linker.md)'s
  "Patchable function entries"). Either
  output's functions compile on lanes (#151, `check_program_emit_lanes`, the
  [lanes guide](../../docs/function-lanes.md)): by default as many as the bodies'
  tokens pay for, up to one per core of the CPUs the process may run on (under
  a finite `RLIMIT_AS`, at most one and one more per 512 MiB the limit leaves
  past what the process maps as it starts, #1118, `check_driver_address_lanes`),
  and with `--lanes N` N of them, at most one per CPU; the output is one lane's,
  byte for byte. Those lanes are the command's session's (#905,
  `lane_session_run`, `check_driver_run`): started once, after the options and
  before any source is read, as many as the command may take (one per core of
  its CPUs, or `--lanes N`, at most `--worker-budget N`), none for one source
  without code to emit (`-fsyntax-only`), and by default for one source with
  code only once it is preprocessed, if it has the tokens to pay for a second
  lane (#1444, `BUSTER_LANE_SESSION_ON_DEMAND`, `check_driver_start_lanes`), so
  that a small file's `-c` creates no thread; lane 1 counting the cores in sysfs
  (`BUSTER_LANE_SESSION_PER_CORE`, `lane_core_count`) and creating the others
  while lane 0 starts the command (`BUSTER_LANE_SESSION_LAZY`; lane 0 learns
  how many it asks for at its first wide part, `lane_session_asked`, and how
  they started, `lane_session_settle`), and kept parked until the process exits;
  the jobserver's slots are taken per wide region, never at the start, an
  emission short of its lanes taking more as other recipes give theirs back
  (#673), and lanes that can't start leave the command on one. For a reproducible experiment,
  `--lanes N --lane-cpus C0,C1,...` pins lane i to CPU Ci using exactly N distinct CPUs in the current affinity mask.
  It rejects malformed lists, unavailable CPUs, nested sessions, reduced capacity, and failed pin or thread start.
  Pinned sessions start eagerly, unlike the default lazy path. With `--time-stages`, each lane prints `lane-pin` with its
  index, expected CPU and observed CPU; those diagnostic system calls and output are not uninstrumented timing samples.
  A source command whose lanes created no thread leaves its address space's teardown to a helper as it exits (#1444,
  `lane_exit_helper_start`, called last in `main_source_command`): its streams flushed and every descriptor closed, it
  starts a process that shares its address space and nothing else (`clone3`'s `CLONE_VM`), which ends as it does
  (`PR_SET_PDEATHSIG`), so that its parent's wait returns before `exit_mmap` runs. By default only where PID 1 reaps
  orphans (`lane_exit_reaped`: it blocks, ignores or catches `SIGCHLD`), since one that never does would keep each
  helper as a zombie; `BUSTER_EXIT_HELPER=1` starts it wherever it can, `=0` never, and a sanitizer build never does.
  The exit status, the output's bytes and the exit handlers are as without it. As it exits, the command reads a page
  that only a huge page of the executable's code maps (`lane_exit_probe`, a read timed by the time-stamp counter, no
  system call where it is mapped); where none maps it, the helper, once the command has gone, looks at how the process
  mapped the executable's code and read-only data and asks the kernel for huge pages of the 2 MB ranges it used
  (#2873, `lane_exit_look`; `BUSTER_EXECUTABLE_HUGE_PAGES=0` turns both off): the
  [build guide](build.md#the-executable-in-huge-pages) has what that takes and saves.
  The checker reads
  a unit in source order; `--deferred-bodies` reads its function bodies apart on
  those lanes too (#113's deferred reader, `check_input_deferred_compile`), where
  its bodies' tokens pay for four lanes or more (two with `--lanes N`) and no
  body dominates the unit (#1668), with
  source order's result, and `--no-deferred-bodies` undoes it: the [lanes
  guide](../../docs/function-lanes.md)'s "The checker's bodies on lanes" has why
  it is off by default. An executable carries the program's symbols in `.symtab` (but `.L` labels; with
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
  units, one each (#779). A lane's units skip the file-scope declarations its
  earlier units read silently and purely and that their own code doesn't reach
  (#2531, `--attested=on|verify|off`, on by default but with `--scalar`, `-g`
  too: the [translation units guide](translation-units.md#attested-declarations---attested-2531)).
  `--attested-memo=DIRECTORY` keeps their memo across processes, in a file
  of that directory for the compiler's build and the command's options, so
  that a command of one source file skips what earlier commands learned too
  (opt-in, step 4).
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
  one lane context (`lane.c`), the x86 encoder's templates
  (`x86_64_encoder.c`'s `BUSTER_X86_SHARED`) and Tier 1's selection tables
  (`tier1_select_burs.c`'s `BUSTER_TIER1_SHARED`), and `./build.sh check-call-graph`
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

## Build-system integration (#1113)

Dependency files (`-M`, `-MM`, `-MD`, `-MMD`, `-MF`, `-MT`, `-MQ` and `-MP`),
buffered standard input (`-`), the explicit `-static`/`-static-pie`/`-pie`/`-no-pie`
link modes (`-no-pie`, #1503: a position-dependent dynamic executable, with
Clang's precedence; `c_driver_no_pie_test.c` compares its links with clang and
GNU ld's), stderr-only `-v`, and the common `-print-*` and numeric-version
probes are handled by the driver. `c_dependency_driver.c` owns dependency
collection, naming and publication; `c_driver_information.c` owns the
information probes; `string8.h` is the bounded byte slice those modules share
with the preprocessor. The [CC driver guide](../../docs/cc-driver.md) records
the contract, publication guarantees and the remaining compatibility limits,
and `test-checker-driver`'s `c_driver_compatibility_test.c` exercises real
executable invocations against Clang and GNU Make.

## Option matching (#1444)

The loops that read a compile's arguments compare option IDs, not strings:
`main_information` and `main_preprocessing` (`main.c`'s routing),
`check_driver_command` and `check_request_scan` (the checker's driver),
`pp_parse_options` with `predefines_option` and `pp_take_family`, and
`pp_option_takes_value`. `c_option_table.c` lists each spelling they match whole,
or through an argument's first `=` (`-march=`, `-std=`), in `BUSTER_C_OPTIONS`,
sorted as `strcmp` orders them, each with a canonical ID, `C_OPTION_<name>`: the
spelling in capitals without its leading dashes, `-` as `_` and `=` as `EQ`
(`-fPIC`'s and `-fpic`'s say `BIG` and `SMALL`, `-L`'s and `-l`'s `DIRECTORY` and
`LIBRARY`, `-` is `STANDARD_INPUT` and `--` `END_OF_OPTIONS`). The spellings are
one contiguous struct of char arrays, whose offsets `offsetof` gives at compile
time, shared from the front end's unit like `x86_target_forms.c`'s data.
`c_option_find` looks an argument up once per loop, by binary search, and
returns its `COptionMatch`: the ID of its whole spelling (`exact`) and of its
spelling through its first `=` (`joined`). `exact == C_OPTION_X` holds exactly
where `strcmp(argument, "x") == 0` did, and `joined == C_OPTION_X_EQ` where
`strncmp(argument, "x=", strlen("x=")) == 0` did, since `x` has no `=`, so each
loop keeps its order of tests and its behaviour. Prefixes without a `=` at their
end (`-I`, `-MF`, `-Wl,`, `-fno-builtin-`, `-fptrauth`) stay character tests or
`c_option_starts`. A new option adds its spelling to the table in order;
`test-checker --requests` fails on one out of order.

The target's resolution names the predefined features by index
(`PredefinesFeatureIndex`, generated with the feature table: the preprocessor
guide's "The predefined macros"), where it looked fixed names up by binary search.
On the 9700X, against main at four code placements, the two take a compile's
user instructions down by 70,000 to 78,000 (12.8% of an empty `-fsyntax-only`,
12.3% of a one-line `-c`) and its user cycles by 4 to 8%, and by 260,000 to
270,000 with `-march=native`, whose host detection looked up each feature it
lists (−33% of its instructions, −9 to −12% of its wall time); `sqlite3.c` is
unchanged. In a one-line `-c` Callgrind counts 951 `strcmp`, 189 `strncmp` and
about 970 `memcmp` calls of option parsing and target resolution on main, and 2,
0 and 0 with them. The record: buster-performance's
`docs/measurements/option-ids-1444-2026-11-11`.
`test-checker --requests` checks the table (`c_option_table_test.c`): its order,
offsets and lengths, `c_option_find` against a linear scan on every spelling and
its neighbours, and every option form through the driver's routing as `main`'s,
in a child: joined and separate values, missing values, `--`, a value spelled as
an option, the forwarded `-W` forms, aliases, last-wins families, the predefined
macros that options set, the information probes and the refusals' messages.

## Warning option names

Unknown warning names produce the default-on `-Wunknown-warning-option`
diagnostic in preprocessing and C compilation (#1467). The complete ordered
command line determines its severity before any name is reported. `-w`,
`-Wno-everything`, `-Wno-unknown-warning-option`, group re-enabling, global and
group-specific `-Werror`/`-Wno-error` and fatal-error controls apply to this
diagnostic. A source pragma cannot suppress a command-line warning. Diagnostics
participate in the existing counters, error limit, summary and deferred-history
replay; promoted errors prevent object and dependency publication.

`c_preprocessor_warning_options.c` keeps the driver spellings separate from
the frozen `__has_warning` group catalog. It recognizes positive valued options
such as `-Wlarger-than=42`, its legacy `-Wlarger-than-42` spelling,
`-Wframe-larger-than=4096`, `-Wlarge-by-value-copy=4096`, `-Wundef-prefix=PREFIX`
and module-header controls. It also retains the legacy implicit-declaration
and `nonportable-cfstrings` spellings. An arbitrary `=VALUE` suffix is not
discarded: unknown names and unsupported spellings of known groups still warn.
Option operands and filenames after `--` are not warning options.

Recognizing a warning name does not implement its consumer or its parameter
semantics. `-Wa,`, `-Wp,` and `-Wl,` are classified as driver transports and do not become warning
names. This change adds no assembler or preprocessor forwarding. Optional
spelling suggestions are omitted because the existing catalog does not record
which Clang groups have diagnostics, which its suggestion search requires.

The original `c_driver_warning_options_test.c` cases run through
`test-checker-driver`'s real CC compatibility harness. They compare independent
expected diagnostic names, counts, severities and complete text with Clang
(omitting only its optional spelling hint), on `-E` and `-fsyntax-only` through
both kernels. They exercise policy ordering with the typo before and after the
controls, valid aliases and narrow lookalikes, long and repeated names,
transport/operand boundaries, and preserved objects and depfiles on promoted
errors. The behavioral references are LLVM revision
`7a941dfb2877a0b8cf30323fefa85ad019e0b0f5`,
[`Warnings.cpp`](https://github.com/llvm/llvm-project/blob/7a941dfb2877a0b8cf30323fefa85ad019e0b0f5/clang/lib/Basic/Warnings.cpp),
[`Diagnostic.cpp`](https://github.com/llvm/llvm-project/blob/7a941dfb2877a0b8cf30323fefa85ad019e0b0f5/clang/lib/Basic/Diagnostic.cpp),
[`Options.td`](https://github.com/llvm/llvm-project/blob/7a941dfb2877a0b8cf30323fefa85ad019e0b0f5/clang/include/clang/Options/Options.td)
and `CompilerInvocation.cpp`'s `addDiagnosticArgs`; no upstream implementation
or fixture is imported.

## Warning capabilities (#1365)

The explicit capability inventory in `c_preprocessor_warning_options.c` refuses
effective positive requests for these families until their consumers are complete:

| Warning options | Capability |
| --- | --- |
| `-Wconversion` | Partial: implemented constant/enum diagnostics do not cover the requested narrowing, sign and floating conversions. |
| `-Wshadow`, `-Wsign-conversion`, `-Wunused-macros` | Recognized; the requested warning analysis is unavailable. |
| `-Wmissing-prototypes`, `-Wmissing-variable-declarations`, `-Wstrict-prototypes`, `-Wnested-externs`, `-Wredundant-decls` | Recognized; the requested warning analysis is unavailable. |
| `-Wundef`, `-Wcast-qual`, `-Wpointer-arith`, `-Wswitch-enum`, `-Wvla`, `-Wimplicit-fallthrough`, `-Wcomma`, `-Wfloat-equal` | Recognized; the requested warning analysis is unavailable. |

For example, `-Wshadow` and `-Werror=shadow` fail with an error naming the
original option and missing family. This is a Buster capability refusal,
independent of source contents, warning promotion, error limits and `-w`.
It applies before source processing in preprocessing/dependency-only,
syntax-only and object commands, preserving existing outputs and dependencies.
A source diagnostic pragma cannot satisfy a missing command-line capability.

Disabling and demotion controls (`-Wno-shadow`, `-Wno-error=shadow`) remain
accepted. Demotion does not cancel an enabling request. A later disable of the
whole family or an ancestor cancels it: `-Wsign-conversion -Wno-conversion`
succeeds, whereas `-Wconversion -Wno-sign-conversion` still requests the rest
of the incomplete family and fails. `-Wno-everything` cancels preceding family
requests; a subsequent explicit request enables them again.

Broad positive groups retain Buster's documented delivered subset, including
`-Wall`, `-Wextra` and `-Weverything`. `__has_warning` retains its existing
recognition contract. This inventory covers the families named in #1365;
recognition of other names or valued options is still separate from a promise
of complete diagnostic coverage. Implemented subgroups such as
`-Wconstant-conversion` and `-Wenum-conversion` remain available independently.

`test-checker-driver` checks the refusal contract with independent family names,
enables, disables, promotion/demotion and ancestor ordering on both kernels in
`-E`, `-fsyntax-only` and `-c` modes. It also checks dependency-only commands,
stdin, deferred multi-lane requests, option operands, post-`--` filenames,
feature queries, source pragmas, preserved outputs and absence of new artifacts.
These fixtures assert Buster's refusal rather than claiming Clang emits it.

## Numeric driver options

The driver reads `--lanes`, `--worker-budget`, `--emit-window`,
`--units-memory` and `--id-order` as nonempty unsigned decimal words (#2786).
Leading zeroes are allowed. Signs, whitespace, base prefixes, trailing text
and overflowing values are refused before a source or output is opened; an
invalid occurrence still fails when another occurrence supplies a valid value.

| Option | Accepted range | Spellings |
| --- | --- | --- |
| `--lanes` | 1 through 64 | `--lanes N`, `--lanes=N` |
| `--worker-budget` | 1 through 64 | `--worker-budget N`, `--worker-budget=N` |
| `--emit-window` | 0 through 4,294,967,295 | `--emit-window=N` |
| `--units-memory` | 1 through 18,446,744,073,709,551,615 | `--units-memory=N` |
| `--id-order` | 0 through 18,446,744,073,709,551,615 | `--id-order N`, `--id-order=N` |

`test-check-lanes --numeric-options-only` runs the real driver through each
spelling on default and scalar paths, checks range boundaries and malformed
words, requires rejected commands to preserve an existing output, and compares
every accepted command's object with the ordinary command's bytes.
`--executable PATH` applies these same cases to a separate compiler binary.

## Response files

Source commands expand `@file` before information probes and selection of
preprocessing, compilation or linking (#1476). Arguments replace that word in
place, including after `--`; write `./@name.c` to name a literal source
whose basename starts with `@`. Nested paths and source/output paths use the
process's working directory. Utility commands, the private worker guardian and
libFuzzer retain their own arguments; Make's dry run reads no response files.

The byte grammar uses ASCII whitespace as separators, single or double quotes
to keep whitespace inside a word, and backslash to quote the next byte inside
or outside either quote. Quoted and unquoted portions join; empty quoted words
remain arguments. A bare `@` stays literal. There is no shell or environment
expansion. These are the GNU-style response-file conventions described in
[GCC 7.1's overall options](https://gcc.gnu.org/onlinedocs/gcc-7.1.0/gcc/Overall-Options.html);
malformed-input behavior is this driver's explicit refusal, not a claim of
complete GCC/Clang compatibility.

Expansion admits regular files only, at most 32 active files, 4,096 file
occurrences, 16 MiB of aggregate response bytes and 65,536 expanded operands.
Active device/inode pairs detect cycles, while repeated sibling files are
allowed and charged again. Missing/nonregular files, read errors, changed file
sizes, embedded NULs, unfinished quotes/escapes and exhausted budgets fail
before source/output access. Successful argument storage lives until process
exit, as the original `argv` does. The CC compatibility suite compares valid
cases with Clang and checks that refusals preserve an existing object.

## Mixed inputs

The implicit compile/link driver classifies each positional input before opening
it (#1120). `.c` and `.h` are C; `.i` is C that has already been preprocessed:
directives run, and tokens are not macro-expanded again. `.s` is assembly; `.S` is preprocessed
assembly. `.o`, `.a`, `.so` and versioned `.so.*` are linker inputs. Known
unsupported source suffixes, including C++ (`.cpp`, `.cc`, `.cxx`, `.C`, `.ii`
and C++ headers and module sources), Objective-C (`.m`, `.mi`, `.mm`, `.M`, `.mii`),
CUDA/HIP/SYCL, OpenCL, HLSL, LLVM IR (`.ll`, `.bc`), precompiled headers and ASTs,
Ada (`.ads`, `.adb`), Fortran and `.asm`, are diagnosed before any
source or output is opened (#1472). Other suffixes remain linker inputs, with
a warning; the foreign reader diagnoses their actual format.
`-x c`, `-x assembler` and `-x assembler-with-cpp` override the
suffix for following inputs only; `-x none` restores suffix inference. Joined
`-xLANG` works too. Stdin requires an explicit language at that operand.
A non-regular C or preprocessed-assembly source (`/dev/null`, a FIFO, `/dev/fd/N`)
is read to EOF once, like stdin. Include probes still refuse those files.

The `-E`, `-M` and `-MM` driver uses the same positional language classification:
`.S` and `-x assembler-with-cpp` enable assembly tokenization and
`__ASSEMBLER__` for that input alone; `.s` and `-x assembler` have no
preprocessing action. `-x c` overrides either suffix and `-x none` restores it.
Preprocessing stdin defaults to C. Option operands never select a language, and
after `--` every argument is an input filename. The shared option parser collects
per-input language records once, then each unit initializes its own preprocessor
from the command's common options and its recorded language. The single-input
`pp_parse_options` API retains its existing callers and default C behavior.
`test-checker-driver`'s compatibility cases check mixed language order, both
kernels, option boundaries and a `.S` preprocessing/reassembly execution round-trip.

C-only links retain the existing direct path, including its shared-library
validation and runtime snapshot. Mixed links validate shared libraries'
references and version needs as the direct path does (#1590, #2910,
`ForeignCommand.library_versions`). A needed library's strong unversioned
reference requires a default library export or a surviving executable
definition with default or protected visibility; a versioned reference
requires an export at that version. An `--as-needed` provider is retained
unless the executable satisfies the reference or the referring library's
`DT_NEEDED` already names it. Unversioned retention waits for archive
resolution and COMDAT selection. Missing strong references are refused
unless a dependency is absent from the link, and the loaded libraries'
version needs are checked before anything is written. Weak references do
not retain providers or cause these refusals. Version needs that don't read
are refused as malformed input. When a `-lNAME` or `-l:FILE` request
selects a populated archive, the source command uses the mixed/foreign path
(#1470), so that archive gets the same lazy member selection and input ordering
as a positional archive. Empty archives retain the direct path. The selection
keeps `-L` directory order and static runtime mode; named DSOs on an archive
link keep their named-search identity and as-needed state. `-no-pie`'s
mixed links use its runtime's startup files (`crt1.o`, `crtbegin.o`,
`crtend.o`) and write the same position-dependent executable through the
foreign resolver. Exact `-l:FILE`
DSO-only links retain the direct importer's existing refusal. No new runtime or
writer mode is introduced. `-c` and syntax checking do not resolve libraries.
Selected nonempty `.a` files also use the mixed path when they are GNU ld scripts
that name archives, as glibc's `libm.a` does; `-static` and `-static-pie` select
that file even when a shared library of the same name is available (#2548).

Mixed commands compile C and assembly on the unit scheduler's lanes, retain
their ELF objects in memory, and use the built-in foreign resolver and writer
with the discovered C runtime.
No host compiler/linker or temporary object files are used. Input order, `-l`
state, archive extraction, named sections, C DWARF and unwind records survive
this boundary. Each `.S` unit owns its preprocessing state on its lane, including
`-D`, `-U`, include paths and dependency output; it does not share the standalone
assembler command's static preprocessor. Dependencies publish only after all
units and the final link succeed. The foreign link reuses the command's worker
session and leases its active lanes from the same worker budget.

`-c` writes each C/assembly source's object and warns about unused linker inputs.
An explicit `-o` requires at most one source, not at most one input. Assembly
syntax checking does not write an object. The explicit `assemble` command keeps
its standalone options; preprocessing-only commands retain their own driver.
Existing unsupported C code-generation and linker options remain refusals.

```sh
./build/compiler main.c helper.o answer.s -o program
./build/compiler main.o libhelper.a -o program
./build/compiler -DBASE=40 main.c answer.S -o program
./build/compiler -x c unusual.s -x none helper.o -o program
./build/compiler -c main.c answer.s
clang -std=c17 -O2 -Wall -Wextra -Wno-unused-function -DBUSTER_DRIVER_MIXED_STANDALONE src/buster/compiler/c_driver_mixed_test.c -o /tmp/buster-mixed-test && /tmp/buster-mixed-test ./build/compiler clang
```

The mixed archive regressions include a needed library's versioned reference
(#2911). An archive between the referring library and its provider must stay
unloaded for that reference alone. Both reader paths at one and three lanes
check execution and defined dynamic symbols: an ordinary reference
still extracts, and a source definition or a member loaded for another name
still exports the unversioned definition that the library uses at runtime.

The last command runs the focused original argv-level fixtures, also included
in `test-checker-driver`: own/Clang objects, object-only links, lazy archives,
shared libraries, runtime modes, scalar/multiple lanes and worker caps, parallel
`.S`, positional languages, stdin, compile-only output and failure preservation.


## -S (#1362)

`-S` writes each C source's assembly where `-c` writes its object: GNU as's
AT&T syntax, which assembles into the object `-c` writes (the assembly sink's
form contract, below), compiled by Tier 0 or with `--tier1` as `-c` compiles it.
Its output is `-o FILE`, standard output with `-o -`, and otherwise each
source's base name with `.s` in the current directory, as clang names it (`-o`
with several sources is refused, as with `-c`). `-E` with `-S` selects
preprocessing in either order. `-fsyntax-only` takes precedence over `-S`,
and `-S` over `-c`, whichever comes first, as clang's phases do. It
runs the compile `-c` runs, its diagnostics and their promotions alike, and
writes its file only after the unit's code and data are made: a unit with an
error, or one the sink refuses, writes nothing and leaves an existing file as it
was.

The emission keeps each function's records for it (`CheckProgramLanes.assembly`,
`CheckProgramCode.assembly_code`: copies of the records and branch columns in
the emission's output arena, after branch sizing) and runs the bodies in one
window (`--emit-window` is 0 under `-S`). Only assembly output requests these copies.

With `-g`, the assembly contains the same DWARF 5 section contributions as
`-c -g`, including the line program, scopes, address table, ranges and
variable location lists. Section offsets use symbolic `.long` directives.
Function and variable addresses use `.quad` directives referring to local
labels on their definitions. These references survive linking several units
and linking code or data in named sections.

For inline assembly with integer constant inputs, `-S` preserves a single
`.ascii "…"` directive after operand substitution, including the literal text
that kernel build scripts extract. It retains the directive in the helper
where `-c` places its bytes. A single raw `->…` line is also supported.
The compiler emits it at the statement's instruction boundary so a script
can read it directly from the assembly listing. Both forms accept basic asm
or extended asm with no outputs, clobbers or goto labels.

What it doesn't print yet it refuses by name, before the source is read where
the options say so, and otherwise naming the source and the symbol:

- other inline assembly statements, naked functions and file-scope assembly;
- thread-local storage, section groups, multiversioned functions' resolvers,
  and the other refusals of the sink below;
- assembly and linker inputs beside the C sources.

The unwind tables are the `.eh_frame` section's bytes with their symbolic
relocations (`.long function-.`), as `-c`'s object has them, not CFI
directives, which are stage 4's. With `-fPIC` (#2101) an FDE of a global
function names a local label the sink puts at it (`.long .Lbuster.lN-.`), as
`-c`'s object names its section's symbol, so that GNU as's object links into a
shared library. A position-independent unit that reaches a thread-local is
refused by name: its accessors are code without instruction records, and the
sink doesn't print thread-local storage yet. `-save-temps` is still an unknown argument.

The acceptance (`test-assembly-output --c-testsuite DIRECTORY --subset FILE`,
the inventory's row): each program of `tests/c-testsuite/subset.txt`, at Tier 0
and with `--tier1`, by this executable's `-S` and `-c`; GNU as's object of the
text must be `-c`'s by the form contract's comparison (below), and the program
linked from GNU as's object (`-lm` for the programs that call libm) must print
the expected output and exit 0. All 440 compiles pass, with `--option
-march=native` too. `--compare SOURCE` compares a source without a link (the
repository's standalone tools, up to 4,300 lines, pass at both tiers, with
`-march=native -O2` too), and `--option ARGUMENT` adds an argument to every
compile.

```sh
./build/compiler -S -o main.s main.c
./build/compiler --tier1 -S main.c -o - | less
./build/compiler test-assembly-output --c-testsuite build/c-testsuite --subset tests/c-testsuite/subset.txt
./build/compiler test-assembly-output --option -march=native --compare tools/ci/costs.c
```

## The assembly sink (#1362)

`src/buster/compiler/assembly_output.c` prints a unit of the link
representation as GNU as's AT&T assembly: the text `-S` writes ([the
design](https://github.com/buster14a/buster/issues/1362#issuecomment-6721142506)'s
stages 1 and 2), which describes the object `-c` writes. It prints from the symbolic emission
view (`AssemblyView`), never from the object: the link's atoms, symbols and
fixups, which hold every relocation's target and addend, and each `TEXT` atom's
instruction records as the emission sized and encoded them, with their branch
columns (`AssemblyCode`). It reads the encoder's form table, the matcher's suffix
widths and the disassembler's register names, and changes none of them.

The form contract, which its tests hold GNU as to:

- **Instructions.** Each record in AT&T syntax, with the pseudo-prefixes GNU as
  takes to keep the record's own encoding where its default is another one:
  `{load}` on the integer instructions' load direction between registers (03,
  0B ... 3B, 8A and 8B) and `{load}` or `{store}` on the vector moves', `{evex}`
  where a VEX encoding would do, `{vex}` where an EVEX one would be GNU as's
  choice, and `{disp32}` on a rel32 jmp or jcc to a label or on a memory operand
  that requests a four-byte displacement. A size suffix only where the operands leave the width open (another form of the mnemonic reads
  the same registers but memory or an immediate of another width: `movl $1,
  8(%rsp)`, `fldt`, `cvtsi2sdl`, `vfpclasspdz`), as objdump prints them. A
  record GNU as encodes otherwise whatever it is written as
  (`assembly_escaped`: the encoder tests' `x86_assembler_prefers_another_form`
  in 64-bit mode, such as `81 /0` with an immediate an imm8 holds) is its bytes,
  `.byte`, after a comment naming it; never another encoding.
- **Relocations.** A fixup is its target's name and its addend as the source
  writes it, never a field's bytes: a PC-relative field's addend is the link's
  plus the distance from the field to the instruction's end
  (`x86_fixup_distance`), which the assembler takes off again. A call is
  `name@PLT`, a GOT slot `name@GOTPCREL(%rip)`, an absolute immediate `$name`
  (R_X86_64_32, 32S or 64 by the instruction's field), an absolute displacement
  `name(%base,%index,scale)` (32S), and data `.quad name`, `.long name`, `.long
  name-.`, `.quad name-.` and `@SIZE`.
- **Labels and names.** A symbol's label at its offset, under its own name
  (quoted where GNU as's plain names don't spell it), but an unnamed or
  temporary symbol's and each branch target's under a local `.Lbuster.` name,
  which no object keeps; a link with a name of that prefix, or two symbols of
  one name, is refused.
- **Data and sections.** Each atom in its section (its descriptor's, or its
  class's object section, `object_class_sections`), in atom order, after
  `.p2align` with int3 in code and zeros elsewhere, as the object writer fills
  its gaps; rows of `.byte`, `.zero` for runs of zeros and for zero-fill
  sections, and `.string` and `.ascii` in string pools. For a tentative
  definition selected by `-fcommon` or the `common` attribute, the writer emits
  `.comm name, size, alignment` with the alignment in bytes. The symbol keeps
  its visibility and the linker allocates its storage. The writer prints a
  patchable function's exact NOP sled around its function label, with a
  pointer to the sled's start in a writable linked-order table section.
  GNU as assembles those sections using `"awo"`, an associated label and
  `unique` to keep same-name entries distinct. Buster's assembler does not
  read this section syntax yet.

What the sink doesn't print yet it refuses with the reason naming it
(`ASSEMBLY_UNSUPPORTED`): thread-local storage, section groups, other
linked-order section types and flags, unique symbols, code without records (inline assembly's),
the C runtime's `.init` and `.fini` fragments, and fixups GNU as can't spell
where they are. A view that contradicts itself (records that don't encode to
their atom's bytes, a symbol inside an instruction, a branch without a target, a
fixup off every field) is `ASSEMBLY_INVALID`, an internal error. An arena too
small for the text or the tables is `ASSEMBLY_FULL`; each failure leaves both
arenas as they were.

Per record and per symbol the writer is scalar: formatting is variable-length
text, and only `-S` runs it. Data's rows of sixteen bytes, the bulk of a large
unit's text, go through an AVX-512 VBMI kernel (`assembly_byte_row`: the
nibbles' digits by `vpermb`, the line's 80 bytes gathered by two more). Its
throughput has not been measured on the 9700X.

```sh
./build/compiler test-assembly-output [--cross-check] [--keep]
```

`test-assembly-output` (`assembly_output_test.c`, the assembler's unit) builds
units the way the emission does, from the assembler's matcher's records of Intel
text (independent of the sink's printer), sized by branch sizing and encoded by
the encoder: a unit of every piece the contract lists, compared piece by piece
with its expected spellings, and a small unit compared whole; each refusal and
invalid view by its reason; output arenas short of the text at lengths across
it, and scratch arenas short of the tables up to the first that holds them,
which must print the same text; and the data rows' kernel against libc's
formatting of random rows. Buster's assembler reads the program fixture's
printed source back and compares its ELF sections, symbols and relocations
with the object writer's. With `--cross-check`, GNU as assembles each printed
unit, and its object must be `object_build`'s object of the unit by the
assembler tests' comparator (`object_test_same_as`). An assembler can resolve a
PC-relative reference to a local symbol in the same section, such as
`call helper@PLT` to a static function. The comparator applies those
relocations in both objects before checking the bytes. GNU as also lists an undefined
`_GLOBAL_OFFSET_TABLE_` in an object with a GOT reference, which the comparison
skips. The same comparison covers patchable entries at both tiers, including
several table sections with the same name and entries associated with
different code sections. The reassembled program must link and run.
Then the form sweep: every form the matcher admits for a target of every
feature but the branches (whose targets are labels, the program fixture's), two
random records each, printed and assembled each in a 16-byte slot of its own,
must be the encoder's bytes; a form the installed GNU as doesn't know is counted
as skipped, and any other message of GNU as's fails its form. With binutils
2.42, all 4,474 forms' 8,948 records match (175 of them escaped), none skipped.
`--keep` keeps the sources and objects and prints the program's text.
