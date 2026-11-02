# Native self-host qualification (#1111)

`.github/workflows/selfhost.yml` continues a **successful scheduled or manually
requested main-branch run** of `Tests and Zen 5 benchmarks`. That upstream run
must finish its hosted suites, sanitizers, fuzzing and native correctness before
this workflow can use the 9700X. It checks out the upstream run's exact
`head_sha`, not the possibly newer default-branch revision. A failed prerequisite,
fork, pull-request run or ordinary main push cannot start its hardware job.
The existing runner job hooks hold the shared host lock.

Pull requests and main pushes touching the harness, its workflow, the build
script or the correctness inventory run only the inexpensive harness regression
job here. Existing normal compiler CI is unchanged. The bootstrap is initially
on the daily/manual path: **there is no measured 9700X bootstrap budget yet**.
Its first successful artifact supplies the compile/link/smoke/probe times needed
to decide whether a later change should run it per PR. The initial job limit is
60 minutes, with a 600-second limit per subprocess and a 10-second termination
grace. These are safety bounds, not performance acceptance thresholds.

To request the native gate manually, dispatch the existing **Tests and Zen 5
benchmarks** workflow on `main`; its successful completion starts this follow-on
workflow. `workflow_run` workflows become active when their definition reaches
the default branch. A newly opened implementation PR therefore validates the
harness, but does not claim an already completed native bootstrap.

## What must pass

`tools/ci/selfhost.c` consumes `build.sh print-sources` and
`build.sh print-compile-flags` as NUL-delimited argument vectors. There is no copied
41-unit source list, shell reevaluation or wildcard link order. It compiles every
listed unit separately with the Clang-built Buster and `--lanes 1`, then links the
objects in source-list order using `clang -no-pie`, `-lm`, `-ldl` and `-lpthread`.
The default-tier bootstrap follows #1111's working recipe: it omits Clang's
`-O3`, `-Wall` and `-Werror`, while preserving the explicit overflow, unsigned-char,
aliasing, stack-protector and recorded host-default flags. Debug, sanitizer and
fuzz exports are rejected. This is not a Tier 1 self-host qualification.

The native workflow requires `BUSTER_HOST_VECTOR=1`, rather than allowing a
scalar run to stand in for native coverage. Baseline `-march=x86-64` self-hosting
still depends on #1108. Sequential, single-lane unit compilation avoids the
multi-unit memory multiplication in #1109; it does not claim to fix that issue
or cap the memory required by one large unit. Do not replace it with one
multi-unit command before that path has its own memory qualification.

The self-hosted compiler must pass 14 smoke commands from the correctness
inventory: baked tables, lexer, encoder, branch sizing, dataflow, link, types,
refinement on `src`, lanes, IR, calling convention, diagnostics, bindings and
x86 semantics. The tool verifies that each command still exists in
`tools/ci/correctness.tests`; costly optional cross-check flags are omitted.

Five original deterministic probes cover wrapping arithmetic/unsigned char,
aggregate calls and floating point, static-data/function-pointer relocations,
8,000 called functions and an 8,000-case switch. Both compilers build each probe
as an object and an executable. All ten pairs must be byte-identical, and both
executables in every pair must exit zero against the probe's embedded independent
expectation. The generator is `write_probe` in the harness; its fixed integer
constants are its only inputs. No external fixture or implementation is imported.

Both compilers receive the same source and output paths. Each result is moved
aside before the next compiler runs; missing or empty outputs cannot reuse the
previous stage's file. The entire output directory must be new. The environment
fixes locale, timezone and `SOURCE_DATE_EPOCH`. This checks **probe-output
equivalence**, not a rebuild and comparison of complete stage-2/stage-3 compiler
binaries, and not a proof of correctness for every compiler path.

## Evidence and local reproduction

The artifact retains the exact revision and toolchain, exported argv, all command
logs, per-command monotonic wall time/peak RSS/status, generated probe sources,
and both compared outputs. `PASS` is written only after every check succeeds;
partial reports survive errors and timeouts. The summary separates bootstrap
compilation, host linking, smoke tests and probes, and records both compiler
sizes. Large bootstrap objects/compiler binaries are not uploaded by default.
Raw timings are CI artifacts, not checked-in measurements. Any durable performance
campaign belongs in `buster14a/buster-performance` under its measurement policy.

From the repository root on an eligible native Linux host, use these single-line
commands (choose an unused absolute output path):

```sh
BUSTER_HOST_ARCH=native BUSTER_DEBUG_INFO=0 BUSTER_SANITIZER=none BUSTER_FUZZ_AVAILABLE=0 ./build.sh
clang -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing tools/ci/selfhost.c -o /tmp/buster-selfhost-gate
BUSTER_HOST_ARCH=native BUSTER_DEBUG_INFO=0 BUSTER_SANITIZER=none BUSTER_FUZZ_AVAILABLE=0 ./build.sh print-sources > /tmp/buster-selfhost-sources.argv
BUSTER_HOST_ARCH=native BUSTER_DEBUG_INFO=0 BUSTER_SANITIZER=none BUSTER_FUZZ_AVAILABLE=0 ./build.sh print-compile-flags > /tmp/buster-selfhost-flags.argv
/tmp/buster-selfhost-gate "$PWD/build/compiler" /tmp/buster-selfhost-sources.argv /tmp/buster-selfhost-flags.argv /tmp/buster-selfhost-result 600
```

The standalone harness tests require only Clang and GNU `timeout`, not a built
Buster or AVX-512. They use subprocess stand-ins to test failures, signals,
timeouts, missing/empty outputs, failed smoke/execution, object/executable byte
differences, invalid lists, duplicate/missing sources, stale directories, inventory
drift and argv containing spaces/quotes. The five generated probes also compile
and execute independently with real Clang. The hosted job runs these checks with
fatal AddressSanitizer, UndefinedBehaviorSanitizer and leak detection:

```sh
clang -std=c17 -O1 -g -Wall -Wextra -Werror -fno-strict-aliasing -fsanitize=address,undefined -fno-sanitize-recover=all tests/ci/selfhost.c -o /tmp/test-selfhost
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/test-selfhost
```
