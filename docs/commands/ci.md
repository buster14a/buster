# ci

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Every main revision

Every merge or direct push to `main`, including documentation/Markdown-only
changes, starts hosted correctness against that push's `github.sha` (#1271).
In the nightly mode it runs the plain rows only (#2057, the owner's choice on
2026-11-10); the sanitizer rows and fuzz campaigns run on the nightly
snapshot's daily run, until the merge queue (#1430) runs the complete suite
before each merge and pushes run no checks. Before the nightly mode a push runs
the full matrix and fuzzing. Main pushes use a separate run-id concurrency
group each, so rapid merges do not replace pending main runs. PRs still cancel
only their own superseded runs; the hardware lock still serializes the 9700X.

Do not add main-push path filters, treat a successful PR run or Measurement
storage as a push's own hosted rows, or let native-evidence reuse suppress
them. `changes --self-test` checks the trigger, concurrency and hosted-scope
policy with counterexamples; `tests/ci/nightly.sh` checks that every push's
evidence keeps `hosted=run` and no hosted reuse source, with `scope=plain` in
the nightly mode and `scope=full` before it, including documentation-only
pushes.

## Sharing the 9700X

Correctness CI runs on GitHub-hosted Ubuntu 26.04 with the official stable
Clang/LLVM 23.1.4 release, in baseline x86-64 and native host builds. Native
builds on CPUs without the complete vector feature set use the scalar fallback.
On a pull request the native row is the vector row instead (#2033): the
explicit vector target (`BUSTER_HOST_ARCH=vector`, `host_isa.h`'s predicate),
whose 4 shards are items that hosted runners claim once a probe found their CPU
qualifies, drawn in up to 4 levels, with no emulation; an item no qualifying CPU
claimed fails it as incomplete ([The vector row](../ci.md#the-vector-row)).
Its claim's race fixture is `.github/workflows/vector-claim-race.yml`.
Each row (`.github/workflows/hosted-correctness.yml`, which the test job calls
per row) runs its inventory as 4 shards, a runner each: `parallel --shard
K/4 --costs tools/ci/correctness.costs` deals the commands by their hosted
costs, the same deal on every shard's runner, so the shards run each command
once. A portable `x86-64` row builds its compiler once, and its shards verify
and run that product (`tools/ci/hosted-product.sh`; [Shared row
products](../ci.md#shared-row-products), #2032); a `native` row's shards each
build their own (a native build must run on the CPU it was built for). Run
`bash tests/ci/hosted-product.sh` after changing the product's packing, identity
or checks. A plain row's test step took 14.6 minutes on one 4-CPU runner; its
shards take about 4 to 5. The steps a row needs once (the baseline fallback,
QEMU and arena checks, the parser smoke test, the performance readers and the
TSan/MSan `main.c` compiles) run in its shard 1. Refresh the costs from a hosted
run's report when the shards' times drift apart ([the runner guide](parallel.md)).
The repository syntax check retains each build's sanitizer feature flags and
predefines, while excluding its exact host-only detector and frame controls
(#1423). The compiler executing the check remains instrumented.
On the daily and dispatched runs, the nightly snapshot's daily run and pushes
before the nightly mode (not on pull requests, which they would hold up for over
an hour, nor on pushes in the nightly mode, #2057), each host architecture also runs the
inventory under `BUSTER_SANITIZER=address|thread|memory`, but the
compiler's own `main.c` checking comparison in the TSan and MSan rows, whose
measured memory (docs/ci.md) exceeds a hosted runner's; their `-c` compiles run
in a separate step after the suite. ASan combines UBSan, bounds,
leak and stack-lifetime checks; TSan and MSan use separate builds.
Standalone `BUSTER_SANITIZER=undefined` remains available for manual diagnosis,
but no longer repeats the full suite on every main push. Diagnostic
builds default to `-O3`, frames and line information, with fatal symbolized detector
proofs and child-report auditing. `BUSTER_FUZZ_AVAILABLE=1` adds in-process
coverage and the `--fuzz` driver override; `BUSTER_FUZZ_ENTRY=FILE.c` replaces
its byte-input callback. Hosted frontend, ELF and crash/replay campaigns gate
the same runs. The hosted matrix runs at `-O3` throughout;
`BUSTER_SANITIZER_OPTIMIZATION=1|2|3` remains available for explicit diagnostic builds.
See `docs/ci.md` for usage and the precise coverage boundaries.
After all the hosted variants and fuzz jobs a run has pass on the same triggering revision, Benchpress
requires the complete vector feature set and runs native correctness before
benchmarks, reusing one build and the existing shared lock. Pull requests never
run it (#2055, the owner's choice on 2026-11-10): hosted correctness qualifies a
pull request, and native correctness and measurements run on its merged
first-parent state in the nightly window. The required aggregate passes a pull
request on its hosted rows and says so. All correctness builds run
the same suite inventory, `tools/ci/correctness.tests`. Failed, cancelled or
skipped hosted tests cannot queue the hardware job; failed native tests cannot
start benchmarks. A pull request's run stops at its first failure: its test rows
fail fast, and its tests don't start when the guide-size or DCO hook check
fails. Pushes to main, the daily and dispatched runs report every row. The
existing required check name is retained by a hosted aggregation job that fails
when either stage fails.
Draft and ready PRs alike get hosted feedback and never queue hardware.
Ready/draft transitions start a new run and cancel the superseded run in the
same PR group. The required aggregate passes a code-changing draft on its hosted
rows as it does a ready PR, and the selector's legitimate no-code exemption
still passes. Its C predicate in
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
- A pull request that changes the checker's sources (the `changes` job's
  `checker` output isn't `false`) also compares `-c sqlite3.c` by its base and
  by its tree ("Compare check-expressions with the pull request's base over
  placements", #1050). Each is built at four placements of
  `main_unit_checker.c` (`./build.sh shifts`). The step then runs:
  - `time-runs` over the placements, with the base's executables again as the
    floor;
  - each placement's `measure-compile` stages;
  - `perf stat`'s counters, with failed store-to-load forwards;
  - the checker's functions' samples;
  - the forwarding failures by function.

  When only selected diagnostic sources, tests and documentation change, the
  resulting objects must be identical; other checker changes may legitimately
  change code generation, so differences are reported with both object sizes
  and the comparison continues (#1376, #1572). It takes about 90 s
  (`docs/c-checker.md`, "The per-node machinery").
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
  It also fails when the hosted rows' shards disagree
  (`changes_shard_policy`, on `hosted-correctness.yml`): its matrix's
  `shard: [1, ..., N]` must count from 1 without a gap, and the same N must be
  in the job's name, the run step's `--shard "${{ matrix.shard }}/N"`, since a
  list and an N that disagree would leave a shard's commands unrun with every
  job green; and when the row workflow weakens its shared products
  (`changes_row_policy`: the portable rows alone, the triggering revision, the
  ownership check on the build's tree, the shards' wait for the build, its
  SHA-256 and their verified install, nothing from a cache or another run); and
  when the vector row's probe, `host_isa.h`'s predicate and the build's vector
  flags fall out of step, or its claims stop being per attempt and
  non-overwriting (`changes_vector_policy`). The
  selector doesn't read the row workflow's steps: a change to that file runs
  the steps it gates on `performance`. Run it
  locally with `cc -std=c17 -O2 -Wall -Wextra -Werror -o /tmp/changes
  tools/ci/changes.c && /tmp/changes --self-test` after changing a gate, a
  benchmark's step, the tables, the rows or the shards. Its oracle policy pins the 9700X
  job's `needs` (`[changes, evidence, test, fuzz]`), its `if:` (the `evidence`
  job's success and admission, hosted rows skipped only by evidenced reuse on a
  dispatch, fuzzing only in the plain scope), the native correctness step's
  skip on pull requests alone and the oracle cache's modes, with
  counterexamples for each.
- `~/buster-fast-bench/README-9700x.md` on the host describes the setup and how
  to undo it.

## Reuse and the nightly backlog

`tools/ci/nightly.c` makes the decisions, `tools/ci/nightly.sh` is their API and
Git glue, and [the CI guide](../../docs/ci.md#nightly-main-measurements) has
the policy. In short:

- Every `9700x.yml` run has an `evidence` job, which must succeed. A push to
  main is admitted and runs its own native correctness: it reuses nothing
  from its pull request, whose 9700X job skips native correctness (#1272).
  A nightly dispatch's `evidence` job checks that the commit is a first-parent state of
  main, may reuse the commit's own earlier plain hosted rows, and enforces
  02:00-08:00 Europe/Madrid. The 9700X job checks the window again after it
  takes the lock, right after the host's OS and CPU-model check. That is
  before the host identity's package queries and hashing, and before any
  setup.
- Every run's `record` job writes the `coverage-record-ATTEMPT` artifact. The
  coordinator, `.github/workflows/nightly-backfill.yml`, harvests those
  records into its ledger. It pins the cycle's snapshot, prepares hosted
  qualifications before the window, and dispatches `9700x.yml` on
  `nightly/SHA` branches with `purpose` `backlog` or `snapshot`. It runs one
  9700X run at a time, and only work that fits before 08:00, unless a manual
  `mode: run` gives an `override` reason. The snapshot's comprehensive run is
  required every cycle. States start at the epoch (the merge that added
  `tools/ci/nightly.c`); earlier ones predate the worker and are reported as
  out of scope. A state whose branch GitHub refuses to create is skipped for
  the rest of the job and reported (docs/ci.md, "Refused branches").
- The coordinator reads each state's immutable `nightly.c` window declarations
  with `nightly worker-window`, cached by source blob and rebuilt for every
  job. Historical 04:00-07:00 workers run only in that interval; later
  02:00-08:00 workers can use the added hours. The planner skips temporarily
  ineligible workers, waits for future openings when needed, and reserves a
  waiting snapshot's own deadline. Unknown declarations are reported and stay
  pending. Every admission must fit both the worker and current windows; the
  worker's own lock-time check still applies.
- The planner's optional `--pending FILE` is the coordinator's ephemeral
  dispatch handoff, not a dispatch history. After validating every input and
  option, `plan` atomically compacts it to requests not yet visible in the run
  facts and still within the 600-second API visibility grace. The 64-request
  limit applies to that active set; expired or observed requests do not end
  a long night's work. Malformed input or too many active requests fails
  without replacing the file. This local housekeeping also occurs in dry-run
  mode; it never dispatches anything.
- `vars.BUSTER_NIGHTLY` set to `on` is the switch. Before it, the coordinator
  only harvests, plans and reports, and pushes work as before. With it, pushes
  run hosted rows alone and keep off the 9700X, and the coordinator measures
  each state (native correctness, then its targeted measurements) in the
  02:00-08:00 window. `9700x.yml` has no daily schedule since 2026-11-05: the
  snapshot's comprehensive run is the daily run.
- Tests: `bash tests/ci/nightly.sh` (the self-test, also under ASan and UBSan,
  and the glue against a fake `gh` and a merge history) and
  `bash tests/ci/caches.sh` (the 9700X's caches). Pull requests run both in
  the `evidence` job before any hardware; `changes` runs
  `nightly --self-test`, which also fails if the build or the inventory ever
  calls `git`.

## Queue and execution timing

The hosted-only `CI latency report` workflow runs at 08:37 Europe/Madrid, after
the nightly window, and on manual dispatch. It samples the latest 100 main CI workflow runs and reports the
median and nearest-rank p95 separately for PR, main-push, nightly targeted and
full (daily, dispatched, snapshot) runs, a nightly backlog run told apart by its
run name. Besides queue and execution, it reports the host's occupation, from
the job's first step (after the runner hook took the lock) to its end. It also
reports the shared lock's wait and hold, which it reads from the runner hooks'
lines in each job's log. It keeps
the raw API responses and normalized observations as workflow artifacts. It
does not run benchmarks, weaken the hardware gate, or change any schedule.

Read `docs/ci.md` for metric definitions and limitations. In particular, the
runner hook's step duration includes its overhead. The shared-lock wait and
hold (`lock-wait`, `lock-hold`) come from the hooks' own log lines, which
bracket the lock operations to within tens of milliseconds.
Missing timestamps are unknown rather than zero. Run its network-free fixtures with
`bash tests/ci/timings.sh`. Keep stored raw evidence in `buster-performance`, not
in this repository's retired `docs/measurements` path.
