Per-file and in-process timings at `94976eccad9c31776cf264a33dcda960dd8ed51a` for the Lua 5.4.8 shape, from the minimum-work audit in https://github.com/buster14a/buster/pull/2627 (`docs/performance-audits/2026-10-04T111831Z.md`). Pristine-worktree Release compiler (tests off), Zen 4 7940HS (8c/16t) desktop shared with two concurrent builds, child rusage; **diagnostic, not acceptance evidence; no production change.** 33 units (`onelua.c` excluded), `-c -g -DLUA_COMPAT_5_3 -DLUA_USE_LINUX`:

| Mode | wall | user / sys | peak RSS |
|---|---:|---|---:|
| 33 processes, serial, objects on btrfs | 846 ms | 394 / 196 | 131 MB (max) |
| 33 processes, serial, objects on tmpfs | 611 ms | 407 / 196 | 131 MB |
| one `cc -c` process, 33 inputs | 554-606 ms | 266-278 / 87-89 | 347 MB |
| 33 processes, `xargs -P 8` | 200 ms | 584-601 / 344-357 | 130 MB per process |
| one link invocation, `-fcompile-jobs=8` | 165 ms | 353 / 105 | 689 MB |
| TinyCC 0.9.28rc, 33 processes, serial, tmpfs | 157 ms | 95 / 60 | 5.6 MB |

Two changes from the 2026-09-27 census: `-fcompile-jobs` at eight lanes is now faster than eight processes (165 vs 200 ms, where the earlier audit found four lanes no faster than four processes), and the 235 ms difference between btrfs and tmpfs in the serial row is the per-artifact `fsync` in `file_publish` (#2621), not compilation. The header share is visible as 1.47 M lexed vs 559 K preprocessed tokens across the 33 processes. The audit's Amdahl note for the self-compile: preparation plus code generation are 36% of today's cycles, so a parallel back half alone bounds a single large unit at ~1.5x on eight lanes until the serial frontend shrinks.
