# The x86-64 encoder

`src/buster/compiler/x86_64_encoder.c` turns a section (or a chunk of one) of
selected machine instructions into machine code: legacy and REX forms, VEX
forms (AVX, AVX2, FMA, BMI1 and BMI2, the opmask instructions) and EVEX forms
(AVX-512 with opmasks, zeroing, broadcasts, embedded rounding and registers 16
to 31). This document records why it is built the way it is: the review of the
historical `encode_wide` that it replaces, the design, the Zen 5 measurements
behind each choice, and what comes next.

## Results

On the Ryzen 7 9700X (Zen 5, clang 23, `build.sh`'s `-O3 -march=native`),
pinned to one CPU, `x86_encode` encodes a compiler-like instruction mix at
**0.66 ns per instruction** (1.5 G instructions/s, 6.0 GB/s of machine code,
about 3.6 cycles per instruction), and a vectorized-code mix of AVX-512, AVX2
and BMI at 0.65 ns (9.3 GB/s). That is:

- **16.4×** a plain one-instruction-at-a-time scalar encoder on the compiler
  mix (10.83 ns), 18.6× on the vector mix and 29× on a uniform mix of every
  form (19.31 ns), where the scalar encoder's branches mispredict;
- **7.5–9.6×** the historical `encode_wide` (with its defects fixed) on the
  subset of instructions it can encode: 0.63 ns against 4.8–6.1 ns, for
  byte-identical output (measured before VEX and EVEX support, which costs
  6–9% on long streams of the compiler mix, 0.61–0.62 ns before, and more on
  short calls and with offsets: see "Evaluation");
- byte-identical to GNU as 2.47 on 1.7 million random instructions covering
  all 1,174 forms (568 legacy, 288 VEX and 318 EVEX).

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
base/index/RIP flags and the scale), the form, `vvvv` (VEX.vvvv and
EVEX.V'vvvv's register) and `decorations` (EVEX's opmask, zeroing and rounding
mode, in the bits EVEX's last byte has them). Registers are numbers by class:
0–15, 0–31 for an EVEX form's vector registers, 0–7 for opmasks. Operands a
form doesn't use are zero, which is also what pads a group: form 0,
`X86_FORM_NONE`, encodes to nothing.

The forms are X-macro tables. The legacy one has 568 rows (integer arithmetic
and moves, all the addressing modes, shifts, multiplies and divides, branches,
`setcc`, `cmovcc`, atomics with `lock`, `crc32`, bit tests and counts, and the
scalar and packed SSE moves and arithmetic a C compiler emits); a row gives the
prefixes, the opcode bytes, how ModRM is used (`/r`, `/digit`, `+r` or none),
the immediate and relative widths, REX.W and the operand classes. The VEX and
EVEX one has 606 (moves, broadcasts, inserts and extracts, integer and floating
point arithmetic, compares into opmasks, shifts, shuffles and permutes,
`vpternlogd`, `vpmultishiftqb`, `vpshufbitqmb`, compresses and expands,
conversions, FMA, BMI, `kmov` and the opmask logic); its rows add the vector
length, W, the vvvv operand, the decorations the form can have and EVEX's N,
the unit of its 8-bit displacements. A broadcast memory operand, embedded
rounding and suppressed exceptions are forms of their own (`_M32BCST`, `_ER`,
`_SAE`), because they set EVEX.b and choose N, which the template then has. The
classes drive validation and the tests' assembly printer.

### Templates and the canonical slot

`x86_encoder_initialize` turns each form into a 32-byte template: the bytes the
form always has (prefixes and REX's `0x40 | W`, or the VEX or EVEX prefix;
opcode; the ModRM `/digit`) at their positions in a 16-byte slot, checks (the
bits of reg, rm, vvvv and the decorations that must be zero for the form),
flags (static length, a static displacement's presence, byte operands, REX.W,
VEX2, EVEX's N), and a layout: for each of the 15 positions, which byte of the
instruction's *canonical slot* goes there.

The canonical slot is the same for every form: immediate in bytes 0–3,
displacement in 4–7 (an EVEX 8-bit displacement divided by N), then ModRM, SIB,
REX.RXB and the `+r` register in 8–11, and what a VEX or EVEX prefix takes from
the operands in 12–15. A template's bytes are XOR'ed in, not OR'ed, so the
prefixes' inverted fields (VEX's R̄X̄B̄ and vvvv̄, EVEX's R̄', V̄' and the rest)
are ones in the template and the operands' bits flip them; for every other byte
the two never overlap. Every operand-dependent decision is about which canonical
bytes are present (REX, SIB, no/8-bit/32-bit displacement, VEX2's missing byte);
every form-dependent decision (byte order, widths, which fields exist, the
prefix's map, pp, W and L) is in the template. So one code path encodes every
form, and the template builder checks that each form's longest encoding fits in
15 bytes (the longest EVEX one is 12).

### VEX and EVEX

The prefix's dynamic bits come from Phase II's register fields: R, X and B are
REX's, which the canonical dword already has; EVEX's R' (reg's bit 4) and X of a
register rm (rm's bit 4) come from the same three `vpmultishiftqb`, into bits
of the canonical dword no legacy instruction uses. vvvv and the decorations are
one more `vpmultishiftqb` of the record's fourth dword, and V' (vvvv's bit 4) a
shift. VEX2 replaces VEX3 when the form allows it (W0, map 0F: a template flag)
and there is no X or B: then C4 becomes C5, R moves to the last byte's bit 7,
and the middle byte's presence bit clears, so `vpcompressb` drops it.

EVEX counts an 8-bit displacement in units of N (1 to 64): it fits if it is a
multiple of N whose quotient is in [−128, 127]. The scale is the template's,
and the template arrives last (see "Zen 5's costs"), so Phase II works out from
the displacement alone a bitmask of the scales it fits for, `x ^ (x − 1)` (the
multiples) without `0x01ffffff >> lzcnt(|x|)` (the scales too small for its
size), and the template's scale then picks a bit with two rotates; rotating
the displacement right by the scale puts the quotient in the byte the canonical
slot takes.

x86_encode also checks what its encoding can't represent, with one test: the
template's checks against the record's reg, rm, vvvv and decorations (a
register past its class's last, an operand or decoration the form doesn't
have), the addressing mode's (a base or index past 15, an invalid combination),
RSP as an index, and a high byte register with REX. Rules that depend on more
than one field (zeroing without an opmask, zeroing into memory) only
x86_validate checks.

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
   the mode always has (and which operand bits it forbids). Three
   `vpmultishiftqb` put every register field where it goes in the canonical
   dword. What depends on values (RBP and R13 bases, RSP and R12 bases, the
   displacement's size, REX for SPL–DIL, VEX2) are masks built with one-cycle
   arithmetic. The result is each instruction's canonical bytes, the presence
   bits of its canonical slot, its length and whether it's valid.
3. **Assemble**, 4 instructions per ZMM register: `vpshufb` puts each canonical
   byte at its position, a `vpternlogd` XORs the template's bytes in,
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
  (37 KB for all forms, of which a stream touches those of its forms), output
  written once, in order.
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

VEX and EVEX added about 20 operations per group (the prefix bytes, the
compressibility bitmask, the second template dword), but what first cost the
most was latency: with the scale's test after the template, ModRM.mod waited
for it, and REX, VEX2 and the presence bits waited for ModRM.mod through the
canonical dword, which made the compiler mix 18% slower (0.77 ns), with FP
scheduler stalls doubled. Computing REX and the prefix from the register fields
before ModRM.mod, the compressibility for every scale before the template, and
the presence bits with the latest terms last brought it to 0.66–0.67 ns, so
that only a few operations wait for the template. The difference that remains
is in the two-groups-per-iteration loop: one group per iteration is as fast as
it was (3.56 against 3.59 cycles per instruction), but interleaving a second
group no longer gains the 5–10% it did. That loop has 415 instructions per 32
encoded against 386, at the same IPC, and about 45 distinct constants, so more
of them are memory broadcast operands: load-queue stalls rose (71 to 84 million
cycles in a run) as FP scheduler stalls fell (46 to 29 million). Two attempts
didn't help: looking rm's base properties up in a constant with
`vpmultishiftqb` (6 operations fewer per group, the same cycles: it moves work
to FP1 and FP2), and telling VEX2 forms from their numbers rather than their
templates (about 1.5% faster, but it needs the form table ordered by an
encoding property).

## Evaluation

`./build/compiler benchmark-x86-encoder` on the 9700X, `taskset -c 2`,
nanoseconds per encoded instruction (the median of three runs, each the median
of 21 passes over at least 4 million instructions), with 32 per iteration
before VEX and EVEX for comparison:

| Stream | 4 | 8 | 16 | **32** | 64 | scalar | 32, before |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 instructions (L1) | 1.48 | 0.98 | 0.70 | **0.69** | 0.64 | 5.15* | 0.57 |
| 16,384 mixed (L2) | 1.49 | 1.16 | 0.65 | **0.66** | 0.67 | 10.83 | 0.61 |
| 16,384 vector | 1.49 | 1.13 | 0.65 | **0.65** | 0.64 | 12.09 | |
| 16,384 uniform | 1.49 | 1.19 | 0.68 | **0.67** | 0.67 | 19.31 | 0.64† |
| 16,384, with offsets | 1.71 | 1.30 | 0.74 | **0.75** | 0.75 | 10.72 | 0.64 |
| calls of 16 | 1.57 | 1.11 | 0.75 | **0.73** | 0.73 | 11.56 | 0.65 |
| calls of 64 | 1.51 | 1.12 | 0.67 | **0.68** | 0.70 | 11.07 | 0.61 |
| calls of 256 | 1.50 | 1.15 | 0.66 | **0.67** | 0.67 | 10.90 | 0.62 |
| 262,144 mixed | 1.50 | 1.17 | 0.66 | **0.66** | 0.66 | 12.63 | 0.62 |

The column is instructions per loop iteration. The mixed streams have only
legacy forms, as the "before" column does. *The scalar encoder's 5.15 ns on
1,024 instructions is flattering: the same short stream repeated thousands of
times lets the branch predictor learn it; and that row varies by about 10%
between runs (where the buffers land), the others by about 3%. †Over the 568
forms of then.

Below 16, Phase II's 16 lanes are partly empty (4 and 8 per iteration use a
quarter and a half of them), so the fixed cost per group dominates. 16, 32 and
64 are now within 2% of each other everywhere but the noisy L1 row (see "Zen 5's
costs" for why a second group no longer helps), so `x86_encode` keeps 32.

The variants that lost (all at 16 per iteration, 16,384 mixed):

| Variant | ns |
| --- | ---: |
| templates by `vinserti64x4` and form loads (`x86_encode_16`) | 0.65 |
| forms taken from the records' vector registers | 0.79 |
| templates by merge-masked loads, 16 bytes per lane | 0.79 |
| masked stores everywhere (exact output) | 0.68 |
| compressing straight to memory | 0.85 |

On Zen 4 (Ryzen 9 7940HS) everything ZMM costs about twice as much, memory
`vpcompressb` is microcoded (about 170 cycles), and merge-masked ZMM loads are
half-rate: 32 per iteration runs at about 1.1 ns there.

## Correctness

`./build/compiler test-x86-encoder` (about 0.3 s) checks:

- that every form's template fits;
- 99 known encodings from GNU as, on the paths most worth pinning down (VEX2
  and VEX3, BMI, opmasks, EVEX's opmasks, zeroing, broadcasts, rounding,
  compressed displacements and registers 16–31 among them), with both
  encoders;
- a digest of the scalar encoder's output for 100,000 random instructions,
  recorded when GNU as encoded the same stream to the same bytes;
- every batch width and variant against the scalar encoder, byte for byte and
  offset for offset, on every count from 0 to 199 and on 300,000 random
  instructions of all forms, with empty instructions mixed in;
- the same at page edges, with `PROT_NONE` pages after the input, the output's
  capacity (or the exact output) and the offsets;
- that invalid operands (a high byte register with REX, RSP as an index, RIP
  with a base, a register past its class's last, vvvv or reg in a form without
  them, decorations the form can't have, unknown mode bits) are rejected by
  `x86_encode` and found by `x86_validate`.

`--print-assembly` and `--compare-binary` cross-check the scalar encoder
against GNU as for any seed (AGENTS.md has the commands): 1.7 million random
instructions over all forms (seeds 1, 7 and 99), 0 differences. The streams
have every family in proportion to its forms: of 200,000 instructions, 54,000
EVEX (26,000 with an opmask, 11,000 zeroing, 15,000 broadcasts, 5,000 rounding
or suppressed exceptions, 39,000 with registers 16–31), 18,000 VEX2 and 32,000
VEX3. Eleven deliberately broken copies of the legacy encoder (wrong table
entries, multishift windows, RBP and RSP checks, displacement bounds, lane
order, store margin) and ten of its VEX and EVEX parts (VEX2's conditions, P0's
bits, the compressibility's rotation, V', OR instead of XOR, register limits,
the displacement's division, an inverted field, a base's bit 4) all fail the
tests; the last one needed a test of its own (a memory operand's base or index
past 15).

## Next steps

- **More VEX and EVEX forms, and REX2.** Rows are data: gathers and scatters
  (VSIB, with V' in the index), `/is4` register immediates, AVX-512 FP16 and
  the forms of VL lengths the table lacks fit the same template; APX's REX2 and
  its EVEX extensions would need a register's bit 4 in legacy forms.
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
  that the load queue and FP scheduler hold more groups at once; and fewer
  constants and live vectors, so that two groups per iteration gain again.
