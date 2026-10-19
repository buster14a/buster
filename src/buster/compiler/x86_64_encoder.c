// The x86-64 encoder: turns a section's selected machine instructions into machine code. An instruction
// (X86Instruction, 16 bytes) is a form (X86Form: the opcode, prefixes, operand roles and field widths) and its
// operands in their encoding roles: the ModRM.reg and ModRM.rm registers, the memory operand's base, index, scale and
// displacement, the VEX.vvvv register, EVEX's opmask, zeroing and rounding, and the immediate. Forms are legacy (with
// REX; x87 among them), VEX (AVX, AVX2, FMA, BMI and the mask register instructions) or EVEX (AVX-512), and each one
// with a memory operand also comes with an FS or GS segment override (thread-local storage). Files that use it include
// it directly; every function in it is static.
//
// Entry points: x86_encoder_initialize (builds the templates; call it once before encoding), x86_encode (the
// vectorized encoder), x86_encode_scalar (a plain reference encoder, for testing), x86_validate and
// x86_form_descriptions (the form table, for tests and diagnostics). The output needs room for
// BUSTER_X86_ENCODER_CAPACITY(count) bytes; x86_encode writes nothing past that, but may write past the instructions'
// bytes (its 64-byte stores).
//
// How x86_encode works, a group of BUSTER_X86_GROUP_INSTRUCTIONS (16) instructions at a time:
//   I.   Load. Four instructions fill a ZMM register. Each one's form has a 32-byte template (X86Template, built from
//        the form table by x86_encoder_initialize): the bytes the form always has (prefixes, REX.W or the VEX or EVEX
//        prefix with its inverted fields as ones, the opcode, a ModRM /digit) at their positions in a 16-byte slot,
//        its checks and flags, and the slot's layout, which says for each position which byte of the instruction's
//        canonical slot goes there.
//   II.  Compute, across the group in the dword lanes of ZMM registers: ModRM, SIB, REX.RXB, the displacement size
//        (EVEX's in units of N), what the VEX or EVEX prefix takes from the operands (VEX2 or VEX3 among it), which
//        canonical bytes the instruction has, its length and whether its operands are ones the encoding can
//        represent. No branches: the addressing mode's decisions are table lookups (vpermd), the register fields move
//        into place with vpmultishiftqb, and the rest are masks.
//   III. Assemble. An instruction's canonical slot is its immediate and displacement followed by the bytes computed
//        in II. One byte shuffle per 4 instructions moves every canonical byte to its position in the instruction and
//        a vpternlogd XORs the template's bytes in. Compressing those 4 slots by the bytes the instructions have
//        (vpcompressb) leaves the 4 instructions' machine code, one after the other, which a 64-byte store appends to
//        the output.
// The form decides everything the bytes' positions depend on, so instructions with different forms (legacy, VEX or
// EVEX) take the same path, and a form with no bytes (X86_FORM_NONE, which zeroed records have) is how a group is
// padded. The choices are Zen 5's: docs/x86-64-encoder.md has the measurements behind them (encode_group's and
// compute_fields's comments say which).
//
// Layout, in this order:
//   - API: registers and addressing modes (BUSTER_X86_MODE_*), X86Instruction and its decorations
//     (BUSTER_X86_DECORATION_*), X86EncoderError, the operand, encoding, family and syntax constants of the form
//     table, BUSTER_X86_VECTOR_FORMS (the VEX and EVEX forms) and BUSTER_X86_FORMS (the form table), X86Form, the
//     segment overrides (BUSTER_X86_FORM_FS and BUSTER_X86_FORM_GS) and the entry points' prototypes.
//   - Templates: the canonical slot (BUSTER_X86_CANONICAL_*), the layout, checks and flags constants, X86Template,
//     build_template and x86_encoder_initialize.
//   - Validation: x86_instruction_is_valid and x86_validate.
//   - The scalar encoder: encode_one and x86_encode_scalar.
//   - The vectorized encoder: X86Gather and X86Store (the variants the benchmark compares), X86Fields, the
//     addressing mode tables (x86_mode_fields, x86_mode_constants, x86_mode_presence), compute_fields (II),
//     encode_group (I-III), encode_batches (the loop, with the instructions per iteration as a parameter), the
//     x86_encode_N instantiations and x86_encode.

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

// A register operand is the register's number (general purpose: RAX = 0 ... R15 = 15; vector: XMM0 = 0 ... XMM15 = 15,
// or XMM31 = 31 in an EVEX form, and the same for YMM and ZMM; opmask: K0 = 0 ... K7 = 7, and a pair of them (an even
// one and the next) as the even one's number; x87: ST(0) = 0 ... ST(7) = 7) or, for AH, CH, DH and BH, 4 to 7 with
// BUSTER_X86_REGISTER_HIGH_BYTE. With a byte form, 4 to 7 alone are SPL, BPL, SIL and DIL, which need a REX
// prefix; the high byte registers can't have one.
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

// One instruction: 16 bytes, so a ZMM register holds 4. Operands the form doesn't have must be zero.
typedef struct X86Instruction X86Instruction;
struct X86Instruction
{
    // The immediate, in as many low bytes as the form's immediate has (ENTER's two are imm16 | imm8 << 16). A
    // 64-bit immediate or address (MOV r64, imm64 and MOVABS) is immediate | displacement << 32.
    int32_t immediate;
    // The memory operand's displacement, or a relative branch's displacement from the end of the instruction.
    int32_t displacement;
    // The register in ModRM.reg.
    uint8_t reg;
    // The register in ModRM.rm (or added to the opcode, for a +r form), or a memory operand's base.
    uint8_t rm;
    // A memory operand's index register.
    uint8_t index;
    // BUSTER_X86_REGISTER_DIRECT or a BUSTER_X86_MEMORY_* addressing mode.
    uint8_t mode;
    // An X86Form, plus BUSTER_X86_FORM_FS or BUSTER_X86_FORM_GS for a segment override.
    uint16_t form;
    // The register in VEX.vvvv (EVEX.V'vvvv): a VEX or EVEX form's operand that isn't in ModRM.
    uint8_t vvvv;
    // An EVEX form's decorations: BUSTER_X86_DECORATION_*.
    uint8_t decorations;
};
_Static_assert(sizeof(X86Instruction) == 16, "four instructions fill a ZMM register");

// X86Instruction.decorations: the opmask register (K1 to K7, or 0 for none) and zeroing (with an opmask, instead of
// merging), for the EVEX forms that can have them, and the rounding mode of a form with embedded rounding (its name ends
// in _ER). The bits are where EVEX's last byte has them; bits 3 and 4 must be zero.
#define BUSTER_X86_DECORATION_MASK 0x07
#define BUSTER_X86_DECORATION_ROUNDING 0x60
#define BUSTER_X86_DECORATION_ZEROING 0x80
#define BUSTER_X86_ROUND_NEAREST 0x00
#define BUSTER_X86_ROUND_DOWN 0x20
#define BUSTER_X86_ROUND_UP 0x40
#define BUSTER_X86_ROUND_ZERO 0x60

typedef enum X86EncoderError
{
    // An instruction's operands can't be encoded (x86_validate finds which).
    X86_ENCODER_ERROR_INVALID_INSTRUCTION = -1,
    X86_ENCODER_ERROR_OUTPUT_TOO_SMALL = -2,
    X86_ENCODER_ERROR_TOO_MANY_INSTRUCTIONS = -3,
} X86EncoderError;

// The form table's operand roles: a register class, and for ModRM.rm whether it can be a register, memory, or both
// (as Intel's opcode maps write them: G a general register in ModRM.reg, E a general register or memory in
// ModRM.rm, V an XMM register, XM an XMM register or memory, M only memory; and Y, YM, Z and ZM for YMM and ZMM, K
// for the opmask registers, ST for the x87 registers). For a +r form, the register is the rm operand. An opmask or
// memory operand would need checks that depend on the mode (k8 to k15 don't exist, but a base can be R8 to R15), so
// those are two forms.
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
#define BUSTER_X86_CLASS_OPMASK_PAIR 10
#define BUSTER_X86_OPERAND_REGISTER 0x10
#define BUSTER_X86_OPERAND_MEMORY 0x20
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
#define BUSTER_X86_OPERAND_KP (BUSTER_X86_CLASS_OPMASK_PAIR | BUSTER_X86_OPERAND_REGISTER)
#define BUSTER_X86_OPERAND_M BUSTER_X86_OPERAND_MEMORY

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
// decorations, and only register operands) or suppressed exceptions (the same, without the rounding mode).
#define BUSTER_X86_EVEX_NONE 0x00
#define BUSTER_X86_EVEX_MASK 0x01
#define BUSTER_X86_EVEX_ZEROING 0x02
#define BUSTER_X86_EVEX_BROADCAST 0x04
#define BUSTER_X86_EVEX_ROUNDING 0x08
#define BUSTER_X86_EVEX_SAE 0x10
#define BUSTER_X86_EVEX_K BUSTER_X86_EVEX_MASK
#define BUSTER_X86_EVEX_KZ (BUSTER_X86_EVEX_MASK | BUSTER_X86_EVEX_ZEROING)
#define BUSTER_X86_EVEX_K_BCST (BUSTER_X86_EVEX_K | BUSTER_X86_EVEX_BROADCAST)
#define BUSTER_X86_EVEX_KZ_BCST (BUSTER_X86_EVEX_KZ | BUSTER_X86_EVEX_BROADCAST)
#define BUSTER_X86_EVEX_ER BUSTER_X86_EVEX_ROUNDING
#define BUSTER_X86_EVEX_KZ_ER (BUSTER_X86_EVEX_KZ | BUSTER_X86_EVEX_ROUNDING)
#define BUSTER_X86_EVEX_K_SAE (BUSTER_X86_EVEX_K | BUSTER_X86_EVEX_SAE)
#define BUSTER_X86_EVEX_KZ_SAE (BUSTER_X86_EVEX_KZ | BUSTER_X86_EVEX_SAE)
#define BUSTER_X86_EVEX_B (BUSTER_X86_EVEX_BROADCAST | BUSTER_X86_EVEX_ROUNDING | BUSTER_X86_EVEX_SAE)

// The order of a form's operands in Intel syntax, with its implicit ones (for printing and tests; the encoding
// doesn't depend on it). ACC is AL, AX, EAX or RAX; MOFFS is a 64-bit absolute address; VVVV is the VEX.vvvv register;
// ST is the x87 register ST(0), where the rm operand is ST(i); XMM0 is the variable blends' implicit mask register;
// IMM8_IMM8 is two 8-bit immediates (SSE4a's extrq and insertq), the immediate's low byte first; and RM_VVVV_REG is a
// masked store's memory, mask and source.
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
} X86Syntax;

// The form table. Each row is X(name, mnemonic, prefixes, opcode bytes, opcode, encoding, immediate bytes, relative
// bytes, REX.W, reg operand, rm operand, operand bytes, syntax):
//   - prefixes: up to two legacy prefixes, the first in the low byte (0x66, 0xF266 for 66 F2), or 0;
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
    X(LOCK_##N##_M8_R8, "lock " m, 0xf0, 2, opcode8, R, 0, 0, 0, GB, M, 1, RM_REG) \
    X(LOCK_##N##_M16_R16, "lock " m, 0xf066, 2, opcode, R, 0, 0, 0, GW, M, 2, RM_REG) \
    X(LOCK_##N##_M32_R32, "lock " m, 0xf0, 2, opcode, R, 0, 0, 0, GD, M, 4, RM_REG) \
    X(LOCK_##N##_M64_R64, "lock " m, 0xf0, 2, opcode, R, 0, 0, 1, GQ, M, 8, RM_REG)

// The arithmetic instructions with a LOCK prefix: a memory destination, and a register or immediate source.
#define BUSTER_X86_LOCK_ARITHMETIC_FORMS(X, N, m, K) \
    X(LOCK_##N##_M8_R8, "lock " m, 0xf0, 1, 0x00 + 8 * K, R, 0, 0, 0, GB, M, 1, RM_REG) \
    X(LOCK_##N##_M16_R16, "lock " m, 0xf066, 1, 0x01 + 8 * K, R, 0, 0, 0, GW, M, 2, RM_REG) \
    X(LOCK_##N##_M32_R32, "lock " m, 0xf0, 1, 0x01 + 8 * K, R, 0, 0, 0, GD, M, 4, RM_REG) \
    X(LOCK_##N##_M64_R64, "lock " m, 0xf0, 1, 0x01 + 8 * K, R, 0, 0, 1, GQ, M, 8, RM_REG) \
    X(LOCK_##N##_M8_IMM8, "lock " m, 0xf0, 1, 0x80, D##K, 1, 0, 0, NONE, M, 1, RM_IMM) \
    X(LOCK_##N##_M16_IMM16, "lock " m, 0xf066, 1, 0x81, D##K, 2, 0, 0, NONE, M, 2, RM_IMM) \
    X(LOCK_##N##_M32_IMM32, "lock " m, 0xf0, 1, 0x81, D##K, 4, 0, 0, NONE, M, 4, RM_IMM) \
    X(LOCK_##N##_M64_IMM32, "lock " m, 0xf0, 1, 0x81, D##K, 4, 0, 1, NONE, M, 8, RM_IMM) \
    X(LOCK_##N##_M16_IMM8, "lock " m, 0xf066, 1, 0x83, D##K, 1, 0, 0, NONE, M, 2, RM_IMM) \
    X(LOCK_##N##_M32_IMM8, "lock " m, 0xf0, 1, 0x83, D##K, 1, 0, 0, NONE, M, 4, RM_IMM) \
    X(LOCK_##N##_M64_IMM8, "lock " m, 0xf0, 1, 0x83, D##K, 1, 0, 1, NONE, M, 8, RM_IMM)

#define BUSTER_X86_LOCK_UNARY_FORMS(X, N, m, opcode8, opcode, K) \
    X(LOCK_##N##_M8, "lock " m, 0xf0, 1, opcode8, D##K, 0, 0, 0, NONE, M, 1, RM) \
    X(LOCK_##N##_M16, "lock " m, 0xf066, 1, opcode, D##K, 0, 0, 0, NONE, M, 2, RM) \
    X(LOCK_##N##_M32, "lock " m, 0xf0, 1, opcode, D##K, 0, 0, 0, NONE, M, 4, RM) \
    X(LOCK_##N##_M64, "lock " m, 0xf0, 1, opcode, D##K, 0, 0, 1, NONE, M, 8, RM)

#define BUSTER_X86_LOCK_BIT_TEST_FORMS(X, N, m, opcode, K) \
    X(LOCK_##N##_M16_R16, "lock " m, 0xf066, 2, 0x0f00 + opcode, R, 0, 0, 0, GW, M, 2, RM_REG) \
    X(LOCK_##N##_M32_R32, "lock " m, 0xf0, 2, 0x0f00 + opcode, R, 0, 0, 0, GD, M, 4, RM_REG) \
    X(LOCK_##N##_M64_R64, "lock " m, 0xf0, 2, 0x0f00 + opcode, R, 0, 0, 1, GQ, M, 8, RM_REG) \
    X(LOCK_##N##_M16_IMM8, "lock " m, 0xf066, 2, 0x0fba, D##K, 1, 0, 0, NONE, M, 2, RM_IMM) \
    X(LOCK_##N##_M32_IMM8, "lock " m, 0xf0, 2, 0x0fba, D##K, 1, 0, 0, NONE, M, 4, RM_IMM) \
    X(LOCK_##N##_M64_IMM8, "lock " m, 0xf0, 2, 0x0fba, D##K, 1, 0, 1, NONE, M, 8, RM_IMM)

// A string instruction at each operand size (its implicit operands: AL to RAX and [RSI] or [RDI]), with `prefixes`
// (none, REP, REPE or REPNE): b (opcode8), w (66, opcode), d (opcode) and q (REX.W, opcode).
#define BUSTER_X86_STRING_FORMS(X, N, m, prefixes, opcode8, opcode) \
    X(N##_B, m "b", prefixes, 1, opcode8, NO, 0, 0, 0, NONE, NONE, 1, NONE) \
    X(N##_W, m "w", (prefixes) << 8 | 0x66, 1, opcode, NO, 0, 0, 0, NONE, NONE, 2, NONE) \
    X(N##_D, m "d", prefixes, 1, opcode, NO, 0, 0, 0, NONE, NONE, 4, NONE) \
    X(N##_Q, m "q", prefixes, 1, opcode, NO, 0, 0, 1, NONE, NONE, 8, NONE)

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
//     EVEX.pp), or 0; opcode: as for the legacy forms, with its escape bytes (0F, 0F 38 or 0F 3A: the opcode map);
//   - W: VEX.W or EVEX.W (0 where it's ignored, as GNU as encodes those); L: VEX.L or EVEX.L'L, 0 (128-bit, scalar
//     or no vector length), 1 (256-bit) or 2 (512-bit);
//   - vvvv operand: the register in VEX.vvvv or EVEX.V'vvvv, or NONE;
//   - operand bytes: the memory operand's size, for printing; for a broadcast, the vector's its element fills (the
//     element's size is the disp8 scale);
//   - disp8 scale: what an 8-bit displacement counts in, EVEX's N (usually the memory operand's size, but the
//     element's for a broadcast, compress or expand); 1 for VEX;
//   - decorations: what an EVEX form's can be (BUSTER_X86_EVEX_*), or NONE.
// Broadcasts, embedded rounding and suppressed exceptions are forms of their own (_M32BCST, _ER, _SAE): they set
// EVEX.b and choose N, so the template has them.
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

// An XMM register's low or high quadword from memory, with the other from VEX.vvvv, and to memory (the next opcode).
#define BUSTER_X86_AVX_HALF_MOVE_FORMS(XV, N, m, prefixes, load) \
    XV(N##_X_X_M64, m, VEX, prefixes, 2, load, R, 0, 0, 0, V, V, M, 8, 1, NONE, REG_VVVV_RM) \
    XV(N##_M64_X, m, VEX, prefixes, 2, load + 1, R, 0, 0, 0, V, NONE, M, 8, 1, NONE, RM_REG)

// Non-temporal stores of 128 and 256 bits.
#define BUSTER_X86_AVX_STREAM_FORMS(XV, N, m, prefixes, opcode) \
    XV(N##_M128_X, m, VEX, prefixes, 2, opcode, R, 0, 0, 0, V, NONE, M, 16, 1, NONE, RM_REG) \
    XV(N##_M256_Y, m, VEX, prefixes, 2, opcode, R, 0, 0, 1, Y, NONE, M, 32, 1, NONE, RM_REG)

// CMPccXADD: compare a 32- or 64-bit memory operand with reg, add vvvv to it if the condition (K) holds, and return
// its old value in reg (VEX.128.66.0F38 E0+K).
#define BUSTER_X86_CMPCCXADD_FORMS(XV, CC, cc, K) \
    XV(CMP##CC##XADD_M32_R32_R32, "cmp" cc "xadd", VEX, 0x66, 3, 0x0f38e0 + K, R, 0, 0, 0, GD, GD, M, 4, 1, NONE, RM_REG_VVVV) \
    XV(CMP##CC##XADD_M64_R64_R64, "cmp" cc "xadd", VEX, 0x66, 3, 0x0f38e0 + K, R, 0, 1, 0, GQ, GQ, M, 8, 1, NONE, RM_REG_VVVV)

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

// Conversions of 32-bit elements to elements twice as wide, from half the vector: a YMM register, a 256-bit memory
// operand or a broadcast of 8 elements.
#define BUSTER_X86_AVX512_WIDENING_FORMS(XV, N, m, prefixes, opcode) \
    XV(N##_Z_YM256, m, EVEX, prefixes, 2, opcode, R, 0, 0, 2, Z, NONE, YM, 32, 32, KZ, REG_RM) \
    XV(N##_Z_M32BCST, m, EVEX, prefixes, 2, opcode, R, 0, 0, 2, Z, NONE, M, 32, 4, KZ_BCST, REG_RM)

#define BUSTER_X86_AVX512_WIDENING_ROUNDING_FORMS(XV, N, m, prefixes, opcode) \
    BUSTER_X86_AVX512_WIDENING_FORMS(XV, N, m, prefixes, opcode) \
    XV(N##_Z_Y_ER, m, EVEX, prefixes, 2, opcode, R, 0, 0, 2, Z, NONE, Y, 32, 32, KZ_ER, REG_RM)

#define BUSTER_X86_AVX512_WIDENING_SAE_FORMS(XV, N, m, prefixes, opcode) \
    BUSTER_X86_AVX512_WIDENING_FORMS(XV, N, m, prefixes, opcode) \
    XV(N##_Z_Y_SAE, m, EVEX, prefixes, 2, opcode, R, 0, 0, 2, Z, NONE, Y, 32, 32, KZ_SAE, REG_RM)

// Conversions of 64-bit elements to elements half as wide, into a YMM register: from a ZMM register, a 512-bit
// memory operand or a broadcast, then from a register with embedded rounding or suppressed exceptions (K: ER or SAE).
#define BUSTER_X86_AVX512_NARROWING_FORMS(XV, N, m, prefixes, opcode, K) \
    XV(N##_Y_ZM512, m, EVEX, prefixes, 2, opcode, R, 0, 1, 2, Y, NONE, ZM, 64, 64, KZ, REG_RM) \
    XV(N##_Y_M64BCST, m, EVEX, prefixes, 2, opcode, R, 0, 1, 2, Y, NONE, M, 64, 8, KZ_BCST, REG_RM) \
    XV(N##_Y_Z_##K, m, EVEX, prefixes, 2, opcode, R, 0, 1, 2, Y, NONE, Z, 64, 64, KZ_##K, REG_RM)

// Compares into an opmask register, which an opmask can mask (but not zero).
#define BUSTER_X86_AVX512_COMPARE_FORM(XV, N, m, prefixes, opcode_bytes, opcode, w) \
    XV(N##_K_Z_ZM512, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 2, K, Z, ZM, 64, 64, K, REG_VVVV_RM)

#define BUSTER_X86_AVX512_COMPARE_BROADCAST_FORMS(XV, N, m, prefixes, opcode_bytes, opcode, w, bits) \
    BUSTER_X86_AVX512_COMPARE_FORM(XV, N, m, prefixes, opcode_bytes, opcode, w) \
    XV(N##_K_Z_M##bits##BCST, m, EVEX, prefixes, opcode_bytes, opcode, R, 0, w, 2, K, Z, M, 64, bits / 8, K_BCST, REG_VVVV_RM)

#define BUSTER_X86_AVX512_COMPARE_IMMEDIATE_FORMS(XV, N, m, opcode, w, bits) \
    XV(N##_K_Z_ZM512_IMM8, m, EVEX, 0x66, 3, opcode, R, 1, w, 2, K, Z, ZM, 64, 64, K, REG_VVVV_RM_IMM) \
    XV(N##_K_Z_M##bits##BCST_IMM8, m, EVEX, 0x66, 3, opcode, R, 1, w, 2, K, Z, M, 64, bits / 8, K_BCST, REG_VVVV_RM_IMM)

// Floating-point compares (0F C2, with a predicate in the immediate), and with suppressed exceptions.
#define BUSTER_X86_AVX512_COMPARE_SAE_FORMS(XV, N, m, prefixes, w, bits) \
    XV(N##_K_Z_ZM512_IMM8, m, EVEX, prefixes, 2, 0x0fc2, R, 1, w, 2, K, Z, ZM, 64, 64, K, REG_VVVV_RM_IMM) \
    XV(N##_K_Z_M##bits##BCST_IMM8, m, EVEX, prefixes, 2, 0x0fc2, R, 1, w, 2, K, Z, M, 64, bits / 8, K_BCST, REG_VVVV_RM_IMM) \
    XV(N##_K_Z_Z_SAE_IMM8, m, EVEX, prefixes, 2, 0x0fc2, R, 1, w, 2, K, Z, Z, 64, 64, K_SAE, REG_VVVV_RM_IMM)

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

// Conversions of 32-bit elements to elements twice as wide: into an XMM register from 64 bits (an XMM register's
// low half, or memory) or a broadcast of 2 elements, and into a YMM register from 128 bits or a broadcast of 4.
#define BUSTER_X86_AVX512VL_WIDENING_FORMS(XV, N, m, prefixes, opcode, suffix) \
    XV(N##_X_XM64##suffix, m, EVEX, prefixes, 2, opcode, R, 0, 0, 0, V, NONE, XM, 8, 8, KZ, REG_RM) \
    XV(N##_X_M32BCST, m, EVEX, prefixes, 2, opcode, R, 0, 0, 0, V, NONE, M, 8, 4, KZ_BCST, REG_RM) \
    XV(N##_Y_XM128##suffix, m, EVEX, prefixes, 2, opcode, R, 0, 0, 1, Y, NONE, XM, 16, 16, KZ, REG_RM) \
    XV(N##_Y_M32BCST, m, EVEX, prefixes, 2, opcode, R, 0, 0, 1, Y, NONE, M, 16, 4, KZ_BCST, REG_RM)

// Conversions of 64-bit elements to elements half as wide, into an XMM register from 128 bits (L0) or 256 bits
// (L1): the broadcasts' names give their element counts, their operands being the same.
#define BUSTER_X86_AVX512VL_NARROWING_FORMS(XV, N, m, prefixes, opcode, suffix) \
    XV(N##_X_XM128##suffix, m, EVEX, prefixes, 2, opcode, R, 0, 1, 0, V, NONE, XM, 16, 16, KZ, REG_RM) \
    XV(N##_X_M64BCST2, m, EVEX, prefixes, 2, opcode, R, 0, 1, 0, V, NONE, M, 16, 8, KZ_BCST, REG_RM) \
    XV(N##_X_YM256##suffix, m, EVEX, prefixes, 2, opcode, R, 0, 1, 1, V, NONE, YM, 32, 32, KZ, REG_RM) \
    XV(N##_X_M64BCST4, m, EVEX, prefixes, 2, opcode, R, 0, 1, 1, V, NONE, M, 32, 8, KZ_BCST, REG_RM)

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

// VAES's rounds on each 128-bit lane, which take no opmask, at 512, 128 and 256 bits (the narrower forms' names are
// VEX's, with _EVEX).
#define BUSTER_X86_AVX512_AES_FORMS(XV, N, m, opcode) \
    XV(N##_Z_Z_ZM512, m, EVEX, 0x66, 3, opcode, R, 0, 0, 2, Z, Z, ZM, 64, 64, NONE, REG_VVVV_RM) \
    XV(N##_X_X_XM128_EVEX, m, EVEX, 0x66, 3, opcode, R, 0, 0, 0, V, V, XM, 16, 16, NONE, REG_VVVV_RM) \
    XV(N##_Y_Y_YM256_EVEX, m, EVEX, 0x66, 3, opcode, R, 0, 0, 1, Y, Y, YM, 32, 32, NONE, REG_VVVV_RM)

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
#define BUSTER_X86_AVX512DQ_CLASS_FORMS(XV, N, m, w, bits, c128, c256, c512) \
    XV(N##_K_XM128_IMM8, m, EVEX, 0x66, 3, 0x0f3a66, R, 1, w, 0, K, NONE, XM, 16, 16, K, REG_RM_IMM) \
    XV(N##_K_M##bits##BCST##c128##_IMM8, m, EVEX, 0x66, 3, 0x0f3a66, R, 1, w, 0, K, NONE, M, 16, bits / 8, K_BCST, REG_RM_IMM) \
    XV(N##_K_YM256_IMM8, m, EVEX, 0x66, 3, 0x0f3a66, R, 1, w, 1, K, NONE, YM, 32, 32, K, REG_RM_IMM) \
    XV(N##_K_M##bits##BCST##c256##_IMM8, m, EVEX, 0x66, 3, 0x0f3a66, R, 1, w, 1, K, NONE, M, 32, bits / 8, K_BCST, REG_RM_IMM) \
    XV(N##_K_ZM512_IMM8, m, EVEX, 0x66, 3, 0x0f3a66, R, 1, w, 2, K, NONE, ZM, 64, 64, K, REG_RM_IMM) \
    XV(N##_K_M##bits##BCST##c512##_IMM8, m, EVEX, 0x66, 3, 0x0f3a66, R, 1, w, 2, K, NONE, M, 64, bits / 8, K_BCST, REG_RM_IMM)

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

// Scalar operations with an 8-bit immediate (66 0F 3A), on `bits` bits, with an opmask, and with suppressed
// exceptions.
#define BUSTER_X86_AVX512_SCALAR_IMMEDIATE_SAE_FORMS(XV, N, m, opcode, w, bits) \
    XV(N##_X_X_XM##bits##_IMM8, m, EVEX, 0x66, 3, opcode, R, 1, w, 0, V, V, XM, bits / 8, bits / 8, KZ, REG_VVVV_RM_IMM) \
    XV(N##_X_X_X_SAE_IMM8, m, EVEX, 0x66, 3, opcode, R, 1, w, 0, V, V, V, bits / 8, bits / 8, KZ_SAE, REG_VVVV_RM_IMM)

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
    XV(VPCLMULQDQ_X_X_XM128_IMM8, "vpclmulqdq", VEX, 0x66, 3, 0x0f3a44, R, 1, 0, 0, V, V, XM, 16, 1, NONE, REG_VVVV_RM_IMM) \
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
    XV(ANDN_R32_R32_RM32, "andn", VEX, 0, 3, 0x0f38f2, R, 0, 0, 0, GD, GD, ED, 4, 1, NONE, REG_VVVV_RM) \
    XV(ANDN_R64_R64_RM64, "andn", VEX, 0, 3, 0x0f38f2, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, NONE, REG_VVVV_RM) \
    XV(BEXTR_R32_RM32_R32, "bextr", VEX, 0, 3, 0x0f38f7, R, 0, 0, 0, GD, GD, ED, 4, 1, NONE, REG_RM_VVVV) \
    XV(BEXTR_R64_RM64_R64, "bextr", VEX, 0, 3, 0x0f38f7, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, NONE, REG_RM_VVVV) \
    XV(BLSI_R32_RM32, "blsi", VEX, 0, 3, 0x0f38f3, D3, 0, 0, 0, NONE, GD, ED, 4, 1, NONE, VVVV_RM) \
    XV(BLSI_R64_RM64, "blsi", VEX, 0, 3, 0x0f38f3, D3, 0, 1, 0, NONE, GQ, EQ, 8, 1, NONE, VVVV_RM) \
    XV(BLSMSK_R32_RM32, "blsmsk", VEX, 0, 3, 0x0f38f3, D2, 0, 0, 0, NONE, GD, ED, 4, 1, NONE, VVVV_RM) \
    XV(BLSMSK_R64_RM64, "blsmsk", VEX, 0, 3, 0x0f38f3, D2, 0, 1, 0, NONE, GQ, EQ, 8, 1, NONE, VVVV_RM) \
    XV(BLSR_R32_RM32, "blsr", VEX, 0, 3, 0x0f38f3, D1, 0, 0, 0, NONE, GD, ED, 4, 1, NONE, VVVV_RM) \
    XV(BLSR_R64_RM64, "blsr", VEX, 0, 3, 0x0f38f3, D1, 0, 1, 0, NONE, GQ, EQ, 8, 1, NONE, VVVV_RM) \
    XV(BZHI_R32_RM32_R32, "bzhi", VEX, 0, 3, 0x0f38f5, R, 0, 0, 0, GD, GD, ED, 4, 1, NONE, REG_RM_VVVV) \
    XV(BZHI_R64_RM64_R64, "bzhi", VEX, 0, 3, 0x0f38f5, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, NONE, REG_RM_VVVV) \
    XV(PDEP_R32_R32_RM32, "pdep", VEX, 0xf2, 3, 0x0f38f5, R, 0, 0, 0, GD, GD, ED, 4, 1, NONE, REG_VVVV_RM) \
    XV(PDEP_R64_R64_RM64, "pdep", VEX, 0xf2, 3, 0x0f38f5, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, NONE, REG_VVVV_RM) \
    XV(PEXT_R32_R32_RM32, "pext", VEX, 0xf3, 3, 0x0f38f5, R, 0, 0, 0, GD, GD, ED, 4, 1, NONE, REG_VVVV_RM) \
    XV(PEXT_R64_R64_RM64, "pext", VEX, 0xf3, 3, 0x0f38f5, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, NONE, REG_VVVV_RM) \
    XV(MULX_R32_R32_RM32, "mulx", VEX, 0xf2, 3, 0x0f38f6, R, 0, 0, 0, GD, GD, ED, 4, 1, NONE, REG_VVVV_RM) \
    XV(MULX_R64_R64_RM64, "mulx", VEX, 0xf2, 3, 0x0f38f6, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, NONE, REG_VVVV_RM) \
    XV(RORX_R32_RM32_IMM8, "rorx", VEX, 0xf2, 3, 0x0f3af0, R, 1, 0, 0, GD, NONE, ED, 4, 1, NONE, REG_RM_IMM) \
    XV(RORX_R64_RM64_IMM8, "rorx", VEX, 0xf2, 3, 0x0f3af0, R, 1, 1, 0, GQ, NONE, EQ, 8, 1, NONE, REG_RM_IMM) \
    XV(SARX_R32_RM32_R32, "sarx", VEX, 0xf3, 3, 0x0f38f7, R, 0, 0, 0, GD, GD, ED, 4, 1, NONE, REG_RM_VVVV) \
    XV(SARX_R64_RM64_R64, "sarx", VEX, 0xf3, 3, 0x0f38f7, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, NONE, REG_RM_VVVV) \
    XV(SHLX_R32_RM32_R32, "shlx", VEX, 0x66, 3, 0x0f38f7, R, 0, 0, 0, GD, GD, ED, 4, 1, NONE, REG_RM_VVVV) \
    XV(SHLX_R64_RM64_R64, "shlx", VEX, 0x66, 3, 0x0f38f7, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, NONE, REG_RM_VVVV) \
    XV(SHRX_R32_RM32_R32, "shrx", VEX, 0xf2, 3, 0x0f38f7, R, 0, 0, 0, GD, GD, ED, 4, 1, NONE, REG_RM_VVVV) \
    XV(SHRX_R64_RM64_R64, "shrx", VEX, 0xf2, 3, 0x0f38f7, R, 0, 1, 0, GQ, GQ, EQ, 8, 1, NONE, REG_RM_VVVV) \
    XV(KMOVB_K_K, "kmovb", VEX, 0x66, 2, 0x0f90, R, 0, 0, 0, K, NONE, K, 1, 1, NONE, REG_RM) \
    XV(KMOVB_K_M8, "kmovb", VEX, 0x66, 2, 0x0f90, R, 0, 0, 0, K, NONE, M, 1, 1, NONE, REG_RM) \
    XV(KMOVB_M8_K, "kmovb", VEX, 0x66, 2, 0x0f91, R, 0, 0, 0, K, NONE, M, 1, 1, NONE, RM_REG) \
    XV(KMOVB_K_R32, "kmovb", VEX, 0x66, 2, 0x0f92, R, 0, 0, 0, K, NONE, GD, 4, 1, NONE, REG_RM) \
    XV(KMOVB_R32_K, "kmovb", VEX, 0x66, 2, 0x0f93, R, 0, 0, 0, GD, NONE, K, 4, 1, NONE, REG_RM) \
    XV(KMOVW_K_K, "kmovw", VEX, 0, 2, 0x0f90, R, 0, 0, 0, K, NONE, K, 2, 1, NONE, REG_RM) \
    XV(KMOVW_K_M16, "kmovw", VEX, 0, 2, 0x0f90, R, 0, 0, 0, K, NONE, M, 2, 1, NONE, REG_RM) \
    XV(KMOVW_M16_K, "kmovw", VEX, 0, 2, 0x0f91, R, 0, 0, 0, K, NONE, M, 2, 1, NONE, RM_REG) \
    XV(KMOVW_K_R32, "kmovw", VEX, 0, 2, 0x0f92, R, 0, 0, 0, K, NONE, GD, 4, 1, NONE, REG_RM) \
    XV(KMOVW_R32_K, "kmovw", VEX, 0, 2, 0x0f93, R, 0, 0, 0, GD, NONE, K, 4, 1, NONE, REG_RM) \
    XV(KMOVD_K_K, "kmovd", VEX, 0x66, 2, 0x0f90, R, 0, 1, 0, K, NONE, K, 4, 1, NONE, REG_RM) \
    XV(KMOVD_K_M32, "kmovd", VEX, 0x66, 2, 0x0f90, R, 0, 1, 0, K, NONE, M, 4, 1, NONE, REG_RM) \
    XV(KMOVD_M32_K, "kmovd", VEX, 0x66, 2, 0x0f91, R, 0, 1, 0, K, NONE, M, 4, 1, NONE, RM_REG) \
    XV(KMOVD_K_R32, "kmovd", VEX, 0xf2, 2, 0x0f92, R, 0, 0, 0, K, NONE, GD, 4, 1, NONE, REG_RM) \
    XV(KMOVD_R32_K, "kmovd", VEX, 0xf2, 2, 0x0f93, R, 0, 0, 0, GD, NONE, K, 4, 1, NONE, REG_RM) \
    XV(KMOVQ_K_K, "kmovq", VEX, 0, 2, 0x0f90, R, 0, 1, 0, K, NONE, K, 8, 1, NONE, REG_RM) \
    XV(KMOVQ_K_M64, "kmovq", VEX, 0, 2, 0x0f90, R, 0, 1, 0, K, NONE, M, 8, 1, NONE, REG_RM) \
    XV(KMOVQ_M64_K, "kmovq", VEX, 0, 2, 0x0f91, R, 0, 1, 0, K, NONE, M, 8, 1, NONE, RM_REG) \
    XV(KMOVQ_K_R64, "kmovq", VEX, 0xf2, 2, 0x0f92, R, 0, 1, 0, K, NONE, GQ, 8, 1, NONE, REG_RM) \
    XV(KMOVQ_R64_K, "kmovq", VEX, 0xf2, 2, 0x0f93, R, 0, 1, 0, GQ, NONE, K, 8, 1, NONE, REG_RM) \
    XV(KANDW_K_K_K, "kandw", VEX, 0, 2, 0x0f41, R, 0, 0, 1, K, K, K, 0, 1, NONE, REG_VVVV_RM) \
    XV(KANDNW_K_K_K, "kandnw", VEX, 0, 2, 0x0f42, R, 0, 0, 1, K, K, K, 0, 1, NONE, REG_VVVV_RM) \
    XV(KORW_K_K_K, "korw", VEX, 0, 2, 0x0f45, R, 0, 0, 1, K, K, K, 0, 1, NONE, REG_VVVV_RM) \
    XV(KXORW_K_K_K, "kxorw", VEX, 0, 2, 0x0f47, R, 0, 0, 1, K, K, K, 0, 1, NONE, REG_VVVV_RM) \
    XV(KANDQ_K_K_K, "kandq", VEX, 0, 2, 0x0f41, R, 0, 1, 1, K, K, K, 0, 1, NONE, REG_VVVV_RM) \
    XV(KORQ_K_K_K, "korq", VEX, 0, 2, 0x0f45, R, 0, 1, 1, K, K, K, 0, 1, NONE, REG_VVVV_RM) \
    XV(KXORQ_K_K_K, "kxorq", VEX, 0, 2, 0x0f47, R, 0, 1, 1, K, K, K, 0, 1, NONE, REG_VVVV_RM) \
    XV(KADDQ_K_K_K, "kaddq", VEX, 0, 2, 0x0f4a, R, 0, 1, 1, K, K, K, 0, 1, NONE, REG_VVVV_RM) \
    XV(KUNPCKBW_K_K_K, "kunpckbw", VEX, 0x66, 2, 0x0f4b, R, 0, 0, 1, K, K, K, 0, 1, NONE, REG_VVVV_RM) \
    XV(KUNPCKWD_K_K_K, "kunpckwd", VEX, 0, 2, 0x0f4b, R, 0, 0, 1, K, K, K, 0, 1, NONE, REG_VVVV_RM) \
    XV(KUNPCKDQ_K_K_K, "kunpckdq", VEX, 0, 2, 0x0f4b, R, 0, 1, 1, K, K, K, 0, 1, NONE, REG_VVVV_RM) \
    XV(KNOTW_K_K, "knotw", VEX, 0, 2, 0x0f44, R, 0, 0, 0, K, NONE, K, 0, 1, NONE, REG_RM) \
    XV(KNOTQ_K_K, "knotq", VEX, 0, 2, 0x0f44, R, 0, 1, 0, K, NONE, K, 0, 1, NONE, REG_RM) \
    XV(KORTESTW_K_K, "kortestw", VEX, 0, 2, 0x0f98, R, 0, 0, 0, K, NONE, K, 0, 1, NONE, REG_RM) \
    XV(KORTESTQ_K_K, "kortestq", VEX, 0, 2, 0x0f98, R, 0, 1, 0, K, NONE, K, 0, 1, NONE, REG_RM) \
    XV(KTESTQ_K_K, "ktestq", VEX, 0, 2, 0x0f99, R, 0, 1, 0, K, NONE, K, 0, 1, NONE, REG_RM) \
    XV(KSHIFTRW_K_K_IMM8, "kshiftrw", VEX, 0x66, 3, 0x0f3a30, R, 1, 1, 0, K, NONE, K, 0, 1, NONE, REG_RM_IMM) \
    XV(KSHIFTRQ_K_K_IMM8, "kshiftrq", VEX, 0x66, 3, 0x0f3a31, R, 1, 1, 0, K, NONE, K, 0, 1, NONE, REG_RM_IMM) \
    XV(KSHIFTLW_K_K_IMM8, "kshiftlw", VEX, 0x66, 3, 0x0f3a32, R, 1, 1, 0, K, NONE, K, 0, 1, NONE, REG_RM_IMM) \
    XV(KSHIFTLQ_K_K_IMM8, "kshiftlq", VEX, 0x66, 3, 0x0f3a33, R, 1, 1, 0, K, NONE, K, 0, 1, NONE, REG_RM_IMM) \
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
    BUSTER_X86_CMPCCXADD_FORMS(XV, O, "o", 0) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, NO, "no", 1) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, B, "b", 2) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, NB, "nb", 3) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, Z, "z", 4) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, NZ, "nz", 5) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, BE, "be", 6) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, NBE, "nbe", 7) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, S, "s", 8) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, NS, "ns", 9) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, P, "p", 10) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, NP, "np", 11) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, L, "l", 12) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, NL, "nl", 13) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, LE, "le", 14) \
    BUSTER_X86_CMPCCXADD_FORMS(XV, NLE, "nle", 15)

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
    X(PUSH_RM64, "push", 0, 1, 0xff, D6, 0, 0, 0, NONE, EQ, 8, RM) \
    X(POP_RM64, "pop", 0, 1, 0x8f, D0, 0, 0, 0, NONE, EQ, 8, RM) \
    X(PUSH_IMM8, "push", 0, 1, 0x6a, NO, 1, 0, 0, NONE, NONE, 8, IMM) \
    X(PUSH_IMM32, "push", 0, 1, 0x68, NO, 4, 0, 0, NONE, NONE, 8, IMM) \
    X(PUSH_R16, "push", 0x66, 1, 0x50, PR, 0, 0, 0, NONE, GW, 2, RM) \
    X(POP_R16, "pop", 0x66, 1, 0x58, PR, 0, 0, 0, NONE, GW, 2, RM) \
    X(PUSH_RM16, "push", 0x66, 1, 0xff, D6, 0, 0, 0, NONE, EW, 2, RM) \
    X(POP_RM16, "pop", 0x66, 1, 0x8f, D0, 0, 0, 0, NONE, EW, 2, RM) \
    X(PUSHW_IMM16, "pushw", 0x66, 1, 0x68, NO, 2, 0, 0, NONE, NONE, 2, IMM) \
    X(CALL_REL32, "call", 0, 1, 0xe8, NO, 0, 4, 0, NONE, NONE, 0, REL) \
    X(CALL_RM64, "call", 0, 1, 0xff, D2, 0, 0, 0, NONE, EQ, 8, RM) \
    X(JMP_REL8, "jmp", 0, 1, 0xeb, NO, 0, 1, 0, NONE, NONE, 0, REL) \
    X(JMP_REL32, "jmp", 0, 1, 0xe9, NO, 0, 4, 0, NONE, NONE, 0, REL) \
    X(JMP_RM64, "jmp", 0, 1, 0xff, D4, 0, 0, 0, NONE, EQ, 8, RM) \
    X(CALL_FAR_M32, "call", 0x66, 1, 0xff, D3, 0, 0, 0, NONE, M, 4, RM) \
    X(CALL_FAR_M48, "call", 0, 1, 0xff, D3, 0, 0, 0, NONE, M, 6, RM) \
    X(CALL_FAR_M80, "rex.w call", 0, 1, 0xff, D3, 0, 0, 1, NONE, M, 6, RM) \
    X(JMP_FAR_M32, "jmp", 0x66, 1, 0xff, D5, 0, 0, 0, NONE, M, 4, RM) \
    X(JMP_FAR_M48, "jmp", 0, 1, 0xff, D5, 0, 0, 0, NONE, M, 6, RM) \
    X(JMP_FAR_M80, "rex.w jmp", 0, 1, 0xff, D5, 0, 0, 1, NONE, M, 6, RM) \
    /* CET's NOTRACK (3E), on an indirect branch through a register. */ \
    X(NOTRACK_JMP_R64, "notrack jmp", 0x3e, 1, 0xff, D4, 0, 0, 0, NONE, GQ, 8, RM) \
    X(NOTRACK_CALL_R64, "notrack call", 0x3e, 1, 0xff, D2, 0, 0, 0, NONE, GQ, 8, RM) \
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
    X(RET, "ret", 0, 1, 0xc3, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(RET_IMM16, "ret", 0, 1, 0xc2, NO, 2, 0, 0, NONE, NONE, 0, IMM) \
    X(NOP, "nop", 0, 1, 0x90, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(NOP_RM16, "nop", 0x66, 2, 0x0f1f, D0, 0, 0, 0, NONE, EW, 2, RM) \
    X(NOP_RM32, "nop", 0, 2, 0x0f1f, D0, 0, 0, 0, NONE, ED, 4, RM) \
    X(INT3, "int3", 0, 1, 0xcc, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(INT_IMM8, "int", 0, 1, 0xcd, NO, 1, 0, 0, NONE, NONE, 0, IMM) \
    X(UD2, "ud2", 0, 2, 0x0f0b, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(LEAVE, "leave", 0, 1, 0xc9, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
    X(ENTER, "enter", 0, 1, 0xc8, NO, 3, 0, 0, NONE, NONE, 0, IMM_IMM) \
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
    /* Conversions (cvtpi2ps and cvtpi2pd from memory; their MMX register forms are #30's). */ \
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
    X(FWAIT, "fwait", 0, 1, 0x9b, NO, 0, 0, 0, NONE, NONE, 0, NONE) \
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
    BUSTER_X86_VECTOR_FORMS(XV)

#define BUSTER_X86_FORM_ENUM(name, ...) X86_FORM_##name,
typedef enum X86Form
{
    BUSTER_X86_FORMS(BUSTER_X86_FORM_ENUM, BUSTER_X86_FORM_ENUM)
    X86_FORM_COUNT,
} X86Form;
#undef BUSTER_X86_FORM_ENUM

typedef struct X86FormDescription X86FormDescription;
struct X86FormDescription
{
    const char* mnemonic;
    uint32_t opcode;
    uint16_t prefixes;
    uint8_t opcode_bytes;
    uint8_t encoding;
    uint8_t immediate_bytes;
    uint8_t relative_bytes;
    // REX.W, VEX.W or EVEX.W.
    uint8_t w;
    uint8_t reg;
    uint8_t rm;
    uint8_t operand_bytes;
    uint8_t syntax;
    // BUSTER_X86_FAMILY_*.
    uint8_t family;
    // VEX.L or EVEX.L'L.
    uint8_t vector_length;
    // The VEX.vvvv operand.
    uint8_t vvvv;
    // BUSTER_X86_EVEX_*.
    uint8_t evex;
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

// A form's segment override: 0 for none, 1 for FS and 2 for GS. These two are branches on the rare overrides, so
// that the scalar encoder pays a compare for them.
static uint32_t x86_form_segment(uint32_t form)
{
    uint32_t result = 0;
    if (BUSTER_UNLIKELY(form >= BUSTER_X86_FORM_FS))
    {
        result = form >= BUSTER_X86_FORM_GS ? 2 : 1;
    }
    return result;
}

// The form table's row of a form, with or without a segment override.
static const X86FormDescription* x86_form_description(uint32_t form)
{
    uint32_t row = form;
    if (BUSTER_UNLIKELY(row >= BUSTER_X86_FORM_FS))
    {
        row -= row >= BUSTER_X86_FORM_GS ? BUSTER_X86_FORM_GS : BUSTER_X86_FORM_FS;
    }
    return &x86_form_descriptions[row];
}

// Builds the templates. Returns false if a form doesn't fit in one (a broken form table).
static __attribute__((unused)) bool x86_encoder_initialize(void);
// Encodes the instructions into `output`, which has room for `capacity` bytes (at least
// BUSTER_X86_ENCODER_CAPACITY(count)), and stores each one's offset in `offsets` if it isn't null. Writes nothing past
// output + capacity, but the bytes after the instructions' may change. Returns the number of bytes, or a negative
// X86EncoderError. Requires x86_encoder_initialize.
static __attribute__((unused)) int64_t x86_encode(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity, uint32_t* offsets);
// The same, one instruction at a time from the form table, after x86_validate: the reference for testing.
static __attribute__((unused)) int64_t x86_encode_scalar(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity, uint32_t* offsets);
// The index of the first instruction that can't be encoded, or count if they all can.
static __attribute__((unused)) uint64_t x86_validate(const X86Instruction* instructions, uint64_t count);

static uint8_t x86_opcode_byte(const X86FormDescription* form, uint32_t i)
{
    return (uint8_t)(form->opcode >> (8 * (form->opcode_bytes - 1 - i)));
}

// A VEX or EVEX form's opcode map (VEX.mmmmm, EVEX.mmm): 1 for 0F, 2 for 0F 38 and 3 for 0F 3A, from the escape bytes
// of its opcode; 0 if they're none of those.
static uint8_t x86_opcode_map(const X86FormDescription* form)
{
    uint8_t result = 0;
    if (form->opcode_bytes == 2 && x86_opcode_byte(form, 0) == 0x0f)
    {
        result = 1;
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
// The checks: the bits that must be zero in an instruction's reg, rm, vvvv and decorations bytes (the vectorized
// encoder has them in one dword): registers past the operand's class's last one (or any, if the form has no such
// operand), and the decorations the form can't have.
// The flags. How many bytes the form always has (with a VEX3 prefix), besides REX: prefixes, opcode, ModRM, immediate
// and a static displacement.
#define BUSTER_X86_TEMPLATE_STATIC_LENGTH 0x0f
// The presence bits (canonical bytes 4-7) of a displacement field the form always has, as many bytes of it as its
// layout uses: a relative branch's displacement, or a 64-bit immediate's high half.
#define BUSTER_X86_TEMPLATE_STATIC_DISPLACEMENT 0xf0
// A VEX or EVEX form: REX's presence bit doesn't count (the layout has no REX byte).
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
#define BUSTER_X86_TEMPLATE_BYTE_REG 0x100000
#define BUSTER_X86_TEMPLATE_BYTE_RM 0x200000
// The kinds of rm operand the form can't have: a memory operand (bit 26, where the operands dword has a memory mode's
// BUSTER_X86_MODE_MEMORY), which a form without an rm operand can't have either, and a register (bit 27).
#define BUSTER_X86_TEMPLATE_NO_MEMORY 0x04000000
#define BUSTER_X86_TEMPLATE_NO_REGISTER 0x08000000
// An EVEX form's log2 of N, which its 8-bit displacements are counted in (0 for the other forms), in bits 29-31, so
// that a shift down by 29 makes it a rotation count.
#define BUSTER_X86_TEMPLATE_DISP8_SCALE 29

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

// The templates, with BUSTER_X86_TEMPLATE_PADDING unused templates on either side: the vectorized encoder loads 16 bytes
// of a template into a lane of a ZMM register with a masked load that spans up to 48 bytes on either side of them.
#define BUSTER_X86_TEMPLATE_PADDING 2
static X86Template x86_template_storage[BUSTER_X86_TEMPLATE_PADDING + BUSTER_X86_FORM_TEMPLATES + BUSTER_X86_TEMPLATE_PADDING] __attribute__((aligned(64)));
static X86Template* const x86_templates = x86_template_storage + BUSTER_X86_TEMPLATE_PADDING;
static bool x86_encoder_initialized;

// The bits of a register operand of the class that must be zero: registers past the class's last (15 for general
// registers and VEX's vector registers, 31 for EVEX's, 7 for opmasks and x87 registers), an opmask pair's odd bit
// (which the processor ignores) and the high byte flag, which only a legacy byte register can have; all of them for no
// operand.
static uint8_t x86_forbidden_register_bits(uint8_t operand, uint8_t family)
{
    const uint8_t class = operand & BUSTER_X86_OPERAND_CLASS;
    uint8_t result = 0xf0;
    if (class == BUSTER_X86_CLASS_NONE)
    {
        result = 0xff;
    }
    else if (class == BUSTER_X86_CLASS_GPR8 && family == BUSTER_X86_FAMILY_LEGACY)
    {
        result = 0xb0;
    }
    else if (class >= BUSTER_X86_CLASS_XMM && class <= BUSTER_X86_CLASS_ZMM && family == BUSTER_X86_FAMILY_EVEX)
    {
        result = 0xe0;
    }
    else if (class == BUSTER_X86_CLASS_OPMASK || class == BUSTER_X86_CLASS_ST)
    {
        result = 0xf8;
    }
    else if (class == BUSTER_X86_CLASS_OPMASK_PAIR)
    {
        result = 0xf9;
    }
    return result;
}

// A form's checks (BUSTER_X86_TEMPLATE_CHECKS_OFFSET): reg's, rm's (as a register, or a memory operand's base: one of
// the general registers), vvvv's and the decorations' bits that must be zero.
static uint32_t x86_template_checks(const X86FormDescription* form)
{
    const uint8_t rm_register = (form->rm & BUSTER_X86_OPERAND_REGISTER) ? x86_forbidden_register_bits(form->rm, form->family) : 0xff;
    const uint8_t rm = (form->rm & BUSTER_X86_OPERAND_MEMORY) ? (uint8_t)(rm_register & 0xf0) : rm_register;
    uint8_t decorations = 0;
    decorations |= (form->evex & BUSTER_X86_EVEX_MASK) ? BUSTER_X86_DECORATION_MASK : 0;
    decorations |= (form->evex & BUSTER_X86_EVEX_ZEROING) ? BUSTER_X86_DECORATION_ZEROING : 0;
    decorations |= (form->evex & BUSTER_X86_EVEX_ROUNDING) ? BUSTER_X86_DECORATION_ROUNDING : 0;
    return (uint32_t)x86_forbidden_register_bits(form->reg, form->family) | ((uint32_t)rm << 8) |
           ((uint32_t)x86_forbidden_register_bits(form->vvvv, form->family) << 16) | ((uint32_t)(uint8_t)~decorations << 24);
}

// Builds the template of a form with a segment override (x86_form_segment's) or none. Returns false if its longest
// encoding wouldn't fit in a slot (15 bytes), or the form table's row is one the encoders can't encode.
static bool build_template(const X86FormDescription* form, uint32_t segment, X86Template* template)
{
    uint32_t position = 0;
    uint32_t static_length = 0;
    // The position after the last of the form's own bytes.
    uint32_t last_static = 0;
    uint32_t flags = BUSTER_X86_TEMPLATE_PREFIX_PRESENT;
    bool valid = true;
    const bool memory = (form->rm & BUSTER_X86_OPERAND_MEMORY) != 0;
    memset(template->bytes, 0, sizeof(template->bytes));
    memset(template->layout, BUSTER_X86_LAYOUT_ABSENT, sizeof(template->layout));
    if (segment != 0 && memory)
    {
        template->bytes[position] = (uint8_t)(0x63 + segment);
        template->layout[position] = BUSTER_X86_LAYOUT_STATIC;
        position += 1;
        static_length += 1;
        last_static = position;
    }
    if (form->opcode_bytes != 0 && form->family == BUSTER_X86_FAMILY_LEGACY)
    {
        for (uint32_t prefixes = form->prefixes; prefixes != 0; prefixes >>= 8)
        {
            template->bytes[position] = (uint8_t)prefixes;
            template->layout[position] = BUSTER_X86_LAYOUT_STATIC;
            position += 1;
            static_length += 1;
            last_static = position;
        }

        template->bytes[position] = (uint8_t)(0x40 | (form->w << 3));
        template->layout[position] = BUSTER_X86_CANONICAL_REX;
        position += 1;

        for (uint32_t i = 0; i < form->opcode_bytes; i += 1)
        {
            const bool last = i + 1 == form->opcode_bytes;
            template->bytes[position] = x86_opcode_byte(form, i);
            template->layout[position] = last && form->encoding == BUSTER_X86_ENCODING_PR ? BUSTER_X86_CANONICAL_OPCODE_REGISTER : BUSTER_X86_LAYOUT_STATIC;
            position += 1;
            static_length += 1;
            last_static = position;
        }
        flags |= form->w ? BUSTER_X86_TEMPLATE_REX_ALWAYS : 0;
    }
    else if (form->opcode_bytes != 0)
    {
        const uint8_t map = x86_opcode_map(form);
        const uint8_t pp = x86_prefix_pp(form);
        if (form->family == BUSTER_X86_FAMILY_VEX)
        {
            // C4 (C5 for VEX2); R, X and B (inverted) and the map; W, vvvv (inverted), L and pp.
            template->bytes[position] = 0xc4;
            template->layout[position] = BUSTER_X86_CANONICAL_VEX_ESCAPE;
            template->bytes[position + 1] = (uint8_t)(0xe0 | map);
            template->layout[position + 1] = BUSTER_X86_CANONICAL_PREFIX_P0;
            template->bytes[position + 2] = (uint8_t)((form->w << 7) | 0x78 | (form->vector_length << 2) | pp);
            template->layout[position + 2] = BUSTER_X86_CANONICAL_PREFIX_P1;
            position += 3;
            static_length += 3;
            flags |= map == 1 && !form->w ? BUSTER_X86_TEMPLATE_VEX2 : 0;
            valid = form->vector_length <= 1 && form->disp8_scale == 1 && form->evex == BUSTER_X86_EVEX_NONE;
        }
        else
        {
            // 62; R, X, B and R' (inverted) and the map; W, vvvv (inverted), 1 and pp; L'L (the rounding mode's
            // place, 00 with suppressed exceptions as GNU as encodes it), b and V' (inverted).
            const bool rounding = (form->evex & (BUSTER_X86_EVEX_ROUNDING | BUSTER_X86_EVEX_SAE)) != 0;
            const bool b = (form->evex & BUSTER_X86_EVEX_B) != 0;
            template->bytes[position] = 0x62;
            template->layout[position] = BUSTER_X86_LAYOUT_STATIC;
            template->bytes[position + 1] = (uint8_t)(0xf0 | map);
            template->layout[position + 1] = BUSTER_X86_CANONICAL_PREFIX_P0;
            template->bytes[position + 2] = (uint8_t)((form->w << 7) | 0x78 | 0x04 | pp);
            template->layout[position + 2] = BUSTER_X86_CANONICAL_PREFIX_P1;
            template->bytes[position + 3] = (uint8_t)(((rounding ? 0 : form->vector_length) << 5) | (b << 4) | 0x08);
            template->layout[position + 3] = BUSTER_X86_CANONICAL_PREFIX_P2;
            position += 4;
            static_length += 4;
            valid = form->vector_length <= 2 && form->disp8_scale != 0 && form->disp8_scale <= 64 && (form->disp8_scale & (form->disp8_scale - 1)) == 0;
            flags |= valid ? (uint32_t)__builtin_ctz(form->disp8_scale) << BUSTER_X86_TEMPLATE_DISP8_SCALE : 0;
        }

        // The opcode's last byte: the map stands for its escape bytes.
        template->bytes[position] = x86_opcode_byte(form, form->opcode_bytes - 1u);
        template->layout[position] = BUSTER_X86_LAYOUT_STATIC;
        position += 1;
        static_length += 1;
        last_static = position;
        flags |= BUSTER_X86_TEMPLATE_NO_REX;
        valid = valid && map != 0 && (form->prefixes == 0 || pp != 0) && form->encoding != BUSTER_X86_ENCODING_PR && form->relative_bytes == 0 &&
                form->immediate_bytes <= 1;
    }

    if (form->opcode_bytes != 0)
    {
        if (form->encoding == BUSTER_X86_ENCODING_R || (form->encoding & BUSTER_X86_ENCODING_DIGIT))
        {
            template->bytes[position] = (form->encoding & BUSTER_X86_ENCODING_DIGIT) ? (uint8_t)((form->encoding & 7) << 3) : 0;
            template->layout[position] = BUSTER_X86_CANONICAL_MODRM;
            position += 1;
            static_length += 1;
            last_static = position;
            if (form->rm & BUSTER_X86_OPERAND_MEMORY)
            {
                template->layout[position] = BUSTER_X86_CANONICAL_SIB;
                position += 1;
                for (uint32_t i = 0; i < 4; i += 1)
                {
                    template->layout[position] = (uint8_t)(BUSTER_X86_CANONICAL_DISPLACEMENT + i);
                    position += 1;
                }
            }
        }

        for (uint32_t i = 0; i < form->relative_bytes; i += 1)
        {
            template->layout[position] = (uint8_t)(BUSTER_X86_CANONICAL_DISPLACEMENT + i);
            position += 1;
            static_length += 1;
        }

        // An immediate's bytes past the fourth are the displacement field's.
        for (uint32_t i = 0; i < form->immediate_bytes; i += 1)
        {
            template->layout[position] = (uint8_t)(BUSTER_X86_CANONICAL_IMMEDIATE + i);
            position += 1;
            static_length += 1;
        }
    }

    flags |= static_length;
    flags |= (form->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8 ? BUSTER_X86_TEMPLATE_BYTE_REG : 0;
    flags |= (form->rm & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8 ? BUSTER_X86_TEMPLATE_BYTE_RM : 0;
    flags |= form->relative_bytes != 0 || form->immediate_bytes > 4 ? BUSTER_X86_TEMPLATE_STATIC_DISPLACEMENT : 0;
    flags |= memory ? 0 : BUSTER_X86_TEMPLATE_NO_MEMORY;
    // A segment override's form takes only a memory operand (and without one in its row, nothing).
    flags |= segment != 0 || (form->rm != BUSTER_X86_OPERAND_NONE && !(form->rm & BUSTER_X86_OPERAND_REGISTER)) ? BUSTER_X86_TEMPLATE_NO_REGISTER : 0;
    const uint32_t checks = x86_template_checks(form);
    memcpy(&template->bytes[BUSTER_X86_TEMPLATE_CHECKS_OFFSET], &checks, sizeof(checks));
    memcpy(&template->bytes[BUSTER_X86_TEMPLATE_FLAGS_OFFSET], &flags, sizeof(flags));
    // A static displacement shares the displacement field with a memory operand's, and the form's own bytes have to
    // come before the checks.
    const bool static_displacement_conflict = (flags & BUSTER_X86_TEMPLATE_STATIC_DISPLACEMENT) && memory;
    return valid && position <= BUSTER_X86_MAX_INSTRUCTION_BYTES && static_length <= BUSTER_X86_TEMPLATE_STATIC_LENGTH && !static_displacement_conflict &&
           last_static <= BUSTER_X86_TEMPLATE_STATIC_POSITIONS;
}

static bool x86_encoder_initialize(void)
{
    bool result = true;
    for (uint32_t form = 0; form < BUSTER_X86_FORM_TEMPLATES; form += 1)
    {
        result = build_template(x86_form_description(form), x86_form_segment(form), &x86_templates[form]) && result;
    }
    x86_encoder_initialized = result;
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Validation

// Whether a register operand of the class can be `value` (in a form of the family): no bits past the class's last
// register, and the high byte flag only for AH to BH, in a legacy byte register outside a memory operand.
static bool register_is_valid(uint8_t value, uint8_t operand, uint8_t family, bool memory_part)
{
    const bool high_byte = (value & BUSTER_X86_REGISTER_HIGH_BYTE) != 0;
    const uint8_t number = value & BUSTER_X86_REGISTER_NUMBER;
    bool result = (value & x86_forbidden_register_bits(operand, family)) == 0;
    if (high_byte)
    {
        result = result && !memory_part && number >= 4 && number <= 7;
    }
    return result;
}

// Whether the instruction can be encoded: a valid form, operands the form has and no others, a consistent addressing
// mode, decorations the form can have, and immediates and relative displacements that fit their fields.
static bool x86_instruction_is_valid(const X86Instruction* instruction)
{
    bool result = instruction->form < BUSTER_X86_FORM_TEMPLATES;
    if (result)
    {
        const X86FormDescription* form = x86_form_description(instruction->form);
        const uint8_t mode = instruction->mode;
        const bool memory = (mode & BUSTER_X86_MODE_MEMORY) != 0;
        const bool base = (mode & BUSTER_X86_MODE_BASE) != 0;
        const bool index = (mode & BUSTER_X86_MODE_INDEX) != 0;
        const bool rip = (mode & BUSTER_X86_MODE_RIP) != 0;
        const bool has_modrm = form->encoding == BUSTER_X86_ENCODING_R || (form->encoding & BUSTER_X86_ENCODING_DIGIT);

        // ModRM.reg
        if (form->encoding == BUSTER_X86_ENCODING_R)
        {
            result = result && register_is_valid(instruction->reg, form->reg, form->family, false);
        }
        else
        {
            result = result && instruction->reg == 0;
        }

        // ModRM.rm or the +r register, and the addressing mode.
        if (form->rm == BUSTER_X86_OPERAND_NONE)
        {
            result = result && instruction->rm == 0 && instruction->index == 0 && mode == 0 && x86_form_segment(instruction->form) == 0;
        }
        else if (!memory)
        {
            result = result && mode == 0 && instruction->index == 0 && (form->rm & BUSTER_X86_OPERAND_REGISTER) && x86_form_segment(instruction->form) == 0;
            result = result && register_is_valid(instruction->rm, form->rm, form->family, false);
        }
        else
        {
            result = result && has_modrm && (form->rm & BUSTER_X86_OPERAND_MEMORY);
            result = result && (mode & ~(BUSTER_X86_MODE_SCALE | BUSTER_X86_MODE_MEMORY | BUSTER_X86_MODE_BASE | BUSTER_X86_MODE_INDEX | BUSTER_X86_MODE_RIP)) == 0;
            result = result && !(rip && (base || index));
            result = result && (index || (mode & BUSTER_X86_MODE_SCALE) == 0);
            result = result && (base ? register_is_valid(instruction->rm, BUSTER_X86_OPERAND_GQ, BUSTER_X86_FAMILY_LEGACY, true) : instruction->rm == 0);
            // RSP can't be an index: its SIB index field means "none".
            result = result && (index ? register_is_valid(instruction->index, BUSTER_X86_OPERAND_GQ, BUSTER_X86_FAMILY_LEGACY, true) &&
                                            instruction->index != X86_REGISTER_RSP
                                      : instruction->index == 0);
        }

        // VEX.vvvv, and EVEX's decorations: the form's, zeroing only with an opmask, and not into memory.
        result = result && (form->vvvv != BUSTER_X86_OPERAND_NONE ? register_is_valid(instruction->vvvv, form->vvvv, form->family, false) : instruction->vvvv == 0);
        const uint8_t decorations = instruction->decorations;
        const bool memory_destination = memory && (form->syntax == X86_SYNTAX_RM_REG || form->syntax == X86_SYNTAX_RM_REG_IMM);
        result = result && (decorations & (x86_template_checks(form) >> 24)) == 0;
        result = result && !((decorations & BUSTER_X86_DECORATION_ZEROING) && ((decorations & BUSTER_X86_DECORATION_MASK) == 0 || memory_destination));

        // A high byte register can't be in an instruction with a REX prefix.
        const uint8_t rm_register = memory ? 0 : instruction->rm;
        const bool high_byte = ((instruction->reg | rm_register) & BUSTER_X86_REGISTER_HIGH_BYTE) != 0;
        const bool byte_reg = (form->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8;
        const bool byte_rm = (form->rm & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8;
        const bool rex = form->w || ((instruction->reg | instruction->rm | instruction->index) & 8) ||
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
        if (form->relative_bytes == 1)
        {
            result = result && displacement >= -128 && displacement <= 127;
        }
        else if (form->relative_bytes == 0 && form->immediate_bytes <= 4 && !memory)
        {
            result = result && displacement == 0;
        }
    }
    return result;
}

static uint64_t x86_validate(const X86Instruction* instructions, uint64_t count)
{
    uint64_t i = 0;
    while (i < count && x86_instruction_is_valid(&instructions[i]))
    {
        i += 1;
    }
    return i;
}

// ---------------------------------------------------------------------------------------------------------------------
// The scalar encoder: one instruction at a time, from the form table rather than the templates, so that it checks
// them.

// Encodes a valid instruction at `out` and returns its length.
static uint32_t encode_one(const X86Instruction* instruction, uint8_t* out)
{
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
    // or the +r register). EVEX's X is a register rm's bit 4.
    const bool r = (reg & 8) != 0;
    const bool x = memory ? index && (index_register & 8) : (rm & 16) != 0;
    const bool b = (!memory || base) && (rm & 8);
    uint32_t length = 0;

    // A segment override comes first, as GNU as puts it.
    if (segment != 0)
    {
        out[length] = (uint8_t)(0x63 + segment);
        length += 1;
    }
    if (form->opcode_bytes != 0 && form->family == BUSTER_X86_FAMILY_LEGACY)
    {
        for (uint32_t prefixes = form->prefixes; prefixes != 0; prefixes >>= 8)
        {
            out[length] = (uint8_t)prefixes;
            length += 1;
        }

        // REX: W, R, X and B; and for SPL, BPL, SIL and DIL.
        const uint8_t rex = (uint8_t)(0x40 | (form->w << 3) | (r << 2) | (x << 1) | b);
        bool needs_rex = rex != 0x40;
        if ((form->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8 && instruction->reg >= 4 && instruction->reg <= 7)
        {
            needs_rex = true;
        }
        if ((form->rm & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8 && !memory && instruction->rm >= 4 && instruction->rm <= 7)
        {
            needs_rex = true;
        }
        if (needs_rex)
        {
            out[length] = rex;
            length += 1;
        }

        for (uint32_t i = 0; i < form->opcode_bytes; i += 1)
        {
            uint8_t byte = x86_opcode_byte(form, i);
            if (i + 1 == form->opcode_bytes && form->encoding == BUSTER_X86_ENCODING_PR)
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
        const uint8_t vvvv_field = (uint8_t)((~vvvv & 15) << 3);
        if (form->family == BUSTER_X86_FAMILY_VEX && map == 1 && !form->w && !x && !b)
        {
            out[length] = 0xc5;
            out[length + 1] = (uint8_t)((!r << 7) | vvvv_field | (form->vector_length << 2) | pp);
            length += 2;
        }
        else if (form->family == BUSTER_X86_FAMILY_VEX)
        {
            out[length] = 0xc4;
            out[length + 1] = (uint8_t)((!r << 7) | (!x << 6) | (!b << 5) | map);
            out[length + 2] = (uint8_t)((form->w << 7) | vvvv_field | (form->vector_length << 2) | pp);
            length += 3;
        }
        else
        {
            // L'L is the vector length, or with embedded rounding the rounding mode (00 for suppressed exceptions).
            const uint8_t decorations = instruction->decorations;
            const bool rounding = (form->evex & (BUSTER_X86_EVEX_ROUNDING | BUSTER_X86_EVEX_SAE)) != 0;
            const uint8_t vector_length = rounding ? (uint8_t)((decorations & BUSTER_X86_DECORATION_ROUNDING) >> 5) : form->vector_length;
            out[length] = 0x62;
            out[length + 1] = (uint8_t)((!r << 7) | (!x << 6) | (!b << 5) | (!(reg & 16) << 4) | map);
            out[length + 2] = (uint8_t)((form->w << 7) | vvvv_field | 0x04 | pp);
            out[length + 3] = (uint8_t)((decorations & BUSTER_X86_DECORATION_ZEROING) | (vector_length << 5) | (((form->evex & BUSTER_X86_EVEX_B) != 0) << 4) |
                                        (!(vvvv & 16) << 3) | (decorations & BUSTER_X86_DECORATION_MASK));
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
            else if (rip)
            {
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

static int64_t x86_encode_scalar(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity, uint32_t* offsets)
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
    else if (x86_validate(instructions, count) != count)
    {
        result = X86_ENCODER_ERROR_INVALID_INSTRUCTION;
    }
    else
    {
        uint64_t length = 0;
        for (uint64_t i = 0; i < count; i += 1)
        {
            if (offsets != 0)
            {
                offsets[i] = (uint32_t)length;
            }
            length += encode_one(&instructions[i], output + length);
        }
        result = (int64_t)length;
    }
    return result;
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
    __m512i length;
    // Nonzero in the lanes of the instructions whose operands the encoding can't represent.
    __m512i invalid;
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
// The presence table's operand bits that must be zero, in places no layout looks at: an invalid mode's own mode bits
// (operands bits 26-29, of which it has some), a memory operand's rm bits 4 and 6 (its base is a general register, and
// not a high byte one), and without an index, the index register and the scale.
#define BUSTER_X86_MODE_TABLE_INVALID 0x3c000000u
#define BUSTER_X86_MODE_TABLE_MEMORY 0x00005000u
#define BUSTER_X86_MODE_TABLE_NO_INDEX 0x030f0000u

// What each addressing mode decides, indexed (by vpermd) with its memory, base, index and RIP bits: operands >> 26.
// x86_mode_fields says which canonical bits come from the operands; x86_mode_constants, which bits are fixed (mod 11
// for a register, rm 100 for a SIB byte or 101 for RIP, a SIB base of 101 or index of 100 for none); and
// x86_mode_presence, the canonical bytes the mode always has (a SIB byte, a 32-bit displacement) and the operand bits
// it forbids. A mode with a base also has a displacement and maybe a SIB byte that depend on the operands, which
// compute_fields works out. The other combinations of the bits are invalid.
static const uint32_t x86_mode_fields[16] __attribute__((aligned(64))) = {
    [0] = BUSTER_X86_FIELDS_ALWAYS | BUSTER_X86_FIELD_RM | BUSTER_X86_FIELD_REX_B,
    [1] = BUSTER_X86_FIELDS_ALWAYS,
    [3] = BUSTER_X86_FIELDS_ALWAYS | BUSTER_X86_FIELD_RM | BUSTER_X86_FIELD_BASE | BUSTER_X86_FIELD_REX_B,
    [5] = BUSTER_X86_FIELDS_ALWAYS | BUSTER_X86_FIELD_INDEX | BUSTER_X86_FIELD_SCALE | BUSTER_X86_FIELD_REX_X,
    [7] = BUSTER_X86_FIELDS_ALWAYS | BUSTER_X86_FIELD_BASE | BUSTER_X86_FIELD_INDEX | BUSTER_X86_FIELD_SCALE | BUSTER_X86_FIELD_REX_B | BUSTER_X86_FIELD_REX_X,
    [9] = BUSTER_X86_FIELDS_ALWAYS,
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
    [0] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | BUSTER_X86_MODE_TABLE_NO_INDEX,
    [1] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | 0x2f0 | BUSTER_X86_MODE_TABLE_MEMORY | BUSTER_X86_MODE_TABLE_NO_INDEX,
    [2] = BUSTER_X86_MODE_TABLE_INVALID,
    [3] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | BUSTER_X86_MODE_TABLE_MEMORY | BUSTER_X86_MODE_TABLE_NO_INDEX,
    [4] = BUSTER_X86_MODE_TABLE_INVALID,
    [5] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | 0x2f0 | BUSTER_X86_MODE_TABLE_MEMORY,
    [6] = BUSTER_X86_MODE_TABLE_INVALID,
    [7] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | 0x200 | BUSTER_X86_MODE_TABLE_MEMORY,
    [8] = BUSTER_X86_MODE_TABLE_INVALID,
    [9] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | 0xf0 | BUSTER_X86_MODE_TABLE_MEMORY | BUSTER_X86_MODE_TABLE_NO_INDEX,
    [10] = BUSTER_X86_MODE_TABLE_INVALID,
    [11] = BUSTER_X86_MODE_TABLE_INVALID,
    [12] = BUSTER_X86_MODE_TABLE_INVALID,
    [13] = BUSTER_X86_MODE_TABLE_INVALID,
    [14] = BUSTER_X86_MODE_TABLE_INVALID,
    [15] = BUSTER_X86_MODE_TABLE_INVALID,
};

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
BUSTER_INLINE X86Fields compute_fields(__m512i operands, __m512i displacement, __m512i extension, __m512i flags, __m512i checks)
{
    const __m512i one = BUSTER_X86_SPLAT(1);
    const __m512i mode = _mm512_srli_epi32(operands, 26);
    const __m512i mode_fields = _mm512_permutexvar_epi32(mode, _mm512_load_si512(x86_mode_fields));
    const __m512i mode_constants = _mm512_permutexvar_epi32(mode, _mm512_load_si512(x86_mode_constants));
    const __m512i mode_presence = _mm512_permutexvar_epi32(mode, _mm512_load_si512(x86_mode_presence));

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
    // pipe runs, two cycles each on Zen 5, where a compare and vpmovm2d take eight cycles on the shuffle pipes.
    const __m512i base = _mm512_srai_epi32(_mm512_slli_epi32(operands, 31 - 26 - 1), 31);
    const __m512i rm_bits = _mm512_and_epi32(operands, BUSTER_X86_SPLAT(0x700));
    const __m512i rm_4 = _mm512_srai_epi32(_mm512_sub_epi32(_mm512_xor_epi32(rm_bits, BUSTER_X86_SPLAT(4 << 8)), one), 31);
    const __m512i rm_5 = _mm512_srai_epi32(_mm512_sub_epi32(_mm512_xor_epi32(rm_bits, BUSTER_X86_SPLAT(5 << 8)), one), 31);
    const __m512i nonzero = _mm512_srai_epi32(_mm512_or_epi32(displacement, _mm512_sub_epi32(_mm512_setzero_si512(), displacement)), 31);
    const __m512i no_displacement = _mm512_ternarylogic_epi32(base, nonzero, rm_5, BUSTER_X86_A_AND_NOT_B_AND_NOT_C);
    const __m512i base_sib = _mm512_and_epi32(base, rm_4);
    // An 8-bit displacement fits if the displacement is a multiple of the form's N (2^s: 1, or for EVEX, the scale in
    // the flags) whose quotient is in [-128, 127]: if s is at most the displacement's trailing zeros and at least the
    // bit length of |displacement| >> 7 (with |x| - 1 for a negative x, as the range is asymmetric). Bit s of
    // `compressible` says so, for every s: the bits up to the trailing zeros, x ^ (x - 1), but those below the bit
    // length, 0x01ffffff >> lzcnt(|x|). Rotated right by one, it then rotates right by s to put bit s in the sign;
    // and the displacement rotated right by s has the quotient in its low byte, which is where the canonical slot
    // takes an 8-bit displacement from. Only the last three operations wait for the flags.
    const __m512i magnitude = _mm512_xor_epi32(displacement, _mm512_srai_epi32(displacement, 31));
    const __m512i too_long = _mm512_srlv_epi32(BUSTER_X86_SPLAT(0x01ffffff), _mm512_lzcnt_epi32(magnitude));
    const __m512i compressible = _mm512_ror_epi32(_mm512_ternarylogic_epi32(displacement, _mm512_sub_epi32(displacement, one), too_long, BUSTER_X86_A_XOR_B_AND_NOT_C), 1);
    const __m512i scale = _mm512_srli_epi32(flags, BUSTER_X86_TEMPLATE_DISP8_SCALE);
    const __m512i fits8 = _mm512_srai_epi32(_mm512_rorv_epi32(compressible, scale), 31);
    const __m512i displacement8 = _mm512_ternarylogic_epi32(base, fits8, no_displacement, BUSTER_X86_A_AND_B_AND_NOT_C);
    const __m512i base_displacement32 = _mm512_andnot_epi32(fits8, base);
    // ModRM.mod: 01 for an 8-bit displacement, 10 for a 32-bit one after a base.
    __m512i canonical = _mm512_ternarylogic_epi32(register_fields, displacement8, BUSTER_X86_SPLAT(0x40), BUSTER_X86_A_OR_B_AND_C);
    canonical = _mm512_ternarylogic_epi32(canonical, base_displacement32, BUSTER_X86_SPLAT(0x80), BUSTER_X86_A_OR_B_AND_C);

    // REX: for REX.RXB or W (REX_ALWAYS, at bit 19), and for SPL, BPL, SIL and DIL: 4 to 7 without the high byte flag,
    // in a byte operand. low_reg and low_rm are zero for those: (register & 0xfc) ^ 4, or'ed with the byte form flag's
    // absence, and for rm, with the memory flag (bit 26, at 18 after the shift).
    //
    // Both are zero tests of values below 2^31, which (x - 1) >> 31 does in two operations of two cycles each (a
    // compare into a mask register takes six, on the busier pipes): rex_needed's bit 31 is set if rex_bits isn't zero
    // or low_byte is.
    const __m512i rex_bits = _mm512_ternarylogic_epi32(register_fields, flags, BUSTER_X86_SPLAT(0xf0000), BUSTER_X86_A_OR_B_THEN_AND_C);
    const __m512i low_reg = _mm512_ternarylogic_epi32(_mm512_xor_epi32(_mm512_and_epi32(operands, BUSTER_X86_SPLAT(0xfc)), BUSTER_X86_SPLAT(4)), flags,
                                                      BUSTER_X86_SPLAT(BUSTER_X86_TEMPLATE_BYTE_REG), BUSTER_X86_A_OR_NOT_B_AND_C);
    const __m512i low_rm = _mm512_ternarylogic_epi32(
        _mm512_xor_epi32(_mm512_and_epi32(_mm512_srli_epi32(operands, 8), BUSTER_X86_SPLAT(0x400fc)), BUSTER_X86_SPLAT(4)), flags,
        BUSTER_X86_SPLAT(BUSTER_X86_TEMPLATE_BYTE_RM), BUSTER_X86_A_OR_NOT_B_AND_C);
    const __m512i low_byte = _mm512_min_epu32(low_reg, low_rm);
    const __m512i rex_needed = _mm512_ternarylogic_epi32(_mm512_sub_epi32(rex_bits, one), _mm512_sub_epi32(low_byte, one), BUSTER_X86_SPLAT(0x80000000),
                                                         BUSTER_X86_NOT_A_OR_B_THEN_AND_C);
    // REX's presence bit (10), and all ones where there's a REX prefix.
    const __m512i rex_presence = _mm512_srli_epi32(rex_needed, 21);
    const __m512i rex = _mm512_srai_epi32(rex_needed, 31);

    // A VEX or EVEX prefix's operand bits (canonical bytes 12-15), which flip the template's: R, X and B (REX's,
    // canonical bits 18-16) in P0's bits 7-5, with EVEX's R' (bit 28) in its bit 4 and EVEX's X of a register rm (bit
    // 30) in its bit 6, which shifting down by 3 and 16 puts there (and nothing in bits 16-31).
    const __m512i p0 = _mm512_ternarylogic_epi32(_mm512_srli_epi32(register_fields, 16), _mm512_srli_epi32(register_fields, 3), BUSTER_X86_SPLAT(0xe000),
                                                 BUSTER_X86_A_OR_B_AND_C);
    // VEX2, if the form can be (BUSTER_X86_TEMPLATE_VEX2, bit 13) and there's no X or B (bits 14 and 13; the flags'
    // bit 14 is always set): C4 becomes C5 (bit 0), R moves to P1's bit 7, and P0 isn't there.
    const __m512i vex2 = _mm512_srai_epi32(_mm512_sub_epi32(_mm512_ternarylogic_epi32(p0, flags, BUSTER_X86_SPLAT(0x6000), BUSTER_X86_A_OR_NOT_B_THEN_AND_C), one), 31);
    const __m512i vex2_bits = _mm512_ternarylogic_epi32(vex2, _mm512_slli_epi32(p0, 8), BUSTER_X86_SPLAT(0x800001), BUSTER_X86_A_AND_NOT_B_AND_C);
    // vvvv in P1's bits 6-3, and EVEX's P2: the decorations as they are, with V' (vvvv's bit 4, record bit 20) in bit 3.
    // The multishift puts the form's bits 13-15 in P1's bits 0-2 (pp and L, the template's), where the last ternlog
    // takes vex2_v's zeros instead.
    const __m512i vvvv_decorations = _mm512_multishift_epi64_epi8(_mm512_set1_epi64(BUSTER_X86_PREFIX_CONTROL), extension);
    const __m512i vex2_v = _mm512_ternarylogic_epi32(_mm512_slli_epi32(extension, 7), vex2_bits, BUSTER_X86_SPLAT(0x08000000), BUSTER_X86_C_SELECTS_A_OVER_B);
    const __m512i p0_vvvv_decorations = _mm512_ternarylogic_epi32(p0, vvvv_decorations, BUSTER_X86_SPLAT(0x0000f000), BUSTER_X86_C_SELECTS_A_OVER_B);
    const __m512i prefix = _mm512_ternarylogic_epi32(vex2_v, p0_vvvv_decorations, BUSTER_X86_SPLAT(0x08870fff), BUSTER_X86_C_SELECTS_A_OVER_B);

    // Which canonical bytes the instruction has: the mode's (the always present ones among them, and a SIB byte),
    // the form's prefix bytes and a static displacement's (bits 4-7 of the flags; their bits 0-3 fall on the
    // immediate's, which are always set), and those that depend on the operands, the latest last. The mode's bits
    // that aren't presence bits fall where no layout looks.
    __m512i presence = _mm512_ternarylogic_epi32(mode_presence, base_sib, BUSTER_X86_SPLAT(0x200), BUSTER_X86_A_OR_B_AND_C);
    presence = _mm512_ternarylogic_epi32(presence, flags, BUSTER_X86_SPLAT(0xdbff), BUSTER_X86_A_OR_B_AND_C);
    presence = _mm512_ternarylogic_epi32(presence, vex2, BUSTER_X86_SPLAT(0x2000), BUSTER_X86_A_OR_NOT_B_AND_C);
    presence = _mm512_ternarylogic_epi32(presence, displacement8, BUSTER_X86_SPLAT(0x10), BUSTER_X86_A_OR_B_AND_C);
    presence = _mm512_ternarylogic_epi32(presence, base_displacement32, BUSTER_X86_SPLAT(0xf0), BUSTER_X86_A_OR_B_AND_C);
    presence = _mm512_or_epi32(presence, rex_presence);
    // The flags' last use (but the length's, with offsets): bit 26 is set if the rm operand is of a kind the form can't
    // have (see the invalid operands below).
    const __m512i wrong_kind = _mm512_ternarylogic_epi32(flags, _mm512_srli_epi32(flags, 1), operands, BUSTER_X86_C_SELECTS_A_OVER_B);

    // The length: the form's static bytes, the displacement, SIB and REX bytes the operands add, and one less for VEX2.
    const __m512i dynamic = _mm512_ternarylogic_epi32(presence, flags, BUSTER_X86_SPLAT(0x6f0), BUSTER_X86_A_AND_NOT_B_AND_C);
    const __m512i length = _mm512_add_epi32(_mm512_add_epi32(_mm512_popcnt_epi32(dynamic), _mm512_and_epi32(flags, BUSTER_X86_SPLAT(BUSTER_X86_TEMPLATE_STATIC_LENGTH))),
                                            vex2);

    // Operands the encoding can't represent: bits that must be zero, the template's checks of reg, rm, vvvv and the
    // decorations (put together in one dword), the mode's (see the tables), an index register's bits 4-7 and unknown
    // mode bits; RSP as an index (zero in rsp_index: the index's low
    // nibble ^ 4, or'ed with the index field's absence; below 2^31, so that rsp_index - 1 has the sign bit only then);
    // a high byte register (bit 6 of reg or rm, which only a legacy byte form allows) with a REX prefix; and a memory
    // operand in a form that can't have one, or a register in a form whose rm can only be memory, the flag that the
    // memory mode's bit picks (NO_REGISTER shifted to NO_MEMORY's bit). The loop ORs them together for one test at the
    // end.
    const __m512i forbidden = _mm512_ternarylogic_epi32(
        mode_presence, BUSTER_X86_SPLAT(BUSTER_X86_MODE_TABLE_INVALID | BUSTER_X86_MODE_TABLE_MEMORY | BUSTER_X86_MODE_TABLE_NO_INDEX), BUSTER_X86_SPLAT(0xc0f00000),
        BUSTER_X86_A_AND_B_OR_C);
    const __m512i registers = _mm512_ternarylogic_epi32(extension, operands, BUSTER_X86_SPLAT(0xffff0000), BUSTER_X86_C_SELECTS_A_OVER_B);
    const __m512i bad = _mm512_ternarylogic_epi32(registers, checks, _mm512_and_epi32(operands, forbidden), BUSTER_X86_A_AND_B_OR_C);
    const __m512i rsp_index = _mm512_ternarylogic_epi32(_mm512_xor_epi32(_mm512_and_epi32(operands, BUSTER_X86_SPLAT(0xf0000)), BUSTER_X86_SPLAT(0x40000)),
                                                        mode_fields, BUSTER_X86_SPLAT(BUSTER_X86_FIELD_INDEX), BUSTER_X86_A_OR_NOT_B_AND_C);

    X86Fields result;
    result.canonical = canonical;
    result.prefix = prefix;
    result.displacement = _mm512_ternarylogic_epi32(_mm512_rorv_epi32(displacement, scale), displacement, displacement8, BUSTER_X86_C_SELECTS_A_OVER_B);
    result.presence = presence;
    result.length = length;
    const __m512i bad_or_rsp_index = _mm512_ternarylogic_epi32(bad, _mm512_sub_epi32(rsp_index, one), BUSTER_X86_SPLAT(0x80000000), BUSTER_X86_A_OR_B_AND_C);
    const __m512i bad_high_byte = _mm512_ternarylogic_epi32(bad_or_rsp_index, rex, _mm512_and_epi32(operands, BUSTER_X86_SPLAT(0x4040)), BUSTER_X86_A_OR_B_AND_C);
    result.invalid = _mm512_ternarylogic_epi32(bad_high_byte, wrong_kind, BUSTER_X86_SPLAT(BUSTER_X86_TEMPLATE_NO_MEMORY), BUSTER_X86_A_OR_B_AND_C);
    return result;
}

// Shifts the dword lanes of x up by `lanes` (a constant), filling with zeros.
#define BUSTER_X86_SHIFT_LANES_UP(x, lanes) _mm512_alignr_epi32((x), _mm512_setzero_si512(), 16 - (lanes))

// Loads the 16 bytes at `pointer` into 128-bit lane `lane` (a constant) of `vector`. The load spans the 64 bytes from
// pointer - 16 * lane, but masked-out elements are neither read nor able to fault.
BUSTER_INLINE __m512i x86_load_lane(__m512i vector, uint32_t lane, const uint8_t* pointer)
{
    return _mm512_mask_loadu_epi64(vector, (__mmask8)(3u << (2 * lane)), pointer - 16 * lane);
}

// Encodes the 4 * `quads` instructions at `instructions` (quads is 1, 2 or 4, and a constant), appending their
// machine code at `out`, and returns where it ends. If `offsets` isn't null, stores the offsets (from `output`) of
// the instructions of `offset_lanes` (a bit per instruction). ORs the invalid instructions' lanes into `*invalid`.
//
// Phase II works on instruction 4q + j (quad q, slot j) in dword lane 4j + q, so that moving its fields between a
// quad's records and the group's lanes is a 4x4 transpose of dwords inside each 128-bit lane, both ways: unpacks,
// which on Zen 5 run on any of the four vector pipes (uops.info), where cross-lane permutes would take two of them
// from Phase III's shuffles and compressions.
BUSTER_INLINE uint8_t* encode_group(const X86Instruction* instructions, uint32_t quads, uint8_t* out, const uint8_t* output, uint32_t* offsets,
                                    uint32_t offset_lanes, X86Gather gather, X86Store store, __m512i* invalid)
{
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
            t[j] = (const uint8_t*)&x86_templates[gather == X86_GATHER_INSERTS_FORMS_FROM_RECORDS ? (uint16_t)(slot_forms[j] >> (16 * q)) : quad[j].form];
        }
        if (gather == X86_GATHER_MASKED_LOADS)
        {
            bytes[q] = x86_load_lane(x86_load_lane(x86_load_lane(_mm512_zextsi128_si512(_mm_load_si128((const __m128i*)t[0])), 1, t[1]), 2, t[2]), 3, t[3]);
            layouts[q] = x86_load_lane(x86_load_lane(x86_load_lane(_mm512_zextsi128_si512(_mm_load_si128((const __m128i*)(t[0] + 16))), 1, t[1] + 16), 2, t[2] + 16),
                                       3, t[3] + 16);
        }
        else
        {
            // [bytes 0, layout 0, bytes 1, layout 1] and [bytes 2, layout 2, bytes 3, layout 3].
            const __m512i low = _mm512_inserti64x4(_mm512_castsi256_si512(_mm256_load_si256((const __m256i*)t[0])), _mm256_load_si256((const __m256i*)t[1]), 1);
            const __m512i high = _mm512_inserti64x4(_mm512_castsi256_si512(_mm256_load_si256((const __m256i*)t[2])), _mm256_load_si256((const __m256i*)t[3]), 1);
            bytes[q] = _mm512_shuffle_i64x2(low, high, 0x88);
            layouts[q] = _mm512_shuffle_i64x2(low, high, 0xdd);
        }
    }

    // Each template's checks and flags (its third and fourth dwords), in lane 4j + q.
    const __m512i template_high01 = _mm512_unpackhi_epi32(bytes[0], bytes[1]);
    const __m512i template_high23 = _mm512_unpackhi_epi32(bytes[2], bytes[3]);
    const __m512i checks = _mm512_unpacklo_epi64(template_high01, template_high23);
    const __m512i flags = _mm512_unpackhi_epi64(template_high01, template_high23);

    // II. The lanes of the quads past `quads` hold zeros, which are valid, as are a zeroed copy's padding.
    const X86Fields fields = compute_fields(operands, displacement, extension, flags, checks);
    *invalid = _mm512_or_epi32(*invalid, fields.invalid);

    if (offsets != 0)
    {
        // The lengths in instruction order, then their running sum.
        const __m512i lengths = _mm512_permutexvar_epi32(_mm512_setr_epi32(0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15), fields.length);
        __m512i end = lengths;
        end = _mm512_add_epi32(end, BUSTER_X86_SHIFT_LANES_UP(end, 1));
        end = _mm512_add_epi32(end, BUSTER_X86_SHIFT_LANES_UP(end, 2));
        end = _mm512_add_epi32(end, BUSTER_X86_SHIFT_LANES_UP(end, 4));
        end = _mm512_add_epi32(end, BUSTER_X86_SHIFT_LANES_UP(end, 8));
        const __m512i start = _mm512_add_epi32(_mm512_sub_epi32(end, lengths), BUSTER_X86_SPLAT(out - output));
        _mm512_mask_storeu_epi32(offsets, (__mmask16)offset_lanes, start);
    }

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
    const __m512i presences[4] = {
        _mm512_shuffle_epi32(fields.presence, _MM_PERM_AAAA),
        _mm512_shuffle_epi32(fields.presence, _MM_PERM_BBBB),
        _mm512_shuffle_epi32(fields.presence, _MM_PERM_CCCC),
        _mm512_shuffle_epi32(fields.presence, _MM_PERM_DDDD),
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
    return out;
}

#undef BUSTER_X86_SHIFT_LANES_UP
#undef BUSTER_X86_SPLAT

// The vectorized encoder's loop: `groups` groups of 4 * `quads` instructions per iteration (all constants), then the
// whole groups left, then the last partial group from a zeroed copy, so that no load reads past the instructions.
// Returns the number of bytes, or X86_ENCODER_ERROR_INVALID_INSTRUCTION. Writes nothing past
// output + BUSTER_X86_ENCODER_CAPACITY(count), but may write past the instructions' bytes.
//
// Why a full 64-byte store stays inside that capacity: a quad's store starts after the bytes of the instructions
// before it, at most 15 for each of the first `count` (the zeroed copy's padding adds none), so it ends by
// 15 * count + 64.
BUSTER_INLINE int64_t encode_batches(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint32_t* offsets, uint32_t quads,
                                     uint32_t groups, X86Gather gather, X86Store store)
{
    const uint64_t group_instructions = BUSTER_X86_QUAD_INSTRUCTIONS * quads;
    const uint64_t iteration_instructions = group_instructions * groups;
    const uint32_t group_lanes = (uint32_t)((1ull << group_instructions) - 1);
    __m512i invalid = _mm512_setzero_si512();
    uint8_t* out = output;
    uint64_t i = 0;
    while (i + iteration_instructions <= count)
    {
        // clang keeps this loop rolled: a group's code takes most of the vector registers, and two groups' together
        // spill (unrolled, it's 14% slower on Zen 5).
        for (uint32_t g = 0; g < groups; g += 1)
        {
            out = encode_group(instructions + i, quads, out, output, offsets != 0 ? offsets + i : 0, group_lanes, gather, store, &invalid);
            i += group_instructions;
        }
    }
    while (i + group_instructions <= count)
    {
        out = encode_group(instructions + i, quads, out, output, offsets != 0 ? offsets + i : 0, group_lanes, gather, store, &invalid);
        i += group_instructions;
    }
    if (i < count)
    {
        X86Instruction tail[BUSTER_X86_GROUP_INSTRUCTIONS] __attribute__((aligned(64))) = { 0 };
        memcpy(tail, instructions + i, (count - i) * sizeof(X86Instruction));
        out = encode_group(tail, quads, out, output, offsets != 0 ? offsets + i : 0, (uint32_t)((1ull << (count - i)) - 1), gather, store, &invalid);
    }
    return _mm512_test_epi32_mask(invalid, invalid) != 0 ? X86_ENCODER_ERROR_INVALID_INSTRUCTION : (int64_t)(out - output);
}

// The instantiations, by instructions per iteration, and for the benchmark, with the other gathers and stores. All of
// them need room for BUSTER_X86_ENCODER_CAPACITY(count) bytes. Each has a loop with offsets and one without, so that
// neither tests for them per group.
#define BUSTER_X86_ENCODE_VARIANT(name, quads, groups, gather, store) \
    __attribute__((noinline)) static int64_t name(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint32_t* offsets) \
    { \
        return offsets != 0 ? encode_batches(instructions, count, output, offsets, quads, groups, gather, store) \
                            : encode_batches(instructions, count, output, 0, quads, groups, gather, store); \
    }
BUSTER_X86_ENCODE_VARIANT(x86_encode_4, 1, 1, X86_GATHER_INSERTS, X86_STORE_FULL)
BUSTER_X86_ENCODE_VARIANT(x86_encode_8, 2, 1, X86_GATHER_INSERTS, X86_STORE_FULL)
BUSTER_X86_ENCODE_VARIANT(x86_encode_16, 4, 1, X86_GATHER_INSERTS, X86_STORE_FULL)
BUSTER_X86_ENCODE_VARIANT(x86_encode_32, 4, 2, X86_GATHER_INSERTS, X86_STORE_FULL)
BUSTER_X86_ENCODE_VARIANT(x86_encode_64, 4, 4, X86_GATHER_INSERTS, X86_STORE_FULL)
BUSTER_X86_ENCODE_VARIANT(x86_encode_16_forms_from_records, 4, 1, X86_GATHER_INSERTS_FORMS_FROM_RECORDS, X86_STORE_FULL)
BUSTER_X86_ENCODE_VARIANT(x86_encode_16_masked_loads, 4, 1, X86_GATHER_MASKED_LOADS, X86_STORE_FULL)
BUSTER_X86_ENCODE_VARIANT(x86_encode_16_masked_stores, 4, 1, X86_GATHER_INSERTS, X86_STORE_MASKED)
BUSTER_X86_ENCODE_VARIANT(x86_encode_16_compress_stores, 4, 1, X86_GATHER_INSERTS, X86_STORE_COMPRESS)
#undef BUSTER_X86_ENCODE_VARIANT

// The production entry point: 64 instructions per iteration, which the Zen 5 benchmark found the fastest
// (docs/x86-64-encoder.md has the numbers).
static int64_t x86_encode(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity, uint32_t* offsets)
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
        result = x86_encode_64(instructions, count, output, offsets);
    }
    return result;
}
