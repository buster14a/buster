<!-- buster-audit-2026-09-07:os-write-error-counted-as-success -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below were re-derived for that commit and each cited site still reads as described, but the reproduction itself was not re-run there.

**Priority:** P1 - demonstrated silent data loss and file corruption on the only write path.
**Evidence:** reproduced first-hand in this audit against `build/Debug/ide` and `build/Release/ide`, differentially against clang 18.

## What is wrong

```c
BUSTER_CHECK(result > 0);
return (u64)result;
```

`BUSTER_CHECK` is `__builtin_unreachable()` in Release (see `release-check-macro-is-unreachable`), so a `write(2)` returning -1 is returned as `UINT64_MAX`. `os_file_write` adds that straight into its progress counter, which wraps:

```c
while (total_written_byte_count < buffer.length) {
    u64 written = os_file_write_partially(fd, buffer.pointer + total_written_byte_count, ...);
    total_written_byte_count += written;
}
```

`os_file_write` returns `void`, so the failure cannot propagate; `file_write()` still reports success and the process exits 0.

**Source locations**

- src/buster/lib/os.c:1280 (`os_file_write_partially`)
- src/buster/lib/os.c:1293 (`os_file_write`, the accumulating loop)

## Reproduction and measured evidence

Two demonstrated shapes, both via a probe against the real sources built with `-DBUSTER_OPTIMIZE=1`:

**Truncation.** `os_file_write(fd, 200000 bytes)` on a pipe that returns `EAGAIN` after accepting 4096: the counter goes `4096 + UINT64_MAX = 4095`, the loop spins down and exits. 195904 bytes are dropped, `file_write()` returns `true`, exit status 0.

**Corruption.** A 100-byte partial write, then one transient failure: the next call is issued at buffer offset 99, so byte 99 is written twice and 257 bytes reach the file for a 256-byte buffer. Probe output: `write#1 at offset 0 -> 100`, `write#2 at offset 100 -> -1`, `write#3 at offset 99 count 157`.

This is the only write path for object files, executables and archives.

## Required fix

Give `os_file_write_partially` a failure-flagged result (a signed return, or a `bool` out-parameter) and `os_file_write` a `bool` return, then have `file_write` and `file_copy` check it and report. A write path that cannot express failure is the underlying problem; the `UINT64_MAX` is just how it surfaces.

## Validation / definition of done

Add a test that injects a short write followed by a failure - a pipe with a small buffer, or an interposed `write` - and asserts that the caller observes failure and that no bytes are duplicated. Run it in a Release build; in Debug the `BUSTER_CHECK` masks the whole defect.

## Existing work / scope

Section S3 of the 2026-09-07 audit. See `release-check-macro-is-unreachable` for the shared root, and `os-read-error-indistinguishable-from-eof` for the same shape on the read side.
