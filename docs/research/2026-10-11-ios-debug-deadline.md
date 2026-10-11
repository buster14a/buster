# iOS Debug launch deadline evidence (#3585)

This is historical hosted diagnostic evidence, not qualified performance
validation or a completed root-cause attribution. The launch budget remains
300 seconds; no deadline increase follows from these measurements.

## Identities and source equivalence

- Green: merge-group commit `f1d937e2b96e3fdaae21829ea34b57d35ddbb80f`,
  [run 38155183276, attempt 1](https://github.com/buster14a/buster/actions/runs/38155183276/attempts/1),
  job `114516656552`, artifact `11707399977`
  (`mobile-ios-aarch64-38155183276-1`).
- Timeout: group `c4e6060d`,
  [run 38155610095, attempt 1](https://github.com/buster14a/buster/actions/runs/38155610095/attempts/1),
  job `114517924234`, artifact `11708920554`
  (`mobile-ios-aarch64-38155610095-1`).
- The failing group adds one request-history line from #3565 to its first
  parent; compiler and test source is identical. The owner recorded this
  comparison on [#3585](https://github.com/buster14a/buster/issues/3585).
- Both jobs use the macOS AArch64 hosted simulator lane, Xcode 26.6,
  iPhoneSimulator26.5 SDK and runner provisioner `20260828.587`.

Measurements below come from each artifact's `ios-console.Debug.log` and
`ios-console.Release.log`, using `TEST_MODULE_TIMING duration_ns`. Keep configs
separate. These are wall intervals, not process CPU usage or exclusive fixture
costs. Parallel module intervals may overlap and their sum is not elapsed suite
time. Both Debug artifacts report the same assertion counts for every completed
module; both frontend modules report 451 fixture scopes and 1,469,196 passing
assertions, with no failures.

## Debug module comparison

| Module | Green seconds | Timeout seconds | Difference seconds |
| --- | ---: | ---: | ---: |
| c_frontend_tests | 93.917853 | 205.375850 | 111.457997 |
| c_ast_tests | 9.174386 | 18.276167 | 9.101781 |
| machine_tests | 1.634937 | 6.895078 | 5.260141 |
| x86_64_metadata_tests | 2.932494 | 5.537493 | 2.604999 |
| aarch64_encoding_tests | 5.550093 | 7.865529 | 2.315436 |
| c_macro_conditional_tests | 2.639201 | 4.383705 | 1.744504 |
| codegen_tests | 0.567297 | 2.218133 | 1.650836 |
| assembly_tests | 1.766343 | 3.311050 | 1.544707 |
| aarch64_memory_semantics_tests | 2.293391 | 3.707653 | 1.414262 |
| ir_tests | 0.804713 | 2.118421 | 1.313708 |
| link_tests | 0.760376 | 1.959423 | 1.199047 |
| object_tests | 0.678142 | 1.767925 | 1.089783 |
| pdb_tests | 0.789669 | 1.845593 | 1.055924 |
| llvm_bitcode_tests | 0.408176 | 1.339721 | 0.931545 |

Completed modules 0–58 sum to 129.593792 seconds in green versus 276.068394
seconds in the failing run (2.1302594×). The C frontend alone contributes
111.457997 seconds of the difference. The green driver's completed interval is
22.546621 seconds; it has no completion row in the failing capture. The timeout
capture reaches `compiler_driver_test_machine_fallback`, fixture index 123, and
six completed `MIR_COVERAGE` rows before a partial seventh row. This shows
progress, not a deadlock in that fixture.

## Payload and host observations

| Observation | Green | Timeout run |
| --- | ---: | ---: |
| Release build seconds | 112 | 175 |
| Debug build seconds | 12 | 21 |
| Simulator boot seconds | 94 | 213 |
| Release host launch/test seconds | 93 | 72 |
| Debug host launch to success/deadline | 155 | 300 (deadline) |
| Release full module sum seconds | 73.049565 | 65.392874 |
| Debug full module sum seconds | 152.140687 | unavailable |

The timeout diagnostic's `TIMING_IOS test_seconds=351` includes bounded probes
after the launch deadline; it is not a 351-second test budget. Debug first
console/app trace/fixture receipt is within 2–3 seconds in both runs.

Green Debug's `fixtures-ready` to `entry-complete` native observations span
151.676855 seconds monotonic and 139.843440 seconds process CPU. Timeout Debug
has no terminal native CPU observation. Release native scope spans are
74.038407 seconds / 39.720012 CPU seconds green and
65.486432 seconds / 57.661521 CPU seconds in the timeout run. Release therefore
was not uniformly slower on the failing host. Slow boot/builds alone do not
prove the reason for the later Debug slowdown.

## What remains to establish

Historical captures have no `TEST_FIXTURE_TIMING_V1` rows. Enable the existing
all-fixture observer in simctl's child environment and pair module-named native
clock observations before changing test work or a budget. Timings should locate
which frontend fixtures consume the interval; process CPU differences should
help distinguish CPU work from elapsed scheduling/wait. They do not by themselves
provide profiler stacks or a precise operation cause. Preserve unsuccessful
attempts, all registered modules, assertion accounting, terminal markers and the
300-second launch deadline. See [fixture attribution](../driver-test-timing.md)
and [timeouts policy](../agents/workflow.md#timeouts-and-budgets).

Buster's first-party license grant is unselected according to
[LICENSES/README.md](../../LICENSES/README.md) at inspected commit `27aac483`.
No external source or dependency is added by this investigation.
