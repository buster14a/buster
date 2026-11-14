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
  links an executable (`-o`, `a.out` by default), for pure-C inputs by the direct path, bound
  against the C runtime's snapshot and the shared libraries `-lNAME` names
  (found through `-LDIR`, as needed: `runtime_libraries_import`), `--verify`
  checking every function's records with `x86_validate`, `--check-transitions`
  checking the expressions by #1050's memoized transitions (opt-in, each hit's
  action run too with `--verify`), `--lower-all`
  lowering every definition, referenced or not (#129's test option; `-c` and
  the direct path lower only what the unit's roots reach,
  `check_program_select`), `--tier1` compiling each function Tier 1's pipeline
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
  `--tier1-no-inline` without expanding the unit's always_inline functions'
  calls, which the report's `inlined` counts (#1282): [the Tier 1
  guide](../../docs/tier1.md)'s "The C driver's `--tier1`" and "Inlining", and [Tier 1 failure
  outcomes](../../docs/tier1-outcomes.md)); `-O` flags set only preprocessor
  optimization macros, not the tier; Tier 0 is the default, and inlines the same calls: `--tier0-no-inline` keeps them, #1398's control, [the Tier 0 guide](tier0.md#inlining-always_inline-functions-1398)), `--emit-window=N` taking one lane's emission in windows of about N tokens, each window's lowering and code before the next one's, in memory the next one reuses (#2536's step 1; 0, the default, is one window: [function lanes](function-lanes.md#windows-2536)), `--id-order N` handing the unit's identifiers their
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
  tokens pay for, up to one per core of the CPUs the process may run on, and
  with `--lanes N` N of them, at most one per CPU; the output is one lane's,
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
  The exit status, the output's bytes and the exit handlers are as without it.
  The checker reads
  a unit in source order; `--deferred-bodies` reads its function bodies apart on
  those lanes too (#113's deferred reader, `check_input_deferred_compile`), where
  its bodies' tokens pay for four lanes or more (two with `--lanes N`), with
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
  (#2531, `--attested=on|verify|off`, on by default but with `-g` or
  `--scalar`: the [translation units guide](translation-units.md#attested-declarations---attested-2531)).
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
buffered standard input (`-`), the explicit `-static`/`-static-pie`/`-pie`
link modes, stderr-only `-v`, and the common `-print-*` and numeric-version
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
semantics; known but unimplemented warnings remain #1365's work. `-Wa,`,
`-Wp,` and `-Wl,` are classified as driver transports and do not become warning
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
version validation and runtime snapshot. When a `-lNAME` or `-l:FILE` request
selects a populated archive, the source command uses the mixed/foreign path
(#1470), so that archive gets the same lazy member selection and input ordering
as a positional archive. Empty archives retain the direct path. The selection
keeps `-L` directory order and static runtime mode; named DSOs on an archive
link keep their named-search identity and as-needed state. Exact `-l:FILE`
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

The last command runs the focused original argv-level fixtures, also included
in `test-checker-driver`: own/Clang objects, object-only links, lazy archives,
shared libraries, runtime modes, scalar/multiple lanes and worker caps, parallel
`.S`, positional languages, stdin, compile-only output and failure preservation.

