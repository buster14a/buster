# The x86-64 encoder

`src/buster/compiler/x86_64_encoder.c` turns a section (or a chunk of one) of
selected machine instructions into machine code: legacy and REX forms (x87, MMX,
SSE to SSE4.2, AES-NI, SHA, GFNI and the system instructions among them), VEX
forms (AVX, AVX2 with its
gathers, F16C, FMA, BMI1 and BMI2, the opmask instructions, VAES, AVX-VNNI and
the other VEX extensions, the variable blends and FMA4 with a register in the
immediate, and AMD's XOP, TBM and LWP) and EVEX forms (AVX-512 with opmasks,
zeroing, broadcasts, embedded rounding, registers 16 to 31, gathers and
scatters, AVX512-FP16 and AVX10.2), with FS and GS segment overrides on any
memory operand (thread-local storage). For the instructions with a symbolic
operand it reports where each one ends, which locates the fields their fixups
patch (see "Fixups").
This document records why it is built the way it is: the review of the
historical `encode_wide` that it replaces, the design, the Zen 5 measurements
behind each choice, and what comes next.

## Results

On the Ryzen 7 9700X (Zen 5, clang 23, `build.sh`'s `-O3 -march=native`),
pinned to one CPU, `x86_encode` encodes a compiler-like instruction mix at
**0.64 ns per instruction** (1.58 G instructions/s, 6.3 GB/s of machine code,
3.46 cycles per instruction), and a vectorized-code mix of AVX-512, AVX2 and
BMI at 0.64 ns (9.4 GB/s). That is:

- **17×** a plain one-instruction-at-a-time scalar encoder on the compiler mix
  (10.60 ns), 19× on the vector mix and 29× on a uniform mix of every form
  (19.53 ns), where the scalar encoder's branches mispredict;
- **7.5–9.6×** the historical `encode_wide` (with its defects fixed) on the
  subset of instructions it can encode: 0.63 ns against 4.8–6.1 ns, for
  byte-identical output (measured before VEX and EVEX support, whose first
  version cost 6–9% on long streams, which the loop's changes won back; the
  checks and forms added since cost about 3%, and segment overrides nothing:
  see "Evaluation");
- byte-identical to GNU as 2.47 on 1.7 million random instructions covering
  all 6,066 forms (1,647 legacy, 137 of them x87, 1,027 VEX, 103 of them XOP,
  and 3,392 EVEX), one in sixteen of them with an FS or GS override;
- with the ends of the instructions that have symbolic operands (the 6% of the
  compiler mix that are calls and RIP-relative operands), **0.69 ns**: as fast
  as every instruction's offset (0.69 ns, and 0.71 before the running sum they
  share was redone for them), and where GNU as 2.47 puts its relocations
  (offset, type, addend and symbol) for all 195,000 symbolic fields of those 1.7
  million instructions, on every form whose symbolic fields GNU as encodes the
  same way.

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
the `+r` register), `index` (a general register, or a gather's or scatter's
vector one, of its VSIB operand's class), `mode` (register direct, or a memory
operand with base/index/RIP flags and the scale), the form (plus
`BUSTER_X86_FORM_FS` or `BUSTER_X86_FORM_GS` for a segment override, and
`BUSTER_X86_FORM_FIXUP` for an instruction with a symbolic operand: see
"Fixups"), `vvvv` (VEX.vvvv and EVEX.V'vvvv's register) and `decorations`
(EVEX's opmask, zeroing and rounding mode, in the bits EVEX's last byte has
them). An /is4 operand, the
register that `vblendvps` and FMA4 take in the immediate's bits 7 to 4, is the
immediate as the instruction has it, the register's number times 16: the
encoders write it as any imm8, where a number would need a shift in the
vectorized loop that waits for the flags, and `x86_validate` checks its other
bits. Registers are numbers by class: 0–15, 0–31 for an EVEX form's vector
registers, 0–7 for opmasks, MMX registers (which REX doesn't extend) and debug
registers, 0–5 for the segment registers (ES, CS, SS, DS, FS and GS), the
numbers of CR0, CR2 to CR4 and CR8 for the control registers, and the even ones
for VP2INTERSECT's opmask pair (the processor ignores the odd bit). Operands a
form doesn't use are zero, which is also what pads a group: form 0,
`X86_FORM_NONE`, encodes to nothing.

The forms are X-macro tables. The legacy one has 1,647 rows (integer arithmetic
and moves at every size, all the addressing modes, shifts and rotates,
multiplies and divides, branches (near and far, and `notrack` ones through a
register), `setcc`, `cmovcc`, `lock` forms of every read-modify-write
instruction, string instructions with `rep`, `repe` and `repne`, `crc32`, bit
tests and counts, the flags, the small extensions
(`movbe`, `adcx`, RTM, `rdrand`, cache control, ...), MOV to and from the
segment, control and debug registers and PUSH and POP of FS and GS, SSE to
SSE4.2 and AMD's
SSE4a with MXCSR and the FXSAVE state, AES-NI, PCLMULQDQ, SHA, GFNI, Key
Locker and VIA PadLock (but not `data16 xstore`, which GNU as rejects), MMX
with the MMX register forms of SSE, SSE2 and SSSE3, all of x87, whose ST(i)
registers are a register class of 8 in ModRM.rm, and the system instructions:
descriptor tables and selectors, port I/O through DX or an immediate and its
strings, system calls and returns, caches and TLBs, MSRs, XSAVE, FSGSBASE, PKU,
SMAP, CET's shadow stacks, and AMD's SVM and SEV-SNP and Intel's VMX, TDX, SGX
and SMX, among others); a row
gives the prefixes, the opcode bytes, how ModRM is used (`/r`, `/digit`, `+r`
or none), the immediate and relative widths, REX.W and the operand classes. The
VEX and EVEX one has 4,419 (moves, masked and non-temporal moves, gathers and
scatters, broadcasts,
inserts and extracts, integer and floating point arithmetic, compares (into
opmasks too) and string compares,
shifts and rotates, shuffles and permutes, `vpternlogd`, `vpmultishiftqb`,
`vpshufbitqmb`, compresses and expands, conversions (F16C's too, and AVX-512F's
and DQ's with unsigned and 64-bit integers), truncating and saturating narrowing
moves, conversions between vectors and opmasks, AVX-512F's `vgetexp`,
`vgetmant`, `vscalef`, `vfixupimm`, `vrndscale` and 14-bit reciprocals, DQ's
`vrange`, `vreduce` and `vfpclass`, CD's conflict detection, Zen 5's other
AVX-512 extensions (VBMI, VBMI2, VNNI, BF16, IFMA, BITALG, VPOPCNTDQ, GFNI,
VAES, VPCLMULQDQ and VP2INTERSECT), FMA3 in its three orders, BMI, CMPccXADD,
`kmov` and the other opmask instructions at every width, and the VEX forms of
AES and VAES, VPCLMULQDQ, GFNI, AVX-VNNI (with its INT8 and INT16 forms),
AVX-IFMA, AVX-NE-CONVERT, SHA512, SM3 and SM4, and AVX's variable blends and
AMD's FMA4 with their /is4 operand, FMA4 in both of its orders: W0 with the
second source in ModRM.rm and W1 with the third; and AVX512-FP16's arithmetic,
FMAs, complex multiplications and conversions on 16-bit floats, and AVX10.2's
BF16 arithmetic, saturating and FP8 conversions, minimums and maximums, dot
products, compares into EFLAGS, read-shared loads, SM4 and zero-extending moves,
many in EVEX's maps 5 and 6, whose rows have the map's number in the escape's
place; AMD's XOP, TBM and LWP, VEX rows in XOP's maps 8 to 0A the same way,
whose four-operand and shift forms swap two sources with W as FMA4's do, and
`vpermil2ps` and `vpermil2pd`, which have a 4-bit selector below their /is4
register; and the MSR instructions with an immediate MSR index, VEX rows in map
7 the same way, with a 32-bit immediate); its rows add the
vector length, W, the vvvv operand, the decorations the form can have and EVEX's
N, the unit of its 8-bit displacements. A broadcast memory operand, embedded
rounding and suppressed exceptions are forms of their own (`_M32BCST`, `_ER`,
`_SAE`), because they set EVEX.b and choose N, which the template then has.
AVX-512F's, BW's, DQ's and CD's forms are at 128 and 256 bits too (AVX512VL),
and an EVEX form whose name a VEX form has ends in `_EVEX`. A gather's or
scatter's memory operand is a VSIB one, whose index (in the record's `index`,
XMM4 too) is a vector register: Intel's vm32x to vm64z, the forms' `VM32X` to
`VM64Z`, name the indexes' size and register. The classes drive validation and
the tests' assembly printer. A class of fewer than 16 registers
(opmasks and MMX's) that ModRM.rm can also address memory with is two forms, a
register one and a memory one, since the template's checks of rm can't depend
on the mode: mm8 to mm15 don't exist, but a base can be R8 to R15. So MMX's
`paddb mm, mm/m64` is `PADDB_MM_MM` and `PADDB_MM_M64`, and `movq`'s store
opcode between registers is `MOVQ_MM_MM_STORE`.

### Templates and the canonical slot

`x86_encoder_initialize` turns each form into a 32-byte template: the bytes the
form always has (prefixes and REX's `0x40 | W`, or the VEX or EVEX prefix;
opcode; the ModRM `/digit`) at their positions in a 16-byte slot, checks (the
bits of reg, rm, vvvv and the decorations that must be zero for the form),
flags (static length, a static displacement's presence, byte operands, REX.W,
VEX2, EVEX's N, the kinds of rm operand the form can't have), and a layout: for
each of the 15 positions, which byte of the instruction's *canonical slot* goes
there.

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

A segment override is the form's too, as `lock` and `rep` are: the form number
plus `BUSTER_X86_FORM_FS` or `BUSTER_X86_FORM_GS` (6,067 or twice that) names a
template of its own, which starts with the prefix as a static byte (64 or 65,
before the other prefixes, as GNU as puts it). `x86_encoder_initialize` builds
every form's template three times, 583 KB in all, of which a stream touches its
own forms'; a form without a memory operand gets copies that take no operand at
all. So x86_encode does nothing for an override that it doesn't do for every
form, and its loop is the same code with them as without.

### VEX and EVEX

The prefix's dynamic bits come from Phase II's register fields: R, X and B are
REX's, which the canonical dword already has; EVEX's R' (reg's bit 4) and X of a
register rm (rm's bit 4) come from the same three `vpmultishiftqb`, into bits of
the canonical dword no legacy instruction uses. vvvv and the decorations are one
more `vpmultishiftqb` of the record's fourth dword, and V' a shift of vvvv's bit
4 OR'ed with the index's: a VSIB index's bit 4 is V' (an EVEX gather or scatter
has no vvvv operand), and every other index's is zero. The byte that puts vvvv
in place starts at the dword's bit 13, so it takes the form number's bits 13–15
too, under pp and L: the last ternlog leaves them out, which matters since #23's
forms took GS's copies past 8,192 (a GS override had turned `66` into no prefix
in `x86_encode`). VEX2 replaces VEX3 when the form allows it (W0, map 0F: a
template flag) and there is no X or B: then C4 becomes C5, R moves to the last
byte's bit 7, and the middle byte's presence bit clears, so `vpcompressb` drops
it. XOP's prefix is VEX3's with 8F in place of C4, for maps 8 to 0A, and never
in two bytes: the template has its first byte as a static one, so the loop does
nothing for it.

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
have), the addressing mode's (a base past 15, an index register or a scale
without an index, an invalid combination), a high byte register with REX, an
rm operand of a kind the form can't have (a memory operand where it has a
register or no rm operand, a register where it takes only memory, or anything
but memory with a segment override), an index past 15 (but an EVEX form's VSIB
one) and RSP as a general index (a VSIB one can be XMM4). In the last three, a
bit of the operands picks one of two of the template's flags: the memory mode's
bit, the index's bit 4, and in its bit 6's place, whether the index is 4. Only
x86_validate checks the rules that depend on more than one field (zeroing
without an opmask, zeroing into memory, a VSIB operand without an index, an EVEX
gather or scatter without an opmask, and a gather whose destination, index and
mask aren't different registers) and the
ranges of the immediates (imm8 in [−128, 255], imm16 in [−32768, 65535] and
ENTER's in [0, 2²⁴)) and of a rel8 ([−128, 127]): x86_encode writes the
field's low bytes, so `jmp rel8` with a displacement of 200 is `eb c8`, a jump
of −56. Checking the ranges there too costs about 2% (see "Zen 5's costs").
x86_encode also ignores fields the form doesn't have (an immediate or a
displacement it has no field for, the rm of a memory operand without a base):
they don't change its bytes, and x86_validate rejects them.

### The loop

`x86_encode` works on groups of 16 instructions, four per iteration of its
outer loop (the loop over them stays rolled: see "Zen 5's costs"):

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
   displacement's size, REX for SPL–DIL, VEX2) are masks built with arithmetic
   any vector pipe runs. The result is each instruction's canonical bytes, the
   presence bits of its canonical slot, its length and whether it's valid.
3. **Assemble**, 4 instructions per ZMM register: `vpshufb` puts each canonical
   byte at its position, a `vpternlogd` XORs the template's bytes in,
   `vpshufbitqmb` picks the presence bit of each position by the layout, and
   `vpcompressb` keeps the bytes that are present: four complete instructions,
   one after the other, stored with one 64-byte store and appended by the
   popcount of the mask.

Every store is a full 64-byte one, inside the output's capacity
(`BUSTER_X86_ENCODER_CAPACITY(count)`: 15 bytes per instruction, and 64 more):
a quad's store starts after at most 15 bytes per earlier instruction (a zeroed
copy's padding adds none), so it ends by `15 * count + 64` bytes in. With a
capacity of 15 bytes per instruction alone, only the last quad's store could
pass it, but the last two groups took masked stores (a `kmovq k, r` and a
masked store per quad), which made calls of 16 instructions about 5% slower.
The last partial group goes through a zeroed copy, so no load reads past the
input. The page-edge test places inputs, outputs, offsets and fixup ends
against `PROT_NONE` pages to check both.

With offsets or fixup ends, Phase II's lengths are summed in each group: a
`vpermb` puts them in instruction order as bytes, a multiplication by
0x0101010101010101 (`vpmullq`) sums each qword's bytes up to each one (eight
lengths of at most 15, so nothing carries), and a `vpshufb` and an add carry the
first qword's total into the second. That is each instruction's end in the group
(at most 240), and minus its length, its start; `vpmovzxbd` widens them and the
group's offset in the output is added (see "Zen 5's costs" for why bytes).

### Fixups

The code generator binds names before instruction selection, so most operands
are numbers by the time an instruction is encoded. What only layout can fill in
are fields: a call's rel32, a RIP-relative displacement to a global, an imm32
or imm64 with a symbol's address. The code generator marks those instructions
with `BUSTER_X86_FORM_FIXUP`, bit 15 of the form (the last form with a segment
override is 17,465), and puts a placeholder in the field. The encoders encode
the form without the flag, and with `fixup_ends`, `x86_encode` stores each
marked instruction's end offset, in order: one per marked instruction, however
many of its fields are symbolic.

The end is enough to find the fields without decoding anything, because the
displacement and the immediate are an instruction's last fields, in that order,
and the form has their widths. `x86_fixup_distance(form, field)` is how far
before the end each one starts:

- the displacement (`X86_FIXUP_DISPLACEMENT`, a memory operand's or a relative
  branch's): the immediate's bytes plus 4, or plus the rel8's or rel32's bytes;
- the immediate (`X86_FIXUP_IMMEDIATE`): the immediate's bytes (8 for a 64-bit
  immediate or MOVABS's address).

The processor measures a PC-relative field from the end of the instruction, so
a PC-relative fixup's addend relative to its field is the symbol's addend minus
that same distance:

| Instruction | Field | Distance | Addend from the field |
| --- | --- | ---: | ---: |
| `call f` | rel32 | 4 | −4 |
| `lea rax, [rip + x]` | displacement | 4 | −4 |
| `cmp byte ptr [rip + x], 1` | displacement | 5 | −5 |
| `mov dword ptr [rip + x], 0x12345678` | displacement | 8 | −8 |
| `vpternlogd zmm0, zmm1, [rip + x], 5` | displacement | 5 | −5 |
| `mov dword ptr [rip + x], offset y` | displacement, immediate | 8, 4 | −8, y's |
| `movabs rax, offset y` | immediate | 8 | y's |

A displacement field is 32-bit when the memory operand is RIP-relative,
absolute or index-only. After a base, the encoder makes small values 8-bit or
leaves them out, as GNU as does with numbers (but not with symbols), so a
symbolic displacement there needs a placeholder that doesn't fit in 8 bits in
units of the form's N, such as INT32_MIN, which the fixup overwrites.

In the loop, the fixup ends are the offsets' running sum, compressed. The
flags come from the forms, which Phase I has in lane order with the records'
fourth dwords: a `vpermb` puts each form's high byte in instruction order and
`vpmovb2m` takes their bit 7, so that the mask is ready long before the
lengths. A `vpcompressd` by it keeps the marked instructions' ends, which a
64-byte store appends to the fixup ends (their capacity has 16 more than the
marked instructions, `BUSTER_X86_ENCODER_FIXUP_CAPACITY`), advanced by the
mask's popcount; the zeroed copy's padding isn't marked. Phase I takes the flag
off the form before looking its template up (an AND per instruction, which
measured no slower; see "Evaluation").

The other way to have the ends is the offsets: the caller picks the marked
instructions' and takes the next instruction's start. Both are in the benchmark
(`x86_encode_64` with `--fixups`, and `x86_encode_64_offsets_then_pick`, which
picks from the list of marked instructions a code generator has); see
"Evaluation".

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
instruction counts. Microbenchmarks on the 9700X (blocks of independent
instructions, alone and mixed with ones of known pipes) confirm it, except that
broadcasts from memory take no vector pipe:

| Pipes | Instructions (ZMM) |
| --- | --- |
| FP1, FP2 only | cross-lane permutes (`vpermd`, `vpermt2d`, `vshufi64x2`, `valignd`), `vpshufb`, `vpshufbitqmb`, `vinserti64x4` (from memory too), compares into mask registers (`vpcmpd`, `vptestmd`), `vpcompressb` (2 µops), a mask register from a general one (`kmovq k, r`, one per cycle) |
| FP0–FP3 | `vpternlogd`, `vpandd`, `vpaddd`, `vpminud`, masked moves, `vpmovm2d`, in-lane `vpshufd` and `vpunpck*`, merge-masked loads |
| FP2, FP3 | shifts, variable ones too |
| FP0, FP1 | `vpmultishiftqb`, rotates |
| FP0, FP3 | `vpopcntd`, `vplzcntd`, mask register logic (`kandw`, `korw`) |
| none | loads, two per cycle at any width, broadcasts and `{1to16}` operands among them; a general register from a mask register (`kmovq r, k`) |

A 64-byte store takes a cycle (two 32-byte ones take one), and stores slow
`vpternlogd` down: four of each take 5.25 cycles, where the stores alone take
4. Latencies, from chains of dependent instructions (a chain of scalar adds
takes a cycle per add), are what the loop's dependency chains are made of:

| Cycles | Instructions |
| ---: | --- |
| 2 | `vpaddd` (XMM, YMM and ZMM alike), `vpternlogd`, `vpandd`, `vpminud`, shifts, rotates, `vplzcntd`, `vpopcntd`, `vpunpck*`, `vpshufd`, `vpshufb`, `vinserti64x4` from a register |
| 3 | `vpmultishiftqb` |
| 5 | `vpermd`, `vpermt2d`, `vshufi64x2`, `valignd`, `vpcompressb` |
| 8 | `vptestmd` then `vpmovm2d`; `vpshufbitqmb` then `vpmovm2b` |
| 10 | `kmovq r, k`, `popcnt`, `kmovq k, r` |
| 11 | a 64-byte store and the load it forwards to |

The first version spent about 94 µops per 16 instructions on FP1 and FP2, a
floor of 47 cycles, against 72 measured. Moving the transposes to in-lane
unpacks, the addressing mode logic to tables, the register fields to
`vpmultishiftqb`, and the masks to arithmetic (a zero test of a small value is
`(x - 1) >> 31`, two operations of two cycles on any pipe, where a compare and
`vpmovm2d` take eight cycles on FP1/FP2) brought that to 48. Clang needed
steering: it turns two-input shuffles into `vpermt2d` and masked moves of
constants into masked broadcasts, both FP1/FP2 µops, and spills mask registers
it has too many of (a reload is 2 more). It also sees that each of Phase III's
unpacks of the immediate takes half of it and merges one with the record
unpack it came from into a `vpermt2d` with a loaded index, a 5-cycle FP1/FP2
µop on Phase III's path, which an empty `asm` statement on the immediate
prevents.

Past that, throughput is limited by how much of a group's dependency chain the
out-of-order window can overlap. The chain is about 50 cycles: the form, the
template (an insert, `vshufi64x2` and two unpacks) and its flags take about 25,
and from the flags to the store about 12 two-cycle operations, `vpshufbitqmb`
and `vpcompressb` take about 25 more. While the window holds about two groups,
dispatch stalls on the FP scheduler (23% of cycles) and the load queue. Adding 8
broadcast loads or 8 register operations per group costs the same 2–3%, so
neither is the limit by itself. Taking the forms from the records already in
registers (16 fewer loads per group) cut the load-queue stalls from 39% of
cycles to 12% before VEX and EVEX, but stalls on the FP scheduler rose from 7%
to 42%, and it is slower. Shortening that chain is where the next gains are.

VEX and EVEX added about 20 operations per group (the prefix bytes, the
compressibility bitmask, the second template dword), but what first cost the
most was latency: with the scale's test after the template, ModRM.mod waited
for it, and REX, VEX2 and the presence bits waited for ModRM.mod through the
canonical dword, which made the compiler mix 18% slower (0.77 ns), with FP
scheduler stalls doubled. Computing REX and the prefix from the register fields
before ModRM.mod, the compressibility for every scale before the template, and
the presence bits with the latest terms last brought it to 0.66–0.67 ns, so
that only a few operations wait for the template.

The 6–9% that remained came from how clang compiled the loop. With the bigger
group, clang no longer unrolls the loop over an iteration's groups, so every
width ran a one-group loop (`perf record` puts 99% of the samples there; the
two-group loop earlier measurements counted is a colder one). Unrolling it by
force makes 32 per iteration 14% slower: two groups' code spills (19 stack
references in 517 instructions). What recovered it:

- **Full stores only**: the capacity's 64 more bytes (see "The loop"), so that
  the last groups don't take masked stores; calls of 16 instructions are 4–6%
  faster.
- **A loop with offsets and one without**: once the masked-store loop was gone,
  the test for offsets in each group made clang rotate 17 vector registers
  through copies every iteration in the loop with offsets (34 more `vmovdqa64`
  per group); each loop specialized for its case has none.
- **No `vpermt2d` in Phase III** (the `asm` statement above).
- **Invalid lanes OR'ed into a vector** and tested once at the end, rather than
  a compare into a mask register and a `kmovd` per group, and each quad's
  presence bits copied with one `vpshufd`, rather than an unpack and a
  `vpshufd`.

The widths still compile differently. Clang's code for 64's loop (and 32's)
broadcasts constants into registers where 16's folds them into operations as
`{1to16}` operands: it has 16% more instructions, and it is 9% faster, with
dispatch stalled on the load queue 3.9% of cycles against 19.5%, and on the FP
scheduler 23% against 26%. Folded constants alone aren't slower (a
microbenchmark of operations waiting for a late value shows the opposite), so
it's the schedule as a whole. Small changes to the source move clang's choices,
which is why the benchmark keeps every width. Two attempts didn't help: looking
rm's base properties up in a constant with `vpmultishiftqb` (6 operations fewer
per group, the same cycles: it moves work to FP1 and FP2), and telling VEX2
forms from their numbers rather than their templates (about 1.5% faster, but it
needs the form table ordered by an encoding property).

Segment overrides (#39) were first a canonical byte (11, with the `+r`
register moved to ModRM's), a presence bit and checks in Phase II. Of the six
designs of that kind measured, the best was about 1% slower on the compiler mix
and 2–3% slower on calls of 16 and with offsets, and the others 2–9% slower on
the compiler mix. Mapping the mode's six bits (FS and GS one each) to the
tables' index with a `vpermb` put 5 cycles in front of every table lookup, 3–5%
at 16 per iteration. Without the remap, the presence bit and EVEX's R' and X
(which the override's byte pushed out of the canonical dword) held registers
through Phase II, and clang made room by spilling two more of Phase I's
template vectors; merging the presence bit where it holds one register less
made the loop 6% slower instead, with fewer spills: the schedule as a whole
again. As template copies (see "Templates and the canonical slot"), the
overrides cost nothing in the loop, whose code is instruction for instruction
the same as without them.

The immediates' and rel8s' ranges stay x86_validate's: checking them in
x86_encode made 64 per iteration about 2% slower on the compiler mix and the
vector mix (15 interleaved rounds against main with #51's forms; 3–4% before
them), 0.4% on calls of 16 and no slower with offsets. The cheapest of nine
versions took 10 operations per group, 6 of them on the immediate alone, before
the template arrives. An immediate x fits b bytes, signed or not, if
`lzcnt(x ^ (x >> 31))`, one less for a negative x, is at least 32 − 8b, so
`0xfffffffe` shifted left by that has a bit at 32 − 8b for each size it
doesn't fit (with ENTER's sign in bit 8). Two bits of the template's flags
then rotate the form's bit to bit 0, and a rel8 flag, the rotation count's bit
0, adds the displacement's fit in a byte (`fits8`, from the compressibility
above).
Taking the rel8's test from the displacement before the template, folding it
into the mask, sharing the constants, an empty `asm` statement on the mask, and
computing the mask first or last were all within the ±1.5% that one build
varies between runs: 10 operations cost about what 8 did before (2–3%).

Gathers and scatters (#35) needed V' from the index and two checks that depend
on the form, an index past 15 and RSP as a general index. Both went into the rm
kind's test, whose two flags the memory mode's bit already picked: the index's
bit 4 picks two more, and so does a bit for an index of 4, which takes its bit
6's place (a valid index doesn't have that bit). That takes the operations
RSP's own test did, plus one for V', so the loop has as many operations as
before, but clang allocated its registers better. The loop without offsets
runs 10 fewer instructions per group (249, with 5 stack references instead of
13), and the one with offsets 3 more (282). Against main with #34's forms (15
interleaved rounds), the compiler mix is 2.0% faster (3.44 to 3.37 cycles) and
the loop with offsets 2.6% faster; the vector mix and calls of 16 are no
different. Two versions with the same operations did worse. With the checks'
bits in a register of their own, as the final test's mask, the loop with
offsets had 8 more instructions (copies) and the other none fewer. With V' taken
from the word that picks the flags, both loops were 2 instructions shorter than
main's, but the compiler mix was no faster.

Offsets and fixup ends cost what the latency of their running sum does. Fixup
ends first summed the lengths as the offsets did: a `vpermd` to instruction
order and four shifts (`valignd`) and adds, 29 cycles from the lengths, at the
end of each group's Phase II. The reorder buffer filled up behind that chain
(dispatch stalled on retire tokens 17% of cycles with offsets and 27% with
fixup ends, against 1.6% with neither, while the FP scheduler's stalls fell),
and fixup ends took 0.76 ns per instruction against 0.71–0.72 for offsets.
Storing each group's ends with the next group's code, when its lengths were
long ready, brought fixup ends to 0.69 ns and offsets to 0.68–0.70, but made
calls of 16 instructions 12–14% slower: each call stored an empty group before
its first, and a flag that skipped it made long streams 9% slower again.
Summing bytes shortens the chain instead: `vpermb` (4 cycles on Zen 5,
uops.info), `vpmullq` (2), `vpshufb` (1) and an add (2), then `vpmovzxbd` (3)
to dwords, 12 cycles, with two fewer µops on FP1 and FP2. With it, offsets got
2–5% faster than before, with the reorder buffer full 5.6% of cycles. Fixup
ends took their flags from the lengths' dwords at first (a `vpmovb2m` and a
`kshiftrq` after the `vpermb`, the longest path to the compress); taking them
from the records instead, with a `vpermb` of their own, took another 3% off
(the reorder buffer full 8.6% of cycles), which made them as fast as offsets.
Taking the flag off the forms in Phase I made clang take the masked-loads
variant's forms out of the records' vector registers with `vpextrw` (20%
slower), until an empty `asm` statement on its record pointer stopped it.

## Evaluation

`./build/compiler benchmark-x86-encoder` on the 9700X, `taskset -c 2`,
nanoseconds per encoded instruction (the median of five runs, each the median
of 21 passes over at least 4 million instructions), with `x86_encode` before
the changes above (32 per iteration) for comparison:

| Stream | 4 | 8 | 16 | 32 | **64** | scalar | before |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 instructions (L1) | 1.46 | 0.95 | 0.69 | 0.64 | **0.69** | 5.08* | 0.67 |
| 16,384 mixed (L2) | 1.46 | 1.11 | 0.67 | 0.65 | **0.64** | 10.60 | 0.66 |
| 16,384 vector | 1.46 | 1.09 | 0.66 | 0.66 | **0.64** | 11.94 | 0.65 |
| 16,384 uniform | 1.47 | 1.13 | 0.66 | 0.68 | **0.66** | 19.53 | 0.67 |
| 16,384, with offsets | 1.77 | 1.27 | 0.68 | 0.70 | **0.69** | 11.14 | 0.75 |
| 16,384, with fixup ends | 1.77 | 1.26 | 0.72 | 0.70 | **0.69** | 11.33 | — |
| calls of 16 | 1.53 | 1.15 | 0.71 | 0.71 | **0.70** | 11.20 | 0.73 |
| calls of 16, with fixup ends | 1.84 | 1.20 | 0.80 | 0.80 | **0.81** | 12.02 | — |
| calls of 64 | 1.48 | 1.11 | 0.67 | 0.68 | **0.66** | 10.74 | 0.68 |
| calls of 256 | 1.47 | 1.08 | 0.67 | 0.67 | **0.66** | 10.57 | 0.66 |
| 262,144 mixed | 1.46 | 1.10 | 0.67 | 0.66 | **0.64** | 12.47 | 0.66 |

The column is instructions per iteration of the outer loop. The mixed streams
have only legacy forms, and the uniform one is a different sample in each
build (its random forms depend on how many there are). In cycles, 64 per
iteration takes 3.46 per instruction of the compiler mix, against 3.56 for the
loop before the changes above, which had none of the checks and forms added
since: the changes measured 3.36 when they were made, and the rm kind check
and the forms of #18 and #29 brought that to 3.43, which segment overrides
don't change (the same code, within the runs' noise). #35's checks made it
3.37 (see "Zen 5's costs").
*The scalar encoder's 5.08 ns on 1,024 instructions is flattering: the same
short stream repeated thousands of times lets the branch predictor learn it;
and that row varies by about 10% between runs (where the buffers land), the
others by about 3%, and a build by up to 4% when other code moves (the scalar
encoder's, which these changes don't touch, did).

Below 16, Phase II's 16 lanes are partly empty (4 and 8 per iteration use a
quarter and a half of them), so the fixed cost per group dominates. 16, 32 and
64 run the same one-group loop with different code (see "Zen 5's costs"), and
64's is the fastest or within 0.01 ns of it everywhere but the noisy L1 row, so
`x86_encode` uses 64.

The rows with offsets and fixup ends are from the fixup interface's changes,
the others from before them. Against the code before them, in 15
interleaved rounds of `x86_encode_64`: without offsets or fixup ends, −2.1% to
+0.9% on the compiler, vector and uniform mixes and in calls of 16, 64 and 256,
and +0.2% and +0.5% on the compiler and vector mixes with both copies of the
function aligned to 64 bytes (the AND that takes the fixup flag off each form);
with offsets, 1.8–5.4% faster (the running sum in bytes), and 4% slower at 4
per iteration.

The fixup ends' two designs and two stores, `x86_encode_64`, nanoseconds per
instruction (medians of five runs; `--fixups` marks the calls and the
RIP-relative operands, 6% of the compiler mix, 2% of the vector mix and 12% of
the uniform one):

| Stream | neither | offsets | **fixup ends** | compressed to memory | offsets, then picked |
| --- | ---: | ---: | ---: | ---: | ---: |
| 16,384 mixed | 0.63 | 0.69 | **0.69** | 0.70 | 0.71 |
| 16,384 vector | 0.64 | 0.69 | **0.69** | 0.69 | 0.69 |
| 16,384 uniform | 0.67 | 0.72 | **0.72** | 0.72 | 0.75 |
| 262,144 mixed | 0.64 | 0.70 | **0.70** | 0.70 | 0.72 |
| calls of 16 | 0.70 | 0.77 | **0.81** | 0.82 | 0.84 |
| calls of 64 | 0.66 | 0.72 | **0.73** | 0.75 | 0.76 |
| calls of 256 | 0.66 | 0.70 | **0.71** | 0.70 | 0.72 |

"Offsets, then picked" is the other design: every offset, then the ends of the
marked instructions picked from the list of them a code generator keeps (the
next instruction's start); it costs 1–4% more than fixup ends. Before these
changes, offsets took 0.70–0.71 ns on the long streams and 0.80 in calls of 16,
so fixup ends cost no more than offsets did except in calls of 16 (1% more).
Compressing the ends straight to memory (`vpcompressd` to memory, 8 µops and a
store every 3 cycles on Zen 5, where Zen 4 takes 72 cycles) is as fast or within
3% (calls of 64), off the loop's critical path; the 64-byte store of the
compressed register stays, as it does for the machine code.

The variants that lost (all at 16 per iteration, 16,384 mixed):

| Variant | ns |
| --- | ---: |
| templates by `vinserti64x4` and form loads (`x86_encode_16`) | 0.67 |
| forms taken from the records' vector registers | 0.81 |
| templates by merge-masked loads, 16 bytes per lane | 0.77 |
| masked stores everywhere (exact output) | 0.69 |
| compressing straight to memory | 0.83 |

On Zen 4 (Ryzen 9 7940HS) everything ZMM costs about twice as much, memory
`vpcompressb` is microcoded (about 170 cycles), and merge-masked ZMM loads are
half-rate: 64 per iteration runs at about 1.2 ns there.

## Correctness

`./build/compiler test-x86-encoder` (about 0.6 s) checks:

- that every form's template fits;
- 492 known encodings from GNU as, on the paths most worth pinning down (VEX2
  and VEX3, BMI, opmasks, EVEX's opmasks, zeroing, broadcasts, rounding,
  compressed displacements, registers 16–31, x87's register forms and memory
  sizes, `lock`, `rep`, far pointers and 16-bit forms, segment overrides on
  legacy, x87, VEX and EVEX forms, RIP-relative addresses and with `66` and
  `lock` too, SSE's shifts by an immediate, both `pextrw` encodings, SSE4a's
  two immediates, the blends' implicit XMM0, AVX's masked stores (memory, mask,
  source), F16C, AVX2 at 256 bits with shift counts and sources from an XMM
  register, FMA3's orders, packed and scalar, and AVX-512F's conversions
  (broadcasts for half the vector, rounding without an opmask), suppressed
  exceptions on scalar and immediate forms, narrowing and extending moves,
  inserts, extracts and the other N, a GS override on the last form,
  AVX512VL's widths, with their broadcasts and N, AVX512BW's, DQ's and CD's
  forms, `vpextrw`'s two encodings, `vfpclass`'s broadcasts, and the other
  extensions' forms, VP2INTERSECT's opmask pairs among them, AES-NI, SHA's
  implicit XMM0, Key Locker's handles and wide forms, PadLock's opcodes with
  their ModRM byte, the VEX forms of AVX-VNNI, AVX-IFMA and the other VEX
  extensions, and MMX's register and memory forms with REX for their other
  operands, `movq`'s two encodings between registers, and the conversions and
  moves between MMX and XMM registers, and /is4 registers up to 15 and after a
  SIB byte and a displacement, with FMA4's two orders (W0 between registers
  from objdump, as GNU as writes W1 there), and segment registers to and from
  memory and registers of every size, PUSH and POP of FS and GS, and CR8 and the
  debug registers with REX, and AVX512-FP16's and AVX10.2's forms in maps 5 and
  6, with word broadcasts, compressed displacements of 2 and the conversions'
  N, and XOP's prefix in maps 8 to 0A with its sources swapped by W (vpcmov's
  W1 between registers from objdump, as GNU as writes W0 there), vpermil2ps's
  selector and TBM's and LWP's 32-bit immediates, and gathers and scatters by
  XMM4 and by indexes past 7 and 15 (X and V'), without a base and after RBP,
  RSP and R13, at the widths their elements and indexes give, with compressed
  and full displacements and an FS override, and the system instructions: `lar`
  and `lsl` with 66 and with REX.W (rex.w, as GNU as drops it), `sldt` to a
  16-bit register (66) and to memory (without), `str` and `smsw` with REX.W,
  `lgdt` with FS, port I/O through DX and an immediate, `rep insw` (66 before
  F3) and `repne outsd`, `iretw`, `sysretq` and `sysexitd`, `xsave64`, F3 before
  REX.W in `incsspq`, `wrussq`, `vmread` (no REX.W), `invpcid` and `enqcmd`
  with REX, the MSR instructions in VEX map 7 with their 32-bit immediates, and
  `hreset`'s ModRM byte in its opcode), with both encoders, and each again
  with the fixup flag, whose end both must report;
- a digest of the scalar encoder's output for 100,000 random instructions,
  recorded when GNU as encoded the same stream to the same bytes;
- every batch width and variant against the scalar encoder, byte for byte,
  offset for offset and fixup end for fixup end, in each of its four loops (with
  neither, either or both of offsets and fixup ends), on every count from 0 to
  199 and on 300,000 random instructions of all forms, with empty instructions
  mixed in and one in eight marked with the fixup flag;
- the same at page edges, with `PROT_NONE` pages after the input, the output's
  capacity (or the exact output), the offsets and the fixup ends' capacity (or
  the marked instructions' ends, for the variant that compresses them straight
  to memory);
- that invalid operands (a high byte register with REX, RSP as a general index,
  RIP with a base, a register past its class's last, VSIB indexes among them,
  an odd opmask pair, vvvv or reg in a form without them, decorations the form
  can't have, unknown mode bits, an index register or a scale without an index,
  a memory operand in a form without one, a register in a form that takes only
  memory, a segment override without a memory operand) are rejected by
  `x86_encode` and found by `x86_validate`, and that immediates and rel8s just
  outside their fields' ranges (above and below, in an imm8, imm16, ENTER's or
  rel8 form, each kind as often), /is4 registers past the last or with bits 3
  to 0 set (or below 0 where those are a selector), control registers that
  don't exist (CR1, CR5 to CR7 and CR9 to CR15), segment registers past GS, MOV
  to CS, complex multiplications into one of their sources, VSIB operands
  without an index (a base alone, an absolute address or RIP), EVEX gathers and
  scatters without an opmask, and gathers whose destination, index and mask
  (AVX2's) aren't different registers are found by `x86_validate`.

`--print-assembly` and `--compare-object` cross-check the scalar encoder against
GNU as for any seed (AGENTS.md has the commands): 1.7 million random
instructions over all forms (seeds 1, 7 and 99), 0 differences. The streams have
every family in proportion to its forms: of 200,000 instructions, 111,700 EVEX
(53,000 with an opmask, 23,900 zeroing, 36,400 broadcasts, 10,300 rounding or
suppressed exceptions, 78,000 with registers 16–31; 15,200 in the AVX-512F
forms of #23, 27,000 in the AVX512VL ones of #24, 16,100 in the AVX512BW, DQ and
CD ones of #25, 6,300 in the other extensions' of #26, 35,000 in the
AVX512-FP16 and AVX10.2 ones of #33 and 1,600 gathers and scatters of #35),
6,900 VEX2, 23,700 VEX3 (3,100 of them FMA3, 900 in the opmask instructions of
#27, 2,600 in the VEX extensions of #28, 2,200 in the /is4 forms of #31, 500
AVX2 gathers of #35 and 140 MSR instructions in map 7 of #19), 3,700 in the
forms of #34 (3,400 of them XOP), 6,500 in the SSE forms of #20, 1,200 in the
AES-NI, SHA, GFNI, Key Locker and PadLock ones of #28, 5,800 in the MMX forms of
#30, 550 with the segment, control and debug registers of #32, 5,800 in the
system instructions of #19, 4,600 x87, 4,000 with `lock`, 2,000 string
instructions and 13,900 with an FS or GS override (500 of them with `lock`,
11,300 vector ones and 2,100 RIP-relative). A VEX form whose
mnemonic an EVEX form has is printed with `{vex}` (19,200 of the 200,000),
because GNU as takes some of those as EVEX otherwise (`vpdpbusd`, `vpmadd52luq`
and `vcvtneps2bf16`, whose AVX-512 forms came first). The printed
streams leave out encodings GNU as writes another way: `movq` and `vmovq`
between an XMM register and memory have two (F3 0F 7E or 66 0F D6, and the W1
forms of `movd`), and GNU as takes the first, but the second with EVEX, and
`movq` between an MMX register and memory is 0F 6F or 0F 7F, not the W1 forms
of `movd` either; FMA4 between registers is W1, the third operand in the
immediate, but XOP's `vpcmov` and `vpperm`, and `vpermil2ps` and `vpermil2pd`,
are W0 (XOP's shifts and rotates by a vector are W1 with {store}, which picks
them); `fop
st(0), st` (DC) is `fop st, st(0)` (D8), `xchg eax, eax` is 87 C0 and `xchg
rax, rax` is NOP, and
`prefetchit0` and `prefetchit1` take only RIP-relative addresses; and LEA with a
segment override, which GNU as warns does nothing. An MSR index in VEX map 7 is
printed unsigned, as GNU as rejects a negative one. Eleven deliberately broken
copies of the legacy encoder (wrong table entries, multishift windows, RBP and
RSP checks, displacement bounds, lane order, store margin) and ten of its VEX
and EVEX parts (VEX2's conditions, P0's bits, the compressibility's rotation,
V', OR instead of XOR, register limits, the displacement's division, an inverted
field, a base's bit 4) all fail the tests; the last one needed a test of its own
(a memory operand's base or index past 15). So did the rm operand's kind: until
its two tests, `x86_encode` encoded `lea rax, rcx` as `48 8d c1` and a memory
operand in register-only forms (510 of the test's broken instructions), which
`x86_validate` rejected. The check costs about 1% on Zen 5. A copy that lets an
x87 register go past ST(7) fails the tests too, as does one that lets an MMX
register go past MM7 (44 broken instructions not found), and so do nine of the
segment overrides' (the prefix's byte and presence in the templates and in the
scalar encoder, an override on an operand that isn't memory in either encoder,
an index without an index, and the gather that takes the forms from the
records). The
random streams found the form number's bit 13 in pp when #23's forms took GS's
templates past 8,192; with the old ternlog, the known encoding of a GS override
on the last form fails too. Ten copies of `x86_validate` whose ranges let one
more value in at either end of imm8, imm16, ENTER's or rel8's, or that lack
imm8's or rel8's rule, fail the range tests (140 to 351 of their broken
instructions not found), as do one that lets an /is4 immediate's bits 3 to 0
through (154), one that takes every control register number for a register
(641), one that lets MOV to CS through (138), one that lets a complex
multiplication's destination be one of its sources (1,147) and one without the
/is4 rule for vpermil2ps's and vpermil2pd's selector (11), and two that leave
out the last value of imm8 or rel8 change the reference digest. Seven copies of
#35's parts fail too: either encoder without the index's bit in V' fails three
known encodings and the random streams, a loop that never has NO_HIGH_INDEX or
GENERAL_INDEX misses 31 and 166 broken instructions, and an `x86_validate`
without its VSIB rule for the index, the opmask or the registers misses 347,
395 and 335.

The same streams cross-check the fixups. `x86_mark_symbolic` marks a quarter of
the fields that GNU as encodes the same way with a symbol in them (32-bit
displacements, which GNU as always gives a symbol even after a base, rel32s,
the rel8s of `loop`, `loope`, `loopne` and `jrcxz`, imm32s, 64-bit immediates
and MOVABS's addresses; GNU as makes `jmp` and `jcc` to a symbol rel32), with a
random sequence of its own, so that the instructions and the digest stay the
same. `--print-assembly` prints those fields as `dsym` or `offset isym` plus the
field's value, and GNU as writes zeros and a relocation for each.
`--compare-object` zeroes the fields where the scalar encoder's fixup ends and
`x86_fixup_distance` put them before comparing the bytes, and checks GNU as's
relocations (offset, type, addend and symbol, matched by offset, since GNU as
emits a relaxed branch's late) against the ones they give, with GNU as's types:
R_X86_64_PLT32 for a branch without an addend and R_X86_64_PC32 with one, PC8
for a rel8, PC32 for RIP-relative operands, 32S for other addresses but 32 for
LEA into a 16- or 32-bit register, 32S or 32 for an imm32 by whether its operand
is 64-bit, and 64 for a 64-bit immediate or address. Over seeds 1, 7 and 99:
194,957 relocations (39,560 PC32, 34 PLT32, 272 PC8, 152,058 32S, 2,400 32 and
633 64), 0 different, on all 4,939 forms whose memory operand or branch target
the streams print (of 4,947 that have one: the other 8 are the MOVQ memory forms
GNU as encodes otherwise) and all 71 with an imm32 or a 64-bit immediate, and
`x86_encode`'s fixup ends the same as the scalar encoder's. `test-x86-encoder`
has 28 known fixups from GNU as too, so that the regular tests check
`x86_fixup_distance` and the reported ends without GNU as. Twelve broken copies
fail the tests: the vectorized loop's byte order of the lengths, the byte that
carries the first qword's total, the flags' byte, the multiplier, the compress
mask and the cursor's advance (92 to 200 of each width's 201 random streams
differ), a fixup capacity one short (a page-edge fault), the template lookup
and the scalar form's row with the flag left in (both fault), the scalar
encoder's end off by one (all 28 known fixups, and the random streams), and
`x86_fixup_distance` without the immediate's bytes or without a branch's own
width (16 and 4 known fixups wrong, and the GNU as cross-check).

## Next steps

- **Every x86-64 instruction** (#17, with an issue per family, #18–#39). Rows
  are data: Xeon Phi's prefetching gathers and scatters (#38) are VSIB forms
  with a ModRM digit; APX's REX2 and its EVEX extensions (map 4, #36) would need
  a register's bit 4 in legacy forms, and its AMX forms #37's tile registers. Of
  #39's prefixes, these are left: `67` (32-bit
  addresses, for the x32 ABI: template copies of the memory forms, as the
  segment overrides are, and 32-bit registers in the memory operand), the CS,
  DS, ES and SS overrides (which 64-bit mode ignores; padding can use the
  multi-byte NOPs), branch hints and `bnd` (#38), `notrack` through memory,
  16-bit near branches (which Intel and AMD decode differently in 64-bit mode),
  and HLE's `xacquire` and `xrelease`.
- **Labels.** Fixups are done (see "Fixups"); branches inside a function are
  resolved before encoding, with forms that branch relaxation chooses, and
  Phase II's length alone is a cheap length-only pass for it. A symbolic
  displacement after a base needs a placeholder that doesn't fit in 8 bits;
  forcing 32 bits there instead would take a free bit of the record (the mode's
  bit 6, say) and an operation or two on Phase II's displacement path, not
  measured yet.
- **Threads.** Sections are independent: encode them in parallel into
  separate buffers.
- **Speed.** Shortening each group's dependency chain (see "Zen 5's costs"),
  so that the FP scheduler and the load queue hold more groups at once: the
  flags arrive about 25 cycles into a group, after a 5-cycle `vshufi64x2`, so
  templates loaded such that the flags need only in-lane shuffles, and fewer
  operations between the flags and the store, are where to look. Clang's code
  for the loop moves with small source changes, so check every width after
  one.
