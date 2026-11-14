# function-lanes

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Function lanes

`check_program_emit_lanes` (`c_check_program_emit.c`) is #151's emission on
lanes: each function's lowering (`check_lower_part`, `c_check_program_lower.c`),
Tier 0 code, branch sizing and encoding run as lane items, in parts of
consecutive bodies cut by their tokens and taken costliest first; then the
calling thread adds every function's atom, symbols and fixups to the link in
source order. A part takes source order's steps in source order's order, and
the failure reported is source order's (the earliest step's, its first
body's), whatever the lane count. The lowering numbers the runtime's
functions (libgcc's) and the label symbols part by part, and
`check_lower_finish` puts them in source order. Lane 0 is the calling thread
with the caller's arenas but for its writer's columns, which take the first
lane's scratch in base pages (`CheckEmitRun.columns`, #531), reserved for
exactly their room (`check_lower_writer_bytes`), or the caller's scratch where
that address space can't be reserved; each other lane
has its own (`CheckProgramLanes.arenas`), which the caller keeps until it is
done with the link. The lanes: one, and one more per `BUSTER_CHECK_EMIT_LANE_TOKENS` of the
bodies' tokens beyond twice the largest body's, no more than the bodies or
the caller allows (the driver: one per core of the CPUs it may run on, or
`--lanes N`). In
the C driver they are its session's (#905): every lane runs the command's flow
(`check_driver_flow`), lane 0 the unit's stages alone, and an emission that
pays for more than one lane is a round of the flow (a `LaneFlow`, `lane.c`),
which lane 0 announces and every lane runs, the first of them the bodies'
parts, a direct call (`check_program_emit_round` and
`check_program_emit_follow`); the worker budget's slots are taken
before the round, without waiting, and given back after it, and a team the
jobserver left short of its lanes grows as other recipes give tokens back,
lane 0 admitting the round's sleeping lanes, and takes every part's lowering
before any part's code (`CheckEmitGrowth`, #673). Elsewhere
(`CheckProgramLanes.flow` null) the emission's lanes are a `lane_run`.

`./build/compiler test-check-lanes [--executable PATH] [--seed N]
[--programs N] [--id-order N] [--keep]` emits generated programs of up to 600
functions (of libgcc's functions in random first-use orders, computed gotos
through static tables of labels' addresses, switches, structs by value, long
doubles, variadic calls) and failing programs on one lane and on 2, 3 and 8 lanes, on
8 with a slow lane, and on 8 that can't be pinned (which `lane_run` runs, then
counts as lanes that didn't start), by both paths, with `-g`'s rows and unwind
tables and without: each must be the one lane's object or failure, run again
on one lane only when its lanes didn't start, and each compiled again with its
identifiers' ids in another order (#153, the driver's `--id-order`) and emitted
on one lane and on 8, the first compile's. Then the serial retry's other
causes, each emission one lane's: a lane short of room
(`CheckProgramLanes.squeeze_body`: the lane that takes a body's part left only
so many bytes in one of its arenas, from none up, doubling, until the part
fits), whose sweeps must reach each reason (the lowering's, Tier 0's and the
code's after it), and lanes that start no thread, in a child that
`RLIMIT_NPROC` limits (not as root); then in a child whose address space
`RLIMIT_AS` limits, after an emission whose lane 0's columns must take exactly
their reservation, emissions with less room than that reservation maps (lane
0's columns in the caller's scratch) and with it and 1 GB more (in their
reservation; the 8 lanes' other arenas not reserved), each one lane's; then
emissions with the least room of the caller's scratch they take, found by
halving, which must be less than lane 0's columns take, each one lane's; then
the lane rule, and the driver's
objects and executables by `--lanes`, by default and by `--lanes 8
--id-order N`, each one lane's, the executables exiting alike, and with lanes
that don't start, and the failing programs' exit statuses and diagnostics by
`--lanes 8`, with the ids in another order too, each `--lanes 1`'s. Then the
driver's session (#905): a library preloaded into the driver counts its calls
of `pthread_create`, on the first program of 600 functions: none for `--lanes
1`, `--worker-budget=1`, a source's `-fsyntax-only` and an object compiled on
one CPU, one per lane past the first of `--lanes 3` and by default of the cores
of the CPUs it may run on (by `/proc/cpuinfo`), for an object, an executable
and two sources' objects; where
the process has a core's two siblings, on them by default one lane (its lane 1
created to count the core) and by `--lanes 2` two, and on CPUs of two cores
two; with its second lane failing to start (`BUSTER_TEST_LANE_FAIL_AT`) the
one created, the object `--lanes 1`'s; and by default none for the program of
three functions' object and executable, too small to pay for a second lane
(#1444: one source's session starts on demand); each with `--time-stages`'
lanes asked for and run on (3 and 3, the cores' by default, 4 and 1, and 1 and
1 for the small object). Then the exit helper (#1444, `lane_exit_helper_start`):
the small program's object and `-E`, a `#warning`'s `-fsyntax-only`, a failing
object and the large program's object, each by `BUSTER_EXIT_HELPER=0`, `=1` and
by default, the test a subreaper for each run: the same status, output, errors
and object each way; a helper by `=1` where the driver's lanes created no thread
(but in a sanitizer build), by default where PID 1 reaps orphans too, none
otherwise, each ended within 10 s of the driver; and the small object by `=1`
held 2 s after its helper's start (`BUSTER_TEST_EXIT_HELPER_PAUSE`), whose
streams must end while it still runs; then
commands in one process, on a session of 4 lanes: a failing command, objects
on 4 lanes and on 2 by `--worker-budget 2`, a `-fsyntax-only`, and two
sources' `-fsyntax-only` by 3 lanes and by 1, three times, every lane calling
each (`run_check_command`'s collective call), each lane the same thread
throughout, no thread created, each object `--lanes 1`'s; then the same on a
lazy session. Then the driver's deferred bodies (`--deferred-bodies`,
`check_input_deferred_compile`, whose walks and merge's pass are rounds of the
command's flow, `CheckDeferredFlow`): the four programs by the vector path by
`--lanes 1`, 2, 3, 8, by default and by `--lanes 8 --id-order N`, each object
and executable source order's; the program of 600 functions read on lanes and
merged by `--lanes 8`, in source order by `--lanes 1`, after a `#pragma clang
section` line (before its top level's walk) and by `--no-deferred-bodies`, by
`--time-stages`' `deferred-lanes` and `deferred-merged`, under a jobserver pipe
of none, 1, 3 and 7 spare tokens (a lane per token, every token back once) and
by `--worker-budget 1` and 3, and with lanes that don't start; a unit of
16-byte lock-free queries by `-mcx16`, source order's object (the caller's
table's cx16, `check_input_target_table`); and the failing programs by
`--lanes 2`, 3 and 8, `--lanes 1`'s diagnostics. Then a growing team: the first program of 600 functions by
`--lanes 4` under a jobserver pipe of no token or one, its lane 0 writing none,
2, 3 or 5 more after its first item (`BUSTER_TEST_WORKER_SUPPLY`): its
`--time-stages` grant, team and growth, no more lanes taking parts than it
admitted, its object `--lanes 1`'s, and every token back in the pipe once;
the growth gate alone around its threshold and bounds, before any part is
lowered and after every one is, and its release of a
guardian that took no token; the driver by the default policy on programs of
4,000 functions (which grow, to `--worker-budget 3` where given) and of 200
(which ask for more than one lane but don't); and in this process, on a session
of 4 lanes, a growth to 4 lanes and with `--worker-budget 2` to 2, every
supplied token back as the command returns.
`./build/compiler benchmark-check-lanes
[--passes N] [--lanes LIST] [--cpus LIST] [preprocessor options] file...`
times a file's emission on each lane count (every lane forced) and by the
rule, with the lowering's preparation, the lanes' run and the merge apart,
every run one lane's code, for the target its options give (#1045:
`test-checker` measures a unit of 512-bit vectors with `-march=znver5` and
refuses the same unit for the baseline profile, whose SSE2 has no variable
shifts to split them into (#1193), and measures a baseline
unit's function whose target attribute takes AVX2's vectors,
`c_check_lanes_benchmark_test.c`). Run it under the 9700X's lock without
`taskset`, with `--cpus 2,0,1,3,4,5,6,7` (lane 0 on CPU 2). The 9700X job
runs the test, and the benchmark on the frozen SQLite source where the
emission changes, on the lane counts of the guide's table (#1069).
[docs/function-lanes.md](../../docs/function-lanes.md) has the design and the
measurements.

## Windows (#2536)

`--emit-window=N` (`CheckProgramLanes.window_tokens`) is step 1 of [the
all-core path](../all-core-path.md): one lane's emission
(`check_program_emit_alone`) cuts its bodies into parts of about N of their
tokens, as the lanes cut theirs, and takes each part's lowering and its code
before the next part's (`check_program_emit_windows`). The driver's default is
2,048 tokens (`BUSTER_CHECK_EMIT_WINDOW_TOKENS`, the owner's choice once step 1
was measured); 0 is one part, the order before windows and their twin, and a
caller without the driver (`CheckProgramLanes` zeroed) gets it. A part's typed streams and Tier 0
code go in the thread's window (`check_program_thread_window`: base pages,
reserved once and never given back), which the part pops when its code is
done, so the next part's pushes find the same pages in the cache. Before the
pop it copies out what the merge reads (`check_program_emit_window_keep`):
the references, and of the records only those they mark, the references
renumbered to them (`CheckEmitBody.kept_records`, which the merge's fixups
take in place of the code's count); the asm labels' names; and with `-g` the
locals' homes, classes and bindings. Everything else of the window is gone:
the code's other slices are nulled, and an inline callee's `ready` is the only
one left set.

- **The parts are the lanes'.** The lowering takes them in any order and
  `check_lower_finish` orders their results, so the object and the failure are
  one part's, as for the lanes: a part that fails keeps its window, which the
  merge's report reads.
- **Tier 0's inline callees** (#1398) are parts of their own, lowered first
  and into the lane's output, since a body before its callee splices the
  callee's stream; their streams stay for the unit.
- **What keeps one part:** `--tier1` and `--helper-report`, which read every
  body's stream after every body's code; a unit on lanes, whose parts are the
  lanes'; and a thread whose window can't be reserved. A window short of room
  (a reservation the address space cut short) sends the bodies again as one
  part, from the arenas as they were, as a lane's part does
  (`check_program_emit_windows_again`, `CheckProgramLanes.again`).
- **Poison:** `--poison-arenas` (`CheckProgramLanes.poison_windows`) overwrites
  each popped window, so that a part that read what another part left shows.
- **What it measures:** on the 9700X, `sqlite3.c` at eight workers by W =
  2,048: the emission 1.15 times its one-worker user cycles (1.33 in one
  part), the lowering 1.49 (1.60), 292.9 MB/s (289.3); one worker the same
  ([the design's "Step 1's result"](../all-core-path.md#step-1s-result)).
  Measure it with step 0's gauge ([all-core](all-core.md)), whose sampled
  split of the two stages windows interleave is their sum alone.
- **The checks:** `test-check-lanes` emits every generated and failing program
  by windows of 1, 64 and 2,048 tokens on one lane, each window poisoned as it
  is popped, by both paths, with `-g`'s rows and unwind tables and without: each
  one part's object or failure, a part a body at one token; and by windows of
  64 in 2 KB (`CheckProgramLanes.squeeze_window`), which go again as one part.
  `test-checker-driver`'s Tier 0 inlining fixtures build by `--emit-window=1`
  (and the lanes' fixture by 64 too): the objects of one part.
