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
  triple as definitions, `-Wno-unused-function -Wno-psabi` for a scalar host,
  the sanitizer and fuzzing flags, `-O3` (or the sanitizers' level), then
  `BUSTER_BUILD_DEFINES`. CMake's own defaults take no part: no `-DNDEBUG`
  (`assert` stays), no other optimization, no PIE, and `CFLAGS` and `LDFLAGS`,
  which CMake would read once and keep, are ignored as before.
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
| `BUSTER_LINKER` | `auto` | `auto` (wild, then mold, then clang's), `system`, `wild`, `mold`, `lld`, `bfd` or a path |
| `BUSTER_BUILD_DEFINES` | none | whitespace-separated arguments every unit's compile adds |
| `BUSTER_SANITIZER` | `none` | `address` (with UBSan and bounds), `undefined`, `thread`, `memory`: a PIE |
| `BUSTER_SANITIZER_OPTIMIZATION` | `3` | a sanitizer build's Release optimization, `1` to `3` |
| `BUSTER_FUZZ_AVAILABLE` | `0` | `1` adds libFuzzer and `--fuzz`, with AddressSanitizer unless a sanitizer is given |
| `BUSTER_FUZZ_ENTRY` | `fuzz.c` | the fuzzing entry's C file, with `BUSTER_FUZZ_AVAILABLE=1` alone |
| `BUSTER_BAKE` | `1` | `0` leaves the tables to be built when the executable runs |
| `BUSTER_PGO` | `0` | `1` builds Release with a profile of the compiler's own compiles (below): a `-pgo` tree, Release alone |
| `BUSTER_PGO_TRAIN` | none | with `BUSTER_PGO=1`, a bash script that trains the profile in place of those compiles |

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
editors and static analyzers read (Release after a plain `./build.sh`).

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
`BUSTER_HOST_RUNTIME`'s explicit runtime stay dynamic, and the units see which
it is as `BUSTER_HOST_STATIC`. A static executable's `dlsym(RTLD_DEFAULT, ...)`
finds nothing, so the host functions that the interpreter's programs and the
tests' libm oracles name come from `test_libm.c`'s table there, and a `dlopen`ed
library brings a C library of its own. The compiler needs no libm either way
(`host_math.h`); a dynamic link takes it only as needed.

The post-link step has each executable write itself with the backend's tables
built (`bake-tables`, `baked.c`, #590): the state that `x86_encoder_initialize`,
`x86_semantics_initialize`, `x86_zen5_initialize`, `tier0_initialize`,
`unwind_initialize` and `tier1_select_initialize` write is in a section of its own (`buster_baked`:
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
the settings that change one step, the presets, the print commands, shifts and
a lexer variant: what runs, what is published and what is left behind, not how
long it takes. Its `clean --all` check removes every tree under `build/cmake`
first. The build's clean, unchanged and edited timings come from the
9700X under its lock, with their raw records in
[buster-performance](https://github.com/buster14a/buster-performance).
