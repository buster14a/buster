# Bootstrap wrapper CI

Each of the five desktop Release lanes owns the controlled behavior suite.
The checks shard retains the required `bootstrap_wrappers` lifecycle step and
reports `owned-by-release-shard` without executing the suite again.

```sh
# Full local harness: behavior, process ownership and recursive build graph.
python3 tests/bootstrap_wrapper_test.py -v
# Bash platforms; use python rather than python3 on Windows.
python3 tests/bootstrap_wrapper_test.py BootstrapWrapperTests -v
# Windows CI runs the same behavior methods through four-case scheduling.
python tools/bootstrap_wrapper_cases.py --jobs 4
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
controls. The plan admits the four probes that reach the vendor shell before
the early failures, so the fixed batches do not pair each long probe with a
short one (#3912). Those four took 24-39 s each on hosted AArch64 until the
fixture, like the CI steps, imported its modules by name
([#3979](#windows-powershell-command-discovery-3979)). Cleanup
attempts every registered child even if one cleanup fails,
then propagates that error. `VS_PROBE_START`/`VS_PROBE_END` records retain slot,
case, outcome and duration. Host-independent scheduling controls cover the
owner limit, complete inventory after timeout, launch failure, cleanup failure
and original deadline accounting. Hosted Windows execution remains the verdict
on startup latency; the scheduler alone is not a measured speedup.

The helper's filesystem operations use the already loaded .NET file APIs,
resolving literal paths through PowerShell's session provider paths so relative
inputs follow the caller's PowerShell location even when the process cwd differs.
VsWhere directories and non-FileSystem providers fail before the vendor shell
runs. GITHUB_ENV paths are resolved at the existing post-vendor append point;
appends use explicit UTF8 without a BOM and platform newlines. The fake dev-shell
launch log uses the same direct file append instead of loading a cmdlet module.
The vendor's real developer-shell script, host/target arguments, compiler probes,
exceptions and separate PowerShell owners are unchanged.

`VS_PROBE_TRACE` retains launch UTC milliseconds, script/helper/fixture stage
rows and completion. PowerShell stages use UTC milliseconds; CMD fixtures label
their local `%TIME%` stamps explicitly. These clocks are diagnostic; deadlines
remain monotonic and launch-relative. The optional
`BUSTER_VS_PROBE_COLD_TEST_PATH=1` control stamps a real cold `Test-Path` call
before helper entry, isolating its cost without retrying or restarting a clock.
Controls cover literal bracket filenames, differing PowerShell/process locations,
provider/directory rejection, UTF8 export bytes and all original failure cases.

## Windows PowerShell command discovery (#3979)

Windows PowerShell 5.1 finds a cmdlet from a module it has not loaded by
asking its module analysis cache about each module on `PSModulePath`, in
order, and analyzing every module the cache cannot answer for. The hosted
Windows runners list 253-262 modules with 2,927-3,024 script files. With a
cold cache, one lookup costs 22-29 s of CPU, or 41 s under x64 emulation on the
ARM64 runner. PowerShell writes what it analyzed back to the cache only after
the analysis has been quiet for more than ten seconds. A short process
therefore exits first, and the next process repeats the work.

The cache keys each module file by its local `LastWriteTime`. The
`windows-11-arm` image's cache was recorded under UTC, but that runner executes
jobs in Pacific Standard Time. In image 20261004.176.1, 108 of its 434 entries,
including the module manifests, therefore differ by exactly the seven- or
eight-hour offset and never match. The `windows-2025` runner runs in UTC, and
its image cache matches entry for entry. Every `build.ps1` launch, VS probe and
Windows PowerShell CI step on the ARM64 runner therefore spent about 23 s before
its first `Split-Path`, `Join-Path` or `Write-Output`. Process startup, the
exit path, JIT, missing native images, lockdown policy and Defender were each
measured and excluded.

Importing `Microsoft.PowerShell.Management` and `Microsoft.PowerShell.Utility`
by name loads them from their own directories, in 0.14-0.16 s even under x64
emulation. After that,
their cmdlets resolve without the lookup. `build.ps1`, the three Windows
PowerShell steps in `ci.yml` and the VS-probe fixture import both first, and
`tools/ci_vs_dev_shell_test.py` requires the import before each step's first
cmdlet. A cmdlet from any other module still triggers the lookup, so new
PowerShell glue imports what it uses by name.

The diagnostic runs live on the #3979 branch. They are hosted diagnostics
only, not 9700X performance acceptance (#2761).

| Runner | First cmdlet, unloaded module | After importing by name | Fresh cache, then after one 25 s process |
| --- | ---: | ---: | ---: |
| `windows-11-arm`, Cobalt 100, ARM64-native | 22.1-22.9 s | 0.05 s for three cmdlets | 23.2-23.3 s, then 0.13-0.14 s |
| Same runner, x64-emulated PowerShell | 40.7-43.4 s | 0.14-0.16 s | not measured |
| `windows-2025`, EPYC 7763, image cache | 0.11-0.15 s | 0.09-0.10 s | 28.8 s, then 0.12 s |

On the same two Cobalt 100 runners, the 22 wrapper launches took 10.2-10.3 s
with the import and 139.9-144.6 s without it: 0.8-2.8 s per launch against
23.2-35.8 s. The VS-shell suite took 3.8-4.0 s, with every probe under 0.9 s.

PowerShell launched from the x64 Git Bash runs x64-emulated on the ARM64
runner. The Python harnesses and the runner's own `shell: powershell` steps
run it ARM64-native. Runs
[38369510621](https://github.com/buster14a/buster/actions/runs/38369510621),
[38372883339](https://github.com/buster14a/buster/actions/runs/38372883339)
and [38374367876](https://github.com/buster14a/buster/actions/runs/38374367876)
hold the probe, cache and same-runner wrapper records; the last retains both
caches as artifacts. Resetting the runner's time zone would also restore the
image cache, but it changes every timestamp the jobs record. Importing by name
does not depend on runner state.

## Bounded independent cases (#2034)

Windows admits four independent behavior cases (`MAX_JOBS`) through one
persistent worker gang, one per logical CPU of the hosted Release runners.
Before [#3979](#windows-powershell-command-discovery-3979), every hosted
Windows AArch64 wrapper launch spent about 22.6 s of CPU before its first
statement completed, and two lanes left half the runner idle for the step's
218 s of cases (#3912). Each original test instance owns a temporary repository, cache, environment
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
a lock, and each child's cleanup is serialized against normal collection.

On Windows, ownership is fixed before a case command starts (#3690). The
scheduler runs every case through `tools/windows_process_tree.c launch`. The
launcher creates a fresh named Job Object with kill-on-close, starts the
command suspended, assigns it to the job and only then resumes it, the same
sequence `os_process_spawn` uses for process groups. Every descendant is
therefore a kernel-tracked member from birth. No membership decision uses PID parent links or wall-clock creation
times, which PID reuse and clock adjustment make unreliable.

The native launcher receives the original absolute payload deadline before
startup. Its selected clock matches the caller's Windows monotonic clock
(`QueryPerformanceCounter` or `GetTickCount64`). It enforces that deadline on
the payload, then allows only the original separate ten-second cleanup budget.
Collection can wait for both phases without extending payload runtime.

Before fixture removal, the mechanical adapter requests cancellation by creating
the private terminal path's `.cancel` file and waits under the original cleanup
deadline. The native owner checks cancellation before creating or resuming the
payload and at most every 25 milliseconds during its bounded wait. This avoids
an external drain racing job creation or payload assignment. Only the native
owner terminates and certifies its job; the launcher stays outside it.

Native cleanup waits on retained root handles and on listed handles confirmed
by `IsProcessInJob`, then requires zero active job members. A terminated member
can leave the PID list before enumeration, so `waited` is diagnostic; controls
retain live process handles before cancellation. A successful termination
request alone cannot certify release of inherited current-directory handles
(#2985).

The owner atomically publishes a fresh terminal record after checked teardown
and handle closes. The adapter requires a complete record bound to the exact
deadline, ordered timing fields and the broker's exit: zero for certified normal
completion or cancellation, 124 for certified runtime timeout, and 125 for
lifecycle failure. Arbitrary payload statuses, including 124 and 125, are restored
from the record to the original result. Native publication must also finish by
the cleanup deadline. Missing, malformed, contradictory or late evidence fails;
failure reaping shares the remaining cleanup deadline and never relabels an
uncertified owner as clean. `BOOTSTRAP_COLLECTION.native_launch` retains the
certificate and actual payload status. Later cleanup reuses the first certified
record. The standalone `drain` CLI and its bounded malformed/late protocol
controls remain available, but production cancellation uses the native owner.

`./build.sh bootstrap_process_owner` (or `build.ps1 bootstrap_process_owner`)
builds the helper through the native driver. It qualifies host Clang, applies
strict warnings and the existing C semantic flags, and always compiles into a
fresh, exclusively claimed `.cache/bootstrap-process-owner/` directory outside
`generate`'s disposable trees. Compilation and the Windows `selftest` each have
one 60-second native deadline; any nonzero exit, capture or cleanup failure prevents
publication of `BOOTSTRAP_PROCESS_OWNER <absolute-path>`. Outputs remain available
for the invoking test process's lifetime. This retains the existing helper's
`launch`, `drain`, `fixture` and `selftest` protocols.
`bootstrap_process_owner_test` also builds and runs the standalone native
deadline controls, under the same flags and a single 60-second deadline.
The startup-cancellation control creates its cancel file synchronously after
job establishment and before the pre-root cancellation check. This test-only
hook covers both QPC and tick clocks without racing a writer process against a
startup delay. The production owner checks cancellation at startup boundaries;
the artificial startup delay is only a deadline-test hook, not a production
wait requiring polling.
When any native deadline-control assertion fails or the control returns an
unexpected code, it prints `WINDOWS_PROCESS_TREE_TEST_CONTEXT` and
`WINDOWS_PROCESS_TREE_TEST_STATUS` after the case assertions. The test-only
snapshot retains the root PID, cancel-observed time, runtime and cleanup
deadlines, cleanup and publication timestamps, clock validity, and the first
failed publication operation with its immediate Win32 error when available.
It also timestamps the end of each publication stage (destination attributes,
create, write, flush, close and rename; zero when not reached), so a late
publication shows which operation consumed the allowance (#3816).
The status line includes the recorded JSON, or an explicit missing marker.
Diagnostic clock sampling uses a copy of the owner's state; the production
status schema, return-code rules and cleanup allowance are unchanged.

CI bootstraps `build.c` with hosted image Clang under the documented exception,
runs the native control command in the workflow-tools and bootstrap-wrapper
steps, and mechanically exports its returned path as `BUSTER_WINDOWS_PROCESS_TREE`.
Python only receives that path; Windows case
execution refuses to start without it. Native `fixture` modes supply silent,
partial, repeated and late drain records for the caller controls on every
host, and `selftest` checks on Windows that a live non-member process is
neither admitted, waited on, nor terminated.

`WorkflowSetupTests` in `tools/matrix_shard_test.py` replays both step bodies.
Six replays used to rerun this identical native command, about 19–23 s each on
hosted Windows, so the suite grew from 38 s to about 178 s and sat on the
critical Windows release path (#3912). The class now runs the step's exact
`bootstrap_process_owner_test` once, on a copy of its source closure, under the
120-second Windows child deadline. The replays' 30-second deadline left about
7 s of margin for this command alone; run beside the other suite lanes on a
slower hosted x86-64 runner, the single run exceeded it. Each replay substitutes only that invocation with
the verified output; the step still tees, parses and exports it, and the fixture
suites require the exported helper path. A changed invocation runs for real. A
changed driver, Clang or closure requalifies before the next replay, and a
missing, corrupt or stale record fails the replay. The real steps still qualify
the helper on every release lane.

Unix uses isolated process groups. Direct children retain the ten-second cleanup
deadline. Workers finish before
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
