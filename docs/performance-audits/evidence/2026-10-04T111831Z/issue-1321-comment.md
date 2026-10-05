## Census: where the 500,552 provisional SSA parameters come from at `94976ecc` (diagnostic, no production change)

Evidence for this owner from a whole-pipeline minimum-work audit (draft `docs/performance-audits/2026-10-04T111831Z.md`, local checkout, not yet published). **No production source changed; no speedup is claimed.**

### Method

- Compilers built from a pristine detached worktree at `94976eccad9c31776cf264a33dcda960dd8ed51a` with Clang 23.1.1, `generate --no-include-tests`, Release. The census compiler is the `-DBUSTER_BENCH_ALLOCATIONS=ON` build plus a 92-line probe entirely under `#if BUSTER_BENCH_ALLOCATIONS`: each `CIrSsaParameter` records `function->instruction_count` at creation (or "created by finish"); after the simplification loop in `c_ir_ssa_finish`, each pending parameter is classified by creation site, memory fallback, final predecessor count, whether every final predecessor's terminator row id is below the creation count ("sealed at creation"), and removed/kept. The probe reads state only; its `sqlite3.c -g0` object is byte-identical to the uninstrumented compiler's (`f38b8959…`).
- Host: Zen 4 desktop; counts are exact and machine-independent.

### Result

| Workload | created | by the finish walk | by construction | sealed single-pred (all removed) | sealed merge removed / kept | unsealed merge removed / kept | memory-owned | preds 0 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| stage-1 self-compile | 500,552 | 353,727 (70.7%) | 146,825 (29.3%) | 107,312 | 14,700 / 9,826 | 3,019 / 5,191 | 6,777 | 617 |
| pinned `sqlite3.c` | 80,580 | 35,604 (44.2%) | 44,976 (55.8%) | 27,558 | 7,068 / 1,566 | 739 / 1,290 | 6,754 | 706 |
| Lua `lvm.c` | 691 | 165 | 526 | 385 | 66 / 31 | 4 / 5 | 34 | 2 |

`IR_FRONTEND_SSA` for the self-compile: `parameters_created=500552 parameters_removed=466774`; `ir_construction`: `before_ssa_value_rows=1,746,321 → after 1,400,064`, `ssa_remap_value_rows=4,170,386`, `ssa_remap_instruction_rows=1,610,066`, `ssa_finish_slot_probes=3,449,652`, `ssa_simplify_incoming_visits=2,157,793`. `c_ir_ssa_finish` is 6.6% of sampled cycles on the self-compile (4.1% on `sqlite3.c -g`).

### What it says

1. **71% of the provisional parameters on the self-compile are created by the finish-time resolution walk itself** (`c_ir_ssa_current` with `predecessor_offsets` set, propagating an unresolved read through merge blocks and stamped cycles), not by body lowering. Tracking predecessors during construction and forwarding eagerly cannot touch them.
2. Of the 29% created during construction, 73% sit in blocks that had exactly one predecessor whose terminator already existed, and 10% more are merges whose incoming values turned out equal. Eager forwarding with predecessor tracking would avoid 122,012 creations (24% of the total) and their incoming rows on the self-compile, 34,626 (43%) on SQLite — but **not the remap**: the 183,509 read placeholders still need replacement, and without use lists every replacement is a whole-function renumbering.
3. The lever for this pool is therefore a fix-up mechanism that never materializes trivial parameters and read placeholders as value rows (use lists on parameters, or deferred operand patch lists resolved at seal time), not sealing at construction and not another pass cut. The dominated-definition shortcut this issue owns remains valid for its slice (`single_entry_definition` forwards are inside the 70.7%).

Probe diff and raw lines are retained beside the audit draft (`evidence/2026-10-04T111831Z/ssa-census-probe.diff`, `measurements.txt`). Hypothesis status: the 24% / 43% avoidable fractions are exact counts; the claim that use lists or patch lists remove the remap is unverified design reasoning.
