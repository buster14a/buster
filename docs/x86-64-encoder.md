# The x86-64 encoder

`src/buster/compiler/x86_64_encoder.c` turns a section (or a chunk of one) of
selected machine instructions into machine code. This document records why it
is built the way it is: the review of the historical `encode_wide` that it
replaces, the design, the Zen 5 measurements behind each choice, and what comes
next (VEX, EVEX, relocations).

## Results

On the Ryzen 7 9700X (Zen 5, clang 23, `build.sh`'s `-O3 -march=native`),
pinned to one CPU, `x86_encode` encodes a compiler-like instruction mix at
**0.62 ns per instruction** (1.6 G instructions/s, 6.4 GB/s of machine code,
about 3.4 cycles per instruction). That is:

- **15.6×** a plain one-instruction-at-a-time scalar encoder on the same mix
  (9.67 ns), and 24.8× on a uniform mix of every form (15.82 ns), where the
  scalar encoder's branches mispredict;
- **7.5–9.6×** the historical `encode_wide` (with its defects fixed) on the
  subset of instructions it can encode: 0.63 ns against 4.8–6.1 ns, for
  byte-identical output;
- byte-identical to GNU as 2.47 on 1.7 million random instructions covering
  all 569 forms.

## The historical encoder

`encode_wide` (`bootstrap/bloat-buster/bb.c` in buster14a/bloat-buster at
e9b3dea9429996c7ad492d80a4923e75126ada87, lines 1428–1706) encodes 64
instructions per call, one byte lane per instruction: a column of bytes per
field (prefixes, REX, opcode, ModRM, SIB, displacement, immediate) and a column
of positions, then a scalar loop that places each instruction's bytes (35
stores per instruction) and a `memcpy` per instruction to concatenate them.

The defects below were reproduced with the unchanged function bodies, compiled
with only the definitions they need:

| Input | Expected | Got | Cause |
| --- | --- | --- | --- |
| 64 × `nop` | 64 bytes of `90` | 128 bytes, `90 00` repeated | the immediate size compares equal to 0 (the 1-byte size) for an absent immediate; the compare isn't masked by `is_immediate` |
| 64 × `movabs rax, imm64` | `48 b8 …` each | `08 b8 …` in 3 of every 4 | `_mm512_set1_epi32(0x40)` puts the REX base in every fourth byte only |
| `mov al/cl/dl/bl, imm8` in blocks of 16 lanes | `b0`, `b1`, `b2`, `b3` | `b0`, `b2`, `b1`, `b3` | the AVX2 nibble unpacks work within 128-bit lanes, swapping lanes 16–31 and 32–47 |
| `add dword ptr ds:0x1000, eax` | `01 04 25 00 10 00 00` | `01 80 00 10 00 00` (`[rax+0x1000]`) | `has_base_register` includes `is_reg_register`, so an absolute address with a ModRM.reg register gets a base; `encode_scalar` has the same bug |
| `encode_scalar` on 64 × `mov qword ptr [rsp+disp32], imm32` | — | stack-buffer-overflow (AddressSanitizer) | its unconditional 8-byte immediate store lands at offset 12 of the last 16-byte slot; a count over 64 also overflows `buffers` in the first loop and is silently truncated in the second |

Beyond the defects, it could not express an index register (REX.X is always 0
and the SIB index always "none"), RIP-relative or absolute addressing that
doesn't hit the bug above, or SPL–DIL (AH and SPL are both register 4, and REX
is only emitted for W/R/X/B), and it had no instruction count, tail, output
capacity or per-instruction offsets: every call is exactly 64 instructions and
writes as much as they need. Its input, 64 instructions in bit planes and byte
columns (about 1.4 KB per call), also has to be built by someone. Its test
broadcast one instruction to all 64 lanes and checked only the first copy, and
the LLVM disassembly check didn't require the whole buffer to be consumed,
which is why these went unnoticed.

With the first three defects fixed (and the stream avoiding the fourth), its
output matches `x86_encode`'s byte for byte on 16,384 random instructions of
the 564 forms it can encode, which is the comparison in the results.

## Design

### Input

An instruction is an `X86Instruction` of 16 bytes, so a ZMM register holds 4:
the immediate (4 bytes; a 64-bit one continues in the displacement), the
displacement (memory, or a branch's relative one), `reg`, `rm` (or the base, or
the `+r` register), `index`, `mode` (register direct, or a memory operand with
base/index/RIP flags and the scale), the form, and 2 reserved bytes for VEX's
vvvv and EVEX's decorations. Operands a form doesn't use are zero, which is
also what pads a group: form 0, `X86_FORM_NONE`, encodes to nothing.

The forms are an X-macro table of 569 rows (integer arithmetic and moves, all
the addressing modes, shifts, multiplies and divides, branches, `setcc`,
`cmovcc`, atomics with `lock`, `crc32`, bit tests and counts, and the scalar
and packed SSE moves and arithmetic a C compiler emits). A row gives the
prefixes, the opcode bytes, how ModRM is used (`/r`, `/digit`, `+r` or none),
the immediate and relative widths, REX.W and the operand classes. The classes
drive validation and the tests' assembly printer; the encoders don't need them.

### Templates and the canonical slot

`x86_encoder_initialize` turns each form into a 32-byte template: the bytes the
form always has (prefixes, REX's `0x40 | W`, opcode, the ModRM `/digit`) at
their positions in a 16-byte slot, flags (static length, a static
displacement's presence, byte operands, REX.W), and a layout: for each of the
15 positions, which byte of the instruction's *canonical slot* goes there.

The canonical slot is the same for every form: immediate in bytes 0–3,
displacement in 4–7, then ModRM, SIB, REX.RXB and the `+r` register in 8–11.
Every operand-dependent decision is about which canonical bytes are present
(REX, SIB, no/8-bit/32-bit displacement); every form-dependent decision (byte
order, widths, which fields exist) is in the layout. So one code path encodes
every form, and the template builder checks that each form's longest encoding
fits in 15 bytes.

### The loop

`x86_encode` works on groups of 16 instructions, two per iteration:

1. **Load.** Four records per ZMM register, and each form's template (a YMM
   load and `vinserti64x4` per two, `vshufi64x2` to split bytes from layouts).
   The records' fields are transposed into one instruction per dword lane with
   in-lane unpacks: instruction `4q + j` goes to lane `4j + q`, so that the
   transposes both ways stay inside 128-bit lanes.
2. **Compute**, 16 instructions per operation. The addressing mode's 4 bits
   index three 16-entry tables (`vpermd`): which canonical bits come from the
   operands, which are fixed (mod 11 for a register, rm 100 for a SIB byte or
   101 for RIP, SIB base 101 or index 100 for none), and which canonical bytes
   the mode always has. Three `vpmultishiftqb` put every register field where it
   goes in the canonical dword. What depends on values (RBP and R13 bases, RSP
   and R12 bases, the displacement's size, REX for SPL–DIL) are masks built with
   one-cycle arithmetic. The result is each instruction's canonical bytes, the
   presence bits of its canonical slot, its length and whether it's valid.
3. **Assemble**, 4 instructions per ZMM register: `vpshufb` puts each canonical
   byte at its position, a `vpternlogd` adds the template's bytes,
   `vpshufbitqmb` picks the presence bit of each position by the layout, and
   `vpcompressb` keeps the bytes that are present: four complete instructions,
   one after the other, stored with one 64-byte store and appended by the
   popcount of the mask.

Every 64-byte store stays inside the output's capacity
(`BUSTER_X86_ENCODER_CAPACITY(count)`, 15 bytes per instruction): a quad's
store starts after at most 15 bytes per earlier instruction, so it ends by
`15 * (i - 4) + 64 < 15 * count` bytes in while `i < count`. The last iteration
uses masked stores, and the last partial group goes through a zeroed copy, so
no load reads past the input. The page-edge test places inputs, outputs and
offsets against `PROT_NONE` pages to check both.

### What Niles Salter's tokenizer suggested

Accelerated-Zig-Parser's guidelines translate to an encoder like this:

- *Process many things at once:* 16 instructions per Phase II operation and 4
  per assembly operation, with no per-instruction loop.
- *Fewer unpredictable branches:* the only branches are the loop's. Cases are
  table lookups and masks; padding is an empty form rather than a branch.
- *Linear traversal of compact memory:* 16-byte records, 32-byte templates
  (18 KB for all forms), output written once, in order.
- *Allocate the upper bound:* the output's capacity is 15 bytes per
  instruction, so the loop never checks for room.
- *Write unconditionally, then advance:* full-width stores of the compressed
  bytes, advanced by a popcount.
- *Tables over logic, one code path:* the addressing mode tables, and the
  template layouts that let every form share one path.
- *Check invariants at build time:* `x86_encoder_initialize` rejects a form
  whose encoding doesn't fit, and `_Static_assert`s pin the record and
  template sizes.

## Zen 5's costs, measured

Tools that model a CPU from instruction tables, llvm-mca among them, predict
basic-block throughput 12–26% off on average on Intel cores, where only uiCA and
Facile (Abel and Reineke's tools, Intel-only) come within about 1% (the
*Facile* paper's comparison on BHive). So the decisions here come from
uops.info's measured Zen 5 data and from the 9700X itself.

What uops.info shows for Zen 5's four vector pipes (FP0–FP3) matters more than
instruction counts:

| Pipes | Instructions (ZMM) |
| --- | --- |
| FP1, FP2 only | cross-lane permutes (`vpermd`, `vpermt2d`, `vshufi64x2`, `valignd`), `vpshufb`, `vpshufbitqmb`, `vinserti64x4`, compares into mask registers (`vpcmpd`, `vptestmd`, latency 6), broadcasts from memory, `vpcompressb` (2 µops), a mask register from a general one (`kmovq k, r`: 2 µops) |
| FP0–FP3 | `vpternlogd`, `vpandd`, `vpaddd`, masked moves, `vpmovm2d`, in-lane `vpshufd` and `vpunpck*`, merge-masked loads |
| FP2, FP3 | shifts |
| FP0, FP1 | `vpmultishiftqb` |
| FP0, FP3 | mask register logic (`kandw`, `korw`) |

The first version spent about 94 µops per 16 instructions on FP1 and FP2, a
floor of 47 cycles, against 72 measured. Moving the transposes to in-lane
unpacks, the addressing mode logic to tables, the register fields to
`vpmultishiftqb`, and the masks to arithmetic (a zero test of a small value is
`(x - 1) >> 31`, two one-cycle operations, where a compare and `vpmovm2d`
take eight cycles on FP1/FP2) brought that to 48. Clang needed steering: it
turns two-input shuffles into `vpermt2d` and masked moves of constants into
masked broadcasts, both FP1/FP2 µops, and spills mask registers it has too
many of (a reload is 2 more).

Past that, `perf stat` on the 9700X shows that throughput is limited by how
much of a group's long dependency chain (template load, compares, presence,
`vpshufbitqmb`, popcount, store address: about 50 cycles) the out-of-order
window can overlap: dispatch stalls on the load queue for 39% of cycles. Taking
the forms from the records already in registers (16 fewer loads per group)
cuts those to 12%, but stalls on the FP scheduler rise from 7% to 42%, and it
is slower. Shortening that chain is where the next gains are.

## Evaluation

`./build/compiler benchmark-x86-encoder` on the 9700X, `taskset -c 2`,
nanoseconds per encoded instruction (the median of three runs, each the median
of 21 passes over at least 4 million instructions):

| Stream | 4 | 8 | 16 | **32** | 64 | scalar |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 instructions (L1) | 1.19 | 0.91 | 0.64 | **0.57** | 0.59 | 4.41* |
| 16,384 mixed (L2) | 1.28 | 1.02 | 0.65 | **0.62** | 0.65 | 9.67 |
| 16,384 uniform | 1.28 | 1.05 | 0.64 | **0.64** | 0.70 | 15.82 |
| 16,384, with offsets | 1.46 | 1.14 | 0.63 | 0.64 | 0.63 | 9.70 |
| calls of 16 | 1.38 | 1.04 | 0.65 | **0.64** | 0.65 | 10.34 |
| calls of 64 | 1.27 | 0.98 | 0.63 | **0.62** | 0.65 | 9.80 |
| calls of 256 | 1.29 | 1.01 | 0.64 | 0.64 | 0.63 | 9.75 |
| 262,144 mixed | 1.28 | 1.04 | 0.66 | **0.62** | 0.66 | 11.46 |

The column is instructions per loop iteration. *The scalar encoder's 4.41 ns
on 1,024 instructions is flattering: the same short stream repeated thousands of
times lets the branch predictor learn it.

Below 16, Phase II's 16 lanes are partly empty (4 and 8 per iteration use a
quarter and a half of them), so the fixed cost per group dominates. Unrolling a
second group (32) helps a little; a fourth (64) doesn't help further. 32 is the
best or within noise of it everywhere, so `x86_encode` uses it.

The variants that lost (all at 16 per iteration, 16,384 mixed):

| Variant | ns |
| --- | ---: |
| templates by `vinserti64x4` and form loads (`x86_encode_16`) | 0.65 |
| forms taken from the records' vector registers | 0.72 |
| templates by merge-masked loads, 16 bytes per lane | 0.71 |
| masked stores everywhere (exact output) | 0.66 |
| compressing straight to memory | 0.79 |

On Zen 4 (Ryzen 9 7940HS) everything ZMM costs about twice as much, memory
`vpcompressb` is microcoded (about 170 cycles), and merge-masked ZMM loads are
half-rate: 32 per iteration runs at about 1.1 ns there.

## Correctness

`./build/compiler test-x86-encoder` (about 0.2 s) checks:

- that every form's template fits;
- 63 known encodings from GNU as, on the paths most worth pinning down, with
  both encoders;
- a digest of the scalar encoder's output for 100,000 random instructions,
  recorded when GNU as encoded the same stream to the same bytes;
- every batch width and variant against the scalar encoder, byte for byte and
  offset for offset, on every count from 0 to 199 and on 300,000 random
  instructions of all forms, with empty instructions mixed in;
- the same at page edges, with `PROT_NONE` pages after the input, the output's
  capacity (or the exact output) and the offsets;
- that invalid operands (a high byte register with REX, RSP as an index, RIP
  with a base, unknown register and mode bits) are rejected by `x86_encode`
  and found by `x86_validate`.

`--print-assembly` and `--compare-binary` cross-check the scalar encoder
against GNU as for any seed (AGENTS.md has the commands): 1.7 million random
instructions over all forms, 0 differences. Eleven deliberately broken copies of
the vectorized encoder (wrong table entries, multishift windows, RBP and RSP
checks, displacement bounds, lane order, store margin) all fail the tests.

## Next steps

- **VEX, EVEX and REX2.** The design leaves room for them. Canonical bytes 12–15
  are free for the prefix payload (VEX's R̄X̄B̄, map, W, vvvv, L, pp; EVEX's P0–P2;
  REX2's), computed in Phase II from the register fields' high bits, the record's
  reserved bytes (vvvv, the opmask, zeroing, broadcast, rounding) and the
  template's flags (map, pp, W, L, the EVEX tuple). VEX2 against VEX3 is just
  which canonical bytes are present. EVEX's compressed displacement is a Phase
  II computation (scale by the tuple's N, test the 8-bit range, replace
  canonical byte 4), which the assembly's inverse transpose already carries.
  The longest EVEX encoding (4 + opcode + ModRM + SIB + disp32 + imm8 = 12
  bytes) fits a slot.
- **Relocations and labels.** The offsets output gives every instruction's
  start; a relocation field's offset is its start plus the position of
  canonical byte 4 (or 0) after compaction, which the presence bits and layout
  determine. Branch relaxation chooses forms before encoding, and Phase II's
  length alone is a cheap length-only pass for it.
- **The full form table.** Generate the rows offline from the original Buster
  catalogue's normalized forms; the template builder already rejects any that
  don't fit.
- **Threads.** Sections are independent: encode them in parallel into
  separate buffers.
- **Speed.** Shortening each group's dependency chain (see "Zen 5's costs"), so
  that the load queue and FP scheduler hold more groups at once.
