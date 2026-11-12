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

## The merge queue

`9700x.yml` also runs on `merge_group` (`checks_requested`, main, no path
filter), the merge queue's candidates (#1430). It is inert until a ruleset
turns the queue on; the ruleset, removing the `push` triggers and the AGENTS.md
rule change together, after the capacity gate and the owner's decision.

- **What a candidate runs.** The full hosted contract on its exact commit,
  documentation alone included:
  - every row and sanitizer;
  - the vector pool;
  - the three fuzz campaigns.

  It takes its own run-id concurrency group, and its rows don't fail fast.
  It never uses the 9700X (`hardware=skip`, reason `merge-group`), and
  SQLite's full Tcl suite stays nightly.
- **Selector.** `changes --event merge_group --workflow
  .github/workflows/9700x.yml` is its profile: `code`, `measure=all` and
  `performance`, whatever it changes.
- **Aggregate.** The required aggregate passes a candidate only through its
  coverage step, `bash tools/ci/nightly.sh coverage DIR`. That step reads the
  run's jobs and runs `nightly coverage --run RUN < FACTS`, which needs:
  - every shard 1..N of every row and sanitizer;
  - the vector verdict;
  - each fuzz campaign;

  all completed and successful. `qualification.c` reads the step's outcome
  (`COVERAGE_RESULT`); no other event runs the step.
- **Record and credit.** The coverage record has purpose `queue`, and its
  `pr_head` is the merge's second parent. The nightly's evidence glue, run
  listing and harvest accept the candidate's runs of the same commit. Once
  one is credited, the report lists the states without a passing candidate run
  as coverage gaps.
- **Policy pins.** `changes --self-test` pins the trigger, the concurrency
  group, the selector's routing and the aggregate's coverage step, with
  mutants: a path filter, another branch or type, a shared slot, a skipped
  selector or coverage step.

[The merge queue](../ci.md#the-merge-queue) has the details and what is still
to come.

## Sharing the 9700X

Correctness CI runs on GitHub-hosted Ubuntu 26.04 with the official stable
Clang/LLVM 23.1.4 release, in baseline x86-64 and native host builds. Native
builds on CPUs without the complete vector feature set use the scalar fallback.
On a pull request the native row is the vector row instead (#2033): the
explicit vector target (`BUSTER_HOST_ARCH=vector`, `host_isa.h`'s predicate),
whose plain configuration's 4 shards are items that each draw's collector
assigns, by rank, to the hosted runners whose probe found their CPU qualifies
(`tools/ci/vector-draw.sh`, tested by `bash tests/ci/vector-draw.sh`), drawn in
up to 4 levels, with no emulation; an item no qualifying CPU held fails it as
incomplete ([The vector row](../ci.md#the-vector-row)). Where the sanitizer rows
run, the vector row runs beside `native` with the plain and the three sanitizer
configurations, 16 items in one pool of draws, the costliest first. Each
configuration's item 1 builds its compiler and publishes it; its other items
install that product (`tools/ci/hosted-product.sh`, #2032). Its race fixture is
`.github/workflows/vector-claim-race.yml`.
Each row (`.github/workflows/hosted-correctness.yml`, which the test job calls
per row) runs its inventory as its own number of shards, a runner each:
`parallel --shard K/N --costs tools/ci/costs/ROW.costs` deals the commands by
the row's hosted costs, the same deal on every shard's runner, so the shards run
each command once ([Shard costs](#shard-costs)). A portable `x86-64` row
builds its compiler once, and its shards verify and run that product
(`tools/ci/hosted-product.sh`; [Shared row
products](../ci.md#shared-row-products), #2032), and so does each vector
configuration, on a qualifying CPU; a `native` row's shards each
build their own (a native build must run on the CPU it was built for). Run
`bash tests/ci/hosted-product.sh` after changing the product's packing, identity
or checks. A plain row's test step took 14.6 minutes on one 4-CPU runner; its
4 shards take about 4 to 6. The steps a row needs once (the baseline fallback,
QEMU and arena checks, the parser smoke test, the performance readers and the
TSan/MSan `main.c` compiles) run in its shard 1.
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
  (`changes_shard_policy`, on `hosted-correctness.yml` and its caller): the
  row workflow's required number input `shards` must be N throughout, its
  matrix's lookup `fromJSON('[[], [1], [1, 2], ...]')[inputs.shards]` must give
  1 to N for every N it has, the job's name and the run step's
  `--shard "${{ matrix.shard }}/${{ inputs.shards }}"` must take the same
  input, and every number the test job's `shards:` passes must be one the
  lookup has, since a list and an N that disagree would leave a shard's
  commands unrun with every job green; and when the row workflow weakens its shared products
  (`changes_row_policy`: the portable rows alone, the triggering revision, the
  ownership check on the build's tree, the shards' wait for the build, its
  SHA-256 and their verified install, nothing from a cache or another run); and
  when the vector row's probe, `host_isa.h`'s predicate and the build's vector
  flags fall out of step, or its records and products stop being per attempt
  and non-overwriting, or its candidates take items without their collector's
  assignment, or a draw lacks its collector (`changes_vector_policy`); and
  when the vector row's items aren't each configuration's shards 1 to N, or its
  first draw's candidates and its collector's count aren't 3 per item, on a
  pull request and for the pool alike (`changes_shard_policy`), or its items
  stop installing their configuration's one product (`changes_row_policy`). The
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

## Hosted inputs

Each run's `prepare-hosted-tools` job acquires every external input its hosted
jobs read, once, and hands it to them. The jobs set up from that handoff alone.
Their setup reaches only GitHub's checkout, cache and artifact services, so an
Ubuntu mirror, DNS or release-host outage can't fail a shard (#2275).

- **Release inputs:** LLVM, CMake, Ninja, mold and wild, pinned by
  `tools/ci/hosted-tools.lock` (`tools/ci/hosted-release.sh`). They come from
  the `hosted-inputs-v1-…` cache, else the run's artifact `ARTIFACT-NAME`,
  which preparation publishes only when that cache didn't verify for it
  (#2372). It and the offline bundle below are kept 1 day.
- **Offline inputs:** `tools/ci/hosted-offline.sh`'s bundle, the run's
  companion artifact `ARTIFACT-NAME-offline`. It holds:
  - the apt closure of `setup-hosted.sh`'s packages, resolved on the
    preparation's runner image, with a manifest of every package and version;
  - the baseline runtime's 4 `.deb`s and the corpus (`downloads/DIGEST`);
  - bare mirrors of c-testsuite and SQLite at `refs/pinned/COMMIT`.

  The preparation keeps them in two caches. `hosted-apt-v1-…` is keyed by the
  package list and the image (`ImageOS`, `ImageVersion`). `hosted-ci-v1-…` is
  keyed by the scripts that pin the rest. On a hit it contacts no external
  host. On a miss it fetches each input once, with retries, and checks it as
  the jobs read it.
- **Consumers:** the `.github/actions/hosted-tools` action downloads the
  companion artifact and checks its manifest. It then sets `BUSTER_CI_CACHE`
  and `BUSTER_CI_OFFLINE=1`, so `setup-hosted.sh` installs with
  `apt-get --no-download` and no package lists, and the fetch scripts read the
  bundle alone. A missing file or a bad hash fails the step; nothing is
  fetched again.
- **Summaries:** each job's step summary lists the installed package versions,
  and the preparation's lists every input's source.
- **Image rollouts:** a shard on another image installs the same `.deb`s and
  warns where the image's own packages differ.

Run `bash tests/ci/hosted-offline.sh` after changing any of these scripts. It
is network-free, and the preparation job runs it. `hosted-inputs-drill.yml`
runs a shard's setup, and a warm preparation, with those hosts down. Details,
failure handling and recovery: [Offline inputs](../hosted-toolchain.md#offline-inputs-2275).

## Shard costs

Each hosted row deals its inventory to its shards by a table of its own,
`tools/ci/costs/ARCH-SANITIZER-O3.costs` (#2031): the `x86-64`, `native` and
`vector` rows (#2033), each sanitizer. Each table is
`tools/ci/costs.c`'s output alone, never edited by hand. Its first line says
so, and `costs --self-test --root .`, which the selector's job runs on every
pull request, fails a table that differs in any byte from what `costs refresh`
would write. In particular it fails a table that lacks an inventory heading or
names one the inventory doesn't have:

```sh
cc -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing tools/ci/costs.c -o build/costs
build/costs --self-test --root .
```

- **What a cost is.** A command's time times the CPUs it held (its heading's
  `[N CPUs]`, at most the runner's 4), in seconds: `parallel`'s unit. A
  measured cost is the 90th percentile (nearest rank) of the command's samples
  in that row. Each sample is first scaled to the slowest CPU's time.
- **CPU speeds.** Hosted runners draw EPYC 7763, 9V74 and 9V45 and Xeon 8370C,
  8573C and 6973P-C, and a deal can't choose one. The scale is each CPU's
  speed against the reference CPU's (the one most jobs drew): the median, over
  the commands both ran in one row, of the ratio of their median times. On
  11 November the 9V45 and the 6973P-C took 0.64 of the 7763's time, the 9V74
  0.79, the 8573C 0.80 and the 8370C 0.89. A CPU that shares fewer than five
  commands with the reference keeps its samples as they are (`-`).
- **Commands without a good sample.** A command whose samples all failed costs
  its longest failure, a lower bound (`#: lower-bound`). One without any sample
  costs the row's median per CPU times its CPUs (`#: unmeasured`).
- **Shard 1's once-only steps.** `#: first-shard SECONDS` is how much longer a
  row's shard 1 runs than its other shards besides their commands: the median
  of shard 1's jobs' time outside the test step, less the others' median, each
  scaled to the slowest CPU as samples are. Only jobs whose commands all passed
  count (a failure skips the later steps), and not the vector row's draws, which
  also wait for their collector; without both kinds of job the line is left out.
- **Provenance.** The `#:` lines record the row, the runner's CPUs, the
  revisions measured and the CPUs drawn, each with its jobs.
- **Collect.** After a full run (the nightly snapshot's, or a dispatched
  `full` run), `bash tools/ci/costs.sh LOGS_DIRECTORY RUN...` fetches the given
  runs' shard logs (`gh api repos/buster14a/buster/actions/jobs/ID/logs`, each
  shard whose test step ran) and runs `costs collect --tests
  tools/ci/correctness.tests --output tools/ci/costs LOG...`, then `costs
  check`. Give it full runs together with recent main pushes, for more plain
  samples. `collect` writes each row the logs have. A log is data. One that
  lacks the job's name, its CPU, its checkout, or the runner's shard line and
  summary is rejected, and so is the same job twice. A vector row's draw
  (`Vector draw L, candidate C`) takes its row from its take's line, `Vector
  item ITEM: SANITIZER, shard K of N`, one of its caller's configurations (or,
  without the line, from its caller's one configuration), and its shard from its
  report, which must be the item's; a candidate that took no item has no
  report, and `costs.sh` skips it.
- **Seed.** A row without logs yet takes another row's measurements: `build/costs
  seed --tests tools/ci/correctness.tests --costs tools/ci/costs --row
  vector-address-O3 --from native-address-O3`. Its `#: seeded ROW` line, first
  among the provenance lines, says whose they are; a collect over the row's own
  logs replaces the table, and a seeded table seeds no other. The vector
  sanitizer rows (#2033) ran their first full run (43027989712) on the native
  rows' seeded tables, and their tables are that run's draws' logs since.
- **Refresh.** A pull request that adds, renames or removes a heading runs
  `build/costs refresh --tests tools/ci/correctness.tests --costs
  tools/ci/costs`. It keeps every measurement, estimates the new commands and
  drops the removed ones; no logs are needed.
- **Plan.** `build/costs plan --tests tools/ci/correctness.tests --costs
  tools/ci/costs/ROW.costs --shards 2-8 [--setup S] [--first S] [--deal
  FILE]` predicts the runner's deal and schedule for each shard count, adding
  the table's `#: first-shard` to shard 1 unless `--first` gives another. For
  each N it prints the longest shard, the mean, the imbalance, the floor (the
  longest command alone) and the runner-minutes. A single N lists each
  command's start and end. `--deal` deals by another file, such as an older
  table, and times the deal by `--costs`.
- **Report.** `build/costs report LOG...` gives a run's rows as they ran: each
  row's longest shard, mean, imbalance, span, runner-minutes and the CPUs drawn.

The tables name their runner's CPUs (`#: runner-cpus 4`). With that line,
`parallel` deals as `test_deal_scheduled` (`test_deal.h`) does: the costliest
command first, each to the shard that, run on 4 CPUs in the list's order as
the runner runs it, would end the earliest with it. A deal by costs alone
couldn't see that the shard holding a row's longest command also started its
8-CPU commands first, which hold every CPU. With the 11 November tables, that
deal predicted 389 s for the plain `x86-64` row's longest shard against 321 s
by schedule, and 527 s against 454 s for the plain `native` row. `plan` shares
the function, so its deal is the runner's.

The test job in `9700x.yml` gives each row its count (`shards`):

| Row | Shards | Why |
| --- | ---: | --- |
| plain, both | 4 | `tools/ci/nightly.c` counts the plain rows' shards as one N; more native shards each rebuild the compiler for under a minute less |
| `x86-64` TSan, MSan | 6 | a portable row's extra shard costs a minute of setup; 6 reach the rows' longest commands |
| `native` MSan | 5 | each extra native shard rebuilds the instrumented compiler (about 10 minutes) |
| `vector`, each configuration | 4 | its items, `BUSTER_VECTOR_ITEMS`, which its draws are sized for; the take gives the run step its configuration's count |
| others | 4 | already shorter than the run's longest row |

No deal beats a row's longest command. In the MSan rows that is a part of the
preprocessor's cases against clang (about 20 minutes on an EPYC 7763), and in
the plain rows the semantic driver's and the parser's clang checks; finer parts
(#2030) lower it. A row's once-only steps run on its shard 1 besides its part of
the deal, and the deal sees them: shard 1's simulated schedule ends its table's
`#: first-shard` later, so the commands go to the other shards until they end as
late.

## Setup failures re-run once

A run of `9700x.yml` ("Tests and Zen 5 benchmarks", the only workflow that
calls `hosted-correctness.yml`) whose first attempt failed only because jobs
stopped in setup, before any test ran, has its failed jobs re-run once,
automatically (#2276). `.github/workflows/rerun.yml` runs when the run
completes (`workflow_run`). `tools/ci/rerun.sh` fetches the attempt's jobs and
steps and its cancelled jobs' annotations, `tools/ci/rerun.c` decides, and the
script asks for `rerun-failed-jobs`. The re-run runs tests that never ran,
never ones that failed, so green keeps meaning the tests passed.

- **Which runs.** A failed first attempt (`run_attempt == 1`) of a push or a
  pull request from this repository, checked again just before the request.
  A second attempt is never re-run automatically, whatever stopped it. Forks'
  pull requests and the nightly dispatches are left alone: the coordinator
  plans the dispatches' 9700X work.
- **Where a job stopped.** At its first step that didn't pass: one that failed,
  or one that was cancelled or left running (a lost runner leaves its step
  running). A job that passed or was skipped didn't stop.
- **Re-run** when at least one job stopped in setup and every stopped job did
  one of these:
  - stopped in a setup step of the allowlist below, whether the step failed or
    was interrupted (fail-fast, its timeout, a lost runner), or failed before
    any step ran (its provisioning);
  - stopped in a step that fails because a setup failure stopped another job.
    "Require this row's compiler product" counts when the row's build job
    ("Build the compiler once for the row's shards") stopped in setup itself.
    The vector row's verdict ("Require one passing qualified execution of each
    vector item") counts when a job of its row did, and the required aggregate
    ("Require hosted and native correctness before successful qualification")
    when any job did;
  - was cancelled by fail-fast after another job failed, wherever it was, by
    its annotation ("The strategy configuration was canceled because ..."), or
    was cancelled before any step ran or after every step it ran passed. The
    other job's failure decides.
- **Never re-run** a run with any of these:
  - a failure in any other step: a test step ("Run correctness tests within the
    hosted CPU budget" and every check), the build ("Build the external Clang
    pragma oracle, the clang server and the compiler"), a probe, a report, an
    artifact's upload or download;
  - a job interrupted outside setup by anything but fail-fast: a lost runner,
    or its `timeout-minutes`. The jobs API reports a timeout exactly as it
    reports a fail-fast cancellation, the job and its step `cancelled`; only
    the annotation "The job has exceeded the maximum execution time" tells them
    apart. A test command's own timeout (`parallel --timeout`) fails its step;
  - a self-hosted job (the 9700X's), a job that didn't complete, or a failed
    job none of whose steps failed.
- **The allowlist** (`rerun_steps`), by step name, for every GitHub-hosted job
  of the run:

  | Steps | What they do |
  | --- | --- |
  | `Set up job` | GitHub's own: the runner, the actions' downloads |
  | `Check out triggering commit`, `Check out the triggering commit and its parents`, `Check out the run's commit and its first parent`, `Check out the exact caller revision`, `Check out the qualification policy`, `Check out reviewed first-party performance fixtures and acceptance harnesses`, `Fetch the vector draw's script from this commit` | the checkouts |
  | `Restore verified hosted release inputs` | `.github/actions/hosted-tools` |
  | `Acquire missing inputs once and verify the complete set`, `Acquire missing offline inputs once and check each as the jobs read it` | `prepare-hosted-tools.yml`'s acquisitions |
  | `Install official Clang 23.1.4 and test tools`, `Install official Clang, sanitizer and libFuzzer runtimes` | `tools/ci/setup-hosted.sh` |
  | `Fetch generic baseline startup objects and runtime for emulation` | `tools/ci/setup-baseline-runtime.sh` |
  | `Fetch and verify frozen corpus inputs`, `Fetch c-testsuite at its pinned commit`, `Fetch SQLite's sources at their pinned commit` | the fetches |

- **Notes.** The workflow's run summary lists every stopped job by the step
  it stopped at and why, re-run or not. After a re-run, the same list goes in
  a comment on the pull request, or on the commit of a push, so that repeated
  infrastructure failures stay visible.
- **API calls.** Every run's jobs share the Actions token's hourly API
  quota, which the vector draws' calls exhausted on 2026-11-11 (their "Take
  the vector item the draw assigns, or end as a redraw" failed with "API
  rate limit exceeded for installation"). So the glue asks for the jobs'
  pages, then only the annotations that could change the decision
  (`rerun annotations`: a cancelled job interrupted outside setup, in a run
  that a setup failure and such jobs alone keep), and, before a re-run, the
  run's state once. Of the 2026-11-05 to 11-11 failed first attempts, none
  needed an annotation.
- **Checks.** `rerun --self-test --root .` runs in `changes` and in the
  workflow before it decides. It tests the rule on synthetic runs and the
  allowlist against the workflow files: each name it holds is a step of the
  watched workflows or of the reusable workflows they call, the row's build
  job and test step keep their names, and `rerun.yml` watches exactly the
  workflows that call `hosted-correctness.yml` and re-runs only failed first
  attempts. A renamed setup step fails it until the list follows.
  `bash tests/ci/rerun.sh`, in the `evidence` job's fixtures step, also runs
  the cases recorded from the API in `tests/ci/rerun/cases.tsv` against their
  reports and Markdown: the 2026-11-11 outage's push and pull request, test,
  build, timeout, fail-fast, lost-runner and self-hosted failures. It checks
  workflow mutants that the policy must refuse, and the glue against a fake
  `gh`. Record a new case with `bash tests/ci/rerun/record.sh RUN ATTEMPT
  NAME`, list it in `cases.tsv`, then run `bash tests/ci/rerun.sh --update`
  and read the difference.

```sh
cc -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing tools/ci/rerun.c -o build/rerun
build/rerun --self-test --root .
bash tests/ci/rerun.sh
```

## Reuse and the nightly backlog

`tools/ci/nightly.c` makes the decisions, `tools/ci/nightly.sh` is their API and
Git glue, and [the CI guide](../../docs/ci.md#nightly-main-measurements) has
the policy. In short:

- Every `9700x.yml` run has an `evidence` job, which must succeed. A push to
  main is admitted and runs its own native correctness: it reuses nothing
  from its pull request, whose 9700X job skips native correctness (#1272).
  A merge-queue candidate is never admitted. When main is fast-forwarded to its
  commit, its plain rows are that state's (#1430).
  A nightly dispatch's `evidence` job checks that the commit is a first-parent state of
  main, may reuse the commit's own earlier plain hosted rows (a push's, a
  dispatch's or a merge-queue candidate's), and enforces
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
