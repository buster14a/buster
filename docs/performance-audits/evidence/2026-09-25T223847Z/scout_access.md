# ACCESS-PATTERN scout: how one native `ide cc -c` traverses its representations

Revision: main ade6ac4b (tree 4c530622). Workload: stage-1 self-compile
(`ide cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g -v -c src/buster/apps/ide/ide.c`),
x86-64 ELF, default `-fregister-allocator=fast` (driver.c:854), FAST passes on (driver.c:858).
Mid-size cross-check: `src/buster/lib/compiler/object/object.c` (130,673 tokens, 66k IR rows, 190 functions).

All numbers are **instrumentation** (deterministic element counts / sizes / census bytes). Nothing here is a
timing or a speedup claim.

Tools used: Read/Grep over source; `clang` probe program that prints `sizeof`/`offsetof` for the real headers
(`access/sizes.c`); a counter probe patched into a scratch worktree (`wt-access`, patch confined to
`ir.c`/`ir_cfg.c`/`ir_promote.c`/`ir_fast.c`, built with the exact Release flags except `-march=x86-64-v3`) and
run natively once on stage-1 and once on object.c (outputs `access/probe_stage1*.txt`, `access/probe_object.txt`);
`ide -v` statistics; the integrator's census (`census/stage1-report/sites.csv`) and `measurements.md`; valgrind
callgrind `--cache-sim=yes` on object.c with an unmodified `-march=x86-64-v3` build (section 6; the trusted
AVX-512 binary cannot run under valgrind 3.22).

Headline: the `IrInstruction` row array is swept ~29 times per stage-1 compile, 8 of them module-wide; most
sweeps read 4-13 of the 64 bytes; operand arrays are materialized three times and immediates/targets are copied
at publication; publication permutes 84% of rows in place and allocates a 5.2 MB remap nobody reads; and in a
cache-sim where the IR exceeds LL, the prepare sweeps are 7.9% of instructions but 41% of LL read misses, with
the `next`-only ownership walk costing exactly one LL miss per row visited.

Important configuration fact (verified): the trusted `build/Release/ide` is compiled with
`-DBUSTER_INCLUDE_TESTS=1 -DBUSTER_OPTIMIZE=1` (compile_commands.json), so `BUSTER_IR_TRANSFORM_CHECKS`
(ir.h:18) is **1** and `ir_prepare_canonical_module` (ir_fast.c) runs **two** full-module validations on stage-1
(promotion output, because 193 locals were promoted; FAST output, because FAST changed rows). Probe:
`validate_calls=2 validate_rows=2,858,522` (= 1,523,381 + 1,335,141). A tests-OFF compiler (the self-built stage)
runs one (the FAST input guard), as the 2026-09-13 census shows (`preparation_fast_input_validations=1`).

---------------------------------------------------------------------------------------------------------------

## 0. Pipeline order and population (stage-1, probe + `-v`)

| stage (function)                                   | IR rows in | rows out | scope                    |
|----------------------------------------------------|-----------:|---------:|--------------------------|
| `c_lower_to_ir_with_options` -> per-function lowering + `c_ir_ssa_finish` (c_gen.c) | built | 1,524,516 | function-at-a-time |
| `ir_promote_function` x all (ir_promote.c)          | 1,524,516 | 1,523,381 | module loop             |
| `ir_validate_canonical_module` #1 (promotion output) | 1,523,381 | same    | module-wide              |
| `ir_fast_function` x 5,206 (ir_fast.c) + `ir_rewrite_compact` x 4,817 | 1,523,381 | 1,335,141 | module loop |
| `ir_validate_canonical_module` #2 (FAST output)     | 1,335,141 | same     | module-wide              |
| `ir_function_publish_cfg` x 5,206 (ir_cfg.c)        | 1,335,141 | same     | module loop              |
| `codegen_generate_canonical_module_attempt` (codegen.c) -> `machine_select_canonical_function_x86_64` (machine_x86_64.c) -> `machine_fast_placement_build` -> `machine_encode_x86_64` | 1,335,141 | MIR | function-at-a-time |
| `object_from_canonical_codegen_module` (object.c)   | CodegenModule | ObjectFile | module              |

Row shape after publication (stage-1 probe): 1,092,638 rows carry a result (82%), 835,410 carry operands (63%),
481,368 carry immediates (36%), 153,879 carry targets (12%). Opcode mix: CONSTANT_INTEGER 322,775 (24%),
BINARY 150,428, LOAD 129,799, FIELD 110,740, CAST 97,732, BRANCH 77,085, BRANCH_IF 76,477, STORE 65,509,
CALL 41,137, FUNCTION 41,136, DEREFERENCE 40,778, INDEX 34,825, AGGREGATE 32,531, LOCAL 32,057, ADDRESS_OF 25,107,
ARGUMENT 14,807, RETURN 14,761, UNARY 12,828, GLOBAL 9,686; atomics 5 rows total, SIMD 33, inline asm 2.

---------------------------------------------------------------------------------------------------------------

## (a) Per-representation access ledger

Sizes are `sizeof` from the probe program against the real headers (x86-64); CT_CHECK-backed ones marked (CT).

| representation | elem size | stage-1 elements (bytes used) | allocated where / how | writers | readers (hot traversals) | access pattern | full passes |
|---|---:|---|---|---|---|---|---|
| `IrInstruction` rows (ir.h) | 64 (CT, ir.h:547) | 1.52M pre-FAST, 1.34M published (85-98 MB used); **5,943,429 rows reserved** (380 MB handed out, census c_gen.c:50655) | one array per function from the TU arena, capacity `body_tokens*3+params*4+16` (c_gen.c `c_lower_to_ir_with_options`); `arena_allocate` aligns to `alignof`=8, so **4,512 of 5,206 arrays (87%) are not 64-B aligned** (probe `rows_base_unaligned64`) and each row then straddles two lines | `ir_instruction_append_trusted` (ir_append.h), `c_ir_ssa_finish`, `ir_fast_fold` (in place), `ir_rewrite_compact` (full row copy), `ir_cfg_publish_instruction_rows` (in-place permutation + `next` rewrite) | see matrix (b): ~29 row sweeps | sequential by index, or by `next` chain (pre-publication), or by `values[v].definition` (random-ish, 76% within 8 rows), or block spans (post-publication) | ~29 (section b) |
| operand arrays `IrValueId[]` | 4 | 1,297,317 slots (5.2 MB) published | **materialized 3x**: (1) per-instruction `arena_allocate(builder->arena, IrValueId, n)` at ~90 c_gen sites, interleaved with immediates/targets/incoming nodes; (2) re-pooled densely by `c_ir_ssa_finish` (c_gen.c:5964, census 5.85 MB); (3) re-pooled again by `ir_rewrite_compact` for 4,817/5,206 functions (ir_promote.c:569, census 5.18 MB). Publication then finds them contiguous in 5,163/5,206 functions and copies only 588 slots | c_gen, `c_ir_ssa_finish`, `ir_rewrite_compact`, `ir_fast_fold` (nulls them when folding to constant) | every pass that checks/uses operands: `c_ir_ssa_finish` x2, validation x2, `ir_fast_dce`, `ir_fast_fold`, compaction, selector use-walk + main selection, promotion event walk | dependent load `row->operands[i]`; pool is sequential **in pre-publication row order**; publication then permutes 84% of rows but not the pool | ~9 derefs of every operand |
| immediates `u64[]` | 8 | 563,952 slots (4.5 MB) | per-instruction `arena_allocate` in c_gen (+ `ir_fast_fold` allocates 128,807 one-slot arrays, census ir_fast.c:293 = 1.03 MB); scattered: in index order only 118k/476k consecutive pairs are adjacent, 97k go backwards, 78k are >4 KiB apart (probe `im_gap_*`) | c_gen, `ir_fast_fold` | validation (ARGUMENT/CONSTANT/FIELD/...), `ir_fast_constant` (by definition of each operand of CAST/UNARY/BINARY), selector | pointer chase from row to a scattered 8-B payload until publication; publication copies 563,697 slots into a pool (census ir_cfg.c:205, 4.51 MB) and rewrites 481k row pointers | ~5 |
| targets `IrBlockId[]` | 4 | 235,006 slots (0.94 MB) | per-terminator `arena_allocate` in c_gen | c_gen | `c_ir_ssa_finish` (terminators), `ir_promote_cfg`, validation, publish (x2 terminator walks), selector | terminator-only; publication copies 234,499 slots (census ir_cfg.c:204) | ~6 (terminators only) |
| `instruction_canonical_sources` `IrSourceRange[]` | 12 (CT) | 1.34M (16 MB); 5.94M reserved (71 MB handed out, c_gen.c:50661) | dense parallel array per function | append, compaction (copy), publication permutation (copy) | `codegen_record_machine_line_marks` (codegen.c) only, once, sequential per mark; `ir_source_position` memoized per distinct range | sequential | 1 read, 3 writes |
| `IrValue` (ir.h) | 16 (CT) | 1,315,524 -> 1,126,637 (18-21 MB); 5.95M reserved (95 MB handed out) | per-function array, same reserve rule | c_gen, `c_ir_ssa_finish` (compaction), `ir_rewrite_compact`, publication (definition remap) | validation (`ir_validate_function_values` sweep + per-operand random lookups), FAST (`values[first]`), selector (`values[used].definition`, `values[result]`), codegen capacity pre-pass (sweep) | sweep + random-by-id; 87.5% of operand->result value-id distances are <=64 (probe) | ~8 sweeps + per-operand lookups |
| `IrBlock` | 64 | 150k blocks; `body_tokens+8` reserved per function (census c_gen.c:50653 = 126 MB handed out) | per-function array | c_gen | chain heads for every pre-publication walk; `published_cfg->blocks` replace most post-publication | indexed | small |
| `IrBlockParameter` 40 / `IrIncoming` 16 / `IrPredecessor` 16 | | 76,331 incoming nodes surviving; 459,618 parameters created (census c_gen.c:5126, 18 MB) | one-node-at-a-time `arena_allocate` in `c_ir_ssa_*` | c_gen, FAST/promotion parameter sweeps | validation (builder form), `ir_function_publish_cfg` (transposes into `IrCfgParameter`/`arguments`), FAST parameter sweeps | linked lists, **but physically adjacent**: every `incoming->next` of the 43,020 multi-node chains is within 64 B (probe `incoming_next_gap_gt64=0`) | ~5, tiny volume |
| `IrPublishedCfg` blocks 32 / edges 12 / params 12 / arguments 4 / `predecessors` u32 | | 5,206 CFGs, 208k edges | built once per function by `ir_function_publish_cfg` | publish | selector (block spans, `parameters`, `arguments`), codegen, validation (`ir_validate_block_parameters` published form, not on the stage-1 path) | dense, indexed | 1-2 |
| `IrPublishedCfg.instruction_remap` / `operand_pool` / `target_pool` / `immediate_pool` | 4 / ptr | remap allocated for 4,054 functions: 5,197,808 B (census ir_cfg.c:312) | publish | publish | **no production reader** (grep: only ir_cfg_test.c / ir_fast_test.c read these fields); consumers use `row->operands/targets/immediates`, which publication redirected into the pools | write-only | 0 reads |
| `IrType` (model.h) | **128** (2 lines; table base not 64-aligned) | 51,921 types (6.6 MB) | `ir_program_add_type` into one table | frontend | c_gen lowering (463 `ir_type_from_id` call sites in c_gen.c), `ir_type_from_id` per row in validation (x2) and per candidate in FAST (`ir_fast_width`, `ir_instruction_is_pure`), `ir_local_type_promotable`; codegen mostly uses projections (`CodegenSlotCost`, `MachineTypeClass` 4 B, CT machine_select.h:185) | random by id, strongly skewed to few types | per-row lookups in ~3 passes |
| `CToken` (c.h) | 12 (CT) | lexed 3,231,860 (38.8 MB, per-file `CLexResult`), final stream 3,600,849 (43.2 MB) + 1-B shape sidecar | lexer writes per-file arrays; `c_preprocess` **copies** each text-line segment with `memcpy` into `CTokenStream` (c_source.c ~L9314) | lexer, preprocessor | parse (`c_parse_ast`, census, binder), `c_lower_to_ir_with_options` (per-body capacity scan, then the lowering machine re-walks token ranges: 878 `preprocess.tokens[...]` sites, 249 `c_token_spelling` sites) | sequential scans + ranged re-walks; spelling via `spelling_base+offset` | lex result: 2 (write, copy-read); stream: >=4 (frontend scout owns detail) |
| per-token side arrays (`identifier_use_by_token_plus_one` u32, `token_classes` u8, `matching_delimiters_plus_one` u32, `token_entities_plus_one` u32) | 1-4 | 3.6M each | parse arena | parse/analysis | lowering lookups by token index | indexed by token | frontend scout |
| `CType` 88 / `CEntity` 136 / `CScope` 24 / `CDeclaration` 112 | | TU tables | parse arena | parse | lowering via `c_type_ir_map[...]` (92 sites; `CTypeId -> IrTypeId`) and `parse.entities[...]` (118 sites) | random by id | frontend scout |
| `MachineInstruction` (machine.h) | 24 (CT) | per function, scratch arena rewound per function | per-function `machine_scratch` (codegen.c `machine_attempt`) | selector | `machine_function_compact_virtual_registers`, `machine_fast_placement_build`, `machine_encode_x86_64`, line marks, debug locations | sequential, cache-resident per function | ~5 per function |
| `MachineLineMark` | 8 (CT) | one per IR row | selector | selector | `codegen_record_machine_line_marks` | sequential | 1 |
| `CodegenModuleRelocation` 24 (CT), `CodegenLineEntry` 12 (CT) | | module | codegen | codegen | `object_from_canonical_codegen_module` | sequential | 1-2 |
| `ObjectSymbol` 48 / `ObjectRelocation` 32 / `ObjectSection` 48 | | module | object.c | object.c | ELF writer | sequential | 1-2 |

---------------------------------------------------------------------------------------------------------------

## (b) Per-pass field-touch matrix for `IrInstruction` rows

Byte map (probe `offsetof`, identical to the declaration order in ir.h):
`operands` 0-7 | `targets` 8-15 | `immediates` 16-23 | `canonical_type` 24-27 | `symbol` 28-31 |
`canonical_local` 32-35 | `next` 36-39 | `result` 40-43 | `operand_count` 44-47 | `target_count` 48-49 |
`immediate_count` 50-51 | `opcode` 52 | `conversion_operation` 53 | `unary_operation` 54 | `binary_operation` 55 |
`memory_order` 56 | `failure_memory_order` 57 | `atomic_operation` 58 | `immediate_is_negative` 59 |
`atomic_signal_fence` 60 | `volatile_access` 61 | `simd_operation` 62 | pad 63.

Legend: R read, W write, D = dereferences the array behind the pointer (leaves the row's line),
(c) only for candidate/opcode-subset rows, (d) only for the row reached through `values[v].definition`.
"Rows" = rows visited on stage-1. Order = pipeline order. "Scope": F = function-at-a-time burst right after the
function's rows were written or last touched (likely cache-resident for small functions), M = module-wide sweep
(one module loop per pass group; 85-98 MB of rows cannot stay in any cache between sweeps).

| # | pass (function) | scope | rows visited | O 0-7 | T 8-15 | I 16-23 | ty 24 | sym 28 | loc 32 | next 36 | res 40 | oc 44 | tc/ic 48-51 | op 52 | conv/un/bin 53-55 | atomic 56-60 | vol 61 | simd 62 | order |
|---|---|---|---:|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | append `ir_instruction_append_trusted` | F | 1.52M | W | W | W | W | W | W | W | W | W | W | W | W | W | W | W | seq |
| 2 | `c_ir_ssa_finish` liveness (c_gen.c ~5840) | F | 1.52M | R D |  |  |  |  |  |  | R | R |  |  |  |  |  |  | seq |
| 3 | `c_ir_ssa_finish` remap (c_gen.c ~5968) | F | 1.52M | R D W |  |  |  |  |  |  | R W | R |  |  |  |  |  |  | seq |
| 4a | `ir_promote_function` barrier + LOCAL count scan (functions whose opcode summary has LOCAL) | M(func burst) | 1,447,058 (3,932 fns) |  |  |  |  | R(c CALL, `ir_local_promotion_call_barrier`) |  |  |  |  |  | R |  |  |  |  | seq |
| 4b | `ir_promote_function` LOCAL table scan | burst | 1,445,015 (3,924 fns) |  |  |  | R(c) |  |  |  | R | R(c) |  | R |  |  | R(c) |  | seq |
| 4c | `ir_promote_function` event walk (every operand checked against `local_by_value`) | burst | 1,445,015 | R D |  |  |  R(c) |  |  | R | R(c) | R |  | R |  |  | R(c) |  | chain |
| 5 | validation #1 ownership `ir_function_instruction_owners` | M | 1.52M |  |  |  |  |  |  | R |  |  |  |  |  |  |  |  | chain |
| 6 | validation #1 values `ir_validate_function_values` -> `ir_label_metadata_transfer_valid`/`_shape_valid` | M | ~1.25M (d) | R D(op[0]) |  |  |  |  |  |  |  | R |  | R |  |  |  |  | by definition |
| 7 | validation #1 blocks `ir_validate_block_instructions` + `ir_validate_instruction_operation` | M | 1.52M | R D | R D | R D(c) | R | R(c) | R(c) | R | R | R | R | R | R(c) | R(c) | R(c) | R(c) | chain |
| 8 | FAST fold `ir_fast_fold` (+`ir_fast_constant` on operand definitions) | M(func burst) | 1.52M | R D(c) |  | R D(d) | R(c) |  |  |  | R | R(c) | R(d) | R | R(c) | R(d) imm_neg | R(c) |  | seq |
| 9 | FAST address `ir_fast_fold(IR_FAST_ADDRESS)` | burst | 1.52M | R D(c) |  |  |  |  |  |  | R | R(c) |  | R |  |  |  |  | seq |
| 10 | FAST dce uses `ir_fast_dce` loop 1 | burst | 1.52M | R D |  |  |  |  |  |  |  | R |  |  |  |  |  |  | seq |
| 11 | FAST dce seeds `ir_fast_dce` loop 2 + queue (`ir_instruction_is_pure`) | burst | 1.52M | R D(c) |  |  | R(c) |  |  |  | R | R(c) |  | R | R(c) |  | R |  | seq + worklist |
| 12 | compaction count `ir_rewrite_compact` loop 1 | burst | 1.52M (4,817 fns) |  |  |  |  |  |  |  | R | R |  |  |  |  |  |  | seq |
| 13 | compaction chain `ir_rewrite_compact` block loop | burst | 1.52M |  |  |  |  |  |  | R |  |  |  |  |  |  |  |  | chain |
| 14 | compaction copy `ir_rewrite_compact` row loop (+ sources) | burst | 1.52M -> 1.33M | R D W | R W | R W | R W | R W | R W | R W | R W | R W | R W | R W | R W | R W | R W | R W | seq, full 64-B copy |
| 15-17 | validation #2 (same three sub-passes as 5-7) | M | 1.34M x (1 + ~0.82 + 1) | as 5-7 | | | | | | | | | | | | | | | |
| 18 | publish: terminators (count + fill) `ir_function_publish_cfg` | M | 150k x2 |  | R D |  |  |  |  |  |  |  | R(tc) |  |  |  |  |  | by block |
| 19 | publish: chain -> remap `ir_cfg_publish_instruction_rows` | M | 1.34M |  |  |  |  |  |  | R |  |  |  |  |  |  |  |  | chain |
| 20 | publish: pool scan + copy `ir_cfg_pool_operands` | M | 1.34M | R | R D W | R D W |  |  |  |  |  | R | R |  |  |  |  |  | seq |
| 21 | publish: in-place permutation (moved functions only) | M | 1,127,495 moved rows (84%) in 4,054 fns | RW | RW | RW | RW | RW | RW | RW | RW | RW | RW | RW | RW | RW | RW | RW | cycle gather |
| 22 | publish: `next` clear loop | M | 1.34M |  |  |  |  |  |  | W |  |  |  |  |  |  |  |  | by span |
| 23 | codegen capacity pre-pass (`codegen_generate_canonical_module_attempt`, value loop) | M | PLACE values only (d) |  |  |  |  |  |  |  |  |  |  | R |  |  |  |  | by definition |
| 24 | selector local discovery (`local_places` path) | F | per local (d) |  |  |  |  |  |  |  | R |  |  | R |  |  |  |  | by definition |
| 25 | selector use walk (machine_x86_64.c ~8041) | F | 1.34M | R D |  |  | R(c div) | R(c CALL) + callee (d) |  |  | R | R | R(tc) | R | R(c) |  | R |  | span seq |
| 26 | selector alias sweeps x2 (candidates LOAD/STORE/DEREF/BRANCH_IF) | F | ~312k x2 | R D |  |  |  |  |  |  |  | R |  | R |  |  |  |  | candidate list |
| 27 | selector classification (~8324) | F | 1.34M |  |  | R D(c ARG) |  |  |  |  | R |  | R(c) | R |  |  |  |  | span seq |
| 28 | selector branch fusion (BRANCH_IF + chain members) | F | 76k + members (d) | R D |  |  |  |  |  |  | R | R |  | R | R |  |  |  | candidate + by definition |
| 29 | selector main selection (~9239) | F | 1.34M | R D | R D | R D | R | R | R(c) |  | R | R | R | R | R | R(c) | R | R(c) | span seq |
| 30-32 | `machine_debug_values_build`: `local_places` scan, `machine_debug_facts_build` wide-count scan, argument scan (`-g`, functions with debug locals) | F | 1,334,053 x3 (5,075 fns) |  |  | R D(c ARG) |  |  | R |  | R |  | R(c) | R |  |  |  |  | seq |
| 33 | line marks `codegen_record_machine_line_marks` | F | 0 rows (reads `instruction_canonical_sources` only) |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  | seq |

What the matrix says, per question asked:

* **Which of the 64 bytes each pass reads.** Only passes 1, 7/17, 14, 21 and 29 use (nearly) the whole row. The
  other ~20 passes read one to four narrow fields: `next` alone (5, 13, 15, 19, 22 write), `result`+`operand_count`
  (12), `opcode`+`result` (9, 23, 24, 27 mostly), `operands`+`operand_count` (2, 3, 10). The union that
  essentially every pass needs is {`opcode` 52, `result` 40, `operand_count` 44, `operands` 0-7}; the next tier is
  {`next` 36 (pre-publication only), `canonical_type` 24, `volatile_access` 61, `binary/conversion/unary` 53-55}.
  Bytes 8-23 (targets/immediates pointers), 28-35 (symbol, canonical_local), 48-51 and 56-62 are opcode-subset
  payload: targets exist on 12% of rows, immediates on 36%, atomics fields on 5 rows of 1.34M, `simd_operation`
  on 33.
* **Does any pass leave the row's line for every row?** Yes. Passes 2, 3, 4c, 7/17, 10, 14, 25 and 29 dereference
  `operands` for every operand-bearing row (835k rows, 1.30M slots on stage-1); 7/17 also dereference every
  `targets` array, and 7/17, 8 (through operand definitions) and 29 dereference `immediates` for the opcode subset.
  Before publication the immediates are scattered (see ledger) so those are real pointer chases; the operand
  arrays are dense (after pass 3) so the second load is sequential.
* **Are operand arrays contiguous across instructions in practice?** At creation, no: they are one small
  `arena_allocate` per instruction in the same arena as immediates, targets, incoming nodes and strings. They
  become contiguous twice by *copying*: `c_ir_ssa_finish` writes a fresh dense pool (census c_gen.c:5964,
  5.85 MB) and `ir_rewrite_compact` writes another for the 4,817 functions FAST changed (ir_promote.c:569,
  5.18 MB). At publication 5,163/5,206 functions are already contiguous (`op_gap_zero=830,047` vs 158 non-zero
  gaps); 588 slots get copied. However the pool is in **pre-publication index order**, and publication then moves
  84% of rows, so in published (block-span) order the operand stream is piecewise sequential: 747,855
  operand-bearing rows continue exactly where the previous one ended and 82,350 (9.9%) jump
  (probe `published_op_adjacent/break`). Immediates after publication: 408,571 adjacent, 67,605 jumps (14%).
  Targets and immediates are never pooled before publication: 950/5,206 and 188/5,206 functions are contiguous,
  so publication copies 234,499 target slots and 563,697 immediate slots into new pools.
* **Does CFG publication duplicate rows/operands, and who reads the copy?** Rows: no copy; they are permuted
  **in place** (cycle-following gather, `ir_cfg_publish_instruction_rows`) for 4,054/5,206 functions,
  1,127,495 rows (84%), together with their 12-B sources, then every row's `next` is rewritten again (pass 22).
  Operands: not copied on stage-1 (already pooled by FAST). Targets and immediates: copied into
  `target_pool`/`immediate_pool`, and the rows are redirected to the copies; the originals become dead but stay
  in the arena. Every later consumer reads the **copies via the row pointers**; nobody reads
  `IrPublishedCfg.operand_pool/target_pool/immediate_pool` or `instruction_remap` directly (grep: only
  `ir_cfg_test.c`/`ir_fast_test.c`). `instruction_remap` (5.2 MB, 4,054 allocations) is write-only in production.
* **How many full passes.** Counting row sweeps that touch (almost) every row: frontend 3 (1-3); prepare 20
  (promotion 3 over 95% of rows, validation #1 3, FAST 4, compaction 3, validation #2 3, publication 4 of which
  the permutation covers 84%); codegen 3 selector sweeps (25, 27, 29) + 3 debug-value sweeps with `-g` (30-32,
  5,075 functions = all rows) + candidate subsets. **~29 row sweeps, ~40.8M row visits = ~2.6 GB of 64-B row
  lines nominal** on stage-1 (row-visit arithmetic from the probe counts: 3x1.52M + 3x1.45M + (1.52+1.25+1.52)M
  + 7x1.52M + (1.34+1.10+1.34)M + (3x1.34+1.13)M + 6x1.34M).
  How many of these are *module-wide* (rows evicted between passes): the loops are function-at-a-time inside
  promotion, FAST+compaction, publication and codegen, but validation runs its ownership proof over the whole
  module first (`ir_validate_module_ownership`) and only then the per-function value/block checks, so the
  distinct module sweeps are: promotion 1, validation #1 2, FAST 1, validation #2 2, publication 1, codegen 1
  = **8 module sweeps x 85-98 MB ~= 0.7 GB** that cannot be served from a 32 MB L3; the remaining ~21 sweeps
  re-touch a function that was just touched (cache-resident unless the function is large; the largest has
  360,844 rows = 23 MB). Frontend sweeps 1-3 happen while the function is being built.

---------------------------------------------------------------------------------------------------------------

## (c) Top 5 representations by passes x bytes touched x consumers

Score = (row sweeps) x (bytes streamed per sweep) x (distinct consumer subsystems). Bytes streamed per sweep is
what a sequential pass pulls through the cache hierarchy, not what it uses.

### 1. `IrInstruction` rows (64-B, 1.3-1.5M rows, ~29 sweeps, 7 consumer subsystems)

Score ~ 29 x 85-98 MB x 7 (c_gen/SSA, promotion, validation, FAST, publication, x86 selector, debug values;
plus LLVM bitcode ~8 row loops, Wasm ~4, eBPF as secondary consumers). ~2.6 GB of nominal line traffic of which
8 module-wide sweeps x ~90 MB (~0.7 GB) cannot be cache-served.

Evidence of multiplicative waste specific to the organization:
* Measured (callgrind, object.c, LL smaller than the IR): the module-wide sweeps in `ir_prepare_canonical_module`
  are 7.9% of instructions but 41% of LL read misses and 22% of D1 read misses; the `next`-only ownership walk
  takes exactly one LL miss per row visited (section 6). With an LL that holds the IR these misses disappear, so
  they are capacity misses caused by re-sweeping 64-B rows, which is the stage-1 regime (97 MB rows vs 32 MB L3).
* Mixed hot/cold record: most sweeps use <=13 B of 64 (matrix b), but a 64-B row is one line per row, so a
  `next`-only or `opcode+result` sweep still streams the whole array. Opcode-subset payload (pointers for targets
  and immediates on 12%/36% of rows, atomic fields on 5 rows) occupies ~40% of every row.
* Placement defeats the one-line design in ir.h ("the row is kept to exactly one cache line"): 87% of row arrays
  are only 8-B aligned, so a by-definition row access that reads both ends of the row (`operands` at 0 and
  `opcode` at 52, as passes 6, 8(d), 28 and the bitcode writer do) touches two lines in 6 of the 8 possible 8-byte
  alignments; a full-row copy (passes 14, 21) always touches two.
* Rows are rewritten in full three times before codegen: compaction copy (pass 14, 1.52M x 64 B), publication
  permutation (pass 21, 1.13M x 64 B), and the per-row `next` rewrite (pass 22) - the permutation exists only
  because compaction (which already moves every row to a new index) emits rows in construction order rather
  than block order.
* Reserve: 5.94M rows reserved for 1.52M written (26%); arrays of consecutive functions are 4x farther apart
  than needed, so module-wide sweeps cannot benefit from adjacent-page prefetch across function boundaries.

Status: the object.c-scale version of the test below was run (section 6) and did **not** falsify; the stage-1
version is still needed on the approved machine.
Cheapest falsification (zero code change): on an AVX-512-free build, run callgrind `--cache-sim=yes` with an
LL smaller than the IR working set (section 6 does this for object.c with a 2 MiB LL) and sum `DLmr+DLmw` for
the row-walking functions (`ir_function_instruction_owners`, `ir_validate_*`, `ir_fast_*`, `ir_rewrite_compact`,
`ir_function_publish_cfg`/`ir_cfg_*`, `ir_promote_function`, `machine_select_canonical_function_x86_64`,
`machine_debug_*`). If that share of all LL misses is small (< ~10%) and their D1 misses per row visit are far
below 1, the 64-B row is not a traffic multiplier and the cost is instruction count, not layout. On the real
9700X the equivalent is `perf record -e mem_load_retired.l3_miss` (or AMD `ls_any_fills_from_sys.dram_io_all`)
attributed to those symbols on stage-1. Note `-fno-canonical-fast` is a confounded test (it also changes the
row population that reaches codegen: 1.52M instead of 1.34M).

### 2. `IrValue` (16-B, 1.13-1.32M values, ~8 sweeps + ~6 per-operand random lookups, 6 consumers)

Score ~ (8 sweeps x 21 MB) + (per-operand lookups ~6 x 1.3M x 16 B). Readers: `c_ir_ssa_finish` (sweep +
compaction), `ir_validate_function_values` (x2 sweeps) + per-operand checks in validation (x2), FAST
(`values[first]`, `values[result]`), `ir_rewrite_compact` (sweep), publication definition remap (sweep),
codegen capacity pre-pass (sweep), selector (`values[used].definition` per operand, `values[result]` per row).
Access to `values[operand]` is local (87.5% within 64 ids of the row's own result) and IrValue is dense, so this
is cheaper per byte than rows. Alignment is 4 B, only 305/5,206 arrays are 64-B aligned; a 16-B record straddles
a line boundary for 1 in 4 base alignments.
Cheapest falsification: callgrind cache-sim `D1mr` on the value arrays is not directly attributable; instead
count, with the existing probe, operand lookups whose value id lies in a different 64-B line than the previous
lookup in the same pass. If that is <10% of lookups, `IrValue` is not a traffic multiplier.

### 3. Operand arrays (4-B ids; 1.30M slots; materialized 3x; dereferenced by ~9 passes)

Score ~ 9 deref sweeps x 5.2 MB x 6 consumers, plus 3 materializations (per-instruction, SSA pool, FAST pool),
each of which also rewrites the `operands` pointer in every operand-bearing row. The pointer (8 B in the row) is
larger than the median payload (1-2 ids): 63% of rows have 1-2 operands (LOAD/CAST/FIELD/DEREFERENCE/ADDRESS_OF
have exactly 1; BINARY/STORE/INDEX 2). The only reason the stream is dense is the two re-pool copies, and it is
dense only in pre-publication order.
Measured (probe): in published order 9.9% of operand-bearing rows jump (82,350 of 830,205), i.e. runs of ~9
rows, so after publication the operand stream is piecewise sequential and mostly prefetchable; the avoidable
cost here is the **materialization** (three writes of 5.2-5.9 MB plus three rewrites of the `operands` pointer in
835k rows), not the read pattern.
Cheapest falsification: callgrind (section 6) `Ir`/`DLmw` of `c_ir_ssa_finish` + `ir_rewrite_compact` vs the
whole compile; if both re-pools together are <1% of instructions and of LL write misses, the triple
materialization is harmless and this entry drops out. Result on object.c: the two functions are 4.5% + 1.1% of
Ir and 13.6% + 3.0% of D1 read misses inclusive, but both do much more than re-pooling (SSA simplification,
full-row compaction), so the re-pool share is not isolated; the test needs a counter around the two re-pool
loops (c_gen.c `c_ir_ssa_finish` final loop, ir_promote.c `ir_rewrite_compact` copy loop). Not falsified, not
confirmed.

### 4. `CToken` stream + per-token side arrays (12 B + 1 + 4 + 4 + 1 B per token; 3.6M tokens)

Score ~ >=4 sweeps x 43 MB (stream) + side arrays; consumers preprocess, parse, analysis, lowering. The lexed
per-file arrays (38.8 MB) are written, then memcpy'd line-segment by line-segment into the final stream
(duplicate materialization; the lexed copy is never read again after its file is done). Lowering re-walks body
token ranges several times (capacity scan in `c_lower_to_ir_with_options`, `c_ir_build_delimiter_index` fallback,
the lowering machine, `c_ir_unsupported_gnu_construct`), and spelling comparisons (`string_equal(c_token_spelling
...)`, 74 sites) leave the token line for the spelling bytes. Detailed pass counts belong to the frontend scout;
this scout verified only the copy and the lowering access sites.
Cheapest falsification: count lowering token visits per body token (one counter in the lowering machine's
token fetch); if <3 visits/token the stream is not a multiplier.

### 5. `IrType` table (128-B records, 51,921 types, 6.6 MB)

Score ~ ~4 per-row lookup passes (validation x2 row + operand types, FAST width/purity, promotion) x 1.3M x up
to 3 lines (128-B record, base not 64-aligned) - but the working set is heavily skewed (a few scalar and pointer
types), so it mostly hits in L1/L2. Hot fields `kind` 80, `layout.size` 56, `layout.alignment` 64,
`layout.resolved` 72, `bit_width` 108, `is_atomic` 114, `is_volatile` 116 span both halves of the record. Codegen
already switched to 4-B/8-B projections (`MachineTypeClass`, `CodegenSlotCost`), which is evidence that the
record shape was a measured cost there.
Cheapest falsification: callgrind cache-sim `D1mr` for `ir_type_from_id` callers; or count distinct type ids
touched per validation pass (if <1% of 51,921, the table is cache-resident and drops out of the ranking).

Minor, repeated-metadata pattern (not ranked): every direct CALL row (41,137 on stage-1) is checked against 14
spellings with `string_equal(symbol->name, ...)` in `ir_call_returns_twice` (ir_promote.c), once by the promotion
barrier scan and once by the x86 selector use walk; a per-symbol returns-twice bit would answer it without
leaving the row for the symbol's name bytes.

Not ranked (verified small): `IrBlockParameter`/`IrIncoming`/`IrPredecessor` linked lists (76k surviving incoming
nodes, all `next` links within 64 B, so the "pointer chase" is physically sequential); `IrPublishedCfg` edge/param
arrays (dense, <3 MB); MIR rows (per-function scratch, rewound each function, cache-resident); object records
(one or two sequential passes).

---------------------------------------------------------------------------------------------------------------

## 5. Probe results (instrumentation)

Patch: `access/probe.patch` (206 lines, counters only, no behavior change; worktree `wt-access`). The probe binary
reproduced the baseline stage-1 object byte-for-byte (`cmp access/stage1_probe2.o base/stage1.o`: identical).
Raw output: `access/probe_stage1b.txt` (stage-1), `access/probe_object2.txt` (object.c).

| counter | stage-1 | object.c | meaning |
|---|---:|---:|---|
| validate_calls / validate_rows | 2 / 2,858,522 | 2 / 124,618 | full-module validations and rows they cover |
| promote_scan_functions / rows | 3,932 / 1,447,058 | 133 / 61,775 | functions whose opcode summary admits LOCAL: barrier/LOCAL scan |
| promote_local_functions / rows | 3,924 / 1,445,015 | 133 / 61,775 | also LOCAL table scan + event chain walk |
| fast_functions_run / rows | 5,206 / 1,523,381 | 190 / 66,379 | FAST fold/address/dce passes |
| compact_calls / rows / operand slots | 4,817 / 1,332,876 / 1,295,142 | 185 / 58,180 / 55,717 | `ir_rewrite_compact` full row copies + fresh operand pool |
| publish_functions / rows | 5,206 / 1,335,141 | 190 / 58,239 | |
| publish_moved_functions / moved rows | 4,054 / 1,127,495 (84%) | 157 / 52,270 (90%) | in-place row permutation + `instruction_remap` allocation |
| operand pool contiguous at publish (functions) | 5,163 / 5,206 | 190 / 190 | only 588 operand slots copied on stage-1 |
| target pool contiguous / slots copied | 950 fns / 234,499 of 235,006 | 22 / 11,646 of 11,716 | |
| immediate pool contiguous / slots copied | 188 fns / 563,697 of 563,952 | 4 / 19,731 of 19,744 | |
| index-order gaps between consecutive immediate arrays (pre-publication) | 0 B: 118,016; <64 B: 122,231; <4 KiB: 60,362; >=4 KiB: 78,429; negative: 97,138 | | scattered per-instruction allocations |
| published-order operand continuity | adjacent 747,855 / jump 82,350 | 31,592 / 4,704 | pool is in pre-publication order |
| published-order immediate continuity | adjacent 408,571 / jump 67,605 | 14,617 / 3,163 | |
| operand pool distance from its row array | >1 MiB for 4,853 of 5,206 | 181 of 190 | pools live far from rows (separate pages/TLB entries) |
| operand -> definition row distance | <=8: 989,562; <=64: 119,216; <=1024: 98,866; >1024: 33,657; parameters: 56,016 | | by-definition row reads are mostly local |
| operand value id vs result value id | <=64: 913,449; >64: 129,808 | | `IrValue` lookups mostly local |
| row arrays 64-B aligned | 619-694 of 5,206 (run-dependent; ~1 in 8) | 32 of 190 | `arena_allocate` gives `alignof(IrInstruction)`=8 |
| value arrays 64-B aligned | 305-306 of 5,206 | 8 of 190 | `alignof(IrValue)`=4 |
| type table 64-B aligned | no (51,921 types) | no (3,804) | 128-B records span 2-3 lines |
| incoming nodes / `next` gap > 64 B | 76,331 / 0 | 3,787 / 0 | linked lists are physically sequential |
| rows reserved (instruction_capacity) | 5,943,429 | 300,192 | vs 1.52M / 66k written |
| functions with debug locals / their rows | 5,075 / 1,334,053 | 190 / 58,239 | three extra row scans in `machine_debug_values_build` |
| blocks | 168,748 | | |

## 6. callgrind (instrumentation; simulated caches, not a timing)

`build/Release/ide` cannot run under valgrind 3.22 (it is `-march=native` on an AVX-512 host; valgrind dies with
SIGILL on an EVEX instruction in `os_thread_set_name`, `access/object.callgrind.log`). I therefore built the
**unmodified** ade6ac4b sources with the Release flags but `-march=x86-64-v3` (`access/clean-build/`, read-only
use of `/home/user/buster/src` and `build/generated`); its object.c output is byte-identical to the probe binary's.
Workload: object.c, `-g`, one process at a time. I did not run stage-1 under valgrind (integrator's job).

Two cache geometries (D1 48 KiB/12-way for both):
* **LL = 2 MiB**: smaller than object.c's IR (66k rows x 64 B = 4.2 MB), i.e. the same regime as stage-1's
  ~97 MB of rows against a 32 MiB L3. Output `access/object_ll2m.callgrind`.
* **LL = 32 MiB**: object.c's IR fits. Output `access/object_ll32m.callgrind`.

Totals (2 MiB LL): Ir 1,005,789,277; D1mr 6,831,505; DLmr 1,628,435; DLmw 1,142,993.

| inclusive cost | Ir | D1mr | DLmr @2 MiB | DLmr @32 MiB |
|---|---:|---:|---:|---:|
| `ir_prepare_canonical_module` (promotion, 2 validations, FAST, compaction, publication) | **7.88%** | **21.85%** | **40.93%** (666,478) | 0.25% (415) |
| `ir_validate_canonical_module` (2 calls) | 3.94% | 7.35% | **22.20%** (361,547) | 0.02% |
| `ir_function_instruction_owners` (380 calls, reads only `next`) | **0.28%** | 2.14% | **8.69%** (141,524) | 0.00% |
| `ir_fast_function` (incl. `ir_rewrite_compact`) | 2.22% | 7.05% | 7.21% | 0.01% |
| `ir_function_publish_cfg` | 1.26% | 4.81% | 6.63% | 0.00% |
| `ir_promote_function` | 0.46% | 2.59% | 4.68% | 0.13% |
| `machine_select_canonical_function_x86_64` | 6.15% | 9.11% | 6.14% | 12.44% |
| `c_ir_ssa_finish` | 4.50% | 13.55% | 1.48% | 0.00% |
| `c_lower_to_ir_with_options` (all lowering, incl. SSA finish) | 21.11% | 21.96% | 12.64% | 1.41% |
| `c_analyze_semantics_core` (parse/analysis) | 25.73% | 24.29% | 11.78% | n/a |

Readings:
* The ownership proof is the cleanest single measurement of the row-width multiplier: the inlined
  `ir_block_next_instruction` line inside `ir_function_instruction_owners` records **exactly 124,618 D1 read
  misses and 124,618 LL read misses (2 MiB LL) for 124,618 row visits** (= the probe's `validate_rows` for
  object.c): one full line fetched from beyond LL for every 4-byte `next` read. Stage-1 does 2,858,522 such
  visits.
* The prepare phase (all module-wide IR sweeps) is 7.9% of instructions but 41% of LL read misses when rows
  exceed LL, and ~0% when they fit: the misses are **capacity misses of repeated sweeps**, not compulsory ones.
  D1 read misses (22% of all at 7.9% of Ir) are present in both geometries.
* `c_ir_ssa_finish` shows the other regime: high D1 misses (13.5% of all), negligible LL misses, because it runs
  while the function's rows are still in LL.
* Not measured here: stage-1 itself. Extrapolating object.c's prepare DLmr (666k for 66k-row IR) linearly to
  stage-1's 23x larger IR gives ~15M LL read misses (~1 GB) for the prepare phase; this is a projection, not a
  measurement, and depends on the real L3 policy.
* Outside this scout's scope but visible: libc `memset` + `memcpy` are 29% of D1 write misses and 33% of LL write
  misses (arena zeroing/copies; lifetime scout).

## 7. Claims verified / not verified

Verified against current source/probe:
* `IrInstruction` is 64 B with three pointers (ir.h:547 CT check; probe offsets). `IrValue` 16 B (CT).
  `CToken` 12 B (c.h:147 CT). `MachineInstruction` 24 B (CT).
* `IrPublishedCfg` has operand/target/immediate pools and an `instruction_remap` (ir.h). New: none of those four
  fields has a production reader; `instruction_remap` is write-only (5.2 MB on stage-1).
* The C frontend is token-range based (c_parse.c header: "It builds no tree; every later consumer re-walks token
  ranges"); lowering reads `preprocess.tokens[...]` directly (878 sites in c_gen.c).
* Stage-1 volume: 3,600,849 preprocessed tokens (brief said ~3.58M; the integrator's baseline stdout says
  3,600,849). IR rows: 1,524,516 built, 1,523,381 after promotion, 1,335,141 after FAST (the #792 figure of
  ~1.35M matches the post-FAST count of an older revision, 1,330,350 on 2026-09-13).
* ir.h comment "the row is kept to exactly one cache line": true for size, false for placement (87% of arrays
  not 64-B aligned).
* ir_cfg.c header "Operand pools reuse contiguous construction data": true for operands on stage-1 only because
  `c_ir_ssa_finish` and `ir_rewrite_compact` already re-pooled them; false for targets and immediates (copied).
* Release `ide` has `BUSTER_INCLUDE_TESTS=1` so two validations run on stage-1 (probe `validate_calls=2`).

Not verified (no PMU access in this container; no timing allowed):
* #792's "52% of DRAM fills" and "function-at-a-time ordering <= 8.19% cycles".
* machine_x86_64.c comment "1,28 M candidate visits": stage-1 candidate rows are LOAD 129,799 + STORE 65,509 +
  DEREFERENCE 40,778 + BRANCH_IF 76,477 = 312,563 per sweep; with two alias sweeps and the fusion sweep that is
  ~0.94M visits, not 1.28M; I did not instrument the selector, so the discrepancy is unresolved.
* Frontend per-token visit counts during lowering (frontend scout's territory).
