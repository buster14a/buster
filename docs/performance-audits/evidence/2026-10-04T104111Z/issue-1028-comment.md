## Measured cost of the separate validation pass on valid input: 17.4–18.7% of instructions, zero effect on generated code

Evidence-backed addition to this attribution owner; **no production change, no merge request, no speedup claim beyond the diagnostic probe described here.** Research branch `devin/rederive-validation-probe` (head `da9908051`, based on main `94976eccad9c31776cf264a33dcda960dd8ed51a`; `src/`, `CMakeLists.txt`, `build.c` and `tools/work_ledger` are identical to that revision — the branch only adds a workflow and probe scripts under `docs/performance-audits/evidence/rederive-validation-probe/`). The branch-only workflow is not intended for main.

### Method

- Probe compiler: the pinned tree with exactly one line removed — the call to `c_parse_validate_lowering_constraints` in `c_analyze_semantics_core` (`c_parse.c:28907`); `c_parse_index_scope_children` is kept because lowering reads it. The probe removes diagnostics for invalid input and is **never a production candidate**; on valid input it isolates what the separate pass costs.
- Subjects built with the in-tree `tools/work_ledger/build_subject.sh` (ledger + plain, `-O2 -march=x86-64-v3`) from the pinned tree and from the probe copy, on GitHub-hosted runners only. Nothing ran on a desktop or the 9700X.
- Workloads: the stage-1 self-compile (`cc -Isrc -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g -march=znver3 src/buster/apps/ide/ide.c -lm`, 3,760,569 tokens, 5,659 functions) and the pinned SQLite 3.53.4 `sqlite3.c` (`-g0 -O2`, 708,484 tokens).
- Measurements: exact work ledger (`-fsource-metrics`), deterministic callgrind Ir with inclusive attribution, output SHA-256, and 8 alternating `taskset -c 1` wall-time pairs. Runs: [37194571306](https://github.com/buster14a/buster/actions/runs/37194571306) (AMD EPYC 9V74, full protocol), [37194027655](https://github.com/buster14a/buster/actions/runs/37194027655) (Xeon 6973P-C, self only, identical Ir and ledger), [37195353936](https://github.com/buster14a/buster/actions/runs/37195353936) (per-section output identity, `-g` vs `-g0`). Artifacts are attached to the runs (30-day retention).

### Results

| Workload | Ir base | Ir probe | Δ Ir | `c_parse_validate_lowering_constraints` inclusive (base) | Paired wall probe/base (median of 8) | Output |
|---|---:|---:|---:|---:|---:|---|
| self-compile `-g` | 24,587,219,402 | 20,310,981,563 | **−17.39%** | 4,598,435,119 (18.70%) | 0.856 (9V74); ≈0.85 (6973P-C) | `.text/.rodata/.data/.eh_frame` identical; only `.debug_info` (2,497,007 → 2,067,332 B) and `.debug_str` (−55 B) differ |
| self-compile `-g0` | — | — | — | — | — | **byte-identical** (`fdf1f459…`) |
| sqlite3.c `-g0` | 4,681,904,692 | 3,807,652,415 | **−18.67%** | 859,495,252 (18.36%) | 0.836 | **byte-identical** (`f0e57a4a…`) |

The retained #906 9700X `cycles:u` sample puts the same subtree at 24.10%, so the pass runs at a lower IPC than the compiler average. `work.machine.*` (rows, virtual registers, placement edits, `encoded_bytes` = 16,044,173) and `ir_construction.*` are identical between base and probe on both workloads. The probe's lowering does +1.9% Ir of fallback array-bound/alignment work (`c_ir_alignof_object_alignment`, `c_parse_infer_initializer_array_count`), so 17.4% is the net and 18.7% the gross cost.

Exact ledger of the removed pass on the self-compile (base − probe): `rederive.type_query_roots` 2,054,240 (cache hits 267,789; literal answers 1,110,807; uncached 675,644 re-walking 3,963,389 tokens — 1.05× the whole token stream); `rederive.type_machine_runs` 735,503; `c_census.semantic.location_recoveries` 735,503 (one `c_preprocess_token_location` per uncached query at `c_parse.c:6243`, computed eagerly for a diagnostic that is never emitted on valid input); `snapshot.query_checkpoint_bytes` 329,714,272 + `snapshot.frame_bytes` 506,204,000 with 99,291 rollbacks; `rederive.initializer_walk_tokens` +1,540,478 over +3,233 walks; `population.layout_rows_seeded` 2,696,197 (108.6 MB scratch); semantic arena requests 7.18 GB (16,116 pages touched); `c_census.semantic.spelling_reads` 8,570,393 and `string_equal_calls` 12,137,726.

Self-Ir decomposition of the removed work (share of base total): `c_type_parse_machine_run` 2.49%, `c_parse_type_layout_solve`+`c_parse_builtin_type_layout` 2.04%, `c_parse_expression_type_query` 1.34%, `c_parse_validate_const_assignments` 1.24% self / 5.94% inclusive, `c_parse_validate_lowering_constraints` self 1.02%, `c_parse_infer_initializer_array_count_core` 0.99% self / 7.45% inclusive, expression leaf/precedence/lookup/member 2.7%, `memcpy`+`memset` 0.72%, `c_parse_typed_constant` 2.97% inclusive (initializer constants folded here and again in `c_ir_constant_initializer_bytes`).

### What this does and does not establish

- On valid input the pass produces nothing the code generator consumes; its only consumer-visible effect is +429,675 bytes (+20.8%) of `.debug_info`, from types interned during speculative typing and the scalar prepublication at `c_parse.c:27894` — the `.debug_info` sensitivity already recorded here, now sized.
- It does **not** justify removing the pass: canonical IR erases `const` (`IrType` carries only `is_atomic`/`is_volatile`), so lowering cannot detect const assignment, return/arity incompatibilities or duplicate cases on its own; those checks are why the pass exists. "Lower first, validate on failure" would silently accept invalid programs and is rejected.
- Hypothesis for a bounded next step (unverified): move one family at a time — `c_parse_validate_const_assignments` first (5.94% inclusive) — from the eager per-body scan to the lowering site that already resolves the target entity/member (`c_ir_assignment_expression_place_frame_push` / `c_ir_emit_field_place_from_value`), keep the eager family for `-fsyntax-only`, and gate each move on byte-identical diagnostics over the negative fixture corpus and byte-identical objects over the work-ledger corpus. The measured ceiling of that migration is the 17–19% above; the trivial sweep is the eager location recovery at `c_parse.c:6243`.

Validation actually run: the three hosted runs above (all steps exit 0). Not run: `test_all`, `test_self_host`, mode matrix — this is a documentation-only evidence record with no production change.
