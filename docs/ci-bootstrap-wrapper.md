# Bootstrap wrapper CI

The controlled suite `python3 tests/bootstrap_wrapper_test.py -v` runs once
on each of the six desktop lanes. On Windows use `python` instead of `python3`.
It exercises the real Bash/PowerShell wrappers with fake TCC and driver inputs;
it is not a real TCC build, compiler benchmark, or self-host acceptance run.

## Independent required gate

`Bootstrap wrapper regression tests` (`bootstrap_wrappers`) has its own
bounded step, independent of the preceding `workflow_tools` result. It runs
after a successful checkout even if a policy test failed, but not after job
cancellation. Both Windows and Unix desktop summaries explicitly require
`workflow_tools`, `bootstrap_wrappers`, Zig setup, and their combination matrix.
A missing, skipped, cancelled, timed-out, or failed wrapper suite cannot be
replaced by a successful compiler matrix. `CI complete` still requires every
desktop lane. No test, runner, or existing compiler gate is removed.

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

In run `35733354799`, Windows AArch64 job
`106764232727` reached the final passing suite before the two-minute step
deadline cancelled it. The bounded Windows allowance covers native fixture
compilation and process startup without removing suites or suppressing errors.
The wrapper step has a separate two-minute Unix budget and a twenty-minute
Windows budget. This is a conservative hang-detection ceiling, not an expected
runtime or an accepted performance limit. It does not switch Windows coverage
from `powershell.exe` to `pwsh.exe` to avoid exercising the existing wrapper.

The Windows allowance addresses [#701](https://github.com/buster14a/buster/issues/701):
job `104876561077` in run `35120438676` used almost its entire two-minute
shared budget on the preceding policy suites and two passing wrapper tests.
The recorded wrapper-test intervals were approximately 47 and 48 seconds.
There are sixteen serial wrapper invocations plus six concurrent publishers
in the original behavior suite. Applying roughly 48 seconds to each serial
invocation and one concurrent batch gives about fourteen minutes; twenty
minutes leaves headroom without an unbounded wait. This is a conservative
budget-sizing extrapolation, **not a measured full Windows suite duration**
or a diagnosis of the underlying shell/runner delay. Retain exact-head Windows
AArch64 and x86-64 durations when validating or tightening this ceiling.

## Child deadlines, cleanup, and diagnostics

Every child has a launch-relative deadline: 120 seconds on Windows, 20 seconds
on Unix. Concurrent publishers keep their own launch deadlines rather than
receiving another full allowance when their results are collected. An expired
child produces an assertion failure with its argv and captured output.

Each child is registered for cleanup immediately. Early assertion or launch
failure therefore also cleans previously launched publishers before the
fixture directory is removed. Timed-out Windows children use the system
`taskkill /T /F`; Unix children use an isolated process group. Direct children
are reaped with a ten-second cleanup deadline, and cleanup errors propagate.
Output is captured in temporary files instead of pipes, avoiding pipe-capacity
or inherited-pipe-handle waits while collecting another publisher. These are
controlled test processes, not a sandbox for arbitrary detached programs.

The retained `bootstrap-wrapper.log` contains `BOOTSTRAP_ENVIRONMENT`,
`BOOTSTRAP_TEST`, and `BOOTSTRAP_PROCESS` JSON records. They identify Python,
OS/architecture, selected shell, test ID, argv, PID, deadline, observed elapsed
time, exit code, and timeouts without dumping the environment. Test durations
include setup, teardown, and cleanup. A concurrent child's observed elapsed
interval includes any delay before collection; it is not precise child CPU
time or an attribution to PowerShell startup.

## Validation

Run the whole suite on both Windows architectures, not only the test that was
interrupted in the original job. The original ten tests remain, including the
build-graph check and all six concurrent publishers. Added controls cover
nonzero status and both output streams, timeout failure and child reaping,
launch-relative deadlines, early-failure cleanup, independent suite scheduling,
and fail-closed required-step lists using the production summary assessor.

For a narrow timeout/CI-contract check without compiling Buster:

```sh
python3 tests/bootstrap_wrapper_test.py BootstrapProcessTests BootstrapWorkflowTests -v
```

For the existing wrapper behavior without the separate build-graph check:

```sh
python3 tests/bootstrap_wrapper_test.py BootstrapWrapperTests -v
```

Native Windows results and final-head workflow lint remain required. A Linux
pass does not reproduce or explain the Windows slowdown.
