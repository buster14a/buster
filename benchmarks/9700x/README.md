# 9700X direct workloads

A single C file placed directly in this directory is compiled and timed on the
Ryzen 7 9700X when `davidgmbb` opens or updates a pull request that adds or
modifies it. A draft pull request is enough; nothing needs to be merged, and
there is no manual dispatch or upload. The run's summary shows every run's
exit status, timings and printed output.

Routine compiler measurements run only after commits land on `main`; the
owner request path below is the deliberate pre-merge exception (#3087).
Use it for an affirmative owner experiment or a task explicitly requiring
applicable measurements, not automatically for bug fixes, CI edits, conflict
repairs or branch refreshes. Read the
[request decisions and eight examples](../../docs/agents/benchmarking.md#request-decisions-and-examples)
first. A missing relevant measurement leaves a performance claim incomplete
under #2761; green correctness CI does not waive an explicit acceptance hold.
An already authorized experiment needs no second unrelated manual approval.

## How an agent runs a workload

1. Record the owner experiment or explicit task/acceptance reference, then
   create a branch in this repository (not a fork) and add or change its C
   file directly in this directory. Compile it once in the task's authorized
   development environment with the flags in
   [Contract](#contract) so a warning does not cost a run.
2. Push and open a pull request, draft if it is not meant to merge, using git
   and `gh` authenticated as `davidgmbb`. A pull request opened or pushed
   under any other identity, including an app or connector identity, is
   skipped before it reaches the host.
3. Two runs follow. "9700X direct workload request" appears in the pull
   request's checks and finishes in seconds. "9700X direct workload
   benchmark" then starts from `main`; it is listed under Actions, not in the
   pull request's checks:

   ```sh
   gh run list --repo buster14a/buster --workflow 9700x-direct-bench.yml --limit 5
   ```

4. Read the report from the run's summary page, or from its log:

   ```sh
   gh run view RUN_ID --repo buster14a/buster --log
   ```

   The report begins at `## 9700X direct workload run` and its first line
   names the head commit it measured; check that it is yours. It lists each
   of the eleven runs with exit status, wall and CPU time, peak RSS and an
   output digest, then the printed output.
5. Report the run URL, the head commit and the numbers as measured. They are
   diagnostic; see [What the numbers mean](#what-the-numbers-mean).

The host has one runner, so runs queue behind each other. A PR run needs a fresh
workload/data or request-file change in its exact head commit, relative to every
parent (#3087). Compiler/scaling markers need a new request line common to all
parent diffs; merging old marker histories is not a renewed experiment. Unrelated updates and merges that inherit old requests do not
start host work. For a deliberately requested new compiler candidate, add a
fresh request line in its head commit; a changed head alone is not a request.
Read the exact-head check, run/attempt and receipt before requesting again.
Reuse a complete result for its original identities; stale "queued" prose is
not a reason to repeat it, and a different head cannot inherit its measurement.
A head-parent diff with 300 or more changed files is refused; put the request
in a smaller follow-up commit. Batch your edits, and
do not use this path as a retry loop. A run that is skipped means the gate
refused it; a `bench` job that waits for a runner means the host or its runner
group is unavailable. Report either instead of working around it.

## Contract

- File name: `[a-z0-9][a-z0-9_-]{0,47}.c`, at most 256 KiB, at most four
  changed workloads per pull request. Subdirectories, headers and build
  scripts are ignored.
- Optional input data (#2769): `<name>.data` beside `<name>.c`, a regular
  file of at most 8 MiB. Before the first run it is copied read-only into the
  run directory as `input.data`, which the program opens by that relative
  name. Its sha256 is reported. Changing only the data file reruns its
  workload. Use it to replay a recorded trace exactly instead of embedding a
  generator.
- Workloads, their data and this directory's request file are classified as
  `tests` by `./build.sh source_size` (#2770). A workload between 32 and
  256 KiB therefore needs no source-size baseline acknowledgement.
- Built once with `clang -std=c11 -O2 -static -fwrapv -fno-strict-aliasing
  -funsigned-char -Wall -Wextra -Werror`.
- Run as two warmups and nine measured fresh processes pinned to CPU 2, with
  no arguments, empty standard input, `PATH` and `LC_ALL=C` only, and a
  ten-second limit per run.
- A nonzero exit, a signal, a timeout or a compile error fails the run. Print
  a self-check line and exit nonzero when it does not hold.
- The report's `Observed host:` line is the CPU model the kernel reports
  (#2761). On any CPU other than the AMD Ryzen 7 9700X nothing is compiled
  or run and the run fails, whatever runner label selected the job.

## Compiler comparison of a pull request

For an explicitly requested compiler comparison before merging (#2769), add
a fresh request line to [`compiler-compare.request`](compiler-compare.request)
in that owner pull request's head commit; a draft is enough. Record the
applicable request or acceptance reference in the issue/PR handoff. For example,
with the issue number and experiment description replaced by the actual request:

```text
request: issue #2769 owner-requested compiler comparison; candidate experiment DESCRIPTION
```

This is an example of a deliberate request, not a line to append for routine
compiler work. The fresh line must be present in the diff against every parent;
editing a comment or inheriting historical marker lines does not renew it.
Combine it with requested workloads if applicable. The same gate applies,
and these jobs follow `authorize`:

- `compare-pull` (the 9700X) builds tests-off Clang Release `ide` binaries of
  the pull request's merge base and of its head. It times both with
  `tools/uarch_lab.py compare` on the same frozen merge-base source, using the
  `compiler-compare-v1` profile. It then runs the native throughput corpus
  (`./build.sh bench_throughput run` from the merge base, profile
  `throughput-corpus-v2`: the default CI corpus under both retained FAST and
  QUALITY modes (12 workload/mode cells, 20 pairs in each of two rounds) on
  the same two binaries (#2761, #3053). Historical four-mode v1 receipts keep
  their original identity. A head
  that moved before measurement is recorded as superseded.
- `start-pull` (hosted) shows the check
  `9700X compiler benchmark (pull request)` on the head commit as soon as the
  request is authorized: queued while the 9700X is busy, then in progress
  with a link to the live job once `compare-pull` starts. It also closes the
  open check of an earlier head of the same pull request as superseded.
- `publish-pull` (hosted) validates the evidence, including that the observed
  CPU is the Ryzen 7 9700X, and completes that same check. Its summary
  states the identities, the pair count, the verdict with its 95% CI, the
  corpus case count with its confirmed regressions and inconclusive cases, the
  host time spent, and links to the workflow attempt and the evidence. The run's artifact `buster-9700x-compiler-<head>-<attempt>`
  holds `receipt.json`, the lab's `summary.json` and raw pairs, and the
  corpus's `throughput/summary.json`, `metadata.json` and raw samples. A
  missing, partial or invalid corpus run, or one whose compiler hashes are
  not the measured binaries, fails the check like a failed self-host run.

The same trusted `compare-pull` route also supports a fixed full Clang
analyzer profile when the request file contains a fresh added occurrence of
the exact line `profile: clang-analyze-full-v1` in the exact head commit.
Append that same selector line once for each explicit analyzer request;
historical occurrences do not replay the profile. The publisher rechecks the
line-count increase against every GitHub parent and the retained request bytes
against the exact head. This opts into an analyzer-only run in place
of the compiler timing/corpus profiles; `scaling.request` and the separate
inline-acceptance selector cannot be combined with it. The trusted merge-base
driver generates one Release split-source compile database from candidate
HEAD, and both separately built native drivers analyze those same 182 selected
rows. Candidate alias proof reduces its inventory to 135 executions and 47
aliases. The profile runs two fresh preflights outside the matched timings,
then baseline, candidate, candidate, baseline, with eight shards, two jobs and
the normal ten-minute per-TU bound. Each full run is followed by an independent
fresh aggregate verification. Raw plans, terminal shard results, logs,
process observations and the exact request are retained and independently
revalidated by `compiler_publish.py`.

The report shows run planning and context-proof time, every worker's planning
and context-proof counters, per-shard elapsed and preflight/postflight context
checks, and the independent aggregate's planning and context-proof time. Shards
run concurrently, so their counters are reported per shard and are not summed
as serial wall or critical-path time. Shard elapsed starts after context
preflight and includes execution plus context postflight; preflight is outside
elapsed and postflight is already inside it, so neither should be added again
to derive wall time. Baseline PLAN_V1 has no internal planning or context-proof
counters; those fields are explicitly unavailable, not zero. Outer analysis and
aggregate wait4 CPU and largest individual RSS are reported separately from the
sampled process-tree RSS lower bound.

Failed or incomplete arms retain their raw phase and sampler records, every
present terminal shard/result record, and partial per-TU and individually
plan-bound cost/counter observations with explicit failed, partial, malformed,
unbound or missing states. Acceptance still requires complete passing arms,
plan-matched full coverage, matching independent aggregates, and complete
process-tree sampling with positive samples, live-process counts and sampled
RSS. Invalid comparisons produce no timing ratios or per-TU median summaries.

This profile is report-only and has no speedup or regression verdict. Its
native campaign is capped at 75 minutes inside the existing 90-minute
`compare-pull` job; setup and report export remain inside that same job budget.
Wait4 CPU and RSS describe the waited process's kernel accounting, with RSS
the largest individual high-water rather than a simultaneous process-tree sum.
Sampled whole-tree RSS remains a lower bound; every full run must explicitly
report a complete process-tree sample before its evidence is accepted. The
exact request still uses the owner-only authorization above and always
requires real execution on the approved Ryzen 7 9700X.

### Multi-TU scaling of a pull request

For an explicitly requested experiment measuring how the pull request's
compiler scales across cores (#424), add a fresh request line to
[`scaling.request`](scaling.request) in that owner pull request's head commit.
Record its request/acceptance reference. This also requests the comparison
above, and `compare-pull` then
adds the `scaling-v1` profile after the corpus:
- `./build.sh bench_throughput scale` from the merge base, on the head's
  compiler only;
- two series, `cores` (CPU 0's physical core excluded, 1/2/4/7 whole cores,
  then 7C/14T) and `machine` (8 cores, then 8C/16T);
- generated multi-TU compile-and-link, with each worker count checked against
  the compiler's own `compilation_workers` report.

The artifact's `scaling/<series>/` directories hold each bundle's
`scaling.json`, `scaling.md`, raw `scaling.csv` and logs. `publish-pull`
re-checks that each bundle is valid and was produced by the measured
candidate, and the check's report shows every point's speedup, bounds,
efficiency, CPU-work and RSS inflation. The leg adds about ten minutes and is
report-only. See the [harness contract](../../tools/throughput/README.md#multi-tu-scaling-scale).

The workflow verdict is report-only; the comparison after a commit lands on
main publishes under a different name. This does not release an issue-specific
pre-merge performance hold. Record correctness CI, performance evidence and
task acceptance separately. Request a candidate only within its authorized
experiment, and reuse an already published exact-head result rather than
repeating it because an older handoff still says "queued".

## What the numbers mean

They are diagnostic process latency from fork through wait4, with CPU time and
peak RSS from the kernel's accounting. They are not a qualified compiler or
runtime comparison. Nothing excludes other activity on the host: one runner
serializes GitHub jobs, but a shell session can overlap. The summary records
the load average before and after so a disturbed run is visible. Results are
the job log and summary; they are not sealed or authenticated.

The workflow is `.github/workflows/9700x-direct-bench.yml` and the harness is
`tools/bench_direct/run_workloads.py`; both are taken from `main`, never from
the pull request. Who may start it, and what an administrator must configure,
is in [the admission guide](ADMISSION.md).
