// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The x86-64 encoder: turns a section's selected machine instructions into machine code. An instruction
// (X86Instruction, 16 bytes) is a form (X86Form: the opcode, prefixes, operand roles and field widths) and its
// operands in their encoding roles: the ModRM.reg and ModRM.rm registers, the memory operand's base, index, scale and
// displacement, the VEX.vvvv register, EVEX's opmask, zeroing and rounding, and the immediate. Forms are legacy (with
// REX; x87 among them), VEX (AVX, AVX2, FMA, BMI and the mask register instructions) or EVEX (AVX-512), and each one
// with a memory operand also comes with an FS or GS segment override (thread-local storage). Files that use it include
// it directly; every function in it is static.
//
// Entry points: x86_encoder_initialize (builds the templates; call it once before encoding), x86_encode (the
// vectorized encoder, the code generator's, which takes the valid instructions instruction selection and register
// allocation make and checks nothing about them), x86_encode_scalar (a plain reference encoder, for testing),
// x86_measure and x86_measure_scalar (the same offsets and length without retaining machine code),
// x86_validate (which checks that every instruction is one x86_encode can take, for tests and debugging) and
// x86_form_descriptions (the form table, for tests and diagnostics). The output needs room for
// BUSTER_X86_ENCODER_CAPACITY(count) bytes; x86_encode writes nothing past that, but may write past the instructions'
// bytes (its 64-byte stores). Besides the machine code, x86_encode can store each instruction's offset, and the end of
// each instruction marked with BUSTER_X86_FORM_FIXUP (one with a symbolic operand), from which x86_fixup_distance
// locates the fields a fixup patches without decoding the machine code again. x86_encoder_initialize_address32,
// x86_encode_address32, x86_encode_scalar_address32 and x86_validate_address32 are the same for 32-bit addresses (a
// 67 on every memory operand), and x86_encoder_initialize_apx, x86_encode_apx, x86_encode_scalar_apx and
// x86_validate_apx for APX's general registers 16 to 31 (a REX2 prefix in a legacy form of maps 0 and 1, and EVEX's B4
// and X4 in an EVEX form), and x86_encoder_initialize_legacy, x86_encode_mode32, x86_encode_scalar_32, x86_validate_32,
// x86_encode_mode16, x86_encode_scalar_16 and x86_validate_16 for 32-bit and 16-bit modes' instructions (the mode's
// operand and address sizes, ModRM's 16-bit addresses, no REX, segment overrides in the decorations, and the
// instructions only those modes have), each with templates of their own, so that the 64-bit loop does nothing for them.
//
// How x86_encode works, a group of BUSTER_X86_GROUP_INSTRUCTIONS (16) instructions at a time:
//   I.   Load. Four instructions fill a ZMM register. Each one's form has a 32-byte template (X86Template, built from
//        the form table by x86_encoder_initialize): the bytes the form always has (prefixes, REX.W or the VEX or EVEX
//        prefix with its inverted fields as ones, the opcode, a ModRM /digit) at their positions in a 16-byte slot,
//        its checks and flags, and the slot's layout, which says for each position which byte of the instruction's
//        canonical slot goes there.
//   II.  Compute, across the group in the dword lanes of ZMM registers: ModRM, SIB, REX.RXB, the displacement size
//        (EVEX's in units of N), what the VEX or EVEX prefix takes from the operands (VEX2 or VEX3 among it), which
//        canonical bytes the instruction has and its length. No branches: the addressing mode's decisions are table
//        lookups (vpermd), the register fields move into place with vpmultishiftqb, and the rest are masks.
//   III. Assemble. An instruction's canonical slot is its immediate and displacement followed by the bytes computed
//        in II. One byte shuffle per 4 instructions moves every canonical byte to its position in the instruction and
//        a vpternlogd XORs the template's bytes in. Compressing those 4 slots by the bytes the instructions have
//        (vpcompressb) leaves the 4 instructions' machine code, one after the other, which a 64-byte store appends to
//        the output. Offsets and fixup ends are the running sum of II's lengths (a vpermb and a multiplication), the
//        fixup ends compressed by the marks (vpcompressd).
// The form decides everything the bytes' positions depend on, so instructions with different forms (legacy, VEX or
// EVEX) take the same path, and a form with no bytes (X86_FORM_NONE, which zeroed records have) is how a group is
// padded. The choices are Zen 5's: docs/x86-64-encoder.md has the measurements behind them (encode_group's and
// compute_fields's comments say which).
//
// Layout, in this order:
//   - API: registers and addressing modes (BUSTER_X86_MODE_*), X86Instruction and its decorations
//     (BUSTER_X86_DECORATION_*), X86EncoderError, the operand, encoding, family and syntax constants of the form
//     table, BUSTER_X86_VECTOR_FORMS (the VEX and EVEX forms), BUSTER_X86_FORMS_LEGACY_MODES (the forms of 16- and
//     32-bit modes alone) and BUSTER_X86_FORMS (the form table), X86Form, the segment overrides (BUSTER_X86_FORM_FS
//     and BUSTER_X86_FORM_GS), the fixup flag (BUSTER_X86_FORM_FIXUP), X86FixupField, x86_fixup_distance and
//     x86_fixup_distance_address16, and the entry points' prototypes.
//   - 16- and 32-bit modes: the forms' properties there (x86_form_legacy), x86_form_exists_in_mode and
//     x86_form_takes_override.
//   - Templates: the canonical slot (BUSTER_X86_CANONICAL_*), the layout, checks and flags constants, X86Template, the
//     five tables (x86_templates, x86_templates_address32 for 32-bit addresses, x86_templates_apx for APX, and
//     x86_templates_32 and x86_templates_16 for 32-bit and 16-bit modes), build_template, build_mode_template and
//     build_mode_templates, x86_encoder_initialize, x86_encoder_initialize_address32, x86_encoder_initialize_apx and
//     x86_encoder_initialize_legacy.
//   - Validation: x86_mode_templates, x86_form_lacks_room, x86_form_has_rex2 and x86_form_has_apx_registers,
//     instruction_is_valid (x86_instruction_is_valid, x86_instruction_is_valid_address32, x86_instruction_is_valid_apx,
//     x86_instruction_is_valid_32 and x86_instruction_is_valid_16) and x86_validate (and x86_validate_address32,
//     x86_validate_apx, x86_validate_32 and x86_validate_16).
//   - The scalar encoder: encode_one, and x86_encode_scalar, x86_encode_scalar_address32, x86_encode_scalar_apx,
//     x86_encode_scalar_32 and x86_encode_scalar_16.
//   - The vectorized encoder: X86Gather, X86Store and X86FixupStore (the variants the benchmark compares), X86Fields,
//     the addressing mode tables (x86_mode_fields, x86_mode_constants and x86_mode_presence; for APX
//     x86_mode_fields_apx; and for 16- and 32-bit modes x86_mode_fields_16, the
//     x86_mode_constants_32 and x86_mode_presence_32 halves and the 16-bit ones, and the vpshufb tables x86_rm16 and
//     x86_overrides), x86_mode_lookup, compute_fields (II), the running sum's tables (x86_length_bytes and the others),
//     encode_group (I-III), encode_batches (the loop, with the instructions per iteration and the entry point's mode as
//     parameters), the x86_encode_N instantiations (and x86_encode_16_address32, x86_encode_16_apx,
//     x86_encode_16_mode32 and x86_encode_16_mode16, with the same at 64 per iteration), x86_encode,
//     x86_encode_address32, x86_encode_apx, x86_encode_mode32 and x86_encode_mode16.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <immintrin.h>

#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#define BUSTER_INLINE static inline __attribute__((always_inline))
#define BUSTER_LIKELY(x) __builtin_expect(!!(x), 1)
#define BUSTER_UNLIKELY(x) __builtin_expect(!!(x), 0)

// ---------------------------------------------------------------------------------------------------------------------
// API

// The architectural limit: longer instructions fault.
#define BUSTER_X86_MAX_INSTRUCTION_BYTES 15
// x86_encode stores 64 bytes at a time, each at most 15 bytes per earlier instruction into the output, so the last
// store ends at most 64 bytes past the instructions' upper bound.
#define BUSTER_X86_ENCODER_STORE_BYTES 64
#define BUSTER_X86_ENCODER_CAPACITY(count) ((uint64_t)(count) * BUSTER_X86_MAX_INSTRUCTION_BYTES + BUSTER_X86_ENCODER_STORE_BYTES)
// Offsets are 32-bit, so a call encodes at most this many instructions.
#define BUSTER_X86_ENCODER_MAX_INSTRUCTIONS (UINT32_MAX / BUSTER_X86_MAX_INSTRUCTION_BYTES)

// A register operand is the register's number (general purpose: RAX = 0 ... R15 = 15, and for x86_encode_apx, R16 = 16
// ... R31 = 31 in a legacy form of maps 0 and 1 or an EVEX form (x86_form_has_apx_registers); vector: XMM0 = 0 ...
// XMM15 = 15, or XMM31 = 31 in an EVEX form, and the same for YMM and ZMM; opmask: K0 = 0 ... K7 = 7, and a pair of
// them (an even one and the next) as the even one's number; x87: ST(0) = 0 ... ST(7) = 7; MMX: MM0 = 0 ... MM7 = 7,
// which REX doesn't extend; control: CR0 = 0 ... CR8 = 8, of which CR0, CR2 to CR4 and CR8 exist; debug: DR0 = 0 ...
// DR7 = 7; segment: ES = 0, CS = 1, SS = 2, DS = 3, FS = 4 and GS = 5; tile: TMM0 = 0 ... TMM7 = 7, and a pair of them
// as the even one's number, as for opmasks) or, for AH, CH, DH and BH, 4 to 7 with BUSTER_X86_REGISTER_HIGH_BYTE. With
// a byte form, 4 to 7 alone are SPL, BPL, SIL and DIL, which need a REX prefix; the high byte registers can't have one
// (nor REX2).
#define BUSTER_X86_REGISTER_NUMBER 0x1f
#define BUSTER_X86_REGISTER_HIGH_BYTE 0x40

typedef enum X86Register
{
    X86_REGISTER_RAX,
    X86_REGISTER_RCX,
    X86_REGISTER_RDX,
    X86_REGISTER_RBX,
    X86_REGISTER_RSP,
    X86_REGISTER_RBP,
    X86_REGISTER_RSI,
    X86_REGISTER_RDI,
    X86_REGISTER_R8,
    X86_REGISTER_R9,
    X86_REGISTER_R10,
    X86_REGISTER_R11,
    X86_REGISTER_R12,
    X86_REGISTER_R13,
    X86_REGISTER_R14,
    X86_REGISTER_R15,
    X86_REGISTER_AH = 4 | BUSTER_X86_REGISTER_HIGH_BYTE,
    X86_REGISTER_CH = 5 | BUSTER_X86_REGISTER_HIGH_BYTE,
    X86_REGISTER_DH = 6 | BUSTER_X86_REGISTER_HIGH_BYTE,
    X86_REGISTER_BH = 7 | BUSTER_X86_REGISTER_HIGH_BYTE,
} X86Register;

// X86Instruction.mode: how ModRM.rm is addressed. 0 is the register `rm`; a memory operand has
// BUSTER_X86_MODE_MEMORY and the parts it's made of, and the scale of its index (log2, 0 to 3) in the low bits.
#define BUSTER_X86_MODE_SCALE 0x03
#define BUSTER_X86_MODE_MEMORY 0x04
#define BUSTER_X86_MODE_BASE 0x08
#define BUSTER_X86_MODE_INDEX 0x10
#define BUSTER_X86_MODE_RIP 0x20
// The addressing modes:
#define BUSTER_X86_REGISTER_DIRECT 0
// [rm + displacement]
#define BUSTER_X86_MEMORY_BASE (BUSTER_X86_MODE_MEMORY | BUSTER_X86_MODE_BASE)
// [rm + index * 2^scale + displacement]
#define BUSTER_X86_MEMORY_BASE_INDEX(scale) (BUSTER_X86_MODE_MEMORY | BUSTER_X86_MODE_BASE | BUSTER_X86_MODE_INDEX | (scale))
// [index * 2^scale + displacement], with a 32-bit displacement
#define BUSTER_X86_MEMORY_INDEX(scale) (BUSTER_X86_MODE_MEMORY | BUSTER_X86_MODE_INDEX | (scale))
// [displacement]: a 32-bit absolute address
#define BUSTER_X86_MEMORY_ABSOLUTE BUSTER_X86_MODE_MEMORY
// [rip + displacement], from the end of the instruction
#define BUSTER_X86_MEMORY_RIP (BUSTER_X86_MODE_MEMORY | BUSTER_X86_MODE_RIP)
// In 16- and 32-bit modes alone (x86_encode_scalar_16 and x86_encode_scalar_32): the mode's other address size, which a
// 67 selects. With a memory operand, its address's: 32-bit in 16-bit mode, and 16-bit in 32-bit mode. A 16-bit address
// is a base (BX or BP: RBX's and RBP's numbers) and an index (SI or DI), either alone, or neither (an absolute
// address), without a scale, and a 16-bit displacement. Without a memory operand, the addresses the instruction makes
// itself (a string instruction's, xlat's, jcxz's and loop's, and the others' of x86_form_has_implicit_address).
#define BUSTER_X86_MODE_ADDRESS_SIZE 0x40

// One instruction: 16 bytes, so a ZMM register holds 4. Operands the form doesn't have must be zero.
typedef struct X86Instruction X86Instruction;
struct X86Instruction
{
    // The immediate, in as many low bytes as the form's immediate has (ENTER's two are imm16 | imm8 << 16), signed or
    // not: imm8 in [-128, 255], imm16 in [-32768, 65535] and ENTER's in [0, 2^24). A 64-bit immediate or address
    // (MOV r64, imm64 and MOVABS) is immediate | displacement << 32. An /is4 operand (a register in the immediate's
    // bits 7 to 4) is the register's number times 16, as the instruction has it, plus vpermil2ps's and vpermil2pd's
    // selector (0 to 15).
    int32_t immediate;
    // The memory operand's displacement, or a relative branch's displacement from the end of the instruction (a rel8's
    // in [-128, 127]).
    int32_t displacement;
    // The register in ModRM.reg.
    uint8_t reg;
    // The register in ModRM.rm (or added to the opcode, for a +r form), or a memory operand's base.
    uint8_t rm;
    // A memory operand's index register: a general one, or in a VSIB operand (a gather's or scatter's), a vector
    // register of the operand's class, XMM4 included.
    uint8_t index;
    // BUSTER_X86_REGISTER_DIRECT or a BUSTER_X86_MEMORY_* addressing mode.
    uint8_t mode;
    // An X86Form, plus BUSTER_X86_FORM_FS or BUSTER_X86_FORM_GS for a segment override, and BUSTER_X86_FORM_FIXUP for an
    // instruction with a symbolic operand.
    uint16_t form;
    // The register in VEX.vvvv (EVEX.V'vvvv): a VEX or EVEX form's operand that isn't in ModRM.
    uint8_t vvvv;
    // An EVEX form's decorations: BUSTER_X86_DECORATION_*.
    uint8_t decorations;
};
_Static_assert(sizeof(X86Instruction) == 16, "four instructions fill a ZMM register");

// X86Instruction.decorations: the opmask register (K1 to K7, or 0 for none) and zeroing (with an opmask, instead of
// merging), for the EVEX forms that can have them, and the rounding mode of a form with embedded rounding (its name
// ends in _ER). The bits are where EVEX's last byte has them; bits 3 and 4 must be zero. But in 16- and 32-bit modes,
// whose segments are more than FS and GS, bits 3 to 5 are a segment override (BUSTER_X86_SEGMENT_OVERRIDE of ES, CS,
// SS, DS, FS or GS, or 0 for none) of an instruction with an address: a memory operand (but in a form with FS or GS
// already), a moffs, or one the instruction makes itself (a string instruction's source, xlat's and the others' of
// x86_form_has_implicit_address but jcxz's and loop's, which make no memory address). A memory operand takes no
// rounding, so bit 5 is free there.
#define BUSTER_X86_DECORATION_MASK 0x07
#define BUSTER_X86_DECORATION_ROUNDING 0x60
#define BUSTER_X86_DECORATION_ZEROING 0x80
#define BUSTER_X86_DECORATION_SEGMENT 0x38
#define BUSTER_X86_ROUND_NEAREST 0x00
#define BUSTER_X86_ROUND_DOWN 0x20
#define BUSTER_X86_ROUND_UP 0x40
#define BUSTER_X86_ROUND_ZERO 0x60
// A segment register's number (ES = 0 ... GS = 5) as a segment override in the decorations.
#define BUSTER_X86_SEGMENT_OVERRIDE(segment) (((segment) + 1) << 3)

typedef enum X86EncoderError
{
    // An instruction's operands can't be encoded: the scalar encoder's, which validates them first (x86_validate finds
    // which). x86_encode doesn't check them.
    X86_ENCODER_ERROR_INVALID_INSTRUCTION = -1,
    X86_ENCODER_ERROR_OUTPUT_TOO_SMALL = -2,
    X86_ENCODER_ERROR_TOO_MANY_INSTRUCTIONS = -3,
} X86EncoderError;

// The form table's operand roles: a register class, and for ModRM.rm whether it can be a register, memory, or both
// (as Intel's opcode maps write them: G a general register in ModRM.reg, E a general register or memory in
// ModRM.rm, V an XMM register, XM an XMM register or memory, M only memory; and Y, YM, Z and ZM for YMM and ZMM, K
// for the opmask registers, ST for the x87 registers, MM for the MMX registers, CR, DR, TR and SREG for the control,
// debug, test and segment registers, TMM for AMX's tiles and BND for MPX's bound registers; KP and TMMP are pairs of
// them, and V4 and Z4 blocks of four XMM or ZMM registers; VSIBX, VSIBY and VSIBZ are a gather's or scatter's VSIB
// memory operand, whose index is an XMM, YMM or ZMM register, Intel's vm32x and the rest, SIBMEM AMX's memory operand,
// which always has a SIB byte, Intel's sibmem, MIB MPX's, Intel's mib, and M_NO_RIP memory that can't be RIP-relative).
// For a +r form, the register is the rm operand. An rm operand that's an opmask, MMX or bound register, or memory,
// would need checks that depend on the mode (k8 to k15 and mm8 to mm15 don't exist, but a base can be R8 to R15), so
// those are two forms. With APX, so would SSE's XMM register or memory (xmm16 to xmm31 don't exist there, but a base
// can be R16 to R31), which x86_validate_apx tells apart instead.
#define BUSTER_X86_OPERAND_CLASS 0x0f
#define BUSTER_X86_CLASS_NONE 0
#define BUSTER_X86_CLASS_GPR8 1
#define BUSTER_X86_CLASS_GPR16 2
#define BUSTER_X86_CLASS_GPR32 3
#define BUSTER_X86_CLASS_GPR64 4
#define BUSTER_X86_CLASS_XMM 5
#define BUSTER_X86_CLASS_YMM 6
#define BUSTER_X86_CLASS_ZMM 7
#define BUSTER_X86_CLASS_OPMASK 8
#define BUSTER_X86_CLASS_ST 9
#define BUSTER_X86_CLASS_BND 10
#define BUSTER_X86_CLASS_MMX 11
#define BUSTER_X86_CLASS_CONTROL 12
#define BUSTER_X86_CLASS_DEBUG 13
#define BUSTER_X86_CLASS_SEGMENT 14
#define BUSTER_X86_CLASS_TILE 15
#define BUSTER_X86_OPERAND_REGISTER 0x10
#define BUSTER_X86_OPERAND_MEMORY 0x20
// A VSIB memory operand: its index (which it always has) is a vector register of the class.
#define BUSTER_X86_OPERAND_VSIB 0x40
// A memory operand that always has a SIB byte: an index, whose 4 is SIB's none (the stride of AMX's loads and stores),
// or no base (and so never RIP).
#define BUSTER_X86_OPERAND_SIB 0x80
// A memory operand that can't be RIP-relative (AMX's SIB operands, and MPX's bndmk and MIB operands).
#define BUSTER_X86_OPERAND_NO_RIP 0x100
// A memory operand whose index isn't scaled: MPX's MIB operands, whose index is the pointer that bndldx and bndstx look
// up, not part of the address.
#define BUSTER_X86_OPERAND_UNSCALED 0x200
// A group of registers, the first's number standing for all of them, a multiple of the group's size (the processor
// ignores the low bits): a pair (VP2INTERSECT's opmasks, AMX-TRANSPOSE's tiles) or a block of four (4FMAPS's and
// 4VNNIW's sources).
#define BUSTER_X86_OPERAND_PAIR 0x400
#define BUSTER_X86_OPERAND_BLOCK 0x800
// Not a register: APX's default flags value (ccmp's and ctest's flags when their source condition is false), in
// EVEX.vvvv as it is (not inverted), OF, SF, ZF and CF in bits 3 to 0.
#define BUSTER_X86_OPERAND_DFV 0x1000
// A debug register operand that is a test register instead (TR: the 386's and 486's TR0 to TR7, which 16- and 32-bit
// modes alone have), which encodes as a debug register does: only the name differs.
#define BUSTER_X86_OPERAND_TEST 0x2000
#define BUSTER_X86_OPERAND_NONE 0
#define BUSTER_X86_OPERAND_GB (BUSTER_X86_CLASS_GPR8 | BUSTER_X86_OPERAND_REGISTER)
#define BUSTER_X86_OPERAND_GW (BUSTER_X86_CLASS_GPR16 | BUSTER_X86_OPERAND_REGISTER)
#define BUSTER_X86_OPERAND_GD (BUSTER_X86_CLASS_GPR32 | BUSTER_X86_OPERAND_REGISTER)
#define BUSTER_X86_OPERAND_GQ (BUSTER_X86_CLASS_GPR64 | BUSTER_X86_OPERAND_REGISTER)
#define BUSTER_X86_OPERAND_EB (BUSTER_X86_OPERAND_GB | BUSTER_X86_OPERAND_MEMORY)
#define BUSTER_X86_OPERAND_EW (BUSTER_X86_OPERAND_GW | BUSTER_X86_OPERAND_MEMORY)
#define BUSTER_X86_OPERAND_ED (BUSTER_X86_OPERAND_GD | BUSTER_X86_OPERAND_MEMORY)
#define BUSTER_X86_OPERAND_EQ (BUSTER_X86_OPERAND_GQ | BUSTER_X86_OPERAND_MEMORY)
#define BUSTER_X86_OPERAND_V (BUSTER_X86_CLASS_XMM | BUSTER_X86_OPERAND_REGISTER)
#define BUSTER_X86_OPERAND_XM (BUSTER_X86_OPERAND_V | BUSTER_X86_OPERAND_MEMORY)
#define BUSTER_X86_OPERAND_Y (BUSTER_X86_CLASS_YMM | BUSTER_X86_OPERAND_REGISTER)
#define BUSTER_X86_OPERAND_YM (BUSTER_X86_OPERAND_Y | BUSTER_X86_OPERAND_MEMORY)
#define BUSTER_X86_OPERAND_Z (BUSTER_X86_CLASS_ZMM | BUSTER_X86_OPERAND_REGISTER)
#define BUSTER_X86_OPERAND_ZM (BUSTER_X86_OPERAND_Z | BUSTER_X86_OPERAND_MEMORY)
#define BUSTER_X86_OPERAND_K (BUSTER_X86_CLASS_OPMASK | BUSTER_X86_OPERAND_REGISTER)
#define BUSTER_X86_OPERAND_ST (BUSTER_X86_CLASS_ST | BUSTER_X86_OPERAND_REGISTER)
#define BUSTER_X86_OPERAND_KP (BUSTER_X86_OPERAND_K | BUSTER_X86_OPERAND_PAIR)
#define BUSTER_X86_OPERAND_BND (BUSTER_X86_CLASS_BND | BUSTER_X86_OPERAND_REGISTER)
#define BUSTER_X86_OPERAND_MM (BUSTER_X86_CLASS_MMX | BUSTER_X86_OPERAND_REGISTER)
#define BUSTER_X86_OPERAND_CR (BUSTER_X86_CLASS_CONTROL | BUSTER_X86_OPERAND_REGISTER)
#define BUSTER_X86_OPERAND_DR (BUSTER_X86_CLASS_DEBUG | BUSTER_X86_OPERAND_REGISTER)
#define BUSTER_X86_OPERAND_TR (BUSTER_X86_OPERAND_DR | BUSTER_X86_OPERAND_TEST)
#define BUSTER_X86_OPERAND_SREG (BUSTER_X86_CLASS_SEGMENT | BUSTER_X86_OPERAND_REGISTER)
#define BUSTER_X86_OPERAND_TMM (BUSTER_X86_CLASS_TILE | BUSTER_X86_OPERAND_REGISTER)
#define BUSTER_X86_OPERAND_TMMP (BUSTER_X86_OPERAND_TMM | BUSTER_X86_OPERAND_PAIR)
#define BUSTER_X86_OPERAND_V4 (BUSTER_X86_OPERAND_V | BUSTER_X86_OPERAND_BLOCK)
#define BUSTER_X86_OPERAND_Z4 (BUSTER_X86_OPERAND_Z | BUSTER_X86_OPERAND_BLOCK)
#define BUSTER_X86_OPERAND_M BUSTER_X86_OPERAND_MEMORY
#define BUSTER_X86_OPERAND_VSIBX (BUSTER_X86_CLASS_XMM | BUSTER_X86_OPERAND_MEMORY | BUSTER_X86_OPERAND_VSIB)
#define BUSTER_X86_OPERAND_VSIBY (BUSTER_X86_CLASS_YMM | BUSTER_X86_OPERAND_MEMORY | BUSTER_X86_OPERAND_VSIB)
#define BUSTER_X86_OPERAND_VSIBZ (BUSTER_X86_CLASS_ZMM | BUSTER_X86_OPERAND_MEMORY | BUSTER_X86_OPERAND_VSIB)
#define BUSTER_X86_OPERAND_SIBMEM (BUSTER_X86_OPERAND_MEMORY | BUSTER_X86_OPERAND_SIB | BUSTER_X86_OPERAND_NO_RIP)
#define BUSTER_X86_OPERAND_MIB (BUSTER_X86_OPERAND_MEMORY | BUSTER_X86_OPERAND_NO_RIP | BUSTER_X86_OPERAND_UNSCALED)
#define BUSTER_X86_OPERAND_M_NO_RIP (BUSTER_X86_OPERAND_MEMORY | BUSTER_X86_OPERAND_NO_RIP)

// How the form uses ModRM: none (NO), a register in ModRM.reg (R, /r), an opcode extension in ModRM.reg (D0-D7,
// /0-/7), or none, with the rm register added to the last opcode byte (PR, +r).
#define BUSTER_X86_ENCODING_NO 0x00
#define BUSTER_X86_ENCODING_R 0x01
#define BUSTER_X86_ENCODING_PR 0x02
#define BUSTER_X86_ENCODING_DIGIT 0x08
#define BUSTER_X86_ENCODING_D0 (BUSTER_X86_ENCODING_DIGIT | 0)
#define BUSTER_X86_ENCODING_D1 (BUSTER_X86_ENCODING_DIGIT | 1)
#define BUSTER_X86_ENCODING_D2 (BUSTER_X86_ENCODING_DIGIT | 2)
#define BUSTER_X86_ENCODING_D3 (BUSTER_X86_ENCODING_DIGIT | 3)
#define BUSTER_X86_ENCODING_D4 (BUSTER_X86_ENCODING_DIGIT | 4)
#define BUSTER_X86_ENCODING_D5 (BUSTER_X86_ENCODING_DIGIT | 5)
#define BUSTER_X86_ENCODING_D6 (BUSTER_X86_ENCODING_DIGIT | 6)
#define BUSTER_X86_ENCODING_D7 (BUSTER_X86_ENCODING_DIGIT | 7)

// A form's prefix family: legacy prefixes and REX, a VEX prefix (2 or 3 bytes) or an EVEX prefix (4 bytes).
#define BUSTER_X86_FAMILY_LEGACY 0
#define BUSTER_X86_FAMILY_VEX 1
#define BUSTER_X86_FAMILY_EVEX 2

// What an EVEX form's decorations can be: an opmask (merging), an opmask with zeroing, and EVEX.b, which the form
// always has: a broadcast memory operand (its element repeated), embedded rounding (with a rounding mode in the
// decorations, and only register operands) or suppressed exceptions (the same, without the rounding mode). And three
// that APX's forms always have, without an opmask: NF, no flags (the instruction leaves them as they are; P2's bit 2,
// the opmask's high bit's place), and ND, a new destination (in EVEX.vvvv; EVEX.b's place, which map 4 calls ND), or
// for setcc and imul, ZU (zero upper: the destination's upper bits are zeroed, rather than kept); and ccmp's and
// ctest's source condition, SCC(K) for condition code K, in P2's bits 3 to 0 (V''s, NF's and the opmask's places),
// with a default flags value in EVEX.vvvv (BUSTER_X86_OPERAND_DFV).
#define BUSTER_X86_EVEX_NONE 0x00
#define BUSTER_X86_EVEX_MASK 0x01
#define BUSTER_X86_EVEX_ZEROING 0x02
#define BUSTER_X86_EVEX_BROADCAST 0x04
#define BUSTER_X86_EVEX_ROUNDING 0x08
#define BUSTER_X86_EVEX_SAE 0x10
#define BUSTER_X86_EVEX_NF 0x20
#define BUSTER_X86_EVEX_ND 0x40
#define BUSTER_X86_EVEX_SOURCE_CONDITION 0x80
#define BUSTER_X86_EVEX_SCC(condition) (BUSTER_X86_EVEX_SOURCE_CONDITION | (condition) << 8)
#define BUSTER_X86_EVEX_K BUSTER_X86_EVEX_MASK
#define BUSTER_X86_EVEX_KZ (BUSTER_X86_EVEX_MASK | BUSTER_X86_EVEX_ZEROING)
#define BUSTER_X86_EVEX_K_BCST (BUSTER_X86_EVEX_K | BUSTER_X86_EVEX_BROADCAST)
#define BUSTER_X86_EVEX_KZ_BCST (BUSTER_X86_EVEX_KZ | BUSTER_X86_EVEX_BROADCAST)
#define BUSTER_X86_EVEX_ER BUSTER_X86_EVEX_ROUNDING
#define BUSTER_X86_EVEX_KZ_ER (BUSTER_X86_EVEX_KZ | BUSTER_X86_EVEX_ROUNDING)
#define BUSTER_X86_EVEX_K_SAE (BUSTER_X86_EVEX_K | BUSTER_X86_EVEX_SAE)
#define BUSTER_X86_EVEX_KZ_SAE (BUSTER_X86_EVEX_KZ | BUSTER_X86_EVEX_SAE)
#define BUSTER_X86_EVEX_ND_NF (BUSTER_X86_EVEX_ND | BUSTER_X86_EVEX_NF)
#define BUSTER_X86_EVEX_B (BUSTER_X86_EVEX_BROADCAST | BUSTER_X86_EVEX_ROUNDING | BUSTER_X86_EVEX_SAE | BUSTER_X86_EVEX_ND)

// The order of a form's operands in Intel syntax, with its implicit ones (for printing and tests; the encoding doesn't
// depend on it). ACC is AL, AX, EAX or RAX, and DX the port of IN and OUT; MOFFS is an absolute address in the
// immediate (64-bit, or in 16- and 32-bit modes, 16- or 32-bit); FAR_POINTER a far branch's selector and offset in the
// immediate (ptr16:16: the offset in its low 16 bits and the selector in the high 16; ptr16:32: the offset in the
// immediate and the selector in the displacement, which ENTER's imm16 | imm8 << 16 and a 64-bit immediate are like);
// VVVV is the VEX.vvvv register (an ND form's destination, before the legacy form's operands: VVVV_REG_RM and the
// rest); ST is the x87 register ST(0), where the rm operand is ST(i); XMM0 is the variable blends' implicit mask
// register; IMM8_IMM8 is two 8-bit immediates (SSE4a's extrq and insertq), the immediate's low byte first; RM_VVVV_REG
// is a masked store's memory, mask and source; IS4 is the register in the immediate's bits 7 to 4, of the reg operand's
// class; and IS4_..._IMM has a 4-bit selector in bits 3 to 0 too (vpermil2ps's and vpermil2pd's); and MEMORY_RCX is
// [rcx] (rmpread's implicit memory operand). A default flags value in vvvv (BUSTER_X86_OPERAND_DFV) isn't one of them:
// GNU as writes it after the mnemonic, as {dfv=of, sf, zf, cf}.
typedef enum X86Syntax
{
    X86_SYNTAX_NONE,
    X86_SYNTAX_RM,
    X86_SYNTAX_RM_REG,
    X86_SYNTAX_REG_RM,
    X86_SYNTAX_RM_IMM,
    X86_SYNTAX_REG_RM_IMM,
    X86_SYNTAX_RM_REG_IMM,
    X86_SYNTAX_RM_REG_CL,
    X86_SYNTAX_RM_ONE,
    X86_SYNTAX_RM_CL,
    X86_SYNTAX_IMM,
    X86_SYNTAX_IMM_IMM,
    X86_SYNTAX_REL,
    X86_SYNTAX_ACC_IMM,
    X86_SYNTAX_ACC_MOFFS,
    X86_SYNTAX_MOFFS_ACC,
    X86_SYNTAX_REG_VVVV_RM,
    X86_SYNTAX_REG_VVVV_RM_IMM,
    X86_SYNTAX_REG_RM_VVVV,
    X86_SYNTAX_VVVV_RM,
    X86_SYNTAX_VVVV_RM_IMM,
    X86_SYNTAX_ST_RM,
    X86_SYNTAX_RM_ST,
    X86_SYNTAX_ACC,
    X86_SYNTAX_RM_ACC,
    X86_SYNTAX_RM_REG_VVVV,
    X86_SYNTAX_REG_RM_XMM0,
    X86_SYNTAX_RM_IMM8_IMM8,
    X86_SYNTAX_REG_RM_IMM8_IMM8,
    X86_SYNTAX_RM_VVVV_REG,
    X86_SYNTAX_REG_VVVV_RM_IS4,
    X86_SYNTAX_REG_VVVV_IS4_RM,
    X86_SYNTAX_REG_VVVV_RM_IS4_IMM,
    X86_SYNTAX_REG_VVVV_IS4_RM_IMM,
    X86_SYNTAX_ACC_DX,
    X86_SYNTAX_DX_ACC,
    X86_SYNTAX_IMM_ACC,
    X86_SYNTAX_IMM_RM,
    X86_SYNTAX_REG,
    X86_SYNTAX_VVVV_REG_RM,
    X86_SYNTAX_VVVV_RM_REG,
    X86_SYNTAX_VVVV_RM_ONE,
    X86_SYNTAX_VVVV_RM_CL,
    X86_SYNTAX_VVVV_RM_REG_IMM,
    X86_SYNTAX_VVVV_RM_REG_CL,
    X86_SYNTAX_MEMORY_RCX_ACC,
    X86_SYNTAX_FAR_POINTER,
} X86Syntax;

// The form table. Each row is X(name, mnemonic, prefixes, opcode bytes, opcode, encoding, immediate bytes, relative
// bytes, REX.W, reg operand, rm operand, operand bytes, syntax):
//   - prefixes: up to two legacy prefixes, the first in the low byte (0x66, 0xF266 for 66 F2), or 0; FWAIT's 9B, as the
//     first, makes the waiting form of an x87 control instruction, before any segment override and 67;
//   - opcode: the opcode's bytes (with any 0F, 0F 38 or 0F 3A escape) as one number, the first byte in its high
//     byte, and how many there are;
//   - immediate bytes: 0, 1, 2, 3 (ENTER), 4 or 8 (a 64-bit immediate or address); relative bytes: 0, 1 or 4, for a
//     rel8 or rel32 in the displacement field;
//   - operand bytes: the size of the memory operand (or accumulator), for printing.
// The families below share rows between mnemonics; their K is the ModRM digit (or condition code) as one token.
#define BUSTER_X86_ARITHMETIC_FORMS(X, N, m, K) \
    X(N##_RM8_R8, m, 0, 1, 0x00 + 8 * K, R, 0, 0, 0, GB, EB, 1, RM_REG) \
    X(N##_RM16_R16, m, 0x66, 1, 0x01 + 8 * K, R, 0, 0, 0, GW, EW, 2, RM_REG) \
    X(N##_RM32_R32, m, 0, 1, 0x01 + 8 * K, R, 0, 0, 0, GD, ED, 4, RM_REG) \
    X(N##_RM64_R64, m, 0, 1, 0x01 + 8 * K, R, 0, 0, 1, GQ, EQ, 8, RM_REG) \
    X(N##_R8_RM8, m, 0, 1, 0x02 + 8 * K, R, 0, 0, 0, GB, EB, 1, REG_RM) \
    X(N##_R16_RM16, m, 0x66, 1, 0x03 + 8 * K, R, 0, 0, 0, GW, EW, 2, REG_RM) \
    X(N##_R32_RM32, m, 0, 1, 0x03 + 8 * K, R, 0, 0, 0, GD, ED, 4, REG_RM) \
    X(N##_R64_RM64, m, 0, 1, 0x03 + 8 * K, R, 0, 0, 1, GQ, EQ, 8, REG_RM) \
    X(N##_AL_IMM8, m, 0, 1, 0x04 + 8 * K, NO, 1, 0, 0, NONE, NONE, 1, ACC_IMM) \
    X(N##_AX_IMM16, m, 0x66, 1, 0x05 + 8 * K, NO, 2, 0, 0, NONE, NONE, 2, ACC_IMM) \
    X(N##_EAX_IMM32, m, 0, 1, 0x05 + 8 * K, NO, 4, 0, 0, NONE, NONE, 4, ACC_IMM) \
    X(N##_RAX_IMM32, m, 0, 1, 0x05 + 8 * K, NO, 4, 0, 1, NONE, NONE, 8, ACC_IMM) \
    X(N##_RM8_IMM8, m, 0, 1, 0x80, D##K, 1, 0, 0, NONE, EB, 1, RM_IMM) \
    X(N##_RM16_IMM16, m, 0x66, 1, 0x81, D##K, 2, 0, 0, NONE, EW, 2, RM_IMM) \
    X(N##_RM32_IMM32, m, 0, 1, 0x81, D##K, 4, 0, 0, NONE, ED, 4, RM_IMM) \
    X(N##_RM64_IMM32, m, 0, 1, 0x81, D##K, 4, 0, 1, NONE, EQ, 8, RM_IMM) \
    X(N##_RM16_IMM8, m, 0x66, 1, 0x83, D##K, 1, 0, 0, NONE, EW, 2, RM_IMM) \
    X(N##_RM32_IMM8, m, 0, 1, 0x83, D##K, 1, 0, 0, NONE, ED, 4, RM_IMM) \
    X(N##_RM64_IMM8, m, 0, 1, 0x83, D##K, 1, 0, 1, NONE, EQ, 8, RM_IMM)

#define BUSTER_X86_SHIFT_FORMS(X, N, m, K) \
    X(N##_RM8_1, m, 0, 1, 0xd0, D##K, 0, 0, 0, NONE, EB, 1, RM_ONE) \
    X(N##_RM16_1, m, 0x66, 1, 0xd1, D##K, 0, 0, 0, NONE, EW, 2, RM_ONE) \
    X(N##_RM32_1, m, 0, 1, 0xd1, D##K, 0, 0, 0, NONE, ED, 4, RM_ONE) \
    X(N##_RM64_1, m, 0, 1, 0xd1, D##K, 0, 0, 1, NONE, EQ, 8, RM_ONE) \
    X(N##_RM8_CL, m, 0, 1, 0xd2, D##K, 0, 0, 0, NONE, EB, 1, RM_CL) \
    X(N##_RM16_CL, m, 0x66, 1, 0xd3, D##K, 0, 0, 0, NONE, EW, 2, RM_CL) \
    X(N##_RM32_CL, m, 0, 1, 0xd3, D##K, 0, 0, 0, NONE, ED, 4, RM_CL) \
    X(N##_RM64_CL, m, 0, 1, 0xd3, D##K, 0, 0, 1, NONE, EQ, 8, RM_CL) \
    X(N##_RM8_IMM8, m, 0, 1, 0xc0, D##K, 1, 0, 0, NONE, EB, 1, RM_IMM) \
    X(N##_RM16_IMM8, m, 0x66, 1, 0xc1, D##K, 1, 0, 0, NONE, EW, 2, RM_IMM) \
    X(N##_RM32_IMM8, m, 0, 1, 0xc1, D##K, 1, 0, 0, NONE, ED, 4, RM_IMM) \
    X(N##_RM64_IMM8, m, 0, 1, 0xc1, D##K, 1, 0, 1, NONE, EQ, 8, RM_IMM)

// Group 3 (F6/F7) and group 4/5 (FE/FF) instructions with a single r/m operand.
#define BUSTER_X86_UNARY_FORMS(X, N, m, opcode8, opcode, K) \
    X(N##_RM8, m, 0, 1, opcode8, D##K, 0, 0, 0, NONE, EB, 1, RM) \
    X(N##_RM16, m, 0x66, 1, opcode, D##K, 0, 0, 0, NONE, EW, 2, RM) \
    X(N##_RM32, m, 0, 1, opcode, D##K, 0, 0, 0, NONE, ED, 4, RM) \
    X(N##_RM64, m, 0, 1, opcode, D##K, 0, 0, 1, NONE, EQ, 8, RM)

#define BUSTER_X86_CONDITION_FORMS(X, CC, cc, K) \
    X(J##CC##_REL8, "j" cc, 0, 1, 0x70 + K, NO, 0, 1, 0, NONE, NONE, 0, REL) \
    X(J##CC##_REL32, "j" cc, 0, 2, 0x0f80 + K, NO, 0, 4, 0, NONE, NONE, 0, REL) \
    X(SET##CC##_RM8, "set" cc, 0, 2, 0x0f90 + K, D0, 0, 0, 0, NONE, EB, 1, RM) \
    X(CMOV##CC##_R16_RM16, "cmov" cc, 0x66, 2, 0x0f40 + K, R, 0, 0, 0, GW, EW, 2, REG_RM) \
    X(CMOV##CC##_R32_RM32, "cmov" cc, 0, 2, 0x0f40 + K, R, 0, 0, 0, GD, ED, 4, REG_RM) \
    X(CMOV##CC##_R64_RM64, "cmov" cc, 0, 2, 0x0f40 + K, R, 0, 0, 1, GQ, EQ, 8, REG_RM)

// A conditional jump with a prefix: MPX's BND (F2) and a branch hint (CS's 2E, not taken, and DS's 3E, taken) at both
// widths, and 66's rel16 (which AMD takes as a jump to a 16-bit address in 64-bit mode, and Intel ignores).
#define BUSTER_X86_PREFIXED_JCC_FORMS(X, CC, cc, K) \
    X(BND_J##CC##_REL8, "bnd j" cc, 0xf2, 1, 0x70 + K, NO, 0, 1, 0, NONE, NONE, 0, REL) \
    X(BND_J##CC##_REL32, "bnd j" cc, 0xf2, 2, 0x0f80 + K, NO, 0, 4, 0, NONE, NONE, 0, REL) \
    X(J##CC##_REL8_NOT_TAKEN, "cs j" cc, 0x2e, 1, 0x70 + K, NO, 0, 1, 0, NONE, NONE, 0, REL) \
    X(J##CC##_REL32_NOT_TAKEN, "cs j" cc, 0x2e, 2, 0x0f80 + K, NO, 0, 4, 0, NONE, NONE, 0, REL) \
    X(J##CC##_REL8_TAKEN, "ds j" cc, 0x3e, 1, 0x70 + K, NO, 0, 1, 0, NONE, NONE, 0, REL) \
    X(J##CC##_REL32_TAKEN, "ds j" cc, 0x3e, 2, 0x0f80 + K, NO, 0, 4, 0, NONE, NONE, 0, REL) \
    X(J##CC##_REL16, "data16 j" cc, 0x66, 2, 0x0f80 + K, NO, 0, 2, 0, NONE, NONE, 0, REL)

#define BUSTER_X86_BIT_TEST_FORMS(X, N, m, opcode, K) \
    X(N##_RM16_R16, m, 0x66, 2, 0x0f00 + opcode, R, 0, 0, 0, GW, EW, 2, RM_REG) \
    X(N##_RM16_IMM8, m, 0x66, 2, 0x0fba, D##K, 1, 0, 0, NONE, EW, 2, RM_IMM) \
    X(N##_RM32_R32, m, 0, 2, 0x0f00 + opcode, R, 0, 0, 0, GD, ED, 4, RM_REG) \
    X(N##_RM64_R64, m, 0, 2, 0x0f00 + opcode, R, 0, 0, 1, GQ, EQ, 8, RM_REG) \
    X(N##_RM32_IMM8, m, 0, 2, 0x0fba, D##K, 1, 0, 0, NONE, ED, 4, RM_IMM) \
    X(N##_RM64_IMM8, m, 0, 2, 0x0fba, D##K, 1, 0, 1, NONE, EQ, 8, RM_IMM)

// A general register from r/m, in 16-, 32- and 64-bit forms (66 comes before a mandatory prefix, as GNU as puts it).
#define BUSTER_X86_R_RM_FORMS(X, N, m, prefixes, opcode) \
    X(N##_R16_RM16, m, (prefixes) << 8 | 0x66, 2, opcode, R, 0, 0, 0, GW, EW, 2, REG_RM) \
    X(N##_R32_RM32, m, prefixes, 2, opcode, R, 0, 0, 0, GD, ED, 4, REG_RM) \
    X(N##_R64_RM64, m, prefixes, 2, opcode, R, 0, 0, 1, GQ, EQ, 8, REG_RM)

// Atomic read-modify-write instructions, plain and with a LOCK prefix (which only memory operands can have).
#define BUSTER_X86_ATOMIC_FORMS(X, N, m, opcode8, opcode) \
    X(N##_RM8_R8, m, 0, 2, opcode8, R, 0, 0, 0, GB, EB, 1, RM_REG) \
    X(N##_RM16_R16, m, 0x66, 2, opcode, R, 0, 0, 0, GW, EW, 2, RM_REG) \
    X(N##_RM32_R32, m, 0, 2, opcode, R, 0, 0, 0, GD, ED, 4, RM_REG) \
    X(N##_RM64_R64, m, 0, 2, opcode, R, 0, 0, 1, GQ, EQ, 8, RM_REG) \
    BUSTER_X86_LOCKED_ATOMIC_FORMS(X, LOCK_##N, "lock " m, 0xf0, opcode8, opcode)

// The locked forms below take their prefixes as P: LOCK's F0, or with HLE's XACQUIRE (F2) or XRELEASE (F3) before it
// (the first prefix in P's low byte). A 16-bit form's 66 comes first.
#define BUSTER_X86_LOCKED_ATOMIC_FORMS(X, N, m, P, opcode8, opcode) \
    X(N##_M8_R8, m, P, 2, opcode8, R, 0, 0, 0, GB, M, 1, RM_REG) \
    X(N##_M16_R16, m, (P) << 8 | 0x66, 2, opcode, R, 0, 0, 0, GW, M, 2, RM_REG) \
    X(N##_M32_R32, m, P, 2, opcode, R, 0, 0, 0, GD, M, 4, RM_REG) \
    X(N##_M64_R64, m, P, 2, opcode, R, 0, 0, 1, GQ, M, 8, RM_REG)

// The arithmetic instructions with a LOCK prefix: a memory destination, and a register or immediate source.
#define BUSTER_X86_LOCKED_ARITHMETIC_FORMS(X, N, m, P, K) \
    X(N##_M8_R8, m, P, 1, 0x00 + 8 * K, R, 0, 0, 0, GB, M, 1, RM_REG) \
    X(N##_M16_R16, m, (P) << 8 | 0x66, 1, 0x01 + 8 * K, R, 0, 0, 0, GW, M, 2, RM_REG) \
    X(N##_M32_R32, m, P, 1, 0x01 + 8 * K, R, 0, 0, 0, GD, M, 4, RM_REG) \
    X(N##_M64_R64, m, P, 1, 0x01 + 8 * K, R, 0, 0, 1, GQ, M, 8, RM_REG) \
    X(N##_M8_IMM8, m, P, 1, 0x80, D##K, 1, 0, 0, NONE, M, 1, RM_IMM) \
    X(N##_M16_IMM16, m, (P) << 8 | 0x66, 1, 0x81, D##K, 2, 0, 0, NONE, M, 2, RM_IMM) \
    X(N##_M32_IMM32, m, P, 1, 0x81, D##K, 4, 0, 0, NONE, M, 4, RM_IMM) \
    X(N##_M64_IMM32, m, P, 1, 0x81, D##K, 4, 0, 1, NONE, M, 8, RM_IMM) \
    X(N##_M16_IMM8, m, (P) << 8 | 0x66, 1, 0x83, D##K, 1, 0, 0, NONE, M, 2, RM_IMM) \
    X(N##_M32_IMM8, m, P, 1, 0x83, D##K, 1, 0, 0, NONE, M, 4, RM_IMM) \
    X(N##_M64_IMM8, m, P, 1, 0x83, D##K, 1, 0, 1, NONE, M, 8, RM_IMM)

#define BUSTER_X86_LOCKED_UNARY_FORMS(X, N, m, P, opcode8, opcode, K) \
    X(N##_M8, m, P, 1, opcode8, D##K, 0, 0, 0, NONE, M, 1, RM) \
    X(N##_M16, m, (P) << 8 | 0x66, 1, opcode, D##K, 0, 0, 0, NONE, M, 2, RM) \
    X(N##_M32, m, P, 1, opcode, D##K, 0, 0, 0, NONE, M, 4, RM) \
    X(N##_M64, m, P, 1, opcode, D##K, 0, 0, 1, NONE, M, 8, RM)

#define BUSTER_X86_LOCKED_BIT_TEST_FORMS(X, N, m, P, opcode, K) \
    X(N##_M16_R16, m, (P) << 8 | 0x66, 2, 0x0f00 + opcode, R, 0, 0, 0, GW, M, 2, RM_REG) \
    X(N##_M32_R32, m, P, 2, 0x0f00 + opcode, R, 0, 0, 0, GD, M, 4, RM_REG) \
    X(N##_M64_R64, m, P, 2, 0x0f00 + opcode, R, 0, 0, 1, GQ, M, 8, RM_REG) \
    X(N##_M16_IMM8, m, (P) << 8 | 0x66, 2, 0x0fba, D##K, 1, 0, 0, NONE, M, 2, RM_IMM) \
    X(N##_M32_IMM8, m, P, 2, 0x0fba, D##K, 1, 0, 0, NONE, M, 4, RM_IMM) \
    X(N##_M64_IMM8, m, P, 2, 0x0fba, D##K, 1, 0, 1, NONE, M, 8, RM_IMM)

#define BUSTER_X86_LOCK_ARITHMETIC_FORMS(X, N, m, K) BUSTER_X86_LOCKED_ARITHMETIC_FORMS(X, LOCK_##N, "lock " m, 0xf0, K)
#define BUSTER_X86_LOCK_UNARY_FORMS(X, N, m, opcode8, opcode, K) BUSTER_X86_LOCKED_UNARY_FORMS(X, LOCK_##N, "lock " m, 0xf0, opcode8, opcode, K)
#define BUSTER_X86_LOCK_BIT_TEST_FORMS(X, N, m, opcode, K) BUSTER_X86_LOCKED_BIT_TEST_FORMS(X, LOCK_##N, "lock " m, 0xf0, opcode, K)

// HLE's XACQUIRE (F2) or XRELEASE (F3), with the prefix's name, mnemonic and byte: on every locked form but
// CMPXCHG16B's (which GNU as rejects), and on XCHG to memory, which is locked without the prefix.
#define BUSTER_X86_HLE_FORMS(X, H, h, B) \
    BUSTER_X86_LOCKED_ATOMIC_FORMS(X, H##_LOCK_CMPXCHG, h " lock cmpxchg", 0xf000 | B, 0x0fb0, 0x0fb1) \
    BUSTER_X86_LOCKED_ATOMIC_FORMS(X, H##_LOCK_XADD, h " lock xadd", 0xf000 | B, 0x0fc0, 0x0fc1) \
    X(H##_LOCK_CMPXCHG8B_M64, h " lock cmpxchg8b", 0xf000 | B, 2, 0x0fc7, D1, 0, 0, 0, NONE, M, 8, RM) \
    BUSTER_X86_LOCKED_ARITHMETIC_FORMS(X, H##_LOCK_ADD, h " lock add", 0xf000 | B, 0) \
    BUSTER_X86_LOCKED_ARITHMETIC_FORMS(X, H##_LOCK_OR, h " lock or", 0xf000 | B, 1) \
    BUSTER_X86_LOCKED_ARITHMETIC_FORMS(X, H##_LOCK_ADC, h " lock adc", 0xf000 | B, 2) \
    BUSTER_X86_LOCKED_ARITHMETIC_FORMS(X, H##_LOCK_SBB, h " lock sbb", 0xf000 | B, 3) \
    BUSTER_X86_LOCKED_ARITHMETIC_FORMS(X, H##_LOCK_AND, h " lock and", 0xf000 | B, 4) \
    BUSTER_X86_LOCKED_ARITHMETIC_FORMS(X, H##_LOCK_SUB, h " lock sub", 0xf000 | B, 5) \
    BUSTER_X86_LOCKED_ARITHMETIC_FORMS(X, H##_LOCK_XOR, h " lock xor", 0xf000 | B, 6) \
    BUSTER_X86_LOCKED_UNARY_FORMS(X, H##_LOCK_INC, h " lock inc", 0xf000 | B, 0xfe, 0xff, 0) \
    BUSTER_X86_LOCKED_UNARY_FORMS(X, H##_LOCK_DEC, h " lock dec", 0xf000 | B, 0xfe, 0xff, 1) \
    BUSTER_X86_LOCKED_UNARY_FORMS(X, H##_LOCK_NOT, h " lock not", 0xf000 | B, 0xf6, 0xf7, 2) \
    BUSTER_X86_LOCKED_UNARY_FORMS(X, H##_LOCK_NEG, h " lock neg", 0xf000 | B, 0xf6, 0xf7, 3) \
    BUSTER_X86_LOCKED_BIT_TEST_FORMS(X, H##_LOCK_BTS, h " lock bts", 0xf000 | B, 0xab, 5) \
    BUSTER_X86_LOCKED_BIT_TEST_FORMS(X, H##_LOCK_BTR, h " lock btr", 0xf000 | B, 0xb3, 6) \
    BUSTER_X86_LOCKED_BIT_TEST_FORMS(X, H##_LOCK_BTC, h " lock btc", 0xf000 | B, 0xbb, 7) \
    X(H##_XCHG_M8_R8, h " xchg", B, 1, 0x86, R, 0, 0, 0, GB, M, 1, RM_REG) \
    X(H##_XCHG_M16_R16, h " xchg", B << 8 | 0x66, 1, 0x87, R, 0, 0, 0, GW, M, 2, RM_REG) \
    X(H##_XCHG_M32_R32, h " xchg", B, 1, 0x87, R, 0, 0, 0, GD, M, 4, RM_REG) \
    X(H##_XCHG_M64_R64, h " xchg", B, 1, 0x87, R, 0, 0, 1, GQ, M, 8, RM_REG)

// A string instruction at each operand size (its implicit operands: AL to RAX and [RSI] or [RDI]), with `prefixes`
// (none, REP, REPE or REPNE): b (opcode8), w (66, opcode), d (opcode) and q (REX.W, opcode).
#define BUSTER_X86_STRING_FORMS(X, N, m, prefixes, opcode8, opcode) \
    X(N##_B, m "b", prefixes, 1, opcode8, NO, 0, 0, 0, NONE, NONE, 1, NONE) \
    X(N##_W, m "w", (prefixes) << 8 | 0x66, 1, opcode, NO, 0, 0, 0, NONE, NONE, 2, NONE) \
    X(N##_D, m "d", prefixes, 1, opcode, NO, 0, 0, 0, NONE, NONE, 4, NONE) \
    X(N##_Q, m "q", prefixes, 1, opcode, NO, 0, 0, 1, NONE, NONE, 8, NONE)

// The same for the port I/O strings (ins and outs, through DX), which have no 64-bit size.
#define BUSTER_X86_IO_STRING_FORMS(X, N, m, prefixes, opcode8, opcode) \
    X(N##_B, m "b", prefixes, 1, opcode8, NO, 0, 0, 0, NONE, NONE, 1, NONE) \
    X(N##_W, m "w", (prefixes) << 8 | 0x66, 1, opcode, NO, 0, 0, 0, NONE, NONE, 2, NONE) \
    X(N##_D, m "d", prefixes, 1, opcode, NO, 0, 0, 0, NONE, NONE, 4, NONE)

// LAR and LSL: a segment descriptor's access rights or limit, by the selector in a register or 16 bits of memory, into
// a 16-, 32- or 64-bit register. GNU as drops REX.W from lar rax, rcx (the same result, zero-extended), so the 64-bit
// forms are rex.w with 32-bit register names; R64 names the second operand as uops.info does.
#define BUSTER_X86_DESCRIPTOR_LOAD_FORMS(X, N, m, opcode, R64) \
    X(N##_R16_RM16, m, 0x66, 2, opcode, R, 0, 0, 0, GW, EW, 2, REG_RM) \
    X(N##_R32_R32, m, 0, 2, opcode, R, 0, 0, 0, GD, GD, 4, REG_RM) \
    X(N##_R32_M16, m, 0, 2, opcode, R, 0, 0, 0, GD, M, 2, REG_RM) \
    X(N##_R64_##R64, "rex.w " m, 0, 2, opcode, R, 0, 0, 1, GD, GD, 4, REG_RM) \
    X(N##_R64_M16, "rex.w " m, 0, 2, opcode, R, 0, 0, 1, GD, M, 2, REG_RM)

// SLDT, STR and SMSW (the opcode's digit K): a selector or the machine status word to 16 bits of memory (without 66),
// or to a 16-, 32- or 64-bit register (`m64`: rex.w where GNU as drops REX.W itself, as for LAR).
#define BUSTER_X86_SELECTOR_STORE_FORMS(X, N, m, m64, opcode, K) \
    X(N##_M16, m, 0, 2, opcode, D##K, 0, 0, 0, NONE, M, 2, RM) \
    X(N##_R16, m, 0x66, 2, opcode, D##K, 0, 0, 0, NONE, GW, 2, RM) \
    X(N##_R32, m, 0, 2, opcode, D##K, 0, 0, 0, NONE, GD, 4, RM) \
    X(N##_R64, m64, 0, 2, opcode, D##K, 0, 0, 1, NONE, GQ, 8, RM)

// FSGSBASE: the FS or GS base (the opcode's digit K) from or to a 32- or 64-bit register.
#define BUSTER_X86_SEGMENT_BASE_FORMS(X, N, m, K) \
    X(N##_R32, m, 0xf3, 2, 0x0fae, D##K, 0, 0, 0, NONE, GD, 4, RM) \
    X(N##_R64, m, 0xf3, 2, 0x0fae, D##K, 0, 0, 1, NONE, GQ, 8, RM)

// An XSAVE instruction (its opcode and digit K) and its 64-bit form (REX.W), on a state area that GNU as takes without
// a size.
#define BUSTER_X86_XSAVE_FORMS(X, N, m, opcode, K) \
    X(N##_M, m, 0, 2, opcode, D##K, 0, 0, 0, NONE, M, 0, RM) \
    X(N##64_M, m "64", 0, 2, opcode, D##K, 0, 0, 1, NONE, M, 0, RM)

// Scalar SSE arithmetic: single precision (F3) and double precision (F2).
#define BUSTER_X86_SSE_SCALAR_FORMS(X, N, m, opcode) \
    X(N##SS_X_XM32, m "ss", 0xf3, 2, 0x0f00 + opcode, R, 0, 0, 0, V, XM, 4, REG_RM) \
    X(N##SD_X_XM64, m "sd", 0xf2, 2, 0x0f00 + opcode, R, 0, 0, 0, V, XM, 8, REG_RM)

// An XMM register from a 128-bit XMM register or memory operand.
#define BUSTER_X86_SSE_PACKED_FORM(X, N, m, prefixes, opcode_bytes, opcode) \
    X(N##_X_XM128, m, prefixes, opcode_bytes, opcode, R, 0, 0, 0, V, XM, 16, REG_RM)

// SSE moves with a load form (register from r/m) and a store form (r/m from register).
#define BUSTER_X86_SSE_MOVE_FORMS(X, N, m, prefixes, load, store, bytes) \
    X(N##_X_XM, m, prefixes, 2, load, R, 0, 0, 0, V, XM, bytes, REG_RM) \
    X(N##_XM_X, m, prefixes, 2, store, R, 0, 0, 0, V, XM, bytes, RM_REG)

// Packed single precision (no prefix) and double precision (66).
#define BUSTER_X86_SSE_PS_PD_FORMS(X, N, m, opcode) \
    BUSTER_X86_SSE_PACKED_FORM(X, N##PS, m "ps", 0, 2, 0x0f00 + opcode) \
    BUSTER_X86_SSE_PACKED_FORM(X, N##PD, m "pd", 0x66, 2, 0x0f00 + opcode)

// An XMM register from a 128-bit XMM register or memory operand, with an 8-bit immediate.
#define BUSTER_X86_SSE_PACKED_IMM8_FORM(X, N, m, prefixes, opcode_bytes, opcode) \
    X(N##_X_XM128_IMM8, m, prefixes, opcode_bytes, opcode, R, 1, 0, 0, V, XM, 16, REG_RM_IMM)

// A packed shift by the low quadword of an XMM register or memory operand, and by an 8-bit immediate (66 0F 71, 72 or
// 73 with K the ModRM digit).
#define BUSTER_X86_SSE_SHIFT_FORMS(X, N, m, opcode, immediate_opcode, K) \
    BUSTER_X86_SSE_PACKED_FORM(X, N, m, 0x66, 2, opcode) \
    X(N##_X_IMM8, m, 0x66, 2, immediate_opcode, D##K, 1, 0, 0, NONE, V, 16, RM_IMM)

// SSE4.1's sign or zero extensions of an XMM register's or memory operand's low elements, with the opcode of the first:
// bytes to words (64 bits of them), doublewords (32) and quadwords (16), words to doublewords (64) and quadwords (32),
// and doublewords to quadwords (64).
#define BUSTER_X86_SSE_EXTEND_FORMS(X, N, m, opcode) \
    X(N##BW_X_XM64, m "bw", 0x66, 3, 0x0f3800 + opcode, R, 0, 0, 0, V, XM, 8, REG_RM) \
    X(N##BD_X_XM32, m "bd", 0x66, 3, 0x0f3801 + opcode, R, 0, 0, 0, V, XM, 4, REG_RM) \
    X(N##BQ_X_XM16, m "bq", 0x66, 3, 0x0f3802 + opcode, R, 0, 0, 0, V, XM, 2, REG_RM) \
    X(N##WD_X_XM64, m "wd", 0x66, 3, 0x0f3803 + opcode, R, 0, 0, 0, V, XM, 8, REG_RM) \
    X(N##WQ_X_XM32, m "wq", 0x66, 3, 0x0f3804 + opcode, R, 0, 0, 0, V, XM, 4, REG_RM) \
    X(N##DQ_X_XM64, m "dq", 0x66, 3, 0x0f3805 + opcode, R, 0, 0, 0, V, XM, 8, REG_RM)

// An XMM register's low (12) or high (16) quadword from and to memory: the load, and the store at the next opcode.
#define BUSTER_X86_SSE_HALF_MOVE_FORMS(X, N, m, prefixes, load) \
    X(N##_X_M64, m, prefixes, 2, load, R, 0, 0, 0, V, M, 8, REG_RM) \
    X(N##_M64_X, m, prefixes, 2, load + 1, R, 0, 0, 0, V, M, 8, RM_REG)

// An MMX register from an MMX register or a 64-bit memory operand (SSE's packed forms without the 66 prefix), two
// forms (see the operand roles).
#define BUSTER_X86_MMX_PACKED_FORMS(X, N, m, opcode_bytes, opcode) \
    X(N##_MM_MM, m, 0, opcode_bytes, opcode, R, 0, 0, 0, MM, MM, 8, REG_RM) \
    X(N##_MM_M64, m, 0, opcode_bytes, opcode, R, 0, 0, 0, MM, M, 8, REG_RM)

// The same with an 8-bit immediate.
#define BUSTER_X86_MMX_PACKED_IMM8_FORMS(X, N, m, opcode_bytes, opcode) \
    X(N##_MM_MM_IMM8, m, 0, opcode_bytes, opcode, R, 1, 0, 0, MM, MM, 8, REG_RM_IMM) \
    X(N##_MM_M64_IMM8, m, 0, opcode_bytes, opcode, R, 1, 0, 0, MM, M, 8, REG_RM_IMM)

// A 3DNow! operation: 0F 0F /r with its operation byte (the opcode's last) after the operands, where an 8-bit immediate
// goes (x86_form_has_operation_byte), on an MMX register and an MMX register or 64-bit memory, two forms (see the
// operand roles).
#define BUSTER_X86_3DNOW_FORMS(X, N, m, operation) \
    X(N##_MM_MM, m, 0, 3, 0x0f0f00 | operation, R, 1, 0, 0, MM, MM, 8, REG_RM) \
    X(N##_MM_M64, m, 0, 3, 0x0f0f00 | operation, R, 1, 0, 0, MM, M, 8, REG_RM)

// The unpacks of the low halves, which read 32 bits of memory.
#define BUSTER_X86_MMX_UNPACK_LOW_FORMS(X, N, m, opcode) \
    X(N##_MM_MM, m, 0, 2, opcode, R, 0, 0, 0, MM, MM, 4, REG_RM) \
    X(N##_MM_M32, m, 0, 2, opcode, R, 0, 0, 0, MM, M, 4, REG_RM)

// A packed MMX shift by an MMX register or memory operand, and by an 8-bit immediate (0F 71, 72 or 73 with K the
// ModRM digit).
#define BUSTER_X86_MMX_SHIFT_FORMS(X, N, m, opcode, immediate_opcode, K) \
    BUSTER_X86_MMX_PACKED_FORMS(X, N, m, 2, opcode) \
    X(N##_MM_IMM8, m, 0, 2, immediate_opcode, D##K, 1, 0, 0, NONE, MM, 8, RM_IMM)

// x87 arithmetic, with K the ModRM digit: ST(0) and a 32-bit (D8) or 64-bit (DC) float in memory, or ST(i) (D8 with
// mod 11); and ST(i) as the destination (DC) or with a pop (DE), whose digit KD swaps subtraction and division with
// their reversed forms.
#define BUSTER_X86_X87_ARITHMETIC_FORMS(X, N, m, K, KD) \
    X(N##_M32, m, 0, 1, 0xd8, D##K, 0, 0, 0, NONE, M, 4, RM) \
    X(N##_M64, m, 0, 1, 0xdc, D##K, 0, 0, 0, NONE, M, 8, RM) \
    X(N##_ST0_ST, m, 0, 1, 0xd8, D##K, 0, 0, 0, NONE, ST, 0, ST_RM) \
    X(N##_ST_ST0, m, 0, 1, 0xdc, D##KD, 0, 0, 0, NONE, ST, 0, RM_ST) \
    X(N##P_ST_ST0, m "p", 0, 1, 0xde, D##KD, 0, 0, 0, NONE, ST, 0, RM_ST)

// x87 arithmetic and compares with a 16-bit (DE) or 32-bit (DA) integer in memory.
#define BUSTER_X86_X87_INTEGER_FORMS(X, N, m, K) \
    X(N##_M16, m, 0, 1, 0xde, D##K, 0, 0, 0, NONE, M, 2, RM) \
    X(N##_M32, m, 0, 1, 0xda, D##K, 0, 0, 0, NONE, M, 4, RM)

// FCMOVcc ST(0), ST(i): DA or DB /K with mod 11.
#define BUSTER_X86_X87_FCMOV_FORM(X, N, m, opcode, K) \
    X(N##_ST0_ST, m, 0, 1, opcode, D##K, 0, 0, 0, NONE, ST, 0, ST_RM)

// An x87 instruction without operands: two opcode bytes (the second a ModRM byte with mod 11, in effect).
#define BUSTER_X86_X87_FORM(X, N, m, opcode) X(N, m, 0, 2, opcode, NO, 0, 0, 0, NONE, NONE, 0, NONE)

// The VEX and EVEX forms. Each row is XV(name, mnemonic, family, prefixes, opcode bytes, opcode, encoding, immediate
// bytes, W, L, reg operand, vvvv operand, rm operand, operand bytes, disp8 scale, decorations, syntax):
//   - family: VEX or EVEX; prefixes: the legacy prefix the form implies (66, F3 or F2, which become VEX.pp and
//     EVEX.pp), or 0; opcode: as for the legacy forms, with its escape bytes (0F, 0F 38 or 0F 3A: the opcode map),
//     or for the maps without escape bytes, 4 to 7 (EVEX's 5 and 6, APX's 4 and 7, and VEX's 5 for AMX-FP8 and 7),
//     and XOP's 8, 9 and 0A (VEX rows, whose prefix is then XOP's: 8F in place of C4), the map's number in the
//     escape's place (0x0558: map 5's 58);
//   - W: VEX.W or EVEX.W (0 where it's ignored, as GNU as encodes those); L: VEX.L or EVEX.L'L, 0 (128-bit, scalar
//     or no vector length), 1 (256-bit) or 2 (512-bit);
//   - vvvv operand: the register in VEX.vvvv or EVEX.V'vvvv, or NONE;
//   - operand bytes: the memory operand's size, for printing; for a broadcast, the vector's its element fills (the
//     element's size is the disp8 scale);
//   - disp8 scale: what an 8-bit displacement counts in, EVEX's N (usually the memory operand's size, but the
//     element's for a broadcast, compress or expand); 1 for VEX, and for APX's EVEX forms of general register
//     instructions;
//   - decorations: what an EVEX form's can be (BUSTER_X86_EVEX_*), or NONE.
// Broadcasts, embedded rounding and suppressed exceptions are forms of their own (_M32BCST, _ER, _SAE): they set
// EVEX.b and choose N, so the template has them. So are APX's NF forms (_NF), and its ND forms, whose destination is an
// operand of their own.
#define BUSTER_X86_AVX_MOVE_FORMS(XV, N, m, prefixes, load, store) \
    XV(N##_X_XM128, m, VEX, prefixes, 2, load, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(N##_XM128_X, m, VEX, prefixes, 2, store, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, RM_REG) \
    XV(N##_Y_YM256, m, VEX, prefixes, 2, load, R, 0, 0, 1, Y, NONE, YM, 32, 1, NONE, REG_RM) \
    XV(N##_YM256_Y, m, VEX, prefixes, 2, store, R, 0, 0, 1, Y, NONE, YM, 32, 1, NONE, RM_REG)

// The destination, the first source in VEX.vvvv and the second in ModRM.rm, at 128 and 256 bits.
#define BUSTER_X86_AVX_PACKED_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w) \
    XV(N##_X_X_XM128, m, VEX, prefixes, opcode_bytes, opcode, R, 0, w, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM) \
    XV(N##_Y_Y_YM256, m, VEX, prefixes, opcode_bytes, opcode, R, 0, w, 1, Y, Y, YM, 32, 1, NONE, REG_VVVV_RM)

#define BUSTER_X86_AVX_PACKED_IMMEDIATE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w) \
    XV(N##_X_X_XM128_IMM8, m, VEX, prefixes, opcode_bytes, opcode, R, 1, w, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IMM) \
    XV(N##_Y_Y_YM256_IMM8, m, VEX, prefixes, opcode_bytes, opcode, R, 1, w, 1, Y, Y, YM, 32, 1, NONE, REG_VVVV_RM_IMM)

// The destination and one source.
#define BUSTER_X86_AVX_UNARY_FORMS(XV, N, m, prefixes, opcode_bytes, opcode) \
    XV(N##_X_XM128, m, VEX, prefixes, opcode_bytes, opcode, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(N##_Y_YM256, m, VEX, prefixes, opcode_bytes, opcode, R, 0, 0, 1, Y, NONE, YM, 32, 1, NONE, REG_RM)

// Scalar arithmetic: single precision (F3) and double precision (F2).
#define BUSTER_X86_AVX_SCALAR_FORMS(XV, N, m, opcode) \
    XV(N##SS_X_X_XM32, m "ss", VEX, 0xf3, 2, 0x0f00 + opcode, R, 0, 0, 0, V, V, XM, 4, 1, NONE, REG_VVVV_RM) \
    XV(N##SD_X_X_XM64, m "sd", VEX, 0xf2, 2, 0x0f00 + opcode, R, 0, 0, 0, V, V, XM, 8, 1, NONE, REG_VVVV_RM)

// Shifts by an immediate: the result goes to VEX.vvvv, and ModRM.reg is the operation (K).
#define BUSTER_X86_AVX_SHIFT_FORMS(XV, N, m, opcode, K) \
    XV(N##_X_X_IMM8, m, VEX, 0x66, 2, 0x0f00 + opcode, D##K, 1, 0, 0, NONE, V, V, 16, 1, NONE, VVVV_RM_IMM) \
    XV(N##_Y_Y_IMM8, m, VEX, 0x66, 2, 0x0f00 + opcode, D##K, 1, 0, 1, NONE, Y, Y, 32, 1, NONE, VVVV_RM_IMM)

// FMA3: an operation in its 132, 213 and 231 orders (which operands the product and the sum take: 1 the destination, 2
// VEX.vvvv and 3 ModRM.rm), from the 132 order's opcode (213's adds 0x10 and 231's 0x20); packed, single precision with
// W0 and double precision with W1, at both widths.
#define BUSTER_X86_FMA_PACKED_FORMS(XV, N, m, opcode) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, N##132PS, m "132ps", 0x66, 3, 0x0f3800 + opcode, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, N##132PD, m "132pd", 0x66, 3, 0x0f3800 + opcode, 1) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, N##213PS, m "213ps", 0x66, 3, 0x0f3810 + opcode, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, N##213PD, m "213pd", 0x66, 3, 0x0f3810 + opcode, 1) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, N##231PS, m "231ps", 0x66, 3, 0x0f3820 + opcode, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, N##231PD, m "231pd", 0x66, 3, 0x0f3820 + opcode, 1)

// The same in scalar, at the next opcode.
#define BUSTER_X86_FMA_SCALAR_FORMS(XV, N, m, opcode) \
    XV(N##132SS_X_X_XM32, m "132ss", VEX, 0x66, 3, 0x0f3801 + opcode, R, 0, 0, 0, V, V, XM, 4, 1, NONE, REG_VVVV_RM) \
    XV(N##132SD_X_X_XM64, m "132sd", VEX, 0x66, 3, 0x0f3801 + opcode, R, 0, 1, 0, V, V, XM, 8, 1, NONE, REG_VVVV_RM) \
    XV(N##213SS_X_X_XM32, m "213ss", VEX, 0x66, 3, 0x0f3811 + opcode, R, 0, 0, 0, V, V, XM, 4, 1, NONE, REG_VVVV_RM) \
    XV(N##213SD_X_X_XM64, m "213sd", VEX, 0x66, 3, 0x0f3811 + opcode, R, 0, 1, 0, V, V, XM, 8, 1, NONE, REG_VVVV_RM) \
    XV(N##231SS_X_X_XM32, m "231ss", VEX, 0x66, 3, 0x0f3821 + opcode, R, 0, 0, 0, V, V, XM, 4, 1, NONE, REG_VVVV_RM) \
    XV(N##231SD_X_X_XM64, m "231sd", VEX, 0x66, 3, 0x0f3821 + opcode, R, 0, 1, 0, V, V, XM, 8, 1, NONE, REG_VVVV_RM)

// Shifts by the low quadword of an XMM register or 128-bit memory operand, at both widths.
#define BUSTER_X86_AVX_SHIFT_COUNT_FORMS(XV, N, m, opcode) \
    XV(N##_X_X_XM128, m, VEX, 0x66, 2, 0x0f00 + opcode, R, 0, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM) \
    XV(N##_Y_Y_XM128, m, VEX, 0x66, 2, 0x0f00 + opcode, R, 0, 0, 1, Y, Y, XM, 16, 1, NONE, REG_VVVV_RM)

// The destination and one source, with an 8-bit immediate.
#define BUSTER_X86_AVX_UNARY_IMMEDIATE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode) \
    XV(N##_X_XM128_IMM8, m, VEX, prefixes, opcode_bytes, opcode, R, 1, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM_IMM) \
    XV(N##_Y_YM256_IMM8, m, VEX, prefixes, opcode_bytes, opcode, R, 1, 0, 1, Y, NONE, YM, 32, 1, NONE, REG_RM_IMM)

// Sign or zero extensions as SSE4.1's (BUSTER_X86_SSE_EXTEND_FORMS), from half, a quarter or an eighth of the
// destination's width: at 128 bits, and at 256 from an XMM register or memory operand.
#define BUSTER_X86_AVX_EXTEND_FORMS(XV, N, m, opcode) \
    XV(N##BW_X_XM64, m "bw", VEX, 0x66, 3, 0x0f3800 + opcode, R, 0, 0, 0, V, NONE, XM, 8, 1, NONE, REG_RM) \
    XV(N##BD_X_XM32, m "bd", VEX, 0x66, 3, 0x0f3801 + opcode, R, 0, 0, 0, V, NONE, XM, 4, 1, NONE, REG_RM) \
    XV(N##BQ_X_XM16, m "bq", VEX, 0x66, 3, 0x0f3802 + opcode, R, 0, 0, 0, V, NONE, XM, 2, 1, NONE, REG_RM) \
    XV(N##WD_X_XM64, m "wd", VEX, 0x66, 3, 0x0f3803 + opcode, R, 0, 0, 0, V, NONE, XM, 8, 1, NONE, REG_RM) \
    XV(N##WQ_X_XM32, m "wq", VEX, 0x66, 3, 0x0f3804 + opcode, R, 0, 0, 0, V, NONE, XM, 4, 1, NONE, REG_RM) \
    XV(N##DQ_X_XM64, m "dq", VEX, 0x66, 3, 0x0f3805 + opcode, R, 0, 0, 0, V, NONE, XM, 8, 1, NONE, REG_RM) \
    XV(N##BW_Y_XM128, m "bw", VEX, 0x66, 3, 0x0f3800 + opcode, R, 0, 0, 1, Y, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(N##BD_Y_XM64, m "bd", VEX, 0x66, 3, 0x0f3801 + opcode, R, 0, 0, 1, Y, NONE, XM, 8, 1, NONE, REG_RM) \
    XV(N##BQ_Y_XM32, m "bq", VEX, 0x66, 3, 0x0f3802 + opcode, R, 0, 0, 1, Y, NONE, XM, 4, 1, NONE, REG_RM) \
    XV(N##WD_Y_XM128, m "wd", VEX, 0x66, 3, 0x0f3803 + opcode, R, 0, 0, 1, Y, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(N##WQ_Y_XM64, m "wq", VEX, 0x66, 3, 0x0f3804 + opcode, R, 0, 0, 1, Y, NONE, XM, 8, 1, NONE, REG_RM) \
    XV(N##DQ_Y_XM128, m "dq", VEX, 0x66, 3, 0x0f3805 + opcode, R, 0, 0, 1, Y, NONE, XM, 16, 1, NONE, REG_RM)

// Masked loads (the mask in VEX.vvvv) and stores (memory, mask, source) at both widths.
#define BUSTER_X86_AVX_MASKED_MOVE_FORMS(XV, N, m, load, store, w) \
    XV(N##_X_X_M128, m, VEX, 0x66, 3, load, R, 0, w, 0, V, V, M, 16, 1, NONE, REG_VVVV_RM) \
    XV(N##_Y_Y_M256, m, VEX, 0x66, 3, load, R, 0, w, 1, Y, Y, M, 32, 1, NONE, REG_VVVV_RM) \
    XV(N##_M128_X_X, m, VEX, 0x66, 3, store, R, 0, w, 0, V, V, M, 16, 1, NONE, RM_VVVV_REG) \
    XV(N##_M256_Y_Y, m, VEX, 0x66, 3, store, R, 0, w, 1, Y, Y, M, 32, 1, NONE, RM_VVVV_REG)

// AVX2's gathers: the elements at a VSIB operand's addresses (a base and scale with each element of an XMM or YMM
// index register) whose mask elements, in VEX.vvvv, have their sign bit set, which the instruction clears as it
// goes; 32-bit elements (W0) or 64-bit ones, by indexes of `bits` bits (Intel's vm32x and vm64y name the indexes), at
// 128 and 256 bits. The vector length is the wider of the elements' and the indexes': WIDENING gathers 64-bit elements
// by 32-bit indexes, from an index register half as wide, and NARROWING 32-bit elements by 64-bit indexes.
#define BUSTER_X86_AVX2_GATHER_FORMS(XV, N, m, opcode, w, bits) \
    XV(N##_X_VM##bits##X_X, m, VEX, 0x66, 3, opcode, R, 0, w, 0, V, V, VSIBX, bits / 8, 1, NONE, REG_RM_VVVV) \
    XV(N##_Y_VM##bits##Y_Y, m, VEX, 0x66, 3, opcode, R, 0, w, 1, Y, Y, VSIBY, bits / 8, 1, NONE, REG_RM_VVVV)

#define BUSTER_X86_AVX2_WIDENING_GATHER_FORMS(XV, N, m, opcode) \
    XV(N##_X_VM32X_X, m, VEX, 0x66, 3, opcode, R, 0, 1, 0, V, V, VSIBX, 8, 1, NONE, REG_RM_VVVV) \
    XV(N##_Y_VM32X_Y, m, VEX, 0x66, 3, opcode, R, 0, 1, 1, Y, Y, VSIBX, 8, 1, NONE, REG_RM_VVVV)

#define BUSTER_X86_AVX2_NARROWING_GATHER_FORMS(XV, N, m, opcode) \
    XV(N##_X_VM64X_X, m, VEX, 0x66, 3, opcode, R, 0, 0, 0, V, V, VSIBX, 4, 1, NONE, REG_RM_VVVV) \
    XV(N##_X_VM64Y_X, m, VEX, 0x66, 3, opcode, R, 0, 0, 1, V, V, VSIBY, 4, 1, NONE, REG_RM_VVVV)

// An XMM register's low or high quadword from memory, with the other from VEX.vvvv, and to memory (the next opcode).
#define BUSTER_X86_AVX_HALF_MOVE_FORMS(XV, N, m, prefixes, load) \
    XV(N##_X_X_M64, m, VEX, prefixes, 2, load, R, 0, 0, 0, V, V, M, 8, 1, NONE, REG_VVVV_RM) \
    XV(N##_M64_X, m, VEX, prefixes, 2, load + 1, R, 0, 0, 0, V, NONE, M, 8, 1, NONE, RM_REG)

// Non-temporal stores of 128 and 256 bits.
#define BUSTER_X86_AVX_STREAM_FORMS(XV, N, m, prefixes, opcode) \
    XV(N##_M128_X, m, VEX, prefixes, 2, opcode, R, 0, 0, 0, V, NONE, M, 16, 1, NONE, RM_REG) \
    XV(N##_M256_Y, m, VEX, prefixes, 2, opcode, R, 0, 0, 1, Y, NONE, M, 32, 1, NONE, RM_REG)

// Loads of 128 and 256 bits, from memory only.
#define BUSTER_X86_AVX_LOAD_FORMS(XV, N, m, prefixes, opcode_bytes, opcode) \
    XV(N##_X_M128, m, VEX, prefixes, opcode_bytes, opcode, R, 0, 0, 0, V, NONE, M, 16, 1, NONE, REG_RM) \
    XV(N##_Y_M256, m, VEX, prefixes, opcode_bytes, opcode, R, 0, 0, 1, Y, NONE, M, 32, 1, NONE, REG_RM)

// Four operands, the last two in ModRM.rm and the immediate (/is4) in either order: with W0 the third in ModRM.rm and
// the fourth in the immediate, with W1 the other way around; at 128 and 256 bits (FMA4's and XOP's vpcmov). Then the
// same with a selector in the immediate's low bits (vpermil2ps and vpermil2pd).
#define BUSTER_X86_AVX_IS4_SWAP_FORMS(XV, N, m, prefixes, opcode_bytes, opcode) \
    XV(N##_X_X_XM128_X, m, VEX, prefixes, opcode_bytes, opcode, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IS4) \
    XV(N##_X_X_X_XM128, m, VEX, prefixes, opcode_bytes, opcode, R, 1, 1, 0, V, V, XM, 16, 1, NONE, REG_VVVV_IS4_RM) \
    XV(N##_Y_Y_YM256_Y, m, VEX, prefixes, opcode_bytes, opcode, R, 1, 0, 1, Y, Y, YM, 32, 1, NONE, REG_VVVV_RM_IS4) \
    XV(N##_Y_Y_Y_YM256, m, VEX, prefixes, opcode_bytes, opcode, R, 1, 1, 1, Y, Y, YM, 32, 1, NONE, REG_VVVV_IS4_RM)

#define BUSTER_X86_AVX_IS4_SWAP_SELECTOR_FORMS(XV, N, m, prefixes, opcode_bytes, opcode) \
    XV(N##_X_X_XM128_X_IMM8, m, VEX, prefixes, opcode_bytes, opcode, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IS4_IMM) \
    XV(N##_X_X_X_XM128_IMM8, m, VEX, prefixes, opcode_bytes, opcode, R, 1, 1, 0, V, V, XM, 16, 1, NONE, REG_VVVV_IS4_RM_IMM) \
    XV(N##_Y_Y_YM256_Y_IMM8, m, VEX, prefixes, opcode_bytes, opcode, R, 1, 0, 1, Y, Y, YM, 32, 1, NONE, REG_VVVV_RM_IS4_IMM) \
    XV(N##_Y_Y_Y_YM256_IMM8, m, VEX, prefixes, opcode_bytes, opcode, R, 1, 1, 1, Y, Y, YM, 32, 1, NONE, REG_VVVV_IS4_RM_IMM)

// XOP (VEX rows in maps 8 to 0A): compares of bytes, words, doublewords and quadwords (at `opcode` and the next three)
// with a predicate in the immediate; shifts and rotates of them by a vector of counts, with W0 the source in ModRM.rm
// and the counts in VEX.vvvv and with W1 the other way around (in map 9); and rotates by an immediate (in map 8).
#define BUSTER_X86_XOP_COMPARE_FORMS(XV, N, m, opcode) \
    XV(N##B_X_X_XM128_IMM8, m "b", VEX, 0, 2, opcode, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IMM) \
    XV(N##W_X_X_XM128_IMM8, m "w", VEX, 0, 2, opcode + 1, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IMM) \
    XV(N##D_X_X_XM128_IMM8, m "d", VEX, 0, 2, opcode + 2, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IMM) \
    XV(N##Q_X_X_XM128_IMM8, m "q", VEX, 0, 2, opcode + 3, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IMM)

#define BUSTER_X86_XOP_SHIFT_PAIR_FORMS(XV, N, m, opcode) \
    XV(N##_X_XM128_X, m, VEX, 0, 2, opcode, R, 0, 0, 0, V, V, XM, 16, 1, NONE, REG_RM_VVVV) \
    XV(N##_X_X_XM128, m, VEX, 0, 2, opcode, R, 0, 1, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM)

#define BUSTER_X86_XOP_SHIFT_FORMS(XV, N, m, opcode) \
    BUSTER_X86_XOP_SHIFT_PAIR_FORMS(XV, N##B, m "b", opcode) \
    BUSTER_X86_XOP_SHIFT_PAIR_FORMS(XV, N##W, m "w", opcode + 1) \
    BUSTER_X86_XOP_SHIFT_PAIR_FORMS(XV, N##D, m "d", opcode + 2) \
    BUSTER_X86_XOP_SHIFT_PAIR_FORMS(XV, N##Q, m "q", opcode + 3)

#define BUSTER_X86_XOP_ROTATE_IMMEDIATE_FORMS(XV, N, m, opcode) \
    XV(N##B_X_XM128_IMM8, m "b", VEX, 0, 2, opcode, R, 1, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM_IMM) \
    XV(N##W_X_XM128_IMM8, m "w", VEX, 0, 2, opcode + 1, R, 1, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM_IMM) \
    XV(N##D_X_XM128_IMM8, m "d", VEX, 0, 2, opcode + 2, R, 1, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM_IMM) \
    XV(N##Q_X_XM128_IMM8, m "q", VEX, 0, 2, opcode + 3, R, 1, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM_IMM)

// TBM's bit manipulations (XOP map 9, ModRM.reg the operation, K), into VEX.vvvv, of a 32- or 64-bit (W1) register or
// memory operand.
#define BUSTER_X86_TBM_FORMS(XV, N, m, opcode, K) \
    XV(N##_R32_RM32, m, VEX, 0, 2, opcode, D##K, 0, 0, 0, NONE, GD, ED, 4, 1, NONE, VVVV_RM) \
    XV(N##_R64_RM64, m, VEX, 0, 2, opcode, D##K, 0, 1, 0, NONE, GQ, EQ, 8, 1, NONE, VVVV_RM)

// Four operands, the last a register in the immediate (/is4, see X86Instruction.immediate): the destination, the
// first source in VEX.vvvv, the second in ModRM.rm and the third in the immediate, at 128 and 256 bits (AVX's
// variable blends, whose mask is the third).
#define BUSTER_X86_AVX_IS4_FORMS(XV, N, m, opcode) \
    XV(N##_X_X_XM128_X, m, VEX, 0x66, 3, opcode, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IS4) \
    XV(N##_Y_Y_YM256_Y, m, VEX, 0x66, 3, opcode, R, 1, 0, 1, Y, Y, YM, 32, 1, NONE, REG_VVVV_RM_IS4)

// AMD's FMA4: the product of the first two sources plus or minus the third, the second and the third in either order
// (BUSTER_X86_AVX_IS4_SWAP_FORMS: GNU as uses W1 between registers); packed at both widths, and scalar (`bits`), from
// the packed single-precision form's opcode: double precision at the next, and single and double scalar at the two
// after it.
#define BUSTER_X86_FMA4_PACKED_FORMS(XV, N, m, opcode) BUSTER_X86_AVX_IS4_SWAP_FORMS(XV, N, m, 0x66, 3, opcode)

#define BUSTER_X86_FMA4_SCALAR_FORMS(XV, N, m, opcode, bits) \
    XV(N##_X_X_XM##bits##_X, m, VEX, 0x66, 3, opcode, R, 1, 0, 0, V, V, XM, bits / 8, 1, NONE, REG_VVVV_RM_IS4) \
    XV(N##_X_X_X_XM##bits, m, VEX, 0x66, 3, opcode, R, 1, 1, 0, V, V, XM, bits / 8, 1, NONE, REG_VVVV_IS4_RM)

#define BUSTER_X86_FMA4_FORMS(XV, N, m, opcode) \
    BUSTER_X86_FMA4_PACKED_FORMS(XV, N##PS, m "ps", opcode) \
    BUSTER_X86_FMA4_PACKED_FORMS(XV, N##PD, m "pd", opcode + 1) \
    BUSTER_X86_FMA4_SCALAR_FORMS(XV, N##SS, m "ss", opcode + 2, 32) \
    BUSTER_X86_FMA4_SCALAR_FORMS(XV, N##SD, m "sd", opcode + 3, 64)

// The opmask instructions at their four widths: bytes (66, W0), words (W0), doublewords (66, W1) and quadwords
// (W1). Two sources (VEX.L1, the first in VEX.vvvv) or one (VEX.L0); the shifts by an immediate are 66 0F 3A, at
// `opcode` for bytes and words and the next opcode for doublewords and quadwords.
#define BUSTER_X86_MASK_BINARY_FORMS(XV, N, m, opcode) \
    XV(N##B_K_K_K, m "b", VEX, 0x66, 2, opcode, R, 0, 0, 1, K, K, K, 0, 1, NONE, REG_VVVV_RM) \
    XV(N##W_K_K_K, m "w", VEX, 0, 2, opcode, R, 0, 0, 1, K, K, K, 0, 1, NONE, REG_VVVV_RM) \
    XV(N##D_K_K_K, m "d", VEX, 0x66, 2, opcode, R, 0, 1, 1, K, K, K, 0, 1, NONE, REG_VVVV_RM) \
    XV(N##Q_K_K_K, m "q", VEX, 0, 2, opcode, R, 0, 1, 1, K, K, K, 0, 1, NONE, REG_VVVV_RM)

#define BUSTER_X86_MASK_UNARY_FORMS(XV, N, m, opcode) \
    XV(N##B_K_K, m "b", VEX, 0x66, 2, opcode, R, 0, 0, 0, K, NONE, K, 0, 1, NONE, REG_RM) \
    XV(N##W_K_K, m "w", VEX, 0, 2, opcode, R, 0, 0, 0, K, NONE, K, 0, 1, NONE, REG_RM) \
    XV(N##D_K_K, m "d", VEX, 0x66, 2, opcode, R, 0, 1, 0, K, NONE, K, 0, 1, NONE, REG_RM) \
    XV(N##Q_K_K, m "q", VEX, 0, 2, opcode, R, 0, 1, 0, K, NONE, K, 0, 1, NONE, REG_RM)

#define BUSTER_X86_MASK_SHIFT_FORMS(XV, N, m, opcode) \
    XV(N##B_K_K_IMM8, m "b", VEX, 0x66, 3, opcode, R, 1, 0, 0, K, NONE, K, 0, 1, NONE, REG_RM_IMM) \
    XV(N##W_K_K_IMM8, m "w", VEX, 0x66, 3, opcode, R, 1, 1, 0, K, NONE, K, 0, 1, NONE, REG_RM_IMM) \
    XV(N##D_K_K_IMM8, m "d", VEX, 0x66, 3, opcode + 1, R, 1, 0, 0, K, NONE, K, 0, 1, NONE, REG_RM_IMM) \
    XV(N##Q_K_K_IMM8, m "q", VEX, 0x66, 3, opcode + 1, R, 1, 1, 0, K, NONE, K, 0, 1, NONE, REG_RM_IMM)

// The opmask moves at the four widths: from an opmask register or memory, to memory, and from and to a general
// register. `family` is VEX, or EVEX for APX's forms, whose `suffix` is _EVEX (the VEX forms have the names).
#define BUSTER_X86_KMOV_FORMS(XV, family, suffix) \
    XV(KMOVB_K_K##suffix, "kmovb", family, 0x66, 2, 0x0f90, R, 0, 0, 0, K, NONE, K, 1, 1, NONE, REG_RM) \
    XV(KMOVB_K_M8##suffix, "kmovb", family, 0x66, 2, 0x0f90, R, 0, 0, 0, K, NONE, M, 1, 1, NONE, REG_RM) \
    XV(KMOVB_M8_K##suffix, "kmovb", family, 0x66, 2, 0x0f91, R, 0, 0, 0, K, NONE, M, 1, 1, NONE, RM_REG) \
    XV(KMOVB_K_R32##suffix, "kmovb", family, 0x66, 2, 0x0f92, R, 0, 0, 0, K, NONE, GD, 4, 1, NONE, REG_RM) \
    XV(KMOVB_R32_K##suffix, "kmovb", family, 0x66, 2, 0x0f93, R, 0, 0, 0, GD, NONE, K, 4, 1, NONE, REG_RM) \
    XV(KMOVW_K_K##suffix, "kmovw", family, 0, 2, 0x0f90, R, 0, 0, 0, K, NONE, K, 2, 1, NONE, REG_RM) \
    XV(KMOVW_K_M16##suffix, "kmovw", family, 0, 2, 0x0f90, R, 0, 0, 0, K, NONE, M, 2, 1, NONE, REG_RM) \
    XV(KMOVW_M16_K##suffix, "kmovw", family, 0, 2, 0x0f91, R, 0, 0, 0, K, NONE, M, 2, 1, NONE, RM_REG) \
    XV(KMOVW_K_R32##suffix, "kmovw", family, 0, 2, 0x0f92, R, 0, 0, 0, K, NONE, GD, 4, 1, NONE, REG_RM) \
    XV(KMOVW_R32_K##suffix, "kmovw", family, 0, 2, 0x0f93, R, 0, 0, 0, GD, NONE, K, 4, 1, NONE, REG_RM) \
    XV(KMOVD_K_K##suffix, "kmovd", family, 0x66, 2, 0x0f90, R, 0, 1, 0, K, NONE, K, 4, 1, NONE, REG_RM) \
    XV(KMOVD_K_M32##suffix, "kmovd", family, 0x66, 2, 0x0f90, R, 0, 1, 0, K, NONE, M, 4, 1, NONE, REG_RM) \
    XV(KMOVD_M32_K##suffix, "kmovd", family, 0x66, 2, 0x0f91, R, 0, 1, 0, K, NONE, M, 4, 1, NONE, RM_REG) \
    XV(KMOVD_K_R32##suffix, "kmovd", family, 0xf2, 2, 0x0f92, R, 0, 0, 0, K, NONE, GD, 4, 1, NONE, REG_RM) \
    XV(KMOVD_R32_K##suffix, "kmovd", family, 0xf2, 2, 0x0f93, R, 0, 0, 0, GD, NONE, K, 4, 1, NONE, REG_RM) \
    XV(KMOVQ_K_K##suffix, "kmovq", family, 0, 2, 0x0f90, R, 0, 1, 0, K, NONE, K, 8, 1, NONE, REG_RM) \
    XV(KMOVQ_K_M64##suffix, "kmovq", family, 0, 2, 0x0f90, R, 0, 1, 0, K, NONE, M, 8, 1, NONE, REG_RM) \
    XV(KMOVQ_M64_K##suffix, "kmovq", family, 0, 2, 0x0f91, R, 0, 1, 0, K, NONE, M, 8, 1, NONE, RM_REG) \
    XV(KMOVQ_K_R64##suffix, "kmovq", family, 0xf2, 2, 0x0f92, R, 0, 1, 0, K, NONE, GQ, 8, 1, NONE, REG_RM) \
    XV(KMOVQ_R64_K##suffix, "kmovq", family, 0xf2, 2, 0x0f93, R, 0, 1, 0, GQ, NONE, K, 8, 1, NONE, REG_RM)

// BMI1's and BMI2's instructions on general registers that set the flags: andn, bextr, blsi, blsmsk, blsr and bzhi.
// `family` is VEX, or EVEX for APX's forms, whose `suffix` is _EVEX (the VEX forms have the names), or _NF with `nf`
// NF for those that leave the flags (and NONE otherwise).
#define BUSTER_X86_BMI_FLAG_FORMS(XV, family, nf, suffix) \
    XV(ANDN_R32_R32_RM32##suffix, "andn", family, 0, 3, 0x0f38f2, R, 0, 0, 0, GD, GD, ED, 4, 1, nf, REG_VVVV_RM) \
    XV(ANDN_R64_R64_RM64##suffix, "andn", family, 0, 3, 0x0f38f2, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, nf, REG_VVVV_RM) \
    XV(BEXTR_R32_RM32_R32##suffix, "bextr", family, 0, 3, 0x0f38f7, R, 0, 0, 0, GD, GD, ED, 4, 1, nf, REG_RM_VVVV) \
    XV(BEXTR_R64_RM64_R64##suffix, "bextr", family, 0, 3, 0x0f38f7, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, nf, REG_RM_VVVV) \
    XV(BLSI_R32_RM32##suffix, "blsi", family, 0, 3, 0x0f38f3, D3, 0, 0, 0, NONE, GD, ED, 4, 1, nf, VVVV_RM) \
    XV(BLSI_R64_RM64##suffix, "blsi", family, 0, 3, 0x0f38f3, D3, 0, 1, 0, NONE, GQ, EQ, 8, 1, nf, VVVV_RM) \
    XV(BLSMSK_R32_RM32##suffix, "blsmsk", family, 0, 3, 0x0f38f3, D2, 0, 0, 0, NONE, GD, ED, 4, 1, nf, VVVV_RM) \
    XV(BLSMSK_R64_RM64##suffix, "blsmsk", family, 0, 3, 0x0f38f3, D2, 0, 1, 0, NONE, GQ, EQ, 8, 1, nf, VVVV_RM) \
    XV(BLSR_R32_RM32##suffix, "blsr", family, 0, 3, 0x0f38f3, D1, 0, 0, 0, NONE, GD, ED, 4, 1, nf, VVVV_RM) \
    XV(BLSR_R64_RM64##suffix, "blsr", family, 0, 3, 0x0f38f3, D1, 0, 1, 0, NONE, GQ, EQ, 8, 1, nf, VVVV_RM) \
    XV(BZHI_R32_RM32_R32##suffix, "bzhi", family, 0, 3, 0x0f38f5, R, 0, 0, 0, GD, GD, ED, 4, 1, nf, REG_RM_VVVV) \
    XV(BZHI_R64_RM64_R64##suffix, "bzhi", family, 0, 3, 0x0f38f5, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, nf, REG_RM_VVVV)

// The rest of BMI2's, which don't: pdep, pext, mulx, rorx and the shifts.
#define BUSTER_X86_BMI2_FORMS(XV, family, suffix) \
    XV(PDEP_R32_R32_RM32##suffix, "pdep", family, 0xf2, 3, 0x0f38f5, R, 0, 0, 0, GD, GD, ED, 4, 1, NONE, REG_VVVV_RM) \
    XV(PDEP_R64_R64_RM64##suffix, "pdep", family, 0xf2, 3, 0x0f38f5, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, NONE, REG_VVVV_RM) \
    XV(PEXT_R32_R32_RM32##suffix, "pext", family, 0xf3, 3, 0x0f38f5, R, 0, 0, 0, GD, GD, ED, 4, 1, NONE, REG_VVVV_RM) \
    XV(PEXT_R64_R64_RM64##suffix, "pext", family, 0xf3, 3, 0x0f38f5, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, NONE, REG_VVVV_RM) \
    XV(MULX_R32_R32_RM32##suffix, "mulx", family, 0xf2, 3, 0x0f38f6, R, 0, 0, 0, GD, GD, ED, 4, 1, NONE, REG_VVVV_RM) \
    XV(MULX_R64_R64_RM64##suffix, "mulx", family, 0xf2, 3, 0x0f38f6, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, NONE, REG_VVVV_RM) \
    XV(RORX_R32_RM32_IMM8##suffix, "rorx", family, 0xf2, 3, 0x0f3af0, R, 1, 0, 0, GD, NONE, ED, 4, 1, NONE, REG_RM_IMM) \
    XV(RORX_R64_RM64_IMM8##suffix, "rorx", family, 0xf2, 3, 0x0f3af0, R, 1, 1, 0, GQ, NONE, EQ, 8, 1, NONE, REG_RM_IMM) \
    XV(SARX_R32_RM32_R32##suffix, "sarx", family, 0xf3, 3, 0x0f38f7, R, 0, 0, 0, GD, GD, ED, 4, 1, NONE, REG_RM_VVVV) \
    XV(SARX_R64_RM64_R64##suffix, "sarx", family, 0xf3, 3, 0x0f38f7, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, NONE, REG_RM_VVVV) \
    XV(SHLX_R32_RM32_R32##suffix, "shlx", family, 0x66, 3, 0x0f38f7, R, 0, 0, 0, GD, GD, ED, 4, 1, NONE, REG_RM_VVVV) \
    XV(SHLX_R64_RM64_R64##suffix, "shlx", family, 0x66, 3, 0x0f38f7, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, NONE, REG_RM_VVVV) \
    XV(SHRX_R32_RM32_R32##suffix, "shrx", family, 0xf2, 3, 0x0f38f7, R, 0, 0, 0, GD, GD, ED, 4, 1, NONE, REG_RM_VVVV) \
    XV(SHRX_R64_RM64_R64##suffix, "shrx", family, 0xf2, 3, 0x0f38f7, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, NONE, REG_RM_VVVV)

// The MSR instructions with an immediate MSR index (an imm32), in map 7 (its number in the escape's place). `family` is
// VEX, or EVEX for APX's forms, whose `suffix` is _EVEX (the VEX forms have the names).
#define BUSTER_X86_MSR_IMMEDIATE_FORMS(XV, family, suffix) \
    XV(RDMSR_R64_IMM32##suffix, "rdmsr", family, 0xf2, 2, 0x07f6, D0, 4, 0, 0, NONE, NONE, GQ, 8, 1, NONE, RM_IMM) \
    XV(WRMSRNS_IMM32_R64##suffix, "wrmsrns", family, 0xf3, 2, 0x07f6, D0, 4, 0, 0, NONE, NONE, GQ, 8, 1, NONE, IMM_RM) \
    XV(URDMSR_R64_IMM32##suffix, "urdmsr", family, 0xf2, 2, 0x07f8, D0, 4, 0, 0, NONE, NONE, GQ, 8, 1, NONE, RM_IMM) \
    XV(UWRMSR_IMM32_R64##suffix, "uwrmsr", family, 0xf3, 2, 0x07f8, D0, 4, 0, 0, NONE, NONE, GQ, 8, 1, NONE, IMM_RM)

// CMPccXADD: compare a 32- or 64-bit memory operand with reg, add vvvv to it if the condition (K) holds, and return
// its old value in reg (VEX.128.66.0F38 E0+K). `family` is VEX, or EVEX for APX's forms, whose `suffix` is _EVEX (the
// VEX forms have the names).
#define BUSTER_X86_CMPCCXADD_FORMS(XV, CC, cc, K, family, suffix) \
    XV(CMP##CC##XADD_M32_R32_R32##suffix, "cmp" cc "xadd", family, 0x66, 3, 0x0f38e0 + K, R, 0, 0, 0, GD, GD, M, 4, 1, NONE, RM_REG_VVVV) \
    XV(CMP##CC##XADD_M64_R64_R64##suffix, "cmp" cc "xadd", family, 0x66, 3, 0x0f38e0 + K, R, 0, 1, 0, GQ, GQ, M, 8, 1, NONE, RM_REG_VVVV)

// The same for every condition.
#define BUSTER_X86_CMPCCXADD_CONDITION_FORMS(XV, family, suffix) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, O, "o", 0, family, suffix) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, NO, "no", 1, family, suffix) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, B, "b", 2, family, suffix) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, NB, "nb", 3, family, suffix) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, Z, "z", 4, family, suffix) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, NZ, "nz", 5, family, suffix) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, BE, "be", 6, family, suffix) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, NBE, "nbe", 7, family, suffix) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, S, "s", 8, family, suffix) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, NS, "ns", 9, family, suffix) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, P, "p", 10, family, suffix) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, NP, "np", 11, family, suffix) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, L, "l", 12, family, suffix) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, NL, "nl", 13, family, suffix) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, LE, "le", 14, family, suffix) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, NLE, "nle", 15, family, suffix)

// AVX-512: loads (merging or zeroing) and stores (merging into memory) of 512 bits.
#define BUSTER_X86_AVX512_MOVE_FORMS(XV, N, m, prefixes, w, load, store) \
    XV(N##_Z_ZM512, m, EVEX, prefixes, 2, load, R, 0, w, 2, Z, NONE, ZM, 64, 64, KZ, REG_RM) \
    XV(N##_ZM512_Z, m, EVEX, prefixes, 2, store, R, 0, w, 2, Z, NONE, ZM, 64, 64, KZ, RM_REG)

// Non-temporal stores of 512, 128 and 256 bits (whose VEX forms have the names without _EVEX).
#define BUSTER_X86_AVX512_STREAM_FORMS(XV, N, m, prefixes, opcode, w) \
    XV(N##_M512_Z, m, EVEX, prefixes, 2, opcode, R, 0, w, 2, Z, NONE, M, 64, 64, NONE, RM_REG) \
    XV(N##_M128_X_EVEX, m, EVEX, prefixes, 2, opcode, R, 0, w, 0, V, NONE, M, 16, 16, NONE, RM_REG) \
    XV(N##_M256_Y_EVEX, m, EVEX, prefixes, 2, opcode, R, 0, w, 1, Y, NONE, M, 32, 32, NONE, RM_REG)

// Compresses (into a register or memory) and expands of 32-bit or 64-bit (W1) elements, at 512, 128 and 256 bits;
// N is the element's.
#define BUSTER_X86_AVX512_COMPRESS_FORMS(XV, N, m, opcode, w, bits) \
    XV(N##_ZM512_Z, m, EVEX, 0x66, 3, opcode, R, 0, w, 2, Z, NONE, ZM, 64, bits / 8, KZ, RM_REG) \
    XV(N##_XM128_X, m, EVEX, 0x66, 3, opcode, R, 0, w, 0, V, NONE, XM, 16, bits / 8, KZ, RM_REG) \
    XV(N##_YM256_Y, m, EVEX, 0x66, 3, opcode, R, 0, w, 1, Y, NONE, YM, 32, bits / 8, KZ, RM_REG)

#define BUSTER_X86_AVX512_EXPAND_FORMS(XV, N, m, opcode, w, bits) \
    XV(N##_Z_ZM512, m, EVEX, 0x66, 3, opcode, R, 0, w, 2, Z, NONE, ZM, 64, bits / 8, KZ, REG_RM) \
    XV(N##_X_XM128, m, EVEX, 0x66, 3, opcode, R, 0, w, 0, V, NONE, XM, 16, bits / 8, KZ, REG_RM) \
    XV(N##_Y_YM256, m, EVEX, 0x66, 3, opcode, R, 0, w, 1, Y, NONE, YM, 32, bits / 8, KZ, REG_RM)

// AVX-512's gathers and scatters: the elements at a VSIB operand's addresses (with an XMM, YMM or ZMM index register,
// 0 to 31: its bit 4 is EVEX.V', and vvvv is unused) under an opmask, which can't be K0 and which the instruction
// clears as it goes; 32-bit elements (W0) or 64-bit ones, N their size, by indexes of `bits` bits, at 512, 128 and
// 256 bits; and as AVX2's, WIDENING ones of 64-bit elements by 32-bit indexes and NARROWING ones of 32-bit elements by
// 64-bit indexes. A scatter's operands are the memory and the source (in ModRM.reg).
#define BUSTER_X86_AVX512_GATHER_FORMS(XV, N, m, opcode, w, bits) \
    XV(N##_Z_VM##bits##Z, m, EVEX, 0x66, 3, opcode, R, 0, w, 2, Z, NONE, VSIBZ, bits / 8, bits / 8, K, REG_RM) \
    XV(N##_X_VM##bits##X, m, EVEX, 0x66, 3, opcode, R, 0, w, 0, V, NONE, VSIBX, bits / 8, bits / 8, K, REG_RM) \
    XV(N##_Y_VM##bits##Y, m, EVEX, 0x66, 3, opcode, R, 0, w, 1, Y, NONE, VSIBY, bits / 8, bits / 8, K, REG_RM)

#define BUSTER_X86_AVX512_WIDENING_GATHER_FORMS(XV, N, m, opcode) \
    XV(N##_Z_VM32Y, m, EVEX, 0x66, 3, opcode, R, 0, 1, 2, Z, NONE, VSIBY, 8, 8, K, REG_RM) \
    XV(N##_X_VM32X, m, EVEX, 0x66, 3, opcode, R, 0, 1, 0, V, NONE, VSIBX, 8, 8, K, REG_RM) \
    XV(N##_Y_VM32X, m, EVEX, 0x66, 3, opcode, R, 0, 1, 1, Y, NONE, VSIBX, 8, 8, K, REG_RM)

#define BUSTER_X86_AVX512_NARROWING_GATHER_FORMS(XV, N, m, opcode) \
    XV(N##_Y_VM64Z, m, EVEX, 0x66, 3, opcode, R, 0, 0, 2, Y, NONE, VSIBZ, 4, 4, K, REG_RM) \
    XV(N##_X_VM64X, m, EVEX, 0x66, 3, opcode, R, 0, 0, 0, V, NONE, VSIBX, 4, 4, K, REG_RM) \
    XV(N##_X_VM64Y, m, EVEX, 0x66, 3, opcode, R, 0, 0, 1, V, NONE, VSIBY, 4, 4, K, REG_RM)

#define BUSTER_X86_AVX512_SCATTER_FORMS(XV, N, m, opcode, w, bits) \
    XV(N##_VM##bits##Z_Z, m, EVEX, 0x66, 3, opcode, R, 0, w, 2, Z, NONE, VSIBZ, bits / 8, bits / 8, K, RM_REG) \
    XV(N##_VM##bits##X_X, m, EVEX, 0x66, 3, opcode, R, 0, w, 0, V, NONE, VSIBX, bits / 8, bits / 8, K, RM_REG) \
    XV(N##_VM##bits##Y_Y, m, EVEX, 0x66, 3, opcode, R, 0, w, 1, Y, NONE, VSIBY, bits / 8, bits / 8, K, RM_REG)

#define BUSTER_X86_AVX512_WIDENING_SCATTER_FORMS(XV, N, m, opcode) \
    XV(N##_VM32Y_Z, m, EVEX, 0x66, 3, opcode, R, 0, 1, 2, Z, NONE, VSIBY, 8, 8, K, RM_REG) \
    XV(N##_VM32X_X, m, EVEX, 0x66, 3, opcode, R, 0, 1, 0, V, NONE, VSIBX, 8, 8, K, RM_REG) \
    XV(N##_VM32X_Y, m, EVEX, 0x66, 3, opcode, R, 0, 1, 1, Y, NONE, VSIBX, 8, 8, K, RM_REG)

#define BUSTER_X86_AVX512_NARROWING_SCATTER_FORMS(XV, N, m, opcode) \
    XV(N##_VM64Z_Y, m, EVEX, 0x66, 3, opcode, R, 0, 0, 2, Y, NONE, VSIBZ, 4, 4, K, RM_REG) \
    XV(N##_VM64X_X, m, EVEX, 0x66, 3, opcode, R, 0, 0, 0, V, NONE, VSIBX, 4, 4, K, RM_REG) \
    XV(N##_VM64Y_X, m, EVEX, 0x66, 3, opcode, R, 0, 0, 1, V, NONE, VSIBY, 4, 4, K, RM_REG)

// An AMX tile load: into ModRM.reg's tile (TMM), or AMX-TRANSPOSE's pair of them (TMMP), through a SIB memory operand.
#define BUSTER_X86_AMX_LOAD_FORM(XV, N, m, reg, prefixes, opcode_bytes, opcode, family, suffix) \
    XV(N##_TMM_SIBMEM##suffix, m, family, prefixes, opcode_bytes, opcode, R, 0, 0, 0, reg, NONE, SIBMEM, 0, 1, NONE, REG_RM)

// AMX's loads and stores: the tile configuration's (64 bytes, which GNU as takes without a size), the tiles' through a
// SIB memory operand (whose index is the stride), and AMX-TRANSPOSE's loads of a pair of tiles (the read-shared ones in
// map 5). `family` is VEX, or EVEX for APX's forms, whose `suffix` is _EVEX (the VEX forms have the names).
#define BUSTER_X86_AMX_MEMORY_FORMS(XV, family, suffix) \
    XV(LDTILECFG_M512##suffix, "ldtilecfg", family, 0, 3, 0x0f3849, D0, 0, 0, 0, NONE, NONE, M, 0, 1, NONE, RM) \
    XV(STTILECFG_M512##suffix, "sttilecfg", family, 0x66, 3, 0x0f3849, D0, 0, 0, 0, NONE, NONE, M, 0, 1, NONE, RM) \
    BUSTER_X86_AMX_LOAD_FORM(XV, TILELOADD, "tileloadd", TMM, 0xf2, 3, 0x0f384b, family, suffix) \
    BUSTER_X86_AMX_LOAD_FORM(XV, TILELOADDT1, "tileloaddt1", TMM, 0x66, 3, 0x0f384b, family, suffix) \
    XV(TILESTORED_SIBMEM_TMM##suffix, "tilestored", family, 0xf3, 3, 0x0f384b, R, 0, 0, 0, TMM, NONE, SIBMEM, 0, 1, NONE, RM_REG) \
    BUSTER_X86_AMX_LOAD_FORM(XV, TILELOADDRS, "tileloaddrs", TMM, 0xf2, 3, 0x0f384a, family, suffix) \
    BUSTER_X86_AMX_LOAD_FORM(XV, TILELOADDRST1, "tileloaddrst1", TMM, 0x66, 3, 0x0f384a, family, suffix) \
    BUSTER_X86_AMX_LOAD_FORM(XV, T2RPNTLVWZ0, "t2rpntlvwz0", TMMP, 0, 3, 0x0f386e, family, suffix) \
    BUSTER_X86_AMX_LOAD_FORM(XV, T2RPNTLVWZ0T1, "t2rpntlvwz0t1", TMMP, 0, 3, 0x0f386f, family, suffix) \
    BUSTER_X86_AMX_LOAD_FORM(XV, T2RPNTLVWZ1, "t2rpntlvwz1", TMMP, 0x66, 3, 0x0f386e, family, suffix) \
    BUSTER_X86_AMX_LOAD_FORM(XV, T2RPNTLVWZ1T1, "t2rpntlvwz1t1", TMMP, 0x66, 3, 0x0f386f, family, suffix) \
    BUSTER_X86_AMX_LOAD_FORM(XV, T2RPNTLVWZ0RS, "t2rpntlvwz0rs", TMMP, 0, 2, 0x05f8, family, suffix) \
    BUSTER_X86_AMX_LOAD_FORM(XV, T2RPNTLVWZ0RST1, "t2rpntlvwz0rst1", TMMP, 0, 2, 0x05f9, family, suffix) \
    BUSTER_X86_AMX_LOAD_FORM(XV, T2RPNTLVWZ1RS, "t2rpntlvwz1rs", TMMP, 0x66, 2, 0x05f8, family, suffix) \
    BUSTER_X86_AMX_LOAD_FORM(XV, T2RPNTLVWZ1RST1, "t2rpntlvwz1rst1", TMMP, 0x66, 2, 0x05f9, family, suffix)

// An AMX product of tiles (a dot product, or a complex or TF32 multiplication): into ModRM.reg's tile, of ModRM.rm's
// and VEX.vvvv's.
#define BUSTER_X86_AMX_TILE_FORM(XV, N, m, prefixes, opcode_bytes, opcode) \
    XV(N##_TMM_TMM_TMM, m, VEX, prefixes, opcode_bytes, opcode, R, 0, 0, 0, TMM, TMM, TMM, 0, 1, NONE, REG_RM_VVVV)

// AVX512PF's prefetching gathers and scatters: a VSIB operand (its elements' and indexes' sizes as a gather's) under
// an opmask, which they need, and the hint in ModRM.reg: /1 and /2 gather into the L1 and L2 caches, and /5 and /6 for
// a scatter.
#define BUSTER_X86_AVX512PF_FORMS(XV, N, m, digit) \
    XV(N##DPS_VM32Z, m "dps", EVEX, 0x66, 3, 0x0f38c6, digit, 0, 0, 2, NONE, NONE, VSIBZ, 4, 4, K, RM) \
    XV(N##DPD_VM32Y, m "dpd", EVEX, 0x66, 3, 0x0f38c6, digit, 0, 1, 2, NONE, NONE, VSIBY, 8, 8, K, RM) \
    XV(N##QPS_VM64Z, m "qps", EVEX, 0x66, 3, 0x0f38c7, digit, 0, 0, 2, NONE, NONE, VSIBZ, 4, 4, K, RM) \
    XV(N##QPD_VM64Z, m "qpd", EVEX, 0x66, 3, 0x0f38c7, digit, 0, 1, 2, NONE, NONE, VSIBZ, 8, 8, K, RM)

// AMX-AVX512: a tile's row (in ModRM.rm) into a ZMM register, the row's number an 8-bit immediate (in map 3) or in a
// 32-bit register in EVEX.vvvv (in map 2), without an opmask.
#define BUSTER_X86_AMX_ROW_FORMS(XV, N, m, immediate_prefixes, immediate_opcode, register_prefixes, register_opcode) \
    XV(N##_Z_TMM_IMM8, m, EVEX, immediate_prefixes, 3, immediate_opcode, R, 1, 0, 2, Z, NONE, TMM, 64, 1, NONE, REG_RM_IMM) \
    XV(N##_Z_TMM_R32, m, EVEX, register_prefixes, 3, register_opcode, R, 0, 0, 2, Z, GD, TMM, 64, 1, NONE, REG_RM_VVVV)

// An XMM register's low or high quadword (two single-precision elements, or with W1 a double-precision one) from
// memory, with the other from EVEX.V'vvvv, and to memory (the next opcode), as the VEX forms.
#define BUSTER_X86_AVX512_HALF_MOVE_FORMS(XV, N, m, prefixes, load, w) \
    XV(N##_X_X_M64_EVEX, m, EVEX, prefixes, 2, load, R, 0, w, 0, V, V, M, 8, 8, NONE, REG_VVVV_RM) \
    XV(N##_M64_X_EVEX, m, EVEX, prefixes, 2, load + 1, R, 0, w, 0, V, NONE, M, 8, 8, NONE, RM_REG)

// Inserts into and extracts from a ZMM register of 128 bits (32x4, a YMM register's too) and 256 bits (64x4, W1): T
// is F for the floating-point ones and I for the integer ones, whose opcodes are 0x20 more.
#define BUSTER_X86_AVX512_INSERT_EXTRACT_FORMS(XV, T, t, opcode) \
    XV(VINSERT##T##32X4_Z_Z_XM128_IMM8, "vinsert" t "32x4", EVEX, 0x66, 3, opcode, R, 1, 0, 2, Z, Z, XM, 16, 16, KZ, REG_VVVV_RM_IMM) \
    XV(VEXTRACT##T##32X4_XM128_Z_IMM8, "vextract" t "32x4", EVEX, 0x66, 3, opcode + 1, R, 1, 0, 2, Z, NONE, XM, 16, 16, KZ, RM_REG_IMM) \
    XV(VINSERT##T##32X4_Y_Y_XM128_IMM8, "vinsert" t "32x4", EVEX, 0x66, 3, opcode, R, 1, 0, 1, Y, Y, XM, 16, 16, KZ, REG_VVVV_RM_IMM) \
    XV(VEXTRACT##T##32X4_XM128_Y_IMM8, "vextract" t "32x4", EVEX, 0x66, 3, opcode + 1, R, 1, 0, 1, Y, NONE, XM, 16, 16, KZ, RM_REG_IMM) \
    XV(VINSERT##T##64X4_Z_Z_YM256_IMM8, "vinsert" t "64x4", EVEX, 0x66, 3, opcode + 2, R, 1, 1, 2, Z, Z, YM, 32, 32, KZ, REG_VVVV_RM_IMM) \
    XV(VEXTRACT##T##64X4_YM256_Z_IMM8, "vextract" t "64x4", EVEX, 0x66, 3, opcode + 3, R, 1, 1, 2, Z, NONE, YM, 32, 32, KZ, RM_REG_IMM)

// AVX512DQ's: 128 bits as two quadwords (64x2, W1, at the 32x4 opcodes) into or from ZMM and YMM registers, and 256 bits
// as eight doublewords (32x8, W0, at the 64x4 ones).
#define BUSTER_X86_AVX512DQ_INSERT_EXTRACT_FORMS(XV, T, t, opcode) \
    XV(VINSERT##T##64X2_Z_Z_XM128_IMM8, "vinsert" t "64x2", EVEX, 0x66, 3, opcode, R, 1, 1, 2, Z, Z, XM, 16, 16, KZ, REG_VVVV_RM_IMM) \
    XV(VEXTRACT##T##64X2_XM128_Z_IMM8, "vextract" t "64x2", EVEX, 0x66, 3, opcode + 1, R, 1, 1, 2, Z, NONE, XM, 16, 16, KZ, RM_REG_IMM) \
    XV(VINSERT##T##64X2_Y_Y_XM128_IMM8, "vinsert" t "64x2", EVEX, 0x66, 3, opcode, R, 1, 1, 1, Y, Y, XM, 16, 16, KZ, REG_VVVV_RM_IMM) \
    XV(VEXTRACT##T##64X2_XM128_Y_IMM8, "vextract" t "64x2", EVEX, 0x66, 3, opcode + 1, R, 1, 1, 1, Y, NONE, XM, 16, 16, KZ, RM_REG_IMM) \
    XV(VINSERT##T##32X8_Z_Z_YM256_IMM8, "vinsert" t "32x8", EVEX, 0x66, 3, opcode + 2, R, 1, 0, 2, Z, Z, YM, 32, 32, KZ, REG_VVVV_RM_IMM) \
    XV(VEXTRACT##T##32X8_YM256_Z_IMM8, "vextract" t "32x8", EVEX, 0x66, 3, opcode + 3, R, 1, 0, 2, Z, NONE, YM, 32, 32, KZ, RM_REG_IMM)

// Narrowing moves of a ZMM register's doublewords or quadwords to bytes, words or doublewords (into a register or
// memory of half, a quarter or an eighth of its width): truncating (0F 38 31-35), saturating as signed (21-25) or as
// unsigned (11-15), from `opcode`, 30, 20 or 10.
#define BUSTER_X86_AVX512_NARROWING_MOVE_FORMS(XV, N, m, opcode) \
    XV(N##DB_XM128_Z, m "db", EVEX, 0xf3, 3, opcode + 1, R, 0, 0, 2, Z, NONE, XM, 16, 16, KZ, RM_REG) \
    XV(N##QB_XM64_Z, m "qb", EVEX, 0xf3, 3, opcode + 2, R, 0, 0, 2, Z, NONE, XM, 8, 8, KZ, RM_REG) \
    XV(N##DW_YM256_Z, m "dw", EVEX, 0xf3, 3, opcode + 3, R, 0, 0, 2, Z, NONE, YM, 32, 32, KZ, RM_REG) \
    XV(N##QW_XM128_Z, m "qw", EVEX, 0xf3, 3, opcode + 4, R, 0, 0, 2, Z, NONE, XM, 16, 16, KZ, RM_REG) \
    XV(N##QD_YM256_Z, m "qd", EVEX, 0xf3, 3, opcode + 5, R, 0, 0, 2, Z, NONE, YM, 32, 32, KZ, RM_REG)

// AVX512BW's narrowing moves of words to bytes (at `opcode` itself), from ZMM, XMM and YMM registers.
#define BUSTER_X86_AVX512BW_NARROWING_MOVE_FORMS(XV, N, m, opcode) \
    XV(N##WB_YM256_Z, m "wb", EVEX, 0xf3, 3, opcode, R, 0, 0, 2, Z, NONE, YM, 32, 32, KZ, RM_REG) \
    XV(N##WB_XM64_X, m "wb", EVEX, 0xf3, 3, opcode, R, 0, 0, 0, V, NONE, XM, 8, 8, KZ, RM_REG) \
    XV(N##WB_XM128_Y, m "wb", EVEX, 0xf3, 3, opcode, R, 0, 0, 1, Y, NONE, XM, 16, 16, KZ, RM_REG)

// Sign or zero extensions to a ZMM register, as BUSTER_X86_AVX_EXTEND_FORMS's but for bytes to words (AVX512BW's).
#define BUSTER_X86_AVX512_EXTEND_FORMS(XV, N, m, opcode) \
    XV(N##BD_Z_XM128, m "bd", EVEX, 0x66, 3, opcode + 1, R, 0, 0, 2, Z, NONE, XM, 16, 16, KZ, REG_RM) \
    XV(N##BQ_Z_XM64, m "bq", EVEX, 0x66, 3, opcode + 2, R, 0, 0, 2, Z, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(N##WD_Z_YM256, m "wd", EVEX, 0x66, 3, opcode + 3, R, 0, 0, 2, Z, NONE, YM, 32, 32, KZ, REG_RM) \
    XV(N##WQ_Z_XM128, m "wq", EVEX, 0x66, 3, opcode + 4, R, 0, 0, 2, Z, NONE, XM, 16, 16, KZ, REG_RM) \
    XV(N##DQ_Z_YM256, m "dq", EVEX, 0x66, 3, opcode + 5, R, 0, 0, 2, Z, NONE, YM, 32, 32, KZ, REG_RM)

// AVX512BW's extensions of bytes to words (at `opcode` itself), into ZMM, XMM and YMM registers.
#define BUSTER_X86_AVX512BW_EXTEND_FORMS(XV, N, m, opcode) \
    XV(N##BW_Z_YM256, m "bw", EVEX, 0x66, 3, opcode, R, 0, 0, 2, Z, NONE, YM, 32, 32, KZ, REG_RM) \
    XV(N##BW_X_XM64_EVEX, m "bw", EVEX, 0x66, 3, opcode, R, 0, 0, 0, V, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(N##BW_Y_XM128_EVEX, m "bw", EVEX, 0x66, 3, opcode, R, 0, 0, 1, Y, NONE, XM, 16, 16, KZ, REG_RM)

// Vectors from an opmask register in ModRM.rm (elements of all ones or zeros, W selecting the element size, or its bits
// broadcast), and opmask registers from vectors (their elements' top bits), at 512, 128 and 256 bits.
#define BUSTER_X86_AVX512_FROM_MASK_FORMS(XV, N, m, opcode, w) \
    XV(N##_Z_K, m, EVEX, 0xf3, 3, opcode, R, 0, w, 2, Z, NONE, K, 0, 1, NONE, REG_RM) \
    XV(N##_X_K, m, EVEX, 0xf3, 3, opcode, R, 0, w, 0, V, NONE, K, 0, 1, NONE, REG_RM) \
    XV(N##_Y_K, m, EVEX, 0xf3, 3, opcode, R, 0, w, 1, Y, NONE, K, 0, 1, NONE, REG_RM)

#define BUSTER_X86_AVX512_TO_MASK_FORMS(XV, N, m, opcode, w) \
    XV(N##_K_Z, m, EVEX, 0xf3, 3, opcode, R, 0, w, 2, K, NONE, Z, 0, 1, NONE, REG_RM) \
    XV(N##_K_X, m, EVEX, 0xf3, 3, opcode, R, 0, w, 0, K, NONE, V, 0, 1, NONE, REG_RM) \
    XV(N##_K_Y, m, EVEX, 0xf3, 3, opcode, R, 0, w, 1, K, NONE, Y, 0, 1, NONE, REG_RM)

// The destination, the first source in EVEX.V'vvvv and the second in ModRM.rm, at 512 bits; then the same with the
// second a broadcast element of `bits` bits; then with embedded rounding or suppressed exceptions.
#define BUSTER_X86_AVX512_PACKED_FORM(XV, N, m, prefixes, opcode_bytes, opcode, w) \
    XV(N##_Z_Z_ZM512, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 2, Z, Z, ZM, 64, 64, KZ, REG_VVVV_RM)

#define BUSTER_X86_AVX512_BROADCAST_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    BUSTER_X86_AVX512_PACKED_FORM(XV, N, m, prefixes, opcode_bytes, opcode, w) \
    XV(N##_Z_Z_M##bits##BCST, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 2, Z, Z, M, 64, bits / 8, KZ_BCST, REG_VVVV_RM)

#define BUSTER_X86_AVX512_ROUNDING_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    XV(N##_Z_Z_Z_ER, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 2, Z, Z, Z, 64, 64, KZ_ER, REG_VVVV_RM)

#define BUSTER_X86_AVX512_SAE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    XV(N##_Z_Z_Z_SAE, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 2, Z, Z, Z, 64, 64, KZ_SAE, REG_VVVV_RM)

#define BUSTER_X86_AVX512_IMMEDIATE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    XV(N##_Z_Z_ZM512_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 2, Z, Z, ZM, 64, 64, KZ, REG_VVVV_RM_IMM) \
    XV(N##_Z_Z_M##bits##BCST_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 2, Z, Z, M, 64, bits / 8, KZ_BCST, REG_VVVV_RM_IMM)

#define BUSTER_X86_AVX512_IMMEDIATE_SAE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    BUSTER_X86_AVX512_IMMEDIATE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    XV(N##_Z_Z_Z_SAE_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 2, Z, Z, Z, 64, 64, KZ_SAE, REG_VVVV_RM_IMM)

// FMA3 (BUSTER_X86_FMA_PACKED_FORMS) at 512 bits, with broadcasts and embedded rounding.
#define BUSTER_X86_AVX512_FMA_PACKED_FORMS(XV, N, m, opcode) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, N##132PS, m "132ps", 0x66, 3, 0x0f3800 + opcode, 0, 32) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, N##132PD, m "132pd", 0x66, 3, 0x0f3800 + opcode, 1, 64) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, N##213PS, m "213ps", 0x66, 3, 0x0f3810 + opcode, 0, 32) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, N##213PD, m "213pd", 0x66, 3, 0x0f3810 + opcode, 1, 64) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, N##231PS, m "231ps", 0x66, 3, 0x0f3820 + opcode, 0, 32) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, N##231PD, m "231pd", 0x66, 3, 0x0f3820 + opcode, 1, 64)

// The destination and one source, as above.
#define BUSTER_X86_AVX512_UNARY_FORM(XV, N, m, prefixes, opcode_bytes, opcode, w) \
    XV(N##_Z_ZM512, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 2, Z, NONE, ZM, 64, 64, KZ, REG_RM)

#define BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    BUSTER_X86_AVX512_UNARY_FORM(XV, N, m, prefixes, opcode_bytes, opcode, w) \
    XV(N##_Z_M##bits##BCST, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 2, Z, NONE, M, 64, bits / 8, KZ_BCST, REG_RM)

#define BUSTER_X86_AVX512_UNARY_ROUNDING_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    XV(N##_Z_Z_ER, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 2, Z, NONE, Z, 64, 64, KZ_ER, REG_RM)

#define BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    XV(N##_Z_Z_SAE, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 2, Z, NONE, Z, 64, 64, KZ_SAE, REG_RM)

#define BUSTER_X86_AVX512_UNARY_IMMEDIATE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    XV(N##_Z_ZM512_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 2, Z, NONE, ZM, 64, 64, KZ, REG_RM_IMM) \
    XV(N##_Z_M##bits##BCST_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 2, Z, NONE, M, 64, bits / 8, KZ_BCST, REG_RM_IMM)

#define BUSTER_X86_AVX512_UNARY_IMMEDIATE_SAE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    BUSTER_X86_AVX512_UNARY_IMMEDIATE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    XV(N##_Z_Z_SAE_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 2, Z, NONE, Z, 64, 64, KZ_SAE, REG_RM_IMM)

// Conversions to elements twice as wide (from `bits`-bit ones) into a ZMM register, from half the vector: a YMM
// register, a 256-bit memory operand or a broadcast; then from a register with embedded rounding or suppressed
// exceptions (K: ER or SAE).
#define BUSTER_X86_AVX512_HALF_WIDENING_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    XV(N##_Z_YM256, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 2, Z, NONE, YM, 32, 32, KZ, REG_RM) \
    XV(N##_Z_M##bits##BCST, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 2, Z, NONE, M, 32, bits / 8, KZ_BCST, REG_RM)

#define BUSTER_X86_AVX512_HALF_WIDENING_DECORATED_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits, K) \
    BUSTER_X86_AVX512_HALF_WIDENING_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    XV(N##_Z_Y_##K, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 2, Z, NONE, Y, 32, 32, KZ_##K, REG_RM)

// The same for 32-bit elements in map 1 (AVX-512F's).
#define BUSTER_X86_AVX512_WIDENING_FORMS(XV, N, m, prefixes, opcode) BUSTER_X86_AVX512_HALF_WIDENING_FORMS(XV, N, m, prefixes, 2, opcode, 0, 32)

#define BUSTER_X86_AVX512_WIDENING_ROUNDING_FORMS(XV, N, m, prefixes, opcode) \
    BUSTER_X86_AVX512_HALF_WIDENING_DECORATED_FORMS(XV, N, m, prefixes, 2, opcode, 0, 32, ER)

#define BUSTER_X86_AVX512_WIDENING_SAE_FORMS(XV, N, m, prefixes, opcode) \
    BUSTER_X86_AVX512_HALF_WIDENING_DECORATED_FORMS(XV, N, m, prefixes, 2, opcode, 0, 32, SAE)

// Conversions to elements four times as wide (AVX512-FP16's from 16-bit ones to 64-bit ones) into a ZMM register,
// from a quarter of the vector (an XMM register, 128 bits of memory or a broadcast) and from a register with embedded
// rounding or suppressed exceptions (K); and into XMM and YMM registers, from 32 or 64 bits or a broadcast.
#define BUSTER_X86_AVX512_QUARTER_WIDENING_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, bits, K) \
    XV(N##_Z_XM128, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 0, 2, Z, NONE, XM, 16, 16, KZ, REG_RM) \
    XV(N##_Z_M##bits##BCST, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 0, 2, Z, NONE, M, 16, bits / 8, KZ_BCST, REG_RM) \
    XV(N##_Z_X_##K, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 0, 2, Z, NONE, V, 16, 16, KZ_##K, REG_RM) \
    XV(N##_X_XM32, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 0, 0, V, NONE, XM, 4, 4, KZ, REG_RM) \
    XV(N##_X_M##bits##BCST, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 0, 0, V, NONE, M, 4, bits / 8, KZ_BCST, REG_RM) \
    XV(N##_Y_XM64, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 0, 1, Y, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(N##_Y_M##bits##BCST, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 0, 1, Y, NONE, M, 8, bits / 8, KZ_BCST, REG_RM)

// Conversions to elements half as wide (from `bits`-bit ones) into a YMM register: from a ZMM register, a 512-bit
// memory operand or a broadcast, then from a register with embedded rounding or suppressed exceptions (K: ER or SAE).
#define BUSTER_X86_AVX512_HALF_NARROWING_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    XV(N##_Y_ZM512, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 2, Y, NONE, ZM, 64, 64, KZ, REG_RM) \
    XV(N##_Y_M##bits##BCST, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 2, Y, NONE, M, 64, bits / 8, KZ_BCST, REG_RM)

#define BUSTER_X86_AVX512_HALF_NARROWING_DECORATED_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits, K) \
    BUSTER_X86_AVX512_HALF_NARROWING_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    XV(N##_Y_Z_##K, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 2, Y, NONE, Z, 64, 64, KZ_##K, REG_RM)

// The same for 64-bit elements in map 1 (AVX-512F's and DQ's).
#define BUSTER_X86_AVX512_NARROWING_FORMS(XV, N, m, prefixes, opcode, K) \
    BUSTER_X86_AVX512_HALF_NARROWING_DECORATED_FORMS(XV, N, m, prefixes, 2, opcode, 1, 64, K)

// Conversions to elements a quarter as wide (AVX512-FP16's from 64-bit ones to 16-bit ones), into an XMM register
// from 128, 256 or 512 bits or a broadcast (whose names give their counts, their operands being the same), and from a
// ZMM register with embedded rounding (K).
#define BUSTER_X86_AVX512_QUARTER_NARROWING_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, K) \
    XV(N##_X_XM128, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 1, 0, V, NONE, XM, 16, 16, KZ, REG_RM) \
    XV(N##_X_M64BCST2, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 1, 0, V, NONE, M, 16, 8, KZ_BCST, REG_RM) \
    XV(N##_X_YM256, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 1, 1, V, NONE, YM, 32, 32, KZ, REG_RM) \
    XV(N##_X_M64BCST4, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 1, 1, V, NONE, M, 32, 8, KZ_BCST, REG_RM) \
    XV(N##_X_ZM512, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 1, 2, V, NONE, ZM, 64, 64, KZ, REG_RM) \
    XV(N##_X_M64BCST8, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 1, 2, V, NONE, M, 64, 8, KZ_BCST, REG_RM) \
    XV(N##_X_Z_##K, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 1, 2, V, NONE, Z, 64, 64, KZ_##K, REG_RM)

// Compares into an opmask register, which an opmask can mask (but not zero).
#define BUSTER_X86_AVX512_COMPARE_FORM(XV, N, m, prefixes, opcode_bytes, opcode, w) \
    XV(N##_K_Z_ZM512, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 2, K, Z, ZM, 64, 64, K, REG_VVVV_RM)

#define BUSTER_X86_AVX512_COMPARE_BROADCAST_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    BUSTER_X86_AVX512_COMPARE_FORM(XV, N, m, prefixes, opcode_bytes, opcode, w) \
    XV(N##_K_Z_M##bits##BCST, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 2, K, Z, M, 64, bits / 8, K_BCST, REG_VVVV_RM)

// Compares with a predicate in the immediate, and with suppressed exceptions.
#define BUSTER_X86_AVX512_COMPARE_PREDICATE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    XV(N##_K_Z_ZM512_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 2, K, Z, ZM, 64, 64, K, REG_VVVV_RM_IMM) \
    XV(N##_K_Z_M##bits##BCST_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 2, K, Z, M, 64, bits / 8, K_BCST, REG_VVVV_RM_IMM)

#define BUSTER_X86_AVX512_COMPARE_PREDICATE_SAE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    BUSTER_X86_AVX512_COMPARE_PREDICATE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    XV(N##_K_Z_Z_SAE_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 2, K, Z, Z, 64, 64, K_SAE, REG_VVVV_RM_IMM)

// The integer compares in 66 0F 3A, and the floating-point ones in 0F C2.
#define BUSTER_X86_AVX512_COMPARE_IMMEDIATE_FORMS(XV, N, m, opcode, w, bits) BUSTER_X86_AVX512_COMPARE_PREDICATE_FORMS(XV, N, m, 0x66, 3, opcode, w, bits)

#define BUSTER_X86_AVX512_COMPARE_SAE_FORMS(XV, N, m, prefixes, w, bits) \
    BUSTER_X86_AVX512_COMPARE_PREDICATE_SAE_FORMS(XV, N, m, prefixes, 2, 0x0fc2, w, bits)

// Shifts and rotates by an immediate (ModRM.reg is the operation, K), into EVEX.V'vvvv, of a register or memory
// operand.
#define BUSTER_X86_AVX512_SHIFT_FORMS(XV, N, m, opcode, w, K, bits) \
    XV(N##_Z_ZM512_IMM8, m, EVEX, 0x66, 2, 0x0f00 + opcode, D##K, 1, w, 2, NONE, Z, ZM, 64, 64, KZ, VVVV_RM_IMM) \
    XV(N##_Z_M##bits##BCST_IMM8, m, EVEX, 0x66, 2, 0x0f00 + opcode, D##K, 1, w, 2, NONE, Z, M, 64, bits / 8, KZ_BCST, VVVV_RM_IMM)

// Shifts by the low quadword of an XMM register or 128-bit memory operand.
#define BUSTER_X86_AVX512_SHIFT_COUNT_FORM(XV, N, m, opcode, w) \
    XV(N##_Z_Z_XM128, m, EVEX, 0x66, 2, 0x0f00 + opcode, R, 0, w, 2, Z, Z, XM, 16, 16, KZ, REG_VVVV_RM)

// AVX512VL: the forms above at 128 and 256 bits (L0 and L1), on XMM and YMM registers (16 to 31 too), with N of the
// narrower vector. Embedded rounding and suppressed exceptions take L'L's place, so they're only at 512 bits.
// `suffix` is _EVEX for an instruction whose VEX forms have the names, and empty otherwise.
#define BUSTER_X86_AVX512VL_MOVE_FORMS(XV, N, m, prefixes, w, load, store, suffix) \
    XV(N##_X_XM128##suffix, m, EVEX, prefixes, 2, load, R, 0, w, 0, V, NONE, XM, 16, 16, KZ, REG_RM) \
    XV(N##_XM128_X##suffix, m, EVEX, prefixes, 2, store, R, 0, w, 0, V, NONE, XM, 16, 16, KZ, RM_REG) \
    XV(N##_Y_YM256##suffix, m, EVEX, prefixes, 2, load, R, 0, w, 1, Y, NONE, YM, 32, 32, KZ, REG_RM) \
    XV(N##_YM256_Y##suffix, m, EVEX, prefixes, 2, store, R, 0, w, 1, Y, NONE, YM, 32, 32, KZ, RM_REG)

// The destination, the first source in EVEX.V'vvvv and the second in ModRM.rm, or a broadcast element of `bits` bits;
// at 256 bits only (the _256_ families) for the permutes and shuffles across 128-bit lanes.
#define BUSTER_X86_AVX512VL_256_BROADCAST_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits, suffix) \
    XV(N##_Y_Y_YM256##suffix, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 1, Y, Y, YM, 32, 32, KZ, REG_VVVV_RM) \
    XV(N##_Y_Y_M##bits##BCST, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 1, Y, Y, M, 32, bits / 8, KZ_BCST, REG_VVVV_RM)

#define BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits, suffix) \
    XV(N##_X_X_XM128##suffix, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 0, V, V, XM, 16, 16, KZ, REG_VVVV_RM) \
    XV(N##_X_X_M##bits##BCST, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 0, V, V, M, 16, bits / 8, KZ_BCST, REG_VVVV_RM) \
    BUSTER_X86_AVX512VL_256_BROADCAST_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits, suffix)

#define BUSTER_X86_AVX512VL_256_IMMEDIATE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits, suffix) \
    XV(N##_Y_Y_YM256_IMM8##suffix, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 1, Y, Y, YM, 32, 32, KZ, REG_VVVV_RM_IMM) \
    XV(N##_Y_Y_M##bits##BCST_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 1, Y, Y, M, 32, bits / 8, KZ_BCST, REG_VVVV_RM_IMM)

#define BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits, suffix) \
    XV(N##_X_X_XM128_IMM8##suffix, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 0, V, V, XM, 16, 16, KZ, REG_VVVV_RM_IMM) \
    XV(N##_X_X_M##bits##BCST_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 0, V, V, M, 16, bits / 8, KZ_BCST, REG_VVVV_RM_IMM) \
    BUSTER_X86_AVX512VL_256_IMMEDIATE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits, suffix)

// FMA3, whose VEX forms have the names.
#define BUSTER_X86_AVX512VL_FMA_PACKED_FORMS(XV, N, m, opcode) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, N##132PS, m "132ps", 0x66, 3, 0x0f3800 + opcode, 0, 32, _EVEX) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, N##132PD, m "132pd", 0x66, 3, 0x0f3800 + opcode, 1, 64, _EVEX) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, N##213PS, m "213ps", 0x66, 3, 0x0f3810 + opcode, 0, 32, _EVEX) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, N##213PD, m "213pd", 0x66, 3, 0x0f3810 + opcode, 1, 64, _EVEX) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, N##231PS, m "231ps", 0x66, 3, 0x0f3820 + opcode, 0, 32, _EVEX) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, N##231PD, m "231pd", 0x66, 3, 0x0f3820 + opcode, 1, 64, _EVEX)

// The destination and one source.
#define BUSTER_X86_AVX512VL_UNARY_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, suffix) \
    XV(N##_X_XM128##suffix, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 0, V, NONE, XM, 16, 16, KZ, REG_RM) \
    XV(N##_Y_YM256##suffix, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 1, Y, NONE, YM, 32, 32, KZ, REG_RM)

#define BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits, suffix) \
    BUSTER_X86_AVX512VL_UNARY_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, suffix) \
    XV(N##_X_M##bits##BCST, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 0, V, NONE, M, 16, bits / 8, KZ_BCST, REG_RM) \
    XV(N##_Y_M##bits##BCST, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 1, Y, NONE, M, 32, bits / 8, KZ_BCST, REG_RM)

#define BUSTER_X86_AVX512VL_256_UNARY_IMMEDIATE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits, suffix) \
    XV(N##_Y_YM256_IMM8##suffix, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 1, Y, NONE, YM, 32, 32, KZ, REG_RM_IMM) \
    XV(N##_Y_M##bits##BCST_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 1, Y, NONE, M, 32, bits / 8, KZ_BCST, REG_RM_IMM)

#define BUSTER_X86_AVX512VL_UNARY_IMMEDIATE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits, suffix) \
    XV(N##_X_XM128_IMM8##suffix, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 0, V, NONE, XM, 16, 16, KZ, REG_RM_IMM) \
    XV(N##_X_M##bits##BCST_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 0, V, NONE, M, 16, bits / 8, KZ_BCST, REG_RM_IMM) \
    BUSTER_X86_AVX512VL_256_UNARY_IMMEDIATE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits, suffix)

// Conversions to elements twice as wide (from `bits`-bit ones): into an XMM register from 64 bits (an XMM register's
// low half, or memory) or a broadcast, and into a YMM register from 128 bits or a broadcast; then the same for 32-bit
// elements in map 1.
#define BUSTER_X86_AVX512VL_HALF_WIDENING_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits, suffix) \
    XV(N##_X_XM64##suffix, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 0, V, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(N##_X_M##bits##BCST, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 0, V, NONE, M, 8, bits / 8, KZ_BCST, REG_RM) \
    XV(N##_Y_XM128##suffix, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 1, Y, NONE, XM, 16, 16, KZ, REG_RM) \
    XV(N##_Y_M##bits##BCST, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 1, Y, NONE, M, 16, bits / 8, KZ_BCST, REG_RM)

#define BUSTER_X86_AVX512VL_WIDENING_FORMS(XV, N, m, prefixes, opcode, suffix) \
    BUSTER_X86_AVX512VL_HALF_WIDENING_FORMS(XV, N, m, prefixes, 2, opcode, 0, 32, suffix)

// Conversions to elements half as wide (from `bits`-bit ones), into an XMM register from 128 bits (L0) or 256 bits
// (L1): the broadcasts' names give their element counts (c128 and c256), their operands being the same; then the same
// for 64-bit elements in map 1.
#define BUSTER_X86_AVX512VL_HALF_NARROWING_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits, c128, c256, suffix) \
    XV(N##_X_XM128##suffix, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 0, V, NONE, XM, 16, 16, KZ, REG_RM) \
    XV(N##_X_M##bits##BCST##c128, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 0, V, NONE, M, 16, bits / 8, KZ_BCST, REG_RM) \
    XV(N##_X_YM256##suffix, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 1, V, NONE, YM, 32, 32, KZ, REG_RM) \
    XV(N##_X_M##bits##BCST##c256, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 1, V, NONE, M, 32, bits / 8, KZ_BCST, REG_RM)

#define BUSTER_X86_AVX512VL_NARROWING_FORMS(XV, N, m, prefixes, opcode, suffix) \
    BUSTER_X86_AVX512VL_HALF_NARROWING_FORMS(XV, N, m, prefixes, 2, opcode, 1, 64, 2, 4, suffix)

// Compares into an opmask register (whose names no VEX form has).
#define BUSTER_X86_AVX512VL_COMPARE_BROADCAST_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    XV(N##_K_X_XM128, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 0, K, V, XM, 16, 16, K, REG_VVVV_RM) \
    XV(N##_K_X_M##bits##BCST, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 0, K, V, M, 16, bits / 8, K_BCST, REG_VVVV_RM) \
    XV(N##_K_Y_YM256, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 1, K, Y, YM, 32, 32, K, REG_VVVV_RM) \
    XV(N##_K_Y_M##bits##BCST, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 1, K, Y, M, 32, bits / 8, K_BCST, REG_VVVV_RM)

#define BUSTER_X86_AVX512VL_COMPARE_IMMEDIATE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    XV(N##_K_X_XM128_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 0, K, V, XM, 16, 16, K, REG_VVVV_RM_IMM) \
    XV(N##_K_X_M##bits##BCST_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 0, K, V, M, 16, bits / 8, K_BCST, REG_VVVV_RM_IMM) \
    XV(N##_K_Y_YM256_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 1, K, Y, YM, 32, 32, K, REG_VVVV_RM_IMM) \
    XV(N##_K_Y_M##bits##BCST_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 1, K, Y, M, 32, bits / 8, K_BCST, REG_VVVV_RM_IMM)

// Shifts and rotates by an immediate (into EVEX.V'vvvv, of a register or memory operand: the VEX forms take only a
// register, and have other names), and shifts by the low quadword of an XMM register or 128-bit memory operand.
#define BUSTER_X86_AVX512VL_SHIFT_FORMS(XV, N, m, opcode, w, K, bits) \
    XV(N##_X_XM128_IMM8, m, EVEX, 0x66, 2, 0x0f00 + opcode, D##K, 1, w, 0, NONE, V, XM, 16, 16, KZ, VVVV_RM_IMM) \
    XV(N##_X_M##bits##BCST_IMM8, m, EVEX, 0x66, 2, 0x0f00 + opcode, D##K, 1, w, 0, NONE, V, M, 16, bits / 8, KZ_BCST, VVVV_RM_IMM) \
    XV(N##_Y_YM256_IMM8, m, EVEX, 0x66, 2, 0x0f00 + opcode, D##K, 1, w, 1, NONE, Y, YM, 32, 32, KZ, VVVV_RM_IMM) \
    XV(N##_Y_M##bits##BCST_IMM8, m, EVEX, 0x66, 2, 0x0f00 + opcode, D##K, 1, w, 1, NONE, Y, M, 32, bits / 8, KZ_BCST, VVVV_RM_IMM)

#define BUSTER_X86_AVX512VL_SHIFT_COUNT_FORMS(XV, N, m, opcode, w, suffix) \
    XV(N##_X_X_XM128##suffix, m, EVEX, 0x66, 2, 0x0f00 + opcode, R, 0, w, 0, V, V, XM, 16, 16, KZ, REG_VVVV_RM) \
    XV(N##_Y_Y_XM128##suffix, m, EVEX, 0x66, 2, 0x0f00 + opcode, R, 0, w, 1, Y, Y, XM, 16, 16, KZ, REG_VVVV_RM)

// Narrowing moves (BUSTER_X86_AVX512_NARROWING_MOVE_FORMS) from XMM and YMM registers.
#define BUSTER_X86_AVX512VL_NARROWING_MOVE_FORMS(XV, N, m, opcode) \
    XV(N##DB_XM32_X, m "db", EVEX, 0xf3, 3, opcode + 1, R, 0, 0, 0, V, NONE, XM, 4, 4, KZ, RM_REG) \
    XV(N##DB_XM64_Y, m "db", EVEX, 0xf3, 3, opcode + 1, R, 0, 0, 1, Y, NONE, XM, 8, 8, KZ, RM_REG) \
    XV(N##QB_XM16_X, m "qb", EVEX, 0xf3, 3, opcode + 2, R, 0, 0, 0, V, NONE, XM, 2, 2, KZ, RM_REG) \
    XV(N##QB_XM32_Y, m "qb", EVEX, 0xf3, 3, opcode + 2, R, 0, 0, 1, Y, NONE, XM, 4, 4, KZ, RM_REG) \
    XV(N##DW_XM64_X, m "dw", EVEX, 0xf3, 3, opcode + 3, R, 0, 0, 0, V, NONE, XM, 8, 8, KZ, RM_REG) \
    XV(N##DW_XM128_Y, m "dw", EVEX, 0xf3, 3, opcode + 3, R, 0, 0, 1, Y, NONE, XM, 16, 16, KZ, RM_REG) \
    XV(N##QW_XM32_X, m "qw", EVEX, 0xf3, 3, opcode + 4, R, 0, 0, 0, V, NONE, XM, 4, 4, KZ, RM_REG) \
    XV(N##QW_XM64_Y, m "qw", EVEX, 0xf3, 3, opcode + 4, R, 0, 0, 1, Y, NONE, XM, 8, 8, KZ, RM_REG) \
    XV(N##QD_XM64_X, m "qd", EVEX, 0xf3, 3, opcode + 5, R, 0, 0, 0, V, NONE, XM, 8, 8, KZ, RM_REG) \
    XV(N##QD_XM128_Y, m "qd", EVEX, 0xf3, 3, opcode + 5, R, 0, 0, 1, Y, NONE, XM, 16, 16, KZ, RM_REG)

// Sign or zero extensions (BUSTER_X86_AVX512_EXTEND_FORMS) into XMM and YMM registers, whose VEX forms have the names.
#define BUSTER_X86_AVX512VL_EXTEND_FORMS(XV, N, m, opcode) \
    XV(N##BD_X_XM32_EVEX, m "bd", EVEX, 0x66, 3, opcode + 1, R, 0, 0, 0, V, NONE, XM, 4, 4, KZ, REG_RM) \
    XV(N##BQ_X_XM16_EVEX, m "bq", EVEX, 0x66, 3, opcode + 2, R, 0, 0, 0, V, NONE, XM, 2, 2, KZ, REG_RM) \
    XV(N##WD_X_XM64_EVEX, m "wd", EVEX, 0x66, 3, opcode + 3, R, 0, 0, 0, V, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(N##WQ_X_XM32_EVEX, m "wq", EVEX, 0x66, 3, opcode + 4, R, 0, 0, 0, V, NONE, XM, 4, 4, KZ, REG_RM) \
    XV(N##DQ_X_XM64_EVEX, m "dq", EVEX, 0x66, 3, opcode + 5, R, 0, 0, 0, V, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(N##BD_Y_XM64_EVEX, m "bd", EVEX, 0x66, 3, opcode + 1, R, 0, 0, 1, Y, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(N##BQ_Y_XM32_EVEX, m "bq", EVEX, 0x66, 3, opcode + 2, R, 0, 0, 1, Y, NONE, XM, 4, 4, KZ, REG_RM) \
    XV(N##WD_Y_XM128_EVEX, m "wd", EVEX, 0x66, 3, opcode + 3, R, 0, 0, 1, Y, NONE, XM, 16, 16, KZ, REG_RM) \
    XV(N##WQ_Y_XM64_EVEX, m "wq", EVEX, 0x66, 3, opcode + 4, R, 0, 0, 1, Y, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(N##DQ_Y_XM128_EVEX, m "dq", EVEX, 0x66, 3, opcode + 5, R, 0, 0, 1, Y, NONE, XM, 16, 16, KZ, REG_RM)

// Byte and word operations, which have no broadcasts (AVX512BW's, and VBMI's, VBMI2's, BITALG's and GFNI's), at 512,
// 128 and 256 bits (`suffix` as for the AVX512VL families, on the narrower forms).
#define BUSTER_X86_AVX512BW_PACKED_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, suffix) \
    BUSTER_X86_AVX512_PACKED_FORM(XV, N, m, prefixes, opcode_bytes, opcode, w) \
    XV(N##_X_X_XM128##suffix, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 0, V, V, XM, 16, 16, KZ, REG_VVVV_RM) \
    XV(N##_Y_Y_YM256##suffix, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 1, Y, Y, YM, 32, 32, KZ, REG_VVVV_RM)

#define BUSTER_X86_AVX512BW_IMMEDIATE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, suffix) \
    XV(N##_Z_Z_ZM512_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 2, Z, Z, ZM, 64, 64, KZ, REG_VVVV_RM_IMM) \
    XV(N##_X_X_XM128_IMM8##suffix, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 0, V, V, XM, 16, 16, KZ, REG_VVVV_RM_IMM) \
    XV(N##_Y_Y_YM256_IMM8##suffix, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 1, Y, Y, YM, 32, 32, KZ, REG_VVVV_RM_IMM)

#define BUSTER_X86_AVX512BW_UNARY_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, suffix) \
    BUSTER_X86_AVX512_UNARY_FORM(XV, N, m, prefixes, opcode_bytes, opcode, w) \
    BUSTER_X86_AVX512VL_UNARY_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, suffix)

#define BUSTER_X86_AVX512BW_UNARY_IMMEDIATE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, suffix) \
    XV(N##_Z_ZM512_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 2, Z, NONE, ZM, 64, 64, KZ, REG_RM_IMM) \
    XV(N##_X_XM128_IMM8##suffix, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 0, V, NONE, XM, 16, 16, KZ, REG_RM_IMM) \
    XV(N##_Y_YM256_IMM8##suffix, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 1, Y, NONE, YM, 32, 32, KZ, REG_RM_IMM)

#define BUSTER_X86_AVX512BW_COMPARE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w) \
    BUSTER_X86_AVX512_COMPARE_FORM(XV, N, m, prefixes, opcode_bytes, opcode, w) \
    XV(N##_K_X_XM128, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 0, K, V, XM, 16, 16, K, REG_VVVV_RM) \
    XV(N##_K_Y_YM256, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 1, K, Y, YM, 32, 32, K, REG_VVVV_RM)

#define BUSTER_X86_AVX512BW_COMPARE_IMMEDIATE_FORMS(XV, N, m, opcode, w) \
    XV(N##_K_Z_ZM512_IMM8, m, EVEX, 0x66, 3, opcode, R, 1, w, 2, K, Z, ZM, 64, 64, K, REG_VVVV_RM_IMM) \
    XV(N##_K_X_XM128_IMM8, m, EVEX, 0x66, 3, opcode, R, 1, w, 0, K, V, XM, 16, 16, K, REG_VVVV_RM_IMM) \
    XV(N##_K_Y_YM256_IMM8, m, EVEX, 0x66, 3, opcode, R, 1, w, 1, K, Y, YM, 32, 32, K, REG_VVVV_RM_IMM)

// Shifts by an immediate (ModRM.reg is the operation, K), into EVEX.V'vvvv; the byte shifts of each 128-bit lane take
// no opmask (`decorations`).
#define BUSTER_X86_AVX512BW_SHIFT_FORMS(XV, N, m, opcode, K, decorations) \
    XV(N##_Z_ZM512_IMM8, m, EVEX, 0x66, 2, 0x0f00 + opcode, D##K, 1, 0, 2, NONE, Z, ZM, 64, 64, decorations, VVVV_RM_IMM) \
    XV(N##_X_XM128_IMM8, m, EVEX, 0x66, 2, 0x0f00 + opcode, D##K, 1, 0, 0, NONE, V, XM, 16, 16, decorations, VVVV_RM_IMM) \
    XV(N##_Y_YM256_IMM8, m, EVEX, 0x66, 2, 0x0f00 + opcode, D##K, 1, 0, 1, NONE, Y, YM, 32, 32, decorations, VVVV_RM_IMM)

// Forms that take no opmask (VAES's rounds on each 128-bit lane, and AVX10.2's SM4), at 512, 128 and 256 bits (the
// narrower forms' names are VEX's, with _EVEX).
#define BUSTER_X86_AVX512_UNMASKED_FORMS(XV, N, m, prefixes, opcode) \
    XV(N##_Z_Z_ZM512, m, EVEX, prefixes, 3, opcode, R, 0, 0, 2, Z, Z, ZM, 64, 64, NONE, REG_VVVV_RM) \
    XV(N##_X_X_XM128_EVEX, m, EVEX, prefixes, 3, opcode, R, 0, 0, 0, V, V, XM, 16, 16, NONE, REG_VVVV_RM) \
    XV(N##_Y_Y_YM256_EVEX, m, EVEX, prefixes, 3, opcode, R, 0, 0, 1, Y, Y, YM, 32, 32, NONE, REG_VVVV_RM)

#define BUSTER_X86_AVX512_AES_FORMS(XV, N, m, opcode) BUSTER_X86_AVX512_UNMASKED_FORMS(XV, N, m, 0x66, opcode)

// VP2INTERSECT: a pair of opmask registers (the even one in ModRM.reg, and the next) from which of each source's
// elements are in the other, at 128, 256 and 512 bits, without an opmask.
#define BUSTER_X86_AVX512_INTERSECT_FORMS(XV, N, m, w, bits) \
    XV(N##_K_X_XM128, m, EVEX, 0xf2, 3, 0x0f3868, R, 0, w, 0, KP, V, XM, 16, 16, NONE, REG_VVVV_RM) \
    XV(N##_K_X_M##bits##BCST, m, EVEX, 0xf2, 3, 0x0f3868, R, 0, w, 0, KP, V, M, 16, bits / 8, BROADCAST, REG_VVVV_RM) \
    XV(N##_K_Y_YM256, m, EVEX, 0xf2, 3, 0x0f3868, R, 0, w, 1, KP, Y, YM, 32, 32, NONE, REG_VVVV_RM) \
    XV(N##_K_Y_M##bits##BCST, m, EVEX, 0xf2, 3, 0x0f3868, R, 0, w, 1, KP, Y, M, 32, bits / 8, BROADCAST, REG_VVVV_RM) \
    XV(N##_K_Z_ZM512, m, EVEX, 0xf2, 3, 0x0f3868, R, 0, w, 2, KP, Z, ZM, 64, 64, NONE, REG_VVVV_RM) \
    XV(N##_K_Z_M##bits##BCST, m, EVEX, 0xf2, 3, 0x0f3868, R, 0, w, 2, KP, Z, M, 64, bits / 8, BROADCAST, REG_VVVV_RM)

// AVX512DQ's tests of the elements' classes (the immediate's categories) into an opmask register, at 128, 256 and 512
// bits: the broadcasts' names give their counts (c128, c256 and c512), their operands being the same.
#define BUSTER_X86_AVX512_CLASS_FORMS(XV, N, m, prefixes, w, bits, c128, c256, c512) \
    XV(N##_K_XM128_IMM8, m, EVEX, prefixes, 3, 0x0f3a66, R, 1, w, 0, K, NONE, XM, 16, 16, K, REG_RM_IMM) \
    XV(N##_K_M##bits##BCST##c128##_IMM8, m, EVEX, prefixes, 3, 0x0f3a66, R, 1, w, 0, K, NONE, M, 16, bits / 8, K_BCST, REG_RM_IMM) \
    XV(N##_K_YM256_IMM8, m, EVEX, prefixes, 3, 0x0f3a66, R, 1, w, 1, K, NONE, YM, 32, 32, K, REG_RM_IMM) \
    XV(N##_K_M##bits##BCST##c256##_IMM8, m, EVEX, prefixes, 3, 0x0f3a66, R, 1, w, 1, K, NONE, M, 32, bits / 8, K_BCST, REG_RM_IMM) \
    XV(N##_K_ZM512_IMM8, m, EVEX, prefixes, 3, 0x0f3a66, R, 1, w, 2, K, NONE, ZM, 64, 64, K, REG_RM_IMM) \
    XV(N##_K_M##bits##BCST##c512##_IMM8, m, EVEX, prefixes, 3, 0x0f3a66, R, 1, w, 2, K, NONE, M, 64, bits / 8, K_BCST, REG_RM_IMM)

#define BUSTER_X86_AVX512DQ_CLASS_FORMS(XV, N, m, w, bits, c128, c256, c512) BUSTER_X86_AVX512_CLASS_FORMS(XV, N, m, 0x66, w, bits, c128, c256, c512)

// Scalar arithmetic (single precision with F3, double precision with F2 and W1) with an opmask, and with embedded
// rounding or suppressed exceptions (K: ER or SAE).
#define BUSTER_X86_AVX512_SCALAR_FORMS(XV, N, m, opcode, K) \
    XV(N##SS_X_X_XM32_EVEX, m "ss", EVEX, 0xf3, 2, 0x0f00 + opcode, R, 0, 0, 0, V, V, XM, 4, 4, KZ, REG_VVVV_RM) \
    XV(N##SS_X_X_X_##K, m "ss", EVEX, 0xf3, 2, 0x0f00 + opcode, R, 0, 0, 0, V, V, V, 4, 4, KZ_##K, REG_VVVV_RM) \
    XV(N##SD_X_X_XM64_EVEX, m "sd", EVEX, 0xf2, 2, 0x0f00 + opcode, R, 0, 1, 0, V, V, XM, 8, 8, KZ, REG_VVVV_RM) \
    XV(N##SD_X_X_X_##K, m "sd", EVEX, 0xf2, 2, 0x0f00 + opcode, R, 0, 1, 0, V, V, V, 8, 8, KZ_##K, REG_VVVV_RM)

// The same in AVX-512's own scalar operations, in 66 0F 38: single precision with W0, double precision with W1.
#define BUSTER_X86_AVX512_SCALAR_38_FORMS(XV, N, m, opcode, K) \
    XV(N##SS_X_X_XM32, m "ss", EVEX, 0x66, 3, opcode, R, 0, 0, 0, V, V, XM, 4, 4, KZ, REG_VVVV_RM) \
    XV(N##SS_X_X_X_##K, m "ss", EVEX, 0x66, 3, opcode, R, 0, 0, 0, V, V, V, 4, 4, KZ_##K, REG_VVVV_RM) \
    XV(N##SD_X_X_XM64, m "sd", EVEX, 0x66, 3, opcode, R, 0, 1, 0, V, V, XM, 8, 8, KZ, REG_VVVV_RM) \
    XV(N##SD_X_X_X_##K, m "sd", EVEX, 0x66, 3, opcode, R, 0, 1, 0, V, V, V, 8, 8, KZ_##K, REG_VVVV_RM)

// FMA3's scalar forms (BUSTER_X86_FMA_SCALAR_FORMS), with an opmask, and with embedded rounding.
#define BUSTER_X86_AVX512_FMA_SCALAR_FORMS(XV, N, m, opcode) \
    XV(N##132SS_X_X_XM32_EVEX, m "132ss", EVEX, 0x66, 3, 0x0f3801 + opcode, R, 0, 0, 0, V, V, XM, 4, 4, KZ, REG_VVVV_RM) \
    XV(N##132SS_X_X_X_ER, m "132ss", EVEX, 0x66, 3, 0x0f3801 + opcode, R, 0, 0, 0, V, V, V, 4, 4, KZ_ER, REG_VVVV_RM) \
    XV(N##132SD_X_X_XM64_EVEX, m "132sd", EVEX, 0x66, 3, 0x0f3801 + opcode, R, 0, 1, 0, V, V, XM, 8, 8, KZ, REG_VVVV_RM) \
    XV(N##132SD_X_X_X_ER, m "132sd", EVEX, 0x66, 3, 0x0f3801 + opcode, R, 0, 1, 0, V, V, V, 8, 8, KZ_ER, REG_VVVV_RM) \
    XV(N##213SS_X_X_XM32_EVEX, m "213ss", EVEX, 0x66, 3, 0x0f3811 + opcode, R, 0, 0, 0, V, V, XM, 4, 4, KZ, REG_VVVV_RM) \
    XV(N##213SS_X_X_X_ER, m "213ss", EVEX, 0x66, 3, 0x0f3811 + opcode, R, 0, 0, 0, V, V, V, 4, 4, KZ_ER, REG_VVVV_RM) \
    XV(N##213SD_X_X_XM64_EVEX, m "213sd", EVEX, 0x66, 3, 0x0f3811 + opcode, R, 0, 1, 0, V, V, XM, 8, 8, KZ, REG_VVVV_RM) \
    XV(N##213SD_X_X_X_ER, m "213sd", EVEX, 0x66, 3, 0x0f3811 + opcode, R, 0, 1, 0, V, V, V, 8, 8, KZ_ER, REG_VVVV_RM) \
    XV(N##231SS_X_X_XM32_EVEX, m "231ss", EVEX, 0x66, 3, 0x0f3821 + opcode, R, 0, 0, 0, V, V, XM, 4, 4, KZ, REG_VVVV_RM) \
    XV(N##231SS_X_X_X_ER, m "231ss", EVEX, 0x66, 3, 0x0f3821 + opcode, R, 0, 0, 0, V, V, V, 4, 4, KZ_ER, REG_VVVV_RM) \
    XV(N##231SD_X_X_XM64_EVEX, m "231sd", EVEX, 0x66, 3, 0x0f3821 + opcode, R, 0, 1, 0, V, V, XM, 8, 8, KZ, REG_VVVV_RM) \
    XV(N##231SD_X_X_X_ER, m "231sd", EVEX, 0x66, 3, 0x0f3821 + opcode, R, 0, 1, 0, V, V, V, 8, 8, KZ_ER, REG_VVVV_RM)

// Scalar operations on the low `bits`-bit elements, with an opmask, and with embedded rounding or suppressed exceptions
// (K: ER or SAE); then the same with an 8-bit immediate.
#define BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits, K) \
    XV(N##_X_X_XM##bits, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 0, V, V, XM, bits / 8, bits / 8, KZ, REG_VVVV_RM) \
    XV(N##_X_X_X_##K, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 0, V, V, V, bits / 8, bits / 8, KZ_##K, REG_VVVV_RM)

#define BUSTER_X86_AVX512_SCALAR_ELEMENT_IMMEDIATE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits, K) \
    XV(N##_X_X_XM##bits##_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 0, V, V, XM, bits / 8, bits / 8, KZ, REG_VVVV_RM_IMM) \
    XV(N##_X_X_X_##K##_IMM8, m, EVEX, prefixes, opcode_bytes, opcode, R, 1, w, 0, V, V, V, bits / 8, bits / 8, KZ_##K, REG_VVVV_RM_IMM)

// AVX-512F's and DQ's in 66 0F 3A, with suppressed exceptions.
#define BUSTER_X86_AVX512_SCALAR_IMMEDIATE_SAE_FORMS(XV, N, m, opcode, w, bits) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_IMMEDIATE_FORMS(XV, N, m, 0x66, 3, opcode, w, bits, SAE)

// Compares of the low `bits`-bit elements into EFLAGS, which take no opmask, and with suppressed exceptions.
#define BUSTER_X86_AVX512_SCALAR_ELEMENT_COMPARE_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    XV(N##_X_XM##bits, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 0, V, NONE, XM, bits / 8, bits / 8, NONE, REG_RM) \
    XV(N##_X_X_SAE, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 0, V, NONE, V, bits / 8, bits / 8, SAE, REG_RM)

// Conversions of the low `bits`-bit element to an integer in a 32- or 64-bit (W1) general register: from a register or
// memory operand, then from a register with embedded rounding or suppressed exceptions (K: ER or SAE).
#define BUSTER_X86_AVX512_SCALAR_TO_INTEGER_ELEMENT_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, bits, K) \
    XV(N##_R32_XM##bits, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 0, 0, GD, NONE, XM, bits / 8, bits / 8, NONE, REG_RM) \
    XV(N##_R64_XM##bits, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 1, 0, GQ, NONE, XM, bits / 8, bits / 8, NONE, REG_RM) \
    XV(N##_R32_X_##K, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 0, 0, GD, NONE, V, bits / 8, bits / 8, K, REG_RM) \
    XV(N##_R64_X_##K, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 1, 0, GQ, NONE, V, bits / 8, bits / 8, K, REG_RM)

// Conversions of a 32- or 64-bit (W1) integer into the low element, with the others from EVEX.V'vvvv: from a general
// register or memory operand, then from a register with embedded rounding.
#define BUSTER_X86_AVX512_FROM_INTEGER_FORMS(XV, N, m, prefixes, opcode_bytes, opcode) \
    XV(N##_X_X_RM32, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 0, 0, V, V, ED, 4, 4, NONE, REG_VVVV_RM) \
    XV(N##_X_X_RM64, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 1, 0, V, V, EQ, 8, 8, NONE, REG_VVVV_RM) \
    XV(N##_X_X_R32_ER, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 0, 0, V, V, GD, 4, 4, ER, REG_VVVV_RM) \
    XV(N##_X_X_R64_ER, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 1, 0, V, V, GQ, 8, 8, ER, REG_VVVV_RM)

// Compares of the low elements into EFLAGS (single precision, or double precision with 66 and W1), and with
// suppressed exceptions.
#define BUSTER_X86_AVX512_SCALAR_COMPARE_FORMS(XV, N, m, opcode) \
    XV(N##SS_X_XM32_EVEX, m "ss", EVEX, 0, 2, opcode, R, 0, 0, 0, V, NONE, XM, 4, 4, NONE, REG_RM) \
    XV(N##SS_X_X_SAE, m "ss", EVEX, 0, 2, opcode, R, 0, 0, 0, V, NONE, V, 4, 4, SAE, REG_RM) \
    XV(N##SD_X_XM64_EVEX, m "sd", EVEX, 0x66, 2, opcode, R, 0, 1, 0, V, NONE, XM, 8, 8, NONE, REG_RM) \
    XV(N##SD_X_X_SAE, m "sd", EVEX, 0x66, 2, opcode, R, 0, 1, 0, V, NONE, V, 8, 8, SAE, REG_RM)

// Conversions of the low element, single precision (F3) or double precision (F2), to a signed (`opcode`, VEX's) or
// unsigned (`unsigned_opcode`) integer in a 32- or 64-bit (W1) general register: from a register or memory operand,
// then from a register with embedded rounding or suppressed exceptions (K: ER or SAE).
#define BUSTER_X86_AVX512_TO_INTEGER_FORMS(XV, N, m, opcode, unsigned_opcode, K) \
    XV(N##SS2SI_R32_XM32_EVEX, m "ss2si", EVEX, 0xf3, 2, opcode, R, 0, 0, 0, GD, NONE, XM, 4, 4, NONE, REG_RM) \
    XV(N##SS2SI_R64_XM32_EVEX, m "ss2si", EVEX, 0xf3, 2, opcode, R, 0, 1, 0, GQ, NONE, XM, 4, 4, NONE, REG_RM) \
    XV(N##SS2SI_R32_X_##K, m "ss2si", EVEX, 0xf3, 2, opcode, R, 0, 0, 0, GD, NONE, V, 4, 4, K, REG_RM) \
    XV(N##SS2SI_R64_X_##K, m "ss2si", EVEX, 0xf3, 2, opcode, R, 0, 1, 0, GQ, NONE, V, 4, 4, K, REG_RM) \
    XV(N##SD2SI_R32_XM64_EVEX, m "sd2si", EVEX, 0xf2, 2, opcode, R, 0, 0, 0, GD, NONE, XM, 8, 8, NONE, REG_RM) \
    XV(N##SD2SI_R64_XM64_EVEX, m "sd2si", EVEX, 0xf2, 2, opcode, R, 0, 1, 0, GQ, NONE, XM, 8, 8, NONE, REG_RM) \
    XV(N##SD2SI_R32_X_##K, m "sd2si", EVEX, 0xf2, 2, opcode, R, 0, 0, 0, GD, NONE, V, 8, 8, K, REG_RM) \
    XV(N##SD2SI_R64_X_##K, m "sd2si", EVEX, 0xf2, 2, opcode, R, 0, 1, 0, GQ, NONE, V, 8, 8, K, REG_RM) \
    XV(N##SS2USI_R32_XM32, m "ss2usi", EVEX, 0xf3, 2, unsigned_opcode, R, 0, 0, 0, GD, NONE, XM, 4, 4, NONE, REG_RM) \
    XV(N##SS2USI_R64_XM32, m "ss2usi", EVEX, 0xf3, 2, unsigned_opcode, R, 0, 1, 0, GQ, NONE, XM, 4, 4, NONE, REG_RM) \
    XV(N##SS2USI_R32_X_##K, m "ss2usi", EVEX, 0xf3, 2, unsigned_opcode, R, 0, 0, 0, GD, NONE, V, 4, 4, K, REG_RM) \
    XV(N##SS2USI_R64_X_##K, m "ss2usi", EVEX, 0xf3, 2, unsigned_opcode, R, 0, 1, 0, GQ, NONE, V, 4, 4, K, REG_RM) \
    XV(N##SD2USI_R32_XM64, m "sd2usi", EVEX, 0xf2, 2, unsigned_opcode, R, 0, 0, 0, GD, NONE, XM, 8, 8, NONE, REG_RM) \
    XV(N##SD2USI_R64_XM64, m "sd2usi", EVEX, 0xf2, 2, unsigned_opcode, R, 0, 1, 0, GQ, NONE, XM, 8, 8, NONE, REG_RM) \
    XV(N##SD2USI_R32_X_##K, m "sd2usi", EVEX, 0xf2, 2, unsigned_opcode, R, 0, 0, 0, GD, NONE, V, 8, 8, K, REG_RM) \
    XV(N##SD2USI_R64_X_##K, m "sd2usi", EVEX, 0xf2, 2, unsigned_opcode, R, 0, 1, 0, GQ, NONE, V, 8, 8, K, REG_RM)

// AVX512-FP16's FMAs, BUSTER_X86_FMA_PACKED_FORMS's operations on 16-bit floats (66 map 6, W0): packed, in the 132,
// 213 and 231 orders from the 132 order's opcode, at every width with broadcasts and with embedded rounding at 512
// bits; and scalar, at the next opcodes. AVX10.2's BF16 FMAs are the packed ones without the prefix or the rounding.
#define BUSTER_X86_AVX512_FMA_PH_FORMS(XV, N, m, opcode) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, N##132PH, m "132ph", 0x66, 2, opcode, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, N##132PH, m "132ph", 0x66, 2, opcode, 0, 16, ) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, N##213PH, m "213ph", 0x66, 2, opcode + 0x10, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, N##213PH, m "213ph", 0x66, 2, opcode + 0x10, 0, 16, ) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, N##231PH, m "231ph", 0x66, 2, opcode + 0x20, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, N##231PH, m "231ph", 0x66, 2, opcode + 0x20, 0, 16, )

#define BUSTER_X86_AVX512_FMA_SH_FORMS(XV, N, m, opcode) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, N##132SH, m "132sh", 0x66, 2, opcode + 1, 0, 16, ER) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, N##213SH, m "213sh", 0x66, 2, opcode + 0x11, 0, 16, ER) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, N##231SH, m "231sh", 0x66, 2, opcode + 0x21, 0, 16, ER)

#define BUSTER_X86_AVX512_FMA_BF16_FORMS(XV, N, m, opcode) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, N##132BF16, m "132bf16", 0, 2, opcode, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, N##132BF16, m "132bf16", 0, 2, opcode, 0, 16, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, N##213BF16, m "213bf16", 0, 2, opcode + 0x10, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, N##213BF16, m "213bf16", 0, 2, opcode + 0x10, 0, 16, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, N##231BF16, m "231bf16", 0, 2, opcode + 0x20, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, N##231BF16, m "231bf16", 0, 2, opcode + 0x20, 0, 16, )

// AVX10.2's conversions of 16-bit floats to 8-bit ones with biases from EVEX.V'vvvv (as wide as the source), into a
// vector half as wide, at every width, from a register, memory or a broadcast.
#define BUSTER_X86_AVX512_BIAS_CONVERT_FORMS(XV, N, m, prefixes, opcode_bytes, opcode) \
    XV(N##_X_X_XM128, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 0, 0, V, V, XM, 16, 16, KZ, REG_VVVV_RM) \
    XV(N##_X_X_M16BCST, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 0, 0, V, V, M, 16, 2, KZ_BCST, REG_VVVV_RM) \
    XV(N##_X_Y_YM256, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 0, 1, V, Y, YM, 32, 32, KZ, REG_VVVV_RM) \
    XV(N##_X_Y_M16BCST, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 0, 1, V, Y, M, 32, 2, KZ_BCST, REG_VVVV_RM) \
    XV(N##_Y_Z_ZM512, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 0, 2, Y, Z, ZM, 64, 64, KZ, REG_VVVV_RM) \
    XV(N##_Y_Z_M16BCST, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, 0, 2, Y, Z, M, 64, 2, KZ_BCST, REG_VVVV_RM)

// Loads of 128, 256 and 512 bits from memory only (AVX10.2's with a read-shared hint), merging or zeroing.
#define BUSTER_X86_AVX512_LOAD_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w) \
    XV(N##_X_M128, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 0, V, NONE, M, 16, 16, KZ, REG_RM) \
    XV(N##_Y_M256, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 1, Y, NONE, M, 32, 32, KZ, REG_RM) \
    XV(N##_Z_M512, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 2, Z, NONE, M, 64, 64, KZ, REG_RM)

// APX's EVEX forms of general register instructions, in map 4 (`opcode` is 0x04 and the opcode byte, as the maps
// without escape bytes have it). A 16-, 32- or 64-bit register from a general register or memory, with `evex` NONE (and
// `suffix` _EVEX, where the legacy forms have the names) or NF (and _NF): the bit counts (tzcnt, lzcnt and popcnt,
// without F3).
#define BUSTER_X86_EVEX_R_RM_FORMS(XV, N, m, opcode, evex, suffix) \
    XV(N##_R16_RM16##suffix, m, EVEX, 0x66, 2, opcode, R, 0, 0, 0, GW, NONE, EW, 2, 1, evex, REG_RM) \
    XV(N##_R32_RM32##suffix, m, EVEX, 0, 2, opcode, R, 0, 0, 0, GD, NONE, ED, 4, 1, evex, REG_RM) \
    XV(N##_R64_RM64##suffix, m, EVEX, 0, 2, opcode, R, 0, 1, 0, GQ, NONE, EQ, 8, 1, evex, REG_RM)

// ADX's adcx (66) and adox (F3): a 32- or 64-bit register plus a general register or memory, and the ND forms, whose
// destination is EVEX.vvvv.
#define BUSTER_X86_EVEX_ADX_FORMS(XV, N, m, prefixes) \
    XV(N##_R32_RM32_EVEX, m, EVEX, prefixes, 2, 0x0466, R, 0, 0, 0, GD, NONE, ED, 4, 1, NONE, REG_RM) \
    XV(N##_R64_RM64_EVEX, m, EVEX, prefixes, 2, 0x0466, R, 0, 1, 0, GQ, NONE, EQ, 8, 1, NONE, REG_RM) \
    XV(N##_R32_R32_RM32, m, EVEX, prefixes, 2, 0x0466, R, 0, 0, 0, GD, GD, ED, 4, 1, ND, VVVV_REG_RM) \
    XV(N##_R64_R64_RM64, m, EVEX, prefixes, 2, 0x0466, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, ND, VVVV_REG_RM)

// A 32- or 64-bit register to memory: RAO-INT's atomic operations (aadd, aand with 66, aor with F2 and axor with F3)
// and movdiri.
#define BUSTER_X86_EVEX_M_R_FORMS(XV, N, m, prefixes, opcode) \
    XV(N##_M32_R32_EVEX, m, EVEX, prefixes, 2, opcode, R, 0, 0, 0, GD, NONE, M, 4, 1, NONE, RM_REG) \
    XV(N##_M64_R64_EVEX, m, EVEX, prefixes, 2, opcode, R, 0, 1, 0, GQ, NONE, M, 8, 1, NONE, RM_REG)

// APX's forms of the legacy arithmetic in map 4, K as for BUSTER_X86_ARITHMETIC_FORMS: the legacy forms but the
// accumulator's, with `evex` NONE (`suffix` _EVEX) or NF (_NF).
#define BUSTER_X86_EVEX_ARITHMETIC_FORMS(XV, N, m, K, evex, suffix) \
    XV(N##_RM8_R8##suffix, m, EVEX, 0, 2, 0x0400 + 8 * K, R, 0, 0, 0, GB, NONE, EB, 1, 1, evex, RM_REG) \
    XV(N##_RM16_R16##suffix, m, EVEX, 0x66, 2, 0x0401 + 8 * K, R, 0, 0, 0, GW, NONE, EW, 2, 1, evex, RM_REG) \
    XV(N##_RM32_R32##suffix, m, EVEX, 0, 2, 0x0401 + 8 * K, R, 0, 0, 0, GD, NONE, ED, 4, 1, evex, RM_REG) \
    XV(N##_RM64_R64##suffix, m, EVEX, 0, 2, 0x0401 + 8 * K, R, 0, 1, 0, GQ, NONE, EQ, 8, 1, evex, RM_REG) \
    XV(N##_R8_RM8##suffix, m, EVEX, 0, 2, 0x0402 + 8 * K, R, 0, 0, 0, GB, NONE, EB, 1, 1, evex, REG_RM) \
    XV(N##_R16_RM16##suffix, m, EVEX, 0x66, 2, 0x0403 + 8 * K, R, 0, 0, 0, GW, NONE, EW, 2, 1, evex, REG_RM) \
    XV(N##_R32_RM32##suffix, m, EVEX, 0, 2, 0x0403 + 8 * K, R, 0, 0, 0, GD, NONE, ED, 4, 1, evex, REG_RM) \
    XV(N##_R64_RM64##suffix, m, EVEX, 0, 2, 0x0403 + 8 * K, R, 0, 1, 0, GQ, NONE, EQ, 8, 1, evex, REG_RM) \
    XV(N##_RM8_IMM8##suffix, m, EVEX, 0, 2, 0x0480, D##K, 1, 0, 0, NONE, NONE, EB, 1, 1, evex, RM_IMM) \
    XV(N##_RM16_IMM16##suffix, m, EVEX, 0x66, 2, 0x0481, D##K, 2, 0, 0, NONE, NONE, EW, 2, 1, evex, RM_IMM) \
    XV(N##_RM32_IMM32##suffix, m, EVEX, 0, 2, 0x0481, D##K, 4, 0, 0, NONE, NONE, ED, 4, 1, evex, RM_IMM) \
    XV(N##_RM64_IMM32##suffix, m, EVEX, 0, 2, 0x0481, D##K, 4, 1, 0, NONE, NONE, EQ, 8, 1, evex, RM_IMM) \
    XV(N##_RM16_IMM8##suffix, m, EVEX, 0x66, 2, 0x0483, D##K, 1, 0, 0, NONE, NONE, EW, 2, 1, evex, RM_IMM) \
    XV(N##_RM32_IMM8##suffix, m, EVEX, 0, 2, 0x0483, D##K, 1, 0, 0, NONE, NONE, ED, 4, 1, evex, RM_IMM) \
    XV(N##_RM64_IMM8##suffix, m, EVEX, 0, 2, 0x0483, D##K, 1, 1, 0, NONE, NONE, EQ, 8, 1, evex, RM_IMM)

// The same with a new destination in EVEX.vvvv (ND) and the sources as the two-operand forms' operands, with `evex` ND
// (and `suffix` empty) or ND_NF (_NF).
#define BUSTER_X86_EVEX_ND_ARITHMETIC_FORMS(XV, N, m, K, evex, suffix) \
    XV(N##_R8_RM8_R8##suffix, m, EVEX, 0, 2, 0x0400 + 8 * K, R, 0, 0, 0, GB, GB, EB, 1, 1, evex, VVVV_RM_REG) \
    XV(N##_R16_RM16_R16##suffix, m, EVEX, 0x66, 2, 0x0401 + 8 * K, R, 0, 0, 0, GW, GW, EW, 2, 1, evex, VVVV_RM_REG) \
    XV(N##_R32_RM32_R32##suffix, m, EVEX, 0, 2, 0x0401 + 8 * K, R, 0, 0, 0, GD, GD, ED, 4, 1, evex, VVVV_RM_REG) \
    XV(N##_R64_RM64_R64##suffix, m, EVEX, 0, 2, 0x0401 + 8 * K, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, evex, VVVV_RM_REG) \
    XV(N##_R8_R8_RM8##suffix, m, EVEX, 0, 2, 0x0402 + 8 * K, R, 0, 0, 0, GB, GB, EB, 1, 1, evex, VVVV_REG_RM) \
    XV(N##_R16_R16_RM16##suffix, m, EVEX, 0x66, 2, 0x0403 + 8 * K, R, 0, 0, 0, GW, GW, EW, 2, 1, evex, VVVV_REG_RM) \
    XV(N##_R32_R32_RM32##suffix, m, EVEX, 0, 2, 0x0403 + 8 * K, R, 0, 0, 0, GD, GD, ED, 4, 1, evex, VVVV_REG_RM) \
    XV(N##_R64_R64_RM64##suffix, m, EVEX, 0, 2, 0x0403 + 8 * K, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, evex, VVVV_REG_RM) \
    XV(N##_R8_RM8_IMM8##suffix, m, EVEX, 0, 2, 0x0480, D##K, 1, 0, 0, NONE, GB, EB, 1, 1, evex, VVVV_RM_IMM) \
    XV(N##_R16_RM16_IMM16##suffix, m, EVEX, 0x66, 2, 0x0481, D##K, 2, 0, 0, NONE, GW, EW, 2, 1, evex, VVVV_RM_IMM) \
    XV(N##_R32_RM32_IMM32##suffix, m, EVEX, 0, 2, 0x0481, D##K, 4, 0, 0, NONE, GD, ED, 4, 1, evex, VVVV_RM_IMM) \
    XV(N##_R64_RM64_IMM32##suffix, m, EVEX, 0, 2, 0x0481, D##K, 4, 1, 0, NONE, GQ, EQ, 8, 1, evex, VVVV_RM_IMM) \
    XV(N##_R16_RM16_IMM8##suffix, m, EVEX, 0x66, 2, 0x0483, D##K, 1, 0, 0, NONE, GW, EW, 2, 1, evex, VVVV_RM_IMM) \
    XV(N##_R32_RM32_IMM8##suffix, m, EVEX, 0, 2, 0x0483, D##K, 1, 0, 0, NONE, GD, ED, 4, 1, evex, VVVV_RM_IMM) \
    XV(N##_R64_RM64_IMM8##suffix, m, EVEX, 0, 2, 0x0483, D##K, 1, 1, 0, NONE, GQ, EQ, 8, 1, evex, VVVV_RM_IMM)

// All four of an operation that sets the flags without reading them: the two-operand forms and the ND ones, each also
// with NF.
#define BUSTER_X86_EVEX_ARITHMETIC_NF_FORMS(XV, N, m, K) \
    BUSTER_X86_EVEX_ARITHMETIC_FORMS(XV, N, m, K, NONE, _EVEX) \
    BUSTER_X86_EVEX_ARITHMETIC_FORMS(XV, N, m, K, NF, _NF) \
    BUSTER_X86_EVEX_ND_ARITHMETIC_FORMS(XV, N, m, K, ND, ) \
    BUSTER_X86_EVEX_ND_ARITHMETIC_FORMS(XV, N, m, K, ND_NF, _NF)

// The shifts and rotates in map 4, K as for BUSTER_X86_SHIFT_FORMS: by 1, by CL and by an 8-bit immediate, with `evex`
// NONE (`suffix` _EVEX) or NF (_NF); and with a new destination, `evex` ND (`suffix` empty) or ND_NF (_NF).
#define BUSTER_X86_EVEX_SHIFT_FORMS(XV, N, m, K, evex, suffix) \
    XV(N##_RM8_1##suffix, m, EVEX, 0, 2, 0x04d0, D##K, 0, 0, 0, NONE, NONE, EB, 1, 1, evex, RM_ONE) \
    XV(N##_RM16_1##suffix, m, EVEX, 0x66, 2, 0x04d1, D##K, 0, 0, 0, NONE, NONE, EW, 2, 1, evex, RM_ONE) \
    XV(N##_RM32_1##suffix, m, EVEX, 0, 2, 0x04d1, D##K, 0, 0, 0, NONE, NONE, ED, 4, 1, evex, RM_ONE) \
    XV(N##_RM64_1##suffix, m, EVEX, 0, 2, 0x04d1, D##K, 0, 1, 0, NONE, NONE, EQ, 8, 1, evex, RM_ONE) \
    XV(N##_RM8_CL##suffix, m, EVEX, 0, 2, 0x04d2, D##K, 0, 0, 0, NONE, NONE, EB, 1, 1, evex, RM_CL) \
    XV(N##_RM16_CL##suffix, m, EVEX, 0x66, 2, 0x04d3, D##K, 0, 0, 0, NONE, NONE, EW, 2, 1, evex, RM_CL) \
    XV(N##_RM32_CL##suffix, m, EVEX, 0, 2, 0x04d3, D##K, 0, 0, 0, NONE, NONE, ED, 4, 1, evex, RM_CL) \
    XV(N##_RM64_CL##suffix, m, EVEX, 0, 2, 0x04d3, D##K, 0, 1, 0, NONE, NONE, EQ, 8, 1, evex, RM_CL) \
    XV(N##_RM8_IMM8##suffix, m, EVEX, 0, 2, 0x04c0, D##K, 1, 0, 0, NONE, NONE, EB, 1, 1, evex, RM_IMM) \
    XV(N##_RM16_IMM8##suffix, m, EVEX, 0x66, 2, 0x04c1, D##K, 1, 0, 0, NONE, NONE, EW, 2, 1, evex, RM_IMM) \
    XV(N##_RM32_IMM8##suffix, m, EVEX, 0, 2, 0x04c1, D##K, 1, 0, 0, NONE, NONE, ED, 4, 1, evex, RM_IMM) \
    XV(N##_RM64_IMM8##suffix, m, EVEX, 0, 2, 0x04c1, D##K, 1, 1, 0, NONE, NONE, EQ, 8, 1, evex, RM_IMM)

#define BUSTER_X86_EVEX_ND_SHIFT_FORMS(XV, N, m, K, evex, suffix) \
    XV(N##_R8_RM8_1##suffix, m, EVEX, 0, 2, 0x04d0, D##K, 0, 0, 0, NONE, GB, EB, 1, 1, evex, VVVV_RM_ONE) \
    XV(N##_R16_RM16_1##suffix, m, EVEX, 0x66, 2, 0x04d1, D##K, 0, 0, 0, NONE, GW, EW, 2, 1, evex, VVVV_RM_ONE) \
    XV(N##_R32_RM32_1##suffix, m, EVEX, 0, 2, 0x04d1, D##K, 0, 0, 0, NONE, GD, ED, 4, 1, evex, VVVV_RM_ONE) \
    XV(N##_R64_RM64_1##suffix, m, EVEX, 0, 2, 0x04d1, D##K, 0, 1, 0, NONE, GQ, EQ, 8, 1, evex, VVVV_RM_ONE) \
    XV(N##_R8_RM8_CL##suffix, m, EVEX, 0, 2, 0x04d2, D##K, 0, 0, 0, NONE, GB, EB, 1, 1, evex, VVVV_RM_CL) \
    XV(N##_R16_RM16_CL##suffix, m, EVEX, 0x66, 2, 0x04d3, D##K, 0, 0, 0, NONE, GW, EW, 2, 1, evex, VVVV_RM_CL) \
    XV(N##_R32_RM32_CL##suffix, m, EVEX, 0, 2, 0x04d3, D##K, 0, 0, 0, NONE, GD, ED, 4, 1, evex, VVVV_RM_CL) \
    XV(N##_R64_RM64_CL##suffix, m, EVEX, 0, 2, 0x04d3, D##K, 0, 1, 0, NONE, GQ, EQ, 8, 1, evex, VVVV_RM_CL) \
    XV(N##_R8_RM8_IMM8##suffix, m, EVEX, 0, 2, 0x04c0, D##K, 1, 0, 0, NONE, GB, EB, 1, 1, evex, VVVV_RM_IMM) \
    XV(N##_R16_RM16_IMM8##suffix, m, EVEX, 0x66, 2, 0x04c1, D##K, 1, 0, 0, NONE, GW, EW, 2, 1, evex, VVVV_RM_IMM) \
    XV(N##_R32_RM32_IMM8##suffix, m, EVEX, 0, 2, 0x04c1, D##K, 1, 0, 0, NONE, GD, ED, 4, 1, evex, VVVV_RM_IMM) \
    XV(N##_R64_RM64_IMM8##suffix, m, EVEX, 0, 2, 0x04c1, D##K, 1, 1, 0, NONE, GQ, EQ, 8, 1, evex, VVVV_RM_IMM)

#define BUSTER_X86_EVEX_SHIFT_NF_FORMS(XV, N, m, K) \
    BUSTER_X86_EVEX_SHIFT_FORMS(XV, N, m, K, NONE, _EVEX) \
    BUSTER_X86_EVEX_SHIFT_FORMS(XV, N, m, K, NF, _NF) \
    BUSTER_X86_EVEX_ND_SHIFT_FORMS(XV, N, m, K, ND, ) \
    BUSTER_X86_EVEX_ND_SHIFT_FORMS(XV, N, m, K, ND_NF, _NF)

// Group 3's and group 4/5's instructions with one operand in map 4 (`opcode8` and `opcode` 0x04F6 and 0x04F7, or 0x04FE
// and 0x04FF), with `evex` and `suffix` as for the arithmetic: in place (not, neg, inc and dec), or into or from
// RDX:RAX (mul, imul, div and idiv); and with a new destination (the first four).
#define BUSTER_X86_EVEX_UNARY_FORMS(XV, N, m, opcode8, opcode, K, evex, suffix) \
    XV(N##_RM8##suffix, m, EVEX, 0, 2, opcode8, D##K, 0, 0, 0, NONE, NONE, EB, 1, 1, evex, RM) \
    XV(N##_RM16##suffix, m, EVEX, 0x66, 2, opcode, D##K, 0, 0, 0, NONE, NONE, EW, 2, 1, evex, RM) \
    XV(N##_RM32##suffix, m, EVEX, 0, 2, opcode, D##K, 0, 0, 0, NONE, NONE, ED, 4, 1, evex, RM) \
    XV(N##_RM64##suffix, m, EVEX, 0, 2, opcode, D##K, 0, 1, 0, NONE, NONE, EQ, 8, 1, evex, RM)

#define BUSTER_X86_EVEX_ND_UNARY_FORMS(XV, N, m, opcode8, opcode, K, evex, suffix) \
    XV(N##_R8_RM8##suffix, m, EVEX, 0, 2, opcode8, D##K, 0, 0, 0, NONE, GB, EB, 1, 1, evex, VVVV_RM) \
    XV(N##_R16_RM16##suffix, m, EVEX, 0x66, 2, opcode, D##K, 0, 0, 0, NONE, GW, EW, 2, 1, evex, VVVV_RM) \
    XV(N##_R32_RM32##suffix, m, EVEX, 0, 2, opcode, D##K, 0, 0, 0, NONE, GD, ED, 4, 1, evex, VVVV_RM) \
    XV(N##_R64_RM64##suffix, m, EVEX, 0, 2, opcode, D##K, 0, 1, 0, NONE, GQ, EQ, 8, 1, evex, VVVV_RM)

// imul's forms with two and three operands in map 4, with `evex` NONE (`suffix` _EVEX) or NF (_NF): reg by rm (AF), and
// rm by an immediate into reg (6B and 69); and with a new destination, `evex` ND (`suffix` empty) or ND_NF (_NF).
#define BUSTER_X86_EVEX_IMUL_FORMS(XV, evex, suffix) \
    XV(IMUL_R16_RM16##suffix, "imul", EVEX, 0x66, 2, 0x04af, R, 0, 0, 0, GW, NONE, EW, 2, 1, evex, REG_RM) \
    XV(IMUL_R32_RM32##suffix, "imul", EVEX, 0, 2, 0x04af, R, 0, 0, 0, GD, NONE, ED, 4, 1, evex, REG_RM) \
    XV(IMUL_R64_RM64##suffix, "imul", EVEX, 0, 2, 0x04af, R, 0, 1, 0, GQ, NONE, EQ, 8, 1, evex, REG_RM) \
    XV(IMUL_R16_RM16_IMM8##suffix, "imul", EVEX, 0x66, 2, 0x046b, R, 1, 0, 0, GW, NONE, EW, 2, 1, evex, REG_RM_IMM) \
    XV(IMUL_R32_RM32_IMM8##suffix, "imul", EVEX, 0, 2, 0x046b, R, 1, 0, 0, GD, NONE, ED, 4, 1, evex, REG_RM_IMM) \
    XV(IMUL_R64_RM64_IMM8##suffix, "imul", EVEX, 0, 2, 0x046b, R, 1, 1, 0, GQ, NONE, EQ, 8, 1, evex, REG_RM_IMM) \
    XV(IMUL_R16_RM16_IMM16##suffix, "imul", EVEX, 0x66, 2, 0x0469, R, 2, 0, 0, GW, NONE, EW, 2, 1, evex, REG_RM_IMM) \
    XV(IMUL_R32_RM32_IMM32##suffix, "imul", EVEX, 0, 2, 0x0469, R, 4, 0, 0, GD, NONE, ED, 4, 1, evex, REG_RM_IMM) \
    XV(IMUL_R64_RM64_IMM32##suffix, "imul", EVEX, 0, 2, 0x0469, R, 4, 1, 0, GQ, NONE, EQ, 8, 1, evex, REG_RM_IMM)

#define BUSTER_X86_EVEX_ND_IMUL_FORMS(XV, evex, suffix) \
    XV(IMUL_R16_R16_RM16##suffix, "imul", EVEX, 0x66, 2, 0x04af, R, 0, 0, 0, GW, GW, EW, 2, 1, evex, VVVV_REG_RM) \
    XV(IMUL_R32_R32_RM32##suffix, "imul", EVEX, 0, 2, 0x04af, R, 0, 0, 0, GD, GD, ED, 4, 1, evex, VVVV_REG_RM) \
    XV(IMUL_R64_R64_RM64##suffix, "imul", EVEX, 0, 2, 0x04af, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, evex, VVVV_REG_RM)

// imul's 16-bit forms by an immediate with ZU (ND's bit), which zeroes the destination's upper bits (GNU as's imulzu),
// with `evex` ND (and `suffix` empty) or ND_NF (_NF).
#define BUSTER_X86_EVEX_IMULZU_FORMS(XV, evex, suffix) \
    XV(IMULZU_R16_RM16_IMM8##suffix, "imulzu", EVEX, 0x66, 2, 0x046b, R, 1, 0, 0, GW, NONE, EW, 2, 1, evex, REG_RM_IMM) \
    XV(IMULZU_R16_RM16_IMM16##suffix, "imulzu", EVEX, 0x66, 2, 0x0469, R, 2, 0, 0, GW, NONE, EW, 2, 1, evex, REG_RM_IMM)

// shld (`opcode` 0x0424) and shrd (0x042C) in map 4, by an 8-bit immediate and by CL (`opcode` + 0x81), with `evex` and
// `suffix` as for the arithmetic; and with a new destination.
#define BUSTER_X86_EVEX_DOUBLE_SHIFT_FORMS(XV, N, m, opcode, evex, suffix) \
    XV(N##_RM16_R16_IMM8##suffix, m, EVEX, 0x66, 2, opcode, R, 1, 0, 0, GW, NONE, EW, 2, 1, evex, RM_REG_IMM) \
    XV(N##_RM32_R32_IMM8##suffix, m, EVEX, 0, 2, opcode, R, 1, 0, 0, GD, NONE, ED, 4, 1, evex, RM_REG_IMM) \
    XV(N##_RM64_R64_IMM8##suffix, m, EVEX, 0, 2, opcode, R, 1, 1, 0, GQ, NONE, EQ, 8, 1, evex, RM_REG_IMM) \
    XV(N##_RM16_R16_CL##suffix, m, EVEX, 0x66, 2, opcode + 0x81, R, 0, 0, 0, GW, NONE, EW, 2, 1, evex, RM_REG_CL) \
    XV(N##_RM32_R32_CL##suffix, m, EVEX, 0, 2, opcode + 0x81, R, 0, 0, 0, GD, NONE, ED, 4, 1, evex, RM_REG_CL) \
    XV(N##_RM64_R64_CL##suffix, m, EVEX, 0, 2, opcode + 0x81, R, 0, 1, 0, GQ, NONE, EQ, 8, 1, evex, RM_REG_CL)

#define BUSTER_X86_EVEX_ND_DOUBLE_SHIFT_FORMS(XV, N, m, opcode, evex, suffix) \
    XV(N##_R16_RM16_R16_IMM8##suffix, m, EVEX, 0x66, 2, opcode, R, 1, 0, 0, GW, GW, EW, 2, 1, evex, VVVV_RM_REG_IMM) \
    XV(N##_R32_RM32_R32_IMM8##suffix, m, EVEX, 0, 2, opcode, R, 1, 0, 0, GD, GD, ED, 4, 1, evex, VVVV_RM_REG_IMM) \
    XV(N##_R64_RM64_R64_IMM8##suffix, m, EVEX, 0, 2, opcode, R, 1, 1, 0, GQ, GQ, EQ, 8, 1, evex, VVVV_RM_REG_IMM) \
    XV(N##_R16_RM16_R16_CL##suffix, m, EVEX, 0x66, 2, opcode + 0x81, R, 0, 0, 0, GW, GW, EW, 2, 1, evex, VVVV_RM_REG_CL) \
    XV(N##_R32_RM32_R32_CL##suffix, m, EVEX, 0, 2, opcode + 0x81, R, 0, 0, 0, GD, GD, ED, 4, 1, evex, VVVV_RM_REG_CL) \
    XV(N##_R64_RM64_R64_CL##suffix, m, EVEX, 0, 2, opcode + 0x81, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, evex, VVVV_RM_REG_CL)

#define BUSTER_X86_EVEX_DOUBLE_SHIFT_NF_FORMS(XV, N, m, opcode) \
    BUSTER_X86_EVEX_DOUBLE_SHIFT_FORMS(XV, N, m, opcode, NONE, _EVEX) \
    BUSTER_X86_EVEX_DOUBLE_SHIFT_FORMS(XV, N, m, opcode, NF, _NF) \
    BUSTER_X86_EVEX_ND_DOUBLE_SHIFT_FORMS(XV, N, m, opcode, ND, ) \
    BUSTER_X86_EVEX_ND_DOUBLE_SHIFT_FORMS(XV, N, m, opcode, ND_NF, _NF)

// The conditional forms in map 4 (K the condition): cmovcc with a new destination, and setcc (F2), and its ZU form
// (setzucc: ND's bit), which zeroes the destination's upper bits and takes only a register; and cfcmovcc, a conditional
// move that doesn't fault on memory it doesn't load or store (when the condition is false): into a register (neither
// ND nor NF), into a register or memory (NF), and with a new destination (ND and NF), which is the first source when
// the condition is false and the second when it's true.
#define BUSTER_X86_EVEX_CONDITION_FORMS(XV, CC, cc, K) \
    XV(CMOV##CC##_R16_R16_RM16, "cmov" cc, EVEX, 0x66, 2, 0x0440 + K, R, 0, 0, 0, GW, GW, EW, 2, 1, ND, VVVV_REG_RM) \
    XV(CMOV##CC##_R32_R32_RM32, "cmov" cc, EVEX, 0, 2, 0x0440 + K, R, 0, 0, 0, GD, GD, ED, 4, 1, ND, VVVV_REG_RM) \
    XV(CMOV##CC##_R64_R64_RM64, "cmov" cc, EVEX, 0, 2, 0x0440 + K, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, ND, VVVV_REG_RM) \
    XV(SET##CC##_RM8_EVEX, "set" cc, EVEX, 0xf2, 2, 0x0440 + K, D0, 0, 0, 0, NONE, NONE, EB, 1, 1, NONE, RM) \
    XV(SETZU##CC##_R8, "setzu" cc, EVEX, 0xf2, 2, 0x0440 + K, D0, 0, 0, 0, NONE, NONE, GB, 1, 1, ND, RM) \
    XV(CFCMOV##CC##_R16_RM16, "cfcmov" cc, EVEX, 0x66, 2, 0x0440 + K, R, 0, 0, 0, GW, NONE, EW, 2, 1, NONE, REG_RM) \
    XV(CFCMOV##CC##_R32_RM32, "cfcmov" cc, EVEX, 0, 2, 0x0440 + K, R, 0, 0, 0, GD, NONE, ED, 4, 1, NONE, REG_RM) \
    XV(CFCMOV##CC##_R64_RM64, "cfcmov" cc, EVEX, 0, 2, 0x0440 + K, R, 0, 1, 0, GQ, NONE, EQ, 8, 1, NONE, REG_RM) \
    XV(CFCMOV##CC##_RM16_R16, "cfcmov" cc, EVEX, 0x66, 2, 0x0440 + K, R, 0, 0, 0, GW, NONE, EW, 2, 1, NF, RM_REG) \
    XV(CFCMOV##CC##_RM32_R32, "cfcmov" cc, EVEX, 0, 2, 0x0440 + K, R, 0, 0, 0, GD, NONE, ED, 4, 1, NF, RM_REG) \
    XV(CFCMOV##CC##_RM64_R64, "cfcmov" cc, EVEX, 0, 2, 0x0440 + K, R, 0, 1, 0, GQ, NONE, EQ, 8, 1, NF, RM_REG) \
    XV(CFCMOV##CC##_R16_R16_RM16, "cfcmov" cc, EVEX, 0x66, 2, 0x0440 + K, R, 0, 0, 0, GW, GW, EW, 2, 1, ND_NF, VVVV_REG_RM) \
    XV(CFCMOV##CC##_R32_R32_RM32, "cfcmov" cc, EVEX, 0, 2, 0x0440 + K, R, 0, 0, 0, GD, GD, ED, 4, 1, ND_NF, VVVV_REG_RM) \
    XV(CFCMOV##CC##_R64_R64_RM64, "cfcmov" cc, EVEX, 0, 2, 0x0440 + K, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, ND_NF, VVVV_REG_RM)

// ccmpcc and ctestcc (K the source condition, whose 10 and 11 are always true and always false: t and f, not p and np),
// which compare or test as cmp and test do (map 4's 38 to 3B, 80 to 83 /7, 84, 85 and F6 and F7 /0) if the source
// condition holds, and otherwise set the flags to the default flags value in vvvv (OF, SF, ZF and CF; the others
// clear).
#define BUSTER_X86_EVEX_CCMP_FORMS(XV, CC, cc, K) \
    XV(CCMP##CC##_RM8_R8, "ccmp" cc, EVEX, 0, 2, 0x0438, R, 0, 0, 0, GB, DFV, EB, 1, 1, SCC(K), RM_REG) \
    XV(CCMP##CC##_RM16_R16, "ccmp" cc, EVEX, 0x66, 2, 0x0439, R, 0, 0, 0, GW, DFV, EW, 2, 1, SCC(K), RM_REG) \
    XV(CCMP##CC##_RM32_R32, "ccmp" cc, EVEX, 0, 2, 0x0439, R, 0, 0, 0, GD, DFV, ED, 4, 1, SCC(K), RM_REG) \
    XV(CCMP##CC##_RM64_R64, "ccmp" cc, EVEX, 0, 2, 0x0439, R, 0, 1, 0, GQ, DFV, EQ, 8, 1, SCC(K), RM_REG) \
    XV(CCMP##CC##_R8_RM8, "ccmp" cc, EVEX, 0, 2, 0x043a, R, 0, 0, 0, GB, DFV, EB, 1, 1, SCC(K), REG_RM) \
    XV(CCMP##CC##_R16_RM16, "ccmp" cc, EVEX, 0x66, 2, 0x043b, R, 0, 0, 0, GW, DFV, EW, 2, 1, SCC(K), REG_RM) \
    XV(CCMP##CC##_R32_RM32, "ccmp" cc, EVEX, 0, 2, 0x043b, R, 0, 0, 0, GD, DFV, ED, 4, 1, SCC(K), REG_RM) \
    XV(CCMP##CC##_R64_RM64, "ccmp" cc, EVEX, 0, 2, 0x043b, R, 0, 1, 0, GQ, DFV, EQ, 8, 1, SCC(K), REG_RM) \
    XV(CCMP##CC##_RM8_IMM8, "ccmp" cc, EVEX, 0, 2, 0x0480, D7, 1, 0, 0, NONE, DFV, EB, 1, 1, SCC(K), RM_IMM) \
    XV(CCMP##CC##_RM16_IMM16, "ccmp" cc, EVEX, 0x66, 2, 0x0481, D7, 2, 0, 0, NONE, DFV, EW, 2, 1, SCC(K), RM_IMM) \
    XV(CCMP##CC##_RM32_IMM32, "ccmp" cc, EVEX, 0, 2, 0x0481, D7, 4, 0, 0, NONE, DFV, ED, 4, 1, SCC(K), RM_IMM) \
    XV(CCMP##CC##_RM64_IMM32, "ccmp" cc, EVEX, 0, 2, 0x0481, D7, 4, 1, 0, NONE, DFV, EQ, 8, 1, SCC(K), RM_IMM) \
    XV(CCMP##CC##_RM16_IMM8, "ccmp" cc, EVEX, 0x66, 2, 0x0483, D7, 1, 0, 0, NONE, DFV, EW, 2, 1, SCC(K), RM_IMM) \
    XV(CCMP##CC##_RM32_IMM8, "ccmp" cc, EVEX, 0, 2, 0x0483, D7, 1, 0, 0, NONE, DFV, ED, 4, 1, SCC(K), RM_IMM) \
    XV(CCMP##CC##_RM64_IMM8, "ccmp" cc, EVEX, 0, 2, 0x0483, D7, 1, 1, 0, NONE, DFV, EQ, 8, 1, SCC(K), RM_IMM) \
    XV(CTEST##CC##_RM8_R8, "ctest" cc, EVEX, 0, 2, 0x0484, R, 0, 0, 0, GB, DFV, EB, 1, 1, SCC(K), RM_REG) \
    XV(CTEST##CC##_RM16_R16, "ctest" cc, EVEX, 0x66, 2, 0x0485, R, 0, 0, 0, GW, DFV, EW, 2, 1, SCC(K), RM_REG) \
    XV(CTEST##CC##_RM32_R32, "ctest" cc, EVEX, 0, 2, 0x0485, R, 0, 0, 0, GD, DFV, ED, 4, 1, SCC(K), RM_REG) \
    XV(CTEST##CC##_RM64_R64, "ctest" cc, EVEX, 0, 2, 0x0485, R, 0, 1, 0, GQ, DFV, EQ, 8, 1, SCC(K), RM_REG) \
    XV(CTEST##CC##_RM8_IMM8, "ctest" cc, EVEX, 0, 2, 0x04f6, D0, 1, 0, 0, NONE, DFV, EB, 1, 1, SCC(K), RM_IMM) \
    XV(CTEST##CC##_RM16_IMM16, "ctest" cc, EVEX, 0x66, 2, 0x04f7, D0, 2, 0, 0, NONE, DFV, EW, 2, 1, SCC(K), RM_IMM) \
    XV(CTEST##CC##_RM32_IMM32, "ctest" cc, EVEX, 0, 2, 0x04f7, D0, 4, 0, 0, NONE, DFV, ED, 4, 1, SCC(K), RM_IMM) \
    XV(CTEST##CC##_RM64_IMM32, "ctest" cc, EVEX, 0, 2, 0x04f7, D0, 4, 1, 0, NONE, DFV, EQ, 8, 1, SCC(K), RM_IMM)

#define BUSTER_X86_VECTOR_FORMS(XV) \
    BUSTER_X86_AVX_MOVE_FORMS(XV, VMOVUPS, "vmovups", 0, 0x0f10, 0x0f11) \
    BUSTER_X86_AVX_MOVE_FORMS(XV, VMOVUPD, "vmovupd", 0x66, 0x0f10, 0x0f11) \
    BUSTER_X86_AVX_MOVE_FORMS(XV, VMOVAPS, "vmovaps", 0, 0x0f28, 0x0f29) \
    BUSTER_X86_AVX_MOVE_FORMS(XV, VMOVAPD, "vmovapd", 0x66, 0x0f28, 0x0f29) \
    BUSTER_X86_AVX_MOVE_FORMS(XV, VMOVDQA, "vmovdqa", 0x66, 0x0f6f, 0x0f7f) \
    BUSTER_X86_AVX_MOVE_FORMS(XV, VMOVDQU, "vmovdqu", 0xf3, 0x0f6f, 0x0f7f) \
    XV(VMOVSS_X_M32, "vmovss", VEX, 0xf3, 2, 0x0f10, R, 0, 0, 0, V, NONE, M, 4, 1, NONE, REG_RM) \
    XV(VMOVSS_M32_X, "vmovss", VEX, 0xf3, 2, 0x0f11, R, 0, 0, 0, V, NONE, M, 4, 1, NONE, RM_REG) \
    XV(VMOVSS_X_X_X, "vmovss", VEX, 0xf3, 2, 0x0f10, R, 0, 0, 0, V, V, V, 4, 1, NONE, REG_VVVV_RM) \
    XV(VMOVSD_X_M64, "vmovsd", VEX, 0xf2, 2, 0x0f10, R, 0, 0, 0, V, NONE, M, 8, 1, NONE, REG_RM) \
    XV(VMOVSD_M64_X, "vmovsd", VEX, 0xf2, 2, 0x0f11, R, 0, 0, 0, V, NONE, M, 8, 1, NONE, RM_REG) \
    XV(VMOVSD_X_X_X, "vmovsd", VEX, 0xf2, 2, 0x0f10, R, 0, 0, 0, V, V, V, 8, 1, NONE, REG_VVVV_RM) \
    XV(VMOVD_X_RM32, "vmovd", VEX, 0x66, 2, 0x0f6e, R, 0, 0, 0, V, NONE, ED, 4, 1, NONE, REG_RM) \
    XV(VMOVD_RM32_X, "vmovd", VEX, 0x66, 2, 0x0f7e, R, 0, 0, 0, V, NONE, ED, 4, 1, NONE, RM_REG) \
    XV(VMOVQ_X_RM64, "vmovq", VEX, 0x66, 2, 0x0f6e, R, 0, 1, 0, V, NONE, EQ, 8, 1, NONE, REG_RM) \
    XV(VMOVQ_RM64_X, "vmovq", VEX, 0x66, 2, 0x0f7e, R, 0, 1, 0, V, NONE, EQ, 8, 1, NONE, RM_REG) \
    XV(VMOVQ_X_XM64, "vmovq", VEX, 0xf3, 2, 0x0f7e, R, 0, 0, 0, V, NONE, XM, 8, 1, NONE, REG_RM) \
    XV(VMOVQ_XM64_X, "vmovq", VEX, 0x66, 2, 0x0fd6, R, 0, 0, 0, V, NONE, XM, 8, 1, NONE, RM_REG) \
    XV(VBROADCASTSS_X_XM32, "vbroadcastss", VEX, 0x66, 3, 0x0f3818, R, 0, 0, 0, V, NONE, XM, 4, 1, NONE, REG_RM) \
    XV(VBROADCASTSS_Y_XM32, "vbroadcastss", VEX, 0x66, 3, 0x0f3818, R, 0, 0, 1, Y, NONE, XM, 4, 1, NONE, REG_RM) \
    XV(VBROADCASTSD_Y_XM64, "vbroadcastsd", VEX, 0x66, 3, 0x0f3819, R, 0, 0, 1, Y, NONE, XM, 8, 1, NONE, REG_RM) \
    XV(VPBROADCASTB_X_XM8, "vpbroadcastb", VEX, 0x66, 3, 0x0f3878, R, 0, 0, 0, V, NONE, XM, 1, 1, NONE, REG_RM) \
    XV(VPBROADCASTB_Y_XM8, "vpbroadcastb", VEX, 0x66, 3, 0x0f3878, R, 0, 0, 1, Y, NONE, XM, 1, 1, NONE, REG_RM) \
    XV(VPBROADCASTD_X_XM32, "vpbroadcastd", VEX, 0x66, 3, 0x0f3858, R, 0, 0, 0, V, NONE, XM, 4, 1, NONE, REG_RM) \
    XV(VPBROADCASTD_Y_XM32, "vpbroadcastd", VEX, 0x66, 3, 0x0f3858, R, 0, 0, 1, Y, NONE, XM, 4, 1, NONE, REG_RM) \
    XV(VPBROADCASTQ_X_XM64, "vpbroadcastq", VEX, 0x66, 3, 0x0f3859, R, 0, 0, 0, V, NONE, XM, 8, 1, NONE, REG_RM) \
    XV(VPBROADCASTQ_Y_XM64, "vpbroadcastq", VEX, 0x66, 3, 0x0f3859, R, 0, 0, 1, Y, NONE, XM, 8, 1, NONE, REG_RM) \
    XV(VINSERTI128_Y_Y_XM128_IMM8, "vinserti128", VEX, 0x66, 3, 0x0f3a38, R, 1, 0, 1, Y, Y, XM, 16, 1, NONE, REG_VVVV_RM_IMM) \
    XV(VINSERTF128_Y_Y_XM128_IMM8, "vinsertf128", VEX, 0x66, 3, 0x0f3a18, R, 1, 0, 1, Y, Y, XM, 16, 1, NONE, REG_VVVV_RM_IMM) \
    XV(VEXTRACTI128_XM128_Y_IMM8, "vextracti128", VEX, 0x66, 3, 0x0f3a39, R, 1, 0, 1, Y, NONE, XM, 16, 1, NONE, RM_REG_IMM) \
    XV(VEXTRACTF128_XM128_Y_IMM8, "vextractf128", VEX, 0x66, 3, 0x0f3a19, R, 1, 0, 1, Y, NONE, XM, 16, 1, NONE, RM_REG_IMM) \
    XV(VPERM2I128_Y_Y_YM256_IMM8, "vperm2i128", VEX, 0x66, 3, 0x0f3a46, R, 1, 0, 1, Y, Y, YM, 32, 1, NONE, REG_VVVV_RM_IMM) \
    XV(VPERMQ_Y_YM256_IMM8, "vpermq", VEX, 0x66, 3, 0x0f3a00, R, 1, 1, 1, Y, NONE, YM, 32, 1, NONE, REG_RM_IMM) \
    XV(VPERMD_Y_Y_YM256, "vpermd", VEX, 0x66, 3, 0x0f3836, R, 0, 0, 1, Y, Y, YM, 32, 1, NONE, REG_VVVV_RM) \
    XV(VPMOVMSKB_R32_X, "vpmovmskb", VEX, 0x66, 2, 0x0fd7, R, 0, 0, 0, GD, NONE, V, 0, 1, NONE, REG_RM) \
    XV(VPMOVMSKB_R32_Y, "vpmovmskb", VEX, 0x66, 2, 0x0fd7, R, 0, 0, 1, GD, NONE, Y, 0, 1, NONE, REG_RM) \
    XV(VMOVMSKPS_R32_X, "vmovmskps", VEX, 0, 2, 0x0f50, R, 0, 0, 0, GD, NONE, V, 0, 1, NONE, REG_RM) \
    XV(VMOVMSKPS_R32_Y, "vmovmskps", VEX, 0, 2, 0x0f50, R, 0, 0, 1, GD, NONE, Y, 0, 1, NONE, REG_RM) \
    XV(VZEROUPPER, "vzeroupper", VEX, 0, 2, 0x0f77, NO, 0, 0, 0, NONE, NONE, NONE, 0, 1, NONE, NONE) \
    XV(VZEROALL, "vzeroall", VEX, 0, 2, 0x0f77, NO, 0, 0, 1, NONE, NONE, NONE, 0, 1, NONE, NONE) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VADDPS, "vaddps", 0, 2, 0x0f58, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VADDPD, "vaddpd", 0x66, 2, 0x0f58, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VSUBPS, "vsubps", 0, 2, 0x0f5c, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VSUBPD, "vsubpd", 0x66, 2, 0x0f5c, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VMULPS, "vmulps", 0, 2, 0x0f59, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VMULPD, "vmulpd", 0x66, 2, 0x0f59, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VDIVPS, "vdivps", 0, 2, 0x0f5e, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VDIVPD, "vdivpd", 0x66, 2, 0x0f5e, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VMINPS, "vminps", 0, 2, 0x0f5d, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VMAXPS, "vmaxps", 0, 2, 0x0f5f, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VANDPS, "vandps", 0, 2, 0x0f54, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VANDNPS, "vandnps", 0, 2, 0x0f55, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VORPS, "vorps", 0, 2, 0x0f56, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VXORPS, "vxorps", 0, 2, 0x0f57, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VXORPD, "vxorpd", 0x66, 2, 0x0f57, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VUNPCKLPS, "vunpcklps", 0, 2, 0x0f14, 0) \
    BUSTER_X86_AVX_UNARY_FORMS(XV, VSQRTPS, "vsqrtps", 0, 2, 0x0f51) \
    BUSTER_X86_AVX_UNARY_FORMS(XV, VSQRTPD, "vsqrtpd", 0x66, 2, 0x0f51) \
    BUSTER_X86_AVX_UNARY_FORMS(XV, VCVTDQ2PS, "vcvtdq2ps", 0, 2, 0x0f5b) \
    BUSTER_X86_AVX_UNARY_FORMS(XV, VCVTTPS2DQ, "vcvttps2dq", 0xf3, 2, 0x0f5b) \
    BUSTER_X86_AVX_SCALAR_FORMS(XV, VADD, "vadd", 0x58) \
    BUSTER_X86_AVX_SCALAR_FORMS(XV, VMUL, "vmul", 0x59) \
    BUSTER_X86_AVX_SCALAR_FORMS(XV, VSUB, "vsub", 0x5c) \
    BUSTER_X86_AVX_SCALAR_FORMS(XV, VMIN, "vmin", 0x5d) \
    BUSTER_X86_AVX_SCALAR_FORMS(XV, VDIV, "vdiv", 0x5e) \
    BUSTER_X86_AVX_SCALAR_FORMS(XV, VMAX, "vmax", 0x5f) \
    BUSTER_X86_AVX_SCALAR_FORMS(XV, VSQRT, "vsqrt", 0x51) \
    XV(VUCOMISS_X_XM32, "vucomiss", VEX, 0, 2, 0x0f2e, R, 0, 0, 0, V, NONE, XM, 4, 1, NONE, REG_RM) \
    XV(VUCOMISD_X_XM64, "vucomisd", VEX, 0x66, 2, 0x0f2e, R, 0, 0, 0, V, NONE, XM, 8, 1, NONE, REG_RM) \
    XV(VCOMISS_X_XM32, "vcomiss", VEX, 0, 2, 0x0f2f, R, 0, 0, 0, V, NONE, XM, 4, 1, NONE, REG_RM) \
    XV(VCOMISD_X_XM64, "vcomisd", VEX, 0x66, 2, 0x0f2f, R, 0, 0, 0, V, NONE, XM, 8, 1, NONE, REG_RM) \
    XV(VCVTSI2SS_X_X_RM32, "vcvtsi2ss", VEX, 0xf3, 2, 0x0f2a, R, 0, 0, 0, V, V, ED, 4, 1, NONE, REG_VVVV_RM) \
    XV(VCVTSI2SS_X_X_RM64, "vcvtsi2ss", VEX, 0xf3, 2, 0x0f2a, R, 0, 1, 0, V, V, EQ, 8, 1, NONE, REG_VVVV_RM) \
    XV(VCVTSI2SD_X_X_RM32, "vcvtsi2sd", VEX, 0xf2, 2, 0x0f2a, R, 0, 0, 0, V, V, ED, 4, 1, NONE, REG_VVVV_RM) \
    XV(VCVTSI2SD_X_X_RM64, "vcvtsi2sd", VEX, 0xf2, 2, 0x0f2a, R, 0, 1, 0, V, V, EQ, 8, 1, NONE, REG_VVVV_RM) \
    XV(VCVTTSS2SI_R32_XM32, "vcvttss2si", VEX, 0xf3, 2, 0x0f2c, R, 0, 0, 0, GD, NONE, XM, 4, 1, NONE, REG_RM) \
    XV(VCVTTSS2SI_R64_XM32, "vcvttss2si", VEX, 0xf3, 2, 0x0f2c, R, 0, 1, 0, GQ, NONE, XM, 4, 1, NONE, REG_RM) \
    XV(VCVTTSD2SI_R32_XM64, "vcvttsd2si", VEX, 0xf2, 2, 0x0f2c, R, 0, 0, 0, GD, NONE, XM, 8, 1, NONE, REG_RM) \
    XV(VCVTTSD2SI_R64_XM64, "vcvttsd2si", VEX, 0xf2, 2, 0x0f2c, R, 0, 1, 0, GQ, NONE, XM, 8, 1, NONE, REG_RM) \
    XV(VCVTSS2SD_X_X_XM32, "vcvtss2sd", VEX, 0xf3, 2, 0x0f5a, R, 0, 0, 0, V, V, XM, 4, 1, NONE, REG_VVVV_RM) \
    XV(VCVTSD2SS_X_X_XM64, "vcvtsd2ss", VEX, 0xf2, 2, 0x0f5a, R, 0, 0, 0, V, V, XM, 8, 1, NONE, REG_VVVV_RM) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPADDB, "vpaddb", 0x66, 2, 0x0ffc, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPADDW, "vpaddw", 0x66, 2, 0x0ffd, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPADDD, "vpaddd", 0x66, 2, 0x0ffe, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPADDQ, "vpaddq", 0x66, 2, 0x0fd4, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPSUBB, "vpsubb", 0x66, 2, 0x0ff8, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPSUBD, "vpsubd", 0x66, 2, 0x0ffa, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPSUBQ, "vpsubq", 0x66, 2, 0x0ffb, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPAND, "vpand", 0x66, 2, 0x0fdb, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPANDN, "vpandn", 0x66, 2, 0x0fdf, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPOR, "vpor", 0x66, 2, 0x0feb, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPXOR, "vpxor", 0x66, 2, 0x0fef, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPCMPEQB, "vpcmpeqb", 0x66, 2, 0x0f74, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPCMPEQD, "vpcmpeqd", 0x66, 2, 0x0f76, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPCMPGTD, "vpcmpgtd", 0x66, 2, 0x0f66, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMINUB, "vpminub", 0x66, 2, 0x0fda, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMAXUB, "vpmaxub", 0x66, 2, 0x0fde, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMULLW, "vpmullw", 0x66, 2, 0x0fd5, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPUNPCKLBW, "vpunpcklbw", 0x66, 2, 0x0f60, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPACKUSWB, "vpackuswb", 0x66, 2, 0x0f67, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPSADBW, "vpsadbw", 0x66, 2, 0x0ff6, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPSHUFB, "vpshufb", 0x66, 3, 0x0f3800, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMINUD, "vpminud", 0x66, 3, 0x0f383b, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMAXSD, "vpmaxsd", 0x66, 3, 0x0f383d, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMULLD, "vpmulld", 0x66, 3, 0x0f3840, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPSRLVD, "vpsrlvd", 0x66, 3, 0x0f3845, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPSRAVD, "vpsravd", 0x66, 3, 0x0f3846, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPSLLVD, "vpsllvd", 0x66, 3, 0x0f3847, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPSLLVQ, "vpsllvq", 0x66, 3, 0x0f3847, 1) \
    BUSTER_X86_FMA_PACKED_FORMS(XV, VFMADDSUB, "vfmaddsub", 0x96) \
    BUSTER_X86_FMA_PACKED_FORMS(XV, VFMSUBADD, "vfmsubadd", 0x97) \
    BUSTER_X86_FMA_PACKED_FORMS(XV, VFMADD, "vfmadd", 0x98) \
    BUSTER_X86_FMA_SCALAR_FORMS(XV, VFMADD, "vfmadd", 0x98) \
    BUSTER_X86_FMA_PACKED_FORMS(XV, VFMSUB, "vfmsub", 0x9a) \
    BUSTER_X86_FMA_SCALAR_FORMS(XV, VFMSUB, "vfmsub", 0x9a) \
    BUSTER_X86_FMA_PACKED_FORMS(XV, VFNMADD, "vfnmadd", 0x9c) \
    BUSTER_X86_FMA_SCALAR_FORMS(XV, VFNMADD, "vfnmadd", 0x9c) \
    BUSTER_X86_FMA_PACKED_FORMS(XV, VFNMSUB, "vfnmsub", 0x9e) \
    BUSTER_X86_FMA_SCALAR_FORMS(XV, VFNMSUB, "vfnmsub", 0x9e) \
    BUSTER_X86_AVX_SHIFT_FORMS(XV, VPSLLD, "vpslld", 0x72, 6) \
    BUSTER_X86_AVX_SHIFT_FORMS(XV, VPSRLD, "vpsrld", 0x72, 2) \
    BUSTER_X86_AVX_SHIFT_FORMS(XV, VPSRAD, "vpsrad", 0x72, 4) \
    BUSTER_X86_AVX_SHIFT_FORMS(XV, VPSLLQ, "vpsllq", 0x73, 6) \
    BUSTER_X86_AVX_SHIFT_FORMS(XV, VPSRLQ, "vpsrlq", 0x73, 2) \
    BUSTER_X86_AVX_SHIFT_FORMS(XV, VPSLLDQ, "vpslldq", 0x73, 7) \
    BUSTER_X86_AVX_SHIFT_FORMS(XV, VPSRLDQ, "vpsrldq", 0x73, 3) \
    XV(VPSHUFD_X_XM128_IMM8, "vpshufd", VEX, 0x66, 2, 0x0f70, R, 1, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM_IMM) \
    XV(VPSHUFD_Y_YM256_IMM8, "vpshufd", VEX, 0x66, 2, 0x0f70, R, 1, 0, 1, Y, NONE, YM, 32, 1, NONE, REG_RM_IMM) \
    BUSTER_X86_AVX_PACKED_IMMEDIATE_FORMS(XV, VSHUFPS, "vshufps", 0, 2, 0x0fc6, 0) \
    BUSTER_X86_AVX_PACKED_IMMEDIATE_FORMS(XV, VPALIGNR, "vpalignr", 0x66, 3, 0x0f3a0f, 0) \
    BUSTER_X86_AVX_PACKED_IMMEDIATE_FORMS(XV, VPBLENDD, "vpblendd", 0x66, 3, 0x0f3a02, 0) \
    BUSTER_X86_AVX_UNARY_FORMS(XV, VPTEST, "vptest", 0x66, 3, 0x0f3817) \
    XV(VPEXTRB_RM32_X_IMM8, "vpextrb", VEX, 0x66, 3, 0x0f3a14, R, 1, 0, 0, V, NONE, ED, 1, 1, NONE, RM_REG_IMM) \
    XV(VPEXTRD_RM32_X_IMM8, "vpextrd", VEX, 0x66, 3, 0x0f3a16, R, 1, 0, 0, V, NONE, ED, 4, 1, NONE, RM_REG_IMM) \
    XV(VPEXTRQ_RM64_X_IMM8, "vpextrq", VEX, 0x66, 3, 0x0f3a16, R, 1, 1, 0, V, NONE, EQ, 8, 1, NONE, RM_REG_IMM) \
    XV(VPINSRB_X_X_RM32_IMM8, "vpinsrb", VEX, 0x66, 3, 0x0f3a20, R, 1, 0, 0, V, V, ED, 1, 1, NONE, REG_VVVV_RM_IMM) \
    XV(VPINSRD_X_X_RM32_IMM8, "vpinsrd", VEX, 0x66, 3, 0x0f3a22, R, 1, 0, 0, V, V, ED, 4, 1, NONE, REG_VVVV_RM_IMM) \
    XV(VPINSRQ_X_X_RM64_IMM8, "vpinsrq", VEX, 0x66, 3, 0x0f3a22, R, 1, 1, 0, V, V, EQ, 8, 1, NONE, REG_VVVV_RM_IMM) \
    /* The rest of AVX and AVX2 (#21): floating-point arithmetic, compares, blends and shuffles. */ \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VADDSUBPD, "vaddsubpd", 0x66, 2, 0x0fd0, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VADDSUBPS, "vaddsubps", 0xf2, 2, 0x0fd0, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VANDPD, "vandpd", 0x66, 2, 0x0f54, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VANDNPD, "vandnpd", 0x66, 2, 0x0f55, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VORPD, "vorpd", 0x66, 2, 0x0f56, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VMINPD, "vminpd", 0x66, 2, 0x0f5d, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VMAXPD, "vmaxpd", 0x66, 2, 0x0f5f, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VHADDPD, "vhaddpd", 0x66, 2, 0x0f7c, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VHADDPS, "vhaddps", 0xf2, 2, 0x0f7c, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VHSUBPD, "vhsubpd", 0x66, 2, 0x0f7d, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VHSUBPS, "vhsubps", 0xf2, 2, 0x0f7d, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VUNPCKLPD, "vunpcklpd", 0x66, 2, 0x0f14, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VUNPCKHPS, "vunpckhps", 0, 2, 0x0f15, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VUNPCKHPD, "vunpckhpd", 0x66, 2, 0x0f15, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPERMILPS, "vpermilps", 0x66, 3, 0x0f380c, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPERMILPD, "vpermilpd", 0x66, 3, 0x0f380d, 0) \
    BUSTER_X86_AVX_PACKED_IMMEDIATE_FORMS(XV, VCMPPS, "vcmpps", 0, 2, 0x0fc2, 0) \
    BUSTER_X86_AVX_PACKED_IMMEDIATE_FORMS(XV, VCMPPD, "vcmppd", 0x66, 2, 0x0fc2, 0) \
    BUSTER_X86_AVX_PACKED_IMMEDIATE_FORMS(XV, VSHUFPD, "vshufpd", 0x66, 2, 0x0fc6, 0) \
    BUSTER_X86_AVX_PACKED_IMMEDIATE_FORMS(XV, VBLENDPS, "vblendps", 0x66, 3, 0x0f3a0c, 0) \
    BUSTER_X86_AVX_PACKED_IMMEDIATE_FORMS(XV, VBLENDPD, "vblendpd", 0x66, 3, 0x0f3a0d, 0) \
    BUSTER_X86_AVX_PACKED_IMMEDIATE_FORMS(XV, VDPPS, "vdpps", 0x66, 3, 0x0f3a40, 0) \
    XV(VDPPD_X_X_XM128_IMM8, "vdppd", VEX, 0x66, 3, 0x0f3a41, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IMM) \
    XV(VCMPSS_X_X_XM32_IMM8, "vcmpss", VEX, 0xf3, 2, 0x0fc2, R, 1, 0, 0, V, V, XM, 4, 1, NONE, REG_VVVV_RM_IMM) \
    XV(VCMPSD_X_X_XM64_IMM8, "vcmpsd", VEX, 0xf2, 2, 0x0fc2, R, 1, 0, 0, V, V, XM, 8, 1, NONE, REG_VVVV_RM_IMM) \
    XV(VROUNDSS_X_X_XM32_IMM8, "vroundss", VEX, 0x66, 3, 0x0f3a0a, R, 1, 0, 0, V, V, XM, 4, 1, NONE, REG_VVVV_RM_IMM) \
    XV(VROUNDSD_X_X_XM64_IMM8, "vroundsd", VEX, 0x66, 3, 0x0f3a0b, R, 1, 0, 0, V, V, XM, 8, 1, NONE, REG_VVVV_RM_IMM) \
    XV(VRSQRTSS_X_X_XM32, "vrsqrtss", VEX, 0xf3, 2, 0x0f52, R, 0, 0, 0, V, V, XM, 4, 1, NONE, REG_VVVV_RM) \
    XV(VRCPSS_X_X_XM32, "vrcpss", VEX, 0xf3, 2, 0x0f53, R, 0, 0, 0, V, V, XM, 4, 1, NONE, REG_VVVV_RM) \
    BUSTER_X86_AVX_UNARY_FORMS(XV, VRSQRTPS, "vrsqrtps", 0, 2, 0x0f52) \
    BUSTER_X86_AVX_UNARY_FORMS(XV, VRCPPS, "vrcpps", 0, 2, 0x0f53) \
    BUSTER_X86_AVX_UNARY_FORMS(XV, VTESTPS, "vtestps", 0x66, 3, 0x0f380e) \
    BUSTER_X86_AVX_UNARY_FORMS(XV, VTESTPD, "vtestpd", 0x66, 3, 0x0f380f) \
    BUSTER_X86_AVX_UNARY_IMMEDIATE_FORMS(XV, VROUNDPS, "vroundps", 0x66, 3, 0x0f3a08) \
    BUSTER_X86_AVX_UNARY_IMMEDIATE_FORMS(XV, VROUNDPD, "vroundpd", 0x66, 3, 0x0f3a09) \
    BUSTER_X86_AVX_UNARY_IMMEDIATE_FORMS(XV, VPERMILPS, "vpermilps", 0x66, 3, 0x0f3a04) \
    BUSTER_X86_AVX_UNARY_IMMEDIATE_FORMS(XV, VPERMILPD, "vpermilpd", 0x66, 3, 0x0f3a05) \
    XV(VPERMPS_Y_Y_YM256, "vpermps", VEX, 0x66, 3, 0x0f3816, R, 0, 0, 1, Y, Y, YM, 32, 1, NONE, REG_VVVV_RM) \
    XV(VPERMPD_Y_YM256_IMM8, "vpermpd", VEX, 0x66, 3, 0x0f3a01, R, 1, 1, 1, Y, NONE, YM, 32, 1, NONE, REG_RM_IMM) \
    XV(VPERM2F128_Y_Y_YM256_IMM8, "vperm2f128", VEX, 0x66, 3, 0x0f3a06, R, 1, 0, 1, Y, Y, YM, 32, 1, NONE, REG_VVVV_RM_IMM) \
    XV(VINSERTPS_X_X_XM32_IMM8, "vinsertps", VEX, 0x66, 3, 0x0f3a21, R, 1, 0, 0, V, V, XM, 4, 1, NONE, REG_VVVV_RM_IMM) \
    XV(VEXTRACTPS_RM32_X_IMM8, "vextractps", VEX, 0x66, 3, 0x0f3a17, R, 1, 0, 0, V, NONE, ED, 4, 1, NONE, RM_REG_IMM) \
    /* Moves, broadcasts and non-temporal moves. */ \
    XV(VMOVDDUP_X_XM64, "vmovddup", VEX, 0xf2, 2, 0x0f12, R, 0, 0, 0, V, NONE, XM, 8, 1, NONE, REG_RM) \
    XV(VMOVDDUP_Y_YM256, "vmovddup", VEX, 0xf2, 2, 0x0f12, R, 0, 0, 1, Y, NONE, YM, 32, 1, NONE, REG_RM) \
    BUSTER_X86_AVX_UNARY_FORMS(XV, VMOVSLDUP, "vmovsldup", 0xf3, 2, 0x0f12) \
    BUSTER_X86_AVX_UNARY_FORMS(XV, VMOVSHDUP, "vmovshdup", 0xf3, 2, 0x0f16) \
    XV(VMOVHLPS_X_X_X, "vmovhlps", VEX, 0, 2, 0x0f12, R, 0, 0, 0, V, V, V, 16, 1, NONE, REG_VVVV_RM) \
    XV(VMOVLHPS_X_X_X, "vmovlhps", VEX, 0, 2, 0x0f16, R, 0, 0, 0, V, V, V, 16, 1, NONE, REG_VVVV_RM) \
    BUSTER_X86_AVX_HALF_MOVE_FORMS(XV, VMOVLPS, "vmovlps", 0, 0x0f12) \
    BUSTER_X86_AVX_HALF_MOVE_FORMS(XV, VMOVHPS, "vmovhps", 0, 0x0f16) \
    BUSTER_X86_AVX_HALF_MOVE_FORMS(XV, VMOVLPD, "vmovlpd", 0x66, 0x0f12) \
    BUSTER_X86_AVX_HALF_MOVE_FORMS(XV, VMOVHPD, "vmovhpd", 0x66, 0x0f16) \
    XV(VMOVMSKPD_R32_X, "vmovmskpd", VEX, 0x66, 2, 0x0f50, R, 0, 0, 0, GD, NONE, V, 0, 1, NONE, REG_RM) \
    XV(VMOVMSKPD_R32_Y, "vmovmskpd", VEX, 0x66, 2, 0x0f50, R, 0, 0, 1, GD, NONE, Y, 0, 1, NONE, REG_RM) \
    BUSTER_X86_AVX_MASKED_MOVE_FORMS(XV, VMASKMOVPS, "vmaskmovps", 0x0f382c, 0x0f382e, 0) \
    BUSTER_X86_AVX_MASKED_MOVE_FORMS(XV, VMASKMOVPD, "vmaskmovpd", 0x0f382d, 0x0f382f, 0) \
    BUSTER_X86_AVX_MASKED_MOVE_FORMS(XV, VPMASKMOVD, "vpmaskmovd", 0x0f388c, 0x0f388e, 0) \
    BUSTER_X86_AVX_MASKED_MOVE_FORMS(XV, VPMASKMOVQ, "vpmaskmovq", 0x0f388c, 0x0f388e, 1) \
    XV(VMASKMOVDQU_X_X, "vmaskmovdqu", VEX, 0x66, 2, 0x0ff7, R, 0, 0, 0, V, NONE, V, 16, 1, NONE, REG_RM) \
    XV(VBROADCASTF128_Y_M128, "vbroadcastf128", VEX, 0x66, 3, 0x0f381a, R, 0, 0, 1, Y, NONE, M, 16, 1, NONE, REG_RM) \
    XV(VBROADCASTI128_Y_M128, "vbroadcasti128", VEX, 0x66, 3, 0x0f385a, R, 0, 0, 1, Y, NONE, M, 16, 1, NONE, REG_RM) \
    XV(VPBROADCASTW_X_XM16, "vpbroadcastw", VEX, 0x66, 3, 0x0f3879, R, 0, 0, 0, V, NONE, XM, 2, 1, NONE, REG_RM) \
    XV(VPBROADCASTW_Y_XM16, "vpbroadcastw", VEX, 0x66, 3, 0x0f3879, R, 0, 0, 1, Y, NONE, XM, 2, 1, NONE, REG_RM) \
    BUSTER_X86_AVX_STREAM_FORMS(XV, VMOVNTPS, "vmovntps", 0, 0x0f2b) \
    BUSTER_X86_AVX_STREAM_FORMS(XV, VMOVNTPD, "vmovntpd", 0x66, 0x0f2b) \
    BUSTER_X86_AVX_STREAM_FORMS(XV, VMOVNTDQ, "vmovntdq", 0x66, 0x0fe7) \
    XV(VMOVNTDQA_X_M128, "vmovntdqa", VEX, 0x66, 3, 0x0f382a, R, 0, 0, 0, V, NONE, M, 16, 1, NONE, REG_RM) \
    XV(VMOVNTDQA_Y_M256, "vmovntdqa", VEX, 0x66, 3, 0x0f382a, R, 0, 0, 1, Y, NONE, M, 32, 1, NONE, REG_RM) \
    XV(VLDDQU_X_M128, "vlddqu", VEX, 0xf2, 2, 0x0ff0, R, 0, 0, 0, V, NONE, M, 16, 1, NONE, REG_RM) \
    XV(VLDDQU_Y_M256, "vlddqu", VEX, 0xf2, 2, 0x0ff0, R, 0, 0, 1, Y, NONE, M, 32, 1, NONE, REG_RM) \
    XV(VLDMXCSR_M32, "vldmxcsr", VEX, 0, 2, 0x0fae, D2, 0, 0, 0, NONE, NONE, M, 4, 1, NONE, RM) \
    XV(VSTMXCSR_M32, "vstmxcsr", VEX, 0, 2, 0x0fae, D3, 0, 0, 0, NONE, NONE, M, 4, 1, NONE, RM) \
    /* Gathers, whose memory operand's index is a vector register (VSIB). */ \
    BUSTER_X86_AVX2_GATHER_FORMS(XV, VPGATHERDD, "vpgatherdd", 0x0f3890, 0, 32) \
    BUSTER_X86_AVX2_WIDENING_GATHER_FORMS(XV, VPGATHERDQ, "vpgatherdq", 0x0f3890) \
    BUSTER_X86_AVX2_NARROWING_GATHER_FORMS(XV, VPGATHERQD, "vpgatherqd", 0x0f3891) \
    BUSTER_X86_AVX2_GATHER_FORMS(XV, VPGATHERQQ, "vpgatherqq", 0x0f3891, 1, 64) \
    BUSTER_X86_AVX2_GATHER_FORMS(XV, VGATHERDPS, "vgatherdps", 0x0f3892, 0, 32) \
    BUSTER_X86_AVX2_WIDENING_GATHER_FORMS(XV, VGATHERDPD, "vgatherdpd", 0x0f3892) \
    BUSTER_X86_AVX2_NARROWING_GATHER_FORMS(XV, VGATHERQPS, "vgatherqps", 0x0f3893) \
    BUSTER_X86_AVX2_GATHER_FORMS(XV, VGATHERQPD, "vgatherqpd", 0x0f3893, 1, 64) \
    /* Conversions, with F16C's. */ \
    BUSTER_X86_AVX_UNARY_FORMS(XV, VCVTPS2DQ, "vcvtps2dq", 0x66, 2, 0x0f5b) \
    XV(VCVTDQ2PD_X_XM64, "vcvtdq2pd", VEX, 0xf3, 2, 0x0fe6, R, 0, 0, 0, V, NONE, XM, 8, 1, NONE, REG_RM) \
    XV(VCVTDQ2PD_Y_XM128, "vcvtdq2pd", VEX, 0xf3, 2, 0x0fe6, R, 0, 0, 1, Y, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VCVTPS2PD_X_XM64, "vcvtps2pd", VEX, 0, 2, 0x0f5a, R, 0, 0, 0, V, NONE, XM, 8, 1, NONE, REG_RM) \
    XV(VCVTPS2PD_Y_XM128, "vcvtps2pd", VEX, 0, 2, 0x0f5a, R, 0, 0, 1, Y, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VCVTPD2DQ_X_XM128, "vcvtpd2dq", VEX, 0xf2, 2, 0x0fe6, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VCVTPD2DQ_X_YM256, "vcvtpd2dq", VEX, 0xf2, 2, 0x0fe6, R, 0, 0, 1, V, NONE, YM, 32, 1, NONE, REG_RM) \
    XV(VCVTTPD2DQ_X_XM128, "vcvttpd2dq", VEX, 0x66, 2, 0x0fe6, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VCVTTPD2DQ_X_YM256, "vcvttpd2dq", VEX, 0x66, 2, 0x0fe6, R, 0, 0, 1, V, NONE, YM, 32, 1, NONE, REG_RM) \
    XV(VCVTPD2PS_X_XM128, "vcvtpd2ps", VEX, 0x66, 2, 0x0f5a, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VCVTPD2PS_X_YM256, "vcvtpd2ps", VEX, 0x66, 2, 0x0f5a, R, 0, 0, 1, V, NONE, YM, 32, 1, NONE, REG_RM) \
    XV(VCVTSS2SI_R32_XM32, "vcvtss2si", VEX, 0xf3, 2, 0x0f2d, R, 0, 0, 0, GD, NONE, XM, 4, 1, NONE, REG_RM) \
    XV(VCVTSS2SI_R64_XM32, "vcvtss2si", VEX, 0xf3, 2, 0x0f2d, R, 0, 1, 0, GQ, NONE, XM, 4, 1, NONE, REG_RM) \
    XV(VCVTSD2SI_R32_XM64, "vcvtsd2si", VEX, 0xf2, 2, 0x0f2d, R, 0, 0, 0, GD, NONE, XM, 8, 1, NONE, REG_RM) \
    XV(VCVTSD2SI_R64_XM64, "vcvtsd2si", VEX, 0xf2, 2, 0x0f2d, R, 0, 1, 0, GQ, NONE, XM, 8, 1, NONE, REG_RM) \
    XV(VCVTPH2PS_X_XM64, "vcvtph2ps", VEX, 0x66, 3, 0x0f3813, R, 0, 0, 0, V, NONE, XM, 8, 1, NONE, REG_RM) \
    XV(VCVTPH2PS_Y_XM128, "vcvtph2ps", VEX, 0x66, 3, 0x0f3813, R, 0, 0, 1, Y, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VCVTPS2PH_XM64_X_IMM8, "vcvtps2ph", VEX, 0x66, 3, 0x0f3a1d, R, 1, 0, 0, V, NONE, XM, 8, 1, NONE, RM_REG_IMM) \
    XV(VCVTPS2PH_XM128_Y_IMM8, "vcvtps2ph", VEX, 0x66, 3, 0x0f3a1d, R, 1, 0, 1, Y, NONE, XM, 16, 1, NONE, RM_REG_IMM) \
    /* Integer arithmetic, compares, packs, unpacks and shifts (AVX's at 128 bits, AVX2's at 256). */ \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPADDSB, "vpaddsb", 0x66, 2, 0x0fec, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPADDSW, "vpaddsw", 0x66, 2, 0x0fed, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPADDUSB, "vpaddusb", 0x66, 2, 0x0fdc, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPADDUSW, "vpaddusw", 0x66, 2, 0x0fdd, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPSUBW, "vpsubw", 0x66, 2, 0x0ff9, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPSUBSB, "vpsubsb", 0x66, 2, 0x0fe8, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPSUBSW, "vpsubsw", 0x66, 2, 0x0fe9, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPSUBUSB, "vpsubusb", 0x66, 2, 0x0fd8, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPSUBUSW, "vpsubusw", 0x66, 2, 0x0fd9, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPAVGB, "vpavgb", 0x66, 2, 0x0fe0, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPAVGW, "vpavgw", 0x66, 2, 0x0fe3, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPCMPEQW, "vpcmpeqw", 0x66, 2, 0x0f75, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPCMPEQQ, "vpcmpeqq", 0x66, 3, 0x0f3829, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPCMPGTB, "vpcmpgtb", 0x66, 2, 0x0f64, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPCMPGTW, "vpcmpgtw", 0x66, 2, 0x0f65, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPCMPGTQ, "vpcmpgtq", 0x66, 3, 0x0f3837, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMADDWD, "vpmaddwd", 0x66, 2, 0x0ff5, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMADDUBSW, "vpmaddubsw", 0x66, 3, 0x0f3804, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMAXSB, "vpmaxsb", 0x66, 3, 0x0f383c, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMAXSW, "vpmaxsw", 0x66, 2, 0x0fee, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMAXUW, "vpmaxuw", 0x66, 3, 0x0f383e, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMAXUD, "vpmaxud", 0x66, 3, 0x0f383f, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMINSB, "vpminsb", 0x66, 3, 0x0f3838, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMINSW, "vpminsw", 0x66, 2, 0x0fea, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMINSD, "vpminsd", 0x66, 3, 0x0f3839, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMINUW, "vpminuw", 0x66, 3, 0x0f383a, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMULHUW, "vpmulhuw", 0x66, 2, 0x0fe4, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMULHW, "vpmulhw", 0x66, 2, 0x0fe5, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMULHRSW, "vpmulhrsw", 0x66, 3, 0x0f380b, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMULUDQ, "vpmuludq", 0x66, 2, 0x0ff4, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMULDQ, "vpmuldq", 0x66, 3, 0x0f3828, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPSIGNB, "vpsignb", 0x66, 3, 0x0f3808, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPSIGNW, "vpsignw", 0x66, 3, 0x0f3809, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPSIGND, "vpsignd", 0x66, 3, 0x0f380a, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPHADDW, "vphaddw", 0x66, 3, 0x0f3801, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPHADDD, "vphaddd", 0x66, 3, 0x0f3802, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPHADDSW, "vphaddsw", 0x66, 3, 0x0f3803, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPHSUBW, "vphsubw", 0x66, 3, 0x0f3805, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPHSUBD, "vphsubd", 0x66, 3, 0x0f3806, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPHSUBSW, "vphsubsw", 0x66, 3, 0x0f3807, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPACKSSWB, "vpacksswb", 0x66, 2, 0x0f63, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPACKSSDW, "vpackssdw", 0x66, 2, 0x0f6b, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPACKUSDW, "vpackusdw", 0x66, 3, 0x0f382b, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPUNPCKLWD, "vpunpcklwd", 0x66, 2, 0x0f61, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPUNPCKLDQ, "vpunpckldq", 0x66, 2, 0x0f62, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPUNPCKLQDQ, "vpunpcklqdq", 0x66, 2, 0x0f6c, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPUNPCKHBW, "vpunpckhbw", 0x66, 2, 0x0f68, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPUNPCKHWD, "vpunpckhwd", 0x66, 2, 0x0f69, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPUNPCKHDQ, "vpunpckhdq", 0x66, 2, 0x0f6a, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPUNPCKHQDQ, "vpunpckhqdq", 0x66, 2, 0x0f6d, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPSRLVQ, "vpsrlvq", 0x66, 3, 0x0f3845, 1) \
    BUSTER_X86_AVX_PACKED_IMMEDIATE_FORMS(XV, VPBLENDW, "vpblendw", 0x66, 3, 0x0f3a0e, 0) \
    BUSTER_X86_AVX_PACKED_IMMEDIATE_FORMS(XV, VMPSADBW, "vmpsadbw", 0x66, 3, 0x0f3a42, 0) \
    BUSTER_X86_AVX_UNARY_FORMS(XV, VPABSB, "vpabsb", 0x66, 3, 0x0f381c) \
    BUSTER_X86_AVX_UNARY_FORMS(XV, VPABSW, "vpabsw", 0x66, 3, 0x0f381d) \
    BUSTER_X86_AVX_UNARY_FORMS(XV, VPABSD, "vpabsd", 0x66, 3, 0x0f381e) \
    XV(VPHMINPOSUW_X_XM128, "vphminposuw", VEX, 0x66, 3, 0x0f3841, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    BUSTER_X86_AVX_UNARY_IMMEDIATE_FORMS(XV, VPSHUFHW, "vpshufhw", 0xf3, 2, 0x0f70) \
    BUSTER_X86_AVX_UNARY_IMMEDIATE_FORMS(XV, VPSHUFLW, "vpshuflw", 0xf2, 2, 0x0f70) \
    BUSTER_X86_AVX_EXTEND_FORMS(XV, VPMOVSX, "vpmovsx", 0x20) \
    BUSTER_X86_AVX_EXTEND_FORMS(XV, VPMOVZX, "vpmovzx", 0x30) \
    BUSTER_X86_AVX_SHIFT_FORMS(XV, VPSRLW, "vpsrlw", 0x71, 2) \
    BUSTER_X86_AVX_SHIFT_FORMS(XV, VPSRAW, "vpsraw", 0x71, 4) \
    BUSTER_X86_AVX_SHIFT_FORMS(XV, VPSLLW, "vpsllw", 0x71, 6) \
    BUSTER_X86_AVX_SHIFT_COUNT_FORMS(XV, VPSRLW, "vpsrlw", 0xd1) \
    BUSTER_X86_AVX_SHIFT_COUNT_FORMS(XV, VPSRLD, "vpsrld", 0xd2) \
    BUSTER_X86_AVX_SHIFT_COUNT_FORMS(XV, VPSRLQ, "vpsrlq", 0xd3) \
    BUSTER_X86_AVX_SHIFT_COUNT_FORMS(XV, VPSRAW, "vpsraw", 0xe1) \
    BUSTER_X86_AVX_SHIFT_COUNT_FORMS(XV, VPSRAD, "vpsrad", 0xe2) \
    BUSTER_X86_AVX_SHIFT_COUNT_FORMS(XV, VPSLLW, "vpsllw", 0xf1) \
    BUSTER_X86_AVX_SHIFT_COUNT_FORMS(XV, VPSLLD, "vpslld", 0xf2) \
    BUSTER_X86_AVX_SHIFT_COUNT_FORMS(XV, VPSLLQ, "vpsllq", 0xf3) \
    XV(VPINSRW_X_X_RM32_IMM8, "vpinsrw", VEX, 0x66, 2, 0x0fc4, R, 1, 0, 0, V, V, ED, 2, 1, NONE, REG_VVVV_RM_IMM) \
    XV(VPEXTRW_R32_X_IMM8, "vpextrw", VEX, 0x66, 2, 0x0fc5, R, 1, 0, 0, GD, NONE, V, 0, 1, NONE, REG_RM_IMM) \
    XV(VPEXTRW_M16_X_IMM8, "vpextrw", VEX, 0x66, 3, 0x0f3a15, R, 1, 0, 0, V, NONE, M, 2, 1, NONE, RM_REG_IMM) \
    /* The string compares (and with W1, 64-bit lengths). */ \
    XV(VPCMPESTRM_X_XM128_IMM8, "vpcmpestrm", VEX, 0x66, 3, 0x0f3a60, R, 1, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM_IMM) \
    XV(VPCMPESTRI_X_XM128_IMM8, "vpcmpestri", VEX, 0x66, 3, 0x0f3a61, R, 1, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM_IMM) \
    XV(VPCMPISTRM_X_XM128_IMM8, "vpcmpistrm", VEX, 0x66, 3, 0x0f3a62, R, 1, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM_IMM) \
    XV(VPCMPISTRI_X_XM128_IMM8, "vpcmpistri", VEX, 0x66, 3, 0x0f3a63, R, 1, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM_IMM) \
    XV(VPCMPESTRMQ_X_XM128_IMM8, "vpcmpestrmq", VEX, 0x66, 3, 0x0f3a60, R, 1, 1, 0, V, NONE, XM, 16, 1, NONE, REG_RM_IMM) \
    XV(VPCMPESTRIQ_X_XM128_IMM8, "vpcmpestriq", VEX, 0x66, 3, 0x0f3a61, R, 1, 1, 0, V, NONE, XM, 16, 1, NONE, REG_RM_IMM) \
    /* AES (AVX's at 128 bits, and VAES's at 256), carry-less multiplication and GFNI. */ \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VAESENC, "vaesenc", 0x66, 3, 0x0f38dc, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VAESENCLAST, "vaesenclast", 0x66, 3, 0x0f38dd, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VAESDEC, "vaesdec", 0x66, 3, 0x0f38de, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VAESDECLAST, "vaesdeclast", 0x66, 3, 0x0f38df, 0) \
    XV(VAESIMC_X_XM128, "vaesimc", VEX, 0x66, 3, 0x0f38db, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VAESKEYGENASSIST_X_XM128_IMM8, "vaeskeygenassist", VEX, 0x66, 3, 0x0f3adf, R, 1, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM_IMM) \
    BUSTER_X86_AVX_PACKED_IMMEDIATE_FORMS(XV, VPCLMULQDQ, "vpclmulqdq", 0x66, 3, 0x0f3a44, 0) \
    BUSTER_X86_AVX_PACKED_IMMEDIATE_FORMS(XV, VGF2P8AFFINEQB, "vgf2p8affineqb", 0x66, 3, 0x0f3ace, 1) \
    BUSTER_X86_AVX_PACKED_IMMEDIATE_FORMS(XV, VGF2P8AFFINEINVQB, "vgf2p8affineinvqb", 0x66, 3, 0x0f3acf, 1) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VGF2P8MULB, "vgf2p8mulb", 0x66, 3, 0x0f38cf, 0) \
    /* Dot products (AVX-VNNI, AVX-VNNI-INT8 and AVX-VNNI-INT16) and 52-bit multiplications (AVX-IFMA). */ \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPDPBUSD, "vpdpbusd", 0x66, 3, 0x0f3850, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPDPBUSDS, "vpdpbusds", 0x66, 3, 0x0f3851, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPDPWSSD, "vpdpwssd", 0x66, 3, 0x0f3852, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPDPWSSDS, "vpdpwssds", 0x66, 3, 0x0f3853, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPDPBSSD, "vpdpbssd", 0xf2, 3, 0x0f3850, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPDPBSSDS, "vpdpbssds", 0xf2, 3, 0x0f3851, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPDPBSUD, "vpdpbsud", 0xf3, 3, 0x0f3850, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPDPBSUDS, "vpdpbsuds", 0xf3, 3, 0x0f3851, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPDPBUUD, "vpdpbuud", 0, 3, 0x0f3850, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPDPBUUDS, "vpdpbuuds", 0, 3, 0x0f3851, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPDPWSUD, "vpdpwsud", 0xf3, 3, 0x0f38d2, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPDPWSUDS, "vpdpwsuds", 0xf3, 3, 0x0f38d3, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPDPWUSD, "vpdpwusd", 0x66, 3, 0x0f38d2, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPDPWUSDS, "vpdpwusds", 0x66, 3, 0x0f38d3, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPDPWUUD, "vpdpwuud", 0, 3, 0x0f38d2, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPDPWUUDS, "vpdpwuuds", 0, 3, 0x0f38d3, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMADD52LUQ, "vpmadd52luq", 0x66, 3, 0x0f38b4, 1) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VPMADD52HUQ, "vpmadd52huq", 0x66, 3, 0x0f38b5, 1) \
    /* AVX-NE-CONVERT: BF16 and FP16 from memory to single precision (one broadcast, or the even or odd elements). */ \
    XV(VBCSTNEBF162PS_X_M16, "vbcstnebf162ps", VEX, 0xf3, 3, 0x0f38b1, R, 0, 0, 0, V, NONE, M, 2, 1, NONE, REG_RM) \
    XV(VBCSTNEBF162PS_Y_M16, "vbcstnebf162ps", VEX, 0xf3, 3, 0x0f38b1, R, 0, 0, 1, Y, NONE, M, 2, 1, NONE, REG_RM) \
    XV(VBCSTNESH2PS_X_M16, "vbcstnesh2ps", VEX, 0x66, 3, 0x0f38b1, R, 0, 0, 0, V, NONE, M, 2, 1, NONE, REG_RM) \
    XV(VBCSTNESH2PS_Y_M16, "vbcstnesh2ps", VEX, 0x66, 3, 0x0f38b1, R, 0, 0, 1, Y, NONE, M, 2, 1, NONE, REG_RM) \
    BUSTER_X86_AVX_LOAD_FORMS(XV, VCVTNEEBF162PS, "vcvtneebf162ps", 0xf3, 3, 0x0f38b0) \
    BUSTER_X86_AVX_LOAD_FORMS(XV, VCVTNEEPH2PS, "vcvtneeph2ps", 0x66, 3, 0x0f38b0) \
    BUSTER_X86_AVX_LOAD_FORMS(XV, VCVTNEOBF162PS, "vcvtneobf162ps", 0xf2, 3, 0x0f38b0) \
    BUSTER_X86_AVX_LOAD_FORMS(XV, VCVTNEOPH2PS, "vcvtneoph2ps", 0, 3, 0x0f38b0) \
    XV(VCVTNEPS2BF16_X_XM128, "vcvtneps2bf16", VEX, 0xf3, 3, 0x0f3872, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VCVTNEPS2BF16_X_YM256, "vcvtneps2bf16", VEX, 0xf3, 3, 0x0f3872, R, 0, 0, 1, V, NONE, YM, 32, 1, NONE, REG_RM) \
    /* SHA512 (registers only), SM3 and SM4. */ \
    XV(VSHA512MSG1_Y_X, "vsha512msg1", VEX, 0xf2, 3, 0x0f38cc, R, 0, 0, 1, Y, NONE, V, 16, 1, NONE, REG_RM) \
    XV(VSHA512MSG2_Y_Y, "vsha512msg2", VEX, 0xf2, 3, 0x0f38cd, R, 0, 0, 1, Y, NONE, Y, 32, 1, NONE, REG_RM) \
    XV(VSHA512RNDS2_Y_Y_X, "vsha512rnds2", VEX, 0xf2, 3, 0x0f38cb, R, 0, 0, 1, Y, Y, V, 16, 1, NONE, REG_VVVV_RM) \
    XV(VSM3MSG1_X_X_XM128, "vsm3msg1", VEX, 0, 3, 0x0f38da, R, 0, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM) \
    XV(VSM3MSG2_X_X_XM128, "vsm3msg2", VEX, 0x66, 3, 0x0f38da, R, 0, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM) \
    XV(VSM3RNDS2_X_X_XM128_IMM8, "vsm3rnds2", VEX, 0x66, 3, 0x0f3ade, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IMM) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VSM4KEY4, "vsm4key4", 0xf3, 3, 0x0f38da, 0) \
    BUSTER_X86_AVX_PACKED_FORMS(XV, VSM4RNDS4, "vsm4rnds4", 0xf2, 3, 0x0f38da, 0) \
    /* Four operands, the last in the immediate (/is4): AVX's variable blends and AMD's FMA4. */ \
    BUSTER_X86_AVX_IS4_FORMS(XV, VBLENDVPS, "vblendvps", 0x0f3a4a) \
    BUSTER_X86_AVX_IS4_FORMS(XV, VBLENDVPD, "vblendvpd", 0x0f3a4b) \
    BUSTER_X86_AVX_IS4_FORMS(XV, VPBLENDVB, "vpblendvb", 0x0f3a4c) \
    BUSTER_X86_FMA4_PACKED_FORMS(XV, VFMADDSUBPS, "vfmaddsubps", 0x0f3a5c) \
    BUSTER_X86_FMA4_PACKED_FORMS(XV, VFMADDSUBPD, "vfmaddsubpd", 0x0f3a5d) \
    BUSTER_X86_FMA4_PACKED_FORMS(XV, VFMSUBADDPS, "vfmsubaddps", 0x0f3a5e) \
    BUSTER_X86_FMA4_PACKED_FORMS(XV, VFMSUBADDPD, "vfmsubaddpd", 0x0f3a5f) \
    BUSTER_X86_FMA4_FORMS(XV, VFMADD, "vfmadd", 0x0f3a68) \
    BUSTER_X86_FMA4_FORMS(XV, VFMSUB, "vfmsub", 0x0f3a6c) \
    BUSTER_X86_FMA4_FORMS(XV, VFNMADD, "vfnmadd", 0x0f3a78) \
    BUSTER_X86_FMA4_FORMS(XV, VFNMSUB, "vfnmsub", 0x0f3a7c) \
    /* AMX: the tile configuration (64 bytes, which GNU as takes without a size), loads and stores through a SIB memory operand (whose index is the stride), AMX-TRANSPOSE's loads of a pair of tiles and transposes, and products of tiles (FP8's and the read-shared pair loads in VEX map 5). */ \
    BUSTER_X86_AMX_MEMORY_FORMS(XV, VEX, ) \
    XV(TILERELEASE, "tilerelease", VEX, 0, 3, 0x0f3849, D0, 0, 0, 0, NONE, NONE, NONE, 0, 1, NONE, NONE) \
    XV(TILEZERO_TMM, "tilezero", VEX, 0xf2, 3, 0x0f3849, R, 0, 0, 0, TMM, NONE, NONE, 0, 1, NONE, REG) \
    XV(TTRANSPOSED_TMM_TMM, "ttransposed", VEX, 0xf3, 3, 0x0f385f, R, 0, 0, 0, TMM, NONE, TMM, 0, 1, NONE, REG_RM) \
    XV(TCONJTFP16_TMM_TMM, "tconjtfp16", VEX, 0x66, 3, 0x0f386b, R, 0, 0, 0, TMM, NONE, TMM, 0, 1, NONE, REG_RM) \
    BUSTER_X86_AMX_TILE_FORM(XV, TDPBSSD, "tdpbssd", 0xf2, 3, 0x0f385e) \
    BUSTER_X86_AMX_TILE_FORM(XV, TDPBSUD, "tdpbsud", 0xf3, 3, 0x0f385e) \
    BUSTER_X86_AMX_TILE_FORM(XV, TDPBUSD, "tdpbusd", 0x66, 3, 0x0f385e) \
    BUSTER_X86_AMX_TILE_FORM(XV, TDPBUUD, "tdpbuud", 0, 3, 0x0f385e) \
    BUSTER_X86_AMX_TILE_FORM(XV, TDPBF16PS, "tdpbf16ps", 0xf3, 3, 0x0f385c) \
    BUSTER_X86_AMX_TILE_FORM(XV, TDPFP16PS, "tdpfp16ps", 0xf2, 3, 0x0f385c) \
    BUSTER_X86_AMX_TILE_FORM(XV, TCMMIMFP16PS, "tcmmimfp16ps", 0x66, 3, 0x0f386c) \
    BUSTER_X86_AMX_TILE_FORM(XV, TCMMRLFP16PS, "tcmmrlfp16ps", 0, 3, 0x0f386c) \
    BUSTER_X86_AMX_TILE_FORM(XV, TCONJTCMMIMFP16PS, "tconjtcmmimfp16ps", 0, 3, 0x0f386b) \
    BUSTER_X86_AMX_TILE_FORM(XV, TTCMMIMFP16PS, "ttcmmimfp16ps", 0xf2, 3, 0x0f386b) \
    BUSTER_X86_AMX_TILE_FORM(XV, TTCMMRLFP16PS, "ttcmmrlfp16ps", 0xf3, 3, 0x0f386b) \
    BUSTER_X86_AMX_TILE_FORM(XV, TTDPBF16PS, "ttdpbf16ps", 0xf3, 3, 0x0f386c) \
    BUSTER_X86_AMX_TILE_FORM(XV, TTDPFP16PS, "ttdpfp16ps", 0xf2, 3, 0x0f386c) \
    BUSTER_X86_AMX_TILE_FORM(XV, TMMULTF32PS, "tmmultf32ps", 0x66, 3, 0x0f3848) \
    BUSTER_X86_AMX_TILE_FORM(XV, TTMMULTF32PS, "ttmmultf32ps", 0, 3, 0x0f3848) \
    BUSTER_X86_AMX_TILE_FORM(XV, TDPBF8PS, "tdpbf8ps", 0, 2, 0x05fd) \
    BUSTER_X86_AMX_TILE_FORM(XV, TDPBHF8PS, "tdpbhf8ps", 0xf2, 2, 0x05fd) \
    BUSTER_X86_AMX_TILE_FORM(XV, TDPHBF8PS, "tdphbf8ps", 0xf3, 2, 0x05fd) \
    BUSTER_X86_AMX_TILE_FORM(XV, TDPHF8PS, "tdphf8ps", 0x66, 2, 0x05fd) \
    /* The MSR instructions with an immediate MSR index, in VEX map 7 (its number in the escape's place). */ \
    BUSTER_X86_MSR_IMMEDIATE_FORMS(XV, VEX, ) \
    /* AMD's XOP (VEX rows in maps 8 to 0A, whose prefix starts with 8F instead of C4), and vpermil2ps and vpermil2pd. */ \
    BUSTER_X86_AVX_UNARY_FORMS(XV, VFRCZPS, "vfrczps", 0, 2, 0x0980) \
    BUSTER_X86_AVX_UNARY_FORMS(XV, VFRCZPD, "vfrczpd", 0, 2, 0x0981) \
    XV(VFRCZSS_X_XM32, "vfrczss", VEX, 0, 2, 0x0982, R, 0, 0, 0, V, NONE, XM, 4, 1, NONE, REG_RM) \
    XV(VFRCZSD_X_XM64, "vfrczsd", VEX, 0, 2, 0x0983, R, 0, 0, 0, V, NONE, XM, 8, 1, NONE, REG_RM) \
    BUSTER_X86_AVX_IS4_SWAP_FORMS(XV, VPCMOV, "vpcmov", 0, 2, 0x08a2) \
    XV(VPPERM_X_X_XM128_X, "vpperm", VEX, 0, 2, 0x08a3, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IS4) \
    XV(VPPERM_X_X_X_XM128, "vpperm", VEX, 0, 2, 0x08a3, R, 1, 1, 0, V, V, XM, 16, 1, NONE, REG_VVVV_IS4_RM) \
    BUSTER_X86_AVX_IS4_SWAP_SELECTOR_FORMS(XV, VPERMIL2PS, "vpermil2ps", 0x66, 3, 0x0f3a48) \
    BUSTER_X86_AVX_IS4_SWAP_SELECTOR_FORMS(XV, VPERMIL2PD, "vpermil2pd", 0x66, 3, 0x0f3a49) \
    BUSTER_X86_XOP_COMPARE_FORMS(XV, VPCOM, "vpcom", 0x08cc) \
    BUSTER_X86_XOP_COMPARE_FORMS(XV, VPCOMU, "vpcomu", 0x08ec) \
    BUSTER_X86_XOP_SHIFT_FORMS(XV, VPROT, "vprot", 0x0990) \
    BUSTER_X86_XOP_SHIFT_FORMS(XV, VPSHL, "vpshl", 0x0994) \
    BUSTER_X86_XOP_SHIFT_FORMS(XV, VPSHA, "vpsha", 0x0998) \
    BUSTER_X86_XOP_ROTATE_IMMEDIATE_FORMS(XV, VPROT, "vprot", 0x08c0) \
    /* Horizontal additions and subtractions of adjacent elements into wider ones. */ \
    XV(VPHADDBW_X_XM128, "vphaddbw", VEX, 0, 2, 0x09c1, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VPHADDBD_X_XM128, "vphaddbd", VEX, 0, 2, 0x09c2, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VPHADDBQ_X_XM128, "vphaddbq", VEX, 0, 2, 0x09c3, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VPHADDWD_X_XM128, "vphaddwd", VEX, 0, 2, 0x09c6, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VPHADDWQ_X_XM128, "vphaddwq", VEX, 0, 2, 0x09c7, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VPHADDDQ_X_XM128, "vphadddq", VEX, 0, 2, 0x09cb, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VPHADDUBW_X_XM128, "vphaddubw", VEX, 0, 2, 0x09d1, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VPHADDUBD_X_XM128, "vphaddubd", VEX, 0, 2, 0x09d2, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VPHADDUBQ_X_XM128, "vphaddubq", VEX, 0, 2, 0x09d3, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VPHADDUWD_X_XM128, "vphadduwd", VEX, 0, 2, 0x09d6, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VPHADDUWQ_X_XM128, "vphadduwq", VEX, 0, 2, 0x09d7, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VPHADDUDQ_X_XM128, "vphaddudq", VEX, 0, 2, 0x09db, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VPHSUBBW_X_XM128, "vphsubbw", VEX, 0, 2, 0x09e1, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VPHSUBWD_X_XM128, "vphsubwd", VEX, 0, 2, 0x09e2, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    XV(VPHSUBDQ_X_XM128, "vphsubdq", VEX, 0, 2, 0x09e3, R, 0, 0, 0, V, NONE, XM, 16, 1, NONE, REG_RM) \
    /* Multiplications with an accumulator from the immediate (/is4), s saturating. */ \
    XV(VPMACSSWW_X_X_XM128_X, "vpmacssww", VEX, 0, 2, 0x0885, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IS4) \
    XV(VPMACSSWD_X_X_XM128_X, "vpmacsswd", VEX, 0, 2, 0x0886, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IS4) \
    XV(VPMACSSDQL_X_X_XM128_X, "vpmacssdql", VEX, 0, 2, 0x0887, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IS4) \
    XV(VPMACSSDD_X_X_XM128_X, "vpmacssdd", VEX, 0, 2, 0x088e, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IS4) \
    XV(VPMACSSDQH_X_X_XM128_X, "vpmacssdqh", VEX, 0, 2, 0x088f, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IS4) \
    XV(VPMACSWW_X_X_XM128_X, "vpmacsww", VEX, 0, 2, 0x0895, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IS4) \
    XV(VPMACSWD_X_X_XM128_X, "vpmacswd", VEX, 0, 2, 0x0896, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IS4) \
    XV(VPMACSDQL_X_X_XM128_X, "vpmacsdql", VEX, 0, 2, 0x0897, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IS4) \
    XV(VPMACSDD_X_X_XM128_X, "vpmacsdd", VEX, 0, 2, 0x089e, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IS4) \
    XV(VPMACSDQH_X_X_XM128_X, "vpmacsdqh", VEX, 0, 2, 0x089f, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IS4) \
    XV(VPMADCSSWD_X_X_XM128_X, "vpmadcsswd", VEX, 0, 2, 0x08a6, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IS4) \
    XV(VPMADCSWD_X_X_XM128_X, "vpmadcswd", VEX, 0, 2, 0x08b6, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IS4) \
    /* TBM's bit manipulations and bextr with a 32-bit immediate (the field's start and length), and LWP's. */ \
    BUSTER_X86_TBM_FORMS(XV, BLCFILL, "blcfill", 0x0901, 1) \
    BUSTER_X86_TBM_FORMS(XV, BLSFILL, "blsfill", 0x0901, 2) \
    BUSTER_X86_TBM_FORMS(XV, BLCS, "blcs", 0x0901, 3) \
    BUSTER_X86_TBM_FORMS(XV, TZMSK, "tzmsk", 0x0901, 4) \
    BUSTER_X86_TBM_FORMS(XV, BLCIC, "blcic", 0x0901, 5) \
    BUSTER_X86_TBM_FORMS(XV, BLSIC, "blsic", 0x0901, 6) \
    BUSTER_X86_TBM_FORMS(XV, T1MSKC, "t1mskc", 0x0901, 7) \
    BUSTER_X86_TBM_FORMS(XV, BLCMSK, "blcmsk", 0x0902, 1) \
    BUSTER_X86_TBM_FORMS(XV, BLCI, "blci", 0x0902, 6) \
    XV(BEXTR_R32_RM32_IMM32, "bextr", VEX, 0, 2, 0x0a10, R, 4, 0, 0, GD, NONE, ED, 4, 1, NONE, REG_RM_IMM) \
    XV(BEXTR_R64_RM64_IMM32, "bextr", VEX, 0, 2, 0x0a10, R, 4, 1, 0, GQ, NONE, EQ, 8, 1, NONE, REG_RM_IMM) \
    XV(LLWPCB_R32, "llwpcb", VEX, 0, 2, 0x0912, D0, 0, 0, 0, NONE, NONE, GD, 4, 1, NONE, RM) \
    XV(LLWPCB_R64, "llwpcb", VEX, 0, 2, 0x0912, D0, 0, 1, 0, NONE, NONE, GQ, 8, 1, NONE, RM) \
    XV(SLWPCB_R32, "slwpcb", VEX, 0, 2, 0x0912, D1, 0, 0, 0, NONE, NONE, GD, 4, 1, NONE, RM) \
    XV(SLWPCB_R64, "slwpcb", VEX, 0, 2, 0x0912, D1, 0, 1, 0, NONE, NONE, GQ, 8, 1, NONE, RM) \
    XV(LWPINS_R32_RM32_IMM32, "lwpins", VEX, 0, 2, 0x0a12, D0, 4, 0, 0, NONE, GD, ED, 4, 1, NONE, VVVV_RM_IMM) \
    XV(LWPINS_R64_RM32_IMM32, "lwpins", VEX, 0, 2, 0x0a12, D0, 4, 1, 0, NONE, GQ, ED, 4, 1, NONE, VVVV_RM_IMM) \
    XV(LWPVAL_R32_RM32_IMM32, "lwpval", VEX, 0, 2, 0x0a12, D1, 4, 0, 0, NONE, GD, ED, 4, 1, NONE, VVVV_RM_IMM) \
    XV(LWPVAL_R64_RM32_IMM32, "lwpval", VEX, 0, 2, 0x0a12, D1, 4, 1, 0, NONE, GQ, ED, 4, 1, NONE, VVVV_RM_IMM) \
    BUSTER_X86_BMI_FLAG_FORMS(XV, VEX, NONE, ) \
    BUSTER_X86_BMI2_FORMS(XV, VEX, ) \
    BUSTER_X86_KMOV_FORMS(XV, VEX, ) \
    BUSTER_X86_MASK_BINARY_FORMS(XV, KAND, "kand", 0x0f41) \
    BUSTER_X86_MASK_BINARY_FORMS(XV, KANDN, "kandn", 0x0f42) \
    BUSTER_X86_MASK_BINARY_FORMS(XV, KOR, "kor", 0x0f45) \
    BUSTER_X86_MASK_BINARY_FORMS(XV, KXNOR, "kxnor", 0x0f46) \
    BUSTER_X86_MASK_BINARY_FORMS(XV, KXOR, "kxor", 0x0f47) \
    BUSTER_X86_MASK_BINARY_FORMS(XV, KADD, "kadd", 0x0f4a) \
    XV(KUNPCKBW_K_K_K, "kunpckbw", VEX, 0x66, 2, 0x0f4b, R, 0, 0, 1, K, K, K, 0, 1, NONE, REG_VVVV_RM) \
    XV(KUNPCKWD_K_K_K, "kunpckwd", VEX, 0, 2, 0x0f4b, R, 0, 0, 1, K, K, K, 0, 1, NONE, REG_VVVV_RM) \
    XV(KUNPCKDQ_K_K_K, "kunpckdq", VEX, 0, 2, 0x0f4b, R, 0, 1, 1, K, K, K, 0, 1, NONE, REG_VVVV_RM) \
    BUSTER_X86_MASK_UNARY_FORMS(XV, KNOT, "knot", 0x0f44) \
    BUSTER_X86_MASK_UNARY_FORMS(XV, KORTEST, "kortest", 0x0f98) \
    BUSTER_X86_MASK_UNARY_FORMS(XV, KTEST, "ktest", 0x0f99) \
    BUSTER_X86_MASK_SHIFT_FORMS(XV, KSHIFTR, "kshiftr", 0x0f3a30) \
    BUSTER_X86_MASK_SHIFT_FORMS(XV, KSHIFTL, "kshiftl", 0x0f3a32) \
    BUSTER_X86_AVX512_MOVE_FORMS(XV, VMOVUPS, "vmovups", 0, 0, 0x0f10, 0x0f11) \
    BUSTER_X86_AVX512VL_MOVE_FORMS(XV, VMOVUPS, "vmovups", 0, 0, 0x0f10, 0x0f11, _EVEX) \
    BUSTER_X86_AVX512_MOVE_FORMS(XV, VMOVUPD, "vmovupd", 0x66, 1, 0x0f10, 0x0f11) \
    BUSTER_X86_AVX512VL_MOVE_FORMS(XV, VMOVUPD, "vmovupd", 0x66, 1, 0x0f10, 0x0f11, _EVEX) \
    BUSTER_X86_AVX512_MOVE_FORMS(XV, VMOVAPS, "vmovaps", 0, 0, 0x0f28, 0x0f29) \
    BUSTER_X86_AVX512VL_MOVE_FORMS(XV, VMOVAPS, "vmovaps", 0, 0, 0x0f28, 0x0f29, _EVEX) \
    BUSTER_X86_AVX512_MOVE_FORMS(XV, VMOVAPD, "vmovapd", 0x66, 1, 0x0f28, 0x0f29) \
    BUSTER_X86_AVX512VL_MOVE_FORMS(XV, VMOVAPD, "vmovapd", 0x66, 1, 0x0f28, 0x0f29, _EVEX) \
    BUSTER_X86_AVX512_MOVE_FORMS(XV, VMOVDQA32, "vmovdqa32", 0x66, 0, 0x0f6f, 0x0f7f) \
    BUSTER_X86_AVX512VL_MOVE_FORMS(XV, VMOVDQA32, "vmovdqa32", 0x66, 0, 0x0f6f, 0x0f7f, ) \
    BUSTER_X86_AVX512_MOVE_FORMS(XV, VMOVDQA64, "vmovdqa64", 0x66, 1, 0x0f6f, 0x0f7f) \
    BUSTER_X86_AVX512VL_MOVE_FORMS(XV, VMOVDQA64, "vmovdqa64", 0x66, 1, 0x0f6f, 0x0f7f, ) \
    BUSTER_X86_AVX512_MOVE_FORMS(XV, VMOVDQU8, "vmovdqu8", 0xf2, 0, 0x0f6f, 0x0f7f) \
    BUSTER_X86_AVX512VL_MOVE_FORMS(XV, VMOVDQU8, "vmovdqu8", 0xf2, 0, 0x0f6f, 0x0f7f, ) \
    BUSTER_X86_AVX512_MOVE_FORMS(XV, VMOVDQU16, "vmovdqu16", 0xf2, 1, 0x0f6f, 0x0f7f) \
    BUSTER_X86_AVX512VL_MOVE_FORMS(XV, VMOVDQU16, "vmovdqu16", 0xf2, 1, 0x0f6f, 0x0f7f, ) \
    BUSTER_X86_AVX512_MOVE_FORMS(XV, VMOVDQU32, "vmovdqu32", 0xf3, 0, 0x0f6f, 0x0f7f) \
    BUSTER_X86_AVX512VL_MOVE_FORMS(XV, VMOVDQU32, "vmovdqu32", 0xf3, 0, 0x0f6f, 0x0f7f, ) \
    BUSTER_X86_AVX512_MOVE_FORMS(XV, VMOVDQU64, "vmovdqu64", 0xf3, 1, 0x0f6f, 0x0f7f) \
    BUSTER_X86_AVX512VL_MOVE_FORMS(XV, VMOVDQU64, "vmovdqu64", 0xf3, 1, 0x0f6f, 0x0f7f, ) \
    BUSTER_X86_AVX512_STREAM_FORMS(XV, VMOVNTPS, "vmovntps", 0, 0x0f2b, 0) \
    BUSTER_X86_AVX512_STREAM_FORMS(XV, VMOVNTPD, "vmovntpd", 0x66, 0x0f2b, 1) \
    BUSTER_X86_AVX512_STREAM_FORMS(XV, VMOVNTDQ, "vmovntdq", 0x66, 0x0fe7, 0) \
    XV(VMOVNTDQA_Z_M512, "vmovntdqa", EVEX, 0x66, 3, 0x0f382a, R, 0, 0, 2, Z, NONE, M, 64, 64, NONE, REG_RM) \
    XV(VMOVNTDQA_X_M128_EVEX, "vmovntdqa", EVEX, 0x66, 3, 0x0f382a, R, 0, 0, 0, V, NONE, M, 16, 16, NONE, REG_RM) \
    XV(VMOVNTDQA_Y_M256_EVEX, "vmovntdqa", EVEX, 0x66, 3, 0x0f382a, R, 0, 0, 1, Y, NONE, M, 32, 32, NONE, REG_RM) \
    BUSTER_X86_AVX512_UNARY_FORM(XV, VMOVSLDUP, "vmovsldup", 0xf3, 2, 0x0f12, 0) \
    BUSTER_X86_AVX512VL_UNARY_FORMS(XV, VMOVSLDUP, "vmovsldup", 0xf3, 2, 0x0f12, 0, _EVEX) \
    BUSTER_X86_AVX512_UNARY_FORM(XV, VMOVSHDUP, "vmovshdup", 0xf3, 2, 0x0f16, 0) \
    BUSTER_X86_AVX512VL_UNARY_FORMS(XV, VMOVSHDUP, "vmovshdup", 0xf3, 2, 0x0f16, 0, _EVEX) \
    BUSTER_X86_AVX512_UNARY_FORM(XV, VMOVDDUP, "vmovddup", 0xf2, 2, 0x0f12, 1) \
    XV(VMOVDDUP_X_XM64_EVEX, "vmovddup", EVEX, 0xf2, 2, 0x0f12, R, 0, 1, 0, V, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(VMOVDDUP_Y_YM256_EVEX, "vmovddup", EVEX, 0xf2, 2, 0x0f12, R, 0, 1, 1, Y, NONE, YM, 32, 32, KZ, REG_RM) \
    XV(VMOVD_X_RM32_EVEX, "vmovd", EVEX, 0x66, 2, 0x0f6e, R, 0, 0, 0, V, NONE, ED, 4, 4, NONE, REG_RM) \
    XV(VMOVD_RM32_X_EVEX, "vmovd", EVEX, 0x66, 2, 0x0f7e, R, 0, 0, 0, V, NONE, ED, 4, 4, NONE, RM_REG) \
    XV(VMOVQ_X_RM64_EVEX, "vmovq", EVEX, 0x66, 2, 0x0f6e, R, 0, 1, 0, V, NONE, EQ, 8, 8, NONE, REG_RM) \
    XV(VMOVQ_RM64_X_EVEX, "vmovq", EVEX, 0x66, 2, 0x0f7e, R, 0, 1, 0, V, NONE, EQ, 8, 8, NONE, RM_REG) \
    XV(VMOVSS_X_M32_EVEX, "vmovss", EVEX, 0xf3, 2, 0x0f10, R, 0, 0, 0, V, NONE, M, 4, 4, KZ, REG_RM) \
    XV(VMOVSS_M32_X_EVEX, "vmovss", EVEX, 0xf3, 2, 0x0f11, R, 0, 0, 0, V, NONE, M, 4, 4, K, RM_REG) \
    XV(VMOVSS_X_X_X_EVEX, "vmovss", EVEX, 0xf3, 2, 0x0f10, R, 0, 0, 0, V, V, V, 4, 4, KZ, REG_VVVV_RM) \
    XV(VMOVSD_X_M64_EVEX, "vmovsd", EVEX, 0xf2, 2, 0x0f10, R, 0, 1, 0, V, NONE, M, 8, 8, KZ, REG_RM) \
    XV(VMOVSD_M64_X_EVEX, "vmovsd", EVEX, 0xf2, 2, 0x0f11, R, 0, 1, 0, V, NONE, M, 8, 8, K, RM_REG) \
    XV(VMOVSD_X_X_X_EVEX, "vmovsd", EVEX, 0xf2, 2, 0x0f10, R, 0, 1, 0, V, V, V, 8, 8, KZ, REG_VVVV_RM) \
    XV(VMOVQ_X_XM64_EVEX, "vmovq", EVEX, 0xf3, 2, 0x0f7e, R, 0, 1, 0, V, NONE, XM, 8, 8, NONE, REG_RM) \
    XV(VMOVQ_XM64_X_EVEX, "vmovq", EVEX, 0x66, 2, 0x0fd6, R, 0, 1, 0, V, NONE, XM, 8, 8, NONE, RM_REG) \
    BUSTER_X86_AVX512_HALF_MOVE_FORMS(XV, VMOVLPS, "vmovlps", 0, 0x0f12, 0) \
    BUSTER_X86_AVX512_HALF_MOVE_FORMS(XV, VMOVHPS, "vmovhps", 0, 0x0f16, 0) \
    BUSTER_X86_AVX512_HALF_MOVE_FORMS(XV, VMOVLPD, "vmovlpd", 0x66, 0x0f12, 1) \
    BUSTER_X86_AVX512_HALF_MOVE_FORMS(XV, VMOVHPD, "vmovhpd", 0x66, 0x0f16, 1) \
    XV(VMOVHLPS_X_X_X_EVEX, "vmovhlps", EVEX, 0, 2, 0x0f12, R, 0, 0, 0, V, V, V, 16, 16, NONE, REG_VVVV_RM) \
    XV(VMOVLHPS_X_X_X_EVEX, "vmovlhps", EVEX, 0, 2, 0x0f16, R, 0, 0, 0, V, V, V, 16, 16, NONE, REG_VVVV_RM) \
    XV(VPBROADCASTB_Z_XM8, "vpbroadcastb", EVEX, 0x66, 3, 0x0f3878, R, 0, 0, 2, Z, NONE, XM, 1, 1, KZ, REG_RM) \
    XV(VPBROADCASTW_Z_XM16, "vpbroadcastw", EVEX, 0x66, 3, 0x0f3879, R, 0, 0, 2, Z, NONE, XM, 2, 2, KZ, REG_RM) \
    XV(VPBROADCASTB_X_XM8_EVEX, "vpbroadcastb", EVEX, 0x66, 3, 0x0f3878, R, 0, 0, 0, V, NONE, XM, 1, 1, KZ, REG_RM) \
    XV(VPBROADCASTB_Y_XM8_EVEX, "vpbroadcastb", EVEX, 0x66, 3, 0x0f3878, R, 0, 0, 1, Y, NONE, XM, 1, 1, KZ, REG_RM) \
    XV(VPBROADCASTW_X_XM16_EVEX, "vpbroadcastw", EVEX, 0x66, 3, 0x0f3879, R, 0, 0, 0, V, NONE, XM, 2, 2, KZ, REG_RM) \
    XV(VPBROADCASTW_Y_XM16_EVEX, "vpbroadcastw", EVEX, 0x66, 3, 0x0f3879, R, 0, 0, 1, Y, NONE, XM, 2, 2, KZ, REG_RM) \
    XV(VPBROADCASTD_Z_XM32, "vpbroadcastd", EVEX, 0x66, 3, 0x0f3858, R, 0, 0, 2, Z, NONE, XM, 4, 4, KZ, REG_RM) \
    XV(VPBROADCASTQ_Z_XM64, "vpbroadcastq", EVEX, 0x66, 3, 0x0f3859, R, 0, 1, 2, Z, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(VPBROADCASTD_X_XM32_EVEX, "vpbroadcastd", EVEX, 0x66, 3, 0x0f3858, R, 0, 0, 0, V, NONE, XM, 4, 4, KZ, REG_RM) \
    XV(VPBROADCASTD_Y_XM32_EVEX, "vpbroadcastd", EVEX, 0x66, 3, 0x0f3858, R, 0, 0, 1, Y, NONE, XM, 4, 4, KZ, REG_RM) \
    XV(VPBROADCASTQ_X_XM64_EVEX, "vpbroadcastq", EVEX, 0x66, 3, 0x0f3859, R, 0, 1, 0, V, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(VPBROADCASTQ_Y_XM64_EVEX, "vpbroadcastq", EVEX, 0x66, 3, 0x0f3859, R, 0, 1, 1, Y, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(VPBROADCASTB_Z_R32, "vpbroadcastb", EVEX, 0x66, 3, 0x0f387a, R, 0, 0, 2, Z, NONE, GD, 4, 1, KZ, REG_RM) \
    XV(VPBROADCASTB_X_R32, "vpbroadcastb", EVEX, 0x66, 3, 0x0f387a, R, 0, 0, 0, V, NONE, GD, 4, 1, KZ, REG_RM) \
    XV(VPBROADCASTB_Y_R32, "vpbroadcastb", EVEX, 0x66, 3, 0x0f387a, R, 0, 0, 1, Y, NONE, GD, 4, 1, KZ, REG_RM) \
    XV(VPBROADCASTW_Z_R32, "vpbroadcastw", EVEX, 0x66, 3, 0x0f387b, R, 0, 0, 2, Z, NONE, GD, 4, 1, KZ, REG_RM) \
    XV(VPBROADCASTW_X_R32, "vpbroadcastw", EVEX, 0x66, 3, 0x0f387b, R, 0, 0, 0, V, NONE, GD, 4, 1, KZ, REG_RM) \
    XV(VPBROADCASTW_Y_R32, "vpbroadcastw", EVEX, 0x66, 3, 0x0f387b, R, 0, 0, 1, Y, NONE, GD, 4, 1, KZ, REG_RM) \
    XV(VPBROADCASTD_Z_R32, "vpbroadcastd", EVEX, 0x66, 3, 0x0f387c, R, 0, 0, 2, Z, NONE, GD, 4, 1, KZ, REG_RM) \
    XV(VPBROADCASTQ_Z_R64, "vpbroadcastq", EVEX, 0x66, 3, 0x0f387c, R, 0, 1, 2, Z, NONE, GQ, 8, 1, KZ, REG_RM) \
    XV(VPBROADCASTD_X_R32, "vpbroadcastd", EVEX, 0x66, 3, 0x0f387c, R, 0, 0, 0, V, NONE, GD, 4, 1, KZ, REG_RM) \
    XV(VPBROADCASTD_Y_R32, "vpbroadcastd", EVEX, 0x66, 3, 0x0f387c, R, 0, 0, 1, Y, NONE, GD, 4, 1, KZ, REG_RM) \
    XV(VPBROADCASTQ_X_R64, "vpbroadcastq", EVEX, 0x66, 3, 0x0f387c, R, 0, 1, 0, V, NONE, GQ, 8, 1, KZ, REG_RM) \
    XV(VPBROADCASTQ_Y_R64, "vpbroadcastq", EVEX, 0x66, 3, 0x0f387c, R, 0, 1, 1, Y, NONE, GQ, 8, 1, KZ, REG_RM) \
    XV(VBROADCASTSS_Z_XM32, "vbroadcastss", EVEX, 0x66, 3, 0x0f3818, R, 0, 0, 2, Z, NONE, XM, 4, 4, KZ, REG_RM) \
    XV(VBROADCASTSD_Z_XM64, "vbroadcastsd", EVEX, 0x66, 3, 0x0f3819, R, 0, 1, 2, Z, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(VBROADCASTSS_X_XM32_EVEX, "vbroadcastss", EVEX, 0x66, 3, 0x0f3818, R, 0, 0, 0, V, NONE, XM, 4, 4, KZ, REG_RM) \
    XV(VBROADCASTSS_Y_XM32_EVEX, "vbroadcastss", EVEX, 0x66, 3, 0x0f3818, R, 0, 0, 1, Y, NONE, XM, 4, 4, KZ, REG_RM) \
    XV(VBROADCASTSD_Y_XM64_EVEX, "vbroadcastsd", EVEX, 0x66, 3, 0x0f3819, R, 0, 1, 1, Y, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(VBROADCASTI32X4_Z_M128, "vbroadcasti32x4", EVEX, 0x66, 3, 0x0f385a, R, 0, 0, 2, Z, NONE, M, 16, 16, KZ, REG_RM) \
    XV(VBROADCASTI64X4_Z_M256, "vbroadcasti64x4", EVEX, 0x66, 3, 0x0f385b, R, 0, 1, 2, Z, NONE, M, 32, 32, KZ, REG_RM) \
    XV(VBROADCASTI32X4_Y_M128, "vbroadcasti32x4", EVEX, 0x66, 3, 0x0f385a, R, 0, 0, 1, Y, NONE, M, 16, 16, KZ, REG_RM) \
    XV(VBROADCASTF32X4_Z_M128, "vbroadcastf32x4", EVEX, 0x66, 3, 0x0f381a, R, 0, 0, 2, Z, NONE, M, 16, 16, KZ, REG_RM) \
    XV(VBROADCASTF64X4_Z_M256, "vbroadcastf64x4", EVEX, 0x66, 3, 0x0f381b, R, 0, 1, 2, Z, NONE, M, 32, 32, KZ, REG_RM) \
    XV(VBROADCASTF32X4_Y_M128, "vbroadcastf32x4", EVEX, 0x66, 3, 0x0f381a, R, 0, 0, 1, Y, NONE, M, 16, 16, KZ, REG_RM) \
    XV(VBROADCASTF32X2_Y_XM64, "vbroadcastf32x2", EVEX, 0x66, 3, 0x0f3819, R, 0, 0, 1, Y, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(VBROADCASTF32X2_Z_XM64, "vbroadcastf32x2", EVEX, 0x66, 3, 0x0f3819, R, 0, 0, 2, Z, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(VBROADCASTI32X2_X_XM64, "vbroadcasti32x2", EVEX, 0x66, 3, 0x0f3859, R, 0, 0, 0, V, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(VBROADCASTI32X2_Y_XM64, "vbroadcasti32x2", EVEX, 0x66, 3, 0x0f3859, R, 0, 0, 1, Y, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(VBROADCASTI32X2_Z_XM64, "vbroadcasti32x2", EVEX, 0x66, 3, 0x0f3859, R, 0, 0, 2, Z, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(VBROADCASTF64X2_Y_M128, "vbroadcastf64x2", EVEX, 0x66, 3, 0x0f381a, R, 0, 1, 1, Y, NONE, M, 16, 16, KZ, REG_RM) \
    XV(VBROADCASTF64X2_Z_M128, "vbroadcastf64x2", EVEX, 0x66, 3, 0x0f381a, R, 0, 1, 2, Z, NONE, M, 16, 16, KZ, REG_RM) \
    XV(VBROADCASTI64X2_Y_M128, "vbroadcasti64x2", EVEX, 0x66, 3, 0x0f385a, R, 0, 1, 1, Y, NONE, M, 16, 16, KZ, REG_RM) \
    XV(VBROADCASTI64X2_Z_M128, "vbroadcasti64x2", EVEX, 0x66, 3, 0x0f385a, R, 0, 1, 2, Z, NONE, M, 16, 16, KZ, REG_RM) \
    XV(VBROADCASTF32X8_Z_M256, "vbroadcastf32x8", EVEX, 0x66, 3, 0x0f381b, R, 0, 0, 2, Z, NONE, M, 32, 32, KZ, REG_RM) \
    XV(VBROADCASTI32X8_Z_M256, "vbroadcasti32x8", EVEX, 0x66, 3, 0x0f385b, R, 0, 0, 2, Z, NONE, M, 32, 32, KZ, REG_RM) \
    BUSTER_X86_AVX512_INSERT_EXTRACT_FORMS(XV, I, "i", 0x0f3a38) \
    BUSTER_X86_AVX512_INSERT_EXTRACT_FORMS(XV, F, "f", 0x0f3a18) \
    BUSTER_X86_AVX512DQ_INSERT_EXTRACT_FORMS(XV, I, "i", 0x0f3a38) \
    BUSTER_X86_AVX512DQ_INSERT_EXTRACT_FORMS(XV, F, "f", 0x0f3a18) \
    XV(VINSERTPS_X_X_XM32_IMM8_EVEX, "vinsertps", EVEX, 0x66, 3, 0x0f3a21, R, 1, 0, 0, V, V, XM, 4, 4, NONE, REG_VVVV_RM_IMM) \
    XV(VEXTRACTPS_RM32_X_IMM8_EVEX, "vextractps", EVEX, 0x66, 3, 0x0f3a17, R, 1, 0, 0, V, NONE, ED, 4, 4, NONE, RM_REG_IMM) \
    XV(VPEXTRB_RM32_X_IMM8_EVEX, "vpextrb", EVEX, 0x66, 3, 0x0f3a14, R, 1, 0, 0, V, NONE, ED, 1, 1, NONE, RM_REG_IMM) \
    XV(VPEXTRW_R32_X_IMM8_EVEX, "vpextrw", EVEX, 0x66, 2, 0x0fc5, R, 1, 0, 0, GD, NONE, V, 0, 1, NONE, REG_RM_IMM) \
    XV(VPEXTRW_M16_X_IMM8_EVEX, "vpextrw", EVEX, 0x66, 3, 0x0f3a15, R, 1, 0, 0, V, NONE, M, 2, 2, NONE, RM_REG_IMM) \
    XV(VPEXTRD_RM32_X_IMM8_EVEX, "vpextrd", EVEX, 0x66, 3, 0x0f3a16, R, 1, 0, 0, V, NONE, ED, 4, 4, NONE, RM_REG_IMM) \
    XV(VPEXTRQ_RM64_X_IMM8_EVEX, "vpextrq", EVEX, 0x66, 3, 0x0f3a16, R, 1, 1, 0, V, NONE, EQ, 8, 8, NONE, RM_REG_IMM) \
    XV(VPINSRB_X_X_RM32_IMM8_EVEX, "vpinsrb", EVEX, 0x66, 3, 0x0f3a20, R, 1, 0, 0, V, V, ED, 1, 1, NONE, REG_VVVV_RM_IMM) \
    XV(VPINSRW_X_X_RM32_IMM8_EVEX, "vpinsrw", EVEX, 0x66, 2, 0x0fc4, R, 1, 0, 0, V, V, ED, 2, 2, NONE, REG_VVVV_RM_IMM) \
    XV(VPINSRD_X_X_RM32_IMM8_EVEX, "vpinsrd", EVEX, 0x66, 3, 0x0f3a22, R, 1, 0, 0, V, V, ED, 4, 4, NONE, REG_VVVV_RM_IMM) \
    XV(VPINSRQ_X_X_RM64_IMM8_EVEX, "vpinsrq", EVEX, 0x66, 3, 0x0f3a22, R, 1, 1, 0, V, V, EQ, 8, 8, NONE, REG_VVVV_RM_IMM) \
    BUSTER_X86_AVX512_COMPRESS_FORMS(XV, VPCOMPRESSB, "vpcompressb", 0x0f3863, 0, 8) \
    BUSTER_X86_AVX512_COMPRESS_FORMS(XV, VPCOMPRESSW, "vpcompressw", 0x0f3863, 1, 16) \
    BUSTER_X86_AVX512_COMPRESS_FORMS(XV, VPCOMPRESSD, "vpcompressd", 0x0f388b, 0, 32) \
    BUSTER_X86_AVX512_COMPRESS_FORMS(XV, VPCOMPRESSQ, "vpcompressq", 0x0f388b, 1, 64) \
    BUSTER_X86_AVX512_EXPAND_FORMS(XV, VPEXPANDB, "vpexpandb", 0x0f3862, 0, 8) \
    BUSTER_X86_AVX512_EXPAND_FORMS(XV, VPEXPANDW, "vpexpandw", 0x0f3862, 1, 16) \
    BUSTER_X86_AVX512_EXPAND_FORMS(XV, VPEXPANDD, "vpexpandd", 0x0f3889, 0, 32) \
    BUSTER_X86_AVX512_EXPAND_FORMS(XV, VPEXPANDQ, "vpexpandq", 0x0f3889, 1, 64) \
    BUSTER_X86_AVX512_COMPRESS_FORMS(XV, VCOMPRESSPS, "vcompressps", 0x0f388a, 0, 32) \
    BUSTER_X86_AVX512_COMPRESS_FORMS(XV, VCOMPRESSPD, "vcompresspd", 0x0f388a, 1, 64) \
    BUSTER_X86_AVX512_EXPAND_FORMS(XV, VEXPANDPS, "vexpandps", 0x0f3888, 0, 32) \
    BUSTER_X86_AVX512_EXPAND_FORMS(XV, VEXPANDPD, "vexpandpd", 0x0f3888, 1, 64) \
    BUSTER_X86_AVX512_GATHER_FORMS(XV, VPGATHERDD, "vpgatherdd", 0x0f3890, 0, 32) \
    BUSTER_X86_AVX512_WIDENING_GATHER_FORMS(XV, VPGATHERDQ, "vpgatherdq", 0x0f3890) \
    BUSTER_X86_AVX512_NARROWING_GATHER_FORMS(XV, VPGATHERQD, "vpgatherqd", 0x0f3891) \
    BUSTER_X86_AVX512_GATHER_FORMS(XV, VPGATHERQQ, "vpgatherqq", 0x0f3891, 1, 64) \
    BUSTER_X86_AVX512_GATHER_FORMS(XV, VGATHERDPS, "vgatherdps", 0x0f3892, 0, 32) \
    BUSTER_X86_AVX512_WIDENING_GATHER_FORMS(XV, VGATHERDPD, "vgatherdpd", 0x0f3892) \
    BUSTER_X86_AVX512_NARROWING_GATHER_FORMS(XV, VGATHERQPS, "vgatherqps", 0x0f3893) \
    BUSTER_X86_AVX512_GATHER_FORMS(XV, VGATHERQPD, "vgatherqpd", 0x0f3893, 1, 64) \
    BUSTER_X86_AVX512_SCATTER_FORMS(XV, VPSCATTERDD, "vpscatterdd", 0x0f38a0, 0, 32) \
    BUSTER_X86_AVX512_WIDENING_SCATTER_FORMS(XV, VPSCATTERDQ, "vpscatterdq", 0x0f38a0) \
    BUSTER_X86_AVX512_NARROWING_SCATTER_FORMS(XV, VPSCATTERQD, "vpscatterqd", 0x0f38a1) \
    BUSTER_X86_AVX512_SCATTER_FORMS(XV, VPSCATTERQQ, "vpscatterqq", 0x0f38a1, 1, 64) \
    BUSTER_X86_AVX512_SCATTER_FORMS(XV, VSCATTERDPS, "vscatterdps", 0x0f38a2, 0, 32) \
    BUSTER_X86_AVX512_WIDENING_SCATTER_FORMS(XV, VSCATTERDPD, "vscatterdpd", 0x0f38a2) \
    BUSTER_X86_AVX512_NARROWING_SCATTER_FORMS(XV, VSCATTERQPS, "vscatterqps", 0x0f38a3) \
    BUSTER_X86_AVX512_SCATTER_FORMS(XV, VSCATTERQPD, "vscatterqpd", 0x0f38a3, 1, 64) \
    BUSTER_X86_AMX_ROW_FORMS(XV, TCVTROWD2PS, "tcvtrowd2ps", 0xf3, 0x0f3a07, 0xf3, 0x0f384a) \
    BUSTER_X86_AMX_ROW_FORMS(XV, TCVTROWPS2BF16H, "tcvtrowps2bf16h", 0xf2, 0x0f3a07, 0xf2, 0x0f386d) \
    BUSTER_X86_AMX_ROW_FORMS(XV, TCVTROWPS2BF16L, "tcvtrowps2bf16l", 0xf3, 0x0f3a77, 0xf3, 0x0f386d) \
    BUSTER_X86_AMX_ROW_FORMS(XV, TCVTROWPS2PHH, "tcvtrowps2phh", 0, 0x0f3a07, 0, 0x0f386d) \
    BUSTER_X86_AMX_ROW_FORMS(XV, TCVTROWPS2PHL, "tcvtrowps2phl", 0xf2, 0x0f3a77, 0x66, 0x0f386d) \
    BUSTER_X86_AMX_ROW_FORMS(XV, TILEMOVROW, "tilemovrow", 0x66, 0x0f3a07, 0x66, 0x0f384a) \
    /* Xeon Phi's AVX-512: AVX512ER's exponentials, reciprocals and reciprocal square roots (to 28 bits), AVX512PF's prefetching gathers and scatters, and AVX512_4FMAPS's and AVX512_4VNNIW's products, of a block of four registers in EVEX.V'vvvv (from a multiple of four) and 16 bytes of memory (N 16). */ \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VEXP2PS, "vexp2ps", 0x66, 3, 0x0f38c8, 0, 32) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VEXP2PD, "vexp2pd", 0x66, 3, 0x0f38c8, 1, 64) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VRCP28PS, "vrcp28ps", 0x66, 3, 0x0f38ca, 0, 32) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VRCP28PD, "vrcp28pd", 0x66, 3, 0x0f38ca, 1, 64) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VRSQRT28PS, "vrsqrt28ps", 0x66, 3, 0x0f38cc, 0, 32) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VRSQRT28PD, "vrsqrt28pd", 0x66, 3, 0x0f38cc, 1, 64) \
    BUSTER_X86_AVX512_SCALAR_38_FORMS(XV, VRCP28, "vrcp28", 0x0f38cb, SAE) \
    BUSTER_X86_AVX512_SCALAR_38_FORMS(XV, VRSQRT28, "vrsqrt28", 0x0f38cd, SAE) \
    BUSTER_X86_AVX512PF_FORMS(XV, VGATHERPF0, "vgatherpf0", D1) \
    BUSTER_X86_AVX512PF_FORMS(XV, VGATHERPF1, "vgatherpf1", D2) \
    BUSTER_X86_AVX512PF_FORMS(XV, VSCATTERPF0, "vscatterpf0", D5) \
    BUSTER_X86_AVX512PF_FORMS(XV, VSCATTERPF1, "vscatterpf1", D6) \
    XV(V4FMADDPS_Z_Z_M128, "v4fmaddps", EVEX, 0xf2, 3, 0x0f389a, R, 0, 0, 2, Z, Z4, M, 16, 16, KZ, REG_VVVV_RM) \
    XV(V4FNMADDPS_Z_Z_M128, "v4fnmaddps", EVEX, 0xf2, 3, 0x0f38aa, R, 0, 0, 2, Z, Z4, M, 16, 16, KZ, REG_VVVV_RM) \
    XV(V4FMADDSS_X_X_M128, "v4fmaddss", EVEX, 0xf2, 3, 0x0f389b, R, 0, 0, 0, V, V4, M, 16, 16, KZ, REG_VVVV_RM) \
    XV(V4FNMADDSS_X_X_M128, "v4fnmaddss", EVEX, 0xf2, 3, 0x0f38ab, R, 0, 0, 0, V, V4, M, 16, 16, KZ, REG_VVVV_RM) \
    XV(VP4DPWSSD_Z_Z_M128, "vp4dpwssd", EVEX, 0xf2, 3, 0x0f3852, R, 0, 0, 2, Z, Z4, M, 16, 16, KZ, REG_VVVV_RM) \
    XV(VP4DPWSSDS_Z_Z_M128, "vp4dpwssds", EVEX, 0xf2, 3, 0x0f3853, R, 0, 0, 2, Z, Z4, M, 16, 16, KZ, REG_VVVV_RM) \
    BUSTER_X86_AVX512_NARROWING_MOVE_FORMS(XV, VPMOV, "vpmov", 0x0f3830) \
    BUSTER_X86_AVX512VL_NARROWING_MOVE_FORMS(XV, VPMOV, "vpmov", 0x0f3830) \
    BUSTER_X86_AVX512_NARROWING_MOVE_FORMS(XV, VPMOVS, "vpmovs", 0x0f3820) \
    BUSTER_X86_AVX512VL_NARROWING_MOVE_FORMS(XV, VPMOVS, "vpmovs", 0x0f3820) \
    BUSTER_X86_AVX512_NARROWING_MOVE_FORMS(XV, VPMOVUS, "vpmovus", 0x0f3810) \
    BUSTER_X86_AVX512VL_NARROWING_MOVE_FORMS(XV, VPMOVUS, "vpmovus", 0x0f3810) \
    BUSTER_X86_AVX512BW_NARROWING_MOVE_FORMS(XV, VPMOV, "vpmov", 0x0f3830) \
    BUSTER_X86_AVX512BW_NARROWING_MOVE_FORMS(XV, VPMOVS, "vpmovs", 0x0f3820) \
    BUSTER_X86_AVX512BW_NARROWING_MOVE_FORMS(XV, VPMOVUS, "vpmovus", 0x0f3810) \
    BUSTER_X86_AVX512BW_EXTEND_FORMS(XV, VPMOVSX, "vpmovsx", 0x0f3820) \
    BUSTER_X86_AVX512BW_EXTEND_FORMS(XV, VPMOVZX, "vpmovzx", 0x0f3830) \
    BUSTER_X86_AVX512_EXTEND_FORMS(XV, VPMOVSX, "vpmovsx", 0x0f3820) \
    BUSTER_X86_AVX512VL_EXTEND_FORMS(XV, VPMOVSX, "vpmovsx", 0x0f3820) \
    BUSTER_X86_AVX512_EXTEND_FORMS(XV, VPMOVZX, "vpmovzx", 0x0f3830) \
    BUSTER_X86_AVX512VL_EXTEND_FORMS(XV, VPMOVZX, "vpmovzx", 0x0f3830) \
    BUSTER_X86_AVX512_FROM_MASK_FORMS(XV, VPMOVM2B, "vpmovm2b", 0x0f3828, 0) \
    BUSTER_X86_AVX512_FROM_MASK_FORMS(XV, VPMOVM2W, "vpmovm2w", 0x0f3828, 1) \
    BUSTER_X86_AVX512_FROM_MASK_FORMS(XV, VPMOVM2D, "vpmovm2d", 0x0f3838, 0) \
    BUSTER_X86_AVX512_FROM_MASK_FORMS(XV, VPMOVM2Q, "vpmovm2q", 0x0f3838, 1) \
    BUSTER_X86_AVX512_FROM_MASK_FORMS(XV, VPBROADCASTMW2D, "vpbroadcastmw2d", 0x0f383a, 0) \
    BUSTER_X86_AVX512_FROM_MASK_FORMS(XV, VPBROADCASTMB2Q, "vpbroadcastmb2q", 0x0f382a, 1) \
    BUSTER_X86_AVX512_TO_MASK_FORMS(XV, VPMOVB2M, "vpmovb2m", 0x0f3829, 0) \
    BUSTER_X86_AVX512_TO_MASK_FORMS(XV, VPMOVW2M, "vpmovw2m", 0x0f3829, 1) \
    BUSTER_X86_AVX512_TO_MASK_FORMS(XV, VPMOVD2M, "vpmovd2m", 0x0f3839, 0) \
    BUSTER_X86_AVX512_TO_MASK_FORMS(XV, VPMOVQ2M, "vpmovq2m", 0x0f3839, 1) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, VADDPS, "vaddps", 0, 2, 0x0f58, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VADDPS, "vaddps", 0, 2, 0x0f58, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, VADDPD, "vaddpd", 0x66, 2, 0x0f58, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VADDPD, "vaddpd", 0x66, 2, 0x0f58, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, VSUBPS, "vsubps", 0, 2, 0x0f5c, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VSUBPS, "vsubps", 0, 2, 0x0f5c, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, VSUBPD, "vsubpd", 0x66, 2, 0x0f5c, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VSUBPD, "vsubpd", 0x66, 2, 0x0f5c, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, VMULPS, "vmulps", 0, 2, 0x0f59, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VMULPS, "vmulps", 0, 2, 0x0f59, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, VMULPD, "vmulpd", 0x66, 2, 0x0f59, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VMULPD, "vmulpd", 0x66, 2, 0x0f59, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, VDIVPS, "vdivps", 0, 2, 0x0f5e, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VDIVPS, "vdivps", 0, 2, 0x0f5e, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, VDIVPD, "vdivpd", 0x66, 2, 0x0f5e, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VDIVPD, "vdivpd", 0x66, 2, 0x0f5e, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, VSCALEFPS, "vscalefps", 0x66, 3, 0x0f382c, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VSCALEFPS, "vscalefps", 0x66, 3, 0x0f382c, 0, 32, ) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, VSCALEFPD, "vscalefpd", 0x66, 3, 0x0f382c, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VSCALEFPD, "vscalefpd", 0x66, 3, 0x0f382c, 1, 64, ) \
    BUSTER_X86_AVX512_SAE_FORMS(XV, VMINPS, "vminps", 0, 2, 0x0f5d, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VMINPS, "vminps", 0, 2, 0x0f5d, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_SAE_FORMS(XV, VMAXPS, "vmaxps", 0, 2, 0x0f5f, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VMAXPS, "vmaxps", 0, 2, 0x0f5f, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_SAE_FORMS(XV, VMINPD, "vminpd", 0x66, 2, 0x0f5d, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VMINPD, "vminpd", 0x66, 2, 0x0f5d, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_SAE_FORMS(XV, VMAXPD, "vmaxpd", 0x66, 2, 0x0f5f, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VMAXPD, "vmaxpd", 0x66, 2, 0x0f5f, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VANDPS, "vandps", 0, 2, 0x0f54, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VANDPS, "vandps", 0, 2, 0x0f54, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VANDNPS, "vandnps", 0, 2, 0x0f55, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VANDNPS, "vandnps", 0, 2, 0x0f55, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VORPS, "vorps", 0, 2, 0x0f56, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VORPS, "vorps", 0, 2, 0x0f56, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VXORPS, "vxorps", 0, 2, 0x0f57, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VXORPS, "vxorps", 0, 2, 0x0f57, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VANDPD, "vandpd", 0x66, 2, 0x0f54, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VANDPD, "vandpd", 0x66, 2, 0x0f54, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VANDNPD, "vandnpd", 0x66, 2, 0x0f55, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VANDNPD, "vandnpd", 0x66, 2, 0x0f55, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VORPD, "vorpd", 0x66, 2, 0x0f56, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VORPD, "vorpd", 0x66, 2, 0x0f56, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VXORPD, "vxorpd", 0x66, 2, 0x0f57, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VXORPD, "vxorpd", 0x66, 2, 0x0f57, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VUNPCKLPS, "vunpcklps", 0, 2, 0x0f14, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VUNPCKLPS, "vunpcklps", 0, 2, 0x0f14, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VUNPCKLPD, "vunpcklpd", 0x66, 2, 0x0f14, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VUNPCKLPD, "vunpcklpd", 0x66, 2, 0x0f14, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VUNPCKHPS, "vunpckhps", 0, 2, 0x0f15, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VUNPCKHPS, "vunpckhps", 0, 2, 0x0f15, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VUNPCKHPD, "vunpckhpd", 0x66, 2, 0x0f15, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VUNPCKHPD, "vunpckhpd", 0x66, 2, 0x0f15, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_IMMEDIATE_FORMS(XV, VSHUFPS, "vshufps", 0, 2, 0x0fc6, 0, 32) \
    BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, VSHUFPS, "vshufps", 0, 2, 0x0fc6, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_IMMEDIATE_FORMS(XV, VSHUFPD, "vshufpd", 0x66, 2, 0x0fc6, 1, 64) \
    BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, VSHUFPD, "vshufpd", 0x66, 2, 0x0fc6, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_IMMEDIATE_FORMS(XV, VSHUFF32X4, "vshuff32x4", 0x66, 3, 0x0f3a23, 0, 32) \
    BUSTER_X86_AVX512VL_256_IMMEDIATE_FORMS(XV, VSHUFF32X4, "vshuff32x4", 0x66, 3, 0x0f3a23, 0, 32, ) \
    BUSTER_X86_AVX512_IMMEDIATE_FORMS(XV, VSHUFF64X2, "vshuff64x2", 0x66, 3, 0x0f3a23, 1, 64) \
    BUSTER_X86_AVX512VL_256_IMMEDIATE_FORMS(XV, VSHUFF64X2, "vshuff64x2", 0x66, 3, 0x0f3a23, 1, 64, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VBLENDMPS, "vblendmps", 0x66, 3, 0x0f3865, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VBLENDMPS, "vblendmps", 0x66, 3, 0x0f3865, 0, 32, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VBLENDMPD, "vblendmpd", 0x66, 3, 0x0f3865, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VBLENDMPD, "vblendmpd", 0x66, 3, 0x0f3865, 1, 64, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPERMILPS, "vpermilps", 0x66, 3, 0x0f380c, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPERMILPS, "vpermilps", 0x66, 3, 0x0f380c, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPERMILPD, "vpermilpd", 0x66, 3, 0x0f380d, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPERMILPD, "vpermilpd", 0x66, 3, 0x0f380d, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_UNARY_IMMEDIATE_FORMS(XV, VPERMILPS, "vpermilps", 0x66, 3, 0x0f3a04, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_IMMEDIATE_FORMS(XV, VPERMILPS, "vpermilps", 0x66, 3, 0x0f3a04, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_UNARY_IMMEDIATE_FORMS(XV, VPERMILPD, "vpermilpd", 0x66, 3, 0x0f3a05, 1, 64) \
    BUSTER_X86_AVX512VL_UNARY_IMMEDIATE_FORMS(XV, VPERMILPD, "vpermilpd", 0x66, 3, 0x0f3a05, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPERMPS, "vpermps", 0x66, 3, 0x0f3816, 0, 32) \
    BUSTER_X86_AVX512VL_256_BROADCAST_FORMS(XV, VPERMPS, "vpermps", 0x66, 3, 0x0f3816, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPERMPD, "vpermpd", 0x66, 3, 0x0f3816, 1, 64) \
    BUSTER_X86_AVX512VL_256_BROADCAST_FORMS(XV, VPERMPD, "vpermpd", 0x66, 3, 0x0f3816, 1, 64, ) \
    BUSTER_X86_AVX512_UNARY_IMMEDIATE_FORMS(XV, VPERMPD, "vpermpd", 0x66, 3, 0x0f3a01, 1, 64) \
    BUSTER_X86_AVX512VL_256_UNARY_IMMEDIATE_FORMS(XV, VPERMPD, "vpermpd", 0x66, 3, 0x0f3a01, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPERMI2PS, "vpermi2ps", 0x66, 3, 0x0f3877, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPERMI2PS, "vpermi2ps", 0x66, 3, 0x0f3877, 0, 32, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPERMI2PD, "vpermi2pd", 0x66, 3, 0x0f3877, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPERMI2PD, "vpermi2pd", 0x66, 3, 0x0f3877, 1, 64, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPERMT2PS, "vpermt2ps", 0x66, 3, 0x0f387f, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPERMT2PS, "vpermt2ps", 0x66, 3, 0x0f387f, 0, 32, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPERMT2PD, "vpermt2pd", 0x66, 3, 0x0f387f, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPERMT2PD, "vpermt2pd", 0x66, 3, 0x0f387f, 1, 64, ) \
    BUSTER_X86_AVX512_FMA_PACKED_FORMS(XV, VFMADDSUB, "vfmaddsub", 0x96) \
    BUSTER_X86_AVX512VL_FMA_PACKED_FORMS(XV, VFMADDSUB, "vfmaddsub", 0x96) \
    BUSTER_X86_AVX512_FMA_PACKED_FORMS(XV, VFMSUBADD, "vfmsubadd", 0x97) \
    BUSTER_X86_AVX512VL_FMA_PACKED_FORMS(XV, VFMSUBADD, "vfmsubadd", 0x97) \
    BUSTER_X86_AVX512_FMA_PACKED_FORMS(XV, VFMADD, "vfmadd", 0x98) \
    BUSTER_X86_AVX512VL_FMA_PACKED_FORMS(XV, VFMADD, "vfmadd", 0x98) \
    BUSTER_X86_AVX512_FMA_PACKED_FORMS(XV, VFMSUB, "vfmsub", 0x9a) \
    BUSTER_X86_AVX512VL_FMA_PACKED_FORMS(XV, VFMSUB, "vfmsub", 0x9a) \
    BUSTER_X86_AVX512_FMA_PACKED_FORMS(XV, VFNMADD, "vfnmadd", 0x9c) \
    BUSTER_X86_AVX512VL_FMA_PACKED_FORMS(XV, VFNMADD, "vfnmadd", 0x9c) \
    BUSTER_X86_AVX512_FMA_PACKED_FORMS(XV, VFNMSUB, "vfnmsub", 0x9e) \
    BUSTER_X86_AVX512VL_FMA_PACKED_FORMS(XV, VFNMSUB, "vfnmsub", 0x9e) \
    BUSTER_X86_AVX512_UNARY_ROUNDING_FORMS(XV, VSQRTPS, "vsqrtps", 0, 2, 0x0f51, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VSQRTPS, "vsqrtps", 0, 2, 0x0f51, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_UNARY_ROUNDING_FORMS(XV, VSQRTPD, "vsqrtpd", 0x66, 2, 0x0f51, 1, 64) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VSQRTPD, "vsqrtpd", 0x66, 2, 0x0f51, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VRCP14PS, "vrcp14ps", 0x66, 3, 0x0f384c, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VRCP14PS, "vrcp14ps", 0x66, 3, 0x0f384c, 0, 32, ) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VRCP14PD, "vrcp14pd", 0x66, 3, 0x0f384c, 1, 64) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VRCP14PD, "vrcp14pd", 0x66, 3, 0x0f384c, 1, 64, ) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VRSQRT14PS, "vrsqrt14ps", 0x66, 3, 0x0f384e, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VRSQRT14PS, "vrsqrt14ps", 0x66, 3, 0x0f384e, 0, 32, ) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VRSQRT14PD, "vrsqrt14pd", 0x66, 3, 0x0f384e, 1, 64) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VRSQRT14PD, "vrsqrt14pd", 0x66, 3, 0x0f384e, 1, 64, ) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VGETEXPPS, "vgetexpps", 0x66, 3, 0x0f3842, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VGETEXPPS, "vgetexpps", 0x66, 3, 0x0f3842, 0, 32, ) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VGETEXPPD, "vgetexppd", 0x66, 3, 0x0f3842, 1, 64) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VGETEXPPD, "vgetexppd", 0x66, 3, 0x0f3842, 1, 64, ) \
    BUSTER_X86_AVX512_UNARY_IMMEDIATE_SAE_FORMS(XV, VGETMANTPS, "vgetmantps", 0x66, 3, 0x0f3a26, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_IMMEDIATE_FORMS(XV, VGETMANTPS, "vgetmantps", 0x66, 3, 0x0f3a26, 0, 32, ) \
    BUSTER_X86_AVX512_UNARY_IMMEDIATE_SAE_FORMS(XV, VGETMANTPD, "vgetmantpd", 0x66, 3, 0x0f3a26, 1, 64) \
    BUSTER_X86_AVX512VL_UNARY_IMMEDIATE_FORMS(XV, VGETMANTPD, "vgetmantpd", 0x66, 3, 0x0f3a26, 1, 64, ) \
    BUSTER_X86_AVX512_UNARY_IMMEDIATE_SAE_FORMS(XV, VRNDSCALEPS, "vrndscaleps", 0x66, 3, 0x0f3a08, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_IMMEDIATE_FORMS(XV, VRNDSCALEPS, "vrndscaleps", 0x66, 3, 0x0f3a08, 0, 32, ) \
    BUSTER_X86_AVX512_UNARY_IMMEDIATE_SAE_FORMS(XV, VRNDSCALEPD, "vrndscalepd", 0x66, 3, 0x0f3a09, 1, 64) \
    BUSTER_X86_AVX512VL_UNARY_IMMEDIATE_FORMS(XV, VRNDSCALEPD, "vrndscalepd", 0x66, 3, 0x0f3a09, 1, 64, ) \
    BUSTER_X86_AVX512_IMMEDIATE_SAE_FORMS(XV, VFIXUPIMMPS, "vfixupimmps", 0x66, 3, 0x0f3a54, 0, 32) \
    BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, VFIXUPIMMPS, "vfixupimmps", 0x66, 3, 0x0f3a54, 0, 32, ) \
    BUSTER_X86_AVX512_IMMEDIATE_SAE_FORMS(XV, VFIXUPIMMPD, "vfixupimmpd", 0x66, 3, 0x0f3a54, 1, 64) \
    BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, VFIXUPIMMPD, "vfixupimmpd", 0x66, 3, 0x0f3a54, 1, 64, ) \
    BUSTER_X86_AVX512_IMMEDIATE_SAE_FORMS(XV, VRANGEPS, "vrangeps", 0x66, 3, 0x0f3a50, 0, 32) \
    BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, VRANGEPS, "vrangeps", 0x66, 3, 0x0f3a50, 0, 32, ) \
    BUSTER_X86_AVX512_IMMEDIATE_SAE_FORMS(XV, VRANGEPD, "vrangepd", 0x66, 3, 0x0f3a50, 1, 64) \
    BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, VRANGEPD, "vrangepd", 0x66, 3, 0x0f3a50, 1, 64, ) \
    BUSTER_X86_AVX512_UNARY_IMMEDIATE_SAE_FORMS(XV, VREDUCEPS, "vreduceps", 0x66, 3, 0x0f3a56, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_IMMEDIATE_FORMS(XV, VREDUCEPS, "vreduceps", 0x66, 3, 0x0f3a56, 0, 32, ) \
    BUSTER_X86_AVX512_UNARY_IMMEDIATE_SAE_FORMS(XV, VREDUCEPD, "vreducepd", 0x66, 3, 0x0f3a56, 1, 64) \
    BUSTER_X86_AVX512VL_UNARY_IMMEDIATE_FORMS(XV, VREDUCEPD, "vreducepd", 0x66, 3, 0x0f3a56, 1, 64, ) \
    BUSTER_X86_AVX512DQ_CLASS_FORMS(XV, VFPCLASSPS, "vfpclassps", 0, 32, 4, 8, 16) \
    BUSTER_X86_AVX512DQ_CLASS_FORMS(XV, VFPCLASSPD, "vfpclasspd", 1, 64, 2, 4, 8) \
    BUSTER_X86_AVX512_UNARY_ROUNDING_FORMS(XV, VCVTDQ2PS, "vcvtdq2ps", 0, 2, 0x0f5b, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTDQ2PS, "vcvtdq2ps", 0, 2, 0x0f5b, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_UNARY_ROUNDING_FORMS(XV, VCVTPS2DQ, "vcvtps2dq", 0x66, 2, 0x0f5b, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTPS2DQ, "vcvtps2dq", 0x66, 2, 0x0f5b, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VCVTTPS2DQ, "vcvttps2dq", 0xf3, 2, 0x0f5b, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTTPS2DQ, "vcvttps2dq", 0xf3, 2, 0x0f5b, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_UNARY_ROUNDING_FORMS(XV, VCVTUDQ2PS, "vcvtudq2ps", 0xf2, 2, 0x0f7a, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTUDQ2PS, "vcvtudq2ps", 0xf2, 2, 0x0f7a, 0, 32, ) \
    BUSTER_X86_AVX512_UNARY_ROUNDING_FORMS(XV, VCVTPS2UDQ, "vcvtps2udq", 0, 2, 0x0f79, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTPS2UDQ, "vcvtps2udq", 0, 2, 0x0f79, 0, 32, ) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VCVTTPS2UDQ, "vcvttps2udq", 0, 2, 0x0f78, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTTPS2UDQ, "vcvttps2udq", 0, 2, 0x0f78, 0, 32, ) \
    BUSTER_X86_AVX512_WIDENING_FORMS(XV, VCVTDQ2PD, "vcvtdq2pd", 0xf3, 0x0fe6) \
    BUSTER_X86_AVX512VL_WIDENING_FORMS(XV, VCVTDQ2PD, "vcvtdq2pd", 0xf3, 0x0fe6, _EVEX) \
    BUSTER_X86_AVX512_WIDENING_FORMS(XV, VCVTUDQ2PD, "vcvtudq2pd", 0xf3, 0x0f7a) \
    BUSTER_X86_AVX512VL_WIDENING_FORMS(XV, VCVTUDQ2PD, "vcvtudq2pd", 0xf3, 0x0f7a, ) \
    BUSTER_X86_AVX512_WIDENING_SAE_FORMS(XV, VCVTPS2PD, "vcvtps2pd", 0, 0x0f5a) \
    BUSTER_X86_AVX512VL_WIDENING_FORMS(XV, VCVTPS2PD, "vcvtps2pd", 0, 0x0f5a, _EVEX) \
    BUSTER_X86_AVX512_WIDENING_ROUNDING_FORMS(XV, VCVTPS2QQ, "vcvtps2qq", 0x66, 0x0f7b) \
    BUSTER_X86_AVX512VL_WIDENING_FORMS(XV, VCVTPS2QQ, "vcvtps2qq", 0x66, 0x0f7b, ) \
    BUSTER_X86_AVX512_WIDENING_ROUNDING_FORMS(XV, VCVTPS2UQQ, "vcvtps2uqq", 0x66, 0x0f79) \
    BUSTER_X86_AVX512VL_WIDENING_FORMS(XV, VCVTPS2UQQ, "vcvtps2uqq", 0x66, 0x0f79, ) \
    BUSTER_X86_AVX512_WIDENING_SAE_FORMS(XV, VCVTTPS2QQ, "vcvttps2qq", 0x66, 0x0f7a) \
    BUSTER_X86_AVX512VL_WIDENING_FORMS(XV, VCVTTPS2QQ, "vcvttps2qq", 0x66, 0x0f7a, ) \
    BUSTER_X86_AVX512_WIDENING_SAE_FORMS(XV, VCVTTPS2UQQ, "vcvttps2uqq", 0x66, 0x0f78) \
    BUSTER_X86_AVX512VL_WIDENING_FORMS(XV, VCVTTPS2UQQ, "vcvttps2uqq", 0x66, 0x0f78, ) \
    BUSTER_X86_AVX512_NARROWING_FORMS(XV, VCVTPD2DQ, "vcvtpd2dq", 0xf2, 0x0fe6, ER) \
    BUSTER_X86_AVX512VL_NARROWING_FORMS(XV, VCVTPD2DQ, "vcvtpd2dq", 0xf2, 0x0fe6, _EVEX) \
    BUSTER_X86_AVX512_NARROWING_FORMS(XV, VCVTTPD2DQ, "vcvttpd2dq", 0x66, 0x0fe6, SAE) \
    BUSTER_X86_AVX512VL_NARROWING_FORMS(XV, VCVTTPD2DQ, "vcvttpd2dq", 0x66, 0x0fe6, _EVEX) \
    BUSTER_X86_AVX512_NARROWING_FORMS(XV, VCVTPD2UDQ, "vcvtpd2udq", 0, 0x0f79, ER) \
    BUSTER_X86_AVX512VL_NARROWING_FORMS(XV, VCVTPD2UDQ, "vcvtpd2udq", 0, 0x0f79, ) \
    BUSTER_X86_AVX512_NARROWING_FORMS(XV, VCVTTPD2UDQ, "vcvttpd2udq", 0, 0x0f78, SAE) \
    BUSTER_X86_AVX512VL_NARROWING_FORMS(XV, VCVTTPD2UDQ, "vcvttpd2udq", 0, 0x0f78, ) \
    BUSTER_X86_AVX512_NARROWING_FORMS(XV, VCVTPD2PS, "vcvtpd2ps", 0x66, 0x0f5a, ER) \
    BUSTER_X86_AVX512VL_NARROWING_FORMS(XV, VCVTPD2PS, "vcvtpd2ps", 0x66, 0x0f5a, _EVEX) \
    BUSTER_X86_AVX512_NARROWING_FORMS(XV, VCVTQQ2PS, "vcvtqq2ps", 0, 0x0f5b, ER) \
    BUSTER_X86_AVX512VL_NARROWING_FORMS(XV, VCVTQQ2PS, "vcvtqq2ps", 0, 0x0f5b, ) \
    BUSTER_X86_AVX512_NARROWING_FORMS(XV, VCVTUQQ2PS, "vcvtuqq2ps", 0xf2, 0x0f7a, ER) \
    BUSTER_X86_AVX512VL_NARROWING_FORMS(XV, VCVTUQQ2PS, "vcvtuqq2ps", 0xf2, 0x0f7a, ) \
    BUSTER_X86_AVX512_UNARY_ROUNDING_FORMS(XV, VCVTPD2QQ, "vcvtpd2qq", 0x66, 2, 0x0f7b, 1, 64) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTPD2QQ, "vcvtpd2qq", 0x66, 2, 0x0f7b, 1, 64, ) \
    BUSTER_X86_AVX512_UNARY_ROUNDING_FORMS(XV, VCVTPD2UQQ, "vcvtpd2uqq", 0x66, 2, 0x0f79, 1, 64) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTPD2UQQ, "vcvtpd2uqq", 0x66, 2, 0x0f79, 1, 64, ) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VCVTTPD2QQ, "vcvttpd2qq", 0x66, 2, 0x0f7a, 1, 64) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTTPD2QQ, "vcvttpd2qq", 0x66, 2, 0x0f7a, 1, 64, ) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VCVTTPD2UQQ, "vcvttpd2uqq", 0x66, 2, 0x0f78, 1, 64) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTTPD2UQQ, "vcvttpd2uqq", 0x66, 2, 0x0f78, 1, 64, ) \
    BUSTER_X86_AVX512_UNARY_ROUNDING_FORMS(XV, VCVTQQ2PD, "vcvtqq2pd", 0xf3, 2, 0x0fe6, 1, 64) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTQQ2PD, "vcvtqq2pd", 0xf3, 2, 0x0fe6, 1, 64, ) \
    BUSTER_X86_AVX512_UNARY_ROUNDING_FORMS(XV, VCVTUQQ2PD, "vcvtuqq2pd", 0xf3, 2, 0x0f7a, 1, 64) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTUQQ2PD, "vcvtuqq2pd", 0xf3, 2, 0x0f7a, 1, 64, ) \
    XV(VCVTPH2PS_Z_YM256, "vcvtph2ps", EVEX, 0x66, 3, 0x0f3813, R, 0, 0, 2, Z, NONE, YM, 32, 32, KZ, REG_RM) \
    XV(VCVTPH2PS_Z_Y_SAE, "vcvtph2ps", EVEX, 0x66, 3, 0x0f3813, R, 0, 0, 2, Z, NONE, Y, 32, 32, KZ_SAE, REG_RM) \
    XV(VCVTPH2PS_X_XM64_EVEX, "vcvtph2ps", EVEX, 0x66, 3, 0x0f3813, R, 0, 0, 0, V, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(VCVTPH2PS_Y_XM128_EVEX, "vcvtph2ps", EVEX, 0x66, 3, 0x0f3813, R, 0, 0, 1, Y, NONE, XM, 16, 16, KZ, REG_RM) \
    XV(VCVTPS2PH_YM256_Z_IMM8, "vcvtps2ph", EVEX, 0x66, 3, 0x0f3a1d, R, 1, 0, 2, Z, NONE, YM, 32, 32, KZ, RM_REG_IMM) \
    XV(VCVTPS2PH_Y_Z_SAE_IMM8, "vcvtps2ph", EVEX, 0x66, 3, 0x0f3a1d, R, 1, 0, 2, Z, NONE, Y, 32, 32, KZ_SAE, RM_REG_IMM) \
    XV(VCVTPS2PH_XM64_X_IMM8_EVEX, "vcvtps2ph", EVEX, 0x66, 3, 0x0f3a1d, R, 1, 0, 0, V, NONE, XM, 8, 8, KZ, RM_REG_IMM) \
    XV(VCVTPS2PH_XM128_Y_IMM8_EVEX, "vcvtps2ph", EVEX, 0x66, 3, 0x0f3a1d, R, 1, 0, 1, Y, NONE, XM, 16, 16, KZ, RM_REG_IMM) \
    BUSTER_X86_AVX512_SCALAR_FORMS(XV, VADD, "vadd", 0x58, ER) \
    BUSTER_X86_AVX512_SCALAR_FORMS(XV, VMUL, "vmul", 0x59, ER) \
    BUSTER_X86_AVX512_SCALAR_FORMS(XV, VSUB, "vsub", 0x5c, ER) \
    BUSTER_X86_AVX512_SCALAR_FORMS(XV, VDIV, "vdiv", 0x5e, ER) \
    BUSTER_X86_AVX512_SCALAR_FORMS(XV, VSQRT, "vsqrt", 0x51, ER) \
    BUSTER_X86_AVX512_SCALAR_FORMS(XV, VMIN, "vmin", 0x5d, SAE) \
    BUSTER_X86_AVX512_SCALAR_FORMS(XV, VMAX, "vmax", 0x5f, SAE) \
    BUSTER_X86_AVX512_FMA_SCALAR_FORMS(XV, VFMADD, "vfmadd", 0x98) \
    BUSTER_X86_AVX512_FMA_SCALAR_FORMS(XV, VFMSUB, "vfmsub", 0x9a) \
    BUSTER_X86_AVX512_FMA_SCALAR_FORMS(XV, VFNMADD, "vfnmadd", 0x9c) \
    BUSTER_X86_AVX512_FMA_SCALAR_FORMS(XV, VFNMSUB, "vfnmsub", 0x9e) \
    BUSTER_X86_AVX512_SCALAR_38_FORMS(XV, VSCALEF, "vscalef", 0x0f382d, ER) \
    BUSTER_X86_AVX512_SCALAR_38_FORMS(XV, VGETEXP, "vgetexp", 0x0f3843, SAE) \
    XV(VRCP14SS_X_X_XM32, "vrcp14ss", EVEX, 0x66, 3, 0x0f384d, R, 0, 0, 0, V, V, XM, 4, 4, KZ, REG_VVVV_RM) \
    XV(VRCP14SD_X_X_XM64, "vrcp14sd", EVEX, 0x66, 3, 0x0f384d, R, 0, 1, 0, V, V, XM, 8, 8, KZ, REG_VVVV_RM) \
    XV(VRSQRT14SS_X_X_XM32, "vrsqrt14ss", EVEX, 0x66, 3, 0x0f384f, R, 0, 0, 0, V, V, XM, 4, 4, KZ, REG_VVVV_RM) \
    XV(VRSQRT14SD_X_X_XM64, "vrsqrt14sd", EVEX, 0x66, 3, 0x0f384f, R, 0, 1, 0, V, V, XM, 8, 8, KZ, REG_VVVV_RM) \
    BUSTER_X86_AVX512_SCALAR_IMMEDIATE_SAE_FORMS(XV, VGETMANTSS, "vgetmantss", 0x0f3a27, 0, 32) \
    BUSTER_X86_AVX512_SCALAR_IMMEDIATE_SAE_FORMS(XV, VGETMANTSD, "vgetmantsd", 0x0f3a27, 1, 64) \
    BUSTER_X86_AVX512_SCALAR_IMMEDIATE_SAE_FORMS(XV, VRNDSCALESS, "vrndscaless", 0x0f3a0a, 0, 32) \
    BUSTER_X86_AVX512_SCALAR_IMMEDIATE_SAE_FORMS(XV, VRNDSCALESD, "vrndscalesd", 0x0f3a0b, 1, 64) \
    BUSTER_X86_AVX512_SCALAR_IMMEDIATE_SAE_FORMS(XV, VFIXUPIMMSS, "vfixupimmss", 0x0f3a55, 0, 32) \
    BUSTER_X86_AVX512_SCALAR_IMMEDIATE_SAE_FORMS(XV, VFIXUPIMMSD, "vfixupimmsd", 0x0f3a55, 1, 64) \
    BUSTER_X86_AVX512_SCALAR_IMMEDIATE_SAE_FORMS(XV, VRANGESS, "vrangess", 0x0f3a51, 0, 32) \
    BUSTER_X86_AVX512_SCALAR_IMMEDIATE_SAE_FORMS(XV, VRANGESD, "vrangesd", 0x0f3a51, 1, 64) \
    BUSTER_X86_AVX512_SCALAR_IMMEDIATE_SAE_FORMS(XV, VREDUCESS, "vreducess", 0x0f3a57, 0, 32) \
    BUSTER_X86_AVX512_SCALAR_IMMEDIATE_SAE_FORMS(XV, VREDUCESD, "vreducesd", 0x0f3a57, 1, 64) \
    XV(VFPCLASSSS_K_XM32_IMM8, "vfpclassss", EVEX, 0x66, 3, 0x0f3a67, R, 1, 0, 0, K, NONE, XM, 4, 4, K, REG_RM_IMM) \
    XV(VFPCLASSSD_K_XM64_IMM8, "vfpclasssd", EVEX, 0x66, 3, 0x0f3a67, R, 1, 1, 0, K, NONE, XM, 8, 8, K, REG_RM_IMM) \
    XV(VCVTSS2SD_X_X_XM32_EVEX, "vcvtss2sd", EVEX, 0xf3, 2, 0x0f5a, R, 0, 0, 0, V, V, XM, 4, 4, KZ, REG_VVVV_RM) \
    XV(VCVTSS2SD_X_X_X_SAE, "vcvtss2sd", EVEX, 0xf3, 2, 0x0f5a, R, 0, 0, 0, V, V, V, 4, 4, KZ_SAE, REG_VVVV_RM) \
    XV(VCVTSD2SS_X_X_XM64_EVEX, "vcvtsd2ss", EVEX, 0xf2, 2, 0x0f5a, R, 0, 1, 0, V, V, XM, 8, 8, KZ, REG_VVVV_RM) \
    XV(VCVTSD2SS_X_X_X_ER, "vcvtsd2ss", EVEX, 0xf2, 2, 0x0f5a, R, 0, 1, 0, V, V, V, 8, 8, KZ_ER, REG_VVVV_RM) \
    BUSTER_X86_AVX512_TO_INTEGER_FORMS(XV, VCVT, "vcvt", 0x0f2d, 0x0f79, ER) \
    BUSTER_X86_AVX512_TO_INTEGER_FORMS(XV, VCVTT, "vcvtt", 0x0f2c, 0x0f78, SAE) \
    XV(VCVTSI2SS_X_X_RM32_EVEX, "vcvtsi2ss", EVEX, 0xf3, 2, 0x0f2a, R, 0, 0, 0, V, V, ED, 4, 4, NONE, REG_VVVV_RM) \
    XV(VCVTSI2SS_X_X_RM64_EVEX, "vcvtsi2ss", EVEX, 0xf3, 2, 0x0f2a, R, 0, 1, 0, V, V, EQ, 8, 8, NONE, REG_VVVV_RM) \
    XV(VCVTSI2SS_X_X_R32_ER, "vcvtsi2ss", EVEX, 0xf3, 2, 0x0f2a, R, 0, 0, 0, V, V, GD, 4, 4, ER, REG_VVVV_RM) \
    XV(VCVTSI2SS_X_X_R64_ER, "vcvtsi2ss", EVEX, 0xf3, 2, 0x0f2a, R, 0, 1, 0, V, V, GQ, 8, 8, ER, REG_VVVV_RM) \
    XV(VCVTSI2SD_X_X_RM32_EVEX, "vcvtsi2sd", EVEX, 0xf2, 2, 0x0f2a, R, 0, 0, 0, V, V, ED, 4, 4, NONE, REG_VVVV_RM) \
    XV(VCVTSI2SD_X_X_RM64_EVEX, "vcvtsi2sd", EVEX, 0xf2, 2, 0x0f2a, R, 0, 1, 0, V, V, EQ, 8, 8, NONE, REG_VVVV_RM) \
    XV(VCVTSI2SD_X_X_R64_ER, "vcvtsi2sd", EVEX, 0xf2, 2, 0x0f2a, R, 0, 1, 0, V, V, GQ, 8, 8, ER, REG_VVVV_RM) \
    XV(VCVTUSI2SS_X_X_RM32, "vcvtusi2ss", EVEX, 0xf3, 2, 0x0f7b, R, 0, 0, 0, V, V, ED, 4, 4, NONE, REG_VVVV_RM) \
    XV(VCVTUSI2SS_X_X_RM64, "vcvtusi2ss", EVEX, 0xf3, 2, 0x0f7b, R, 0, 1, 0, V, V, EQ, 8, 8, NONE, REG_VVVV_RM) \
    XV(VCVTUSI2SS_X_X_R32_ER, "vcvtusi2ss", EVEX, 0xf3, 2, 0x0f7b, R, 0, 0, 0, V, V, GD, 4, 4, ER, REG_VVVV_RM) \
    XV(VCVTUSI2SS_X_X_R64_ER, "vcvtusi2ss", EVEX, 0xf3, 2, 0x0f7b, R, 0, 1, 0, V, V, GQ, 8, 8, ER, REG_VVVV_RM) \
    XV(VCVTUSI2SD_X_X_RM32, "vcvtusi2sd", EVEX, 0xf2, 2, 0x0f7b, R, 0, 0, 0, V, V, ED, 4, 4, NONE, REG_VVVV_RM) \
    XV(VCVTUSI2SD_X_X_RM64, "vcvtusi2sd", EVEX, 0xf2, 2, 0x0f7b, R, 0, 1, 0, V, V, EQ, 8, 8, NONE, REG_VVVV_RM) \
    XV(VCVTUSI2SD_X_X_R64_ER, "vcvtusi2sd", EVEX, 0xf2, 2, 0x0f7b, R, 0, 1, 0, V, V, GQ, 8, 8, ER, REG_VVVV_RM) \
    BUSTER_X86_AVX512_SCALAR_COMPARE_FORMS(XV, VCOMI, "vcomi", 0x0f2f) \
    BUSTER_X86_AVX512_SCALAR_COMPARE_FORMS(XV, VUCOMI, "vucomi", 0x0f2e) \
    XV(VCMPSS_K_X_XM32_IMM8, "vcmpss", EVEX, 0xf3, 2, 0x0fc2, R, 1, 0, 0, K, V, XM, 4, 4, K, REG_VVVV_RM_IMM) \
    XV(VCMPSS_K_X_X_SAE_IMM8, "vcmpss", EVEX, 0xf3, 2, 0x0fc2, R, 1, 0, 0, K, V, V, 4, 4, K_SAE, REG_VVVV_RM_IMM) \
    XV(VCMPSD_K_X_XM64_IMM8, "vcmpsd", EVEX, 0xf2, 2, 0x0fc2, R, 1, 1, 0, K, V, XM, 8, 8, K, REG_VVVV_RM_IMM) \
    XV(VCMPSD_K_X_X_SAE_IMM8, "vcmpsd", EVEX, 0xf2, 2, 0x0fc2, R, 1, 1, 0, K, V, V, 8, 8, K_SAE, REG_VVVV_RM_IMM) \
    BUSTER_X86_AVX512_COMPARE_SAE_FORMS(XV, VCMPPS, "vcmpps", 0, 0, 32) \
    BUSTER_X86_AVX512VL_COMPARE_IMMEDIATE_FORMS(XV, VCMPPS, "vcmpps", 0, 2, 0x0fc2, 0, 32) \
    BUSTER_X86_AVX512_COMPARE_SAE_FORMS(XV, VCMPPD, "vcmppd", 0x66, 1, 64) \
    BUSTER_X86_AVX512VL_COMPARE_IMMEDIATE_FORMS(XV, VCMPPD, "vcmppd", 0x66, 2, 0x0fc2, 1, 64) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPADDB, "vpaddb", 0x66, 2, 0x0ffc, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPADDW, "vpaddw", 0x66, 2, 0x0ffd, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPADDSB, "vpaddsb", 0x66, 2, 0x0fec, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPADDSW, "vpaddsw", 0x66, 2, 0x0fed, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPADDUSB, "vpaddusb", 0x66, 2, 0x0fdc, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPADDUSW, "vpaddusw", 0x66, 2, 0x0fdd, 0, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPADDD, "vpaddd", 0x66, 2, 0x0ffe, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPADDD, "vpaddd", 0x66, 2, 0x0ffe, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPADDQ, "vpaddq", 0x66, 2, 0x0fd4, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPADDQ, "vpaddq", 0x66, 2, 0x0fd4, 1, 64, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPSUBB, "vpsubb", 0x66, 2, 0x0ff8, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPSUBW, "vpsubw", 0x66, 2, 0x0ff9, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPSUBSB, "vpsubsb", 0x66, 2, 0x0fe8, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPSUBSW, "vpsubsw", 0x66, 2, 0x0fe9, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPSUBUSB, "vpsubusb", 0x66, 2, 0x0fd8, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPSUBUSW, "vpsubusw", 0x66, 2, 0x0fd9, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPAVGB, "vpavgb", 0x66, 2, 0x0fe0, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPAVGW, "vpavgw", 0x66, 2, 0x0fe3, 0, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPSUBD, "vpsubd", 0x66, 2, 0x0ffa, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPSUBD, "vpsubd", 0x66, 2, 0x0ffa, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPSUBQ, "vpsubq", 0x66, 2, 0x0ffb, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPSUBQ, "vpsubq", 0x66, 2, 0x0ffb, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPANDD, "vpandd", 0x66, 2, 0x0fdb, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPANDD, "vpandd", 0x66, 2, 0x0fdb, 0, 32, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPANDQ, "vpandq", 0x66, 2, 0x0fdb, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPANDQ, "vpandq", 0x66, 2, 0x0fdb, 1, 64, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPANDND, "vpandnd", 0x66, 2, 0x0fdf, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPANDND, "vpandnd", 0x66, 2, 0x0fdf, 0, 32, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPANDNQ, "vpandnq", 0x66, 2, 0x0fdf, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPANDNQ, "vpandnq", 0x66, 2, 0x0fdf, 1, 64, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPORD, "vpord", 0x66, 2, 0x0feb, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPORD, "vpord", 0x66, 2, 0x0feb, 0, 32, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPORQ, "vporq", 0x66, 2, 0x0feb, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPORQ, "vporq", 0x66, 2, 0x0feb, 1, 64, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPXORD, "vpxord", 0x66, 2, 0x0fef, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPXORD, "vpxord", 0x66, 2, 0x0fef, 0, 32, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPXORQ, "vpxorq", 0x66, 2, 0x0fef, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPXORQ, "vpxorq", 0x66, 2, 0x0fef, 1, 64, ) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPMINUB, "vpminub", 0x66, 2, 0x0fda, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPMAXUB, "vpmaxub", 0x66, 2, 0x0fde, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPMINSB, "vpminsb", 0x66, 3, 0x0f3838, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPMAXSB, "vpmaxsb", 0x66, 3, 0x0f383c, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPMINUW, "vpminuw", 0x66, 3, 0x0f383a, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPMAXUW, "vpmaxuw", 0x66, 3, 0x0f383e, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPMINSW, "vpminsw", 0x66, 2, 0x0fea, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPMAXSW, "vpmaxsw", 0x66, 2, 0x0fee, 0, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPMINUD, "vpminud", 0x66, 3, 0x0f383b, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPMINUD, "vpminud", 0x66, 3, 0x0f383b, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPMAXUD, "vpmaxud", 0x66, 3, 0x0f383f, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPMAXUD, "vpmaxud", 0x66, 3, 0x0f383f, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPMINSD, "vpminsd", 0x66, 3, 0x0f3839, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPMINSD, "vpminsd", 0x66, 3, 0x0f3839, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPMAXSD, "vpmaxsd", 0x66, 3, 0x0f383d, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPMAXSD, "vpmaxsd", 0x66, 3, 0x0f383d, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPMINUQ, "vpminuq", 0x66, 3, 0x0f383b, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPMINUQ, "vpminuq", 0x66, 3, 0x0f383b, 1, 64, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPMAXUQ, "vpmaxuq", 0x66, 3, 0x0f383f, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPMAXUQ, "vpmaxuq", 0x66, 3, 0x0f383f, 1, 64, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPMINSQ, "vpminsq", 0x66, 3, 0x0f3839, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPMINSQ, "vpminsq", 0x66, 3, 0x0f3839, 1, 64, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPMAXSQ, "vpmaxsq", 0x66, 3, 0x0f383d, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPMAXSQ, "vpmaxsq", 0x66, 3, 0x0f383d, 1, 64, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPMULLD, "vpmulld", 0x66, 3, 0x0f3840, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPMULLD, "vpmulld", 0x66, 3, 0x0f3840, 0, 32, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPMULLW, "vpmullw", 0x66, 2, 0x0fd5, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPMULHW, "vpmulhw", 0x66, 2, 0x0fe5, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPMULHUW, "vpmulhuw", 0x66, 2, 0x0fe4, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPMULHRSW, "vpmulhrsw", 0x66, 3, 0x0f380b, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPMADDWD, "vpmaddwd", 0x66, 2, 0x0ff5, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPMADDUBSW, "vpmaddubsw", 0x66, 3, 0x0f3804, 0, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPMULLQ, "vpmullq", 0x66, 3, 0x0f3840, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPMULLQ, "vpmullq", 0x66, 3, 0x0f3840, 1, 64, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPMULUDQ, "vpmuludq", 0x66, 2, 0x0ff4, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPMULUDQ, "vpmuludq", 0x66, 2, 0x0ff4, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPMULDQ, "vpmuldq", 0x66, 3, 0x0f3828, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPMULDQ, "vpmuldq", 0x66, 3, 0x0f3828, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPDPBUSD, "vpdpbusd", 0x66, 3, 0x0f3850, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPDPBUSD, "vpdpbusd", 0x66, 3, 0x0f3850, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPDPBUSDS, "vpdpbusds", 0x66, 3, 0x0f3851, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPDPBUSDS, "vpdpbusds", 0x66, 3, 0x0f3851, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPDPWSSD, "vpdpwssd", 0x66, 3, 0x0f3852, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPDPWSSD, "vpdpwssd", 0x66, 3, 0x0f3852, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPDPWSSDS, "vpdpwssds", 0x66, 3, 0x0f3853, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPDPWSSDS, "vpdpwssds", 0x66, 3, 0x0f3853, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPMADD52LUQ, "vpmadd52luq", 0x66, 3, 0x0f38b4, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPMADD52LUQ, "vpmadd52luq", 0x66, 3, 0x0f38b4, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPMADD52HUQ, "vpmadd52huq", 0x66, 3, 0x0f38b5, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPMADD52HUQ, "vpmadd52huq", 0x66, 3, 0x0f38b5, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VCVTNE2PS2BF16, "vcvtne2ps2bf16", 0xf2, 3, 0x0f3872, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VCVTNE2PS2BF16, "vcvtne2ps2bf16", 0xf2, 3, 0x0f3872, 0, 32, ) \
    XV(VCVTNEPS2BF16_Y_ZM512, "vcvtneps2bf16", EVEX, 0xf3, 3, 0x0f3872, R, 0, 0, 2, Y, NONE, ZM, 64, 64, KZ, REG_RM) \
    XV(VCVTNEPS2BF16_Y_M32BCST, "vcvtneps2bf16", EVEX, 0xf3, 3, 0x0f3872, R, 0, 0, 2, Y, NONE, M, 64, 4, KZ_BCST, REG_RM) \
    XV(VCVTNEPS2BF16_X_XM128_EVEX, "vcvtneps2bf16", EVEX, 0xf3, 3, 0x0f3872, R, 0, 0, 0, V, NONE, XM, 16, 16, KZ, REG_RM) \
    XV(VCVTNEPS2BF16_X_M32BCST4, "vcvtneps2bf16", EVEX, 0xf3, 3, 0x0f3872, R, 0, 0, 0, V, NONE, M, 16, 4, KZ_BCST, REG_RM) \
    XV(VCVTNEPS2BF16_X_YM256_EVEX, "vcvtneps2bf16", EVEX, 0xf3, 3, 0x0f3872, R, 0, 0, 1, V, NONE, YM, 32, 32, KZ, REG_RM) \
    XV(VCVTNEPS2BF16_X_M32BCST8, "vcvtneps2bf16", EVEX, 0xf3, 3, 0x0f3872, R, 0, 0, 1, V, NONE, M, 32, 4, KZ_BCST, REG_RM) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VDPBF16PS, "vdpbf16ps", 0xf3, 3, 0x0f3852, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VDPBF16PS, "vdpbf16ps", 0xf3, 3, 0x0f3852, 0, 32, ) \
    BUSTER_X86_AVX512_IMMEDIATE_FORMS(XV, VGF2P8AFFINEQB, "vgf2p8affineqb", 0x66, 3, 0x0f3ace, 1, 64) \
    BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, VGF2P8AFFINEQB, "vgf2p8affineqb", 0x66, 3, 0x0f3ace, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_IMMEDIATE_FORMS(XV, VGF2P8AFFINEINVQB, "vgf2p8affineinvqb", 0x66, 3, 0x0f3acf, 1, 64) \
    BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, VGF2P8AFFINEINVQB, "vgf2p8affineinvqb", 0x66, 3, 0x0f3acf, 1, 64, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VGF2P8MULB, "vgf2p8mulb", 0x66, 3, 0x0f38cf, 0, _EVEX) \
    BUSTER_X86_AVX512_AES_FORMS(XV, VAESENC, "vaesenc", 0x0f38dc) \
    BUSTER_X86_AVX512_AES_FORMS(XV, VAESENCLAST, "vaesenclast", 0x0f38dd) \
    BUSTER_X86_AVX512_AES_FORMS(XV, VAESDEC, "vaesdec", 0x0f38de) \
    BUSTER_X86_AVX512_AES_FORMS(XV, VAESDECLAST, "vaesdeclast", 0x0f38df) \
    XV(VPCLMULQDQ_Z_Z_ZM512_IMM8, "vpclmulqdq", EVEX, 0x66, 3, 0x0f3a44, R, 1, 0, 2, Z, Z, ZM, 64, 64, NONE, REG_VVVV_RM_IMM) \
    XV(VPCLMULQDQ_X_X_XM128_IMM8_EVEX, "vpclmulqdq", EVEX, 0x66, 3, 0x0f3a44, R, 1, 0, 0, V, V, XM, 16, 16, NONE, REG_VVVV_RM_IMM) \
    XV(VPCLMULQDQ_Y_Y_YM256_IMM8_EVEX, "vpclmulqdq", EVEX, 0x66, 3, 0x0f3a44, R, 1, 0, 1, Y, Y, YM, 32, 32, NONE, REG_VVVV_RM_IMM) \
    BUSTER_X86_AVX512_INTERSECT_FORMS(XV, VP2INTERSECTD, "vp2intersectd", 0, 32) \
    BUSTER_X86_AVX512_INTERSECT_FORMS(XV, VP2INTERSECTQ, "vp2intersectq", 1, 64) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPSHUFB, "vpshufb", 0x66, 3, 0x0f3800, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPERMB, "vpermb", 0x66, 3, 0x0f388d, 0, ) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPERMW, "vpermw", 0x66, 3, 0x0f388d, 1, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPERMD, "vpermd", 0x66, 3, 0x0f3836, 0, 32) \
    BUSTER_X86_AVX512VL_256_BROADCAST_FORMS(XV, VPERMD, "vpermd", 0x66, 3, 0x0f3836, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPERMQ, "vpermq", 0x66, 3, 0x0f3836, 1, 64) \
    BUSTER_X86_AVX512VL_256_BROADCAST_FORMS(XV, VPERMQ, "vpermq", 0x66, 3, 0x0f3836, 1, 64, ) \
    BUSTER_X86_AVX512_UNARY_IMMEDIATE_FORMS(XV, VPERMQ, "vpermq", 0x66, 3, 0x0f3a00, 1, 64) \
    BUSTER_X86_AVX512VL_256_UNARY_IMMEDIATE_FORMS(XV, VPERMQ, "vpermq", 0x66, 3, 0x0f3a00, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPERMT2D, "vpermt2d", 0x66, 3, 0x0f387e, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPERMT2D, "vpermt2d", 0x66, 3, 0x0f387e, 0, 32, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPERMT2Q, "vpermt2q", 0x66, 3, 0x0f387e, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPERMT2Q, "vpermt2q", 0x66, 3, 0x0f387e, 1, 64, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPERMI2D, "vpermi2d", 0x66, 3, 0x0f3876, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPERMI2D, "vpermi2d", 0x66, 3, 0x0f3876, 0, 32, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPERMI2Q, "vpermi2q", 0x66, 3, 0x0f3876, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPERMI2Q, "vpermi2q", 0x66, 3, 0x0f3876, 1, 64, ) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPERMT2B, "vpermt2b", 0x66, 3, 0x0f387d, 0, ) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPERMI2B, "vpermi2b", 0x66, 3, 0x0f3875, 0, ) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPERMT2W, "vpermt2w", 0x66, 3, 0x0f387d, 1, ) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPERMI2W, "vpermi2w", 0x66, 3, 0x0f3875, 1, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPMULTISHIFTQB, "vpmultishiftqb", 0x66, 3, 0x0f3883, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPMULTISHIFTQB, "vpmultishiftqb", 0x66, 3, 0x0f3883, 1, 64, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPSRLVD, "vpsrlvd", 0x66, 3, 0x0f3845, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPSRLVD, "vpsrlvd", 0x66, 3, 0x0f3845, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPSRAVD, "vpsravd", 0x66, 3, 0x0f3846, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPSRAVD, "vpsravd", 0x66, 3, 0x0f3846, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPSLLVD, "vpsllvd", 0x66, 3, 0x0f3847, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPSLLVD, "vpsllvd", 0x66, 3, 0x0f3847, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPSRLVQ, "vpsrlvq", 0x66, 3, 0x0f3845, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPSRLVQ, "vpsrlvq", 0x66, 3, 0x0f3845, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPSRAVQ, "vpsravq", 0x66, 3, 0x0f3846, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPSRAVQ, "vpsravq", 0x66, 3, 0x0f3846, 1, 64, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPSLLVQ, "vpsllvq", 0x66, 3, 0x0f3847, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPSLLVQ, "vpsllvq", 0x66, 3, 0x0f3847, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPROLVD, "vprolvd", 0x66, 3, 0x0f3815, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPROLVD, "vprolvd", 0x66, 3, 0x0f3815, 0, 32, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPROLVQ, "vprolvq", 0x66, 3, 0x0f3815, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPROLVQ, "vprolvq", 0x66, 3, 0x0f3815, 1, 64, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPRORVD, "vprorvd", 0x66, 3, 0x0f3814, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPRORVD, "vprorvd", 0x66, 3, 0x0f3814, 0, 32, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPRORVQ, "vprorvq", 0x66, 3, 0x0f3814, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPRORVQ, "vprorvq", 0x66, 3, 0x0f3814, 1, 64, ) \
    BUSTER_X86_AVX512_IMMEDIATE_FORMS(XV, VPSHLDD, "vpshldd", 0x66, 3, 0x0f3a71, 0, 32) \
    BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, VPSHLDD, "vpshldd", 0x66, 3, 0x0f3a71, 0, 32, ) \
    BUSTER_X86_AVX512_IMMEDIATE_FORMS(XV, VPSHLDQ, "vpshldq", 0x66, 3, 0x0f3a71, 1, 64) \
    BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, VPSHLDQ, "vpshldq", 0x66, 3, 0x0f3a71, 1, 64, ) \
    BUSTER_X86_AVX512BW_IMMEDIATE_FORMS(XV, VPSHLDW, "vpshldw", 0x66, 3, 0x0f3a70, 1, ) \
    BUSTER_X86_AVX512_IMMEDIATE_FORMS(XV, VPSHRDD, "vpshrdd", 0x66, 3, 0x0f3a73, 0, 32) \
    BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, VPSHRDD, "vpshrdd", 0x66, 3, 0x0f3a73, 0, 32, ) \
    BUSTER_X86_AVX512_IMMEDIATE_FORMS(XV, VPSHRDQ, "vpshrdq", 0x66, 3, 0x0f3a73, 1, 64) \
    BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, VPSHRDQ, "vpshrdq", 0x66, 3, 0x0f3a73, 1, 64, ) \
    BUSTER_X86_AVX512BW_IMMEDIATE_FORMS(XV, VPSHRDW, "vpshrdw", 0x66, 3, 0x0f3a72, 1, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPSHLDVD, "vpshldvd", 0x66, 3, 0x0f3871, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPSHLDVD, "vpshldvd", 0x66, 3, 0x0f3871, 0, 32, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPSHLDVQ, "vpshldvq", 0x66, 3, 0x0f3871, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPSHLDVQ, "vpshldvq", 0x66, 3, 0x0f3871, 1, 64, ) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPSHLDVW, "vpshldvw", 0x66, 3, 0x0f3870, 1, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPSHRDVD, "vpshrdvd", 0x66, 3, 0x0f3873, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPSHRDVD, "vpshrdvd", 0x66, 3, 0x0f3873, 0, 32, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPSHRDVQ, "vpshrdvq", 0x66, 3, 0x0f3873, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPSHRDVQ, "vpshrdvq", 0x66, 3, 0x0f3873, 1, 64, ) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPSHRDVW, "vpshrdvw", 0x66, 3, 0x0f3872, 1, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPBLENDMD, "vpblendmd", 0x66, 3, 0x0f3864, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPBLENDMD, "vpblendmd", 0x66, 3, 0x0f3864, 0, 32, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPBLENDMQ, "vpblendmq", 0x66, 3, 0x0f3864, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPBLENDMQ, "vpblendmq", 0x66, 3, 0x0f3864, 1, 64, ) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPBLENDMB, "vpblendmb", 0x66, 3, 0x0f3866, 0, ) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPBLENDMW, "vpblendmw", 0x66, 3, 0x0f3866, 1, ) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPSLLVW, "vpsllvw", 0x66, 3, 0x0f3812, 1, ) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPSRAVW, "vpsravw", 0x66, 3, 0x0f3811, 1, ) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPSRLVW, "vpsrlvw", 0x66, 3, 0x0f3810, 1, ) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPUNPCKLBW, "vpunpcklbw", 0x66, 2, 0x0f60, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPUNPCKHBW, "vpunpckhbw", 0x66, 2, 0x0f68, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPUNPCKLWD, "vpunpcklwd", 0x66, 2, 0x0f61, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPUNPCKHWD, "vpunpckhwd", 0x66, 2, 0x0f69, 0, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPUNPCKLDQ, "vpunpckldq", 0x66, 2, 0x0f62, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPUNPCKLDQ, "vpunpckldq", 0x66, 2, 0x0f62, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPUNPCKLQDQ, "vpunpcklqdq", 0x66, 2, 0x0f6c, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPUNPCKLQDQ, "vpunpcklqdq", 0x66, 2, 0x0f6c, 1, 64, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPUNPCKHDQ, "vpunpckhdq", 0x66, 2, 0x0f6a, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPUNPCKHDQ, "vpunpckhdq", 0x66, 2, 0x0f6a, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPUNPCKHQDQ, "vpunpckhqdq", 0x66, 2, 0x0f6d, 1, 64) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPUNPCKHQDQ, "vpunpckhqdq", 0x66, 2, 0x0f6d, 1, 64, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPACKUSWB, "vpackuswb", 0x66, 2, 0x0f67, 0, _EVEX) \
    BUSTER_X86_AVX512BW_PACKED_FORMS(XV, VPACKSSWB, "vpacksswb", 0x66, 2, 0x0f63, 0, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPACKSSDW, "vpackssdw", 0x66, 2, 0x0f6b, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPACKSSDW, "vpackssdw", 0x66, 2, 0x0f6b, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPACKUSDW, "vpackusdw", 0x66, 3, 0x0f382b, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPACKUSDW, "vpackusdw", 0x66, 3, 0x0f382b, 0, 32, _EVEX) \
    XV(VPSADBW_Z_Z_ZM512, "vpsadbw", EVEX, 0x66, 2, 0x0ff6, R, 0, 0, 2, Z, Z, ZM, 64, 64, NONE, REG_VVVV_RM) \
    XV(VPSADBW_X_X_XM128_EVEX, "vpsadbw", EVEX, 0x66, 2, 0x0ff6, R, 0, 0, 0, V, V, XM, 16, 16, NONE, REG_VVVV_RM) \
    XV(VPSADBW_Y_Y_YM256_EVEX, "vpsadbw", EVEX, 0x66, 2, 0x0ff6, R, 0, 0, 1, Y, Y, YM, 32, 32, NONE, REG_VVVV_RM) \
    BUSTER_X86_AVX512BW_IMMEDIATE_FORMS(XV, VDBPSADBW, "vdbpsadbw", 0x66, 3, 0x0f3a42, 0, ) \
    BUSTER_X86_AVX512_SHIFT_COUNT_FORM(XV, VPSLLD, "vpslld", 0xf2, 0) \
    BUSTER_X86_AVX512VL_SHIFT_COUNT_FORMS(XV, VPSLLD, "vpslld", 0xf2, 0, _EVEX) \
    BUSTER_X86_AVX512_SHIFT_COUNT_FORM(XV, VPSRLD, "vpsrld", 0xd2, 0) \
    BUSTER_X86_AVX512VL_SHIFT_COUNT_FORMS(XV, VPSRLD, "vpsrld", 0xd2, 0, _EVEX) \
    BUSTER_X86_AVX512_SHIFT_COUNT_FORM(XV, VPSRAD, "vpsrad", 0xe2, 0) \
    BUSTER_X86_AVX512VL_SHIFT_COUNT_FORMS(XV, VPSRAD, "vpsrad", 0xe2, 0, _EVEX) \
    BUSTER_X86_AVX512_SHIFT_COUNT_FORM(XV, VPSLLQ, "vpsllq", 0xf3, 1) \
    BUSTER_X86_AVX512VL_SHIFT_COUNT_FORMS(XV, VPSLLQ, "vpsllq", 0xf3, 1, _EVEX) \
    BUSTER_X86_AVX512_SHIFT_COUNT_FORM(XV, VPSRLQ, "vpsrlq", 0xd3, 1) \
    BUSTER_X86_AVX512VL_SHIFT_COUNT_FORMS(XV, VPSRLQ, "vpsrlq", 0xd3, 1, _EVEX) \
    BUSTER_X86_AVX512_SHIFT_COUNT_FORM(XV, VPSRAQ, "vpsraq", 0xe2, 1) \
    BUSTER_X86_AVX512VL_SHIFT_COUNT_FORMS(XV, VPSRAQ, "vpsraq", 0xe2, 1, ) \
    BUSTER_X86_AVX512_SHIFT_COUNT_FORM(XV, VPSLLW, "vpsllw", 0xf1, 0) \
    BUSTER_X86_AVX512VL_SHIFT_COUNT_FORMS(XV, VPSLLW, "vpsllw", 0xf1, 0, _EVEX) \
    BUSTER_X86_AVX512_SHIFT_COUNT_FORM(XV, VPSRLW, "vpsrlw", 0xd1, 0) \
    BUSTER_X86_AVX512VL_SHIFT_COUNT_FORMS(XV, VPSRLW, "vpsrlw", 0xd1, 0, _EVEX) \
    BUSTER_X86_AVX512_SHIFT_COUNT_FORM(XV, VPSRAW, "vpsraw", 0xe1, 0) \
    BUSTER_X86_AVX512VL_SHIFT_COUNT_FORMS(XV, VPSRAW, "vpsraw", 0xe1, 0, _EVEX) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VPOPCNTD, "vpopcntd", 0x66, 3, 0x0f3855, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VPOPCNTD, "vpopcntd", 0x66, 3, 0x0f3855, 0, 32, ) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VPOPCNTQ, "vpopcntq", 0x66, 3, 0x0f3855, 1, 64) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VPOPCNTQ, "vpopcntq", 0x66, 3, 0x0f3855, 1, 64, ) \
    BUSTER_X86_AVX512BW_UNARY_FORMS(XV, VPOPCNTB, "vpopcntb", 0x66, 3, 0x0f3854, 0, ) \
    BUSTER_X86_AVX512BW_UNARY_FORMS(XV, VPOPCNTW, "vpopcntw", 0x66, 3, 0x0f3854, 1, ) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VPLZCNTD, "vplzcntd", 0x66, 3, 0x0f3844, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VPLZCNTD, "vplzcntd", 0x66, 3, 0x0f3844, 0, 32, ) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VPLZCNTQ, "vplzcntq", 0x66, 3, 0x0f3844, 1, 64) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VPLZCNTQ, "vplzcntq", 0x66, 3, 0x0f3844, 1, 64, ) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VPCONFLICTD, "vpconflictd", 0x66, 3, 0x0f38c4, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VPCONFLICTD, "vpconflictd", 0x66, 3, 0x0f38c4, 0, 32, ) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VPCONFLICTQ, "vpconflictq", 0x66, 3, 0x0f38c4, 1, 64) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VPCONFLICTQ, "vpconflictq", 0x66, 3, 0x0f38c4, 1, 64, ) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VPABSD, "vpabsd", 0x66, 3, 0x0f381e, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VPABSD, "vpabsd", 0x66, 3, 0x0f381e, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VPABSQ, "vpabsq", 0x66, 3, 0x0f381f, 1, 64) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VPABSQ, "vpabsq", 0x66, 3, 0x0f381f, 1, 64, ) \
    BUSTER_X86_AVX512BW_UNARY_FORMS(XV, VPABSB, "vpabsb", 0x66, 3, 0x0f381c, 0, _EVEX) \
    BUSTER_X86_AVX512BW_UNARY_FORMS(XV, VPABSW, "vpabsw", 0x66, 3, 0x0f381d, 0, _EVEX) \
    BUSTER_X86_AVX512_IMMEDIATE_FORMS(XV, VPTERNLOGD, "vpternlogd", 0x66, 3, 0x0f3a25, 0, 32) \
    BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, VPTERNLOGD, "vpternlogd", 0x66, 3, 0x0f3a25, 0, 32, ) \
    BUSTER_X86_AVX512_IMMEDIATE_FORMS(XV, VPTERNLOGQ, "vpternlogq", 0x66, 3, 0x0f3a25, 1, 64) \
    BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, VPTERNLOGQ, "vpternlogq", 0x66, 3, 0x0f3a25, 1, 64, ) \
    BUSTER_X86_AVX512_IMMEDIATE_FORMS(XV, VALIGND, "valignd", 0x66, 3, 0x0f3a03, 0, 32) \
    BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, VALIGND, "valignd", 0x66, 3, 0x0f3a03, 0, 32, ) \
    BUSTER_X86_AVX512_IMMEDIATE_FORMS(XV, VALIGNQ, "valignq", 0x66, 3, 0x0f3a03, 1, 64) \
    BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, VALIGNQ, "valignq", 0x66, 3, 0x0f3a03, 1, 64, ) \
    BUSTER_X86_AVX512_IMMEDIATE_FORMS(XV, VSHUFI32X4, "vshufi32x4", 0x66, 3, 0x0f3a43, 0, 32) \
    BUSTER_X86_AVX512VL_256_IMMEDIATE_FORMS(XV, VSHUFI32X4, "vshufi32x4", 0x66, 3, 0x0f3a43, 0, 32, ) \
    BUSTER_X86_AVX512_IMMEDIATE_FORMS(XV, VSHUFI64X2, "vshufi64x2", 0x66, 3, 0x0f3a43, 1, 64) \
    BUSTER_X86_AVX512VL_256_IMMEDIATE_FORMS(XV, VSHUFI64X2, "vshufi64x2", 0x66, 3, 0x0f3a43, 1, 64, ) \
    BUSTER_X86_AVX512BW_IMMEDIATE_FORMS(XV, VPALIGNR, "vpalignr", 0x66, 3, 0x0f3a0f, 0, _EVEX) \
    BUSTER_X86_AVX512_UNARY_IMMEDIATE_FORMS(XV, VPSHUFD, "vpshufd", 0x66, 2, 0x0f70, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_IMMEDIATE_FORMS(XV, VPSHUFD, "vpshufd", 0x66, 2, 0x0f70, 0, 32, _EVEX) \
    BUSTER_X86_AVX512BW_UNARY_IMMEDIATE_FORMS(XV, VPSHUFHW, "vpshufhw", 0xf3, 2, 0x0f70, 0, _EVEX) \
    BUSTER_X86_AVX512BW_UNARY_IMMEDIATE_FORMS(XV, VPSHUFLW, "vpshuflw", 0xf2, 2, 0x0f70, 0, _EVEX) \
    BUSTER_X86_AVX512_SHIFT_FORMS(XV, VPSLLD, "vpslld", 0x72, 0, 6, 32) \
    BUSTER_X86_AVX512VL_SHIFT_FORMS(XV, VPSLLD, "vpslld", 0x72, 0, 6, 32) \
    BUSTER_X86_AVX512_SHIFT_FORMS(XV, VPSRLD, "vpsrld", 0x72, 0, 2, 32) \
    BUSTER_X86_AVX512VL_SHIFT_FORMS(XV, VPSRLD, "vpsrld", 0x72, 0, 2, 32) \
    BUSTER_X86_AVX512_SHIFT_FORMS(XV, VPSRAD, "vpsrad", 0x72, 0, 4, 32) \
    BUSTER_X86_AVX512VL_SHIFT_FORMS(XV, VPSRAD, "vpsrad", 0x72, 0, 4, 32) \
    BUSTER_X86_AVX512_SHIFT_FORMS(XV, VPSLLQ, "vpsllq", 0x73, 1, 6, 64) \
    BUSTER_X86_AVX512VL_SHIFT_FORMS(XV, VPSLLQ, "vpsllq", 0x73, 1, 6, 64) \
    BUSTER_X86_AVX512_SHIFT_FORMS(XV, VPSRLQ, "vpsrlq", 0x73, 1, 2, 64) \
    BUSTER_X86_AVX512VL_SHIFT_FORMS(XV, VPSRLQ, "vpsrlq", 0x73, 1, 2, 64) \
    BUSTER_X86_AVX512_SHIFT_FORMS(XV, VPSRAQ, "vpsraq", 0x72, 1, 4, 64) \
    BUSTER_X86_AVX512VL_SHIFT_FORMS(XV, VPSRAQ, "vpsraq", 0x72, 1, 4, 64) \
    BUSTER_X86_AVX512_SHIFT_FORMS(XV, VPROLD, "vprold", 0x72, 0, 1, 32) \
    BUSTER_X86_AVX512VL_SHIFT_FORMS(XV, VPROLD, "vprold", 0x72, 0, 1, 32) \
    BUSTER_X86_AVX512_SHIFT_FORMS(XV, VPROLQ, "vprolq", 0x72, 1, 1, 64) \
    BUSTER_X86_AVX512VL_SHIFT_FORMS(XV, VPROLQ, "vprolq", 0x72, 1, 1, 64) \
    BUSTER_X86_AVX512_SHIFT_FORMS(XV, VPRORD, "vprord", 0x72, 0, 0, 32) \
    BUSTER_X86_AVX512VL_SHIFT_FORMS(XV, VPRORD, "vprord", 0x72, 0, 0, 32) \
    BUSTER_X86_AVX512_SHIFT_FORMS(XV, VPRORQ, "vprorq", 0x72, 1, 0, 64) \
    BUSTER_X86_AVX512VL_SHIFT_FORMS(XV, VPRORQ, "vprorq", 0x72, 1, 0, 64) \
    BUSTER_X86_AVX512BW_SHIFT_FORMS(XV, VPSLLW, "vpsllw", 0x71, 6, KZ) \
    BUSTER_X86_AVX512BW_SHIFT_FORMS(XV, VPSRLW, "vpsrlw", 0x71, 2, KZ) \
    BUSTER_X86_AVX512BW_SHIFT_FORMS(XV, VPSRAW, "vpsraw", 0x71, 4, KZ) \
    BUSTER_X86_AVX512BW_SHIFT_FORMS(XV, VPSLLDQ, "vpslldq", 0x73, 7, NONE) \
    BUSTER_X86_AVX512BW_SHIFT_FORMS(XV, VPSRLDQ, "vpsrldq", 0x73, 3, NONE) \
    BUSTER_X86_AVX512BW_COMPARE_FORMS(XV, VPCMPEQB, "vpcmpeqb", 0x66, 2, 0x0f74, 0) \
    BUSTER_X86_AVX512BW_COMPARE_FORMS(XV, VPCMPEQW, "vpcmpeqw", 0x66, 2, 0x0f75, 0) \
    BUSTER_X86_AVX512BW_COMPARE_FORMS(XV, VPCMPGTB, "vpcmpgtb", 0x66, 2, 0x0f64, 0) \
    BUSTER_X86_AVX512BW_COMPARE_FORMS(XV, VPCMPGTW, "vpcmpgtw", 0x66, 2, 0x0f65, 0) \
    BUSTER_X86_AVX512_COMPARE_BROADCAST_FORMS(XV, VPCMPEQD, "vpcmpeqd", 0x66, 2, 0x0f76, 0, 32) \
    BUSTER_X86_AVX512VL_COMPARE_BROADCAST_FORMS(XV, VPCMPEQD, "vpcmpeqd", 0x66, 2, 0x0f76, 0, 32) \
    BUSTER_X86_AVX512_COMPARE_BROADCAST_FORMS(XV, VPCMPGTD, "vpcmpgtd", 0x66, 2, 0x0f66, 0, 32) \
    BUSTER_X86_AVX512VL_COMPARE_BROADCAST_FORMS(XV, VPCMPGTD, "vpcmpgtd", 0x66, 2, 0x0f66, 0, 32) \
    BUSTER_X86_AVX512_COMPARE_BROADCAST_FORMS(XV, VPCMPEQQ, "vpcmpeqq", 0x66, 3, 0x0f3829, 1, 64) \
    BUSTER_X86_AVX512VL_COMPARE_BROADCAST_FORMS(XV, VPCMPEQQ, "vpcmpeqq", 0x66, 3, 0x0f3829, 1, 64) \
    BUSTER_X86_AVX512_COMPARE_BROADCAST_FORMS(XV, VPCMPGTQ, "vpcmpgtq", 0x66, 3, 0x0f3837, 1, 64) \
    BUSTER_X86_AVX512VL_COMPARE_BROADCAST_FORMS(XV, VPCMPGTQ, "vpcmpgtq", 0x66, 3, 0x0f3837, 1, 64) \
    BUSTER_X86_AVX512_COMPARE_BROADCAST_FORMS(XV, VPTESTMD, "vptestmd", 0x66, 3, 0x0f3827, 0, 32) \
    BUSTER_X86_AVX512VL_COMPARE_BROADCAST_FORMS(XV, VPTESTMD, "vptestmd", 0x66, 3, 0x0f3827, 0, 32) \
    BUSTER_X86_AVX512_COMPARE_BROADCAST_FORMS(XV, VPTESTNMD, "vptestnmd", 0xf3, 3, 0x0f3827, 0, 32) \
    BUSTER_X86_AVX512VL_COMPARE_BROADCAST_FORMS(XV, VPTESTNMD, "vptestnmd", 0xf3, 3, 0x0f3827, 0, 32) \
    BUSTER_X86_AVX512_COMPARE_BROADCAST_FORMS(XV, VPTESTMQ, "vptestmq", 0x66, 3, 0x0f3827, 1, 64) \
    BUSTER_X86_AVX512VL_COMPARE_BROADCAST_FORMS(XV, VPTESTMQ, "vptestmq", 0x66, 3, 0x0f3827, 1, 64) \
    BUSTER_X86_AVX512_COMPARE_BROADCAST_FORMS(XV, VPTESTNMQ, "vptestnmq", 0xf3, 3, 0x0f3827, 1, 64) \
    BUSTER_X86_AVX512VL_COMPARE_BROADCAST_FORMS(XV, VPTESTNMQ, "vptestnmq", 0xf3, 3, 0x0f3827, 1, 64) \
    BUSTER_X86_AVX512BW_COMPARE_FORMS(XV, VPTESTMB, "vptestmb", 0x66, 3, 0x0f3826, 0) \
    BUSTER_X86_AVX512BW_COMPARE_FORMS(XV, VPTESTMW, "vptestmw", 0x66, 3, 0x0f3826, 1) \
    BUSTER_X86_AVX512BW_COMPARE_FORMS(XV, VPTESTNMB, "vptestnmb", 0xf3, 3, 0x0f3826, 0) \
    BUSTER_X86_AVX512BW_COMPARE_FORMS(XV, VPTESTNMW, "vptestnmw", 0xf3, 3, 0x0f3826, 1) \
    BUSTER_X86_AVX512BW_COMPARE_FORMS(XV, VPSHUFBITQMB, "vpshufbitqmb", 0x66, 3, 0x0f388f, 0) \
    /* AMD's AVX512_BMM: 16x16 bit matrix multiplications accumulated with OR and XOR, at 256 and 512 bits and without an opmask, and the bits of each byte reversed. */ \
    XV(VBMACOR16X16X16_Y_Y_YM256, "vbmacor16x16x16", EVEX, 0, 2, 0x0680, R, 0, 0, 1, Y, Y, YM, 32, 32, NONE, REG_VVVV_RM) \
    XV(VBMACOR16X16X16_Z_Z_ZM512, "vbmacor16x16x16", EVEX, 0, 2, 0x0680, R, 0, 0, 2, Z, Z, ZM, 64, 64, NONE, REG_VVVV_RM) \
    XV(VBMACXOR16X16X16_Y_Y_YM256, "vbmacxor16x16x16", EVEX, 0, 2, 0x0680, R, 0, 1, 1, Y, Y, YM, 32, 32, NONE, REG_VVVV_RM) \
    XV(VBMACXOR16X16X16_Z_Z_ZM512, "vbmacxor16x16x16", EVEX, 0, 2, 0x0680, R, 0, 1, 2, Z, Z, ZM, 64, 64, NONE, REG_VVVV_RM) \
    BUSTER_X86_AVX512BW_UNARY_FORMS(XV, VBITREVB, "vbitrevb", 0, 2, 0x0681, 0, ) \
    BUSTER_X86_AVX512_COMPARE_IMMEDIATE_FORMS(XV, VPCMPD, "vpcmpd", 0x0f3a1f, 0, 32) \
    BUSTER_X86_AVX512VL_COMPARE_IMMEDIATE_FORMS(XV, VPCMPD, "vpcmpd", 0x66, 3, 0x0f3a1f, 0, 32) \
    BUSTER_X86_AVX512_COMPARE_IMMEDIATE_FORMS(XV, VPCMPUD, "vpcmpud", 0x0f3a1e, 0, 32) \
    BUSTER_X86_AVX512VL_COMPARE_IMMEDIATE_FORMS(XV, VPCMPUD, "vpcmpud", 0x66, 3, 0x0f3a1e, 0, 32) \
    BUSTER_X86_AVX512_COMPARE_IMMEDIATE_FORMS(XV, VPCMPQ, "vpcmpq", 0x0f3a1f, 1, 64) \
    BUSTER_X86_AVX512VL_COMPARE_IMMEDIATE_FORMS(XV, VPCMPQ, "vpcmpq", 0x66, 3, 0x0f3a1f, 1, 64) \
    BUSTER_X86_AVX512_COMPARE_IMMEDIATE_FORMS(XV, VPCMPUQ, "vpcmpuq", 0x0f3a1e, 1, 64) \
    BUSTER_X86_AVX512VL_COMPARE_IMMEDIATE_FORMS(XV, VPCMPUQ, "vpcmpuq", 0x66, 3, 0x0f3a1e, 1, 64) \
    BUSTER_X86_AVX512BW_COMPARE_IMMEDIATE_FORMS(XV, VPCMPUB, "vpcmpub", 0x0f3a3e, 0) \
    BUSTER_X86_AVX512BW_COMPARE_IMMEDIATE_FORMS(XV, VPCMPB, "vpcmpb", 0x0f3a3f, 0) \
    BUSTER_X86_AVX512BW_COMPARE_IMMEDIATE_FORMS(XV, VPCMPUW, "vpcmpuw", 0x0f3a3e, 1) \
    BUSTER_X86_AVX512BW_COMPARE_IMMEDIATE_FORMS(XV, VPCMPW, "vpcmpw", 0x0f3a3f, 1) \
    /* AVX512-FP16: arithmetic on 16-bit floats, in maps 5 and 6. */ \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, VADDPH, "vaddph", 0, 2, 0x0558, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VADDPH, "vaddph", 0, 2, 0x0558, 0, 16, ) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, VMULPH, "vmulph", 0, 2, 0x0559, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VMULPH, "vmulph", 0, 2, 0x0559, 0, 16, ) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, VSUBPH, "vsubph", 0, 2, 0x055c, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VSUBPH, "vsubph", 0, 2, 0x055c, 0, 16, ) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, VDIVPH, "vdivph", 0, 2, 0x055e, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VDIVPH, "vdivph", 0, 2, 0x055e, 0, 16, ) \
    BUSTER_X86_AVX512_SAE_FORMS(XV, VMINPH, "vminph", 0, 2, 0x055d, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VMINPH, "vminph", 0, 2, 0x055d, 0, 16, ) \
    BUSTER_X86_AVX512_SAE_FORMS(XV, VMAXPH, "vmaxph", 0, 2, 0x055f, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VMAXPH, "vmaxph", 0, 2, 0x055f, 0, 16, ) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, VSCALEFPH, "vscalefph", 0x66, 2, 0x062c, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VSCALEFPH, "vscalefph", 0x66, 2, 0x062c, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_ROUNDING_FORMS(XV, VSQRTPH, "vsqrtph", 0, 2, 0x0551, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VSQRTPH, "vsqrtph", 0, 2, 0x0551, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VGETEXPPH, "vgetexpph", 0x66, 2, 0x0642, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VGETEXPPH, "vgetexpph", 0x66, 2, 0x0642, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VRCPPH, "vrcpph", 0x66, 2, 0x064c, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VRCPPH, "vrcpph", 0x66, 2, 0x064c, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VRSQRTPH, "vrsqrtph", 0x66, 2, 0x064e, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VRSQRTPH, "vrsqrtph", 0x66, 2, 0x064e, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_IMMEDIATE_SAE_FORMS(XV, VGETMANTPH, "vgetmantph", 0, 3, 0x0f3a26, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_IMMEDIATE_FORMS(XV, VGETMANTPH, "vgetmantph", 0, 3, 0x0f3a26, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_IMMEDIATE_SAE_FORMS(XV, VREDUCEPH, "vreduceph", 0, 3, 0x0f3a56, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_IMMEDIATE_FORMS(XV, VREDUCEPH, "vreduceph", 0, 3, 0x0f3a56, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_IMMEDIATE_SAE_FORMS(XV, VRNDSCALEPH, "vrndscaleph", 0, 3, 0x0f3a08, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_IMMEDIATE_FORMS(XV, VRNDSCALEPH, "vrndscaleph", 0, 3, 0x0f3a08, 0, 16, ) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, VADDSH, "vaddsh", 0xf3, 2, 0x0558, 0, 16, ER) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, VMULSH, "vmulsh", 0xf3, 2, 0x0559, 0, 16, ER) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, VSUBSH, "vsubsh", 0xf3, 2, 0x055c, 0, 16, ER) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, VDIVSH, "vdivsh", 0xf3, 2, 0x055e, 0, 16, ER) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, VSQRTSH, "vsqrtsh", 0xf3, 2, 0x0551, 0, 16, ER) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, VMINSH, "vminsh", 0xf3, 2, 0x055d, 0, 16, SAE) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, VMAXSH, "vmaxsh", 0xf3, 2, 0x055f, 0, 16, SAE) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, VSCALEFSH, "vscalefsh", 0x66, 2, 0x062d, 0, 16, ER) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, VGETEXPSH, "vgetexpsh", 0x66, 2, 0x0643, 0, 16, SAE) \
    XV(VRCPSH_X_X_XM16, "vrcpsh", EVEX, 0x66, 2, 0x064d, R, 0, 0, 0, V, V, XM, 2, 2, KZ, REG_VVVV_RM) \
    XV(VRSQRTSH_X_X_XM16, "vrsqrtsh", EVEX, 0x66, 2, 0x064f, R, 0, 0, 0, V, V, XM, 2, 2, KZ, REG_VVVV_RM) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_IMMEDIATE_FORMS(XV, VGETMANTSH, "vgetmantsh", 0, 3, 0x0f3a27, 0, 16, SAE) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_IMMEDIATE_FORMS(XV, VREDUCESH, "vreducesh", 0, 3, 0x0f3a57, 0, 16, SAE) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_IMMEDIATE_FORMS(XV, VRNDSCALESH, "vrndscalesh", 0, 3, 0x0f3a0a, 0, 16, SAE) \
    /* FMAs, and the complex multiplications (of pairs of 16-bit floats, as 32-bit elements). */ \
    BUSTER_X86_AVX512_FMA_PH_FORMS(XV, VFMADD, "vfmadd", 0x0698) \
    BUSTER_X86_AVX512_FMA_PH_FORMS(XV, VFMSUB, "vfmsub", 0x069a) \
    BUSTER_X86_AVX512_FMA_PH_FORMS(XV, VFNMADD, "vfnmadd", 0x069c) \
    BUSTER_X86_AVX512_FMA_PH_FORMS(XV, VFNMSUB, "vfnmsub", 0x069e) \
    BUSTER_X86_AVX512_FMA_PH_FORMS(XV, VFMADDSUB, "vfmaddsub", 0x0696) \
    BUSTER_X86_AVX512_FMA_PH_FORMS(XV, VFMSUBADD, "vfmsubadd", 0x0697) \
    BUSTER_X86_AVX512_FMA_SH_FORMS(XV, VFMADD, "vfmadd", 0x0698) \
    BUSTER_X86_AVX512_FMA_SH_FORMS(XV, VFMSUB, "vfmsub", 0x069a) \
    BUSTER_X86_AVX512_FMA_SH_FORMS(XV, VFNMADD, "vfnmadd", 0x069c) \
    BUSTER_X86_AVX512_FMA_SH_FORMS(XV, VFNMSUB, "vfnmsub", 0x069e) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, VFMADDCPH, "vfmaddcph", 0xf3, 2, 0x0656, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VFMADDCPH, "vfmaddcph", 0xf3, 2, 0x0656, 0, 32, ) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, VFCMADDCPH, "vfcmaddcph", 0xf2, 2, 0x0656, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VFCMADDCPH, "vfcmaddcph", 0xf2, 2, 0x0656, 0, 32, ) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, VFMULCPH, "vfmulcph", 0xf3, 2, 0x06d6, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VFMULCPH, "vfmulcph", 0xf3, 2, 0x06d6, 0, 32, ) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, VFCMULCPH, "vfcmulcph", 0xf2, 2, 0x06d6, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VFCMULCPH, "vfcmulcph", 0xf2, 2, 0x06d6, 0, 32, ) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, VFMADDCSH, "vfmaddcsh", 0xf3, 2, 0x0657, 0, 32, ER) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, VFCMADDCSH, "vfcmaddcsh", 0xf2, 2, 0x0657, 0, 32, ER) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, VFMULCSH, "vfmulcsh", 0xf3, 2, 0x06d7, 0, 32, ER) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, VFCMULCSH, "vfcmulcsh", 0xf2, 2, 0x06d7, 0, 32, ER) \
    /* Compares, classes and moves. */ \
    BUSTER_X86_AVX512_COMPARE_PREDICATE_SAE_FORMS(XV, VCMPPH, "vcmpph", 0, 3, 0x0f3ac2, 0, 16) \
    BUSTER_X86_AVX512VL_COMPARE_IMMEDIATE_FORMS(XV, VCMPPH, "vcmpph", 0, 3, 0x0f3ac2, 0, 16) \
    XV(VCMPSH_K_X_XM16_IMM8, "vcmpsh", EVEX, 0xf3, 3, 0x0f3ac2, R, 1, 0, 0, K, V, XM, 2, 2, K, REG_VVVV_RM_IMM) \
    XV(VCMPSH_K_X_X_SAE_IMM8, "vcmpsh", EVEX, 0xf3, 3, 0x0f3ac2, R, 1, 0, 0, K, V, V, 2, 2, K_SAE, REG_VVVV_RM_IMM) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_COMPARE_FORMS(XV, VCOMISH, "vcomish", 0, 2, 0x052f, 0, 16) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_COMPARE_FORMS(XV, VUCOMISH, "vucomish", 0, 2, 0x052e, 0, 16) \
    BUSTER_X86_AVX512_CLASS_FORMS(XV, VFPCLASSPH, "vfpclassph", 0, 0, 16, 8, 16, 32) \
    XV(VFPCLASSSH_K_XM16_IMM8, "vfpclasssh", EVEX, 0, 3, 0x0f3a67, R, 1, 0, 0, K, NONE, XM, 2, 2, K, REG_RM_IMM) \
    XV(VMOVSH_X_M16, "vmovsh", EVEX, 0xf3, 2, 0x0510, R, 0, 0, 0, V, NONE, M, 2, 2, KZ, REG_RM) \
    XV(VMOVSH_M16_X, "vmovsh", EVEX, 0xf3, 2, 0x0511, R, 0, 0, 0, V, NONE, M, 2, 2, K, RM_REG) \
    XV(VMOVSH_X_X_X, "vmovsh", EVEX, 0xf3, 2, 0x0510, R, 0, 0, 0, V, V, V, 2, 2, KZ, REG_VVVV_RM) \
    XV(VMOVW_X_RM32, "vmovw", EVEX, 0x66, 2, 0x056e, R, 0, 0, 0, V, NONE, ED, 2, 2, NONE, REG_RM) \
    XV(VMOVW_RM32_X, "vmovw", EVEX, 0x66, 2, 0x057e, R, 0, 0, 0, V, NONE, ED, 2, 2, NONE, RM_REG) \
    /* Conversions: between 16-bit floats and integers of the same width, twice or four times as wide, and scalar. */ \
    BUSTER_X86_AVX512_UNARY_ROUNDING_FORMS(XV, VCVTPH2UW, "vcvtph2uw", 0, 2, 0x057d, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTPH2UW, "vcvtph2uw", 0, 2, 0x057d, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_ROUNDING_FORMS(XV, VCVTPH2W, "vcvtph2w", 0x66, 2, 0x057d, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTPH2W, "vcvtph2w", 0x66, 2, 0x057d, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_ROUNDING_FORMS(XV, VCVTUW2PH, "vcvtuw2ph", 0xf2, 2, 0x057d, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTUW2PH, "vcvtuw2ph", 0xf2, 2, 0x057d, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_ROUNDING_FORMS(XV, VCVTW2PH, "vcvtw2ph", 0xf3, 2, 0x057d, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTW2PH, "vcvtw2ph", 0xf3, 2, 0x057d, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VCVTTPH2UW, "vcvttph2uw", 0, 2, 0x057c, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTTPH2UW, "vcvttph2uw", 0, 2, 0x057c, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VCVTTPH2W, "vcvttph2w", 0x66, 2, 0x057c, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTTPH2W, "vcvttph2w", 0x66, 2, 0x057c, 0, 16, ) \
    BUSTER_X86_AVX512_HALF_WIDENING_DECORATED_FORMS(XV, VCVTPH2DQ, "vcvtph2dq", 0x66, 2, 0x055b, 0, 16, ER) \
    BUSTER_X86_AVX512VL_HALF_WIDENING_FORMS(XV, VCVTPH2DQ, "vcvtph2dq", 0x66, 2, 0x055b, 0, 16, ) \
    BUSTER_X86_AVX512_HALF_WIDENING_DECORATED_FORMS(XV, VCVTPH2UDQ, "vcvtph2udq", 0, 2, 0x0579, 0, 16, ER) \
    BUSTER_X86_AVX512VL_HALF_WIDENING_FORMS(XV, VCVTPH2UDQ, "vcvtph2udq", 0, 2, 0x0579, 0, 16, ) \
    BUSTER_X86_AVX512_HALF_WIDENING_DECORATED_FORMS(XV, VCVTPH2PSX, "vcvtph2psx", 0x66, 2, 0x0613, 0, 16, SAE) \
    BUSTER_X86_AVX512VL_HALF_WIDENING_FORMS(XV, VCVTPH2PSX, "vcvtph2psx", 0x66, 2, 0x0613, 0, 16, ) \
    BUSTER_X86_AVX512_HALF_WIDENING_DECORATED_FORMS(XV, VCVTTPH2DQ, "vcvttph2dq", 0xf3, 2, 0x055b, 0, 16, SAE) \
    BUSTER_X86_AVX512VL_HALF_WIDENING_FORMS(XV, VCVTTPH2DQ, "vcvttph2dq", 0xf3, 2, 0x055b, 0, 16, ) \
    BUSTER_X86_AVX512_HALF_WIDENING_DECORATED_FORMS(XV, VCVTTPH2UDQ, "vcvttph2udq", 0, 2, 0x0578, 0, 16, SAE) \
    BUSTER_X86_AVX512VL_HALF_WIDENING_FORMS(XV, VCVTTPH2UDQ, "vcvttph2udq", 0, 2, 0x0578, 0, 16, ) \
    BUSTER_X86_AVX512_QUARTER_WIDENING_FORMS(XV, VCVTPH2QQ, "vcvtph2qq", 0x66, 2, 0x057b, 16, ER) \
    BUSTER_X86_AVX512_QUARTER_WIDENING_FORMS(XV, VCVTPH2UQQ, "vcvtph2uqq", 0x66, 2, 0x0579, 16, ER) \
    BUSTER_X86_AVX512_QUARTER_WIDENING_FORMS(XV, VCVTPH2PD, "vcvtph2pd", 0, 2, 0x055a, 16, SAE) \
    BUSTER_X86_AVX512_QUARTER_WIDENING_FORMS(XV, VCVTTPH2QQ, "vcvttph2qq", 0x66, 2, 0x057a, 16, SAE) \
    BUSTER_X86_AVX512_QUARTER_WIDENING_FORMS(XV, VCVTTPH2UQQ, "vcvttph2uqq", 0x66, 2, 0x0578, 16, SAE) \
    BUSTER_X86_AVX512_HALF_NARROWING_DECORATED_FORMS(XV, VCVTDQ2PH, "vcvtdq2ph", 0, 2, 0x055b, 0, 32, ER) \
    BUSTER_X86_AVX512VL_HALF_NARROWING_FORMS(XV, VCVTDQ2PH, "vcvtdq2ph", 0, 2, 0x055b, 0, 32, 4, 8, ) \
    BUSTER_X86_AVX512_HALF_NARROWING_DECORATED_FORMS(XV, VCVTUDQ2PH, "vcvtudq2ph", 0xf2, 2, 0x057a, 0, 32, ER) \
    BUSTER_X86_AVX512VL_HALF_NARROWING_FORMS(XV, VCVTUDQ2PH, "vcvtudq2ph", 0xf2, 2, 0x057a, 0, 32, 4, 8, ) \
    BUSTER_X86_AVX512_HALF_NARROWING_DECORATED_FORMS(XV, VCVTPS2PHX, "vcvtps2phx", 0x66, 2, 0x051d, 0, 32, ER) \
    BUSTER_X86_AVX512VL_HALF_NARROWING_FORMS(XV, VCVTPS2PHX, "vcvtps2phx", 0x66, 2, 0x051d, 0, 32, 4, 8, ) \
    BUSTER_X86_AVX512_QUARTER_NARROWING_FORMS(XV, VCVTPD2PH, "vcvtpd2ph", 0x66, 2, 0x055a, ER) \
    BUSTER_X86_AVX512_QUARTER_NARROWING_FORMS(XV, VCVTQQ2PH, "vcvtqq2ph", 0, 2, 0x055b, ER) \
    BUSTER_X86_AVX512_QUARTER_NARROWING_FORMS(XV, VCVTUQQ2PH, "vcvtuqq2ph", 0xf2, 2, 0x057a, ER) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, VCVTSD2SH, "vcvtsd2sh", 0xf2, 2, 0x055a, 1, 64, ER) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, VCVTSS2SH, "vcvtss2sh", 0, 2, 0x051d, 0, 32, ER) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, VCVTSH2SD, "vcvtsh2sd", 0xf3, 2, 0x055a, 0, 16, SAE) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_FORMS(XV, VCVTSH2SS, "vcvtsh2ss", 0, 2, 0x0613, 0, 16, SAE) \
    BUSTER_X86_AVX512_SCALAR_TO_INTEGER_ELEMENT_FORMS(XV, VCVTSH2SI, "vcvtsh2si", 0xf3, 2, 0x052d, 16, ER) \
    BUSTER_X86_AVX512_SCALAR_TO_INTEGER_ELEMENT_FORMS(XV, VCVTSH2USI, "vcvtsh2usi", 0xf3, 2, 0x0579, 16, ER) \
    BUSTER_X86_AVX512_SCALAR_TO_INTEGER_ELEMENT_FORMS(XV, VCVTTSH2SI, "vcvttsh2si", 0xf3, 2, 0x052c, 16, SAE) \
    BUSTER_X86_AVX512_SCALAR_TO_INTEGER_ELEMENT_FORMS(XV, VCVTTSH2USI, "vcvttsh2usi", 0xf3, 2, 0x0578, 16, SAE) \
    BUSTER_X86_AVX512_FROM_INTEGER_FORMS(XV, VCVTSI2SH, "vcvtsi2sh", 0xf3, 2, 0x052a) \
    BUSTER_X86_AVX512_FROM_INTEGER_FORMS(XV, VCVTUSI2SH, "vcvtusi2sh", 0xf3, 2, 0x057b) \
    /* AVX10.2: BF16 arithmetic, without embedded rounding. */ \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VADDBF16, "vaddbf16", 0x66, 2, 0x0558, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VADDBF16, "vaddbf16", 0x66, 2, 0x0558, 0, 16, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VMULBF16, "vmulbf16", 0x66, 2, 0x0559, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VMULBF16, "vmulbf16", 0x66, 2, 0x0559, 0, 16, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VSUBBF16, "vsubbf16", 0x66, 2, 0x055c, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VSUBBF16, "vsubbf16", 0x66, 2, 0x055c, 0, 16, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VDIVBF16, "vdivbf16", 0x66, 2, 0x055e, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VDIVBF16, "vdivbf16", 0x66, 2, 0x055e, 0, 16, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VMINBF16, "vminbf16", 0x66, 2, 0x055d, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VMINBF16, "vminbf16", 0x66, 2, 0x055d, 0, 16, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VMAXBF16, "vmaxbf16", 0x66, 2, 0x055f, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VMAXBF16, "vmaxbf16", 0x66, 2, 0x055f, 0, 16, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VSCALEFBF16, "vscalefbf16", 0, 2, 0x062c, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VSCALEFBF16, "vscalefbf16", 0, 2, 0x062c, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VSQRTBF16, "vsqrtbf16", 0x66, 2, 0x0551, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VSQRTBF16, "vsqrtbf16", 0x66, 2, 0x0551, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VGETEXPBF16, "vgetexpbf16", 0, 2, 0x0642, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VGETEXPBF16, "vgetexpbf16", 0, 2, 0x0642, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VRCPBF16, "vrcpbf16", 0, 2, 0x064c, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VRCPBF16, "vrcpbf16", 0, 2, 0x064c, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VRSQRTBF16, "vrsqrtbf16", 0, 2, 0x064e, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VRSQRTBF16, "vrsqrtbf16", 0, 2, 0x064e, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_IMMEDIATE_FORMS(XV, VGETMANTBF16, "vgetmantbf16", 0xf2, 3, 0x0f3a26, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_IMMEDIATE_FORMS(XV, VGETMANTBF16, "vgetmantbf16", 0xf2, 3, 0x0f3a26, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_IMMEDIATE_FORMS(XV, VREDUCEBF16, "vreducebf16", 0xf2, 3, 0x0f3a56, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_IMMEDIATE_FORMS(XV, VREDUCEBF16, "vreducebf16", 0xf2, 3, 0x0f3a56, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_IMMEDIATE_FORMS(XV, VRNDSCALEBF16, "vrndscalebf16", 0xf2, 3, 0x0f3a08, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_IMMEDIATE_FORMS(XV, VRNDSCALEBF16, "vrndscalebf16", 0xf2, 3, 0x0f3a08, 0, 16, ) \
    BUSTER_X86_AVX512_FMA_BF16_FORMS(XV, VFMADD, "vfmadd", 0x0698) \
    BUSTER_X86_AVX512_FMA_BF16_FORMS(XV, VFMSUB, "vfmsub", 0x069a) \
    BUSTER_X86_AVX512_FMA_BF16_FORMS(XV, VFNMADD, "vfnmadd", 0x069c) \
    BUSTER_X86_AVX512_FMA_BF16_FORMS(XV, VFNMSUB, "vfnmsub", 0x069e) \
    BUSTER_X86_AVX512_COMPARE_PREDICATE_FORMS(XV, VCMPBF16, "vcmpbf16", 0xf2, 3, 0x0f3ac2, 0, 16) \
    BUSTER_X86_AVX512VL_COMPARE_IMMEDIATE_FORMS(XV, VCMPBF16, "vcmpbf16", 0xf2, 3, 0x0f3ac2, 0, 16) \
    XV(VCOMISBF16_X_XM16, "vcomisbf16", EVEX, 0x66, 2, 0x052f, R, 0, 0, 0, V, NONE, XM, 2, 2, NONE, REG_RM) \
    BUSTER_X86_AVX512_CLASS_FORMS(XV, VFPCLASSBF16, "vfpclassbf16", 0xf2, 0, 16, 8, 16, 32) \
    /* Saturating conversions to 8-bit integers in 16- or 32-bit elements (SAT_CVT) and to 32- or 64-bit ones (SAT_CVT_DS). */ \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VCVTBF162IBS, "vcvtbf162ibs", 0xf2, 2, 0x0569, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTBF162IBS, "vcvtbf162ibs", 0xf2, 2, 0x0569, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VCVTBF162IUBS, "vcvtbf162iubs", 0xf2, 2, 0x056b, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTBF162IUBS, "vcvtbf162iubs", 0xf2, 2, 0x056b, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VCVTTBF162IBS, "vcvttbf162ibs", 0xf2, 2, 0x0568, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTTBF162IBS, "vcvttbf162ibs", 0xf2, 2, 0x0568, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_BROADCAST_FORMS(XV, VCVTTBF162IUBS, "vcvttbf162iubs", 0xf2, 2, 0x056a, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTTBF162IUBS, "vcvttbf162iubs", 0xf2, 2, 0x056a, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_ROUNDING_FORMS(XV, VCVTPH2IBS, "vcvtph2ibs", 0, 2, 0x0569, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTPH2IBS, "vcvtph2ibs", 0, 2, 0x0569, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_ROUNDING_FORMS(XV, VCVTPH2IUBS, "vcvtph2iubs", 0, 2, 0x056b, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTPH2IUBS, "vcvtph2iubs", 0, 2, 0x056b, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VCVTTPH2IBS, "vcvttph2ibs", 0, 2, 0x0568, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTTPH2IBS, "vcvttph2ibs", 0, 2, 0x0568, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VCVTTPH2IUBS, "vcvttph2iubs", 0, 2, 0x056a, 0, 16) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTTPH2IUBS, "vcvttph2iubs", 0, 2, 0x056a, 0, 16, ) \
    BUSTER_X86_AVX512_UNARY_ROUNDING_FORMS(XV, VCVTPS2IBS, "vcvtps2ibs", 0x66, 2, 0x0569, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTPS2IBS, "vcvtps2ibs", 0x66, 2, 0x0569, 0, 32, ) \
    BUSTER_X86_AVX512_UNARY_ROUNDING_FORMS(XV, VCVTPS2IUBS, "vcvtps2iubs", 0x66, 2, 0x056b, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTPS2IUBS, "vcvtps2iubs", 0x66, 2, 0x056b, 0, 32, ) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VCVTTPS2IBS, "vcvttps2ibs", 0x66, 2, 0x0568, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTTPS2IBS, "vcvttps2ibs", 0x66, 2, 0x0568, 0, 32, ) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VCVTTPS2IUBS, "vcvttps2iubs", 0x66, 2, 0x056a, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTTPS2IUBS, "vcvttps2iubs", 0x66, 2, 0x056a, 0, 32, ) \
    BUSTER_X86_AVX512_HALF_NARROWING_DECORATED_FORMS(XV, VCVTTPD2DQS, "vcvttpd2dqs", 0, 2, 0x056d, 1, 64, SAE) \
    BUSTER_X86_AVX512VL_HALF_NARROWING_FORMS(XV, VCVTTPD2DQS, "vcvttpd2dqs", 0, 2, 0x056d, 1, 64, 2, 4, ) \
    BUSTER_X86_AVX512_HALF_NARROWING_DECORATED_FORMS(XV, VCVTTPD2UDQS, "vcvttpd2udqs", 0, 2, 0x056c, 1, 64, SAE) \
    BUSTER_X86_AVX512VL_HALF_NARROWING_FORMS(XV, VCVTTPD2UDQS, "vcvttpd2udqs", 0, 2, 0x056c, 1, 64, 2, 4, ) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VCVTTPD2QQS, "vcvttpd2qqs", 0x66, 2, 0x056d, 1, 64) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTTPD2QQS, "vcvttpd2qqs", 0x66, 2, 0x056d, 1, 64, ) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VCVTTPD2UQQS, "vcvttpd2uqqs", 0x66, 2, 0x056c, 1, 64) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTTPD2UQQS, "vcvttpd2uqqs", 0x66, 2, 0x056c, 1, 64, ) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VCVTTPS2DQS, "vcvttps2dqs", 0, 2, 0x056d, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTTPS2DQS, "vcvttps2dqs", 0, 2, 0x056d, 0, 32, ) \
    BUSTER_X86_AVX512_UNARY_SAE_FORMS(XV, VCVTTPS2UDQS, "vcvttps2udqs", 0, 2, 0x056c, 0, 32) \
    BUSTER_X86_AVX512VL_UNARY_BROADCAST_FORMS(XV, VCVTTPS2UDQS, "vcvttps2udqs", 0, 2, 0x056c, 0, 32, ) \
    BUSTER_X86_AVX512_HALF_WIDENING_DECORATED_FORMS(XV, VCVTTPS2QQS, "vcvttps2qqs", 0x66, 2, 0x056d, 0, 32, SAE) \
    BUSTER_X86_AVX512VL_HALF_WIDENING_FORMS(XV, VCVTTPS2QQS, "vcvttps2qqs", 0x66, 2, 0x056d, 0, 32, ) \
    BUSTER_X86_AVX512_HALF_WIDENING_DECORATED_FORMS(XV, VCVTTPS2UQQS, "vcvttps2uqqs", 0x66, 2, 0x056c, 0, 32, SAE) \
    BUSTER_X86_AVX512VL_HALF_WIDENING_FORMS(XV, VCVTTPS2UQQS, "vcvttps2uqqs", 0x66, 2, 0x056c, 0, 32, ) \
    BUSTER_X86_AVX512_SCALAR_TO_INTEGER_ELEMENT_FORMS(XV, VCVTTSD2SIS, "vcvttsd2sis", 0xf2, 2, 0x056d, 64, SAE) \
    BUSTER_X86_AVX512_SCALAR_TO_INTEGER_ELEMENT_FORMS(XV, VCVTTSD2USIS, "vcvttsd2usis", 0xf2, 2, 0x056c, 64, SAE) \
    BUSTER_X86_AVX512_SCALAR_TO_INTEGER_ELEMENT_FORMS(XV, VCVTTSS2SIS, "vcvttss2sis", 0xf3, 2, 0x056d, 32, SAE) \
    BUSTER_X86_AVX512_SCALAR_TO_INTEGER_ELEMENT_FORMS(XV, VCVTTSS2USIS, "vcvttss2usis", 0xf3, 2, 0x056c, 32, SAE) \
    /* Conversions between 16-bit floats and 8-bit ones (FP8: BF8 and HF8, with s saturating). */ \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VCVT2PH2BF8, "vcvt2ph2bf8", 0xf2, 3, 0x0f3874, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VCVT2PH2BF8, "vcvt2ph2bf8", 0xf2, 3, 0x0f3874, 0, 16, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VCVT2PH2BF8S, "vcvt2ph2bf8s", 0xf2, 2, 0x0574, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VCVT2PH2BF8S, "vcvt2ph2bf8s", 0xf2, 2, 0x0574, 0, 16, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VCVT2PH2HF8, "vcvt2ph2hf8", 0xf2, 2, 0x0518, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VCVT2PH2HF8, "vcvt2ph2hf8", 0xf2, 2, 0x0518, 0, 16, ) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VCVT2PH2HF8S, "vcvt2ph2hf8s", 0xf2, 2, 0x051b, 0, 16) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VCVT2PH2HF8S, "vcvt2ph2hf8s", 0xf2, 2, 0x051b, 0, 16, ) \
    BUSTER_X86_AVX512_BIAS_CONVERT_FORMS(XV, VCVTBIASPH2BF8, "vcvtbiasph2bf8", 0, 3, 0x0f3874) \
    BUSTER_X86_AVX512_BIAS_CONVERT_FORMS(XV, VCVTBIASPH2BF8S, "vcvtbiasph2bf8s", 0, 2, 0x0574) \
    BUSTER_X86_AVX512_BIAS_CONVERT_FORMS(XV, VCVTBIASPH2HF8, "vcvtbiasph2hf8", 0, 2, 0x0518) \
    BUSTER_X86_AVX512_BIAS_CONVERT_FORMS(XV, VCVTBIASPH2HF8S, "vcvtbiasph2hf8s", 0, 2, 0x051b) \
    BUSTER_X86_AVX512_HALF_NARROWING_FORMS(XV, VCVTPH2BF8, "vcvtph2bf8", 0xf3, 3, 0x0f3874, 0, 16) \
    BUSTER_X86_AVX512VL_HALF_NARROWING_FORMS(XV, VCVTPH2BF8, "vcvtph2bf8", 0xf3, 3, 0x0f3874, 0, 16, 8, 16, ) \
    BUSTER_X86_AVX512_HALF_NARROWING_FORMS(XV, VCVTPH2BF8S, "vcvtph2bf8s", 0xf3, 2, 0x0574, 0, 16) \
    BUSTER_X86_AVX512VL_HALF_NARROWING_FORMS(XV, VCVTPH2BF8S, "vcvtph2bf8s", 0xf3, 2, 0x0574, 0, 16, 8, 16, ) \
    BUSTER_X86_AVX512_HALF_NARROWING_FORMS(XV, VCVTPH2HF8, "vcvtph2hf8", 0xf3, 2, 0x0518, 0, 16) \
    BUSTER_X86_AVX512VL_HALF_NARROWING_FORMS(XV, VCVTPH2HF8, "vcvtph2hf8", 0xf3, 2, 0x0518, 0, 16, 8, 16, ) \
    BUSTER_X86_AVX512_HALF_NARROWING_FORMS(XV, VCVTPH2HF8S, "vcvtph2hf8s", 0xf3, 2, 0x051b, 0, 16) \
    BUSTER_X86_AVX512VL_HALF_NARROWING_FORMS(XV, VCVTPH2HF8S, "vcvtph2hf8s", 0xf3, 2, 0x051b, 0, 16, 8, 16, ) \
    XV(VCVTHF82PH_X_XM64, "vcvthf82ph", EVEX, 0xf2, 2, 0x051e, R, 0, 0, 0, V, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(VCVTHF82PH_Y_XM128, "vcvthf82ph", EVEX, 0xf2, 2, 0x051e, R, 0, 0, 1, Y, NONE, XM, 16, 16, KZ, REG_RM) \
    XV(VCVTHF82PH_Z_YM256, "vcvthf82ph", EVEX, 0xf2, 2, 0x051e, R, 0, 0, 2, Z, NONE, YM, 32, 32, KZ, REG_RM) \
    /* Dot products of 8- and 16-bit integers (whose VEX forms are AVX-VNNI-INT8's and INT16's) and of FP16 pairs. */ \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPDPBSSD, "vpdpbssd", 0xf2, 3, 0x0f3850, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPDPBSSD, "vpdpbssd", 0xf2, 3, 0x0f3850, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPDPBSSDS, "vpdpbssds", 0xf2, 3, 0x0f3851, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPDPBSSDS, "vpdpbssds", 0xf2, 3, 0x0f3851, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPDPBSUD, "vpdpbsud", 0xf3, 3, 0x0f3850, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPDPBSUD, "vpdpbsud", 0xf3, 3, 0x0f3850, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPDPBSUDS, "vpdpbsuds", 0xf3, 3, 0x0f3851, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPDPBSUDS, "vpdpbsuds", 0xf3, 3, 0x0f3851, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPDPBUUD, "vpdpbuud", 0, 3, 0x0f3850, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPDPBUUD, "vpdpbuud", 0, 3, 0x0f3850, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPDPBUUDS, "vpdpbuuds", 0, 3, 0x0f3851, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPDPBUUDS, "vpdpbuuds", 0, 3, 0x0f3851, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPDPWSUD, "vpdpwsud", 0xf3, 3, 0x0f38d2, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPDPWSUD, "vpdpwsud", 0xf3, 3, 0x0f38d2, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPDPWSUDS, "vpdpwsuds", 0xf3, 3, 0x0f38d3, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPDPWSUDS, "vpdpwsuds", 0xf3, 3, 0x0f38d3, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPDPWUSD, "vpdpwusd", 0x66, 3, 0x0f38d2, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPDPWUSD, "vpdpwusd", 0x66, 3, 0x0f38d2, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPDPWUSDS, "vpdpwusds", 0x66, 3, 0x0f38d3, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPDPWUSDS, "vpdpwusds", 0x66, 3, 0x0f38d3, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPDPWUUD, "vpdpwuud", 0, 3, 0x0f38d2, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPDPWUUD, "vpdpwuud", 0, 3, 0x0f38d2, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VPDPWUUDS, "vpdpwuuds", 0, 3, 0x0f38d3, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VPDPWUUDS, "vpdpwuuds", 0, 3, 0x0f38d3, 0, 32, _EVEX) \
    BUSTER_X86_AVX512_BROADCAST_FORMS(XV, VDPPHPS, "vdpphps", 0, 3, 0x0f3852, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VDPPHPS, "vdpphps", 0, 3, 0x0f3852, 0, 32, ) \
    BUSTER_X86_AVX512_ROUNDING_FORMS(XV, VCVT2PS2PHX, "vcvt2ps2phx", 0x66, 3, 0x0f3867, 0, 32) \
    BUSTER_X86_AVX512VL_BROADCAST_FORMS(XV, VCVT2PS2PHX, "vcvt2ps2phx", 0x66, 3, 0x0f3867, 0, 32, ) \
    /* Minimums and maximums by the immediate's rules, and the other AVX10.2 additions. */ \
    BUSTER_X86_AVX512_IMMEDIATE_SAE_FORMS(XV, VMINMAXPD, "vminmaxpd", 0x66, 3, 0x0f3a52, 1, 64) \
    BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, VMINMAXPD, "vminmaxpd", 0x66, 3, 0x0f3a52, 1, 64, ) \
    BUSTER_X86_AVX512_IMMEDIATE_SAE_FORMS(XV, VMINMAXPS, "vminmaxps", 0x66, 3, 0x0f3a52, 0, 32) \
    BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, VMINMAXPS, "vminmaxps", 0x66, 3, 0x0f3a52, 0, 32, ) \
    BUSTER_X86_AVX512_IMMEDIATE_SAE_FORMS(XV, VMINMAXPH, "vminmaxph", 0, 3, 0x0f3a52, 0, 16) \
    BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, VMINMAXPH, "vminmaxph", 0, 3, 0x0f3a52, 0, 16, ) \
    BUSTER_X86_AVX512_IMMEDIATE_FORMS(XV, VMINMAXBF16, "vminmaxbf16", 0xf2, 3, 0x0f3a52, 0, 16) \
    BUSTER_X86_AVX512VL_IMMEDIATE_FORMS(XV, VMINMAXBF16, "vminmaxbf16", 0xf2, 3, 0x0f3a52, 0, 16, ) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_IMMEDIATE_FORMS(XV, VMINMAXSD, "vminmaxsd", 0x66, 3, 0x0f3a53, 1, 64, SAE) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_IMMEDIATE_FORMS(XV, VMINMAXSS, "vminmaxss", 0x66, 3, 0x0f3a53, 0, 32, SAE) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_IMMEDIATE_FORMS(XV, VMINMAXSH, "vminmaxsh", 0, 3, 0x0f3a53, 0, 16, SAE) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_COMPARE_FORMS(XV, VCOMXSD, "vcomxsd", 0xf2, 2, 0x0f2f, 1, 64) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_COMPARE_FORMS(XV, VCOMXSS, "vcomxss", 0xf3, 2, 0x0f2f, 0, 32) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_COMPARE_FORMS(XV, VCOMXSH, "vcomxsh", 0xf3, 2, 0x052f, 0, 16) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_COMPARE_FORMS(XV, VUCOMXSD, "vucomxsd", 0xf2, 2, 0x0f2e, 1, 64) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_COMPARE_FORMS(XV, VUCOMXSS, "vucomxss", 0xf3, 2, 0x0f2e, 0, 32) \
    BUSTER_X86_AVX512_SCALAR_ELEMENT_COMPARE_FORMS(XV, VUCOMXSH, "vucomxsh", 0xf3, 2, 0x052e, 0, 16) \
    BUSTER_X86_AVX512_LOAD_FORMS(XV, VMOVRSB, "vmovrsb", 0xf2, 2, 0x056f, 0) \
    BUSTER_X86_AVX512_LOAD_FORMS(XV, VMOVRSW, "vmovrsw", 0xf2, 2, 0x056f, 1) \
    BUSTER_X86_AVX512_LOAD_FORMS(XV, VMOVRSD, "vmovrsd", 0xf3, 2, 0x056f, 0) \
    BUSTER_X86_AVX512_LOAD_FORMS(XV, VMOVRSQ, "vmovrsq", 0xf3, 2, 0x056f, 1) \
    BUSTER_X86_AVX512BW_IMMEDIATE_FORMS(XV, VMPSADBW, "vmpsadbw", 0xf3, 3, 0x0f3a42, 0, _EVEX) \
    BUSTER_X86_AVX512_UNMASKED_FORMS(XV, VSM4KEY4, "vsm4key4", 0xf3, 0x0f38da) \
    BUSTER_X86_AVX512_UNMASKED_FORMS(XV, VSM4RNDS4, "vsm4rnds4", 0xf2, 0x0f38da) \
    XV(VMOVD_X_X, "vmovd", EVEX, 0xf3, 2, 0x0f7e, R, 0, 0, 0, V, NONE, V, 4, 4, NONE, REG_RM) \
    XV(VMOVW_X_X, "vmovw", EVEX, 0xf3, 2, 0x056e, R, 0, 0, 0, V, NONE, V, 2, 2, NONE, REG_RM) \
    BUSTER_X86_CMPCCXADD_CONDITION_FORMS(XV, VEX, ) \
    /* APX's EVEX forms of general register instructions, whose general registers can be 16 to 31 with x86_encode_apx: VEX's (BMI's, kmov's, CMPccXADD's, AMX's loads and stores, and the MSR instructions with an immediate) in their maps, and those of legacy maps 2 and 3 and the bit counts in map 4. An 8-bit displacement isn't scaled (N is 1). The NF forms leave the flags as they are, and ADX's ND forms write EVEX.vvvv. */ \
    BUSTER_X86_BMI_FLAG_FORMS(XV, EVEX, NONE, _EVEX) \
    BUSTER_X86_BMI_FLAG_FORMS(XV, EVEX, NF, _NF) \
    BUSTER_X86_BMI2_FORMS(XV, EVEX, _EVEX) \
    BUSTER_X86_KMOV_FORMS(XV, EVEX, _EVEX) \
    BUSTER_X86_CMPCCXADD_CONDITION_FORMS(XV, EVEX, _EVEX) \
    BUSTER_X86_AMX_MEMORY_FORMS(XV, EVEX, _EVEX) \
    BUSTER_X86_MSR_IMMEDIATE_FORMS(XV, EVEX, _EVEX) \
    XV(URDMSR_R64_R64_EVEX, "urdmsr", EVEX, 0xf2, 2, 0x04f8, R, 0, 0, 0, GQ, NONE, GQ, 8, 1, NONE, RM_REG) \
    XV(UWRMSR_R64_R64_EVEX, "uwrmsr", EVEX, 0xf3, 2, 0x04f8, R, 0, 0, 0, GQ, NONE, GQ, 8, 1, NONE, REG_RM) \
    BUSTER_X86_EVEX_R_RM_FORMS(XV, TZCNT, "tzcnt", 0x04f4, NONE, _EVEX) \
    BUSTER_X86_EVEX_R_RM_FORMS(XV, TZCNT, "tzcnt", 0x04f4, NF, _NF) \
    BUSTER_X86_EVEX_R_RM_FORMS(XV, LZCNT, "lzcnt", 0x04f5, NONE, _EVEX) \
    BUSTER_X86_EVEX_R_RM_FORMS(XV, LZCNT, "lzcnt", 0x04f5, NF, _NF) \
    BUSTER_X86_EVEX_R_RM_FORMS(XV, POPCNT, "popcnt", 0x0488, NONE, _EVEX) \
    BUSTER_X86_EVEX_R_RM_FORMS(XV, POPCNT, "popcnt", 0x0488, NF, _NF) \
    XV(CRC32_R32_RM8_EVEX, "crc32", EVEX, 0, 2, 0x04f0, R, 0, 0, 0, GD, NONE, EB, 1, 1, NONE, REG_RM) \
    XV(CRC32_R32_RM16_EVEX, "crc32", EVEX, 0x66, 2, 0x04f1, R, 0, 0, 0, GD, NONE, EW, 2, 1, NONE, REG_RM) \
    XV(CRC32_R32_RM32_EVEX, "crc32", EVEX, 0, 2, 0x04f1, R, 0, 0, 0, GD, NONE, ED, 4, 1, NONE, REG_RM) \
    XV(CRC32_R64_RM8_EVEX, "crc32", EVEX, 0, 2, 0x04f0, R, 0, 1, 0, GQ, NONE, EB, 1, 1, NONE, REG_RM) \
    XV(CRC32_R64_RM64_EVEX, "crc32", EVEX, 0, 2, 0x04f1, R, 0, 1, 0, GQ, NONE, EQ, 8, 1, NONE, REG_RM) \
    BUSTER_X86_EVEX_ADX_FORMS(XV, ADCX, "adcx", 0x66) \
    BUSTER_X86_EVEX_ADX_FORMS(XV, ADOX, "adox", 0xf3) \
    /* movbe's EVEX forms can byte-swap a register too. */ \
    XV(MOVBE_R16_RM16, "movbe", EVEX, 0x66, 2, 0x0460, R, 0, 0, 0, GW, NONE, EW, 2, 1, NONE, REG_RM) \
    XV(MOVBE_R32_RM32, "movbe", EVEX, 0, 2, 0x0460, R, 0, 0, 0, GD, NONE, ED, 4, 1, NONE, REG_RM) \
    XV(MOVBE_R64_RM64, "movbe", EVEX, 0, 2, 0x0460, R, 0, 1, 0, GQ, NONE, EQ, 8, 1, NONE, REG_RM) \
    XV(MOVBE_RM16_R16, "movbe", EVEX, 0x66, 2, 0x0461, R, 0, 0, 0, GW, NONE, EW, 2, 1, NONE, RM_REG) \
    XV(MOVBE_RM32_R32, "movbe", EVEX, 0, 2, 0x0461, R, 0, 0, 0, GD, NONE, ED, 4, 1, NONE, RM_REG) \
    XV(MOVBE_RM64_R64, "movbe", EVEX, 0, 2, 0x0461, R, 0, 1, 0, GQ, NONE, EQ, 8, 1, NONE, RM_REG) \
    XV(MOVRS_R8_M8_EVEX, "movrs", EVEX, 0, 2, 0x048a, R, 0, 0, 0, GB, NONE, M, 1, 1, NONE, REG_RM) \
    XV(MOVRS_R16_M16_EVEX, "movrs", EVEX, 0x66, 2, 0x048b, R, 0, 0, 0, GW, NONE, M, 2, 1, NONE, REG_RM) \
    XV(MOVRS_R32_M32_EVEX, "movrs", EVEX, 0, 2, 0x048b, R, 0, 0, 0, GD, NONE, M, 4, 1, NONE, REG_RM) \
    XV(MOVRS_R64_M64_EVEX, "movrs", EVEX, 0, 2, 0x048b, R, 0, 1, 0, GQ, NONE, M, 8, 1, NONE, REG_RM) \
    BUSTER_X86_EVEX_M_R_FORMS(XV, AADD, "aadd", 0, 0x04fc) \
    BUSTER_X86_EVEX_M_R_FORMS(XV, AAND, "aand", 0x66, 0x04fc) \
    BUSTER_X86_EVEX_M_R_FORMS(XV, AOR, "aor", 0xf2, 0x04fc) \
    BUSTER_X86_EVEX_M_R_FORMS(XV, AXOR, "axor", 0xf3, 0x04fc) \
    BUSTER_X86_EVEX_M_R_FORMS(XV, MOVDIRI, "movdiri", 0, 0x04f9) \
    XV(WRSSD_M32_R32_EVEX, "wrssd", EVEX, 0, 2, 0x0466, R, 0, 0, 0, GD, NONE, M, 4, 1, NONE, RM_REG) \
    XV(WRSSQ_M64_R64_EVEX, "wrssq", EVEX, 0, 2, 0x0466, R, 0, 1, 0, GQ, NONE, M, 8, 1, NONE, RM_REG) \
    XV(WRUSSD_M32_R32_EVEX, "wrussd", EVEX, 0x66, 2, 0x0465, R, 0, 0, 0, GD, NONE, M, 4, 1, NONE, RM_REG) \
    XV(WRUSSQ_M64_R64_EVEX, "wrussq", EVEX, 0x66, 2, 0x0465, R, 0, 1, 0, GQ, NONE, M, 8, 1, NONE, RM_REG) \
    XV(INVEPT_R64_M128_EVEX, "invept", EVEX, 0xf3, 2, 0x04f0, R, 0, 0, 0, GQ, NONE, M, 16, 1, NONE, REG_RM) \
    XV(INVVPID_R64_M128_EVEX, "invvpid", EVEX, 0xf3, 2, 0x04f1, R, 0, 0, 0, GQ, NONE, M, 16, 1, NONE, REG_RM) \
    XV(INVPCID_R64_M128_EVEX, "invpcid", EVEX, 0xf3, 2, 0x04f2, R, 0, 0, 0, GQ, NONE, M, 16, 1, NONE, REG_RM) \
    XV(MOVDIR64B_R64_M512_EVEX, "movdir64b", EVEX, 0x66, 2, 0x04f8, R, 0, 0, 0, GQ, NONE, M, 0, 1, NONE, REG_RM) \
    XV(ENQCMD_R64_M512_EVEX, "enqcmd", EVEX, 0xf2, 2, 0x04f8, R, 0, 0, 0, GQ, NONE, M, 0, 1, NONE, REG_RM) \
    XV(ENQCMDS_R64_M512_EVEX, "enqcmds", EVEX, 0xf3, 2, 0x04f8, R, 0, 0, 0, GQ, NONE, M, 0, 1, NONE, REG_RM) \
    /* APX's forms of the legacy arithmetic in map 4: the two-operand forms (but the accumulator's: an EVEX form's first operand can be R16 to R31 anyway), and the ND forms, which write a new destination in EVEX.vvvv; both with NF forms for the operations that set the flags without reading them (not adc, sbb, rcl and rcr) and change something else (not not); and the one-operand multiplications and divisions (with NF), imul's other forms (with ND, NF and ZU: imulzu), shld and shrd, and for each condition, cmovcc with ND, setcc and setzucc, and the new cfcmovcc. */ \
    BUSTER_X86_EVEX_ARITHMETIC_NF_FORMS(XV, ADD, "add", 0) \
    BUSTER_X86_EVEX_ARITHMETIC_NF_FORMS(XV, OR, "or", 1) \
    BUSTER_X86_EVEX_ARITHMETIC_FORMS(XV, ADC, "adc", 2, NONE, _EVEX) \
    BUSTER_X86_EVEX_ND_ARITHMETIC_FORMS(XV, ADC, "adc", 2, ND, ) \
    BUSTER_X86_EVEX_ARITHMETIC_FORMS(XV, SBB, "sbb", 3, NONE, _EVEX) \
    BUSTER_X86_EVEX_ND_ARITHMETIC_FORMS(XV, SBB, "sbb", 3, ND, ) \
    BUSTER_X86_EVEX_ARITHMETIC_NF_FORMS(XV, AND, "and", 4) \
    BUSTER_X86_EVEX_ARITHMETIC_NF_FORMS(XV, SUB, "sub", 5) \
    BUSTER_X86_EVEX_ARITHMETIC_NF_FORMS(XV, XOR, "xor", 6) \
    BUSTER_X86_EVEX_SHIFT_NF_FORMS(XV, ROL, "rol", 0) \
    BUSTER_X86_EVEX_SHIFT_NF_FORMS(XV, ROR, "ror", 1) \
    BUSTER_X86_EVEX_SHIFT_NF_FORMS(XV, SHL, "shl", 4) \
    BUSTER_X86_EVEX_SHIFT_NF_FORMS(XV, SHR, "shr", 5) \
    BUSTER_X86_EVEX_SHIFT_NF_FORMS(XV, SAR, "sar", 7) \
    BUSTER_X86_EVEX_SHIFT_FORMS(XV, RCL, "rcl", 2, NONE, _EVEX) \
    BUSTER_X86_EVEX_ND_SHIFT_FORMS(XV, RCL, "rcl", 2, ND, ) \
    BUSTER_X86_EVEX_SHIFT_FORMS(XV, RCR, "rcr", 3, NONE, _EVEX) \
    BUSTER_X86_EVEX_ND_SHIFT_FORMS(XV, RCR, "rcr", 3, ND, ) \
    BUSTER_X86_EVEX_UNARY_FORMS(XV, NOT, "not", 0x04f6, 0x04f7, 2, NONE, _EVEX) \
    BUSTER_X86_EVEX_ND_UNARY_FORMS(XV, NOT, "not", 0x04f6, 0x04f7, 2, ND, ) \
    BUSTER_X86_EVEX_UNARY_FORMS(XV, NEG, "neg", 0x04f6, 0x04f7, 3, NONE, _EVEX) \
    BUSTER_X86_EVEX_UNARY_FORMS(XV, NEG, "neg", 0x04f6, 0x04f7, 3, NF, _NF) \
    BUSTER_X86_EVEX_ND_UNARY_FORMS(XV, NEG, "neg", 0x04f6, 0x04f7, 3, ND, ) \
    BUSTER_X86_EVEX_ND_UNARY_FORMS(XV, NEG, "neg", 0x04f6, 0x04f7, 3, ND_NF, _NF) \
    BUSTER_X86_EVEX_UNARY_FORMS(XV, INC, "inc", 0x04fe, 0x04ff, 0, NONE, _EVEX) \
    BUSTER_X86_EVEX_UNARY_FORMS(XV, INC, "inc", 0x04fe, 0x04ff, 0, NF, _NF) \
    BUSTER_X86_EVEX_ND_UNARY_FORMS(XV, INC, "inc", 0x04fe, 0x04ff, 0, ND, ) \
    BUSTER_X86_EVEX_ND_UNARY_FORMS(XV, INC, "inc", 0x04fe, 0x04ff, 0, ND_NF, _NF) \
    BUSTER_X86_EVEX_UNARY_FORMS(XV, DEC, "dec", 0x04fe, 0x04ff, 1, NONE, _EVEX) \
    BUSTER_X86_EVEX_UNARY_FORMS(XV, DEC, "dec", 0x04fe, 0x04ff, 1, NF, _NF) \
    BUSTER_X86_EVEX_ND_UNARY_FORMS(XV, DEC, "dec", 0x04fe, 0x04ff, 1, ND, ) \
    BUSTER_X86_EVEX_ND_UNARY_FORMS(XV, DEC, "dec", 0x04fe, 0x04ff, 1, ND_NF, _NF) \
    BUSTER_X86_EVEX_UNARY_FORMS(XV, MUL, "mul", 0x04f6, 0x04f7, 4, NONE, _EVEX) \
    BUSTER_X86_EVEX_UNARY_FORMS(XV, MUL, "mul", 0x04f6, 0x04f7, 4, NF, _NF) \
    BUSTER_X86_EVEX_UNARY_FORMS(XV, IMUL, "imul", 0x04f6, 0x04f7, 5, NONE, _EVEX) \
    BUSTER_X86_EVEX_UNARY_FORMS(XV, IMUL, "imul", 0x04f6, 0x04f7, 5, NF, _NF) \
    BUSTER_X86_EVEX_UNARY_FORMS(XV, DIV, "div", 0x04f6, 0x04f7, 6, NONE, _EVEX) \
    BUSTER_X86_EVEX_UNARY_FORMS(XV, DIV, "div", 0x04f6, 0x04f7, 6, NF, _NF) \
    BUSTER_X86_EVEX_UNARY_FORMS(XV, IDIV, "idiv", 0x04f6, 0x04f7, 7, NONE, _EVEX) \
    BUSTER_X86_EVEX_UNARY_FORMS(XV, IDIV, "idiv", 0x04f6, 0x04f7, 7, NF, _NF) \
    BUSTER_X86_EVEX_IMUL_FORMS(XV, NONE, _EVEX) \
    BUSTER_X86_EVEX_IMUL_FORMS(XV, NF, _NF) \
    BUSTER_X86_EVEX_ND_IMUL_FORMS(XV, ND, ) \
    BUSTER_X86_EVEX_ND_IMUL_FORMS(XV, ND_NF, _NF) \
    BUSTER_X86_EVEX_IMULZU_FORMS(XV, ND, ) \
    BUSTER_X86_EVEX_IMULZU_FORMS(XV, ND_NF, _NF) \
    BUSTER_X86_EVEX_DOUBLE_SHIFT_NF_FORMS(XV, SHLD, "shld", 0x0424) \
    BUSTER_X86_EVEX_DOUBLE_SHIFT_NF_FORMS(XV, SHRD, "shrd", 0x042c) \
    BUSTER_X86_EVEX_CONDITION_FORMS(XV, O, "o", 0) \
    BUSTER_X86_EVEX_CONDITION_FORMS(XV, NO, "no", 1) \
    BUSTER_X86_EVEX_CONDITION_FORMS(XV, B, "b", 2) \
    BUSTER_X86_EVEX_CONDITION_FORMS(XV, AE, "ae", 3) \
    BUSTER_X86_EVEX_CONDITION_FORMS(XV, E, "e", 4) \
    BUSTER_X86_EVEX_CONDITION_FORMS(XV, NE, "ne", 5) \
    BUSTER_X86_EVEX_CONDITION_FORMS(XV, BE, "be", 6) \
    BUSTER_X86_EVEX_CONDITION_FORMS(XV, A, "a", 7) \
    BUSTER_X86_EVEX_CONDITION_FORMS(XV, S, "s", 8) \
    BUSTER_X86_EVEX_CONDITION_FORMS(XV, NS, "ns", 9) \
    BUSTER_X86_EVEX_CONDITION_FORMS(XV, P, "p", 10) \
    BUSTER_X86_EVEX_CONDITION_FORMS(XV, NP, "np", 11) \
    BUSTER_X86_EVEX_CONDITION_FORMS(XV, L, "l", 12) \
    BUSTER_X86_EVEX_CONDITION_FORMS(XV, GE, "ge", 13) \
    BUSTER_X86_EVEX_CONDITION_FORMS(XV, LE, "le", 14) \
    BUSTER_X86_EVEX_CONDITION_FORMS(XV, G, "g", 15) \
    /* APX's other new instructions: ccmpcc and ctestcc, and push2 and pop2 (two registers, the first in EVEX.vvvv: ND's bit), with a hint that the pop2 pops what the push2 pushed (W1: push2p and pop2p). */ \
    BUSTER_X86_EVEX_CCMP_FORMS(XV, O, "o", 0) \
    BUSTER_X86_EVEX_CCMP_FORMS(XV, NO, "no", 1) \
    BUSTER_X86_EVEX_CCMP_FORMS(XV, B, "b", 2) \
    BUSTER_X86_EVEX_CCMP_FORMS(XV, AE, "ae", 3) \
    BUSTER_X86_EVEX_CCMP_FORMS(XV, E, "e", 4) \
    BUSTER_X86_EVEX_CCMP_FORMS(XV, NE, "ne", 5) \
    BUSTER_X86_EVEX_CCMP_FORMS(XV, BE, "be", 6) \
    BUSTER_X86_EVEX_CCMP_FORMS(XV, A, "a", 7) \
    BUSTER_X86_EVEX_CCMP_FORMS(XV, S, "s", 8) \
    BUSTER_X86_EVEX_CCMP_FORMS(XV, NS, "ns", 9) \
    BUSTER_X86_EVEX_CCMP_FORMS(XV, T, "t", 10) \
    BUSTER_X86_EVEX_CCMP_FORMS(XV, F, "f", 11) \
    BUSTER_X86_EVEX_CCMP_FORMS(XV, L, "l", 12) \
    BUSTER_X86_EVEX_CCMP_FORMS(XV, GE, "ge", 13) \
    BUSTER_X86_EVEX_CCMP_FORMS(XV, LE, "le", 14) \
    BUSTER_X86_EVEX_CCMP_FORMS(XV, G, "g", 15) \
    XV(PUSH2_R64_R64, "push2", EVEX, 0, 2, 0x04ff, D6, 0, 0, 0, NONE, GQ, GQ, 8, 1, ND, VVVV_RM) \
    XV(PUSH2P_R64_R64, "push2p", EVEX, 0, 2, 0x04ff, D6, 0, 1, 0, NONE, GQ, GQ, 8, 1, ND, VVVV_RM) \
    XV(POP2_R64_R64, "pop2", EVEX, 0, 2, 0x048f, D0, 0, 0, 0, NONE, GQ, GQ, 8, 1, ND, VVVV_RM) \
    XV(POP2P_R64_R64, "pop2p", EVEX, 0, 2, 0x048f, D0, 0, 1, 0, NONE, GQ, GQ, 8, 1, ND, VVVV_RM)

// A push of a segment register (and its pop, for all but CS) at each operand size: w (66) and d.
#define BUSTER_X86_SEGMENT_PUSH_FORMS(X, S, s, push) \
    X(PUSHW_##S, "pushw " s, 0x66, 1, push, NO, 0, 0, 0, NONE, NONE, 2, NONE) \
    X(PUSHD_##S, "pushd " s, 0, 1, push, NO, 0, 0, 0, NONE, NONE, 4, NONE)
#define BUSTER_X86_SEGMENT_POP_FORMS(X, S, s, pop) \
    X(POPW_##S, "popw " s, 0x66, 1, pop, NO, 0, 0, 0, NONE, NONE, 2, NONE) \
    X(POPD_##S, "popd " s, 0, 1, pop, NO, 0, 0, 0, NONE, NONE, 4, NONE)

// MOV between the accumulator (AL, AX or EAX) and an absolute address of the immediate's size (moffs16 or moffs32),
// both ways: a 16-bit address's forms (`bytes` 2), with a 67 in 32-bit mode, and a 32-bit one's (4), with a 67 in
// 16-bit mode.
#define BUSTER_X86_MOFFS_FORMS(X, bits, bytes) \
    X(MOV_AL_MOFFS##bits, "mov", 0, 1, 0xa0, NO, bytes, 0, 0, NONE, NONE, 1, ACC_MOFFS) \
    X(MOV_AX_MOFFS##bits, "mov", 0x66, 1, 0xa1, NO, bytes, 0, 0, NONE, NONE, 2, ACC_MOFFS) \
    X(MOV_EAX_MOFFS##bits, "mov", 0, 1, 0xa1, NO, bytes, 0, 0, NONE, NONE, 4, ACC_MOFFS) \
    X(MOV_MOFFS##bits##_AL, "mov", 0, 1, 0xa2, NO, bytes, 0, 0, NONE, NONE, 1, MOFFS_ACC) \
    X(MOV_MOFFS##bits##_AX, "mov", 0x66, 1, 0xa3, NO, bytes, 0, 0, NONE, NONE, 2, MOFFS_ACC) \
    X(MOV_MOFFS##bits##_EAX, "mov", 0, 1, 0xa3, NO, bytes, 0, 0, NONE, NONE, 4, MOFFS_ACC)

// The forms of 16- and 32-bit modes alone, at the end of the form table (BUSTER_X86_FORM_LEGACY_MODES): the 32-bit
// forms of what 64-bit mode does with 64-bit operands (push and pop, near branches through a register or memory, with
// NOTRACK and BND too, flags, and pushd's imm8 and imm32, the moves to and from control and debug registers, which are
// 32-bit there, VMX's vmread, vmwrite, invept, invvpid and invpcid, MPX's bound checks, rdpid, and the instructions
// with an address in a general register, whose size is the address's: umonitor, movdir64b, enqcmd and enqcmds), the
// near branches through a 16-bit register or memory, explicit 16- and 32-bit forms of ret, leave and enter, pushes and
// pops of ES, CS, SS and DS and 32-bit ones of FS and GS, the moves between the accumulator and moffs16 or moffs32, and
// the instructions 64-bit mode doesn't have: the BCD adjustments (aaa, aas, daa, das, aam and aad), salc, into, bound,
// arpl, lds and les, pusha and popa, far branches to a pointer in the immediate (ptr16:16 and ptr16:32), inc and dec of
// a register in the opcode (40+r and 48+r, REX in 64-bit mode), the moves to and from the 386's and 486's test
// registers (TR0 to TR7, #UD since the Pentium), the descriptor table registers' 6-byte operands at each operand size
// (16-bit: a 24-bit base), and VIA's montmul, which XED takes with 16- and 32-bit addresses alone.
#define BUSTER_X86_FORMS_LEGACY_MODES(X) \
    X(PUSH_R32, "push", 0, 1, 0x50, PR, 0, 0, 0, NONE, GD, 4, RM) \
    X(POP_R32, "pop", 0, 1, 0x58, PR, 0, 0, 0, NONE, GD, 4, RM) \
    X(PUSH_RM32, "push", 0, 1, 0xff, D6, 0, 0, 0, NONE, ED, 4, RM) \
    X(POP_RM32, "pop", 0, 1, 0x8f, D0, 0, 0, 0, NONE, ED, 4, RM) \
    X(CALL_RM32, "call", 0, 1, 0xff, D2, 0, 0, 0, NONE, ED, 4, RM) \
    X(JMP_RM32, "jmp", 0, 1, 0xff, D4, 0, 0, 0, NONE, ED, 4, RM) \
    X(PUSHD_IMM8, "pushd", 0, 1, 0x6a, NO, 1, 0, 0, NONE, NONE, 4, IMM) \
    X(PUSHD_IMM32, "pushd", 0, 1, 0x68, NO, 4, 0, 0, NONE, NONE, 4, IMM) \
    X(PUSHFD, "pushfd", 0, 1, 0x9c, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(POPFD, "popfd", 0, 1, 0x9d, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(MOV_CR_R32, "mov", 0, 2, 0x0f22, R, 0, 0, 0, CR, GD, 4, REG_RM) \
    X(MOV_R32_CR, "mov", 0, 2, 0x0f20, R, 0, 0, 0, CR, GD, 4, RM_REG) \
    X(MOV_DR_R32, "mov", 0, 2, 0x0f23, R, 0, 0, 0, DR, GD, 4, REG_RM) \
    X(MOV_R32_DR, "mov", 0, 2, 0x0f21, R, 0, 0, 0, DR, GD, 4, RM_REG) \
    X(NOTRACK_CALL_RM32, "notrack call", 0x3e, 1, 0xff, D2, 0, 0, 0, NONE, ED, 4, RM) \
    X(NOTRACK_JMP_RM32, "notrack jmp", 0x3e, 1, 0xff, D4, 0, 0, 0, NONE, ED, 4, RM) \
    X(BND_CALL_RM32, "bnd call", 0xf2, 1, 0xff, D2, 0, 0, 0, NONE, ED, 4, RM) \
    X(BND_JMP_RM32, "bnd jmp", 0xf2, 1, 0xff, D4, 0, 0, 0, NONE, ED, 4, RM) \
    X(VMREAD_RM32_R32, "vmread", 0, 2, 0x0f78, R, 0, 0, 0, GD, ED, 4, RM_REG) \
    X(VMWRITE_R32_RM32, "vmwrite", 0, 2, 0x0f79, R, 0, 0, 0, GD, ED, 4, REG_RM) \
    X(INVEPT_R32_M128, "invept", 0x66, 3, 0x0f3880, R, 0, 0, 0, GD, M, 16, REG_RM) \
    X(INVVPID_R32_M128, "invvpid", 0x66, 3, 0x0f3881, R, 0, 0, 0, GD, M, 16, REG_RM) \
    X(INVPCID_R32_M128, "invpcid", 0x66, 3, 0x0f3882, R, 0, 0, 0, GD, M, 16, REG_RM) \
    X(BNDCL_BND_RM32, "bndcl", 0xf3, 2, 0x0f1a, R, 0, 0, 0, BND, ED, 4, REG_RM) \
    X(BNDCU_BND_RM32, "bndcu", 0xf2, 2, 0x0f1a, R, 0, 0, 0, BND, ED, 4, REG_RM) \
    X(BNDCN_BND_RM32, "bndcn", 0xf2, 2, 0x0f1b, R, 0, 0, 0, BND, ED, 4, REG_RM) \
    X(RDPID_R32, "rdpid", 0xf3, 2, 0x0fc7, D7, 0, 0, 0, NONE, GD, 4, RM) \
    X(UMONITOR_R32, "umonitor", 0xf3, 2, 0x0fae, D6, 0, 0, 0, NONE, GD, 4, RM) \
    X(MOVDIR64B_R32_M512, "movdir64b", 0x66, 3, 0x0f38f8, R, 0, 0, 0, GD, M, 0, REG_RM) \
    X(ENQCMD_R32_M512, "enqcmd", 0xf2, 3, 0x0f38f8, R, 0, 0, 0, GD, M, 0, REG_RM) \
    X(ENQCMDS_R32_M512, "enqcmds", 0xf3, 3, 0x0f38f8, R, 0, 0, 0, GD, M, 0, REG_RM) \
    X(CALL_RM16, "call", 0x66, 1, 0xff, D2, 0, 0, 0, NONE, EW, 2, RM) \
    X(JMP_RM16, "jmp", 0x66, 1, 0xff, D4, 0, 0, 0, NONE, EW, 2, RM) \
    X(NOTRACK_CALL_RM16, "notrack call", 0x3e66, 1, 0xff, D2, 0, 0, 0, NONE, EW, 2, RM) \
    X(NOTRACK_JMP_RM16, "notrack jmp", 0x3e66, 1, 0xff, D4, 0, 0, 0, NONE, EW, 2, RM) \
    X(BND_CALL_RM16, "bnd call", 0xf266, 1, 0xff, D2, 0, 0, 0, NONE, EW, 2, RM) \
    X(BND_JMP_RM16, "bnd jmp", 0xf266, 1, 0xff, D4, 0, 0, 0, NONE, EW, 2, RM) \
    X(RETW, "retw", 0x66, 1, 0xc3, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(RETW_IMM16, "retw", 0x66, 1, 0xc2, NO, 2, 0, 0, NONE, NONE, 0, IMM) \
    X(RETD, "retd", 0, 1, 0xc3, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(RETD_IMM16, "retd", 0, 1, 0xc2, NO, 2, 0, 0, NONE, NONE, 0, IMM) \
    X(LEAVED, "leaved", 0, 1, 0xc9, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(ENTERD, "enterd", 0, 1, 0xc8, NO, 3, 0, 0, NONE, NONE, 0, IMM_IMM) \
    BUSTER_X86_SEGMENT_PUSH_FORMS(X, ES, "es", 0x06) \
    BUSTER_X86_SEGMENT_POP_FORMS(X, ES, "es", 0x07) \
    BUSTER_X86_SEGMENT_PUSH_FORMS(X, CS, "cs", 0x0e) \
    BUSTER_X86_SEGMENT_PUSH_FORMS(X, SS, "ss", 0x16) \
    BUSTER_X86_SEGMENT_POP_FORMS(X, SS, "ss", 0x17) \
    BUSTER_X86_SEGMENT_PUSH_FORMS(X, DS, "ds", 0x1e) \
    BUSTER_X86_SEGMENT_POP_FORMS(X, DS, "ds", 0x1f) \
    X(PUSHD_FS, "pushd fs", 0, 2, 0x0fa0, NO, 0, 0, 0, NONE, NONE, 4, NONE) \
    X(POPD_FS, "popd fs", 0, 2, 0x0fa1, NO, 0, 0, 0, NONE, NONE, 4, NONE) \
    X(PUSHD_GS, "pushd gs", 0, 2, 0x0fa8, NO, 0, 0, 0, NONE, NONE, 4, NONE) \
    X(POPD_GS, "popd gs", 0, 2, 0x0fa9, NO, 0, 0, 0, NONE, NONE, 4, NONE) \
    BUSTER_X86_MOFFS_FORMS(X, 16, 2) \
    BUSTER_X86_MOFFS_FORMS(X, 32, 4) \
    X(AAA, "aaa", 0, 1, 0x37, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(AAS, "aas", 0, 1, 0x3f, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(DAA, "daa", 0, 1, 0x27, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(DAS, "das", 0, 1, 0x2f, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(AAM_IMM8, "aam", 0, 1, 0xd4, NO, 1, 0, 0, NONE, NONE, 0, IMM) \
    X(AAD_IMM8, "aad", 0, 1, 0xd5, NO, 1, 0, 0, NONE, NONE, 0, IMM) \
    X(SALC, "salc", 0, 1, 0xd6, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(INTO, "into", 0, 1, 0xce, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(BOUND_R16_M32, "bound", 0x66, 1, 0x62, R, 0, 0, 0, GW, M, 4, REG_RM) \
    X(BOUND_R32_M64, "bound", 0, 1, 0x62, R, 0, 0, 0, GD, M, 8, REG_RM) \
    X(ARPL_RM16_R16, "arpl", 0, 1, 0x63, R, 0, 0, 0, GW, EW, 2, RM_REG) \
    X(LDS_R16_M32, "lds", 0x66, 1, 0xc5, R, 0, 0, 0, GW, M, 4, REG_RM) \
    X(LDS_R32_M48, "lds", 0, 1, 0xc5, R, 0, 0, 0, GD, M, 6, REG_RM) \
    X(LES_R16_M32, "les", 0x66, 1, 0xc4, R, 0, 0, 0, GW, M, 4, REG_RM) \
    X(LES_R32_M48, "les", 0, 1, 0xc4, R, 0, 0, 0, GD, M, 6, REG_RM) \
    X(PUSHAW, "pushaw", 0x66, 1, 0x60, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(PUSHAD, "pushad", 0, 1, 0x60, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(POPAW, "popaw", 0x66, 1, 0x61, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(POPAD, "popad", 0, 1, 0x61, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(CALL_FAR_PTR16, "call", 0x66, 1, 0x9a, NO, 4, 0, 0, NONE, NONE, 4, FAR_POINTER) \
    X(CALL_FAR_PTR32, "call", 0, 1, 0x9a, NO, 6, 0, 0, NONE, NONE, 6, FAR_POINTER) \
    X(JMP_FAR_PTR16, "jmp", 0x66, 1, 0xea, NO, 4, 0, 0, NONE, NONE, 4, FAR_POINTER) \
    X(JMP_FAR_PTR32, "jmp", 0, 1, 0xea, NO, 6, 0, 0, NONE, NONE, 6, FAR_POINTER) \
    X(INC_R16, "inc", 0x66, 1, 0x40, PR, 0, 0, 0, NONE, GW, 2, RM) \
    X(INC_R32, "inc", 0, 1, 0x40, PR, 0, 0, 0, NONE, GD, 4, RM) \
    X(DEC_R16, "dec", 0x66, 1, 0x48, PR, 0, 0, 0, NONE, GW, 2, RM) \
    X(DEC_R32, "dec", 0, 1, 0x48, PR, 0, 0, 0, NONE, GD, 4, RM) \
    X(MOV_TR_R32, "mov", 0, 2, 0x0f26, R, 0, 0, 0, TR, GD, 4, REG_RM) \
    X(MOV_R32_TR, "mov", 0, 2, 0x0f24, R, 0, 0, 0, TR, GD, 4, RM_REG) \
    X(LGDTW_M48, "lgdtw", 0x66, 2, 0x0f01, D2, 0, 0, 0, NONE, M, 6, RM) \
    X(LGDTD_M48, "lgdtd", 0, 2, 0x0f01, D2, 0, 0, 0, NONE, M, 6, RM) \
    X(LIDTW_M48, "lidtw", 0x66, 2, 0x0f01, D3, 0, 0, 0, NONE, M, 6, RM) \
    X(LIDTD_M48, "lidtd", 0, 2, 0x0f01, D3, 0, 0, 0, NONE, M, 6, RM) \
    X(SGDTW_M48, "sgdtw", 0x66, 2, 0x0f01, D0, 0, 0, 0, NONE, M, 6, RM) \
    X(SGDTD_M48, "sgdtd", 0, 2, 0x0f01, D0, 0, 0, 0, NONE, M, 6, RM) \
    X(SIDTW_M48, "sidtw", 0x66, 2, 0x0f01, D1, 0, 0, 0, NONE, M, 6, RM) \
    X(SIDTD_M48, "sidtd", 0, 2, 0x0f01, D1, 0, 0, 0, NONE, M, 6, RM) \
    X(MONTMUL, "montmul", 0xf3, 3, 0x0fa6c0, NO, 0, 0, 0, NONE, NONE, 0, NONE)

#define BUSTER_X86_FORMS(X, XV) \
    X(NONE, "", 0, 0, 0, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    BUSTER_X86_ARITHMETIC_FORMS(X, ADD, "add", 0) \
    BUSTER_X86_ARITHMETIC_FORMS(X, OR, "or", 1) \
    BUSTER_X86_ARITHMETIC_FORMS(X, ADC, "adc", 2) \
    BUSTER_X86_ARITHMETIC_FORMS(X, SBB, "sbb", 3) \
    BUSTER_X86_ARITHMETIC_FORMS(X, AND, "and", 4) \
    BUSTER_X86_ARITHMETIC_FORMS(X, SUB, "sub", 5) \
    BUSTER_X86_ARITHMETIC_FORMS(X, XOR, "xor", 6) \
    BUSTER_X86_ARITHMETIC_FORMS(X, CMP, "cmp", 7) \
    X(MOV_RM8_R8, "mov", 0, 1, 0x88, R, 0, 0, 0, GB, EB, 1, RM_REG) \
    X(MOV_RM16_R16, "mov", 0x66, 1, 0x89, R, 0, 0, 0, GW, EW, 2, RM_REG) \
    X(MOV_RM32_R32, "mov", 0, 1, 0x89, R, 0, 0, 0, GD, ED, 4, RM_REG) \
    X(MOV_RM64_R64, "mov", 0, 1, 0x89, R, 0, 0, 1, GQ, EQ, 8, RM_REG) \
    X(MOV_R8_RM8, "mov", 0, 1, 0x8a, R, 0, 0, 0, GB, EB, 1, REG_RM) \
    X(MOV_R16_RM16, "mov", 0x66, 1, 0x8b, R, 0, 0, 0, GW, EW, 2, REG_RM) \
    X(MOV_R32_RM32, "mov", 0, 1, 0x8b, R, 0, 0, 0, GD, ED, 4, REG_RM) \
    X(MOV_R64_RM64, "mov", 0, 1, 0x8b, R, 0, 0, 1, GQ, EQ, 8, REG_RM) \
    X(MOV_R8_IMM8, "mov", 0, 1, 0xb0, PR, 1, 0, 0, NONE, GB, 1, RM_IMM) \
    X(MOV_R16_IMM16, "mov", 0x66, 1, 0xb8, PR, 2, 0, 0, NONE, GW, 2, RM_IMM) \
    X(MOV_R32_IMM32, "mov", 0, 1, 0xb8, PR, 4, 0, 0, NONE, GD, 4, RM_IMM) \
    X(MOV_R64_IMM64, "movabs", 0, 1, 0xb8, PR, 8, 0, 1, NONE, GQ, 8, RM_IMM) \
    X(MOV_RM8_IMM8, "mov", 0, 1, 0xc6, D0, 1, 0, 0, NONE, EB, 1, RM_IMM) \
    X(MOV_RM16_IMM16, "mov", 0x66, 1, 0xc7, D0, 2, 0, 0, NONE, EW, 2, RM_IMM) \
    X(MOV_RM32_IMM32, "mov", 0, 1, 0xc7, D0, 4, 0, 0, NONE, ED, 4, RM_IMM) \
    X(MOV_RM64_IMM32, "mov", 0, 1, 0xc7, D0, 4, 0, 1, NONE, EQ, 8, RM_IMM) \
    X(MOVABS_AL_MOFFS, "movabs", 0, 1, 0xa0, NO, 8, 0, 0, NONE, NONE, 1, ACC_MOFFS) \
    X(MOVABS_EAX_MOFFS, "movabs", 0, 1, 0xa1, NO, 8, 0, 0, NONE, NONE, 4, ACC_MOFFS) \
    X(MOVABS_RAX_MOFFS, "movabs", 0, 1, 0xa1, NO, 8, 0, 1, NONE, NONE, 8, ACC_MOFFS) \
    X(MOVABS_MOFFS_AL, "movabs", 0, 1, 0xa2, NO, 8, 0, 0, NONE, NONE, 1, MOFFS_ACC) \
    X(MOVABS_MOFFS_EAX, "movabs", 0, 1, 0xa3, NO, 8, 0, 0, NONE, NONE, 4, MOFFS_ACC) \
    X(MOVABS_MOFFS_RAX, "movabs", 0, 1, 0xa3, NO, 8, 0, 1, NONE, NONE, 8, MOFFS_ACC) \
    X(MOVZX_R16_RM8, "movzx", 0x66, 2, 0x0fb6, R, 0, 0, 0, GW, EB, 1, REG_RM) \
    X(MOVZX_R32_RM8, "movzx", 0, 2, 0x0fb6, R, 0, 0, 0, GD, EB, 1, REG_RM) \
    X(MOVZX_R64_RM8, "movzx", 0, 2, 0x0fb6, R, 0, 0, 1, GQ, EB, 1, REG_RM) \
    X(MOVZX_R32_RM16, "movzx", 0, 2, 0x0fb7, R, 0, 0, 0, GD, EW, 2, REG_RM) \
    X(MOVZX_R64_RM16, "movzx", 0, 2, 0x0fb7, R, 0, 0, 1, GQ, EW, 2, REG_RM) \
    X(MOVSX_R16_RM8, "movsx", 0x66, 2, 0x0fbe, R, 0, 0, 0, GW, EB, 1, REG_RM) \
    X(MOVSX_R32_RM8, "movsx", 0, 2, 0x0fbe, R, 0, 0, 0, GD, EB, 1, REG_RM) \
    X(MOVSX_R64_RM8, "movsx", 0, 2, 0x0fbe, R, 0, 0, 1, GQ, EB, 1, REG_RM) \
    X(MOVSX_R32_RM16, "movsx", 0, 2, 0x0fbf, R, 0, 0, 0, GD, EW, 2, REG_RM) \
    X(MOVSX_R64_RM16, "movsx", 0, 2, 0x0fbf, R, 0, 0, 1, GQ, EW, 2, REG_RM) \
    X(MOVSXD_R64_RM32, "movsxd", 0, 1, 0x63, R, 0, 0, 1, GQ, ED, 4, REG_RM) \
    X(LEA_R16_M, "lea", 0x66, 1, 0x8d, R, 0, 0, 0, GW, M, 0, REG_RM) \
    X(LEA_R32_M, "lea", 0, 1, 0x8d, R, 0, 0, 0, GD, M, 0, REG_RM) \
    X(LEA_R64_M, "lea", 0, 1, 0x8d, R, 0, 0, 1, GQ, M, 0, REG_RM) \
    X(TEST_RM8_R8, "test", 0, 1, 0x84, R, 0, 0, 0, GB, EB, 1, RM_REG) \
    X(TEST_RM16_R16, "test", 0x66, 1, 0x85, R, 0, 0, 0, GW, EW, 2, RM_REG) \
    X(TEST_RM32_R32, "test", 0, 1, 0x85, R, 0, 0, 0, GD, ED, 4, RM_REG) \
    X(TEST_RM64_R64, "test", 0, 1, 0x85, R, 0, 0, 1, GQ, EQ, 8, RM_REG) \
    X(TEST_RM8_IMM8, "test", 0, 1, 0xf6, D0, 1, 0, 0, NONE, EB, 1, RM_IMM) \
    X(TEST_RM16_IMM16, "test", 0x66, 1, 0xf7, D0, 2, 0, 0, NONE, EW, 2, RM_IMM) \
    X(TEST_RM32_IMM32, "test", 0, 1, 0xf7, D0, 4, 0, 0, NONE, ED, 4, RM_IMM) \
    X(TEST_RM64_IMM32, "test", 0, 1, 0xf7, D0, 4, 0, 1, NONE, EQ, 8, RM_IMM) \
    X(TEST_AL_IMM8, "test", 0, 1, 0xa8, NO, 1, 0, 0, NONE, NONE, 1, ACC_IMM) \
    X(TEST_AX_IMM16, "test", 0x66, 1, 0xa9, NO, 2, 0, 0, NONE, NONE, 2, ACC_IMM) \
    X(TEST_EAX_IMM32, "test", 0, 1, 0xa9, NO, 4, 0, 0, NONE, NONE, 4, ACC_IMM) \
    X(TEST_RAX_IMM32, "test", 0, 1, 0xa9, NO, 4, 0, 1, NONE, NONE, 8, ACC_IMM) \
    X(XCHG_RM8_R8, "xchg", 0, 1, 0x86, R, 0, 0, 0, GB, EB, 1, RM_REG) \
    X(XCHG_RM16_R16, "xchg", 0x66, 1, 0x87, R, 0, 0, 0, GW, EW, 2, RM_REG) \
    X(XCHG_RM32_R32, "xchg", 0, 1, 0x87, R, 0, 0, 0, GD, ED, 4, RM_REG) \
    X(XCHG_RM64_R64, "xchg", 0, 1, 0x87, R, 0, 0, 1, GQ, EQ, 8, RM_REG) \
    BUSTER_X86_UNARY_FORMS(X, NOT, "not", 0xf6, 0xf7, 2) \
    BUSTER_X86_UNARY_FORMS(X, NEG, "neg", 0xf6, 0xf7, 3) \
    BUSTER_X86_UNARY_FORMS(X, MUL, "mul", 0xf6, 0xf7, 4) \
    BUSTER_X86_UNARY_FORMS(X, IMUL, "imul", 0xf6, 0xf7, 5) \
    BUSTER_X86_UNARY_FORMS(X, DIV, "div", 0xf6, 0xf7, 6) \
    BUSTER_X86_UNARY_FORMS(X, IDIV, "idiv", 0xf6, 0xf7, 7) \
    BUSTER_X86_UNARY_FORMS(X, INC, "inc", 0xfe, 0xff, 0) \
    BUSTER_X86_UNARY_FORMS(X, DEC, "dec", 0xfe, 0xff, 1) \
    X(IMUL_R16_RM16, "imul", 0x66, 2, 0x0faf, R, 0, 0, 0, GW, EW, 2, REG_RM) \
    X(IMUL_R32_RM32, "imul", 0, 2, 0x0faf, R, 0, 0, 0, GD, ED, 4, REG_RM) \
    X(IMUL_R64_RM64, "imul", 0, 2, 0x0faf, R, 0, 0, 1, GQ, EQ, 8, REG_RM) \
    X(IMUL_R16_RM16_IMM8, "imul", 0x66, 1, 0x6b, R, 1, 0, 0, GW, EW, 2, REG_RM_IMM) \
    X(IMUL_R32_RM32_IMM8, "imul", 0, 1, 0x6b, R, 1, 0, 0, GD, ED, 4, REG_RM_IMM) \
    X(IMUL_R64_RM64_IMM8, "imul", 0, 1, 0x6b, R, 1, 0, 1, GQ, EQ, 8, REG_RM_IMM) \
    X(IMUL_R16_RM16_IMM16, "imul", 0x66, 1, 0x69, R, 2, 0, 0, GW, EW, 2, REG_RM_IMM) \
    X(IMUL_R32_RM32_IMM32, "imul", 0, 1, 0x69, R, 4, 0, 0, GD, ED, 4, REG_RM_IMM) \
    X(IMUL_R64_RM64_IMM32, "imul", 0, 1, 0x69, R, 4, 0, 1, GQ, EQ, 8, REG_RM_IMM) \
    BUSTER_X86_SHIFT_FORMS(X, ROL, "rol", 0) \
    BUSTER_X86_SHIFT_FORMS(X, ROR, "ror", 1) \
    BUSTER_X86_SHIFT_FORMS(X, SHL, "shl", 4) \
    BUSTER_X86_SHIFT_FORMS(X, SHR, "shr", 5) \
    BUSTER_X86_SHIFT_FORMS(X, SAR, "sar", 7) \
    BUSTER_X86_SHIFT_FORMS(X, RCL, "rcl", 2) \
    BUSTER_X86_SHIFT_FORMS(X, RCR, "rcr", 3) \
    X(SHLD_RM16_R16_IMM8, "shld", 0x66, 2, 0x0fa4, R, 1, 0, 0, GW, EW, 2, RM_REG_IMM) \
    X(SHLD_RM16_R16_CL, "shld", 0x66, 2, 0x0fa5, R, 0, 0, 0, GW, EW, 2, RM_REG_CL) \
    X(SHRD_RM16_R16_IMM8, "shrd", 0x66, 2, 0x0fac, R, 1, 0, 0, GW, EW, 2, RM_REG_IMM) \
    X(SHRD_RM16_R16_CL, "shrd", 0x66, 2, 0x0fad, R, 0, 0, 0, GW, EW, 2, RM_REG_CL) \
    X(SHLD_RM32_R32_IMM8, "shld", 0, 2, 0x0fa4, R, 1, 0, 0, GD, ED, 4, RM_REG_IMM) \
    X(SHLD_RM64_R64_IMM8, "shld", 0, 2, 0x0fa4, R, 1, 0, 1, GQ, EQ, 8, RM_REG_IMM) \
    X(SHLD_RM32_R32_CL, "shld", 0, 2, 0x0fa5, R, 0, 0, 0, GD, ED, 4, RM_REG_CL) \
    X(SHLD_RM64_R64_CL, "shld", 0, 2, 0x0fa5, R, 0, 0, 1, GQ, EQ, 8, RM_REG_CL) \
    X(SHRD_RM32_R32_IMM8, "shrd", 0, 2, 0x0fac, R, 1, 0, 0, GD, ED, 4, RM_REG_IMM) \
    X(SHRD_RM64_R64_IMM8, "shrd", 0, 2, 0x0fac, R, 1, 0, 1, GQ, EQ, 8, RM_REG_IMM) \
    X(SHRD_RM32_R32_CL, "shrd", 0, 2, 0x0fad, R, 0, 0, 0, GD, ED, 4, RM_REG_CL) \
    X(SHRD_RM64_R64_CL, "shrd", 0, 2, 0x0fad, R, 0, 0, 1, GQ, EQ, 8, RM_REG_CL) \
    X(PUSH_R64, "push", 0, 1, 0x50, PR, 0, 0, 0, NONE, GQ, 8, RM) \
    X(POP_R64, "pop", 0, 1, 0x58, PR, 0, 0, 0, NONE, GQ, 8, RM) \
    /* APX's push and pop with a hint that the pop pops what a push pushed (PPX, REX2's W), which always have REX2: its D5 is their prefix. */ \
    X(PUSHP_R64, "pushp", 0xd5, 1, 0x50, PR, 0, 0, 1, NONE, GQ, 8, RM) \
    X(POPP_R64, "popp", 0xd5, 1, 0x58, PR, 0, 0, 1, NONE, GQ, 8, RM) \
    X(PUSH_RM64, "push", 0, 1, 0xff, D6, 0, 0, 0, NONE, EQ, 8, RM) \
    X(POP_RM64, "pop", 0, 1, 0x8f, D0, 0, 0, 0, NONE, EQ, 8, RM) \
    X(PUSH_IMM8, "push", 0, 1, 0x6a, NO, 1, 0, 0, NONE, NONE, 8, IMM) \
    X(PUSH_IMM32, "push", 0, 1, 0x68, NO, 4, 0, 0, NONE, NONE, 8, IMM) \
    X(PUSH_R16, "push", 0x66, 1, 0x50, PR, 0, 0, 0, NONE, GW, 2, RM) \
    X(POP_R16, "pop", 0x66, 1, 0x58, PR, 0, 0, 0, NONE, GW, 2, RM) \
    X(PUSH_RM16, "push", 0x66, 1, 0xff, D6, 0, 0, 0, NONE, EW, 2, RM) \
    X(POP_RM16, "pop", 0x66, 1, 0x8f, D0, 0, 0, 0, NONE, EW, 2, RM) \
    X(PUSHW_IMM16, "pushw", 0x66, 1, 0x68, NO, 2, 0, 0, NONE, NONE, 2, IMM) \
    X(PUSHW_IMM8, "pushw", 0x66, 1, 0x6a, NO, 1, 0, 0, NONE, NONE, 2, IMM) \
    X(CALL_REL32, "call", 0, 1, 0xe8, NO, 0, 4, 0, NONE, NONE, 0, REL) \
    X(CALL_RM64, "call", 0, 1, 0xff, D2, 0, 0, 0, NONE, EQ, 8, RM) \
    X(JMP_REL8, "jmp", 0, 1, 0xeb, NO, 0, 1, 0, NONE, NONE, 0, REL) \
    X(JMP_REL32, "jmp", 0, 1, 0xe9, NO, 0, 4, 0, NONE, NONE, 0, REL) \
    X(JMP_RM64, "jmp", 0, 1, 0xff, D4, 0, 0, 0, NONE, EQ, 8, RM) \
    /* APX's jump to a 64-bit absolute address: A1 with REX2 (W0, map 0). */ \
    X(JMPABS_IMM64, "jmpabs", 0xd5, 1, 0xa1, NO, 8, 0, 0, NONE, NONE, 8, IMM) \
    X(CALL_FAR_M32, "call", 0x66, 1, 0xff, D3, 0, 0, 0, NONE, M, 4, RM) \
    X(CALL_FAR_M48, "call", 0, 1, 0xff, D3, 0, 0, 0, NONE, M, 6, RM) \
    X(CALL_FAR_M80, "rex.w call", 0, 1, 0xff, D3, 0, 0, 1, NONE, M, 6, RM) \
    X(JMP_FAR_M32, "jmp", 0x66, 1, 0xff, D5, 0, 0, 0, NONE, M, 4, RM) \
    X(JMP_FAR_M48, "jmp", 0, 1, 0xff, D5, 0, 0, 0, NONE, M, 6, RM) \
    X(JMP_FAR_M80, "rex.w jmp", 0, 1, 0xff, D5, 0, 0, 1, NONE, M, 6, RM) \
    /* CET's NOTRACK (3E), on an indirect branch through a register. */ \
    X(NOTRACK_JMP_R64, "notrack jmp", 0x3e, 1, 0xff, D4, 0, 0, 0, NONE, GQ, 8, RM) \
    X(NOTRACK_CALL_R64, "notrack call", 0x3e, 1, 0xff, D2, 0, 0, 0, NONE, GQ, 8, RM) \
    /* NOTRACK through memory (with which a segment override would come before the 3E, leaving 3E the last segment prefix, which is the one that counts), and MPX's BND (F2) on the near branches, with NOTRACK too. */ \
    X(NOTRACK_JMP_M64, "notrack jmp", 0x3e, 1, 0xff, D4, 0, 0, 0, NONE, M, 8, RM) \
    X(NOTRACK_CALL_M64, "notrack call", 0x3e, 1, 0xff, D2, 0, 0, 0, NONE, M, 8, RM) \
    X(BND_CALL_REL32, "bnd call", 0xf2, 1, 0xe8, NO, 0, 4, 0, NONE, NONE, 0, REL) \
    X(BND_CALL_RM64, "bnd call", 0xf2, 1, 0xff, D2, 0, 0, 0, NONE, EQ, 8, RM) \
    X(BND_JMP_REL8, "bnd jmp", 0xf2, 1, 0xeb, NO, 0, 1, 0, NONE, NONE, 0, REL) \
    X(BND_JMP_REL32, "bnd jmp", 0xf2, 1, 0xe9, NO, 0, 4, 0, NONE, NONE, 0, REL) \
    X(BND_JMP_RM64, "bnd jmp", 0xf2, 1, 0xff, D4, 0, 0, 0, NONE, EQ, 8, RM) \
    X(BND_RET, "bnd ret", 0xf2, 1, 0xc3, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(BND_RET_IMM16, "bnd ret", 0xf2, 1, 0xc2, NO, 2, 0, 0, NONE, NONE, 0, IMM) \
    X(NOTRACK_BND_JMP_R64, "notrack bnd jmp", 0xf23e, 1, 0xff, D4, 0, 0, 0, NONE, GQ, 8, RM) \
    X(NOTRACK_BND_JMP_M64, "notrack bnd jmp", 0xf23e, 1, 0xff, D4, 0, 0, 0, NONE, M, 8, RM) \
    X(NOTRACK_BND_CALL_R64, "notrack bnd call", 0xf23e, 1, 0xff, D2, 0, 0, 0, NONE, GQ, 8, RM) \
    X(NOTRACK_BND_CALL_M64, "notrack bnd call", 0xf23e, 1, 0xff, D2, 0, 0, 0, NONE, M, 8, RM) \
    /* The 16-bit near branches (66 with a rel16), which AMD takes as branches to a 16-bit address in 64-bit mode and Intel ignores 66 on. */ \
    X(JMP_REL16, "data16 jmp", 0x66, 1, 0xe9, NO, 0, 2, 0, NONE, NONE, 0, REL) \
    X(CALL_REL16, "data16 call", 0x66, 1, 0xe8, NO, 0, 2, 0, NONE, NONE, 0, REL) \
    BUSTER_X86_CONDITION_FORMS(X, O, "o", 0) \
    BUSTER_X86_CONDITION_FORMS(X, NO, "no", 1) \
    BUSTER_X86_CONDITION_FORMS(X, B, "b", 2) \
    BUSTER_X86_CONDITION_FORMS(X, AE, "ae", 3) \
    BUSTER_X86_CONDITION_FORMS(X, E, "e", 4) \
    BUSTER_X86_CONDITION_FORMS(X, NE, "ne", 5) \
    BUSTER_X86_CONDITION_FORMS(X, BE, "be", 6) \
    BUSTER_X86_CONDITION_FORMS(X, A, "a", 7) \
    BUSTER_X86_CONDITION_FORMS(X, S, "s", 8) \
    BUSTER_X86_CONDITION_FORMS(X, NS, "ns", 9) \
    BUSTER_X86_CONDITION_FORMS(X, P, "p", 10) \
    BUSTER_X86_CONDITION_FORMS(X, NP, "np", 11) \
    BUSTER_X86_CONDITION_FORMS(X, L, "l", 12) \
    BUSTER_X86_CONDITION_FORMS(X, GE, "ge", 13) \
    BUSTER_X86_CONDITION_FORMS(X, LE, "le", 14) \
    BUSTER_X86_CONDITION_FORMS(X, G, "g", 15) \
    BUSTER_X86_PREFIXED_JCC_FORMS(X, O, "o", 0) \
    BUSTER_X86_PREFIXED_JCC_FORMS(X, NO, "no", 1) \
    BUSTER_X86_PREFIXED_JCC_FORMS(X, B, "b", 2) \
    BUSTER_X86_PREFIXED_JCC_FORMS(X, AE, "ae", 3) \
    BUSTER_X86_PREFIXED_JCC_FORMS(X, E, "e", 4) \
    BUSTER_X86_PREFIXED_JCC_FORMS(X, NE, "ne", 5) \
    BUSTER_X86_PREFIXED_JCC_FORMS(X, BE, "be", 6) \
    BUSTER_X86_PREFIXED_JCC_FORMS(X, A, "a", 7) \
    BUSTER_X86_PREFIXED_JCC_FORMS(X, S, "s", 8) \
    BUSTER_X86_PREFIXED_JCC_FORMS(X, NS, "ns", 9) \
    BUSTER_X86_PREFIXED_JCC_FORMS(X, P, "p", 10) \
    BUSTER_X86_PREFIXED_JCC_FORMS(X, NP, "np", 11) \
    BUSTER_X86_PREFIXED_JCC_FORMS(X, L, "l", 12) \
    BUSTER_X86_PREFIXED_JCC_FORMS(X, GE, "ge", 13) \
    BUSTER_X86_PREFIXED_JCC_FORMS(X, LE, "le", 14) \
    BUSTER_X86_PREFIXED_JCC_FORMS(X, G, "g", 15) \
    X(RET, "ret", 0, 1, 0xc3, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(RET_IMM16, "ret", 0, 1, 0xc2, NO, 2, 0, 0, NONE, NONE, 0, IMM) \
    X(NOP, "nop", 0, 1, 0x90, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(NOP_RM16, "nop", 0x66, 2, 0x0f1f, D0, 0, 0, 0, NONE, EW, 2, RM) \
    X(NOP_RM32, "nop", 0, 2, 0x0f1f, D0, 0, 0, 0, NONE, ED, 4, RM) \
    X(INT3, "int3", 0, 1, 0xcc, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(INT_IMM8, "int", 0, 1, 0xcd, NO, 1, 0, 0, NONE, NONE, 0, IMM) \
    X(UD2, "ud2", 0, 2, 0x0f0b, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    /* The other undefined instructions: ud0 and ud1 without ModRM (as older AMD processors take them, and GNU as without operands) and with it (as Intel's take them), D6's udb (64-bit mode's alone), and int1. */ \
    X(UD0, "ud0", 0, 2, 0x0fff, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    BUSTER_X86_R_RM_FORMS(X, UD0, "ud0", 0, 0x0fff) \
    X(UD1, "ud1", 0, 2, 0x0fb9, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    BUSTER_X86_R_RM_FORMS(X, UD1, "ud1", 0, 0x0fb9) \
    X(UDB, "udb", 0, 1, 0xd6, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(INT1, "int1", 0, 1, 0xf1, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(LEAVE, "leave", 0, 1, 0xc9, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(ENTER, "enter", 0, 1, 0xc8, NO, 3, 0, 0, NONE, NONE, 0, IMM_IMM) \
    X(LEAVEW, "leavew", 0x66, 1, 0xc9, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(ENTERW, "enterw", 0x66, 1, 0xc8, NO, 3, 0, 0, NONE, NONE, 0, IMM_IMM) \
    X(CBW, "cbw", 0x66, 1, 0x98, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(CWDE, "cwde", 0, 1, 0x98, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(CDQE, "cdqe", 0, 1, 0x98, NO, 0, 0, 1, NONE, NONE, 0, NONE) \
    X(CWD, "cwd", 0x66, 1, 0x99, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(CDQ, "cdq", 0, 1, 0x99, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(CQO, "cqo", 0, 1, 0x99, NO, 0, 0, 1, NONE, NONE, 0, NONE) \
    X(SYSCALL, "syscall", 0, 2, 0x0f05, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(ENDBR64, "endbr64", 0xf3, 3, 0x0f1efa, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(PAUSE, "pause", 0xf3, 1, 0x90, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(MFENCE, "mfence", 0, 3, 0x0faef0, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(LFENCE, "lfence", 0, 3, 0x0faee8, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(SFENCE, "sfence", 0, 3, 0x0faef8, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(RDTSC, "rdtsc", 0, 2, 0x0f31, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(RDTSCP, "rdtscp", 0, 3, 0x0f01f9, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(CPUID, "cpuid", 0, 2, 0x0fa2, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(BSWAP_R32, "bswap", 0, 2, 0x0fc8, PR, 0, 0, 0, NONE, GD, 4, RM) \
    X(BSWAP_R64, "bswap", 0, 2, 0x0fc8, PR, 0, 0, 1, NONE, GQ, 8, RM) \
    BUSTER_X86_BIT_TEST_FORMS(X, BT, "bt", 0xa3, 4) \
    BUSTER_X86_BIT_TEST_FORMS(X, BTS, "bts", 0xab, 5) \
    BUSTER_X86_BIT_TEST_FORMS(X, BTR, "btr", 0xb3, 6) \
    BUSTER_X86_BIT_TEST_FORMS(X, BTC, "btc", 0xbb, 7) \
    BUSTER_X86_R_RM_FORMS(X, BSF, "bsf", 0, 0x0fbc) \
    BUSTER_X86_R_RM_FORMS(X, BSR, "bsr", 0, 0x0fbd) \
    BUSTER_X86_R_RM_FORMS(X, TZCNT, "tzcnt", 0xf3, 0x0fbc) \
    BUSTER_X86_R_RM_FORMS(X, LZCNT, "lzcnt", 0xf3, 0x0fbd) \
    BUSTER_X86_R_RM_FORMS(X, POPCNT, "popcnt", 0xf3, 0x0fb8) \
    BUSTER_X86_ATOMIC_FORMS(X, CMPXCHG, "cmpxchg", 0x0fb0, 0x0fb1) \
    BUSTER_X86_ATOMIC_FORMS(X, XADD, "xadd", 0x0fc0, 0x0fc1) \
    X(CMPXCHG16B_M128, "cmpxchg16b", 0, 2, 0x0fc7, D1, 0, 0, 1, NONE, M, 16, RM) \
    X(LOCK_CMPXCHG16B_M128, "lock cmpxchg16b", 0xf0, 2, 0x0fc7, D1, 0, 0, 1, NONE, M, 16, RM) \
    X(CMPXCHG8B_M64, "cmpxchg8b", 0, 2, 0x0fc7, D1, 0, 0, 0, NONE, M, 8, RM) \
    X(LOCK_CMPXCHG8B_M64, "lock cmpxchg8b", 0xf0, 2, 0x0fc7, D1, 0, 0, 0, NONE, M, 8, RM) \
    BUSTER_X86_LOCK_ARITHMETIC_FORMS(X, ADD, "add", 0) \
    BUSTER_X86_LOCK_ARITHMETIC_FORMS(X, OR, "or", 1) \
    BUSTER_X86_LOCK_ARITHMETIC_FORMS(X, ADC, "adc", 2) \
    BUSTER_X86_LOCK_ARITHMETIC_FORMS(X, SBB, "sbb", 3) \
    BUSTER_X86_LOCK_ARITHMETIC_FORMS(X, AND, "and", 4) \
    BUSTER_X86_LOCK_ARITHMETIC_FORMS(X, SUB, "sub", 5) \
    BUSTER_X86_LOCK_ARITHMETIC_FORMS(X, XOR, "xor", 6) \
    BUSTER_X86_LOCK_UNARY_FORMS(X, INC, "inc", 0xfe, 0xff, 0) \
    BUSTER_X86_LOCK_UNARY_FORMS(X, DEC, "dec", 0xfe, 0xff, 1) \
    BUSTER_X86_LOCK_UNARY_FORMS(X, NOT, "not", 0xf6, 0xf7, 2) \
    BUSTER_X86_LOCK_UNARY_FORMS(X, NEG, "neg", 0xf6, 0xf7, 3) \
    BUSTER_X86_LOCK_BIT_TEST_FORMS(X, BTS, "bts", 0xab, 5) \
    BUSTER_X86_LOCK_BIT_TEST_FORMS(X, BTR, "btr", 0xb3, 6) \
    BUSTER_X86_LOCK_BIT_TEST_FORMS(X, BTC, "btc", 0xbb, 7) \
    /* HLE: XACQUIRE and XRELEASE on the locked forms and XCHG to memory, and XRELEASE on MOV to memory. */ \
    BUSTER_X86_HLE_FORMS(X, XACQUIRE, "xacquire", 0xf2) \
    BUSTER_X86_HLE_FORMS(X, XRELEASE, "xrelease", 0xf3) \
    X(XRELEASE_MOV_M8_R8, "xrelease mov", 0xf3, 1, 0x88, R, 0, 0, 0, GB, M, 1, RM_REG) \
    X(XRELEASE_MOV_M16_R16, "xrelease mov", 0xf366, 1, 0x89, R, 0, 0, 0, GW, M, 2, RM_REG) \
    X(XRELEASE_MOV_M32_R32, "xrelease mov", 0xf3, 1, 0x89, R, 0, 0, 0, GD, M, 4, RM_REG) \
    X(XRELEASE_MOV_M64_R64, "xrelease mov", 0xf3, 1, 0x89, R, 0, 0, 1, GQ, M, 8, RM_REG) \
    X(XRELEASE_MOV_M8_IMM8, "xrelease mov", 0xf3, 1, 0xc6, D0, 1, 0, 0, NONE, M, 1, RM_IMM) \
    X(XRELEASE_MOV_M16_IMM16, "xrelease mov", 0xf366, 1, 0xc7, D0, 2, 0, 0, NONE, M, 2, RM_IMM) \
    X(XRELEASE_MOV_M32_IMM32, "xrelease mov", 0xf3, 1, 0xc7, D0, 4, 0, 0, NONE, M, 4, RM_IMM) \
    X(XRELEASE_MOV_M64_IMM32, "xrelease mov", 0xf3, 1, 0xc7, D0, 4, 0, 1, NONE, M, 8, RM_IMM) \
    X(CRC32_R32_RM8, "crc32", 0xf2, 3, 0x0f38f0, R, 0, 0, 0, GD, EB, 1, REG_RM) \
    X(CRC32_R32_RM16, "crc32", 0xf266, 3, 0x0f38f1, R, 0, 0, 0, GD, EW, 2, REG_RM) \
    X(CRC32_R32_RM32, "crc32", 0xf2, 3, 0x0f38f1, R, 0, 0, 0, GD, ED, 4, REG_RM) \
    X(CRC32_R64_RM8, "crc32", 0xf2, 3, 0x0f38f0, R, 0, 0, 1, GQ, EB, 1, REG_RM) \
    X(CRC32_R64_RM64, "crc32", 0xf2, 3, 0x0f38f1, R, 0, 0, 1, GQ, EQ, 8, REG_RM) \
    X(MOVSS_X_XM32, "movss", 0xf3, 2, 0x0f10, R, 0, 0, 0, V, XM, 4, REG_RM) \
    X(MOVSS_XM32_X, "movss", 0xf3, 2, 0x0f11, R, 0, 0, 0, V, XM, 4, RM_REG) \
    X(MOVSD_X_XM64, "movsd", 0xf2, 2, 0x0f10, R, 0, 0, 0, V, XM, 8, REG_RM) \
    X(MOVSD_XM64_X, "movsd", 0xf2, 2, 0x0f11, R, 0, 0, 0, V, XM, 8, RM_REG) \
    BUSTER_X86_SSE_SCALAR_FORMS(X, ADD, "add", 0x58) \
    BUSTER_X86_SSE_SCALAR_FORMS(X, MUL, "mul", 0x59) \
    BUSTER_X86_SSE_SCALAR_FORMS(X, SUB, "sub", 0x5c) \
    BUSTER_X86_SSE_SCALAR_FORMS(X, MIN, "min", 0x5d) \
    BUSTER_X86_SSE_SCALAR_FORMS(X, DIV, "div", 0x5e) \
    BUSTER_X86_SSE_SCALAR_FORMS(X, MAX, "max", 0x5f) \
    BUSTER_X86_SSE_SCALAR_FORMS(X, SQRT, "sqrt", 0x51) \
    X(UCOMISS_X_XM32, "ucomiss", 0, 2, 0x0f2e, R, 0, 0, 0, V, XM, 4, REG_RM) \
    X(UCOMISD_X_XM64, "ucomisd", 0x66, 2, 0x0f2e, R, 0, 0, 0, V, XM, 8, REG_RM) \
    X(COMISS_X_XM32, "comiss", 0, 2, 0x0f2f, R, 0, 0, 0, V, XM, 4, REG_RM) \
    X(COMISD_X_XM64, "comisd", 0x66, 2, 0x0f2f, R, 0, 0, 0, V, XM, 8, REG_RM) \
    X(CVTSI2SS_X_RM32, "cvtsi2ss", 0xf3, 2, 0x0f2a, R, 0, 0, 0, V, ED, 4, REG_RM) \
    X(CVTSI2SS_X_RM64, "cvtsi2ss", 0xf3, 2, 0x0f2a, R, 0, 0, 1, V, EQ, 8, REG_RM) \
    X(CVTSI2SD_X_RM32, "cvtsi2sd", 0xf2, 2, 0x0f2a, R, 0, 0, 0, V, ED, 4, REG_RM) \
    X(CVTSI2SD_X_RM64, "cvtsi2sd", 0xf2, 2, 0x0f2a, R, 0, 0, 1, V, EQ, 8, REG_RM) \
    X(CVTTSS2SI_R32_XM32, "cvttss2si", 0xf3, 2, 0x0f2c, R, 0, 0, 0, GD, XM, 4, REG_RM) \
    X(CVTTSS2SI_R64_XM32, "cvttss2si", 0xf3, 2, 0x0f2c, R, 0, 0, 1, GQ, XM, 4, REG_RM) \
    X(CVTTSD2SI_R32_XM64, "cvttsd2si", 0xf2, 2, 0x0f2c, R, 0, 0, 0, GD, XM, 8, REG_RM) \
    X(CVTTSD2SI_R64_XM64, "cvttsd2si", 0xf2, 2, 0x0f2c, R, 0, 0, 1, GQ, XM, 8, REG_RM) \
    X(CVTSS2SD_X_XM32, "cvtss2sd", 0xf3, 2, 0x0f5a, R, 0, 0, 0, V, XM, 4, REG_RM) \
    X(CVTSD2SS_X_XM64, "cvtsd2ss", 0xf2, 2, 0x0f5a, R, 0, 0, 0, V, XM, 8, REG_RM) \
    BUSTER_X86_SSE_MOVE_FORMS(X, MOVAPS, "movaps", 0, 0x0f28, 0x0f29, 16) \
    BUSTER_X86_SSE_MOVE_FORMS(X, MOVUPS, "movups", 0, 0x0f10, 0x0f11, 16) \
    BUSTER_X86_SSE_MOVE_FORMS(X, MOVAPD, "movapd", 0x66, 0x0f28, 0x0f29, 16) \
    BUSTER_X86_SSE_MOVE_FORMS(X, MOVUPD, "movupd", 0x66, 0x0f10, 0x0f11, 16) \
    BUSTER_X86_SSE_MOVE_FORMS(X, MOVDQA, "movdqa", 0x66, 0x0f6f, 0x0f7f, 16) \
    BUSTER_X86_SSE_MOVE_FORMS(X, MOVDQU, "movdqu", 0xf3, 0x0f6f, 0x0f7f, 16) \
    BUSTER_X86_SSE_PACKED_FORM(X, XORPS, "xorps", 0, 2, 0x0f57) \
    BUSTER_X86_SSE_PACKED_FORM(X, XORPD, "xorpd", 0x66, 2, 0x0f57) \
    BUSTER_X86_SSE_PACKED_FORM(X, ANDPS, "andps", 0, 2, 0x0f54) \
    BUSTER_X86_SSE_PACKED_FORM(X, ANDPD, "andpd", 0x66, 2, 0x0f54) \
    BUSTER_X86_SSE_PACKED_FORM(X, ANDNPS, "andnps", 0, 2, 0x0f55) \
    BUSTER_X86_SSE_PACKED_FORM(X, ORPS, "orps", 0, 2, 0x0f56) \
    BUSTER_X86_SSE_PACKED_FORM(X, ADDPS, "addps", 0, 2, 0x0f58) \
    BUSTER_X86_SSE_PACKED_FORM(X, MULPS, "mulps", 0, 2, 0x0f59) \
    BUSTER_X86_SSE_PACKED_FORM(X, PXOR, "pxor", 0x66, 2, 0x0fef) \
    BUSTER_X86_SSE_PACKED_FORM(X, PAND, "pand", 0x66, 2, 0x0fdb) \
    BUSTER_X86_SSE_PACKED_FORM(X, PANDN, "pandn", 0x66, 2, 0x0fdf) \
    BUSTER_X86_SSE_PACKED_FORM(X, POR, "por", 0x66, 2, 0x0feb) \
    BUSTER_X86_SSE_PACKED_FORM(X, PADDB, "paddb", 0x66, 2, 0x0ffc) \
    BUSTER_X86_SSE_PACKED_FORM(X, PADDD, "paddd", 0x66, 2, 0x0ffe) \
    BUSTER_X86_SSE_PACKED_FORM(X, PADDQ, "paddq", 0x66, 2, 0x0fd4) \
    BUSTER_X86_SSE_PACKED_FORM(X, PSUBD, "psubd", 0x66, 2, 0x0ffa) \
    BUSTER_X86_SSE_PACKED_FORM(X, PCMPEQB, "pcmpeqb", 0x66, 2, 0x0f74) \
    BUSTER_X86_SSE_PACKED_FORM(X, PCMPEQD, "pcmpeqd", 0x66, 2, 0x0f76) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMINUB, "pminub", 0x66, 2, 0x0fda) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMAXUB, "pmaxub", 0x66, 2, 0x0fde) \
    BUSTER_X86_SSE_PACKED_FORM(X, PUNPCKLBW, "punpcklbw", 0x66, 2, 0x0f60) \
    BUSTER_X86_SSE_PACKED_FORM(X, PSHUFB, "pshufb", 0x66, 3, 0x0f3800) \
    BUSTER_X86_SSE_PACKED_FORM(X, PTEST, "ptest", 0x66, 3, 0x0f3817) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMINUD, "pminud", 0x66, 3, 0x0f383b) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMULLD, "pmulld", 0x66, 3, 0x0f3840) \
    X(PMOVMSKB_R32_X, "pmovmskb", 0x66, 2, 0x0fd7, R, 0, 0, 0, GD, V, 0, REG_RM) \
    X(MOVMSKPS_R32_X, "movmskps", 0, 2, 0x0f50, R, 0, 0, 0, GD, V, 0, REG_RM) \
    X(MOVD_X_RM32, "movd", 0x66, 2, 0x0f6e, R, 0, 0, 0, V, ED, 4, REG_RM) \
    X(MOVD_RM32_X, "movd", 0x66, 2, 0x0f7e, R, 0, 0, 0, V, ED, 4, RM_REG) \
    X(MOVQ_X_RM64, "movq", 0x66, 2, 0x0f6e, R, 0, 0, 1, V, EQ, 8, REG_RM) \
    X(MOVQ_RM64_X, "movq", 0x66, 2, 0x0f7e, R, 0, 0, 1, V, EQ, 8, RM_REG) \
    X(MOVQ_X_XM64, "movq", 0xf3, 2, 0x0f7e, R, 0, 0, 0, V, XM, 8, REG_RM) \
    X(MOVQ_XM64_X, "movq", 0x66, 2, 0x0fd6, R, 0, 0, 0, V, XM, 8, RM_REG) \
    X(PSHUFD_X_XM128_IMM8, "pshufd", 0x66, 2, 0x0f70, R, 1, 0, 0, V, XM, 16, REG_RM_IMM) \
    X(SHUFPS_X_XM128_IMM8, "shufps", 0, 2, 0x0fc6, R, 1, 0, 0, V, XM, 16, REG_RM_IMM) \
    X(PALIGNR_X_XM128_IMM8, "palignr", 0x66, 3, 0x0f3a0f, R, 1, 0, 0, V, XM, 16, REG_RM_IMM) \
    X(ROUNDSS_X_XM32_IMM8, "roundss", 0x66, 3, 0x0f3a0a, R, 1, 0, 0, V, XM, 4, REG_RM_IMM) \
    X(ROUNDSD_X_XM64_IMM8, "roundsd", 0x66, 3, 0x0f3a0b, R, 1, 0, 0, V, XM, 8, REG_RM_IMM) \
    X(PEXTRB_RM32_X_IMM8, "pextrb", 0x66, 3, 0x0f3a14, R, 1, 0, 0, V, ED, 1, RM_REG_IMM) \
    X(PEXTRD_RM32_X_IMM8, "pextrd", 0x66, 3, 0x0f3a16, R, 1, 0, 0, V, ED, 4, RM_REG_IMM) \
    X(PEXTRQ_RM64_X_IMM8, "pextrq", 0x66, 3, 0x0f3a16, R, 1, 0, 1, V, EQ, 8, RM_REG_IMM) \
    X(PINSRB_X_RM32_IMM8, "pinsrb", 0x66, 3, 0x0f3a20, R, 1, 0, 0, V, ED, 1, REG_RM_IMM) \
    X(PINSRD_X_RM32_IMM8, "pinsrd", 0x66, 3, 0x0f3a22, R, 1, 0, 0, V, ED, 4, REG_RM_IMM) \
    X(PINSRQ_X_RM64_IMM8, "pinsrq", 0x66, 3, 0x0f3a22, R, 1, 0, 1, V, EQ, 8, REG_RM_IMM) \
    /* The rest of SSE and SSE2: floating-point arithmetic, compares, shuffles and moves. */ \
    BUSTER_X86_SSE_PS_PD_FORMS(X, SUB, "sub", 0x5c) \
    BUSTER_X86_SSE_PS_PD_FORMS(X, MIN, "min", 0x5d) \
    BUSTER_X86_SSE_PS_PD_FORMS(X, DIV, "div", 0x5e) \
    BUSTER_X86_SSE_PS_PD_FORMS(X, MAX, "max", 0x5f) \
    BUSTER_X86_SSE_PS_PD_FORMS(X, SQRT, "sqrt", 0x51) \
    BUSTER_X86_SSE_PS_PD_FORMS(X, UNPCKL, "unpckl", 0x14) \
    BUSTER_X86_SSE_PS_PD_FORMS(X, UNPCKH, "unpckh", 0x15) \
    BUSTER_X86_SSE_PACKED_FORM(X, ADDPD, "addpd", 0x66, 2, 0x0f58) \
    BUSTER_X86_SSE_PACKED_FORM(X, MULPD, "mulpd", 0x66, 2, 0x0f59) \
    BUSTER_X86_SSE_PACKED_FORM(X, ANDNPD, "andnpd", 0x66, 2, 0x0f55) \
    BUSTER_X86_SSE_PACKED_FORM(X, ORPD, "orpd", 0x66, 2, 0x0f56) \
    BUSTER_X86_SSE_PACKED_FORM(X, RSQRTPS, "rsqrtps", 0, 2, 0x0f52) \
    BUSTER_X86_SSE_PACKED_FORM(X, RCPPS, "rcpps", 0, 2, 0x0f53) \
    X(RSQRTSS_X_XM32, "rsqrtss", 0xf3, 2, 0x0f52, R, 0, 0, 0, V, XM, 4, REG_RM) \
    X(RCPSS_X_XM32, "rcpss", 0xf3, 2, 0x0f53, R, 0, 0, 0, V, XM, 4, REG_RM) \
    BUSTER_X86_SSE_PACKED_IMM8_FORM(X, CMPPS, "cmpps", 0, 2, 0x0fc2) \
    BUSTER_X86_SSE_PACKED_IMM8_FORM(X, CMPPD, "cmppd", 0x66, 2, 0x0fc2) \
    X(CMPSS_X_XM32_IMM8, "cmpss", 0xf3, 2, 0x0fc2, R, 1, 0, 0, V, XM, 4, REG_RM_IMM) \
    X(CMPSD_X_XM64_IMM8, "cmpsd", 0xf2, 2, 0x0fc2, R, 1, 0, 0, V, XM, 8, REG_RM_IMM) \
    BUSTER_X86_SSE_PACKED_IMM8_FORM(X, SHUFPD, "shufpd", 0x66, 2, 0x0fc6) \
    BUSTER_X86_SSE_HALF_MOVE_FORMS(X, MOVLPS, "movlps", 0, 0x0f12) \
    BUSTER_X86_SSE_HALF_MOVE_FORMS(X, MOVHPS, "movhps", 0, 0x0f16) \
    BUSTER_X86_SSE_HALF_MOVE_FORMS(X, MOVLPD, "movlpd", 0x66, 0x0f12) \
    BUSTER_X86_SSE_HALF_MOVE_FORMS(X, MOVHPD, "movhpd", 0x66, 0x0f16) \
    X(MOVHLPS_X_X, "movhlps", 0, 2, 0x0f12, R, 0, 0, 0, V, V, 16, REG_RM) \
    X(MOVLHPS_X_X, "movlhps", 0, 2, 0x0f16, R, 0, 0, 0, V, V, 16, REG_RM) \
    X(MOVDDUP_X_XM64, "movddup", 0xf2, 2, 0x0f12, R, 0, 0, 0, V, XM, 8, REG_RM) \
    BUSTER_X86_SSE_PACKED_FORM(X, MOVSLDUP, "movsldup", 0xf3, 2, 0x0f12) \
    BUSTER_X86_SSE_PACKED_FORM(X, MOVSHDUP, "movshdup", 0xf3, 2, 0x0f16) \
    X(MOVMSKPD_R32_X, "movmskpd", 0x66, 2, 0x0f50, R, 0, 0, 0, GD, V, 0, REG_RM) \
    X(LDDQU_X_M128, "lddqu", 0xf2, 2, 0x0ff0, R, 0, 0, 0, V, M, 16, REG_RM) \
    X(MOVNTDQA_X_M128, "movntdqa", 0x66, 3, 0x0f382a, R, 0, 0, 0, V, M, 16, REG_RM) \
    X(MASKMOVDQU_X_X, "maskmovdqu", 0x66, 2, 0x0ff7, R, 0, 0, 0, V, V, 16, REG_RM) \
    /* Non-temporal stores. */ \
    X(MOVNTPS_M128_X, "movntps", 0, 2, 0x0f2b, R, 0, 0, 0, V, M, 16, RM_REG) \
    X(MOVNTPD_M128_X, "movntpd", 0x66, 2, 0x0f2b, R, 0, 0, 0, V, M, 16, RM_REG) \
    X(MOVNTDQ_M128_X, "movntdq", 0x66, 2, 0x0fe7, R, 0, 0, 0, V, M, 16, RM_REG) \
    X(MOVNTSS_M32_X, "movntss", 0xf3, 2, 0x0f2b, R, 0, 0, 0, V, M, 4, RM_REG) \
    X(MOVNTSD_M64_X, "movntsd", 0xf2, 2, 0x0f2b, R, 0, 0, 0, V, M, 8, RM_REG) \
    X(MOVNTI_M32_R32, "movnti", 0, 2, 0x0fc3, R, 0, 0, 0, GD, M, 4, RM_REG) \
    X(MOVNTI_M64_R64, "movnti", 0, 2, 0x0fc3, R, 0, 0, 1, GQ, M, 8, RM_REG) \
    /* Conversions (cvtpi2ps and cvtpi2pd from memory; their MMX register forms are with MMX's). */ \
    X(CVTSS2SI_R32_XM32, "cvtss2si", 0xf3, 2, 0x0f2d, R, 0, 0, 0, GD, XM, 4, REG_RM) \
    X(CVTSS2SI_R64_XM32, "cvtss2si", 0xf3, 2, 0x0f2d, R, 0, 0, 1, GQ, XM, 4, REG_RM) \
    X(CVTSD2SI_R32_XM64, "cvtsd2si", 0xf2, 2, 0x0f2d, R, 0, 0, 0, GD, XM, 8, REG_RM) \
    X(CVTSD2SI_R64_XM64, "cvtsd2si", 0xf2, 2, 0x0f2d, R, 0, 0, 1, GQ, XM, 8, REG_RM) \
    X(CVTPI2PS_X_M64, "cvtpi2ps", 0, 2, 0x0f2a, R, 0, 0, 0, V, M, 8, REG_RM) \
    X(CVTPI2PD_X_M64, "cvtpi2pd", 0x66, 2, 0x0f2a, R, 0, 0, 0, V, M, 8, REG_RM) \
    X(CVTPS2PD_X_XM64, "cvtps2pd", 0, 2, 0x0f5a, R, 0, 0, 0, V, XM, 8, REG_RM) \
    BUSTER_X86_SSE_PACKED_FORM(X, CVTPD2PS, "cvtpd2ps", 0x66, 2, 0x0f5a) \
    BUSTER_X86_SSE_PACKED_FORM(X, CVTDQ2PS, "cvtdq2ps", 0, 2, 0x0f5b) \
    BUSTER_X86_SSE_PACKED_FORM(X, CVTPS2DQ, "cvtps2dq", 0x66, 2, 0x0f5b) \
    BUSTER_X86_SSE_PACKED_FORM(X, CVTTPS2DQ, "cvttps2dq", 0xf3, 2, 0x0f5b) \
    X(CVTDQ2PD_X_XM64, "cvtdq2pd", 0xf3, 2, 0x0fe6, R, 0, 0, 0, V, XM, 8, REG_RM) \
    BUSTER_X86_SSE_PACKED_FORM(X, CVTPD2DQ, "cvtpd2dq", 0xf2, 2, 0x0fe6) \
    BUSTER_X86_SSE_PACKED_FORM(X, CVTTPD2DQ, "cvttpd2dq", 0x66, 2, 0x0fe6) \
    /* SSE2's integer arithmetic, compares, packs, unpacks, shifts and shuffles. */ \
    BUSTER_X86_SSE_PACKED_FORM(X, PADDW, "paddw", 0x66, 2, 0x0ffd) \
    BUSTER_X86_SSE_PACKED_FORM(X, PADDSB, "paddsb", 0x66, 2, 0x0fec) \
    BUSTER_X86_SSE_PACKED_FORM(X, PADDSW, "paddsw", 0x66, 2, 0x0fed) \
    BUSTER_X86_SSE_PACKED_FORM(X, PADDUSB, "paddusb", 0x66, 2, 0x0fdc) \
    BUSTER_X86_SSE_PACKED_FORM(X, PADDUSW, "paddusw", 0x66, 2, 0x0fdd) \
    BUSTER_X86_SSE_PACKED_FORM(X, PSUBB, "psubb", 0x66, 2, 0x0ff8) \
    BUSTER_X86_SSE_PACKED_FORM(X, PSUBW, "psubw", 0x66, 2, 0x0ff9) \
    BUSTER_X86_SSE_PACKED_FORM(X, PSUBQ, "psubq", 0x66, 2, 0x0ffb) \
    BUSTER_X86_SSE_PACKED_FORM(X, PSUBSB, "psubsb", 0x66, 2, 0x0fe8) \
    BUSTER_X86_SSE_PACKED_FORM(X, PSUBSW, "psubsw", 0x66, 2, 0x0fe9) \
    BUSTER_X86_SSE_PACKED_FORM(X, PSUBUSB, "psubusb", 0x66, 2, 0x0fd8) \
    BUSTER_X86_SSE_PACKED_FORM(X, PSUBUSW, "psubusw", 0x66, 2, 0x0fd9) \
    BUSTER_X86_SSE_PACKED_FORM(X, PAVGB, "pavgb", 0x66, 2, 0x0fe0) \
    BUSTER_X86_SSE_PACKED_FORM(X, PAVGW, "pavgw", 0x66, 2, 0x0fe3) \
    BUSTER_X86_SSE_PACKED_FORM(X, PCMPEQW, "pcmpeqw", 0x66, 2, 0x0f75) \
    BUSTER_X86_SSE_PACKED_FORM(X, PCMPGTB, "pcmpgtb", 0x66, 2, 0x0f64) \
    BUSTER_X86_SSE_PACKED_FORM(X, PCMPGTW, "pcmpgtw", 0x66, 2, 0x0f65) \
    BUSTER_X86_SSE_PACKED_FORM(X, PCMPGTD, "pcmpgtd", 0x66, 2, 0x0f66) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMADDWD, "pmaddwd", 0x66, 2, 0x0ff5) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMAXSW, "pmaxsw", 0x66, 2, 0x0fee) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMINSW, "pminsw", 0x66, 2, 0x0fea) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMULHUW, "pmulhuw", 0x66, 2, 0x0fe4) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMULHW, "pmulhw", 0x66, 2, 0x0fe5) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMULLW, "pmullw", 0x66, 2, 0x0fd5) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMULUDQ, "pmuludq", 0x66, 2, 0x0ff4) \
    BUSTER_X86_SSE_PACKED_FORM(X, PSADBW, "psadbw", 0x66, 2, 0x0ff6) \
    BUSTER_X86_SSE_PACKED_FORM(X, PACKSSWB, "packsswb", 0x66, 2, 0x0f63) \
    BUSTER_X86_SSE_PACKED_FORM(X, PACKUSWB, "packuswb", 0x66, 2, 0x0f67) \
    BUSTER_X86_SSE_PACKED_FORM(X, PACKSSDW, "packssdw", 0x66, 2, 0x0f6b) \
    BUSTER_X86_SSE_PACKED_FORM(X, PUNPCKLWD, "punpcklwd", 0x66, 2, 0x0f61) \
    BUSTER_X86_SSE_PACKED_FORM(X, PUNPCKLDQ, "punpckldq", 0x66, 2, 0x0f62) \
    BUSTER_X86_SSE_PACKED_FORM(X, PUNPCKHBW, "punpckhbw", 0x66, 2, 0x0f68) \
    BUSTER_X86_SSE_PACKED_FORM(X, PUNPCKHWD, "punpckhwd", 0x66, 2, 0x0f69) \
    BUSTER_X86_SSE_PACKED_FORM(X, PUNPCKHDQ, "punpckhdq", 0x66, 2, 0x0f6a) \
    BUSTER_X86_SSE_PACKED_FORM(X, PUNPCKLQDQ, "punpcklqdq", 0x66, 2, 0x0f6c) \
    BUSTER_X86_SSE_PACKED_FORM(X, PUNPCKHQDQ, "punpckhqdq", 0x66, 2, 0x0f6d) \
    BUSTER_X86_SSE_SHIFT_FORMS(X, PSRLW, "psrlw", 0x0fd1, 0x0f71, 2) \
    BUSTER_X86_SSE_SHIFT_FORMS(X, PSRAW, "psraw", 0x0fe1, 0x0f71, 4) \
    BUSTER_X86_SSE_SHIFT_FORMS(X, PSLLW, "psllw", 0x0ff1, 0x0f71, 6) \
    BUSTER_X86_SSE_SHIFT_FORMS(X, PSRLD, "psrld", 0x0fd2, 0x0f72, 2) \
    BUSTER_X86_SSE_SHIFT_FORMS(X, PSRAD, "psrad", 0x0fe2, 0x0f72, 4) \
    BUSTER_X86_SSE_SHIFT_FORMS(X, PSLLD, "pslld", 0x0ff2, 0x0f72, 6) \
    BUSTER_X86_SSE_SHIFT_FORMS(X, PSRLQ, "psrlq", 0x0fd3, 0x0f73, 2) \
    BUSTER_X86_SSE_SHIFT_FORMS(X, PSLLQ, "psllq", 0x0ff3, 0x0f73, 6) \
    X(PSRLDQ_X_IMM8, "psrldq", 0x66, 2, 0x0f73, D3, 1, 0, 0, NONE, V, 16, RM_IMM) \
    X(PSLLDQ_X_IMM8, "pslldq", 0x66, 2, 0x0f73, D7, 1, 0, 0, NONE, V, 16, RM_IMM) \
    X(PSHUFHW_X_XM128_IMM8, "pshufhw", 0xf3, 2, 0x0f70, R, 1, 0, 0, V, XM, 16, REG_RM_IMM) \
    X(PSHUFLW_X_XM128_IMM8, "pshuflw", 0xf2, 2, 0x0f70, R, 1, 0, 0, V, XM, 16, REG_RM_IMM) \
    X(PINSRW_X_RM32_IMM8, "pinsrw", 0x66, 2, 0x0fc4, R, 1, 0, 0, V, ED, 2, REG_RM_IMM) \
    X(PEXTRW_R32_X_IMM8, "pextrw", 0x66, 2, 0x0fc5, R, 1, 0, 0, GD, V, 0, REG_RM_IMM) \
    X(PEXTRW_M16_X_IMM8, "pextrw", 0x66, 3, 0x0f3a15, R, 1, 0, 0, V, M, 2, RM_REG_IMM) \
    /* SSE3 and SSSE3. */ \
    BUSTER_X86_SSE_PACKED_FORM(X, ADDSUBPD, "addsubpd", 0x66, 2, 0x0fd0) \
    BUSTER_X86_SSE_PACKED_FORM(X, ADDSUBPS, "addsubps", 0xf2, 2, 0x0fd0) \
    BUSTER_X86_SSE_PACKED_FORM(X, HADDPD, "haddpd", 0x66, 2, 0x0f7c) \
    BUSTER_X86_SSE_PACKED_FORM(X, HADDPS, "haddps", 0xf2, 2, 0x0f7c) \
    BUSTER_X86_SSE_PACKED_FORM(X, HSUBPD, "hsubpd", 0x66, 2, 0x0f7d) \
    BUSTER_X86_SSE_PACKED_FORM(X, HSUBPS, "hsubps", 0xf2, 2, 0x0f7d) \
    BUSTER_X86_SSE_PACKED_FORM(X, PHADDW, "phaddw", 0x66, 3, 0x0f3801) \
    BUSTER_X86_SSE_PACKED_FORM(X, PHADDD, "phaddd", 0x66, 3, 0x0f3802) \
    BUSTER_X86_SSE_PACKED_FORM(X, PHADDSW, "phaddsw", 0x66, 3, 0x0f3803) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMADDUBSW, "pmaddubsw", 0x66, 3, 0x0f3804) \
    BUSTER_X86_SSE_PACKED_FORM(X, PHSUBW, "phsubw", 0x66, 3, 0x0f3805) \
    BUSTER_X86_SSE_PACKED_FORM(X, PHSUBD, "phsubd", 0x66, 3, 0x0f3806) \
    BUSTER_X86_SSE_PACKED_FORM(X, PHSUBSW, "phsubsw", 0x66, 3, 0x0f3807) \
    BUSTER_X86_SSE_PACKED_FORM(X, PSIGNB, "psignb", 0x66, 3, 0x0f3808) \
    BUSTER_X86_SSE_PACKED_FORM(X, PSIGNW, "psignw", 0x66, 3, 0x0f3809) \
    BUSTER_X86_SSE_PACKED_FORM(X, PSIGND, "psignd", 0x66, 3, 0x0f380a) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMULHRSW, "pmulhrsw", 0x66, 3, 0x0f380b) \
    BUSTER_X86_SSE_PACKED_FORM(X, PABSB, "pabsb", 0x66, 3, 0x0f381c) \
    BUSTER_X86_SSE_PACKED_FORM(X, PABSW, "pabsw", 0x66, 3, 0x0f381d) \
    BUSTER_X86_SSE_PACKED_FORM(X, PABSD, "pabsd", 0x66, 3, 0x0f381e) \
    /* SSE4.1 and SSE4.2. */ \
    BUSTER_X86_SSE_EXTEND_FORMS(X, PMOVSX, "pmovsx", 0x20) \
    BUSTER_X86_SSE_EXTEND_FORMS(X, PMOVZX, "pmovzx", 0x30) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMULDQ, "pmuldq", 0x66, 3, 0x0f3828) \
    BUSTER_X86_SSE_PACKED_FORM(X, PCMPEQQ, "pcmpeqq", 0x66, 3, 0x0f3829) \
    BUSTER_X86_SSE_PACKED_FORM(X, PACKUSDW, "packusdw", 0x66, 3, 0x0f382b) \
    BUSTER_X86_SSE_PACKED_FORM(X, PCMPGTQ, "pcmpgtq", 0x66, 3, 0x0f3837) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMINSB, "pminsb", 0x66, 3, 0x0f3838) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMINSD, "pminsd", 0x66, 3, 0x0f3839) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMINUW, "pminuw", 0x66, 3, 0x0f383a) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMAXSB, "pmaxsb", 0x66, 3, 0x0f383c) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMAXSD, "pmaxsd", 0x66, 3, 0x0f383d) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMAXUW, "pmaxuw", 0x66, 3, 0x0f383e) \
    BUSTER_X86_SSE_PACKED_FORM(X, PMAXUD, "pmaxud", 0x66, 3, 0x0f383f) \
    BUSTER_X86_SSE_PACKED_FORM(X, PHMINPOSUW, "phminposuw", 0x66, 3, 0x0f3841) \
    X(PBLENDVB_X_XM128_XMM0, "pblendvb", 0x66, 3, 0x0f3810, R, 0, 0, 0, V, XM, 16, REG_RM_XMM0) \
    X(BLENDVPS_X_XM128_XMM0, "blendvps", 0x66, 3, 0x0f3814, R, 0, 0, 0, V, XM, 16, REG_RM_XMM0) \
    X(BLENDVPD_X_XM128_XMM0, "blendvpd", 0x66, 3, 0x0f3815, R, 0, 0, 0, V, XM, 16, REG_RM_XMM0) \
    BUSTER_X86_SSE_PACKED_IMM8_FORM(X, ROUNDPS, "roundps", 0x66, 3, 0x0f3a08) \
    BUSTER_X86_SSE_PACKED_IMM8_FORM(X, ROUNDPD, "roundpd", 0x66, 3, 0x0f3a09) \
    BUSTER_X86_SSE_PACKED_IMM8_FORM(X, BLENDPS, "blendps", 0x66, 3, 0x0f3a0c) \
    BUSTER_X86_SSE_PACKED_IMM8_FORM(X, BLENDPD, "blendpd", 0x66, 3, 0x0f3a0d) \
    BUSTER_X86_SSE_PACKED_IMM8_FORM(X, PBLENDW, "pblendw", 0x66, 3, 0x0f3a0e) \
    X(EXTRACTPS_RM32_X_IMM8, "extractps", 0x66, 3, 0x0f3a17, R, 1, 0, 0, V, ED, 4, RM_REG_IMM) \
    X(INSERTPS_X_XM32_IMM8, "insertps", 0x66, 3, 0x0f3a21, R, 1, 0, 0, V, XM, 4, REG_RM_IMM) \
    BUSTER_X86_SSE_PACKED_IMM8_FORM(X, DPPS, "dpps", 0x66, 3, 0x0f3a40) \
    BUSTER_X86_SSE_PACKED_IMM8_FORM(X, DPPD, "dppd", 0x66, 3, 0x0f3a41) \
    BUSTER_X86_SSE_PACKED_IMM8_FORM(X, MPSADBW, "mpsadbw", 0x66, 3, 0x0f3a42) \
    BUSTER_X86_SSE_PACKED_IMM8_FORM(X, PCMPESTRM, "pcmpestrm", 0x66, 3, 0x0f3a60) \
    BUSTER_X86_SSE_PACKED_IMM8_FORM(X, PCMPESTRI, "pcmpestri", 0x66, 3, 0x0f3a61) \
    BUSTER_X86_SSE_PACKED_IMM8_FORM(X, PCMPISTRM, "pcmpistrm", 0x66, 3, 0x0f3a62) \
    BUSTER_X86_SSE_PACKED_IMM8_FORM(X, PCMPISTRI, "pcmpistri", 0x66, 3, 0x0f3a63) \
    /* The explicit-length string compares with REX.W: 64-bit lengths in RAX and RDX. */ \
    X(PCMPESTRMQ_X_XM128_IMM8, "pcmpestrmq", 0x66, 3, 0x0f3a60, R, 1, 0, 1, V, XM, 16, REG_RM_IMM) \
    X(PCMPESTRIQ_X_XM128_IMM8, "pcmpestriq", 0x66, 3, 0x0f3a61, R, 1, 0, 1, V, XM, 16, REG_RM_IMM) \
    /* AMD's SSE4a. */ \
    X(EXTRQ_X_X, "extrq", 0x66, 2, 0x0f79, R, 0, 0, 0, V, V, 16, REG_RM) \
    X(EXTRQ_X_IMM8_IMM8, "extrq", 0x66, 2, 0x0f78, D0, 2, 0, 0, NONE, V, 16, RM_IMM8_IMM8) \
    X(INSERTQ_X_X, "insertq", 0xf2, 2, 0x0f79, R, 0, 0, 0, V, V, 16, REG_RM) \
    X(INSERTQ_X_X_IMM8_IMM8, "insertq", 0xf2, 2, 0x0f78, R, 2, 0, 0, V, V, 16, REG_RM_IMM8_IMM8) \
    /* AES, carry-less multiplication, SHA (sha256rnds2 also reads XMM0) and GFNI. */ \
    BUSTER_X86_SSE_PACKED_FORM(X, AESIMC, "aesimc", 0x66, 3, 0x0f38db) \
    BUSTER_X86_SSE_PACKED_FORM(X, AESENC, "aesenc", 0x66, 3, 0x0f38dc) \
    BUSTER_X86_SSE_PACKED_FORM(X, AESENCLAST, "aesenclast", 0x66, 3, 0x0f38dd) \
    BUSTER_X86_SSE_PACKED_FORM(X, AESDEC, "aesdec", 0x66, 3, 0x0f38de) \
    BUSTER_X86_SSE_PACKED_FORM(X, AESDECLAST, "aesdeclast", 0x66, 3, 0x0f38df) \
    BUSTER_X86_SSE_PACKED_IMM8_FORM(X, AESKEYGENASSIST, "aeskeygenassist", 0x66, 3, 0x0f3adf) \
    BUSTER_X86_SSE_PACKED_IMM8_FORM(X, PCLMULQDQ, "pclmulqdq", 0x66, 3, 0x0f3a44) \
    BUSTER_X86_SSE_PACKED_FORM(X, SHA1NEXTE, "sha1nexte", 0, 3, 0x0f38c8) \
    BUSTER_X86_SSE_PACKED_FORM(X, SHA1MSG1, "sha1msg1", 0, 3, 0x0f38c9) \
    BUSTER_X86_SSE_PACKED_FORM(X, SHA1MSG2, "sha1msg2", 0, 3, 0x0f38ca) \
    BUSTER_X86_SSE_PACKED_IMM8_FORM(X, SHA1RNDS4, "sha1rnds4", 0, 3, 0x0f3acc) \
    X(SHA256RNDS2_X_XM128_XMM0, "sha256rnds2", 0, 3, 0x0f38cb, R, 0, 0, 0, V, XM, 16, REG_RM_XMM0) \
    BUSTER_X86_SSE_PACKED_FORM(X, SHA256MSG1, "sha256msg1", 0, 3, 0x0f38cc) \
    BUSTER_X86_SSE_PACKED_FORM(X, SHA256MSG2, "sha256msg2", 0, 3, 0x0f38cd) \
    BUSTER_X86_SSE_PACKED_FORM(X, GF2P8MULB, "gf2p8mulb", 0x66, 3, 0x0f38cf) \
    BUSTER_X86_SSE_PACKED_IMM8_FORM(X, GF2P8AFFINEQB, "gf2p8affineqb", 0x66, 3, 0x0f3ace) \
    BUSTER_X86_SSE_PACKED_IMM8_FORM(X, GF2P8AFFINEINVQB, "gf2p8affineinvqb", 0x66, 3, 0x0f3acf) \
    /* Key Locker: a handle of 384 or 512 bits in memory, which GNU as takes without a size. */ \
    X(AESENC128KL_X_M384, "aesenc128kl", 0xf3, 3, 0x0f38dc, R, 0, 0, 0, V, M, 48, REG_RM) \
    X(AESDEC128KL_X_M384, "aesdec128kl", 0xf3, 3, 0x0f38dd, R, 0, 0, 0, V, M, 48, REG_RM) \
    X(AESENC256KL_X_M512, "aesenc256kl", 0xf3, 3, 0x0f38de, R, 0, 0, 0, V, M, 0, REG_RM) \
    X(AESDEC256KL_X_M512, "aesdec256kl", 0xf3, 3, 0x0f38df, R, 0, 0, 0, V, M, 0, REG_RM) \
    X(AESENCWIDE128KL_M384, "aesencwide128kl", 0xf3, 3, 0x0f38d8, D0, 0, 0, 0, NONE, M, 48, RM) \
    X(AESDECWIDE128KL_M384, "aesdecwide128kl", 0xf3, 3, 0x0f38d8, D1, 0, 0, 0, NONE, M, 48, RM) \
    X(AESENCWIDE256KL_M512, "aesencwide256kl", 0xf3, 3, 0x0f38d8, D2, 0, 0, 0, NONE, M, 0, RM) \
    X(AESDECWIDE256KL_M512, "aesdecwide256kl", 0xf3, 3, 0x0f38d8, D3, 0, 0, 0, NONE, M, 0, RM) \
    X(ENCODEKEY128_R32_R32, "encodekey128", 0xf3, 3, 0x0f38fa, R, 0, 0, 0, GD, GD, 4, REG_RM) \
    X(ENCODEKEY256_R32_R32, "encodekey256", 0xf3, 3, 0x0f38fb, R, 0, 0, 0, GD, GD, 4, REG_RM) \
    X(LOADIWKEY_X_X, "loadiwkey", 0xf3, 3, 0x0f38dc, R, 0, 0, 0, V, V, 16, REG_RM) \
    /* VIA PadLock: the ModRM byte is part of the opcode, and the operands are implicit. */ \
    X(XSTORE, "xstore", 0, 3, 0x0fa7c0, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(REP_XSTORE, "rep xstore", 0xf3, 3, 0x0fa7c0, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(REP_XCRYPTECB, "rep xcryptecb", 0xf3, 3, 0x0fa7c8, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(REP_XCRYPTCBC, "rep xcryptcbc", 0xf3, 3, 0x0fa7d0, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(REP_XCRYPTCTR, "rep xcryptctr", 0xf3, 3, 0x0fa7d8, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(REP_XCRYPTCFB, "rep xcryptcfb", 0xf3, 3, 0x0fa7e0, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(REP_XCRYPTOFB, "rep xcryptofb", 0xf3, 3, 0x0fa7e8, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(REP_XSHA1, "rep xsha1", 0xf3, 3, 0x0fa6c8, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(REP_XSHA256, "rep xsha256", 0xf3, 3, 0x0fa6d0, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    /* Zhaoxin's PadLock and GMI instructions, which GNU as names without their F3 (or sm2's F2). */ \
    X(XRNG2, "xrng2", 0xf3, 3, 0x0fa7f8, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(XSHA384, "xsha384", 0xf3, 3, 0x0fa6d8, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(XSHA512, "xsha512", 0xf3, 3, 0x0fa6e0, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(MONTMUL2, "montmul2", 0xf3, 3, 0x0fa6f0, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(XMODEXP, "xmodexp", 0xf3, 3, 0x0fa6f8, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(SM2, "sm2", 0xf2, 3, 0x0fa6c0, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(SM3, "sm3", 0xf3, 3, 0x0fa6e8, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(SM4, "sm4", 0xf3, 3, 0x0fa7f0, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    /* MMX, and the MMX register forms of SSE, SSE2 and SSSE3. */ \
    X(EMMS, "emms", 0, 2, 0x0f77, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(MOVD_MM_RM32, "movd", 0, 2, 0x0f6e, R, 0, 0, 0, MM, ED, 4, REG_RM) \
    X(MOVD_RM32_MM, "movd", 0, 2, 0x0f7e, R, 0, 0, 0, MM, ED, 4, RM_REG) \
    X(MOVQ_MM_RM64, "movq", 0, 2, 0x0f6e, R, 0, 0, 1, MM, EQ, 8, REG_RM) \
    X(MOVQ_RM64_MM, "movq", 0, 2, 0x0f7e, R, 0, 0, 1, MM, EQ, 8, RM_REG) \
    X(MOVQ_MM_MM, "movq", 0, 2, 0x0f6f, R, 0, 0, 0, MM, MM, 8, REG_RM) \
    X(MOVQ_MM_M64, "movq", 0, 2, 0x0f6f, R, 0, 0, 0, MM, M, 8, REG_RM) \
    X(MOVQ_M64_MM, "movq", 0, 2, 0x0f7f, R, 0, 0, 0, MM, M, 8, RM_REG) \
    /* The store's opcode between registers: ModRM.rm is the destination. */ \
    X(MOVQ_MM_MM_STORE, "movq", 0, 2, 0x0f7f, R, 0, 0, 0, MM, MM, 8, RM_REG) \
    X(MOVNTQ_M64_MM, "movntq", 0, 2, 0x0fe7, R, 0, 0, 0, MM, M, 8, RM_REG) \
    X(MASKMOVQ_MM_MM, "maskmovq", 0, 2, 0x0ff7, R, 0, 0, 0, MM, MM, 8, REG_RM) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PXOR, "pxor", 2, 0x0fef) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PAND, "pand", 2, 0x0fdb) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PANDN, "pandn", 2, 0x0fdf) \
    BUSTER_X86_MMX_PACKED_FORMS(X, POR, "por", 2, 0x0feb) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PADDB, "paddb", 2, 0x0ffc) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PADDW, "paddw", 2, 0x0ffd) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PADDD, "paddd", 2, 0x0ffe) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PADDQ, "paddq", 2, 0x0fd4) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PADDSB, "paddsb", 2, 0x0fec) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PADDSW, "paddsw", 2, 0x0fed) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PADDUSB, "paddusb", 2, 0x0fdc) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PADDUSW, "paddusw", 2, 0x0fdd) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PSUBB, "psubb", 2, 0x0ff8) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PSUBW, "psubw", 2, 0x0ff9) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PSUBD, "psubd", 2, 0x0ffa) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PSUBQ, "psubq", 2, 0x0ffb) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PSUBSB, "psubsb", 2, 0x0fe8) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PSUBSW, "psubsw", 2, 0x0fe9) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PSUBUSB, "psubusb", 2, 0x0fd8) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PSUBUSW, "psubusw", 2, 0x0fd9) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PAVGB, "pavgb", 2, 0x0fe0) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PAVGW, "pavgw", 2, 0x0fe3) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PCMPEQB, "pcmpeqb", 2, 0x0f74) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PCMPEQW, "pcmpeqw", 2, 0x0f75) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PCMPEQD, "pcmpeqd", 2, 0x0f76) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PCMPGTB, "pcmpgtb", 2, 0x0f64) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PCMPGTW, "pcmpgtw", 2, 0x0f65) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PCMPGTD, "pcmpgtd", 2, 0x0f66) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PMADDWD, "pmaddwd", 2, 0x0ff5) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PMAXSW, "pmaxsw", 2, 0x0fee) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PMAXUB, "pmaxub", 2, 0x0fde) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PMINSW, "pminsw", 2, 0x0fea) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PMINUB, "pminub", 2, 0x0fda) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PMULHUW, "pmulhuw", 2, 0x0fe4) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PMULHW, "pmulhw", 2, 0x0fe5) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PMULLW, "pmullw", 2, 0x0fd5) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PMULUDQ, "pmuludq", 2, 0x0ff4) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PSADBW, "psadbw", 2, 0x0ff6) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PACKSSWB, "packsswb", 2, 0x0f63) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PACKUSWB, "packuswb", 2, 0x0f67) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PACKSSDW, "packssdw", 2, 0x0f6b) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PUNPCKHBW, "punpckhbw", 2, 0x0f68) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PUNPCKHWD, "punpckhwd", 2, 0x0f69) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PUNPCKHDQ, "punpckhdq", 2, 0x0f6a) \
    BUSTER_X86_MMX_UNPACK_LOW_FORMS(X, PUNPCKLBW, "punpcklbw", 0x0f60) \
    BUSTER_X86_MMX_UNPACK_LOW_FORMS(X, PUNPCKLWD, "punpcklwd", 0x0f61) \
    BUSTER_X86_MMX_UNPACK_LOW_FORMS(X, PUNPCKLDQ, "punpckldq", 0x0f62) \
    BUSTER_X86_MMX_SHIFT_FORMS(X, PSRLW, "psrlw", 0x0fd1, 0x0f71, 2) \
    BUSTER_X86_MMX_SHIFT_FORMS(X, PSRAW, "psraw", 0x0fe1, 0x0f71, 4) \
    BUSTER_X86_MMX_SHIFT_FORMS(X, PSLLW, "psllw", 0x0ff1, 0x0f71, 6) \
    BUSTER_X86_MMX_SHIFT_FORMS(X, PSRLD, "psrld", 0x0fd2, 0x0f72, 2) \
    BUSTER_X86_MMX_SHIFT_FORMS(X, PSRAD, "psrad", 0x0fe2, 0x0f72, 4) \
    BUSTER_X86_MMX_SHIFT_FORMS(X, PSLLD, "pslld", 0x0ff2, 0x0f72, 6) \
    BUSTER_X86_MMX_SHIFT_FORMS(X, PSRLQ, "psrlq", 0x0fd3, 0x0f73, 2) \
    BUSTER_X86_MMX_SHIFT_FORMS(X, PSLLQ, "psllq", 0x0ff3, 0x0f73, 6) \
    BUSTER_X86_MMX_PACKED_IMM8_FORMS(X, PSHUFW, "pshufw", 2, 0x0f70) \
    X(PINSRW_MM_RM32_IMM8, "pinsrw", 0, 2, 0x0fc4, R, 1, 0, 0, MM, ED, 2, REG_RM_IMM) \
    X(PEXTRW_R32_MM_IMM8, "pextrw", 0, 2, 0x0fc5, R, 1, 0, 0, GD, MM, 0, REG_RM_IMM) \
    X(PMOVMSKB_R32_MM, "pmovmskb", 0, 2, 0x0fd7, R, 0, 0, 0, GD, MM, 0, REG_RM) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PSHUFB, "pshufb", 3, 0x0f3800) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PHADDW, "phaddw", 3, 0x0f3801) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PHADDD, "phaddd", 3, 0x0f3802) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PHADDSW, "phaddsw", 3, 0x0f3803) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PMADDUBSW, "pmaddubsw", 3, 0x0f3804) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PHSUBW, "phsubw", 3, 0x0f3805) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PHSUBD, "phsubd", 3, 0x0f3806) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PHSUBSW, "phsubsw", 3, 0x0f3807) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PSIGNB, "psignb", 3, 0x0f3808) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PSIGNW, "psignw", 3, 0x0f3809) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PSIGND, "psignd", 3, 0x0f380a) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PMULHRSW, "pmulhrsw", 3, 0x0f380b) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PABSB, "pabsb", 3, 0x0f381c) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PABSW, "pabsw", 3, 0x0f381d) \
    BUSTER_X86_MMX_PACKED_FORMS(X, PABSD, "pabsd", 3, 0x0f381e) \
    BUSTER_X86_MMX_PACKED_IMM8_FORMS(X, PALIGNR, "palignr", 3, 0x0f3a0f) \
    /* 3DNow! and its extensions (pf2iw, pfnacc, pfpnacc, pi2fw and pswapd), and femms. */ \
    X(FEMMS, "femms", 0, 2, 0x0f0e, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    BUSTER_X86_3DNOW_FORMS(X, PAVGUSB, "pavgusb", 0xbf) \
    BUSTER_X86_3DNOW_FORMS(X, PF2ID, "pf2id", 0x1d) \
    BUSTER_X86_3DNOW_FORMS(X, PF2IW, "pf2iw", 0x1c) \
    BUSTER_X86_3DNOW_FORMS(X, PFACC, "pfacc", 0xae) \
    BUSTER_X86_3DNOW_FORMS(X, PFADD, "pfadd", 0x9e) \
    BUSTER_X86_3DNOW_FORMS(X, PFCMPEQ, "pfcmpeq", 0xb0) \
    BUSTER_X86_3DNOW_FORMS(X, PFCMPGE, "pfcmpge", 0x90) \
    BUSTER_X86_3DNOW_FORMS(X, PFCMPGT, "pfcmpgt", 0xa0) \
    BUSTER_X86_3DNOW_FORMS(X, PFMAX, "pfmax", 0xa4) \
    BUSTER_X86_3DNOW_FORMS(X, PFMIN, "pfmin", 0x94) \
    BUSTER_X86_3DNOW_FORMS(X, PFMUL, "pfmul", 0xb4) \
    BUSTER_X86_3DNOW_FORMS(X, PFNACC, "pfnacc", 0x8a) \
    BUSTER_X86_3DNOW_FORMS(X, PFPNACC, "pfpnacc", 0x8e) \
    BUSTER_X86_3DNOW_FORMS(X, PFRCP, "pfrcp", 0x96) \
    BUSTER_X86_3DNOW_FORMS(X, PFRCPIT1, "pfrcpit1", 0xa6) \
    BUSTER_X86_3DNOW_FORMS(X, PFRCPIT2, "pfrcpit2", 0xb6) \
    BUSTER_X86_3DNOW_FORMS(X, PFRSQIT1, "pfrsqit1", 0xa7) \
    BUSTER_X86_3DNOW_FORMS(X, PFRSQRT, "pfrsqrt", 0x97) \
    BUSTER_X86_3DNOW_FORMS(X, PFSUB, "pfsub", 0x9a) \
    BUSTER_X86_3DNOW_FORMS(X, PFSUBR, "pfsubr", 0xaa) \
    BUSTER_X86_3DNOW_FORMS(X, PI2FD, "pi2fd", 0x0d) \
    BUSTER_X86_3DNOW_FORMS(X, PI2FW, "pi2fw", 0x0c) \
    BUSTER_X86_3DNOW_FORMS(X, PMULHRW, "pmulhrw", 0xb7) \
    BUSTER_X86_3DNOW_FORMS(X, PSWAPD, "pswapd", 0xbb) \
    /* MPX: bounds from an address (bndmk's, whose base is the lower bound, so it's never RIP-relative), checks of an address (a general register or memory) against them, their moves, and their loads and stores in the bound tables through a MIB operand (whose index is the pointer they look up). */ \
    X(BNDMK_BND_M64, "bndmk", 0xf3, 2, 0x0f1b, R, 0, 0, 0, BND, M_NO_RIP, 8, REG_RM) \
    X(BNDCL_BND_RM64, "bndcl", 0xf3, 2, 0x0f1a, R, 0, 0, 0, BND, EQ, 8, REG_RM) \
    X(BNDCU_BND_RM64, "bndcu", 0xf2, 2, 0x0f1a, R, 0, 0, 0, BND, EQ, 8, REG_RM) \
    X(BNDCN_BND_RM64, "bndcn", 0xf2, 2, 0x0f1b, R, 0, 0, 0, BND, EQ, 8, REG_RM) \
    X(BNDMOV_BND_BND, "bndmov", 0x66, 2, 0x0f1a, R, 0, 0, 0, BND, BND, 16, REG_RM) \
    X(BNDMOV_BND_M128, "bndmov", 0x66, 2, 0x0f1a, R, 0, 0, 0, BND, M, 16, REG_RM) \
    X(BNDMOV_M128_BND, "bndmov", 0x66, 2, 0x0f1b, R, 0, 0, 0, BND, M, 16, RM_REG) \
    X(BNDLDX_BND_MIB, "bndldx", 0, 2, 0x0f1a, R, 0, 0, 0, BND, MIB, 0, REG_RM) \
    X(BNDSTX_MIB_BND, "bndstx", 0, 2, 0x0f1b, R, 0, 0, 0, BND, MIB, 0, RM_REG) \
    /* Between MMX and XMM registers: two doublewords to and from floats, and moves of a quadword. */ \
    X(CVTPI2PS_X_MM, "cvtpi2ps", 0, 2, 0x0f2a, R, 0, 0, 0, V, MM, 8, REG_RM) \
    X(CVTPI2PD_X_MM, "cvtpi2pd", 0x66, 2, 0x0f2a, R, 0, 0, 0, V, MM, 8, REG_RM) \
    X(CVTPS2PI_MM_XM64, "cvtps2pi", 0, 2, 0x0f2d, R, 0, 0, 0, MM, XM, 8, REG_RM) \
    X(CVTTPS2PI_MM_XM64, "cvttps2pi", 0, 2, 0x0f2c, R, 0, 0, 0, MM, XM, 8, REG_RM) \
    X(CVTPD2PI_MM_XM128, "cvtpd2pi", 0x66, 2, 0x0f2d, R, 0, 0, 0, MM, XM, 16, REG_RM) \
    X(CVTTPD2PI_MM_XM128, "cvttpd2pi", 0x66, 2, 0x0f2c, R, 0, 0, 0, MM, XM, 16, REG_RM) \
    X(MOVDQ2Q_MM_X, "movdq2q", 0xf2, 2, 0x0fd6, R, 0, 0, 0, MM, V, 8, REG_RM) \
    X(MOVQ2DQ_X_MM, "movq2dq", 0xf3, 2, 0x0fd6, R, 0, 0, 0, V, MM, 8, REG_RM) \
    /* MXCSR, and the x87, MMX and SSE state (512 bytes). */ \
    X(LDMXCSR_M32, "ldmxcsr", 0, 2, 0x0fae, D2, 0, 0, 0, NONE, M, 4, RM) \
    X(STMXCSR_M32, "stmxcsr", 0, 2, 0x0fae, D3, 0, 0, 0, NONE, M, 4, RM) \
    X(FXSAVE_M, "fxsave", 0, 2, 0x0fae, D0, 0, 0, 0, NONE, M, 0, RM) \
    X(FXRSTOR_M, "fxrstor", 0, 2, 0x0fae, D1, 0, 0, 0, NONE, M, 0, RM) \
    X(FXSAVE64_M, "fxsave64", 0, 2, 0x0fae, D0, 0, 0, 1, NONE, M, 0, RM) \
    X(FXRSTOR64_M, "fxrstor64", 0, 2, 0x0fae, D1, 0, 0, 1, NONE, M, 0, RM) \
    BUSTER_X86_STRING_FORMS(X, MOVS, "movs", 0, 0xa4, 0xa5) \
    BUSTER_X86_STRING_FORMS(X, REP_MOVS, "rep movs", 0xf3, 0xa4, 0xa5) \
    BUSTER_X86_STRING_FORMS(X, REPNE_MOVS, "repne movs", 0xf2, 0xa4, 0xa5) \
    BUSTER_X86_STRING_FORMS(X, STOS, "stos", 0, 0xaa, 0xab) \
    BUSTER_X86_STRING_FORMS(X, REP_STOS, "rep stos", 0xf3, 0xaa, 0xab) \
    BUSTER_X86_STRING_FORMS(X, REPNE_STOS, "repne stos", 0xf2, 0xaa, 0xab) \
    BUSTER_X86_STRING_FORMS(X, LODS, "lods", 0, 0xac, 0xad) \
    BUSTER_X86_STRING_FORMS(X, REP_LODS, "rep lods", 0xf3, 0xac, 0xad) \
    BUSTER_X86_STRING_FORMS(X, REPNE_LODS, "repne lods", 0xf2, 0xac, 0xad) \
    BUSTER_X86_STRING_FORMS(X, CMPS, "cmps", 0, 0xa6, 0xa7) \
    BUSTER_X86_STRING_FORMS(X, REPE_CMPS, "repe cmps", 0xf3, 0xa6, 0xa7) \
    BUSTER_X86_STRING_FORMS(X, REPNE_CMPS, "repne cmps", 0xf2, 0xa6, 0xa7) \
    BUSTER_X86_STRING_FORMS(X, SCAS, "scas", 0, 0xae, 0xaf) \
    BUSTER_X86_STRING_FORMS(X, REPE_SCAS, "repe scas", 0xf3, 0xae, 0xaf) \
    BUSTER_X86_STRING_FORMS(X, REPNE_SCAS, "repne scas", 0xf2, 0xae, 0xaf) \
    X(XLATB, "xlatb", 0, 1, 0xd7, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(CLC, "clc", 0, 1, 0xf8, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(STC, "stc", 0, 1, 0xf9, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(CMC, "cmc", 0, 1, 0xf5, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(CLD, "cld", 0, 1, 0xfc, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(STD, "std", 0, 1, 0xfd, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(LAHF, "lahf", 0, 1, 0x9f, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(SAHF, "sahf", 0, 1, 0x9e, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(PUSHFW, "pushfw", 0x66, 1, 0x9c, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(PUSHFQ, "pushfq", 0, 1, 0x9c, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(POPFW, "popfw", 0x66, 1, 0x9d, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(POPFQ, "popfq", 0, 1, 0x9d, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(LOOP_REL8, "loop", 0, 1, 0xe2, NO, 0, 1, 0, NONE, NONE, 0, REL) \
    X(LOOPE_REL8, "loope", 0, 1, 0xe1, NO, 0, 1, 0, NONE, NONE, 0, REL) \
    X(LOOPNE_REL8, "loopne", 0, 1, 0xe0, NO, 0, 1, 0, NONE, NONE, 0, REL) \
    X(JRCXZ_REL8, "jrcxz", 0, 1, 0xe3, NO, 0, 1, 0, NONE, NONE, 0, REL) \
    X(XCHG_R16_AX, "xchg", 0x66, 1, 0x90, PR, 0, 0, 0, NONE, GW, 2, RM_ACC) \
    X(XCHG_R32_EAX, "xchg", 0, 1, 0x90, PR, 0, 0, 0, NONE, GD, 4, RM_ACC) \
    X(XCHG_R64_RAX, "xchg", 0, 1, 0x90, PR, 0, 0, 1, NONE, GQ, 8, RM_ACC) \
    X(MOVABS_AX_MOFFS, "movabs", 0x66, 1, 0xa1, NO, 8, 0, 0, NONE, NONE, 2, ACC_MOFFS) \
    X(MOVABS_MOFFS_AX, "movabs", 0x66, 1, 0xa3, NO, 8, 0, 0, NONE, NONE, 2, MOFFS_ACC) \
    X(RETFD, "retfd", 0, 1, 0xcb, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(RETFD_IMM16, "retfd", 0, 1, 0xca, NO, 2, 0, 0, NONE, NONE, 0, IMM) \
    X(RETFQ, "retfq", 0, 1, 0xcb, NO, 0, 0, 1, NONE, NONE, 0, NONE) \
    X(RETFQ_IMM16, "retfq", 0, 1, 0xca, NO, 2, 0, 1, NONE, NONE, 0, IMM) \
    X(RETFW, "retfw", 0x66, 1, 0xcb, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(RETFW_IMM16, "retfw", 0x66, 1, 0xca, NO, 2, 0, 0, NONE, NONE, 0, IMM) \
    /* GNU as only takes the 64-bit LFS, LGS and LSS as rex.w with a 32-bit register name. */ \
    X(LFS_R16_M32, "lfs", 0x66, 2, 0x0fb4, R, 0, 0, 0, GW, M, 4, REG_RM) \
    X(LFS_R32_M48, "lfs", 0, 2, 0x0fb4, R, 0, 0, 0, GD, M, 6, REG_RM) \
    X(LFS_R64_M80, "rex.w lfs", 0, 2, 0x0fb4, R, 0, 0, 1, GD, M, 6, REG_RM) \
    X(LGS_R16_M32, "lgs", 0x66, 2, 0x0fb5, R, 0, 0, 0, GW, M, 4, REG_RM) \
    X(LGS_R32_M48, "lgs", 0, 2, 0x0fb5, R, 0, 0, 0, GD, M, 6, REG_RM) \
    X(LGS_R64_M80, "rex.w lgs", 0, 2, 0x0fb5, R, 0, 0, 1, GD, M, 6, REG_RM) \
    X(LSS_R16_M32, "lss", 0x66, 2, 0x0fb2, R, 0, 0, 0, GW, M, 4, REG_RM) \
    X(LSS_R32_M48, "lss", 0, 2, 0x0fb2, R, 0, 0, 0, GD, M, 6, REG_RM) \
    X(LSS_R64_M80, "rex.w lss", 0, 2, 0x0fb2, R, 0, 0, 1, GD, M, 6, REG_RM) \
    /* Segment registers: from one to 16 bits of memory or any register (rex.w for GNU as's 64-bit one), and to one. */ \
    X(MOV_M16_SREG, "mov", 0, 1, 0x8c, R, 0, 0, 0, SREG, M, 2, RM_REG) \
    X(MOV_R16_SREG, "mov", 0x66, 1, 0x8c, R, 0, 0, 0, SREG, GW, 2, RM_REG) \
    X(MOV_R32_SREG, "mov", 0, 1, 0x8c, R, 0, 0, 0, SREG, GD, 4, RM_REG) \
    X(MOV_R64_SREG, "rex.w mov", 0, 1, 0x8c, R, 0, 0, 1, SREG, GQ, 8, RM_REG) \
    X(MOV_SREG_RM16, "mov", 0, 1, 0x8e, R, 0, 0, 0, SREG, EW, 2, REG_RM) \
    X(PUSH_FS, "push fs", 0, 2, 0x0fa0, NO, 0, 0, 0, NONE, NONE, 8, NONE) \
    X(PUSH_GS, "push gs", 0, 2, 0x0fa8, NO, 0, 0, 0, NONE, NONE, 8, NONE) \
    X(PUSHW_FS, "pushw fs", 0x66, 2, 0x0fa0, NO, 0, 0, 0, NONE, NONE, 2, NONE) \
    X(PUSHW_GS, "pushw gs", 0x66, 2, 0x0fa8, NO, 0, 0, 0, NONE, NONE, 2, NONE) \
    X(POP_FS, "pop fs", 0, 2, 0x0fa1, NO, 0, 0, 0, NONE, NONE, 8, NONE) \
    X(POP_GS, "pop gs", 0, 2, 0x0fa9, NO, 0, 0, 0, NONE, NONE, 8, NONE) \
    X(POPW_FS, "popw fs", 0x66, 2, 0x0fa1, NO, 0, 0, 0, NONE, NONE, 2, NONE) \
    X(POPW_GS, "popw gs", 0x66, 2, 0x0fa9, NO, 0, 0, 0, NONE, NONE, 2, NONE) \
    /* Control and debug registers, from and to a 64-bit register (REX.R selects CR8). */ \
    X(MOV_CR_R64, "mov", 0, 2, 0x0f22, R, 0, 0, 0, CR, GQ, 8, REG_RM) \
    X(MOV_R64_CR, "mov", 0, 2, 0x0f20, R, 0, 0, 0, CR, GQ, 8, RM_REG) \
    X(MOV_DR_R64, "mov", 0, 2, 0x0f23, R, 0, 0, 0, DR, GQ, 8, REG_RM) \
    X(MOV_R64_DR, "mov", 0, 2, 0x0f21, R, 0, 0, 0, DR, GQ, 8, RM_REG) \
    /* System instructions. Descriptor tables, tasks and segments: GDTR and IDTR (a limit and a base, 10 bytes), selectors. */ \
    X(LGDT_M80, "lgdt", 0, 2, 0x0f01, D2, 0, 0, 0, NONE, M, 10, RM) \
    X(LIDT_M80, "lidt", 0, 2, 0x0f01, D3, 0, 0, 0, NONE, M, 10, RM) \
    X(SGDT_M80, "sgdt", 0, 2, 0x0f01, D0, 0, 0, 0, NONE, M, 10, RM) \
    X(SIDT_M80, "sidt", 0, 2, 0x0f01, D1, 0, 0, 0, NONE, M, 10, RM) \
    X(LLDT_RM16, "lldt", 0, 2, 0x0f00, D2, 0, 0, 0, NONE, EW, 2, RM) \
    X(LTR_RM16, "ltr", 0, 2, 0x0f00, D3, 0, 0, 0, NONE, EW, 2, RM) \
    X(VERR_RM16, "verr", 0, 2, 0x0f00, D4, 0, 0, 0, NONE, EW, 2, RM) \
    X(VERW_RM16, "verw", 0, 2, 0x0f00, D5, 0, 0, 0, NONE, EW, 2, RM) \
    X(LKGS_RM16, "lkgs", 0xf2, 2, 0x0f00, D6, 0, 0, 0, NONE, EW, 2, RM) \
    X(LMSW_RM16, "lmsw", 0, 2, 0x0f01, D6, 0, 0, 0, NONE, EW, 2, RM) \
    BUSTER_X86_SELECTOR_STORE_FORMS(X, SLDT, "sldt", "rex.w sldt", 0x0f00, 0) \
    BUSTER_X86_SELECTOR_STORE_FORMS(X, STR, "str", "rex.w str", 0x0f00, 1) \
    BUSTER_X86_SELECTOR_STORE_FORMS(X, SMSW, "smsw", "smsw", 0x0f01, 4) \
    BUSTER_X86_DESCRIPTOR_LOAD_FORMS(X, LAR, "lar", 0x0f02, R64) \
    BUSTER_X86_DESCRIPTOR_LOAD_FORMS(X, LSL, "lsl", 0x0f03, R32) \
    X(CLTS, "clts", 0, 2, 0x0f06, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    /* Port I/O, through DX or an 8-bit immediate port, and as strings; interrupts, halting and SMM. */ \
    X(IN_AL_IMM8, "in", 0, 1, 0xe4, NO, 1, 0, 0, NONE, NONE, 1, ACC_IMM) \
    X(IN_AX_IMM8, "in", 0x66, 1, 0xe5, NO, 1, 0, 0, NONE, NONE, 2, ACC_IMM) \
    X(IN_EAX_IMM8, "in", 0, 1, 0xe5, NO, 1, 0, 0, NONE, NONE, 4, ACC_IMM) \
    X(IN_AL_DX, "in", 0, 1, 0xec, NO, 0, 0, 0, NONE, NONE, 1, ACC_DX) \
    X(IN_AX_DX, "in", 0x66, 1, 0xed, NO, 0, 0, 0, NONE, NONE, 2, ACC_DX) \
    X(IN_EAX_DX, "in", 0, 1, 0xed, NO, 0, 0, 0, NONE, NONE, 4, ACC_DX) \
    X(OUT_IMM8_AL, "out", 0, 1, 0xe6, NO, 1, 0, 0, NONE, NONE, 1, IMM_ACC) \
    X(OUT_IMM8_AX, "out", 0x66, 1, 0xe7, NO, 1, 0, 0, NONE, NONE, 2, IMM_ACC) \
    X(OUT_IMM8_EAX, "out", 0, 1, 0xe7, NO, 1, 0, 0, NONE, NONE, 4, IMM_ACC) \
    X(OUT_DX_AL, "out", 0, 1, 0xee, NO, 0, 0, 0, NONE, NONE, 1, DX_ACC) \
    X(OUT_DX_AX, "out", 0x66, 1, 0xef, NO, 0, 0, 0, NONE, NONE, 2, DX_ACC) \
    X(OUT_DX_EAX, "out", 0, 1, 0xef, NO, 0, 0, 0, NONE, NONE, 4, DX_ACC) \
    BUSTER_X86_IO_STRING_FORMS(X, INS, "ins", 0, 0x6c, 0x6d) \
    BUSTER_X86_IO_STRING_FORMS(X, REP_INS, "rep ins", 0xf3, 0x6c, 0x6d) \
    BUSTER_X86_IO_STRING_FORMS(X, REPNE_INS, "repne ins", 0xf2, 0x6c, 0x6d) \
    BUSTER_X86_IO_STRING_FORMS(X, OUTS, "outs", 0, 0x6e, 0x6f) \
    BUSTER_X86_IO_STRING_FORMS(X, REP_OUTS, "rep outs", 0xf3, 0x6e, 0x6f) \
    BUSTER_X86_IO_STRING_FORMS(X, REPNE_OUTS, "repne outs", 0xf2, 0x6e, 0x6f) \
    X(CLI, "cli", 0, 1, 0xfa, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(STI, "sti", 0, 1, 0xfb, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(HLT, "hlt", 0, 1, 0xf4, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(MONITOR, "monitor", 0, 3, 0x0f01c8, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(MWAIT, "mwait", 0, 3, 0x0f01c9, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(RSM, "rsm", 0, 2, 0x0faa, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    /* System calls and returns: GNU as names the 16-bit iret iretw, the 32-bit sysexit and sysret sysexitd and sysretd, and the 64-bit ones sysexitq and sysretq. */ \
    X(IRET, "iretw", 0x66, 1, 0xcf, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(IRETD, "iretd", 0, 1, 0xcf, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(IRETQ, "iretq", 0, 1, 0xcf, NO, 0, 0, 1, NONE, NONE, 0, NONE) \
    X(SYSENTER, "sysenter", 0, 2, 0x0f34, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(SYSEXIT, "sysexitd", 0, 2, 0x0f35, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(SYSEXITQ, "sysexitq", 0, 2, 0x0f35, NO, 0, 0, 1, NONE, NONE, 0, NONE) \
    X(SYSRET, "sysretd", 0, 2, 0x0f07, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(SYSRET64, "sysretq", 0, 2, 0x0f07, NO, 0, 0, 1, NONE, NONE, 0, NONE) \
    X(SWAPGS, "swapgs", 0, 3, 0x0f01f8, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(ERETS, "erets", 0xf2, 3, 0x0f01ca, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(ERETU, "eretu", 0xf3, 3, 0x0f01ca, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    /* Caches and TLBs. */ \
    X(INVD, "invd", 0, 2, 0x0f08, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(WBINVD, "wbinvd", 0, 2, 0x0f09, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(WBNOINVD, "wbnoinvd", 0xf3, 2, 0x0f09, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(INVLPG_M8, "invlpg", 0, 2, 0x0f01, D7, 0, 0, 0, NONE, M, 1, RM) \
    X(INVPCID_R64_M128, "invpcid", 0x66, 3, 0x0f3882, R, 0, 0, 0, GQ, M, 16, REG_RM) \
    X(INVLPGB, "invlpgb", 0, 3, 0x0f01fe, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(TLBSYNC, "tlbsync", 0, 3, 0x0f01ff, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    /* MSRs and counters (with an immediate MSR index, in VEX map 7). */ \
    X(RDMSR, "rdmsr", 0, 2, 0x0f32, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(WRMSR, "wrmsr", 0, 2, 0x0f30, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(WRMSRNS, "wrmsrns", 0, 3, 0x0f01c6, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(RDMSRLIST, "rdmsrlist", 0xf2, 3, 0x0f01c6, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(WRMSRLIST, "wrmsrlist", 0xf3, 3, 0x0f01c6, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(URDMSR_R64_R64, "urdmsr", 0xf2, 3, 0x0f38f8, R, 0, 0, 0, GQ, GQ, 8, RM_REG) \
    X(UWRMSR_R64_R64, "uwrmsr", 0xf3, 3, 0x0f38f8, R, 0, 0, 0, GQ, GQ, 8, REG_RM) \
    X(RDPMC, "rdpmc", 0, 2, 0x0f33, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(RDPRU, "rdpru", 0, 3, 0x0f01fd, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    /* Processor state: XSAVE and XCR0, FSGSBASE, PKU, SMAP and CET's shadow stacks. */ \
    BUSTER_X86_XSAVE_FORMS(X, XSAVE, "xsave", 0x0fae, 4) \
    BUSTER_X86_XSAVE_FORMS(X, XRSTOR, "xrstor", 0x0fae, 5) \
    BUSTER_X86_XSAVE_FORMS(X, XSAVEOPT, "xsaveopt", 0x0fae, 6) \
    BUSTER_X86_XSAVE_FORMS(X, XSAVEC, "xsavec", 0x0fc7, 4) \
    BUSTER_X86_XSAVE_FORMS(X, XSAVES, "xsaves", 0x0fc7, 5) \
    BUSTER_X86_XSAVE_FORMS(X, XRSTORS, "xrstors", 0x0fc7, 3) \
    X(XGETBV, "xgetbv", 0, 3, 0x0f01d0, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(XSETBV, "xsetbv", 0, 3, 0x0f01d1, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    BUSTER_X86_SEGMENT_BASE_FORMS(X, RDFSBASE, "rdfsbase", 0) \
    BUSTER_X86_SEGMENT_BASE_FORMS(X, RDGSBASE, "rdgsbase", 1) \
    BUSTER_X86_SEGMENT_BASE_FORMS(X, WRFSBASE, "wrfsbase", 2) \
    BUSTER_X86_SEGMENT_BASE_FORMS(X, WRGSBASE, "wrgsbase", 3) \
    X(RDPKRU, "rdpkru", 0, 3, 0x0f01ee, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(WRPKRU, "wrpkru", 0, 3, 0x0f01ef, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(CLAC, "clac", 0, 3, 0x0f01ca, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(STAC, "stac", 0, 3, 0x0f01cb, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(ENDBR32, "endbr32", 0xf3, 3, 0x0f1efb, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(INCSSPD_R32, "incsspd", 0xf3, 2, 0x0fae, D5, 0, 0, 0, NONE, GD, 4, RM) \
    X(INCSSPQ_R64, "incsspq", 0xf3, 2, 0x0fae, D5, 0, 0, 1, NONE, GQ, 8, RM) \
    X(RDSSPD_R32, "rdsspd", 0xf3, 2, 0x0f1e, D1, 0, 0, 0, NONE, GD, 4, RM) \
    X(RDSSPQ_R64, "rdsspq", 0xf3, 2, 0x0f1e, D1, 0, 0, 1, NONE, GQ, 8, RM) \
    X(WRSSD_M32_R32, "wrssd", 0, 3, 0x0f38f6, R, 0, 0, 0, GD, M, 4, RM_REG) \
    X(WRSSQ_M64_R64, "wrssq", 0, 3, 0x0f38f6, R, 0, 0, 1, GQ, M, 8, RM_REG) \
    X(WRUSSD_M32_R32, "wrussd", 0x66, 3, 0x0f38f5, R, 0, 0, 0, GD, M, 4, RM_REG) \
    X(WRUSSQ_M64_R64, "wrussq", 0x66, 3, 0x0f38f5, R, 0, 0, 1, GQ, M, 8, RM_REG) \
    X(SETSSBSY, "setssbsy", 0xf3, 3, 0x0f01e8, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(CLRSSBSY_M64, "clrssbsy", 0xf3, 2, 0x0fae, D6, 0, 0, 0, NONE, M, 8, RM) \
    X(RSTORSSP_M64, "rstorssp", 0xf3, 2, 0x0f01, D5, 0, 0, 0, NONE, M, 8, RM) \
    X(SAVEPREVSSP, "saveprevssp", 0xf3, 3, 0x0f01ea, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    /* Virtualization and confidential computing: AMD's SVM (with an address in RAX), SEV-ES's vmgexit and SEV-SNP (rmpread reads [rcx] into rax), Intel's VMX, TDX, SGX and SMX, and the rest. */ \
    X(VMRUN_RAX, "vmrun", 0, 3, 0x0f01d8, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(VMMCALL, "vmmcall", 0, 3, 0x0f01d9, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(VMGEXIT, "vmgexit", 0xf3, 3, 0x0f01d9, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(VMLOAD_RAX, "vmload", 0, 3, 0x0f01da, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(VMSAVE, "vmsave", 0, 3, 0x0f01db, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(STGI, "stgi", 0, 3, 0x0f01dc, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(CLGI, "clgi", 0, 3, 0x0f01dd, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(SKINIT_EAX, "skinit", 0, 3, 0x0f01de, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(INVLPGA_RAX_ECX, "invlpga", 0, 3, 0x0f01df, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(PSMASH_RAX, "psmash", 0xf3, 3, 0x0f01ff, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(PVALIDATE_RAX_ECX_EDX, "pvalidate", 0xf2, 3, 0x0f01ff, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(RMPADJUST_RAX_RCX_RDX, "rmpadjust", 0xf3, 3, 0x0f01fe, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(RMPUPDATE_RAX_RCX, "rmpupdate", 0xf2, 3, 0x0f01fe, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(RMPQUERY_RAX_RCX_RDX, "rmpquery", 0xf3, 3, 0x0f01fd, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(RMPREAD_RAX_RCX, "rmpread", 0xf2, 3, 0x0f01fd, NO, 0, 0, 0, NONE, NONE, 8, MEMORY_RCX_ACC) \
    X(VMCALL, "vmcall", 0, 3, 0x0f01c1, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(VMLAUNCH, "vmlaunch", 0, 3, 0x0f01c2, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(VMRESUME, "vmresume", 0, 3, 0x0f01c3, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(VMXOFF, "vmxoff", 0, 3, 0x0f01c4, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(VMFUNC, "vmfunc", 0, 3, 0x0f01d4, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(VMXON_M64, "vmxon", 0xf3, 2, 0x0fc7, D6, 0, 0, 0, NONE, M, 8, RM) \
    X(VMCLEAR_M64, "vmclear", 0x66, 2, 0x0fc7, D6, 0, 0, 0, NONE, M, 8, RM) \
    X(VMPTRLD_M64, "vmptrld", 0, 2, 0x0fc7, D6, 0, 0, 0, NONE, M, 8, RM) \
    X(VMPTRST_M64, "vmptrst", 0, 2, 0x0fc7, D7, 0, 0, 0, NONE, M, 8, RM) \
    X(VMREAD_RM64_R64, "vmread", 0, 2, 0x0f78, R, 0, 0, 0, GQ, EQ, 8, RM_REG) \
    X(VMWRITE_R64_RM64, "vmwrite", 0, 2, 0x0f79, R, 0, 0, 0, GQ, EQ, 8, REG_RM) \
    X(INVEPT_R64_M128, "invept", 0x66, 3, 0x0f3880, R, 0, 0, 0, GQ, M, 16, REG_RM) \
    X(INVVPID_R64_M128, "invvpid", 0x66, 3, 0x0f3881, R, 0, 0, 0, GQ, M, 16, REG_RM) \
    X(TDCALL, "tdcall", 0x66, 3, 0x0f01cc, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(SEAMRET, "seamret", 0x66, 3, 0x0f01cd, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(SEAMOPS, "seamops", 0x66, 3, 0x0f01ce, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(SEAMCALL, "seamcall", 0x66, 3, 0x0f01cf, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(ENCLS, "encls", 0, 3, 0x0f01cf, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(ENCLU, "enclu", 0, 3, 0x0f01d7, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(ENCLV, "enclv", 0, 3, 0x0f01c0, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(GETSEC, "getsec", 0, 2, 0x0f37, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(PCONFIG, "pconfig", 0, 3, 0x0f01c5, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(PBNDKB, "pbndkb", 0, 3, 0x0f01c7, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(CLUI, "clui", 0xf3, 3, 0x0f01ee, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(STUI, "stui", 0xf3, 3, 0x0f01ef, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(TESTUI, "testui", 0xf3, 3, 0x0f01ed, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(UIRET, "uiret", 0xf3, 3, 0x0f01ec, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(SENDUIPI_R64, "senduipi", 0xf3, 2, 0x0fc7, D6, 0, 0, 0, NONE, GQ, 8, RM) \
    /* ENQCMD's 64-byte command, which GNU as takes without a size. */ \
    X(ENQCMD_R64_M512, "enqcmd", 0xf2, 3, 0x0f38f8, R, 0, 0, 0, GQ, M, 0, REG_RM) \
    X(ENQCMDS_R64_M512, "enqcmds", 0xf3, 3, 0x0f38f8, R, 0, 0, 0, GQ, M, 0, REG_RM) \
    /* HRESET's ModRM byte (C0) is part of the opcode, and EAX is implicit. */ \
    X(HRESET_IMM8, "hreset", 0xf3, 4, 0x0f3af0c0, NO, 1, 0, 0, NONE, NONE, 0, IMM) \
    X(SERIALIZE, "serialize", 0, 3, 0x0f01e8, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(MCOMMIT, "mcommit", 0xf3, 3, 0x0f01fa, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(ADCX_R32_RM32, "adcx", 0x66, 3, 0x0f38f6, R, 0, 0, 0, GD, ED, 4, REG_RM) \
    X(ADCX_R64_RM64, "adcx", 0x66, 3, 0x0f38f6, R, 0, 0, 1, GQ, EQ, 8, REG_RM) \
    X(ADOX_R32_RM32, "adox", 0xf3, 3, 0x0f38f6, R, 0, 0, 0, GD, ED, 4, REG_RM) \
    X(ADOX_R64_RM64, "adox", 0xf3, 3, 0x0f38f6, R, 0, 0, 1, GQ, EQ, 8, REG_RM) \
    X(AADD_M32_R32, "aadd", 0, 3, 0x0f38fc, R, 0, 0, 0, GD, M, 4, RM_REG) \
    X(AADD_M64_R64, "aadd", 0, 3, 0x0f38fc, R, 0, 0, 1, GQ, M, 8, RM_REG) \
    X(AAND_M32_R32, "aand", 0x66, 3, 0x0f38fc, R, 0, 0, 0, GD, M, 4, RM_REG) \
    X(AAND_M64_R64, "aand", 0x66, 3, 0x0f38fc, R, 0, 0, 1, GQ, M, 8, RM_REG) \
    X(AOR_M32_R32, "aor", 0xf2, 3, 0x0f38fc, R, 0, 0, 0, GD, M, 4, RM_REG) \
    X(AOR_M64_R64, "aor", 0xf2, 3, 0x0f38fc, R, 0, 0, 1, GQ, M, 8, RM_REG) \
    X(AXOR_M32_R32, "axor", 0xf3, 3, 0x0f38fc, R, 0, 0, 0, GD, M, 4, RM_REG) \
    X(AXOR_M64_R64, "axor", 0xf3, 3, 0x0f38fc, R, 0, 0, 1, GQ, M, 8, RM_REG) \
    X(MOVBE_R16_M16, "movbe", 0x66, 3, 0x0f38f0, R, 0, 0, 0, GW, M, 2, REG_RM) \
    X(MOVBE_R32_M32, "movbe", 0, 3, 0x0f38f0, R, 0, 0, 0, GD, M, 4, REG_RM) \
    X(MOVBE_R64_M64, "movbe", 0, 3, 0x0f38f0, R, 0, 0, 1, GQ, M, 8, REG_RM) \
    X(MOVBE_M16_R16, "movbe", 0x66, 3, 0x0f38f1, R, 0, 0, 0, GW, M, 2, RM_REG) \
    X(MOVBE_M32_R32, "movbe", 0, 3, 0x0f38f1, R, 0, 0, 0, GD, M, 4, RM_REG) \
    X(MOVBE_M64_R64, "movbe", 0, 3, 0x0f38f1, R, 0, 0, 1, GQ, M, 8, RM_REG) \
    X(MOVRS_R8_M8, "movrs", 0, 3, 0x0f388a, R, 0, 0, 0, GB, M, 1, REG_RM) \
    X(MOVRS_R16_M16, "movrs", 0x66, 3, 0x0f388b, R, 0, 0, 0, GW, M, 2, REG_RM) \
    X(MOVRS_R32_M32, "movrs", 0, 3, 0x0f388b, R, 0, 0, 0, GD, M, 4, REG_RM) \
    X(MOVRS_R64_M64, "movrs", 0, 3, 0x0f388b, R, 0, 0, 1, GQ, M, 8, REG_RM) \
    X(MOVDIRI_M32_R32, "movdiri", 0, 3, 0x0f38f9, R, 0, 0, 0, GD, M, 4, RM_REG) \
    X(MOVDIRI_M64_R64, "movdiri", 0, 3, 0x0f38f9, R, 0, 0, 1, GQ, M, 8, RM_REG) \
    /* A 64-byte memory operand, which GNU as takes without a size. */ \
    X(MOVDIR64B_R64_M512, "movdir64b", 0x66, 3, 0x0f38f8, R, 0, 0, 0, GQ, M, 0, REG_RM) \
    X(XBEGIN_REL32, "xbegin", 0, 2, 0xc7f8, NO, 0, 4, 0, NONE, NONE, 0, REL) \
    /* A 16-bit fallback address (66 and a rel16), as the 16-bit near branches have. */ \
    X(XBEGIN_REL16, "data16 xbegin", 0x66, 2, 0xc7f8, NO, 0, 2, 0, NONE, NONE, 0, REL) \
    X(XABORT_IMM8, "xabort", 0, 2, 0xc6f8, NO, 1, 0, 0, NONE, NONE, 0, IMM) \
    X(XEND, "xend", 0, 3, 0x0f01d5, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(XTEST, "xtest", 0, 3, 0x0f01d6, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(XSUSLDTRK, "xsusldtrk", 0xf2, 3, 0x0f01e8, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(XRESLDTRK, "xresldtrk", 0xf2, 3, 0x0f01e9, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(PTWRITE_RM32, "ptwrite", 0xf3, 2, 0x0fae, D4, 0, 0, 0, NONE, ED, 4, RM) \
    X(PTWRITE_RM64, "ptwrite", 0xf3, 2, 0x0fae, D4, 0, 0, 1, NONE, EQ, 8, RM) \
    X(RDRAND_R16, "rdrand", 0x66, 2, 0x0fc7, D6, 0, 0, 0, NONE, GW, 2, RM) \
    X(RDRAND_R32, "rdrand", 0, 2, 0x0fc7, D6, 0, 0, 0, NONE, GD, 4, RM) \
    X(RDRAND_R64, "rdrand", 0, 2, 0x0fc7, D6, 0, 0, 1, NONE, GQ, 8, RM) \
    X(RDSEED_R16, "rdseed", 0x66, 2, 0x0fc7, D7, 0, 0, 0, NONE, GW, 2, RM) \
    X(RDSEED_R32, "rdseed", 0, 2, 0x0fc7, D7, 0, 0, 0, NONE, GD, 4, RM) \
    X(RDSEED_R64, "rdseed", 0, 2, 0x0fc7, D7, 0, 0, 1, NONE, GQ, 8, RM) \
    X(RDPID_R64, "rdpid", 0xf3, 2, 0x0fc7, D7, 0, 0, 0, NONE, GQ, 8, RM) \
    X(TPAUSE_R32, "tpause", 0x66, 2, 0x0fae, D6, 0, 0, 0, NONE, GD, 4, RM) \
    X(UMONITOR_R64, "umonitor", 0xf3, 2, 0x0fae, D6, 0, 0, 0, NONE, GQ, 8, RM) \
    X(UMWAIT_R32, "umwait", 0xf2, 2, 0x0fae, D6, 0, 0, 0, NONE, GD, 4, RM) \
    X(NOP_RM64, "nop", 0, 2, 0x0f1f, D0, 0, 0, 1, NONE, EQ, 8, RM) \
    X(PREFETCH_M8, "prefetch", 0, 2, 0x0f0d, D0, 0, 0, 0, NONE, M, 1, RM) \
    X(PREFETCHW_M8, "prefetchw", 0, 2, 0x0f0d, D1, 0, 0, 0, NONE, M, 1, RM) \
    X(PREFETCHNTA_M8, "prefetchnta", 0, 2, 0x0f18, D0, 0, 0, 0, NONE, M, 1, RM) \
    X(PREFETCHT0_M8, "prefetcht0", 0, 2, 0x0f18, D1, 0, 0, 0, NONE, M, 1, RM) \
    X(PREFETCHT1_M8, "prefetcht1", 0, 2, 0x0f18, D2, 0, 0, 0, NONE, M, 1, RM) \
    X(PREFETCHT2_M8, "prefetcht2", 0, 2, 0x0f18, D3, 0, 0, 0, NONE, M, 1, RM) \
    X(PREFETCHWT1_M8, "prefetchwt1", 0, 2, 0x0f0d, D2, 0, 0, 0, NONE, M, 1, RM) \
    X(PREFETCHRST2_M8, "prefetchrst2", 0, 2, 0x0f18, D4, 0, 0, 0, NONE, M, 1, RM) \
    /* RIP-relative addresses only: with another, they're NOPs. */ \
    X(PREFETCHIT0_M8, "prefetchit0", 0, 2, 0x0f18, D7, 0, 0, 0, NONE, M, 1, RM) \
    X(PREFETCHIT1_M8, "prefetchit1", 0, 2, 0x0f18, D6, 0, 0, 0, NONE, M, 1, RM) \
    X(CLFLUSH_M8, "clflush", 0, 2, 0x0fae, D7, 0, 0, 0, NONE, M, 1, RM) \
    X(CLFLUSHOPT_M8, "clflushopt", 0x66, 2, 0x0fae, D7, 0, 0, 0, NONE, M, 1, RM) \
    X(CLWB_M8, "clwb", 0x66, 2, 0x0fae, D6, 0, 0, 0, NONE, M, 1, RM) \
    X(CLDEMOTE_M8, "cldemote", 0, 2, 0x0f1c, D0, 0, 0, 0, NONE, M, 1, RM) \
    X(CLZERO, "clzero", 0, 3, 0x0f01fc, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(MONITORX, "monitorx", 0, 3, 0x0f01fa, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(MWAITX, "mwaitx", 0, 3, 0x0f01fb, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    BUSTER_X86_X87_ARITHMETIC_FORMS(X, FADD, "fadd", 0, 0) \
    BUSTER_X86_X87_ARITHMETIC_FORMS(X, FMUL, "fmul", 1, 1) \
    BUSTER_X86_X87_ARITHMETIC_FORMS(X, FSUB, "fsub", 4, 5) \
    BUSTER_X86_X87_ARITHMETIC_FORMS(X, FSUBR, "fsubr", 5, 4) \
    BUSTER_X86_X87_ARITHMETIC_FORMS(X, FDIV, "fdiv", 6, 7) \
    BUSTER_X86_X87_ARITHMETIC_FORMS(X, FDIVR, "fdivr", 7, 6) \
    X(FCOM_M32, "fcom", 0, 1, 0xd8, D2, 0, 0, 0, NONE, M, 4, RM) \
    X(FCOM_M64, "fcom", 0, 1, 0xdc, D2, 0, 0, 0, NONE, M, 8, RM) \
    X(FCOM_ST, "fcom", 0, 1, 0xd8, D2, 0, 0, 0, NONE, ST, 0, RM) \
    X(FCOMP_M32, "fcomp", 0, 1, 0xd8, D3, 0, 0, 0, NONE, M, 4, RM) \
    X(FCOMP_M64, "fcomp", 0, 1, 0xdc, D3, 0, 0, 0, NONE, M, 8, RM) \
    X(FCOMP_ST, "fcomp", 0, 1, 0xd8, D3, 0, 0, 0, NONE, ST, 0, RM) \
    BUSTER_X86_X87_FORM(X, FCOMPP, "fcompp", 0xded9) \
    BUSTER_X86_X87_INTEGER_FORMS(X, FIADD, "fiadd", 0) \
    BUSTER_X86_X87_INTEGER_FORMS(X, FIMUL, "fimul", 1) \
    BUSTER_X86_X87_INTEGER_FORMS(X, FICOM, "ficom", 2) \
    BUSTER_X86_X87_INTEGER_FORMS(X, FICOMP, "ficomp", 3) \
    BUSTER_X86_X87_INTEGER_FORMS(X, FISUB, "fisub", 4) \
    BUSTER_X86_X87_INTEGER_FORMS(X, FISUBR, "fisubr", 5) \
    BUSTER_X86_X87_INTEGER_FORMS(X, FIDIV, "fidiv", 6) \
    BUSTER_X86_X87_INTEGER_FORMS(X, FIDIVR, "fidivr", 7) \
    X(FLD_M32, "fld", 0, 1, 0xd9, D0, 0, 0, 0, NONE, M, 4, RM) \
    X(FLD_M64, "fld", 0, 1, 0xdd, D0, 0, 0, 0, NONE, M, 8, RM) \
    X(FLD_M80, "fld", 0, 1, 0xdb, D5, 0, 0, 0, NONE, M, 10, RM) \
    X(FLD_ST, "fld", 0, 1, 0xd9, D0, 0, 0, 0, NONE, ST, 0, RM) \
    X(FST_M32, "fst", 0, 1, 0xd9, D2, 0, 0, 0, NONE, M, 4, RM) \
    X(FST_M64, "fst", 0, 1, 0xdd, D2, 0, 0, 0, NONE, M, 8, RM) \
    X(FST_ST, "fst", 0, 1, 0xdd, D2, 0, 0, 0, NONE, ST, 0, RM) \
    X(FSTP_M32, "fstp", 0, 1, 0xd9, D3, 0, 0, 0, NONE, M, 4, RM) \
    X(FSTP_M64, "fstp", 0, 1, 0xdd, D3, 0, 0, 0, NONE, M, 8, RM) \
    X(FSTP_M80, "fstp", 0, 1, 0xdb, D7, 0, 0, 0, NONE, M, 10, RM) \
    X(FSTP_ST, "fstp", 0, 1, 0xdd, D3, 0, 0, 0, NONE, ST, 0, RM) \
    X(FILD_M16, "fild", 0, 1, 0xdf, D0, 0, 0, 0, NONE, M, 2, RM) \
    X(FILD_M32, "fild", 0, 1, 0xdb, D0, 0, 0, 0, NONE, M, 4, RM) \
    X(FILD_M64, "fild", 0, 1, 0xdf, D5, 0, 0, 0, NONE, M, 8, RM) \
    X(FIST_M16, "fist", 0, 1, 0xdf, D2, 0, 0, 0, NONE, M, 2, RM) \
    X(FIST_M32, "fist", 0, 1, 0xdb, D2, 0, 0, 0, NONE, M, 4, RM) \
    X(FISTP_M16, "fistp", 0, 1, 0xdf, D3, 0, 0, 0, NONE, M, 2, RM) \
    X(FISTP_M32, "fistp", 0, 1, 0xdb, D3, 0, 0, 0, NONE, M, 4, RM) \
    X(FISTP_M64, "fistp", 0, 1, 0xdf, D7, 0, 0, 0, NONE, M, 8, RM) \
    X(FISTTP_M16, "fisttp", 0, 1, 0xdf, D1, 0, 0, 0, NONE, M, 2, RM) \
    X(FISTTP_M32, "fisttp", 0, 1, 0xdb, D1, 0, 0, 0, NONE, M, 4, RM) \
    X(FISTTP_M64, "fisttp", 0, 1, 0xdd, D1, 0, 0, 0, NONE, M, 8, RM) \
    X(FBLD_M80, "fbld", 0, 1, 0xdf, D4, 0, 0, 0, NONE, M, 10, RM) \
    X(FBSTP_M80, "fbstp", 0, 1, 0xdf, D6, 0, 0, 0, NONE, M, 10, RM) \
    X(FXCH_ST, "fxch", 0, 1, 0xd9, D1, 0, 0, 0, NONE, ST, 0, RM) \
    BUSTER_X86_X87_FCMOV_FORM(X, FCMOVB, "fcmovb", 0xda, 0) \
    BUSTER_X86_X87_FCMOV_FORM(X, FCMOVE, "fcmove", 0xda, 1) \
    BUSTER_X86_X87_FCMOV_FORM(X, FCMOVBE, "fcmovbe", 0xda, 2) \
    BUSTER_X86_X87_FCMOV_FORM(X, FCMOVU, "fcmovu", 0xda, 3) \
    BUSTER_X86_X87_FCMOV_FORM(X, FCMOVNB, "fcmovnb", 0xdb, 0) \
    BUSTER_X86_X87_FCMOV_FORM(X, FCMOVNE, "fcmovne", 0xdb, 1) \
    BUSTER_X86_X87_FCMOV_FORM(X, FCMOVNBE, "fcmovnbe", 0xdb, 2) \
    BUSTER_X86_X87_FCMOV_FORM(X, FCMOVNU, "fcmovnu", 0xdb, 3) \
    X(FCOMI_ST0_ST, "fcomi", 0, 1, 0xdb, D6, 0, 0, 0, NONE, ST, 0, ST_RM) \
    X(FCOMIP_ST0_ST, "fcomip", 0, 1, 0xdf, D6, 0, 0, 0, NONE, ST, 0, ST_RM) \
    X(FUCOMI_ST0_ST, "fucomi", 0, 1, 0xdb, D5, 0, 0, 0, NONE, ST, 0, ST_RM) \
    X(FUCOMIP_ST0_ST, "fucomip", 0, 1, 0xdf, D5, 0, 0, 0, NONE, ST, 0, ST_RM) \
    X(FUCOM_ST, "fucom", 0, 1, 0xdd, D4, 0, 0, 0, NONE, ST, 0, RM) \
    X(FUCOMP_ST, "fucomp", 0, 1, 0xdd, D5, 0, 0, 0, NONE, ST, 0, RM) \
    BUSTER_X86_X87_FORM(X, FUCOMPP, "fucompp", 0xdae9) \
    X(FFREE_ST, "ffree", 0, 1, 0xdd, D0, 0, 0, 0, NONE, ST, 0, RM) \
    X(FFREEP_ST, "ffreep", 0, 1, 0xdf, D0, 0, 0, 0, NONE, ST, 0, RM) \
    X(FLDCW_M16, "fldcw", 0, 1, 0xd9, D5, 0, 0, 0, NONE, M, 2, RM) \
    X(FNSTCW_M16, "fnstcw", 0, 1, 0xd9, D7, 0, 0, 0, NONE, M, 2, RM) \
    X(FNSTSW_M16, "fnstsw", 0, 1, 0xdd, D7, 0, 0, 0, NONE, M, 2, RM) \
    X(FNSTSW_AX, "fnstsw", 0, 2, 0xdfe0, NO, 0, 0, 0, NONE, NONE, 2, ACC) \
    X(FLDENV_M28, "fldenv", 0, 1, 0xd9, D4, 0, 0, 0, NONE, M, 28, RM) \
    X(FLDENVW_M14, "fldenvw", 0x66, 1, 0xd9, D4, 0, 0, 0, NONE, M, 14, RM) \
    X(FNSTENV_M28, "fnstenv", 0, 1, 0xd9, D6, 0, 0, 0, NONE, M, 28, RM) \
    X(FNSTENVW_M14, "fnstenvw", 0x66, 1, 0xd9, D6, 0, 0, 0, NONE, M, 14, RM) \
    X(FRSTOR_M108, "frstor", 0, 1, 0xdd, D4, 0, 0, 0, NONE, M, 108, RM) \
    X(FRSTORW_M94, "frstorw", 0x66, 1, 0xdd, D4, 0, 0, 0, NONE, M, 94, RM) \
    X(FNSAVE_M108, "fnsave", 0, 1, 0xdd, D6, 0, 0, 0, NONE, M, 108, RM) \
    X(FNSAVEW_M94, "fnsavew", 0x66, 1, 0xdd, D6, 0, 0, 0, NONE, M, 94, RM) \
    BUSTER_X86_X87_FORM(X, FNINIT, "fninit", 0xdbe3) \
    BUSTER_X86_X87_FORM(X, FNCLEX, "fnclex", 0xdbe2) \
    /* The 8087's and 287's control instructions (which later processors take as NOPs, but the 287XL's frstpm). */ \
    BUSTER_X86_X87_FORM(X, FNENI, "fneni", 0xdbe0) \
    BUSTER_X86_X87_FORM(X, FNDISI, "fndisi", 0xdbe1) \
    BUSTER_X86_X87_FORM(X, FNSETPM, "fnsetpm", 0xdbe4) \
    BUSTER_X86_X87_FORM(X, FRSTPM, "frstpm", 0xdbe5) \
    X(FWAIT, "fwait", 0, 1, 0x9b, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    /* The waiting forms of the control instructions: fwait and the no-wait form as one instruction, whose 9B comes first (before a segment override and a 67). */ \
    X(FINIT, "finit", 0x9b, 2, 0xdbe3, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(FCLEX, "fclex", 0x9b, 2, 0xdbe2, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(FENI, "feni", 0x9b, 2, 0xdbe0, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(FDISI, "fdisi", 0x9b, 2, 0xdbe1, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(FSETPM, "fsetpm", 0x9b, 2, 0xdbe4, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(FSTCW_M16, "fstcw", 0x9b, 1, 0xd9, D7, 0, 0, 0, NONE, M, 2, RM) \
    X(FSTSW_M16, "fstsw", 0x9b, 1, 0xdd, D7, 0, 0, 0, NONE, M, 2, RM) \
    X(FSTSW_AX, "fstsw", 0x9b, 2, 0xdfe0, NO, 0, 0, 0, NONE, NONE, 2, ACC) \
    X(FSTENV_M28, "fstenv", 0x9b, 1, 0xd9, D6, 0, 0, 0, NONE, M, 28, RM) \
    X(FSTENVW_M14, "fstenvw", 0x669b, 1, 0xd9, D6, 0, 0, 0, NONE, M, 14, RM) \
    X(FSAVE_M108, "fsave", 0x9b, 1, 0xdd, D6, 0, 0, 0, NONE, M, 108, RM) \
    X(FSAVEW_M94, "fsavew", 0x669b, 1, 0xdd, D6, 0, 0, 0, NONE, M, 94, RM) \
    BUSTER_X86_X87_FORM(X, FNOP, "fnop", 0xd9d0) \
    BUSTER_X86_X87_FORM(X, FDECSTP, "fdecstp", 0xd9f6) \
    BUSTER_X86_X87_FORM(X, FINCSTP, "fincstp", 0xd9f7) \
    BUSTER_X86_X87_FORM(X, FCHS, "fchs", 0xd9e0) \
    BUSTER_X86_X87_FORM(X, FABS, "fabs", 0xd9e1) \
    BUSTER_X86_X87_FORM(X, FTST, "ftst", 0xd9e4) \
    BUSTER_X86_X87_FORM(X, FXAM, "fxam", 0xd9e5) \
    BUSTER_X86_X87_FORM(X, FLD1, "fld1", 0xd9e8) \
    BUSTER_X86_X87_FORM(X, FLDL2T, "fldl2t", 0xd9e9) \
    BUSTER_X86_X87_FORM(X, FLDL2E, "fldl2e", 0xd9ea) \
    BUSTER_X86_X87_FORM(X, FLDPI, "fldpi", 0xd9eb) \
    BUSTER_X86_X87_FORM(X, FLDLG2, "fldlg2", 0xd9ec) \
    BUSTER_X86_X87_FORM(X, FLDLN2, "fldln2", 0xd9ed) \
    BUSTER_X86_X87_FORM(X, FLDZ, "fldz", 0xd9ee) \
    BUSTER_X86_X87_FORM(X, F2XM1, "f2xm1", 0xd9f0) \
    BUSTER_X86_X87_FORM(X, FYL2X, "fyl2x", 0xd9f1) \
    BUSTER_X86_X87_FORM(X, FPTAN, "fptan", 0xd9f2) \
    BUSTER_X86_X87_FORM(X, FPATAN, "fpatan", 0xd9f3) \
    BUSTER_X86_X87_FORM(X, FXTRACT, "fxtract", 0xd9f4) \
    BUSTER_X86_X87_FORM(X, FPREM1, "fprem1", 0xd9f5) \
    BUSTER_X86_X87_FORM(X, FPREM, "fprem", 0xd9f8) \
    BUSTER_X86_X87_FORM(X, FYL2XP1, "fyl2xp1", 0xd9f9) \
    BUSTER_X86_X87_FORM(X, FSQRT, "fsqrt", 0xd9fa) \
    BUSTER_X86_X87_FORM(X, FSINCOS, "fsincos", 0xd9fb) \
    BUSTER_X86_X87_FORM(X, FRNDINT, "frndint", 0xd9fc) \
    BUSTER_X86_X87_FORM(X, FSCALE, "fscale", 0xd9fd) \
    BUSTER_X86_X87_FORM(X, FSIN, "fsin", 0xd9fe) \
    BUSTER_X86_X87_FORM(X, FCOS, "fcos", 0xd9ff) \
    BUSTER_X86_VECTOR_FORMS(XV) \
    BUSTER_X86_FORMS_LEGACY_MODES(X)

#define BUSTER_X86_FORM_ENUM(name, ...) X86_FORM_##name,
typedef enum X86Form
{
    BUSTER_X86_FORMS(BUSTER_X86_FORM_ENUM, BUSTER_X86_FORM_ENUM)
    X86_FORM_COUNT,
} X86Form;
#undef BUSTER_X86_FORM_ENUM

// The rows at the end of the form table that exist in 16- and 32-bit modes alone (BUSTER_X86_FORMS_LEGACY_MODES).
#define BUSTER_X86_COUNT_FORM(...) +1
enum
{
    BUSTER_X86_LEGACY_MODE_FORMS = 0 BUSTER_X86_FORMS_LEGACY_MODES(BUSTER_X86_COUNT_FORM)
};
#undef BUSTER_X86_COUNT_FORM
#define BUSTER_X86_FORM_LEGACY_MODES (X86_FORM_COUNT - BUSTER_X86_LEGACY_MODE_FORMS)

typedef struct X86FormDescription X86FormDescription;
struct X86FormDescription
{
    const char* mnemonic;
    uint32_t opcode;
    // The legacy prefixes, the first in the low byte.
    uint32_t prefixes;
    uint8_t opcode_bytes;
    uint8_t encoding;
    uint8_t immediate_bytes;
    uint8_t relative_bytes;
    // REX.W, VEX.W or EVEX.W.
    uint8_t w;
    uint16_t reg;
    uint16_t rm;
    uint8_t operand_bytes;
    uint8_t syntax;
    // BUSTER_X86_FAMILY_*.
    uint8_t family;
    // VEX.L or EVEX.L'L.
    uint8_t vector_length;
    // The VEX.vvvv operand.
    uint16_t vvvv;
    // BUSTER_X86_EVEX_*, and a source condition's code in bits 8 to 11.
    uint16_t evex;
    // EVEX's N.
    uint8_t disp8_scale;
};

#define BUSTER_X86_FORM_DESCRIPTION(name, mnemonic, prefixes, opcode_bytes, opcode, encoding, immediate, relative, w, reg, rm, \
                                    operand_bytes, syntax) \
    { mnemonic, opcode, prefixes, opcode_bytes, BUSTER_X86_ENCODING_##encoding, immediate, relative, w, BUSTER_X86_OPERAND_##reg, \
      BUSTER_X86_OPERAND_##rm, operand_bytes, X86_SYNTAX_##syntax, BUSTER_X86_FAMILY_LEGACY, 0, BUSTER_X86_OPERAND_NONE, \
      BUSTER_X86_EVEX_NONE, 1 },
#define BUSTER_X86_VECTOR_FORM_DESCRIPTION(name, mnemonic, family, prefixes, opcode_bytes, opcode, encoding, immediate, w, length, \
                                           reg, vvvv, rm, operand_bytes, disp8_scale, evex, syntax) \
    { mnemonic, opcode, prefixes, opcode_bytes, BUSTER_X86_ENCODING_##encoding, immediate, 0, w, BUSTER_X86_OPERAND_##reg, \
      BUSTER_X86_OPERAND_##rm, operand_bytes, X86_SYNTAX_##syntax, BUSTER_X86_FAMILY_##family, length, BUSTER_X86_OPERAND_##vvvv, \
      BUSTER_X86_EVEX_##evex, disp8_scale },
static const X86FormDescription x86_form_descriptions[X86_FORM_COUNT] = { BUSTER_X86_FORMS(BUSTER_X86_FORM_DESCRIPTION,
                                                                                            BUSTER_X86_VECTOR_FORM_DESCRIPTION) };
#undef BUSTER_X86_VECTOR_FORM_DESCRIPTION
#undef BUSTER_X86_FORM_DESCRIPTION

// A segment override (thread-local storage) on a form's memory operand, RIP-relative too: the form plus one of these.
// As a lock or rep prefix is, the override is part of the form, whose template has it as a static byte, FS's 64 or
// GS's 65 (before the other prefixes, where GNU as puts it): x86_encoder_initialize builds three templates per form, so
// that the vectorized encoder does no more for an override than for any other form. A form without a memory operand
// takes no operands with one.
#define BUSTER_X86_FORM_FS X86_FORM_COUNT
#define BUSTER_X86_FORM_GS (2 * X86_FORM_COUNT)
// The forms with the segment overrides': an instruction's form is below this.
#define BUSTER_X86_FORM_TEMPLATES (3 * X86_FORM_COUNT)
_Static_assert(BUSTER_X86_FORM_TEMPLATES <= 65536, "a form with a segment override fits in X86Instruction.form");

// A marked instruction: one with a symbolic operand, a field (its displacement, its immediate, or both) that the code
// generator can't fill in before layout. The form, with or without a segment override, plus this flag. The encoders
// encode the form as it is without the flag (the vectorized one with a copy of its template), and report where each
// marked instruction ends (x86_encode's `fixup_ends`), which locates its fields without decoding anything
// (x86_fixup_distance).
#define BUSTER_X86_FORM_FIXUP 0x8000
// The bits of X86Instruction.form below the flag: the form and its segment override.
#define BUSTER_X86_FORM_NUMBER (BUSTER_X86_FORM_FIXUP - 1)
_Static_assert(BUSTER_X86_FORM_TEMPLATES <= BUSTER_X86_FORM_FIXUP, "the fixup flag is past every form with a segment override");

// A form's segment override: 0 for none, 1 for FS and 2 for GS. These two are branches on the rare overrides, so
// that the scalar encoder pays a compare for them.
static uint32_t x86_form_segment(uint32_t form)
{
    const uint32_t number = form & BUSTER_X86_FORM_NUMBER;
    uint32_t result = 0;
    if (BUSTER_UNLIKELY(number >= BUSTER_X86_FORM_FS))
    {
        result = number >= BUSTER_X86_FORM_GS ? 2 : 1;
    }
    return result;
}

// The form table's row of a form, with or without a segment override (and with or without the fixup flag).
static const X86FormDescription* x86_form_description(uint32_t form)
{
    uint32_t row = form & BUSTER_X86_FORM_NUMBER;
    if (BUSTER_UNLIKELY(row >= BUSTER_X86_FORM_FS))
    {
        row -= row >= BUSTER_X86_FORM_GS ? BUSTER_X86_FORM_GS : BUSTER_X86_FORM_FS;
    }
    return &x86_form_descriptions[row];
}

// The fields of a marked instruction that a fixup patches: X86Instruction's displacement (a memory operand's, or a
// relative branch's) and its immediate (an imm32, or a 64-bit immediate or address; in 16- and 32-bit modes an imm16 or
// a moffs too, or a far pointer's offset, which its selector follows).
typedef enum X86FixupField
{
    X86_FIXUP_DISPLACEMENT,
    X86_FIXUP_IMMEDIATE,
} X86FixupField;

// How many bytes before the end of an instruction of the form the field starts: its offset is the end x86_encode
// reports minus this. The processor measures PC-relative values from the end, so a PC-relative fixup's addend from its
// field is the symbol's addend minus this too: -4 for call f and lea rax, [rip + x], -5 for cmp byte ptr [rip + x], 1
// and -8 for mov dword ptr [rip + x], imm32. The displacement and the immediate are the last fields, in that order. A
// memory operand's displacement field is 32-bit when it is RIP-relative, absolute or index-only; after a base, only when
// the displacement doesn't fit in 8 bits (in units of an EVEX form's N), so a symbolic one there needs a placeholder that
// doesn't, such as INT32_MIN (and the fixup overwrites it).
static __attribute__((unused)) uint32_t x86_fixup_distance(uint32_t form, X86FixupField field)
{
    const X86FormDescription* description = x86_form_description(form);
    uint32_t result = description->immediate_bytes;
    if (field == X86_FIXUP_DISPLACEMENT)
    {
        result += description->relative_bytes != 0 ? description->relative_bytes : 4;
    }
    return result;
}

// The same for an instruction with a 16-bit address (in 16-bit mode, or in 32-bit mode with
// BUSTER_X86_MODE_ADDRESS_SIZE), whose memory operand's displacement field is 16-bit: always for an absolute address,
// and after a base when the displacement doesn't fit in 8 bits (in units of an EVEX form's N, and modulo 2^16).
static __attribute__((unused)) uint32_t x86_fixup_distance_address16(uint32_t form, X86FixupField field)
{
    const X86FormDescription* description = x86_form_description(form);
    uint32_t result = description->immediate_bytes;
    if (field == X86_FIXUP_DISPLACEMENT)
    {
        result += description->relative_bytes != 0 ? description->relative_bytes : 2;
    }
    return result;
}

// x86_encode stores a group's fixup ends 16 at a time, each store after the ends before it, so the last store ends at
// most this many past the marked instructions' ends.
#define BUSTER_X86_ENCODER_FIXUP_STORE_ENDS 16
#define BUSTER_X86_ENCODER_FIXUP_CAPACITY(marked) ((uint64_t)(marked) + BUSTER_X86_ENCODER_FIXUP_STORE_ENDS)

// Builds the templates. Returns false if a form doesn't fit in one (a broken form table).
static __attribute__((unused)) bool x86_encoder_initialize(void);
// Encodes the instructions into `output`, which has room for `capacity` bytes (at least
// BUSTER_X86_ENCODER_CAPACITY(count)), and stores each one's offset in `offsets` if it isn't null. If `fixup_ends` isn't
// null, stores the end offset of each marked instruction (BUSTER_X86_FORM_FIXUP), in order, in an array with room for
// BUSTER_X86_ENCODER_FIXUP_CAPACITY(marked instructions). Writes nothing past output + capacity or past the fixup ends'
// capacity, but the bytes after the instructions' and the ends after the marked ones' may change. Returns the number of
// bytes, or a negative X86EncoderError (of the output's size or the count). Requires x86_encoder_initialize. It's the
// code generator's, an internal function: it takes instructions that x86_validate accepts, which instruction selection
// and register allocation make so, and checks nothing about them in its loop (fields the form doesn't have it ignores).
// With one x86_validate rejects, what it writes is undefined (in 16- and 32-bit modes, where such an instruction can
// have 16 bytes, even past the capacity), as is a form number past the form table's, whose template it would read.
static __attribute__((unused)) int64_t x86_encode(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity, uint32_t* offsets,
                                                  uint32_t* fixup_ends);
// The same, one instruction at a time from the form table, after x86_validate: the reference for testing. Its fixup
// ends need room for the marked instructions' alone.
static __attribute__((unused)) int64_t x86_encode_scalar(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity, uint32_t* offsets,
                                                         uint32_t* fixup_ends);
// The offsets and length x86_encode would produce, without a machine-code buffer. Requires the same initialized
// templates and validated 64-bit-mode instructions. The records and offsets are disjoint; offsets has room for count
// uint32_t values, and no padding is read or written. A zero count permits null pointers. Counts past
// BUSTER_X86_ENCODER_MAX_INSTRUCTIONS return X86_ENCODER_ERROR_TOO_MANY_INSTRUCTIONS without accessing either array.
// The scalar twin uses encode_one's independent form rules, with a bounded local buffer for one instruction.
static __attribute__((unused)) int64_t x86_measure(const X86Instruction* instructions, uint64_t count, uint32_t* offsets);
static __attribute__((unused)) int64_t x86_measure_scalar(const X86Instruction* instructions, uint64_t count, uint32_t* offsets);
// The index of the first instruction that can't be encoded, or count if they all can (a memory operand in a form whose
// template has no room for it among them: see x86_form_lacks_room). Requires x86_encoder_initialize.
static __attribute__((unused)) uint64_t x86_validate(const X86Instruction* instructions, uint64_t count);

// The same three for 32-bit addresses (the x32 ABI's), with templates of their own, which x86_encoder_initialize_address32
// builds (and all three require, validation too): each memory operand has a 67, and its base and index are 32-bit
// registers (and RIP is EIP). Instructions without a memory operand encode as for 64-bit addresses.
// x86_validate_address32 rejects too MPX's instructions with a memory operand, which are #UD with 67 in 64-bit mode,
// and memory operands in the forms whose template has no room for the 67 (see x86_form_lacks_room).
static __attribute__((unused)) bool x86_encoder_initialize_address32(void);
static __attribute__((unused)) int64_t x86_encode_address32(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity,
                                                            uint32_t* offsets, uint32_t* fixup_ends);
static __attribute__((unused)) int64_t x86_encode_scalar_address32(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity,
                                                                   uint32_t* offsets, uint32_t* fixup_ends);
static __attribute__((unused)) uint64_t x86_validate_address32(const X86Instruction* instructions, uint64_t count);

// The same three for APX's 32 general registers (Intel's Advanced Performance Extensions), with templates of their own,
// which x86_encoder_initialize_apx builds (and all three require): in a legacy form of maps 0 and 1 (an opcode without
// 0F, or 0F and one byte), a general register operand, base or index can be 16 to 31, which a REX2 prefix encodes in
// REX's place: D5, then a payload of M0, R4, X4, B4, W, R3, X3 and B3, where M0 replaces map 1's 0F; and in an EVEX
// form, a general register operand, base or general index can be too, which EVEX's R', V', B4 (P0's bit 3) and X4 (P1's
// bit 2, inverted) encode (x86_form_has_apx_registers). An instruction without such a register encodes as for
// x86_encode. The forms whose template has no room for REX2's payload take registers 0 to 15 alone, as do the XSAVE
// family (with which REX2 is #UD), maps 2 and 3 and VEX: APX gives their instructions with general registers EVEX forms
// for that (the EVEX forms of BMI, kmov, CMPccXADD, AMX's loads and stores, and the legacy ones in map 4, with ND, NF
// and ZU). APX's new instructions ccmp, ctest, cfcmov, push2 and pop2 are EVEX forms too, and pushp, popp and jmpabs
// always have REX2 (which the other entry points encode with registers 0 to 15).
static __attribute__((unused)) bool x86_encoder_initialize_apx(void);
static __attribute__((unused)) int64_t x86_encode_apx(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity, uint32_t* offsets,
                                                      uint32_t* fixup_ends);
static __attribute__((unused)) int64_t x86_encode_scalar_apx(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity,
                                                             uint32_t* offsets, uint32_t* fixup_ends);
static __attribute__((unused)) uint64_t x86_validate_apx(const X86Instruction* instructions, uint64_t count);

// The same three for 32-bit mode (protected and compatibility mode with a 32-bit code segment) and for 16-bit mode
// (real mode, virtual-8086 mode and protected mode with a 16-bit code segment), with templates of their own, which
// x86_encoder_initialize_legacy builds with the forms' properties there (x86_form_legacy), and all six require. An
// instruction is the same as in 64-bit mode, its operand sizes those of its form, but without REX: registers 0 to 7 (AH
// to BH with BUSTER_X86_REGISTER_HIGH_BYTE, as there's no SPL to DIL), no 64-bit operands and no RIP, so that
// [displacement] is ModRM's own absolute address. The mode decides the prefixes: a 16-bit form's 66 is the operand
// size's, which 16-bit mode has without it, while a 32-bit form takes one there, and BUSTER_X86_MODE_ADDRESS_SIZE gives
// an instruction the other address size (and a 67). The forms of BUSTER_X86_FORMS_LEGACY_MODES exist in these modes
// alone, and those of 64-bit mode alone (x86_form_exists_in) don't exist in them. x86_encode_mode32 and
// x86_encode_mode16 take what x86_validate_32 and x86_validate_16 accept, as x86_encode does, and those reject too, among
// others, a 16-bit address other than ModRM's, a VSIB, SIB or MPX operand with one, BUSTER_X86_MODE_ADDRESS_SIZE on an
// instruction without an address, a segment override where there can't be one, an instruction longer than 15 bytes,
// and a 16-bit address's displacement outside [-32768, 65535].
static __attribute__((unused)) bool x86_encoder_initialize_legacy(void);
static __attribute__((unused)) int64_t x86_encode_mode32(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity,
                                                         uint32_t* offsets, uint32_t* fixup_ends);
static __attribute__((unused)) int64_t x86_encode_scalar_32(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity,
                                                            uint32_t* offsets, uint32_t* fixup_ends);
static __attribute__((unused)) uint64_t x86_validate_32(const X86Instruction* instructions, uint64_t count);
static __attribute__((unused)) int64_t x86_encode_mode16(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity,
                                                         uint32_t* offsets, uint32_t* fixup_ends);
static __attribute__((unused)) int64_t x86_encode_scalar_16(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity,
                                                            uint32_t* offsets, uint32_t* fixup_ends);
static __attribute__((unused)) uint64_t x86_validate_16(const X86Instruction* instructions, uint64_t count);

// Which entry point an internal function works for (a constant parameter): x86_encode's 64-bit addresses and
// registers, x86_encode_address32's 32-bit addresses, x86_encode_apx's 32 general registers, or 32-bit or 16-bit mode.
typedef enum X86EncoderMode
{
    X86_ENCODER_MODE_64,
    X86_ENCODER_MODE_ADDRESS32,
    X86_ENCODER_MODE_APX,
    X86_ENCODER_MODE_32,
    X86_ENCODER_MODE_16,
} X86EncoderMode;

// Whether the mode is 32-bit or 16-bit mode.
static bool x86_mode_is_legacy(X86EncoderMode encoder)
{
    return encoder == X86_ENCODER_MODE_32 || encoder == X86_ENCODER_MODE_16;
}

// The opcode's byte `i`, the first in its high byte; 0 past its bytes (X86_FORM_NONE has none, and x86_opcode_map and
// x86_legacy_map read the first byte of every form they're given).
static uint8_t x86_opcode_byte(const X86FormDescription* form, uint32_t i)
{
    return i < form->opcode_bytes ? (uint8_t)(form->opcode >> (8 * (form->opcode_bytes - 1 - i))) : 0;
}

// Whether one of the form's legacy prefixes is `byte`.
static bool x86_form_has_prefix(const X86FormDescription* form, uint8_t byte)
{
    bool result = false;
    for (uint32_t prefixes = form->prefixes; prefixes != 0; prefixes >>= 8)
    {
        result = result || (uint8_t)prefixes == byte;
    }
    return result;
}

// Whether the form is 3DNow!'s: 0F 0F with the operation in the opcode's last byte, which comes after ModRM, SIB and the
// displacement, in an 8-bit immediate's place. The record's immediate is that byte (x86_validate checks it), and the
// encoders write it as an immediate.
static bool x86_form_has_operation_byte(const X86FormDescription* form)
{
    return form->family == BUSTER_X86_FAMILY_LEGACY && form->opcode_bytes == 3 && (form->opcode >> 8) == 0x0f0f;
}

// A VEX or EVEX form's opcode map (VEX.mmmmm, EVEX.mmm): 1 for 0F, 2 for 0F 38 and 3 for 0F 3A, from the escape bytes
// of its opcode, and maps 4 to 7 (EVEX's 5 and 6, and APX's 4 and 7), VEX's 5 and 7 and XOP's 8 to 0A from the number
// in the escape's place; 0 if they're none of those.
static uint8_t x86_opcode_map(const X86FormDescription* form)
{
    uint8_t result = 0;
    const uint8_t first = x86_opcode_byte(form, 0);
    if (form->opcode_bytes == 2 && first == 0x0f)
    {
        result = 1;
    }
    else if (form->opcode_bytes == 2 && form->family == BUSTER_X86_FAMILY_EVEX && first >= 4 && first <= 7)
    {
        result = first;
    }
    else if (form->opcode_bytes == 2 && form->family == BUSTER_X86_FAMILY_VEX && first >= 5 && first <= 10)
    {
        result = first;
    }
    else if (form->opcode_bytes == 3 && x86_opcode_byte(form, 0) == 0x0f && x86_opcode_byte(form, 1) == 0x38)
    {
        result = 2;
    }
    else if (form->opcode_bytes == 3 && x86_opcode_byte(form, 0) == 0x0f && x86_opcode_byte(form, 1) == 0x3a)
    {
        result = 3;
    }
    return result;
}

// VEX.pp and EVEX.pp: the legacy prefix the form implies (none, 66, F3 or F2).
static uint8_t x86_prefix_pp(const X86FormDescription* form)
{
    return form->prefixes == 0x66 ? 1 : form->prefixes == 0xf3 ? 2 : form->prefixes == 0xf2 ? 3 : 0;
}

// A legacy form's opcode map: 0 without 0F, 2 and 3 for 0F 38 and 0F 3A, and 1 for the rest of 0F (with a second
// byte that is ModRM's, as in the fences, or another 0F, 3DNow!'s).
static uint8_t x86_legacy_map(const X86FormDescription* form)
{
    const uint8_t second = form->opcode_bytes >= 3 ? x86_opcode_byte(form, 1) : 0;
    return x86_opcode_byte(form, 0) != 0x0f ? 0 : second == 0x38 ? 2 : second == 0x3a ? 3 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// 16- and 32-bit modes

// A form's properties in 16- and 32-bit modes (x86_form_legacy, which x86_encoder_initialize_legacy builds): whether it
// exists there, and how its operand size takes the mode. A 16-bit form's 66 is the operand size's, which 16-bit mode
// has without it, and a 32-bit form (the same instruction without the 66) takes one there; the others keep their
// prefixes in every mode (a mandatory 66 is the opcode's, as in ADCX). Whether BUSTER_X86_MODE_ADDRESS_SIZE can give
// its instruction the other address size without a memory operand (x86_form_has_implicit_address). And a moffs form's
// address size, which its moffs's size fixes: a 16-bit one takes a 67 in 32-bit mode, and a 32-bit one in 16-bit mode.
#define BUSTER_X86_LEGACY_IN_32 0x01
#define BUSTER_X86_LEGACY_IN_16 0x02
#define BUSTER_X86_LEGACY_SIZE_16 0x04
#define BUSTER_X86_LEGACY_SIZE_32 0x08
#define BUSTER_X86_LEGACY_IMPLICIT_ADDRESS 0x10
#define BUSTER_X86_LEGACY_ADDRESS_16 0x20
#define BUSTER_X86_LEGACY_ADDRESS_32 0x40
static uint8_t x86_form_legacy[X86_FORM_COUNT];
static bool x86_encoder_initialized_legacy;

// Whether one of the form's operands is a vector, opmask, bound, MMX or tile register (a VSIB operand's index too),
// which the operand size doesn't change.
static bool x86_form_has_vector_operand(const X86FormDescription* form)
{
    const uint16_t operands[] = { form->reg, form->vvvv, form->rm };
    bool result = false;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operands); i += 1)
    {
        const uint8_t class = operands[i] & BUSTER_X86_OPERAND_CLASS;
        result = result || (class >= BUSTER_X86_CLASS_XMM && class <= BUSTER_X86_CLASS_OPMASK) || class == BUSTER_X86_CLASS_BND || class == BUSTER_X86_CLASS_MMX ||
                 class == BUSTER_X86_CLASS_TILE;
    }
    return result;
}

// Whether one of the form's operands is of the class.
static bool x86_form_has_class(const X86FormDescription* form, uint8_t class)
{
    return (form->reg & BUSTER_X86_OPERAND_CLASS) == class || (form->vvvv & BUSTER_X86_OPERAND_CLASS) == class || (form->rm & BUSTER_X86_OPERAND_CLASS) == class;
}

// Whether the form's 66 is the operand size's, so that it's a 16-bit form: a legacy form without vector operands, of
// map 0 (one opcode byte, x87's too) or with a 16-bit general register, a 16-bit memory operand, a rel16 or a 6-byte
// pseudo-descriptor (lgdtw's and the others', whose base is 24-bit). The 66 of the others (ADCX, CLWB, TDCALL ...) is
// part of their opcode.
static bool x86_form_has_size_prefix(const X86FormDescription* form)
{
    const bool narrow = x86_form_has_class(form, BUSTER_X86_CLASS_GPR16) || form->operand_bytes == 2 || form->relative_bytes == 2 || form->operand_bytes == 6;
    return form->family == BUSTER_X86_FAMILY_LEGACY && form->opcode_bytes != 0 && x86_form_has_prefix(form, 0x66) && !x86_form_has_vector_operand(form) &&
           (x86_legacy_map(form) == 0 || narrow);
}

// The prefixes without the first `byte` among them, in their order.
static uint32_t x86_prefixes_without(uint32_t prefixes, uint8_t byte)
{
    uint32_t result = 0;
    uint32_t shift = 0;
    bool removed = false;
    for (; prefixes != 0; prefixes >>= 8)
    {
        const bool skip = !removed && (uint8_t)prefixes == byte;
        result |= skip ? 0 : (prefixes & 0xff) << shift;
        shift += skip ? 0 : 8;
        removed = removed || skip;
    }
    return result;
}

// Whether two operands are the same but for the size of a general register: a 16-bit one in `narrow`, a 32-bit one in
// `wide`.
static bool x86_operands_differ_in_size(uint16_t narrow, uint16_t wide)
{
    const uint16_t kind = BUSTER_X86_OPERAND_REGISTER | BUSTER_X86_OPERAND_MEMORY;
    const bool sizes = (narrow & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR16 && (wide & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR32 &&
                       (narrow & ~BUSTER_X86_OPERAND_CLASS) == (wide & ~BUSTER_X86_OPERAND_CLASS) && (narrow & kind) != 0;
    return narrow == wide || sizes;
}

// Whether `wide` is the 32-bit form of `narrow`, a 16-bit one (x86_form_has_size_prefix): the same instruction without
// the operand size's 66, with 32-bit general registers, an imm32 and a rel32 where `narrow` has 16-bit ones, and a
// ptr16:32 where it has a ptr16:16 (and memory operands of any size).
static bool x86_forms_differ_in_size(const X86FormDescription* narrow, const X86FormDescription* wide)
{
    const bool immediates = narrow->immediate_bytes == wide->immediate_bytes || (narrow->immediate_bytes == 2 && wide->immediate_bytes == 4) ||
                            (narrow->syntax == X86_SYNTAX_FAR_POINTER && narrow->immediate_bytes == 4 && wide->immediate_bytes == 6);
    const bool relatives = narrow->relative_bytes == wide->relative_bytes || (narrow->relative_bytes == 2 && wide->relative_bytes == 4);
    return wide->family == BUSTER_X86_FAMILY_LEGACY && wide->opcode_bytes == narrow->opcode_bytes && wide->opcode == narrow->opcode &&
           wide->encoding == narrow->encoding && wide->w == 0 && narrow->w == 0 && wide->syntax == narrow->syntax &&
           wide->prefixes == x86_prefixes_without(narrow->prefixes, 0x66) && immediates && relatives && x86_operands_differ_in_size(narrow->reg, wide->reg) &&
           x86_operands_differ_in_size(narrow->rm, wide->rm);
}

// Whether the form's instruction makes addresses of the address size itself, so that BUSTER_X86_MODE_ADDRESS_SIZE gives
// it the other size without a memory operand: the string instructions (rSI and rDI, and the rep prefixes' rCX), xlat,
// jcxz and loop (rCX), MASKMOVQ's, MASKMOVDQU's and VMASKMOVDQU's rDI, PadLock's and Zhaoxin's rSI and rDI (0F A6 and
// 0F A7), the system instructions with an address in rAX (monitor, monitorx, clzero, invlpga, vmrun, vmload, vmsave,
// invlpgb, pvalidate, psmash, rmpadjust, rmpupdate, rmpquery and rmpread), and umonitor's 32-bit form, whose register
// is the address (16-bit with a 16-bit address).
static bool x86_form_has_implicit_address(const X86FormDescription* form)
{
    const uint32_t opcode = form->opcode;
    const bool one = form->family == BUSTER_X86_FAMILY_LEGACY && form->opcode_bytes == 1;
    const bool three = form->family == BUSTER_X86_FAMILY_LEGACY && form->opcode_bytes == 3;
    const bool string = one && ((opcode >= 0xa4 && opcode <= 0xa7) || (opcode >= 0xaa && opcode <= 0xaf) || (opcode >= 0x6c && opcode <= 0x6f));
    const bool counted = one && (opcode == 0xd7 || (opcode >= 0xe0 && opcode <= 0xe3));
    const bool masked = form->opcode_bytes == 2 && opcode == 0x0ff7;
    const bool padlock = three && ((opcode >> 8) == 0x0fa6 || (opcode >> 8) == 0x0fa7);
    // 0F 01 C8, FA and FC; DF, D8, DA and DB; and FE, FF and FD but TLBSYNC (FF) and RDPRU (FD), which have no prefix.
    const uint8_t last = (uint8_t)opcode;
    const bool system_byte = last == 0xc8 || last == 0xfa || last == 0xfc || last == 0xdf || last == 0xd8 || last == 0xda || last == 0xdb || last == 0xfe ||
                             ((last == 0xfd || last == 0xff) && form->prefixes != 0);
    const bool system = three && (opcode >> 8) == 0x0f01 && system_byte;
    const bool umonitor = form == &x86_form_descriptions[X86_FORM_UMONITOR_R32];
    return string || counted || masked || padlock || system || umonitor;
}

// Whether, in 16- and 32-bit modes, the form's instructions can have a segment override in their decorations' bits 3 to
// 5 (see BUSTER_X86_DECORATION_SEGMENT): those of the forms with an address (a memory operand, a moffs, or one the
// instruction makes itself, but jcxz's and loop's, which make none), but NOTRACK's, whose 3E would come after it. The
// other forms' decorations keep their meaning, an EVEX form's rounding mode in bits 5 and 6 among it. Requires
// x86_encoder_initialize_legacy.
static bool x86_form_takes_override(const X86FormDescription* form)
{
    const uint8_t properties = x86_form_legacy[form - x86_form_descriptions];
    const bool moffs = (properties & (BUSTER_X86_LEGACY_ADDRESS_16 | BUSTER_X86_LEGACY_ADDRESS_32)) != 0;
    const bool implicit = (properties & BUSTER_X86_LEGACY_IMPLICIT_ADDRESS) && form->syntax != X86_SYNTAX_REL;
    const bool notrack = form->family == BUSTER_X86_FAMILY_LEGACY && x86_form_has_prefix(form, 0x3e);
    return ((form->rm & BUSTER_X86_OPERAND_MEMORY) || moffs || implicit) && !notrack;
}

// The forms of 64-bit mode alone that x86_form_exists_in can't tell by their operands: 64-bit pushes and pops of the
// flags, the descriptor table registers' 10-byte operands, FSGSBASE, swapgs, PREFETCHIT0 and PREFETCHIT1 (which only
// RIP-relative addresses make prefetches), LKGS, FRED's erets and eretu, MSRLIST, UINTR, TDX's seamcall, seamret and
// seamops, pbndkb, SEV-SNP's instructions but pvalidate, AMX's tile configuration and tilerelease, the W1 forms of
// vpcmpestri and vpcmpestrm (vpcmpestriq and vpcmpestrmq), MOVRS, AVX10.2's vmovrs (EVEX's map 5, 6F) and CMPccXADD
// (VEX's map 2, E0 to EF).
static bool x86_form_is_64_bit_only(uint32_t form_index)
{
    static const uint16_t forms[] = {
        X86_FORM_PUSHFQ, X86_FORM_POPFQ, X86_FORM_LGDT_M80, X86_FORM_LIDT_M80, X86_FORM_SGDT_M80, X86_FORM_SIDT_M80, X86_FORM_RDFSBASE_R32,
        X86_FORM_RDGSBASE_R32, X86_FORM_WRFSBASE_R32, X86_FORM_WRGSBASE_R32, X86_FORM_SWAPGS, X86_FORM_PREFETCHIT0_M8, X86_FORM_PREFETCHIT1_M8,
        X86_FORM_LKGS_RM16, X86_FORM_ERETS, X86_FORM_ERETU, X86_FORM_RDMSRLIST, X86_FORM_WRMSRLIST, X86_FORM_CLUI, X86_FORM_STUI, X86_FORM_TESTUI,
        X86_FORM_UIRET, X86_FORM_SEAMCALL, X86_FORM_SEAMRET, X86_FORM_SEAMOPS, X86_FORM_PBNDKB, X86_FORM_PSMASH_RAX, X86_FORM_RMPADJUST_RAX_RCX_RDX,
        X86_FORM_RMPUPDATE_RAX_RCX, X86_FORM_RMPQUERY_RAX_RCX_RDX, X86_FORM_RMPREAD_RAX_RCX, X86_FORM_LDTILECFG_M512, X86_FORM_STTILECFG_M512,
        X86_FORM_TILERELEASE, X86_FORM_VPCMPESTRMQ_X_XM128_IMM8, X86_FORM_VPCMPESTRIQ_X_XM128_IMM8, X86_FORM_MOVRS_R8_M8, X86_FORM_MOVRS_R16_M16,
        X86_FORM_MOVRS_R32_M32,
    };
    const X86FormDescription* form = &x86_form_descriptions[form_index];
    const uint8_t last = (uint8_t)form->opcode;
    const bool vmovrs = form->family == BUSTER_X86_FAMILY_EVEX && x86_opcode_map(form) == 5 && last == 0x6f;
    const bool cmpccxadd = form->family == BUSTER_X86_FAMILY_VEX && x86_opcode_map(form) == 2 && last >= 0xe0 && last <= 0xef;
    bool result = vmovrs || cmpccxadd;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(forms); i += 1)
    {
        result = result || forms[i] == form_index;
    }
    return result;
}

// Whether the form exists in 64-bit mode (`legacy` false) or in 16- or 32-bit mode: the forms of
// BUSTER_X86_FORMS_LEGACY_MODES exist in those alone, and these in 64-bit mode alone: REX.W's (and REX2's) legacy
// forms, those with a 64-bit general register (a 64-bit push or pop of a register or memory, a near branch through one,
// or a 64-bit address's register), a 64-bit immediate or address, or a tile, APX's EVEX forms (map 4, and those without
// vector registers or an opmask, which AVX-512's vfpclass of a broadcast element has), udb (D6, which salc is in the
// others), and x86_form_is_64_bit_only's.
static bool x86_form_exists_in(uint32_t form_index, bool legacy)
{
    const X86FormDescription* form = &x86_form_descriptions[form_index];
    const bool vectors = x86_form_has_class(form, BUSTER_X86_CLASS_XMM) || x86_form_has_class(form, BUSTER_X86_CLASS_YMM) ||
                         x86_form_has_class(form, BUSTER_X86_CLASS_ZMM);
    const bool apx = form->family == BUSTER_X86_FAMILY_EVEX && (x86_opcode_map(form) == 4 || (!vectors && !(form->evex & BUSTER_X86_EVEX_MASK)));
    const bool rex = form->family == BUSTER_X86_FAMILY_LEGACY && (form->w || x86_form_has_prefix(form, 0xd5));
    const uint8_t digit = form->encoding & 7;
    const bool stack = form->family == BUSTER_X86_FAMILY_LEGACY && form->opcode_bytes == 1 && form->operand_bytes == 8 && (form->encoding & BUSTER_X86_ENCODING_DIGIT) &&
                       ((form->opcode == 0xff && (digit == 2 || digit == 4 || digit == 6)) || form->opcode == 0x8f);
    const bool only64 = rex || apx || stack || form_index == X86_FORM_UDB || x86_form_has_class(form, BUSTER_X86_CLASS_GPR64) ||
                        x86_form_has_class(form, BUSTER_X86_CLASS_TILE) || form->immediate_bytes == 8 || x86_form_is_64_bit_only(form_index);
    const bool legacy_only = form_index >= BUSTER_X86_FORM_LEGACY_MODES;
    return legacy ? !only64 : !legacy_only;
}

// Whether the form table's row exists in the entry point's mode: in 16- and 32-bit modes, as x86_form_legacy says (see
// x86_form_exists_in), and in 64-bit mode's entry points, the rows before BUSTER_X86_FORM_LEGACY_MODES.
static bool x86_form_exists_in_mode(uint32_t row, X86EncoderMode encoder)
{
    const uint8_t in_mode = encoder == X86_ENCODER_MODE_16 ? BUSTER_X86_LEGACY_IN_16 : BUSTER_X86_LEGACY_IN_32;
    return x86_mode_is_legacy(encoder) ? (x86_form_legacy[row] & in_mode) != 0 : row < BUSTER_X86_FORM_LEGACY_MODES;
}

// Builds x86_form_legacy (for x86_encoder_initialize_legacy). A legacy form without a 66 or vector operands is a 32-bit
// form if it has a 32-bit general register (but the moves to and from control, debug and test registers, always 32-bit
// there) or a rel32, or x86_forms_differ_in_size finds its 16-bit form, but those that keep their default operand size
// in every mode (ret, enter, leave, push of an imm8, and push and pop of FS and GS: 16-bit in 16-bit mode, 32-bit in
// 32-bit mode), whose 16- and 32-bit forms are other rows, and those whose 32-bit register W alone sizes, which a 66
// would make another instruction of or leave as it is (CRC32 of a byte, ADOX, MOVNTI, MOVDIRI, PTWRITE, UMWAIT, CET's
// incsspd, rdsspd and wrssd, RAO-INT's aadd, aor and axor, vmread and vmwrite, and rdpid: GNU as's IgnoreSize ones), or
// the address size does (umonitor, enqcmd and enqcmds). And push of an imm32 has none in 16-bit mode, which pushd's row
// is.
static void build_legacy_properties(void)
{
    // The 16-bit forms by their opcode's last byte, which their 32-bit forms share.
    static uint16_t narrow[X86_FORM_COUNT];
    static uint16_t bucket_start[257];
    uint32_t counts[256] = { 0 };
    for (uint32_t i = 1; i < X86_FORM_COUNT; i += 1)
    {
        counts[(uint8_t)x86_form_descriptions[i].opcode] += x86_form_has_size_prefix(&x86_form_descriptions[i]);
    }
    bucket_start[0] = 0;
    for (uint32_t b = 0; b < 256; b += 1)
    {
        bucket_start[b + 1] = (uint16_t)(bucket_start[b] + counts[b]);
        counts[b] = 0;
    }
    for (uint32_t i = 1; i < X86_FORM_COUNT; i += 1)
    {
        const uint8_t b = (uint8_t)x86_form_descriptions[i].opcode;
        if (x86_form_has_size_prefix(&x86_form_descriptions[i]))
        {
            narrow[bucket_start[b] + counts[b]] = (uint16_t)i;
            counts[b] += 1;
        }
    }

    static const uint16_t unsized[] = {
        X86_FORM_RET,           X86_FORM_RET_IMM16,       X86_FORM_ENTER,          X86_FORM_LEAVE,           X86_FORM_PUSH_IMM8,     X86_FORM_PUSH_FS,
        X86_FORM_POP_FS,        X86_FORM_PUSH_GS,         X86_FORM_POP_GS,         X86_FORM_CRC32_R32_RM8,   X86_FORM_ADOX_R32_RM32, X86_FORM_MOVNTI_M32_R32,
        X86_FORM_MOVDIRI_M32_R32, X86_FORM_PTWRITE_RM32,  X86_FORM_UMWAIT_R32,     X86_FORM_INCSSPD_R32,     X86_FORM_RDSSPD_R32,    X86_FORM_WRSSD_M32_R32,
        X86_FORM_AADD_M32_R32,  X86_FORM_AOR_M32_R32,     X86_FORM_AXOR_M32_R32,   X86_FORM_VMREAD_RM32_R32, X86_FORM_VMWRITE_R32_RM32, X86_FORM_RDPID_R32,
        X86_FORM_UMONITOR_R32,  X86_FORM_ENQCMD_R32_M512, X86_FORM_ENQCMDS_R32_M512,
    };
    for (uint32_t i = 0; i < X86_FORM_COUNT; i += 1)
    {
        const X86FormDescription* form = &x86_form_descriptions[i];
        uint8_t properties = 0;
        if (x86_form_exists_in(i, true))
        {
            properties |= BUSTER_X86_LEGACY_IN_32 | (i == X86_FORM_PUSH_IMM32 ? 0 : BUSTER_X86_LEGACY_IN_16);
        }
        bool wide = false;
        if (form->family == BUSTER_X86_FAMILY_LEGACY && form->opcode_bytes != 0 && !x86_form_has_prefix(form, 0x66) && !x86_form_has_vector_operand(form))
        {
            const uint8_t b = (uint8_t)form->opcode;
            wide = (x86_form_has_class(form, BUSTER_X86_CLASS_GPR32) && !x86_form_has_class(form, BUSTER_X86_CLASS_CONTROL) &&
                    !x86_form_has_class(form, BUSTER_X86_CLASS_DEBUG)) ||
                   form->relative_bytes == 4;
            for (uint32_t j = bucket_start[b]; j < bucket_start[b + 1]; j += 1)
            {
                wide = wide || x86_forms_differ_in_size(&x86_form_descriptions[narrow[j]], form);
            }
            for (uint32_t j = 0; j < BUSTER_ARRAY_LENGTH(unsized); j += 1)
            {
                wide = wide && unsized[j] != i;
            }
        }
        const bool moffs = form->syntax == X86_SYNTAX_ACC_MOFFS || form->syntax == X86_SYNTAX_MOFFS_ACC;
        properties |= x86_form_has_size_prefix(form) ? BUSTER_X86_LEGACY_SIZE_16 : 0;
        properties |= wide ? BUSTER_X86_LEGACY_SIZE_32 : 0;
        properties |= x86_form_has_implicit_address(form) ? BUSTER_X86_LEGACY_IMPLICIT_ADDRESS : 0;
        properties |= moffs && form->immediate_bytes == 2 ? BUSTER_X86_LEGACY_ADDRESS_16 : moffs && form->immediate_bytes == 4 ? BUSTER_X86_LEGACY_ADDRESS_32 : 0;
        x86_form_legacy[i] = properties;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Templates

// An instruction's canonical slot: the bytes of its fields, in a fixed order. The record's first 8 bytes are the
// immediate and displacement as they are (an EVEX form's 8-bit displacement divided by its N), and compute_fields
// makes the rest. A template's layout picks from these.
#define BUSTER_X86_CANONICAL_IMMEDIATE 0
#define BUSTER_X86_CANONICAL_DISPLACEMENT 4
#define BUSTER_X86_CANONICAL_MODRM 8
#define BUSTER_X86_CANONICAL_SIB 9
// REX.R, REX.X and REX.B; the template has the rest of the REX byte.
#define BUSTER_X86_CANONICAL_REX 10
// The rm register's low 3 bits, which a +r form adds to its last opcode byte.
#define BUSTER_X86_CANONICAL_OPCODE_REGISTER 11
// What a VEX or EVEX prefix's bytes take from the operands; the template has the rest, with the inverted fields as
// ones (as they are for registers below 8), and the canonical byte's ones flip them. 12 is VEX's first byte, which
// VEX2 turns from C4 into C5; 13 is P0 (R, X, B and EVEX's R'), which VEX2 doesn't have; 14 is P1 (vvvv, and VEX2's
// R, in the place of VEX3's W); 15 is EVEX's P2 (zeroing, the rounding mode, V' and the opmask).
#define BUSTER_X86_CANONICAL_VEX_ESCAPE 12
#define BUSTER_X86_CANONICAL_PREFIX_P0 13
#define BUSTER_X86_CANONICAL_PREFIX_P1 14
#define BUSTER_X86_CANONICAL_PREFIX_P2 15
// The layout of a position that only has the template's byte: vpshufb makes it zero (bit 7), and its vpshufbitqmb
// index is presence bit 0 (the first immediate byte's, always set).
#define BUSTER_X86_LAYOUT_STATIC 0x80
// The layout of a position that isn't part of the instruction: its vpshufbitqmb index is presence bit 63 (always
// clear), and what vpshufb puts there doesn't matter.
#define BUSTER_X86_LAYOUT_ABSENT 0x3f
// The presence bit of a 32-bit address's 67, which compute_fields sets for a memory operand in x86_encode_address32's
// loop, and in 16- and 32-bit modes' for BUSTER_X86_MODE_ADDRESS_SIZE (no other presence has bit 23), and the layout of
// its position: the template's byte, as for a static one, present only then.
#define BUSTER_X86_PRESENCE_ADDRESS32 23
#define BUSTER_X86_LAYOUT_ADDRESS32 (BUSTER_X86_LAYOUT_STATIC | BUSTER_X86_PRESENCE_ADDRESS32)
// In 16- and 32-bit modes, which have no REX, REX's canonical byte and presence bit are the segment override's in an
// instruction's decorations, in the templates of the forms that take one (x86_form_takes_override: a position among the
// prefixes), and CR8's LOCK prefix in a move to or from a control register's (in REX's position: see build_template).
// A valid instruction has neither REX.RXB there (registers 0 to 7) nor REX (no SPL to DIL), but CR8's REX.R.
#define BUSTER_X86_LAYOUT_OVERRIDE BUSTER_X86_CANONICAL_REX
// APX's REX2, in x86_encode_apx's templates of the legacy forms of maps 0 and 1 (BUSTER_X86_TEMPLATE_REX2), which have no
// VEX or EVEX prefix to take canonical bytes 12-15: REX's position (canonical byte 10) has D5 instead, map 1's 0F is
// canonical byte 13, which turns it into the payload, and in map 0, the payload has a position of its own after REX's,
// canonical byte 15, present only then.
#define BUSTER_X86_CANONICAL_REX2_MAP1 13
#define BUSTER_X86_CANONICAL_REX2_PAYLOAD 15
// The canonical bytes that every instruction whose layout uses them has: the immediate, ModRM and the opcode
// register (and from the template's flags, the prefix bytes but VEX's P0). Whether the displacement, SIB, REX and
// VEX's P0 are there depends on the operands.
#define BUSTER_X86_CANONICAL_ALWAYS_PRESENT 0x090f

// A template's checks and flags: the third and fourth dwords of its bytes (bytes[8-11] and bytes[12-15]). A form's
// own bytes all come before position 8 (a segment override, prefixes, REX or the VEX or EVEX prefix, opcode and ModRM
// end by position 7), and Phase III only takes the template's bytes there.
#define BUSTER_X86_TEMPLATE_CHECKS_OFFSET 8
#define BUSTER_X86_TEMPLATE_FLAGS_OFFSET 12
#define BUSTER_X86_TEMPLATE_STATIC_POSITIONS 8
// The checks: the bits that must be zero in an instruction's reg, rm, vvvv and decorations bytes (as x86_validate's
// template_checks has them, in one dword): registers past the operand's class's last one (or any, if the form has no
// such operand), and the decorations the form can't have. Of them, compute_fields takes only whether a 16- or 32-bit
// mode's form can take a segment override in its decorations (bit 27: see mode_template_checks).
// The flags. How many bytes the form always has (with a VEX3 prefix), besides REX: prefixes, opcode, ModRM, immediate
// and a static displacement.
#define BUSTER_X86_TEMPLATE_STATIC_LENGTH 0x0f
// The presence bits (canonical bytes 4-7) of a displacement field the form always has, as many bytes of it as its
// layout uses: a relative branch's displacement, or a 64-bit immediate's high half.
#define BUSTER_X86_TEMPLATE_STATIC_DISPLACEMENT 0xf0
// A VEX or EVEX form: REX's presence bit doesn't count (the layout has no REX byte). Not in 16- and 32-bit modes, where
// REX's position is a segment override's or CR8's LOCK's (see BUSTER_X86_LAYOUT_OVERRIDE): the length counts its
// presence bit, which only CR8 sets before compute_fields adds the override's (a valid instruction has no REX there).
#define BUSTER_X86_TEMPLATE_NO_REX 0x400
// The presence bits of canonical bytes 12, 14 and 15, which a form's layout uses only if it always has them.
#define BUSTER_X86_TEMPLATE_PREFIX_PRESENT 0xd000
// A VEX form that can be VEX2 (W0, opcode map 0F): it is if the operands need neither X nor B. Its static length
// counts VEX3's three bytes, and VEX2 takes one off. Bit 13, P0's presence bit, which the template's flags don't
// otherwise have.
#define BUSTER_X86_TEMPLATE_VEX2 0x2000
// REX.W: the instruction always has a REX prefix. Bit 19, next to REX.RXB in the canonical dword, so that one test of
// both says whether there's a REX prefix.
#define BUSTER_X86_TEMPLATE_REX_ALWAYS 0x80000
// ModRM.reg or the rm register is a byte register, so 4 to 7 are SPL to DIL (with a REX prefix) or AH to BH.
#define BUSTER_X86_TEMPLATE_BYTE_REG 0x1000000
#define BUSTER_X86_TEMPLATE_BYTE_RM 0x2000000
// The template takes no memory operand: the form has none (a form without an rm operand neither), or the template is
// the one of the form without a memory operand's bytes it has no room for (see build_mode_template and
// x86_form_lacks_room, for x86_validate).
#define BUSTER_X86_TEMPLATE_NO_MEMORY 0x04000000
// One more than the log2 of N, which an EVEX form's 8-bit displacements are counted in (1 for the other forms), in bits
// 29-31, so that a shift down by 29 makes it the count that rotates bit log2(N) of compute_fields's compressibility into
// the sign.
#define BUSTER_X86_TEMPLATE_DISP8_SCALE 29
#define BUSTER_X86_TEMPLATE_DISP8_SCALE_MASK 0xe0000000u
// In x86_encode_apx's templates: one with REX2's positions (bit 8: the always present ModRM's presence bit, which the
// flags may set), whose general registers can be 16 to 31.
#define BUSTER_X86_TEMPLATE_REX2 0x100
// In x86_encode_apx's templates of EVEX forms, which have no REX (so REX_ALWAYS's bit is free): the rm operand is a
// general register, whose bit 4 is EVEX's B4 (P0's bit 3), where a vector register's is X̄ (P0's bit 6). And in REX.R's
// bit, free there too: the index is a general register (a SIB operand's as well, whose 4 is none, so GENERAL_INDEX
// can't say it), whose bit 4 is X4 (P1's bit 2, inverted), where a VSIB operand's vector register's is V' (P2's bit 3).
#define BUSTER_X86_TEMPLATE_GENERAL_RM BUSTER_X86_TEMPLATE_REX_ALWAYS
#define BUSTER_X86_TEMPLATE_GENERAL_INDEX_4 0x40000

// A form's template. Positions follow the instruction's order: prefixes, REX (or the VEX or EVEX prefix), opcode,
// ModRM, SIB, displacement, immediate. A position's byte is bytes[position] ^ canonical[layout[position]] (or just
// bytes[position], for BUSTER_X86_LAYOUT_STATIC), and it's part of the instruction if that canonical byte is.
typedef struct X86Template X86Template;
struct X86Template
{
    uint8_t bytes[16];
    uint8_t layout[16];
};
_Static_assert(sizeof(X86Template) == 32, "a template is one YMM load");

// A form's template as the vectorized encoder loads it: the template, and its two halves again the other way around,
// so that one 32-byte load has its bytes in the low 128-bit lane (offset 0, an even slot of a quad's) or the high one
// (offset 32, an odd slot's), where Phase I wants them: then a quad's bytes are one blend of two loads' lanes, which any
// vector pipe runs, rather than a shuffle across lanes, and the template's flags arrive a few cycles sooner (see
// encode_group). One cache line per form.
typedef struct X86TemplateEntry X86TemplateEntry;
struct X86TemplateEntry
{
    X86Template template;
    uint8_t layout[16];
    uint8_t bytes[16];
};
_Static_assert(sizeof(X86TemplateEntry) == 64, "a template entry is a cache line");

// The templates, with BUSTER_X86_TEMPLATE_PADDING unused entries on either side: the vectorized encoder loads 16 bytes
// of a template into a lane of a ZMM register with a masked load that spans up to 48 bytes on either side of them. A
// marked form's are a copy at BUSTER_X86_FORM_FIXUP and up, so that the vectorized encoder looks the form up as the
// record has it: taking the flag off each form cost the loop 2.4% on Zen 5. Between the two, the pages nothing touches
// stay unmapped.
#define BUSTER_X86_TEMPLATE_PADDING 1
static X86TemplateEntry x86_template_storage[BUSTER_X86_TEMPLATE_PADDING + BUSTER_X86_FORM_FIXUP + BUSTER_X86_FORM_TEMPLATES + BUSTER_X86_TEMPLATE_PADDING]
    __attribute__((aligned(64)));
static X86TemplateEntry* const x86_templates = x86_template_storage + BUSTER_X86_TEMPLATE_PADDING;
static bool x86_encoder_initialized;
// The same for 32-bit addresses (x86_encode_address32): the templates of the forms with a memory operand have a 67.
static X86TemplateEntry x86_template_storage_address32[BUSTER_X86_TEMPLATE_PADDING + BUSTER_X86_FORM_FIXUP + BUSTER_X86_FORM_TEMPLATES + BUSTER_X86_TEMPLATE_PADDING]
    __attribute__((aligned(64)));
static X86TemplateEntry* const x86_templates_address32 = x86_template_storage_address32 + BUSTER_X86_TEMPLATE_PADDING;
static bool x86_encoder_initialized_address32;
// The same for APX (x86_encode_apx): the templates of the legacy forms of maps 0 and 1 have REX2's positions.
static X86TemplateEntry x86_template_storage_apx[BUSTER_X86_TEMPLATE_PADDING + BUSTER_X86_FORM_FIXUP + BUSTER_X86_FORM_TEMPLATES + BUSTER_X86_TEMPLATE_PADDING]
    __attribute__((aligned(64)));
static X86TemplateEntry* const x86_templates_apx = x86_template_storage_apx + BUSTER_X86_TEMPLATE_PADDING;
static bool x86_encoder_initialized_apx;
// The same for 32-bit and 16-bit modes (x86_encode_mode32 and x86_encode_mode16): the mode's prefixes, a position for
// the decorations' segment override and one for BUSTER_X86_MODE_ADDRESS_SIZE's 67, and no REX (see build_template).
static X86TemplateEntry x86_template_storage_32[BUSTER_X86_TEMPLATE_PADDING + BUSTER_X86_FORM_FIXUP + BUSTER_X86_FORM_TEMPLATES + BUSTER_X86_TEMPLATE_PADDING]
    __attribute__((aligned(64)));
static X86TemplateEntry* const x86_templates_32 = x86_template_storage_32 + BUSTER_X86_TEMPLATE_PADDING;
static X86TemplateEntry x86_template_storage_16[BUSTER_X86_TEMPLATE_PADDING + BUSTER_X86_FORM_FIXUP + BUSTER_X86_FORM_TEMPLATES + BUSTER_X86_TEMPLATE_PADDING]
    __attribute__((aligned(64)));
static X86TemplateEntry* const x86_templates_16 = x86_template_storage_16 + BUSTER_X86_TEMPLATE_PADDING;

// The bits of a register operand of the class that must be zero: registers past the class's last (15 for general
// registers, VEX's vector registers and control registers, 31 for EVEX's, 7 for opmasks, x87, MMX, debug, segment and
// tile registers, of which register_is_valid finds the ones that don't exist, and 3 for MPX's bound registers), a
// group's low bits (which the processor ignores) and the high byte flag, which only a legacy byte register can have;
// all of them for no operand, and those past 15 for a default flags value.
static uint8_t x86_forbidden_register_bits(uint16_t operand, uint8_t family)
{
    const uint8_t class = operand & BUSTER_X86_OPERAND_CLASS;
    uint8_t result = 0xf0;
    if (class == BUSTER_X86_CLASS_NONE)
    {
        result = (operand & BUSTER_X86_OPERAND_DFV) ? 0xf0 : 0xff;
    }
    else if (class == BUSTER_X86_CLASS_GPR8 && family == BUSTER_X86_FAMILY_LEGACY)
    {
        result = 0xb0;
    }
    else if (class >= BUSTER_X86_CLASS_XMM && class <= BUSTER_X86_CLASS_ZMM && family == BUSTER_X86_FAMILY_EVEX)
    {
        result = 0xe0;
    }
    else if (class == BUSTER_X86_CLASS_OPMASK || class == BUSTER_X86_CLASS_ST || class == BUSTER_X86_CLASS_MMX || class == BUSTER_X86_CLASS_DEBUG ||
             class == BUSTER_X86_CLASS_SEGMENT || class == BUSTER_X86_CLASS_TILE)
    {
        result = 0xf8;
    }
    else if (class == BUSTER_X86_CLASS_BND)
    {
        result = 0xfc;
    }
    result |= (operand & BUSTER_X86_OPERAND_PAIR) ? 1 : 0;
    result |= (operand & BUSTER_X86_OPERAND_BLOCK) ? 3 : 0;
    return result;
}

// Whether the operand is a general register (of any size), which REX2 extends to 16-31.
static bool x86_operand_is_general(uint16_t operand)
{
    const uint8_t class = operand & BUSTER_X86_OPERAND_CLASS;
    return class >= BUSTER_X86_CLASS_GPR8 && class <= BUSTER_X86_CLASS_GPR64;
}

// The same with APX's registers (`extended`, with REX2 or EVEX): a general register can be 16 to 31.
static uint8_t forbidden_register_bits(uint16_t operand, uint8_t family, bool extended)
{
    return (uint8_t)(x86_forbidden_register_bits(operand, family) & ~(extended && x86_operand_is_general(operand) ? 0x10 : 0));
}

// A form's checks (BUSTER_X86_TEMPLATE_CHECKS_OFFSET): reg's, rm's (as a register, or a memory operand's base: one of
// the general registers), vvvv's and the decorations' bits that must be zero. With `extended` (APX's REX2 or EVEX), a
// general register and a base can be 16 to 31; then a legacy form's vector register rm can't, but its checks no
// longer say so (x86_validate_apx checks it).
static uint32_t template_checks(const X86FormDescription* form, bool extended)
{
    const uint8_t reg = forbidden_register_bits(form->reg, form->family, extended);
    const uint8_t rm_register = (form->rm & BUSTER_X86_OPERAND_REGISTER) ? forbidden_register_bits(form->rm, form->family, extended) : 0xff;
    const uint8_t rm = (form->rm & BUSTER_X86_OPERAND_MEMORY) ? (uint8_t)(rm_register & (extended ? 0xe0 : 0xf0)) : rm_register;
    uint8_t decorations = 0;
    decorations |= (form->evex & BUSTER_X86_EVEX_MASK) ? BUSTER_X86_DECORATION_MASK : 0;
    decorations |= (form->evex & BUSTER_X86_EVEX_ZEROING) ? BUSTER_X86_DECORATION_ZEROING : 0;
    decorations |= (form->evex & BUSTER_X86_EVEX_ROUNDING) ? BUSTER_X86_DECORATION_ROUNDING : 0;
    return (uint32_t)reg | ((uint32_t)rm << 8) | ((uint32_t)forbidden_register_bits(form->vvvv, form->family, extended) << 16) |
           ((uint32_t)(uint8_t)~decorations << 24);
}

static uint32_t x86_template_checks(const X86FormDescription* form)
{
    return template_checks(form, false);
}

// A form's checks in the mode's template of it (with its segment override, or none): template_checks's, and in 16- and
// 32-bit modes, which have neither REX nor EVEX's R' and V', bits 3 and 4 of every register too, but a control
// register's bit 3 (CR8 is a LOCK prefix there: see build_template), and none of the decorations' segment override, in
// a form that takes one (x86_form_takes_override) and has no FS or GS.
static uint32_t mode_template_checks(const X86FormDescription* form, uint32_t segment, X86EncoderMode encoder, bool extended)
{
    uint32_t result = template_checks(form, extended);
    if (x86_mode_is_legacy(encoder))
    {
        result |= (form->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_CONTROL ? 0x181810 : 0x181818;
        result &= segment == 0 && x86_form_takes_override(form) ? ~((uint32_t)BUSTER_X86_DECORATION_SEGMENT << 24) : ~(uint32_t)0;
    }
    return result;
}

// Whether the form is one of the XSAVE family's (xsave, xrstor, xsaveopt, xsavec, xsaves and xrstors, and their 64-bit
// forms: 0F AE /4 to /6 and 0F C7 /3 to /5 on memory, without prefixes), with which REX2 is #UD.
static bool x86_form_is_xsave(const X86FormDescription* form)
{
    const uint8_t digit = form->encoding & 7;
    return form->family == BUSTER_X86_FAMILY_LEGACY && form->prefixes == 0 && form->rm == BUSTER_X86_OPERAND_M && (form->encoding & BUSTER_X86_ENCODING_DIGIT) &&
           ((form->opcode == 0x0fae && digit >= 4 && digit <= 6) || (form->opcode == 0x0fc7 && digit >= 3 && digit <= 5));
}

// Whether a REX2 prefix can encode the form's registers 16 to 31 (in x86_encode_apx's templates of it, given room for
// the payload): a legacy form of opcode maps 0 and 1, but the XSAVE family's. Maps 2 and 3 have none.
static bool x86_form_takes_rex2(const X86FormDescription* form)
{
    return form->family == BUSTER_X86_FAMILY_LEGACY && form->opcode_bytes != 0 && x86_legacy_map(form) <= 1 && !x86_form_is_xsave(form);
}

// Appends a position to a template that build_template is building: its byte (zero where the canonical slot gives it
// all) and its layout, if the slot has room for it. A form whose positions don't fit in a slot is rejected once all of
// them are counted, so those past the slot's are counted, not written.
BUSTER_INLINE void template_append(X86Template* template, uint32_t* position, uint8_t byte, uint8_t layout)
{
    if (*position < sizeof(template->layout))
    {
        template->bytes[*position] = byte;
        template->layout[*position] = layout;
    }
    *position += 1;
}

// Builds the template of a form with a segment override (x86_form_segment's) or none, for the mode's entry point: for
// 32-bit addresses, with a 67 for a memory operand, and for APX, with REX2's positions in a legacy form of maps 0 and 1
// (and otherwise x86_encode's template, whose general registers and base can't be 16 to 31). In 16- and 32-bit modes,
// in encode_one's order: FWAIT's 9B, a segment override (the form's FS or GS, or a position for the decorations' in a
// form that takes one), NOTRACK's 3E, a 67 (a moffs form's of the other size, or a position for
// BUSTER_X86_MODE_ADDRESS_SIZE's in a form with a memory operand or an address it makes itself), the mode's operand
// size's 66 and the form's other prefixes; then no REX, but in a move to or from a control register, its position for
// CR8's LOCK prefix (AMD's AltMovCr8). Returns false if its longest encoding wouldn't fit in a slot (15 bytes, or in
// 16- and 32-bit modes, 16 with a position for BUSTER_X86_MODE_ADDRESS_SIZE's 67, which compute_fields drops from an
// instruction that would have them all, as it's invalid), its own bytes (a segment override's, a 67, the prefixes, REX
// or the VEX or EVEX prefix, REX2's payload with W, the opcode and a ModRM digit) don't fit before the checks, or the
// form table's row is one the encoders can't encode.
static bool build_template(const X86FormDescription* form, uint32_t segment, X86EncoderMode encoder, X86Template* template)
{
    const bool address32 = encoder == X86_ENCODER_MODE_ADDRESS32;
    const bool legacy = x86_mode_is_legacy(encoder);
    const bool mode16 = encoder == X86_ENCODER_MODE_16;
    const uint8_t properties = legacy ? x86_form_legacy[form - x86_form_descriptions] : 0;
    // The decorations' segment override's position, whose byte is REX's (BUSTER_X86_LAYOUT_OVERRIDE), which only a move
    // to or from a control register uses there.
    const bool override_position = legacy && segment == 0 && x86_form_takes_override(form);
    const bool lock_control = legacy && form->family == BUSTER_X86_FAMILY_LEGACY && (form->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_CONTROL;
    // Whether the template has a position for BUSTER_X86_MODE_ADDRESS_SIZE's 67.
    bool address_size_position = false;
    const bool rex2 = encoder == X86_ENCODER_MODE_APX && x86_form_takes_rex2(form);
    // Map 1's 0F, which REX2's payload replaces.
    const bool rex2_map1 = rex2 && x86_legacy_map(form) == 1;
    // For APX, an EVEX form's general registers, base and general index can be 16 to 31 too: EVEX's R', V', B4 and X4.
    const bool evex_extended = encoder == X86_ENCODER_MODE_APX && form->family == BUSTER_X86_FAMILY_EVEX;
    const bool extended = rex2 || evex_extended;
    // A form that always has REX2 (its row's last prefix is REX2's D5) always has REX too (REX_ALWAYS, whatever W is),
    // whose presence is REX2's: with REX2's positions, D5 is a static byte in REX's position, present with REX, and the
    // payload's position is always there; without them, D5 is a static byte before REX's position, which is the
    // payload, whose R3, X3 and B3 are REX's R, X and B (and M0, R4, X4 and B4 zero, as registers 0 to 15 have them).
    const bool always_rex2 = form->family == BUSTER_X86_FAMILY_LEGACY && x86_form_has_prefix(form, 0xd5);
    uint32_t position = 0;
    uint32_t static_length = 0;
    // The position after the last of the form's own bytes in the template.
    uint32_t last_static = 0;
    uint32_t flags = BUSTER_X86_TEMPLATE_PREFIX_PRESENT | 1u << BUSTER_X86_TEMPLATE_DISP8_SCALE;
    bool valid = true;
    const bool memory = (form->rm & BUSTER_X86_OPERAND_MEMORY) != 0;
    const bool vsib = (form->rm & BUSTER_X86_OPERAND_VSIB) != 0;
    memset(template->bytes, 0, sizeof(template->bytes));
    memset(template->layout, BUSTER_X86_LAYOUT_ABSENT, sizeof(template->layout));
    // FWAIT's 9B first (a waiting x87 control instruction is FWAIT and the no-wait form), then a segment override. In
    // 16- and 32-bit modes, a 16-bit form's 66 is the mode's (below).
    uint32_t prefixes = form->opcode_bytes != 0 && form->family == BUSTER_X86_FAMILY_LEGACY ? form->prefixes : 0;
    prefixes = (properties & BUSTER_X86_LEGACY_SIZE_16) ? x86_prefixes_without(prefixes, 0x66) : prefixes;
    if ((prefixes & 0xff) == 0x9b)
    {
        template_append(template, &position, 0x9b, BUSTER_X86_LAYOUT_STATIC);
        static_length += 1;
        last_static = position;
        prefixes >>= 8;
    }
    if (segment != 0 && memory)
    {
        template_append(template, &position, (uint8_t)(0x63 + segment), BUSTER_X86_LAYOUT_STATIC);
        static_length += 1;
        last_static = position;
    }
    else if (override_position)
    {
        template_append(template, &position, 0, BUSTER_X86_LAYOUT_OVERRIDE);
    }
    // NOTRACK's 3E, which GNU as puts with the segment overrides (but in 16- and 32-bit modes, a branch hint's comes
    // after the mode's 66), and then a 32-bit address's 67, present with a memory operand, or the other address size's.
    while ((prefixes & 0xff) == 0x3e && !(legacy && form->syntax == X86_SYNTAX_REL))
    {
        template_append(template, &position, 0x3e, BUSTER_X86_LAYOUT_STATIC);
        static_length += 1;
        last_static = position;
        prefixes >>= 8;
    }
    const bool other_moffs = (properties & (mode16 ? BUSTER_X86_LEGACY_ADDRESS_32 : BUSTER_X86_LEGACY_ADDRESS_16)) != 0;
    if (other_moffs || ((address32 || legacy) && (memory || (properties & BUSTER_X86_LEGACY_IMPLICIT_ADDRESS))))
    {
        template_append(template, &position, 0x67, other_moffs ? BUSTER_X86_LAYOUT_STATIC : BUSTER_X86_LAYOUT_ADDRESS32);
        static_length += other_moffs ? 1 : 0;
        last_static = position;
        address_size_position = !other_moffs;
    }
    // The mode's operand size's 66: a 16-bit form's in 32-bit mode, and a 32-bit form's in 16-bit mode.
    if ((properties & BUSTER_X86_LEGACY_SIZE_16) ? !mode16 : (properties & BUSTER_X86_LEGACY_SIZE_32) && mode16)
    {
        template_append(template, &position, 0x66, BUSTER_X86_LAYOUT_STATIC);
        static_length += 1;
        last_static = position;
    }
    if (form->opcode_bytes != 0 && form->family == BUSTER_X86_FAMILY_LEGACY)
    {
        for (; prefixes != 0 && !(rex2 && (uint8_t)prefixes == 0xd5); prefixes >>= 8)
        {
            template_append(template, &position, (uint8_t)prefixes, BUSTER_X86_LAYOUT_STATIC);
            static_length += 1;
            last_static = position;
        }

        // REX, or REX2's D5 (in a form that always has REX2, the static D5, or the payload after one). In 16- and
        // 32-bit modes no REX, but a move to or from a control register's LOCK prefix for CR8 in its position, present
        // with REX.R: F4, which REX.R's 4 makes F0.
        if (!legacy || lock_control)
        {
            const uint8_t byte = lock_control ? 0xf4 : always_rex2 && rex2 ? 0xd5 : (uint8_t)((always_rex2 ? 0 : 0x40) | (form->w << 3));
            template_append(template, &position, byte, always_rex2 && rex2 ? BUSTER_X86_LAYOUT_STATIC | BUSTER_X86_CANONICAL_REX : BUSTER_X86_CANONICAL_REX);
        }
        // REX2's payload, after D5: in map 0 a position of its own, present with REX2 alone (but always, in a form that
        // always has REX2), whose template byte is W. Like REX's, it comes before the opcode, the last of the form's
        // own bytes.
        if (rex2 && !rex2_map1)
        {
            template_append(template, &position, (uint8_t)(form->w << 3), BUSTER_X86_CANONICAL_REX2_PAYLOAD);
            static_length += always_rex2 ? 1 : 0;
        }

        // 3DNow!'s operation byte is the immediate's. With REX2, map 1's 0F is the payload's position.
        const uint32_t opcode_bytes = form->opcode_bytes - (x86_form_has_operation_byte(form) ? 1u : 0u);
        for (uint32_t i = 0; i < opcode_bytes; i += 1)
        {
            const bool last = i + 1 == opcode_bytes;
            const uint8_t layout = i == 0 && rex2_map1 ? BUSTER_X86_CANONICAL_REX2_MAP1
                                   : last && form->encoding == BUSTER_X86_ENCODING_PR ? BUSTER_X86_CANONICAL_OPCODE_REGISTER
                                                                                       : BUSTER_X86_LAYOUT_STATIC;
            template_append(template, &position, x86_opcode_byte(form, i), layout);
            static_length += 1;
            last_static = position;
        }
        flags |= form->w || always_rex2 ? BUSTER_X86_TEMPLATE_REX_ALWAYS : 0;
    }
    else if (form->opcode_bytes != 0)
    {
        const uint8_t map = x86_opcode_map(form);
        const uint8_t pp = x86_prefix_pp(form);
        if (form->family == BUSTER_X86_FAMILY_VEX)
        {
            // C4 (C5 for VEX2), or XOP's 8F (for maps 8 and up, never in two bytes); R, X and B (inverted) and the
            // map; W, vvvv (inverted), L and pp.
            template_append(template, &position, map >= 8 ? 0x8f : 0xc4, map >= 8 ? BUSTER_X86_LAYOUT_STATIC : BUSTER_X86_CANONICAL_VEX_ESCAPE);
            template_append(template, &position, (uint8_t)(0xe0 | map), BUSTER_X86_CANONICAL_PREFIX_P0);
            template_append(template, &position, (uint8_t)((form->w << 7) | 0x78 | (form->vector_length << 2) | pp), BUSTER_X86_CANONICAL_PREFIX_P1);
            static_length += 3;
            flags |= map == 1 && !form->w ? BUSTER_X86_TEMPLATE_VEX2 : 0;
            valid = form->vector_length <= 1 && form->disp8_scale == 1 && form->evex == BUSTER_X86_EVEX_NONE && form->vvvv != BUSTER_X86_OPERAND_DFV;
        }
        else
        {
            // 62; R, X, B and R' (inverted) and the map; W, vvvv (inverted, but a default flags value), 1 and pp; L'L
            // (the rounding mode's place, 00 with suppressed exceptions as GNU as encodes it), b (or APX's ND), V'
            // (inverted) and APX's NF, or a source condition in V''s, NF's and the opmask's places (where the canonical
            // byte has zeros, as a default flags value has no bit 4).
            const bool rounding = (form->evex & (BUSTER_X86_EVEX_ROUNDING | BUSTER_X86_EVEX_SAE)) != 0;
            const bool b = (form->evex & BUSTER_X86_EVEX_B) != 0;
            const bool nf = (form->evex & BUSTER_X86_EVEX_NF) != 0;
            const bool scc = (form->evex & BUSTER_X86_EVEX_SOURCE_CONDITION) != 0;
            const bool dfv = form->vvvv == BUSTER_X86_OPERAND_DFV;
            template_append(template, &position, 0x62, BUSTER_X86_LAYOUT_STATIC);
            template_append(template, &position, (uint8_t)(0xf0 | map), BUSTER_X86_CANONICAL_PREFIX_P0);
            template_append(template, &position, (uint8_t)((form->w << 7) | (dfv ? 0 : 0x78) | 0x04 | pp), BUSTER_X86_CANONICAL_PREFIX_P1);
            template_append(template, &position, (uint8_t)(((rounding ? 0 : form->vector_length) << 5) | (b << 4) | (scc ? form->evex >> 8 : 0x08 | (nf << 2))),
                            BUSTER_X86_CANONICAL_PREFIX_P2);
            static_length += 4;
            // NF and ND are in the opmask's and EVEX.b's places, so a form with either has no other decoration; and a
            // source condition is in NF's and the opmask's, so a form with one has no other, and has a default flags
            // value, as only it does.
            const uint16_t apx = BUSTER_X86_EVEX_NF | BUSTER_X86_EVEX_ND;
            valid = form->vector_length <= 2 && form->disp8_scale != 0 && form->disp8_scale <= 64 && (form->disp8_scale & (form->disp8_scale - 1)) == 0 &&
                    !((form->evex & apx) && (form->evex & ~apx)) && (scc ? (form->evex & 0xf07f) == 0 : form->evex < BUSTER_X86_EVEX_SOURCE_CONDITION) && scc == dfv;
            flags = valid ? (flags & ~BUSTER_X86_TEMPLATE_DISP8_SCALE_MASK) | (uint32_t)(__builtin_ctz(form->disp8_scale) + 1) << BUSTER_X86_TEMPLATE_DISP8_SCALE : flags;
        }

        // The opcode's last byte: the map stands for its escape bytes.
        template_append(template, &position, x86_opcode_byte(form, form->opcode_bytes - 1u), BUSTER_X86_LAYOUT_STATIC);
        static_length += 1;
        last_static = position;
        flags |= legacy ? 0 : BUSTER_X86_TEMPLATE_NO_REX;
        // An 8-bit immediate, map 4's 16- and 32-bit ones (APX's legacy instructions), or the 32-bit ones of the MSR
        // instructions (map 7) and of TBM and LWP (XOP's maps).
        valid = valid && map != 0 && (form->prefixes == 0 || pp != 0) && form->encoding != BUSTER_X86_ENCODING_PR && form->relative_bytes == 0 &&
                (form->immediate_bytes <= 1 || (map == 4 && (form->immediate_bytes == 2 || form->immediate_bytes == 4)) ||
                 (map >= 7 && form->immediate_bytes == 4));
    }

    if (form->opcode_bytes != 0)
    {
        if (form->encoding == BUSTER_X86_ENCODING_R || (form->encoding & BUSTER_X86_ENCODING_DIGIT))
        {
            template_append(template, &position, (form->encoding & BUSTER_X86_ENCODING_DIGIT) ? (uint8_t)((form->encoding & 7) << 3) : 0, BUSTER_X86_CANONICAL_MODRM);
            static_length += 1;
            // A /r ModRM's template byte is zero, so it may be past the form's own bytes.
            last_static = (form->encoding & BUSTER_X86_ENCODING_DIGIT) ? position : last_static;
            if (form->rm & BUSTER_X86_OPERAND_MEMORY)
            {
                template_append(template, &position, 0, BUSTER_X86_CANONICAL_SIB);
                for (uint32_t i = 0; i < 4; i += 1)
                {
                    template_append(template, &position, 0, (uint8_t)(BUSTER_X86_CANONICAL_DISPLACEMENT + i));
                }
            }
        }

        for (uint32_t i = 0; i < form->relative_bytes; i += 1)
        {
            template_append(template, &position, 0, (uint8_t)(BUSTER_X86_CANONICAL_DISPLACEMENT + i));
            static_length += 1;
        }

        // An immediate's bytes past the fourth are the displacement field's.
        for (uint32_t i = 0; i < form->immediate_bytes; i += 1)
        {
            template_append(template, &position, 0, (uint8_t)(BUSTER_X86_CANONICAL_IMMEDIATE + i));
            static_length += 1;
        }
    }

    flags |= static_length;
    flags |= (form->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8 ? BUSTER_X86_TEMPLATE_BYTE_REG : 0;
    flags |= (form->rm & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8 ? BUSTER_X86_TEMPLATE_BYTE_RM : 0;
    flags |= form->relative_bytes != 0 || form->immediate_bytes > 4 ? BUSTER_X86_TEMPLATE_STATIC_DISPLACEMENT : 0;
    flags |= memory ? 0 : BUSTER_X86_TEMPLATE_NO_MEMORY;
    // REX2: map 0's payload (canonical byte 15) is present with REX2 alone, and counts in the length (but in a form
    // that always has REX2, it's always present, in the static length). In an EVEX form, a general register rm's bit 4
    // is B4, and a general index's X4.
    flags |= rex2 ? BUSTER_X86_TEMPLATE_REX2 : 0;
    flags &= rex2 && !rex2_map1 && !always_rex2 ? ~(uint32_t)0x8000 : ~(uint32_t)0;
    flags |= evex_extended && x86_operand_is_general(form->rm) ? BUSTER_X86_TEMPLATE_GENERAL_RM : 0;
    flags |= evex_extended && !vsib ? BUSTER_X86_TEMPLATE_GENERAL_INDEX_4 : 0;
    const uint32_t checks = mode_template_checks(form, segment, encoder, extended);
    memcpy(&template->bytes[BUSTER_X86_TEMPLATE_CHECKS_OFFSET], &checks, sizeof(checks));
    memcpy(&template->bytes[BUSTER_X86_TEMPLATE_FLAGS_OFFSET], &flags, sizeof(flags));
    // A static displacement shares the displacement field with a memory operand's, and the form's own bytes have to
    // come before the checks. A VSIB operand is memory, in a VEX or EVEX form.
    const bool static_displacement_conflict = (flags & BUSTER_X86_TEMPLATE_STATIC_DISPLACEMENT) && memory;
    const bool vsib_valid = !vsib || (memory && form->family != BUSTER_X86_FAMILY_LEGACY);
    return valid && position <= (legacy && address_size_position ? sizeof(template->layout) : BUSTER_X86_MAX_INSTRUCTION_BYTES) &&
           static_length <= BUSTER_X86_TEMPLATE_STATIC_LENGTH && !static_displacement_conflict && last_static <= BUSTER_X86_TEMPLATE_STATIC_POSITIONS &&
           vsib_valid && !(override_position && lock_control);
}

// Builds the mode's template of a form (with its segment override, or none) into `template`. A form that doesn't exist
// in the mode (x86_form_exists_in_mode: in 64-bit mode's entry points, one of 16- and 32-bit modes alone, and in those
// modes, one of 64-bit mode alone) takes no operands, and so no instruction, in x86_encode's template of it. A form
// whose longest encoding has no room for the mode's bytes gets another: in APX's, for REX2's payload, x86_encode's
// template, whose
// base can't be 16 to 31 in x86_encode_apx's loop either (see x86_form_has_rex2); and for a memory operand's bytes (a
// segment override, or a 32-bit address's 67, after an imm32 and a 32-bit displacement: HLE's locked arithmetic, TBM's
// and LWP's forms with a segment override and APX's map 4 forms), x86_encode's template of the form without them, which
// takes no memory operand, and with a segment override, no operand at all (see x86_form_lacks_room). Returns false if
// not even that fits (a broken form table).
static bool build_mode_template(uint32_t form, X86EncoderMode encoder, X86Template* template)
{
    const X86FormDescription* description = x86_form_description(form);
    const uint32_t segment = x86_form_segment(form);
    const bool exists = x86_form_exists_in_mode((uint32_t)(description - x86_form_descriptions), encoder);
    bool result = build_template(description, segment, exists ? encoder : X86_ENCODER_MODE_64, template);
    uint32_t extra = 0;
    if (!result && encoder == X86_ENCODER_MODE_APX)
    {
        result = build_template(description, segment, X86_ENCODER_MODE_64, template);
    }
    if (!result)
    {
        result = build_template(description, 0, X86_ENCODER_MODE_64, template);
        extra = BUSTER_X86_TEMPLATE_NO_MEMORY;
    }
    uint32_t flags;
    memcpy(&flags, &template->bytes[BUSTER_X86_TEMPLATE_FLAGS_OFFSET], sizeof(flags));
    flags |= extra;
    memcpy(&template->bytes[BUSTER_X86_TEMPLATE_FLAGS_OFFSET], &flags, sizeof(flags));
    return result;
}

// Builds every form's template for the mode in `templates`, with its halves again the other way around, and the marked
// forms' copies of them.
static bool build_mode_templates(X86EncoderMode encoder, X86TemplateEntry* templates)
{
    bool result = true;
    const uint32_t built_forms = encoder == X86_ENCODER_MODE_64 ? BUSTER_X86_FORM_GS : BUSTER_X86_FORM_TEMPLATES;
    for (uint32_t form = 0; form < built_forms; form += 1)
    {
        result = build_mode_template(form, encoder, &templates[form].template) && result;
        memcpy(templates[form].layout, templates[form].template.layout, sizeof(templates[form].layout));
        memcpy(templates[form].bytes, templates[form].template.bytes, sizeof(templates[form].bytes));
    }
    if (encoder == X86_ENCODER_MODE_64)
    {
        // FS and GS have identical layouts, checks and fallback decisions: only the optional prefix byte differs.
        // Copy the whole span, then patch both byte halves. Keep the direct builder for any future layout change.
        memcpy(templates + BUSTER_X86_FORM_GS, templates + BUSTER_X86_FORM_FS, X86_FORM_COUNT * sizeof(*templates));
        for (uint32_t row = 0; row < X86_FORM_COUNT; row += 1)
        {
            const X86FormDescription* form = &x86_form_descriptions[row];
            X86TemplateEntry* entry = &templates[BUSTER_X86_FORM_GS + row];
            uint32_t flags;
            memcpy(&flags, &entry->template.bytes[BUSTER_X86_TEMPLATE_FLAGS_OFFSET], sizeof(flags));
            if ((form->rm & BUSTER_X86_OPERAND_MEMORY) && !(flags & BUSTER_X86_TEMPLATE_NO_MEMORY))
            {
                const uint32_t prefix = form->opcode_bytes != 0 && form->family == BUSTER_X86_FAMILY_LEGACY && (form->prefixes & 0xff) == 0x9b;
                if (entry->template.bytes[prefix] == 0x64 && entry->bytes[prefix] == 0x64)
                {
                    entry->template.bytes[prefix] = 0x65;
                    entry->bytes[prefix] = 0x65;
                }
                else
                {
                    result = build_mode_template(BUSTER_X86_FORM_GS + row, encoder, &entry->template) && result;
                    memcpy(entry->layout, entry->template.layout, sizeof(entry->layout));
                    memcpy(entry->bytes, entry->template.bytes, sizeof(entry->bytes));
                }
            }
        }
    }
    memcpy(&templates[BUSTER_X86_FORM_FIXUP], &templates[0], BUSTER_X86_FORM_TEMPLATES * sizeof(X86TemplateEntry));
    return result;
}

static bool x86_encoder_initialize(void)
{
    x86_encoder_initialized = build_mode_templates(X86_ENCODER_MODE_64, x86_templates);
    return x86_encoder_initialized;
}

// Kept in every translation unit (used), so that its table is one it writes: in a unit that doesn't call it, clang
// would otherwise make the table read-only zeros, megabytes of the executable file.
__attribute__((used)) static bool x86_encoder_initialize_address32(void)
{
    x86_encoder_initialized_address32 = build_mode_templates(X86_ENCODER_MODE_ADDRESS32, x86_templates_address32);
    return x86_encoder_initialized_address32;
}

static bool x86_encoder_initialize_apx(void)
{
    x86_encoder_initialized_apx = build_mode_templates(X86_ENCODER_MODE_APX, x86_templates_apx);
    return x86_encoder_initialized_apx;
}

// First the forms' properties in 16- and 32-bit modes (x86_form_legacy), which their templates, validation and the
// scalar encoder need, then both modes' templates.
static bool x86_encoder_initialize_legacy(void)
{
    build_legacy_properties();
    x86_encoder_initialized_legacy = build_mode_templates(X86_ENCODER_MODE_32, x86_templates_32) && build_mode_templates(X86_ENCODER_MODE_16, x86_templates_16);
    return x86_encoder_initialized_legacy;
}

// ---------------------------------------------------------------------------------------------------------------------
// Validation

// Whether a register operand of the class can be `value` (in a form of the family, and with `extended`, one whose
// general registers can be 16 to 31): no bits past the class's last register, the high byte flag only for AH to BH, in a
// legacy byte register outside a memory operand, and a control or segment register that exists (CR0, CR2 to CR4 and CR8;
// ES to GS), whose numbers have gaps the bits don't show.
static bool register_is_valid(uint8_t value, uint16_t operand, uint8_t family, bool memory_part, bool extended)
{
    const bool high_byte = (value & BUSTER_X86_REGISTER_HIGH_BYTE) != 0;
    const uint8_t number = value & BUSTER_X86_REGISTER_NUMBER;
    const uint8_t class = operand & BUSTER_X86_OPERAND_CLASS;
    bool result = (value & forbidden_register_bits(operand, family, extended)) == 0;
    if (high_byte)
    {
        result = result && !memory_part && number >= 4 && number <= 7;
    }
    if (class == BUSTER_X86_CLASS_CONTROL)
    {
        result = result && (value == 0 || (value >= 2 && value <= 4) || value == 8);
    }
    else if (class == BUSTER_X86_CLASS_SEGMENT)
    {
        result = result && value <= 5;
    }
    return result;
}

// Whether the form's destination can't be one of its sources: AVX512-FP16's complex multiplications (vfmulcph,
// vfcmulcph, vfmaddcph and vfcmaddcph, and their scalar forms: F3 or F2 at 56, 57, D6 and D7 in map 6), which are #UD
// then.
static bool x86_form_has_distinct_destination(const X86FormDescription* form)
{
    const uint8_t opcode = (uint8_t)form->opcode;
    return x86_opcode_map(form) == 6 && (form->prefixes == 0xf3 || form->prefixes == 0xf2) &&
           (opcode == 0x56 || opcode == 0x57 || opcode == 0xd6 || opcode == 0xd7);
}

// Whether the form's three tiles must be different registers: AMX's products of tiles (dot products and
// multiplications), which are #UD otherwise.
static bool x86_form_has_distinct_tiles(const X86FormDescription* form)
{
    return form->reg == BUSTER_X86_OPERAND_TMM && form->vvvv == BUSTER_X86_OPERAND_TMM && form->rm == BUSTER_X86_OPERAND_TMM;
}

// Whether the form is APX's push2 or pop2 (or push2p or pop2p: map 4's FF /6 and 8F /0), whose two registers can't be
// RSP, and pop2's must be different registers: #UD otherwise.
static bool x86_form_pushes_or_pops_two(const X86FormDescription* form)
{
    return form->family == BUSTER_X86_FAMILY_EVEX && ((form->opcode == 0x04ff && form->encoding == BUSTER_X86_ENCODING_D6) || form->opcode == 0x048f);
}

// Whether the form's immediate has a 4-bit selector in bits 3 to 0 below an /is4 register (vpermil2ps's and
// vpermil2pd's).
static bool x86_form_has_is4_selector(const X86FormDescription* form)
{
    return form->syntax == X86_SYNTAX_REG_VVVV_RM_IS4_IMM || form->syntax == X86_SYNTAX_REG_VVVV_IS4_RM_IMM;
}

// Whether the form's immediate is an /is4 register (a fourth operand, in bits 7 to 4).
static bool x86_form_has_is4(const X86FormDescription* form)
{
    return form->syntax == X86_SYNTAX_REG_VVVV_RM_IS4 || form->syntax == X86_SYNTAX_REG_VVVV_IS4_RM || x86_form_has_is4_selector(form);
}

// The mode's templates.
static const X86TemplateEntry* x86_mode_templates(X86EncoderMode encoder)
{
    return encoder == X86_ENCODER_MODE_ADDRESS32 ? x86_templates_address32
           : encoder == X86_ENCODER_MODE_APX     ? x86_templates_apx
           : encoder == X86_ENCODER_MODE_32      ? x86_templates_32
           : encoder == X86_ENCODER_MODE_16      ? x86_templates_16
                                                 : x86_templates;
}

// Whether the form (with its segment override) can have a memory operand, but not in the mode's entry point, whose
// template of it has no room for the operand's bytes: those whose longest instruction (with a 32-bit displacement and
// an imm32) would be 16 bytes, past x86's 15, with a segment override (APX's map 4 forms, and for 32-bit addresses,
// HLE's arithmetic, TBM's bextr and LWP's too) or a 32-bit address's 67 (APX's map 4 forms, with registers alone then),
// or whose own bytes would run past the template's 8 (with a segment override and a 67, HLE's bit tests of a word by an
// immediate). A segment override's form without a memory operand in its row takes no operand at all either, but isn't
// one of these, nor is a form that doesn't exist in the mode (x86_form_exists_in_mode), which takes none. Requires the
// mode's initializer.
static bool x86_form_lacks_room(uint32_t form, X86EncoderMode encoder)
{
    uint32_t flags;
    memcpy(&flags, &x86_mode_templates(encoder)[form & BUSTER_X86_FORM_NUMBER].template.bytes[BUSTER_X86_TEMPLATE_FLAGS_OFFSET], sizeof(flags));
    const uint32_t row = (uint32_t)(x86_form_description(form) - x86_form_descriptions);
    return (x86_form_description(form)->rm & BUSTER_X86_OPERAND_MEMORY) && (flags & BUSTER_X86_TEMPLATE_NO_MEMORY) && x86_form_exists_in_mode(row, encoder);
}

// Whether x86_encode_apx's template of the form (with its segment override) has REX2's positions, so that its general
// registers and its base and index can be 16 to 31: a legacy form of maps 0 and 1 but the XSAVE family's, with room for
// the payload (not, with a segment override, HLE's locked arithmetic on a doubleword or a quadword by an imm32, which
// would be 16 bytes long). Requires x86_encoder_initialize_apx.
static bool x86_form_has_rex2(uint32_t form)
{
    uint32_t flags;
    memcpy(&flags, &x86_templates_apx[form & BUSTER_X86_FORM_NUMBER].template.bytes[BUSTER_X86_TEMPLATE_FLAGS_OFFSET], sizeof(flags));
    return (flags & BUSTER_X86_TEMPLATE_REX2) != 0;
}

// Whether x86_encode_apx lets the form's general registers, base and general index be 16 to 31: a form with REX2, or
// an EVEX one (with EVEX's R', V', B4 and X4). Requires x86_encoder_initialize_apx.
static bool x86_form_has_apx_registers(uint32_t form)
{
    return x86_form_has_rex2(form) || x86_form_description(form)->family == BUSTER_X86_FAMILY_EVEX;
}

// The scalar encoder's one instruction (below), which validation in 16- and 32-bit modes measures.
static uint32_t encode_one(const X86Instruction* instruction, uint8_t* out, X86EncoderMode encoder);

// Whether a register operand of the class can be `value` in the mode (register_is_valid's, and in 16- and 32-bit modes,
// `legacy`, registers 0 to 7 alone but a control register's, whose CR8 AMD's LOCK prefix gives (see encode_one), and in
// a legacy byte register, 4 to 7 only with the high byte flag: AH to BH, as SPL to DIL need REX).
static bool mode_register_is_valid(uint8_t value, uint16_t operand, uint8_t family, bool memory_part, bool extended, bool legacy)
{
    const uint8_t number = value & BUSTER_X86_REGISTER_NUMBER;
    const bool byte = (operand & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8 && family == BUSTER_X86_FAMILY_LEGACY && !memory_part;
    const bool control = (operand & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_CONTROL;
    const bool without_rex = (control || (number & 0x18) == 0) && !(byte && number >= 4 && (value & BUSTER_X86_REGISTER_HIGH_BYTE) == 0);
    return register_is_valid(value, operand, family, memory_part, extended) && (!legacy || without_rex);
}

// Whether a 16-bit address's base and index can be the instruction's: BX or BP and SI or DI, or one of the four alone
// (RBX's, RBP's, RSI's and RDI's numbers), without a scale, or neither.
static bool address16_is_valid(const X86Instruction* instruction)
{
    const uint8_t mode = instruction->mode;
    const bool base = (mode & BUSTER_X86_MODE_BASE) != 0;
    const bool index = (mode & BUSTER_X86_MODE_INDEX) != 0;
    const uint8_t rm = instruction->rm;
    const bool pair = base && index && (rm == X86_REGISTER_RBX || rm == X86_REGISTER_RBP) && (instruction->index == X86_REGISTER_RSI || instruction->index == X86_REGISTER_RDI);
    const bool alone = base && !index && (rm == X86_REGISTER_RBX || rm == X86_REGISTER_RBP || rm == X86_REGISTER_RSI || rm == X86_REGISTER_RDI) &&
                       instruction->index == 0;
    const bool neither = !base && !index && rm == 0 && instruction->index == 0;
    return (mode & BUSTER_X86_MODE_SCALE) == 0 && (pair || alone || neither);
}

// Whether the instruction can be encoded by the mode's entry point (with a 32-bit address, with APX's registers 16 to
// 31 where REX2 or EVEX can encode them, or in 16- or 32-bit mode, see x86_encoder_initialize_legacy): a valid form
// that exists in the mode, operands the form has and no others, a consistent addressing mode, decorations the form can
// have, and immediates and relative displacements that fit their fields. In 16- and 32-bit modes too, the instruction's
// bytes fit x86's 15 (which a template's room says in the others: see x86_form_lacks_room).
static bool instruction_is_valid(const X86Instruction* instruction, X86EncoderMode encoder)
{
    const bool address32 = encoder == X86_ENCODER_MODE_ADDRESS32;
    const bool legacy = x86_mode_is_legacy(encoder);
    bool result = (instruction->form & BUSTER_X86_FORM_NUMBER) < BUSTER_X86_FORM_TEMPLATES;
    if (result)
    {
        const X86FormDescription* form = x86_form_description(instruction->form);
        const uint32_t row = (uint32_t)(form - x86_form_descriptions);
        const uint8_t properties = legacy ? x86_form_legacy[row] : 0;
        // Whether the general registers, base and index can be 16 to 31.
        const bool extended = encoder == X86_ENCODER_MODE_APX && x86_form_has_apx_registers(instruction->form);
        const uint8_t mode = instruction->mode;
        const bool memory = (mode & BUSTER_X86_MODE_MEMORY) != 0;
        const bool base = (mode & BUSTER_X86_MODE_BASE) != 0;
        const bool index = (mode & BUSTER_X86_MODE_INDEX) != 0;
        const bool rip = (mode & BUSTER_X86_MODE_RIP) != 0;
        const bool has_modrm = form->encoding == BUSTER_X86_ENCODING_R || (form->encoding & BUSTER_X86_ENCODING_DIGIT);
        const bool vsib = (form->rm & BUSTER_X86_OPERAND_VSIB) != 0;
        const bool sib = (form->rm & BUSTER_X86_OPERAND_SIB) != 0;
        // In 16- and 32-bit modes, the address size BUSTER_X86_MODE_ADDRESS_SIZE gives the instruction: 16-bit
        // addresses in 16-bit mode without it and in 32-bit mode with it.
        const bool other_size = legacy && (mode & BUSTER_X86_MODE_ADDRESS_SIZE) != 0;
        const bool address16 = legacy && (encoder == X86_ENCODER_MODE_16) != other_size;

        // The form exists in the mode.
        result = result && x86_form_exists_in_mode(row, encoder);

        // ModRM.reg
        if (form->encoding == BUSTER_X86_ENCODING_R)
        {
            result = result && mode_register_is_valid(instruction->reg, form->reg, form->family, false, extended, legacy);
        }
        else
        {
            result = result && instruction->reg == 0;
        }

        // ModRM.rm or the +r register, and the addressing mode: in 16- and 32-bit modes, without a memory operand, the
        // other address size for the addresses its instruction makes itself.
        const uint8_t own_mode = (properties & BUSTER_X86_LEGACY_IMPLICIT_ADDRESS) ? BUSTER_X86_MODE_ADDRESS_SIZE : 0;
        if (form->rm == BUSTER_X86_OPERAND_NONE)
        {
            result = result && instruction->rm == 0 && instruction->index == 0 && (mode & ~own_mode) == 0 && x86_form_segment(instruction->form) == 0;
        }
        else if (!memory)
        {
            result = result && (mode & ~own_mode) == 0 && instruction->index == 0 && (form->rm & BUSTER_X86_OPERAND_REGISTER) && x86_form_segment(instruction->form) == 0;
            result = result && mode_register_is_valid(instruction->rm, form->rm, form->family, false, extended, legacy);
        }
        else
        {
            const uint8_t known = BUSTER_X86_MODE_SCALE | BUSTER_X86_MODE_MEMORY | BUSTER_X86_MODE_BASE | BUSTER_X86_MODE_INDEX | BUSTER_X86_MODE_RIP |
                                  (legacy ? BUSTER_X86_MODE_ADDRESS_SIZE : 0);
            result = result && has_modrm && (form->rm & BUSTER_X86_OPERAND_MEMORY);
            result = result && (mode & ~known) == 0;
            result = result && !(rip && (base || index)) && !(legacy && rip);
            result = result && (index || (mode & BUSTER_X86_MODE_SCALE) == 0);
            result = result && (base ? mode_register_is_valid(instruction->rm, BUSTER_X86_OPERAND_GQ, BUSTER_X86_FAMILY_LEGACY, true, extended, legacy) : instruction->rm == 0);
            // Some can't be RIP-relative (AMX's SIB operands, and MPX's bndmk and MIB operands), and a MIB operand's index
            // takes no scale.
            result = result && !(rip && (form->rm & BUSTER_X86_OPERAND_NO_RIP)) && !((form->rm & BUSTER_X86_OPERAND_UNSCALED) && (mode & BUSTER_X86_MODE_SCALE));
            // A 32-bit address can't be MPX's, which is #UD with 67 in 64-bit mode, nor can a 16-bit one, and no memory
            // operand can be in a form whose template has no room for its bytes.
            const bool bound = (form->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_BND;
            result = result && !((address32 || address16) && bound) && !x86_form_lacks_room(instruction->form, encoder);
            if (address16)
            {
                // A 16-bit address has no SIB byte, so no VSIB or SIB operand, and a 16-bit displacement.
                result = result && !vsib && !sib && address16_is_valid(instruction);
                result = result && instruction->displacement >= -32768 && instruction->displacement <= 65535;
            }
            else if (vsib)
            {
                // A VSIB operand always has an index, a vector register of its class, which can be XMM4.
                result = result && index && mode_register_is_valid(instruction->index, form->rm, form->family, true, false, legacy);
            }
            else if (sib)
            {
                // A SIB operand always has a SIB byte: an index (a general register, whose 4 is none and takes no
                // scale) or no base.
                const bool scaled_none = instruction->index == X86_REGISTER_RSP && (mode & BUSTER_X86_MODE_SCALE) != 0;
                result = result && (index || !base) && !scaled_none;
                result = result && (index ? mode_register_is_valid(instruction->index, BUSTER_X86_OPERAND_GQ, BUSTER_X86_FAMILY_LEGACY, true, extended, legacy)
                                          : instruction->index == 0);
            }
            else
            {
                // RSP can't be a general index: its SIB index field means "none" (but R20's, with REX2's X4, doesn't).
                result = result && (index ? mode_register_is_valid(instruction->index, BUSTER_X86_OPERAND_GQ, BUSTER_X86_FAMILY_LEGACY, true, extended, legacy) &&
                                                instruction->index != X86_REGISTER_RSP
                                          : instruction->index == 0);
            }
        }

        // In 16- and 32-bit modes, a segment override in the decorations of a form that takes one
        // (x86_form_takes_override): ES to GS, on an instruction with an address (a memory operand in a form without FS
        // or GS, a moffs, or an address the instruction makes itself).
        const bool overridable = legacy && x86_form_takes_override(form);
        const uint8_t override = overridable ? (uint8_t)((instruction->decorations & BUSTER_X86_DECORATION_SEGMENT) >> 3) : 0;
        const bool addressed = memory || !(form->rm & BUSTER_X86_OPERAND_MEMORY);
        result = result && (override == 0 || (override <= 6 && addressed && x86_form_segment(instruction->form) == 0));

        // VEX.vvvv, and EVEX's decorations: the form's, zeroing only with an opmask, and not into memory.
        result = result && (form->vvvv != BUSTER_X86_OPERAND_NONE ? mode_register_is_valid(instruction->vvvv, form->vvvv, form->family, false, extended, legacy)
                                                                  : instruction->vvvv == 0);
        const uint8_t decorations = (uint8_t)(instruction->decorations & (overridable ? ~BUSTER_X86_DECORATION_SEGMENT : 0xff));
        const bool memory_destination = memory && (form->syntax == X86_SYNTAX_RM_REG || form->syntax == X86_SYNTAX_RM_REG_IMM);
        result = result && (decorations & (x86_template_checks(form) >> 24)) == 0;
        result = result && !((decorations & BUSTER_X86_DECORATION_ZEROING) && ((decorations & BUSTER_X86_DECORATION_MASK) == 0 || memory_destination));

        // A complex multiplication's destination can't be one of its sources, an AMX product's three tiles must be
        // different registers, push2's and pop2's registers can't be RSP, and pop2's two must be different.
        result = result && !(x86_form_has_distinct_destination(form) && (instruction->reg == instruction->vvvv || (!memory && instruction->reg == instruction->rm)));
        result = result && !(x86_form_has_distinct_tiles(form) && (instruction->reg == instruction->vvvv || instruction->reg == instruction->rm || instruction->vvvv == instruction->rm));
        const bool two = x86_form_pushes_or_pops_two(form);
        result = result && !(two && (instruction->vvvv == X86_REGISTER_RSP || instruction->rm == X86_REGISTER_RSP));
        result = result && !(two && form->opcode == 0x048f && instruction->vvvv == instruction->rm);

        // A 3DNow! form's immediate is its operation byte.
        result = result && !(x86_form_has_operation_byte(form) && instruction->immediate != (int32_t)(form->opcode & 0xff));

        // An EVEX gather or scatter needs an opmask (K0 is #UD), and a gather's destination, index and mask (AVX2's, in
        // vvvv) must be different registers (a prefetching one has no destination).
        const bool gather = vsib && form->encoding == BUSTER_X86_ENCODING_R && form->syntax != X86_SYNTAX_RM_REG;
        const bool aliased = instruction->reg == instruction->index ||
                             (form->vvvv != BUSTER_X86_OPERAND_NONE && (instruction->vvvv == instruction->reg || instruction->vvvv == instruction->index));
        result = result && !(vsib && form->family == BUSTER_X86_FAMILY_EVEX && (decorations & BUSTER_X86_DECORATION_MASK) == 0);
        result = result && !(gather && aliased);

        // MOV to CS (8E with CS in ModRM.reg) doesn't exist: it's #UD.
        result = result && !(form->opcode_bytes == 1 && form->opcode == 0x8e && instruction->reg == 1);

        // A NOTRACK form can't have a segment override, which would come before its 3E: 3E would be the last segment
        // prefix, the one that counts.
        result = result && !(x86_form_segment(instruction->form) != 0 && form->family == BUSTER_X86_FAMILY_LEGACY && x86_form_has_prefix(form, 0x3e));

        // A high byte register can't be in an instruction with a REX prefix, or REX2's.
        const uint8_t rm_register = memory ? 0 : instruction->rm;
        const bool high_byte = ((instruction->reg | rm_register) & BUSTER_X86_REGISTER_HIGH_BYTE) != 0;
        const bool byte_reg = (form->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8;
        const bool byte_rm = (form->rm & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8;
        const bool rex = form->w || ((instruction->reg | instruction->rm | instruction->index) & (extended ? 0x18 : 8)) ||
                         (byte_reg && instruction->reg >= 4 && instruction->reg <= 7) ||
                         (byte_rm && !memory && instruction->rm >= 4 && instruction->rm <= 7);
        result = result && !(high_byte && rex);

        // The immediate and displacement fields.
        const int64_t immediate = instruction->immediate;
        const int64_t displacement = instruction->displacement;
        switch (form->immediate_bytes)
        {
            case 0: result = result && immediate == 0; break;
            case 1: result = result && immediate >= -128 && immediate <= 255; break;
            case 2: result = result && immediate >= -32768 && immediate <= 65535; break;
            case 3: result = result && immediate >= 0 && immediate < (1 << 24); break;
            default: break;
        }
        // An /is4 register: 0 to 15 in bits 7 to 4 (0 to 7 in 16- and 32-bit modes, where the processor ignores bit 7),
        // and bits 3 to 0 clear or a selector.
        if (x86_form_has_is4(form))
        {
            const int64_t is4_bits = (legacy ? 0x70 : 0xf0) | (x86_form_has_is4_selector(form) ? 0x0f : 0);
            result = result && (immediate & ~is4_bits) == 0;
        }
        if (form->relative_bytes == 1)
        {
            result = result && displacement >= -128 && displacement <= 127;
        }
        else if (form->relative_bytes == 2)
        {
            result = result && displacement >= -32768 && displacement <= 32767;
        }
        else if (form->immediate_bytes == 6)
        {
            // A ptr16:32's selector.
            result = result && displacement >= -32768 && displacement <= 65535;
        }
        else if (form->relative_bytes == 0 && form->immediate_bytes <= 4 && !memory)
        {
            result = result && displacement == 0;
        }

        // In 16- and 32-bit modes, the prefixes the mode adds (a 66 or a 67) must leave the instruction within 15
        // bytes.
        if (result && legacy)
        {
            uint8_t bytes[32];
            result = encode_one(instruction, bytes, encoder) <= BUSTER_X86_MAX_INSTRUCTION_BYTES;
        }
    }
    return result;
}

static bool x86_instruction_is_valid(const X86Instruction* instruction)
{
    return instruction_is_valid(instruction, X86_ENCODER_MODE_64);
}

static __attribute__((unused)) bool x86_instruction_is_valid_address32(const X86Instruction* instruction)
{
    return instruction_is_valid(instruction, X86_ENCODER_MODE_ADDRESS32);
}

static __attribute__((unused)) bool x86_instruction_is_valid_apx(const X86Instruction* instruction)
{
    return instruction_is_valid(instruction, X86_ENCODER_MODE_APX);
}

static __attribute__((unused)) bool x86_instruction_is_valid_32(const X86Instruction* instruction)
{
    return instruction_is_valid(instruction, X86_ENCODER_MODE_32);
}

static __attribute__((unused)) bool x86_instruction_is_valid_16(const X86Instruction* instruction)
{
    return instruction_is_valid(instruction, X86_ENCODER_MODE_16);
}

static uint64_t validate_instructions(const X86Instruction* instructions, uint64_t count, X86EncoderMode encoder)
{
    uint64_t i = 0;
    while (i < count && instruction_is_valid(&instructions[i], encoder))
    {
        i += 1;
    }
    return i;
}

static uint64_t x86_validate(const X86Instruction* instructions, uint64_t count)
{
    return validate_instructions(instructions, count, X86_ENCODER_MODE_64);
}

static uint64_t x86_validate_address32(const X86Instruction* instructions, uint64_t count)
{
    return validate_instructions(instructions, count, X86_ENCODER_MODE_ADDRESS32);
}

static uint64_t x86_validate_apx(const X86Instruction* instructions, uint64_t count)
{
    return validate_instructions(instructions, count, X86_ENCODER_MODE_APX);
}

static uint64_t x86_validate_32(const X86Instruction* instructions, uint64_t count)
{
    return validate_instructions(instructions, count, X86_ENCODER_MODE_32);
}

static uint64_t x86_validate_16(const X86Instruction* instructions, uint64_t count)
{
    return validate_instructions(instructions, count, X86_ENCODER_MODE_16);
}

// ---------------------------------------------------------------------------------------------------------------------
// The scalar encoder: one instruction at a time, from the form table rather than the templates, so that it checks
// them.

// Encodes a valid instruction at `out` for the mode's entry point (with a 32-bit address, or with REX2 for APX's
// registers 16 to 31) and returns its length.
static uint32_t encode_one(const X86Instruction* instruction, uint8_t* out, X86EncoderMode encoder)
{
    const bool address32 = encoder == X86_ENCODER_MODE_ADDRESS32;
    const X86FormDescription* form = x86_form_description(instruction->form);
    const uint32_t segment = x86_form_segment(instruction->form);
    const uint8_t mode = instruction->mode;
    const bool memory = (mode & BUSTER_X86_MODE_MEMORY) != 0;
    const bool base = (mode & BUSTER_X86_MODE_BASE) != 0;
    const bool index = (mode & BUSTER_X86_MODE_INDEX) != 0;
    const bool rip = (mode & BUSTER_X86_MODE_RIP) != 0;
    const uint8_t reg = instruction->reg & BUSTER_X86_REGISTER_NUMBER;
    const uint8_t rm = instruction->rm & BUSTER_X86_REGISTER_NUMBER;
    const uint8_t index_register = instruction->index & BUSTER_X86_REGISTER_NUMBER;
    const uint8_t vvvv = instruction->vvvv & BUSTER_X86_REGISTER_NUMBER;
    const bool has_modrm = form->encoding == BUSTER_X86_ENCODING_R || (form->encoding & BUSTER_X86_ENCODING_DIGIT);
    // REX.R, REX.X and REX.B (and VEX's and EVEX's): the high bits of ModRM.reg, the index and ModRM.rm (or the base,
    // or the +r register). EVEX's X is a vector register rm's bit 4 (a general register's is B4, with APX). In 16- and
    // 32-bit modes, without REX, CR8 is AMD's LOCK prefix on a move of CR0 (AltMovCr8), as GNU as writes it.
    const bool legacy = x86_mode_is_legacy(encoder);
    const bool general_rm = x86_operand_is_general(form->rm);
    const bool lock_control = legacy && (form->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_CONTROL && (reg & 8) != 0;
    const bool r = (reg & 8) != 0 && !lock_control;
    const bool x = memory ? index && (index_register & 8) : (rm & 16) && !general_rm;
    const bool b = (!memory || base) && (rm & 8);
    // In 16- and 32-bit modes, the mode writes the operand size's 66 (a 16-bit form's, which isn't among its prefixes
    // then, in 32-bit mode, and a 32-bit form's in 16-bit mode) and BUSTER_X86_MODE_ADDRESS_SIZE's 67.
    const uint8_t properties = legacy ? x86_form_legacy[form - x86_form_descriptions] : 0;
    const bool size_prefix = (properties & BUSTER_X86_LEGACY_SIZE_16) ? encoder != X86_ENCODER_MODE_16
                                                                      : (properties & BUSTER_X86_LEGACY_SIZE_32) != 0 && encoder == X86_ENCODER_MODE_16;
    // The other address size: BUSTER_X86_MODE_ADDRESS_SIZE's, or a moffs form's.
    const bool other_moffs = (properties & (encoder == X86_ENCODER_MODE_16 ? BUSTER_X86_LEGACY_ADDRESS_32 : BUSTER_X86_LEGACY_ADDRESS_16)) != 0;
    const bool other_size = legacy && ((mode & BUSTER_X86_MODE_ADDRESS_SIZE) != 0 || other_moffs);
    const bool address16 = legacy && (encoder == X86_ENCODER_MODE_16) != other_size;
    uint32_t length = 0;

    // FWAIT's 9B comes first, then a segment override, as GNU as puts them.
    uint32_t prefixes = form->opcode_bytes != 0 && form->family == BUSTER_X86_FAMILY_LEGACY ? form->prefixes : 0;
    prefixes = (properties & BUSTER_X86_LEGACY_SIZE_16) ? x86_prefixes_without(prefixes, 0x66) : prefixes;
    if ((prefixes & 0xff) == 0x9b)
    {
        out[length] = 0x9b;
        length += 1;
        prefixes >>= 8;
    }
    // The form's FS or GS override, or in 16- and 32-bit modes the decorations' (ES's 26 to GS's 65) in a form that
    // takes one.
    static const uint8_t overrides[8] = { 0, 0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65, 0 };
    const bool overridable = legacy && x86_form_takes_override(form);
    const uint8_t override = segment != 0 ? (uint8_t)(0x63 + segment) : overridable ? overrides[(instruction->decorations & BUSTER_X86_DECORATION_SEGMENT) >> 3] : 0;
    if (override != 0)
    {
        out[length] = override;
        length += 1;
    }
    // Then NOTRACK's 3E, another segment override for GNU as, a 32-bit address's 67 (or the other address size's, in
    // 16- and 32-bit modes) and the mode's operand size's 66, which GNU as puts before a branch hint's 2E or 3E.
    while ((prefixes & 0xff) == 0x3e && form->syntax != X86_SYNTAX_REL)
    {
        out[length] = 0x3e;
        length += 1;
        prefixes >>= 8;
    }
    if ((address32 && memory) || other_size)
    {
        out[length] = 0x67;
        length += 1;
    }
    if (size_prefix)
    {
        out[length] = 0x66;
        length += 1;
    }
    if (lock_control)
    {
        out[length] = 0xf0;
        length += 1;
    }
    if (form->opcode_bytes != 0 && form->family == BUSTER_X86_FAMILY_LEGACY)
    {
        // A form that always has REX2 has its D5 as its last prefix, which REX2 writes.
        const bool always_rex2 = x86_form_has_prefix(form, 0xd5);
        for (; prefixes != 0 && (uint8_t)prefixes != 0xd5; prefixes >>= 8)
        {
            out[length] = (uint8_t)prefixes;
            length += 1;
        }

        // REX: W, R, X and B (whose X is the index's alone); and for SPL, BPL, SIL and DIL. Or with APX's general
        // registers 16 to 31, or in a form that always has it, REX2: D5, then M0 (map 1, whose 0F it replaces), R4, X4,
        // B4 (the same registers' bit 4), W, R, X and B.
        const bool legacy_x = memory && index && (index_register & 8);
        const uint8_t rex = (uint8_t)(0x40 | (form->w << 3) | (r << 2) | (legacy_x << 1) | b);
        const bool r4 = (reg & 16) != 0;
        const bool x4 = memory && index && (index_register & 16);
        const bool b4 = (!memory || base) && (rm & 16);
        const bool rex2 = always_rex2 || (encoder == X86_ENCODER_MODE_APX && (r4 || x4 || b4));
        const bool map1 = x86_legacy_map(form) == 1;
        bool needs_rex = rex != 0x40;
        if ((form->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8 && instruction->reg >= 4 && instruction->reg <= 7)
        {
            needs_rex = true;
        }
        if ((form->rm & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8 && !memory && instruction->rm >= 4 && instruction->rm <= 7)
        {
            needs_rex = true;
        }
        if (rex2)
        {
            out[length] = 0xd5;
            out[length + 1] = (uint8_t)((map1 << 7) | (r4 << 6) | (x4 << 5) | (b4 << 4) | (rex & 15));
            length += 2;
        }
        else if (needs_rex)
        {
            out[length] = rex;
            length += 1;
        }

        // 3DNow!'s operation byte is the immediate's.
        const uint32_t opcode_bytes = form->opcode_bytes - (x86_form_has_operation_byte(form) ? 1u : 0u);
        for (uint32_t i = rex2 && map1 ? 1 : 0; i < opcode_bytes; i += 1)
        {
            uint8_t byte = x86_opcode_byte(form, i);
            if (i + 1 == opcode_bytes && form->encoding == BUSTER_X86_ENCODING_PR)
            {
                byte |= rm & 7;
            }
            out[length] = byte;
            length += 1;
        }
    }
    else if (form->opcode_bytes != 0)
    {
        const uint8_t map = x86_opcode_map(form);
        const uint8_t pp = x86_prefix_pp(form);
        // vvvv is inverted, but a default flags value.
        const uint8_t vvvv_field = (uint8_t)(((form->vvvv == BUSTER_X86_OPERAND_DFV ? vvvv : ~vvvv) & 15) << 3);
        if (form->family == BUSTER_X86_FAMILY_VEX && map == 1 && !form->w && !x && !b)
        {
            out[length] = 0xc5;
            out[length + 1] = (uint8_t)((!r << 7) | vvvv_field | (form->vector_length << 2) | pp);
            length += 2;
        }
        else if (form->family == BUSTER_X86_FAMILY_VEX)
        {
            out[length] = map >= 8 ? 0x8f : 0xc4;
            out[length + 1] = (uint8_t)((!r << 7) | (!x << 6) | (!b << 5) | map);
            out[length + 2] = (uint8_t)((form->w << 7) | vvvv_field | (form->vector_length << 2) | pp);
            length += 3;
        }
        else
        {
            // L'L is the vector length, or with embedded rounding the rounding mode (00 for suppressed exceptions). V' is
            // vvvv's bit 4, or a VSIB operand's index's. APX's ND is EVEX.b, and NF the opmask's bit 2, and a source
            // condition is in the bits of V', NF and the opmask.
            const uint8_t decorations = instruction->decorations;
            const bool rounding = (form->evex & (BUSTER_X86_EVEX_ROUNDING | BUSTER_X86_EVEX_SAE)) != 0;
            const uint8_t vector_length = rounding ? (uint8_t)((decorations & BUSTER_X86_DECORATION_ROUNDING) >> 5) : form->vector_length;
            const uint8_t v_high = (form->rm & BUSTER_X86_OPERAND_VSIB) ? index_register : vvvv;
            // With APX's registers 16 to 31, B4 (P0's bit 3) is a base's or a general register rm's bit 4, and X4 (P1's
            // bit 2, inverted) a general index's.
            const bool b4 = (memory ? base : general_rm) && (rm & 16);
            const bool x4 = memory && index && !(form->rm & BUSTER_X86_OPERAND_VSIB) && (index_register & 16);
            out[length] = 0x62;
            out[length + 1] = (uint8_t)((!r << 7) | (!x << 6) | (!b << 5) | (!(reg & 16) << 4) | (b4 << 3) | map);
            out[length + 2] = (uint8_t)((form->w << 7) | vvvv_field | (!x4 << 2) | pp);
            const uint8_t low = (form->evex & BUSTER_X86_EVEX_SOURCE_CONDITION)
                                    ? (uint8_t)(form->evex >> 8)
                                    : (uint8_t)((!(v_high & 16) << 3) | (((form->evex & BUSTER_X86_EVEX_NF) != 0) << 2) | (decorations & BUSTER_X86_DECORATION_MASK));
            out[length + 3] = (uint8_t)((decorations & BUSTER_X86_DECORATION_ZEROING) | (vector_length << 5) | (((form->evex & BUSTER_X86_EVEX_B) != 0) << 4) | low);
            length += 4;
        }
        // The opcode's last byte: the map stands for its escape bytes.
        out[length] = x86_opcode_byte(form, form->opcode_bytes - 1u);
        length += 1;
    }

    if (form->opcode_bytes != 0)
    {
        if (has_modrm)
        {
            const uint8_t reg_field = (form->encoding & BUSTER_X86_ENCODING_DIGIT) ? (form->encoding & 7) : (reg & 7);
            uint8_t mod;
            uint8_t rm_field;
            bool sib = false;
            uint32_t displacement_bytes = 0;
            // EVEX's 8-bit displacements count in N (form->disp8_scale) bytes.
            uint32_t displacement = (uint32_t)instruction->displacement;
            if (!memory)
            {
                mod = 3;
                rm_field = rm & 7;
            }
            else if (address16)
            {
                // A 16-bit address: ModRM's own [bx+si], [bx+di], [bp+si], [bp+di], [si], [di], [bp] and [bx], with an
                // 8- or 16-bit displacement (but [bp] always with one, as its mod 00 is an absolute address), and no
                // SIB. The displacement is modulo 2^16, as the address is: 65535 is -1, an 8-bit one.
                const int32_t scale = form->disp8_scale;
                const int32_t displacement16 = (int16_t)instruction->displacement;
                const int32_t scaled = displacement16 / scale;
                const uint8_t pair = (uint8_t)((rm == X86_REGISTER_RBP ? 2 : 0) | (index_register == X86_REGISTER_RDI ? 1 : 0));
                rm_field = !base ? 6 : index ? pair : rm == X86_REGISTER_RSI ? 4 : rm == X86_REGISTER_RDI ? 5 : rm == X86_REGISTER_RBP ? 6 : 7;
                if (!base)
                {
                    mod = 0;
                    displacement_bytes = 2;
                }
                else if (displacement16 == 0 && rm_field != 6)
                {
                    mod = 0;
                }
                else if (displacement16 % scale == 0 && scaled >= -128 && scaled <= 127)
                {
                    mod = 1;
                    displacement_bytes = 1;
                    displacement = (uint32_t)scaled;
                }
                else
                {
                    mod = 2;
                    displacement_bytes = 2;
                }
            }
            else if (rip)
            {
                mod = 0;
                rm_field = 5;
                displacement_bytes = 4;
            }
            else if (!base && !index && legacy)
            {
                // In 16- and 32-bit modes, without RIP, an absolute address is ModRM's own.
                mod = 0;
                rm_field = 5;
                displacement_bytes = 4;
            }
            else if (!base)
            {
                // [index * scale + disp32] and [disp32] both use a SIB byte with no base.
                mod = 0;
                rm_field = 4;
                sib = true;
                displacement_bytes = 4;
            }
            else
            {
                const int32_t scale = form->disp8_scale;
                const int32_t scaled = instruction->displacement / scale;
                rm_field = (index || (rm & 7) == 4) ? 4 : (rm & 7);
                sib = index || (rm & 7) == 4;
                if (instruction->displacement == 0 && (rm & 7) != 5)
                {
                    mod = 0;
                }
                else if (instruction->displacement % scale == 0 && scaled >= -128 && scaled <= 127)
                {
                    mod = 1;
                    displacement_bytes = 1;
                    displacement = (uint32_t)scaled;
                }
                else
                {
                    mod = 2;
                    displacement_bytes = 4;
                }
            }

            out[length] = (uint8_t)((mod << 6) | (reg_field << 3) | rm_field);
            length += 1;
            if (sib)
            {
                const uint8_t scale = mode & BUSTER_X86_MODE_SCALE;
                const uint8_t index_field = index ? (index_register & 7) : 4;
                const uint8_t base_field = base ? (rm & 7) : 5;
                out[length] = (uint8_t)((scale << 6) | (index_field << 3) | base_field);
                length += 1;
            }
            for (uint32_t i = 0; i < displacement_bytes; i += 1)
            {
                out[length] = (uint8_t)(displacement >> (8 * i));
                length += 1;
            }
        }

        for (uint32_t i = 0; i < form->relative_bytes; i += 1)
        {
            out[length] = (uint8_t)((uint32_t)instruction->displacement >> (8 * i));
            length += 1;
        }

        const uint64_t immediate = (uint32_t)instruction->immediate | ((uint64_t)(uint32_t)instruction->displacement << 32);
        for (uint32_t i = 0; i < form->immediate_bytes; i += 1)
        {
            out[length] = (uint8_t)(immediate >> (8 * i));
            length += 1;
        }
    }
    return length;
}

static int64_t encode_scalar(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity, uint32_t* offsets, uint32_t* fixup_ends,
                             X86EncoderMode encoder)
{
    int64_t result;
    if (count > BUSTER_X86_ENCODER_MAX_INSTRUCTIONS)
    {
        result = X86_ENCODER_ERROR_TOO_MANY_INSTRUCTIONS;
    }
    else if (capacity < BUSTER_X86_ENCODER_CAPACITY(count))
    {
        result = X86_ENCODER_ERROR_OUTPUT_TOO_SMALL;
    }
    else if (validate_instructions(instructions, count, encoder) != count)
    {
        result = X86_ENCODER_ERROR_INVALID_INSTRUCTION;
    }
    else
    {
        uint64_t length = 0;
        uint64_t marked = 0;
        for (uint64_t i = 0; i < count; i += 1)
        {
            if (offsets != 0)
            {
                offsets[i] = (uint32_t)length;
            }
            length += encode_one(&instructions[i], output + length, encoder);
            if (fixup_ends != 0 && (instructions[i].form & BUSTER_X86_FORM_FIXUP))
            {
                fixup_ends[marked] = (uint32_t)length;
                marked += 1;
            }
        }
        result = (int64_t)length;
    }
    return result;
}

static int64_t x86_encode_scalar(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity, uint32_t* offsets,
                                 uint32_t* fixup_ends)
{
    return encode_scalar(instructions, count, output, capacity, offsets, fixup_ends, X86_ENCODER_MODE_64);
}

static int64_t x86_measure_scalar(const X86Instruction* instructions, uint64_t count, uint32_t* offsets)
{
    int64_t result;
    if (count > BUSTER_X86_ENCODER_MAX_INSTRUCTIONS)
    {
        result = X86_ENCODER_ERROR_TOO_MANY_INSTRUCTIONS;
    }
    else
    {
        uint32_t length = 0;
        uint8_t bytes[BUSTER_X86_MAX_INSTRUCTION_BYTES];
        for (uint64_t i = 0; i < count; i += 1)
        {
            offsets[i] = length;
            length += encode_one(&instructions[i], bytes, X86_ENCODER_MODE_64);
        }
        result = length;
    }
    return result;
}

static int64_t x86_encode_scalar_address32(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity, uint32_t* offsets,
                                           uint32_t* fixup_ends)
{
    return encode_scalar(instructions, count, output, capacity, offsets, fixup_ends, X86_ENCODER_MODE_ADDRESS32);
}

static int64_t x86_encode_scalar_apx(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity, uint32_t* offsets,
                                     uint32_t* fixup_ends)
{
    return encode_scalar(instructions, count, output, capacity, offsets, fixup_ends, X86_ENCODER_MODE_APX);
}

static int64_t x86_encode_scalar_32(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity, uint32_t* offsets,
                                    uint32_t* fixup_ends)
{
    return encode_scalar(instructions, count, output, capacity, offsets, fixup_ends, X86_ENCODER_MODE_32);
}

static int64_t x86_encode_scalar_16(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity, uint32_t* offsets,
                                    uint32_t* fixup_ends)
{
    return encode_scalar(instructions, count, output, capacity, offsets, fixup_ends, X86_ENCODER_MODE_16);
}

// ---------------------------------------------------------------------------------------------------------------------
// The vectorized encoder

// Instructions per group: the dword lanes of a ZMM register in Phase II.
#define BUSTER_X86_GROUP_INSTRUCTIONS 16
// Instructions per ZMM register in Phases I and III.
#define BUSTER_X86_QUAD_INSTRUCTIONS 4
// The most instructions per loop iteration of encode_batches.
#define BUSTER_X86_MAX_ITERATION_INSTRUCTIONS 64

// How Phase I loads a quad's templates: a YMM load and vinserti64x4 per two templates, and vshufi64x2 to separate
// bytes from layouts (shuffles, which only two of Zen 5's four vector pipes run), with each template's address from a
// load of the record's form; the same, with the forms taken from the records Phase I already has in vector registers
// (fewer loads in flight, as the load queue is what fills first on Zen 5, but a longer dependency chain, which fills
// the FP scheduler instead); or 16 bytes into each lane with merge-masked loads (which any vector pipe can merge, but
// each spans 64 bytes). On Zen 5 the first is the fastest; the benchmark times the others.
typedef enum X86Gather
{
    X86_GATHER_INSERTS,
    X86_GATHER_INSERTS_FORMS_FROM_RECORDS,
    X86_GATHER_MASKED_LOADS,
} X86Gather;

// How Phase III appends a quad's machine code to the output: compressed in a register and stored whole (64 bytes,
// past the instructions but inside the output's capacity), compressed in a register and stored with a mask (exactly
// the instruction bytes; the mask is a GPR to mask register move, two shuffle-pipe µops on Zen 5), or compressed
// straight to memory (8 µops on Zen 5, and microcoded, about 170 cycles, on Zen 4). On Zen 5 the first two are about
// as fast; the third is slower.
typedef enum X86Store
{
    X86_STORE_FULL,
    X86_STORE_MASKED,
    X86_STORE_COMPRESS,
} X86Store;

// How a group appends the end offsets of its marked instructions to the fixup ends: compressed in a register
// (vpcompressd, two µops on FP1 and FP2 on Zen 5) and stored whole (16 ends, past the marked ones' but inside the fixup
// ends' capacity), or compressed straight to memory (8 µops, one store every 3 cycles on Zen 5, and 144 µops, one every
// 72 cycles, on Zen 4: uops.info).
typedef enum X86FixupStore
{
    X86_FIXUP_STORE_FULL,
    X86_FIXUP_STORE_COMPRESS,
} X86FixupStore;

// What Phase II computes for each instruction of a group, one instruction per dword lane.
typedef struct X86Fields X86Fields;
struct X86Fields
{
    // Canonical bytes 8-11: ModRM, SIB, REX.RXB and the opcode register.
    __m512i canonical;
    // Canonical bytes 12-15: what a VEX or EVEX prefix takes from the operands.
    __m512i prefix;
    // Canonical bytes 4-7: the displacement, or an EVEX form's 8-bit displacement in units of N.
    __m512i displacement;
    // Bit c is set if the instruction has canonical byte c (when its layout uses it).
    __m512i presence;
    // In 16- and 32-bit modes, the presence bits with the segment override's and BUSTER_X86_MODE_ADDRESS_SIZE's 67's,
    // which encode_group takes instead (a select of the two in compute_fields would change the other modes' code).
    __m512i legacy_presence;
    __m512i length;
};

#define BUSTER_X86_SPLAT(value) _mm512_set1_epi32((int)(value))

// The canonical dword's fields (Phase II builds them where they go: ModRM in bits 0-7, SIB in 8-15, REX.RXB in 16-18
// and the opcode register in 24-26, with EVEX's bit 4 of reg in 28 and of a register rm in 30), as masks.
#define BUSTER_X86_FIELD_RM 0x00000007
#define BUSTER_X86_FIELD_REG 0x00000038
#define BUSTER_X86_FIELD_BASE 0x00000700
#define BUSTER_X86_FIELD_INDEX 0x00003800
#define BUSTER_X86_FIELD_SCALE 0x0000c000
#define BUSTER_X86_FIELD_REX_B 0x00010000
#define BUSTER_X86_FIELD_REX_X 0x00020000
#define BUSTER_X86_FIELD_REX_R 0x00040000
#define BUSTER_X86_FIELD_OPCODE_REGISTER 0x07000000
#define BUSTER_X86_FIELD_REG_HIGH 0x10000000
#define BUSTER_X86_FIELD_RM_HIGH 0x40000000
#define BUSTER_X86_FIELDS_ALWAYS \
    (BUSTER_X86_FIELD_REG | BUSTER_X86_FIELD_REX_R | BUSTER_X86_FIELD_OPCODE_REGISTER | BUSTER_X86_FIELD_REG_HIGH | BUSTER_X86_FIELD_RM_HIGH)
// What each addressing mode decides, indexed (by vpermd) with its memory, base, index and RIP bits: operands >> 26.
// x86_mode_fields says which canonical bits come from the operands; x86_mode_constants, which bits are fixed (mod 11
// for a register, rm 100 for a SIB byte or 101 for RIP, a SIB base of 101 or index of 100 for none); and
// x86_mode_presence, the canonical bytes the mode always has (a SIB byte, a 32-bit displacement). A mode with a base also
// has a displacement and maybe a SIB byte that depend on the operands, which compute_fields works out. The other
// combinations of the bits are invalid (x86_validate rejects them), and their entries zero.
static const uint32_t x86_mode_fields[16] __attribute__((aligned(64))) = {
    [0] = BUSTER_X86_FIELDS_ALWAYS | BUSTER_X86_FIELD_RM | BUSTER_X86_FIELD_REX_B,
    [1] = BUSTER_X86_FIELDS_ALWAYS,
    [3] = BUSTER_X86_FIELDS_ALWAYS | BUSTER_X86_FIELD_RM | BUSTER_X86_FIELD_BASE | BUSTER_X86_FIELD_REX_B,
    [5] = BUSTER_X86_FIELDS_ALWAYS | BUSTER_X86_FIELD_INDEX | BUSTER_X86_FIELD_SCALE | BUSTER_X86_FIELD_REX_X,
    [7] = BUSTER_X86_FIELDS_ALWAYS | BUSTER_X86_FIELD_BASE | BUSTER_X86_FIELD_INDEX | BUSTER_X86_FIELD_SCALE | BUSTER_X86_FIELD_REX_B | BUSTER_X86_FIELD_REX_X,
    [9] = BUSTER_X86_FIELDS_ALWAYS,
};
// x86_encode_apx's: a memory operand's rm is its base, which can be 16 to 31 there, and whose bit 4 is EVEX's B4 (see
// compute_fields), not EVEX's X, as a vector register rm's is: the memory modes leave out bit 30.
#define BUSTER_X86_FIELDS_ALWAYS_MEMORY (BUSTER_X86_FIELDS_ALWAYS & ~BUSTER_X86_FIELD_RM_HIGH)
static const uint32_t x86_mode_fields_apx[16] __attribute__((aligned(64))) = {
    [0] = BUSTER_X86_FIELDS_ALWAYS | BUSTER_X86_FIELD_RM | BUSTER_X86_FIELD_REX_B,
    [1] = BUSTER_X86_FIELDS_ALWAYS_MEMORY,
    [3] = BUSTER_X86_FIELDS_ALWAYS_MEMORY | BUSTER_X86_FIELD_RM | BUSTER_X86_FIELD_BASE | BUSTER_X86_FIELD_REX_B,
    [5] = BUSTER_X86_FIELDS_ALWAYS_MEMORY | BUSTER_X86_FIELD_INDEX | BUSTER_X86_FIELD_SCALE | BUSTER_X86_FIELD_REX_X,
    [7] = BUSTER_X86_FIELDS_ALWAYS_MEMORY | BUSTER_X86_FIELD_BASE | BUSTER_X86_FIELD_INDEX | BUSTER_X86_FIELD_SCALE | BUSTER_X86_FIELD_REX_B |
          BUSTER_X86_FIELD_REX_X,
    [9] = BUSTER_X86_FIELDS_ALWAYS_MEMORY,
};
static const uint32_t x86_mode_constants[16] __attribute__((aligned(64))) = {
    [0] = 0xc0,
    [1] = 0x04 | (5 << 8) | (4 << 11),
    [3] = 4 << 11,
    [5] = 0x04 | (5 << 8),
    [7] = 0x04,
    [9] = 0x05,
};
static const uint32_t x86_mode_presence[16] __attribute__((aligned(64))) = {
    [0] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT,
    [1] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | 0x2f0,
    [3] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT,
    [5] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | 0x2f0,
    [7] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | 0x200,
    [9] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | 0xf0,
};
// 16- and 32-bit modes' (x86_encode_mode32's and x86_encode_mode16's), in two halves, the 32-bit address one's and the
// 16-bit address one's, which BUSTER_X86_MODE_ADDRESS_SIZE's bit (operands bit 30, bit 4 of the index) picks between
// with vpermt2d (see x86_mode_lookup). A 32-bit address is as in 64-bit mode's tables but RIP, which these modes don't
// have, so an absolute address is ModRM's own (mod 00, rm 101, without a SIB byte). A 16-bit address has no SIB byte,
// and an index only with a base, without a scale: ModRM.rm comes from the base and whether there's an index (see
// compute_fields), and an absolute address is mod 00, rm 110, with a 16-bit displacement. Its entries say it's 16-bit
// in bit 21, which no layout looks at.
#define BUSTER_X86_MODE_TABLE_ADDRESS16 0x00200000u
static const uint32_t x86_mode_fields_16[16] __attribute__((aligned(64))) = {
    [0] = BUSTER_X86_FIELDS_ALWAYS | BUSTER_X86_FIELD_RM | BUSTER_X86_FIELD_REX_B,
    [1] = BUSTER_X86_FIELDS_ALWAYS,
    [3] = BUSTER_X86_FIELDS_ALWAYS,
    [7] = BUSTER_X86_FIELDS_ALWAYS,
};
static const uint32_t x86_mode_constants_32[16] __attribute__((aligned(64))) = {
    [0] = 0xc0,
    [1] = 0x05,
    [3] = 4 << 11,
    [5] = 0x04 | (5 << 8),
    [7] = 0x04,
};
static const uint32_t x86_mode_constants_16[16] __attribute__((aligned(64))) = {
    [0] = 0xc0,
    [1] = 0x06,
};
static const uint32_t x86_mode_presence_32[16] __attribute__((aligned(64))) = {
    [0] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT,
    [1] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | 0xf0,
    [3] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT,
    [5] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | 0x2f0,
    [7] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | 0x200,
};
static const uint32_t x86_mode_presence_16[16] __attribute__((aligned(64))) = {
    [0] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT,
    [1] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | 0x30 | BUSTER_X86_MODE_TABLE_ADDRESS16,
    [3] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | BUSTER_X86_MODE_TABLE_ADDRESS16,
    [7] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | BUSTER_X86_MODE_TABLE_ADDRESS16,
};
// vpshufb's tables (the same 16 bytes in each 128-bit lane), for 16- and 32-bit modes. A 16-bit address's ModRM.rm by
// its base's number (0 to 7) and whether there's an index (8), which compute_fields XORs with the index's low 3 bits:
// BX's 7, BP's 6, SI's 4 and DI's 5 alone, and with an index (SI's 6 or DI's 7), BX's 6 ([bx+si] 0, [bx+di] 1) and BP's
// 4 ([bp+si] 2, [bp+di] 3); zero for a base that can't be. And a segment override's byte by its number in the
// decorations (ES's 1 to GS's 6).
#define BUSTER_X86_LANES(...) __VA_ARGS__, __VA_ARGS__, __VA_ARGS__, __VA_ARGS__
// By a base's low nibble (in operands byte 1, where the result lands too): SIB's presence bit (9, here bit 1) for RSP's
// and R12's 4 and 12 (and R20's and R28's, with APX), which need a SIB byte, and bit 15 (here 7) for RBP's and R13's 5
// and 13, which need a displacement (see compute_fields).
static const uint8_t x86_base_kinds[64] __attribute__((aligned(64))) = { BUSTER_X86_LANES(0, 0, 0, 0, 2, 0x80, 0, 0, 0, 0, 0, 0, 2, 0x80, 0, 0) };
static const uint8_t x86_rm16[64] __attribute__((aligned(64))) = { BUSTER_X86_LANES(0, 0, 0, 7, 0, 6, 4, 5, 0, 0, 0, 6, 0, 4, 0, 0) };
static const uint8_t x86_overrides[64] __attribute__((aligned(64))) = { BUSTER_X86_LANES(0, 0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65, 0, 0, 0, 0, 0, 0, 0, 0, 0) };
#undef BUSTER_X86_LANES

// vpmultishiftqb controls that put the operands' register fields where they go in the canonical dword: each byte of
// the result is the 8 bits of its quadword (two instructions' operands) starting at the control byte's offset. The
// first puts rm's low bits in ModRM.rm, SIB.base and the opcode register, and rm's bit 3 in REX.B; the second, reg's
// low bits in ModRM.reg (from bit 61 for the low dword: bits 61-63 wrap around below them), the index's low bits in
// SIB.index and its bit 3 in REX.X, and reg's bit 4 in bit 28; the third, the scale in SIB.scale, reg's bit 3 in
// REX.R and rm's bit 4 in bit 30.
#define BUSTER_X86_FIELDS_RM_CONTROL 0x282b2828080b0808ll
#define BUSTER_X86_FIELDS_REG_INDEX_CONTROL 0x20322d1d00120d3dll
#define BUSTER_X86_FIELDS_SCALE_REX_R_CONTROL 0x2621320006011200ll
#define BUSTER_X86_FIELDS_FROM_RM (BUSTER_X86_FIELD_RM | BUSTER_X86_FIELD_BASE | BUSTER_X86_FIELD_REX_B | BUSTER_X86_FIELD_OPCODE_REGISTER)
#define BUSTER_X86_FIELDS_FROM_SCALE_REX_R (BUSTER_X86_FIELD_SCALE | BUSTER_X86_FIELD_REX_R | BUSTER_X86_FIELD_RM_HIGH)
// A vpmultishiftqb control that puts each record's fourth dword (form | vvvv << 16 | decorations << 24) where a
// prefix wants it: vvvv's low bits in P1's bits 6-3 (bits 13-20, in byte 14), the decorations as they are (byte 15).
#define BUSTER_X86_PREFIX_CONTROL 0x382d0000180d0000ll

// vpternlogd truth tables, of operands A, B and C.
#define BUSTER_X86_A_OR_B_AND_C 0xf8
#define BUSTER_X86_A_AND_B_OR_C 0xea
#define BUSTER_X86_A_OR_B_THEN_AND_C 0xa8
#define BUSTER_X86_C_SELECTS_A_OVER_B 0xe4
#define BUSTER_X86_A_AND_B_AND_NOT_C 0x40
#define BUSTER_X86_A_AND_NOT_B_AND_NOT_C 0x10
#define BUSTER_X86_A_AND_NOT_B_AND_C 0x20
#define BUSTER_X86_A_OR_NOT_B_AND_C 0xf2
#define BUSTER_X86_A_AND_NOT_B_OR_C 0xb0
#define BUSTER_X86_A_OR_B_OR_C 0xfe
#define BUSTER_X86_NOT_A_OR_B_THEN_AND_C 0x8a
#define BUSTER_X86_A_OR_NOT_B_THEN_AND_C 0xa2
#define BUSTER_X86_A_XOR_B_AND_C 0x78
#define BUSTER_X86_A_XOR_B_AND_NOT_C 0x14
#define BUSTER_X86_A_XOR_B_THEN_AND_C 0x28
#define BUSTER_X86_A_AND_B_AND_C 0x80
// A where B or C is set.
#define BUSTER_X86_A_WHERE_B_OR_C 0xe0
// A where not both B and C.
#define BUSTER_X86_A_AND_NOT_B_AND_C_BOTH 0x70
#define BUSTER_X86_NOT_A_AND_B_OR_C 0xae
#define BUSTER_X86_A_AND_B_THEN_XOR_C 0x6a
// B where A is set, C where it isn't.
#define BUSTER_X86_A_SELECTS_B_OVER_C 0xca

// A 16- or 32-bit mode's table entries (see x86_mode_fields_16) for each lane's addressing mode (`mode`, operands >>
// 26): from the half of the mode's own address size, or with BUSTER_X86_MODE_ADDRESS_SIZE (bit 4), from the other's.
BUSTER_INLINE __m512i x86_mode_lookup(__m512i mode, const uint32_t* address32, const uint32_t* address16, bool mode16)
{
    return _mm512_permutex2var_epi32(_mm512_load_si512(mode16 ? address16 : address32), mode, _mm512_load_si512(mode16 ? address32 : address16));
}

// Phase II. `operands` is each record's third dword (reg | rm << 8 | index << 16 | mode << 24), `displacement` its
// second, `extension` its fourth (form | vvvv << 16 | decorations << 24), and `flags` and `checks` its template's.
//
// On Zen 5 (uops.info), compares into mask registers, permutes and broadcasts run on two of the four vector pipes,
// the ones Phase III's shuffles and compressions need, and moving a mask register from a general one (or reloading a
// spilled one) takes two µops there; ALU operations, vpternlogd and vpmovm2d run on all four, shifts (variable ones
// too) on two others, and vpmultishiftqb, rotates and vplzcntd on two more. So the addressing mode's decisions are
// table lookups, the register fields move with vpmultishiftqb, and the compares are vector arithmetic, for vpternlogd
// to combine.
//
// The template's checks and flags arrive last, at the end of the longest dependency chain (the record's form, its
// template's address, the template's loads and Phase I's shuffles), so what depends on the operands alone comes
// first and what needs the flags is a few operations from them: an EVEX displacement's scale picks among
// compressibilities worked out from the displacement for every scale, and REX and the VEX and EVEX prefixes take the
// register fields from before ModRM.mod, which depends on the displacement's size and so on the scale.
//
// For x86_encode_apx and for 16- and 32-bit modes (`encoder` is a constant), what REX2 and the modes add is worked out
// alongside, and the values that change selected at the end, so that the other loops' code is the same as without it
// (as with 32-bit addresses' masks: an `if` on the constant changes theirs, as clang simplifies the function before it
// inlines it).
BUSTER_INLINE X86Fields compute_fields(__m512i operands, __m512i displacement, __m512i extension, __m512i flags, __m512i checks, X86EncoderMode encoder)
{
    const bool address32 = encoder == X86_ENCODER_MODE_ADDRESS32;
    const bool apx = encoder == X86_ENCODER_MODE_APX;
    const bool legacy = x86_mode_is_legacy(encoder);
    const bool mode16 = encoder == X86_ENCODER_MODE_16;
    const __m512i one = BUSTER_X86_SPLAT(1);
    const __m512i mode = _mm512_srli_epi32(operands, 26);
    const __m512i mode_fields = legacy ? x86_mode_lookup(mode, x86_mode_fields, x86_mode_fields_16, mode16)
                                       : _mm512_permutexvar_epi32(mode, _mm512_load_si512(apx ? x86_mode_fields_apx : x86_mode_fields));
    const __m512i mode_constants = legacy ? x86_mode_lookup(mode, x86_mode_constants_32, x86_mode_constants_16, mode16)
                                          : _mm512_permutexvar_epi32(mode, _mm512_load_si512(x86_mode_constants));
    const __m512i mode_presence = legacy ? x86_mode_lookup(mode, x86_mode_presence_32, x86_mode_presence_16, mode16)
                                         : _mm512_permutexvar_epi32(mode, _mm512_load_si512(x86_mode_presence));

    // All the register fields in place, then those the mode takes, and its constants: the canonical dword but for
    // ModRM.mod's displacement bits.
    const __m512i from_rm = _mm512_multishift_epi64_epi8(_mm512_set1_epi64(BUSTER_X86_FIELDS_RM_CONTROL), operands);
    const __m512i from_reg_index = _mm512_multishift_epi64_epi8(_mm512_set1_epi64(BUSTER_X86_FIELDS_REG_INDEX_CONTROL), operands);
    const __m512i from_scale_rex_r = _mm512_multishift_epi64_epi8(_mm512_set1_epi64(BUSTER_X86_FIELDS_SCALE_REX_R_CONTROL), operands);
    __m512i fields = _mm512_ternarylogic_epi32(from_rm, from_reg_index, BUSTER_X86_SPLAT(BUSTER_X86_FIELDS_FROM_RM), BUSTER_X86_C_SELECTS_A_OVER_B);
    fields = _mm512_ternarylogic_epi32(from_scale_rex_r, fields, BUSTER_X86_SPLAT(BUSTER_X86_FIELDS_FROM_SCALE_REX_R), BUSTER_X86_C_SELECTS_A_OVER_B);
    const __m512i register_fields = _mm512_ternarylogic_epi32(fields, mode_fields, mode_constants, BUSTER_X86_A_AND_B_OR_C);

    // After a base, the displacement is none if it's zero (except after RBP and R13, whose ModRM.mod 00 means
    // something else, so they take an 8-bit zero), 8-bit if it fits and 32-bit otherwise; and RSP and R12 need a SIB
    // byte. All ones where each holds: the base bit, shifted to the top and back; and zero tests, which for a value x
    // below 2^31 are (x - 1) >> 31, and otherwise ~(x | -x) >> 31. Each takes up to four operations that any vector
    // pipe runs, two cycles each on Zen 5, where a compare and vpmovm2d take eight cycles on the shuffle pipes. Which of
    // the two kinds the base is comes from one vpshufb of x86_base_kinds by rm's low nibble (operands byte 1): SIB's
    // presence bit (9) for 4 and 12, bit 15 for 5 and 13.
    const __m512i base = _mm512_srai_epi32(_mm512_slli_epi32(operands, 31 - 26 - 1), 31);
    const __m512i base_kind = _mm512_shuffle_epi8(_mm512_load_si512(x86_base_kinds), operands);
    const __m512i rm_5 = _mm512_srai_epi32(_mm512_slli_epi32(base_kind, 31 - 15), 31);
    // In 16- and 32-bit modes, a 16-bit address (the mode table's bit 21, all ones) has a 16-bit displacement, modulo
    // 2^16 as the address is (65535 is -1, an 8-bit one), so its low 16 bits, sign-extended, decide its size; and BP
    // alone needs one as RBP does, but [bp + si] and [bp + di] don't.
    const __m512i address16 = _mm512_srai_epi32(_mm512_slli_epi32(mode_presence, 31 - 21), 31);
    const __m512i has_index = _mm512_srai_epi32(_mm512_slli_epi32(operands, 31 - 28), 31);
    const __m512i sized_displacement = legacy ? _mm512_ternarylogic_epi32(_mm512_srai_epi32(_mm512_slli_epi32(displacement, 16), 16), displacement, address16, BUSTER_X86_C_SELECTS_A_OVER_B) : displacement;
    const __m512i needs_displacement = legacy ? _mm512_ternarylogic_epi32(rm_5, address16, has_index, BUSTER_X86_A_AND_NOT_B_AND_C_BOTH) : rm_5;
    const __m512i nonzero = _mm512_srai_epi32(_mm512_or_epi32(sized_displacement, _mm512_sub_epi32(_mm512_setzero_si512(), sized_displacement)), 31);
    // All ones after a base with a displacement, 8-bit or 32-bit.
    const __m512i base_displacement = _mm512_ternarylogic_epi32(base, nonzero, needs_displacement, BUSTER_X86_A_WHERE_B_OR_C);
    const __m512i base_sib = _mm512_and_epi32(base, base_kind);
    // An 8-bit displacement fits if the displacement is a multiple of the form's N (2^s: 1, or for EVEX, the scale in
    // the flags) whose quotient is in [-128, 127]: if s is at most the displacement's trailing zeros and at least the
    // bit length of |displacement| >> 7 (with |x| - 1 for a negative x, as the range is asymmetric). Bit s of
    // `compressible` says so, for every s: the bits up to the trailing zeros, x ^ (x - 1), but those below the bit
    // length, 0x01ffffff >> lzcnt(|x|). The template's scale is s + 1, which rotates bit s into the sign; and the
    // displacement rotated left by one, then right by it, has the quotient in its low byte, which is where the
    // canonical slot takes an 8-bit displacement from. Only the rotations and fits8's shift wait for the flags, and
    // what depends on fits8 (ModRM.mod, the displacement and its presence bits) is one select each between values
    // worked out before it.
    const __m512i magnitude = _mm512_xor_epi32(sized_displacement, _mm512_srai_epi32(sized_displacement, 31));
    const __m512i too_long = _mm512_srlv_epi32(BUSTER_X86_SPLAT(0x01ffffff), _mm512_lzcnt_epi32(magnitude));
    const __m512i compressible = _mm512_ternarylogic_epi32(sized_displacement, _mm512_sub_epi32(sized_displacement, one), too_long, BUSTER_X86_A_XOR_B_AND_NOT_C);
    const __m512i scale = _mm512_srli_epi32(flags, BUSTER_X86_TEMPLATE_DISP8_SCALE);
    const __m512i fits8 = _mm512_srai_epi32(_mm512_rorv_epi32(compressible, scale), 31);
    // ModRM.mod: 01 for an 8-bit displacement, 10 for a 32-bit one after a base.
    const __m512i canonical8 = _mm512_ternarylogic_epi32(register_fields, base_displacement, BUSTER_X86_SPLAT(0x40), BUSTER_X86_A_OR_B_AND_C);
    const __m512i canonical32 = _mm512_ternarylogic_epi32(register_fields, base, BUSTER_X86_SPLAT(0x80), BUSTER_X86_A_OR_B_AND_C);
    const __m512i canonical = _mm512_ternarylogic_epi32(fits8, canonical8, canonical32, BUSTER_X86_A_SELECTS_B_OVER_C);

    // REX: for REX.RXB or W (REX_ALWAYS, at bit 19), and for SPL, BPL, SIL and DIL: 4 to 7 without the high byte flag,
    // in a byte operand. (register & 0xfc) ^ 4 is zero for those (for rm, with the memory flag, bit 26, at 18 after the
    // shift), and one less, all ones then and below 0x400fc otherwise: so its bit 24, and rm's bit 25, are where the
    // template's BYTE_REG and BYTE_RM are. rex_reasons has a bit set for each reason the instruction needs REX (REX.RXB,
    // and the flags' W, BYTE_REG and BYTE_RM where they apply), all of them at 0x10000 or above, so that one vpminud
    // with 0x400 makes REX's presence bit (10): only that waits for the flags, and none of it for a compare into a mask
    // register, which takes six cycles on the busier pipes. REX.RXB is the register fields' before the mode takes its
    // own (`fields`, without the mode table's vpermd): an instruction x86_validate accepts has zeros in the fields its
    // mode doesn't use (an rm without a base, an index without one), whose bits 3 are then REX.B's and REX.X's zeros.
    const __m512i spl_reg = _mm512_sub_epi32(_mm512_ternarylogic_epi32(operands, BUSTER_X86_SPLAT(0xfc), BUSTER_X86_SPLAT(4), BUSTER_X86_A_AND_B_THEN_XOR_C), one);
    const __m512i spl_rm = _mm512_sub_epi32(_mm512_ternarylogic_epi32(_mm512_srli_epi32(operands, 8), BUSTER_X86_SPLAT(0x400fc), BUSTER_X86_SPLAT(4), BUSTER_X86_A_AND_B_THEN_XOR_C),
                                            one);
    const __m512i spl_reg_rex = _mm512_ternarylogic_epi32(spl_reg, BUSTER_X86_SPLAT(BUSTER_X86_TEMPLATE_BYTE_REG), BUSTER_X86_SPLAT(0xf0000), BUSTER_X86_A_AND_B_OR_C);
    const __m512i rex_flags = _mm512_ternarylogic_epi32(spl_rm, BUSTER_X86_SPLAT(BUSTER_X86_TEMPLATE_BYTE_RM), spl_reg_rex, BUSTER_X86_A_AND_B_OR_C);
    const __m512i rex_reasons = _mm512_ternarylogic_epi32(flags, rex_flags, _mm512_and_epi32(fields, BUSTER_X86_SPLAT(0x70000)), BUSTER_X86_A_AND_B_OR_C);
    // REX's presence bit (10).
    const __m512i rex_presence = _mm512_min_epu32(rex_reasons, BUSTER_X86_SPLAT(0x400));

    // APX's REX2, for a legacy form whose general registers or base or index are 16 to 31: all ones where the operands'
    // reg, rm or index has bit 4 (R4, B4 and X4), which in another form is a VEX or EVEX one's (whose template has no
    // REX2 position to use it) or invalid. Then REX's position has D5 (canonical byte 10 flips the template's 0x40 | W by
    // 0x95 ^ W), and the payload is canonical byte 15 in map 0 (R4, X4, B4, R3, X3 and B3; the template has W) and
    // canonical byte 13 in map 1 (which flips its 0F into the payload with M0 and W), in the prefix dword, which a legacy
    // form doesn't otherwise use. A +r form's opcode register (canonical byte 11) drops the bits 4 above it.
    //
    // The payload's R4, X4 and B4 (bits 30, 29 and 28) are reg's, the index's and rm's bit 4 (the operands' 4, 20 and
    // 12), which one multiplication by 2^26 + 2^9 + 2^16 puts there: its other products, at bits 13, 20 and 21, don't
    // reach them, nor carry (and the mode needn't mask them: a valid instruction has no index's or rm's bit 4 where the
    // mode has neither); R3, X3 and B3 (bits 26, 25 and 24) are REX's, the register fields'. Only the payload's byte 3
    // is used (prefix takes it alone).
    const __m512i high_registers = _mm512_and_epi32(operands, BUSTER_X86_SPLAT(0x101010));
    const __m512i rex2 = _mm512_srai_epi32(_mm512_sub_epi32(_mm512_setzero_si512(), high_registers), 31);
    const __m512i payload = _mm512_ternarylogic_epi32(_mm512_slli_epi32(register_fields, 8), _mm512_mullo_epi32(high_registers, BUSTER_X86_SPLAT((1u << 26) | (1u << 9) | (1u << 16))),
                                                      BUSTER_X86_SPLAT(0x07000000), BUSTER_X86_C_SELECTS_A_OVER_B);
    const __m512i payload_w = _mm512_ternarylogic_epi32(payload, _mm512_slli_epi32(flags, 8), BUSTER_X86_SPLAT(0x08000000), BUSTER_X86_A_OR_B_AND_C);
    const __m512i map1_payload = _mm512_ternarylogic_epi32(_mm512_srli_epi32(payload_w, 16), BUSTER_X86_SPLAT(0x8f00), rex2, BUSTER_X86_A_XOR_B_THEN_AND_C);
    const __m512i d5 = _mm512_ternarylogic_epi32(BUSTER_X86_SPLAT(0x950000), flags, BUSTER_X86_SPLAT(0x80000), BUSTER_X86_A_XOR_B_AND_C);

    // A VEX or EVEX prefix's operand bits (canonical bytes 12-15), which flip the template's: R, X and B (REX's,
    // canonical bits 18-16) in P0's bits 7-5, with EVEX's R' (bit 28) in its bit 4 and EVEX's X of a register rm (bit
    // 30) in its bit 6, which shifting down by 3 and 16 puts there (and nothing in bits 16-31).
    const __m512i p0 = _mm512_ternarylogic_epi32(_mm512_srli_epi32(register_fields, 16), _mm512_srli_epi32(register_fields, 3), BUSTER_X86_SPLAT(0xe000),
                                                 BUSTER_X86_A_OR_B_AND_C);
    // With APX, EVEX's B4 (P0's bit 3, the prefix dword's bit 11) is a base's or a general register rm's bit 4 (rm's,
    // the operands' bit 12, with a base or with GENERAL_RM, the flags' bit 19; a memory operand without a base has rm
    // 0), and X4 (P1's bit 2, bit 18, inverted, a one in the template) a general index's (the index's bit 4, the
    // operands' bit 20, with GENERAL_INDEX_4, the flags' bit 18), which then isn't V''s. A general register rm's bit 4
    // isn't X's either, which p0 has from bit 30 (a base's isn't there: see x86_mode_fields_apx): it flips that back.
    // They come last, after the flags, which only they wait for. A legacy form's prefix dword is REX2's (below), and a
    // VEX form's instructions have none.
    const __m512i general_index_4 = _mm512_ternarylogic_epi32(operands, _mm512_slli_epi32(flags, 20 - 18), BUSTER_X86_SPLAT(0x100000), BUSTER_X86_A_AND_B_AND_C);
    const __m512i b4 = _mm512_ternarylogic_epi32(_mm512_srli_epi32(operands, 12 - 11), base, _mm512_srli_epi32(flags, 19 - 11), BUSTER_X86_A_WHERE_B_OR_C);
    const __m512i general_x = _mm512_ternarylogic_epi32(_mm512_slli_epi32(b4, 14 - 11), base, BUSTER_X86_SPLAT(0x4000), BUSTER_X86_A_AND_NOT_B_AND_C);
    const __m512i b4_x4 = _mm512_ternarylogic_epi32(b4, _mm512_srli_epi32(general_index_4, 20 - 18), BUSTER_X86_SPLAT(0x800), BUSTER_X86_C_SELECTS_A_OVER_B);
    const __m512i evex_bits = _mm512_or_epi32(b4_x4, general_x);
    // VEX2, if the form can be (BUSTER_X86_TEMPLATE_VEX2, bit 13) and there's no X or B (REX's, canonical bits 17 and
    // 16, as REX's test takes them, which shifting down by 3 puts in bits 14 and 13; the flags' bit 14 is always set):
    // C4 becomes C5 (bit 0), R moves to P1's bit 7, and P0 isn't there. vex2_test, which has no other bits, is zero only
    // then, so that a vpminud with 0x2000 makes P0's presence bit. (A VEX form's register rm can't be past 15, so p0's
    // EVEX X of one doesn't matter here.)
    const __m512i vex2_test = _mm512_ternarylogic_epi32(_mm512_srli_epi32(fields, 3), flags, BUSTER_X86_SPLAT(0x6000), BUSTER_X86_A_OR_NOT_B_THEN_AND_C);
    const __m512i vex2 = _mm512_srai_epi32(_mm512_sub_epi32(vex2_test, one), 31);
    const __m512i p0_presence = _mm512_min_epu32(vex2_test, BUSTER_X86_SPLAT(0x2000));
    const __m512i vex2_bits = _mm512_ternarylogic_epi32(vex2, _mm512_slli_epi32(p0, 8), BUSTER_X86_SPLAT(0x800001), BUSTER_X86_A_AND_NOT_B_AND_C);
    // vvvv in P1's bits 6-3, and EVEX's P2: the decorations as they are, with V' in bit 3: vvvv's bit 4 (record bit
    // 20) or a VSIB index's (the operands' bit 20), one of which is zero (a form with an index past 15 has no vvvv
    // operand; with APX, a general index's bit 4 is X4's, which v_operands leaves out). The multishift puts the form's
    // bits 13-15 in P1's bits 0-2 (pp and L, the template's), where the last ternlog takes vex2_v's zeros instead.
    const __m512i vvvv_decorations = _mm512_multishift_epi64_epi8(_mm512_set1_epi64(BUSTER_X86_PREFIX_CONTROL), extension);
    const __m512i v_operands = apx ? _mm512_xor_epi32(operands, general_index_4) : operands;
    const __m512i vex2_v = _mm512_ternarylogic_epi32(_mm512_slli_epi32(_mm512_or_epi32(extension, v_operands), 7), vex2_bits, BUSTER_X86_SPLAT(0x08000000),
                                                     BUSTER_X86_C_SELECTS_A_OVER_B);
    const __m512i p0_vvvv_decorations = _mm512_ternarylogic_epi32(p0, vvvv_decorations, BUSTER_X86_SPLAT(0x0000f000), BUSTER_X86_C_SELECTS_A_OVER_B);
    const __m512i vex_prefix = _mm512_ternarylogic_epi32(vex2_v, p0_vvvv_decorations, BUSTER_X86_SPLAT(0x08870fff), BUSTER_X86_C_SELECTS_A_OVER_B);
    // With APX, an EVEX form's have B4 and X4 too, and a legacy form's (NO_REX clear, bit 10) are REX2's.
    const __m512i vector_form = _mm512_srai_epi32(_mm512_slli_epi32(flags, 31 - 10), 31);
    const __m512i prefix = apx ? _mm512_ternarylogic_epi32(_mm512_xor_epi32(vex_prefix, evex_bits),
                                                           _mm512_ternarylogic_epi32(payload, BUSTER_X86_SPLAT(0xff000000), map1_payload, BUSTER_X86_A_AND_B_OR_C), vector_form,
                                                           BUSTER_X86_C_SELECTS_A_OVER_B)
                               : vex_prefix;

    // Which canonical bytes the instruction has: the mode's (the always present ones among them, and a SIB byte),
    // the form's prefix bytes and a static displacement's (bits 4-7 of the flags; their bits 0-3 fall on the
    // immediate's, which are always set), and those that depend on the operands, the latest last: a tree, so that
    // the flags' terms and the displacement's (by fits8) come in last. The mode's bits that aren't presence bits fall
    // where no layout looks. A 16-bit address's displacement after a base is 16-bit.
    __m512i early_presence = _mm512_ternarylogic_epi32(mode_presence, base_sib, BUSTER_X86_SPLAT(0x200), BUSTER_X86_A_OR_B_AND_C);
    // With 32-bit addresses, a memory operand's 67: the mode's memory flag (bit 26) in BUSTER_X86_PRESENCE_ADDRESS32.
    // Without, a mask of zero (not a branch, which clang simplifies before it inlines the loop and knows the constant,
    // changing the 64-bit loop's code; a vpternlogd with it wouldn't fold away either).
    early_presence = _mm512_or_epi32(early_presence, _mm512_and_epi32(_mm512_srli_epi32(operands, 26 - BUSTER_X86_PRESENCE_ADDRESS32),
                                                                      BUSTER_X86_SPLAT(address32 ? 1u << BUSTER_X86_PRESENCE_ADDRESS32 : 0)));
    // With REX2, D5 in REX's position and map 0's payload.
    early_presence = apx ? _mm512_ternarylogic_epi32(early_presence, rex2, BUSTER_X86_SPLAT(0x8400), BUSTER_X86_A_OR_B_AND_C) : early_presence;
    const __m512i flags_presence = _mm512_ternarylogic_epi32(early_presence, flags, BUSTER_X86_SPLAT(0xdbff), BUSTER_X86_A_OR_B_AND_C);
    const __m512i operand_presence = _mm512_ternarylogic_epi32(flags_presence, p0_presence, rex_presence, BUSTER_X86_A_OR_B_OR_C);
    // The displacement after a base: its first byte if it fits in 8 bits, all of them (two for a 16-bit address)
    // otherwise.
    const __m512i displacement_bytes = legacy ? _mm512_ternarylogic_epi32(BUSTER_X86_SPLAT(0xf0), address16, BUSTER_X86_SPLAT(0xc0), BUSTER_X86_A_XOR_B_AND_C) : BUSTER_X86_SPLAT(0xf0);
    const __m512i displacement8_presence = _mm512_and_epi32(base_displacement, BUSTER_X86_SPLAT(0x10));
    const __m512i displacement32_presence = _mm512_and_epi32(base, displacement_bytes);
    const __m512i presence = _mm512_or_epi32(operand_presence, _mm512_ternarylogic_epi32(fits8, displacement8_presence, displacement32_presence, BUSTER_X86_A_SELECTS_B_OVER_C));

    // The length: the form's static bytes, the displacement, SIB and REX bytes the operands add (and a 32-bit address's
    // 67, or REX2's D5 in REX's place and map 0's payload, whose flags' bit 15 is clear), and one less for VEX2. The flags'
    // bit 23 is clear.
    const uint32_t dynamic_bytes = 0x6f0 | (address32 ? 1u << BUSTER_X86_PRESENCE_ADDRESS32 : 0) | (apx ? 0x8000 : 0);
    const __m512i dynamic = _mm512_ternarylogic_epi32(presence, flags, BUSTER_X86_SPLAT(dynamic_bytes), BUSTER_X86_A_AND_NOT_B_AND_C);
    const __m512i length = _mm512_add_epi32(_mm512_add_epi32(_mm512_popcnt_epi32(dynamic), _mm512_and_epi32(flags, BUSTER_X86_SPLAT(BUSTER_X86_TEMPLATE_STATIC_LENGTH))),
                                            vex2);

    // In 16- and 32-bit modes (worked out after the rest, whose first uses of the checks and the extension as dwords
    // have to come first for the other modes' code to be the same as without them), the decorations' segment override
    // (their bits 3-5, the extension's 27-29) where the template has a position for it, as its checks allow it (bit 27
    // clear: see mode_template_checks; those of an EVEX form with embedded rounding allow bit 29, the rounding mode's):
    // all ones in `override` where there's one, and its byte (a vpshufb of x86_overrides) in REX's canonical byte (see
    // BUSTER_X86_LAYOUT_OVERRIDE). An EVEX form's P2 has the decorations as they are (see vvvv_decorations), where the
    // override's bits would be b's and L'L's: they come out of the prefix.
    const __m512i no_override = _mm512_srai_epi32(_mm512_slli_epi32(checks, 31 - 27), 31);
    const __m512i override_bits = _mm512_ternarylogic_epi32(extension, no_override, BUSTER_X86_SPLAT(0x38000000), BUSTER_X86_A_AND_NOT_B_AND_C);
    const __m512i override = _mm512_srai_epi32(_mm512_sub_epi32(_mm512_setzero_si512(), override_bits), 31);
    const __m512i override_byte = _mm512_shuffle_epi8(_mm512_load_si512(x86_overrides), _mm512_srli_epi32(override_bits, 27 - 16));
    // And a 16-bit address's ModRM.rm, by its base and whether it has an index (a vpshufb of x86_rm16, by the base's
    // low 3 bits and the index bit), XORed with the index's low 3 bits.
    const __m512i rm16_key = _mm512_ternarylogic_epi32(_mm512_srli_epi32(operands, 8), _mm512_srli_epi32(operands, 28 - 3), BUSTER_X86_SPLAT(7), BUSTER_X86_C_SELECTS_A_OVER_B);
    const __m512i rm16_entry = _mm512_shuffle_epi8(_mm512_load_si512(x86_rm16), rm16_key);
    const __m512i rm16 = _mm512_ternarylogic_epi32(rm16_entry, _mm512_srli_epi32(operands, 16), BUSTER_X86_SPLAT(7), BUSTER_X86_A_XOR_B_THEN_AND_C);
    const __m512i base16 = _mm512_and_epi32(address16, base);
    const __m512i legacy_canonical = _mm512_or_epi32(_mm512_ternarylogic_epi32(canonical, rm16, base16, BUSTER_X86_A_OR_B_AND_C), override_byte);

    // The segment override in REX's place (presence bit 10, which the length counts) and BUSTER_X86_MODE_ADDRESS_SIZE's
    // 67 (its bit, 30, all ones in `other_size`, in BUSTER_X86_PRESENCE_ADDRESS32), one more byte each. A valid
    // instruction has at most 15 bytes; an invalid one can have 16, a template's every position (one of them for the 67:
    // see build_template), which is why these modes' output takes BUSTER_X86_ENCODER_LEGACY_CAPACITY.
    const __m512i other_size = _mm512_srai_epi32(_mm512_slli_epi32(operands, 31 - 30), 31);
    const __m512i legacy_presence = _mm512_ternarylogic_epi32(_mm512_ternarylogic_epi32(presence, override, BUSTER_X86_SPLAT(0x400), BUSTER_X86_A_OR_B_AND_C), other_size,
                                                              BUSTER_X86_SPLAT(1u << BUSTER_X86_PRESENCE_ADDRESS32), BUSTER_X86_A_OR_B_AND_C);
    const __m512i legacy_length = _mm512_sub_epi32(_mm512_sub_epi32(length, override), other_size);

    X86Fields result;
    const __m512i rex2_canonical = _mm512_and_epi32(_mm512_ternarylogic_epi32(d5, canonical, _mm512_and_epi32(rex2, BUSTER_X86_SPLAT(0xff0000)), BUSTER_X86_C_SELECTS_A_OVER_B),
                                                    BUSTER_X86_SPLAT(0x0fffffff));
    result.canonical = legacy ? legacy_canonical : apx ? rex2_canonical : canonical;
    result.prefix = legacy ? _mm512_andnot_epi32(override_bits, prefix) : prefix;
    // The displacement: after a base with an 8-bit one, the quotient (see compressible).
    const __m512i quotient = _mm512_ternarylogic_epi32(base_displacement, _mm512_rorv_epi32(_mm512_rol_epi32(displacement, 1), scale), displacement,
                                                       BUSTER_X86_A_SELECTS_B_OVER_C);
    result.displacement = _mm512_ternarylogic_epi32(fits8, quotient, displacement, BUSTER_X86_A_SELECTS_B_OVER_C);
    result.presence = presence;
    result.legacy_presence = legacy_presence;
    result.length = legacy ? legacy_length : length;
    return result;
}

// Loads the 16 bytes at `pointer` into 128-bit lane `lane` (a constant) of `vector`. The load spans the 64 bytes from
// pointer - 16 * lane, but masked-out elements are neither read nor able to fault.
BUSTER_INLINE __m512i x86_load_lane(__m512i vector, uint32_t lane, const uint8_t* pointer)
{
    return _mm512_mask_loadu_epi64(vector, (__mmask8)(3u << (2 * lane)), pointer - 16 * lane);
}

// vpermb's indices that put a byte of each of Phase II's lanes in instruction order: byte i takes byte 0 of lane
// 4(i % 4) + i / 4 (instruction i's length), and with the second, byte 1 (the high byte of instruction i's form, with the
// fixup flag in bit 7).
static const uint8_t x86_length_bytes[64] __attribute__((aligned(64))) = { 0, 16, 32, 48, 4, 20, 36, 52, 8, 24, 40, 56, 12, 28, 44, 60 };
static const uint8_t x86_form_high_bytes[64] __attribute__((aligned(64))) = { 1, 17, 33, 49, 5, 21, 37, 53, 9, 25, 41, 57, 13, 29, 45, 61 };
// vpshufb's indices that give bytes 8-15 of each 128-bit lane its byte 7, and the others zero.
static const uint8_t x86_first_qword_total[64] __attribute__((aligned(64))) = {
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 7, 7, 7, 7, 7, 7, 7, 7, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
};

// Measures one full group; a partial group is copied and zero-padded by x86_measure. compute_fields shares the
// encoder's length rules, while its unused canonical bytes, prefixes and displacement are eliminated. In 64-bit mode
// checks and extension affect those unused fields alone. No machine-code bytes, layout vectors or fixup ends are made.
BUSTER_INLINE uint32_t x86_measure_group(const X86Instruction* instructions, uint32_t base, uint32_t* offsets, __mmask16 lanes)
{
    const __m512i record0 = _mm512_loadu_si512(instructions);
    const __m512i record1 = _mm512_loadu_si512(instructions + 4);
    const __m512i record2 = _mm512_loadu_si512(instructions + 8);
    const __m512i record3 = _mm512_loadu_si512(instructions + 12);
    const __m512i low01 = _mm512_unpacklo_epi32(record0, record1);
    const __m512i low23 = _mm512_unpacklo_epi32(record2, record3);
    const __m512i high01 = _mm512_unpackhi_epi32(record0, record1);
    const __m512i high23 = _mm512_unpackhi_epi32(record2, record3);
    const __m512i displacement = _mm512_unpackhi_epi64(low01, low23);
    const __m512i operands = _mm512_unpacklo_epi64(high01, high23);
    // The forms are already in lane order in the extension's low 16 bits. Keep those bits and multiply by 16;
    // the gather's scale of 4 then addresses each 64-byte template's flags at byte 12. One dword-index gather
    // avoids Clang 23's reconstruction of scalar flag loads into pointer vectors and several partial gathers.
    const __m512i extension = _mm512_unpackhi_epi64(high01, high23);
    const __m512i form_indices = _mm512_srli_epi32(_mm512_slli_epi32(extension, 16), 12);
    const __m512i flags = _mm512_i32gather_epi32(form_indices, (const uint8_t*)x86_templates + 12, 4);
    const __m512i zero = _mm512_setzero_si512();
    const X86Fields fields = compute_fields(operands, displacement, zero, flags, zero, X86_ENCODER_MODE_64);
    const __m128i lengths = _mm512_castsi512_si128(_mm512_permutexvar_epi8(_mm512_load_si512(x86_length_bytes), fields.length));
    // Each group has at most 240 bytes: byte-wise sums cannot carry into their neighbours. Zero-padded records
    // contribute zero, so byte 15 is also the total of a partial group.
    const __m128i qword_sums = _mm_mullo_epi64(lengths, _mm_set1_epi64x(0x0101010101010101ll));
    const __m128i ends = _mm_add_epi8(qword_sums, _mm_shuffle_epi8(qword_sums, _mm_load_si128((const __m128i*)x86_first_qword_total)));
    const __m512i starts = _mm512_cvtepu8_epi32(_mm_sub_epi8(ends, lengths));
    _mm512_mask_storeu_epi32(offsets, lanes, _mm512_add_epi32(starts, _mm512_set1_epi32((int32_t)base)));
    return base + (uint32_t)_mm_extract_epi8(ends, BUSTER_X86_GROUP_INSTRUCTIONS - 1);
}

static int64_t x86_measure(const X86Instruction* instructions, uint64_t count, uint32_t* offsets)
{
    int64_t result;
    if (count > BUSTER_X86_ENCODER_MAX_INSTRUCTIONS)
    {
        result = X86_ENCODER_ERROR_TOO_MANY_INSTRUCTIONS;
    }
    else
    {
        uint32_t length = 0;
        uint64_t i = 0;
        for (; i + BUSTER_X86_GROUP_INSTRUCTIONS <= count; i += BUSTER_X86_GROUP_INSTRUCTIONS)
        {
            length = x86_measure_group(instructions + i, length, offsets + i, (__mmask16)0xffff);
        }
        if (i < count)
        {
            X86Instruction tail[BUSTER_X86_GROUP_INSTRUCTIONS] __attribute__((aligned(64))) = { 0 };
            memcpy(tail, instructions + i, (count - i) * sizeof(X86Instruction));
            const __mmask16 lanes = (__mmask16)((1u << (count - i)) - 1);
            length = x86_measure_group(tail, length, offsets + i, lanes);
        }
        result = length;
    }
    return result;
}

// Encodes the 4 * `quads` instructions at `instructions` (quads is 1, 2 or 4, and a constant), appending their
// machine code at `out`, and returns where it ends. If `offsets` isn't null, stores the offsets (from `output`) of
// the instructions of `offset_lanes` (a bit per instruction). If `fixup_ends` isn't null, appends the end offsets (from
// `output`) of the marked instructions at *fixup_ends and advances it past them. `encoder` (a constant) is the entry
// point's mode: 64-bit or 32-bit addresses, APX, or 32-bit or 16-bit mode. The instructions must be valid (see
// x86_encode): nothing here checks them.
//
// Phase II works on instruction 4q + j (quad q, slot j) in dword lane 4j + q, so that moving its fields between a
// quad's records and the group's lanes is a 4x4 transpose of dwords inside each 128-bit lane, both ways: unpacks,
// which on Zen 5 run on any of the four vector pipes (uops.info), where cross-lane permutes would take two of them
// from Phase III's shuffles and compressions.
BUSTER_INLINE uint8_t* encode_group(const X86Instruction* instructions, uint32_t quads, uint8_t* out, const uint8_t* output, uint32_t* offsets,
                                    uint32_t offset_lanes, uint32_t** fixup_ends, X86Gather gather, X86Store store, X86FixupStore fixup_store,
                                    X86EncoderMode encoder)
{
    const X86TemplateEntry* const templates = encoder == X86_ENCODER_MODE_ADDRESS32 ? x86_templates_address32
                                         : encoder == X86_ENCODER_MODE_APX     ? x86_templates_apx
                                         : encoder == X86_ENCODER_MODE_32      ? x86_templates_32
                                         : encoder == X86_ENCODER_MODE_16      ? x86_templates_16
                                                                               : x86_templates;
    // I. Records and templates (zero for the quads past `quads`).
    __m512i records[4];
    __m512i bytes[4];
    __m512i layouts[4];
    for (uint32_t q = 0; q < 4; q += 1)
    {
        records[q] = q < quads ? _mm512_loadu_si512(instructions + BUSTER_X86_QUAD_INSTRUCTIONS * q) : _mm512_setzero_si512();
        bytes[q] = _mm512_setzero_si512();
        layouts[q] = _mm512_setzero_si512();
    }

    // Each record's immediate, displacement (its second dword), operands (its third) and form, vvvv and decorations
    // (its fourth), in lane 4j + q: a 4x4 transpose of dwords inside each 128-bit lane.
    const __m512i low01 = _mm512_unpacklo_epi32(records[0], records[1]);
    const __m512i low23 = _mm512_unpacklo_epi32(records[2], records[3]);
    const __m512i high01 = _mm512_unpackhi_epi32(records[0], records[1]);
    const __m512i high23 = _mm512_unpackhi_epi32(records[2], records[3]);
    __m512i immediate = _mm512_unpacklo_epi64(low01, low23);
    // Opaque to clang, which otherwise sees that each of Phase III's unpacks of it takes half and merges one with
    // low23 into a vpermt2d: a µop on FP1 and FP2, with an index to load, on Phase III's path.
    __asm__("" : "+v"(immediate));
    const __m512i displacement = _mm512_unpackhi_epi64(low01, low23);
    const __m512i operands = _mm512_unpacklo_epi64(high01, high23);
    const __m512i extension = _mm512_unpackhi_epi64(high01, high23);

    // With X86_GATHER_INSERTS_FORMS_FROM_RECORDS, the forms as words in lane order, four to a general register: slot
    // j's for quads 0-3.
    uint64_t slot_forms[4] = { 0 };
    if (gather == X86_GATHER_INSERTS_FORMS_FROM_RECORDS)
    {
        const __m256i forms = _mm512_cvtepi32_epi16(extension);
        slot_forms[0] = (uint64_t)_mm256_extract_epi64(forms, 0);
        slot_forms[1] = (uint64_t)_mm256_extract_epi64(forms, 1);
        slot_forms[2] = (uint64_t)_mm256_extract_epi64(forms, 2);
        slot_forms[3] = (uint64_t)_mm256_extract_epi64(forms, 3);
    }

    for (uint32_t q = 0; q < quads; q += 1)
    {
        const X86Instruction* quad = instructions + BUSTER_X86_QUAD_INSTRUCTIONS * q;
        const uint8_t* t[BUSTER_X86_QUAD_INSTRUCTIONS];
        for (uint32_t j = 0; j < BUSTER_X86_QUAD_INSTRUCTIONS; j += 1)
        {
            t[j] = (const uint8_t*)&templates[gather == X86_GATHER_INSERTS_FORMS_FROM_RECORDS ? (uint16_t)(slot_forms[j] >> (16 * q)) : quad[j].form];
        }
        if (gather == X86_GATHER_MASKED_LOADS)
        {
            bytes[q] = x86_load_lane(x86_load_lane(x86_load_lane(_mm512_zextsi128_si512(_mm_load_si128((const __m128i*)t[0])), 1, t[1]), 2, t[2]), 3, t[3]);
            layouts[q] = x86_load_lane(x86_load_lane(x86_load_lane(_mm512_zextsi128_si512(_mm_load_si128((const __m128i*)(t[0] + 16))), 1, t[1] + 16), 2, t[2] + 16),
                                       3, t[3] + 16);
        }
        else
        {
            // [bytes 0, layout 0, bytes 2, layout 2] and, from the entries' second halves, [layout 1, bytes 1, layout 3,
            // bytes 3]: each slot's bytes in its own 128-bit lane, so that the bytes, whose flags come first, are one
            // blend (any vector pipe, a cycle), and only the layouts, which Phase III takes much later, a permute.
            const __m512i even = _mm512_inserti64x4(_mm512_castsi256_si512(_mm256_load_si256((const __m256i*)t[0])), _mm256_load_si256((const __m256i*)t[2]), 1);
            const __m512i odd = _mm512_inserti64x4(_mm512_castsi256_si512(_mm256_load_si256((const __m256i*)(t[1] + 32))), _mm256_load_si256((const __m256i*)(t[3] + 32)), 1);
            bytes[q] = _mm512_mask_blend_epi64(0xcc, even, odd);
            layouts[q] = _mm512_permutex2var_epi64(even, _mm512_setr_epi64(2, 3, 8, 9, 6, 7, 12, 13), odd);
        }
    }

    // Each template's checks and flags (its third and fourth dwords), in lane 4j + q.
    const __m512i template_high01 = _mm512_unpackhi_epi32(bytes[0], bytes[1]);
    const __m512i template_high23 = _mm512_unpackhi_epi32(bytes[2], bytes[3]);
    const __m512i checks = _mm512_unpacklo_epi64(template_high01, template_high23);
    const __m512i flags = _mm512_unpackhi_epi64(template_high01, template_high23);

    // II. The lanes of the quads past `quads` hold zeros, which are valid, as are a zeroed copy's padding.
    const X86Fields fields = compute_fields(operands, displacement, extension, flags, checks, encoder);

    // Where the group's machine code starts, for its offsets and fixup ends.
    uint8_t* const group_out = out;

    // III. Each quad's canonical slots: the immediate, the displacement, canonical bytes 8-11 and the prefix bytes
    // 12-15. And its presence bits, in every dword of each slot, for vpshufbitqmb to pick from by the layout: an
    // absent position picks bit 63, the second dword's bit 31, which no presence has.
    const __m512i immediate_displacement_low = _mm512_unpacklo_epi32(immediate, fields.displacement);
    const __m512i immediate_displacement_high = _mm512_unpackhi_epi32(immediate, fields.displacement);
    const __m512i canonical_low = _mm512_unpacklo_epi32(fields.canonical, fields.prefix);
    const __m512i canonical_high = _mm512_unpackhi_epi32(fields.canonical, fields.prefix);
    const __m512i canonicals[4] = {
        _mm512_unpacklo_epi64(immediate_displacement_low, canonical_low),
        _mm512_unpackhi_epi64(immediate_displacement_low, canonical_low),
        _mm512_unpacklo_epi64(immediate_displacement_high, canonical_high),
        _mm512_unpackhi_epi64(immediate_displacement_high, canonical_high),
    };
    const __m512i group_presence = x86_mode_is_legacy(encoder) ? fields.legacy_presence : fields.presence;
    const __m512i presences[4] = {
        _mm512_shuffle_epi32(group_presence, _MM_PERM_AAAA),
        _mm512_shuffle_epi32(group_presence, _MM_PERM_BBBB),
        _mm512_shuffle_epi32(group_presence, _MM_PERM_CCCC),
        _mm512_shuffle_epi32(group_presence, _MM_PERM_DDDD),
    };
    for (uint32_t q = 0; q < quads; q += 1)
    {
        const __m512i canonical = canonicals[q];
        const __m512i presence = presences[q];
        const __m512i data = _mm512_ternarylogic_epi64(_mm512_shuffle_epi8(canonical, layouts[q]), bytes[q], _mm512_setr_epi64(-1, 0, -1, 0, -1, 0, -1, 0),
                                                       BUSTER_X86_A_XOR_B_AND_C);
        const __mmask64 present = _mm512_bitshuffle_epi64_mask(presence, layouts[q]);
        const uint32_t length = (uint32_t)__builtin_popcountll(present);
        if (store == X86_STORE_COMPRESS)
        {
            _mm512_mask_compressstoreu_epi8(out, present, data);
        }
        else if (store == X86_STORE_FULL)
        {
            _mm512_storeu_si512(out, _mm512_maskz_compress_epi8(present, data));
        }
        else
        {
            _mm512_mask_storeu_epi8(out, _cvtu64_mask64(_bzhi_u64(~0ull, length)), _mm512_maskz_compress_epi8(present, data));
        }
        out += length;
    }

    if (offsets != 0 || fixup_ends != 0)
    {
        // After Phase III, so that these wait for the lengths behind its operations rather than ahead of them in the FP
        // scheduler. Each instruction's end in the group, the running sum of the lengths (at most 240): as bytes in
        // instruction order (a vpermb), a multiplication by 0x0101010101010101 sums each qword's bytes up to each one
        // (eight lengths of at most 15, so nothing carries), and a vpshufb adds the first qword's total to the second's.
        // About 12 cycles on Zen 5, where the shifts and adds of dwords that this replaced took about 30 and held up the
        // retirement of the group's Phase III: the reorder buffer was full 17% of the time with offsets and 27% with
        // fixup ends.
        const __m512i lengths = _mm512_permutexvar_epi8(_mm512_load_si512(x86_length_bytes), fields.length);
        const __m512i qword_sums = _mm512_mullo_epi64(lengths, _mm512_set1_epi64(0x0101010101010101ll));
        const __m512i ends = _mm512_add_epi8(qword_sums, _mm512_shuffle_epi8(qword_sums, _mm512_load_si512(x86_first_qword_total)));
        // The group's offset in the output, broadcast from memory: a load, where a broadcast from a general register
        // takes FP1 or FP2 too.
        uint32_t group_offset = (uint32_t)(group_out - output);
        __asm__("" : "+m"(group_offset));
        const __m512i group_start = BUSTER_X86_SPLAT(group_offset);
        if (offsets != 0)
        {
            const __m512i starts = _mm512_cvtepu8_epi32(_mm512_castsi512_si128(_mm512_sub_epi8(ends, lengths)));
            _mm512_mask_storeu_epi32(offsets, (__mmask16)offset_lanes, _mm512_add_epi32(starts, group_start));
        }
        if (fixup_ends != 0)
        {
            // The marked instructions' ends, compressed, by the fixup flags of the forms, which the records had long
            // before the lengths. The zeroed copy's padding isn't marked.
            const __mmask16 marked = (__mmask16)_mm512_movepi8_mask(_mm512_permutexvar_epi8(_mm512_load_si512(x86_form_high_bytes), extension));
            const __m512i marked_ends = _mm512_add_epi32(_mm512_cvtepu8_epi32(_mm512_castsi512_si128(ends)), group_start);
            if (fixup_store == X86_FIXUP_STORE_COMPRESS)
            {
                _mm512_mask_compressstoreu_epi32(*fixup_ends, marked, marked_ends);
            }
            else
            {
                _mm512_storeu_si512(*fixup_ends, _mm512_maskz_compress_epi32(marked, marked_ends));
            }
            *fixup_ends += __builtin_popcount(marked);
        }
    }
    return out;
}

#undef BUSTER_X86_SPLAT

// The vectorized encoder's loop: `groups` groups of 4 * `quads` instructions per iteration (all constants), then the
// whole groups left, then the last partial group from a zeroed copy, so that no load reads past the instructions.
// Returns the number of bytes. The instructions must be valid (see x86_encode): then it writes nothing past
// output + BUSTER_X86_ENCODER_CAPACITY(count), but may write past the instructions' bytes.
//
// Why a full 64-byte store stays inside that capacity: a quad's store starts after the bytes of the instructions
// before it, at most 15 for each of the first `count` (the zeroed copy's padding adds none), so it ends by
// 15 * count + 64.
BUSTER_INLINE int64_t encode_batches(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint32_t* offsets, uint32_t* fixup_ends,
                                     uint32_t quads, uint32_t groups, X86Gather gather, X86Store store, X86FixupStore fixup_store, X86EncoderMode encoder)
{
    const uint64_t group_instructions = BUSTER_X86_QUAD_INSTRUCTIONS * quads;
    const uint64_t iteration_instructions = group_instructions * groups;
    const uint32_t group_lanes = (uint32_t)((1ull << group_instructions) - 1);
    uint8_t* out = output;
    // Where the next fixup end goes, through a pointer that is null (a constant) in the loops without fixup ends.
    uint32_t* fixup_cursor = fixup_ends;
    uint32_t** fixups = fixup_ends != 0 ? &fixup_cursor : 0;
    uint64_t i = 0;
    while (i + iteration_instructions <= count)
    {
        // clang keeps this loop rolled: a group's code takes most of the vector registers, and two groups' together
        // spill (unrolled, it's 14% slower on Zen 5).
        for (uint32_t g = 0; g < groups; g += 1)
        {
            out = encode_group(instructions + i, quads, out, output, offsets != 0 ? offsets + i : 0, group_lanes, fixups, gather, store, fixup_store, encoder);
            i += group_instructions;
        }
    }
    while (i + group_instructions <= count)
    {
        out = encode_group(instructions + i, quads, out, output, offsets != 0 ? offsets + i : 0, group_lanes, fixups, gather, store, fixup_store, encoder);
        i += group_instructions;
    }
    if (i < count)
    {
        X86Instruction tail[BUSTER_X86_GROUP_INSTRUCTIONS] __attribute__((aligned(64))) = { 0 };
        memcpy(tail, instructions + i, (count - i) * sizeof(X86Instruction));
        out = encode_group(tail, quads, out, output, offsets != 0 ? offsets + i : 0, (uint32_t)((1ull << (count - i)) - 1), fixups, gather, store, fixup_store,
                           encoder);
    }
    return (int64_t)(out - output);
}

// The instantiations, by instructions per iteration, and for the benchmark, with the other gathers and stores. All of
// them need room for BUSTER_X86_ENCODER_CAPACITY(count) bytes, and those with full fixup stores, room for
// BUSTER_X86_ENCODER_FIXUP_CAPACITY(marked) fixup ends. Each has a loop for each combination of offsets and fixup ends,
// so that none tests for them per group. x86_encode_16_address32, x86_encode_16_apx, x86_encode_16_mode32 and
// x86_encode_16_mode16 are x86_encode_16 for 32-bit addresses, for APX, and for 32-bit and 16-bit modes, and the
// x86_encode_64_ ones the same for x86_encode_64. The tests and the benchmark take each its own few of them.
#define BUSTER_X86_ENCODE_VARIANT(name, quads, groups, gather, store, fixup_store, encoder) \
    __attribute__((noinline, unused)) static int64_t name(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint32_t* offsets, uint32_t* fixup_ends) \
    { \
        int64_t result; \
        if (fixup_ends == 0) \
        { \
            result = offsets != 0 ? encode_batches(instructions, count, output, offsets, 0, quads, groups, gather, store, fixup_store, encoder) \
                                  : encode_batches(instructions, count, output, 0, 0, quads, groups, gather, store, fixup_store, encoder); \
        } \
        else \
        { \
            result = offsets != 0 ? encode_batches(instructions, count, output, offsets, fixup_ends, quads, groups, gather, store, fixup_store, encoder) \
                                  : encode_batches(instructions, count, output, 0, fixup_ends, quads, groups, gather, store, fixup_store, encoder); \
        } \
        return result; \
    }
BUSTER_X86_ENCODE_VARIANT(x86_encode_4, 1, 1, X86_GATHER_INSERTS, X86_STORE_FULL, X86_FIXUP_STORE_FULL, X86_ENCODER_MODE_64)
BUSTER_X86_ENCODE_VARIANT(x86_encode_8, 2, 1, X86_GATHER_INSERTS, X86_STORE_FULL, X86_FIXUP_STORE_FULL, X86_ENCODER_MODE_64)
BUSTER_X86_ENCODE_VARIANT(x86_encode_16, 4, 1, X86_GATHER_INSERTS, X86_STORE_FULL, X86_FIXUP_STORE_FULL, X86_ENCODER_MODE_64)
BUSTER_X86_ENCODE_VARIANT(x86_encode_32, 4, 2, X86_GATHER_INSERTS, X86_STORE_FULL, X86_FIXUP_STORE_FULL, X86_ENCODER_MODE_64)
BUSTER_X86_ENCODE_VARIANT(x86_encode_64, 4, 4, X86_GATHER_INSERTS, X86_STORE_FULL, X86_FIXUP_STORE_FULL, X86_ENCODER_MODE_64)
BUSTER_X86_ENCODE_VARIANT(x86_encode_16_forms_from_records, 4, 1, X86_GATHER_INSERTS_FORMS_FROM_RECORDS, X86_STORE_FULL, X86_FIXUP_STORE_FULL, X86_ENCODER_MODE_64)
BUSTER_X86_ENCODE_VARIANT(x86_encode_16_masked_loads, 4, 1, X86_GATHER_MASKED_LOADS, X86_STORE_FULL, X86_FIXUP_STORE_FULL, X86_ENCODER_MODE_64)
BUSTER_X86_ENCODE_VARIANT(x86_encode_16_masked_stores, 4, 1, X86_GATHER_INSERTS, X86_STORE_MASKED, X86_FIXUP_STORE_FULL, X86_ENCODER_MODE_64)
BUSTER_X86_ENCODE_VARIANT(x86_encode_16_compress_stores, 4, 1, X86_GATHER_INSERTS, X86_STORE_COMPRESS, X86_FIXUP_STORE_FULL, X86_ENCODER_MODE_64)
BUSTER_X86_ENCODE_VARIANT(x86_encode_64_fixup_compress_stores, 4, 4, X86_GATHER_INSERTS, X86_STORE_FULL, X86_FIXUP_STORE_COMPRESS, X86_ENCODER_MODE_64)
BUSTER_X86_ENCODE_VARIANT(x86_encode_64_address32, 4, 4, X86_GATHER_INSERTS, X86_STORE_FULL, X86_FIXUP_STORE_FULL, X86_ENCODER_MODE_ADDRESS32)
BUSTER_X86_ENCODE_VARIANT(x86_encode_64_apx, 4, 4, X86_GATHER_INSERTS, X86_STORE_FULL, X86_FIXUP_STORE_FULL, X86_ENCODER_MODE_APX)
BUSTER_X86_ENCODE_VARIANT(x86_encode_64_mode32, 4, 4, X86_GATHER_INSERTS, X86_STORE_FULL, X86_FIXUP_STORE_FULL, X86_ENCODER_MODE_32)
BUSTER_X86_ENCODE_VARIANT(x86_encode_64_mode16, 4, 4, X86_GATHER_INSERTS, X86_STORE_FULL, X86_FIXUP_STORE_FULL, X86_ENCODER_MODE_16)
BUSTER_X86_ENCODE_VARIANT(x86_encode_16_address32, 4, 1, X86_GATHER_INSERTS, X86_STORE_FULL, X86_FIXUP_STORE_FULL, X86_ENCODER_MODE_ADDRESS32)
BUSTER_X86_ENCODE_VARIANT(x86_encode_16_apx, 4, 1, X86_GATHER_INSERTS, X86_STORE_FULL, X86_FIXUP_STORE_FULL, X86_ENCODER_MODE_APX)
BUSTER_X86_ENCODE_VARIANT(x86_encode_16_mode32, 4, 1, X86_GATHER_INSERTS, X86_STORE_FULL, X86_FIXUP_STORE_FULL, X86_ENCODER_MODE_32)
BUSTER_X86_ENCODE_VARIANT(x86_encode_16_mode16, 4, 1, X86_GATHER_INSERTS, X86_STORE_FULL, X86_FIXUP_STORE_FULL, X86_ENCODER_MODE_16)
#undef BUSTER_X86_ENCODE_VARIANT

// The production entry point: 16 instructions per iteration, which the Zen 5 benchmark found the fastest after the
// scalar code a compiler runs before it, 1% to 6% faster than 64 per iteration there, and at most 7% slower in loops
// that run for longer than a compiler's encoding does (docs/x86-64-encoder.md has the numbers). The other entry
// points take the same width.
static int64_t x86_encode(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity, uint32_t* offsets, uint32_t* fixup_ends)
{
    int64_t result;
    if (count > BUSTER_X86_ENCODER_MAX_INSTRUCTIONS)
    {
        result = X86_ENCODER_ERROR_TOO_MANY_INSTRUCTIONS;
    }
    else if (capacity < BUSTER_X86_ENCODER_CAPACITY(count))
    {
        result = X86_ENCODER_ERROR_OUTPUT_TOO_SMALL;
    }
    else
    {
        result = x86_encode_16(instructions, count, output, offsets, fixup_ends);
    }
    return result;
}

static int64_t x86_encode_address32(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity, uint32_t* offsets,
                                    uint32_t* fixup_ends)
{
    int64_t result;
    if (count > BUSTER_X86_ENCODER_MAX_INSTRUCTIONS)
    {
        result = X86_ENCODER_ERROR_TOO_MANY_INSTRUCTIONS;
    }
    else if (capacity < BUSTER_X86_ENCODER_CAPACITY(count))
    {
        result = X86_ENCODER_ERROR_OUTPUT_TOO_SMALL;
    }
    else
    {
        result = x86_encode_16_address32(instructions, count, output, offsets, fixup_ends);
    }
    return result;
}

static int64_t x86_encode_apx(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity, uint32_t* offsets, uint32_t* fixup_ends)
{
    int64_t result;
    if (count > BUSTER_X86_ENCODER_MAX_INSTRUCTIONS)
    {
        result = X86_ENCODER_ERROR_TOO_MANY_INSTRUCTIONS;
    }
    else if (capacity < BUSTER_X86_ENCODER_CAPACITY(count))
    {
        result = X86_ENCODER_ERROR_OUTPUT_TOO_SMALL;
    }
    else
    {
        result = x86_encode_16_apx(instructions, count, output, offsets, fixup_ends);
    }
    return result;
}

static int64_t x86_encode_mode32(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity, uint32_t* offsets, uint32_t* fixup_ends)
{
    int64_t result;
    if (count > BUSTER_X86_ENCODER_MAX_INSTRUCTIONS)
    {
        result = X86_ENCODER_ERROR_TOO_MANY_INSTRUCTIONS;
    }
    else if (capacity < BUSTER_X86_ENCODER_CAPACITY(count))
    {
        result = X86_ENCODER_ERROR_OUTPUT_TOO_SMALL;
    }
    else
    {
        result = x86_encode_16_mode32(instructions, count, output, offsets, fixup_ends);
    }
    return result;
}

static int64_t x86_encode_mode16(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity, uint32_t* offsets, uint32_t* fixup_ends)
{
    int64_t result;
    if (count > BUSTER_X86_ENCODER_MAX_INSTRUCTIONS)
    {
        result = X86_ENCODER_ERROR_TOO_MANY_INSTRUCTIONS;
    }
    else if (capacity < BUSTER_X86_ENCODER_CAPACITY(count))
    {
        result = X86_ENCODER_ERROR_OUTPUT_TOO_SMALL;
    }
    else
    {
        result = x86_encode_16_mode16(instructions, count, output, offsets, fixup_ends);
    }
    return result;
}
