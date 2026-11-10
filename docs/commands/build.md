# build

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

The build is CMake's, with the Ninja Multi-Config generator: `CMakeLists.txt`
applies the description in `cmake/BusterSettings.cmake` (the sources, in link
order, and the flags each setting selects), `CMakePresets.json` names the
common trees, and Ninja schedules the work by its dependencies. `./build.sh` is
the convenient front end: it configures a tree when needed, builds, publishes
`build/compiler`, and keeps the print and experiment commands. Nothing else
lists the sources or the flags; CI and the inventory read them from
`./build.sh print-sources` and `print-compile-flags`.

Prerequisites: Linux/x86-64, Clang, CMake 3.25 or later (the presets' schema 6
and `find_program(NO_CACHE)`), Ninja 1.10 or later (Ninja Multi-Config; 1.12
and later start the longest compiles first from their log), Bash, `flock` and
the binutils `nm` and `readelf`. CI stages the official Clang 23.1.4, CMake
4.4.3 and Ninja 1.13.2 (`tools/ci/setup-build-tools.sh`, by SHA-256).

## Commands

```sh
./build.sh [build] [--config Debug|Release] [--target TARGET]... [--jobs N] [--verbose]
./build.sh test [--config Debug|Release] [--jobs N] [-- CTEST_ARGUMENT...]
./build.sh clean [--config Debug|Release | --all]
./build.sh configure
./build.sh print-sources | print-compile-flags [--config C] | print-sanitizer-flags | print-output [--config C]
./build.sh check-call-graph [--config C] [--jobs N]
./build.sh lexer-variant [--config C] [--jobs N] NAME=SOURCE...
./build.sh lexer-variant-assembly NAME SOURCE OUTPUT.s
./build.sh shifts [--config C] [--jobs N] DIRECTORY SOURCE SHIFT...
```

- **build** (the default) builds Release unless `--config Debug`, the
  executable unless `--target` names CMake targets, then publishes the
  configuration's executable, `build/cmake/VARIANT/CONFIG/compiler`, as
  `build/compiler` and its compile commands as `build/compile_commands.json`.
  Plain `./build.sh` is the optimized build it has always been.
- **test** builds and publishes as `build` does, then runs the tree's CTest
  tests for the configuration (below) without holding the tree's lock.
- **clean** removes this environment's tree; with `--config`, that
  configuration's objects and outputs in it, not those both share (the call
  graph's IR); with `--all`, every tree and the published files, once no
  build or clean is using a tree. Nothing else under `build/` (fixtures, `predefines.h`) is touched, and
  no ordinary build deletes anything.
- **configure** configures this environment's tree and builds nothing.
- **print-sources**, **print-compile-flags** and **print-sanitizer-flags** print
  NUL-delimited argv without configuring a tree (`cmake/BusterPrint.cmake`):
  the sources in link order; a unit's compile arguments but `-c`, the source
  and the output (`-g`, the flags with `-Isrc`, `-O3`, `BUSTER_BUILD_DEFINES`;
  Debug's with `--config Debug`); and a sanitizer build's optimization, debug
  information, sanitizer flags and link mode, or nothing. Consumers read arrays
  rather than shell text. **print-output** prints the configuration's executable's
  path, built or not.
- **check-call-graph** captures each unit's LLVM IR in the tree
  (`call-graph-ir`) and checks the no-recursion rule with `BUSTER_COMPILER`,
  which the parallel runner sets, or else the configuration's built executable
  ([call-graph](call-graph.md)).
- **lexer-variant**, **lexer-variant-assembly** and **shifts** are the
  layout and lexer experiments below.

`--jobs` caps the compiles that run at once; by default one per core of the
CPUs the process may run on, read from `/sys` (the logical CPU count where it
doesn't say), since two compiles on one core's threads slow each other down,
the longest among them: on the 9700X before `main.c`'s homes, its 15 threads
compiling at once took 40.0 s and 331 s of CPU, and its 8 cores one compile
each, the longest first, 32.3 s and 233 s; with the homes, 25.3 s and 200 s
(#270). Under `cores`, that is the grant. A build reports the units it
compiled, its time and the three longest compiles, from Ninja's log.

## Configurations

One tree holds **Release** and **Debug**, in `CONFIG/` subdirectories of their
own; switching between them builds nothing that is up to date.

- **Release** is the production build, with the flags the build has always had:
  `-g` (none with `BUSTER_DEBUG_INFO=0`), `-march=$BUSTER_HOST_ARCH -Isrc
  -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror
  -fno-stack-protector`, the host Clang's stack-protector default and default
  triple as definitions, `-DBUSTER_EXECUTABLE_UNIT` (a unit of its own, such
  as `c_lexer_benchmark.c`, then calls the driver unit's Unicode names rather
  than compiling its own copy, which it has when compiled without it to be
  linked alone), `-Wno-unused-function -Wno-psabi` for a scalar host,
  the sanitizer and fuzzing flags, `-O3` (or the sanitizers' level), then
  `BUSTER_BUILD_DEFINES`. CMake's own defaults take no part: no `-DNDEBUG`
  (`assert` stays), no other optimization, no PIE, and `CFLAGS` and `LDFLAGS`,
  which CMake would read once and keep, are ignored as before.
  The policy applies before CMake enables C, including its compiler probes,
  for `build.sh`, direct CMake and presets. Cached or `-D` overrides of
  `CMAKE_C_FLAGS*` and `CMAKE_EXE_LINKER_FLAGS*` are also cleared; select the
  flags through the `BUSTER_*` settings below.
- **Debug** is the same flags with `-O0 -g` in place of the debug information
  and optimization: the same language and aliasing contracts, host ISA,
  sanitizers and definitions, with every variable readable in a debugger.
  It compiles in a fraction of Release's time, and its tests run several times
  slower. The bake and the canary check run on it as on Release.

The flags differ from a direct clang command only in the build's mechanics:
CMake's depfile options, `CMAKE_INTDIR`, a definition no source reads, the
include directory and the source as absolute paths, and
`-fmacro-prefix-map=SOURCE_DIR/=`, which keeps `__FILE__` (assert's messages)
relative to the source directory, as it is when clang runs from there. With
`BUSTER_DEBUG_INFO=0` the Release executable is the old `build.sh`'s byte for
byte (the CI configuration, checked at `x86-64`); with `-g`, its allocated
sections are, and only `.debug_str` and `.debug_line_str` (source paths) and
the build ID, a hash over the whole file, differ.

## Trees, settings and publication

`build.sh` keeps a tree per toolchain and variant, `build/cmake/VARIANT`: the
host ISA, then `-runtime` (`BUSTER_HOST_RUNTIME`), `-SANITIZER-ON` (the
sanitizer and its optimization), `-fuzz` and `-g0` (`BUSTER_DEBUG_INFO=0`, but
in a sanitizer build, whose units have line tables either way) where they
apply, and `-lexer` for lexer variants: `native`, `x86-64-g0`,
`native-address-O3`, `x86-64-address-O1-fuzz`. These change every unit's flags,
so each has its objects apart and switching between them rebuilds nothing.

It passes every setting to CMake explicitly, so unsetting an environment
variable restores the default rather than leaving a tree's cached value, and
configures only when the arguments, the tools and linkers `PATH` finds for it
or the toolchain changed since it last did (`build.sh.configuration` in the
tree). A different `clang` (its path, its file or its version) starts the tree
afresh, since its objects and CMake's cache can't be reused; any other change
reconfigures in place, and Ninja reruns exactly the steps whose commands
changed: `BUSTER_BUILD_DEFINES` recompiles every unit, `BUSTER_LINKER` relinks,
and `BUSTER_BAKE` reruns the post-link step alone. A tree that a preset
configured is reused when its clang is the one it records
(`buster-toolchain.txt`). CMake alone notices a compiler at another path, but
not another file or version at the same one: after such an upgrade, configure
a preset's tree with `cmake --fresh --preset NAME`, or build it with
`build.sh`. An interrupt or termination of `build.sh` stops the build it runs
(its own process group) at once.

| Variable | Default | Effect |
| --- | --- | --- |
| `BUSTER_HOST_ARCH` | `native` | `-march`: `x86-64` for the scalar baseline, `znver5` for benchmark builds |
| `BUSTER_HOST_RUNTIME` | none | links CI's emulated old CPU's runtime from that root (`x86-64` alone) |
| `BUSTER_DEBUG_INFO` | `1` | `0` leaves `-g` out of Release, a fifth of its build time |
| `BUSTER_LINKER` | `auto` | `auto` (wild, then mold, then clang's; for a static link lld, then mold, then clang's), `system`, `wild`, `mold`, `lld`, `bfd` or a path; lld links at `-O0` |
| `BUSTER_BUILD_DEFINES` | none | whitespace-separated arguments every unit's compile adds |
| `BUSTER_SANITIZER` | `none` | `address` (with UBSan and bounds), `undefined`, `thread`, `memory`: a PIE |
| `BUSTER_SANITIZER_OPTIMIZATION` | `3` | a sanitizer build's Release optimization, `1` to `3` |
| `BUSTER_FUZZ_AVAILABLE` | `0` | `1` adds libFuzzer and `--fuzz`, with AddressSanitizer unless a sanitizer is given |
| `BUSTER_FUZZ_ENTRY` | `fuzz.c` | the fuzzing entry's C file, with `BUSTER_FUZZ_AVAILABLE=1` alone |
| `BUSTER_BAKE` | `1` | `0` leaves the tables to be built when the executable runs |
| `BUSTER_PGO` | `0` | `1` builds Release with a profile of the compiler's own compiles (below): a `-pgo` tree, Release alone |
| `BUSTER_PGO_TRAIN` | none | with `BUSTER_PGO=1`, a bash script that trains the profile in place of those compiles |
| `BUSTER_PARALLEL_CODEGEN` | `0` | `1` enables experimental parallel native generation for CHECKER and BACKEND in a Release-g0 `-pcg8` tree |

An empty value is the default, but for `BUSTER_DEBUG_INFO` and `BUSTER_LINKER`,
which reject it; the incompatible combinations fail as they always have
(fuzzing with ThreadSanitizer, a sanitizer with `BUSTER_HOST_RUNTIME`, a runtime
without `x86-64`). With CMake directly, they are cache variables of the same
names.

`build/compiler` is a hard link to the configuration's executable (a copy where
the file system has none): the same file, so a benchmark through it starts
exactly as through the tree's path, renamed into place only after the build
succeeded, and only when it is another file. Publications wait for each other
(`build/.publish.lock`), as do builds and cleans of one tree
(`build/cmake/VARIANT.lock`); every command on a tree holds
`build/.trees.lock` shared while it holds the tree's, and `clean --all` waits
for it exclusively, so it removes no tree a build is using. Each command takes
these locks in that order, the trees', the tree's, then the publication's, so
none waits for another that waits for it. An interrupted, failed or partial
build publishes nothing, and an unchanged one nothing again. CI runs the configuration's own path (`print-output`, exported
as `BUSTER_COMPILER`) and checks that `build/compiler` is that file. Ninja
Multi-Config's `compile_commands.json` has every unit once per configuration;
`build/compile_commands.json` has the published configuration's alone, which
editors and static analyzers read (Release after a plain `./build.sh`). Ninja
keeps that filtered database in `CONFIG/compile_commands.json`, rebuilding it
when CMake regenerates the multi-config database, its filter changes, or the
cached file is missing. Unchanged builds and source edits reuse it. Publication
compares it with the published database under the publication lock and stages a
copy only when the contents differ or the published file is missing. Unchanged
builds and source edits therefore write no temporary database. A needed copy is
completed before replacing the published compiler, so a failed build or staging
copy leaves both published files intact even when a new cached database exists.

## Targets and outputs

In `build/cmake/VARIANT/` (a preset's tree is the same):

- `buster_UNIT`, an object library per source, defined in the compile order
  (`BUSTER_COMPILE_ORDER`, the longest first by their times on the 9700X, which
  is the order a clean build starts them in; Ninja 1.12 and later then order
  them from their logged times), and `compiler_link`, which links their objects
  in the sources' order into `CONFIG/compiler.link`: the same executable as one
  clang command for all of them. A new source goes in `BUSTER_SOURCES` and where
  its time puts it in the order; configuring fails for one the order doesn't
  name.
- `compiler` (built by default): `CONFIG/compiler`, the post-link step's output
  (`cmake/BusterPostLink.cmake`), which bakes the tables and checks the canary.
  The step writes a new file and renames it into place, removing the old one
  first, so that a failed step leaves no executable there.
- `compiler_compile_commands`, when `CMAKE_EXPORT_COMPILE_COMMANDS` is enabled:
  `CONFIG/compile_commands.json`, the configuration's filtered database. The
  `compiler` target depends on it; direct CMake builds with export disabled
  need no compilation database.
- `call-graph-ir` and `check-call-graph`: `call-graph/NAME.ll`, the units' IR,
  one capture for both configurations, and the check by `CONFIG/compiler`.
- `shifts`, with `BUSTER_SHIFTS`: `CONFIG/shifts/compiler-SHIFT`.

The executable is linked at a fixed address (`-no-pie`), from the same code: as
a PIE, its pointer tables' 366,684 relative relocations made every start take 8
to 13 ms of page faults, which `link-foreign` and the tests pay per process
(#86). The sanitizers' builds are PIEs.

It is also linked statically (`-static`, #652): the dynamic loader took 8% to
12% of a small compile's task-clock on the 9700X. The sanitizers' PIEs, a
fuzzing build (libFuzzer's runtime needs C++'s shared libraries) and
`BUSTER_HOST_RUNTIME`'s explicit runtime stay dynamic, and so does a link by
wild: wild 0.10.0's static executables fault in glibc's `__ctype_init`, at
their first TLS access, against Ubuntu's glibc, so a static build's `auto`
linker is lld, then mold (or clang's default without either), and an explicit
`BUSTER_LINKER=wild` links dynamically. lld links at `-O0`, which keeps each
object's strings in its order rather than merging the objects' in their hashes'
order: a compile's comparisons of its options against their names then read a
few pages of the pool, which took 7% to 14% off a small compile from a cold
start on the 9700X, for 1.8 MB more of the executable. The configure step's status line
says which (`static: TRUE`), and the units see it as `BUSTER_HOST_STATIC`. A static executable's `dlsym(RTLD_DEFAULT, ...)`
finds nothing, so the host functions that the interpreter's programs and the
tests' libm oracles name come from `test_libm.c`'s table there and nothing else:
a `dlopen`ed library would bring a C library of its own, whose `tmpfile`'s
`FILE` this one's `fclose` can't free. Nothing can be preloaded into it either:
the tests that count a command's threads read the count its lanes write at exit
to the file `BUSTER_TEST_THREADS` names (`lane_threads_report`), which a
dynamic executable's preloaded counter of `pthread_create` must match. Its start doesn't register its
`.eh_frame` with libgcc's unwinder, as `crtbeginT.o` would where glibc's
`pthread_exit` links the unwinder in: libgcc 13 and later sort every FDE at
that registration, 1.16 million instructions of an empty file's 1.41 million.
The link wraps `__register_frame_info` and `__deregister_frame_info` into
`main.c`'s functions that do nothing (`--wrap`), libgcc finds the frames
through `_dl_find_object` instead, as in a dynamic executable, and `test-lanes`
checks that `backtrace` walks them. The compiler needs no libm either way
(`host_math.h`); a dynamic link takes it only as needed.

The post-link step has each executable write itself with the backend's tables
built (`bake-tables`, `baked.c`, #590): the state that `x86_encoder_initialize`,
`x86_semantics_initialize`, `x86_zen5_initialize`, `tier0_initialize`,
`unwind_initialize`, `tier1_select_initialize` and
`check_lower_runtime_names_initialize` (the lowering's runtime functions' names'
lengths and hashes, #442) write is in a section of its own (`buster_baked`:
`BUSTER_BAKED` for state main.c's units share, `BUSTER_BAKED_UNIT` for a
module's statics, baked in the checker's unit, the C driver's), so a compile
starts with them built; the file's build ID is recomputed over the baked
bytes. A new table those initializers write goes there too: the bake fails
the build when they write anything outside it, including when its own
tracking arrays fill. PIEs (the sanitizers' builds) and `BUSTER_BAKE=0` build
them when they run; `test-baked-tables` checks either kind and fails, rather
than skips, when the section is missing (the compile-time guide's "Baked
tables"). The stack-protector policy is pinned (#916): `-fno-stack-protector`,
and the step fails an executable whose objects reference `__stack_chk_fail`,
which the toolchain's configured default would have added back (in the
objects, since a static link's libc.a defines and calls it itself).

`BUSTER_DEBUG_INFO=0` builds without `-g`, which took a fifth of the build's
time; CI builds so, since nothing there reads it (clang's code then differs in
a few functions' stack slots). The checker's unit takes longest: `check` and
the default driver, `test-checker`, and its other commands each reach the whole
compiler, and before the homes a unit for any one of them took 84 to 88% of its
time (clang 18, locally).

### Unity test dependencies

The eleven `main.c` units share the compiler implementation and the fixtures
used across test families. Independent test files are included only by the unit
that runs them, with the same guard around their command branches. This avoids
parsing those tests in the other ten units during a clean build, and Ninja's
depfiles keep their edits local: for example, `c_check_builtin_bits_test.c`
rebuilds only `main_unit_check_builtins.c`, `profile_test.c` only `main.c`,
`c_files_test.c` only `main_unit_frontend.c`, and `tier1_wide_test.c` only
`main_unit_backend.c`, followed by the link and post-link checks.

The checker keeps `c_checker_test.c` shared for its value and stream comparison
helpers, while its private fixtures, test routines and nested test files are
compiled only by `main_unit_checker.c`. Editing `c_check_program_test.c`,
`c_check_generic_test.c`, `c_check_attribute_test.c`, or the program tests'
nested `c_check_program_amx_test.c` therefore rebuilds that unit alone. Editing
the checker driver suite `c_checker_driver_test.c` or the debug test
`c_check_debug_tier1_parameters_test.c` also rebuilds only that unit. Editing
the shared `c_checker_test.c` root still rebuilds all eleven units.

The backend similarly owns the private IR, interpreter, optimizer and Tier 1
fixtures. Edits to nested files such as `ir_simd_model_test.c`,
`interpreter_wide_test.c`, `ir_optimize_simd_test.c`, `tier1_call_probe_test.c`
and `tier1_address_coalesce_test.c` rebuild only `main_unit_backend.c`.
The optimizer roots `ir_optimize_test.c` and `ir_optimize_text_test.c`, including
their nested if-conversion and verified-pass fixtures, also belong to BACKEND.
Other units include the production `ir_if_convert.c` and
`ir_optimize_verified.c` directly at the same positions. Editing those production
passes still rebuilds all eleven main units. The source preparation interface in
`ir_source_test.c` serves BACKEND and CHECKER; other units include production
`ir_dataflow.c` directly at the same position. The source root and
`tier1_select_burs_test.c` therefore rebuild those two consumers alone, while
`ir_dataflow.c` remains a dependency of all eleven units. Four narrow semantic
references retain production unused-function checking without emitting code.

The backend also owns the private Tier 0 fixtures, their nested tests and vector
baseline, the mask tests and the streaming tests and benchmark. Editing those
files rebuilds only `main_unit_backend.c`. `tier0_test_same_code` and its unwind
consumer belong to that same unit, so the entire `tier0_test.c` root is private.
Production `tier0_generate.c` and `streaming.c` retain all eleven owners.

Nonstandard support names follow the same rule. The differential command's
`differential.c` belongs to BACKEND. `test_tool.c` keeps probing helpers for
DRIVER, TYPES, X86, FRONTEND, LINK and BACKEND, while `test_tool_test.c` belongs
to DRIVER. `test_process.c` keeps the CPU-limit and capture-file helpers shared;
its process-worker functions and `test_process_test.c` belong to FRONTEND.
Standalone inclusion of either support root retains its complete self-tests.

The frontend owns the private preprocessor, parser, GNU, bindings and lowering
fixtures and their fixture-consuming commands. Edits to
`preprocessor_fuzz_generate.c`, `c_preprocessor_directives_test.c`,
`c_predefines_test.c`, `c_gnu_test.c`, `c_parser_bindings_test.c` or the nested
`x86_target_policy_test.c` rebuild only `main_unit_frontend.c`.
`c_preprocessor_history_test.c` is included by the frontend and checker units:
the checker shares its diagnostic scratch-location normalization, while the
frontend alone includes its private tests. The shared helper spans in
`c_preprocessor_test.c`, `c_preprocessor_oracle.c` and `c_parser_test.c` stay at
their original positions, so editing those roots still rebuilds all eleven
units, as does editing the production `c_parser_bindings.c` or
`c_builtin_intrinsic_catalog.c`. The intrinsic catalog keeps its first inclusion
position in every unit, including those that omit the feature tests. On vector
hosts, editing it also rebuilds the standalone `c_checker_benchmark.c`, which
includes the catalog itself.

The executable unit owns the private foreign input fixtures and the benchmark
commands that use them. Editing `foreign_unwind_runtime_test.c`,
the nested `foreign_driver_symbols_test.c`, `foreign_strings_output_test.c`
or the directly included `foreign_gc_benchmark.c` rebuilds only
`main_unit_executable.c`. The checker shares helpers that read and write ELF files
and link test programs in `foreign_test.c`; their definitions stay at their original positions,
and editing that root rebuilds exactly the checker and executable units. The
production `foreign_link.c` and `foreign_unwind.c` remain shared by all eleven
main units.

The frontend roots with internal guards, `foreign_test.c`, `tier0_test.c` and
`tier1_select_source_benchmark.c` also preserve standalone preprocessing. Without
`main.c`'s unity-unit definitions, their guards keep the complete source. The
correctness inventory preprocesses `main.c` and every `*_benchmark.c` as independent inputs
and compares them with Clang. The build regression also preprocesses the
guarded roots directly, checking that their private entry points remain in the
output.

The remaining independent diagnostic, dataflow, SSA, allocation, builtin-lowering,
frame, unwind, intrinsic, measurement and source-benchmark fixtures follow the
same rule. In particular, the allocator's nested `tier1_allocate_test_cases.c`
and the scheduler's `tier1_schedule_fixture.c` belong to BACKEND; the lowering
inventory belongs to CHECKER; and the driver owns the process measurements and
SQLite harness. The row-source header is shared by DRIVER and CHECKER alone.
The benchmark counter utilities have exactly BACKEND and CHECKER as consumers;
the debug benchmark keeps only its clock available to BACKEND beside its LINK
owner. Intrinsic object-reading helpers remain available to the checker's AMX
fixtures and X86. Standalone benchmark translation units retain their own
includes and dependency sets.

The type fixtures keep table allocation available to TYPES, FRONTEND, BACKEND
and CHECKER, with the generated-world helpers restricted to TYPES and CHECKER.
Their private type tests, including the deferred and lookup suites, belong to
TYPES. The ABI root similarly shares only its generated-world builder with
CHECKER; `calling_convention_test_limits.h` supplies its three contract bounds
to the backend without including that fixture root. Integer builtin oracles
serve TYPES and CHECK_BUILTINS; environment jobs serve FRONTEND and CHECKER.
Lane topology helpers serve DRIVER, CHECKER and EXECUTABLE, while the VM helpers
serve DRIVER and CHECKER alone. Private bodies retain complete standalone
preprocessing.

X86 owns the encoder and intrinsic fixtures, with their printing, generation
and object-reading helpers retained for other families. The branch-sizing
root shares its process runner with DRIVER. The semantics root deliberately
keeps six `used` globals in every unity object at their original positions;
its form names also serve BACKEND, and only DRIVER reads the fixture bodies.
The lexer and Tier 0 roots now belong wholly to DRIVER and BACKEND respectively.
The BURS and source-corpus helper roots have exactly BACKEND and CHECKER as
consumers. The c-testsuite command's fixtures belong to DRIVER; BACKEND shares
only its process/file helpers for the SSA source tests.

ELF fixtures use the same boundaries. LINK owns private object/DWARF/import
and binding checks; EXECUTABLE owns executable, symbol and snapshot checks;
DYNAMIC owns its PIE, library, bounds and static-link suites. Their small
constructors, readers, runners and shared inputs remain at their original
positions, with individual helper guards matching their actual consumers.
The linker, import and binding roots have LINK, DYNAMIC and EXECUTABLE as
consumers; the debug native helpers have LINK and BACKEND; snapshot's patching
helpers also serve CHECKER. The shared object-view declarations and helpers in
`object_writer_test.c` remain available to the production symbol reader.
All owning commands and their emitted code stay in the same units, and the
importer's five assertion coordinates are preserved.

The test commands, their owning units and compiler optimization flags stay the
same. The checker's common helpers retain their callers; the driver's
`oracle_cache_test.c` is private. Narrow semantic references keep production
helpers checked in units that omit their fixture callers and emit no code.
Check every unit with scalar and vector flags
after changing these guards; the build regression harness checks representative
private-test edits against their exact owning units and keeps shared helper and
production dependencies explicit.

### Production parsing and implementation dependencies

`c_parser.c` holds the parser's shared types, declarations and small helpers;
`c_parser_impl.c` holds its private data and function bodies. Only FRONTEND
includes those bodies in the unity build. Other units call the same frontend
entry points through ordinary external declarations. Standalone inclusion of
`c_parser.c` keeps the complete implementation, including the vector parser
benchmark. An implementation edit therefore rebuilds FRONTEND alone on a scalar
host, and FRONTEND plus `c_parser_benchmark.c` on a vector host. Shared interface
edits still rebuild their consumers.

The x86 target-form row enums, initializers and per-row policy assertions are
also parsed only by their data owner or a standalone consumer. Other units use
complete array declarations with two explicit bounds. The owner recomputes
those bounds from the canonical rows and asserts equality, so adding or removing
a row requires updating the interface deliberately. Editing row contents rebuilds
FRONTEND and `tier1_model_measure.c`, plus `tier1_select_benchmark.c` on a vector
host; changing a shared bound correctly rebuilds the readers too.

The ownership-only check pins both production paths to FRONTEND in the unity
graph. The full build regression also checks their exact incremental fanout,
including standalone consumers, and rejects both kinds of target row-count
drift. A new dependency cannot silently undo these boundaries.

The [validation packet](https://github.com/buster14a/buster-performance/tree/f29dc2c6616fb0530a2b21b1555507da79f7ba25/docs/measurements/build-production-codegen-2026-11-09)
records the before/after dependencies, the parser definition and object
preservation checks, and the optional parallel-codegen contract and runtime
results below. Ordinary unity expansion falls by about 35 MB (10.6%) in each
host configuration. Its elapsed-time observations are development diagnostics;
approved-host build and runtime measurements are still required before changing
the experimental mode's default.

### Preventing dependency growth

Give new test, inline benchmark and measurement C files the `*_test.c`,
`*_benchmark.c` and `*_measure.c` suffixes; name test headers/fragments
`*_test.h`/`*_test.inc`. Keep their includes and command
branches inside their owning unit's guard. If another family needs a helper,
share the narrow helper span and keep private fixtures and nested includes
guarded. A root that is also preprocessed as a standalone input must keep its
complete source when `BUSTER_UNIT` is undefined.

The build regression's `--ownership-only [COMPILER]` mode reads all active
repository paths in the selected compiler's Ninja graph. Test/benchmark/measure
names, fixture names and explicit nonstandard roots seed a literal quoted-include
closure. Every active descendant is checked regardless of suffix, extension or
directory, including headers, `.inc` files and generated fixture data. The
default is exactly one unity owner. A shared root never exempts a new child or
prefix. Important nonstandard paths also assert the command's exact owner.

Shared exceptions name exact paths, explicit owner sets and a reason; even the
current eleven-unit set is explicit, so adding a unit cannot silently widen it.
Remove obsolete exceptions. Production implementations/data reached through
fixtures have their own exact rows, rather than an allowance for descendants.
The two scalar host headers have separate, exact configuration-scoped rows.
Do not add private fixtures to the exception table merely to pass the check.

Shared fixture/support roots also pin a small lexical fingerprint of their
shared declaration headers, aggregate members, include directives and macro
names with their owner masks. Unit-private islands do not contribute. Unknown
host predicates conservatively include both branches, so scalar CI protects
vector-only shared growth too. New unguarded functions, data declarations,
headers or widened helper guards therefore require review even inside an
already-approved root. Guard private additions for their owner; update the
reported fingerprint only after reviewing a justified shared change. This is
not a C semantic analysis: function bodies, data initializers and macro
replacement bodies still need review for private fixture growth. Macro-computed
include paths need an explicit assertion if literal closure cannot discover
them. Keep includes literal where practical.

After building the selected configuration, run the check directly with:

```sh
cc -std=c17 -O2 -Wall -Wextra -Werror -o /tmp/buster-build-regression tools/ci/build_regression.c
/tmp/buster-build-regression --ownership-only "$(./build.sh print-output --config Release)"
```

Pass the Debug output path to check Debug instead. The check refuses an
incomplete graph or pending build work, rather than accepting old dependency
data after an unbuilt edit. Each unity record must be VALID, have its declared
number of dependencies and contain its own source; missing, duplicate, stale
or wrong-configuration records fail. It also preprocesses internally guarded
roots without a unity selector. It reads one dependency snapshot and does not
rebuild the compiler. The ordinary correctness inventory runs it against its
own compiler, including on source-only PRs that do not trigger the separate
build-regression workflow. Representative edit-plan checks remain in the full
build regression to verify Ninja's actual invalidation behavior.

The [cleanup inventory](../build-dependency-cleanup.md) records completed packets,
necessary sharing, remaining production boundaries and validation limits.

## Presets and CTest

`CMakePresets.json` has the configure presets `native`, `x86-64` and `znver5`,
whose trees are `build.sh`'s `build/cmake/native`, `x86-64` and `znver5`; the
build and test presets `debug` and `release` (native), `x86-64-debug`,
`x86-64-release`, `znver5-debug` and `znver5-release`; the test presets
`debug-build` and `release-build`, the `build` label alone; and the workflow
presets `debug` and `release`, which configure, build and run those:

```sh
cmake --preset native
cmake --build --preset release
ctest --preset release-build
cmake --workflow --preset debug
```

A direct build doesn't publish `build/compiler`; its executable is
`build/cmake/native/Release/compiler`. Keep `CMakeUserPresets.json`, which git
ignores, for presets of your own.

CTest's tests run the configuration's own executable: `build` (`baked-tables`,
`parallel-runner`) checks what the build made and needs nothing else;
`correctness` runs the whole inventory, `tools/ci/correctness.tests`, through
that executable's parallel runner, which gives each command the executable as
`BUSTER_COMPILER`, and the tree's settings to the commands' own `./build.sh`
calls. The inventory needs CI's tools and frozen inputs ([CI](../ci.md)) and
every core, so it runs alone. `./build.sh test --config Debug -- -L build`
runs the first label in Debug.

## Experimental parallel native generation

`BUSTER_PARALLEL_CODEGEN=1` makes the two longest units' native-code generation
separate Ninja jobs, after their ordinary Clang `-O3` optimization:

```sh
BUSTER_DEBUG_INFO=0 BUSTER_PARALLEL_CODEGEN=1 ./build.sh --jobs 8
```

The mode is off by default. It currently accepts Release without debug information,
sanitizers, fuzzing, PGO, an explicit host runtime, lexer variants or
`BUSTER_BUILD_DEFINES`; unsupported explicit combinations fail. `build.sh` uses a
separate `VARIANT-pcg8` tree with Release alone. Direct CMake configuration also
requires `CMAKE_CONFIGURATION_TYPES=Release` and `BUSTER_DEBUG_INFO=0`. Normal
builds retain their existing configurations, flags and tool requirements.

The opt-in mode needs `llvm-split`, `llvm-nm`, `llvm-objcopy` and `ld.lld` matching
the configured Clang version, found beside Clang or on `PATH`. These tools generate
ordinary native objects before the final link, so `BUSTER_LINKER` still selects
any supported final linker. A missing or mismatched partition tool fails the
explicit opt-in configuration.

The original CMake object target compiles each heavy C source to fully optimized
LLVM bitcode with `-emit-llvm`; it does not use an LTO pre-link optimization
pipeline. That target retains the source's original depfile and compilation
database record. `llvm-split` writes eight fixed partitions, which eight generated
object targets compile with the same host CPU choice and driver PIC defaults,
`-O3` native-code generation, and the IR optimizer disabled. Ninja schedules those
jobs normally, so `--jobs` and a `cores` grant bound their concurrency. The generated
IR jobs are excluded from the compilation database.

Each unit's eight objects are partially linked in fixed order, at ELF linker
`-O0` with one linker thread. Partitioning promotes private functions, data and TLS
to hidden globals; the merge restores all definitions outside the source module's
original public export list to local binding. Actual unresolved strong and weak
symbols are preserved and checked. No source-level unit shares new private state
with another unit. The final native object lives at
`Release/codegen/UNIT/native.o`; the original `CMakeFiles/.../UNIT.c.o` is bitcode
in this mode. Linking, stack-canary checks and shifted links use the native object.

An unrelated source edit reuses both cached native objects. A change to a heavy
unit rebuilds its source IR and split, but byte-identical partitions and the public
export list retain their existing timestamps. Native generation runs only for
changed or missing pieces; comment-only and timestamp-only source edits that leave
optimized IR unchanged do not repeat native generation or linking. Changed native
objects then link and bake as usual. Missing generated pieces are restored through
their declared dependencies. Failed splitting, native compilation or symbol
restoration stops the graph before a replacement executable can be published.

C frontend diagnostics keep their normal source locations. Diagnostics produced
only when compiling inline assembly remain fatal under `-Werror`, but in this
experimental mode they report the inline assembly without the original C file
and line. Native code layout can also differ after partitioning; this option does
not promise byte-identical machine code or establish a runtime performance gain.

`build-regression --parallel-codegen-only` exercises the production helpers on two
translation units with identically named private functions, data and TLS, distinct
hidden public entries, and shared strong and weak external references. It checks
runtime isolation across threads, the source compilation database, unchanged and
source-only edits, one-partition private data edits, missing pieces, deterministic
regeneration, late assembly warnings and failed-stage recovery. The full build
regression also builds the real opt-in compiler once and runs its baking checks.

## A profile-guided build

`BUSTER_PGO=1` builds the Release executable, and `shifts`' executables, with a
profile of the compiler's own compiles (#880), in a tree of its own
(`VARIANT-pgo`) with the Release configuration alone. Under its `pgo/`: the
sources compiled with `-fprofile-instr-generate`, without debug information,
into an executable left unbaked, since its counters are writable memory outside
`buster_baked`, which `bake-tables` refuses; that executable compiles each
source as `-E -P`, `-fsyntax-only` and `-c` (a Ninja step per source, one per
core at a time), a profile per process; and the `llvm-profdata` beside `clang`
(or on `PATH`) merges them into `pgo/compiler.profdata`, with which every unit
then compiles (`-fprofile-instr-use`). Each step reruns when its inputs
change: an edited source retrains the profile, and a new profile recompiles
every unit. `print-compile-flags` leaves the profile out, since only a build
trains it. `BUSTER_PGO_TRAIN=SCRIPT` replaces the training with a bash script
of your own, run in `pgo/` with `BUSTER_PGO_COMPILER` naming the instrumented
executable. On the 9700X, the compiler's own sources gave a better profile
than the frozen `sqlite3.c`, musl and c-testsuite of buster-performance#182
("The compiler's own code and a profile", `docs/compile-time.md`).

The mode isn't the default, and no CI job builds with it. Sanitizer builds,
`BUSTER_HOST_RUNTIME` and `lexer-variant` refuse it (the lexer variants change
`main.c`'s code, so a profile of the build without them no longer matches it),
as do invalid values of `BUSTER_PGO` and `BUSTER_PGO_TRAIN`; the settings'
checks make each refusal before any tree is configured, and
`.github/workflows/build-pgo.yml` runs them on GitHub's runner with a `clang`
that fails at any compile.

## Experiments

`./build.sh lexer-variant NAME=SOURCE...` builds `build/compiler` with other
lexers in it, for `benchmark-lexer` to check and time beside `c_tokenize` in
one process: each SOURCE is a copy of `c_lexer.c` (compiled through
`src/buster/compiler/c_lexer_variant.c` into `c_lexer_variant_NAME`), or that
file's assembly (`.s`), which `./build.sh lexer-variant-assembly NAME SOURCE
OUTPUT.s` writes with debug information, and `label-spills` may label. The
variants have a tree of their own (`VARIANT-lexer`), whose units all take the
list of variants in place of `BUSTER_BUILD_DEFINES`.

`./build.sh shifts DIRECTORY SOURCE SHIFT...` builds the executable, then links
the same objects into `DIRECTORY/compiler-SHIFT` for each SHIFT, with that many
bytes of padding in `.text` before SOURCE's code (`cmake/BusterShiftPadding.cmake`),
a multiple of its `.text`'s alignment, for `time-runs` to compare builds over
their code's placements (#509, "Comparing two versions" in
`docs/profiling.md`): the same code ran up to 2% faster or slower so on the
9700X. The padding is a section of its own, `.text.buster_shift_padding`,
with the source's `.text`'s alignment and `SHF_GNU_RETAIN`, which the linkers
place right before the source's objects; the step fails when the linker dropped
it. The padding moves units' `.text`, never the page-aligned `.text.buster_checker`
that holds the checker's own code. To place that code, build with
`BUSTER_BUILD_DEFINES=-DBUSTER_CHECKER_OFFSET=N` (`main.c`, a multiple of 16 below
4,096), one full build per offset (#1233).

## Regressions and timings

`tools/ci/build_regression.c` (the `Build` workflow, on GitHub's runners)
checks a fresh, an unchanged, an edited, a failed, an interrupted, a
terminated and a switched build, an invalid setting, a configuration's clean,
a clean and a `clean --all` during a build,
the settings that change one step, hostile inherited/cached flags, the presets,
the print commands, shifts and a lexer variant: what runs, what is published and what is left behind, not how
long it takes. Its `clean --all` check removes every tree under `build/cmake`
first. `build-regression --flags-only` runs just the fresh-tree flag checks:
independent hostile `CFLAGS` and `LDFLAGS` through `build.sh`, direct CMake and
a preset, plus hostile global and Debug/Release cache flags. It checks compiler
probe logs, the generated compile/link commands and real unit compiles in both
configurations; the full harness also builds the executable with both hostile
environment variables. The focused mode cleans the selected `build.sh` tree.
The build's clean, unchanged and edited timings come from the
9700X under its lock, with their raw records in
[buster-performance](https://github.com/buster14a/buster-performance).

`build-regression --compile-commands-only` checks the database dependencies
with a small CMake project: both configurations, an ordinary source edit,
changed compile arguments, a missing cached database, an edited filter and
export disabled. It builds only the fixture. The full harness also checks
that missing cached or published databases are restored without recompiling
or relinking Buster, and that configuration switches publish the right one.

`build-regression --driver-only` checks the build driver's actual version,
description and publication helpers in a disposable directory, without building the compiler.
It checks version thresholds, publication without a database copy when the
contents match, changed and missing databases, copy fallback, and staging
failures that must preserve both published files. The full harness runs these
checks too. Keep these behavioral checks when changing the wrapper's fixed
overhead; no-op timing thresholds would be sensitive to runner load.
