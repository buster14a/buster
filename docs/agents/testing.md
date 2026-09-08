# Tests and CI

[Agent instructions](../../AGENTS.md) · Paths and commands below are relative to the repository root.

## Tests

- All tests run inside the `ide` executable; there is no external unit-test
  framework. From the repository root, run `ide test --verbose=1 --ci=1` or
  build the `test_all` target.
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
  the Android and iOS suites without repeating desktop work. Require the
  aggregate `CI complete` result, not just the desktop names. Both matrices
  disable fail-fast, and a combination failure does not hide Unix mode tests.
  See `docs/ci-workflow-audit.md` for cache trust boundaries, diagnostics,
  cancellation, coverage details, and reproduction. Every job stays inert
  until its repository variable is set, and skips itself outright on Forgejo.
  For cancelled current-PR validation, see [bounded CI recovery](../ci-cancellation-recovery.md)
  and its offline checks: `python3 tests/ci_recovery_test.py`.
  Changing a `runs-on` label means changing `.github/actionlint.yaml` too,
  because actionlint knows only the labels its own release predates. Preserve
  Debug/Release, unity/non-unity, sanitizer/fuzz, self-host, and
  supported-platform coverage when changing build orchestration or the
  compiler pipeline. Do not add source mirroring, Actions artifacts/caches,
  durable GitHub-side credentials, verbose broker logs, or untrusted-PR
  triggers to the broker; see
  `docs/ci-github-hosted-runners.md`.

- Test implementations are not registered as modules; add new test pairs under
  `src/buster/tests/`, add
  the implementation to `BUSTER_TEST_SOURCES` and the header to
  `BUSTER_TEST_HEADERS` in `CMakeLists.txt`, and include both from
  `src/buster/tests/test.c` in the existing registration order (the
  implementation only in the `BUSTER_UNITY_BUILD` block).
- **Adding a module** (`foo.c`/`foo.h` under `src/buster/lib/`) takes three
  edits: (1) `buster_register_module(foo ...)` in `CMakeLists.txt`;
  (2) add `foo` to the `MODULES` list of `buster_add_executable(ide ...)`;
  (3) add `#include <buster/lib/foo.c>` to the `BUSTER_UNITY_BUILD` block at
  the top of `src/buster/apps/ide/ide.c` — optimized non-sanitized configs compile as
  unity builds, so forgetting this breaks ordinary Release builds.
- Headers are included as `<buster/lib/...>` or `<buster/tests/...>` (include
  root is `src/`).
  `compile_commands.json` is exported to `build/` by default.
