# ci

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Sharing the 9700X

Correctness CI runs on GitHub-hosted Ubuntu 26.04 with the official stable
Clang/LLVM 23.1.4 release, in baseline x86-64 and native host builds. Native
builds on CPUs without the complete vector feature set use the scalar fallback.
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
start benchmarks. The existing required check name is retained by a hosted
aggregation job that fails when either stage fails.
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
  includes, for one built as a unit of its own (`build.sh`'s `*_benchmark.c`),
  or one that its entry in `changes_benchmarks` names, for one in `main.c`'s
  units (the files that define the functions it calls); or the benchmark's own
  step. A change to `build.sh` runs every one, and so does a push to main, so
  that main has each benchmark's numbers at every commit. A new benchmark's
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
  to main runs none of them: its pull request's run measured what it changes,
  and the full set took the job past its 10 minutes. The daily and dispatched
  runs have 25 minutes.
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
  `zen5`'s digit, closures through includes, `build.sh`, pushes). Run it
  locally with `cc -std=c17 -O2 -Wall -Wextra -Werror -o /tmp/changes
  tools/ci/changes.c && /tmp/changes --self-test` after changing a gate, a
  benchmark's step or the tables.
- `~/buster-fast-bench/README-9700x.md` on the host describes the setup and how
  to undo it.
