# Frontend scout: the C frontend's token-range representation

Different-hypothesis scout. Main is `ade6ac4b` (tree `4c530622`). Every number here is
**instrumentation**: element counts, bytes, calls, callgrind Ir, minor faults and RSS
from this 4-vCPU Firecracker VM. None of it is a timing result or a speedup claim.
The one cycle-share source is the committed 9700X capture in
`docs/performance-audits/evidence/2026-09-23T144607Z/`. It was taken at `194bd65b` and
is labelled wherever it is used.

## Tools and evidence classes

- **Source reading.** Grep, Read and sed over `src/buster/lib/compiler/frontend/c/{c.h,c_internal.h,c_source.c,c_parse.c,c_gen.c,c.c}` and `lib/arena.h`/`arena.c`/`os.c`.
- **Probe build.** Disposable worktree `scratchpad/wt-frontend`, built into `scratchpad/bld-probe` with the Release clang flags. `fe/probe_patch.py` wraps every `X.tokens[i]` accessor in `c_parse.c`/`c_gen.c` (865 and 904 sites) with a per-phase read counter. It also adds counters for type queries, cache hits, `CParseResult` checkpoints, `c_parse_scope_for_token`, entity lookups, type/IR frame pushes and location recoveries, plus table fill reports. Both probe binaries produced an object **byte-identical** to the Release compiler's stage-1 object.
- **callgrind.** valgrind 3.22 callgrind on the full stage-1 compile, 24.79 G Ir. It could not run the Release `-march=native` binary: `os_thread_set_name` uses AVX-512, which valgrind rejects as SIGILL. I therefore rebuilt clean HEAD at `-march=x86-64-v3` (`scratchpad/bld-a/ide`). Run natively, that build produces an object identical to Release. Under valgrind the host detection reports `haswell`, so codegen output differs by 1.7 KB, and the SIMD frontend kernels run their AVX2 or scalar paths (see the caveats in the verdict). Outputs: `fe/cg.stage1.out`, `fe/cg_incl.txt`, `fe/cg_self.txt`, `fe/cg_callers.txt`.
- **gdb.** gdb/Python breakpoints (`fe/fe_rss.py`) record VmRSS and minflt at each frontend phase entry, using the Release binary.
- **Re-analysis.** The committed 9700X stacks were re-analysed with `fe/incl.py` and `fe/callers.py`. The coordinator's census `census/stage1-report/sites.csv` was also used.
- **Workload.** The stage-1 recipe produces 3,600,849 preprocessed tokens, 25,826,727 spelling bytes, 5,206 lowered functions and 1,523,381 canonical instructions before FAST.

## (a) Frontend data-flow map, with element sizes and stage-1 counts

| Stage (entry) | Representation produced or read | Element size | Stage-1 count | Notes (verified in source) |
|---|---|---:|---:|---|
| `c_lex_dispatch` → `c_preprocess` | `CToken` rows + `CTokenShape` sidecar | 12 B + 1 B | 3,231,860 lexed → 3,600,849 final | `c.h:CToken`, which checks `sizeof==12`. The spelling is `spelling_base+offset`. `symbol` is meaningful only for identifiers. |
| same | `CPpToken` staging row, used for expansion and line staging | 16 B | n/a | `c_source.c:CPpToken`. The comment says an expanded token is "copied eight to ten times". I did not verify that. |
| `c_symbols_intern_tokens` | Writes `CToken.symbol` for identifier rows | – | 1 pass | A SIMD shape scan, followed by a row read/write per identifier. |
| `c_parse_ast` | `CParserDeclaration` linked list | 120 B-ish | 13,530 declarations | The header says "It builds no tree; every later consumer re-walks token ranges". It also validates every integer literal and discards the value (`c_parser_validate_integer_token`, 640,176 calls). |
| `c_analyze_semantics_core` | `CParseResult` (416 B), passed by pointer but snapshotted by value | see §d | types 147,264 × 88 B; entities 69,636 × 136 B; identifier_uses 384,887 × 12 B; scopes 52,916 × 24 B | Its tables are sized from a token census. Per-token maps: `identifier_use_by_token_plus_one` (u32/token), `token_classes` (u8/token), `CTokenPositionIndex.matching_delimiters_plus_one` (u32/token), `declaration_range_words` (1 bit/token). |
| same (type machine) | `CTypeParseMachine`: `CTypeParseFrame` stack, `CParseExpressionTypeTask` stack | **984 B frame** (embeds a 416-B `CParseResult checkpoint` and a 176-B `CPreprocessResult`); 28 B task | 2,527,885 frame pushes | `c_internal.h:CTypeParseFrame`. `c_parse_expression_type_query` copies `CParseResult checkpoint = *result` on every miss. |
| `c_parse_validate_lowering_constraints` | Per-body scratch: `CParseExpressionQuery` cache (16 B/body token, memset), `skipped` (1 B/body token), plus per-validator scratch | 17 B/body token | 1,938,594 body tokens in 5,362 bodies | About 16 validators, each a separate linear walk of the body's 12-B rows. The cache exists **only inside function bodies**: `expression_queries` is null for file-scope initializers. |
| `c_lower_to_ir_with_options` | Per-token `token_entities_plus_one` (u32/token), a **second copy** of the binder's token→entity map | 4 B/token | 14.4 MB | Rebuilt from `identifier_uses` and entity declaration tokens (`c_gen.c`, around lines 48612–48628). |
| same, per body | `prepared_call_indices`, `prepared_call_token_heads`, `group_type_names` (u32/body token, initialised by loop); `CIrLowerFrame` 88 B; `CIrQueryFrame` 136 B | – | 1,847,469 lower-frame pushes; 185,843 IR queries | This is a second type/constant/layout engine (`CIrQueryMachine`, `c_ir_constant_*`) that works on IR type ids. The whole-stream delimiter index is reused rather than rebuilt (verified). |
| Output | `IrInstruction` 64 B, `IrSourceRange` 12 B, `IrValue` 16 B, `IrBlock` 64 B | – | capacity 5,938,152 instruction rows (3 × body tokens) vs 1.52 M written | Here the frontend hands off to the IR-row hypothesis. |

## (b) Multiplicity ledger: token-row reads per token, by phase

Reads are counted through the named accessors in `c_parse.c`/`c_gen.c` (probe, stage-1).
The count excludes `c_source.c` preprocessing, shape-sidecar reads, the SIMD census and a
few local `CToken* tokens` aliases, so it is a lower bound.

| Phase | Row reads | / TU token | / body token | Scope lookups | Type queries (hits) | `CParseResult` checkpoints | Type-frame pushes | Entity lookups |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Preprocess: lex, intern, stage, final write (static estimate) | ~4 touches/token | ~4 | – | – | – | – | – | – |
| `c_parse_ast` | 8,535,297 | 2.37 | – | 0 | 0 | 0 | 0 | 0 |
| Semantic model (declarations, file scope) | 22,327,789 | 6.20 | – | 0 | 1,444 | 47,142 | 113,055 | 39,708 |
| Body binder (`c_parse_bind_function_body`) | 14,350,587 | 3.99 | 7.40 | 22 | 505 | 67,419 | 102,284 | 504,019 |
| **Validation** (16 validators + file-scope initializer checks) | **85,907,658** | 23.86 | **44.31** | **700,401** | **1,987,934 (246,637)** | **1,797,654** | **2,312,546** | **1,032,524** |
| Lowering setup and body scans | 13,885,183 | 3.86 | 7.16 | 0 | 0 | 0 | 0 | 73,767 |
| Lowering bodies (`c_ir_lower_body`) | 67,709,568 | 18.80 | **34.93** | 11,229 | – | – | – | 73,715 |
| Lowering global initializers | 12,479,119 | 3.47 | – | 0 | – | – | – | 36,023 |
| **Total after preprocessing** | **225,195,201** | **62.5** | – | 711,652 | 1,989,883 | 1,912,215 | 2,527,885 | 1,759,756 |

Reads per body token for each validator: const-assign 8.95, statement-expression 4.34,
call-arity 3.35, control 2.36, VLA 2.34, label-values 2.11 (the gate's own scan), builtin 1.42,
compound 1.41, return 1.23, switch 1.18, sizeof 1.17, labels 1.09, asm 1.00, generic 0.99,
atomic 0.95. File-scope work before the per-body loop adds 10.4 per body-token-equivalent.

What the ledger shows:

- **Row reads are not the multiplicative cost.** A function body is read about 87 times after parsing: 7.4 by the binder, 44.3 by validation and 34.9 by lowering. That is about 2.7 GB of 12-B reads. But the average body is 371 tokens (4.4 KB), so these reads are L1/L2-resident.
- **The cost is re-deriving facts on each use.**
  - Validation re-resolves scopes 700,401 times and entities 1.03 M times. The binder already stored every body identifier's entity per token (384,887 uses), and lowering needs only 11 K scope and 184 K entity lookups.
  - Validation runs 1.99 M expression-type queries with a 12.4% cache hit rate. It snapshots the 416-B `CParseResult` 1.91 M times (**795 MB copied**) and pushes 2.53 M frames of 984 B (**≥2.49 GB of frame stores**; the compound-literal build and by-value copy add more that I did not measure).
- **Source-range resolution is no longer per-instruction.** Only 105,570 `c_preprocess_token_location` calls happen, and `IrSourceRange` carries the spelling-map offset (`c_ir_source_range`). The `+80 M` per-instruction line/column residual that 2026-08-09g recorded is gone. Verified.

callgrind Ir split (ade6ac4, x86-64-v3 build, 24.79 G Ir):

| Phase | Share of Ir |
|---|---:|
| Preprocess | 6.39% |
| `c_parse_ast` | 2.05% |
| Semantic, total | **34.01%** |
| – of which validation | 26.91% |
| Lowering | **23.29%** |
| – of which bodies | 12.46% |
| – of which global initializers | 5.65% |
| – of which SSA finish | 3.77% |
| IR prepare | 7.37% |
| Codegen | 25.00% |
| Object | 1.59% |
| **Frontend total** | **65.7% of Ir** |
| Backend total | 34.0% of Ir |

The 9700X cycle profile at 194bd65b agrees: semantic 29.49%, lowering 25.20%, preprocess 4.26% and parse 1.73% sum to **60.7% of cycles**.

Frontend RSS steps (gdb, Release binary; minflt counts 4 KiB faults and is deterministic):

| Step | RSS change | Minor faults |
|---|---:|---:|
| Preprocess, including startup | → 245.7 MB | 61,139 |
| `c_parse_ast` | +1.4 MB | – |
| Semantic model | +86.4 MB | 24,559 |
| Validation | +62.5 MB | 16,113 |
| **Lowering** | **+413.5 MB** | 103,935 |

The run's high-water mark is 1,123 MB.

## (c) Top three frontend candidates

### 1. The expression-type fact is recomputed per query; frames and snapshots are heavy (`CTypeParseMachine`)

**Evidence**

- `c_parse_expression_type_query` is **13.32% of Ir** inclusive (1.74 M calls).
- It drives `c_type_parse_machine_run` for 10.34% of Ir, with 3.81% self.
- Its `memcpy` of the `CParseResult` checkpoint costs 275.8 M Ir (1.11%, 3.49 M calls).
- On the 9700X at 194bd65b it was 8.31% of cycles inclusive.

**Callers**

| Caller | Calls | Ir |
|---|---:|---:|
| `c_parse_incompatible_aggregate_value` | 786,301 | 1.01 G |
| `c_parse_validate_const_assignments` | 475,174 | 0.88 G |
| `c_parse_initializer_expression_constraint` | 579,110 | 0.83 G |
| statement expressions | 79,509 | 0.40 G |

**File-scope initializers are the clearest duplicate.** Static initializer validation is 9.64% of Ir (`c_parse_validate_initializer_shape`); lowering the same initializers is another 5.65% (`c_ir_global_initializer`). For each initializer element, `c_parse_initializer_expression_constraint` does two things:

- It types the range checked (`c_parse_checked_expression_type`).
- It immediately re-types the **same range** through `c_parse_incompatible_aggregate_value` → `c_parse_expression_type_query`.

At file scope `machine->expression_queries` is null, so the second query cannot hit. That is true even though "checked facts can answer unchecked queries" (`c_parse_expression_type_query`).

The duplicate is not only CPU work. Audit 2026-09-24T124205Z found that reusing the checked type changed `.debug_info`: DWARF array types went from 39,333 to 38,936. So the duplicate queries **materialize duplicate `CType` rows that are emitted as DWARF**.

**Structural change (two parts)**

- (a) Keep a translation-unit-wide expression-fact table keyed by start token. Each entry is `{end, scope, type, flags}` at 16 B, as `CParseExpressionQuery` already is. Size it by expression starts rather than by body. File-scope initializers, validators and the checked/unchecked pair then all share one answer.
- (b) Slim `CTypeParseFrame` from 984 B toward its hot fields:
  - `result`, `preprocess` and `arena` are invariant per machine run and can move to `CTypeParseMachine`.
  - The per-frame `CParseResult checkpoint` can move to a side stack that only rolling-back frames push.

**Expected removal (instrumentation estimate)**

- The checked/unchecked duplicate: up to about 579 K of 1.74 M queries, roughly 3% of Ir.
- About 1% of Ir in checkpoint `memcpy`.
- Part of the 3.81% machine self cost from ~1 KB frame construction.
- Realistic total: **about 3–5% of Ir.** On cycles this is probably lower, because the work is compute-bound and L1-resident.

**Cheapest falsification**

- A probe that counts distinct `(start, end, scope, flags)` keys across the whole TU against 1.99 M queries. If the distinct/query ratio is above 0.9, the reuse potential is small; abandon (a).
- Count bytes actually read per frame kind. If the hot set is above 50% of 984 B, abandon (b).

**Counterarguments**

- The objects change (fewer DWARF types): the fixed point holds, but the 124205Z artifact-equivalence rule rejected this before.
- Rollback semantics need the snapshot for any frame that mutates the type table.
- Validation is a required boundary (#292).
- Lowering does not consume these facts, so this only removes duplication inside validation, not validation→lowering duplication.

### 2. Literal facts are thrown away by the token row (the `CToken.symbol` slot is dead on number and string tokens)

**Evidence: integer literals**

`c_parse_ast` already parses every integer literal (`c_parser_validate_integer_token` → `c_conditional_number`, 640,176 calls) and discards the value. The same spellings are parsed again:

| Re-parse site | Calls |
|---|---:|
| Semantic typing (`c_parse_expression_leaf_without_cast`) | 1,198,588 |
| Global-initializer lowering (`c_ir_constant_initializer_fold_integer_leaf`) | 520,098 |
| Body lowering | 84,180 |

That is about 3.8 parses per literal. `c_conditional_number` costs **532.7 M Ir (2.15%)** and `c_semantic_integer_literal_kind` 283 M Ir (1.14%); on the 9700X, `c_conditional_number` was 1.21% self.

**Evidence: string literals**

String literals are counted in validation (`c_ir_count_string_literal_range_for_target`, 740.8 M Ir, 2.99%) and decoded again in lowering (`c_ir_decode_string_literal_range_for_target`, 512.5 M Ir, 2.07%). Literals are 40.8% of the unit's code bytes. This part is **ISA-sensitive**: my AVX2 binary runs non-AVX-512 paths, and the 9700X profile had `c_ir_decode_quoted` at 0.16%.

**Structural change**

`CToken.symbol` is meaningful only for `C_TOKEN_IDENTIFIER` (per the `c.h:CToken` comment). For number and string tokens it can index a literal-fact table:

- Integers: `{u64 value (+hi), literal kind}`.
- Strings: decoded element count.

The table is filled by the walks that already compute these facts. It adds zero bytes per token. Synthesized or pasted tokens keep `symbol=0` and the existing spelling fallback, exactly like identifiers.

**Expected removal:** about 2.5% of Ir for integers (about 1–2% of cycles), plus up to about 2% of Ir for string counting on non-AVX-512 hosts.

**Cheapest falsification:** count `c_conditional_number` calls per distinct token index. If that is close to 1.2 rather than 3.8, reject.

**Counterargument:** respell paths must clear or reissue the id ("every site that respells a token must re-intern"), and literal-type selection depends on dialect and target. Both are fixed per translation unit, but the kind cache must be keyed accordingly.

### 3. Per-query scratch that grows with the type table and is actually cleared or seeded

This answers the coordinator's census question. Most type-count-sized requests are inert, but two sites really scale with calls × type count.

- **`c_parse_type_layout_core` with `offset_out`** (the `offsetof` and member-offset path from `c_parse_typed_constant`) **bypasses `CTypeLayoutCache`**. The guard is `cache = !offset_out && ...`. Each call therefore seeds and solves the whole type table: pending covers all ~147 K types, and 13–14 B/type are copied or set.
  - 181 such calls cost **606.6 M Ir (2.45%)**.
  - They stream about 147 K `CType` rows (88 B, 13 MB) per call.
- **`c_parse_member_type`** memsets `bool visited[type_count+1]` on every promoted-member search: 2,767 calls, **297 MB written**.
  - That shows as 296.9 M Ir of `memset`, but valgrind counts `rep stosb` per byte. The real cost is probably under 0.3% of cycles, because the 107-KB block stays in L2.
  - `CTypeParseMachine` already owns a generation-stamped `promoted_member_visited`/`promoted_member_generation` pair that this function does not use.

**Structural change:** answer member offsets from the cached aggregate layout (walk only the requested aggregate's members), and use generation-stamped visited arrays.

**Expected removal:** about 2.5% of Ir, plus a few tenths of a percent of real cycles.

**Falsification:** log `pending_count` and the cache state per `offset_out` call. If most calls already find the aggregate committed in the cache and still spend less than 1 M Ir, reject.

**Counterargument:** this is a local fix, not a representation redesign. It is bounded by the number of `offsetof` sites in this unit, which is heavy in `BUSTER_CT_CHECK(offsetof…)`.

**Also noted, smaller**

- Validation re-resolves scope and entity even though the binder's per-token table exists: `c_parse_scope_for_token` 1.15% of Ir and `c_parse_lookup_entity_symbol` 0.54% (3.5% of cycles at 194bd65b, when label values were still ungated). For example, `c_parse_update_operand_modifiable` calls `c_parse_scope_for_token` before checking `identifier_use`.
- `token_entities_plus_one` in `c_lower_to_ir_with_options` is a 14.4 MB duplicate per-token map.

## (d) Coordinator follow-up: are the census allocations touched, and does sizing imply clearing?

**Arena facts.** `arena_allocate` does not clear memory. `arena_allocate_zeroed` clears only below the dirty high-water mark (`arena.h`). Commit is `mprotect` (`os.c:os_commit`) with no prefault. The high-water `os_position` means a rewound scratch region is never recommitted: 11,416 `mprotect` calls in total versus 108 K calls to the compatibility check alone.

| Site | Requested | Per call | Touched / cleared? | Evidence |
|---|---:|---|---|---|
| `c_parse_types_compatible_core` (`c_parse.c:12664`, `CTypePair`[2T+1] × 12 B) | 270.6 GB over 108,534 calls | 2.5 MB | **Handed out only.** No memset. Touched = stack depth × 12 B. | callgrind: **27.2 M Ir inclusive for all calls (~250 Ir/call)** |
| `c_ir_promoted_member_path` (`c_gen.c:25797`) | 47.8 GB over 22,941 calls | 2.1 MB | Handed out only. Queue ≤ 4 entries (audit 2026-09-25T003323Z). | 11.7 M Ir inclusive |
| `c_ir_types_compatible` (`c_gen.c:17515`) | 4.76 GB over 6,951 calls | 0.7 MB | Handed out only | 0.7 M Ir |
| `c_ir_lower_nested_compound_literal_step` (cursors sized `types.count`) | 1.97 GB | 0.56 MB | Handed out only (`cursor_count=0`) | – |
| **`c_parse_member_type`** (`c_parse.c:2660–2662`) | 1.19 GB work + 297 MB visited | 107 KB | **visited: memset per call (297 MB written)**; work: not cleared | 296.9 M Ir of `rep stosb` |
| **`c_parse_type_layout_core`** (`c_parse.c:1512–1525`) | 70.6 MB (sizes) + alignments, resolved, provisional | ~14 B × T | **Seeded per call:** memcpy from the cache plus memset of provisional (83 M Ir memcpy, 149 memset calls). The **offset path skips the cache and solves the whole table.** | 606.6 M Ir under `c_parse_typed_constant` |

**`c_analyze_semantics_core` capacity tables** (stage-1 counts from the probe):

| Table | Capacity × size | Reserved | Written | Written % |
|---|---|---:|---:|---:|
| types (`c_parse.c:22623`) | (2T+1) × 88 B | 633.7 MB | 147,264 rows = 13.0 MB | 2.0% |
| diagnostics (`:22681`) | (T+1) × 48 B | 172.8 MB | 0 rows | 0% |
| deferred static asserts (`:22632`) | (T+1) × 32 B | 115.2 MB | 259 rows = 8 KB | ~0% |
| entities (`:22630`) | (identifiers+1) × 136 B | 108.3 MB | 69,636 rows = 9.5 MB | 8.7% |
| expression tasks (`:22594`, machine arena) | (T+1) × 28 B | 100.8 MB | stack depth only | – |
| promoted-member work (`:22599`) | (2T+1) × 12 B, plus visited u32 28.8 MB and incomplete-array chain 28.8 MB | 86.4 MB | depth only | – |
| enum members (`:22626`) | 796,683 × 104 B | 82.9 MB | 5,290 rows = 0.55 MB | 0.7% |
| members (`:22625`) | 910,274 × 64 B | 58.3 MB | 7,782 rows = 0.5 MB | 0.9% |
| alignments (`CAlignmentSpecifier`, "one record per token") | (T+1) × 12 B | 43.2 MB | **2 rows (24 B)** | ~0% |
| parameters | × 48 B | 43.8 MB | 20,301 rows | – |
| declarations | × 112 B | 30.4 MB | 13,530 rows | – |

- **None of these is zeroed**: census `zero_requested=0`, and the code uses `arena_allocate`.
- **Physical cost = written rows only**, about 30 MB across all rows. The ~1.5 GB reservation costs virtual address space and a few `mprotect` calls, nothing more.
- **The 4-byte-per-token argument behind `CAlignmentSpecifier` no longer holds.** Its comment justifies omitting a flag word because a flag "would commit four more bytes per token". Nothing per-token is physically committed here, and only 2 records are written.
- **Per-token maps that are written and resident:**
  - `identifier_use_by_token_plus_one`: 14.4 MB (zero from fresh pages, written sparsely but on nearly every page).
  - `token_classes`: 3.6 MB.
  - `matching_delimiters_plus_one`: 14.4 MB.
  - Lowering's duplicate `token_entities_plus_one`: 14.4 MB.
  - These fit inside the +86 MB semantic-model RSS step together with the written rows.
- **Sizing alone does not imply clearing or traversal.** It does only where code explicitly `memset`s, `memcpy`-seeds or loops over the capacity. In the frontend's top census sites that happens in `c_parse_member_type`, `c_parse_type_layout_core` and the per-body lowering initialisers: `prepared_call_*`/`group_type_names`, 12 B/body token, about 23 MB total, cache-resident.

## (e) Verdict

**Where the cycles are.** The frontend dominates on Ir and cycles:

- Here: 65.7% of Ir, with validation alone at 26.9%.
- 9700X at 194bd65b: 60.7% of cycles.
- By comparison, the #792/PR #833 six module-wide IR passes own 15.17% of cycles (51.6% of DRAM fills). Reordering them to function-at-a-time is bounded at ≤8.19%.

**But the frontend cost is not one representation with a clean rows × passes multiplier.**

- Token rows are touched about 62 times per token, yet they are small and cache-resident per body.
- The multiplicative cost is **re-derivation work**: types, literal values, scopes, entities and whole-type-table seeds, recomputed by each consumer over an unstructured token range.
- The three candidates above are credible and removable, with no new IR required. Together they come to roughly **8–10% of Ir** (instrumentation). Converted to cycles that is probably about 5–8%. That is comparable to, not clearly larger than, the IR ordering ceiling.
- None of them approaches the IR rows' DRAM footprint:
  - The frontend's largest RSS step is lowering (+413 MB), and that step *is* IR-row construction: 5.94 M reserved, 1.52 M written 64-B `IrInstruction` rows.
  - Semantic analysis and validation add only +149 MB.

**My verdict:**

- The frontend hypothesis **does not beat** the IR-row hypothesis as the single heavily traversed representation, because the frontend has no memory-bound representation of comparable size.
- It **does** identify the largest *compute-side* waste:
  - The expression-type fact is recomputed per query, including the checked/unchecked initializer duplicate that also materializes extra DWARF types.
  - The per-query state is heavyweight (984-B frames, 416-B snapshots).
- Recommend treating candidate 1 as the compute counterpart to the IR-row proposal, and candidates 2–3 as cheap independent slices.

**Caveats**

- The Ir came from an AVX2 rebuild under valgrind. The lexer (`BUSTER_C_LEX_COMPACT` needs `BUSTER_SIMD_512`) and quoted-string kernels run slower paths than on Zen 5, so preprocess and string shares are inflated. `rep stosb` memset is counted per byte.
- The 9700X percentages are from a different revision (194bd65b). They predate the −533 M initializer-frame change and the label-values gate.
- No timing was measured.

## Historical claims checked against current source

| Claim | Status |
|---|---|
| `CToken` is 12 B (audit 2026-08-09g's 16-B design was superseded by `CPackAlignment` spans) | Verified (`c.h`) |
| Locations are recovered on demand; per-instruction line/column is gone | Verified (`c_ir_source_range`, 105 K location calls) |
| First-error syntax diagnostic storage (2026-09-10T011438Z) | Verified: `CParserResult.diagnostics` is null until the first error. The *semantic* diagnostics array is still (T+1) × 48 B = 172.8 MB reserved and 0 written. |
| 2026-08-09c tokenizer compaction | Not applicable: it was the removed custom-language `tokenize()`. `c_lex` has its own `BUSTER_C_LEX_COMPACT` path. |
| Lowering reuses the parse delimiter index instead of rebuilding it per body | Verified (`stream_matching_delimiters_plus_one`) |
| `c_ir_unsupported_gnu_construct` walks are skipped under GNU dialects | Verified (0 reads in both phases) |
| 2026-09-23T144607Z: `c_parse_label_expression` lookups at 0.81% of cycles | Stale: now gated by `c_parse_label_values_needed` (160 lookups) |
| 2026-09-24T124205Z: the checked-type reuse prototype changed DWARF | Consistent with the file-scope duplicate query found here; not re-run |

Probe worktree `scratchpad/wt-frontend` is disposable; it was never pushed and nothing under `/home/user/buster` was modified.
