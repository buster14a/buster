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
./build.sh [build] [--config Debug|Release|Develop] [--target TARGET]... [--jobs N] [--verbose]
./build.sh test [--config Debug|Release|Develop] [--jobs N] [-- CTEST_ARGUMENT...]
./build.sh clean [--config Debug|Release|Develop | --all]
./build.sh configure
./build.sh print-sources | print-compile-flags [--config C] | print-sanitizer-flags | print-host-flags | print-output [--config C]
./build.sh check-call-graph [--config C] [--jobs N]
./build.sh lexer-variant [--config C] [--jobs N] NAME=SOURCE...
./build.sh lexer-variant-assembly NAME SOURCE OUTPUT.s
./build.sh shifts [--config C] [--jobs N] DIRECTORY SOURCE SHIFT...
```

- **build** (the default) builds Release unless `--config Debug` or
  `--config Develop`, the executable unless `--target` names CMake targets, then publishes the
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
  information, sanitizer flags and link mode, or nothing (Develop's flags with
  `--config Develop`, its units' own: the mechanics of its parts, below, aside).
  **print-host-flags**
  prints the host target alone, `-march=$BUSTER_HOST_ARCH` or the explicit
  vector target's flags (below), which the inventory's commands that compile for
  the build's host pass to clang and to the compiler. Consumers read arrays
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

One tree holds **Release**, **Debug** and **Develop**, in `CONFIG/`
subdirectories of their own; switching between them builds nothing that is up to
date. Release is for the product and every timing, Develop for edits and tests,
Debug for a debugger.

- **Release** is the production build, with the flags the build has always had:
  `-g` (none with `BUSTER_DEBUG_INFO=0`), the host target (`-march=$BUSTER_HOST_ARCH`), `-Isrc
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
- **Develop** is Release's flags and optimization with DWARF 4 line tables
  (`-gdwarf-4 -gline-tables-only`, whatever `BUSTER_DEBUG_INFO` says) in place
  of `-g`, its `main.c` units optimized in parts, the tests' own functions
  unoptimized and the benchmark units left out ([Develop](#develop), #2559):
  the edit loop's configuration. Its executable runs the compile path about 1%
  to 2% slower than Release's, writes the same outputs, and has lines, not
  variables, for a debugger. The bake and the canary check run on it too.

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
changed: `BUSTER_BUILD_DEFINES` recompiles every unit, `BUSTER_LINKER` relinks
(and joins the split units' parts again where it changes whether the link merges
constants, below),
and `BUSTER_BAKE` reruns the post-link step alone. A tree that a preset
configured is reused when its clang is the one it records
(`buster-toolchain.txt`). CMake alone notices a compiler at another path, but
not another file or version at the same one: after such an upgrade, configure
a preset's tree with `cmake --fresh --preset NAME`, or build it with
`build.sh`. An interrupt or termination of `build.sh` stops the build it runs
(its own process group) at once.

| Variable | Default | Effect |
| --- | --- | --- |
| `BUSTER_HOST_ARCH` | `native` | `-march`: `x86-64` for the scalar baseline, `znver5` for benchmark builds; `vector` is the explicit vector target, `-march=x86-64-v4` and host_isa.h's other features (`-maes -mgfni -mvaes -mavx512vbmi -mavx512vbmi2 -mavx512vpopcntdq -mavx512bitalg -mavx512vnni`), which must select `BUSTER_HOST_VECTOR=1` (#2033; the hosted vector row's) |
| `BUSTER_HOST_RUNTIME` | none | links CI's emulated old CPU's runtime from that root (`x86-64` alone) |
| `BUSTER_DEBUG_INFO` | `1` | `0` leaves `-g` out of Release, a fifth of its build time; Develop keeps its line tables |
| `BUSTER_LINKER` | `auto` | `auto` (wild, then mold, then clang's; for a static link lld, then mold, then clang's), `system`, `wild`, `mold`, `lld`, `bfd` or a path; lld links at `-O0` |
| `BUSTER_BUILD_DEFINES` | none | whitespace-separated arguments every unit's compile adds |
| `BUSTER_SANITIZER` | `none` | `address` (with UBSan and bounds), `undefined`, `thread`, `memory`; `thread`, `memory` and fuzzing builds link a PIE, the others at a fixed address and baked |
| `BUSTER_SANITIZER_OPTIMIZATION` | `3` | a sanitizer build's Release optimization, `1` to `3` |
| `BUSTER_FUZZ_AVAILABLE` | `0` | `1` adds libFuzzer and `--fuzz`, with AddressSanitizer unless a sanitizer is given |
| `BUSTER_FUZZ_ENTRY` | `fuzz.c` | the fuzzing entry's C file, with `BUSTER_FUZZ_AVAILABLE=1` alone |
| `BUSTER_BAKE` | `1` | `0` leaves the tables to be built when the executable runs |
| `BUSTER_PGO` | `0` | `1` builds Release with a profile of the compiler's own compiles (below): a `-pgo` tree, Release alone |
| `BUSTER_PGO_TRAIN` | none | with `BUSTER_PGO=1`, a bash script that trains the profile in place of those compiles |
| `BUSTER_PARALLEL_CODEGEN` | `auto` | CHECKER's and BACKEND's Release native code (a sanitizer build's too), and Develop's `main.c` units, in parallel parts (below): `auto` where the tools are, `1` requires them, `0` never |

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
- In Develop: `Develop/develop/UNIT/`, each `main.c` unit's parts (`partN.bc`,
  `partN.o`) and their join (`native.o`), and `Develop/develop/absent.o`, the
  stand-ins for the benchmark units it leaves out.

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
`unwind_initialize`, `tier1_select_initialize`,
`check_lower_runtime_names_initialize` (the lowering's runtime functions' names'
lengths and hashes, #442) and `check_asm_index_initialize` (the asm matcher's
index of every form, #2532) write is in a section of its own (`buster_baked`:
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

The eleven `main.c` units share the compiler interfaces and the fixtures
used across test families. Production bodies are limited to their consumers,
as described below. Independent test files are included only by the unit
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
the shared `c_checker_test.c` root rebuilds the checker semantic consumers
pinned in the ownership table. Unrelated units no longer include that root;
a configured fuzz DRIVER retains its helpers.

The backend similarly owns the private IR, interpreter, optimizer and Tier 1
fixtures. Edits to nested files such as `ir_simd_model_test.c`,
`interpreter_wide_test.c`, `ir_optimize_simd_test.c`, `tier1_call_probe_test.c`
and `tier1_address_coalesce_test.c` rebuild only `main_unit_backend.c`.
The optimizer roots `ir_optimize_test.c` and `ir_optimize_text_test.c`, including
their nested if-conversion and verified-pass fixtures, also belong to BACKEND.
`__builtin_convertvector`'s row producer, `c_check_program_vector_convert.c`, is
shared the same way: the checker reaches it through
`c_check_program_vector_builtins.c`, and BACKEND includes it beside
`c_check_program_vectors.c` and `c_check_program_x86.c` so that the IR decoder's
conversion tests (#1840) check the real producer. Production pass includes retain their positions within the units that consume
them, so narrowing the test roots does not reorder emitted code. Source
preparation and the BURS/selection helpers serve BACKEND and CHECKER. The
production IR and code-generation families are also guarded for their actual
consumers; their exact owner sets are pinned in the build regression. Narrow
semantic references retain production unused-function checking without emitting
code in units that omit the associated fixture callers.

The backend also owns the private Tier 0 fixtures, their nested tests and vector
baseline, the mask tests and the streaming tests and benchmark. Editing those
files rebuilds only `main_unit_backend.c`. `tier0_test_same_code` and its unwind
consumer belong to that same unit, so the entire `tier0_test.c` root is private.
Production `tier0_generate.c` and `streaming.c` follow the code-generation
consumer guards rather than making unrelated units parse their bodies.

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
correctness inventory preprocesses `main.c` (with `-DBUSTER_COMPLETE_DRIVER=1`)
and every `*_benchmark.c` as independent inputs and compares them with Clang. The build regression also preprocesses the
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

The preprocessor follows the same interface/body boundary. Shared state,
declarations, feature tables and reader helpers remain in `c_preprocessor.c`;
`c_preprocessor_impl.c` holds the private bodies, private tables and generated
predefines. `c_preprocessor_history.c` keeps the shared history reader and
diagnostic declarations; `c_preprocessor_history_impl.c` holds history writers
and diagnostic rendering. The literal evaluator in `c_preprocessor_literal.c`
is included through the private implementation. Ordinary unity builds read
these private files only in FRONTEND and keep the same externally emitted entry
points there. Standalone preprocessor, parser and files benchmarks keep the
complete source through the shared interface.

`executable.c` retains the writer's public types and declaration, section names,
hash helpers and unwind/debug readers. `executable_impl.c` contains layout,
patching, file output and the original `executable_write` body. DYNAMIC remains
the externally emitted writer's home; EXECUTABLE also reads the private bodies
for its direct tests, retaining the existing entry-point linkage. Other unity
units read the shared interface. The standalone executable, snapshot and link
benchmarks retain the complete implementation.

Source checking and code-generation dependencies are guarded separately.
CHECKER owns the source reader, graph checking, whole-program checking and
source lowering. BACKEND calls its existing `ir_source_prepare` entry through
a declaration, retaining the shared status, vector/x86 rows and frame verifier
needed by its own commands. CHECKER and BACKEND both read the code-generation
families; DRIVER retains only its existing shared IR counter and baked BURS
table definitions. CHECK_BUILTINS keeps semantic actions, constant helpers and
comparisons without the source reader or code-generation families. These
boundaries keep command homes, section placement and optimization settings
unchanged.
`BUSTER_COMPLETE_DRIVER`, which `main.c` defines, is DRIVER with the rest of
the compiler as well: the source, checker and code-generation families and the
private preprocessor and executable bodies, the closure `main.c` read before
these boundaries. A `BUSTER_FUZZ_AVAILABLE=1` build selects it, so both the
default callback and a `BUSTER_FUZZ_ENTRY` override keep their existing module
access; ordinary builds do not acquire that additional owner. The self-tests and
9700X benchmarks whose input is `main.c` (`tools/ci/correctness.tests` and
`9700x.yml`) pass `-DBUSTER_COMPLETE_DRIVER=1`, so that they still check, compile
and measure the whole compiler without changing any production object: with
`-march=native`, `main.c` preprocesses to 25.76 MB with it and 12.02 MB without
(25.75 MB before the boundaries). Pass it when running those commands by hand
too. The build regression checks that it keeps a definition from each block
that the ordinary driver leaves out, on both hosts.

The x86 target-form row enums, initializers and per-row policy assertions are
also parsed only by their data owner or a standalone consumer. Other units use
complete array declarations with two explicit bounds. The owner recomputes
those bounds from the canonical rows and asserts equality, so adding or removing
a row requires updating the interface deliberately. Editing row contents rebuilds
FRONTEND and `tier1_model_measure.c`, plus `tier1_select_benchmark.c` on a vector
host; changing a shared bound correctly rebuilds the readers too.

The ownership-only check pins production paths to their exact named unity
consumers, including configured fuzz access. The full build regression also
checks incremental fanout, including standalone consumers, and rejects both
kinds of target row-count drift. A new dependency cannot silently undo these
boundaries.

The [2026-11-10 validation packet](https://github.com/buster14a/buster-performance/tree/5223db00aaf609229db2d7911d70d1404bb1e774/docs/measurements/build-owner-boundaries-2026-11-10)
compares this ownership pass with merged revision `39506e86`. It reduces the
eleven unity units' preprocessed input by 43.7% for scalar and 44.0% for vector
builds. Actual Ninja plans for private preprocessor edits shrink from eleven
compilations to one on scalar, or fourteen to four on vector; private writer
edits shrink from eleven to two or fourteen to five. Shared-interface positive
controls retain their original consumers. All 86 Release objects and both
complete baked compiler executables are byte-identical to the baseline.
The packet includes the exact input manifest, candidate patch, dependency
captures and validation receipts. These are structural-work measurements;
approved Ryzen 7 9700X elapsed-time qualification remains pending.

The earlier parser/target-row [validation packet](https://github.com/buster14a/buster-performance/tree/f29dc2c6616fb0530a2b21b1555507da79f7ba25/docs/measurements/build-production-codegen-2026-11-09)
records the before/after dependencies, the parser definition and object
preservation checks, and the optional parallel-codegen contract and runtime
results below. That packet reduced ordinary unity expansion by about 35 MB
(10.6%) in each host configuration. Its elapsed-time observations are
development diagnostics; approved-host build and runtime measurements are still
required before changing the experimental mode's default.

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
Production implementation boundaries have separate exact path/owner rows even
when their filenames do not identify fixtures. A boundary may name several
consumers; missing, added or substituted owners all fail. Configuration-specific
access must come from the selected build's settings, not from the dependency
record being checked.

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

When splitting a production implementation, preserve its original definitions,
entry-point linkage and standalone access. Register the private file and its
private descendants in the ownership assertions, check the complete edit plan,
and update `tools/ci/changes.c` so runtime consumers keep their benchmark
selection. Its standalone benchmark traversal follows literal
`#include "buster/..."` paths; use that spelling for new implementation
includes, including nested implementation files. Compile the ordinary scalar
and vector consumers and any configured fuzz DRIVER after changing the guards.

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
build and test presets `debug`, `release` and `develop` (native),
`x86-64-debug`, `x86-64-release`, `x86-64-develop`, `znver5-debug`,
`znver5-release` and `znver5-develop`; the test presets `debug-build`,
`release-build` and `develop-build`, the `build` label alone; and the workflow
presets `debug`, `release` and `develop`, which configure, build and run those:

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

## Parallel native generation

A clean build waits for its longest unit: on the 9700X, `main_unit_checker.c`
took 99.6 s of a 100.3 s build at `fa6254e0`, with the other units done long
before. More than half of a unit's compile is native code generation after the
O3 IR pipeline: 57.3 of 112.5 s for the checker and 37.9 of 75.5 s for the
backend (`-ftime-trace`, locally), single-threaded in one Clang process. So the
Release configuration generates those two units' native code in parallel parts:

1. The unit compiles C through the ordinary O3 pipeline to LLVM bitcode
   (`-emit-llvm` in Release alone), keeping its depfile and its compilation
   database record. Clang's bitcode keeps the module's use-list order.
2. `tools/codegen/split.c` (`Partitioning UNIT's optimized IR`) writes eight
   parts of contiguous function ranges in module order, the costliest (by
   instruction count) as cheap as the alignment rule below allows, and a ninth
   with the unit's global variables: one per job of `build.sh`'s default, a
   physical core (fourteen, one per hardware thread of the 9700X, ran in two
   waves there: 74.0 s for a clean build against 73.1 s). It parses the module
   once and forks a child per part, as many at once as the CPUs it may use and
   the available memory allow: each child copies most of the module's pages
   (fifteen children of the checker came to about 14.8 GB, four to 4.7 GB).
3. A Clang backend job per part (`Generating UNIT's native part N`) compiles
   it with the unit's `-march`, debug information and O3 code generation, the
   IR optimizer disabled.
4. `cmake/BusterCodegenMerge.cmake` (`Combining UNIT's native parts`) joins
   them in order with a relocatable link and makes local again everything the
   unit didn't export, before the ordinary final link. It fails when any of the
   unit's local names (the split's `locals.txt`) is still undefined.

Each unit's parts start once its own split is done: with CMake 3.27 and later
these commands take `DEPENDS_EXPLICIT_ONLY`, without which CMake makes a
command the link uses wait for every unit's object (the backend's parts waited
for the checker's bitcode, 14.5 s, then competed with its parts).

The executable's code is the single object's: every function at the same
address with the same instructions. The split keeps what the code generator
saw. A function another part refers to stays local where it is defined, so its
jumps relax as before (a near tail call's two-byte `jmp`); a hidden alias,
`NAME.buster_split`, names it for the other parts. A code part reads strong
constants' initializers `available_externally`, so a string's bytes still fold
into immediate stores; every local global such an initializer names is a
hidden external one, since folding a pointer out of a table can name it too. A
local definition another part owns and this one doesn't use is an ordinary
external declaration, which emits no symbol: as a hidden one it would be an
undefined hidden symbol, which lld ignores and GNU ld refuses. The module's inline assembly, which places
`.text.buster_checker`, stays in the first part. A part starts, in each text
section (`.text`, a named section, a cold function's `.text.unlikely.`), at a
function of the greatest alignment it holds there, so that the relocatable
link adds no padding. The bitcode the parts are written as keeps its use-list
order: `split_bitcode.cpp` holds the calls to LLVM's C++ API that its C API
lacks (that writer, and erasing an alias). The merge clears the parts' allocated `SHF_MERGE` flags first: a
relocatable link merges those sections, which the final link at `-O0` copies
whole from the single object (the checker's parts lost 27,904 bytes of
duplicate constants that way). A final link that merges them itself, every
linker but lld at `-O0` (a sanitizer build's dynamic link by wild), merges the
single object's with the other units', so there the parts keep the flags: the
join reads `-DMERGES`, set where `-Wl,-O0` isn't. Read-only data keeps its order and each item's
alignment, but a part's `.rodata` takes its greatest item's alignment (64), so
items after a part boundary can move by less than that: 256 bytes in all for
the two units (768 with fourteen parts), with `.eh_frame` 16 bytes longer. Between a part's last function
and the next part's first, the linker's padding is `int3` where the assembler's
was `nop`; neither is executed.

On the 9700X, against the same tree compiled whole (#2056's merged head
`cd920bf1`), a clean build took 72.8 s against 100.3 s, and editing `main.c`,
`c_check_input.c` or `tier0.c` rebuilt in 69.0, 62.7 and 66.0 s against 96.2,
90.5 and 93.0 s (medians of 3). CPU time rose about 11%: every part reads the
module's declarations again. The produced compilers ran a `-c` of `sqlite3.c`
and a `-fsyntax-only` of the checker's unit within ±0.33% of each other, as
noise. The [record](https://github.com/buster14a/buster-performance/tree/dc6be30185f2c22b8944f24a02c092760b96cf7a/docs/measurements/build-parallel-native-2026-11-10)
has the runs, scripts and the comparison of the two executables' code.

`BUSTER_PARALLEL_CODEGEN` is `auto` by default: the build uses the parts
wherever its flags allow and the tools are there, and otherwise says why in the
configure step's `parallel native generation` line and compiles the units
whole. `1` requires the parts, failing the configuration without them; `0`
never uses them. Fuzzing, PGO, an explicit host runtime,
`BUSTER_BUILD_DEFINES` and lexer variants keep the single compile; sanitizer
builds take the parts ([below](#in-sanitizer-builds)). The tools,
beside the configured Clang or on `PATH`, at its version: `llvm-config` with
LLVM's C API headers and library (the splitter is built in the tree, at
`codegen-tools/split`, and Develop's at `codegen-tools/presplit`, with the
flags it gives), `clang++`, `llvm-nm`,
`llvm-objcopy`, `llvm-readobj` and `ld.lld`. LLVM's libraries link shared
where `llvm-config` finds them, else static, as the official release's
archives do in CI, hosted and on the 9700X. A system library `llvm-config`
names by a path the host lacks links by its name instead: the release names
Ubuntu's `/usr/lib/x86_64-linux-gnu/libzstd.a`, and the 9700X, an Arch host,
links `-lzstd`. The Debug configuration compiles
the units as before; the final link still uses any supported `BUSTER_LINKER`.

An unrelated edit reuses both units' native objects. A change to a split unit
compiles its bitcode and splits it again, but parts whose bytes didn't change
keep their timestamps, so Ninja repeats only the changed parts' code generation
(a mutable global's initializer changes the data part alone); a comment-only
edit that leaves the optimized IR unchanged generates nothing. Failed
splitting, native compilation or symbol restoration stops the graph before a
replacement executable can be published. Diagnostics produced only when
compiling inline assembly remain fatal under `-Werror`, but report the inline
assembly without the original C file and line.

`build-regression --parallel-codegen-only` exercises the production helpers on two
translation units with identically named private functions, data and TLS, distinct
hidden public entries, and shared strong and weak external references. It checks
runtime isolation across threads, the source compilation database, unchanged and
source-only edits, one-part private data edits, missing pieces, deterministic
regeneration, late assembly warnings and failed-stage recovery. It also checks
the mapping of `llvm-config`'s system libraries (`cmake -P`): hosted runners
have the release's paths, so only the 9700X's build maps one. The full build
regression also builds the real opt-in compiler once and runs its baking checks.

The parts are made after the optimizer, so a unit's `-O3` pipeline is still one
serial job. The Develop configuration ([below](#develop)) moves the split in
front of the optimizer, in a configuration of its own; Release keeps this one.
Release split before the optimizer as Develop is, the design's step 5, failed
its gate on the 9700X: `benchmark-assembler` ran 9.6% slower, and 4.6% after
five changes to the splitter, where its clean build took 77.6 s against 86.4 s
and an edit of `c_check_input.c` 32.5 s against 75.1 s ([A faster stage 1
build](../stage1-build-design.md#step-5-measured), #2559).

### In sanitizer builds

A sanitizer build (`BUSTER_SANITIZER=address|undefined|thread|memory`) generates
the two units' native code in parts too (#2639): its checker unit's single
compile was the whole build's wall time, and its CI rows build it once per
candidate. The sanitizers instrument the module inside the unit's own
optimization pipeline, before the split, and MemorySanitizer's cleanup passes
run there too, so the parts generate the single compile's instrumented, optimized
module, with the unit's optimization level and the sanitizer's flags (its
`-fPIE` among them). What a sanitizer adds to the module goes with the part
that emits it:

- **The module constructors.** `llvm.used` and the constructor and destructor
  arrays keep their entries in the part that emits the function they name, so
  that the constructor keeps its retained section (`.text.msan.module_ctor`,
  `"axR"`) and AddressSanitizer's its comdat with its `.init_array.1` entry.
  The split refuses arrays whose entries a part order would reorder, or whose
  key another part emits.
- **AddressSanitizer's globals.** Each instrumented global is in a comdat of its
  own, with its descriptor in `asan_globals` and a private alias the
  descriptor names: the data part keeps all three, no declaration keeps a
  comdat, and a code part drops the aliases (the split fails for one it still
  refers to).
- **The comdat flags.** `llvm-objcopy`, which the join runs to clear the merge
  flags and to localize names, also clears `GRP_COMDAT` on every group whose
  signature is local, as the module constructor's is. The join's last step,
  `codegen-tools/comdat` (`tools/codegen/comdat.c`), sets it again on the groups
  the parts had as comdats: without it the executable ran each split unit's
  AddressSanitizer constructor beside the first unit's.

`build-regression --parallel-codegen-only` builds and runs the codegen fixture
under AddressSanitizer and MemorySanitizer and checks that the joined objects
keep the comdat constructor (one in the executable) and the retained one. A
sanitizer tree's Develop units still compile whole (below). `BUSTER_DEBUG_INFO`
changes no sanitizer build: its units have line tables either way, in the same
tree.

**Measured** on the 9700X, clean builds at `-O3` with the host's clang 23.1.1,
whole against in parts ([the record](https://github.com/buster14a/buster-performance/tree/b6b0666ca131c5ab26cec4b085878d8fcd7e0b54/docs/measurements/sanitizer-build-parts-2639-2026-11-13)):

| Shape | Build | Whole | In parts |
| --- | --- | ---: | ---: |
| 8 cores, 8 jobs | native MSan | 346.5 s | 188.7 s |
| | native ASan | 282.7 s | 199.6 s |
| | native TSan | 117.4 s | 81.5 s |
| 2 cores of 2 threads, 4 jobs (a hosted runner's, as CI builds there) | x86-64 MSan | 314.8 s | 258.2 s |
| | vector ASan | 365.3 s | 367.1 s |
| the same, 2 jobs (`build.sh`'s default there) | x86-64 MSan | 312.7 s | 324.5 s |
| 4 cores, 4 jobs | x86-64 MSan | 266.9 s | 186.5 s |

The checker's unit took 345.7 s whole under MSan, 318 s of it compiled alone:
the front end 6.8 s, the optimizer 86.5 s (the MSan pass 2.0 s) and code
generation 224.6 s, which the parts divide; its bitcode compile, 130.5 s in the
build, is now the critical path. The parts add 5% to 13% of user time, which a
build bound by its work pays: 2 jobs on 2 cores, and the vector ASan build,
whose benchmark units are 29% of its work. The executables are the whole
builds' function for function, every function at one offset from where it was
(the joined parts' merged constants move `.rodata`'s end by 50 to 66 KB), with
the same instructions but for the padding between parts and, in the
fixed-address ASan executable, the moved data's addresses.

## Develop

The edit loop's configuration (#2559; [A faster stage 1 build](../stage1-build-design.md)
is its design, with the measurements that sized it). A Release edit waits for one
clang process, a unit's front end and `-O3` pipeline, whatever the edit: the
checker's unit's took 56.6 s on the 9700X. Develop stops each `main.c` unit's
compile before LLVM's optimizer and optimizes it in parts, at once, so that an
edit repeats only the parts it changed:

```text
UNIT.c --clang, no LLVM pass--> UNIT bitcode --presplit--> part0.bc ... partN.bc, the data part
partK.bc --clang -x ir -O3--> partK.o      (a job a part, rerun only when its bitcode changed)
part*.o --join, as Release's parts--> native.o --link--> Develop/compiler.link --bake--> Develop/compiler
```

1. The unit compiles with Develop's flags and `-Xclang -disable-llvm-passes
   -emit-llvm`, keeping its depfile and its compile database record.
2. `tools/codegen/presplit.c` (`Partitioning UNIT's IR before the optimizer`)
   deals its functions into parts (`BUSTER_DEVELOP_PARTITIONS` in
   `cmake/BusterCodegen.cmake`: 48 for the checker's unit, 32 for the
   backend's, 16 for the front end's, 8 for each other one) and writes a part
   only when its bytes changed, as `split.c` does.
3. A job a part (`Optimizing and generating UNIT's part K`) runs clang's `-O3`
   pipeline and code generator on it, with the unit's `-march` and Develop's
   debug information.
4. `cmake/BusterCodegenMerge.cmake` joins them as it joins Release's parts.

The splitter's rules, each for a measured reason (the file's first comment has
them in full; `--report` prints a plan's parts, files and largest functions):

- **A function's part comes from its file and the names around it**, not from
  a count of what came before. Each file's functions, in source order, are cut
  at the functions whose name's hash is 0 modulo 24, and dealt over a run of
  parts of the file's own by their place in the file, as many parts as the file
  holds of half a part's mean size. An appended function changes one part; an
  edit that moves a file's lines changes that file's parts.
- **A static function that one function alone calls goes with its caller**,
  where the caller can inline it, so that it still can: not into an `optnone`
  caller.
- **A part reads the bodies it may inline**: those of at most 100 unoptimized
  instructions that its code calls and another part defines, and every
  `always_inline` one, `available_externally` and without their line numbers.
  Without them the compiler was 29% slower; with them, 1% to 2%.
- **A part reads the constants it refers to**, as Release's parts do, and a
  static variable more than one part uses lives once, in the data part.
- **Nothing in a part names the rest of the unit**: string literals and
  numbered statics are named by their contents, inline assembly loses its
  `!srcloc`, struct types lose their names (`struct.anon.N`), and DWARF 4
  records no file's checksum.
- **A function whose inline assembly has text stays in part 0**, with the
  module's own assembly, and no other part reads its body: its text can define
  a label another function's names.
- **The tests' own functions are `optnone` and `noinline`** (`--tests-optnone`),
  as #681's pragma has the checker's: a function whose file's name has `_test_`,
  `_test.`, `_tests.`, `test_` or `fuzz` in it. No source changes, and no
  optimizer time on the tests; their commands run a few percent slower.
- **The same input gives the same parts**, whatever the jobs: a rebuilt
  executable is the one a clean build of the same sources makes.

**The benchmark units are built on demand**: Develop's executable leaves out
the 27 `*_benchmark.c` units that `BUSTER_DEVELOP_ON_DEMAND`
(`cmake/BusterSettings.cmake`) names, whose only entry is a benchmark command's
`run_*` function and that no test runs: the correctness inventory runs none of
them. Their object
libraries have no source in Develop, so Develop's executable doesn't wait for
them, and `cmake/BusterDevelopAbsent.cmake` writes `Develop/develop/absent.o`
from their sources, a stand-in for each `run_*` entry, which says that the
command is Release's and Debug's and fails. `floors_benchmark.c` also holds
`test-floors-input`'s tests and stays, as does `c_preprocessor_benchmark.c`,
whose `benchmark-preprocessor` `test-preprocessor` runs for its location
option, and `tier1_model_measure.c` and the two test units. A test that comes to
run another benchmark command needs its unit taken off the list.

**Measured** on the 9700X, eight jobs, the host's clang 23.1.1 ([the record](https://github.com/buster14a/buster-performance/tree/ba33a36de7aecf915f9ffceb48ea9d5cf666ae8a/docs/measurements/develop-configuration-2559-2026-11-12)):

| Scenario | Develop | Release |
| --- | ---: | ---: |
| Clean build | 33.8 s (265 s of CPU) | 84.3 s (494 s) |
| Nothing changed | 0.04 s | 0.03 s |
| A function appended to `c_check_input.c` (1 part job) | 13.6 s | 73.7 s |
| One before `c_check_input.c`'s first line (7) | 15.4 s | |
| `differential.c` (1) | 7.3 s | |
| `tier0.c`, which two units read (2) | 14.9 s | |
| `c_parser_impl.c` (1) | 4.9 s | |
| `host_isa.h`'s time alone (0) | 11.7 s | |

After a series of eleven edits the rebuilt executable is a clean build's; the
correctness inventory passes on Develop's executable (with CI's toolchain, at
`znver5`); it writes the same objects and preprocessed output as Release's on
`sqlite3.c` and the compiler's own units; the record's 21 test commands take
47.3 s against Release's 43.3 s (+9%); and the compile path runs 1.9% more user
cycles (`-c sqlite3.c`, `-fsyntax-only` of the checker's unit, one placement).
A clean Develop build is bound by its work on eight jobs; an edit by the edited
unit's front end (6.6 s for the checker's unit), its split and its largest
changed part.

**What it costs.** Develop's executable runs the compile path 1% to 2% slower
than Release's (above) and writes the same outputs; its debug
information has functions and lines, not variables, so `perf` and backtraces
work and a debugger's `print` doesn't (Debug is for that). Release and Debug are
untouched: their commands are the same, and so are their executables, byte for
byte.

**When it compiles whole.** Develop's units are split where Release's parallel
native generation is allowed and its tools are there (`BUSTER_PARALLEL_CODEGEN`,
above: the configure step's `Develop configuration: TRUE, in parts: ...` line
says which). Otherwise, and in
sanitizer, fuzzing, `BUSTER_BUILD_DEFINES` and lexer-variant trees, they compile
whole with Develop's flags; a `BUSTER_PGO` tree has Release alone.

**Checks.** `build-regression --develop-only` (and the full regression, in the
Build workflow) builds Develop beside Release, checks that Release's executable
doesn't change and that switching builds nothing, that a benchmark command names
Release (a scalar host's executable refuses every benchmark command first, so
there the stand-in's symbol is checked), that both executables write the same objects at both tiers, that a
function appended to `c_check_input.c` optimizes one part of the checker's unit
and one before its first line a few, that a header's time alone optimizes none,
and that the executable after those edits is a clean build's, byte for byte.

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

## The start's code order

A small compile's code is spread over the executable: on the 9700X, an empty
file's `-fsyntax-only` ran 185 functions in 52 of `.text`'s 64 KB windows, the
stretch the kernel maps at a fault on a file's pages (fault-around), and
`hello.c`'s `-c` 532 in 106 (#1444). Each window it touches is a fault. So
where lld links the static executable, the functions that small compiles run
go first, together, in a section of their own before `.text`
(`.text.buster_start`), and the C library's ones right after it, first in
`.text`. The checker's own code stays in its section, `.text.buster_checker`
(one section of each unit, `main.c`'s pragmas, #502), which holds 18 of the
empty file's windows and 60 of `hello.c`'s.

- **The list:** `cmake/BusterStartOrder.txt`, the functions in the order the
  profiled build had them, `UNIT NAME` for a local function of one of
  `main.c`'s units and `- NAME` for any other: a global one, whose name is the
  link's alone, or the C library's.
- **Each unit's copy apart:** every unit compiles a copy of the static helpers
  it uses, and a symbol ordering file, which names symbols, moves every copy:
  one built so faulted more (75 times against 69 in `.text`, #1444's opening
  report). So the units are compiled with `-ffunction-sections`, and the link
  script (`cmake/BusterStartOrder.cmake` writes `start-order.ld` into the
  tree at configuration) names each function by its unit's object, the unit's
  own (`*/UNIT.c.o`) or its parallel parts' join (`*/UNIT/native.o`), and its
  section (`.text.NAME`, `.text.unlikely.NAME`, `.text.hot.NAME`). The
  others go by name in a symbol ordering file (`start-order.symbols`), which
  puts their sections first in `.text`, right after; `start-order` leaves out
  any whose name a unit's static shares, which the name would move too.
- **Shifts:** a shifted link's padding (`.text.buster_shift_padding`, above)
  goes in `.text.buster_start` right before the shift source's first
  function, so that the shift moves that unit's code there too.
- **Where:** a static link by lld (`start order: TRUE` in the configure
  step's status line). The sanitizers', fuzzing and `BUSTER_HOST_RUNTIME`'s
  dynamic links, and the other linkers, keep their layout and compile without
  `-ffunction-sections`. `-ffunction-sections` alone made `.text` 6 KB longer
  (0.02%): a jump to another function's section can't take the assembler's
  short form.
- **A stale list:** a function that is gone, renamed or no longer an object's
  matches nothing and stays out; a link whose list matches nothing at all
  fails (the script's `ASSERT`), which a changed object path would make.

On the 9700X, the empty file's `-fsyntax-only` then took 54 faults in the
executable's code (44 in `.text`, 10 in `.text.buster_start`) against 67, and
`hello.c`'s `-c` 90 against 114, with 18% to 33% fewer iTLB misses in the
small compiles. With the same change's `.bss` advice and token columns, they
ran 4.1% to 5.8% faster from a cold start, a file-first `-c` 8.7%, while
`main.c`'s `-fsyntax-only` and `sqlite3.c`'s `-c` stayed within their floors
([the record](https://github.com/buster14a/buster-performance/tree/0880281cac17cceb9749d19b1554873e51028928/docs/measurements/start-order-1444-2026-11-13)).

Refresh the list when small compiles' code has moved, from a Release build of
the tree as the benchmark builds are, on the 9700X under its lock: sample each
control (`perf record -b -e instructions:u -c 4999`, a few hundred processes
of each of an empty file's `-fsyntax-only` and `-E -P`, `tiny.c`'s `-c` and
`hello.c`'s `-c`, `perf script -F ip,brstack` of each), take the executable's
symbols (`readelf -sW`), then write the list with `start-order` below the
header, which names the revision, the host and the commands:

```sh
./build/compiler start-order symbols.txt syntax.txt preprocess.txt tiny.txt hello.txt > body.txt
```

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
