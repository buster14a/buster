# 9700X direct workloads

A single C file placed directly in this directory is compiled and timed on the
Ryzen 7 9700X when `davidgmbb` opens or updates a pull request that adds or
modifies it. A draft pull request is enough; nothing needs to be merged, and
there is no manual dispatch or upload. The run's summary shows every run's
exit status, timings and printed output.

## How an agent runs a workload

1. Create a branch in this repository (not a fork) and add or change one C
   file directly in this directory. Compile it once locally with the flags in
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

The host has one runner, so runs queue behind each other, and every push to a
pull request that changes a workload starts another run. Batch your edits, and
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

## Compiler comparison of a pull request

To measure a compiler change before merging (#2769), add or change any line
of [`compiler-compare.request`](compiler-compare.request) in an owner pull
request; a draft is enough. Combine it with workloads if you like. The same
gate applies, and these jobs follow `authorize`:

- `compare-pull` (the 9700X) builds tests-off Clang Release `ide` binaries of
  the pull request's merge base and of its head. It times both with
  `tools/uarch_lab.py compare` on the same frozen merge-base source, using the
  `compiler-compare-v1` profile. A head that moved before measurement is
  recorded as superseded.
- `start-pull` (hosted) shows the check
  `9700X compiler benchmark (pull request)` on the head commit as soon as the
  request is authorized: queued while the 9700X is busy, then in progress
  with a link to the live job once `compare-pull` starts. It also closes the
  open check of an earlier head of the same pull request as superseded.
- `publish-pull` (hosted) validates the evidence, including that the observed
  CPU is the Ryzen 7 9700X, and completes that same check. Its summary
  states the identities, the pair count, the verdict with its 95% CI, the
  host time spent, and links to the workflow attempt and the evidence. The run's artifact `buster-9700x-compiler-<head>-<attempt>`
  holds `receipt.json` and the lab's `summary.json` and raw pairs.

The verdict is report-only and blocks nothing; the comparison of each commit
after it lands on main publishes under a different name. A comparison takes a pilot
plus about ten minutes of pairs after three builds, so request it once per
head you intend to report.

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
