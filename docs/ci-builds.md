# Benchmark compiler preparation

The 9700X job prepares its current compiler, pragma oracle and any required
pull-request base in one step, before its native correctness or measurements. This is
build scheduling only: the [CI admission policy](commands/ci.md), host lock,
benchmark selection, compiler flags, placement offsets, A/A controls and
measurement commands are unchanged.

## Selection and reuse

`tools/ci/build-benchmarks.sh` receives the existing checker and Tier 1
selectors from `9700x.yml`. Only a pull request selecting either comparison
gets a base worktree, pinned to `github.event.pull_request.base.sha`. Empty
selector outputs retain the workflow's existing `!= 'false'` behavior; the
shell receives explicit booleans, never an implicit empty decision.

When the checker comparison is selected, each revision runs `build.sh shifts`
once, producing its ordinary Release compiler and placements 0, 1088, 2112 and
3136 of `main_unit_checker.c` from shared objects. Otherwise each needed
revision runs the ordinary Release build. The Tier 1 comparison uses that
same base worktree's ordinary executable, including when the checker
comparison also runs. There is no second base checkout or build in a
measurement step.

The worktree remains alive until the benchmark job's final cleanup step;
executables may still need their revision's source-tree resources. Failed
preparation removes it immediately. The cleanup step is idempotent and runs
on failure/cancellation too; it removes only this run's recorded directory,
not other worktrees.

## One physical-core budget

`tools/ci/build-budget.c` reads the calling process's allowed CPU mask and
Linux package/core topology, keeping one allowed logical CPU per physical
core. Missing topology is an error, not permission to count SMT siblings as
independent cores. It divides these CPUs into disjoint command masks and
passes each build its explicit `BUSTER_BUILD_JOBS` limit, which the glue gives
to `build.sh --jobs`.

On eight available cores, a current/base/oracle preparation receives 4, 3 and
1 cores respectively. Without a base, the current build and oracle receive
7 and 1. The oracle's single-core allocation is part of the total, not an
extra process on top of eight compiler jobs. These are deliberately simple,
static partitions; finishing a command does not redistribute its allocation.
With fewer cores than commands, batches serialize, including the one-core
case. No separate core reservation overrides the runner's existing affinity
or the shared host lock.

Each command gets its own process group. The supervisor propagates failures
and cancellation, terminates the remaining groups, escalates to SIGKILL after
one second if needed, and reaps orphaned descendants as a Linux child
subreaper. Nested groups created by build.sh or Ninja are handled through adopted children too.
Parent death also signals the supervisor to stop its groups. It publishes
success only after there are no children left.
The shell forwards cancellation to the supervisor and waits for it before
removing the worktree. This barrier separates compilation from timed work.
The standalone branch-sizing fixtures keep their existing, separate
build-before-timing sequence.

The supervisor prints each allocation and the preparation batch's wall time.
Use locked Benchpress runs to compare total job/preparation time against the
sequential workflow. Concurrent resource contention and static partitions
mean a speedup must be measured; no fixed speedup is claimed here.

## Regression coverage

Run `bash tests/ci/build-benchmarks.sh`. It builds the small C supervisor and
probe, checks plans with 1 through 16 physical cores and SMT/socket fixtures,
proves two builds overlap using a rendezvous rather than a timing threshold,
and checks a one-core fallback. Disposable Git repositories and toolchain
stand-ins exercise all selector combinations, exact base identity, every
placement, reuse, publication after all processes exit, build/oracle failure,
cancellation of grandchildren and nested process groups, and worktree cleanup. They neither build the
Buster compiler nor fetch from the network.

The workflow's hosted `evidence` job runs these fixtures on pull requests
before hardware admission. They are functional tests, not performance
evidence. Compiler correctness and actual hardware timings still belong to
the normal hosted and locked 9700X jobs.
