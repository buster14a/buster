# tier1

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- Tier 1 selection publishes effective uses for `tier1_liveness.c`; the native
  consumer connects required locations, allocation, frames and spill/remat
  records. Its vector path covers the integer core by bottom-up rewriting over
  state tables `tier1_select_initialize` compiles from a grammar of the
  candidates' patterns (`tier1_select_burs.c`, baked; labeled 16 nodes a depth
  level), maximal munch its scalar twin and its fallback; `test-tier1-select`
  checks the tables and the kernels' twins, which `test-tier1-pipeline` and
  `benchmark-tier1-select-source` check on every function they plan. Bounded
  direct scalar calls preserve live caller registers and publish symbolic CALL
  references. Byte/word memory and integer conversions
  use canonical 32-bit storage; terminal chains retain explicit traps or empty
  self-loops. Every other function, general CFGs with loops and phis, goes
  through `tier1_emit_cfg.c` (#140): `tier1_cfg.c` allocates it and its edges
  move values, before a jump, at a successor's start or in a split critical
  edge's stub. A straight line whose allocation can't place a value (an evicted
  parameter whose incoming register another operand holds) goes through the CFG
  consumer too. The 128-bit integers are split into 64-bit halves before the
  optimizer (`ir_wide.c`, #141): `test-tier1-wide [--seed N] [--programs N]
  [--print] [--file IR]` runs hand-written and random 128-bit IR, and Tier 0's
  random 128-bit programs, through the split by both paths, the optimizer and
  both of Tier 1's paths natively, against the IR's interpreter on the unsplit
  function and the programs' stack machine; and Tier 0's random long double
  programs, whose values the CFG consumer keeps in frame objects and computes on
  the x87 stack (`TIER1_RULE_X87`, #141), natively against their stack machine.
  The convention's rarer aggregate shapes are taken; those of vectors still
  refuse explicitly ([the Tier 1 guide](../../docs/tier1.md)'s list). AVX-512's
  masks (#179) are selected (`kmov`, `kand`, `kor`, `kxor`, `knot`, and loads,
  stores, constants and bitcasts to and from integers of every width) and
  allocated in the opmask class by the CFG consumer, which moves and spills them
  at their widths and saves them around calls; and vectors (#141), in the vector
  class at their widths, passed and returned by value as aggregates in a vector
  register whole; at a target without AVX512VL (x86-64-v3), by VEX forms in xmm0 to
  xmm15, machine rows' roles below 16 too (#1281). Run
  `test-tier1-select`, `test-tier1-liveness`, `test-tier1-emit`,
  `test-tier1-pipeline --cross-check` and `test-tier1-masks` (hand-written mask and vector IR
  natively against the IR's interpreter; Tier 0's mask programs and every `_k*`
  builtin's row through the IR, SSA and the optimizer to Tier 1's native code) for bounded typed-stream/native
  validation; `benchmark-tier1-pipeline [--clang PATH] [--repeat N]` times the
  pipeline's fixtures by Tier 1, Tier 0 and clang `-O0`/`-O2` (bounded kernels,
  not programs; under the 9700X's lock, CPU 2), and `benchmark-tier1-select-source
  [--only select|select-scalar] [-E's options] file...` selection's core cycles
  per IR instruction on real C's functions, as `--tier1` prepares them, by both
  paths (#141). A division by a constant that isn't a power of two is a
  multiplication by its reciprocal (Granlund and Montgomery), in either consumer,
  which saves the values `mul`'s `rax` and `rdx` would clobber (the [selection
  guide](../../docs/tier1-selection.md)). `measure-mask-spills` times a mask's spill and
  reload, to memory against to a general register (`mask_spill_measure.c`, #179's
  measured choice; the 9700X job runs it, `test-mask-spills` checks its programs).
  `test-tier1-model` checks the emitted-case measurement reader;
  `measure-tier1-model --manifest PATH --verify` constructs its eligible
  experiments without reporting timings. See [the Tier 1 guide](../../docs/tier1.md)
  for supported inputs and the complete C-source acceptance still pending.
- `measure-tier1-programs --clang PATH [--tcc PATH] --sqlite DIR --speedtest DIR
  --lua DIR --scripts DIR [--runs N] [--warmups N] [--directory DIR] [--keep]
  [--uncounted]` (`tier1_programs_measure.c`, #138 and #1189) builds the
  programs Tier 1 is measured by against clang -O2: speedtest1 (DIR/sqlite3.c,
  DIR/speedtest1.c, #136's flags and options) and Lua 5.4.8's interpreter (its
  33 sources) running the five scripts of the `cc3387f` record. Each is built
  by this compiler's Tier 0 and `--tier1 -fno-asynchronous-unwind-tables`, clang
  -O0 and -O2, and tcc (left out without `--tcc`), as that record's `corpora.sh`
  builds them: speedtest1 as one translation unit but by clang, which compiles an
  object of each file and links them. It checks that speedtest1's `--verify`
  gives one hash and each script prints clang -O0's output, then runs every
  build of every program in `--runs` rounds (11) of a rotating order, as whole
  processes (`profile.c`'s), each with its wall time and its user cycles and
  instructions, every speedtest1 run from a fresh database: its files and
  SQLite's temporary directory removed before it, outside its time, as the
  record's runs were. It prints a tab-separated `tier1-programs-run` line per
  run (its phase, round, position, program, build, wall time, counters, status
  and whether it was counted), each pair's medians, then a table of
  speedtest1's and Lua's (the sum of the scripts' medians) by build, with
  `--tier1` against clang -O2 and -O0. Every run must exit 0 and be counted
  (`--uncounted`, for a host without the counters, skips the latter). The
  caller pins it; the 9700X job runs it under the `tier1_programs` gate (Tier
  1's input and pipeline: the checker's lowering, the emission's Tier 1 step,
  the IR's passes and Tier 1's own modules; and the measurement: its harness,
  `profile.c` and `compile_measure.c`), and on its daily and dispatched runs,
  with the scripts from a pinned buster-performance commit and the other inputs
  by their hashes. Against the record, its clang builds link with clang rather
  than `cc`, and each run's counters are its own rather than `perf stat`'s of
  one run per build. `correctness.tests` checks its dispatch on every run: an
  incomplete command prints its usage, and one without its inputs fails its
  first build.
- `measure-tier1-rows --clang PATH [--base COMPILER] [--src DIR] [--lexer
  FILE]... [--march ARCH] [--rounds N] [--passes N] [--runs N] [--warmups N]
  [--directory DIR] [--keep] [--uncounted]` (`tier1_rows_measure.c`, #1283)
  times vector code that machine rows run, by this compiler's Tier 0 and
  `--tier1`, clang -O2 and, with `--base`, another compiler's `--tier1` (the
  pull request's base's): `test-checker-driver`'s machine rows' unit (its other
  unit's rounds, `--rounds` of them), a lexer's byte classes over a 1 MiB
  C-like text by byte vectors and compares into masks (`--passes` times), and,
  with `--lexer`, the compiler's own lexer (`--src`'s
  `buster/compiler/c_lexer_benchmark.c`, linked with a main by clang) running
  `benchmark-lexer` on those files. Each unit is built by the build's compiler
  and each main by clang -O2, at `-march` (native by default). Tier 1's builds'
  `--tier1-report` gives each unit's records, machine rows and the moves around
  them (`row_moves`; `none` from a compiler before #1283). Every program's output
  must be the same by every build (the lexer's corpus check and tokens, and its
  exit status, which says its vector lexer agreed with its scalar one), and the
  lexer's `c_tokenize` median is printed; then the runs, as
  `measure-tier1-programs` times its own (`tier1-rows-run` lines), each pair's
  medians, the counts' table and the medians' table with `--tier1` against Tier
  0 and the base. The 9700X job runs it under the `tier1_programs` gate, with a
  pull request's base built in a worktree; `correctness.tests` checks its
  dispatch on every run.
