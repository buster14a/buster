<!-- buster-audit-2026-09-07:release-check-macro-is-unreachable -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below were re-derived for that commit and each cited site still reads as described, but the reproduction itself was not re-run there.

**Priority:** P2 - architectural. No single demonstrated failure, but it is the shared root of four P1 findings in this audit.
**Evidence:** read directly in `src/buster/lib/os.h`; the downstream consequences are reproduced in the four issues listed below.

## What is wrong

```c
#if BUSTER_OPTIMIZE
#define BUSTER_CHECK(ok) ((void)(BUSTER_UNLIKELY(!(ok)) ? (BUSTER_UNREACHABLE(), 0) : 0))
#else
#define BUSTER_CHECK(ok) ((void)(BUSTER_UNLIKELY(!(ok)) ? (os_fail_message(S8("assertion failed")), 0) : 0))
#endif
```

That is the correct semantic for an **invariant the code has already established** - it lets the optimizer use the fact, and the Debug build proves the fact holds.

It is the wrong semantic wherever `BUSTER_CHECK` is currently guarding **data the program does not control**. There the Release build does not merely skip the check: it grants the optimizer permission to assume the bad case cannot occur, which can delete the surrounding handling entirely. A bounds check that is `__builtin_unreachable()` in the shipping configuration is not a bounds check.

Known sites where `BUSTER_CHECK` is doing validation duty rather than asserting an established invariant:

- `c_parse.c:11720` - the identifier-use capacity bound (see `c-parse-identifier-use-overflow`), where the Release build performs the out-of-bounds write the Debug build catches.
- `os.c:1280` - `BUSTER_CHECK(result > 0)` on a `write(2)` return (see `os-write-error-counted-as-success`), where Release turns -1 into `UINT64_MAX`.
- `arena.h:129` / `arena.c:45` - the arena position and commit bounds (see `arena-commit-failure-and-granularity`).
- The capacity pre-checks in the object readers (`object.c:4254`, `:4258`, `:4474`, `:4481`, `:5108`, `:5115` and peers), where the following `arena_allocate` runs unconditionally after the check has already set `read_ok = false`. Not currently reachable - the driver's object arena is 32 GB and every subsequent *write* is `read_ok`-guarded - but the pattern is one arena-size change away from mattering.

**Source locations**

- src/buster/lib/os.h:288-292 (`BUSTER_CHECK`)

## Reproduction and measured evidence

No standalone reproduction: the macro is the mechanism, not the failure. The failures are in the four issues named above, each of which is contained in Debug and unsafe in Release.

The asymmetry is visible directly in this audit's results: `c-bool-aggregate-initializer` and `c-unsigned-to-float-constant-fold` **abort the Debug compiler** on valid C, while Release silently miscompiles it. The `#if !BUSTER_OPTIMIZE` differential gates are doing real work - they caught both defects - but they gate aborts, not corrections, so the shipped configuration has no defence at all.

## Required fix

Separate the two uses at the macro level rather than site by site. Keep `BUSTER_CHECK` for established invariants with its current Release semantics, and introduce a second macro - a validating check that retains its test in every configuration and fails loudly - for any condition derived from input, file contents, sizes, syscall returns, or counts the program did not itself compute and prove.

Then sweep the existing `BUSTER_CHECK` uses and reclassify. The sweep is the work; the macro is the easy part.

## Validation / definition of done

Each reclassified site needs a test that exercises the failing condition in a **Release** build and asserts the check fires. A test that only passes in Debug is testing the wrong configuration - that is precisely how the four P1 findings survived.

Consider also building one CI configuration with `BUSTER_OPTIMIZE` set and the checks retained, so the optimized code paths are exercised with the checks live.

## Existing work / scope

Section S3 of the 2026-09-07 audit, and the structural observation at the end of `AUDIT_REPORT.md`. Fixing the four downstream issues individually is correct and should not wait for this; this issue is about preventing the fifth.
