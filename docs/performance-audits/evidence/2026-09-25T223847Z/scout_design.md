# Design scout: IR construction storage — alternatives, red team, verdict

Revision: main `ade6ac4b` (tree `4c530622`). Prototype under review: `scratchpad/wt-proto` (`git diff`: `c_gen.c:c_ir_publish_function_rows` plus the new test `c_test.c:c_test_published_function_rows`) and its binary `scratchpad/proto/ide-proto`. Nothing under `/home/user/buster` was modified.

All numbers are **instrumentation**: deterministic counts, bytes, minor faults and RSS in this 4-vCPU VM with THP=madvise, so the VM ran on 4 KiB pages. **No timing conclusions.** Where I state a wall-time bound, it is arithmetic on the committed 9700X audit and is labelled as such.

## Tools and evidence classes

- **Source reading.** Read, Grep and sed over `c_gen.c`, `ir.c`, `ir.h`, `ir_append.h`, `ir_promote.c`, `ir_cfg.c`, `ir_fast.c`, `codegen.c`, `machine.c`, `wasm.c`, `bitcode.c`, `driver.c`, `arena.h`, `arena.c`, `os.c`, and the tests.
- **gdb Python probes on the unmodified `build/Release/ide`.** The probed objects are byte-identical to the baseline, sha `26f11f7e…`.
  - `design/fn_probe.py`: per-function counts, capacities and array addresses at the first `ir_prepare_canonical_module`. Output: `design/fn_stage1.json`.
  - `design/scratch_probe.py`: lowering-scratch position and dirty mark at the success-path `scratch_end(lowering_temporary)` (`c_gen.c:50931`), for all 5,206 functions. Output: `design/scratch_stage1.json`.
  - `design/sizes.gdb`: `sizeof` of the relevant structs.
- **Python models** over the probe data:
  - page and huge-page unions of the current layout;
  - capacity-policy simulations;
  - a scratch-layout model that uses the measured per-function scratch.
- **Executions.** One process at a time, niced, via `rusage_run.py`:
  - `build/Release/ide` against the integrator's `proto/ide-proto` on stage-1, default mode and `-fno-frontend-ssa`;
  - synthetic single-function inputs `design/big_*k.c`.
- **GitHub, read-only** (`issue_read`): #531, #546, #52 with its comments, and #525.
- **Integrator artifacts, read and not re-run:** `proto/differential.log`, `proto/self_host.log`, and the `proto/*poison*` objects.

Stage-1 facts used throughout (probe = census; VERIFIED):

- 5,206 lowered functions; 1,931,876 body tokens; median body N = 105; 1,880 functions have N < 64.
- The largest function is `codegen_generate_canonical_module_attempt`: N = 106,970, 90,211 rows, a capacity footprint of 36.4 MB and 8.7 MB used.
- At lowering end, the rows used are 1,524,516 instructions, 1,315,524 values and 168,748 blocks. With 64 + 12 + 16 + 64 B per row respectively, that is **147.7 MB**:
  - instructions: 97.6 MB;
  - sources: 18.3 MB;
  - values: 21.0 MB;
  - blocks: 10.8 MB.
- Handed out today: 672.6 MB (census c_gen.c:50653/50655/50661/50663).
- Resident today: 237.7 MB of 4 KiB pages at `ir_prepare` entry (53,070 used-prefix pages plus 4,974 beyond-count pages, from the lifetime scout).
- Per-function ratios:

  | Ratio | Median | p90 | p99 | Max |
  |---|---:|---:|---:|---:|
  | instructions / N | 0.85 | 1.41 | 2.67 | 16.75 |
  | values / N | 0.71 | — | — | — |
  | blocks / N | 0.104 | — | 0.52 | — |

- Correction to the access scout: "the largest has 360,844 rows" is wrong. 360,844 is the *byte* maximum of `ir_promote.c:488` (90,211 × 4 B). The largest function has 90,211 rows (5.8 MB).
- Lowering scratch, measured on main: high-water **84.3 MB**, set by the largest function at 788 B/body token. At function end, functions with N ≥ 4,096 hold 411–825 B/token (median 534).

---

## 1. Comparison table

Designs:

- **M.** Main.
- **F.** The proposal as prototyped.
- **F\*.** F with the fixes from §2: dedicated construction scratch, promotion reserve, and optional publish fused into `c_ir_ssa_finish`.
- **A.** Tighter pre-count estimate (issue #52's scope). Shown at 1.5N instructions, 1.25N values and 0.15N blocks.
- **B.** Geometric growth from 16 in the TU arena.
- **C.** Per-representation dedicated persistent arenas: each function's arrays are the tail, trimmed in place.
- **D′.** Module-wide flat rows carved from TU-arena chunks. Four chunks are reserved in the TU arena before the loop, each sized to Σ of the per-function estimates. Each function carves its arrays at the chunk cursor at its estimate, and the cursor is trimmed to the count at finish. This is D implemented without new arenas and without offsets in `IrFunction`.
- **E.** Keep the layout and use madvise/decommit, or a THP policy.

### 1a. Memory and work (stage-1; M, F and B/A rows are measured or model as marked)

| | TU bytes handed (virtual) | 4 KiB touched (IR) | THP-zeroed (IR share) | New copy work | Scratch high-water | Windows commit (IR) | Alignment of rows | Growth behaviour |
|---|---:|---:|---:|---:|---:|---:|---|---|
| **M** | 672.6 MB (census) | 237.7 MB (measured) | IR prefixes touch 336 distinct 2 MiB frames = 704.6 MB, shared with other lowering data (probe addresses) | 0.65 MB (31 grows, census) | 84.3 MB (measured) | ≈673 MB | 8 B; 87% of instruction arrays not 64-B aligned | doubling in TU, rare |
| **F** | 147.7 MB + ≤0.29 MB padding | **measured −15,822 minflt (−64.8 MB), maxrss −62.5 MB** (1,123,388→1,060,884 KiB) | model: lowering interval shrinks by ~525 MB handed, so roughly −500 MB zeroed; scratch +~19 MB once | **+147.7 MB memcpy** (read scratch + write TU) | model **≈120.7 MB** (84.3 + 36.4) | TU ≈148 MB; scratch +36 MB (pooled) | instructions 64 B (values 4 B, blocks 8 B) | exact capacity; the first post-lowering append doubles (§2 R5) |
| **F\*** | as F | as F; no scratch interaction | as F | 147.7 MB, or about 0 extra read pass if fused into the SSA-finish remap loop (NOT VERIFIED) | lowering scratch unchanged at 84.3 MB; separate construction scratch ≤ max capacity footprint (36.4 MB) | as F | 64 B | as F, plus a one-shot reserve in `ir_promote_global` |
| **A** | model 306.5 MB | small gain: arrays stay ≥1 page apart for median N, so partial-page fragmentation remains (NOT VERIFIED numerically) | model: interval shrinks by ~367 MB handed | 6.5 MB (636 grow events, model) | 84.3 MB | ≈307 MB | 8 B unless also aligned (one-liner) | more frequent doubling |
| **B** | model 430.9 MB | ≈357 MB, **worse** (abandoned arrays are fully written) | worse than A | 208.9 MB copies, 36,627 grows (model) | 84.3 MB | ≈431 MB | 8 B | constant churn |
| **C** | ≈148 MB in 4 new arenas | ≈148 MB + value-peak overshoot, reused | ≈ dense | ≈0 (growth extends the tail in place) | 84.3 MB | ≈148 MB + 4 granules | 64 B automatic (64-B rows from a 64-aligned base) | in place during lowering; TU doubling after |
| **D′** | 672.6 MB virtual (as M) | ≈148 MB, dense and contiguous across functions | ≈148 MB; untouched chunk tail never faulted | ≈0 (fallback: TU doubling for the rare over-estimate) | 84.3 MB | ≈673 MB, unless 4 page-aligned tail decommits after lowering → ≈148 MB | 64 B if the chunk base is aligned | as C |
| **E** (MADV_NOHUGEPAGE / PR_SET_THP_DISABLE) | as M | as M | removes THP inflation for *every* sparse site (~0.8 GB excess, not only IR); forfeits huge-page TLB reach for the dense row sweeps | 0 | 84.3 MB | as M (per-range decommit would need about 20K calls) | as M | as M |

### 1b. Semantics and compatibility

| | Stable identity | Alias/overlap rules | Error-path ownership | Determinism, lanes (`parallelism.md`) | #531 (parallel lowering) | #546 (IR sharing) | Canonical IR free of frontend IDs | Change size / API |
|---|---|---|---|---|---|---|---|---|
| **M** | IDs = indices; arrays fixed after creation (except growth) | Capacity slack hides nothing; appends are capacity-checked | Rejected rows stay in TU (H4) | Per-TU; lowering arena from the thread-local pool | Arrays land in a shared TU arena, so lanes would need private arenas | Neutral | Yes | — |
| **F** | IDs unchanged; array pointers move **once** at function finish (the builder dies there) | No aliasing between the scratch copy and the published copy after publish | Rejection copies rows (safe; consumers never read them, §2 R4) | Deterministic (fit test uses reserved size and position, identical on every lane); per-lane pools hold a larger high-water | **Good fit:** private construction then a publish step. For lanes, "publish" must target a per-lane result arena or a serial merge. | Neutral (no representation change) | Yes | ~80 lines in one file; caller arena contract unchanged |
| **F\*** | as F | as F | as F | as F; a dedicated arena of non-default size is unmapped on destroy, which also trims H5 | as F | Neutral | Yes | ~+30 lines |
| **A** | as M | as M | as M | as M | as M | Neutral | Yes (token census only sizes) | small; this is #52 (closed; the estimator did not land, and main still uses one `lowering_capacity`) |
| **B** | as M | as M | as M | as M | as M | Neutral | Yes | small but pathological |
| **C** | Pointers never move after creation | Adjacent functions share an arena; OOB is guarded only by capacity checks (all appends check) | Rejection: rewind the tail or keep it; **new arenas must be destroyed by every `c_lower_to_ir` caller** (driver multi-input paths and hundreds of test sites that `scratch_end` the caller arena), otherwise H1-style leaks | Per-lane arenas: deterministic contents, addresses differ | Natural: one arena set per lane, no copy | Neutral | Yes | Large ownership and API change |
| **D′** | Pointers fixed after carve; the chunk is an internal sub-allocator | Same as C | Rejection: advance the cursor (keep) or rewind before the next carve; everything stays in the caller's arena | Serial carve order is deterministic | **Poor fit:** a shared cursor cannot be carved by lanes without per-lane chunks (sizes unknown) or a publish copy (that is F) | Neutral | Yes | Medium: pre-pass sums estimates, 4 cursors, trim |
| **E** | as M | as M | as M | Process-wide policy, Linux only | Neutral | Neutral | Yes | Tiny, but a policy change |

Readings:

- **A (1.5N)** gets about 70% of F's reduction in handed TU bytes, with no copy and none of F's risks. It gets almost nothing at 4 KiB, and it is the old #52 scope, closed without an estimator landing.
- **D′** dominates F on Linux serial lowering: zero copy, no scratch growth, no crash band, and every function's rows contiguous per representation (best for #792's module sweeps). It is worse for Windows commit (unless the four tails are decommitted) and for #531.
- **C** has D′'s memory profile, at a large ownership cost.
- **E** is the best *falsifier* rather than a design.
- **64-B alignment is not unique to F.** A one-line `arena_allocate_bytes(…, 64)` on main gives it too.

---

## 2. Red-team findings

**R1. The prototype aborts on valid large functions where main compiles. VERIFIED by execution.**

The fit rule in the prototype (`c_gen.c:c_lower_to_ir_with_options`) is `construction_bytes <= (lowering_arena->reserved_size - lowering_arena->position) / 2`. It admits bodies up to N ≈ 394.7K, because 340 B/token × N must be ≤ 128 MiB. The rest of per-function lowering scratch then needs:

- fixed arrays: `CIrLowerFrame` 88 B × ~3N, label metadata 13 B × 3N, prepared-call arrays 12 B × N;
- dynamic growth, such as `c_ir_ssa_event` doubling into `builder->scratch_arena`.

That was measured at 411–825 B/token on stage-1 (788 B/token for the largest function), so the remaining half overflows.

Synthetic straight-line single-function inputs (`design/big_*k.c`), run through `rusage_run.py`:

| Body tokens | main | ide-proto |
|---:|---|---|
| 250K | ok | ok |
| 270K | ok | ok |
| 290K | ok | ok |
| 310K | ok | ok |
| **330K** | ok | **abort: "validation failed at arena.h:191 in arena_allocate_bytes"** (gdb: `c_ir_ssa_event` allocating 10 MB) |
| **380K** | ok | **abort** |
| 395K | ok | ok (falls back to TU at capacity) |
| 420K | ok | ok |

For this cheap code shape the band is about [315K, 394K]. For denser shapes (825 B/token) arithmetic puts the lower edge near 163K.

- The graceful label-metadata reservation check (`c_gen.c`, "C IR label metadata capacity exceeds the lowering scratch reservation") becomes reachable at smaller bodies. That would reject valid source with a diagnostic. NOT exercised.
- **Fix (F\*).** Put the four construction arrays in a **dedicated construction scratch arena**. Reserve it once per TU at max over definitions of (capacity footprint) + slack; body_token_count is known before the loop. Leave `lowering_arena` untouched. That removes the band entirely and keeps each array at offset 0, which gives the tightest touched union.
- Pre-existing and out of scope: **main itself aborts at 700K tokens** (same `arena.h:191`, under `c_ir_lower_body_advance`). I tried to queue a separate suggested task via spawn_task; the call timed out and I did not retry.

**R2. Nothing reads rows, values, blocks or sources beyond `count`, or relies on a zero tail. VERIFIED by grep and reasoning; not an exhaustive audit.**

- The only capacity readers are:
  - the growth paths in `ir.c`: `ir_function_add_block`, `ir_function_add_value`, `ir_function_add_instruction`;
  - `ir_append.h:ir_instruction_append_trusted`;
  - builder side arrays sized from `value_capacity` (`c_gen.c:c_ir_mark_unsigned_bit_field_value`, and the label-metadata growth around `c_gen.c:4042`), which are sized during lowering, before publish.
- No backend, emitter or codegen file reads `*_capacity` (grep over src/buster/lib).
- No `instructions[count]`, `values[count]` or `blocks[count]` read exists (grep).
- Main already leaves stale rows above `count`, so no consumer can depend on zero tails:
  - `c_ir_ssa_finish` compacts values in place and sets `function->value_count = value_count` (c_gen.c ~6031), leaving 1.63M → 1.32M stale rows;
  - place-load retraction truncates counts (c_gen.c ~30186);
  - the TU temporal scope in `c_ir_value_integer_constant_evaluate` (c_gen.c:21819) dirties TU pages that later IR arrays reuse. The lifetime probe saw 4,974 present beyond-count pages.
- The integrator's poison build produced a byte-identical stage-1 (`proto/stage1_poison.o` = `26f11f7e…`). I read the hashes but not the poison patch: NOT VERIFIED what it poisons.

**R3. No raw pointers into the four arrays survive the function-finish point. VERIFIED for the structures listed; the 617 `function->values` sites in lib were not audited one by one.**

- `CIntegerIrBuilder` (holding the frames, prepared calls, label metadata stores, locals and `direct_ssa`) is a loop-local in `c_lower_to_ir_with_options`.
- Module-lifetime objects hold only token, type or entity IDs:
  - `CIrQueryMachine` / `CIrQueryFrame` (c_gen.c:1862/1905);
  - `CIrInitializerSlotCache`;
  - `CIrFunctionNameIndex`;
  - `CIrWideFloatCache`;
  - `CIrPointerTypeCache`;
  - `CIrConstantEntityIndex`.
- IR objects use IDs or point only into other TU objects, never at the row arrays:
  - `IrBlock` holds pointers to `IrBlockParameter` and `IrPredecessor`, which are TU objects (c_gen.c:5126, 5905), not row arrays. `IrBlockParameter` and `IrIncoming` point to each other only.
  - `extras`/`extra_instructions` (`ir.c:ir_instruction_extra_ensure`), `label_metadata*`, `local_places` (`c_gen.c:c_ir_record_local_place`, TU) and `debug_locals` (by value) are ID-keyed.
  - `IrPublishedCfg` has no row pointers (`ir.h`).
- `ir_instruction_self_id` recovers an ID from a row pointer, so it is valid only for the live array; it is used within passes, not across lowering finish.
- Minor inaccuracy in the prototype's comment "Consecutive functions' published rows are then contiguous". What sits between them is written data, not unwritten capacity: the next function's operands, immediates, block parameters and strings are allocated in the TU during its lowering, between the published row arrays (allocation order in `c_ir_publish_function_rows` and `builder->arena`). Only D′/C make the rows of all functions contiguous per representation.

**R4. Rejection path: no consumer reads a body-rejected function's rows. VERIFIED.**

- Every rejection appends a diagnostic, and the driver then leaves before `ir_prepare` (`driver.c:compiler_driver_execute_c_single`, `lowered.diagnostic_count` → `goto end`).
- Wasm and bitcode refuse `IR_FUNCTION_REJECTED` without reading rows (`wasm.c` ~1141, `bitcode.c` ~1668).
- Validation, FAST and codegen skip non-LOWERED functions (`ir.c:4503/4530`, `ir_fast.c:535/582/602`, `codegen.c:11622`).
- The tests that inspect rejected functions (`c_test.c:17254/17406/17501`) use signature-rejected functions, which `continue` before any array is allocated (count = 0).
- The prototype's copy-on-reject is therefore safe and preserves main's observable state at negligible cost. Nulling with count = 0 would also be safe today.

**R5. Post-lowering appends and exact capacity. VERIFIED.**

- The only production appender after lowering is `ir_promote.c:ir_promote_global` → `ir_function_add_value(program->arena, …)`.
- Grep finds no `ir_function_add_*` in `ir_fast.c`, `ir_cfg.c`, codegen, wasm, llvm or ebpf. `ir_rewrite_compact` compacts in place, and CFG publication permutes in place.
- With exact capacity, the first append doubles, allocating 2 × count in TU and copying count. The scratch copy is already dead by then; the old exact array becomes dead too.
- Measured (4 KiB, deterministic minflt; objects byte-identical main vs proto in both modes):

  | Mode | Parameters inserted | minflt change |
  |---|---:|---:|
  | default | 1,374 | **−15,822** |
  | `-fno-frontend-ssa` | 370,566 | **−5,811** (−63% of the gain; maxrss −22.4 MB) |

- Under THP the doubled arrays reintroduce holes.
- Mitigation: `ir_promote_global` already counts `parameter_count` before its append loop. Reserve once, to count + parameter_count, or publish values with headroom when the opcode summary still has LOCAL.
- Worst-case default-mode regrowth bound (the 193 largest-value functions): 9.5 MB copied and 19 MB allocated.

**R6. Codegen retry and CFG invalidation. VERIFIED.**

- `codegen.c:codegen_generate_canonical_module_with_trace` opens `attempt_scope` (l.23763) after `ir_prepare`. All construction arrays and promotion regrowths therefore sit below the scope and survive a retry.
- `ir_function_invalidate_cfg` (ir_cfg.c) allocates in `cfg->arena` and rewrites `next` in place; it never touches capacity.
- The latent H2 (`machine.c:machine_select_canonical_function_internal` publishing a CFG inside an attempt) is unchanged by F, because publication does not reallocate rows.

**R7. Tests.**

- `c_test.c:27061-27062` asserts `value_count > initial` and `value_capacity > initial`. That still holds, because capacity == count > initial. VERIFIED by reading.
- `ir_test.c:292` checks hand-built capacities. Unaffected.
- The construction-counter tests (`c_test.c:12048/12212`, `driver_test.c:1969`, `driver_fast_test.c:267`) compare deltas around semantics-only or whole-driver runs. None asserts `VALUE_GROWS == 0` for lowered code. VERIFIED by reading.
- The prototype's new test asserts exact capacities, 64-B alignment and ownership inside the caller arena for four small functions × 3 targets × 2 SSA forms. It covers neither rejection, nor growth during lowering, nor the R1 band.
- The integrator's `proto/differential.log` reports 2,516 objects compared with 0 mismatches, and `proto/self_host.log` ends `exit 0`. `proto/test_all.log` was empty when I read it: **test_all NOT VERIFIED green.**

**R8. A single huge function regresses at 4 KiB. VERIFIED by execution.**

For 250K–310K-token single functions, F gives minflt +3.2K to +3.9K (+9%) and maxrss +13 to +16 MB. The rows are touched twice (scratch, then TU), and at 4 KiB main's untouched capacity tail cost nothing.

**R9. Pool retention (lifetime H5) grows.**

- The model's scratch high-water goes from 84.3 to ≈120.7 MB.
- The lowering arena is thread-pooled (`arena.c:arena_destroy`, `ARENA_POOL_LIMIT` 16, thread-local), so every lane worker parks its high-water dirty pages.
- NOT measured on a multi-input build. F\* (a separate, non-default-size construction arena) avoids this.

**R10. The headline benefits are not all specific to F.**

- Alignment is a one-liner on main.
- Removing the THP inflation can be had for every sparse site with E.
- Density with zero copy is D′ or C.
- What is unique to F: exact-size TU publication with no ownership change, plus a natural #531 publish point.

---

## 3. Regression-risk workloads and quantified new work

| Workload | Effect of F (as prototyped) | Evidence |
|---|---|---|
| Stage-1, default mode | +147.7 MB copied (census `after_ssa_{instruction,value,block}_rows` × 76/16/64 B), + ≤0.29 MB padding; −15.8K faults and −62.5 MB RSS at 4 KiB | measured and census |
| Tiny TU / many tiny functions (1,880 functions with N < 64 carry 6.0 MB of rows) | Per function: 4 extra allocations + 4 small memcpys; padding ≤56 B per function. Neutral. | model |
| One enormous function | N in about [163K–315K, 394K]: **abort** (R1). Below that: +9% faults (R8). Above 394K: falls back to M. Copy of rows > L3 streams DRAM twice. | measured and arithmetic |
| Functions that grow after lowering (`-fno-frontend-ssa`; functions with `&&label`/asm, which disable direct SSA; >65,535 locals) | One doubling per promoted function; gain eroded 63% on stage-1 | measured |
| Multi-input lanes | +~36 MB parked high-water per worker for stage-1-sized TUs (up to 16 pooled arenas per thread) | model; NOT measured |
| Windows hosts | Large win: TU commit −~525 MB; scratch +36 MB committed once | source (`os.c:os_commit` = `VirtualAlloc(MEM_COMMIT)`; Linux = `mprotect` on a MAP_NORESERVE RW mapping, VERIFIED) |

Honest upside bound (arithmetic, not a measurement):

- The #906 9700X audit has sys time of 0.090–0.113 s out of 1.545 s wall, with 77.7% of kernel cycles in `clear_page_erms`, so roughly 70–88 ms of clearing for about 1.95 GB.
- F removes about 500 MB of that (model), which is ≤ about 18–23 ms (≈1.2–1.5% of wall), before subtracting a 148 MB cache-warm memcpy.
- At 4 KiB the gain is a 5.6% cut in minor faults.
- This is a memory, commit and fault improvement with at most a low-single-digit throughput upside, unless the 64-B alignment and density measurably speed up the ~29 row sweeps.

---

## 4. Verdict and falsifiers

**Verdict.** F is a *defensible* first slice, but **not as prototyped**, and not clearly the best.

Why it is defensible:

- It is small and local to one file.
- It keeps every returned byte in the caller's arena, so there is no ownership change for the driver or the hundreds of test call sites.
- It is output-identical: my stage-1 runs in both SSA modes, plus the integrator's 2,516-object differential and self-host.
- It measurably cuts faults and RSS (−15.8K faults, −62.5 MB).
- It is the shape #531 would need: private construction, then publish.

What must change before landing:

- (1) Fix R1: a dedicated construction scratch reserved from the TU's largest body, or at minimum a fit rule that accounts for the other ≥825 B/token.
- (2) Add a regression fixture in the R1 band.
- (3) Add the promotion reserve (R5).
- Optionally, (4) fuse the publish into `c_ir_ssa_finish`'s remap to drop the extra read pass.

Is it the best? For Linux serial lowering, **D′** (carve-and-trim chunks in the TU arena) appears to dominate it:

- same 4 KiB and THP savings;
- zero copy;
- no scratch growth and no crash band;
- all rows contiguous per representation.

F keeps two real advantages: Windows commit (D′ needs four tail decommits to match) and #531 compatibility. **A** is the low-risk fallback if the THP benefit turns out to be the only one that matters.

**Cheap falsifiers**, in order of cost:

1. **Done here, not falsified.** Deterministic minflt at 4 KiB, main vs proto. Predicted about −19K by the model; measured −15.8K.
2. **THP fault count** on the 9700X, or here if the integrator can run a THP=always pass. Take the `thp_fault_alloc` delta from `/proc/vmstat` per stage-1 compile, main vs proto. F predicts ≥ about 240 fewer 2 MiB faults (≈ −500 MB zeroing). **Fewer than ~100 fewer falsifies the THP rationale.**
3. **Policy counterfactual (E).** Run main with THP disabled for the process (a `prctl(PR_SET_THP_DISABLE)` wrapper) on the 9700X.
   - If wall and sys do not move beyond noise, F's THP-derived upside is also nil and F is a memory-only change.
   - If main-without-THP ≈ proto-with-THP, a policy knob matches F.
4. **R1 fixture.** A 330K-token single function must compile. It currently aborts in proto: an immediate reject until fixed.
5. **F vs D′ A/B.** Prototype D′ (similar size: a pre-pass sum, 4 cursors, trim) and compare minflt, THP faults and callgrind Ir of the copy. **If D′ is ≤ F on faults with zero copy, F is not the best serial slice.** Keep F only if #531 or Windows is the deciding constraint.
6. **Multi-lane RSS.** `-fcompile-jobs` over N stage-1-like TUs, main vs proto. A growth of more than ~30 MB per lane in peak RSS means the scratch placement must move (F\*).
