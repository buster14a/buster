<!-- buster-audit-2026-09-07:codeview-fieldlist-length-overflow -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below were re-derived for that commit and each cited site still reads as described, but the reproduction itself was not re-run there.

**Priority:** P1 - demonstrated corrupt type stream; every later record is misparsed.
**Evidence:** found by a subsystem pass and traced in source; the reproduction below was run there and not independently re-executed for this bundle. Confirm it before starting the fix.

## What is wrong

`codeview_type_record_end` writes the record length as `(u16)(count - offset - 2)` with no overflow check, and `LF_FIELDLIST` (`codeview.c:541-565`) accumulates every struct or union member and every enumerator into a single record with no `LF_INDEX` continuation split.

CodeView requires field lists to be split into continuation records before they exceed the 16-bit length field. Without the split, the length silently wraps.

**Source locations**

- src/buster/lib/compiler/codeview/codeview.c:238 (`codeview_type_record_end`)
- src/buster/lib/compiler/codeview/codeview.c:541-565 (`LF_FIELDLIST` accumulation)

## Reproduction and measured evidence

A struct with 2500 `int` members, compiled `--target=x86_64-pc-windows-msvc -g -c`, produces an `LF_FIELDLIST` whose real payload is about 135 KB but whose length field reads 3930 (135010 mod 65536).

```
llvm-readobj --codeview-merged-types
  The CodeView record is corrupted
```

Every type record after it is then parsed out of the member-name text. 2400 members is fine; 2500 breaks.

## Required fix

Split a field list at approximately 64 KB into continuation records chained with `LF_INDEX`, and fail the build with a diagnostic rather than truncating if any single record still exceeds `UINT16_MAX`. A silent wrap is the worst available outcome.

## Validation / definition of done

First confirm the reproduction; this comes from a subsystem pass. Then add a debug-info test with a struct and an enum each large enough to require at least two continuations, validated with `llvm-readobj --codeview-merged-types` or `llvm-pdbutil`. Include the boundary case that lands exactly on the split threshold.

## Existing work / scope

Section S4 of the 2026-09-07 audit. Windows targets only.
