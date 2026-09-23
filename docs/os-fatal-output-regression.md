# Fatal-output subprocess isolation

[Tests and CI](agents/testing.md) · Regression for GitHub issue [#961](https://github.com/buster14a/buster/issues/961).

## Failure and cause

The macOS x86-64 Clang Debug sanitizer run on 2026-09-22 failed the first
`raw-live` and `formatted-live` fatal-output children at the existing
30,000,000-microsecond deadline. Its `os_tests` row reported 961 passing and
six failing assertions out of 967, with a module duration of 131,936,887,000 ns.

Retained source evidence:

- Run [35760863730, job 106858258898](https://github.com/buster14a/buster/actions/runs/35760863730/job/106858258898),
  merge-group revision `aeec596c9a1b11e9493466674fb67749d2c08759`.
- Artifact `desktop-macos-x86_64-checks-35760863730-1`, ID `10710906466`.
  Downloaded ZIP SHA-256: `3cdfee0a31f9f173bf0c93e2999799189b9150901a88cd0ef858b3bf9034bd28`.
- Extracted `combinations.log` SHA-256:
  `0232921ee5d4dec4dd2fc2424d298f86e9add776e9b029074c90429325876378`.

The two timed-out children consumed respectively 5.274873/19.599891 and
5.094743/19.005109 seconds of user/system CPU. By contrast, nearby existing
early-dispatched OS helpers used roughly 0.3 seconds of CPU. These are CPU
observations, not wall-clock measurements or compiler-throughput results.
The log alone does not identify the cost of each pre-payload operation.

Source inspection at `c4db10072425ef67db133eaa115593f4927ff9c3` explains the
unintended workload: `BUSTER_OS_FATAL_OUTPUT_MODE` was handled only at the
start of `os_tests`. Reaching that descriptor from `library_tests` first ran
`compiler_prewarm` and seven unrelated modules, including the sanitizer suite
and its subprocess controls. Resource-failure, flood and process-tree child
modes already used an early hook and avoided this work. Thus the fatal-output
budget included a nested test-suite prefix, not merely the fatal payload.

## Fix boundary

`library_tests` routes a selected fatal child directly to the existing
first-block payload in `os_tests`, alongside the early OS child hook, before
compiler prewarming, temporary-root initialization or descriptor execution.
The payload remains in one place. An unexpected return exits with a distinct
non-payload status rather than entering the ordinary suite.

No production fatal formatter, low-level write, process wait or timeout policy
changes. Ordinary parent runs still execute every original OS test. Raw and
formatted live/closed stream cases, Linux `/dev/full` cases, exact exit-status
and stderr assertions, and all original deadlines remain intact. Mobile and
non-test builds retain their existing behavior.

## Executable regression and acceptance

The registered OS descriptor calls `test_os_with_fatal_child_isolation`. It
first runs the unchanged `os_tests`, then runs three additional repetitions
of each live mode. Each child receives `test --verbose=1 --ci=1`, inherits the
sanitizer environment, and uses `BUSTER_TEST_JOBS=1`. Both streams are captured.
Each probe must spawn, finish before the same 30-second deadline, terminate
normally with exit code 1, write exactly the following 52 bytes to stderr,
and write zero bytes to stdout:

```text
fatal-output-37 at os-fail-regression.c:19 in child
```

The newline is part of the expected bytes. Verbose nested modules produce
stdout, so the isolation assertion detects late dispatch even when a faster
machine finishes the accidental work within the deadline. A launch failure,
timeout, crash, wrong exit code, wrong/missing diagnostic or unrelated stdout
remains a failed assertion; none is converted to a passing result.

Every completed probe emits `OS_FATAL_OUTPUT_CHILD_V1` with mode, zero-based
repetition, spawn-plus-wait wall duration in nanoseconds, timeout state,
platform status, and byte counts. A spawn failure emits `status=spawn-failed`.
These rows are diagnostics; the normal OS assertion accounting decides pass
or fail. They run in the existing CI matrix, not an optional workflow.

On an idle configured checkout, sanitized validation uses the normal build
entry points (generation recreates the selected build directory):

```sh
./build.sh generate --sanitize
./build.sh build --config Debug -t test_all
```

For #961 acceptance, retain the exact tested revision and the actual hosted
`macos-26-intel` Clang Debug sanitizer log. Require six successful probe rows
(two modes times three repetitions), `stdout_bytes=0`, `stderr_bytes=52`,
`timed_out=0`, and passing native-exit assertions. Also require the complete
OS suite, the affected macOS x86-64 job, and the repository's required PR
checks. A source review or a different platform's pass is not a substitute
for that native evidence.
