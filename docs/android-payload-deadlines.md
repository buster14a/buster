# Android payload deadlines

The Android on-emulator deadline is a **hung-payload watchdog**, not a compiler
performance budget. It covers the complete compiler test payload after logcat
monitoring starts. Boot, adb commands, APK installation, and emulator cleanup
have separate deadlines.

## Recorded baselines and defaults

Issue #728 records equivalent hosted `ubuntu-26.04` emulator runs where Debug
completed in 42 seconds and Release completed in 16-18 seconds. The same work
varied by about 1.5x between hosts. Android CI therefore selects configuration
specific defaults before invoking `android/run_tests.sh`:

| Configuration | Recorded baseline | 1.5x baseline | Default watchdog |
|---|---:|---:|---:|
| Debug | 42 s | 63 s | 180 s |
| Release | 16-18 s | 24-27 s | 60 s |

The Debug value preserves the 180-second whole-suite watchdog introduced after
real Debug compiler-driver fixtures exceeded the former one-minute limit. The
Release value retains more than twice the observed 1.5x runtime without copying
Debug's much larger budget. Terminal success or failure markers still stop the
monitor immediately; a missing marker or a payload that reaches its selected
deadline still fails and names that deadline in the output.

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
