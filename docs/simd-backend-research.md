# SIMD research: the native backend (selection, allocation, encoding)

Research notes, 2026-08-19, Linux x86-64. Measurements were taken on the
Zen 4 7940HS development machine against the post-`2026-08-19b` tree
(stage 1 = 10.593 G instructions); the design target is Zen 5's native
4-pipe 512-bit execution per the microarchitecture rules in `AGENTS.md`.
This is a research catalogue, not an audit: nothing here has landed, and
every proposal below still owes the full same-source A/B measurement
protocol before it may claim a number. Audit history and the negative
results this document must not re-litigate live in `PERFORMANCE_AUDITS.md`.

## Method and reference profile

Ten back-to-back stage-1 unity compiles (`ide cc -Isrc -Ibuild/generated
-DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g src/buster/apps/ide/ide.c`)
under `perf record -F 1999 -g --call-graph fp`, 18,950 samples, clang
Release build with `BUSTER_DEBUG_INFO`/`BUSTER_FRAME_POINTERS` on. The
backend — everything after canonical IR — is roughly 27% of stage-1 cycles:

| Share | Symbol | Phase |
|---|---|---|
| 4.98% | `machine_select_canonical_function_internal` | selection |
| 3.93% | `machine_fast_placement_build_prepassed` | allocation |
| 2.21% | `machine_encode_x86_64` | encoding |
| 2.17% | `machine_fast_prepass_build` | allocation |
| 1.97% | `codegen_generate_canonical_module_attempt` | orchestration |
| 1.70% | `machine_x64_select_instruction` | selection |
| 1.44% | `ir_source_region_position` + `ir_source_map_position` | line tables |
| 1.31% | `machine_x64_emit_exact_recipe` | encoding |
| ~4.3% | `buster_x86_metadata_emit_*` + generated-blob accessors | encoding (interpretive + prewarm decode) |
| 0.72% | `machine_fast_conform_edge` | allocation |
| 0.62% | `dwarf_build_legacy` + `dwarf_build_model` | debug |
| 0.58% | `machine_x64_emit_exact_frame_chunk` | encoding |
| ~0.5% | object writer (`object_symbol_name_index_add` etc.) | object |

`libc` is another 6.79%, almost all `__memcpy_avx512`/`__memset_avx512`/
`__memcmp_evex` — already vector code; the only lever there is calling it
less. The QUALITY allocator and `machine_schedule.c` do not appear: the
default pipeline runs FAST, so scheduling work is a quality-mode lever only.

Populations, from the audit record: 1,493,600 machine rows per stage-1
compile, of which 1,049,817 are unconstrained virtual-only "simple" rows
(`2026-08-18r`/`19a`); 1,461,577 exact-recipe emissions, 1,136,155 of them
dense GPR-table forms (`2026-08-19a`).

## Ground rules this research inherits

The audit series `2026-08-18a`–`2026-08-19b` already walked the layout-first
half of this road: operand lanes are classified once into a per-row `u32`
mask SoA (`18q`), the simple-row population has a dedicated kernel (`18r`),
the builder append is a typed cursor bump (`18o`), selection facts are
compact value-indexed streams (`19b`), and the first real vector kernel —
the AVX-512 free-register query in `machine_fast_free_candidates` — landed
in `18o` at a measured −0.189%. Designs that were built, measured negative,
and closed (do not retry as proposed):

- Gathering arbitrary already-encoded AoS records into a 64-byte tile and
  compacting with `vpcompressb` (`18g`, +0.20% in its best form). A SIMD
  encoder must start earlier, on homogeneous pre-encoding state.
- A sparse `(instruction_index, encoding_word)` command stream merged back
  into the ordered emission loop (+0.26%). No second sparse stream beside
  the row stream.
- Run-length batching of simple allocator rows (+20.8 M): the scan's
  per-row state dependency is real.
- An `IrInstruction` SoA row split (2026-08-09b, +2.1%): the 64-byte
  one-line row is load-bearing; project hot facts out of it instead.
- A never-evicting emission cache (2026-08-15 series): worse than none.

Everything below is shaped by those results: each kernel consumes state
that is already dense and homogeneous, replaces scalar work instead of
running beside it, and stays inside the `<buster/lib/simd.h>` discipline —
full 512-bit width, `u64`/`u16` masks in general registers, scalar
fallback for non-AVX-512 builds, performance quoted from clang binaries
only. Zen 4 must break even; Zen 5 collects the width.

## 1. A dword lane sub-vocabulary for `simd.h` (the enabler)

Every hot backend population is `u32` lanes, not bytes: `MachineRef`
operands, owner/contract register files, virtual-register fact streams,
interval bounds, GPR encoding dwords. The existing vocabulary is
byte-oriented, and the one landed backend kernel already pays the tax:
`machine_fast_free_candidates` compares sixteen `u32` owner lanes with
`simd512_equal_byte` and then collapses the byte mask through the five-step
SWAR chain in `machine_fast_compact_u32_mask`. In today's profile that
collapse alone is ~9% of the placement symbol's samples (~0.35% of
stage 1); `vpcmpeqd` would deliver the sixteen-lane mask directly and the
whole collapse disappears.

Proposed additions, one instruction each, with the mask type widened by a
`Mask16`/`Mask32` alias family (`u64` remains the carrier; only the useful
width changes):

| Operation | Instruction | First consumer |
|---|---|---|
| `simd512_splat_word(u32)` | `vpbroadcastd` | register-file value search |
| `simd512_equal_word` → Mask16 | `vpcmpeqd` | free/owner queries, contract scans |
| `simd512_less_word` (unsigned) → Mask16 | `vpcmpud` | interval/last-use compares |
| `simd512_test_word` → Mask16 | `vptestmd` | flag-lane tests |
| `simd512_min_word`/`max_word` (unsigned) | `vpminud`/`vpmaxud` | interval accumulation |
| `simd512_compress_word` (+ store) | `vpcompressd` | active-lane compaction |
| `simd512_permute2_word` | `vpermt2d` | 24-byte row deinterleave |
| `simd512_conflict_word` | `vpconflictd` | in-batch same-vreg detection |
| `simd512_add_word`/`subtract_word` | `vpaddd`/`vpsubd` | index arithmetic |

Each addition follows the full checklist from `AGENTS.md`: an
`IrSimdOperation`, its arity in `ir_simd_operation_shape`, validation, the
EVEX lowering in `codegen_canonical_x64_simd_operation`, the builtin in
`c_ir_simd_builtins`, a scalar fallback, and a `tests/basic_c_simd.c` case.
That cost is why the list above is ranked: `splat_word` + `equal_word`
alone pay for themselves on the landed kernel, and the rest can arrive
with the kernel that first needs them. Note `vpconflictd` and the
two-source dword permutes are in the "check uops.info" class the AGENTS
rules call out — AMD's `vpconflictd` is usable (unlike pre-Ice-Lake
Intel), but verify the Zen 4 number before a kernel leans on it.

Deliberately not proposed: gathers and scatters as vocabulary operations.
`vpgatherdd` is ~1 lane/cycle on Zen 4 (better on Zen 5) and only earns a
place inside a kernel whose alternative is 16 dependent scalar loads from
an L1-resident table; `vpscatterdd` is slow on both and the deterministic-
output rule makes scatter ordering a hazard. Where a proposal below wants
a gather it says so explicitly, sized against the scalar alternative.

## 2. Register-file kernels in FAST placement (3.9% + 0.7%)

The per-block contract machinery operates on register files of
`active_register_count` (≤48) `u32` owner lanes plus `bool` dirty arrays:
contract construction filters a donor file, the intersection pass compares
what every scanned predecessor delivers, `machine_fast_conform_edge` runs
a "is this resident value kept by the contract" inner loop that is O(R²)
in the worst case, and split-span installation searches the file for a
displaced value. All of these are 3-ZMM populations with the same three
primitives:

- **Active mask**: `simd512_equal_word(file, splat_word(UINT32_MAX))`
  inverted — one compare per 16 lanes replaces 48 branch tests. Most of
  the file is empty most of the time, so every loop that today walks 48
  entries to find 2–3 live ones becomes mask iteration over the live ones.
- **Value search**: broadcast the value, compare the three ZMMs, `kortest`.
  This directly replaces the `kept` scan in `machine_fast_conform_edge`
  pass 1 and the split-entry displacement scan.
- **Filter compaction**: `vpcompressd` the active owners (and their lane
  indices from an iota vector) into a dense worklist, then run the scalar
  per-entry logic (escape/remat/liveness gathers) only over real entries.

Two layout changes make the kernels simpler and are worth doing first,
scalar-measurable on their own: `dirty` as a `u64` bitmask per file
instead of `bool[48]` (pass 1 of conform becomes mask arithmetic), and a
prepass-owned per-block edge index so the two `for (edge_index <
edge_count)` linear searches in contract construction stop rescanning the
whole edge array per predecessor (`MachineEdge` is 16 bytes; the scan is
also a fine `vpcmpeqq`-over-pairs kernel, but an index removes it).

The serial core of the scan — LRU pick, bind, spill per row — stays
scalar; `18r`'s run-length experiment already proved the row-to-row
dependency is load-bearing. The kernels above live in the per-block and
per-edge machinery around it, which is exactly where the remaining
placement samples concentrate now that the simple-row path is lean.

## 3. Batch row classification in the FAST prepass (2.2%)

`machine_fast_prepass_build`'s first walk loads a 96-byte
`MachineOpcodeInfo` row per instruction (a 19 KB table, two cache lines a
row) and assembles the per-row `operand_masks` word one operand at a time
from `operand_info` role bits plus each `MachineRef`'s kind bits. Both
inputs are static-per-opcode or pure bit extraction — the population is
1.5 M rows of exactly the "classify, then batch" shape.

Step 1 (layout, scalar win on its own): a compact per-opcode template
table — one `u32` per opcode, 199 × 4 B ≈ 800 B, L1-resident forever —
carrying the role lane bits already shifted into `operand_masks` layout
plus summary bits (constrained, call, terminator, has-clobber,
simple-eligible). The scalar loop then reads one dword instead of walking
a 96-byte descriptor; the full descriptor stays authoritative for the
irregular rows that placement revisits.

Step 2 (the wide kernel): process rows eight at a time. Eight 24-byte rows
are three ZMM loads; two `vpermt2d` rounds deinterleave them into operand
columns and an opcode/flags column (a reusable primitive — the encoder
capacity walk and QUALITY's foreclosure pass want the same projection).
Then, per 8-row batch, entirely in registers: kind bits are the top three
bits of each operand lane (`vpsrld`/`vpcmpeqd` against the
VIRTUAL/PHYSICAL/BLOCK constants), templates arrive either through a
16-lane `vpgatherdd` from the 800-byte table or through the scalar column
(eight L1 loads — measure both; the table is small enough that the gather
is competitive on Zen 5 and marginal on Zen 4), and the OR of template and
kind bits plus the simple-row predicate is `vpternlogd` work. The
scatter-shaped tail — `interval_starts/ends`, `last_use`,
`definition_blocks` per virtual register — stays scalar but consumes a
`vpcompressd`-compacted `(vreg, row, role)` stream so it touches only real
virtual-register lanes (~2.2 per row) with no branches. The `18g`/`18h`
lesson applies: this stream must *replace* the scalar per-slot decode, not
run beside it, and the batch must be gated per block so malformed-row
fail-closed behavior is preserved.

The same deinterleave + template shape serves three other whole-population
walks noticed in the profile: the encoder's capacity pre-walk (a
switch over 1.5 M opcodes; a 199-entry byte table of worst-case sizes
turns it into a gather-plus-add or even a plain `u8` table sum),
`codegen_canonical_x64_function_shape`'s per-function scan for
atomics/inline-assembly (better: accumulate a per-function opcode
presence summary during lowering and delete the scan), and QUALITY's
foreclosure pass, which re-derives per-row constraint state the template
also covers.

## 4. Encoding: homogeneous batches or nothing (≈8% combined)

The exact-recipe path is already table-driven — the dense GPR forms load a
pre-encoded ≤4-byte unit from a 256-entry table and the compact path
already stores four bytes unconditionally. Per `18g`, re-batching the
*output* records is closed. What remains, in expected-value order:

- **Retire the interpretive residue.** ~4.3% of stage 1 is still
  `buster_x86_metadata_emit_*`: roughly 0.6% is the one-time prewarm table
  decode (real per compile — a compile is one process), and the rest is
  per-emission `machine_x64_metadata_shape_cache_find` lookups and
  form-bind interpretation from the prologue/epilogue, allocator-edit
  runs, and non-exact rows, plus by-value `BusterX86MetadataMachineExactQuery`
  construction (the single hottest instruction in `machine_encode_x86_64`
  is that struct copy). None of this wants vectors; it wants the same
  prepared-plan treatment the GPR tables got: pre-resolved byte templates
  for the closed set of prologue/edit shapes (PUSH/MOV/SUB/spill/reload
  per register × frame-offset width), stamped at prewarm. This is the
  biggest single backend win available and it is pure data layout — listed
  here because every SIMD plan for the encoder is worth less than
  deleting this interpretation first.
- **Batch the homogeneous GPR population, if runs are long enough.**
  1,136,155 emitted rows are dense GPR-table units of ≤4 bytes whose
  operands placement already resolved. The `18g`-sanctioned design is:
  during a run of consecutive compact rows uninterrupted by edits,
  fixups, or expansion rows, compute the encoding dwords sixteen at a
  time (table base per opcode from the template column, index
  `low + high·16` from the operand-register bytes placement wrote, one
  `vpgatherdd` against L1-resident tables), then pack the variable ≤4-byte
  units with `vpcompressb` under a length-derived mask — Muła's packed-
  varint shape ("AVX512VBMI2 and packed varuint format") — with one store
  and cursor advance per 16 rows. **Estimate before implementing**: count
  the run-length distribution of edit-free compact rows first (a one-off
  census like `18r`'s); if median runs are short, the merge predicate
  eats the win and this stays closed.
- **Wide-store patching.** The immediate and displacement patch loops in
  `machine_x64_emit_exact_recipe` write byte-at-a-time; the capacity
  budget (24 B/row) already guarantees room for an unconditional 8-byte
  store + cursor arithmetic. Scalar-wide, small, safe.

## 5. DWARF line program and LEB128 (≈0.6%, clean and self-contained)

`dwarf_build_legacy` is a per-entry state machine emitting
`DW_LNS_*` opcodes with byte-loop ULEB128/SLEB128 writers, fed by
`codegen_record_line`'s already-deduplicated entries. Two Muła recipes
apply directly: batch varint emission via VBMI2 (`vpexpandb` +
mask-derived continuation bits — the same packed-varuint note as above),
and, upstream, the adjacent-equality suppression over the 12-byte line
entries is a shifted-compare mask. The line-table share also includes
1.44% of `ir_source_*_position` resolution, but that path is already a
paged binary search with a stepping memo the audits tuned (the checkpoint
comment records the failed alternatives); leave it.

The object writer's symbol-name work (`object_symbol_name_index_add`,
`buster_hash_64`, ~0.9% together with other string traffic) is a hash
build over a string population — the Muła "searching in unique constant
dictionary" group-probe layout and `vpconflictd` batch dedup both fit,
but at this share it only earns a kernel after the bigger items above.

## 6. Selection (6.7%): near its scalar floor, batch the walks that remain

`19b` moved the selector's facts into SoA streams accumulated inside the
target-order walk, and the row append is a typed cursor store. What is
left is inherent: a linked `instruction->next` walk over 64-byte rows
(nearly sequential in memory, so prediction and prefetch already work),
per-opcode dispatch that emits 1–4 rows, and value-map random access. The
`IrInstruction` row split is measured-negative; do not revisit. The
batchable remainder is the pre-walks: the promotable-local qualification
and the two reverse aliasing sweeps re-scan every operand of every row.
Both can consume one compacted `(operand value, row, opcode-class)`
stream built once per function — the same compress-shape as the prepass
proposal, and the same measure-first caveat. Expected value here is
modest; selection's next big step is more likely another boundary
removal in the `19a`/`19b` style than a lane kernel.

## 7. QUALITY and scheduling (cold today, catalogued for when they matter)

The default pipeline never runs `machine_schedule.c` or the QUALITY
allocator, so nothing here moves the stage-1 gate. When quality-mode
compile time matters: per-block pressure modeling is a +1/−1 event
prefix-sum (a textbook 16-lane scan), the greedy ready-set choice is an
argmin over compact keys (`vpminud` reduction), QUALITY's foreclosure
pass shares the prepass template/deinterleave kernels wholesale, and
interval-overlap probes over the sorted span arrays are
`vpcmpud`-mask counts. The allocation-side levers on the self-host
workload were declared exhausted at `11l`; these are throughput levers
for the pass itself, not for its output.

## Zen 4 / Zen 5 economics the kernels assume

Per the AGENTS tuning rules: kernels are written at 512 bits, validated
on Zen 4 (double-pumped, break-even required), shaped for Zen 5 (native
width, roughly 2× the same code). Specifics leaned on above: `vpcmpd`/
`vpternlogd`/`vpermt2d`/`vpcompressd` are full-rate or near it on both;
`vpcompressb` is ~9 cycles on Zen 4 vs ~5 on Zen 5 (safe, per AGENTS);
gathers are ~1 lane/cycle on Zen 4 and materially faster on Zen 5, so
gather-based variants should be A/B'd per generation; scatters stay out;
one 512-bit store per cycle on Zen 4 makes store-bound batches (the
encoder packer) Zen 5-biased by construction; masks stay `u64` on the
scalar ALUs per the existing vocabulary contract. Keep hot streamed
buffers 64-byte aligned; check uops.info before adopting any two-source
permute or conflict-detection instruction in a hot loop.

## Scalar finds recorded in passing (fund the SIMD work)

Noticed while profiling, all pre-SIMD in the doctrine's ordering:

1. `codegen_generate_canonical_module_attempt`'s capacity loop divides by
   `slot_alignment` per value module-wide (`codegen.c` frame-estimation
   loop; the `div` is ~11% of the symbol's samples, ~0.2% of stage 1).
   Alignments are powers of two; a mask replaces the divide.
2. `codegen_canonical_x64_function_shape` rescans every instruction of
   every function for atomics/inline asm (over 15% of the same symbol's
   samples): a per-function opcode-presence summary bit set during
   lowering deletes the scan.
3. The encoder's per-row `BusterX86MetadataMachineExactQuery` by-value
   construction is the hottest single instruction in
   `machine_encode_x86_64` — prepared plans (section 4) subsume it.

## Suggested landing order

1. Dword vocabulary seed (`splat_word`, `equal_word`) + rewrite
   `machine_fast_free_candidates` on it — smallest change, direct
   measured motivation, unlocks everything in section 2.
2. Prepared plans for prologue/edit emission (section 4, first bullet) —
   the largest single expected win; no vectors required.
3. Scalar finds 1–2 above (each a one-evening change with a clean A/B).
4. Register-file kernels + dirty-bitmask/edge-index layout (section 2).
5. Prepass opcode-template table, then the 8-row deinterleave batch
   (section 3), extending the vocabulary as the kernels demand.
6. GPR run-length census; build the batch packer only if the census says
   the runs are there (section 4, second bullet).
7. DWARF LEB batch emitter (section 5) as a self-contained exercise of
   the varint kernel the packer also wants.

Every step keeps the standing gates: byte-identical `test_self_host` at
every lane count, the differential scalar fallbacks compiled and tested
via `-march=x86-64`, `test_all_combinations_ci` locally before push, and
numbers quoted only from clang-built binaries under the seven-pair
pinned-instruction protocol.
