Current per-process floor at `94976eccad9c31776cf264a33dcda960dd8ed51a`, from the minimum-work audit in https://github.com/buster14a/buster/pull/2627 (`docs/performance-audits/2026-10-04T111831Z.md`). Pristine-worktree Release compiler (tests off), Zen 4 desktop, `perf stat -r 3` and child rusage with `taskset -c 5`; timings diagnostic, instruction counts exact. **No production change.**

| Command | instructions | wall | user / sys | peak RSS |
|---|---:|---:|---|---:|
| `ide help` | 791,591 | 0.8 ms | — | 1.7 MB |
| `cc -fsyntax-only empty.c` | 1,351,088 | 2.9 ms | 0.0 / 2.3 | 25.8 MB |
| `cc -c empty.c` | **65,960,937** | **15.3 ms** | 2.6 / 6.8 | 52.5 MB |
| `cc -c -g headers.c` (20 glibc headers, one function) | 161,836,501 | 28.3 ms | 12.6 / 10.7 | 89.1 MB |
| `cc -c -g lapi.c` (Lua 5.4.8) | — | 29-33 ms to btrfs, 22 ms to tmpfs | 15-17 / 5-9 | 86 MB |

So the content-independent floor is now **66.0 M instructions** (down from the 124 M this issue records before #1317/#1536 landed), about 6-7 ms of CPU here, plus ~6 ms of system time for arena reservation and first touch, plus the ~8 ms `fsync` of the published object on btrfs (#2621). On the 33-file Lua 5.4.8 build (846 ms serial, 25.6 ms per file) the floor is roughly 20 of 25.6 ms per file; TinyCC compiles `lapi.c` in 5.6 ms total. In the self-compile the prewarm is ~0.17% of cycles (`machine_x86_64_exact_prewarm`), 0.75% on `sqlite3.c`.
