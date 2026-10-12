# Android payload deadlines

The Android on-emulator deadline is a **hung-payload watchdog**, not a compiler
performance budget. It covers the complete compiler test payload after logcat
monitoring starts. Boot, adb commands, APK installation, and emulator cleanup
have separate deadlines.

## Recorded baselines and defaults

The earlier #728 baseline (42 s Debug, 16–18 s Release, about 1.5x hosted
variance) predates the intended deep logical-chain coverage in #3225. #3265
subsequently removed two quadratic switch-overlap scans while retaining the
65,535-label fixture. Post-fix Debug still reached the 180 s watchdog on slow
hosts; #3258 records the measured cause and the new sizing.

The failing and fast jobs below both checked out
`11be402e1b27e0e68aa26a57bdd44eea0c887bbc` (PR #3273 merge checkout,
head `1775237cd716fcf6cd8eab3a4ff4b1c2f54a889d`), on the same hosted
ubuntu-26.04 image. These are different hosts at identical source, not a
before/after compiler speedup.

| Configuration | Slow attempt 1 | Fast attempt 2 | Default watchdog |
|---|---:|---:|---:|
| Debug | 180 s cutoff; 59/62 modules completed | 75 s; all 62 modules pass | 240 s |
| Release | 52 s; passes | 21 s; passes | 60 s |

Receipts: [slow job 113913719062](https://github.com/buster14a/buster/actions/runs/37958029351/job/113913719062)
and [fast job 113923566277](https://github.com/buster14a/buster/actions/runs/37958029351/job/113923566277).
The slow log retains the last Debug fixture, `compiler_driver_test_native_frame_vectors`
(index 116), when the slow attempt stops. Release success leaves the Debug
failure intact. Module measurements use `TEST_MODULE_TIMING`; fixture
measurements use consecutive device timestamps on `TEST_FIXTURE_START_V1`.

| Debug module | Fast seconds | Slow seconds | Slow/fast |
|---|---:|---:|---:|
| c_frontend_tests | 40.781 | 108.668 | 2.66x |
| c_ast_tests | 2.076 | 5.167 | 2.49x |
| aarch64_encoding_tests | 2.838 | 5.989 | 2.11x |
| c_macro_conditional_tests | 1.631 | 3.878 | 2.38x |
| x86_64_completion_census_tests | 2.165 | 4.714 | 2.18x |
| Same 59 completed modules, sum | 59.775 | 152.691 | 2.55x |

The fast full-module sum is 74.415 s; `compiler_driver_tests` accounts for
14.640 s (19.7%), and `c_frontend_tests` for 54.8%. Driver fixtures already
completed by the cutoff also slow: `object_write_limits` 1.98 → 4.92 s,
`machine_fallback` 1.37 → 3.71 s and `large_static_initializers` 1.16 → 2.99 s.
The interrupted `native_frame_vectors` has no complete slow duration. The
[full same-source analysis](https://github.com/buster14a/buster/issues/3258#issuecomment-6089950433)
records module and driver-fixture comparisons. Variation affects unrelated
modules and Release as well as Debug; it is not one concentrated code tail.
The exact physical host mechanism remains unmeasured.

Applying the matched 2.554x module ratio to the fast full sum estimates
190.089 s for slow Debug completion. This is an extrapolation, not an observed
completed run. The 240 s Debug watchdog is 3.2x the observed 75 s fast payload
and about 1.26x that slow estimate. The executable sizing control rounds host
variation up to 2.6x (195 s); that fits 240 s and still triggers the retained
40% warning. Release's recorded slow 52 s remains below its unchanged 60 s
watchdog and warns. Restore/reassess Debug sizing against matched emulator
receipts if future payload-cost fixes make this allowance unnecessary.

The standalone `android/run_tests.sh` default remains 180 s. CI selects the
configuration-specific defaults before calling that monitor. Terminal success
or failure markers stop monitoring immediately; missing or malformed markers,
assertion failures and watchdog expiry still fail. This sizing changes no
compiler behavior and makes no approved-host performance claim.

No test is skipped, shortened, disabled, or moved to fit either watchdog.

## Headroom warning

Android CI passes a default warning margin of **40%**. A 1.5x slowdown consumes
one third of the original deadline as headroom, so 40% reports risk before that
ordinary variance can make the lane red. In the recorded 42-of-60-second Debug
run, 18 seconds (30%) remained; the former 25% threshold did not warn, while the
40% threshold does.

The warning is diagnostic only. A payload with a terminal success marker still
passes. Treat `headroom_warning=yes` as a reason to investigate suite growth or a
payload regression, not as an automatic reason to enlarge the watchdog.

## Overrides

`BUSTER_ANDROID_TEST_TIMEOUT_SECONDS` remains the highest-precedence shared
override and preserves existing launcher and harness behavior. When it is not
set, CI accepts per-configuration overrides:

- `BUSTER_ANDROID_DEBUG_TEST_TIMEOUT_SECONDS`
- `BUSTER_ANDROID_RELEASE_TEST_TIMEOUT_SECONDS`

`BUSTER_ANDROID_TEST_HEADROOM_WARNING_PERCENT` overrides the 40% CI warning
margin. `android/run_tests.sh` validates the selected timeout and percentage.

The policy and its precedence are covered without an emulator by:

```sh
bash android/payload_deadline_policy_test.sh
```

The existing monitor harness continues to prove that a genuinely hung payload
fails with its deadline named:

```sh
bash android/run_tests_test.sh
```
