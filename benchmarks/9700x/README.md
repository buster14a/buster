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
- Built once with `clang -std=c11 -O2 -static -fwrapv -fno-strict-aliasing
  -funsigned-char -Wall -Wextra -Werror`.
- Run as two warmups and nine measured fresh processes pinned to CPU 2, with
  no arguments, empty standard input, `PATH` and `LC_ALL=C` only, and a
  ten-second limit per run.
- A nonzero exit, a signal, a timeout or a compile error fails the run. Print
  a self-check line and exit nonzero when it does not hold.

## What the numbers mean

They are diagnostic process latency from fork through wait4, with CPU time and
peak RSS from the kernel's accounting. They are not a qualified compiler or
runtime comparison. This path does not hold the benchmark service's host lease
and does not seal its results: one runner serializes GitHub jobs, but a service
job or a shell session on the host can overlap. The summary records the load
average before and after so a disturbed run is visible.

The workflow is `.github/workflows/9700x-direct-bench.yml` and the harness is
`tools/bench_direct/run_workloads.py`; both are taken from `main`, never from
the pull request. Who may start it, and what an administrator must configure,
is in [the admission guide](../../tools/bench_service/deploy/GITHUB_ADMISSION.md#direct-workload-gate).
For contained execution with sealed evidence use
[the benchmark service](../../tools/bench_service/USING.md) instead.
