RSS stop points at `94976eccad9c31776cf264a33dcda960dd8ed51a`, from the minimum-work audit in https://github.com/buster14a/buster/pull/2627 (`docs/performance-audits/2026-10-04T111831Z.md`). Pristine-worktree Release compiler (tests off), Zen 4 desktop with THP active, child rusage (`ru_maxrss`), `taskset -c 5`; diagnostic. **No production change.**

| Stop | self-compile wall | peak RSS | `sqlite3.c` wall | peak RSS |
|---|---:|---:|---:|---:|
| `-E` | 210 ms | 590 MB | 79 ms | 153 MB |
| `-fsyntax-only` | 708 ms | 590 MB | 176 ms | 153 MB |
| `-c -g0` | 1,957 ms | 906 MB | 440 ms | 259 MB |
| `-c -g` | 2,098 ms | 1,024 MB | 490 ms | 306 MB |
| link, `-g` | 2,075-2,124 ms | 1,072 MB | — | — |

The frontend's peak is the preprocessor's peak on both inputs. Against the retained state (self-compile: ~86 MB tokens and spelling, ~195 MB IR, ~30 MB semantic model, 47 MB machine rows, 16 MB code, 47 MB object, 41 MB image ≈ 460 MB) the process touches 1.19 GB (304,021 four-KiB faults in the hosted ledger of this revision, run 37194571306 in #2606: preprocess 94,127, semantic 42,404, lower 89,539, prepare 7,217, codegen 28,589, object 20,563, output 13,341) and spends 68-86 ms (4%) in the kernel zero-filling it even with THP. A function-at-a-time back half would peak near 200 MB; TinyCC compiles SQLite in 13 MB. The work ledger's requested-bytes rows for the same compile are preprocess 1.23 GB, semantic 9.83 GB, lower 63.1 GB, codegen 3.47 GB (requested, mostly rewound scratch — the 330 MB of query checkpoints and 600 MB of type-machine frames are actually copied).
