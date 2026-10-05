## Observed

At `94976eccad9c31776cf264a33dcda960dd8ed51a`, `file_publish_slices_checked` (`src/buster/lib/file.c`) calls `os_file_flush` — `fsync(2)` on Linux/macOS, `FlushFileBuffers` on Windows — on the staging file of every published artifact before `os_file_replace` renames it over the destination. `strace` of `ide cc -c lapi.c -o lapi.o` shows exactly one `fsync(3)` followed by the `rename` of `.buster-staging-<pid>-0.tmp`.

On a btrfs (zstd) home on an NVMe desktop (Zen 4 7940HS, Linux 7.2.9), the flush costs 7-8 ms of wall per object that is neither user nor system CPU time (measured with `wait4` rusage of the child, `taskset -c 5`, Release compiler built with `generate --no-include-tests`):

| Command | Output on btrfs | Output on tmpfs |
|---|---:|---:|
| `cc -c -g -DLUA_COMPAT_5_3 -DLUA_USE_LINUX lapi.c` (3 runs each) | 29.0-33.1 ms wall (user 13.5-17.2, sys 6.0-9.0) | 22.1-22.5 ms wall (user 14.9-17.1, sys 5.0-7.0) |
| Lua 5.4.8, 33 files, one process each, serial | 846 ms wall (user 394, sys 196) | 611 ms wall (user 407, sys 196) |

So the flush is about 27% of the wall time of a per-file Lua build on this host; on the stage-1 self-compile it is one flush, ~0.4%. The remaining non-CPU wall per small process (~2 ms) is exec and exit.

## Expected

`os.h` documents the contract above `os_file_flush`: "Flush is explicit: ordinary artifact writes promise completion, not crash durability." The publish path nevertheless flushes every ordinary object and executable, so the implementation and the documented promise disagree. Atomic replacement (the goal of #83) needs the rename, not the flush; neither Clang nor GCC fsync object files, and `make`/`ninja` do not rely on object durability across a crash (they rebuild from timestamps).

## Affected symbols

`file_publish_slices_checked`, `file_publish_checked`, `os_file_flush` (`src/buster/lib/file.c`, `src/buster/lib/os.c`, `src/buster/lib/os.h`); every driver/linker artifact goes through `compiler_driver_publish` → `file_publish_*`.

## Validation already run

Timing only (above). No source was changed; no test, self-host or harness gate ran. Numbers are diagnostic desktop observations, not acceptance evidence; the cost on a GitHub-hosted ext4 VM, on Windows (`FlushFileBuffers`) and on macOS (`fsync` is not a full barrier there) is unmeasured.

## Uncertainty

- Whether any consumer depends on the flush (e.g. the build driver's self-host stage verification reading the executable immediately after publication does not need durability, only completion, which `close`+`rename` already provides on the same mount).
- Whether a per-filesystem cost on CI runners is material; btrfs with compression is a plausible worst case among Linux filesystems.

## Completion criteria

- Decide the contract: either make the flush opt-in (a driver option or an environment policy for installers that want crash durability) and update the `os.h` comment, or keep it and correct the comment to say artifacts are flushed.
- If removed: `tests/`-level publication tests still pass (atomic replace, partial-write cleanup, concurrent reader), `test_self_host` fixed point unchanged, and a per-file build (Lua 5.4.8 harness or the compiler's own split build) shows the per-object wall reduction on at least one Linux filesystem.
- Report the decision in `docs/agents/driver.md` or wherever artifact publication is documented.

Found while auditing the per-stage minimum work of the compiler (draft `docs/performance-audits/2026-10-04T111831Z.md`, local, not yet published).
