# Bootstrap wrapper CI

Each of the five desktop Release lanes owns the controlled behavior suite.
The checks shard retains the required `bootstrap_wrappers` lifecycle step and
reports `owned-by-release-shard` without executing the suite again.

```sh
# Full local harness: behavior, process ownership and recursive build graph.
python3 tests/bootstrap_wrapper_test.py -v
# Bash platforms; use python rather than python3 on Windows.
python3 tests/bootstrap_wrapper_test.py BootstrapWrapperTests -v
# Windows CI runs the same behavior methods through two-case scheduling.
python tools/bootstrap_wrapper_cases.py --jobs 2
# Serial control with identical instrumentation and assertions.
python tools/bootstrap_wrapper_cases.py --jobs 1
# Lifecycle/admission controls, executed by workflow_tools in Release.
python3 tools/bootstrap_wrapper_cases_test.py -v
# Authoritative cache/bootstrap workflow contract, executed by workflow_tools.
python3 tools/ci_zig_cache_test.py -v
```

Fake TCC/driver inputs exercise the real Bash/PowerShell wrappers. This is not
a real TCC bootstrap, compiler benchmark or self-host acceptance run.
`tools/ci_zig_cache_test.py` owns the workflow guard, separate budgets, retained
logs and required-summary failure assessment. The wrapper module owns behavior,
child-process controls and immutable-driver build-graph checks; it carries no
duplicate workflow assertions. CI selects only its behavior class in the wrapper
owner and executes the authoritative workflow contract in `workflow_tools`.

## Required gate and budgets

The wrapper step runs after successful checkout and Zig cache policy, even if
`workflow_tools` failed; cancellation stops it. Required desktop summaries
retain both gates. A missing, skipped, cancelled, timed-out or failed wrapper
owner cannot be replaced by a passing compiler matrix. `CI complete` still
requires the desktop inventory. Platform and combination coverage is preserved.

The wrapper step retains twenty-minute Windows and two-minute Unix hang
ceilings. Workflow-tools suites retain five-minute Windows/macOS and two-minute
Linux ceilings. These are hang bounds, not expected runtime or performance
thresholds. #701/#702 established the separate budgets; #2034 changes admission.

## Workflow-tool suite scheduling (#2021)

The workflow-tool suites share a five-minute Windows and macOS budget and
retain the two-minute Linux budget.
[`tools/ci_workflow_tools.py`](../tools/ci_workflow_tools.py) runs them in
three concurrent lanes, slowest first. Each suite is still its own
`python SUITE -v` process with every assertion. Its log goes to
`$RUNNER_TEMP/buster-ci/<log>` and is echoed as one block before `SUITE_END`.
A failure no longer stops the remaining suites. The step fails with the first
failing suite's status, and `WORKFLOW_TOOLS_END result=failure` names every
failed suite. While suites are still running, `SUITE_RUNNING` lines name them,
so a timeout shows what was in flight. At the end, `SUITE_DURATION` lines list
every suite's time, slowest first. The suites are independent: each builds
fixtures under its own temporary directory, and none writes to the checkout.
Most of their Windows time is spent waiting on child processes: three clang
builds of `build.c` and the Windows PowerShell starts in
`ci_vs_dev_shell_test.py`.

[#2021](https://github.com/buster14a/buster/issues/2021) measured the serial
step on hosted Windows AArch64 after #2024. Eight passing jobs took 143–187 s
of the 300 s budget. `ci_vs_dev_shell_test.py` took about 50 s,
`coverage_manifest_test.py` 32 s, `matrix_shard_test.py` 26 s,
`ci_tools_test.py` 16–25 s, `ci_matrix_phases_test.py` 15 s and
`ci_native_observation_test.py` 12 s. In main run `36771768551`
(job `110083316078`), the whole runner was slow: the same suites took up to
5.5× longer (`ci_matrix_phases_test.py` 82 s, `coverage_manifest_test.py`
59 s), and the step timed out. The budget is unchanged. Overlapping the
suites is what restores headroom; see #2021 for the before/after step
durations.

The VS-shell suite admits at most two independent PowerShell probe owners per
batch. All seven controls still run as separate processes with private scripts,
logs and environment outputs; each retains its 120-second deadline measured
from its own launch and its existing ten-second cleanup deadline. This bounds
PowerShell startup pressure while the other two suite lanes compile their
fixtures. A timed-out owner remains a failure and does not suppress later
controls. Cleanup attempts every registered child even if one cleanup fails,
then propagates that error. `VS_PROBE_START`/`VS_PROBE_END` records retain slot,
case, outcome and duration. Host-independent scheduling controls cover the
owner limit, complete inventory after timeout, launch failure, cleanup failure
and original deadline accounting. Hosted Windows execution remains the verdict
on startup latency; the scheduler alone is not a measured speedup.

## Bounded independent cases (#2034)

Windows admits two independent behavior cases through one persistent worker
gang. Each original test instance owns a temporary repository, cache, environment
dictionary, fake compiler log, outputs and child owners. Cold/warm, invalidation,
corrupt/incomplete-entry and failure sequences stay ordered inside their method.
Assertions and shell selection stay in the unchanged, ledger-pinned test module.

The deliberate publication case runs exclusively after the other cases finish.
It retains all six competing writers and every immutable-output assertion.
The existing six-wrapper peak therefore remains the maximum. `--jobs 1` runs
the same method inventory and ordering with identical ownership and clocks.
Other budgets, empty/duplicate inventories and skipped cases fail closed.

Results occupy slots indexed by the original test inventory, and the final
failure/status report follows that order. Timeline records follow actual
execution; a line writer keeps concurrent JSON records intact. Assertion
failures do not suppress other admitted cases. Exceptions, cancellation,
incomplete cases and cleanup errors never become successful results.

## Child ownership and diagnostics

Every child retains its launch-relative deadline: 120 seconds Windows, 20
seconds Unix. Publication collection does not restart the allowance. File-backed
stdout/stderr avoids pipe-capacity and inherited-handle waits. Each child registers
cleanup immediately, before another launch or assertion, and cleanup precedes
fixture removal.

The scheduler adds a cancellation registry. Registration and cancellation share
a lock, and each child's cleanup is serialized against normal collection. Windows
uses the original system `taskkill /T /F`; Unix uses isolated process groups.
Direct children retain the ten-second cleanup deadline. Workers finish before
module state is restored; the registry releases native handles between runs.
These are controlled fixture trees, not containment for detached programs.

The cancellation control releases its coordinator when child readiness succeeds
or fails. Readiness uses the existing child launch-relative deadline; the
coordinator has no earlier timer that can silently skip cancellation during
startup. Setup failures release and join the coordinator, reap owned children
before removing fixtures, and retain the inner case output and summary in the
assertion diagnostic. Controls cover readiness delayed beyond five seconds and
a child exiting before publishing its descendant marker, without assertion
retries or changes to the production child/suite deadlines.

The retained log includes environment, test and child JSON, plus
`BOOTSTRAP_LAUNCH`, `BOOTSTRAP_COLLECTION` and ordered `BOOTSTRAP_CASE_SUMMARY`
records. It identifies Python/architecture, image, actual shell, child inventory,
peak live wrapper children and unreaped children without dumping the environment.
Windows still prefers `powershell.exe`; the CI launcher remains Bash.

Test time includes setup/teardown/cleanup. Launch observes constructor work;
collection includes scripted work, waiting, capture and cleanup, with cleanup
also clocked separately. Concurrent lifetime can include collection delay;
Windows clock resolution can round short intervals to zero. These observations
do not measure CPU or isolate PowerShell startup, Defender, storage or emulation.

## Repeated comparison

In an idle disposable hosted checkout:

```sh
python tools/bootstrap_wrapper_compare.py --output wrapper-comparison
```

The tool executes three serial/two-case pairs in order `1,2 / 2,1 / 1,2`,
with fresh fixtures and no assertion retries. It retains every attempt, requires
identical test identities and 22 launched/reaped children per successful arm,
and checks the six-child peak. Cancellation stops subsequent samples. This
multi-run experiment is outside routine CI.

Native ARM64 and x64 comparisons qualify scheduling changes. Report phase
latency separately from whole-job latency, queue delay and runner occupancy.
CPU and simultaneous process-tree RSS remain unavailable unless independently
measured. Experiment occupancy includes all six samples and is not production
runner-work savings. The [#2034 audit](performance-audits/2026-09-30T201055Z.md)
preserves exact revisions, observations, the failed setup attempt and limits.
