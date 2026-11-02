# build

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `build.sh` invokes Clang and produces **only** `build/compiler`, using
  `-march=native`, so the machine executing the build determines the host ISA.
  The benchmark is linked into this executable rather than built separately.
  It compiles the translation units in a clang process each, one per core of
  the CPUs it may run on at a time (`cores`), the longest first (`order`, by
  their times on the 9700X: a new source goes where its time puts it, and the
  build stops at a source that `order` doesn't name), and links them in the
  sources' order: the same executable as one clang command for all of them.
  Two compiles on one core's threads slow each other down, the longest among
  them: on the 9700X before the homes (`main.c`), its 15 threads compiling at
  once took 40.0 s and 331 s of CPU, and its 8 cores one compile each 32.3 s
  and 233 s; with the homes, one compile per core takes 25.3 s and 200 s, the
  cores' work rather than the longest unit's. It reports how long the
  compiles took, and the three that took longest.
  `BUSTER_DEBUG_INFO=0` builds without `-g`, which took a fifth of the build's
  time; the 9700X job builds so, since nothing there reads it (clang's code
  then differs in a few functions' stack slots). The checker's unit takes
  longest: `check` and the default driver, `test-checker`, and its other
  commands each reach the whole compiler, and before the homes a unit for any
  one of them took 84 to 88% of its time (clang 18, locally).
  The executable is linked at a fixed address (`-no-pie`), from the same code:
  as a PIE, its pointer tables' 366,684 relative relocations made every start
  take 8 to 13 ms of page faults, which `link-foreign` and the tests pay per
  process.
  Each executable it links then writes itself with the backend's tables built
  (`bake-tables`, `baked.c`, #590): the state that `x86_encoder_initialize`,
  `x86_semantics_initialize`, `x86_zen5_initialize`, `tier0_initialize` and
  `unwind_initialize` write is in a section of its own (`buster_baked`:
  `BUSTER_BAKED` for state main.c's units share, `BUSTER_BAKED_UNIT` for a
  module's statics, baked in the checker's unit, the C driver's), so a compile
  starts with them built; the file's build ID is recomputed over the baked
  bytes. A new table those initializers write goes there too: the bake fails
  the build when they write anything outside it, including when its own
  tracking arrays fill. PIEs (the sanitizers' builds) and
  `BUSTER_BAKE=0` build them when they run; `test-baked-tables` checks either
  kind and fails, rather than skips, when the section is missing (the
  compile-time guide's "Baked tables").
  `./build.sh shifts DIRECTORY SOURCE SHIFT...` also links the same objects
  into `DIRECTORY/compiler-SHIFT` for each SHIFT, with that many bytes of
  padding in `.text` before SOURCE's code, for `time-runs` to compare builds
  over their code's placements (#509, "Comparing two versions" in
  `docs/profiling.md`).
- `BUSTER_PGO=1` builds `build/compiler`, and `shifts`' executables, with a
  profile of the compiler's own compiles (#880). First the sources are
  compiled with `-fprofile-instr-generate` into an executable in a temporary
  directory. It is left unbaked, since its counters are writable memory
  outside `buster_baked`, which `bake-tables` refuses. That executable
  compiles every source as `-E -P`, `-fsyntax-only` and `-c`, one process per
  core. The `llvm-profdata` beside `clang` (or on `PATH`) merges the counts
  into `build/compiler.profdata`, which stays beside the executable, and
  every source is then compiled with `-fprofile-instr-use`.
  `BUSTER_PGO_TRAIN=SCRIPT` replaces that training with a bash script of
  your own: it runs in a directory of its own, with `BUSTER_PGO_COMPILER`
  naming the instrumented executable. On the 9700X, the compiler's own
  sources gave a better profile than the frozen `sqlite3.c`, musl and
  c-testsuite of buster-performance#182 ("The compiler's own code and a
  profile", `docs/compile-time.md`).
  The mode isn't the default, and no CI job builds with it. Sanitizer builds,
  `BUSTER_HOST_RUNTIME` and `lexer-variant` refuse it; the lexer variants
  change `main.c`'s code, so a profile of the build without them no longer
  matches it.
