# Tests and CI

[Agent instructions](../../AGENTS.md) · Paths and commands below are relative to the repository root.

## Tests

- All tests run inside the `ide` executable; there is no external unit-test
  framework. From the repository root, run `ide test --verbose=1 --ci=1` or
  build the `test_all` target.
- The bootstrap wrappers have a controlled platform test at
  `python3 tests/bootstrap_wrapper_test.py -v`. It supplies a fake TCC and
  driver, and covers cold/warm reuse, dependency and compiler invalidation,
  corrupt/incomplete entries, failure propagation, argument forwarding and
  concurrent immutable publication. CI runs it on every desktop OS/architecture
  lane before installing optional tools.
- Test modules live under `src/buster/tests/` as mirrored `*_test.c` and
  `*_test.h` pairs. `src/buster/tests/test.c` owns registration. Unity builds
  include implementations into the main translation unit; non-unity builds
  compile each test source independently.
- C frontend and driver fixtures live under `tests/` and use `.c`, `.h`, native
  object, archive, and shell-script inputs. Keep fixture paths relative to the
  repository root because tests intentionally exercise the real file loader.
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
- Keep test-only declarations behind `BUSTER_INCLUDE_TESTS`. Private structures
  shared with tests belong in a narrow `*_internal.h` seam rather than being
  exposed through a production public header.
- CI is defined under `.forgejo/`; Forgejo remains the source of truth. The
  opt-in GitHub-hosted desktop capacity uses the source-free broker template in
  `.forgejo/github-bridge/`, not a repository mirror. `.github/workflows/ci.yml`
  runs the same coverage as the Forgejo matrix — combination matrix, execution-mode
  matrix, Android and iOS — on GitHub's standard runners for the migration
  described in `docs/ci-github-actions.md`. Its six desktop lanes cover every
  desktop OS at both x86-64 and AArch64; three independent mobile shards retain
  the Android and iOS suites without repeating desktop work. The independent
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
  a 30-second total metadata budget. A later exact snapshot may recover a
  transient omission; a persistent empty, stale or ambiguous record fails
  closed. It never borrows step proof from an older attempt when a newer attempt
  shadows that job. Run a fresh full CI attempt when required metadata remains
  unresolved; a green job-level conclusion alone is not execution evidence.
  Both workflows cover the same PR merge revision, main/tag pushes, merge groups
  and explicit dispatches without duplicate feature-push runs. Buster CI keeps
  full matrix diagnostics for pull requests, main/tag pushes and manual runs.
  On `merge_group`, desktop and mobile enable matrix fail-fast; the native
  matrix retains `fail-fast: false` under the frozen CI test contract. The trusted
  controller cancels exact-head merge-group runs after a failed Buster CI job
  or required check from another workflow.
  See `docs/ci-workflow-audit.md` for cache trust boundaries, diagnostics,
  cancellation, coverage details, and reproduction. Every job stays inert
  until its repository variable is set, and skips itself outright on Forgejo.
  For cancelled current-PR validation, see [bounded CI recovery](../ci-cancellation-recovery.md)
  and its offline checks: `python3 tests/ci_recovery_test.py` and
  `python3 .github/scripts/test_merge_queue_fail_fast.py`.
  Changing a `runs-on` label means changing `.github/actionlint.yaml` too,
  because actionlint knows only the labels its own release predates. Preserve
  Debug/Release, unity/non-unity, sanitizer/fuzz, self-host, and
  supported-platform coverage when changing build orchestration or the
  compiler pipeline. The GitHub workflow keeps the six existing platform check
  names, runs full PR/main/tag/merge-group coverage without duplicate feature
  push runs, revalidates exact-key Zig archive caches, and treats UBSan reports
  as failures. Independent later suites run after earlier test failures;
  captured logs and fail-closed summaries remain outside generated build trees.
  See `docs/ci-github-actions.md` for timing cohorts and exact reproductions. Do not add source mirroring, Actions artifacts/caches,
  durable GitHub-side credentials, verbose broker logs, or untrusted-PR
  triggers to the broker; see
  `docs/ci-github-hosted-runners.md`.

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
  this path deterministically. A process-state query that loses the PID after
  an initial `kill -0` is reconciled with one more liveness probe: confirmed
  disappearance is stopped while persistent ambiguity remains fail-closed.
  Once teardown observes a terminal state it is monotonic for that ownership
  check and is not immediately re-probed. SIGTERM and SIGKILL are followed by
  bounded stop verification; sending SIGKILL alone is not a failure, but an
  owned process that remains live after it is.

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

## Throughput runner integration

The desktop combination matrix builds and runs `bench_throughput self-test`
before compiler/configuration trees. The tool links shared foundations through
`tools/throughput/shared.c`; its native process tests retain per-child RSS,
timeout/descendant cleanup, argv, diagnostics and dedicated-host locking.
SHA-256 and recoverable file/path contracts also run in the registered hash and
OS module tests. See `tools/throughput/README.md` for the diagnostic build.

## Bench service self-test

`./build.sh bench_service self-test` (and its `--sanitize` variant) runs the
POSIX queue, materializer, journal-replay and fake-worker regressions plus the
Linux lease-handoff and result-evidence suites; see
`tools/bench_service/README.md` for the full contract. Interrupted workers
retain and hash existing result evidence into the published `BQ-BUNDLE-V1`
index, a bundle-only crash prefix completes idempotently, and invalid
published controls are never repaired. The coordinator removes the
`.lease-handoff` socket before the worker is continued. On Linux the suite
also runs a materializer-to-recipe bridge: a real `bq_materialize` fixture
feeds the real `bench_service_recipe` build graph through
`bench_service_recipe_self_test JOB TOKEN WORKSPACE BASE CANDIDATE RESULT`,
with only the external build and throughput programs stubbed, followed by the
fixed no-argument recipe suite. These tests are fake-backend and
stubbed-external evidence; privileged live-systemd and deployment
qualification remain explicit operator gates and are not covered here.

## Configured external compiler fixtures

The registered driver PIC fixture uses `BUSTER_HOST_C_COMPILER_ID`, supplied
from CMake's configured compiler identity, rather than assuming that the host
compiler accepts Clang flags. Clang/AppleClang use `-target`; native GCC does
not. The executable path and optional `BUSTER_HOST_C_COMPILER_ARG1` remain
separate arguments, including `zig` with `cc`. An unknown family fails the
fixture with an explicit diagnostic instead of inheriting Clang's options.
The argument-policy regression runs on every test host; real ELF fixture
compilation, relocation inspection, linking and execution are native Linux
x86-64 checks. They preserve signed absolute `R_X86_64_32S` and GOTPCREL
coverage; the indexed fixture makes both GCC and Clang produce those forms.
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

## Source-equivalence campaigns

`ide test` also runs a bounded source-equivalence smoke campaign under all native
allocator modes. `ide metamorphic` exposes the wider native/LLVM/Wasm64/eBPF
matrix, optional engine discovery, explicit unexecuted rows, and grammar-aware
failure reduction. See [metamorphic testing](../metamorphic-testing.md) for the
transformation preconditions, reproducible seeds, strict execution mode and
failure bundles. Cross-target compilation is not a behavioral pass.

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

`test_arena_self_test` runs as a fail-closed harness check without changing
registered assertion/module counts. It covers nested and empty scopes, retained
scopes, an internal rewind, decommit, dirty-byte zeroing, quiet mode, and buffered
failure diagnostics surviving a rewind and overwrite. Observation uses separate
arena header storage in test-enabled builds and adds no allocation-path work.


Fatal-output regressions in `os_tests` run raw and formatted reporters in
isolated children. A working stream must preserve the full diagnostic; closed
streams, and `/dev/full` on Linux, must still terminate normally with status 1
within the existing deadline. Fatal reporters use recoverable output attempts
so an output failure cannot recursively report itself. These are unsuccessful
process controls, not successful compiler or missing-evidence observations.

## Node-backed Wasm oracle deadlines

The compiler-driver Node oracles use a bounded 30-second deadline on Linux and macOS and a bounded 60-second deadline on Windows. The Windows allowance covers measured hosted-runner startup and execution variance without changing the process-deadline primitive or other platforms.

Oracle output is evidence, not completion. A run passes only after the child exits normally with status zero, leaves stderr empty, and ends stdout with the oracle's exact terminal summary marker. A process that prints the marker and remains alive is killed at the deadline and fails as `summary-before-timeout`. `compiler_driver_test_wasm_node_policy` also locks down launch failure, nonzero exit, incomplete output, stderr output, a true hang, and summary-then-hang behavior.

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
