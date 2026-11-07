# A unit's functions on lanes

[#151](https://github.com/buster14a/buster/issues/151): the C driver compiles a
translation unit's functions on a group of lanes. Each function's lowering to
Tier 0's typed stream, its Tier 0 code, branch sizing and encoding are a lane
item. The link the functions' code goes into, and the object or executable
made from it, are byte for byte what one lane makes, whatever the lane count
and whichever lane finishes first ([#153](https://github.com/buster14a/buster/issues/153)).

The driver runs it for `-c` and for the direct path's executables:
`./build/compiler [--lanes N] [--worker-budget N] ...`. By default it may run
one lane per core of the CPUs the process may run on (#905, below), and runs as
many as the bodies' tokens pay for (below). External worker ownership further bounds this count
(#673). `--lanes N` forces that many profitable-work candidates, up to the
body count, within the explicit worker cap and GNU make's available slots.
The checker reads the unit in source order before the emission, which
`--deferred-bodies` puts on the same lanes: "The checker's bodies on lanes",
below, has why that is off by default.

## Build-system worker ownership (#673)

The calling thread is one worker. `--worker-budget N` caps this process's
total workers, including that thread; values are 1 through 64, also accepted
as `--worker-budget=N`. A build system running eight files under a total
eight-worker budget can pass `--worker-budget=1` to each process. The cap
does not enlarge affinity or change the existing work-based lane rule.
An explicit `--lanes N` still overrides that work rule, within the external
cap. Without an external budget, the previous affinity/forced-lane behavior
is retained; system-wide idle sampling is not used as an ownership claim.

Under GNU make, the compiler also participates in the shared jobserver.
Make has already reserved the caller's implicit slot; each additional lane
requires one available token byte. Authentication is captured at source-command
entry, before compiler descriptors could reuse numbers which make closed.
The final `--jobserver-auth=` or legacy `--jobserver-fds=` MAKEFLAGS option
wins. FIFO paths (with escaped spaces) and inherited `R,W` pipe descriptors
are supported. An unavailable, disabled or unrecognized advertised server
permits only the calling thread. With pipe-style make jobservers, mark the
recipe with `+` so make passes the descriptors. A `+` recipe also runs under
`make -n`; the compiler recognizes that short flag and performs no source work.
See [GNU make's job-slot rules](https://www.gnu.org/software/make/manual/html_node/Job-Slots.html)
and [POSIX protocol](https://www.gnu.org/software/make/manual/html_node/POSIX-Jobserver.html).

Tokens are taken without waiting, after the existing lane rule chooses useful
work and before active-lane allocation. Pipe descriptors are reopened through
`/proc/self/fd` into private nonblocking open descriptions. The inherited
blocking mode is never changed. Regular files, mismatched endpoints, wrong
access directions and packet-mode pipes are refused as jobservers. No spare
bytes means one lane, without starting another process.

When bytes are available, a small guardian starts in its own session before
reading any. It inherits blocked catchable signals and owns the exact bytes
until the compiler closes its private control pipe. The guardian returns them
and acknowledges completion after workers join, before a serial retry or merge.
Compiler exit, including whole-process-group termination, closes that pipe too.
It starts through `posix_spawn` and an immediate internal command, so its address
space retains no front-end mappings during emission. The nonempty-token check
still precedes process creation, and no credit is held during front-end work.
The guardian closes unrelated inherited handles; the caller's signal handlers
and masks are preserved. The implicit slot is never returned. Failed guardian
setup, descriptor or process limits leave the
compiler on one lane; failure to return owned slots refuses output.

A full Linux pipe can lack append space even after a partial read frees logical
bytes. The guardian reserves recovery storage before borrowing. If a return
hits that page-buffer limit, it drains available opaque bytes into this storage
and restores every pending byte, retaining any unwritten suffix. This compacts
the pipe without requiring another recipe to read it; an I/O error never
discards held slots. GNU make fills the initial pipe before starting jobs, so
its capacity bounds the total available and borrowed explicit tokens. The
recovery storage is private to the clean guardian and is released before its
acknowledgement completes.

### A team that grows

A team the jobserver gives fewer lanes than its bodies pay for grows while it
runs (`CheckEmitGrowth`, `c_check_program_emit.c`): in an uneven build two
large files reach their emissions together, the first takes the six spare
tokens and the second none, and without growth the second ran its bodies on one
lane after the first team had given its tokens back. Such a team, in the C
driver's flow and under a jobserver, cuts its bodies' parts for every lane it may
take (the lane rule's, the explicit cap's and the session's), 16 per lane rather
than a round's 64, and puts them in one order: the costliest first, then the
others in source order. Each part is two items, its lowering and its code, and
the team takes every part's lowering, in that order, before any part's code, in
the same order; a part's code waits on the part's word until its lowering is
done, as another lane may still be at it (`check_program_emit_item`). A lane
that took each part's lowering and code by turns would refill its caches at
each turn, and lane 0 takes most of the items of a team that grows late or
never: on the 9700X, one lane's emission of `sqlite3.c` took 0.35% more user
cycles with its bodies in 2 parts taken that way than in 1, 0.73% in 8, 0.79%
in 16 and 0.92% in 128, with the same instructions and objects, and none in 128
whose lowering all came first (one executable switched by an experiment, 30
rounds each, #673). Lane 0 takes the items alone, or in a round of all those
lanes once it has admitted another, where each lane past the admitted ones
sleeps on the admission's word until lane 0 admits it, and then takes whichever
items are left. Between its items, at most every
100 µs, lane 0 reads the jobserver's spare tokens (`FIONREAD`,
`worker_budget_available`). It takes them under a lease of its own
(`worker_budget_grow`, a guardian like the first's), up to the lanes it may
take and the items left, when the work left pays for the guardian's start: the
lowering's share (11 sixteenths, `sqlite3.c`'s on one lane) of the tokens of
the parts not lowered yet and the code's share of those not coded yet, on the
admitted lanes less on that many more, must be at least
`BUSTER_CHECK_EMIT_GROW_TOKENS`, about a guardian's 160 µs of one lane's
emission. With `--lanes N` the team grows whatever the work left pays. A team
holds up to eight leases. When lane 0 finds no item left it closes the
admission, so that the lanes still asleep leave, and the round ends as every
round does. Every lease is returned after the round, before a serial retry or
the merge. The output is one lane's, whatever lanes took the parts.

A team that starts on one lane and never grows takes its items on lane 0 alone,
without waking a lane, at the cost of the clock read per item and a `FIONREAD`
per 100 µs. A team the jobserver granted
some of its lanes starts its round at once on all of them: the lanes past the
grant wake for the round, sleep until lane 0 admits them, and wake again as it
closes, which the round's end waits for.
Ownership stays the jobserver's: a team takes only spare tokens, never waits
for one, and no machine-wide idle sampling decides anything.

With `--time-stages`, `worker-budget` reports the external mode, explicit cap,
requested and granted workers, the team at its end, the lanes it grew by, the
lanes that took a part of the bodies (one when the calling thread ran them
alone, a serial retry's among them; never more than the team) and the
serial-fallback reason. The team, its growth and its lanes are a single unit's
emission's: with several source files, the team is their units' lanes, and
the other two are 0.
This also identifies pipe-style recipes whose descriptors were unavailable;
ordinary source diagnostics retain their previous bytes and ordering.

The original approximately 3% all-core loss compared different affinities.
It does not isolate an oversubscription or waiting-state cause. Qualification
must compare forced one lane, the default, and forced lanes under the same
physical-CPU mask, with a separate pinned-process control, and compare actual
make file jobs times internal teams at 1/2/4/8 jobs. Preserve statuses, objects,
diagnostics, team size, RSS, switches, migrations and physical traffic; neither
protocol tests nor a one-file gain establishes full-build throughput.

### Qualification and bounds

The [frozen qualification record](https://github.com/buster14a/buster-performance/blob/4df43f7d7a685133b6beade655d9e9a306947081/docs/measurements/unclaimed-perf-2026-10-28/issue-673-spawn/REPORT.md)
retains the source/binary identities, independent ownership checks, balanced
workload controls and negative fork/mixed-order experiments. Its measurements
describe the revisions named there; refreshing this change onto later main
establishes compatibility rather than a new timing result.

The clean-launch source `1d076d6736df9b7350610ec646dc1fe1ef39f7dc` was qualified
on the 9700X with frozen SQLite inputs, GNU make 4.4.1 and the same physical
CPU mask 0–7 for all shared-affinity controls. The separate pinned-process
controls keep their own label. A three-repeat matrix covers external jobs
1/2/4/8 and forced one lane, default lanes, forced eight lanes, explicit cap one
and the real make jobserver. All 120 cohorts produce the same object bytes;
their resource variation makes this matrix a characterization screen.

The policy comparison uses one locked group per workload, a full-size primer
and a cyclic nine-edge role sequence repeated four times. Each of the nine
predecessor/current policy pairs occurs four times, for twelve measured cohorts
per policy. The independent reader checks the actual schedule, zero exit status,
output comparisons and full counter coverage. An earlier mixed-workload list
rotation is retained as rejected evidence because preceding workloads were
confounded with the policy.

All three policies request eight emission lanes. The jobserver policy has a
worker cap of eight; cap one has a worker cap of one; the unrestricted control
clears `MAKEFLAGS` for each compiler. Every policy uses the same real make
recipes and resource wrappers. Median whole-cohort wall times were:

| Eight-file workload | Jobserver (ms) | Cap one (ms) | Unrestricted (ms) |
| --- | ---: | ---: | ---: |
| Eight SQLite units | 408.827 | 408.011 | 428.449 |
| Eight tiny units | 8.766 | 9.101 | 9.323 |
| Two SQLite and six tiny | 180.681 | 177.824 | 172.410 |

The saturated large cohort uses 4.58% less wall time than unrestricted teams.
The uneven cohort costs 4.80% more wall time, alongside fewer cycles, switches
and migrations: its leases were fixed as each team started, the tail that a
team that grows now closes (below). Tiny-file ranges overlap. These are one-placement workload results with retained major
faults, not a universal throughput or neutrality guarantee.

A same-head one-file control with spare tokens uses eight lanes in both cases:
134.221 ms with the jobserver and 134.277 ms without it, with overlapping ranges
and 4,912 versus 4,910 minor faults. The superseded post-front-end fork guardian
had added 1,549 faults and 1.28% wall time in its matched control. Clean spawning
removes that measured copy-on-write amplification without reserving credits
during the front end. Exact method, counters, identities and rejected controls
are retained with #673's performance evidence.

### Growing teams' qualification

The [growing teams' record](https://github.com/buster14a/buster-performance/tree/908be6e2a36247da0226cc7f5b5e2f86f1d84c2b/docs/measurements/tail-growth-673-2026-11-02)
reruns the same harness on the 9700X, main `3ce71ec3` against `e7416f30` (#1161), in two rounds of
12 cohorts per policy. Medians of whole-cohort wall time:

| Eight-file workload | Build | Jobserver (ms) | Cap one (ms) | Unrestricted (ms) |
| --- | --- | ---: | ---: | ---: |
| Two SQLite and six tiny | main | 147.099, 146.914 | 145.331, 144.990 | 139.306, 139.926 |
| | `e7416f30` | 137.855, 137.309 | 144.940, 144.687 | 140.783, 140.460 |
| Eight SQLite units | main | 301.304, 301.201 | 302.414, 303.212 | 321.145, 320.512 |
| | `e7416f30` | 299.902, 300.995 | 302.465, 302.687 | 319.443, 320.413 |
| Eight tiny units | main | 7.225 | 7.287 | 7.223 |
| | `e7416f30` | 7.175 | 7.156 | 7.192 |

- **Uneven.** The jobserver's cohort takes 6.3–6.5% less wall time than main's and is the fastest of the
  three. In every cohort, the SQLite file it started on one lane grew to seven.
- **Eight SQLite.** It moves −0.5% and −0.1%, with a few files growing at the tail.
- **The other policies.** Cap one and unrestricted, whose paths the change leaves alone, move −0.5%
  to +1.1% on the two SQLite workloads.
- **Objects.** They are one lane's in every cohort.
- **Controls.** `time-runs` on `sqlite3.c`'s `-c`, against main: wall time +0.17% for a team that
  never grows, −0.08% for one granted 4 of 8 lanes, +0.20% with every lane granted and −0.38%
  without a jobserver.

### A growing team's halves

Over six placements, a team that never grew took 0.73% more user cycles than one lane's emission
before #1161, with the same instructions. The [two-phase record](https://github.com/buster14a/buster-performance/tree/073d1180c141c91485165fdb00d0f89456326d35/docs/measurements/two-phase-growth-673-2026-11-04) finds the cause in one
executable, switched by an experiment's environment variable: each turn between a part's lowering and
its code refills a lane's caches. On one lane, `sqlite3.c`'s bodies took 0.35% more user cycles in 2
parts than in 1, 0.79% in 16 and 0.92% in 128, and none more in 128 with every part's lowering first.
The team now takes them in that order ("A team that grows", above). Against main `747ab617`
(#1161 merged), `09e22269`:

- **Never grows** (a jobserver without a spare token, six placements): −0.96% user cycles (−1.26%
  to −0.35% over placements, main against itself −0.69% to +0.55%) and −0.77% wall time.
- **Grows from 4 of 8 lanes:** −1.34% user cycles and −1.09% wall time at one placement.
- **Without a jobserver**, a path the change leaves alone: +0.03% user cycles over placements.
- **Cohorts** (the same harness, 12 a policy in each of two rounds): every one within its spread,
  the jobserver still the fastest on the uneven and eight-SQLite workloads. Eight compiles at once
  don't show the compile's own 1%, and the record doesn't say why.

### Physical traffic

The same record counts both memory controllers' CAS commands (`amd_umc_0`, `amd_umc_1`, read and
write apart), around each policy of #695's matched-affinity and real-make controls; buster-performance#49
rejects the Data Fabric's bandwidth aliases on this CPU. Eight SQLite compiles on eight workers:

| Policy | Read CAS (M) | Write CAS (M) | Page faults | Wall (ms) |
| --- | ---: | ---: | ---: | ---: |
| One lane each, pinned or sharing CPUs 0–7 | 127.7–127.8 | 76.4–76.8 | 42,208 | 277.7 (shared) |
| Default lanes, sharing CPUs 0–7 | 132.2 | 81.2 | 59,637 | 295.2 |
| `make -j8`, jobserver | 127.0 | 77.0 | 45,572 | 278.8 |
| `make -j8`, free eight | 132.4 | 81.1 | 62,906 | 300.2 |

Uncoordinated lanes read 3.5% more and write 6% more, with 41% more faults, about 5% more CPU time
and 6–8% more wall time: #673's loss, with its traffic. Affinity alone changes none of it, and the
jobserver's cohorts match one lane's. A growing team spends traffic where it pays: in the uneven
cohort it reads 7% more than cap one, for 6% less wall time. One SQLite compile reads about 1.0 GB and
writes 0.6 GB (64 bytes a CAS command, which #49 leaves conditional), and eight at once move about
47 GB/s of the memory's 83.2.

## The phase

`check_program_emit_lanes` (`c_check_program_emit.c`) is the emission. Its
steps are source order's (`check_program_emit` before #151), cut differently:

1. **Before the lanes**, on the calling thread: the lowering's preparation
   (`check_lower_prepare`, `c_check_program_lower.c`): the helpers, the errno
   accessor, every type's ABI classification, and the tables a body's lowering
   reads or writes by token and object (`CheckLowerShared`); and the access
   column Tier 0 reads (`Tier0Function.symbol_access`).
2. **The lanes**: the bodies are cut into parts of consecutive bodies by their
   tokens (`lane_parts_by_cost`) and taken costliest first (`lane_take`). A
   lane takes a part's steps in source order's order for the whole program:
   its bodies' lowering (`check_lower_part`), its calls' signatures assigned
   together (`abi_assign`), each body's parameters and `tier0_validate`'s
   check; then each body's Tier 0 code (`tier0_compile`) and, while the part
   has no failure, its code after Tier 0 (`check_program_emit_code`): branch
   sizing, encoding into the lane's bytes, its unwind frame, its labels'
   offsets and with `-g` its raw rows, and what its fixups need. A lane makes
   its writer (`check_lower_writer_begin`) when it takes its first part, so a
   lane that takes none costs nothing more than its start. A growing team
   ("A team that grows", above) takes every part's lowering before any part's
   code instead.
3. **The merge**, on the calling thread: the lowering's result
   (`check_lower_finish`), the attributes' checks, the link's capacity,
   names and symbols, the C runtime's binding (`bind_begin`, for an
   executable), then each body's atom, symbol definitions and fixups in source
   order (`link_add_atom`, `link_define_symbol`, `link_add_code_fixups`), the
   helpers', and the unwind tables.

A body writes only its own entries of what the lanes share: its
`Tier0Function` and code, its frame, its result and its tokens' and objects'
entries of the lowering's tables (`local_map`, `switch_locals`,
`save_locals`, whose `local_map` entries it puts back before it ends).

### What a part numbers

Two things source order numbered as the bodies went, which a lane can't know:

- **The runtime's functions.** The lowering calls libgcc's (`__divti3`,
  `__mulsc3`, ...) where Tier 0 has no entry, and numbers each one's symbol
  at its first call, after the program's and the helpers'. A writer gives a
  call of one a provisional symbol of its own (`CheckLowerShared.provisional_runtimes`,
  past the helpers'), whose access is its final symbol's, and records the
  functions its part calls first, in the order it does. `check_lower_finish`
  numbers them part by part in source order, as source order's lowering would,
  and the merge puts the final symbols in the code's references before the
  link takes them. A unit's own declaration of one of them stays its symbol,
  found once before the lanes.
- **The label symbols** (GNU C's `&&label`). A part's go to its writer's room
  for them, after its earlier parts'; `check_lower_finish` puts the parts'
  together in source order.

### Failures

The failure reported is source order's, at every lane count. Source order's
emission takes its steps for the whole program in turn: every body's
lowering, the label symbols' check, the locations' room, every call's
convention, every body's frame, the attributes', every body's Tier 0 code,
the rows' room, the link and its binding, then each body's code. A part takes
the same steps, stops at its first failure (but past a failure after Tier 0 it
goes on with Tier 0 alone, whose failures come first), and records its step;
the merge reports the earliest step's failure, its first body's. A failure the
convention's check finds is at the token source order's writer had after the
last body, the last part's.

A resource's failure on lanes, or lanes that can't start, run the emission
again on the calling thread alone, from the arenas as they were: one lane's
arenas, the caller's, decide whether a unit fits. `CheckProgramLanes.again`
says why it ran again (`CheckProgramAgain`): lanes that didn't start
(`lane_run`, which also runs lanes it can't pin, then says so), or a part that
ran out of room in its lowering (or whose lane had none for its writer), in a
body's Tier 0 code or in a body's code after Tier 0.

### The command's session (#905)

In the C driver the lanes are the command's session's (`lane_session_run`,
`check_driver_run`), started once, after the options and before any source is
read: as many as the command may take (one per core of the CPUs it may run
on, or `--lanes N`, at most one per CPU, and at most `--worker-budget N`), none
for one source without code to emit (`-fsyntax-only`), the calling thread lane
0. The session is lazy (`BUSTER_LANE_SESSION_LAZY`): lane 0 creates lane 1
alone and starts the command, and lane 1 counts the cores and creates the
others while lane 0 runs the front end, so that the count and the threads'
creation are off lane 0's path; lane 0 learns how many lanes the session asks
for when an emission pays for more than one (`lane_session_asked`), how their
start went at its first round or at the flow's end (`lane_session_settle`),
and a round whose lanes didn't start runs its bodies on one lane
(`CHECK_PROGRAM_AGAIN_START`).

One lane per core rather than one per CPU (`BUSTER_LANE_SESSION_PER_CORE`): two
lanes on one core's SMT siblings share its pipes, caches and power, and every
lane costs its thread, its arenas and its waits. On the 9700X (8 cores, 15 CPUs
online), `-c sqlite3.c` by default on 8 lanes took 0.9% to 1.8% less wall time
than on main's 15 (medians of three rounds of 11 runs, 114.8 to 114.9 ms against
116.0 to 117.0), with 18% fewer user cycles and 45 MB less peak memory. On 4
cores' 7 CPUs (CPUs 0 to 3 and their online siblings) its 4 lanes took 0.5% to
0.9% longer than 7, with 7% fewer user cycles and 20 MB less: there the
siblings' lanes still buy a little time, at a cost. A small file's object, its
executable and four units' objects didn't change beyond their spread. Lane 1
counts the cores from sysfs' sibling lists (`lane_core_count`: one
`core_cpus_list` read per core, its siblings skipped): on the 9700X 9.4 µs for
its 8 cores and 19 to 55 µs as a process's first count, on a 16-CPU Zen 4 31 µs
and 50 to 65 µs, as much as the lazy session's whole start on lane 0's path, so
lane 1 counts them first, publishes the lanes it asks for, then creates them;
lane 0's start (`lanes-start`) stayed 0.03 to 0.06 ms. On a core's two siblings it asks for one, and lane 0 joins it at its
settle; the flow's group, which lane 0 keeps from before the settle
(`lane_flow_begin`), takes the settled count (`lane_flow_group`). Every lane runs the
command's flow (`check_driver_flow`): lane 0 the unit's stages alone, while the
others park on the futex (`lane_park`), and each emission that pays for more
than one lane is a round of the flow (a `LaneFlow`, `lane.c`), which lane 0
announces with its run and its lanes; the first of them, lane 0 among them,
take the bodies' parts on a group of their own (`lane_narrow`), lane 0 in
`check_program_emit_round` and the others in `check_program_emit_follow`, a
direct call rather than a `lane_run` of the emission's own. A round's lanes are those the lane rule and the worker budget
give it: lane 0 takes the jobserver's slots before the round, without waiting,
and gives them back after the round's lanes have met, before any serial work,
so no slot is held through the front end, and no lane waits on the jobserver.
Lanes that can't start leave the session on one lane, which no round retries;
a terminal command's lanes stay parked until the process exits. Lane 0's
stages stay one call chain, its frames holding the unit's state across the
rounds (an executable's emission takes another round when binding finds the
runtime's snapshot damaged). Several units' command runs its wide parts as
rounds of its flow too (`CheckUnitsFlow`, the [translation units
guide](translation-units.md)), and so does `link-foreign` (the [driver's
contract](foreign-driver.md)).

### Memory

Lane 0 is the calling thread, with the caller's arenas but for its writer's
columns, so one lane's emission pushes what source order's did, where it did.
Each other lane has three arenas of its own (`CheckProgramLanes.arenas`): its
functions' lowered columns, its writer's columns with its functions' Tier 0
code and what the merge reads, and its functions' bytes. The caller keeps them
until it is done with the emission's link (`check_program_lanes_release`); the
next emission with them puts them back.

A lane's writer has some thirty columns, each with room for any of the
program's bodies, far apart in its scratch arena, of which a lane touches a
little at each column's start. In huge pages, each column's first touch cleared
a 2 MB page, and each lane's arenas are new: on the 9700X, 8 lanes' emissions
of `sqlite3.c` took 6.7 billion kernel cycles against one lane's 0.17, and
were slower than 4 lanes'. The lanes' scratch arenas are in base pages
(`arena_reserve_small`), so a lane clears only what it touches: 0.58 billion.
So are lane 0's writer's columns (`CheckEmitRun.columns`), in the first lane's
scratch of `CheckProgramLanes.arenas`, or without lanes the calling thread's
(`check_program_thread_columns`): in the caller's scratch, they cleared a huge
page each too.
On one lane on the 9700X, the lowering of `sqlite3.c` added 56 MB to the peak
RSS, and 9.8 MB with them in base pages (#531). The functions' columns and
bytes are packed, and keep huge pages past their first 64 KB.

Lane 0's arena is reserved for exactly the room its writer's columns take
(`check_lower_writer_bytes`: the writer's pushes on an arena of no memory,
`arena_measure`, so that the room doesn't depend on what the caller's arenas
hold), and the first lane's scratch is kept while it has that room, as the
calling thread's is without lanes. A units' lane keeps it from one unit to the
next, as it keeps its own three arenas (`check_program_lanes_settle`, the
translation units guide's "Memory"). Until #442, both were reserved again for
each unit: the reservation, its release and the faults of the pages its
columns' starts touch were most of a small unit's time after checking (the
lowering guide's "Whole-unit passes after checking"). The other
lanes' arenas are 64 GB each, since their scratch also holds their functions'
Tier 0 code, whose size isn't known before they run; a lane that can't
reserve its arenas refuses its parts, and the calling thread runs them again.
Lane 0 is that last resort, so where its columns' address space can't be
reserved either (`RLIMIT_AS`, or overcommit refused), they go in the caller's
scratch, as before #531, if it has their room: an emission then needs no
address space past the caller's arenas. `test-check-lanes` checks both in a
child whose address space it limits, and that an emission whose caller's
scratch has room for all but lane 0's columns makes its object.

### On the units' lanes

With several source files (#152) each unit runs on one lane of the units' group,
where `lane_run` can't start a group of the unit's own. Its emission shares its
parts on the units' board instead (#777, `CheckProgramLanes.board`): cut by the
same rule for the board's lanes, while one of them has no unit left to take, and
taken by its own lane and by those (`check_program_emit_help`), each with its
writer and arenas, and merged as above. [The translation units
guide](translation-units.md)'s "The functions on the units' lanes" has the
protocol.

## The lane rule

One lane, and one more per `BUSTER_CHECK_EMIT_LANE_TOKENS` of the bodies'
tokens beyond twice the largest body's, no more than the bodies (a part has at
least one) or the lanes the caller allows. A lane costs its wake-up (its
thread's start, outside the driver's session), its writer's columns and its
arenas' first pages, which its share of the bodies must pay for; and the lane
that takes the largest body takes at least as long as it. #113's deferred reader sizes its lanes the same way
([docs/c-deferred-bodies.md](c-deferred-bodies.md)).

## The checker's bodies on lanes

#151's last question was whether the bodies' parsing and checking go on lanes too, decided by the
measured serial share. The answer is **not by default**. The driver can read a unit's function
bodies apart with #113's deferred reader ([c-deferred-bodies.md](c-deferred-bodies.md)), with
`--deferred-bodies` (`check_input_deferred_compile`). #1404 also moves per-body controls, binding
answers, constant contexts and independent expression/graph construction onto those lanes. The
caller publishes their results in source order and retains shared preparation and exceptional
actions. The option remains off by default; the post-merge measurements below decide it again.

### How it runs

- **Rounds.** The bodies' walks, the merge's pass and the post-merge controls, binding answers,
  constant contexts and independent expression parts run as rounds of the command's flow
  (`CheckDeferredFlow`), on the session's lanes. The other lanes run them from the emission's follow loop
  (`check_program_emit_follow`, `check_deferred_follow`). A session refuses `lane_run`'s threads,
  which the reader started before. The post-merge rounds take the bodies' lanes only where those are
  four or more (`BUSTER_CHECK_DEFERRED_POST_LANES`, #447): by `--lanes 2` or `3` they run on the
  calling thread.
- **The grant.** Before the top level's walk, the reader counts the bodies' tokens and asks the
  driver for the lanes its rule plans (`check_driver_deferred_grant`). The driver gives no more than
  the session asks for and started, and no more than the jobserver's spare tokens. It takes those
  without waiting, under a lease of their own, and the reader gives them back after the last
  post-merge round, or before a source-order retry.
- **Source order.** These units read in source order, before the reader reserves or zeroes
  anything:
  - a unit whose bodies take fewer than `BUSTER_CHECK_DEFERRED_COMPILE_LANES` (4) lanes, or two
    with `--lanes N` (below);
  - a unit with a `#pragma clang section` line, which the merge doesn't take
    (`check_deferred_sections`);
  - a prefix, a repair and `-flazy-system-headers`' selection;
  - `--scalar`, which keeps the reader's twin;
  - each unit of several source files.

  A unit the merge refuses after its walk goes to source order, as `check_input_deferred` sends it.
- **The report.** `--time-stages` gives the bodies' lanes and why a unit went to source order
  (`time-check deferred-lanes` and `deferred-merged`).

The result is source order's at every lane count: `test-check-lanes` compares the driver's
objects, executables and failing programs' diagnostics with source order's.

### The earlier measurements (#151)

[The record](https://github.com/buster14a/buster-performance/tree/d8ec07c3bd1eb4ba2bd60281738a56f739be6265/docs/measurements/deferred-driver-151-2026-11-05)
measures #1403's first head, `4452efbc`, against main `483e5f70` on the 9700X, on CPUs 0–7, where
the driver's default is 8 lanes. The workload is `-c` of the frozen `sqlite3.c`, with the 9700X job's
options and `-w`: with warnings on, #1049's common warnings add 48 ms of serial work (#1340), which
dilutes every difference here. The whole process, medians of 21 runs:

| | source order | deferred bodies |
| --- | ---: | ---: |
| wall, `-w` | 108.24 ms | 105.66 ms (−2.4%) |
| user cycles, `-w` | 623.8 M | 703.7 M (+12.8%) |
| minor faults, `-w` | 8,552 | 23,577 |
| peak resident set, `-w` | 325 MB | 357 MB |
| wall, with warnings | 158.88 ms | 157.17 ms (−1.1%) |

`check-input` by `--lanes N`, medians of 11 runs, in ms:

| Lanes | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| source order | 56.66 | 56.87 | 56.73 | 56.69 | 56.94 | 56.89 | 56.75 |
| deferred bodies | 59.71 | 56.58 | 55.41 | 54.53 | 54.18 | 53.95 | 53.77 |

Two lanes are slower than source order and three break even, hence the rule's four.
`benchmark-checker-deferred` splits the reading on 8 lanes into the following, in ns/token
(source order's reading is 74.03):

| part | ns/token |
| --- | ---: |
| the top level's walk | 22.57 |
| the lanes' span | 5.65 |
| the merge (its pass on the lanes: 0.99) | 2.35 |
| `check_input_run`'s phases after the merge | 37.29 |
| the whole reading | 68.21 |

The lanes take a tenth of it. Without the option the driver's time is main's: −0.54% wall over 11
runs. The compiler's own `main.c` reads in source order for its `#pragma clang section` lines, at
the same time, and a small unit's bodies take one lane.

### The earlier decision (#151)

**Checking bodies on lanes, as #113's reader does it, stays off by default.** It moves only the
bodies' walks, which are a tenth of the reading. On 8 lanes it buys 2.6 ms of a 108 ms compile, and
in a build that runs a compile per core, its 12.8% more cycles take cores the other compiles would
use. `--deferred-bodies` keeps it available, with source order's result, for the work that would
change that.

**The serial share is where the compile's time is.** On 8 lanes with deferred bodies, about 9 ms of
the 104.6 ms that `--time-stages` counts runs on lanes: the emission's 4.6, the bodies' walks
(about 4) and the merge's pass (0.7). The other 91% is serial:

| serial part | ms |
| --- | ---: |
| the reading's top level's walk | about 16 |
| the reading's phases after the merge, the expressions' walk the largest | about 27 |
| preprocessing | 10.1 |
| `check-expressions` | 10.1 |
| `check-program` | 10.1 |
| the parse | 7.4 |

These figures precede #1404's post-merge rounds. The independent portions of the controls,
binding answers, constant contexts, expressions and graph records now run on the same lanes.
The top-level walk and shared preparation remain serial; runtime VLA/region work and other
exceptional expression parts retain source-order execution.

**Body parsing stays serial.** The parse is 7.4 ms, 7% of the `-w` compile. The reader's walks read
each body's parse, so parsing on lanes would pay only after the checking.

### The post-merge measurements and decision (#1404)

[The post-merge record](https://github.com/buster14a/buster-performance/tree/10dd107714257b05b618cd6a99430a19e13973ed/docs/measurements/postmerge-checker-1404-2026-11-05)
measures implementation `5abff541` against main `e7d3fc79` on the locked 9700X,
CPUs 0–7. Native and ThreadSanitizer checks compare all columns and diagnostics,
including reordered identifiers, warning metadata, graph relocation and failure
recovery. A delayed-caller fixture requires expression parts to complete on both
the caller and a nonzero lane. The driver's spare-worker lease ends as soon as
the last lane round joins, before ordered publication and serial finishing.

For the same implementation binary, at its default eight lanes, `-c sqlite3.c -w`
has these medians over 21 rotated runs per case:

| | Source order | Deferred |
| --- | ---: | ---: |
| Whole-process wall | 111.064 ms | 108.427 ms (−2.37%) |
| User cycles | 633.504 million | 825.271 million (+30.27%) |
| Minor faults | 8,524 | 25,132 |
| `wait4` peak | 324,916 KiB | 386,252 KiB |

With warnings, wall time changes −1.64% and user cycles +21.61%. The matched
baseline/implementation control with deferred checking enabled changes −1.27%
wall and +15.75% cycles, over three fresh copies of each build. With it disabled,
that control changes −0.13% wall and −0.15% cycles. These are the recorded code
placements, not multiple internal function placements.

The explicit 2–8-lane sweep preserves every object's bytes. Two deferred lanes
cost 4.54% more wall time, three cost 0.42% more, and four through eight save
1.38%–2.88%, while user cycles increase 15.76%–30.31%. A representative stage
report confirms eight actual expression workers and no retry, but planning and
ordered publication still limit the gain. Summed `expressions.work.*` rows are
lane work, which overlaps in elapsed time.

The #673 real-make control balances both modes and all three policies, with
24 measured cohorts per mode/policy/workload. All 432 cohorts exit zero and
produce identical objects. The default decision includes the concurrent costs:

| Eight-job workload | Policy | Source wall | Deferred wall | Deferred user-cycle change |
| --- | --- | ---: | ---: | ---: |
| Eight SQLite | Jobserver | 362.420 ms | 360.658 ms | +0.39% |
| Eight SQLite | Free eight | 378.337 ms | 429.435 ms | +10.19% |
| Two SQLite + six tiny | Jobserver | 188.887 ms | 191.256 ms | +10.55% |
| Two SQLite + six tiny | Free eight | 191.514 ms | 196.695 ms | +20.30% |

The one-worker controls keep source-order reading and are essentially unchanged;
the tiny-unit ranges overlap. Receipts confirm real inherited jobserver ownership
and free-lane runs without it. Deferred checking provides no concurrent throughput
gain in these controls.

**Deferred checking stays opt-in.** Moving the independent post-merge work onto
lanes buys a small isolated latency reduction at higher lane counts, with more
CPU work and memory cost. The source-order default and the explicit
`--deferred-bodies` / `--no-deferred-bodies` options remain. Shared preparation,
exceptional expressions and publication stay ordered; body parsing stays serial.

## Tests and measurements

`./build/compiler test-check-lanes [--executable PATH] [--seed N]
[--programs N] [--id-order N] [--keep]` (`c_check_lanes_test.c`):

- the external ownership protocol, against independent pipe/FIFO byte contents:
  empty and partial supplies, exact exceptional bytes, caps, malformed and
  overridden flags, escaped paths, stale/reused descriptor numbers, resource
  limits, unchanged inherited flags, caller signal state, ignored SIGCHLD,
  and whole compiler-process-group SIGTERM/SIGKILL; and growth: no guardian
  without a spare byte, each growth bounded by what it asks for and what the
  pipe holds, the start's report kept, every lease's bytes returned once, and
  none on a dry run or without a jobserver;
- budgeted real emissions and their failed-start/resource retries, by both
  paths with and without rows/unwind: one lane's objects or failures, with
  every owned byte returned once;
- a growing team in the driver, under a jobserver pipe of no byte or one,
  whose lane 0 supplies more after its first part: the team reported at its
  end and its growth, up to `--lanes 4`, one lane's object, and every byte
  back once, those it had no lane for never taken;

- generated programs of up to 600 functions of many kinds, among them
  libgcc's functions in random first-use orders, computed gotos through static
  tables of labels' addresses, switches, structs by value, long doubles and
  variadic calls, emitted on one lane and on 2, 3 and 8 lanes, on 8 with one
  lane slow, and on 8 that can't be pinned (CPUs past every one `lane_pin`
  takes), with `-g`'s rows and unwind tables and without: each the one lane's
  object, byte for byte, by each path, run again on one lane only when its lanes
  didn't start;
- the lane rule on programs of 1 to 1,200 functions;
- failing programs whose bodies fail at different steps in both orders: one
  lane's failure at every lane count;
- the identifiers' IDs in another order ([#153](https://github.com/buster14a/buster/issues/153)):
  each generated and failing program compiled again by the same path with
  the driver's `--id-order` table (`check_driver_id_order`, from `--id-order
  N` on, 153 by default), which must have every name of the first compile,
  most of them at other IDs, then emitted on one lane and on 8, with `-g`'s
  rows and unwind tables and without: the first compile's object or failure
  on one lane, byte for byte;
- a lane short of room (`CheckProgramLanes.squeeze_body`, the tests' alone):
  the lane that takes a body's part has only so many bytes to spare in one of
  its arenas while it makes its writer and works on the part. In a generated
  program of its own seed and in the failing programs, by each path, with `-g`
  and without, the bodies with the fewest and the most tokens are squeezed in
  each arena from no room up, doubling, until the part fits: each emission one
  lane's object or failure (the generated program's an object, the failing
  programs' a failure), the sweeps reaching each reason a lane short of room
  has, on lane 0 and on the others;
- lanes that start no thread, in a child limited to the processes it has
  (`RLIMIT_NPROC`, which doesn't limit root), under an alarm that ends it if
  its lanes hang: one lane's object or failure;
- the driver's objects and executables by `--lanes 1`, `2`, `3`, `8` and by
  default, and by `--lanes 8 --id-order N`, by each path: the same bytes, and
  executables that exit alike; with lanes that start no thread, by `--lanes 8`,
  `--lanes 1`'s objects and executables; the failing programs' exit statuses
  and diagnostics by `--lanes 8`, with lanes, with the IDs in another order and
  without lanes, `--lanes 1`'s; and the values of `--lanes`, `--worker-budget`
  and `--id-order` it refuses.

The lanes' serial retry, which the last three exercise, was otherwise untested
([#538's review](https://github.com/buster14a/buster/pull/538#pullrequestreview-5664568010)).
Mutants of the emission that ignore each of its reasons to run again, or that
never run again, each fail the test.

`./build/compiler benchmark-check-lanes [--passes N] [--lanes LIST] [--cpus
LIST] [preprocessor options] file...` (`c_check_lanes_benchmark.c`) times the
emission of a file's checked program on each lane count and by the rule, with
the steps apart (`CheckProgramLanes.timing`). The program is the one `-c`
compiles for the target its options give (`-march` and the `-m` flags, and the
predefines a function's `target` attribute resolves against); before #1045 it
was always the baseline x86-64 profile's. Since #1057 the file is prepared by
the preparation the source clients share ([the request
guide](compilation-requests.md)), which refuses what the driver refuses and
runs `-c`'s target-feature check, and the report gives its configuration's
receipt. A unit `-c` would refuse is left out, as the other source benchmarks
leave it out, and a run that measures no file fails. `test-checker` runs it on a unit of
512-bit vectors (`c_check_lanes_benchmark_test.c`), measured with
`-march=znver5` and refused for the baseline profile, as `-c` refuses it, and
on a baseline unit whose function's `target("avx2")` attribute takes a 32-byte
vector, measured only with the predefines.

### The 9700X

The 9700X CI job's `benchmark-check-lanes` step (#1069): under the host's
lock, lane 0 on CPU 2 and the others on one CPU of each other core
(`--cpus 2,$others`), medians of 11 runs. Clang 23.1.4 built the compiler
(`BUSTER_HOST_ARCH=znver5`, `BUSTER_DEBUG_INFO=0`). The frozen `sqlite3.c`
uses the CI step's options, `-march=znver5` among them, for which the
benchmark emits znver5 code, as `-c` compiles it (#1045): 2,602 functions,
554,920 tokens in their bodies, the largest 28,603, and 1,227,192 bytes of
code. The run is #1072's, at
`c8a4838b`; [the record](https://github.com/buster14a/buster-performance/tree/29d000e41628428f478785fc6d4c5e20f40603c2/docs/measurements/function-lanes-znver5-1069-2026-11-01)
has its log and an earlier run's. Times are in milliseconds, with each
row's fastest and slowest of the 11.

| Lanes | Emission | Fastest | Slowest | Speedup | Preparation | Lanes' part | Merge |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 19.8 | 19.6 | 20.2 | 1.00 | 1.9 | 16.4 | 1.6 |
| 2 | 12.6 | 12.5 | 12.8 | 1.57 | 1.8 | 9.1 | 1.7 |
| 3 | 10.2 | 10.1 | 12.8 | 1.93 | 1.8 | 6.7 | 1.7 |
| 4 | 9.1 | 9.0 | 9.6 | 2.17 | 1.8 | 5.6 | 1.7 |
| 6 | 8.3 | 8.1 | 9.7 | 2.40 | 1.9 | 4.7 | 1.7 |
| 8 | 8.1 | 7.9 | 12.9 | 2.45 | 1.9 | 4.5 | 1.7 |
| The rule's (8) | 8.0 | 7.9 | 11.3 | 2.47 | 1.9 | 4.4 | 1.7 |

The lanes' part takes 3.6 times less time on 8 lanes than on one. The
preparation and the merge stay serial, 3.6 ms, 44% of 8 lanes' time; most of
both is passes over every token of the unit. The 8-lane rows have the widest
spread: their slowest runs are up to 60% over their medians. The earlier
run's forced 8-lane median was 8.8 ms, its lanes' part 5.2 ms (3.1 times less
than one lane's); its 1, 2 and 4 lanes and the rule's row stayed within 0.6%
of this table's medians (8.07 against 8.02 ms for the rule's row).

The table before #1069 (clang 23.1.1) timed baseline x86-64 code, though the
step passed `-march=znver5`, and an earlier compiler: 36.6 ms on one lane and
11.9 on 8, the lanes' part 4.9 times less. Its difference from this one mixes
the target, the toolchain and the compiler's other changes since.

The whole driver, `-c` of #136's frozen copy of `sqlite3.c`, 11 rounds in a
rotating order, against main (`e04462c1`). The objects are byte-identical:

| | Median | CPU time |
| --- | ---: | ---: |
| main, on CPU 2 | 192.8 ms | 191.5 ms |
| `--lanes 1`, on CPU 2 | 193.7 ms | |
| by default, on CPUs 0 to 7 (8 lanes) | 168.6 ms | 219.7 ms |
| `--lanes 4`, on CPUs 0 to 7 | 171.7 ms | |
| by default, on every online CPU (15 lanes) | 170.0 ms | |

Eight lanes take 12.6% off the compile. One lane runs 0.02% more user
instructions than main (`perf stat`, 2,290.7 million against 2,290.2), and its
times are within the rounds' spread of main's (±0.5%). Lanes on both SMT
siblings of a core cost about 1 ms against one lane per core, which isn't worth
reading each CPU's topology for in every compile.

The rule also holds on smaller units. On generated programs of 403 functions
(26,892 tokens in their bodies, the largest 6,034), it takes 4 lanes: 0.66 ms
against 1.51 on one lane, 2.29 times faster, where 6 lanes are the best at
2.48. On programs of 603 functions (40,125 tokens), it takes 6 lanes: 0.90 ms
against 2.29, 2.53 times faster, where the best is 2.69, also on 6.

### Backend startup for a unit without selected bodies (#590)

The emission initializes the encoder's templates, x86 operand semantics and
Tier 0's tables only when `CheckProgram.body_count` is nonzero. It initializes
unwind tables under the same condition when unwind output is requested. The
selected program's builtin and asm helpers come only from selected bodies, so
a unit without bodies has no record sizing, encoding or unwind consumer.
Data and symbol publication, attribute checks, linking and failure rollback
still run. A later unit in the same process initializes the tables normally
before its workers start; data-only emissions preserve already initialized
state.

`test-check-lanes` checks empty units, initialized data with string references,
and dropped static functions containing a builtin helper, on both scalar and
vector paths. Cold objects equal objects emitted after a real function has
initialized the backend, with and without source rows/unwind and with a lane
team requested. Data-only resource failures leave the tables cold. The test
runs in an isolated child so it also works after a caller has used the backend.

This does not change the tables or their hot lookup paths. The encoder's
fixup-copy layout retains the measured choice documented in
[x86-64-encoder.md](x86-64-encoder.md#fixups); lazy/subset tables would also have
to cover validation, arbitrary asm forms and helpers before the workers start.
Since #1078, `build.sh` bakes the tables into the executable, so a unit with
selected bodies starts with them built too. The initializers run only where the
executable isn't baked ([compile-time](compile-time.md), "Baked tables").
