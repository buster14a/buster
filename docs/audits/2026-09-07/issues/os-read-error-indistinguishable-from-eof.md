<!-- buster-audit-2026-09-07:os-read-error-indistinguishable-from-eof -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below were re-derived for that commit and each cited site still reads as described, but the reproduction itself was not re-run there.

**Priority:** P2 - requires an induced I/O error; consequence is a silently truncated source or object.
**Evidence:** found by a subsystem pass and traced in source; the reproduction below was run there and not independently re-executed for this bundle. Confirm it before starting the fix.

## What is wrong

`os_file_read_partially` converts a read failure to a return of `0` after printing a message. `os_file_read` treats `0` as EOF and breaks out of its loop, so a mid-file failure is indistinguishable from a short file. The caller has no return value that expresses the difference.

**Source locations**

- src/buster/lib/os.c:1330-1349 (`os_file_read_partially` and `os_file_read`)

## Reproduction and measured evidence

A 4096-byte file whose `read(2)` succeeds for 16 bytes and then returns `EIO`: `file_read` returns a 16-byte `ByteSlice`. A truncated source or object file is then consumed as if complete - the compiler parses 16 bytes of a 4 KB file and proceeds.

## Required fix

Distinguish error from EOF out of `os_file_read` - return `UINT64_MAX` or a status - and have `file_read` return an empty slice plus a failure indication on error. This is the read-side counterpart of `os-write-error-counted-as-success`; fix both together so the file layer has a consistent error contract.

## Validation / definition of done

First confirm the reproduction; this comes from a subsystem pass. Then add a test with an interposed `read` that fails mid-file and assert the caller observes failure rather than a short buffer.

## Existing work / scope

Section S3 of the 2026-09-07 audit.
