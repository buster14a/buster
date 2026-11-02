# deferred

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Deferred function bodies

`src/buster/compiler/c_check_deferred.c` is #113's deferred reader:
`check_input_deferred` checks a unit as `check_input` does, but reads each
file-scope function body apart from the top level, out of order on any number
of lanes, from the reader's state at the body's opening (the history's heads,
a view of the top level's table), and merges the bodies in source order
(`c_types.c`'s change records and `type_replay`), into source order's result:
the merge's pass over the binding events and the token columns runs on the
lanes the bodies ran on, each a share of whole bodies by their tokens
(`check_deferred_pass_items`). A unit it can't merge yet (a body whose
declaration with linkage changes its entity's linked state, one that makes a
union of the top level's transparent, one whose walk reports an error, and a
unit with overloadable functions) is checked in source order, with the reason.
The driver still checks in source order.

The reader runs one lane, and one more per `BUSTER_CHECK_DEFERRED_LANE_TOKENS`
(and one per `BUSTER_CHECK_DEFERRED_LANE_TOP_TOKENS` of the top level's tokens)
of the bodies' tokens beyond the larger of the largest body's and twice the
second largest's, no more than its lanes or the bodies: a lane's copy of the
reader, which grows with the top level, and its wake-up must be paid for by its
share of the walks. The calling thread cuts the bodies into parts before the
lanes start (`parts_by_cost`), stably promotes the part containing the largest
body, and reads that part at once as the first lane. The others, woken for the
bodies, take the rest in the remaining cost order from the counter
and read at about half its speed. The state (`CheckDeferred`, zeroed first)
keeps its memory across its readings, each lane's arenas one mapping reserved
on the calling thread before the lanes start, until `check_deferred_release`.
A lane copies the top level's table but none of its per-name columns, which are
the lane's own at places they keep from one reading to the next
(`check_deferred_lane_names`), each set for the names a body spells before its
walk, nor its binding events: a body's stream starts with proxies of the file
scope's events its heads reach (`check_deferred_proxy`).

`./build/compiler test-checker-deferred [--id-order N] [--keep]` checks 96
fixtures of the issue's cases through the scalar path, the vector path and the
deferred reader on 1, 2, 3 and 8 lanes with the stacks, the batch and the direct
resolution, and on 8 with the identifiers' ids in another order (#153):
the deferred result must be the vector path's column for column, merged or
left to source order as each fixture says, on every lane its bodies can take
and by the lanes' rule, its reader copied only by lanes that take a part, in
as many parts as a group of its lanes cuts. Eight fixtures of bodies of known
tokens must take the rule's lanes on each side of its steps, their largest body
the first lane's, also when the first lane starts after every other lane's
first take (`CheckDeferred.late_first`). Six grouped-part fixtures around
128 and 512 parts also require the first lane to read the largest body, with
that lane delayed under the reader's rule and with all lanes forced. Each that
merges must give source
order's result again with its merge failing after the replay and when a state
that read it before reads it again (its memory poisoned or as left, and kept,
or its lanes' stamps then at their last, which its bodies wrap),
and count as a resource failure with its state's allocation failing; timed, one
lane must wait for no other, preserve
its lane/counter/running state and affinity inside a live team on distinct
CPUs, and leave that team usable with the same workers. Eight
lanes' dispatch and wake-up must fall within their span; and the benchmark's
`--cpus` parser must refuse 65 CPUs without a write past its room for 64.
A lane's per-name columns must be laid out with their names' entries
committed, kept, cleared after a poisoned reading and laid out again for more
names, cleared where the kernel keeps their old pages
(`check_deferred_test_lane_names`), and the lanes' mappings and the
state's own arenas must be base pages (`check_deferred_test_memory`).
Empty, duplicate-position and nonzero-start linked histories are checked against
a forward oracle with their ends at a protected page; the last update at the
opening must be selected. The merge's scans of the token columns
(`check_deferred_entries` and the others) must give their scalar twins'
positions and counts on ranges ending at a protected page, and every merged
fixture must merge alike with the twins (`CheckDeferred.scalar_merge`).
`test-checker-corpus --deferred N
[--deferred-bindings stacks|batch|direct]` adds the reader to a corpus's
comparison, on every lane the bodies can take up to N, and tallies the units
left to source order by reason. `./build/compiler benchmark-checker-deferred
[--passes N] [--lanes N] [--cpus LIST] [--every-lane] [--warm] [-E's options]
file...` times source order's reader against the deferred one on one lane and
on up to N, with the parse, the top level, the bodies' walks, their position
checks, the lanes' span and the merge apart (and the merge's parts: its replay,
appended tables, events, tags, pass and rest), and what a lane costs: its copy of
the reader, the launcher's dispatch, the other lanes' wake-up, which no lane
waits for, and the lanes' arenas before the lanes start; and in how many passes
the first lane read the largest body. Each deferred reading releases its state,
as a process's one reading does, or with `--warm` keeps it for the next, as a
process's later readings do. One active lane uses
the existing `lane_run_cold(1, ..., NULL)`, which starts no worker, makes no
affinity calls and isolates the reading without changing generic team validation;
multiple lanes retain `lane_run` and their CPU list. Its passes take the six
orders of its three readings in turn, so `--passes` is a multiple of six (24).
`--every-lane` runs every lane the bodies can take, whatever
their tokens; `--cpus` pins the command to the first
CPU and a lane team to the list (on the 9700X, `--cpus 2,0,1,3,4,5,6,7` under
its lock). The 9700X job runs the fixtures, c-testsuite and the frozen SQLite
source with `--deferred 8`, and the benchmark where the checker changes.
[docs/c-deferred-bodies.md](../../docs/c-deferred-bodies.md) has
the design, the corpora and the measurements.

`./build/compiler test-bindings [--seed N]` checks the standalone scoped C
binding kernels: stack replay and the vector batch against an independent
scope oracle, 100,000-level nesting, protected-page tails and allocation
failure. `benchmark-bindings [--events N] [--names N] [--only scalar|batch|sort]
[--after-scalar MS]` measures synthetic events under the 9700X lock, pinned to
CPU 2. `test-parser-bindings [--clang [PATH]] [--seed N] [--count N]
[preprocessor options] [file or directory...]` checks real-source declaration
identities, both parser schedules and both resolvers. Without files it runs
scope fixtures, generated programs, K3 fallback checks and allocation failure
tests. `benchmark-parser-bindings [--passes N] [--only
prepare|scalar|batch|sort|source-scalar|source-batch] [--after-scalar MS]
[preprocessor options] file...` measures source-event production, resolution
and the combined parser/adapter/consumer path. Run benchmarks on the locked
9700X, pinned to CPU 2. [docs/c-bindings.md](../../docs/c-bindings.md) describes the
opt-in adapter, the declaration-token columns, corpus provenance and the
explicit cost of its scalar grammar replay. The syntax-only driver does not
request these columns.
