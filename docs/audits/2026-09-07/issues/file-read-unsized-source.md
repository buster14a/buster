<!-- buster-audit-2026-09-07:file-read-unsized-source -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below were re-derived for that commit and each cited site still reads as described, but the reproduction itself was not re-run there.

**Priority:** P1 - demonstrated acceptance of an empty translation unit with exit status 0.
**Evidence:** found by a subsystem pass and traced in source; the reproduction below was run there and not independently re-executed for this bundle. Confirm it before starting the fix.

## What is wrong

`file_read` sizes its buffer from `os_file_get_size` (`fstat`) and skips the read entirely when `st_size == 0`. Anything that does not report a size in `st_size` - a FIFO, a pipe, a `/proc` file, a character device - therefore reads as a zero-length buffer with no error.

**Source locations**

- src/buster/lib/file.c:229-241 (`file_read`)

## Reproduction and measured evidence

```
file_read(arena, S8("/proc/self/fd/3"), (FileReadOptions){0})
```

on a pipe holding 26 bytes of C source returns `length = 0`.

This reaches users through `ide cc <(...)` process substitution, FIFOs, `/proc` files and character devices: the driver compiles an **empty translation unit and succeeds**, producing an object with no code and exit status 0.

## Required fix

When the reported size is 0, fall back to a grow-and-read loop until `os_file_read` returns 0, rather than trusting `st_size`. Keep the fast path for regular files with a known size.

## Validation / definition of done

First confirm the reproduction; this comes from a subsystem pass. Then add tests compiling from a FIFO and from process substitution, asserting the source is read in full. Add a test that an empty *regular* file still behaves as today, so the fallback does not change that case.

## Existing work / scope

Section S3 of the 2026-09-07 audit. See `os-read-error-indistinguishable-from-eof` for the related read-side defect.
