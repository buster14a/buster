# Tests and CI

[Agent instructions](../../AGENTS.md) · Paths and commands below are relative to the repository root.

## Tests

- Compiler and foundation module tests run inside the `ide` executable; there
  is no external unit-test framework. From the repository root, run
  `ide test --verbose=1 --ci=1` or build the `test_all` target.
- `ide test --module=<name>[,<name>...]` runs only the named modules. A name
  is a `TestDescriptor.name` from `test_descriptors` in
  `src/buster/tests/test.c`, such as `object_tests`, and must match exactly.
  Unknown or empty names fail before any module runs and print the names
  registered for the target. A named table audit runs even under
  `BUSTER_TEST_TABLE_AUDITS=0`. Modules at or after the parallel aarch64 group
  still get that group's table prewarm, but no state a skipped earlier module
  would have left behind. The summary reads
  `[N/N] Unit tests (k of M modules selected)`. Without `--module`, every
  module runs, as in CI and `test_all`.
- The bootstrap wrappers have a controlled platform test at
  `python3 tests/bootstrap_wrapper_test.py BootstrapWrapperTests -v`. It supplies a fake TCC and
  driver, and covers cold/warm reuse, dependency and compiler invalidation,
  corrupt/incomplete entries, failure propagation, argument forwarding and
  concurrent immutable publication. Each desktop Release shard owns this suite;
  the checks shard retains the required lifecycle step without repeating it.
  Windows runs the same tests through
  `python tools/bootstrap_wrapper_cases.py --jobs 2`: two independent scenarios
  at once, with the existing six-writer publication scenario running alone.
  All within-scenario cache transitions stay ordered. The policy step executes
  `python3 tools/bootstrap_wrapper_cases_test.py -v` on each Release lane,
  covering deadlines, launch/startup failures, cancellation, descendant cleanup,
  failure status and stable diagnostics. See [wrapper CI](../ci-bootstrap-wrapper.md).
  `python3 tests/bootstrap_wrapper_test.py -v` runs the full local behavior,
  child-process and immutable-driver build-graph harness. The authoritative
  workflow guard, budgets, logs and required-summary failure checks live in
  `tools/ci_zig_cache_test.py`, which the policy step executes on each Release
  lane. The wrapper module has no duplicate workflow contract.
- Test modules live under `src/buster/tests/` as mirrored `*_test.c` and
  `*_test.h` pairs. `src/buster/tests/test.c` owns registration. Unity builds
  include implementations into the main translation unit; non-unity builds
  compile each test source independently.
- The desktop `test_ui_slider` component target runs the focused
  `ui_slider_tests` module against production `ui_core`/`ui_builder` and an
  unused native renderer boundary. It is part of `test_all` and `test_units`
  when tests and libc are enabled, without adding UI dependencies to `ide`.
  The retained broad `ui_tests` suite remains unregistered; see
  [graphics/UI](../projects/graphics-ui.md).
- The separate desktop `test_ui_utf8` component target runs actual UI text-event
  activation and underline draw-command consumers using the production
  `ui_core` module and an inert native renderer boundary. It is included in
  `test_all` and `test_units` when tests and libc are enabled, without adding UI
  dependencies or a descriptor to `ide`; see [graphics/UI](../projects/graphics-ui.md).
- The desktop `test_ui_scale` component target (`ui_scale_component_test.c`)
  counts keyed-box lookup probes and focus-navigation work against the
  production `ui_core` and an inert renderer, with behavior controls for the box
  table. It is part of `test_all` and `test_units` when tests and libc are
  enabled; see [graphics/UI](../projects/graphics-ui.md).
- C frontend and driver fixtures live under `tests/` and use `.c`, `.h`, native
  object, archive, and shell-script inputs. Keep fixture paths relative to the
  repository root because tests intentionally exercise the real file loader.
- The Linux x86-64 parameter-alignment driver regression uses a separately
  host-compiled caller and observer with the Buster-compiled callee at every
  registered optimization level and allocator. Its expanded caller, callee
  and header are private fixture inputs under
  `src/buster/tests/compiler/driver/fixtures/parameter_alignment/`; the frozen
  `tests/basic_c_parameter_alignment*` corpus stays byte-for-byte unchanged.
  The existing host observer remains shared because its ABI is unchanged.
  These files are loaded by the registered driver test, not compiled as test
  modules. The regression asserts the selected
  allocator after parsing, verifies every function through MIR without native
  fallback (including the NONE compatibility spelling for MIR-stack), and executes aligned
  parameter reads/writes after integer, vector and combined bank exhaustion.
  Volatile caller objects independently check that callee writes stay in the
  callee's by-value copies.
- Dormant `.bbb` fixtures remain under `tests/` as preservation material. Do
  not register, compile, parse, benchmark, package, or execute them in the
  default build or CI until the custom frontend is deliberately reactivated.
- A new production module or public behavior must receive a focused module
  test. Frontend changes should cover preprocessing, parsing/diagnostics,
  semantic typing, canonical-IR lowering, and driver behavior as applicable.
- `BUSTER_TEST` records a failure and continues. When later reads depend on a
  pointer, count, status, or other prerequisite, guard that dependent body with
  `if (BUSTER_REQUIRE(arguments, prerequisite))`. It records the prerequisite
  with normal assertion accounting, evaluates it once, and skips only the
  guarded body when it fails; unrelated fixtures and modules continue.
- With a debugger attached, assertion failures stop through `os_fail()` after
  reporting the diagnostic. The arena, fixture-timing, and prerequisite harness
  self-tests set `UnitTestArguments.suppress_debugger_break` only around their
  deliberately failed assertions and clear it before any dependent body or
  later assertion. Failure counts and diagnostics remain unchanged.
  `test_debugger_failure_self_test` checks debugger-present/absent decisions,
  restoration, and argument-free failures without changing registered totals.
- Keep test-only declarations behind `BUSTER_INCLUDE_TESTS`. Private structures
  shared with tests belong in a narrow `*_internal.h` seam rather than being
  exposed through a production public header.
- Modules with only test consumers join `ide` through
  `BUSTER_COMPILER_TEST_MODULES` and a matching `#if BUSTER_INCLUDE_TESTS`
  unity include. `truetype` and the AArch64 syntax model (`aarch64_syntax.c`
  and its generated table) follow this rule; `aarch64_syntax.c` fails with
  `#error` in a tests-disabled compile, so self-host stage 1 cannot silently
  regain it.
- Place additions by name, not after the newest neighbour, so independent PRs
  land at different anchors and merge in either order. In
  `src/buster/lib/compiler/frontend/c/c_parse_internal.h`, each seam group
  (comment, types, declarations) is one blank-line-separated block ordered by
  the byte (`LC_ALL=C sort`) order of its first `c_test_` function name; insert
  a new group at its sorted position or extend the owning group, and do not
  enumerate groups in the file comment. `c_frontend_tests` in
  `src/buster/tests/compiler/frontend/c/c_test.c` registers every
  `BUSTER_TEST_FIXTURE` in one block sorted the same way, so fixtures must not
  depend on run order; inline assertions follow that block. Define a new test
  function next to the tests of the feature it covers, not after the most
  recently added one. Names that sort adjacently can still conflict.
- Active CI is defined under `.github/workflows/`; the current tree has no
  Forgejo workflow definitions. The source-free broker retirement record is
  documented in `docs/ci-github-hosted-runners.md`. `.github/workflows/ci.yml`
  runs the combination matrix, execution-mode matrix, Android and iOS on
  GitHub standard runners. Its five desktop lanes cover Linux and Windows at
  x86-64 and AArch64, plus macOS AArch64; two independent mobile shards retain
  Android x86-64 and iOS AArch64. Apple x86-64 is best-effort source/target
  compatibility, outside routine CI; see [Apple CI policy](../apple-ci-policy.md). The independent
  `UEFI firmware boot` lane executes both firmware targets in every allocator
  and retains boot evidence; see [UEFI validation](../uefi-target.md#reference-firmware-execution-gate).
  Require the
  aggregate `CI complete` result, not just the desktop names. It also requires
  the independent `Clang analyzer shards` job and its coverage/failure controls;
  see [analyzer sharding](../clang-analyze-shards.md). The separate
  `Linux x86-64 bootstrap evidence` check is required as well when the stronger
  repeated self-host audit is mandatory; `CI complete` does not aggregate it.
  The aggregate's independent desktop inventory checks exact-run job attempts
  and required step records. When the Actions API returns incomplete or stale
  metadata, it retries with 1/2/4-second backoff, at most three refreshes and
  a 30-second total metadata budget. Transient 5xx/429/transport reads retry
  in that budget, falling back to smaller jobs pages after a 5xx; a 4xx fails
  immediately. A later exact snapshot may recover a transient omission; a
  persistent empty, stale or ambiguous record fails closed, and its errors are
  printed to the log and step summary. It never borrows step proof from an older attempt when a newer attempt
  shadows that job. Run a fresh full CI attempt when required metadata remains
  unresolved; a green job-level conclusion alone is not execution evidence.
  GitHub can attach externally published admission and compiler-benchmark checks
  to this Actions inventory. `github_ci_time.py` separates their metadata only
  after exact check ID/name/head/app/namespace proof. The compiler benchmark
  additionally binds request and measurement attempts, re-reads the matching
  same-repository request and trusted-main publisher, and requires the publisher's
  exact-attempt checkout and writer-step execution. Its queued/running/completed
  verdict never supplies workload or performance acceptance. Raw job rows, check
  rows and publisher provenance are retained; same-attempt duplicates, unknown
  rows and unavailable provenance fail closed. Historical benchmark rows from
  earlier CI attempts remain separately recorded. Both main-reuse readers apply
  the same separation before validating actual workload execution (#3030).
  Both workflows cover the same PR merge revision, main/tag pushes, merge groups
  and explicit dispatches without duplicate feature-push runs. Buster CI keeps
  full matrix diagnostics for pull requests, main/tag pushes and manual runs.
  On `merge_group`, desktop and mobile enable matrix fail-fast; the native
  matrix retains `fail-fast: false` under the frozen CI test contract. The trusted
  controller cancels exact-head merge-group runs after a failed Buster CI job
  or required check from another workflow. It also stops a desktop Release
  `Workflow tool regression tests` step that is still in progress past its
  `ci.yml` budget plus grace. That budget table is mirrored in
  `.github/scripts/recover-ci.py` and checked for drift.
  See `docs/ci-workflow-audit.md` for cache trust boundaries, diagnostics,
  cancellation, coverage details, and reproduction. Every job stays inert
  until its repository variable is set, and skips itself outright on Forgejo.
  For cancelled current-PR validation, see [bounded CI recovery](../ci-cancellation-recovery.md)
  and its offline checks: `python3 tests/ci_recovery_test.py` and
  `python3 .github/scripts/test_merge_queue_fail_fast.py`.
  The lint job's `Validate the performance audit index` step also runs
  `tools/check_markdown_links.py`: every relative inline link, image and
  reference definition in tracked Markdown must resolve to a tracked path.
  Fenced code, code spans, URL schemes and `#anchor` fragments are not checked,
  and audit records get no exemption, so deleting a linked file fails with
  `file:line: target`.
  Changing a `runs-on` label means changing `.github/actionlint.yaml` too,
  because actionlint knows only the labels its own release predates. Preserve
  Debug/Release, unity/non-unity, sanitizer/fuzz, self-host, and
  supported-platform coverage when changing build orchestration or the
  compiler pipeline. The GitHub workflow keeps the five retained platform check
  names, runs full PR/main/tag/merge-group coverage without duplicate feature
  push runs, revalidates exact-key Zig archive caches, and treats UBSan reports
  as failures. Independent later suites run after earlier test failures;
  captured logs and fail-closed summaries remain outside generated build trees.
  See `docs/ci-github-actions.md` for timing cohorts and exact reproductions.
  The removed Forgejo broker has no current setup or validation commands.
  Its regression source is preserved as
  `tests/retired/github_runner_bridge_test.py.txt`, outside Python discovery,
  with the same dependency-only support identity. It is historical input,
  not executed bridge coverage; see `docs/ci-github-hosted-runners.md`.

- The workflow-tools aggregate regression executes the actual `CI complete`
  shell body for all 633 shard outcomes. Git Bash on Windows has a 120-second
  subprocess budget; Unix retains 30 seconds. A completed run must still report
  exactly 633 cases and fail for every missing, failed, skipped, or cancelled
  shard. The test file is in the reviewed native-retirement support ledger;
  change its exact byte/hash row through a policy transition.

- Test implementations are not registered as modules; add new test pairs under
  `src/buster/tests/`, add
  the implementation to `BUSTER_TEST_SOURCES` and the header to
  `BUSTER_TEST_HEADERS` in `CMakeLists.txt`, and include both from
  `src/buster/tests/test.c` in the existing registration order (the
  implementation only in the `BUSTER_UNITY_BUILD` block).
- `sanitizer_tests` is the executable contract for sanitized correctness
  launches. Under the effective `BUSTER_TEST_ENV` it runs freed main- and
  worker-thread controls, a fatal undefined-shift child, and Linux
  main/worker-thread leak children. It requires the reserved UBSan/LSan exit
  codes and their own diagnostics, so a
  launch failure, crash, or unrelated stderr cannot pass. Unsupported
  LeakSanitizer platforms emit an explicit `status=unsupported` record instead
  of being reported as a canary pass. The canary environment variable is a
  private subprocess seam; use the registered suite rather than invoking it as
  standalone evidence.
- `sanitizer_tests` also pins the invariant-macro selection in every
  configuration. `BUSTER_CHECK`, `BUSTER_ASSERT` and `BUSTER_UNREACHABLE`
  become optimizer assumptions or unevaluated expressions only under
  `BUSTER_OPTIMIZE && !BUSTER_SANITIZE`; Debug and sanitized builds at every
  optimization level keep them as diagnostics. The suite compares the BUSTER_
  defines with the compiler's own `__OPTIMIZE__` and AddressSanitizer
  predicates, counts operand evaluations (an optimized unsanitized
  `BUSTER_ASSERT` must evaluate nothing), and, where the macros are
  diagnostics, requires false `BUSTER_CHECK`/`BUSTER_ASSERT` children to exit
  1 with their `assertion failed at` report and, outside Windows, a
  `BUSTER_UNREACHABLE` child to die by SIGILL or SIGTRAP. Android and iOS
  payloads cannot relaunch themselves, so they run only the in-process
  controls and report the children as `status=unsupported`. It never executes
  a false raw assumption.
  It also requires `BUSTER_REFERENCE_CHECKS` (the reference-equivalence
  checks once reached only in Debug) to equal that checked-contract
  configuration. Routine CI runs this suite in the optimized sanitized
  Release tree; sanitized Debug is compile-and-link coverage there
  ([sanitizer execution policy](../ci-combination-shards.md#sanitizer-execution-policy-2657)).
- **Adding a module** (`foo.c`/`foo.h` under `src/buster/lib/`) takes three
  edits: (1) `buster_register_module(foo ...)` in `CMakeLists.txt`;
  (2) add `foo` to the `MODULES` list of `buster_add_executable(ide ...)`;
  (3) add `#include <buster/lib/foo.c>` to the `BUSTER_UNITY_BUILD` block at
  the top of `src/buster/apps/ide/ide.c` — optimized non-sanitized configs compile as
  unity builds, so forgetting this breaks ordinary Release builds.
- Headers are included as `<buster/lib/...>` or `<buster/tests/...>` (include
  root is `src/`).
  `compile_commands.json` is exported to `build/` by default.

- Mobile build-graph regressions run at the start of
  `tests/mobile_ci_scripts_test.sh`. The Android and iOS fixture suites include
  the production CMake graph with controlled targets and real Ninja
  Multi-Config scheduling. They are host graph evidence; native mobile
  compilation and device/simulator execution remain separate CI gates. Android
  resolves safe `.` and `..` segments inside the rooted APK asset namespace so
  nested quoted includes consume the same fixture bytes as desktop tests;
  traversal above the asset root is rejected.
- The iOS payload runs `test --verbose=1 --ci=1`. Launches emit
  opt-in `BUSTER_IOS_LAUNCH_V1` records, enabled by the launcher through
  `SIMCTL_CHILD_BUSTER_IOS_LAUNCH_TRACE=1`, from native `main`, UIKit entry,
  delegate entry, window readiness, worker creation/entry, runtime and argument
  readiness, suite entry, compiler prewarm completion, fixture readiness and
  entry completion. Each fixed-size direct stderr write carries PID, app
  monotonic/wall microseconds, process CPU microseconds and separate clock/query
  statuses; nonzero statuses make the corresponding measurement unavailable.
  This path needs no arena or thread context. Right after the `main` record,
  `BUSTER_IOS_PROCESS_V1` reports the kernel's process start wall time
  (`start_wall_us`, from `sysctl` `KERN_PROC_PID`) and `start_status`. Host
  launch to `start_wall_us` is simulator spawn scheduling. `start_wall_us` to
  the `main` wall time is loader and static-initialization work.
  `ios/test_ci.sh` launches Release before Debug by default (checked by
  `ios/hosted_signing_budget_test.py`), so the first
  launch on a freshly booted device does not consume the Debug budget (#2819).
  `BUSTER_IOS_LAUNCH_OBSERVATION`
  records the host's first polled console, app trace and fixture receipt using
  the existing Bash launch clock. Poll observations include scheduling and
  scanning delay and are not native timestamps; missing events stay absent.
  Compare app monotonic deltas to separate UIKit, worker/runtime and pre-fixture
  preparation, process CPU deltas to distinguish CPU use from elapsed time,
  and app wall time with host log receipt to investigate console delay (wall
  clocks can adjust). No record proves a cause for a historical timeout or
  changes marker acceptance, process ownership or the launch deadline.
  `python3 ios/launch_trace_test.py -v` executes the native producer with
  host POSIX clocks. `bash ios/launch_trace_monitor_test.sh` checks delayed
  receipt and trace-only deadline rejection without modifying the frozen shared
  fixtures; UIKit and actual simulator acceptance remain in mobile CI.
  Failed launches report
  `BUSTER_IOS_TEST_PROGRESS` with the last completed `TEST_MODULE_TIMING`
  module/index and the last module observed in timing or arena records;
  `unavailable` means no such record arrived. The last observed module is
  evidence of progress, not a claim that it is still running. Simulator
  process-table, unified-log and crash-report probes retain separate bounded
  lifecycle receipts and up to 64 KiB of stdout/stderr per command, with
  native exit, timeout/helper status and capture completion distinguished.
  `ios/lifecycle_capture_bridge.sh` starts GNU command and capture clocks before
  interpreter startup. The launcher and caller-clock controls share
  `ios/gnu_timeout.py`: it prefers `gnutimeout`/`gtimeout`, verifies bounded
  GNU `--version` output with its own clock and owned probe cleanup, then uses
  one absolute executable path and retains that path/version. Bare `timeout`
  is admitted only after the same verification. Unknown, malformed, oversized,
  hanging or nonzero providers refuse selection; unresolved probe cleanup is
  fatal. The hosted Linux fixture installs `gnu-coreutils` aliases only when
  existing providers cannot be verified, preserving its system coreutils
  provider. `python3 ios/gnu_timeout_test.py -v` covers those boundaries.
  Those timers own only bootstrap/collector groups; the
  initialized `ios/lifecycle_capture.py` owner detaches before admitting a
  keeper or native command. The owner anchors its private command group with a
  deliberately unreaped keeper and retains a separate owned native handle for
  a command that leaves that group. Native exit/signal, command-clock result,
  real output EOF and owned cleanup remain separate facts. Late native exit 0
  does not turn a deadline into success. The first 64 KiB are retained while
  remaining output is drained. The existing command, at-most-ten-second TERM
  grace and absolute capture budgets remain; loss of the collector's lifetime
  pipe triggers final owned dispatch without a new post-cap grace period.
  Native completion without observed EOF triggers owned cleanup; that
  observation alone does not prove a descendant is alive. Escaped grandchildren
  remain outside signal authority and can yield incomplete capture at the bound.
  Complete capture requires a real zero-byte output read before the capture
  bound. Ordinary keeper release also waits for one complete, authenticated
  command-monitor result and final protocol EOF. The collector requires a
  complete helper token, real completion-pipe EOF and matching actual invocation
  wait status. It snapshots only that closed private generation before writing
  its admission receipt. One batch copies the valid regular, non-symlink
  snapshot files, including binary output, within the existing caller clock;
  symlink or nonregular stable destinations and failed or interrupted copies
  refuse admission. This removes repeated
  copy/rename launches after an observed interruption during stable receipt
  promotion, without establishing the runner's delay cause or a future native
  result. Expiry, cancellation, malformed handoffs or a shim's
  mismatched exit refuse admission; a residual old owner can only publish in
  its old generation. The two caller records use guarded builtin writes;
  admission requires the actual collector exit zero and both complete matching
  records, including matching helper/invocation statuses. Private fixed-order
  Bash `SECONDS` stages observe setup, monitor launch, interpreter handoff and
  publication without changing either clock or authorizing admission. Builtin
  path trimming and caller writes remove known external launches. The seven
  private IPC endpoints use the same fresh generation directory as the receipts,
  removing a redundant directory-process launch before monitor/helper startup;
  the existing clocks and descriptor protocol are unchanged. A retained
  late-start failure does not establish the cause of its post-setup delay or
  a hosted speed improvement. Startup refusal preserves any already observed
  command-monitor deadline even when the caller is then lost.
  The owner and keeper use only standard-library imports and invoke Python with
  [`-S`](https://docs.python.org/3/using/cmdline.html#cmdoption-S), which disables
  automatic [`site`](https://docs.python.org/3/library/site.html) initialization
  and its customization hooks. Payload arguments and environment are unchanged.
  The dedicated keeper enters its unchanged ready/control/acknowledgment protocol
  after importing only `os`, `signal` and `sys`; owner-only imports are skipped.
  Its two private descriptor arguments are validated before descriptor access.
  Imported-module and owner entry paths retain their ordinary initialization.
  A finite thirteen-second site hook remains unentered while both roles complete
  and are reaped; separate retained counterfactuals show this is a reachable
  startup delay class, not the established cause of the hosted late-start failure.
  Caller cancellation retains status 130/143. Residual
  cleanup and unknown reaps remain failures, rather than synchronized-cleanup
  claims. Ordinary EOF releases only the keeper and does not prove silent
  descendants are gone. Named receipts retain dispatches, authority release,
  native/keeper reaps and keeper control-EOF acknowledgement. After writing that
  acknowledgement and closing its private descriptors, the dedicated keeper
  exits directly with zero; it owns no payload or buffered output requiring
  interpreter finalization. The owner still requires both the exact
  acknowledgement and a real zero reap within the existing release bound.
  A finite injected finalization delay reproduces acknowledgement plus keeper
  SIGKILL/cleanup failure, while the direct exit succeeds; before-acknowledgement
  hangs and abnormal exits still fail. This control establishes the reachable
  finalization tail, not the cause or acknowledgement timing of a native probe
  failure. Probe failure does not establish an app crash.
  `bash ios/launch_diagnostics_mock_test.sh` runs the existing attached-monitor
  controls, `python3 ios/lifecycle_capture_test.py -v` and the caller-clock
  controls in `python3 ios/lifecycle_capture_bridge_test.py -v`. They cover
  actual native statuses versus launch errors, binary retention, held and
  escaped writers, cancellation and keeper failure, startup clock expiry,
  ignored/blocked signals, final protocol EOF, actual shim exit, generation
  isolation and malformed receipt refusal. The signing fixture also requires
  empty or malformed capture receipts to prevent install/launch, including
  native-success cases. The Python lifecycle and caller-clock controls use
  owned handles or finite fixture release markers; they do not claim
  CoreSimulator descendants are contained by a group.
  The ten-minute hosted fixture job runs four independent signing, install,
  attached-monitor and shared-mobile groups concurrently. The attached group
  retains its capture/caller/mock sequence; the shared group retains its asset
  graphs/mobile cases/legacy monitor sequence. `ios/monitor_groups.py` gives each
  group separate evidence, temporary and working directories plus an immediately
  owned session-leader anchor. Payloads use absolute repository script paths.
  Private pipes retain all four payload statuses without reaping any anchor;
  final group KILL precedes individual anchor waits and the first nonzero status
  in the fixed role order is propagated. INT/TERM retains
  status 130/143 through one-second TERM grace, final KILL and one absolute
  one-second reap bound while all anchors remain owned. Android controls begin
  only after all four groups pass. `python3 ios/monitor_workflow_test.py -v`
  exercises the actual extracted
  workflow with finite leaf fixtures for four-way overlap, directory isolation,
  individual/multiple failures, independent waits, cancellation and conditional
  GNU availability. The frozen shared mobile fixture and all command/capture/job
  deadlines remain unchanged.
  `bash ios/launch_diagnostics_simulator_test.sh` uses a synthetic timed-out
  payload with real CoreSimulator boot, probes and shutdown on hosted macOS
  ARM64. The mobile lifecycle workflow retains actual probe availability and
  failure reasons there; real app compilation/execution remains in mobile CI.
  These optional diagnostics retain a ten-second command deadline and a
  thirty-second caller capture cap. A hosted phase observed at 42 seconds in
  #2742 had no promoted snapshot; increasing those evidence budgets would delay
  failure reporting without making the launch verdict stronger. The native
  control therefore accepts an explicitly unavailable probe with an actual
  command- or caller-clock expiry receipt and warning, including expiry before
  native admission when the requested command and declined-admission proof are
  retained. Missing proof of a native attempt cannot establish probe success.
  Missing/malformed protocol
  evidence, tracebacks and oversized output still fail. A closed final
  completion pipe becomes a private helper-failure receipt without retry;
  native exit, deadline and cancellation facts remain separate. The bridge
  controls cover closed completion after native success, deadline and INT/TERM,
  plus accepted/rejected native-control receipt shapes without a simulator.
- On GitHub-hosted macOS arm64, `ios/test_ci.sh` supplies a 180-second
  codesign deadline when the caller has not supplied one. This is separate
  from the test-execution, boot, install, and shutdown deadlines. Local and
  self-hosted defaults remain unchanged, and an explicit
  `BUSTER_IOS_CODESIGN_TIMEOUT_SECONDS` is preserved for launcher validation.
  No signing retry or failure suppression is introduced. The policy and native
  status propagation are covered by `python3 ios/hosted_signing_budget_test.py`
  in the mobile lifecycle workflow; actual Apple signing and simulator tests
  remain a distinct native CI gate.
- For an invocation-owned hosted ARM64 device, a true first `bootstatus -b`
  helper timeout gets one 120-second continuation on the same UDID before the
  existing single replacement. `BUSTER_IOS_BOOT_CONTINUATION_SECONDS` can
  override that positive budget for a controlled diagnostic run. A caller that
  shortens the first 180-second readiness deadline opts into continuation by
  setting this override; otherwise its original single-replacement budget is
  preserved. The replacement
  still gets only one readiness check; native exit 124 and borrowed, explicit,
  local, and self-hosted devices do not enter continuation or replacement.
  Only a successful `bootstatus -b` permits app execution. Each failed boot
  phase retains its own UDID/runtime/source context and bounded host probes;
  command and capture timings and missing capture receipts remain distinct.
- For an invocation-owned GitHub-hosted macOS arm64 simulator, a true
  shutdown-helper timeout is first reconciled against one bounded exact-UDID
  state probe. If a successful payload still leaves that device non-Shutdown,
  the launcher retries shutdown for that exact UDID once and requires a second
  bounded probe to prove `Shutdown`. Retry rejection, ambiguous or malformed
  identity evidence, a final non-Shutdown state, borrowed/explicit devices,
  and every pre-existing payload failure remain failed. The retained recovery
  receipt distinguishes this path from direct shutdown and the already-Shutdown
  timeout reconciliation; `ios/hosted_signing_budget_test.py` covers both the
  successful and fail-closed cases.
- Android SDK setup uses `tools/ci_android_sdk.py` with three bounded attempts.
  Each attempt requests only structurally missing or invalid packages, keeping
  valid preinstalled packages and packages completed by an earlier attempt.
  Detailed `sdkmanager` output and initial package validation are retained in
  the mobile artifact. Every required package is validated after each attempt;
  a nonzero installer status or invalid package still fails setup. Run
  `python3 tools/ci_android_sdk_test.py -v` for the hermetic setup controls.
- Qualification tool observations use `tools/ci_checks_tools.py` only when
  `BUSTER_CI_CONDITIONS_EVIDENCE=1`. `python3 tools/ci_checks_tools_test.py -v`
  exercises selected CMake/override paths, exact source/run/job binding,
  deadlines, output limits, tool replacement and disabled no-op behavior using
  Python-only fake tools. `python3 tools/ci_checks_qualification_test.py -v`
  covers required role keys, wrong/unknown observations, digest-bound selected
  tools, unchanged split-role maps and the existing Android validity scope.
  Workflow lint runs both controls normally; the qualification branches alone
  retain real selected Go/Ninja/adb receipts. No compiler build or measurement
  dispatch is needed to run these controls. Missing historical observations
  remain pending; see [checks qualification](../ci-combination-shards.md#further-checks-partition-qualification-2120).
- The same opt-in retains `buster-ci/ios-simulator-selection.json` from the
  selected discovery record, or the arguments and UUID of an actual creation.
  `tools/ci_ios_simulator.py` adds no simulator query and preserves the launcher's
  first available name match. Explicit UUIDs and missing observed fields remain
  unknown. `python3 -B tools/ci_ios_simulator_test.py -v` checks this producer with
  finite JSON; workflow lint also runs it. Qualification requires a digest-bound
  `simulator_selection` reference with matching source/run/attempt/mobile-job
  identity. Runtime and device type are comparable; the initial UUID remains
  provenance. The CI batch selects once for Debug and Release; collection must
  join that UUID to the initial launcher log, reject retention failures or
  repeated selection, and retain any distinct replacement UUID from recovery.
- Android CI reports per-phase status lines that must be read together before
  treating a mobile job as green: `ANDROID_PAYLOAD_RESULT` (run_tests.sh, one
  per configuration with `config=`, `phase=` and the wrapper's exit `status=`),
  `ANDROID_MONITOR_RESULT` (logcat reader/producer exit statuses, the monitor
  deadline, and the payload's `elapsed_seconds`, `headroom_seconds` and
  `headroom_warning`), `ANDROID_CONFIG_RESULT` (test_ci.sh, one line per selected
  configuration, `not-run` when a configuration never reached execution), and
  `ANDROID_BATCH_RESULT` (test_ci.sh batch phase, first failed configuration,
  preserved overall status, and emulator cleanup status). The workflow step
  itself ends with `ANDROID_CI_RESULT` separating `payload_status` from
  `cleanup_status`. A later Release success never clears an earlier Debug
  failure: always inspect every `ANDROID_CONFIG_RESULT` line — both Debug and
  Release — plus the batch line's `status=` field; a missing per-config line or
  `status=not-run` is itself evidence of an incomplete run.
  `android/run_tests.sh` bounds each complete suite with a 180-second monitor
  watchdog, independently of adb command/install/boot deadlines. Debug compiler
  fixtures exceeded the former 60-second budget while still reporting progress
  in PR #687's run `35157195321`; this is a correctness-suite execution budget,
  not a throughput threshold. `BUSTER_ANDROID_TEST_TIMEOUT_SECONDS` explicitly
  overrides it. Terminal markers end monitoring immediately, and timeout,
  malformed or missing markers still fail; the fake monitor suite checks the
  default and override without lengthening its short timeout failure controls.
  Payload interruption statuses 130/143 stop the batch immediately and remain
  the workflow status even when emulator cleanup also fails; unstarted
  configurations remain `not-run`. Ordinary test failures still run the later
  configurations and keep the batch failed. Run `bash android/run_tests_test.sh`
  alongside the frozen shared mobile suite to cover both attribution and
  cancellation through the real workflow body. Android/workflow changes also
  schedule the unchanged frozen-support contract checks automatically.
  Reader status 0 with producer status 124/137 is the payload exhausting its own
  `BUSTER_ANDROID_TEST_TIMEOUT_SECONDS` deadline, not emulator teardown: the
  wrapper names that at the failure, reports how many log lines the payload
  emitted with a truncated last line, and the summary repeats it as a
  `Payload deadline:` note. A payload that passes with less than
  `BUSTER_ANDROID_TEST_HEADROOM_WARNING_PERCENT` (default 25) of its deadline
  left reports `headroom_warning=yes` and a wrapper warning while still passing.
  Treat that as a signal to find the payload regression, not as a reason to
  raise the deadline; `docs/ci-github-actions.md` records the #685 occurrence.
  The lifecycle helper treats an owned terminated zombie as already stopped,
  not as a signalable emulator; the harness holds a child unreaped to cover
  this path deterministically. The numeric PID marker stays compatible with
  existing callers; its `.identity` sidecar records the launch identity before
  the PID marker is published. The Linux Android lane compares boot ID and
  kernel start ticks from `/proc/<pid>/stat`, reading state and identity in one
  snapshot. The portable/macOS fake-tool path uses `LC_ALL=C ps` start time
  (one-second resolution) and state. These are checked at start reuse, wait,
  shutdown and immediately before signals; an identity mismatch is a retired
  owned process, not a new cleanup target. This is a shell snapshot check, not
  an atomic pidfd signal operation; the portable timestamp also cannot resolve
  reuse within the same second. Missing/malformed ownership or an unverifiable
  live PID fails cleanup without targeting that PID, and retains the record
  for retry. Once cleanup proves the owned process stopped it removes both
  records, preventing another invocation from resurrecting the ownership.
  SIGTERM and SIGKILL retain bounded stop verification; sending SIGKILL alone
  is not a failure, but a surviving owned process is. `android/run_tests_test.sh`
  also runs `android/emulator_identity_test.py` for independent identity,
  malformed-record, unavailable-probe and surviving-process controls. Its real
  workflow-body fixture simulates PID reuse after the payload observes the
  original emulator terminal, and requires successful structured cleanup with
  no adb shutdown or signal to the unrelated replacement.

The private OS resource-failure, flood and process-tree child modes dispatch at
the start of `library_tests`, before compiler prewarming and other test modules.
They must not recursively run the suite before triggering the injected failure
or producing their pipe payload or readiness marker. Capture limits, exit and
diagnostic assertions, and process deadlines remain identical for these modes.

The native Wasm-oracle policy children publish readiness after writing their
summary, before the parent starts the existing 500 ms hang deadline. Setup has
its own bounded five-second budget; missing readiness fails and still reaps the
child. `WASM_NODE_PROCESS` retains separate startup and wait timings. Delayed
startup and missing-readiness controls cover the handshake. Actual Node-backed
Wasm oracle deadlines and success requirements are unchanged.

The bit-field aggregate Node oracle (#2194) logs `WASM_NODE_MODULE` with the
exact module size and SHA-256 before each run, and the module bytes as hex
(`WASM_NODE_MODULE_BYTES`, at most 64 KiB) when the oracle fails. After
`WASM_NODE_READY` its script writes a `WASM_NODE_PHASE <name> uptime_us=...`
line after Node provenance (`ready`), the artifact read, module compilation and
instantiation, then the summary and `WASM_NODE_DONE`/`WASM_NODE_EXIT` stamps.
`WASM_NODE_PROCESS` reports the last complete phase as `last_phase`, so a
timeout names the step it interrupted. Phases and stamps are evidence only:
success still requires the summary, a normal zero exit and empty stderr.

## Throughput runner integration

The desktop combination matrix builds and runs `bench_throughput self-test`
before compiler/configuration trees. The tool links shared foundations through
`tools/throughput/shared.c`; its native process tests retain per-child RSS,
timeout/descendant cleanup, argv, diagnostics and dedicated-host locking.
SHA-256 and recoverable file/path contracts also run in the registered hash and
OS module tests. See `tools/throughput/README.md` for the diagnostic build.

## Configured external compiler fixtures

`compiler_driver_object_path_tests` includes the ELF stack boundary fixture
on native Linux x86-64/AArch64. It links Buster C objects (all four allocators,
PIC and non-PIC) into a shared image with the configured host compiler and
requires exactly one RW `PT_GNU_STACK`. Host-assembled empty/X notes then
cross back into Buster: the empty note links and executes with an RW stack;
the X request is refused with the input name and no published image.
Buster assembly and object-reader/writer tests also check empty/X declaration
round trips, malformed allocated/nonempty notes, missing-note policy and
request propagation through merge. External compiler and executable children
use bounded 30-second deadlines. Non-Linux hosts retain the format and
assembly checks without running the Linux host-toolchain boundary.

The registered driver PIC fixture uses `BUSTER_HOST_C_COMPILER_ID`, supplied
from CMake's configured compiler identity, rather than assuming that the host
compiler accepts Clang flags. Clang/AppleClang use `-target`; native GCC does
not. The executable path and optional `BUSTER_HOST_C_COMPILER_ARG1` remain
separate arguments, including `zig` with `cc`. An unknown family fails the
fixture with an explicit diagnostic instead of inheriting Clang's options.
The argument-policy regression runs on every test host; real ELF fixture
compilation, relocation inspection, linking and execution are native Linux
x86-64 checks. The direct-call regression compiles an undefined import and a
module-local function through all four allocator modes, requires PLT32 for the
import and PC32 for the local call, and links/runs each default-model object
with the configured host compiler as a PIE. It also verifies that a direct-call
only function value leaves no separate address relocation. The argument-policy
regression requires `-fPIE` and `-fpie` to select the position-independent model
on every target, with the last positive spelling winning and `-fno-pie`
cancelling only a PIE spelling. The existing fixtures preserve
signed absolute `R_X86_64_32S` and GOTPCREL coverage; the indexed fixture makes
both GCC and Clang produce those forms.
The fixture is compiled `-O2`, because that is where both narrow an address to
32 bits and emit a GOT load with no REX prefix -- the `R_X86_64_GOTPCRELX`
shapes the linker converts to an absolute immediate rather than to an address
computation.
For x86-64 Linux, the native linker removes an undefined
`_GLOBAL_OFFSET_TABLE_` marker only when no relocation or explicit entry request
uses it. It copies the symbol/relocation view before remapping indices, preserving
the input object. Real GOT-base references and ordinary unresolved imports keep
their errors. The registered link tests cover these boundaries and byte-identical
output relative to an object without the unused marker.

`compiler_driver_test_wide_vector_boundaries` exchanges padded-vector calls
with the PATH `clang` at baseline, Haswell and Zen 5. Each row runs only when
the host CPU can execute it. Buster compiles its half of the Zen 5 row for
`znver5`, and Clang compiles its half for `x86-64-v4`, which has the same
64-byte vector ABI. Clang accepts `znver5` only from version 19 (Ubuntu 24.04
ships 18), but it accepts `x86-64-v4` from version 12, so the row needs no
host-compiler gate; see the [layout guide](frontend/layout.md).

The native driver's `compiler_discovery_self_test` runs before every combination
matrix and covers real Clang identity, platform/override selection, and failed
GCC requests preserving existing configurations. The GCC row selects Homebrew
`gcc-15` on macOS; see [build policy](build.md) for `BUSTER_GCC` overrides and
the logged compiler provenance.

For the user-level GCC workflow, with the selected build directory idle:

```sh
./build.sh generate --cc gcc --ci --linker DEFAULT
BUSTER_TEST_JOBS=1 CMAKE_BUILD_PARALLEL_LEVEL=1 ./build.sh build --config Debug -t test_all
```

Repeat with `--cc clang` on an idle tree. `generate` recreates the tree; do not
run the two configurations concurrently in it. The ordinary combination
matrix's GCC row is compile-only, so a green row alone does not certify this
runtime workflow. Run the full registered suite explicitly; unrelated test
failures remain failures and must not be hidden by this fixture repair.

## Native differential matrix

`build.c` exposes `test_differential` (implementation: `tools/differential.c`).
Use a fresh output directory and a built native `ide`; run `--self-test` before
matrix execution. The driver and runner share `codegen_configurations.h` for
allocator/optimization discovery. See [differential-testing.md](../differential-testing.md)
for ABI fixtures, diagnostics, opt-in IR/MIR validation, sanitizer controls,
reduction limits and evidence format. This supplements all existing gates;
it does not replace target-matrix execution or the seeded differential corpus.

Allocator-matrix commands place optimization flags before the explicit allocator
flag because the last allocator-affecting option wins. Assert the parsed allocator
on the invocation passed to execution; retain `-fverify-codegen` and
`-fno-machine-fallback` on applicable native rows in every mode. NONE retains its
parsed spelling but selects MIR-stack, so it has no direct-emitter exception.

## Oracle independence

A differential test is evidence only when its two sides obtain the answer
independently. [The oracle independence map](../oracle-independence.md)
records, per route, the shared dependency and the independent oracle
(Clang-derived layout corpus `record_layout_tests`, specification constants
for debug information, host-compiled references). New golden data needs an
independent producer and a regeneration command; never derive one Buster
path's expectation from another's.

## Source-equivalence campaigns

`ide test` also runs a bounded source-equivalence smoke campaign under all native
allocator modes. `ide metamorphic` exposes the wider native/LLVM/Wasm64/eBPF
matrix, optional engine discovery, explicit unexecuted rows, and grammar-aware
failure reduction. See [metamorphic testing](../metamorphic-testing.md) for the
transformation preconditions, reproducible seeds, strict execution mode and
failure bundles. Cross-target compilation is not a behavioral pass.

## Executed DWARF lifetimes

`tools/debug_info_lifetime_oracle.py` executes a Linux x86-64 DWARF fixture
through GDB with Python support. It checks exact source breakpoints, live
`x`/`y`, three loop/callee transitions, the caller frame, callee lexical scope,
and a correct-value-to-unavailable transition under FAST and QUALITY. The loop
index may be explicitly unavailable before its first certified use; the callee
parameter is required after its use. Arbitrary lookup errors are failures.

On an authorized correctness host, run:

```sh
python3 tools/debug_info_lifetime_oracle.py --ide build/Release/ide --output /tmp/dwarf-lifetime
```

The Clang/GDB reference and wrong-value/missing-debug negative controls are
required. `--baseline-ide PATH --expect-baseline-defect` additionally requires
an executed wrong `x` after correct initial live values at the pinned baseline;
it retains that failure explicitly. `--skip-costs` runs only the small fixture.
The default three-trial cost workload separates object compilation with/without
debug info, DWARF/relocation bytes, native/external linking, symbol loading,
breakpoint resolution and stopped queries. Baseline failure aborts earlier, so
aggregate debugger/query durations contain unequal work. Compare matching live
queries. `.text` hashes must match across debug modes and compiler revisions.

`debug-lifetime-slice.yml` retains the executed oracle on relevant PRs and main
changes on GitHub-hosted Ubuntu. The issue branch additionally runs matched
serial builds and cost diagnostics; no qualified performance hardware is
selected. Qualified performance acceptance remains pending.
The existing static-type oracle is format/consumer coverage without inferior
execution and does not replace these checks. Selector stack-slot aliases,
sibling lexical blocks and optimized constant reconstruction are outside this
bounded vreg-home slice.

## Canonical IR executable oracle

The test-only `ir_oracle_tests` module provides an independent bounded canonical
IR interpreter and isolated native comparison. Run
`build/Release/ide test --ci=1 --verbose=1 --module=ir_oracle_tests`.
See [canonical IR oracle](../canonical-ir-oracle.md) for the admitted subset,
resource bounds, observable results, mutation controls and unavailable native
legs. The private `ir_oracle_native_tests` module is an internal child payload.

## Constant name-binding oracle

`tools/scope_oracle/` is a hand-run, stdlib-only detector for a subset the other
campaigns do not generate: ordinary identifiers shadowed across scopes and used in
member bounds, bit-field widths, `_Alignas`, type names, and enumerator
initializers. Expected values come from an independent C17 scope and psABI layout
model, which Clang and GCC confirm. A mismatch counts only when both reference
compilers reproduce the model. See its [README](../../tools/scope_oracle/README.md)
for the dependency gap, the validation record, and the limits.

## External GPU consumers

`./build.sh test_gpu_toolchains` exposes explicit optional status; each selected
`--profile` is required and fails on missing tools, version drift, compiler or
consumer errors. See [GPU toolchain acceptance](../gpu-toolchain-validation.md).
Its native `--self-test` checks the evidence machinery without vendor tools;
registered GPU planner tests remain fast and deterministic.

## Fixture arena ownership and memory reports

`ide test --verbose=1 --ci=1` emits `TEST_ARENA_V1` records; either verbose
or CI mode enables them. Ordinary quiet tests retain the same fixture lifetimes.
Records have stable module names, static fixture names, and invocation indices.
`arena_slot=0` is the supplied fixture arena; slots 1 and 2 are the selected
thread context's scratch arenas. Parallel modules buffer complete records and
failure text separately, then replay in descriptor order after the lane barrier.

| Field | Meaning (bytes, relative to the arena mapping) |
|---|---|
| `start`, `end` | Cursor at scope entry and before scope cleanup |
| `retained_bytes` | `end - start`, including alignment padding |
| `high_water` | Maximum cursor reached inside this scope, including internal rewinds |
| `peak_bytes` | `high_water - start`; an arena high water, not process RSS |
| `after`, `live_bytes` | Cursor after cleanup and `after - start` |
| `rewind` | Whether this scope reclaims the supplied fixture arena |

`TEST_ARENA_TOP_V1` names each module's largest retaining fixture and largest
fixture peak in its supplied arena. The full records retain scratch peaks
separately. A module's `body` record covers the complete module, including any
assertions not split into named fixtures. Scope peaks overlap and must not be
summed as a process peak. Separate arenas created by a fixture, pooled mappings,
metadata caches, executable mappings, worker contexts, and subprocesses are not
represented by the three observed cursors; measure process RSS independently.
The [#91 audit](../performance-audits/2026-09-12T162525Z.md) records the
representative configuration: Linux x86-64, Clang Release, split translation
units, all table audits enabled, and `BUSTER_TEST_JOBS=2`. Its replay script checks
**640 MiB** maximum RSS for the complete invocation and primary-arena module
peaks of **64 MiB driver / 40 MiB frontend / 80 MiB machine**. These are explicit
reference-profile budgets, not thresholds for sanitized, foreign-platform, or
larger parallel configurations. Keep compiler/configuration, corpus, and host
fixed when comparing. The RSS field is wait4's process-tree high water and is
separate from both live arena bytes and reserved address space.

Use `BUSTER_TEST_FIXTURE(arguments, function)` for a direct fixture function that
returns only `UnitTestResult` counts. It preserves call order, consumes counts,
and rewinds the primary arena after the last assertion and diagnostic consumer.
For inline fixture groups, pair `buster_test_arena_begin` and
`buster_test_arena_end` around the complete set of consumers. Names must be
static, whitespace-free tokens; the ordinal distinguishes repeated invocations.
Scratch cursors are observed only: their existing temporal owners still rewind
them. Do not close a scope before nested scratch lifetimes have closed.

Ownership intentionally spans assertions in the driver's undefined-reference
source/invocation pair and its native-versus-CPU-model vector comparison group.
Static target lists and scalar comparison results may outlive a group; arena
paths, IR, objects, and diagnostic strings may not. The machine module's shared
program/verifier body remains live through its dependent checks. The runner's
work-indexed parallel records lie below module marks, and parallel output has
its own arena. The temporary-root pathname lives in a separate run-owned arena;
compiler-global metadata and persistent lane contexts keep their existing owners.

A fixture that compiles and runs an executable on every loop iteration names it
with `buster_test_temporary_unique_path`, which appends a process-wide serial to
`buster_test_temporary_path`. Windows may refuse to overwrite an image that has
just run (`ERROR_ACCESS_DENIED`; #2089, #2836), so no iteration may rewrite a
path an earlier one launched.

`test_arena_self_test` runs as a fail-closed harness check without changing
registered assertion/module counts. It covers nested and empty scopes, retained
scopes, an internal rewind, decommit, dirty-byte zeroing, quiet mode, and buffered
failure diagnostics surviving a rewind and overwrite. Observation uses the
scoped `Arena.high_water` header field, separate from `dirty_position`, and adds
no allocation-path work; rewinds fold the cursor into it in every build, and
the driver's per-input metrics save and restore it around a unit in the same
way.


Fatal-output regressions in `os_tests` run raw and formatted reporters in
isolated children. A working stream must preserve the full diagnostic; closed
streams, and `/dev/full` on Linux, must still terminate normally with status 1
within the existing deadline. Fatal reporters use recoverable output attempts
so an output failure cannot recursively report itself. These are unsuccessful
process controls, not successful compiler or missing-evidence observations.

## Fixture start records and the hang watchdog

Verbose and CI runs print a start record when each arena scope opens, before
anything the scope itself reports, so the last start line of a hung or crashed
serial run names its fixture (`body` is the module scope):

```text
TEST_FIXTURE_START_V1 kind=fixture module=c_frontend_tests fixture=c_test_enum_runtime index=17
```

This line is a **format example**. Parallel lanes keep their fixture, arena,
failure and timing rows buffered so successful replay remains deterministic.
Verbose and CI runs bracket each non-empty parallel gang with live serial rows:

```text
TEST_PARALLEL_GANG_V1 status=started module_count=3 modules=aarch64_direct_simd_tests,aarch64_complex_simd_tests,aarch64_memory_semantics_tests
TEST_PARALLEL_GANG_V1 status=completed module_count=3 modules=aarch64_direct_simd_tests,aarch64_complex_simd_tests,aarch64_memory_semantics_tests
```

The `started` row is complete before `lane_run` begins. The matching
`completed` row appears only after every lane buffer has replayed in descriptor
order. A crash in any lane therefore leaves a started row without its
completion and names the exact eligible module set, while the buffered payload
between the boundaries is unchanged. The watchdog below still names the exact
lane and innermost scope when execution hangs instead of exiting.

Every registered module also runs under a wall-clock watchdog on desktop
Linux, macOS and Windows. Each scope begin or end publishes the module's
innermost open scope. When one position stays current longer than
`BUSTER_TEST_FIXTURE_TIMEOUT_SECONDS`, the runner prints one record per
expired module and ends the process with status 124:

```text
TEST_FIXTURE_TIMEOUT_V1 kind=fixture module=c_frontend_tests fixture=c_test_enum_runtime index=17 elapsed_ms=1800012 deadline_seconds=1800 last_completed=c_test_volatile_split_bit_fields
```

This is also a format example; the index and elapsed time are illustrative.
`elapsed_ms` counts from when that scope last became innermost: its begin or
the end of its most recent nested scope. `last_completed` names the module's
most recently closed scope, which localizes a hang in a `body` between
fixtures. The deadline therefore bounds the longest stretch between scope transitions, not a
whole module. The default is 1800 seconds. The slowest recorded module,
`compiler_driver_tests` under sanitized Clang Debug on hosted Windows x86-64,
took 414 seconds across 39 scopes (see
[driver test timing](../driver-test-timing.md)). The default is still far below
the 300-minute desktop job timeout that such a hang used to exhaust.
Set a whole number of seconds to override it, or `0` to disable it. An invalid
value keeps the default and fails the run with a
`TEST_FIXTURE_WATCHDOG_V1 status=invalid-deadline` record. Verbose and CI runs
state the deadline in force with `TEST_FIXTURE_WATCHDOG_V1 status=...`:

| Status | Meaning |
|---|---|
| `armed` | The watchdog thread is running |
| `disabled` | The deadline is `0` |
| `debugger` | A debugger was attached at startup, and a breakpoint would look like a hang |
| `unsupported` | Android, iOS or `BUSTER_SINGLE_THREADED` builds; the mobile launchers keep their own deadlines |
| `thread-failed` | The thread could not start; the run fails |

The watchdog thread is created with `ThreadCreateOptions.untracked`. It reads
only its slots, and `os_is_only_live_thread()` must stay true for modules
that check serial initialization. On expiry it terminates the process without
running exit handlers, because the stuck thread may hold any lock.
`test_fixture_watchdog_self_test` covers deadline parsing, start records,
nested transitions, expiry and the record text without a thread or a sleep.
`os_tests` also runs a real `ide test` child through the private
`BUSTER_TEST_WATCHDOG_CHILD_MODE` payload. That payload spins in one fixture
under a one-second override. The child must exit with status 124, print the
timeout record after the fixture's start record, write no stderr, and take at
least the deadline. This check adds about one second to `os_tests`.

`test_parallel_gang_report_self_test` verifies the exact boundary text, module
order, quiet-mode suppression and an unchanged replay payload without adding
registered assertions. Desktop `os_tests` also launches a selected parallel
module through the private `BUSTER_TEST_PARALLEL_CRASH_CHILD_MODE` seam. The
child opens its buffered module scope and exits with status 1. The parent
requires the live started row, forbids the completed row, and checks the fatal
stderr message. This harness regression changes neither assertion totals nor
`TEST_MODULE_TIMING` rows.

## AArch64 ELF direct memory references

`compiler_driver_test_aarch64_elf_ldst` runs on desktop Linux AArch64 with
the configured host compiler. Its host-only input is
`src/buster/tests/compiler/driver/fixtures/aarch64_elf_ldst.c`, outside the
frozen `tests/*.c` native-retirement census. The default fixture is the
volatile-int store/load from GitHub #2077; `AARCH64_LDST_SCALE_FAMILY=1`
checks byte, halfword, word, doubleword and 128-bit SIMD stores/loads with
nonzero addends past one page. Each input is compiled at `-O2 -fno-pie`,
with GNU's section anchors disabled to keep independent symbol references,
linked and executed independently with the host toolchain, then imported,
linked and executed through Buster. The test requires the expected scaled
relocation kinds, loads and stores, and reports `AARCH64_ELF_LDST_NATIVE_V1`
only after both executions succeed. Compiler/linker children and executions
have bounded 30-second deadlines. `object_tests` covers REL/RELA, instruction
classes, scale mismatches and malformed sites; `link_tests` derives patched
addresses from static/dynamic section tables and imported-data copy slots.

## Wasm canonical index signedness

`compiler_driver_test_wasm_index_signedness` emits a direct canonical module for
each pointer width before any C control. Thirty-two exported address functions
cover signed/unsigned 8/16/32/64-bit INDEX operands, pointer/array bases and
one/four-byte strides. Two additional signed-i32 LOAD exports exercise -2 through
2 from an interior address within one five-element linear-memory region. Every
row passes the shared commit and canonical validation gates before emission.

Wasm emission normalizes each INDEX operand to its declared integer width and
signedness before converting to the address width. Memory64 uses signed extension
for signed i32 carriers and unsigned extension for unsigned carriers; Wasm32
retains i64-to-i32 wrapping before the unchanged element-stride arithmetic.

Each original direct module receives 534 independent engine comparisons:
512 fixed carrier samples, 12 separately pinned address literals and ten LOAD
results. BigInt signed/unsigned interpretation and address-width arithmetic
provide the oracle; it does not encode Wasm instructions or call the emitter.
Dirty carrier bits and unsigned high-bit values remain mathematical address
returns with no memory access. The LOAD oracle initializes and checks the
five-element region independently.

A small C source uses one static five-element array. Its pointer starts at
element two, so -2/-1/0/1/2 accesses remain inside that array; an array-subscript
neighbor is a positive control. Both frontend forms and both pointer widths
receive ten engine comparisons each. The fixture logs actual canonical INDEX
operand widths and signedness as `WASM_C_INDEX`/`WASM_C_INDEX_ROW`, including each
function's nonconstant operand definition; a frontend-prewidened i64 operand
does not establish coverage of signed i32 or narrow direct canonical operands.

The six original compiler modules retain repeated-emission equality, SHA-256
identity, file readbacks before/after the engine, and the existing 30/60-second
Node deadlines. Success requires normal zero exit, empty stderr and the exact
fixed terminal marker. The source and oracle are embedded in the registered
fixture; frozen support inputs and oracle/shim files remain unchanged.

## Wasm function-address capability

`compiler_driver_test_wasm_function_addresses` constructs and validates canonical
IR before emission for both pointer widths. Four topologies cover first-index
imports/definitions and later definitions. Six escape paths use pointer FUNCTION,
place FUNCTION/address-of, void-pointer/integer round trips, volatile storage,
returned pointers and address/dereference aliases. Wasm32 must refuse these
runtime addresses with instruction attribution and empty artifact aliases;
Wasm64 keeps its nonzero handles. Direct CALL and unused inert references are
accepted controls. Independent Node checks use the original module SHA-256,
repeated emission equality and file readbacks, with the existing deadline and
exact terminal marker. Baseline modules that are incorrectly admitted are still
executed, so null collisions are observable rather than hidden by a refusal check.

`compiler_driver_test_wasm_function_address_outputs` checks both frontend forms
through the actual Wasm32 driver: alias, storage/return, indirect-call and static
relocation refusals preserve absent/existing output destinations. Both direct
Wasm targets retain successful C direct-call controls. Inline source/oracle bytes
leave the frozen support inventory and startup shims unchanged.

## Wasm object-address alignment

`compiler_driver_test_wasm_stack_alignment` lowers both C frontend forms for
wasm32 and Memory64, validates canonical IR and compares repeated module bytes.
An opaque Node import observes actual linear-memory addresses and checks ordinary
eight-byte objects, extended 64-byte objects, mixed locals and aggregate copies.
Odd runtime allocation sizes stay live across nested calls; the next allocation
must start at the independently rounded original caller end.

Direct canonical controls raise only each LOCAL place's alignment above its
unchanged byte-aligned array type. A deliberately non-64-aligned stack base
exercises exact-limit success, padding-plus-frame exhaustion and base-padding
exhaustion. The same instance must recover its exact entry pointer after every
normal return and deliberate trap. Each pointer-width/frontend run completes
84 engine checks, records the consumed module SHA-256 and checks the artifact
against the original returned bytes after execution. The source and oracle are
generated inline, leaving the frozen support inventory unchanged. Existing
stack-reservation and memory-hint regressions remain required.

## Direct canonical Wasm bit counts

`compiler_driver_test_wasm_bit_counts` constructs CLZ, CTZ and population-count
functions directly in canonical IR, so C integer promotions cannot hide a
narrow backend defect. The block-row protocol commits each argument, unary
operation and return; canonical preparation validates the module before each
pointer-width emitter run. Signed and unsigned widths 7, 8, 16, 24, 32, 33, 48
and 64 separate semantic width from the i32/i64 carrier.

An inline Node oracle checks twelve literal expectations, exhausts both 7- and
8-bit bit patterns, and checks zero, all ones, sign boundaries, alternating
patterns, every single bit and its complement at larger widths. Dirty carrier
bits separately check normalization. Each pointer-size run makes 5,352 actual
export calls; zero CLZ/CTZ results use the canonical width convention, and
32/64-bit controls retain full-carrier behavior. The oracle loops over the
semantic bits instead of calling a host count intrinsic.

Repeated emission must be byte-identical. The consumed module SHA-256 and
before/after artifact comparisons prove that Node receives the original bytes.
Normal zero exit, empty stderr and the exact terminal summary are required
through the existing bounded Node runner. Missing Node is reported as an
execution skip, not an engine pass. The script is inline; frozen Wasm oracles,
startup shims and support inventory stay untouched.

## Node-backed Wasm oracle deadlines

`codegen_test_ebpf_argument_images` checks equality, ordering, unsigned
division/remainder, right shifts and widening from signed/unsigned 8/16/32-bit
arguments, Boolean arguments and full-width controls in both frontend forms.
Clean and dirty incoming register images must give the same value at the
declared width. The eBPF prologue normalizes a private R0 copy before storing
each argument; input registers remain available for later captures. Boolean
capture first discards bits outside its one-bit canonical representation.
The existing VM executes every case; the kernel verifier/JIT is compared when
available and its participation is reported separately.

`compiler_driver_test_wasm_integers` runs the frozen integer oracle and the
additive `tools/wasm_unsigned_div_rem_execution.js` companion on the same freshly
emitted artifact. It compares the file with the compiler's returned bytes before
and after execution; the companion also records the consumed SHA-256. Four
parameterized unsigned div/rem calls use UINT32_MAX/UINT64_MAX divided by two,
with independent literal expectations. A hand-emitted unsigned baseline and four
single-opcode signedness controls require the corresponding equality mismatch;
validation failures, missing exports and traps do not count as semantic rejection.
These controls establish the checker boundary, not production mutation coverage.
The companion lives beside the startup shim in `tools/`, outside the frozen
`tests/` input inventory. Both frozen oracle scripts and the reviewed support
inventory remain unchanged.

The compiler-driver Node oracles use a bounded 30-second deadline on Linux and macOS and a bounded 60-second deadline on Windows. The Windows allowance covers measured hosted-runner startup and execution variance without changing the process-deadline primitive or other platforms.

The first Node launch in a job pages the Node executable in from disk; every later launch starts warm. On hosted Linux AArch64 that cold page-in has taken between 0.1 s and 2.1 s in passing jobs. In one incident it stalled for about a minute at near-zero CPU (#2194). The incident looked like this:

- The bit-field oracle's first attempt timed out silently.
- Its retry printed `WASM_NODE_READY` with under a second of budget left.
- Together, the two attempts paged in about one normal cold start (roughly 76,500 blocks).

`compiler_driver_test_wasm_node_cold_start` therefore runs immediately before the first real oracle in module order. It starts Node once, compiles and instantiates an empty Wasm module, synchronously writes `WASM_NODE_COLD_START_DONE` and exits.

- It has its own bounded 120-second budget and logs a `WASM_NODE_COLD_START` line with its elapsed time.
- It fails on a timeout, a nonzero exit, any stderr output or a missing marker.
- Each oracle's deadline then measures a warm start plus the oracle's own work.

This fixture accounts for a cold start the runner was charging to an oracle. It does not relax any oracle's deadline, retry or success rule. A test or module selection that skips the fixture gets the previous behavior.

Oracle output is evidence, not completion. A run passes only after the child exits normally with status zero, leaves stderr empty, and ends stdout with the oracle's exact terminal summary marker. The integer oracle's startup shim in `tools/` writes `WASM_NODE_READY startup_ms=<timestamp>` synchronously before loading the frozen semantic oracle, and a successful run must contain that first-line marker. The harness logs it with both attempts when applicable. Only a timeout with no observed stdout or stderr before this marker, successful process-tree cleanup, and no capture failure retries once in a fresh Node process. A second failure remains a failure. A hang after readiness, partial output, nonzero exit, launch failure, and a process that prints the terminal marker but remains alive all fail without retry. The latter is reported as `summary-before-timeout`. `compiler_driver_test_wasm_node_policy` exercises each boundary with native child controls.

The startup shim also stamps the rest of the integer run, so a post-summary timeout (#2066) can be located. It writes the frozen oracle's console output synchronously, restoring `console.log` even when loading or checking throws. Only after the oracle returns successfully does it synchronously write `WASM_NODE_DONE uptime_us=<n> resources=<active Node resources>` and explicitly exit zero. This prevents the observed post-summary `PipeWrap` event-loop stall (#1793/#2066) without dropping buffered success output. From Node's `exit` event it writes `WASM_NODE_EXIT uptime_us=<n>` synchronously. For readiness oracles the harness strips well-formed trailing stamps before the terminal-marker check. It logs `node_done`, `done_uptime_us`, `node_exit`, `exit_uptime_us` and `post_done_us` (elapsed harness time minus the DONE uptime, an upper bound on the time spent after the oracle returned). A timed-out run with the summary becomes `summary-then-teardown-stall` when EXIT was written, `summary-then-event-loop-stall` when only DONE was written, and stays `summary-before-timeout` otherwise. All three still fail without retry. Stamps are evidence only: they never replace the summary, a zero exit or empty stderr, and a malformed stamp fails the terminal-marker check.

The Wasm oracle process-policy controls launch a native `ide test` child before
compiler prewarming. Their short deadlines exercise completion, output, errors
and hangs without depending on Node startup latency; the actual Wasm execution
fixtures still use Node and its platform-specific 30/60-second deadline.

## Win64 padded-vector execution

The inline padded-vector fixture runs natively on Windows x86-64 in all four
allocator modes for supported baseline/Haswell/Zen 5 models. On Linux with
Wine, its Clang/Buster halves also run in both directions. The freestanding
Clang consumer supplies its own `memset` for aggregate initialization; that
helper is enabled only for this mixed-object build. Link failures retain the
symbol diagnostic, and runtime failures retain the process status and captured
output. Exit 1 identifies the first U8x3 value check, not an ISA probe.

## Retirement adapter test checkouts

The immutable statistics-adapter tests use private checkouts of the exact
current commit. Native-retirement CI reconstructs generated bindings in its
working tree before running these tests; that expected generated drift must
not become the fixture for a control that requires clean committed source.
Tracked-drift, source-identity, and checkout-race controls still exercise the
production validator against those private checkouts.

`node tools/wasm_integer_execution_startup_test.js` checks the real frozen oracle against an independently emitted Wasm fixture, buffered-output and live-resource controls, arithmetic/load/output failures, and an implicit-exit mutation that must time out. The focused hosted workflow runs these controls on Linux and Windows; the full driver policy still rejects deliberate hangs, nonzero exits, stderr and missing summaries.

## Binary-coverage inventory controls

The native build driver's `binary_coverage_inventory --self-test` runs in the
existing Release/combinations preflight. Its hand-authored ELF fixture has
independent expected identity/range values; malformed/truncated/overflowed and
unsupported ELF inputs fail rather than producing a partial denominator.
Regenerated-report comparisons reject omitted artifact/range, wrong-build hash,
fabricated instruction/edge counts, missing MC/DC pair/completeness claims, and
incomplete-collection claims. These are report-gate controls, not trace collector
validation or test sensitivity evidence for the inventoried binary.

Linux x86-64 also inventories the actual running native driver from
`/proc/self/exe` into its ordinary combination log. Every executable byte stays
unclassified, and instruction execution, machine edges, MC/DC and functional
assertions each stay unmeasured. No percentages or approved tracing capability
are inferred from a successful preflight. See
[the exact-artifact pilot command and gaps](build.md#exact-artifact-binary-coverage-pilot).
