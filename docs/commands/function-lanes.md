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
before the round, without waiting, and given back after it. Elsewhere
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
of `pthread_create`: none for `--lanes 1`, `--worker-budget=1`, a source's
`-fsyntax-only` and an object compiled on one CPU, one per lane past the first
of `--lanes 3` and by default of the cores of the CPUs it may run on (by
`/proc/cpuinfo`), for an object, an executable and two sources' objects; where
the process has a core's two siblings, on them by default one lane (its lane 1
created to count the core) and by `--lanes 2` two, and on CPUs of two cores
two; and with its second lane failing to start (`BUSTER_TEST_LANE_FAIL_AT`) the
one created, the object `--lanes 1`'s, each with `--time-stages`' lanes asked
for and run on (3 and 3, the cores' by default, 4 and 1); and
commands in one process, on a session of 4 lanes: a failing command, objects
on 4 lanes and on 2 by `--worker-budget 2`, a `-fsyntax-only`, and two
sources' `-fsyntax-only` by 3 lanes and by 1, three times, every lane calling
each (`run_check_command`'s collective call), each lane the same thread
throughout, no thread created, each object `--lanes 1`'s; then the same on a
lazy session.
`./build/compiler benchmark-check-lanes
[--passes N] [--lanes LIST] [--cpus LIST] [preprocessor options] file...`
times a file's emission on each lane count (every lane forced) and by the
rule, with the lowering's preparation, the lanes' run and the merge apart,
every run one lane's code, for the target its options give (#1045:
`test-checker` measures a unit of 512-bit vectors with `-march=znver5` and
refuses the same unit for the baseline profile, and measures a baseline
unit's function whose target attribute takes AVX2's vectors,
`c_check_lanes_benchmark_test.c`). Run it under the 9700X's lock without
`taskset`, with `--cpus 2,0,1,3,4,5,6,7` (lane 0 on CPU 2). The 9700X job
runs the test, and the benchmark on the frozen SQLite source where the
emission changes, on the lane counts of the guide's table (#1069).
[docs/function-lanes.md](../../docs/function-lanes.md) has the design and the
measurements.
