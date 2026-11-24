# compile-time

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `--time-stages` (`c_check_clock.c`, #518) makes the C driver report, on standard error after the command, each
  stage's milliseconds (startup, preprocessing and its phases, parsing, checking, lowering and its phases (#1048),
  Tier 0, branch sizing, encoding, the object's build and write, the direct path's runtime, binding and write, with the write's phases on
  `time-write` lines (#778), the release; on several lanes their run is one stage, `lanes`), with its system time,
  page faults and peak resident set added, and the process's resource use, with the lanes its session asked for and
  ran on (#905: `lanes-asked` and `lanes`), whether transparent huge pages were off for it (#1109: `huge-pages`, `off`,
  `busy` or `system`; `lane.c`'s `arena_page_policy` turns them off on a virtual machine or by `BUSTER_HUGE_PAGES=0`,
  `BUSTER_HUGE_PAGES=1` leaves them as the system has them, and `busy` means the process's arenas alone took base pages
  on a host busy when it started, #672: its tasks runnable at least a quarter of its CPUs online), how many of the
  command's arenas are warm (#2850: `warm-arenas`, 2 with `BUSTER_ARENA_POOL` naming a directory whose file it could
  claim, [arena memory](../arena-memory.md#warm-arenas-2850)), how many of its
  units were preprocessed a second time for their expansion history (#2850: `history-replays`; a unit that says nothing
  must read 0, since only a diagnostic or a warning's test of a macro asks for the history), the tiles its parses
  took and the units a tile sent to be parsed as one (#2536: `parse-tiles` and `parse-tiles-refused`, for a unit parsed
  by tiles, `--parse-tile=N`), and with
  `--check-transitions` the memoized transitions'
  hits, misses, folds and entries (#1050: `time-check transition-*`); off by default, its laps test a null pointer.
  `measure-compile [--runs N] [--reference PATH [--reference-option OPTION]... | --variant OPTION]... -- ARGUMENT...`
  times `ARGUMENT... -o FILE` as whole processes, with and without the option and against up to four references:
  compilers (tcc, or clang and gcc with `--reference-option -O0`, #146), or with `--variant` this compiler with OPTION
  before the arguments (#172's `--variant --tier1-schedule=post`), in a Williams design's order, with each process's
  user cycles and instructions where perf_event_open allows them, each compiler's time and cycles per preprocessed token
  (the report's `time-process tokens`), each stage's medians with its TSC ticks and core cycles per token, and the
  lowering's coverage; the 9700X job runs it on
  `sqlite3.c` against tcc, clang -O0 and gcc -O0 (those two unpinned, their versions printed), with perf's counters and profile, where `changes`' `compile_time` says. [The compile-time
  guide](../../docs/compile-time.md) has the attribution. `test-checker-driver --executable` checks both.
  With `--instructions-baseline FILE [--instructions-tolerance PARTS]` (#2850) the default compile's median user
  instructions are held to the count FILE states (its first line that is no `#` comment, a decimal number alone): the
  command fails where they are more than PARTS parts a million of it away, above or below (5,000, half a percent),
  where FILE states no count, and where the host counts no instructions. It prints the count to write where a change
  means to move it. The 9700X job's last measurement runs it on `sqlite3.c` against
  `tools/ci/compile-instructions.txt` ([CI](ci.md)); a test's switch, `BUSTER_TEST_MEASURE_INSTRUCTIONS=N`, gives the
  count in the median's place.

With `-flazy-system-headers --time-stages`, each planned translation unit also
prints `time-lazy planner=eager|tree|bindings tokens=N scalar-replay-tokens=N
bodies=N selected=N deferred=N skipped=N references=N retained-bytes=N` (#1364); `skipped` counts the deferred
definitions whose whole headers the tree planner skipped too (#3307).
`eager` proves the existing opaque-reference policy requires every body and
allocates no plan. `tree` certifies the completed syntax tree without a second grammar traversal;
`bindings` is the exact fallback for inputs the tree certificate cannot prove.
The planner's time remains part of `check-input`. Compare lazy and eager whole
processes on the approved 9700X, retaining these coverage counters alongside
the timings; do not infer a speedup solely from a zero replay-token count.
