# ci

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Sharing the 9700X

Correctness CI runs on GitHub-hosted Ubuntu 26.04 with the official stable
Clang/LLVM 23.1.4 release, in baseline x86-64 and native host builds. Native
builds on CPUs without the complete vector feature set use the scalar fallback.
Each row's inventory runs as 4 shards, a runner each that builds the compiler
itself (a native build must run on the CPU it was built for): `parallel --shard
K/4 --costs tools/ci/correctness.costs` deals the commands by their hosted
costs, the same deal on every shard's runner, so the shards run each command
once. A plain row's test step took 14.6 minutes on one 4-CPU runner; its shards
take about 4 to 5. The steps a row needs once (the baseline fallback, QEMU and
arena checks, the parser smoke test, the performance readers and the TSan/MSan
`main.c` compiles) run in its shard 1. Refresh the costs from a hosted run's
report when the shards' times drift apart ([the runner guide](parallel.md)).
On pushes to main and the daily and dispatched runs (not on pull requests, which
they would hold up for over an hour), each host architecture also runs the
inventory under `BUSTER_SANITIZER=address|undefined|thread|memory`, but the
compiler's own `main.c` checking comparison in the TSan and MSan rows, whose
measured memory (docs/ci.md) exceeds a hosted runner's; their `-c` compiles run
in a separate step after the suite. ASan combines UBSan, bounds,
leak and stack-lifetime checks; TSan and MSan use separate builds. Diagnostic
builds default to `-O3`, frames and line information, with fatal symbolized detector
proofs and child-report auditing. `BUSTER_FUZZ_AVAILABLE=1` adds in-process
coverage and the `--fuzz` driver override; `BUSTER_FUZZ_ENTRY=FILE.c` replaces
its byte-input callback. Hosted frontend, ELF and crash/replay campaigns gate
the same runs. Additional full ASan/UBSan suites run at `-O1`;
`BUSTER_SANITIZER_OPTIMIZATION=1|2|3` selects an explicit level.
See `docs/ci.md` for usage and the precise coverage boundaries.
After all the hosted variants and fuzz jobs a run has pass on the same triggering revision, Benchpress
requires the complete vector feature set and runs native correctness before
benchmarks, reusing one build and the existing shared lock. All correctness builds run
the same suite inventory, `tools/ci/correctness.tests`. Failed, cancelled or
skipped hosted tests cannot queue the hardware job; failed native tests cannot
start benchmarks. A pull request's run stops at its first failure: its test rows
fail fast, and its tests don't start when the guide-size or DCO hook check
fails. Pushes to main, the daily and dispatched runs report every row. The
existing required check name is retained by a hosted aggregation job that fails
when either stage fails.
Draft PRs retain hosted feedback but cannot queue hardware. Ready/draft transitions
start a new run and cancel the superseded run in the same PR group. The required
aggregate explicitly fails a code-changing draft until ready; the selector's
legitimate no-code exemption still passes. Its C predicate in
`tools/ci/qualification.c` exhaustively tests the allowed outcomes, including
missing/invalid/skipped results and event/selector consistency.
See [the CI guide](../../docs/ci.md) for tool and frozen-input setup.

Agent sessions and CI share the 9700X: the buster14a org's self-hosted runner
runs on the same machine. Its timings only mean something when one thing runs
there at a time, so everything takes one lock:

- Run every build, test, benchmark and perf run there through
  `EXCLUSIVE_OWNER=<name> ~/buster-fast-bench/exclusive.sh COMMAND...`.
  Also set `EXCLUSIVE_TITLE` to the session's title and `EXCLUSIVE_LINK` to
  its link (its claude.ai page, or else the PR or issue it works on): whoever
  waits sees them in `/var/lib/buster-9700x/owner`.
- The runner's job hooks take the same lock for every CI job. Sessions and CI
  jobs wait for it alike: neither goes first. A CI job's owner lines give its
  workflow, job, PR number and title (or a push's first line), the run's link
  and the PR's.
- Hold the lock per run, never around a series of runs: a CI job waits for it
  and times out after 10 minutes.
- Every pull request's job queues behind every other's, so keep the job short.
  A pull request runs a benchmark's plain run, which checks its paths against
  their scalar twins, where it changes what the benchmark measures (the
  `changes` job's `measure`): a file that the benchmark's translation unit
  includes, for one built as a unit of its own (the build's `*_benchmark.c`),
  or one that its entry in `changes_benchmarks` names, for one in `main.c`'s
  units (the files that define the functions it calls); or the benchmark's own
  step. A change to the build's description (`build.sh`, `CMakeLists.txt`,
  `cmake/`) runs every one, and so does a push to main before the nightly mode
  (below), so that main has each benchmark's numbers at every commit; with it,
  a push measures what its first-parent change selects, as its pull request
  did (the selector's targeted push), and the nightly backlog measures the
  states no push measured. A new benchmark's
  step gets an entry in `changes_benchmarks` and an `if:` on `MEASURE`, as the
  others have. Its isolated and after-scalar samples (`--only`, `--after-scalar`,
  repeated processes), which cost about 200 ms before each of 21 samples per
  case, go in a step of their own that runs where the `changes` job's output
  for the module isn't `false`: on the daily scheduled run, dispatched runs and
  pull requests that change the module's sources (its `changes_modules` entry,
  which no `*_test.c` matches: tests don't change what the samples measure).
  Keep the pattern to what the samples measure: a module's own decision
  campaign (#336's binding handoff) has a module of its own, not the whole
  checker's. Samples of different code split by what changes each: #336's
  time the default handoff (`direct`) where its inputs change, and the
  comparison of every mode only where the handoffs themselves do
  (`checker_binding_modes`); #395's time the passes the rows' producers reach
  (`emit` and `build`) where the emission, the lowering or Tier 0 change, and
  every pass only where the rows' own passes do (`check_debug_rows`).
  The daily and dispatched runs time them all. A step gated so also runs on
  the pull request that changes it.
  The same holds for the linkers' baselines and floors, mold's and wild's
  foreign phases, `clang -E`'s times beside the preprocessor's benchmark
  (`--clang`, its harness `c_preprocessor_benchmark.c`) and the first-party
  performance readers' qualification (`tools/performance`, `docs/performance`,
  `web/perf`, their pin; a change to `docs/performance` alone runs the job for
  it). A push
  to main runs none of them before the nightly mode: its pull request's run
  measured what it changes, and the full set took the job past its 10 minutes.
  The daily and dispatched runs, and the nightly snapshot's, have 25 minutes;
  a nightly backlog run has a pull request's 20.
- The `changes` job's selector is `tools/ci/changes.c`, with those tables:
  the job builds it with the hosted runner's `cc`, then `--self-test` checks it
  before it decides, and fails the job (so that `test` runs everything) when
  its tables and the workflow disagree: an output a step reads that it doesn't
  write, a `MEASURE` name that isn't a benchmark or a benchmark no step runs, a
  step that doesn't start its `MEASURE` name's `./build/compiler
  benchmark-NAME` (or `measure-NAME`) on a line of its `run:` script, after
  only the line's indentation, a `run:` and a `taskset -c CPUS` (the selector
  finds a step's benchmarks by those lines alone, not in other keys' values,
  folded blocks, quotes, comments, joined lines, here-documents' bodies or
  parentheses earlier lines left open (an array's elements, and a subshell's
  too), or after a `case` pattern's unmatched `)`, though it doesn't read the shell's control flow; mutants of `benchmark-types`'
  step that hide its command in each must fail), a gated step that doesn't run when changed alone, a pattern alternative or a
  unit that names no source; or when a fixture's decision changes (docs alone,
  `docs/performance`, `web/perf`, tests alone, the binding events' producers,
  `zen5`'s digit, closures through includes, the build's description, pushes).
  It also fails when the hosted test job's shards disagree
  (`changes_shard_policy`): its matrix's `shard: [1, ..., N]` must count from 1
  without a gap, and the same N must be in the job's name, the run step's
  `--shard "${{ matrix.shard }}/N"` and the O1 rows' include entries (shards 1
  to N of each architecture), since a list and an N that disagree would leave a
  shard's commands unrun with every job green. Run it
  locally with `cc -std=c17 -O2 -Wall -Wextra -Werror -o /tmp/changes
  tools/ci/changes.c && /tmp/changes --self-test` after changing a gate, a
  benchmark's step, the tables or the shards. Its oracle policy pins the 9700X
  job's `needs` (`[changes, evidence, test, fuzz]`), its `if:` (the `evidence`
  job's success and admission, hosted rows skipped only by evidenced reuse on a
  dispatch, fuzzing only in the plain scope) and the oracle cache's modes, with
  counterexamples for each.
- `~/buster-fast-bench/README-9700x.md` on the host describes the setup and how
  to undo it.

## Reuse and the nightly backlog

`tools/ci/nightly.c` makes the decisions, `tools/ci/nightly.sh` is their API and
Git glue, and [the CI guide](../../docs/ci.md#nightly-main-measurements) has
the policy. In short:

- Every `9700x.yml` run has an `evidence` job, which must succeed. For a push
  it checks the merged pull request's newest run: the identical tree, its 9700X
  job, aggregate and plain rows green, within a day, its coverage record
  agreeing. A push it qualifies that way skips its 9700X job in the nightly
  mode, and the required context passes, naming the source run. A nightly
  dispatch's `evidence` job checks that the commit is a first-parent state of
  main, may reuse the commit's own earlier plain hosted rows, and enforces
  04:00-07:00 Europe/Madrid. The 9700X job checks the window again after it
  takes the lock.
- Every run's `record` job writes the `coverage-record-ATTEMPT` artifact. The
  coordinator, `.github/workflows/nightly-backfill.yml`, harvests those
  records into its ledger. It pins the cycle's snapshot, prepares hosted
  qualifications before the window, and dispatches `9700x.yml` on
  `nightly/SHA` branches with `purpose` `backlog` or `snapshot`. It runs one
  9700X run at a time, and only work that fits before 07:00.
- `vars.BUSTER_NIGHTLY` set to `on` is the switch. Before it, the coordinator
  only harvests, plans and reports; pushes and the daily `23 7 * * *` run work
  as before. With it, pushes measure targeted, the coordinator dispatches, and
  the old daily run retires.
- Tests: `bash tests/ci/nightly.sh` (the self-test, also under ASan and UBSan,
  and the glue against a fake `gh` and a merge history) and
  `bash tests/ci/caches.sh` (the 9700X's caches). Pull requests run both in
  the `evidence` job before any hardware; `changes` runs
  `nightly --self-test`, which also fails if the build or the inventory ever
  calls `git`, since tree-identity reuse depends on that.

## Queue and execution timing

The hosted-only `CI latency report` workflow runs at 07:07 Europe/Madrid and on
manual dispatch. It samples the latest 100 main CI workflow runs and reports the
median and nearest-rank p95 separately for PR, main-push, nightly targeted and
full (daily, dispatched, snapshot) runs, a nightly backlog run told apart by its
run name. Besides queue and execution, it reports the host's occupation, from
the job's first step (after the runner hook took the lock) to its end. It keeps
the raw API responses and normalized observations as workflow artifacts. It
does not run benchmarks, weaken the hardware gate, or change any schedule.

Read `docs/ci.md` for metric definitions and limitations. In particular, runner
hook duration includes overhead and is not an exact shared-lock wait; missing
timestamps are unknown rather than zero. Run its network-free fixtures with
`bash tests/ci/timings.sh`. Keep stored raw evidence in `buster-performance`, not
in this repository's retired `docs/measurements` path.
