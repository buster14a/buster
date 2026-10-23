# The x86-64 encoder

`src/buster/compiler/x86_64_encoder.c` turns a section (or a chunk of one) of
selected machine instructions into machine code: legacy and REX forms (x87 with
its waiting forms, MMX, 3DNow!, SSE to SSE4.2, AES-NI, SHA, GFNI, MPX, VIA's and
Zhaoxin's PadLock and the system instructions among them), VEX forms (AVX, AVX2
with its gathers, F16C, FMA, BMI1 and BMI2, the opmask instructions, VAES,
AVX-VNNI and the other VEX extensions, the variable blends and FMA4 with a
register in the immediate, AMD's XOP, TBM and LWP, and AMX's tiles) and EVEX
forms (AVX-512 with opmasks, zeroing, broadcasts, embedded rounding, registers
16 to 31, gathers and scatters, AVX512-FP16, AVX10.2, AMX-AVX512, Xeon Phi's
extensions and AMD's AVX512_BMM), which is every instruction of 64-bit mode
that XED and GNU as know but a few (see "Coverage"), with FS and GS segment
overrides on any memory operand (thread-local storage), and the prefixes of
HLE, MPX's `bnd`,
CET's `notrack` and the branch hints, and 16-bit near branches; and through
entry points of their own, 32-bit addresses (`67`, see "32-bit addresses"),
APX's general registers 16 to 31 in legacy and EVEX forms (REX2, and EVEX's B4
and X4, see "APX's registers"), and 16- and 32-bit modes, with every
instruction of theirs (see "16- and 32-bit modes").
For the instructions with a symbolic operand it reports where each one ends,
which locates the fields their fixups patch (see "Fixups").
For layout before encoding, `x86_measure` and `x86_measure_scalar` give each
instruction's offset and the total length in 64-bit mode without a
machine-code buffer (see "Lengths without bytes").
This document records why it is built the way it is: the review of the
historical `encode_wide` that it replaces, the design, the Zen 5 measurements
behind each choice, and what comes next.

## Results

On the Ryzen 7 9700X (Zen 5, clang 23, `build.sh`'s `-O3 -march=native`),
pinned to one CPU, `x86_encode` encodes a compiler-like instruction mix at
**0.55 ns per instruction** (1.82 G instructions/s, 7.2 GB/s of machine code,
3.02 cycles per instruction), and a vectorized-code mix of AVX-512, AVX2 and
BMI at 0.56 ns (10.8 GB/s), and right after 100 ms of the scalar encoder, the
state a compiler runs it in, at 0.56 ns too (see "Both states"; after 60 ms of
it, the loop before #182 took 0.62). That is:

- **23×** a plain one-instruction-at-a-time scalar encoder on the compiler mix
  (12.98 ns), 26× on the vector mix and 37× on a uniform mix of every form
  (22.83 ns), where the scalar encoder's branches mispredict;
- **7.5–9.6×** the historical `encode_wide` (with its defects fixed) on the
  subset of instructions it can encode: 0.63 ns against 4.8–6.1 ns, for
  byte-identical output (measured before VEX and EVEX support, whose first
  version cost 6–9% on long streams, which the loop's changes won back; the
  checks and forms added since cost about 3%, segment overrides nothing, and
  #182's changes took off 11%: see "Evaluation");
- byte-identical to GNU as 2.47 on 1.7 million random instructions covering
  all 8,240 forms (2,136 legacy, 154 of them x87, 1,065 VEX, 103 of them XOP,
  and 5,039 EVEX), one in sixteen of them with an FS or GS override;
- with the ends of the instructions that have symbolic operands (the 6% of the
  compiler mix that are calls and RIP-relative operands), **0.60 ns**, as much
  as every instruction's offset, and where GNU as 2.47 puts its relocations
  (offset, type, addend and symbol) for all 198,573 symbolic fields of those 1.7
  million instructions, on every form whose symbolic fields GNU as encodes the
  same way;
- with 32-bit addresses, byte-identical to GNU as 2.47 on another 1.7 million
  random instructions and their 197,926 relocations, from a second entry point
  whose templates have the `67`, so that the 64-bit loop's code is instruction
  for instruction the same;
- with APX's general registers 16 to 31, byte-identical to GNU as 2.47 on 1.7
  million more random instructions, 315,369 of them with REX2 and 276,792 EVEX
  ones with registers 16 to 31 (80,878 in map 4's forms of the legacy
  arithmetic, 16,634 in the EVEX forms APX gives VEX's and maps 2 and 3's
  instructions, and 46,248 in its new ones), and their 197,735 relocations,
  from a third entry point, whose loop costs 20% more than `x86_encode`'s (0.66
  ns per instruction on the compiler mix) and leaves the 64-bit loop's code the
  same;
- in 32-bit and 16-bit modes, byte-identical to GNU as 2.47 after `.code32` and
  `.code16` on 1.2 million random instructions each, over the 6,187 and 6,186
  forms each mode has (111 of them its own: `aaa`, `bound`, `lds`, `pusha`, far
  pointers, the test registers' moves and the rest), with both address sizes,
  ModRM's 16-bit addresses, segment overrides of every segment and AMD's CR8,
  and their 135,563 and 136,322 relocations, from entry points of their own,
  whose loops encode a mix like 32-bit compiler output at **0.68 ns per
  instruction** and one like real-mode code at 0.68 ns, 22× and 24× their
  scalar encoders, and leave the other loops' code the same.

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
registers (and for `x86_encode_apx`'s general registers in a legacy form of maps
0 and 1 or an EVEX form), 0–7 for opmasks, MMX registers (which REX doesn't extend), debug
registers and AMX's tiles, 0–3 for MPX's bound registers, 0–5 for the segment
registers (ES, CS, SS, DS, FS and GS), the numbers of CR0, CR2 to CR4 and CR8
for the control registers, and for a group of registers its first, whose number
is a multiple of the group's size: the even ones for VP2INTERSECT's opmask pair
and AMX-TRANSPOSE's tile pairs, and multiples of four for 4FMAPS's and 4VNNIW's
blocks of four (the processor ignores the low bits). Operands a
form doesn't use are zero, which is also what pads a group: form 0,
`X86_FORM_NONE`, encodes to nothing.

The forms are X-macro tables. The legacy one has 2,088 rows (integer arithmetic
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
with the MMX register forms of SSE, SSE2 and SSSE3, AMD's 3DNow! (whose
operation byte, the opcode's last, comes after the operands, where an 8-bit
immediate goes: the record's immediate is that byte, which `x86_validate`
checks), all of x87, whose ST(i) registers are a register class of 8 in
ModRM.rm, MPX with its bound registers, HLE's `xacquire` and `xrelease` on
the locked forms and on `xchg` and `mov` to memory (three prefixes with 66, so a
form's prefixes are four bytes), MPX's `bnd` and the branch hints (`cs`, not
taken, and `ds`, taken) on the near branches, `notrack` through memory too,
16-bit near branches (66 with a rel16), and the system instructions:
descriptor tables and selectors, port I/O through DX or an immediate and its
strings, system calls and returns, caches and TLBs, MSRs, XSAVE, FSGSBASE, PKU,
SMAP, CET's shadow stacks, and AMD's SVM and SEV-SNP and Intel's VMX, TDX, SGX
and SMX, among others); a row
gives the prefixes, the opcode bytes, how ModRM is used (`/r`, `/digit`, `+r`
or none), the immediate and relative widths, REX.W and the operand classes. The
VEX and EVEX one has 4,517 (moves, masked and non-temporal moves, gathers and
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
register; the MSR instructions with an immediate MSR index, VEX rows in map 7
the same way, with a 32-bit immediate; and AMX: the tile configuration, tile
loads (and AMX-TRANSPOSE's of a pair of tiles) and stores, transposes, and dot
products and multiplications of three tiles (FP8's and the read-shared pair
loads in VEX map 5, the same way), and AMX-AVX512's moves and conversions of a
tile's row into a ZMM register, by an immediate or a general register in
EVEX.vvvv; and Xeon Phi's AVX512ER, AVX512PF's prefetching gathers and scatters
(VSIB forms with a ModRM digit), and AVX512_4FMAPS's and AVX512_4VNNIW's
products of a block of four registers and 16 bytes of memory); its rows add the
vector length, W, the vvvv operand, the decorations the form can have and EVEX's
N, the unit of its 8-bit displacements. A broadcast memory operand, embedded
rounding and suppressed exceptions are forms of their own (`_M32BCST`, `_ER`,
`_SAE`), because they set EVEX.b and choose N, which the template then has. So
are APX's NF and ND forms (see "APX's registers"), whose EVEX forms of general
register instructions are rows of this table too.
AVX-512F's, BW's, DQ's and CD's forms are at 128 and 256 bits too (AVX512VL),
and an EVEX form whose name a VEX form has ends in `_EVEX`. A gather's or
scatter's memory operand is a VSIB one, whose index (in the record's `index`,
XMM4 too) is a vector register: Intel's vm32x to vm64z, the forms' `VM32X` to
`VM64Z`, name the indexes' size and register. AMX's tile loads and stores take
a SIB memory operand (`SIBMEM`, Intel's sibmem), which always has a SIB byte:
its index (the stride) is a general register, of which 4 (RSP's number) is
SIB's none, without a scale, and it can be an absolute address, but not a base
alone (that is a base with none for the index) or RIP-relative. The tiles are a
class of 8 registers (TMM0 to TMM7). MPX's `bndldx` and `bndstx` take a MIB
operand (`MIB`, Intel's mib), whose index is the pointer they look up in the
bound tables, not part of the address, so it takes no scale, and `bndmk`'s
memory operand (`M_NO_RIP`) has the lower bound in its base; neither can be
RIP-relative. These kinds are flags of an operand, as is a group of registers (a
pair, or a block of four), so the form table's operands are 16 bits. The classes
drive validation and the tests' assembly printer. A class of fewer than 16
registers (opmasks, MMX's and the bound registers) that ModRM.rm can also
address memory with is two forms, a
register one and a memory one, since the template's checks of rm can't depend
on the mode: mm8 to mm15 don't exist, but a base can be R8 to R15. So MMX's
`paddb mm, mm/m64` is `PADDB_MM_MM` and `PADDB_MM_M64`, and `movq`'s store
opcode between registers is `MOVQ_MM_MM_STORE`. With APX, SSE's XMM register
or memory would need the same (xmm16 to xmm31 don't exist there, but a base can
be R16 to R31), which `x86_validate_apx` checks instead (see "APX's
registers").

### Templates and the canonical slot

`x86_encoder_initialize` turns each form into a 32-byte template: the bytes the
form always has (prefixes and REX's `0x40 | W`, or the VEX or EVEX prefix;
opcode; the ModRM `/digit`) at their positions in a 16-byte slot, checks (the
bits of reg, rm, vvvv and the decorations that must be zero for the form, as
`x86_validate` has them, of which the loop takes only whether a 16- or 32-bit
mode's form can take a segment override), flags (static length, a static
displacement's presence, byte operands, REX.W, VEX2, EVEX's N, and for
`x86_validate`, a template without room for a memory operand's bytes), and a
layout: for
each of the 15 positions, which byte of the instruction's *canonical slot* goes
there. The template is stored in a 64-byte entry, a cache line, followed by its
two halves the other way around (the layout, then the bytes), so that a 32-byte
load of the entry has the template's bytes in its low 128-bit lane when it starts
at the entry and in its high one when it starts at the middle: Phase I then puts
each instruction's bytes in its own lane with a blend (see "The loop" and "Zen
5's costs").

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
15 bytes (the longest EVEX one is 12). It counts every position a form asks for,
but writes only those that fit in the slot's 16, so a form that doesn't fit
(APX's map 4 forms with an imm32, a segment override and a `67` ask for 17) is
rejected without writing past its template.

A segment override is the form's too, as `lock` and `rep` are: the form number
plus `BUSTER_X86_FORM_FS` or `BUSTER_X86_FORM_GS` (8,189 or twice that) names a
template of its own, which starts with the prefix as a static byte (64 or 65,
before the other prefixes, as GNU as puts it). `x86_encoder_initialize` builds
every form's template three times, 24,567 entries and 1.57 MB in all, of which a
stream touches its own forms' lines; a form without a memory operand gets copies
that take no operand at all. So x86_encode does nothing for an override that it
doesn't do for every form, and its loop is the same code with them as without.
The fixup flag works the same way (see "Fixups"): the templates are copied again
at the flag's index, 1.57 MB more.

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
multiple of N whose quotient is in [−128, 127]. The scale is the template's, and
the template arrives last (see "Zen 5's costs"), so Phase II works out from the
displacement alone a bitmask of the scales it fits for, `x ^ (x − 1)` (the
multiples) without `0x01ffffff >> lzcnt(|x|)` (the scales too small for its
size). The template's scale field holds log2(N) plus one, so that rotating the
bitmask right by it puts bit log2(N) in the sign, one rotation; and the
displacement rotated left by one and then right by the field has the quotient in
the byte the canonical slot takes. What depends on whether it fits (ModRM.mod,
the displacement and its presence bits) is then one select each between values
worked out before the template arrives.

`x86_encode` checks nothing about the instructions: it's the code generator's,
and takes the ones instruction selection and register allocation make, which
`x86_validate` accepts (the tests and debugging check them with it). Until #182
its loop also tested what its encoding can't represent, in one test at the end
of the call (the template's checks against reg, rm, vvvv and the decorations, the
addressing mode's forbidden bits, a high byte register with REX, the kinds of
rm operand and index the form can't have): about 30 instructions per group in
the 64-bit loop, and in 16- and 32-bit modes 50, a vector of invalid lanes that
clang spilled with its store and reload back to back at the end of each group,
and a clamp of the output's advance to 15 bytes per instruction (see "16- and
32-bit modes"). Without them the loops are 9% to 20% faster (see "Evaluation"),
and `x86_validate` checks everything they did and the rules that depend on more
than one field (zeroing without an opmask, zeroing into memory, a VSIB operand
without an index, an EVEX gather or scatter without an opmask, a gather whose
destination, index and mask aren't different registers, an AMX product whose
three tiles aren't either, a SIB operand without a SIB byte, a base alone or
RIP, or with none and a scale, a RIP-relative operand of MPX's `bndmk` or a MIB
operand, a MIB operand's scaled index, a 3DNow! form's immediate other than its
operation byte, and a `notrack` form with a segment override), and the ranges of
the immediates (imm8 in [−128, 255], imm16 in [−32768, 65535] and ENTER's in
[0, 2²⁴)) and of a rel8 ([−128, 127]) and a rel16 ([−32768, 32767]): x86_encode
writes the field's low bytes, so `jmp rel8` with a displacement of 200 is `eb
c8`, a jump of −56. With an instruction `x86_validate` rejects, what `x86_encode`
writes is undefined (in 16- and 32-bit modes, where one can be 16 bytes long,
even past the output's capacity). x86_encode ignores an immediate or a
displacement the form has no field for, but a register field the instruction
doesn't use must be zero, as `x86_validate` requires: REX.B and REX.X come from
rm's and the index's bit 3 before the addressing mode's mask (see "Zen 5's
costs"), so the rm of a memory operand without a base, or an index without one,
would set them.

### The loop

`x86_encode` works on groups of 16 instructions, one per iteration of its loop
(the benchmark's widths of 32 and 64 instructions per iteration run two and
four, in a loop over them that stays rolled; see "Zen 5's costs" and
"Evaluation" for why the entry points take 16):

1. **Load.** Four records per ZMM register, and each form's template (a YMM
   load and `vinserti64x4` per two: the even slots' entries from their start and
   the odd slots' from their middle, so that each slot's bytes land in its own
   128-bit lane, and a blend takes a quad's bytes and a `vpermt2q` its layouts).
   The records' fields are transposed into one instruction per dword lane with
   in-lane unpacks: instruction `4q + j` goes to lane `4j + q`, so that the
   transposes both ways stay inside 128-bit lanes.
2. **Compute**, 16 instructions per operation. The addressing mode's 4 bits
   index three 16-entry tables (`vpermd`): which canonical bits come from the
   operands, which are fixed (mod 11 for a register, rm 100 for a SIB byte or
   101 for RIP, SIB base 101 or index 100 for none), and which canonical bytes
   the mode always has. Three
   `vpmultishiftqb` put every register field where it goes in the canonical
   dword. What depends on values (RBP and R13 bases and RSP and R12 bases, from
   a `vpshufb` of a 16-byte table by the base's low nibble, the displacement's
   size, REX for SPL–DIL, VEX2) are masks built with arithmetic any vector pipe
   runs; a presence bit that waits for the template's flags is one `vpminud` of
   a value that is zero only without it (REX's, P0's) or one select by the
   displacement's fit, ORed in last. The result is each instruction's canonical
   bytes, the presence bits of its canonical slot and its length.
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

With offsets or fixup ends, Phase II's lengths are summed in each group, after
Phase III: a `vpermb` puts them in instruction order as bytes, a
multiplication by 0x0101010101010101 (`vpmullq`) sums each qword's bytes up to
each one (eight lengths of at most 15, so nothing carries), and a `vpshufb` and
an add carry the first qword's total into the second. That is each
instruction's end in the group (at most 240), and minus its length, its start;
`vpmovzxbd` widens them and the group's offset in the output, broadcast from
memory, is added (see "Zen 5's costs" for why bytes, why after Phase III and why
from memory).

### Lengths without bytes

```c
static int64_t x86_measure(const X86Instruction* instructions, uint64_t count, uint32_t* offsets);
static int64_t x86_measure_scalar(const X86Instruction* instructions, uint64_t count, uint32_t* offsets);
```

These internal entry points return the total byte length and write the same
instruction-start offsets as `x86_encode`, starting from zero on each call.
They require `x86_encoder_initialize` and instructions accepted by
`x86_validate`; they do not validate the records. They use the ordinary
64-bit-mode templates, not the address32, APX, mode32 or mode16 entry points.
FS/GS overrides and `BUSTER_X86_FORM_FIXUP` markings have the same length
semantics as in `x86_encode`; measuring does not produce fixup ends.

For a nonzero count, `offsets` is required and has room for `count`
u32 elements, disjoint from the input records. Neither array needs padding:
the functions read only those records and write only those offsets, with no
extra end element. A zero count permits null input and output pointers and
returns zero. Counts above `BUSTER_X86_ENCODER_MAX_INSTRUCTIONS` return
`X86_ENCODER_ERROR_TOO_MANY_INSTRUCTIONS` before either array is accessed.
Other invalid inputs are outside the contract, as with `x86_encode`.

The vector path takes 16 records at a time. It transposes their operand and
displacement fields as the encoder does, but loads only each template's
32-bit flags, with scalar loads rather than a gather. `compute_fields` shares
the encoder's length rules; its unused canonical bytes, prefixes and
displacement results are eliminated. Template byte/layout vectors, assembled
machine code and its stores are absent. The lengths are put in instruction
order and summed as bytes with the same multiplication and shuffle as the
encoder's offsets, using 128-bit operations for the 16 bytes. Their group
total is at most 240, so the byte sums do not carry into neighbours. The
starting offset is added and the 16 u32 starts are stored. A final partial
group is copied to a zeroed 16-record local array and its offsets are stored
with a mask; zero records contribute zero length.

The scalar twin derives each length from `encode_one` and its independent
form rules, using a local buffer of `BUSTER_X86_MAX_INSTRUCTION_BYTES` for one
instruction. It provides a separate reference for the vector length rules.
`test-x86-encoder` checks both against the known encodings and both encoders,
over all admitted forms with segment overrides and fixup markings, random
streams, vector and former chunk boundaries, and separately rebased
partitions. Guard pages and canaries check input and output bounds at every
dword alignment, and empty and oversized counts are tested without accessible
arrays.

Branch sizing now uses these entry points for its initial offsets, removing
its discarded-byte scratch buffer, encode chunks and offset-rebasing pass
([branch sizing](branch-sizing.md#lengths-and-memory)). The caller then
encodes the sized records once. The implementation comparison, benchmark
commands and host qualification are recorded in
[the lengths-only measurement record](measurements/x86-lengths-2026-10-23/README.md).

### Fixups

The code generator binds names before instruction selection, so most operands
are numbers by the time an instruction is encoded. What only layout can fill in
are fields: a call's rel32, a RIP-relative displacement to a global, an imm32
or imm64 with a symbol's address. The code generator marks those instructions
with `BUSTER_X86_FORM_FIXUP`, bit 15 of the form (the last form with a segment
override is 18,350), and puts a placeholder in the field. The encoders encode
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
mask's popcount; the zeroed copy's padding isn't marked. A marked form's
template is a copy at its own index: `x86_encoder_initialize` copies the 18,351
templates to `BUSTER_X86_FORM_FIXUP` and up (the pages between stay unmapped),
so Phase I looks a form up as the record has it, and the loop without offsets
and fixup ends is the same code as before. Taking the flag off each form with
an AND instead cost the compiler mix 2.4% (15 interleaved rounds, after #35's
changes; it had measured no slower before them).

The other way to have the ends is the offsets: the caller picks the marked
instructions' and takes the next instruction's start. Both are in the benchmark
(`x86_encode_64` with `--fixups`, and `x86_encode_64_offsets_then_pick`, which
picks from the list of marked instructions a code generator has); see
"Evaluation".

### 32-bit addresses

The address-size override `67` makes a memory operand's base and index 32-bit
registers, and RIP EIP: the x32 ABI's addresses. Template copies can't hold it:
copies of every form for `67`, with and without FS or GS, would take the
templates past the 32,768 that the form number's 15 bits beside the fixup flag
reach, and a prefix the loop builds from a bit of the record would cost every
group operations. So it has an entry point of its own, with templates of its
own: `x86_encode_address32`, `x86_encode_scalar_address32` and
`x86_validate_address32` work as the 64-bit functions do, but give every memory
operand a `67`, from the templates `x86_encoder_initialize_address32` builds
(all three need it, validation too). A call can't mix address sizes, and the
64-bit loop pays nothing: its code is instruction for instruction the same.

The 32-bit table, `x86_templates_address32`, has the 64-bit one's indices (FS
and GS copies, and the fixup copies): another 3.14 MB, which only a program that
calls its initializer touches. `build_template` puts the `67` after the segment
override and `notrack`'s `3E` and before the other prefixes, GNU as's order:
segment, `3E`, `67`, `66`, `F2` or `F3`, `F0`, then REX, VEX or EVEX (`notrack
bnd jmp qword ptr [eax]` is `3E 67 F2 FF 20`). Its layout byte,
`BUSTER_X86_LAYOUT_ADDRESS32`, is a static byte's (`vpshufb` makes it zero) with
presence bit 23, which no other presence term has (the 16-bit address mode
table's bit 21 aside, the others' are all below 16): `compute_fields` copies the
memory mode's bit (the operands' bit 26) there, and counts it in the length,
only in the 32-bit loop, so a form's register operands get no `67`. Which loop
it is is a constant parameter of `encode_group`, `encode_batches` and the
instantiations (`x86_encode_16_address32` and `x86_encode_64_address32`), and
`compute_fields` uses it to pick a mask, zero in the 64-bit loop, rather than in
a branch: with an `if`, clang simplified the code before inlining it, and the
diff of `x86_encode_64`'s disassembly with main's had 3,346 lines (clang 23 on
the 9700X). A `/r` ModRM's template byte is zero, so it may come after the
form's own 8 bytes (Phase III XORs in only bytes 0–7), which leaves room for the
`67` in 22 more templates (the FS and GS copies of HLE's two-byte `/r` forms and
of `crc32` on a word) and changes no 64-bit one.

80 templates had no room for it, all FS or GS copies: 12 would have their own
bytes run past the first 8 positions (HLE's `lock bts`, `btr` and `btc` on a
word by an imm8: `64 67 66 F2 F0`, REX's place, `0F BA` and the ModRM digit),
and 68 would be 16 bytes long, past x86's 15, with a 32-bit displacement and an
imm32 (HLE's locked arithmetic on a doubleword or a quadword, TBM's `bextr` and
LWP's `lwpins` and `lwpval`). APX's map 4 forms with an imm32 (see "APX's
registers"), the legacy arithmetic's and `ccmp`'s and `ctest`'s, add 232 FS and
GS copies, which have no room for the segment override with 64-bit addresses
either, and 116 forms without one, whose 67 has none. The initializer gives each
of them x86_encode's template of the form without the segment override, marked
as taking no memory operand, so that validation rejects those instructions, in
every mode (`x86_form_lacks_room`), and the scalar encoder with it. Validation
also rejects MPX's memory operands, which are #UD with a `67` in 64-bit mode (GNU
as rejects them). The scalar encoder writes the segment override, `3E` and
`67`, then the other prefixes. The CS, DS, ES and SS overrides that pad are left
out (see "Next steps").

In 15 interleaved rounds of `x86_encode_64` on the 9700X against main
(`x86-opt/ab.sh`, now `build/compiler ab`: [profiling.md](profiling.md)), the
compiler mix took 0.620 ns per instruction against 0.627
(3.36 cycles against 3.38), and the vector and uniform mixes, calls of 16 and
the loops with offsets and with fixup ends −1.4% to +0.6%, within the ±1.5% a
build varies between runs; with both built with `-falign-functions=64`, the
compiler mix took 0.623 ns either way and the others −1.0% to −0.1%.
`x86_encode_16`, `x86_encode_32` and `x86_encode_64` disassemble the same as
main's, both copies of each, both ways.

### APX's registers

Intel's APX doubles the general registers to 32. A legacy instruction of opcode
maps 0 and 1 (no escape, or `0F` and one byte) reaches R16 to R31 through a REX2
prefix in REX's place: `D5`, then a payload of M0, R4, X4, B4, W, R3, X3 and B3,
where R4, X4 and B4 are bit 4 of ModRM.reg's register, the index's and
ModRM.rm's (the base's, or the `+r` register's), R3, X3 and B3 are REX's R, X
and B, and M0 says the opcode is in map 1, whose `0F` it replaces:
`movzx r16d, byte ptr [rax]` is `D5 C0 B6 00` (3DNow!'s second `0F` stays). REX2
counts as REX for SPL to DIL, so a high byte register can't be in such an
instruction either. R21 and R29 as a base take an 8-bit zero and R20 and R28 a
SIB byte, as their low bits are RBP's and RSP's, but R20 is an index, which X4
tells from SIB's none. Maps 2 and 3 have no REX2 (APX encodes their
instructions with EVEX, in map 4), nor does the XSAVE family, with which REX2 is
#UD (GNU as rejects it too).

Like 32-bit addresses, APX's registers have an entry point of their own, with
templates of their own: `x86_encode_apx`, `x86_encode_scalar_apx` and
`x86_validate_apx`, from the table `x86_encoder_initialize_apx` builds (3.14 MB
more). An instruction without registers 16 to 31 encodes as with `x86_encode`:
REX2 is there only where one of them needs it. Template copies couldn't do it,
as REX2 depends on the operands, not the form, and in map 1 it takes the `0F`'s
place. `build_template` gives a legacy form of maps 0 and 1 two positions for
it. REX's takes canonical byte 10 as before, which `compute_fields` makes `D5`
with REX2 (flipping the template's `0x40 | W` by `0x95 ^ W`). A legacy form has
no VEX or EVEX prefix to take canonical bytes 12–15, so the payload is one of
those: in map 0, a position of its own after REX's (canonical byte 15, whose
template byte is W), present with REX2 alone and then counted in the length;
in map 1, the `0F`'s (canonical byte 13), which is zero without REX2 and with it
flips the `0F` into the payload, M0 and W too. 1,950 forms have REX2's
positions, and their FS and GS copies but 56: HLE's locked arithmetic on a
doubleword or a quadword by an imm32 would be 16 bytes long with a segment
override, so those get `x86_encode`'s template, as the forms without REX2 do, and
take registers 0 to 15 alone (`x86_form_has_rex2`).

`compute_fields` works out REX2's mask (bit 4 of reg, rm or the index: in
another form, a vector register's, whose template has no REX2 position to use
it), its payload and `D5`, and selects them in at the end, in a legacy form's
prefix dword, REX's byte and the presence bits; a `+r` form's opcode register
drops the bits above it, which with REX2 have rm's bit 4. `x86_validate_apx`'s
rules change in three places: an index's bit 4 is X4, so R20 isn't RSP; a high
byte register can't have REX2; and a base can be R16 to R31 in a form with REX2
or an EVEX one but in no other, nor can a register rm that isn't a general one in
a form with REX2 (SSE's XMM register or memory; the classes of fewer than 16
registers are two forms anyway). Until #182 the loop checked those too, with a
mode table that let a base's bit 4 through and two template flags.

In an EVEX form, APX reaches registers 16 to 31 in the general registers, the
base and a general index through bits every template had fixed: B4 (P0's bit 3)
is a base's or a general register rm's bit 4, and X4 (P1's bit 2, inverted:
EVEX's U bit, which was always 1) a general index's, while a general register in
reg or vvvv takes R' or V', as a vector register does (`vmovq xmm17, r31` is
`62 C9 FD 08 6E CF`). A general register rm's bit 4 isn't X's, as a vector
register's is, so the templates of EVEX forms whose rm is a general register
have a flag (`GENERAL_RM`, in the place of REX.W's flag, which an EVEX form
doesn't use), and `x86_mode_fields_apx` leaves a base's bit 4 out of X; and a
general index's bit 4 isn't V''s, as a VSIB one's is. `compute_fields` works B4
and X4 and those corrections out from the operands and the flags, and XORs them
into the prefix at the end: making P0 wait for the flags instead cost about 2%
more. A VEX form can't reach registers 16 to 31: the code generator picks the
EVEX form, as GNU as does (`vaddps xmm1, xmm2, [r16]` is EVEX), and the VEX
forms without one (`vtestps`, `vpblendvb`, `vldmxcsr`, AVX2's gathers, FMA4 and
XOP) have none with APX either.

APX gives VEX's and maps 2 and 3's instructions on general registers EVEX forms
for that, which reach registers 16 to 31 the same way: BMI1's and BMI2's,
`kmov`'s, CMPccXADD's, AMX's loads and stores (AMX-TRANSPOSE's too) and the MSR
instructions with an immediate in their VEX maps (map 7 for EVEX too), and in
map 4, the legacy ones of maps 2 and 3 (`crc32`, ADX, `movbe`, which can
byte-swap a register there, MOVRS, RAO-INT, CET's `wrss` and `wruss`, ENQCMD,
VMX's `invept` and `invvpid`, `invpcid`, MOVDIRI, MOVDIR64B and USER_MSR's
register forms) and the bit counts (`tzcnt`, `lzcnt` and `popcnt`, without
their `F3`): `crc32 r16d, eax` is `62 E4 7C 08 F1 C0`. They are 172 rows of the
vector table (named with `_EVEX` where a legacy or VEX form has the name), with
two more decorations that a form always has: NF, which leaves the flags as they
are (P2's bit 2, the opmask's high bit, which these forms don't have: the `_NF`
forms of BMI's instructions that set flags and of the bit counts), and ND, a new
destination in EVEX.vvvv (in EVEX.b's place: ADX's three-operand forms, whose
syntax is `VVVV_REG_RM`). Their 8-bit displacements aren't scaled (N is 1).
The templates have NF, ND and the maps as they have the rest of the prefix, so
the loops do no more for these forms, with one exception, which costs nothing:
the index of AMX's SIB operand is a general register, whose bit 4 is X4 too, so
the templates of EVEX forms say which index is general with a flag of their own
(`GENERAL_INDEX_4`, in REX.R's flag's place, which they don't use either), which
`compute_fields` takes X4 from. They take registers 0 to 15 in every entry point,
and 16 to 31 in `x86_encode_apx`'s.

Map 4 has APX's forms of the legacy arithmetic, 892 more rows: the two-operand
forms of `add` to `xor` but `cmp` (without the accumulator's), the shifts and
rotates, `not`, `neg`, `inc`, `dec`, the multiplications and divisions, `imul`'s
other forms and `shld` and `shrd`, as the legacy forms with EVEX's prefix; ND
forms of all but the multiplications and divisions, with a new destination in
EVEX.vvvv (`add r16d, r17d, r18d` is `62 EC 7C 10 01 D1`), in the syntaxes
`VVVV_RM_REG`, `VVVV_RM_ONE` and the rest; NF forms of both for the operations
that set flags without reading them (not `adc`, `sbb`, `rcl`, `rcr` or `not`);
`cmovcc` with ND; `setcc`'s EVEX form (`F2`); and ZU's (ND's bit, which zeroes
the destination's upper bits instead of keeping them) of `setcc` (`setzucc`, on a
register) and of `imul`'s 16-bit forms by an immediate (`imulzu`). Their
templates take imm16s and imm32s in map 4, and with an imm32 and a memory
operand, a form's longest encoding is 15 bytes (`add dword ptr
[rax+rbx*4+0x12345678], 0x12345678` is `62 F4 7C 08 81 84 98` and 8 bytes), so
its FS and GS copies have no room for the segment override, nor its 32-bit
addresses for the 67: those take no memory operand (see "32-bit addresses").
GNU as rejects only the instructions that are 16 bytes long; validation rejects
the forms' every memory operand, so `add dword ptr fs:[rax], 0x12345678` (11
bytes) takes the legacy form, with REX2 for registers 16 to 31.

APX's new instructions are map 4's too, but for `pushp`, `popp` and `jmpabs`
(below): 516 more rows. `ccmpcc` and `ctestcc` compare or test as
`cmp` and `test` do if their source condition holds, and otherwise set the flags
to a default flags value (OF, SF, ZF and CF): the condition (whose 10 and 11 are
always true and always false, `t` and `f`, rather than `p` and `np`) is in P2's
bits 3 to 0, where V', NF and the opmask are otherwise, as a decoration of the
form (`SCC(K)`), and the default flags value is in EVEX.vvvv as it is, not
inverted, as an operand of its own (`DFV`, 0 to 15), which GNU as writes after
the mnemonic: `ccmpb {dfv=of, sf} eax, ecx` is `62 F4 64 02 39 C8`. Their
templates have the condition in P2 and zeros in P1's vvvv, where the others have
ones, so that the canonical byte's vvvv goes in as it is: the loops do nothing
new for them either (all twelve disassemble as they did before these rows). With
an imm32, they have the legacy arithmetic's 15 bytes and its FS and GS copies
without room. `cfcmovcc` is a conditional move at `cmovcc`'s opcodes that
doesn't fault on memory it doesn't load or store (when the condition is false):
into a register without ND or NF, into a register or memory with NF (its store
form) and with a new destination with both (`cfcmovb eax, ecx, edx` is `62 F4 7C
1C 42 CA`). `push2` and `pop2` push and pop two registers, the first in
EVEX.vvvv (with ND's bit), and `push2p` and `pop2p` (W1) hint that the pop pairs
with a push; their registers can't be RSP, and `pop2`'s must be different (#UD
otherwise), which validation alone checks, as it does AMX's tiles.

`pushp` and `popp` (`push` and `pop` with REX2's W, a hint that the pop pops
what a push pushed) and `jmpabs` (a jump to a 64-bit absolute address: `A1` with
REX2 and W0) always have REX2, so their rows have its `D5` as a prefix, and they
always have REX as well (`REX_ALWAYS`'s flag, whatever W is), whose presence
stands for REX2's. `x86_encode_apx`'s templates of them have `D5` as a static
byte in REX's position, present with REX (a static byte with REX's presence bit,
as the `67`'s layout is one with presence bit 23), and the payload's position
always there, in the static length, which needs nothing new of the loop: forcing
REX2's mask from a flag of the template instead cost the APX loop 3–5%, as the
mask then waited for the flags. The other entry points' templates have a static
`D5` before REX's position, which is the payload (W, R3, X3 and B3 in REX's
place), for registers 0 to 15: `pushp r8` is `D5 09 50` from every entry point,
and `pushp r16` `D5 18 50` from `x86_encode_apx`. The loops are all the same
code as before them.

The loops pick all of it by a constant parameter, as with 32-bit addresses, so
their code is the same: all eleven 64-bit and 32-bit loops disassemble as main's
(clang 23 on the 9700X), and in 15 interleaved rounds against main,
`x86_encode_64` took 0.621 ns per instruction against 0.627 on the compiler mix,
0.633 against 0.627 on the vector mix and 0.662 against 0.664 on the uniform
one, within the ±1.5% a build varies between runs.

The APX loop costs 21% more: 0.748 ns per instruction against `x86_encode_64`'s
0.619 on the compiler mix (4.03 cycles against 3.37, in 7 interleaved rounds
of the benchmark's `x86_encode_64_apx`), 20% on the vector mix, 19% on the
uniform one, and 16% with offsets or with fixup ends; with REX2 alone, it cost
18%, 18%, 14% and 12%. The benchmark's streams have no registers 16 to 31, but
the loop has no branches, so it does the same work with them. It runs 3.4 more
instructions per instruction: about 32 vector operations per group of 16 for
REX2 and 10 for EVEX's bits, and the spills and constants of the registers they
take (with REX2 alone, 60 more stack operands and 53 more broadcasts over the
function's copies of the group). Since #182 (see "Zen 5's costs"),
`x86_encode_apx`'s loop, `x86_encode_16_apx`, takes 0.663 ns per instruction on
the compiler mix against `x86_encode`'s 0.553, 20% more (and 0.646 after the
scalar loop, against main's 0.781), which is why APX's registers stay an entry
point of their own rather than part of `x86_encode`'s loop: on code without
them, that would cost every group the same 20%.

### 16- and 32-bit modes

32-bit mode (protected mode, and compatibility mode under a 64-bit kernel, with
a 32-bit code segment) and 16-bit mode (real mode, virtual-8086 mode, and
protected mode with a 16-bit code segment) have entry points of their own:
`x86_encode_mode32`, `x86_encode_scalar_32` and `x86_validate_32`, and
`x86_encode_mode16`, `x86_encode_scalar_16` and `x86_validate_16`, after
`x86_encoder_initialize_legacy`. An instruction is the same record, with the
form table's forms and their fixed operand sizes, and the mode decides the
prefixes:

- **Operand size.** The table's 16-bit forms carry the `66` that 64-bit mode
  needs, and its 32-bit forms none. `x86_encoder_initialize_legacy` gives each
  form a byte of properties (`x86_form_legacy`): a legacy form without vector
  operands whose `66` is the operand size's (of map 0, or with a 16-bit
  register, memory operand, rel16 or 6-byte pseudo-descriptor) is a 16-bit form,
  which has no `66` in 16-bit mode (342 forms); and a legacy form without a `66`
  or vector operands that has a 32-bit register or a rel32, or is a 16-bit
  form's instruction without its `66` (with an imm32, a rel32 or a ptr16:32
  where that has 16 bits), is a 32-bit form, which takes a `66` in 16-bit mode
  (400): `add eax, ebx` is `01 D8` in 32-bit mode and `66 01 D8` in 16-bit
  mode, and `add ax, bx` the other way around. The others keep their prefixes in
  both modes: a mandatory `66` is the opcode's (ADCX's); the moves to and from
  control and debug registers are always 32-bit; `ret`, `enter`, `leave`, a push
  of an imm8 and the pushes and pops of FS and GS keep the mode's default size
  (their 16- and 32-bit forms are rows of their own); and the forms whose 32-bit
  register W alone sizes, which a `66` would make another instruction of (`66 0F
  38 F6` is ADCX, not `wrssd`) or leave as they are, are GNU as's IgnoreSize
  ones (MOVNTI, MOVDIRI, PTWRITE, UMWAIT, ADOX, `crc32` of a byte, CET's
  `incsspd`, `rdsspd` and `wrssd`, RAO-INT's `aadd`, `aor` and `axor`, `vmread`,
  `vmwrite` and `rdpid`), as are those whose register is an address of the
  address size (`umonitor`, `enqcmd` and `enqcmds`).
- **Address size.** The mode's, or with `BUSTER_X86_MODE_ADDRESS_SIZE` in the
  record's mode (bit 6, which 64-bit mode doesn't take) the other one, with a
  `67`. A 16-bit address is ModRM's own: BX or BP and SI or DI, one of the four
  alone, or absolute (mod 00 rm 110 and a disp16), without a SIB byte, so
  without a scale, and VSIB and SIB operands need a 32-bit address, as MPX's do
  (#UD with a 16-bit one). Its displacement is modulo 2^16, as the address is
  (`[bx+65535]` is `[bx-1]`, an 8-bit one, as GNU as takes it), and `[bp]`
  always has one. Without RIP, a 32-bit absolute address is ModRM's own too
  (mod 00 rm 101, without a SIB byte). Without a memory operand, the bit gives
  the other size to the addresses the instruction makes itself (100 forms): the
  string instructions', `xlat`'s, `jcxz`'s and `loop`'s (rCX's), `maskmovq`'s
  and `maskmovdqu`'s, PadLock's, `monitor`'s, `clzero`'s and the other system
  instructions' in rAX, and `umonitor`'s register, a 16-bit one with it in
  32-bit mode (and `jrcxz`'s form is `jecxz` or `jcxz` by it). The 12 moves
  between the accumulator and an absolute address are forms of their own for
  each size, moffs16 or moffs32 in the immediate, whose `67` the mode adds where
  the size isn't its own.
- **No REX.** Registers 0 to 7 in every class, AH to BH but not SPL to DIL, no
  64-bit operands, and VEX's and EVEX's register bits always set, but CR8, which
  is AMD's `lock mov` of CR0 there (AltMovCr8: an `F0` and ModRM.reg 0, so `mov
  eax, cr8` is `F0 0F 20 C0`), as GNU as writes it (XED doesn't know it). A VEX
  or EVEX prefix in 16-bit mode is 16-bit protected mode's (real mode and
  virtual-8086 mode take `C4`, `C5` and `62` for `les`, `lds` and `bound`); the
  encoder encodes what it is given.
- **Segment overrides.** An FS or GS form, as in 64-bit mode; and as segments
  are more than FS and GS in these modes, an override of any of the six in the
  decorations' bits 3 to 5 (`BUSTER_X86_SEGMENT_OVERRIDE`, which a memory
  operand leaves free, as rounding is for registers): on a memory operand, a
  moffs, or an address the instruction makes itself, a string instruction's
  source or `xlat`'s (`es lodsw` is `26 AD`), but not on a `notrack` form, whose
  `3E` would come after it. The encoder writes what the record asks for, a DS
  override on DS's addresses too, as GNU as does for a prefix (it drops an
  override in the operand that is the address's default segment).
- **Prefix order.** GNU as's: FWAIT's `9B`, the segment override, `notrack`'s
  `3E`, `67`, the mode's `66`, then the form's own (`F2` or `F3`, `F0`, and a
  branch hint's `2E` or `3E`, which GNU as puts after the `66`: `data32 cs jne`
  is `66 2E 0F 85` in 16-bit mode, but `notrack call eax` is `3E 66 FF D0`).

Validation takes the forms that exist in the mode: 6,187 in 32-bit mode (1,853
legacy, 950 VEX and 3,384 EVEX) and 6,186 in 16-bit mode, which has no `push` of
an imm32 (`pushd`'s row has it). The 2,164 rows of 64-bit mode alone are REX.W's
and REX2's, those with a 64-bit register, immediate or address or a tile, APX's
EVEX forms, `udb` (`salc` in these modes), and the instructions GNU as takes in
64-bit mode alone: FSGSBASE, `swapgs`, LKGS, FRED, MSRLIST, UINTR, TDX's
`seamcall`, `seamret` and `seamops`, `pbndkb`, SEV-SNP's but `pvalidate`, AMX's
tile configuration, `vpcmpestriq` and `vpcmpestrmq`, MOVRS and AVX10.2's
`vmovrs`, CMPccXADD, PREFETCHIT0 and PREFETCHIT1, the descriptor table
registers' 10-byte operands and the 64-bit flag pushes. It takes registers 0 to
7 (and CR8; an /is4 register too, as the processor ignores the immediate's bit 7
there), 16-bit addresses by their rules, with displacements in [-32768,
65535], no RIP, and instructions within x86's 15 bytes, which a `66` and a `67`
can go past where REX was: in 16-bit mode, HLE's locked arithmetic by an imm32
with an FS or GS override and a 32-bit address with a SIB byte and a disp32
would be 16 bytes long.

The instructions 64-bit mode doesn't have are 111 rows at the end of the table
(`BUSTER_X86_FORMS_LEGACY_MODES`, from `BUSTER_X86_FORM_LEGACY_MODES` on), which
the 64-bit entry points' validation rejects: the
BCD adjustments (`aaa`, `aas`, `daa`, `das`, `aam` and `aad`), `salc`, `into`,
`bound`, `arpl`, `lds` and `les`, `pusha` and `popa` at both sizes, pushes and
pops of ES, CS, SS and DS (and 32-bit ones of FS and GS), far calls and jumps to
a pointer in the immediate (a ptr16:16, or a ptr16:32's offset and its selector
in the displacement), `inc` and `dec` at `40` to `4F`, the moves to and from the
386's and 486's test registers (TR0 to TR7 at `0F 24` and `0F 26`, #UD since the
Pentium: a debug register operand with `BUSTER_X86_OPERAND_TEST`, as the
operand's class field has no room for a class of their own), the descriptor
table registers' 6-byte operands at both sizes (`lgdtw`'s base is 24-bit), VIA's
`montmul` (which XED takes with 16- and 32-bit addresses alone), and the 32-bit
forms of what 64-bit mode does with 64-bit operands: `push` and `pop`, near
branches through a register or memory (with `notrack` and `bnd` too), `pushfd`
and `popfd`, `pushd`, the moves to and from control and debug registers,
`vmread`, `vmwrite`, `invept`, `invvpid`, `invpcid`, MPX's bound checks and
`rdpid`, and the instructions with an address in a register (`umonitor`,
`movdir64b`, `enqcmd` and `enqcmds`); and near branches through a 16-bit
register or memory, explicit sizes of `ret`, `leave` and `enter`, and the moffs
moves. A 16-bit address's displacement is a 16-bit field, whose distance
`x86_fixup_distance_address16` gives; a rel16 and an imm16 are fields of their
own, and a far pointer's offset is the immediate's start, which the selector
follows.

The 64-bit entry points are unchanged: their random streams, which leave the
new rows out, give the same three digests as before them.

`x86_encode_mode32` and `x86_encode_mode16` run `x86_encode`'s loop
(`x86_encode_16_mode32` and `x86_encode_16_mode16`, and the benchmark's
`x86_encode_64_mode32` and `x86_encode_64_mode16`) on templates of their own,
which `x86_encoder_initialize_legacy` builds at the 64-bit table's indices (FS
and GS copies, and the fixup copies): 3.14 MB more for each mode. A template
has the mode's prefixes in the scalar encoder's order: FWAIT's `9B`, a segment
override (an FS or GS form's, as a static byte, or in a form that takes one, a
position for the decorations'), `notrack`'s `3E`, a `67` (a moffs form's of
the other size, as a static byte, or in a form with a memory operand or an
address it makes itself, a position for `BUSTER_X86_MODE_ADDRESS_SIZE`'s, with
the layout and presence bit 23 of `x86_encode_address32`'s), the mode's `66`
and the form's own prefixes; then no REX, but in a move to or from a control
register, REX's position for CR8's `F0`: its template byte is `F4`, which
REX.R's 4 makes `F0`, and REX.R makes it present.

These modes have no REX, so REX's canonical byte and presence bit (10 and 10)
are the segment override's: an override position's layout is REX's
(`BUSTER_X86_LAYOUT_OVERRIDE`), and `compute_fields` puts the override's byte in
that canonical byte, a `vpshufb` of a 16-byte table by the decorations' bits 3
to 5 (`26`, `2E`, `36`, `3E`, `64` and `65` for ES to GS), and sets the presence
bit where those bits aren't zero. The template says which instructions have one:
its checks allow those bits only with an override position, so the loop takes
them where the check of bit 3 is clear (a form with embedded rounding, whose rm
is a register, takes no override, and allows bit 5, the rounding mode's, but not
bit 3). An EVEX prefix's P2 has the decorations as they are, where an override's
bits would be b's and L'L's: the loop clears them from the prefix.

The mode tables come in two halves, a 32-bit address's and a 16-bit address's,
which `vpermt2d` picks between by `BUSTER_X86_MODE_ADDRESS_SIZE` (the operands'
bit 30, bit 4 of the table index), in the order the mode takes them: three
lookups, as the 64-bit loop's three `vpermd`. A 32-bit address's entries are
64-bit mode's but RIP, which these modes don't have (an absolute address is
ModRM's mod 00 rm 101). A 16-bit address's have no SIB byte and a 16-bit
displacement (presence bits `0x30`), and ModRM.rm comes from the base and the
index rather than from a field: a `vpshufb` of a 16-byte table by the base's low
3 bits and whether there is an index gives BX's 7, BP's 6, SI's 4 and DI's 5
alone, and BX's 6 and BP's 4 with an index, which the index's low 3 bits (SI's 6
or DI's 7) XORed in make 0 to 3, `[bx+si]` to `[bp+di]`. The displacement's size
is its low 16 bits', sign
extended (`[bx+65535]` takes an 8-bit −1), and BP alone needs one, as RBP does,
but `[bp+si]` and `[bp+di]` don't. `BUSTER_X86_MODE_ADDRESS_SIZE`'s `67` is
present by the operands' bit 30, where `x86_encode_address32`'s is by the memory
bit, and counts in the length.

`x86_validate_32` and `x86_validate_16` add the modes' rules to
`x86_validate`'s: a register from 8 to 15 but CR8, SPL to DIL, which need REX,
`BUSTER_X86_MODE_ADDRESS_SIZE` on a register operand of a form without an
address of its own, a 16-bit address in a VSIB, SIB or MPX operand or with a
base or index that can't be, a 16-bit address's displacement outside [−32768,
65535], an override on a register operand of a form whose rm can be memory or
past GS, and an instruction longer than 15 bytes. Until #182 the loop checked
those its encoding can't represent too, about 50 vector operations per group of
16 with `x86_encode`'s (a table bit beside each mode's entries, template flags,
and the 16-bit address table's and the override table's marks).

A template can have 16 positions, one of them the `67`'s (9 in 32-bit mode and
51 in 16-bit mode, with their FS and GS copies), and in 16-bit mode an
instruction can fill them: HLE's locked arithmetic by an imm32 (with the 32-bit
form's `66`, `F2` or `F3` and `F0`) and TBM's `bextr` and LWP's `lwpins` and
`lwpval` with their imm32, with an override and a 32-bit address with a SIB
byte and a disp32, 16 bytes. Such an instruction is invalid, and
`x86_validate_16` rejects it; the loop would write its 16 bytes, which is why it
is out of `x86_encode_mode16`'s contract, with the other invalid ones (its stores
could pass the output's capacity). Until #182, the loop instead advanced the
output by at most 15 bytes per instruction in these modes, so that no store
passed the capacity (60 per quad of a whole group, and in the last, padded,
group each instruction's own), and before that it dropped such an
instruction's `67`, which put the instruction's length in front of every
presence bit (see "Zen 5's costs"). In 32-bit mode none can fill them: a `67`
makes its address 16-bit, without a SIB byte and with at most 2 displacement
bytes.

The mode is a constant parameter of the loop, as with 32-bit addresses and APX,
so the other loops' code stays the same, with two precautions. The modes' work
comes after the rest of `compute_fields`, whose first uses of the templates'
checks and the records' fourth dwords as dwords have to come first: otherwise
clang placed those conversions differently in every loop. And `encode_group`
takes the modes' presence bits from a field of their own
(`X86Fields.legacy_presence`): selecting them in `compute_fields` changed how
clang 23 reassociated `x86_encode_64_address32`'s presence bits in `main.c`'s
translation unit (an `or` of the `67`'s bit moved, and two lost `disjoint`).
All twelve 64-bit and 32-bit-address loops disassemble as before (clang 23 on
the 9700X).

On the 9700X (clang 23, pinned to one CPU, medians of five runs of 21 passes,
which time the mode's scalar loop between the passes), `x86_encode_mode32`'s
loop, `x86_encode_16_mode32`, encodes a mix like 32-bit compiler output (stack
loads and stores, pushes and pops, calls, branches and arithmetic, x87 and
SSE2: `--mode32`) at 0.683 ns per instruction (3.78 cycles), and
`x86_encode_16_mode16` a mix like real-mode code (16-bit operands and addresses,
`[bp+disp]` for the stack and BX, SI and DI for pointers, absolute globals,
string instructions, segment registers and ES overrides, interrupts, and some
`66` and `67`: `--mode16`) at 0.678 ns (3.75 cycles), 22× and 24× their scalar
encoders, and 16% faster than before #182 (`x86_encode_64_mode32` and
`x86_encode_64_mode16` with its checks, 0.816 and 0.812):

| Stream | 32-bit, 16 | 64 | before | scalar | 16-bit, 16 | 64 | before | scalar |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16,384 mixed | **0.683** | 0.717 | 0.816 | 14.79 | **0.678** | 0.728 | 0.812 | 16.30 |
| 16,384, with offsets | 0.743 | 0.748 | 0.857 | 14.65 | — | — | — | — |
| 16,384, with fixup ends | 0.749 | 0.755 | 0.860 | 14.76 | 0.758 | 0.752 | 0.858 | 16.27 |
| calls of 64, with fixup ends | 0.795 | 0.797 | 0.911 | 14.85 | — | — | — | — |
| 16,384 uniform | 0.820 | 0.855 | 0.911 | 23.65 | 0.814 | 0.845 | 0.912 | 21.82 |

After 60 ms of the scalar loop, `x86_encode_16_mode32` and
`x86_encode_16_mode16` take 0.682 and 0.688 ns (main's 64-per-iteration loops
0.814 each). In the same runs, `x86_encode` took 0.553 ns on its compiler mix,
so the modes' loops cost 24% and 23% more on their mixes, whose instructions
are shorter (3.22 and 2.58 bytes against 3.97); the loop has no branches but its
own, so it does the same work on any stream. They run 2.8 more instructions per
instruction (15.4 against 12.6), for the modes' addresses, overrides and `67`,
and the registers they take. The benchmark's scalar loops are one function per
mode around a flattened helper, as clang stopped inlining `encode_one` into them
once it had three: the 64-bit one measured 1.6% faster than before (12.50 ns on
the compiler mix against 12.71, three runs each). The 16-bit one measures 9%
slower than before #182 (16.25 ns against 14.86, five runs of it alone), from
the 64-byte template entries: its validation reads each memory operand's form's
template flags (`x86_form_lacks_room`), and the tree before them measured 0.7%
slower, the one with them 8.7%. The 64-bit and 32-bit ones measure within 2%.

### What Niles Salter's tokenizer suggested

Accelerated-Zig-Parser's guidelines translate to an encoder like this:

- *Process many things at once:* 16 instructions per Phase II operation and 4
  per assembly operation, with no per-instruction loop.
- *Fewer unpredictable branches:* the only branches are the loop's. Cases are
  table lookups and masks; padding is an empty form rather than a branch.
- *Linear traversal of compact memory:* 16-byte records, 32-byte templates in
  64-byte entries (a cache line per form, of which a stream touches those of its
  forms), output
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
to 42%, and it is slower. Shortening that chain was #182's work (see "The
group's chain, and validation out of the loop" below).

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
  a compare into a mask register and a `kmovd` per group (until #182 took the
  checks out of the loop), and each quad's presence bits copied with one
  `vpshufd`, rather than an unpack and a `vpshufd`.

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
Merging #35's changes, which made the loop with offsets 2.6% faster, left fixup
ends 4% slower than main's offsets loop (15 interleaved rounds: 0.725 ns per
instruction against 0.696), with dispatch stalled on the FP scheduler 26% of
cycles against 23%. Two changes brought them level: the running sum and the
stores moved after Phase III, so that their operations wait for the lengths
behind Phase III's rather than ahead of them in the FP scheduler, and the
group's offset is broadcast from memory, a load, where a broadcast from a
general register takes FP1 or FP2 too. In nine rounds, the broadcast alone took
fixup ends from 0.721 ns to 0.702 (and both to 0.697), and the position alone
took offsets from 0.699 to 0.685.

#### The group's chain, and validation out of the loop (#182)

uops.info's Zen 5 data puts more of the chain at its end than the latencies
above show: `vpcompressb` takes 9 cycles from its mask (4 from its data) and
`vpshufbitqmb` 6, so from a group's presence bits to its stores there are about
17 cycles, which nothing in this design shortens. A model of one group's
dataflow, from the loop's disassembly with the latencies above (2 cycles for the
simple operations, 8 for a load) and uops.info's for the rest, put main's chain
at 56 cycles to its first store, with four paths within four cycles of each
other into a presence of five serial `vpternlogd`: the template's flags (about
23 cycles in: the form's load, its shift, the template's load and insert,
`vshufi64x2` and two unpacks), the addressing mode's `vpermd` through the
register fields to VEX2's test, the displacement's compressibility, and REX's
test of SPL to DIL. The 16- and 32-bit modes' chain was 79 cycles: their test of
an instruction longer than 15 bytes put its length (a popcount and three
operations) in front of every presence bit. AMD's guide (58455) has the queues
these stall in: a 96-entry queue in front of the vector renamer, in order, and
three schedulers of 38 behind it, one for FP0 and FP2, one for FP1 and FP3 and
one for the stores' FP4 and FP5, so that FP1's and FP2's µops (the shuffles,
`vpshufbitqmb`, the compress, compares into mask registers) have one pipe in
each; `fp_sch_rsrc_stall` counts that queue full.

What shortened the chain, each measured against the tree before it and against
main (medians of 5 to 11 interleaved rounds of `--only` runs):

- **The presence bits each one operation from the flags.** REX's and P0's are a
  `vpminud` with 0x400 and 0x2000 of a value that is zero only without them
  (REX's reasons, all at bit 16 or above, and VEX2's test); the displacement's
  are one select by fits8 between values worked out before the template
  arrives, as are ModRM.mod and the quotient; and they're ORed as a tree, the
  latest last. The template's scale is log2(N) plus one, so that the
  compressibility needs no rotation by one before the template's. The chain was
  50 cycles in the model, and the 16-per-iteration loop 15% faster than main's,
  but the 64-per-iteration loop 1.5% slower: the values worked out ahead took
  registers, and clang spilled template vectors (20 stores per group instead of
  6).
- **RBP's, R13's, RSP's and R12's tests in one `vpshufb`** of a 16-byte table by
  the base's low nibble: 4 operations and 2 constants fewer, and 2.7% faster
  than main on the compiler mix.
- **64-byte template entries** (see "Templates and the canonical slot"): a quad's
  bytes, whose flags start Phase II, are a blend (any of the four pipes) rather
  than a `vshufi64x2` (4 cycles, FP1 and FP2), and only the layouts, which Phase
  III takes much later, a `vpermt2q`. 48 cycles in the model, and 4.5% to 5.6%
  faster than main.
- **APX's payload's R4, X4 and B4 from one `vpmulld`** (one µop on FP0 or FP1,
  2 or 3 cycles) of the registers' bits 4 by 2^26 + 2^9 + 2^16, whose other
  products land in bytes the payload doesn't use: 4 operations instead of 8.

In the benchmark's full runs those made `x86_encode` 3.9% faster than main
(3.23 cycles per instruction of the compiler mix against 3.36). But the 16- and
32-bit modes' full runs, which time the scalar loop between the vector loop's
passes, had their loops only 2% faster than main, and 1% to 3% slower on two
rows, where `--only` runs had them 9% faster. The cause is Zen 5's per-core
power management, not the scalar loop's code. [#199's
report](https://github.com/buster14a/buster/issues/199#issuecomment-6297314071)
has the controls, and says which parts are measured (the clock, RAPL energy,
temperature, the counters) and which inferred from them (the boost, the cap,
umask 0x08 as its counter):

- **The boost.** While the core runs anything light (the scalar loop at 13 W,
  but also a spin, a sleep or the vectorized lexer), its clock climbs from
  about 5.31 to 5.52 GHz over 30 to 300 ms.
- **The cap.** At that clock the vector loop would draw more than its core may.
  The core then holds at about 21.5 W while dispatch loses slots, which #199
  reads as a fast power throttle withholding them. That reading is an
  inference: AMD doesn't document what PMCx1A0's umask 0x08 (the benchmark's
  "throttle") counts. But it is the one dispatch counter that rose in this
  state, from about 0.5 to 6–10 slots per instruction, and its extra slots
  account for the extra cycles (8 slots a cycle), while dispatch stalls on a
  full FP queue fell from 20% to 5% of cycles and every token stall fell too.
  The throttle engages within 10 µs, at once on a core warm from scalar work,
  and 1–3 ms later on one cooled by a spin or a sleep.
- **The release.** It holds until the firmware has stepped the clock down to
  what the loop sustains (5.3 GHz) after 5 to 20 ms.
- **The cost.** Under the cap a loop's speed follows its energy per
  instruction, and 512-bit stores and loads cost the most (a zmm store about
  six `vpaddd zmm`, a spill about ten). So each extra vector store per group
  cost about 0.8 cycles: main's loop has 6, 2 of them spills; those changes had
  13, with 9 spills; the first had 20. The loop's spilled vector of invalid
  lanes, stored and reloaded back to back at the end of each group, cost more
  again.
- **The scale.** After 300 ms of the scalar loop, the 64-bit loop with those
  changes took 3.84 cycles per instruction where it takes 3.20 back to back.
  From a settled clock, 1 to 30 ms of scalar code left it unchanged.

Every scalar phase of the compiler triggers the same state (the object writer,
import's exact path, the scalar lexer, the dataflow solver's scalar twin). A
compiler runs its encoder in short bursts after them, so the throttled state is
the one that counts there. The benchmark measures it with `--after-scalar MS`
(see "Both states" in "Evaluation").

What took the registers was the loop's validation: about 30 operations per
group (50 in 16- and 32-bit modes), whose values pushed Phase I's template
vectors onto the stack, and the vector of invalid lanes. The code generator's
instructions are valid by construction, and `x86_validate` checks them for the
tests and for debugging, so the loop doesn't check them any more (see "VEX and
EVEX"). That took the 64-bit loop from 248 instructions per group to 219, with
7 stores (3 spills) instead of 13, and the modes' from 321 to 268, with the
clamp of their advance. REX's and VEX2's tests then take REX.RXB from the
register fields before the addressing mode's mask: the instruction's zeros in
the fields its mode doesn't use make them the same, 2 cycles sooner than the
mode table's `vpermd`, and clang allocated better (220 instructions, 6 stores,
2 spills). The 64-bit loop's chain is 48 cycles in the model, the template's
path again, and its vector µops take 33 cycles of the four pipes per group.

The same state decides the width. After 60 ms of the scalar loop, 16 per
iteration is 1% to 6% faster than 64 on every stream (0.557 ns per instruction
of the compiler mix against 0.566, and 0.605 against 0.643 with fixup ends), and
APX's and the 16- and 32-bit modes' loops 6% to 8% faster at 16; in runs of the
vector loops alone, 64 is up to 7% faster on some streams (the uniform mix, the
modes' mixes in `--only` runs) and 16 up to 10% on others (1,024 instructions,
fixup ends, calls of 64). Clang folds 16's constants into memory operands (12.6
instructions per encoded instruction against 13.9), so it keeps fewer values and
spills fewer of them. A compiler runs its encoder in bursts after lighter
phases, where the clock is boosted and the throttle caps the loop's power (16
per iteration spends less energy per instruction, so it loses less to the
throttle: 2% in cycles on the compiler mix against 64's 6%; see "Both states"),
so every entry point takes 16 per iteration, and 64's loops stay for the
benchmark and the tests. After the scalar loop, `x86_encode` takes 0.557 ns per
instruction of the compiler mix against main's 0.620, 0.605 against 0.743 with
fixup ends and 0.711 against 0.772 on the uniform mix; APX's loop 0.646 against
0.781, and the 16- and 32-bit modes' 0.688 and 0.682 against 0.814 (see
"Evaluation").

What didn't, against the best tree at the time:

- **16 per iteration as `x86_encode`'s width with the first change alone** (it is
  the width now, with all of them: see above): the fastest on the compiler mix
  then (3.26 cycles per instruction against main's 64's 3.35), but its loop
  folds its constants into memory operands, which stalled dispatch on the load
  queue 17% to 19% of cycles, and it was 4% slower on the 1,024-instruction
  stream and 3% on the uniform mix in the vector loops' own runs.
- **A merge-masked XOR of the template's bytes**, without the 64-byte mask
  constant: clang emits a whole XOR and a byte-masked blend, two operations per
  quad.
- **VEX2's template flag at REX.B's bit**, so that VEX2's test takes the flags
  unshifted, and **REX.RXB from the fields and the mode's mask**, an operation
  before the register fields: a cycle or two shorter in the model, but clang
  allocated registers worse (260 instructions and 19 moves per group) and
  neither was faster.
- **An empty `volatile` asm statement** pinning the immediate's unpack or the
  layouts' permute early in the group: fewer spills (238 instructions and 7
  stores per group with the second), and no faster: 1% to 3% either way.
- **In 16- and 32-bit modes, a 16-bit address's flag from the operands** (the
  memory and address size bits, sooner than the mode table's bit 21): with the
  checks, and both magnitudes worked out and the select before `lzcnt`, or just
  their override's and `67`'s presence bits in the presence's tree, 2% to 4%
  slower than the clamp alone; without the checks, a chain 4 cycles shorter to
  the first store and no faster (−0.3% to +2.5%).
- **REX2's payload in REX's canonical byte**, with `D5` and map 1's `0F` as
  static positions present with and without REX2: two positions more, which put
  the opcode of HLE's locked map 1 forms with a segment override past the
  template's 8 static positions, so those forms would lose REX2.
- **The invalid lanes in a mask register** (a `vptestmd` and a `korw` per group),
  before the checks came out: no store and reload, but 1.6% faster than the
  vector only after the scalar loop, and 2% slower in 32-bit mode.
- **The marked instructions' mask computed with the records**, so that the loop
  with fixup ends keeps a mask register rather than the records' fourth dwords
  to the end of the group: 3 fewer stack operands per group, and no faster in
  the 64-bit loop, 10% slower in APX's.

## Evaluation

`./build/compiler benchmark-x86-encoder` on the 9700X, `taskset -c 2`,
nanoseconds per encoded instruction (the median of five runs, each the median
of 21 passes over at least 4 million instructions), with `x86_encode` before
#182's changes (main at 4622693, 64 per iteration, in the same interleaved runs)
for comparison:

| Stream | 4 | 8 | **16** | 32 | 64 | scalar | before |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 instructions (L1) | 1.24 | 0.88 | **0.52** | 0.54 | 0.58 | 6.73* | 0.60 |
| 16,384 mixed (L2) | 1.24 | 0.90 | **0.55** | 0.56 | 0.55 | 12.98 | 0.62 |
| 16,384 vector | 1.24 | 0.91 | **0.56** | 0.55 | 0.55 | 14.25 | 0.62 |
| 16,384 uniform | 1.25 | 0.95 | **0.62** | 0.63 | 0.61 | 22.83 | 0.66 |
| 16,384, with offsets | 1.45 | 1.04 | **0.60** | 0.60 | 0.60 | 12.92 | 0.67 |
| 16,384, with fixup ends | 1.51 | 1.06 | **0.60** | 0.62 | 0.63 | 12.99 | 0.71 |
| calls of 16 | 1.30 | 0.99 | **0.61** | 0.61 | 0.62 | 13.61 | 0.72 |
| calls of 16, with fixup ends | 1.58 | 1.04 | **0.69** | 0.69 | 0.69 | 13.63 | 0.80 |
| calls of 64 | 1.25 | 0.93 | **0.56** | 0.59 | 0.58 | 13.09 | 0.66 |
| calls of 64, with fixup ends | 1.53 | 1.05 | **0.62** | 0.65 | 0.65 | 13.22 | 0.73 |
| calls of 256 | 1.24 | 0.88 | **0.56** | 0.56 | 0.56 | 13.05 | 0.63 |
| calls of 256, with offsets | 1.46 | 1.03 | **0.61** | 0.62 | 0.60 | 12.97 | 0.70 |
| 262,144 mixed | 1.25 | 0.92 | **0.56** | 0.56 | 0.56 | 14.68 | 0.63 |

The column is instructions per iteration of the outer loop. The mixed streams
have only legacy forms, and the uniform one is a different sample in each
build (its random forms depend on how many there are). In cycles, 16 per
iteration takes 3.02 per instruction of the compiler mix and 64 per iteration
2.98, against main's 3.36; before #182, 3.56 for the loop before the changes
above, which had none of the checks and forms added since: the changes measured
3.36 when they were made, and the rm kind check and the forms of #18 and #29
brought that to 3.43, which segment overrides didn't change (the same code,
within the runs' noise), and #35's checks made it 3.37. The scalar encoder,
which #182's changes don't touch, has got slower as forms were added (10.60 ns
on the compiler mix when this table was first made).
*The scalar encoder's 6.73 ns on 1,024 instructions is flattering: the same
short stream repeated thousands of times lets the branch predictor learn it;
and that row varies by about 10% between runs (where the buffers land), the
others by about 3%, and a build by up to 4% when other code moves (the scalar
encoder's, which these changes don't touch, did).

After 60 ms of the scalar loop between passes (an experiment copy of the
benchmark, five runs), `x86_encode` took 0.557 ns on the compiler mix, 0.605
with fixup ends and 0.711 on the uniform mix, against main's 0.620, 0.743 and
0.772 then: the state a compiler runs its encoder in (see "Zen 5's costs"). The
benchmark now measures that state itself (see "Both states" below).

Below 16, Phase II's 16 lanes are partly empty (4 and 8 per iteration use a
quarter and a half of them), so the fixed cost per group dominates. 16, 32 and
64 run the same one-group loop with different code (see "Zen 5's costs"): 16's
folds its constants into memory operands (12.6 instructions per encoded
instruction against 64's 13.9), and keeps fewer values in registers. In these
full runs, whose loops run partly throttled after the scalar loop's passes (see
"Both states"), 64's is up to 3% faster on the vector and uniform mixes and
16's up to 10% on the L1 row, with fixup ends and in calls of 64; after the
scalar loop, 16's is faster on every stream, so `x86_encode` and the other
entry points use 16.

Every row is faster than before #182, by 6% (the uniform mix) to 15% (fixup
ends, and calls of 16 and of 64), and 10% on the compiler mix (0.553 ns against
0.617).

The fixup ends' two designs and two stores, at 64 per iteration, where the
alternatives are (`x86_encode_16`'s neither, offsets and fixup ends are within
0.02 ns of these: see the table above), nanoseconds per instruction (medians of
five runs; `--fixups` marks the calls and the RIP-relative operands, 6% of the
compiler mix, 2% of the vector mix and 12% of the uniform one):

| Stream | neither | offsets | **fixup ends** | compressed to memory | offsets, then picked |
| --- | ---: | ---: | ---: | ---: | ---: |
| 16,384 mixed | 0.55 | 0.60 | **0.63** | 0.63 | 0.61 |
| 16,384 vector | 0.55 | 0.60 | **0.62** | 0.62 | 0.61 |
| 16,384 uniform | 0.61 | 0.65 | **0.69** | 0.69 | 0.73 |
| 262,144 mixed | 0.56 | 0.61 | **0.63** | 0.64 | 0.63 |
| calls of 16 | 0.62 | 0.65 | **0.69** | 0.70 | 0.74 |
| calls of 64 | 0.58 | 0.62 | **0.65** | 0.66 | 0.64 |
| calls of 256 | 0.56 | 0.60 | **0.64** | 0.64 | 0.62 |

"Offsets, then picked" is the other design: every offset, then the ends of the
marked instructions picked from the list of them a code generator keeps (the
next instruction's start). It costs about as much as fixup ends on the long
compiler and vector streams (and since #182, 2% to 3% less on them and in calls
of 64 and 256), 6% more on the uniform mix and 7% more in calls of 16, and it
needs the offsets' 4 bytes per instruction and the list. Compressing the ends
straight to memory (`vpcompressd` to memory, 8 µops and a store every 3 cycles
on Zen 5, where Zen 4 takes 72 cycles) is within 2% of the full store either
way, off the loop's critical path; `x86_encode` stores the compressed register
whole, as it does the machine code.

The variants that lost (all at 16 per iteration, 16,384 mixed):

| Variant | ns |
| --- | ---: |
| templates by `vinserti64x4` and form loads (`x86_encode_16`) | 0.55 |
| forms taken from the records' vector registers | 0.65 |
| templates by merge-masked loads, 16 bytes per lane | 0.75 |
| masked stores everywhere (exact output) | 0.57 |
| compressing straight to memory | 0.76 |

On Zen 4 (Ryzen 9 7940HS) everything ZMM costs about twice as much, memory
`vpcompressb` is microcoded (about 170 cycles), and merge-masked ZMM loads are
half-rate: 64 per iteration runs at about 1.2 ns there.

### Both states

`--after-scalar MS` runs the scalar loop (on the stream's first 256
instructions per call, as #182's experiment copy did) for MS ms before every
timed pass of the other implementations, and every row of the benchmark has the
throttle: PMCx1A0's undocumented umask 0x08 (raw event `0x1000008a0`) per
encoded instruction, which #199 reads as the dispatch slots the power cap takes
(see "Zen 5's costs"). `x86_encode_64` and `x86_encode_16` alone (`--only`)
on the compiler mix, after MS ms of the scalar loop before each pass (medians
of five processes, and of two at 10, 30 and 300 ms, measured on 2026-10-20,
each under the 9700X's lock):

| Before each pass | 64: ns | cycles | GHz | throttle | 16: ns | cycles | GHz | throttle |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| nothing (back to back) | 0.562 | 3.01 | 5.36 | 0.7 | 0.566 | 3.05 | 5.40 | 0.6 |
| 10 ms | 0.552 | 3.01 | 5.45 | 2.1 | 0.549 | 3.02 | 5.50 | 2.1 |
| 30 ms | 0.585 | 3.25 | 5.54 | 6.6 | 0.566 | 3.13 | 5.53 | 5.2 |
| 100 ms | 0.577 | 3.20 | 5.54 | 7.0 | 0.560 | 3.11 | 5.54 | 5.2 |
| 300 ms | 0.582 | 3.23 | 5.54 | 6.8 | 0.552 | 3.06 | 5.54 | 5.8 |

Back to back, the clock settles at what each loop sustains, and the throttle
takes under one slot per instruction. After the scalar loop, the clock is at
5.54 GHz and the throttle takes 5 to 7: 64 per iteration loses 6% in cycles and
3% in time, and 16 per iteration 2% in cycles and nothing in time. A pass of
4 million instructions takes 2.3 to 2.5 ms, too short for the clock to come
down, so the boost carries over from pass to pass: 10 ms of scalar code between
them gives part of the state (the throttle counts 2 slots per instruction but
costs no cycles, and the higher clock makes both loops faster), and 30 ms all
of it. From a settled clock, as after a long vector phase, 30 ms of scalar code
barely boosts it and 100 ms gives most of the effect (#199), so use 100 to 300:
100 ms reaches the state whatever the passes before did (`x86_encode_4`'s
passes take 5 ms).

The entry points' loops and 64's alone (`--only`), back to back and after
100 ms of the scalar loop before each pass, in nanoseconds per instruction with
cycles in parentheses (medians of three processes, five on the compiler mix):

| Stream | 16, back to back | 16, after | 64, back to back | 64, after | Throttle after, 16 / 64 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 16,384 mixed | 0.566 (3.05) | 0.560 (3.11) | 0.562 (3.01) | 0.577 (3.20) | 5.2 / 7.0 |
| 16,384 vector | 0.595 (3.21) | 0.579 (3.21) | 0.558 (2.98) | 0.581 (3.22) | 5.6 / 7.8 |
| 16,384 uniform | 0.632 (3.35) | 0.716 (3.97) | 0.658 (3.48) | 0.746 (4.14) | 12.4 / 13.3 |
| with fixup ends | 0.613 (3.31) | 0.617 (3.42) | 0.624 (3.34) | 0.648 (3.59) | 6.0 / 8.0 |
| calls of 16 | 0.614 (3.30) | 0.634 (3.51) | 0.627 (3.37) | 0.647 (3.58) | 7.7 / 6.8 |
| APX's loops, mixed | 0.658 (3.55) | 0.648 (3.59) | 0.672 (3.60) | 0.697 (3.86) | 5.8 / 9.1 |
| 32-bit mode | 0.697 (3.78) | 0.691 (3.82) | 0.665 (3.54) | 0.744 (4.12) | 5.1 / 9.9 |
| 16-bit mode | 0.694 (3.76) | 0.689 (3.82) | 0.677 (3.60) | 0.727 (4.03) | 5.2 / 8.8 |

After the scalar loop, 16 per iteration is faster than 64 on every stream, by
0.3% (the vector mix) to 7% (APX's loops and 32-bit mode), which is why the
entry points use it. Back to back, 64 is up to 6% faster (the vector mix, and
5% in 32-bit mode) and 16 up to 4% (the uniform mix). The uniform mix, whose
many VEX and EVEX forms make the heaviest groups, loses 13% in time at either
width.

Without the option, a full run is in neither state. Each pass of the scalar
loop (54 ms on the compiler mix) boosts the clock, and the vector loops after it
run partly throttled, the more so the nearer they follow it. In five full runs
on the compiler mix, the clock falls from 5.53 GHz in `x86_encode_4`'s passes,
the first after the scalar loop's, to 5.36 GHz in `x86_encode_16_apx`'s, the
last, and the throttle takes 2.8 slots per instruction at 16 per iteration and
2.2 at 64, against 0.6 and 0.7 alone; the table at the top of this section is
of such runs. In the 16- and 32-bit modes' full runs the scalar loop is most of
each pass, and their two loops run at 5.52 to 5.55 GHz, as if after it. The
processes of one binary can differ by more than the states do: one full run of
32-bit mode with `--after-scalar 100` had `x86_encode_16_mode32` at 4.90 cycles
per instruction in all 21 passes, with less throttle than the other runs, and
three `--only` processes didn't reproduce it (3.82), so compare over several
processes.

## Correctness

`./build/compiler test-x86-encoder` (about 1.5 s) checks:

- that every form's template fits, and that building every form's in every mode
  into a template that ends at a `PROT_NONE` page writes nothing past it;
- 761 known encodings from GNU as, on the paths most worth pinning down (VEX2
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
  with REX, the MSR instructions in VEX map 7 with their 32-bit immediates,
  `hreset`'s ModRM byte in its opcode, and AMX's tile configuration,
  `tilerelease` and `tilezero`, tile loads and stores and AMX-TRANSPOSE's pair
  loads through SIB operands with none for the index (after RAX, RBP and R12),
  without a base and as an absolute address, with X and B and with FS and GS,
  dot products, transposes and multiplications of tiles (FP8's and the
  read-shared pair loads in VEX map 5), and AMX-AVX512's forms with an immediate
  (EVEX map 3) and with a general register in EVEX.vvvv (map 2), with R and
  R', and MPX's `bndmk` (with an index but no base, which GNU as keeps, as the
  lower bound is the base), `bndcl` with REX.B after F3, the bound moves, and
  `bndldx` and `bndstx` through MIB operands, 3DNow!'s operation byte after the
  displacement (RIP's too) and with FS, and Xeon Phi's AVX512ER forms with
  broadcasts, opmasks and suppressed exceptions, AVX512PF's prefetching gathers
  and scatters with V' and N = 4, and 4FMAPS's and 4VNNIW's blocks of four with
  V' and N = 16, and the prefixes: HLE's on the locked forms (`64 66 f2 f0` on
  `lock bts`, whose template has all eight of its own bytes), on `xchg` without
  `lock` and on `mov`, `bnd` on branches by a displacement, a register and
  memory (with FS), the branch hints at both widths, `notrack` through memory
  and with `bnd`, and the 16-bit near branches, and APX's EVEX forms of general
  register instructions with registers 0 to 15: 8-bit displacements of N = 1,
  NF and ND, map 4 with `66` for 16 bits, maps 5 and 7, `kmov` at every width,
  `crc32` with SPL, `movbe` between registers, and AMX's SIB operands; and map
  4's legacy arithmetic both ways, with ND, NF and both, by 1, CL and an
  immediate, `imulzu`, `cmovcc`, `setcc` and `setzucc`, and the longest, 15
  bytes; and APX's new instructions: `ccmp` and `ctest` with default flags
  values and every kind of operand, `t` and `f`, `cfcmov`'s three forms,
  `push2` and `pop2` and their `p` forms, and `pushp`, `popp` and `jmpabs` with
  their static `D5`; and the instructions the coverage audit found missing:
  `enterw`, `leavew`, `pushw` by an imm8, `sysexitq`, `sysretd` and `xbegin`'s
  rel16, `ud0` and `ud1` with and without ModRM, `udb` and `int1`, `ffreep`,
  the 8087's and 287's control instructions and the waiting forms with `9B`
  before a segment override and `66`, `enclv`, `vmgexit`, `rmpquery` and
  `rmpread`, Zhaoxin's PadLock and GMI instructions, and AVX512_BMM in map 6),
  with both encoders, and each again with the fixup flag, whose end both must
  report;
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
  RIP with a base, a register past its class's last, VSIB indexes among them, a
  group's register that isn't a multiple of its size (an odd opmask or tile
  pair, a block of four's), vvvv or reg in a form without them, decorations the
  form can't have, unknown mode bits, an index register or a scale without an
  index, a memory operand in a form without one, a register in a form that takes
  only memory, a segment override without a memory operand: ones the encoding
  can't represent) are found by `x86_validate`, and rejected by the scalar
  encoder, which validates first (`x86_encode` doesn't check them), and that
  immediates, rel8s and rel16s
  just outside their fields' ranges (above and below, in an imm8, imm16, ENTER's,
  rel8 or rel16 form, each kind as often), /is4 registers past the last (or past
  7 in 16- and 32-bit modes) or with bits 3 to 0 set (or below 0 where those are
  a selector), control registers
  that don't exist (CR1, CR5 to CR7 and CR9 to CR15), segment registers past GS,
  MOV to CS, complex multiplications into one of their sources, AMX products
  with a tile twice, `push2` and `pop2` with RSP and `pop2` with one register
  twice, RIP-relative operands of MPX's `bndmk` and MIB operands,
  MIB operands with a scaled index, 3DNow! forms' immediates other than their
  operation byte, `notrack` forms with a segment override, VSIB operands without
  an index (a base alone, an absolute address or RIP), SIB operands without a
  SIB byte (a base alone or RIP) or with none and a scale, EVEX gathers and
  scatters without an opmask, and gathers whose destination, index and mask
  (AVX2's) aren't different registers, which the encoding can represent, are
  too;
- then all but the known fixups (which are 64-bit) again with 32-bit addresses,
  which is all `--address32` runs: both encoders on 34 known encodings from GNU
  as (the `67` after a segment override and `3E` and before `66`, `F2`, `F0`,
  REX, VEX and EVEX, and after FWAIT's `9B`, RBP, R13, RSP and R12 bases, an
  index scaled by 1 without a base, an absolute address and EIP, gathers by XMM2
  and ZMM20, AMX's SIB operands with and without an index, 3DNow!, far and
  `notrack` jumps through memory, `movdir64b`, `lock cmpxchg16b` with GS and
  REX, no `67` without a memory operand, and the longest, 15 bytes), a digest of
  their own, `x86_encode_16_address32` and `x86_encode_64_address32` against the
  scalar encoder on the random streams and at page edges, and the invalid
  operands with two more breaks: an MPX memory operand and a memory operand in a
  form without room for the `67` (as every mode's validation rejects its own
  forms without room);
- then all but the known fixups again for APX, which is all `--apx` runs: both
  encoders on 153 known encodings from GNU as (REX2 in maps 0 and 1 with
  registers 16 to 31 in reg, rm, the base and the index, R20 and R28 as bases
  with a SIB byte and R21 and R29 with an 8-bit zero, R20 as an index, W, `66`,
  `F2`, `F3`, `F0`, `3E` and FS before `D5`, the `+r` forms, SIL with REX2, x87,
  SSE, 3DNow!, MPX, CR8 and the other system instructions with registers 16 to
  31, the longest, 15 bytes; EVEX with B4, X4 and both, R20 as a base and as an
  index, R', V', X, B, B4 and X4 at once, general registers in rm, reg and vvvv,
  VSIB with B4 and V', and FS before `62`; APX's EVEX forms of general register
  instructions with R', V', B4 and X4 in BMI, `kmov`, ADX's ND forms, NF forms,
  map 4's and map 7's, and AMX's SIB operands with X4, R20 as an index and R20
  as a base before none; map 4's legacy arithmetic with registers 16 to 31 in
  every field; APX's new instructions with them, B4 and X4 beside a default
  flags value among them, and `pushp`, `popp` and `jmpabs`, with REX2 for
  registers 0 to 15 too; and REX, VEX, EVEX, map 2 and the XSAVE family
  without them, as `x86_encode` encodes them), 31 known invalid instructions
  that `x86_validate_apx` and the scalar encoder must reject, as GNU as does
  (the XSAVE family with a
  base or index from 16 to 31, a high byte register with REX2 or in an EVEX
  form, XMM16 to XMM31 in SSE's forms, registers 16 to 31 in VEX forms without
  an EVEX one, and in the VEX and legacy map 2 forms of instructions with an APX
  EVEX one, `setzucc` on memory, a default flags value past 15, `ctest` with an
  opmask and `push2` on memory), a digest of their own, `x86_encode_16_apx`
  and `x86_encode_64_apx` against the scalar encoder on the random streams
  (half the forms with REX2
  and a quarter EVEX ones, whose general registers, bases and general indexes
  are 16 to 31 half the time) and at page edges, and the invalid operands with
  three more breaks: a high byte register with REX2, a general register, base
  or index from 16 to 31 in a form without REX2 or EVEX, and a vector register
  rm from 16 to 31 in a form with REX2;
- then in 32-bit mode and in 16-bit mode, which is all `--mode32` and `--mode16`
  run: both encoders on 159 and 150 known encodings from GNU as (both operand
  and address sizes, ModRM's eight
  16-bit addresses and its absolute one, 16-bit displacements modulo 2^16,
  `[bp]`'s zero, 32-bit absolute addresses without a SIB byte, high byte
  registers, pushes and pops, branches by a rel16 and a rel32 with hints and
  `bnd`, near and far branches through memory, the addresses instructions make
  themselves with a `67`, `jcxz` and `jecxz`, control registers (CR8 with AMD's
  `F0` too), debug, test and segment registers, VEX and EVEX with a 16-bit
  address's disp8*N, a gather with a `67`, the x87 environment at both sizes,
  the forms with fixed sizes, HLE, the rows of these modes alone, and segment
  overrides of every segment in the decorations), 13 and 16 known fixups (rel8s,
  rel16s and rel32s, 16- and 32-bit displacements and immediates, far pointers'
  offsets and moffs), a digest of each, the modes' loops at 16 and 64 per
  iteration (`x86_encode_16_mode32` and the rest) against the scalar encoders
  on the random streams
  (of whose 300,000 instructions 40,300 and 116,400 have a 16-bit address,
  17,500 an override in the decorations, 10,500 of them in EVEX forms, 1,100 to
  1,200 the other address size without a memory operand, and 13 to 19 CR8) and
  at page edges, 4,096 instructions longer than 15 bytes of each of three kinds
  (in 16-bit mode, the longest of the 17 forms that have them, HLE's locked
  arithmetic by an imm32, `bextr`, `lwpins` and `lwpval`: with an FS form, with
  an override in the decorations, and with REX's bits instead, an index past 7,
  which validation and the scalar encoder must reject), and
  the invalid operands, with the 64-bit breaks that apply and five more: a
  register that would need REX (8 to 15 but CR8, or SPL to DIL), a memory
  operand the address size can't have (RIP, a VSIB, SIB or MPX operand with a
  16-bit address, or a 16-bit address with another base or index, an index
  alone, a scale or a displacement past 16 bits),
  a form that doesn't exist in the mode (which the 64-bit entry points get too,
  with the rows of these modes), an instruction longer than 15 bytes, and a
  segment override where the instruction can't take one, or past GS.

`--print-assembly` and `--compare-object` cross-check the scalar encoder against
GNU as for any seed (AGENTS.md has the commands): 1.7 million random
instructions over all forms (seeds 1, 7 and 99), 0 differences. The streams have
every family in proportion to its forms: of 200,000 instructions, 104,700 EVEX
(49,800 with an opmask, 22,000 zeroing, 33,800 broadcasts, 9,800 rounding or
suppressed exceptions, 73,100 with registers 16–31; 14,000 in the AVX-512F
forms of #23, 24,900 in the AVX512VL ones of #24, 14,600 in the AVX512BW, DQ and
CD ones of #25, 5,700 in the other extensions' of #26, 32,400 in the
AVX512-FP16 and AVX10.2 ones of #33, 1,500 gathers and scatters of #35, 400 in
AMX-AVX512's forms of #37 and 1,500 in Xeon Phi's of #38), 6,200 VEX2, 22,800
VEX3 (2,900 of them FMA3, 800 in the opmask instructions of #27, 2,300 in the
VEX extensions of #28, 2,100 in the /is4 forms of #31, 500 AVX2 gathers of #35,
130 MSR instructions in map 7 of #19 and 1,200 in AMX's forms of #37), 3,400 in
the forms of #34 (3,100 of them XOP), 6,100 in the SSE forms of #20, 1,200 in
the AES-NI, SHA, GFNI, Key Locker and PadLock ones of #28, 5,200 in the MMX
forms of #30, 510 with the segment, control and debug registers of #32, 5,400 in
the system instructions of #19, 1,500 in 3DNow!'s forms and 270 in MPX's of #38,
11,700 with the prefixes of #39 (7,800 HLE, 1,300 `bnd`, 1,900 branch hints,
570 16-bit near branches and 60 `notrack` through memory), 4,100 x87, 3,700
with `lock`, 1,800 string instructions and 14,000 with an FS or GS override
(1,500 of them locked, with HLE's, 10,700 vector ones and 2,300 RIP-relative).
A VEX form whose
mnemonic an EVEX form has is printed with `{vex}` (17,300 of the 200,000),
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
printed unsigned, as GNU as rejects a negative one; a SIB operand's none isn't
printed, as GNU as reads `riz` as a symbol in Intel syntax; GNU as keeps
MPX's index scaled by 1 without a base, as `bndmk`'s lower bound is its base;
the printed assembly enables MPX, AMX-TRANSPOSE and AMX-TF32 with `.arch`, as
GNU as's default architecture leaves them out; and a 16-bit near branch is
printed with `{disp32}`, which GNU as takes for its rel16 form, as it does for
a rel32 (a branch's target counts its prefixes too). Eleven deliberately broken
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
395 and 335. So do five of #37's: an `x86_validate` without the SIB operand's
rule (a base alone or RIP), its rule for none with a scale, or the products'
three tiles misses 989, 320 and 358 (and changes the reference digest, as the
random streams then have such instructions), a template without the check of a
tile pair's odd tile misses 978, and an `x86_validate` without it makes the
random streams differ, as `x86_encode` rejects the odd tile. And five of #38's:
an `x86_validate` without the rule against RIP for `bndmk`'s and MIB operands,
without MIB's rule against a scale, or without 3DNow!'s operation byte misses
895, 300 and 936 broken instructions (the first also changes the reference
digest), a template check without a group's low bits misses 931, and a template
with 3DNow!'s operation byte among the opcode's own bytes gets 7 known encodings
wrong and makes the random streams differ. And two of #39's: an `x86_validate`
without the rule against a segment override on a `notrack` form or without
rel16's range misses 875 (and changes the reference digest) and 152. Since #182
the loop checks nothing, so the copies above that broke its checks (the rm
kind's, NO_HIGH_INDEX's and GENERAL_INDEX's, a template's of a group's low bits
and of a tile pair's odd tile) no longer exist; the invalid-operand test still
breaks those operands, which `x86_validate` must find.

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
is 64-bit (but 32 for map 7's MSR index, which is unsigned), and 64 for a
64-bit immediate or address. Over seeds 1, 7 and 99: 198,573 relocations (39,689
PC32, 78 PLT32, 227 PC8, 152,289 32S, 5,743 32 and 547 64), 0 different, on all
7,010 forms whose memory operand or branch target the streams print (of 7,018
that have one: the other 8 are the MOVQ memory forms GNU as encodes otherwise)
and all 226 with an imm32 or a 64-bit immediate, and `x86_encode`'s fixup ends
the same as the scalar encoder's. `test-x86-encoder`
has 29 known fixups from GNU as too, so that the regular tests check
`x86_fixup_distance` and the reported ends without GNU as. Twelve broken copies
fail the tests: the vectorized loop's byte order of the lengths, the byte that
carries the first qword's total, the flags' byte, the multiplier, the compress
mask and the cursor's advance (92 to 200 of each width's 201 random streams
differ), a fixup capacity one short (a page-edge fault), the templates' copy
for marked forms left out (all 28 known fixups and 188 to 195 random streams),
the scalar form's row with the flag left in (a fault), the scalar encoder's end
off by one (all 28 known fixups, and the random streams), and
`x86_fixup_distance` without the immediate's bytes or without a branch's own
width (16 and 4 known fixups wrong, and the GNU as cross-check).

With `--address32`, the same seeds cross-check the 32-bit streams: 1.7 million
instructions, 0 differences, and 197,926 relocations (38,538 PC32, 76 PLT32, 223
PC8, 5,374 32S, 153,200 32 and 515 64), 0 different, on all 6,886 forms whose
memory operand or branch target those streams print (of the others, 8 are MPX's,
whose memory operands GNU as rejects with 32-bit addresses, and 116 map 4's with
an imm32, whose templates have no room for a 67) and all 226 with an imm32 or a
64-bit immediate. A displacement's relocation is R_X86_64_32 there
rather than 32S, but a RIP-relative one's is still PC32, and the immediates'
don't change. The printer writes 32-bit registers and `eip` in the memory
operands, and `addr32` where no general register says the address's size (an
absolute address, or a VSIB operand without a base, which GNU as otherwise
encodes without the `67`), but on `movdir64b`, `enqcmd` and `enqcmds`, which
take the address size from their register (printed as a 32-bit one) and reject
`addr32`. It leaves out an `addr32 mov` between the accumulator and an absolute
address, which GNU as makes `A0` to `A3` (but not with `xrelease`), and keeps an
index scaled by 1 without a base, as GNU as does with 32-bit addresses. Ten
broken copies of the 32-bit parts fail the tests: the scalar encoder without the
`67` (62 of the known encodings' 66 checks wrong, the digest, the random streams
and the page edges), with it after a legacy form's other prefixes (12, the
digest and the random streams), both encoders with it there (12 each and the
digest, which only GNU as's bytes can catch), `compute_fields` without presence
bit 23 (62), or with it on register operands too (4), the length without the
`67` (31), the scalar encoder's `67` on register operands (4 and the digest),
the initializer keeping a form's template without room for the `67` (the digest
and the random streams), and `x86_validate_address32` accepting such a form (820
broken instructions not found) or an MPX memory operand (796).

With `--apx`, the same seeds cross-check APX's streams: 1.7 million
instructions, 315,369 of them with REX2 and 685,500 EVEX ones (276,792 with
registers 16 to 31), 0 differences, and 197,735 relocations (42,791 PC32, 148
PLT32, 485 PC8, 145,363 32S, 7,737 32 and 1,211 64), 0 different. Of those,
121,630 are in map 4's forms of the legacy arithmetic (80,878 of them with
registers 16 to 31, 61,574 ND or ZU and 47,259 NF), as are 183,924 of the
64-bit streams' 1.7 million, 23,649 in APX's EVEX forms of VEX's and maps 2 and
3's general register instructions (16,634 with registers 16 to 31), and 71,510
in APX's new instructions (46,748 with registers 16 to 31): 32,641 `ccmp`,
17,224 `ctest` (47,134 of the two with a default flags value other than 0),
19,649 `cfcmov`, 545 `push2` and `pop2`, and 1,451 `pushp`, `popp` and
`jmpabs`, as are 628 of the 64-bit streams', with their static `D5`. (With the
legacy arithmetic, before
those, the streams had 321,505 instructions with REX2 and 675,582 EVEX ones,
252,921 with registers 16 to 31, 132,974 of them in the legacy arithmetic; with
the EVEX forms of general register instructions alone, they had 31,072
instructions in them, 22,159 with
registers 16 to 31, 3,891 NF, 744 ND and 11,414 in map 4, and 528 of AMX's SIB
operands with an index from 16 to 31; before them, 327,348 instructions
with REX2 and 647,361 EVEX ones, 183,303 with registers 16 to 31, 136,652 of
them with B4 and 66,019 with X4; and with REX2 alone, they had 469,795
instructions with REX2 among three forms in four that can have it:
255,332 in map 0 and 214,463 in map 1, 138,984 with R4, 113,054 with X4 and
322,541 with B4, 142,467 with `lock`, 220,074 with `66`, `F2` or `F3`, and 41,921
with FS or GS.) The printer needs nothing new but the registers' names, `{nf}`,
ND's order of operands and its `{load}` and `{store}`, the rules by which GNU
as picks map 4's forms by an immediate as it picks the legacy ones, and a
default flags value's `{dfv=...}` after the mnemonic, with which GNU as takes no
prefix before it: so `ccmp` and `ctest` go without `{evex}` (they have no other
form), and the streams without the instructions with a default flags value that
would need `{load}` (`ccmp`'s `3A` and `3B` between registers) or `addr32` (an
absolute address); and `cfcmov` goes without `{nf}`, which GNU as doesn't take
for it, as `{store}` picks its store form. GNU as encodes the instructions of
maps 0 and 1 and the EVEX forms with registers 16 to 31 as the encoders do, and
rejects the XSAVE family's. Eighteen broken
copies of APX's parts fail the tests: the scalar encoder keeping map 1's `0F`
after REX2 (46 of the 62 known encodings wrong, the digest and the random
streams), without M0 (46), or with X4 or REX's X from a register rm (58 each);
the loop with map 1's payload without W (22, and the random streams), the
payload without X3 and B3 (16), R20 taken for SIB's none (2), map 0's payload
left out of the length (34) or of the presence bits (68), a high byte register
let through with REX2 (253 broken instructions not found) or rm's bit 4
unchecked (961); templates without `NO_HIGH_RM_REGISTER` (767) or
`NO_HIGH_BASE` (194); REX2 for the XSAVE family (6 of the 15 known invalid
instructions accepted, and the digest) or for maps 2 and 3 (the digest, which
only GNU as's bytes catch); and an `x86_validate_apx` that lets every form's
general registers be 16 to 31 (835, the digest and the random streams), lets a
high byte register through with REX2 (266, the digest and the random streams)
or a vector register be 16 to 31 in a form with REX2 (798), or takes R20 for
RSP as an index (2 known encodings and the digest). Two more change nothing, as
validation leaves no instruction they would encode differently: the scalar
encoder's REX2 outside `x86_encode_apx`'s mode, and its B4 from an absent base,
whose rm is 0. And eleven of EVEX's: the scalar encoder without B4 (32 of the 82
known encodings wrong, the digest and the random streams) or X4 (8), or with a
general register rm's bit 4 in X too (14); the loop with the 64-bit mode table
(16), with a general register rm's bit 4 in X (14), with V' from a general index
(6), without B4 and X4 (36) or with B4 for bases alone (14); templates without
`GENERAL_RM` (14) or with `NO_HIGH_INDEX` (8); and an `x86_validate_apx`
without EVEX's registers 16 to 31 (40 known encodings, the digest and 766
broken instructions not found). And nine of APX's EVEX forms of general register
instructions: templates without NF (8 of the 642 known encodings wrong, and the
random streams), the scalar encoder without it (8, the digest and the random
streams) or both (8 and 4 of APX's 115, and the three digests, which only GNU
as's bytes catch); ND left out of EVEX.b (2 and 2, and the digests); EVEX
without map 4 or map 7 (templates that don't fit); and the loop's X4 from
`GENERAL_INDEX`, as before these forms (6 of APX's known encodings, the random
streams and the page edges), templates without `GENERAL_INDEX_4` (22) or with
it for a VSIB operand too (2). And five of map 4's legacy arithmetic and the
templates without room: map 4 without imm32s (templates that don't fit), a
template without room that takes memory operands (the digest and 6 to 10 random
streams of each width) or validation that takes them (the same), the template of
a form without room with its segment override still (templates that don't fit),
and APX's forms without room for REX2's payload without x86_encode's template
(56 more forms that take no memory operand, and the digest). And nine of APX's
new instructions: templates that invert the default flags value or leave the
source condition out (22 of the 710 known encodings' checks wrong in the
vectorized encoder, and the random streams), the scalar encoder doing either
(22 in it, the digest and the random streams), the condition off by one (22 in
each encoder, 8 of APX's 147 in each and the three digests), `cfcmov`'s store
form without NF (4 in each and the digests), a default flags value that can be
16 to 31 (21 to 44 broken instructions not found in each mode, and one of the
known invalid instructions accepted), and validation that lets `push2` and
`pop2` take RSP (42 to 62, and the digests) or `pop2` one register twice (6 to
12, and the digests). And eight of `pushp`, `popp` and `jmpabs`: in
`x86_encode_apx`'s templates, `D5` from REX's canonical byte, as for REX2 by
registers (4 of APX's 152 known encodings wrong in the vectorized encoder, 16
random streams and 30 page edges), the payload present with REX2 by registers
alone (7) or left out of the static length (5); `REX_ALWAYS` for W alone, which
`jmpabs` doesn't have (2 of the 714 and a known fixup); in the other templates,
the payload with REX's `4` (8) or without the static `D5` (8 and a known fixup);
and the scalar encoder with REX2 for registers 16 to 31 alone (8, a known fixup
and the digest) or with `D5` among the prefixes too (the same). Since #182, the
loop's checks among these (a high byte register with REX2, rm's bit 4,
`NO_HIGH_RM_REGISTER`, `NO_HIGH_BASE`, `NO_HIGH_INDEX`) no longer exist, and
`x86_validate_apx`'s, which the same broken instructions test, stay.

With `--mode32` and `--mode16`, the same seeds cross-check the 16- and 32-bit
streams, which GNU as assembles after `.code32` or `.code16`: 1.2 million
instructions each, 0 differences, and 135,563 and 136,322 relocations (3,374 and
3,230 PC32, 939 and 883 PC16, 211 and 194 PC8, 95,848 and 35,096 32, and 35,191
and 96,919 16), 0 different, and `x86_encode_mode32`'s and
`x86_encode_mode16`'s bytes and fixup ends the same as the scalar encoders'. A
quarter of their memory operands, and of the addresses instructions make
themselves, have the other address size, and an eighth have an FS or GS form and
another eighth an override in the decorations: of 400,000 instructions, about
27,500 and 25,000, with 57,500 to 57,900 16-bit memory operands in 32-bit mode
and 166,700 to 167,000 in 16-bit mode, 7,060 to 7,430 instructions of the rows
of these modes alone (122 to 153 of them moves to and from test registers), and
18 to 31 moves to and from CR8. GNU as's relocations there are
R_X86_64_PC32 for every rel32 (never PLT32), PC16 for a rel16, and 32 or 16 for
an address, an immediate (an imm16 too) or a far pointer's offset (never 32S).
The printer writes 16- and 32-bit address registers, `addr16` or `addr32` where
nothing in the instruction says the size (`jcxz` and `jecxz` do, as address
registers do), `data16` or `data32` where a mnemonic can't (`data32 jne` for a
rel32 in 16-bit mode, `data32 call [bx]`, which GNU as otherwise takes for a far
call through a doubleword, and `lcall` and `ljmp` for far branches through
memory, with the prefix of the other size), `{disp16}` for a rel16 in 16-bit
mode, a `d` on the x87 environment and state forms for their `66` there, a moffs
unsigned, an override in the decorations as a prefix (`es mov ax, [bx]`), which
GNU as keeps on any address, while it drops one in the operand that is the
address's default segment (`ds:[bx]`, `ss:[bp]`), and a test register's move
between `.arch push`, `.arch i386` and `.arch pop`, as GNU as takes the test
registers for the 386 and the 486 alone (`.arch i486` would also tune for the
486, which `.arch pop` keeps, and GNU as then writes a shift by 1 with an imm8).
The streams leave out what GNU as encodes another way: `inc` and `dec` of a
register with `FF` (GNU as takes `40` to `4F`), the accumulator's moves to and
from an absolute address with `88` to `8B` (it takes `A0` to `A3`), and in
16-bit mode a jump or a conditional jump by a rel32 whose target a rel8 reaches,
which GNU as relaxes even with `data32`. GNU as rejects every row the modes
leave out (or takes the text for another instruction: `push rbp` pushes a
symbol's memory in `.code32`), but `bndcl` and `bndcn` through memory, which the
32-bit rows encode. Twenty-nine broken copies of the 16- and 32-bit parts fail
the tests: the `66` in the wrong mode or kept by a 16-bit form (72 and 82 of the
known encodings wrong), `[si]` and `[di]` swapped (12), displacements not modulo
2^16 (2), `[bp]` without its zero (2 and 6), a moffs without its `67` (6 and 4),
a 32-bit absolute address with a SIB byte (2), a rel32 or a pseudo-descriptor
not 32- and 16-bit (4 and 6), far pointers not paired (4), MOVNTI or `ret`
sized, `udb` in these modes, the `67` after the `66` (6 and 4) or a branch hint
before it (the digest, which only GNU as's bytes catch), `umonitor` without its
address size (2), CR8 without its `F0`, with REX.R too or rejected (4), the test
registers' moves at other opcodes (2), the moves of control, debug and test
registers sized (4 in 16-bit mode), the 16-bit fixup distance (1 and 3 known
fixups), and validation that takes any base in a 16-bit address (105 and 97
broken instructions not found), registers 8 to 15 (640 and 518), displacements
past 16 bits (94 and 102), a ptr16:32's selector past 16 bits (112 and 97) or
instructions past 15 bytes (631 in 16-bit mode).

Fifty broken copies of the modes' loops fail the tests too, most of them the
known encodings, the random streams, the invalid instructions or the
templates' check: for addresses, the lookup's halves swapped, a 16-bit
absolute address as rm 101 or with a disp32, a 32-bit one with a SIB byte, a
16-bit address with a scale, ModRM.rm's table with the pairs wrong or SP or SI
with an index taken, its key without the index, the index ORed in, ModRM.rm
left out or without the base's mask, the index's check off by one, the index
taken from the base's bit, the 16-bit displacement not sign-extended or 32-bit,
and `[bp+si]` with one; for overrides, the byte or the presence bit left out,
the override counted not at all or twice, taken where the checks allow
rounding or everywhere, left in EVEX's P2, CS's byte wrong, no mark past GS,
and the position a static byte; the `67` from the memory bit or not counted;
the modes' presence bits, length or invalid lanes not used; the checks of the
index's bits 3 and 4, mode bit 7, register bit 4, SPL to DIL, the other address
size on a register operand (and the tables' mark or the templates' flag for
it), 16-bit VSIB, SIB and MPX operands (and either flag), an override on a
register operand or past GS, and a 16-bit base that can't be, left out; and
CR8's position left out. And three on the overlong streams: the check of an
instruction longer than 15 bytes left out (398 of their 597 streams accepted,
and the invalid instructions' longest), its `67` not dropped (a fault at a page
end), and VEX's and EVEX's templates with `NO_REX`, under which an invalid
instruction's REX bits light the override position without counting (a fault
at a page end, once the streams had instructions with an index past 7). Two
more came from a review, with checks of their own: `build_template` writing the
positions past a slot's 16 (it faults at a page end on the FS copy of map 4's
`add` by an imm32 with 32-bit addresses, which asks for 17), and validation
taking an /is4 register up to 15 in these modes (51 and 61 broken instructions
not found, once the breaker tried 8 to 15 there). Since #182, the loops'
checks, their invalid lanes and the clamp that kept an overlong instruction's
stores inside the capacity are gone, and with them the mutants above that broke
them and the overlong streams at a page end; `x86_validate_32` and
`x86_validate_16` must reject the same broken instructions and overlong ones.

## Coverage

#17 asks for every x86-64 instruction. The form table was audited against
three lists of 64-bit mode's instructions, with scripts that read them as data
(none of them committed):

- uops.info's `instructions.xml` (2026-03-29), which XED-to-XML makes from XED
  v2025.06.08: 22,276 instruction variants (XED's iforms by operand size,
  opmask, zeroing, broadcast, rounding and APX's NF). Matched by mnemonic
  (condition codes and `rep`'s names folded), encoding (legacy, VEX, XOP or
  EVEX), the operands in Intel order without the fixed implicit registers, and
  the decorations, 22,227 have a form, and 48 more have one whose memory
  operand's size the table writes otherwise (a cache line as a byte, as GNU as
  takes the prefetches and flushes, the XSAVE and FXSAVE areas, larger than its
  255, without a size, and far pointers and MPX's MIB operands). The one left is
  `data16 xstore`, which GNU as rejects. Five forms came from this: `enterw`,
  `leavew`, `pushw` by an imm8, `sysexitq` and `xbegin`'s rel16.
- XED v2026.08.23's datafiles, every instruction record valid in 64-bit mode.
  XED-to-XML leaves out the records XED marks undocumented, `ud0`, `ud1`,
  `enclv`, `int1`, the 32-bit `sysret`, the reserved prefetches and hint NOPs,
  and the encodings no assembler writes; and XED added `udb`, IBHF, ACE-1 and
  AVX10.2's auxiliary conversions since v2025.06.08. From these came `ud0` and
  `ud1` (with ModRM at each operand size, and without it, as older AMD
  processors take them), `udb`, `int1`, `enclv`, `sysretd`, `ffreep`, and the
  8087's and 287's control instructions (`fneni`, `fndisi` and `fnsetpm`, which
  later processors take as NOPs, and the 287XL's `frstpm`).
- GNU as 2.47's opcode table (`opcodes/i386-opc.tbl`, its templates expanded as
  `i386-gen` expands them): every mnemonic it takes in 64-bit mode, but the
  aliases (condition codes, compare predicates, AT&T names, pseudo-prefixes).
  Beyond XED's list, it has AMD's `vmgexit` (SEV-ES), `rmpquery` and `rmpread`
  (SEV-SNP) and AVX512_BMM (`vbmacor16x16x16` and `vbmacxor16x16x16` at 256
  and 512 bits, and `vbitrevb`), Zhaoxin's PadLock and GMI instructions
  (`xrng2`, `xsha384`, `xsha512`, `montmul2`, `xmodexp`, `sm2`, `sm3` and
  `sm4`), and the waiting forms of the x87 control instructions (`finit`,
  `fclex`, `feni`, `fdisi`, `fsetpm`, `fstcw`, `fstsw`, `fstenv` and `fsave`):
  FWAIT and the no-wait form as one instruction, as the SDM lists them, whose
  `9B` GNU as puts before a segment override and a `67` (`9B 64 DD 30` for
  `fsave fs:[rax]`), and the encoders too.

What 64-bit mode has that the table doesn't: `data16 xstore`; IBHF (`F3 48 0F 1E
F8`, a hint NOP elsewhere), ACE-1's tile outer products and AVX10.2's auxiliary
FP8, FP6 and FP4 conversions, which neither GNU as 2.47 nor its `objdump` knows
yet, so that nothing checks their bytes (XED's tests have IBHF's alone); the
encodings that only alias another instruction and that no assembler writes
(XED's undocumented `shl` /6, `test` F6 and F7 /1, and x87's DC D0+i, DC D8+i,
DE D0+i, DF D0+i, DF D8+i, D9 D8+i, DD C8+i and DF C8+i), the reserved NOPs
of `0F 0D` and `0F 18` to `0F 1F` and the reserved prefetches `0F 0D` /3 to /7;
VIA's `montmul`, which XED takes with 16- and 32-bit addresses alone; and
Geode's 3DNow! `pfrcpv` and `pfrsqrtv`, which GNU as 2.47 no longer takes.

In 16- and 32-bit modes, the table has every instruction GNU as 2.47 takes after
`.code16` and `.code32` but the aliases: each of its templates for those modes
alone (`No64`) has a row, GNU as rejects each row the modes leave out (see
"Correctness"), and the 111 rows of their own instructions cross-check with the
rest, the moves to and from the 386's and 486's test registers among them (TR0
to TR7, #UD since the Pentium, and unknown to XED), as does AMD's CR8 through a
`lock mov` of CR0, which GNU as writes for `cr8` there (XED doesn't know it
either). Nor does the table have the `82` group (`80`'s alias, which no
assembler writes) or the 286's and 386's LOADALL, which neither GNU as nor XED
knows.

## Next steps

- **The rest of 64-bit mode** (#17): the instructions in "Coverage" that no
  assembler checks yet, once one does. Of #39's prefixes, `67` has its entry
  point (see "32-bit addresses"), and the CS, DS, ES and SS overrides on
  instructions other than branches are left out: 64-bit mode ignores them, and
  they only pad, as GNU as's branch alignment's CS prefixes do, which the
  multi-byte NOPs do too. Template copies can't hold them (copies of every form
  for each would take the templates past the 32,768 that the form number's 15
  bits beside the fixup flag reach, of which there are 24,723), and a prefix
  the loop builds from a bit of the record would cost every group operations.
- **Labels.** Fixups are done (see "Fixups"); branches inside a function are
  resolved before encoding, with forms that branch relaxation chooses, and
  Phase II's length alone is a cheap length-only pass for it. A symbolic
  displacement after a base needs a placeholder that doesn't fit in 8 bits;
  forcing 32 bits there instead would take a free bit of the record (the mode's
  bit 6, say) and an operation or two on Phase II's displacement path, not
  measured yet.
- **Threads.** Sections are independent: encode them in parallel into
  separate buffers.
- **Speed.** #182 shortened the group's chain and took validation out of the
  loop (see "Zen 5's costs"). What's left: the chain is about 48 cycles, the
  template's path again, and Phase III's `vpshufbitqmb` and `vpcompressb` take
  17 of them; the out-of-order window holds about one and a half groups; and
  after lighter code the power cap makes energy the cost, and 512-bit stores
  cost the most, so the next gains are fewer spills (clang's register allocation
  moves with small source changes: check every width in both states, `--only`
  and `--after-scalar 100`, after each change). APX's loop costs 20% more than
  `x86_encode`'s, its REX2 and EVEX operations and their registers, which is
  why it stays an entry point of its own; and the 16- and 32-bit loops cost
  23% to 24% more, their Phase II's work for the modes' addresses, overrides and
  `67`. The templates still carry checks and flags that only validation used
  to read (the checks dword's register bits, `NO_MEMORY` aside, which
  `x86_validate` reads): bits a later change can take.
