<!-- buster-audit-2026-09-07:arena-commit-failure-and-granularity -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below were re-derived for that commit and each cited site still reads as described, but the reproduction itself was not re-run there.

**Priority:** P1 - demonstrated segfault and demonstrated abort on a documented-supported configuration.
**Evidence:** found by a subsystem pass and traced in source; the reproduction below was run there and not independently re-executed for this bundle. Confirm it before starting the fix.

## What is wrong

Two defects in `arena_allocate_commit`.

**Commit failure is ignored.** `os_commit`'s return value is used only to decide whether to advance `os_position`; the failure is otherwise discarded. The inline bump in `arena_allocate_bytes` (`arena.h:128`) then publishes `position = aligned_size_after` and returns a **non-null** pointer into memory the OS refused to commit. The `BUSTER_CHECK(arena->position <= arena->os_position)` at `arena.h:129` is `__builtin_unreachable()` in Release, so it is not a guard.

**Non-granular reservations fail a bound the bump never applied.** The commit target is rounded up to `granularity` and then compared against `reserved_size`, so an allocation ending exactly at a `reserved_size` that is not a multiple of `granularity` is rejected - even though `arena.h:122` already accepted it. `arena.c:36-40` explicitly documents this configuration as supported ("reservations are alignment-granular, and they are not").

**Source locations**

- src/buster/lib/arena.c:44-45 (`arena_allocate_commit`, the granularity bound)
- src/buster/lib/arena.c:49 (`arena_allocate_commit`, the ignored commit failure)
- src/buster/lib/arena.h:122 and :128-129 (`arena_allocate_bytes`, the inline bump)

## Reproduction and measured evidence

**Commit failure.** An arena with a 64 MB reservation and 64 KB granularity grows to 4 MB, is rewound with `arena_set_position_and_decommit` (which leaves the released range `PROT_NONE` at `os.c:475`), then allocates 2 MB again while the recommit fails with `ENOMEM`. `arena_allocate_bytes` returns a valid-looking pointer with `position=2097216, os_position=65536`; the first write past 64 KB **segfaults** (probe exit 139). The same shape occurs on Windows for any `VirtualAlloc(MEM_COMMIT)` refusal at the system commit limit.

**Granularity.** `arena_create({.reserved_size = BUSTER_MB(1)+64, .granularity = BUSTER_KB(64), .initial_size = BUSTER_KB(64)})`, then `arena_allocate_bytes(arena, reserved_size - 64, 1)`: `aligned_size_after = 1048640 <= reserved_size` passes at `arena.h:122`, but `target_committed_size = 1114112 > 1048640`. Debug aborts (`assertion failed at src/buster/lib/arena.c:45`); Release proceeds to `os_commit` a range past the reservation, which for `count > 1` reaches into the next arena's pages.

## Required fix

Report commit failure through `os_fail` - the file's own contract is that allocation must not return null, so the honest failure is to abort, not to return a pointer that segfaults on first touch. And clamp the commit target: `target_committed_size = BUSTER_MIN(align_forward(aligned_size_after, granularity), reserved_size)`, or reject non-granular `reserved_size` in `arena_create` and update the comment at `arena.c:36-40` to match.

## Validation / definition of done

First confirm both reproductions; this comes from a subsystem pass. Then add arena tests for: commit failure under an `RLIMIT_AS` or interposed `mmap` returning failure; a non-granular `reserved_size` allocation ending exactly at the reservation; and a rewind-then-regrow cycle. Run in Release, where the checks are absent.

## Existing work / scope

Section S3 of the 2026-09-07 audit. Highest-leverage file in the repo - every subsystem allocates through it.
