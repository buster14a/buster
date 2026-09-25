# Branch-only arena ownership / invocation evidence

Product pin: `ade6ac4b6ecb21f30b61b656439bac476c145e2f`, tree
`4c5306221fdb22fccc929b55e333163742de17d0`. Extend #53; no production repair.

`ownership_probe.c` uses the actual `lane_run`, allocator and existing
one-shot reservation-failure seam. A successful creation while that seam is
armed proves a pool hit; the pending seam is consumed by a nonpooled control.
No pointer-reuse heuristic, stress timing, race detector silence, or RSS
inference substitutes for this assertion. It compares caller reclamation
(the current TU topology) with origin-lane reclamation after the caller's
last payload read. One uniform kernel covers one/two lanes and single-threaded
builds. It also checks valid work after deterministic allocation failure and
15 bounded cohorts; on two lanes the isolated topology predicts 16 entries
in the caller pool and zero in the worker pool. This is NOT a compiler RSS
measurement: only 256 KiB is initially committed per 32 GiB reservation.

`compiler_probe.h` drives 16 real compiler invocations in one process: four
serial target/order references, then four serial-error/multilane-error/valid
sequences. It covers x86-64/AArch64 ELF, FAST, debug output, odd five-TU tails,
128-function skew, two input orders, jobs 2/1/2/2, earliest errors, unchanged
output on failure, full structured diagnostic comparison and exact linked
bytes after recovery. It includes negative controls for original source
offsets and backend-only diagnostic differences. Cross-target or cross-order
byte identity is deliberately not required. It does not claim all allocator,
platform, self-host, code-execution, or sanitizer coverage.

`prepare.py` adds only an opt-in IDE test entry seam in the isolated hosted
checkout. Its exact diff is retained. It does not edit compiler/runtime source,
generated bindings, build policy, or the persistent lane implementation.
The workflow uses the existing build.c generate/build commands for the IDE.
Run these probes only on an authorized hosted executor. From the repository
root, the workflow's compiler replay is:

```sh
python3 tools/experiments/arena-owner/prepare.py
clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -g build.c -o /tmp/buster-owner-build
/tmp/buster-owner-build generate --cc clang --ci --linker DEFAULT
/tmp/buster-owner-build build --config Release -t ide -- -j2
BUSTER_ARENA_OWNER_COMPILER_PROBE=1 build/Release/ide test
```

The ordinary IDE command remains available with the environment variable unset.
Use an isolated checkout: preparation intentionally refuses a second injection
or different source. All host timings printed by the build driver are ignored.
No admitted performance or speedup, production fix, race freedom, universal
repeatability, or new scheduler is claimed. A hypothetical owner-return repair
must still prove final/error cleanup, retained-memory limits, and every last
consumer lifetime, then undergo the approved matched-build 9700X experiment.
