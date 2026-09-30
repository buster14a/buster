# Lifetime / ownership ledger: one native `ide cc -c` compile (stage-1 self-compile)

Scout: LIFETIME/OWNERSHIP. Revision: main `ade6ac4b6ecb21f30b61b656439bac476c145e2f`. Nothing under
/home/user/buster was modified. All numbers are **diagnostic instrumentation (counts and bytes), not timing**.

## Method and tools

* Source reading (Read/Grep/sed) of the driver, arena, preprocess, parse, lowering, IR prepare, codegen and object code.
  Every claim cites `path:symbol` (line numbers at the pinned revision).
* **gdb Python probe** (`scratchpad/lifetime/probe.py`, `probe2.py`) on `build/Release/ide` (integrator build, sha256
  5f769d8a…). Breakpoints sit at the exact entry addresses (`*symbol`) of `c_preprocess`, `c_parse_ast`,
  `c_analyze_semantics_core`, `c_lower_to_ir_with_options`, `ir_prepare_canonical_module`,
  `codegen_generate_canonical_module_with_trace`, `object_from_canonical_codegen_module`, `object_write`, and at `munmap` of the TU
  arena. At each one the probe:
  (a) finds every live arena by walking `Arena` headers in the anonymous `rw-p` VMAs;
  (b) counts resident pages in each arena from `/proc/pid/pagemap`, excluding the shared zero page;
  (c) reads `CParseResult`, `IrFunction[]`, `IrPublishedCfg`, `CSourceMapRecovery`/`IrSourceMap` and `CodegenModule` straight
  from memory to compare count × size (used) with capacity × size (handed out) and with resident pages.
  The probed run's object sha256 is 26f11f7e…, which matches the integrator's baseline, so the probe did not change the output.
* The integrator's `measurements.md`, the stage-1 allocation census (`census/stage1-report/sites.csv`) and the first residency
  report (`residency/stage1_report.txt`, used only for cross-checks, not repeated here).
* Workload: `ide cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g -c src/buster/apps/ide/ide.c`.
  3,600,849 preprocessed tokens, 5,206 function bodies, 1,524,516 IR instructions before FAST and 1,335,141 after, 21.4 MB of code.

Units: MB = 10^6 bytes. "Resident" means pages present in pagemap (first-touched, not the zero page). "Handed out" means the
arena cursor advanced. On Linux, "commit" is `mprotect` on a mapping that is already RW with MAP_NORESERVE
(`os.c:os_reserve`, `os.c:os_commit`), so it costs a syscall and nothing else. On Windows the same call is
`VirtualAlloc(MEM_COMMIT)`, a real commit charge. The census shows 5.87 GB committed over 11,417 `os_commit` calls.

---

## (a) Pipeline timeline: arenas, representations, creation, last consumer, release

### Arena inventory (single-input `ide cc -c`)

| Arena | Created | Reserve / flags | Released | Peak resident (stage-1) |
|---|---|---|---|---|
| **Result = TU arena** (one arena for everything) | `ide.c:run_c_compiler` (l.945) | 32 GiB, not pooled | `arena_destroy` at end of `run_c_compiler` → munmap, i.e. process end | **904.6 MB** |
| Spelling space | `c_source.c:c_preprocess` (l.8069) | 1 GiB, `pool_reuse` | **never** (see H1) | 35.5 MB |
| Token rows `CToken[]` | `c_preprocess` (l.8080) | 1 GiB, `pool_reuse` | **never** | 43.2 MB |
| Token shapes `CTokenShape[]` | `c_preprocess` (l.8085) | 1 GiB, `pool_reuse` | **never** | 3.6 MB |
| Type-parse machine buffer | `c_parse.c:c_analyze_semantics_core` (l.22578) | exact size, not pooled | `arena_destroy` in the same function (l.23254) → unmapped | phase-local (released correctly) |
| Lowering scratch `lowering_arena` | `c_gen.c:c_lower_to_ir_with_options` (l.50519) | 256 MiB default | `arena_destroy` (l.51051) → **parked in the thread pool with its dirty pages** (`arena.c:arena_destroy`) | **54.8 MB, never reused, held until exit** |
| Main-thread scratch ×2 | `os.c:thread_context_allocate` | 256 MiB default | rewound by `scratch_end`, never decommitted on the main thread (only resident lane workers decommit above 16 MB, `os.c:lane_persistent_worker_entry_point`) | 81.2 MB + 1.8 MB (high-water from codegen/object) |
| Lex diagnostic overflow arena | `c_source.c:c_lex_dispatch` (l.2668) | ≥64 MB, only when scratch lacks room | destroyed in the same call | transient |
| Codegen per-function MIR | scratch (`codegen.c:codegen_generate_canonical_module_attempt` `machine_scratch`, l.12159…12766) | — | `scratch_end` per function | inside scratch high-water |
| Object/ELF writer | **TU arena** (`object.c:object_from_canonical_codegen_module`, `object_write_elf64_with_capacity` l.11196) | — | with TU | 85.2 MB + 52.2 MB |

The 64 MB arena in `object.c` (l.9326) is used only by the fuzzer. Nothing on the compile path creates its own object arena.

Single-input compiles keep **every** stage in one arena. `driver.c:compiler_driver_execute_invocation` sets
`unit_in_result_arena = invocation.input_count == 1` (l.4424) and passes the caller's arena, which is `run_c_compiler`'s, straight to
`compiler_driver_execute_c_single`. That function allocates preprocess, parse, analysis, IR, codegen, the object and the
ELF image from that same `arena`. The TU arena is therefore also the result arena, and it lives until `run_c_compiler` has printed its metrics.

### Phase timeline (resident = pagemap, handed = cursor delta)

| Phase (entry → next entry) | TU handed | TU resident at end of run | Other arenas | VmRSS at phase end (KiB) |
|---|---:|---:|---|---:|
| `c_preprocess` | 1,048.3 MB | 163.2 MB | +82.3 MB (spelling/token/shape) | 245,496 |
| `c_parse_ast` | 15.8 MB | 1.4 MB | — | 246,996 |
| `c_analyze_semantics_core` (semantic analysis) | 1,439.8 MB | 147.3 MB | machine buffer (unmapped) | 395,968 |
| `c_lower_to_ir_with_options` (lowering) | 1,031.6 MB | 363.4 MB | +54.8 MB pooled lowering arena, +5.0 MB scratch | 809,512 |
| `ir_prepare_canonical_module` (promote, FAST, CFG publish) | 27.6 MB | 27.6 MB | — | 836,608 |
| codegen prelude (to 2nd `ir_prepare`) | 5.1 MB | 5.1 MB | — | 860,020 |
| codegen attempt | 1,501.6 MB | 59.3 MB | +70.4 MB scratch high-water | 989,072 |
| `object_from_canonical_codegen_module` | 231.2 MB | 85.2 MB | +1.8 MB scratch | 1,072,396 |
| `object_write` + `file_publish` | 63.2 MB | 52.2 MB | — | 1,123,416 |
| **Total** | **5,364 MB** | **904.6 MB** | ~222 MB | peak 1,124,828 (integrator) |

The ~562 MB that `c_analyze_with_options` adds (integrator's figure) breaks down as: TU semantic interval 147.3 MB
(35,968 pages), TU lowering interval 363.4 MB (88,721 pages), pooled lowering arena 54.8 MB, scratch 10.8 MB.
Together that is 576.3 MB. The integrator's VmRSS delta is 562,504 KiB = 576.0 MB, so the two reconcile.

### Representation ledger: holder and last real consumer

| Representation | Holder | Created by | **Last reader** (success path) | Released |
|---|---|---|---|---|
| Per-file raw lex `CLexResult.tokens/token_shapes` (capacity = bytes+17 per file) | TU | `c_source.c:c_lex_dispatch` (l.2651-2652) via the include path `c_preprocess` (l.9022) | the `c_preprocess` main loop (`source_frame->lex`); frames are popped at EOF (l.8727-8757) | never (TU) |
| Per-file class masks | TU | `c_source.c:c_pp_class_masks_build` | `c_preprocess` loop | never |
| Macro table, expansion node lists, stamps, include table | TU | `c_preprocess` (`c_macro_define`, `c_preprocess_expand`, `CPpStampTable`) | `c_preprocess` return | never |
| Checkpoints and offsets per TEXT region (capacity = bytes+2 per file) | TU | `c_source.c:c_translate_source` (l.647-648) | **debug info and diagnostics**: `codegen.c` line marks (l.9487, 11651, 13153), `object.c:object_from_canonical_codegen_module` (l.10613), `debug.c:debug_source_from_ir`, `driver_diagnostic.c` | never |
| Source map `IrSourceMap` (keys/regions/pages) | TU | `c_source.c:c_source_map_append/publish`. `c_gen.c:c_lower_to_ir_with_options` copies it by value into `program->source_map` | same as the checkpoints (via `ir_source_position`) | never |
| Spelling space (translated text of all 383 lexed files plus expansion spellings, 35.5 MB) | private 1 GiB | `c_preprocess` | **`object_write`**. IR names are slices into it: `CDeclaration.name`, `IrSymbol.name`, `IrFunction.name`, `IrDebugLocal.name`. `c_gen.c:c_ir_space_name_token` asserts `name.pointer >= spelling_base`. They reach ELF strtab and DWARF strings. | **never** |
| Preprocessed token rows (43.2 MB) and shapes (3.6 MB) | private 1 GiB ×2 | `c_preprocess` (`CTokenStream`) | **`c_lower_to_ir_with_options`** (per-body token scans, `c_preprocess_token_location`). No backend file references `CToken`/`spelling_base`/`CParseResult` (grep over ir/, codegen/, object/, dwarf/, debug/). Driver diagnostics read only `preprocess->files` and `recovery->map` (`driver_diagnostic.c:compiler_driver_c_diagnostic`). | **never** |
| `CSymbolTable` | TU | `c_preprocess` (`c_symbol_table_create`, `c_symbol_intern`) | `c_lower_to_ir_with_options` | never |
| `CParserResult` (declaration list) and `delimiter_stack` | TU | `c_parse.c:c_parse_ast` (l.16919) | `c_analyze_semantics_core` (the stack: `c_parse_ast` itself) | never |
| **`CParseResult`** (types, entities, scopes, identifier uses, per-token tables, position index) | TU | `c_parse.c:c_analyze_semantics_core` (l.22594-22681), `c_parse_position_index_build` (l.452-454) | **`c_lower_to_ir_with_options`**. It is a local of `c_analyze_with_options` and is never returned (`c_parse.c:c_analyze_with_options` l.23274). | never |
| Lowering-only tables (`token_entities_plus_one`, `token_function_declarations_plus_one`, `c_type_ir_map`, `entity_symbols`, cleanup tables, array-type cache) | TU (not scratch) | `c_gen.c:c_lower_to_ir_with_options` (l.48436, 48612, 49871, …) | `c_lower_to_ir_with_options` | never |
| Per-function lowering state (frames, locals, prepared calls) | `lowering_arena` | l.50583-50760 | the same function iteration | rewound per function. The arena is then pooled (see above). |
| **Canonical IR rows** (`IrInstruction` 64 B, `IrValue` 16 B, `IrBlock` 64 B, `IrSourceRange` 12 B; capacities from body tokens) | TU | `c_gen.c:c_lower_to_ir_with_options` (l.50653-50663) | **codegen of that function** (`codegen_generate_canonical_module_attempt`). After codegen, `object_from_canonical_codegen_module` reads only `function->symbol/source`, `debug_locals`, symbols and types. | never |
| Published CFG (blocks, edges, params, args, pools, `instruction_remap`) | TU (`program->arena`) | `ir_fast.c:ir_prepare_canonical_module` → `ir_cfg.c:ir_function_publish_cfg` | codegen selection/emission (`ir_function_cfg_edge`). **`instruction_remap` has no production reader** (only `tests/compiler/ir/ir_cfg_test.c`, `ir_fast_test.c`). | never |
| MIR (selected/scheduled/allocated) | main-thread scratch | `machine_select_*`, `machine_encode_*` | the same function | `scratch_end` per function (not duplicated long-term) |
| Codegen per-function side arrays (`value_offsets`, `aligned_local_offsets`, `direct_call_uses`, `block_offsets`, `branch_patches`) | **TU** | `codegen.c` l.11711, 11859, 13056-13057 | the same function | never |
| Code buffer (21.4 MB used of 64.6 MB) | TU | `codegen.c` l.11592 | **`object.c:object_from_canonical_codegen_module` memcpy into `.text`** (l.10462-10465) | never |
| Line entries (621,321 × 12 B), debug location seeds (187,989 × 56 B), relocations (57,343 × 24 B) | TU | `codegen.c` l.11607-11608, 11527 | `object_from_…` / `dwarf_build_model` / `debug_model_build` (`lines` aliases `module->line_entries`, `object.c` l.10627) | never |
| `ObjectFile` sections (text copy, rodata alias, DWARF sections) | TU | `object_from_canonical_codegen_module`, `dwarf_build_model` | `object_write` | never |
| ELF image (51.9 MB file, 63.0 MB capacity) | TU | `object.c:object_write_elf64_with_capacity` | `file_publish` (inlined in `compiler_driver_emit_object_output`) | never |
| Driver result metrics (`lexed_files` rows, paths), warnings, fallback records | TU | driver | `run_c_compiler` after execution returns (report and `-fsource-metrics`) | `run_c_compiler` end |

**Q1 answer.** The token array, spelling space and parse results all stay resident through IR prepare, codegen and object
writing. Token rows and shapes (46.8 MB, private arenas) and `CParseResult` (TU) have their last reader in
`c_lower_to_ir_with_options`. The spelling space's last reader is `object_write` (names only), and the source map's is debug-info
emission. Neither debug info nor diagnostics read tokens after lowering.

**Q2 answer.** Yes. Every function's IR rows, CFG and operand pools stay resident until process exit, while later functions are
code-generated and through object, DWARF and ELF writing. That residence is required for as long as
`codegen_generate_canonical_module_with_trace` keeps its whole-module retry (codegen.c l.23763-23786), because a retry re-reads every
function. MIR is per function and discarded, so machine IR is not kept next to IR. Duplication does happen in the IR itself:
old operand arrays are abandoned when FAST compaction (`ir_promote.c:ir_rewrite_compact` l.569) or CFG publication
(`ir_cfg.c:ir_cfg_pool_operands`, when rows are not contiguous) copies them into a new pool in the TU arena.

---

## (b) Retained beyond last use, and over-capacity (stage-1 bytes)

### b1. Resident storage whose last reader has already run (all held until exit)

| # | Item | Last reader | Resident bytes | How derived |
|---|---|---|---:|---|
| 1 | Preprocessed token rows and shapes (private arenas) | `c_lower_to_ir_with_options` | **46.8 MB** | pagemap of the two 1 GiB arenas (10,550 + 880 pages) |
| 2 | `CParseResult` and everything else touched during semantic analysis | `c_lower_to_ir_with_options` | **147.3 MB** | pagemap of the TU interval [`c_analyze_semantics_core` entry, `c_lower_to_ir_with_options` entry). The 20 named arrays account for 46.4 MB of it (table below). The rest, per census and residency sites: `c_parse_position_index_build` (13.7 MB), `c_parse_validate_lowering_constraints`, type layout, scope-children and member tables. |
| 3 | `c_parse_ast` results | `c_analyze_semantics_core` | 1.4 MB | pagemap interval |
| 4 | Raw per-file lexes, class masks, macro tables and expansion nodes (the preprocess interval minus the live source map, checkpoints, paths and metrics) | `c_preprocess` return | **≈140 MB** of 163.2 MB | pagemap interval 163.2 MB minus live items: regions 84,886 × 80 B (6.8 MB, capacity 10.5 MB) plus keys (~1 MB), pages (34,668 × 4 B) and checkpoints 417,071 × 16 B (6.7 MB) plus checkpoint pages 253,500 × 4 B (1.0 MB), about 16-20 MB (probe2). Residency cross-check: `c_lex_dispatch` raw tokens alone are 44.4 MB resident, and `c_source_map_append` is 17.2 MB resident for about 7-10 MB live, because the growth copies are abandoned. |
| 5 | Pooled `lowering_arena` | end of `c_lower_to_ir_with_options` | **54.8 MB** | pagemap (13,381 pages; `pos=64`, `dirty=84.3 MB` after destroy). No later default-size `arena_create` reuses it in this compile. |
| 6 | Lowering-only TU tables (per-token u32 maps and others) | `c_lower_to_ir_with_options` | ≤ 119.8 MB (bucket upper bound) | Lowering interval 363.4 MB minus the IR-array pages (53,070 live + 4,974 beyond-count = 237.7 MB at `ir_prepare` entry). This bucket also holds live IR (types, symbols, globals, operands, extras, debug locals). Known dead members: `token_entities_plus_one` (9.7 MB resident per residency) and `token_function_declarations_plus_one` (14.4 MB handed). |
| 7 | IR rows written, then dropped by SSA-finish and FAST compaction (rows beyond final counts) | before codegen | **37.4 MB** | pagemap pages overlapping no used prefix (9,131 pages at codegen entry, up from 4,974 at `ir_prepare` entry) |
| 8 | Live IR rows (instructions, values, blocks, sources) of functions already emitted | codegen of that function | 130.3 MB of rows in **200.4 MB** of resident pages | count × size vs pages overlapping used prefixes (48,918 pages) |
| 9 | Published CFG bytes | codegen | 21.2 MB (`cfg->allocated_bytes` sum), of which **`instruction_remap` 5.2 MB is never read** (1,299,452 rows over 4,054 functions) | probe over `IrPublishedCfg` |
| 10 | FAST abandoned and re-pooled operand storage | FAST / publish | ≤ 18.0 MB | driver `-v` `IR_FAST retained_bound=18011220` (`ir_fast.c:ir_fast_function`) |
| 11 | Codegen code buffer after the `.text` copy; object sections after `object_write` | `object_from…` / `object_write` | 21.4 MB (code) plus the ELF input sections | `CodegenModule.code.length`, `object.c` l.10462 |

**Sum of firm dead-but-resident storage** (items 1-5, 7, 9-remap): about 47 + 147 + 1 + 140 + 55 + 37 + 5 = **≈432 MB of the
1.12 GB peak**, before counting item 6's dead share or item 8 (IR of already-emitted functions, ~200 MB of pages).

For throughput this matters as **fresh first-touch faults, not RSS**. Codegen, object and write touched 48,014 new TU pages plus
about 17,000 new scratch pages: roughly 65,000 of the 282,800 minor faults (≈23%). Those could have been served by recycling the
dead frontend pages above, if frontend and backend storage lived in separate arenas. This is an estimate from page counts, with no timing claim.

### b2. Semantic arrays (`CParseResult`, read at `c_lower_to_ir_with_options` entry)

| Array | Count | Capacity (sizing) | Handed | Used | Resident |
|---|---:|---|---:|---:|---:|
| types (88 B) | 147,264 | 7,201,701 (tokens × 2 + 1) | 633.7 MB | 13.0 MB | 13.0 MB |
| diagnostics (48 B) | 0 | 3,600,851 (tokens + 1) | 172.8 MB | 0 | 0 |
| deferred_static_asserts (32 B) | 259 | tokens + 1 | 115.2 MB | 8 KB | 0 |
| entities (136 B) | 69,636 | identifiers + 1 | 108.3 MB | 9.5 MB | 9.5 MB |
| enum_members (104 B) | 5,290 | identifiers + 1 | 82.9 MB | 0.55 MB | 0.6 MB |
| members (64 B) | 7,782 | identifiers + ; + 1 | 58.3 MB | 0.5 MB | 0.5 MB |
| parameters (48 B) | 20,301 | , + ( + 1 | 43.8 MB | 1.0 MB | 1.0 MB |
| alignments (12 B) | 2 | tokens + 1 | 43.2 MB | 24 B | 0 |
| declarations (112 B) | 13,530 | ; + { + list-commas + 1 | 30.4 MB | 1.5 MB | 1.5 MB |
| identifier_use_by_token_plus_one (u32, **zeroed**) | — | tokens | 14.4 MB | — | 9.4 MB |
| identifier_uses, scopes, token_classes, buckets, others | | | 34 MB | | 10.9 MB |
| **Total** | | | **1,337 MB** | ≤ 234 MB | **46.4 MB** |

### b3. IR, codegen and preprocess over-capacity (handed out vs used)

| Allocation | Sizing rule | Handed | Used |
|---|---|---:|---:|
| `IrFunction.instructions` (`c_gen.c` l.50655) | body_tokens × 3 + params × 4 + 16 | 380.4 MB | 97.6 MB before FAST, 85.4 MB at codegen |
| `IrFunction.blocks` (l.50653) | body_tokens + 8 | 126.3 MB | 10.8 MB |
| `IrFunction.values` (l.50663) | same as instructions | 95.3 MB | 21.0 → 18.0 MB |
| `instruction_canonical_sources` (l.50661) | same as instructions | 71.3 MB | 18.3 → 16.0 MB |
| Per-file lex tokens and shapes (`c_source.c` l.2651-2652) | translated bytes + 17 | 432.7 MB | ≈47 MB |
| Checkpoints and offsets (`c_source.c` l.647-648) | bytes + 2 | 526 MB | 6.7 MB |
| `debug_locations` (`codegen.c` l.11608) | Σ locals × (blocks + 1) | **1,305.6 MB** | 10.5 MB (187,989 seeds) |
| relocations (l.11527) | instructions × 3 + … | 96.3 MB | 1.4 MB |
| code buffer (l.11592) | instructions × 48 + … | 64.6 MB | 21.4 MB |
| line entries (l.11607) | instructions + functions | 16.1 MB | 7.5 MB |

On Linux the cost of over-capacity is address space plus one `mprotect` per 64 KB granule step. The measurable RSS effect is **page
fragmentation**. The per-function IR arrays sit back-to-back at capacity, so each used prefix ends on a partly used page, and the
next array begins on a fresh page. Result: **200.4 MB resident for 130.3 MB of live rows** (+54%). On Windows the whole cursor is
committed charge.

**Zeroing (Q3):** dirty-prefix zeroing is negligible. The census records 117.4 MB of `zero_requested` but only **4.2 MB
`zero_written`**, all of it in `object.c:object_symbol_name_index_build` (l.9516). The large zeroed per-token tables
(`c_gen.c` l.48436, 48612, 49871; `c_parse.c` l.452, 22679) land on fresh pages. Two TU-arena rewinds raise the dirty
watermark: `c_source.c:c_source_map_sort` (`arena_begin_temporal(arena)`, l.289) and `c_gen.c` value-constant evaluation
(`arena_begin_temporal(builder->arena)`, l.21819). Every later allocation sits above that watermark, so no memset follows. Any
proposal that recycles dead pages turns zeroed allocations into real memsets, and must count that cost.

---

## (c) Ownership hazards on error and retry paths

* **H1: the preprocess private arenas are never destroyed (leak in multi-input compiles).** `c_preprocess` creates three 1 GiB
  `pool_reuse` arenas and records them in `CSourceMapRecovery` (c.h: "a caller compiling many units may destroy it"). The driver
  never destroys them, anywhere in driver.c. Only tests, `ide.c:compiler_run_benchmarks` and `selection_benchmark.h` do. In
  multi-input builds, `compiler_driver_execute_invocation` destroys `unit_arena` (l.4582, 4620, 4700) and the lanes' `unit->arena`,
  but each TU's spelling, token and shape arenas stay mapped and resident (≈82 MB per stage-1-sized TU) until exit, and they never
  reach the pool. The copy-out at driver.c l.4630-4695 duplicates every name the linker needs, so destroying the three arenas right
  after `arena_destroy(unit_arena)` would be safe.
* **H2: the codegen whole-module retry rewinds the TU arena.** `codegen_generate_canonical_module_with_trace` opens
  `attempt_scope = arena_begin_temporal(arena)` on the **result/TU arena** and calls `scratch_end(attempt_scope)` when the code
  buffer runs out (l.23763-23786).
  * Everything allocated in the TU arena inside an attempt must be unreachable from outside it. `machine.c:machine_select_canonical_function_internal`
    (l.5373/5387) calls `ir_function_publish_cfg(program->arena, …)` and, when `!assume_validated`, `ir_function_invalidate_cfg`.
    Both allocate in the same TU arena inside the attempt. After a rewind, `function->published_cfg` or the rebuilt builder links
    would dangle.
  * This is latent in the driver: `ir_prepare_canonical_module` publishes every CFG before the scope opens, and the driver passes
    `.assume_validated = true`. It is reachable through `machine_select_canonical_function`, the non-validated entry.
  * A retry repeats all selection, allocation and encoding, and doubles every per-attempt reserve. The next attempt would reserve
    2.6 GB for `debug_locations` alone.
  * Stage-1 needed one attempt: census `calls = 1` at `codegen.c` l.11592/11608.
  * This retry is also the constraint that forces whole-module IR residency (Q2).
* **H3: TU-arena temporal scopes in the frontend.** `c_source.c:c_source_map_sort` and the `c_gen.c` value-constant evaluator
  (l.21819) rewind the long-lived TU arena. Any persistent allocation made inside those scopes, such as IR row growth, type interning
  into `program->types` or `string_format` into the TU arena, would silently be reused. The scopes appear to hold only temporaries,
  but I did not audit every callee inside the evaluator.
* **H4: rejected function lowering** (`c_gen.c:c_lower_to_ir_with_options`, failure branch l.50880-50899). `scratch_end(lowering_temporary)`
  reclaims the lowering scratch. The function's TU arrays, sized by body tokens (up to ~156 B of handed-out space per body token),
  and any operands, types or strings already appended are abandoned, and `IrFunction` still points at them (state `REJECTED`). The
  compile then fails with diagnostics. There is no retry, so this is harmless today, but any "continue after rejection" mode would
  keep all of it.
* **H5: pool retention.** `arena_destroy` parks default-shaped arenas (256 MiB) with their faulted pages
  (`arena.c:arena_destroy`, `ARENA_POOL_LIMIT` 16). They are unmapped only by `arena_pool_release_thread` at thread teardown. This is
  correct, since the dirty watermark is carried, so zeroed allocations stay correct. It pins 54.8 MB for a single-TU compile that
  never reuses the arena.
* **H6: error paths in `compiler_driver_execute_c_single`.** Every `goto end` leaves all storage in the result arena. Codegen and
  object failure diagnostics read `lowered.program`: the source map via `ir_source_position` and `ir_source_map_original_position`,
  function names and sources (`compiler_driver_backend_location/_context`, l.3772-3870). So an early release of the program or the
  source map would have to wait until the error is reported.
* **H7: main-thread scratch high-water.** Scratch reaches 81.2 MB during codegen and is never decommitted on the main thread,
  unlike resident lane workers. This is reuse capacity, not a leak.

---

## (d) Constraints any lifetime-shortening proposal must respect

1. **AGENTS.md: "Retain the translation-unit arena until every downstream consumer finishes."** In the single-input path, the
   TU arena is the result arena. Its downstream consumers are, in order:
   * `ir_prepare_canonical_module` (allocates in `program->arena`, which is the TU arena);
   * codegen: reads IR and the source map, and allocates its outputs in the TU arena;
   * `object_from_canonical_codegen_module`, `dwarf_build_model` and `debug_model_build`: IR types, symbols, sources, source map,
     function source, `debug_locals`, and names that are spelling slices;
   * `object_write` and `file_publish`;
   * driver backend diagnostics on failure (H6);
   * `run_c_compiler` after execution returns: `compile.warning`, `diagnostic`, `output`, `lexed_files` rows and paths for
     `-v`/`-fsource-metrics`, and fallback-census strings.

   The multi-input path shows the approved way to shorten the lifetime: deep-copy the `ObjectFile` and its names into the result
   arena, then destroy the unit arena (driver.c l.4630-4700).
2. **Live-object interleaving.** Dead and live objects share one bump arena. Examples: source-map checkpoints from
   `c_translate_source` sit next to dead raw lex tokens, and IR types, symbols and strings sit next to dead lowering tables. No prefix
   or suffix can be released. Shortening lifetimes requires **segregated arenas**: a frontend-scratch TU arena, and a
   program/output arena that holds the IR program, source map, checkpoints, paths and names. It also requires copying or interning
   the IR-referenced names out of the 35.5 MB spelling space if that space is to be dropped. The minimum retention set after
   lowering is the source map plus checkpoints (~16-20 MB), identifier names, and the IR program.
3. **Canonical-IR boundary.** IR must stay independent of frontend IDs and AST pointers, and must be validated before backend
   consumption. The existing `c_ir_space_name_token` assertion shows that IR names are pointers into the spelling space. Replacing
   them with owned copies must keep byte-identical symbol, DWARF and strtab output and the self-host fixed point.
4. **Per-function IR release** is blocked by the whole-module codegen retry (H2). Object and debug info also still need
   `IrFunction.source`, `debug_locals`, symbols and types after codegen, so only rows, values, blocks, sources and CFG could be
   freed per function.
5. **Arena contracts.** Rewinding or recycling makes pages dirty, so `arena_allocate_zeroed` will memset them. The pool hands out
   dirty pages only to default-size or `pool_reuse` arenas. Windows commit accounting differs from Linux.
6. **Parallel and determinism rules.** Unit lanes create each TU arena on a worker (`compiler_driver_unit_lane`), and workers pool
   per thread. Worker scratch is decommitted above 16 MB per dispatch. Output must remain byte-identical and merged in input order.
7. **Diagnostics.** Invalid source must still yield structured diagnostics. Anything a failure path reads must outlive that path:
   `preprocess->files` and `recovery->map` for C diagnostics, and `program` plus the source map for backend diagnostics.

## Verified vs. not verified

* **Verified in current source or by measurement:** single-arena ownership; the never-destroyed preprocess arenas; the pooled
  lowering arena; whole-module IR residency and the retry mechanics; that `instruction_remap` has no production reader; the text
  copy in `object_from_…`; every count, capacity and resident figure above; zero-write totals.
* **Not verified:** the exact dead share inside bucket 6; whether the evaluator scope in H3 ever allocates persistent data; H2 being
  reachable in a production path; a codegen retry, which never triggered on stage-1. Site-level residency is being produced
  separately by the integrator (`residency/`) and is not repeated here.
