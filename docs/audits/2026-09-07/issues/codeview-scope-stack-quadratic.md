<!-- buster-audit-2026-09-07:codeview-scope-stack-quadratic -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below were re-derived for that commit and each cited site still reads as described, but the reproduction itself was not re-run there.

**Priority:** P2 - demonstrated arena exhaustion at 70k functions; quadratic in time as well.
**Evidence:** found by a subsystem pass and traced in source; the reproduction below was run there and not independently re-executed for this bundle. Confirm it before starting the fix.

## What is wrong

`codeview_emit_scope_tree` allocates its frame stack with `arena_allocate(arena, CodeviewScopeFrame, model->scope_count + 1)` on **every call**, and the caller at `codeview.c:711` calls it once per function. Nothing releases the previous allocation, so the cost is O(function_count x scope_count) of arena. The inner scan at `:376-384` is O(scope_count) per frame, giving the same quadratic in time.

The DWARF path allocates its stack once, at `dwarf.c:1531` - that is the shape this should follow.

**Source locations**

- src/buster/lib/compiler/codeview/codeview.c:369 (`codeview_emit_scope_tree`, the per-call allocation)
- src/buster/lib/compiler/codeview/codeview.c:711 (`codeview_build_legacy`, the per-function call)

## Reproduction and measured evidence

70001 functions compiled `--target=x86_64-pc-windows-msvc -g -c`: 70001 x 560024 bytes exhausts the 32 GB translation-unit arena and aborts at `arena.h:122`.

Backtrace: `arena_allocate_bytes <- codeview_emit_scope_tree:369 <- codeview_build_legacy:711`.

In an optimized build `BUSTER_CHECK` is `BUSTER_UNREACHABLE()`, so instead of aborting it returns a pointer past the reservation and writes there. The same input on the DWARF path succeeds.

## Required fix

Allocate the frame stack once per `codeview_build_legacy` call and pass it in, as `DwarfModelWriter::scope_stack` already does. While there, consider whether the O(scope_count) inner scan can consume a prepared index instead.

## Validation / definition of done

First confirm the reproduction; this comes from a subsystem pass. Then add a large-input debug-info test - a generated TU with many functions - and assert bounded arena growth, not just successful completion. A peak-arena assertion is what makes this a regression test rather than a one-time fix.

## Existing work / scope

Section S4 of the 2026-09-07 audit. Windows targets only. See `release-check-macro-is-unreachable` for why the Release behaviour is worse than the Debug abort.
