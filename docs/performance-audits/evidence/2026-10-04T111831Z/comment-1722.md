Evidence at `94976eccad9c31776cf264a33dcda960dd8ed51a` for the placement cost, from the minimum-work audit in https://github.com/buster14a/buster/pull/2627 (`docs/performance-audits/2026-10-04T111831Z.md`). Diagnostic desktop sample (Zen 4, pristine-worktree Release compiler, `generate --no-include-tests`, `perf record -F 4999 --call-graph fp`, inclusive cycle shares); work-ledger counts are exact. **No production change.**

| | stage-1 self-compile | pinned `sqlite3.c -g` |
|---|---:|---:|
| `machine_predicate_placement_build` inclusive | 11.35% of cycles (≈2.44 G of 21.51 G instructions) | 7.53% |
| `machine_fast_placement_build_prepassed` self | 3.63% | 2.63% |
| `machine_fast_close_live_ranges` self | 2.88% | 0.78% |
| `machine_fast_prepass_build` self | 1.38% | — |
| machine rows / virtual registers / blocks | 1,971,400 / 940,283 / 187,803 | — |
| placement edits / liveness word updates | 824,976 / 18,976,373 | — |
| allocator edit rows in the encoded output | 676,480 of 1,824,010 instructions (37%): spills 222,559, reloads 203,606, copies 195,322, rematerializations 54,993 | — |

That is about **1,240 instructions per machine row** for placement against a linear-scan bound of a few dozen, and the edit rows it emits are then carried by encoding (4.23%), `-g` location/line recording (4.0%), the object writer and the linker. The audit ranks "machine-row volume and the placement algorithm" second among removable pools (−8…−12% of the self-compile as an Amdahl-adjusted estimate, not a measurement), and notes that the same change shrinks generated code (Buster `sqlite3.o` `.text` 2,039,847 B vs TinyCC 1,377,406 vs Clang `-O0` 1,113,473). Owner search unchanged: #54, #1473, #49 and this issue; the draft #2344 cache-locality slice is separate.
