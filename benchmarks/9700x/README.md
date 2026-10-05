# 9700X direct workloads

A single C file placed directly in this directory is compiled and timed on the
Ryzen 7 9700X when `davidgmbb` opens or updates a pull request that adds or
modifies it. A draft pull request is enough; nothing needs to be merged, and
there is no manual dispatch or upload. The run's summary shows every run's
exit status, timings and printed output.

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
