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

## Troubleshooting

Where the results are (#2902): the job log and the step summary hold the same
report, written one piece at a time as each workload finishes, so an earlier
workload's section survives a later failure. The job log's standard error also
carries `progress:` lines, and the runner's work directory
(`$RUNNER_TEMP/direct-bench`) holds `progress.log` with the same lines. Each
line is written before the next operation: the plan, the start of every stage
of every workload, and every finished run with its exit state and wall time.
Stage starts and the closing lines are `fsync`ed; the per-run lines are not, so
no disk flush overlaps the next timed run (a killed runner loses nothing it
wrote, only a power failure can). The file is created when the first stage starts, truncated at the
start of each run, capped at 64 KiB (it ends with a marker line when the cap is
hit, after which only standard error continues), and each line is cut at 300
bytes. If it cannot be written the run still measures, then fails with
`cannot write progress.log`.

Read the run top to bottom: the last `**FAILED:**` lines at the end of the
report summarize every problem. The exit status is 0 only when every run of
every workload was valid, 1 for any reported failure, and 128 plus the signal
number when the runner was stopped (below).

### Refused before anything is compiled

These end the run with a `**FAILED:**` line and no section for any workload:
an observed CPU other than the AMD Ryzen 7 9700X, a base or head that is not a
full lowercase commit ID, a missing compiler, candidate checkout or summary
directory, a pinned CPU the runner process cannot use, an unsupported
workload file name or more than four changed workloads (see
[Contract](#contract)), and a pull request that changes no workload. A source
over 256 KiB, an input over 8 MiB, or either not being a regular file fails
that workload alone, with its reason, before it is compiled.

### A workload that did not finish

A workload that fails, including with an unexpected exception, or is
interrupted gets an **INCOMPLETE** section naming
the stage it was in, the kind (`failed`, `timed out` or `interrupted`), the
exception and how many of the 11 planned runs completed. Completed runs are
listed as exit/timed out/wall milliseconds; no latency summary is given, and
every workload after it is listed as `NOT RUN`. The stages are:

| stage | what a failure there usually means |
|---|---|
| preparing the run directory | the work directory is not writable or already holds this workload's directory |
| staging the source | the source could not be read from the candidate checkout or copied |
| compilation | the compiler could not be started, or ran for more than 120 seconds (`timed out`); a compile *error* is reported separately as `Compilation failed:` with the diagnostics, because the source is compiled alone and only system headers resolve |
| reading the input data | `<name>.data` could not be read |
| hashing the executable | normally never fails; a program that cannot be read is hashed as `missing` |
| measurement | a run could not be started or waited for, or the runner was stopped during it |
| reporting | the section could not be written; a source that cannot be re-read is hashed as `missing`, which marks the runs invalid |

### Runs that finished but are not valid

A report headed **INVALID, NOT A COMPLETE MEASUREMENT** completed its runs
but cannot be compared. Its table keeps every run. Causes: a nonzero exit, a
signal or a timeout (ten seconds per run, the whole process group is killed),
a program that modified or removed `input.data`, a file over 16 MiB (SIGXFSZ)
or run files totalling over 64 MiB, an executable whose sha256 changed after
the first run (the series stops there), and a source file that changed after
compilation. Failed runs never enter the statistics.

### A cancelled or timed-out job

A cancelled job or the 15-minute job timeout makes the Actions runner signal
the step, typically SIGINT, then SIGTERM, then SIGKILL if it is still running;
a closed terminal sends SIGHUP. SIGINT, SIGTERM and SIGHUP take the path of a
failing stage: the section of the workload in progress is published as
INCOMPLETE with the stage and the runs that completed, the workloads that had
not started are listed as `NOT RUN`, and the closing lines are written before
the runner exits with 130, 143 or 129. A report piece that is being written
when the signal arrives is finished first; if that was a workload's last
section, no INCOMPLETE section is added for it, only a `**FAILED:**` line. If
standard output cannot be written (the terminal that sent SIGHUP is gone, or
the log reader exited), that is reported as `**FAILED:** cannot write standard
output` and the rest of the report goes to the step summary and `progress.log`
alone, with the same exit status. The compiler or workload process
group that was running is killed and reaped on every exit path, so nothing
started by the runner outlives it. A signal that was ignored when the runner
started stays ignored, and any signal after the first stop is only recorded,
so the closing report is always completed.

SIGKILL cannot be handled, so it leaves no INCOMPLETE section and no `NOT RUN`
list, and a workload that was running (it is in its own session) continues
until it exits on its own. The job log and `progress.log` still show the plan,
the last stage started and every run that finished before it; the summary
holds the sections published before it. Treat such a run as not measured and
rerun it.

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
compiler work. The fresh line must be present in the diff against every parent.
The gate checks line freshness, not the meaning of `request:`: a newly added
comment can also activate this route. Edit this file only to express an
authorized experiment; inheriting historical lines does not renew it.
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
- `start-pull` (hosted) creates or adopts the exact attempt's queued
  `9700X compiler benchmark (pull request)` check after authorization, then
  exits in one short pass. It does not wait for a runner. Follow the linked
  Actions `compare-pull` job for live scheduling, preparation and measurement;
  the custom check remains queued until trusted terminal publication or
  terminal-only recovery. Earlier open heads are reconciled as superseded
  without rewriting a completed result. The trusted hosted
  `9700X terminal lifecycle recovery` workflow closes unresolved cancelled
  attempts even when no later request arrives; it starts no measurement and
  never synthesizes a successful result.
- `publish-pull` (hosted) validates the evidence, including that the observed
  CPU is the Ryzen 7 9700X, and completes that same check. Its summary
  states the identities, the pair count, the verdict with its 95% CI, the
  corpus case count with its confirmed regressions and inconclusive cases, the
  host time spent, and links to the workflow attempt and the evidence. The run's artifact `buster-9700x-compiler-<head>-<attempt>`
  holds `receipt.json`, the lab's `summary.json` and raw pairs, and the
  corpus's `throughput/summary.json`, `metadata.json` and raw samples. A
  missing, partial or invalid corpus run, or one whose compiler hashes are
  not the measured binaries, fails the check like a failed self-host run.

The same trusted `compare-pull` route supports versioned full Clang analyzer
profiles when the request file contains one fresh exact selector in the head:
`profile: clang-analyze-full-v1` or `profile: clang-analyze-full-v2`. Append one
selector line for each explicit analyzer request. Historical occurrences of
either selector do not replay the profile; the selected line must increase
exactly once against every GitHub parent while the other recognized selector
count stays unchanged. The publisher checks the exact head request bytes and
hash. Analyzer profiles run in place of compiler timing/corpus profiles;
`scaling.request` and a newly requested inline-acceptance selector cannot be
combined with them.

V1 preserves its historical fixed 182-row qualification and 135-execution,
47-alias candidate profile. V2 independently derives the ordered Release
inventory and analyzer argument projection from the exact retained candidate
compile database, then checks every baseline and candidate plan and aggregate
count against that inventory. Its selected, unique and alias totals are
recomputed from candidate PLAN_V2 rows and their context proof; a whole
invocation class may safely run as unique executions only when every row is
self-represented and carries the same explicit unproven reason with no proof
envelope. V2 does not bake in a TU or alias count.

Both profiles use two fresh preflights outside the matched timings, then
baseline, candidate, candidate, baseline, with eight shards, two jobs and the
normal ten-minute per-TU bound. Each full run is followed by an independent
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
