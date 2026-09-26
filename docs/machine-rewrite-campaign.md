# x86-64 machine rewrite campaign

[Agent instructions](../AGENTS.md) · [Machine backend guide](agents/machine.md) · Paths and commands below are relative to the repository root.

This document records a bounded superoptimization campaign over the native
x86-64 code Buster emits: how frequent machine-code windows were measured,
how replacements were searched for and proved, which rewrites were accepted,
deferred or rejected, and how to regenerate every number. It is the durable
methodology; per-change status lives on the owning issue and pull requests.

The campaign adds no pass, IR, pattern language or runtime dependency.
Accepted rewrites are encoding choices inside the existing x86-64 MIR
encoder (`machine_encode_x86_64` in
`src/buster/lib/compiler/codegen/machine_x86_64.c`), each emitted through the
checked x86 metadata authority (prewarmed fixed templates or the shape
cache), each guarded by a stated precondition the encoder can decide
locally, and each proved equivalent under that precondition.

## Evidence base

| Item | Value |
|---|---|
| Source | `main` `ade6ac4b6ecb21f30b61b656439bac476c145e2f`, tree `4c5306221fdb22fccc929b55e333163742de17d0` |
| Measuring compiler | Release `ide`, Clang 18.1.3, hosted-style Clang bootstrap of `build.c` (no TCC in the container) |
| Target | `x86_64-linux`, FAST allocator (the production default), host CPU model |
| Self-host corpus | `src/buster/apps/ide/ide.c` unity build of the pinned source, `-DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g0`: 5,205 functions |
| External corpus | Pinned compatibility inputs, `-O2 -march=baseline -g0`: Lua `6e22fedb74cf0c9b6656e9fce8b7331db847c605`, zlib `51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf`, cJSON `c859b25da02955fef659d658b8f324b5cde87be3`, LZ4 `ebb370ca83af193212df4dcbadcc5d87bc0de2f0`, yyjson `8b4a38dc994a110abaec8a400615567bd996105f`, QuickJS `3d5e064e9dd67c70f7962836505a7fa067bf0a4e` (57 objects, 2,084 functions) |

Five external units are excluded because the pinned compiler rejects them
for frontend reasons unrelated to code generation: Lua `ltablib.c` (comma
expression in a `while` condition), zlib `gzread.c` and QuickJS `quickjs.c`,
`dtoa.c`, `quickjs-libc.c` (`*--p` modification destinations and a
`void *` to function-pointer conversion). SQLite is not included: its
download host is not reachable from the measuring container.

## Method

### 1. Census

`tools/machine_pattern_census.py` disassembles objects with GNU objdump
(Intel syntax), splits functions into basic blocks at branch targets and
after terminators, and solves conservative register and flag liveness per
function (calls read the System V argument registers and clobber the
caller-saved set; returns keep results and callee-saved registers live;
unknown instructions read everything). It counts every window of one to
three (optionally four) consecutive instructions inside a block, normalized
so that a rewrite rule can depend on exactly what the key keeps:

* mnemonic and operand widths;
* register relations as window-local names (`A`, `B`, ...), with `rsp` and
  `rbp` literal;
* memory operands as base, index and scale plus window-local displacement
  names; a displacement within sixteen bytes of an earlier one is named
  relative to it (`d0+8`), so adjacent frame chunks remain visible, and each
  name records the width its value needs (`:8` or `:32`);
* immediates by class: zero, one, all-ones, power of two, signed 8-bit,
  signed/unsigned 32-bit, 64-bit.

Each key carries its instance count, encoded bytes, a histogram of which
window-written registers are live after the window, the number of instances
with any flag live after it, immediate values and example locations.

Attribution to MIR used a research-only encoder trace that is not part of
any commit: it records, per emitted chunk, the MIR row opcode, allocator
edit kind or prologue that produced it, and prints `ATTR <text offset>
<tag>` lines after a function's bytes are committed. The traced compiler
emits byte-identical objects. `--attribution` joins the trace to the census
by `.text` offset. The trace patch is reproduced in the campaign issue.

### 2. Ranking

Windows are ranked by instance count and encoded bytes, then split into
precondition classes by their dominant liveness signatures and by whether a
flag is live after the window. A class's plausible benefit is its instance
count times the bytes a verified replacement saves.

### 3. Bounded search

`tools/machine_rewrite_search.py --search` enumerates, for each ranked class,
every replacement of at most two instructions (and never more instructions
than the window) from a closed vocabulary instantiated over the window's own
registers, memory operands and immediates plus one dead XMM scratch:
register/immediate/memory moves, the zero idiom, two-operand ALU and compare
forms, unary forms, zero/sign extension, stores, and sixteen-byte `MOVUPS`
transfers where the window touches two adjacent eight-byte chunks. A
candidate survives only if it agrees with the window on every live output
(registers, consumed flags, all written memory) for sixteen randomized
bindings that include edge values; survivors are ordered by an approximate
size model. Survivors are leads: a rule enters the catalog only after the
proofs below and a review of its integration preconditions.

### 4. Semantic domain and proof strategy

The model covers the integer and data-movement subset the selector emits:
general registers with 8/16/32/64-bit views (32-bit writes zero-extend,
8/16-bit writes merge), 128-bit XMM registers used as data, the six
arithmetic flags with the architecture's undefined results modelled as
undefined, byte-addressed memory, and `PUSH`/`POP`/`LEAVE`. An undefined
flag equals nothing, so a replacement may leave a flag undefined only where
it is dead. No Buster-emitted consumer reads AF (`Jcc`, `SETcc`, `CMOVcc`
read CF, PF, ZF, SF and OF), so "all consumed flags" excludes AF.

`--verify` re-proves every catalog rule with mechanisms that fail
independently:

1. **Exhaustive reduced width.** Registers, frame cells, immediates and (when
   read) flags are enumerated completely at 8 bits (4 bits when the input
   space is too large), with 64/32/16/8-bit views scaled to W/W÷2/W÷4/W÷8.
   Used only when the rule is structurally width-generic; XMM data, stack
   forms and pointer aliasing are not.
2. **Randomized full width.** Thousands of random bindings and inputs,
   weighted toward sign, carry and width boundaries.
3. **Z3 proof.** The same model evaluated over bit-vectors: registers, flags,
   immediates and displacements are symbolic, memory is an array, and the
   solver must show that no input satisfying the preconditions distinguishes
   the two sides. Z3 is an optional research import.
4. **Emulation.** Both sides are encoded, executed in the Unicorn x86
   emulator from identical random states, and compared with each other and
   with the model. Unicorn shares no code with the model.
5. **Encode/decode round trip.** Both sides are assembled by Buster's own
   assembler (`--ide`) and by GNU as, for legacy and REX-extended register
   bindings; the bytes must agree, and objdump and Capstone must decode the
   expected instruction counts. The claimed byte deltas come from these
   encodings, not from the size model.
6. **Negative tests.** Each stated precondition is removed in turn; random
   testing or Z3 must then find a counterexample, so no precondition is
   decorative. Rules recorded as invalid must fail every mechanism.

Control-flow rules are proved separately over all sixteen condition codes
and all 64 flag states: condition inversion is exact negation in the model,
and Unicorn executes both layouts of every `Jcc`/`JMP` pair, a `JMP` to the
next instruction, and rel8 against rel32 forms.

### 5. Integration rule

A proved rule is integrated only where its precondition is decidable by the
smallest existing mechanism. For this campaign that is the x86-64 encoder:
it already chooses each row's encoding, owns allocator edits, block order and
branch fixups, and emits through checked metadata. A precondition that
needs facts the encoder does not have (register liveness across rows, value
facts, canonical constant folding) makes the rule **deferred** with its
owner named, never an encoder heuristic.

## Rule ledger

See [the results section](#results) for measured coverage. Status is one of
*accepted* (proved and integrated), *deferred* (proved, not integrated;
integration named), *rejected-policy* (proved but refused) or
*rejected-invalid* (not an equivalence; every mechanism finds a
counterexample).

| Rule | Pattern → replacement | Preconditions | Status and integration |
|---|---|---|---|
| `frame-displacement-disp8` | `mov [rbp+disp32], r` / `mov r, [rbp+disp32]` → same with disp8 | final displacement is a nonzero signed byte | accepted: frame chunks (spills, reloads, edge temporaries, copy frame sides) pick the prepared disp8 record |
| `self-copy-64` | `mov r64, r64` (same register) → nothing | none | accepted: allocator `COPY` edits onto their own register emit nothing |
| `store-reload-same-slot-64` | `mov [s], r; mov r, [s]` → `mov [s], r` | 64-bit, adjacent, same block, the load is an allocator reload | accepted: `RELOAD`/`TEMP_RELOAD` edits right after the matching spill emit nothing |
| `zero-idiom` | `mov r32, 0` → `xor r32, r32` | no flag live after | accepted: `MOV_RI` rows and `REMATERIALIZE` edits of zero, with the block-local flag-liveness proof below |
| `epilogue-leave` | `mov rsp, rbp; pop rbp` → `leave` | System V frame without callee-saved pushes | accepted: `RET` rows; Win64/UEFI save-first frames unchanged |
| `jmp-to-next` | `jmp L; L:` → `L:` | `L` is the next emitted block; no edit after the terminator; no asm-goto addend | accepted: `JMP` rows |
| `jcc-fallthrough` | `jcc T; jmp F; F:` → `jcc T; F:` | as above | accepted: `JCC` rows emit only their `Jcc` step |
| `jcc-inversion` | `jcc T; jmp F; T:` → `jncc F; T:` | as above; condition is a plain x86 nibble | accepted: `JCC` rows |
| `frame-copy-16`, `frame-copy-24`, `frame-copy-16-same-object` | 8-byte GPR chunk pairs → 16-byte `MOVUPS` pairs through XMM0 (+ GPR tail) | objects disjoint or identical; XMM0 dead | accepted: `COPY_FRAME_FROM_FRAME` |
| `pointer-destination-copy-16`, `pointer-source-copy-16` | as above with one pointer side | objects disjoint or identical (C11 6.5.16.1p3); XMM0 dead | accepted: `COPY_PTR_FROM_FRAME`, `COPY_FRAME_FROM_PTR` |
| aggregate-copy row plan | whole row: 8/4/2/1 plan → 16/8/4/2/1 plan, ascending, each chunk loaded before it is stored | none beyond the former plan's | accepted: checked for every size through 256 bytes and every source/destination overlap; exact wherever the former plan was, so the rows gain no aliasing assumption |
| `zero-compare-to-test` | `mov a32, 0; cmp b64, a64` → `test b64, b64` | `a` dead after; AF unobserved | deferred: needs a compare-with-immediate MIR form or row-level liveness |
| `copy-forward-into-add` | `mov a, b; add c, a` → `add c, b` | `a` dead after | deferred: the copy is an allocator `COPY` edit; its death is an allocator fact (FAST follow-up) |
| `byte-zero-extend-drop-rex-w` | `movzx r64, r/m8` → `movzx r32, r/m8` | legacy registers (else no saving) | deferred: one byte on legacy registers only; four exact-form keys would need regeneration |
| `constant-sign-extend-fold` | `mov a32, k; movsxd b64, a32` → `mov b32, k` | `k` nonnegative; `a` dead after | deferred: a canonical-IR constant-cast fold, not a machine rewrite |
| `add-one-to-inc` | `add r, 1` → `inc r` | CF dead | rejected-policy: no MIR row emits `add r, 1` outside address folding |
| `self-copy-32-is-not-a-nop` | `mov r32, r32` → nothing | — | rejected-invalid: clears bits 63:32 |
| `store-reload-same-slot-32-is-not-a-nop` | 32-bit store/reload pair → store | — | rejected-invalid: the reload zero-extends |

Other leads the census exposed were not admitted because their precondition
is not a local encoding fact or their benefit is not a static size win:
removing the small-frame page probe (a stack-clash policy), `ENTER` for the
prologue (shorter but microcoded and slower, and the unwind records name the
fixed prologue shape), `IMUL r, r, 2^k` to shifts or `LEA` (no byte saving),
and the redundant constant materializations and copies that only register
allocation or canonical IR folding can remove.

### The flag-liveness proof

`machine_x64_flags_dead_at` decides the zero idiom's precondition. It is
the least fixed point of backward liveness with one bit per block:

* a **reader** is any row whose opcode metadata carries
  `MACHINE_OPCODE_ROW_FLAGS_USE` (`SETCC`, `JCC`), plus inline assembly;
* a **killer** is a row whose instruction unconditionally writes every
  consumed flag: `CMP`, `TEST`, the two-operand `ADD`/`SUB`/`AND`/`OR`/`XOR`
  rows and `NEG`;
* every other row is transparent, including rows that write the flags only
  partially or conditionally (shifts by CL, `BSF`/`BSR`, `IMUL`, calls and
  every expansion), and allocator edits never touch the flags;
* a block's successors come from its `JMP` or `JCC` terminator, `RET` and
  `UD2` have none, and any other ending is assumed to read the flags.

The proof is only ever conservative: a row not known to overwrite every
consumed flag never ends a live range, and a block without a recognized
terminator keeps the flags live on exit. Row expansions that use `ADC`,
`SBB`, `SETcc` or `Jcc` internally (128-bit atomics, `SWITCH`, float
compares) consume only flags produced earlier in the same expansion, so they
are not readers of incoming flags.

## Results

All numbers compare the pinned `main` compiler with the same compiler plus
each change, on the frozen corpora above. Code bytes are the `CODEGEN
code_bytes` totals printed by `ide cc -v`; machine instructions are counted
from `objdump -d`, excluding alignment `NOP`s. MIR row counts are identical
in every column (1,336,056 self-host, 235,948 external): every rewrite is an
encoding choice.

### Static size and instruction count

| Stage | Self-host bytes | Self-host instructions | External bytes | External instructions |
|---|---:|---:|---:|---:|
| `main` | 21,421,838 | 3,745,765 | 1,717,757 | 358,203 |
| + local rewrites | 19,572,837 (−8.63%) | 3,684,370 (−1.64%) | 1,462,352 (−14.87%) | 350,999 (−2.01%) |
| + branch layout | 19,324,624 (−1.27%) | 3,634,647 (−1.35%) | 1,420,775 (−2.84%) | 342,811 (−2.33%) |
| + aggregate copies | 15,690,141 (−18.81%) | 3,087,621 (−15.05%) | 1,415,767 (−0.35%) | 341,915 (−0.26%) |
| **total** | **−5,731,697 (−26.76%)** | **−658,144 (−17.57%)** | **−301,990 (−17.58%)** | **−16,288 (−4.55%)** |

Stage percentages are relative to the previous row. All 5,205 self-host and
2,084 external functions stay on the MIR path (zero fallbacks), and the
driver's strict `-fno-machine-fallback` legs pass for every new fixture.

### Coverage by rule (self-host object)

Counted from `objdump` before and after the stage that owns each rule; the
local-rewrite rows sum exactly to that stage's byte change.

| Rule | Instances | Bytes |
|---|---:|---:|
| `frame-displacement-disp8` | 317,787 frame chunks move from disp32 to disp8 | −953,361 |
| `zero-idiom` | 207,177 of 208,381 `mov r32, 0` (1,204 keep: flags live) | −621,531 |
| `store-reload-same-slot-64` | 44,231 of 44,546 adjacent 64-bit store/reload pairs (the other 315 are not a spill edit followed by its reload in one block) | −222,215 |
| `self-copy-64` | 10,857 of 10,859 same-register copies (2 are `CVT_U64_TO_F64` internals) | −32,571 |
| `epilogue-leave` | 6,307 returns | −18,921 |
| `jmp-to-next`, `jcc-fallthrough`, `jcc-inversion` | 49,723 `JMP`s | −248,615 |
| aggregate-copy rules | 273,513 sixteen-byte chunks (547,026 `MOVUPS`) | −3,633,675 |

The branch rules leave 330 `JMP`s to the next instruction (asm-goto landing
addends and terminators followed by allocator edits) and 39 invertible
`Jcc`/`JMP` pairs. Falling through also places 7,142 spills directly before
a reload of the same slot at the start of the next block; the same-block
precondition keeps those reloads (see deferred leads). The copy rules cover
every sixteen-byte chunk of every aggregate-copy row; the search's
four-instruction leads (305,588 frame, 61,617 + 59,111 pointer chunk pairs)
are their census footprint.

### Dynamic instruction counts

Valgrind (`--tool=cachegrind --cache-sim=no`) counts executed instructions,
independent of the host's timing. Workloads link pinned zlib and LZ4
sources compiled at `-O2 -march=baseline` with deterministic inputs and
self-checking outputs (the two small driver programs are in the campaign
issue); both builds print identical results.

| Workload | `main` | All changes | Change |
|---|---:|---:|---:|
| zlib: deflate level 6 + inflate, 1 MiB | 984,489,982 | 962,692,699 | −2.21% |
| LZ4: fast and HC-9 round trips, 1 MiB | 1,390,781,561 | 1,335,748,815 | −3.96% |

Text sections shrink from 261,458 to 232,284 bytes (zlib) and from 130,176
to 110,561 bytes (LZ4).

### Correctness evidence

Proofs (`tools/machine_rewrite_search.py --verify`, final catalog):

| Rule | Reduced width | Random full width | Z3 | Unicorn | Encoders agree | Bytes (legacy / REX) | Preconditions shown necessary |
|---|---|---|---|---|---|---|---|
| `self-copy-64` | 8-bit, 256 cases | 3,000 trials | proved | 200 trials | yes | 3→0 / 3→0 | none stated |
| `store-reload-same-slot-64` | 8-bit, 65,536 | 3,000 | proved | 200 | yes | 14→7 / 14→7 | none stated |
| `zero-idiom` | 8-bit, 256 | 3,000 | proved | 200 | yes | 5→2 / 6→3 | no flag live after |
| `epilogue-leave` | not width-generic | 3,000 | proved | 200 | yes | 4→1 / 4→1 | none stated |
| `frame-copy-16`, `frame-copy-24`, `frame-copy-16-same-object` | not width-generic | 3,000 | proved | 200 | yes | 28→14 / 28→16; 42→28 / 42→30 | objects disjoint |
| `pointer-destination-copy-16`, `pointer-source-copy-16` | not width-generic | 3,000 | proved | 200 | yes | 21→10 / 21→12 | objects disjoint or identical |
| `frame-displacement-disp8` | — | — | — | 684 forms × 4 states | GNU as, Capstone | −3 per form | — |
| aggregate-copy row plan | 74,240 size/overlap cases | — | — | — | — | — | — |
| branch layout | 16 conditions × 64 flag states | — | — | 1,152 runs | rel8/rel32 decode | — | — |
| deferred rules (`zero-compare-to-test`, `copy-forward-into-add`, `byte-zero-extend-drop-rex-w`, `constant-sign-extend-fold`) and `add-one-to-inc` | proved | proved | proved | agree | yes | see ledger | each stated precondition |
| `self-copy-32-is-not-a-nop`, `store-reload-same-slot-32-is-not-a-nop` | counterexample | counterexample | counterexample | counterexample | yes | — | — |

Compiler gates on the final commit (Linux x86-64 container, Clang 18.1.3;
`test_all` also on each intermediate encoder change):

* `test_all`: every module passes except two
  `compiler_driver_test_wide_vector_boundaries` assertions that only check
  the host Clang compile of a fixture with `-march=znver5`, which Clang 18
  rejects on this AVX-512 host; they do not involve Buster output.
* New self-checking fixtures `tests/basic_c_machine_rewrites.c`,
  `tests/basic_c_branch_layout.c` and `tests/basic_c_aggregate_copies.c` run
  under the driver in the NONE, MIR_STACK, FAST and QUALITY allocators with
  `-fno-machine-fallback`, and agree with Clang. The machine tests add a
  JIT-executed zero-idiom flag test (a zero between `CMP` and `SETCC`, across
  a `JMP` edge, and with dead flags) in every allocator; the frame-chunk
  golden bytes now expect disp8.
* `test_self_host`: fixed point, `ide` 40,868,368 bytes (46,675,696 on
  `main`).
* `test_self_host_audit`: `BOOTSTRAP PASS generations=3 repetitions=2;
  tokens, canonical IR, selected MIR, diagnostics, binary fixed point;
  probes=none,mir-stack,fast,quality`.
* `test_mode_matrix`: 24 legs pass (4 native, 20 oracle-checked; no `qemu`
  or `wine` in the container).
* `test_differential --ide build/Release/ide --cc clang --sanitize-oracle`:
  20 cases (14 permanent, 2 rejection controls, 4 generated), 432
  configurations each, 0 failures. The container's Clang first lacked its
  sanitizer runtime (`libclang-rt-18-dev`); every case then failed at the
  host reference link, before any Buster output was compared.

The compiler's own dynamic instruction count, with the source pinned: the
`main` compiler source built for Haswell once by `main` and once by the
final compiler, each compiling pinned units to byte-identical objects
under Valgrind:

| Unit compiled | Built by `main` | Built by final | Change |
|---|---:|---:|---:|
| zlib `deflate.c` | 2,042,847,860 | 1,842,330,810 | −9.81% |
| zlib `inflate.c` | 1,957,042,120 | 1,762,982,135 | −9.92% |
| LZ4 `lz4.c` | 1,990,637,723 | 1,791,731,314 | −9.99% |

Hosted wall time is supplementary and noisy (a shared four-core container,
other jobs running): the stage-2 self-compile in `test_self_host` took
24.2 s on `main`, 22.5 s with the local rewrites and 20.8 s with all
changes.

### Deferred leads, by measured size

| Lead | Census coverage (self-host) | Owner |
|---|---:|---|
| frame slots out of disp8 reach | at least 1,296,861 RBP-relative accesses (42% of instructions, 8.9 MB) still need disp32 because the slot lies more than 128 bytes from RBP; up to 3 bytes each | stack placement: order slots by reference count, or bias the frame base |
| rel8 branch relaxation | 52,223 `JMP` and 36,692 `Jcc` rel32 whose target fits rel8 after all changes (~303 KB) | x86 branch fixups need an iterative sizing pass; the metadata tables already carry rel8 forms |
| reload after a fallthrough spill | 7,142 pairs made adjacent by branch layout | encoder, once it records that the next block's only predecessor falls through |
| edge-copy temporaries through a sixteen-byte chunk | 26,767 `TEMP_RELOAD`/`SPILL` chain pairs | allocator edit grouping (needs a vector scratch in edit runs) |
| copy forwarding into the consumer | 22,419 of 27,661 `mov a, b; add c, a` with `a` dead (67 KB) | FAST allocator coalescing |
| `zero-compare-to-test` | 2,712 of 3,264 `mov a32, 0; cmp b64, a64` with `a` dead | compare-with-immediate MIR form |
| `constant-sign-extend-fold` | 9,537 of 10,393 `mov a32, 0; movsxd b64, a32` with `a` dead | canonical IR constant folding |
| AArch64 analogues (`LDP`/`STP` copy pairs, branch layout) | not measured here | AArch64 encoder; same method |

### Regeneration

From a configured checkout (`./build.sh generate`, then
`./build.sh build --config Release -t ide`):

```sh
# Corpus objects: self-host unity object and the external units listed above.
build/Release/ide cc -c -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g0 -v \
    src/buster/apps/ide/ide.c -o build/ide-unity.o 2>&1 | grep '^CODEGEN '

# Census, optionally joined to a research attribution trace (ATTR lines).
python3 tools/machine_pattern_census.py --window 4 [--attribution ide-unity.attr] \
    --output build/census.json --report build/census.md build/ide-unity.o

# Bounded search over the ranked classes.
python3 tools/machine_rewrite_search.py --search build/census.json --top 12 --max-length 2 --output build/search.json

# Proofs. Z3, Unicorn and Capstone are research-only imports; GNU as and
# objdump are used for encodings.
python3 -m venv build/rewrite-venv
build/rewrite-venv/bin/pip install z3-solver unicorn capstone
build/rewrite-venv/bin/python tools/machine_rewrite_search.py --verify --ide build/Release/ide --output build/verify.json

# Tool self-tests.
python3 tools/machine_pattern_census.py --self-test
python3 tools/machine_rewrite_search.py --self-test
```

`--verify` exits nonzero if any rule disagrees with its catalog status, any
precondition is not shown necessary, any disp8 form differs from its disp32
twin, the copy row plan regresses, or any control-flow layout differs in the
emulator.
