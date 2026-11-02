# lanes

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/lane.c` is the lane base layer that every phase of the
  object writer and the linker runs on (groups of threads that run the same
  code, their barrier and broadcast, `lane_range`, and arenas).
  `lane_test.c` holds its tests (`main.c` includes it; `./build/compiler
  test-lanes` runs them) and `lane_benchmark.c` provides `benchmark-lanes`.
## Lane tests

`./build/compiler test-lanes` checks the lane base layer. The arena checks run
on the calling thread, and the collective checks run on groups of 1, 2, 3 and
8 lanes and compare what the lanes produce with a sequential reference:

- arenas: pushes at every power-of-two alignment up to 2 MB, invalid
  alignments, a push one byte past the capacity (it fails and leaves the arena
  as it was), sizes and counts whose arithmetic overflows, the page sizes
  asked for, read back from `/proc/self/smaps`, and a measuring arena's
  (`arena_measure`) random pushes, which must take what a reserved arena's do;
- `lane_range` and `lane_range_by_cost` for every count from 0 to 1,000, with
  seven cost patterns: the parts must cover the items in lane order, and each
  item must go to the lane whose share of the total cost holds its midpoint;
  and the same of `range_part` and `range_part_by_cost` with 64, 512 and 1,001
  parts;
- 10,000 rounds of a sync and a broadcast, with a lane arriving late every
  1,000 rounds, by turns so late that the others sleep on the futex and only
  so late that they wait in `mwaitx`, then as many broadcasts back to back,
  and narrow tasks spread by `lane_from_task_index`;
- a lane that makes itself a group of one and rejoins, and the scratch arenas;
- `lane_advance` (#777's board): the lanes but lane 0 each advance one word
  20,000 times, pausing together now and then, while lane 0 waits on it with
  `lane_wait` for every advance, sleeping on its futex with more than one lane:
  each value it sees even and larger than the last, and no advance lost;
- groups that share CPUs (#824): `lane_group_shares_cpus` against this thread's
  affinity mask — a group of exactly the process's CPUs doesn't share them, one
  of more does unless every lane is pinned, and one lane never does — and a
  group of two lanes on one CPU, which the test cuts this thread's mask to: both
  lanes must read their group as sharing it, and the lane that waited for lane
  0's stall must have slept, its group's wait leaving `mwaitx` and the spin that
  takes its place without it at once, since either holds the CPU the late lane
  needs; two lanes on the process's own CPUs, where it has more than one, read
  theirs as not sharing them;
- command-owned teams: stable worker identities across repeated operations
  and changing active counts, exact participation, shared-write visibility,
  per-operation worker scratch cleanup (or with `lane_team_keep_scratch` the
  same arenas, emptied, from one operation to the next, and unmapped when the
  team stops), caller context restoration, one-lane operation, invalid/nested
  dispatch and startup failure cleanup;
- sessions (#905): sessions of 1, 2, 3 and 8 lanes, and lazy ones of 1, 3 and
  8, each lane running the flow once, lane 0 the calling thread, with
  capacity − 1 threads created (`LaneProcess.threads_created`, and the
  process's threads) and joined after, none for one lane; in the flow, the created lanes' asynchronous signals blocked
  and the synchronous ones not, a signal sent to the process handled on lane
  0, parked broadcasts, narrowings to 1, 2, 3, every lane and more, nested,
  each narrowed group syncing, broadcasting and taking parts, a flow of rounds
  (`LaneFlow`) of 1, 3, every lane and more, of two kinds, lane 0 running its
  part itself and the others theirs by kind, a nested run in a round refused,
  a run, a team, a
  cold group and a session of several refused where they would create a
  thread, and a child forked from lane 0, which forgets the session; then
  sessions whose third lane fails to start (`LaneProcess.fail_at`), eagerly
  and lazily, and a lazy
  one whose second does, the lanes created joined and the flow on lane 0
  alone; sessions of a lane per core (`BUSTER_LANE_SESSION_PER_CORE`), eager,
  lazy and lazy with a flow of rounds begun before lane 0 settles, of 2 and 8
  lanes, on test topologies of the process's CPUs (`LaneProcess.topology`) in
  pairs, in fours and all one core, as many lanes as the capacity and the cores allow,
  each round on them and each lane's `lane_session_asked` as many, a lazy
  session's lane 1 created though it runs alone on one core; and in a
  child, a session that leaves its lanes parked at its end;
- cores (#905): `lane_cores` on a test topology of SMT siblings n and n + 8,
  adjacent pairs, single threads, CPUs without a list, lists that don't parse,
  leave their CPU out or overrun the read's buffer, a newline, a CPU whose
  sibling's bad list must be skipped, siblings past the mask's words and an
  empty mask, against counts worked out by hand; and `lane_core_count` against
  `/proc/cpuinfo`'s cores of the process's CPUs, then on a core's two siblings
  (one) and CPUs of two cores (two). That oracle (`lane_test_cpuinfo`, which
  test-check-lanes, test-foreign and test-parallel share) reads only the
  `processor`, `physical id` and `core id` lines, each value one bounded
  decimal; its reader is checked first on texts of `/proc/cpuinfo`'s shape:
  four processors' blocks whose flags lines hold 1,024 names with digits,
  values at and past their bounds and past 64 bits, malformed values and keys
  that only start like those three;
- the design's exchange (rule 4 in "Lanes"): records counted per hash
  partition, placed, filled and resolved by partition, byte-identical to the
  reference for every lane count;
- parts (rules 1 and 3): 13 inputs cut by `lane_parts_by_cost`, whose parts the
  lanes take with `lane_take`, then count, place and fill by part, with a late
  lane in every loop. The output must be byte-identical to the reference,
  every part must be taken once, and every lane must get the same order: the
  costliest part first, then the others costliest first to within a factor of
  two (one input's costs grow within one factor of two, so that its costliest
  part isn't its first). `parts_by_cost`, the same cut by one thread for a phase
  cut before its lanes start, must give the group's boundaries and order for its
  lane count, and for 1, 2, 3, 5, 8, 64, 256 and 257 lanes as many parts as such
  a group cuts (of 256 lanes, the most a group has, for 257),
  `range_part_by_cost`'s boundaries and every part once in that order.

A watchdog ends the run with status 1 if a group doesn't finish within 60 s,
which is how a lost wake-up would show. `--rounds N` and `--seed N` change the
rounds and the random inputs.

The command deliberately starts without a team or a session so it can test
cold `lane_run`, explicit `lane_team_start`/`lane_team_stop` lifetimes and
sessions. Do not wrap the main command dispatcher in a team or a session:
`main.c`'s units share one thread context, but the benchmarks' units, which the
dispatcher also runs, have contexts of their own. The compiler's commands (the
C driver's and `link-foreign`) own a session (#905, `lane_session_run`): their
lanes, started once after the command's options and before it reads any
input, the calling thread lane 0, as many as the command may take, none for
one lane, which run the command's flow together until it ends. Their sessions
are lazy (`BUSTER_LANE_SESSION_LAZY`): lane 0 creates lane 1 alone and starts
the command, while lane 1 creates the others and says how their start went,
which lane 0 learns (`lane_session_settle`) before it first meets them. By
default a session asks for no more lanes than the cores of the CPUs the process
may run on (`BUSTER_LANE_SESSION_PER_CORE`): lane 1 counts them from sysfs'
sibling lists first (`lane_core_count`), a read per core (9.4 µs for the 9700X's
8, 19 to 55 µs as a process's first count), off lane 0's path,
and says how many lanes the session asks for (`lane_session_asked`), on a core's
two siblings one, itself then joined. Under a session
nothing creates a thread: a command's wide parts are rounds of its flow
(`LaneFlow`), `lane_run` runs on a lane alone, and a team, a cold group of
several or a second session is refused. A session whose lanes can't start runs on one lane,
never retrying. A terminal command's lanes stay parked for the process's exit
(but in sanitizer builds), a test's are joined. Other parallel commands (the
benchmarks', the tests') own their team in the code that runs their
operations. An operation's phases stay direct calls within its whole-operation
lane entry. `test-checker-driver` and `test-check-units` are serial code that
runs the driver's commands in-process: each command starts and joins a
session of its own, as alone.

## Lane benchmark

`./build/compiler benchmark-lanes [--lanes N] [--cpus LIST] [--rounds N]` pins
lane i of groups of 1 to 8 lanes to the i-th CPU of `--cpus` (by default the
allowed CPUs in order: 0 to 7 on the 9700X, one per core) and reports
21-pass medians of a sync and a broadcast when every lane arrives at once, of
`pthread_barrier_wait` (the RAD Debugger's barrier on Linux) in the same loop,
of waking lanes that wait in `mwaitx` and that sleep on the futex, and of
starting a group. Its `start` measure remains a cold `lane_run` outside a
command-owned team, including thread creation and joining. Then, on the
largest group, how much slower lanes run after
waiting 0.2 to 5 ms for a lane that works alone, and how well rule 1's parts
and the static ranges balance three phases, with every lane at full speed and
with half of them at half speed. Then what an arena's reservation and first
touches cost. Run it under the 9700X's lock
without `taskset`, since it pins its lanes itself.
`docs/object-writer-and-linker.md` ("Lanes") has the numbers. The default 9700X
job runs it after the tests; like the others, its measurements are
informational.
