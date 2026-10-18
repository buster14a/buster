// The x86-64 encoder: turns a section's selected machine instructions into machine code. An instruction
// (X86Instruction, 16 bytes) is a form (X86Form: the opcode, prefixes, operand roles and field widths) and its
// operands in their encoding roles: the ModRM.reg and ModRM.rm registers, the memory operand's base, index, scale and
// displacement, and the immediate. Files that use it include it directly; every function in it is static.
//
// Entry points: x86_encoder_initialize (builds the templates; call it once before encoding), x86_encode (the
// vectorized encoder), x86_encode_scalar (a plain reference encoder, for testing), x86_validate and
// x86_form_descriptions (the form table, for tests and diagnostics). The output needs room for
// BUSTER_X86_ENCODER_CAPACITY(count) bytes; x86_encode writes nothing past that, but may write past the instructions'
// bytes (its 64-byte stores).
//
// How x86_encode works, a group of BUSTER_X86_GROUP_INSTRUCTIONS (16) instructions at a time:
//   I.   Load. Four instructions fill a ZMM register. Each one's form has a 32-byte template (X86Template, built from
//        the form table by x86_encoder_initialize): the bytes the form always has (prefixes, REX.W, the opcode, a
//        ModRM /digit) at their positions in a 16-byte slot, its flags, and the slot's layout, which says for each
//        position which byte of the instruction's canonical slot goes there.
//   II.  Compute, across the group in the dword lanes of ZMM registers: ModRM, SIB, REX.RXB, the displacement size,
//        which canonical bytes the instruction has, and its length. No branches: the addressing mode's decisions are
//        table lookups (vpermd), the register fields move into place with vpmultishiftqb, and the rest are masks.
//   III. Assemble. An instruction's canonical slot is its immediate and displacement followed by the bytes computed
//        in II. One byte shuffle per 4 instructions moves every canonical byte to its position in the instruction and
//        a vpternlogd adds the template's bytes. Compressing those 4 slots by the bytes the instructions have
//        (vpcompressb) leaves the 4 instructions' machine code, one after the other, which a 64-byte store appends to
//        the output (a masked one, near the end).
// The form decides everything the bytes' positions depend on, so instructions with different forms take the same
// path, and a form with no bytes (X86_FORM_NONE, which zeroed records have) is how a group is padded. The choices are
// Zen 5's: docs/x86-64-encoder.md has the measurements behind them (encode_group's comments say which).
//
// Layout, in this order:
//   - API: registers and addressing modes (BUSTER_X86_MODE_*), X86Instruction, X86EncoderError, the operand,
//     encoding and syntax constants of the form table, BUSTER_X86_FORMS (the form table), X86Form and the entry
//     points' prototypes.
//   - Templates: the canonical slot (BUSTER_X86_CANONICAL_*), the layout and flags constants, X86Template,
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
#define BUSTER_X86_ENCODER_CAPACITY(count) ((uint64_t)(count) * BUSTER_X86_MAX_INSTRUCTION_BYTES)
// Offsets are 32-bit, so a call encodes at most this many instructions.
#define BUSTER_X86_ENCODER_MAX_INSTRUCTIONS (UINT32_MAX / BUSTER_X86_MAX_INSTRUCTION_BYTES)

// A register operand is the register's number (general purpose: RAX = 0 ... R15 = 15; XMM0 = 0 ... XMM15 = 15) or,
// for AH, CH, DH and BH, 4 to 7 with BUSTER_X86_REGISTER_HIGH_BYTE. With a byte form, 4 to 7 alone are SPL, BPL, SIL
// and DIL, which need a REX prefix; the high byte registers can't have one.
#define BUSTER_X86_REGISTER_NUMBER 0x0f
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
    // An X86Form.
    uint16_t form;
    // Zero, for now (VEX.vvvv and the EVEX decorations).
    uint16_t reserved;
};
_Static_assert(sizeof(X86Instruction) == 16, "four instructions fill a ZMM register");

typedef enum X86EncoderError
{
    // An instruction's operands can't be encoded (x86_validate finds which).
    X86_ENCODER_ERROR_INVALID_INSTRUCTION = -1,
    X86_ENCODER_ERROR_OUTPUT_TOO_SMALL = -2,
    X86_ENCODER_ERROR_TOO_MANY_INSTRUCTIONS = -3,
} X86EncoderError;

// The form table's operand roles: a register class, and for ModRM.rm whether it can be a register, memory, or both
// (as Intel's opcode maps write them: G a general register in ModRM.reg, E a general register or memory in
// ModRM.rm, V an XMM register, XM an XMM register or memory, M only memory). For a +r form, the register is the rm
// operand.
#define BUSTER_X86_OPERAND_CLASS 0x07
#define BUSTER_X86_CLASS_NONE 0
#define BUSTER_X86_CLASS_GPR8 1
#define BUSTER_X86_CLASS_GPR16 2
#define BUSTER_X86_CLASS_GPR32 3
#define BUSTER_X86_CLASS_GPR64 4
#define BUSTER_X86_CLASS_XMM 5
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

// The order of a form's operands in Intel syntax, with its implicit ones (for printing and tests; the encoding
// doesn't depend on it). ACC is AL, AX, EAX or RAX; MOFFS is a 64-bit absolute address.
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
    X(CMOV##CC##_R32_RM32, "cmov" cc, 0, 2, 0x0f40 + K, R, 0, 0, 0, GD, ED, 4, REG_RM) \
    X(CMOV##CC##_R64_RM64, "cmov" cc, 0, 2, 0x0f40 + K, R, 0, 0, 1, GQ, EQ, 8, REG_RM)

#define BUSTER_X86_BIT_TEST_FORMS(X, N, m, opcode, K) \
    X(N##_RM32_R32, m, 0, 2, 0x0f00 + opcode, R, 0, 0, 0, GD, ED, 4, RM_REG) \
    X(N##_RM64_R64, m, 0, 2, 0x0f00 + opcode, R, 0, 0, 1, GQ, EQ, 8, RM_REG) \
    X(N##_RM32_IMM8, m, 0, 2, 0x0fba, D##K, 1, 0, 0, NONE, ED, 4, RM_IMM) \
    X(N##_RM64_IMM8, m, 0, 2, 0x0fba, D##K, 1, 0, 1, NONE, EQ, 8, RM_IMM)

// A general register from r/m, in 32- and 64-bit forms.
#define BUSTER_X86_R_RM_FORMS(X, N, m, prefixes, opcode) \
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

#define BUSTER_X86_FORMS(X) \
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
    X(CALL_REL32, "call", 0, 1, 0xe8, NO, 0, 4, 0, NONE, NONE, 0, REL) \
    X(CALL_RM64, "call", 0, 1, 0xff, D2, 0, 0, 0, NONE, EQ, 8, RM) \
    X(JMP_REL8, "jmp", 0, 1, 0xeb, NO, 0, 1, 0, NONE, NONE, 0, REL) \
    X(JMP_REL32, "jmp", 0, 1, 0xe9, NO, 0, 4, 0, NONE, NONE, 0, REL) \
    X(JMP_RM64, "jmp", 0, 1, 0xff, D4, 0, 0, 0, NONE, EQ, 8, RM) \
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
    X(PINSRQ_X_RM64_IMM8, "pinsrq", 0x66, 3, 0x0f3a22, R, 1, 0, 1, V, EQ, 8, REG_RM_IMM)

#define BUSTER_X86_FORM_ENUM(name, ...) X86_FORM_##name,
typedef enum X86Form
{
    BUSTER_X86_FORMS(BUSTER_X86_FORM_ENUM)
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
    uint8_t rex_w;
    uint8_t reg;
    uint8_t rm;
    uint8_t operand_bytes;
    uint8_t syntax;
};

#define BUSTER_X86_FORM_DESCRIPTION(name, mnemonic, prefixes, opcode_bytes, opcode, encoding, immediate, relative, w, reg, rm, \
                                    operand_bytes, syntax) \
    { mnemonic, opcode, prefixes, opcode_bytes, BUSTER_X86_ENCODING_##encoding, immediate, relative, w, BUSTER_X86_OPERAND_##reg, \
      BUSTER_X86_OPERAND_##rm, operand_bytes, X86_SYNTAX_##syntax },
static const X86FormDescription x86_form_descriptions[X86_FORM_COUNT] = { BUSTER_X86_FORMS(BUSTER_X86_FORM_DESCRIPTION) };
#undef BUSTER_X86_FORM_DESCRIPTION

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

// ---------------------------------------------------------------------------------------------------------------------
// Templates

// An instruction's canonical slot: the bytes of its fields, in a fixed order. The record's first 8 bytes are the
// immediate and displacement as they are, and compute_fields makes the rest. A template's layout picks from these.
#define BUSTER_X86_CANONICAL_IMMEDIATE 0
#define BUSTER_X86_CANONICAL_DISPLACEMENT 4
#define BUSTER_X86_CANONICAL_MODRM 8
#define BUSTER_X86_CANONICAL_SIB 9
// REX.R, REX.X and REX.B; the template has the rest of the REX byte.
#define BUSTER_X86_CANONICAL_REX 10
// The rm register's low 3 bits, which a +r form adds to its last opcode byte.
#define BUSTER_X86_CANONICAL_OPCODE_REGISTER 11
// The layout of a position that only has the template's byte: vpshufb makes it zero (bit 7), and its vpshufbitqmb
// index is presence bit 0 (the first immediate byte's, always set).
#define BUSTER_X86_LAYOUT_STATIC 0x80
// The layout of a position that isn't part of the instruction: its vpshufbitqmb index is presence bit 63 (always
// clear), and what vpshufb puts there doesn't matter.
#define BUSTER_X86_LAYOUT_ABSENT 0x3f
// The canonical bytes that every instruction whose layout uses them has: the immediate, ModRM and the opcode
// register. Whether the displacement, SIB and REX are there depends on the operands.
#define BUSTER_X86_CANONICAL_ALWAYS_PRESENT 0x090f

// A template's flags: the fourth dword of its bytes (bytes[12-15]). A form's own bytes all come before position 8
// (prefixes, REX, opcode and ModRM end by position 6), and Phase III only takes the template's bytes there.
#define BUSTER_X86_TEMPLATE_FLAGS_OFFSET 12
#define BUSTER_X86_TEMPLATE_STATIC_POSITIONS 8
// How many bytes the form always has, besides REX: prefixes, opcode, ModRM, immediate and a static displacement.
#define BUSTER_X86_TEMPLATE_STATIC_LENGTH 0x0f
// The presence bits (canonical bytes 4-7) of a displacement field the form always has, as many bytes of it as its
// layout uses: a relative branch's displacement, or a 64-bit immediate's high half.
#define BUSTER_X86_TEMPLATE_STATIC_DISPLACEMENT 0xf0
// REX.W: the instruction always has a REX prefix. Bit 19, next to REX.RXB in the canonical dword, so that one test of
// both says whether there's a REX prefix.
#define BUSTER_X86_TEMPLATE_REX_ALWAYS 0x80000
// ModRM.reg or the rm register is a byte register, so 4 to 7 are SPL to DIL (with a REX prefix) or AH to BH.
#define BUSTER_X86_TEMPLATE_BYTE_REG 0x100000
#define BUSTER_X86_TEMPLATE_BYTE_RM 0x200000

// A form's template. Positions follow the instruction's order: prefixes, REX, opcode, ModRM, SIB, displacement,
// immediate. A position's byte is bytes[position] | canonical[layout[position]] (or just bytes[position], for
// BUSTER_X86_LAYOUT_STATIC), and it's part of the instruction if that canonical byte is.
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
static X86Template x86_template_storage[BUSTER_X86_TEMPLATE_PADDING + X86_FORM_COUNT + BUSTER_X86_TEMPLATE_PADDING] __attribute__((aligned(64)));
static X86Template* const x86_templates = x86_template_storage + BUSTER_X86_TEMPLATE_PADDING;
static bool x86_encoder_initialized;

// Builds a form's template. Returns false if its longest encoding wouldn't fit in a slot (15 bytes).
static bool build_template(const X86FormDescription* form, X86Template* template)
{
    uint32_t position = 0;
    uint32_t static_length = 0;
    // The position after the last of the form's own bytes.
    uint32_t last_static = 0;
    uint32_t flags = 0;
    memset(template->bytes, 0, sizeof(template->bytes));
    memset(template->layout, BUSTER_X86_LAYOUT_ABSENT, sizeof(template->layout));
    if (form->opcode_bytes != 0)
    {
        for (uint32_t prefixes = form->prefixes; prefixes != 0; prefixes >>= 8)
        {
            template->bytes[position] = (uint8_t)prefixes;
            template->layout[position] = BUSTER_X86_LAYOUT_STATIC;
            position += 1;
            static_length += 1;
            last_static = position;
        }

        template->bytes[position] = (uint8_t)(0x40 | (form->rex_w << 3));
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
    flags |= form->rex_w ? BUSTER_X86_TEMPLATE_REX_ALWAYS : 0;
    flags |= (form->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8 ? BUSTER_X86_TEMPLATE_BYTE_REG : 0;
    flags |= (form->rm & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8 ? BUSTER_X86_TEMPLATE_BYTE_RM : 0;
    flags |= form->relative_bytes != 0 || form->immediate_bytes > 4 ? BUSTER_X86_TEMPLATE_STATIC_DISPLACEMENT : 0;
    memcpy(&template->bytes[BUSTER_X86_TEMPLATE_FLAGS_OFFSET], &flags, sizeof(flags));
    // A static displacement shares the displacement field with a memory operand's, and the form's own bytes have to
    // come before the flags.
    const bool static_displacement_conflict = (flags & BUSTER_X86_TEMPLATE_STATIC_DISPLACEMENT) && (form->rm & BUSTER_X86_OPERAND_MEMORY);
    return position <= BUSTER_X86_MAX_INSTRUCTION_BYTES && static_length <= BUSTER_X86_TEMPLATE_STATIC_LENGTH && !static_displacement_conflict &&
           last_static < BUSTER_X86_TEMPLATE_STATIC_POSITIONS;
}

static bool x86_encoder_initialize(void)
{
    bool result = true;
    for (uint32_t form = 0; form < X86_FORM_COUNT; form += 1)
    {
        result = build_template(&x86_form_descriptions[form], &x86_templates[form]) && result;
    }
    x86_encoder_initialized = result;
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Validation

static bool register_is_valid(uint8_t value, uint8_t operand, bool memory_part)
{
    const uint8_t class = operand & BUSTER_X86_OPERAND_CLASS;
    const bool high_byte = (value & BUSTER_X86_REGISTER_HIGH_BYTE) != 0;
    const uint8_t number = value & BUSTER_X86_REGISTER_NUMBER;
    bool result = (value & ~(BUSTER_X86_REGISTER_NUMBER | BUSTER_X86_REGISTER_HIGH_BYTE)) == 0;
    if (high_byte)
    {
        result = result && !memory_part && class == BUSTER_X86_CLASS_GPR8 && number >= 4 && number <= 7;
    }
    return result;
}

// Whether the instruction can be encoded: a valid form, operands the form has and no others, a consistent addressing
// mode, and immediates and relative displacements that fit their fields.
static bool x86_instruction_is_valid(const X86Instruction* instruction)
{
    bool result = instruction->form < X86_FORM_COUNT && instruction->reserved == 0;
    if (result)
    {
        const X86FormDescription* form = &x86_form_descriptions[instruction->form];
        const uint8_t mode = instruction->mode;
        const bool memory = (mode & BUSTER_X86_MODE_MEMORY) != 0;
        const bool base = (mode & BUSTER_X86_MODE_BASE) != 0;
        const bool index = (mode & BUSTER_X86_MODE_INDEX) != 0;
        const bool rip = (mode & BUSTER_X86_MODE_RIP) != 0;
        const bool has_modrm = form->encoding == BUSTER_X86_ENCODING_R || (form->encoding & BUSTER_X86_ENCODING_DIGIT);

        // ModRM.reg
        if (form->encoding == BUSTER_X86_ENCODING_R)
        {
            result = result && register_is_valid(instruction->reg, form->reg, false);
        }
        else
        {
            result = result && instruction->reg == 0;
        }

        // ModRM.rm or the +r register, and the addressing mode.
        if (form->rm == BUSTER_X86_OPERAND_NONE)
        {
            result = result && instruction->rm == 0 && instruction->index == 0 && mode == 0;
        }
        else if (!memory)
        {
            result = result && mode == 0 && instruction->index == 0 && (form->rm & BUSTER_X86_OPERAND_REGISTER);
            result = result && register_is_valid(instruction->rm, form->rm, false);
        }
        else
        {
            result = result && has_modrm && (form->rm & BUSTER_X86_OPERAND_MEMORY);
            result = result && (mode & ~(BUSTER_X86_MODE_SCALE | BUSTER_X86_MODE_MEMORY | BUSTER_X86_MODE_BASE | BUSTER_X86_MODE_INDEX | BUSTER_X86_MODE_RIP)) == 0;
            result = result && !(rip && (base || index));
            result = result && (index || (mode & BUSTER_X86_MODE_SCALE) == 0);
            result = result && (base ? register_is_valid(instruction->rm, BUSTER_X86_OPERAND_GQ, true) : instruction->rm == 0);
            // RSP can't be an index: its SIB index field means "none".
            result = result && (index ? register_is_valid(instruction->index, BUSTER_X86_OPERAND_GQ, true) && instruction->index != X86_REGISTER_RSP
                                      : instruction->index == 0);
        }

        // A high byte register can't be in an instruction with a REX prefix.
        const uint8_t rm_register = memory ? 0 : instruction->rm;
        const bool high_byte = ((instruction->reg | rm_register) & BUSTER_X86_REGISTER_HIGH_BYTE) != 0;
        const bool byte_reg = (form->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8;
        const bool byte_rm = (form->rm & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8;
        const bool rex = form->rex_w || ((instruction->reg | instruction->rm | instruction->index) & 8) ||
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
    const X86FormDescription* form = &x86_form_descriptions[instruction->form];
    const uint8_t mode = instruction->mode;
    const bool memory = (mode & BUSTER_X86_MODE_MEMORY) != 0;
    const bool base = (mode & BUSTER_X86_MODE_BASE) != 0;
    const bool index = (mode & BUSTER_X86_MODE_INDEX) != 0;
    const bool rip = (mode & BUSTER_X86_MODE_RIP) != 0;
    const uint8_t reg = instruction->reg & BUSTER_X86_REGISTER_NUMBER;
    const uint8_t rm = instruction->rm & BUSTER_X86_REGISTER_NUMBER;
    const uint8_t index_register = instruction->index & BUSTER_X86_REGISTER_NUMBER;
    const bool has_modrm = form->encoding == BUSTER_X86_ENCODING_R || (form->encoding & BUSTER_X86_ENCODING_DIGIT);
    uint32_t length = 0;

    if (form->opcode_bytes != 0)
    {
        for (uint32_t prefixes = form->prefixes; prefixes != 0; prefixes >>= 8)
        {
            out[length] = (uint8_t)prefixes;
            length += 1;
        }

        // REX: W, then the high bits of ModRM.reg, the index and ModRM.rm (or the base, or the +r register).
        uint8_t rex = 0x40;
        bool needs_rex = form->rex_w;
        if (form->rex_w)
        {
            rex |= 0x08;
        }
        if (reg & 8)
        {
            rex |= 0x04;
        }
        if (memory && index && (index_register & 8))
        {
            rex |= 0x02;
        }
        if ((!memory || base) && (rm & 8))
        {
            rex |= 0x01;
        }
        needs_rex = needs_rex || rex != 0x40;
        // SPL, BPL, SIL and DIL.
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

        if (has_modrm)
        {
            const uint8_t reg_field = (form->encoding & BUSTER_X86_ENCODING_DIGIT) ? (form->encoding & 7) : (reg & 7);
            uint8_t mod;
            uint8_t rm_field;
            bool sib = false;
            uint32_t displacement_bytes = 0;
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
                const int32_t displacement = instruction->displacement;
                rm_field = (index || (rm & 7) == 4) ? 4 : (rm & 7);
                sib = index || (rm & 7) == 4;
                if (displacement == 0 && (rm & 7) != 5)
                {
                    mod = 0;
                }
                else if (displacement >= -128 && displacement <= 127)
                {
                    mod = 1;
                    displacement_bytes = 1;
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
                out[length] = (uint8_t)((uint32_t)instruction->displacement >> (8 * i));
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
    // Bit c is set if the instruction has canonical byte c (when its layout uses it).
    __m512i presence;
    __m512i length;
    // The instructions whose operands the encoding can't represent.
    __mmask16 invalid;
};

#define BUSTER_X86_SPLAT(value) _mm512_set1_epi32((int)(value))

// The canonical dword's fields (Phase II builds them where they go: ModRM in bits 0-7, SIB in 8-15, REX.RXB in 16-18
// and the opcode register in 24-26), as masks.
#define BUSTER_X86_FIELD_RM 0x00000007
#define BUSTER_X86_FIELD_REG 0x00000038
#define BUSTER_X86_FIELD_BASE 0x00000700
#define BUSTER_X86_FIELD_INDEX 0x00003800
#define BUSTER_X86_FIELD_SCALE 0x0000c000
#define BUSTER_X86_FIELD_REX_B 0x00010000
#define BUSTER_X86_FIELD_REX_X 0x00020000
#define BUSTER_X86_FIELD_REX_R 0x00040000
#define BUSTER_X86_FIELD_OPCODE_REGISTER 0x07000000
#define BUSTER_X86_FIELDS_ALWAYS (BUSTER_X86_FIELD_REG | BUSTER_X86_FIELD_REX_R | BUSTER_X86_FIELD_OPCODE_REGISTER)
// The presence table's flags, above its presence bits.
#define BUSTER_X86_MODE_TABLE_BASE 0x80000000u
#define BUSTER_X86_MODE_TABLE_INVALID 0x40000000u
#define BUSTER_X86_MODE_TABLE_MEMORY 0x20000000u

// What each addressing mode decides, indexed (by vpermd) with its memory, base, index and RIP bits: operands >> 26.
// x86_mode_fields says which canonical bits come from the operands; x86_mode_constants, which bits are fixed (mod 11
// for a register, rm 100 for a SIB byte or 101 for RIP, a SIB base of 101 or index of 100 for none); and
// x86_mode_presence, the canonical bytes the mode always has (a SIB byte, a 32-bit displacement) and its flags. A
// mode with a base also has a displacement and maybe a SIB byte that depend on the operands, which compute_fields
// works out. The other combinations of the bits are invalid.
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
    [0] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT,
    [1] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | 0x2f0 | BUSTER_X86_MODE_TABLE_MEMORY,
    [2] = BUSTER_X86_MODE_TABLE_INVALID,
    [3] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | BUSTER_X86_MODE_TABLE_BASE | BUSTER_X86_MODE_TABLE_MEMORY,
    [4] = BUSTER_X86_MODE_TABLE_INVALID,
    [5] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | 0x2f0 | BUSTER_X86_MODE_TABLE_MEMORY,
    [6] = BUSTER_X86_MODE_TABLE_INVALID,
    [7] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | 0x200 | BUSTER_X86_MODE_TABLE_BASE | BUSTER_X86_MODE_TABLE_MEMORY,
    [8] = BUSTER_X86_MODE_TABLE_INVALID,
    [9] = BUSTER_X86_CANONICAL_ALWAYS_PRESENT | 0xf0 | BUSTER_X86_MODE_TABLE_MEMORY,
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
// SIB.index and its bit 3 in REX.X; the third, the scale in SIB.scale and reg's bit 3 in REX.R.
#define BUSTER_X86_FIELDS_RM_CONTROL 0x282b2828080b0808ll
#define BUSTER_X86_FIELDS_REG_INDEX_CONTROL 0x00322d1d00120d3dll
#define BUSTER_X86_FIELDS_SCALE_REX_R_CONTROL 0x0021320000011200ll
#define BUSTER_X86_FIELDS_FROM_RM (BUSTER_X86_FIELD_RM | BUSTER_X86_FIELD_BASE | BUSTER_X86_FIELD_REX_B | BUSTER_X86_FIELD_OPCODE_REGISTER)
#define BUSTER_X86_FIELDS_FROM_SCALE_REX_R (BUSTER_X86_FIELD_SCALE | BUSTER_X86_FIELD_REX_R)

// vpternlogd truth tables, of operands A, B and C.
#define BUSTER_X86_A_OR_B_AND_C 0xf8
#define BUSTER_X86_A_AND_B_OR_C 0xea
#define BUSTER_X86_A_OR_B_THEN_AND_C 0xa8
#define BUSTER_X86_C_SELECTS_A_OVER_B 0xe4
#define BUSTER_X86_A_AND_B_AND_NOT_C 0x40
#define BUSTER_X86_A_AND_NOT_B_AND_C 0x20
#define BUSTER_X86_A_OR_NOT_B_AND_C 0xf2
#define BUSTER_X86_A_AND_NOT_B_OR_C 0xb0
#define BUSTER_X86_A_OR_B_OR_C 0xfe
#define BUSTER_X86_NOT_A_OR_B_THEN_AND_C 0x8a

// Phase II. `operands` is each record's third dword (reg | rm << 8 | index << 16 | mode << 24), `displacement` its
// second and `flags` its template's flags.
//
// On Zen 5 (uops.info), compares into mask registers, permutes and broadcasts run on two of the four vector pipes,
// the ones Phase III's shuffles and compressions need, and moving a mask register from a general one (or reloading a
// spilled one) takes two µops there; ALU operations, vpternlogd and vpmovm2d run on all four, and vpmultishiftqb on
// two others. So the addressing mode's decisions are table lookups, the register fields move with three
// vpmultishiftqb, and the four compares the displacement needs become vectors right away, for vpternlogd to combine.
BUSTER_INLINE X86Fields compute_fields(__m512i operands, __m512i displacement, __m512i flags)
{
    const __m512i mode = _mm512_srli_epi32(operands, 26);
    const __m512i mode_fields = _mm512_permutexvar_epi32(mode, _mm512_load_si512(x86_mode_fields));
    const __m512i mode_constants = _mm512_permutexvar_epi32(mode, _mm512_load_si512(x86_mode_constants));
    const __m512i mode_presence = _mm512_permutexvar_epi32(mode, _mm512_load_si512(x86_mode_presence));

    // All the register fields in place, then those the mode takes, and its constants.
    const __m512i from_rm = _mm512_multishift_epi64_epi8(_mm512_set1_epi64(BUSTER_X86_FIELDS_RM_CONTROL), operands);
    const __m512i from_reg_index = _mm512_multishift_epi64_epi8(_mm512_set1_epi64(BUSTER_X86_FIELDS_REG_INDEX_CONTROL), operands);
    const __m512i from_scale_rex_r = _mm512_multishift_epi64_epi8(_mm512_set1_epi64(BUSTER_X86_FIELDS_SCALE_REX_R_CONTROL), operands);
    __m512i fields = _mm512_ternarylogic_epi32(from_rm, from_reg_index, BUSTER_X86_SPLAT(BUSTER_X86_FIELDS_FROM_RM), BUSTER_X86_C_SELECTS_A_OVER_B);
    fields = _mm512_ternarylogic_epi32(from_scale_rex_r, fields, BUSTER_X86_SPLAT(BUSTER_X86_FIELDS_FROM_SCALE_REX_R), BUSTER_X86_C_SELECTS_A_OVER_B);
    __m512i canonical = _mm512_ternarylogic_epi32(fields, mode_fields, mode_constants, BUSTER_X86_A_AND_B_OR_C);

    // After a base, the displacement is none if it's zero (except after RBP and R13, whose ModRM.mod 00 means
    // something else, so they take an 8-bit zero), 8-bit if it fits and 32-bit otherwise; and RSP and R12 need a SIB
    // byte. All ones where each holds: the base bit, shifted to the top and back; and zero tests, which for a value x
    // below 2^31 are (x - 1) >> 31, and otherwise ~(x | -x) >> 31. Each takes up to four operations of a cycle, where
    // a compare and vpmovm2d would take eight cycles on the shuffle pipes.
    const __m512i one = BUSTER_X86_SPLAT(1);
    const __m512i base = _mm512_srai_epi32(_mm512_slli_epi32(operands, 31 - 26 - 1), 31);
    const __m512i rm_bits = _mm512_and_epi32(operands, BUSTER_X86_SPLAT(0x700));
    const __m512i rm_4 = _mm512_srai_epi32(_mm512_sub_epi32(_mm512_xor_epi32(rm_bits, BUSTER_X86_SPLAT(4 << 8)), one), 31);
    const __m512i rm_5 = _mm512_srai_epi32(_mm512_sub_epi32(_mm512_xor_epi32(rm_bits, BUSTER_X86_SPLAT(5 << 8)), one), 31);
    const __m512i nonzero = _mm512_srai_epi32(_mm512_or_epi32(displacement, _mm512_sub_epi32(_mm512_setzero_si512(), displacement)), 31);
    const __m512i zero = _mm512_ternarylogic_epi32(nonzero, nonzero, nonzero, 0x0f);
    const __m512i fits8 = _mm512_srai_epi32(_mm512_sub_epi32(_mm512_srli_epi32(_mm512_add_epi32(displacement, BUSTER_X86_SPLAT(128)), 8), one), 31);
    const __m512i no_displacement = _mm512_ternarylogic_epi32(base, zero, rm_5, BUSTER_X86_A_AND_B_AND_NOT_C);
    const __m512i displacement8 = _mm512_ternarylogic_epi32(base, fits8, no_displacement, BUSTER_X86_A_AND_B_AND_NOT_C);
    const __m512i base_displacement32 = _mm512_andnot_epi32(fits8, base);
    const __m512i base_sib = _mm512_and_epi32(base, rm_4);
    // ModRM.mod: 01 for an 8-bit displacement, 10 for a 32-bit one after a base.
    canonical = _mm512_ternarylogic_epi32(canonical, displacement8, BUSTER_X86_SPLAT(0x40), BUSTER_X86_A_OR_B_AND_C);
    canonical = _mm512_ternarylogic_epi32(canonical, base_displacement32, BUSTER_X86_SPLAT(0x80), BUSTER_X86_A_OR_B_AND_C);

    // REX: for REX.RXB or W (REX_ALWAYS, at bit 19), and for SPL, BPL, SIL and DIL: 4 to 7 without the high byte flag,
    // in a byte operand. low_reg and low_rm are zero for those: (register & 0xfc) ^ 4, or'ed with the byte form flag's
    // absence, and for rm, with the memory flag (bit 26, at 18 after the shift).
    //
    // Both are zero tests of values below 2^31, which (x - 1) >> 31 does in two operations of a cycle each (a compare
    // into a mask register takes six, on the busier pipes): rex_needed's bit 31 is set if rex_bits isn't zero or
    // low_byte is.
    const __m512i rex_bits = _mm512_and_epi32(_mm512_or_epi32(canonical, flags), BUSTER_X86_SPLAT(0xf0000));
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

    // Which canonical bytes the instruction has: the mode's (the always present ones among them), a static
    // displacement's (bits 4-7 of the flags; their bits 0-3 fall on the immediate's, which are always set), and those
    // that depend on the operands.
    __m512i presence = _mm512_ternarylogic_epi32(mode_presence, flags, BUSTER_X86_SPLAT(0xbff), BUSTER_X86_A_OR_B_THEN_AND_C);
    presence = _mm512_ternarylogic_epi32(presence, displacement8, BUSTER_X86_SPLAT(0x10), BUSTER_X86_A_OR_B_AND_C);
    presence = _mm512_ternarylogic_epi32(presence, base_displacement32, BUSTER_X86_SPLAT(0xf0), BUSTER_X86_A_OR_B_AND_C);
    presence = _mm512_ternarylogic_epi32(presence, base_sib, BUSTER_X86_SPLAT(0x200), BUSTER_X86_A_OR_B_AND_C);
    presence = _mm512_or_epi32(presence, rex_presence);

    // The length: the form's static bytes and the displacement, SIB and REX bytes the operands add.
    const __m512i dynamic = _mm512_ternarylogic_epi32(presence, flags, BUSTER_X86_SPLAT(0x6f0), BUSTER_X86_A_AND_NOT_B_AND_C);
    const __m512i length = _mm512_add_epi32(_mm512_popcnt_epi32(dynamic), _mm512_and_epi32(flags, BUSTER_X86_SPLAT(BUSTER_X86_TEMPLATE_STATIC_LENGTH)));

    // Operands the encoding can't represent: an invalid addressing mode; register bits that aren't a register number
    // or the high byte flag (or the high byte flag on the index), and unknown mode bits; RSP as an index (zero in
    // rsp_index: the index's low nibble ^ 4, or'ed with the index field's absence); and a high byte register outside
    // a byte form (bit 6 of reg, without BYTE_REG shifted there), as a memory operand's base (bit 14, rm's flag, with
    // BYTE_RM's absence or the memory flag shifted there), or with a REX prefix.
    const __m512i unknown = _mm512_ternarylogic_epi32(_mm512_and_epi32(operands, BUSTER_X86_SPLAT(0xc0f0b0b0)), mode_presence,
                                                      BUSTER_X86_SPLAT(BUSTER_X86_MODE_TABLE_INVALID), BUSTER_X86_A_OR_B_AND_C);
    const __m512i high_reg = _mm512_ternarylogic_epi32(operands, _mm512_srli_epi32(flags, 14), BUSTER_X86_SPLAT(0x40), BUSTER_X86_A_AND_NOT_B_AND_C);
    const __m512i high_rm = _mm512_and_epi32(_mm512_ternarylogic_epi32(operands, _mm512_srli_epi32(flags, 7), _mm512_srli_epi32(mode_presence, 15), BUSTER_X86_A_AND_NOT_B_OR_C),
                                             BUSTER_X86_SPLAT(0x4000));
    const __m512i bad = _mm512_ternarylogic_epi32(unknown, high_reg, high_rm, BUSTER_X86_A_OR_B_OR_C);
    const __m512i rsp_index = _mm512_ternarylogic_epi32(_mm512_xor_epi32(_mm512_and_epi32(operands, BUSTER_X86_SPLAT(0xf0000)), BUSTER_X86_SPLAT(0x40000)),
                                                        mode_fields, BUSTER_X86_SPLAT(BUSTER_X86_FIELD_INDEX), BUSTER_X86_A_OR_NOT_B_AND_C);

    X86Fields result;
    result.canonical = canonical;
    result.presence = presence;
    result.length = length;
    const __m512i bad_or_high_with_rex = _mm512_ternarylogic_epi32(bad, rex, _mm512_and_epi32(operands, BUSTER_X86_SPLAT(0x4040)), BUSTER_X86_A_OR_B_AND_C);
    result.invalid = (__mmask16)(_mm512_test_epi32_mask(bad_or_high_with_rex, bad_or_high_with_rex) | _mm512_testn_epi32_mask(rsp_index, rsp_index));
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
// the instructions of `offset_lanes` (a bit per instruction). Adds the invalid instructions to `*invalid`.
//
// Phase II works on instruction 4q + j (quad q, slot j) in dword lane 4j + q, so that moving its fields between a
// quad's records and the group's lanes is a 4x4 transpose of dwords inside each 128-bit lane, both ways: unpacks,
// which on Zen 5 run on any of the four vector pipes (uops.info), where cross-lane permutes would take two of them
// from Phase III's shuffles and compressions.
BUSTER_INLINE uint8_t* encode_group(const X86Instruction* instructions, uint32_t quads, uint8_t* out, const uint8_t* output, uint32_t* offsets,
                                    uint32_t offset_lanes, X86Gather gather, X86Store store, __mmask16* invalid)
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

    // Each record's immediate, displacement (its second dword), operands (its third) and form (its fourth), in lane
    // 4j + q: a 4x4 transpose of dwords inside each 128-bit lane.
    const __m512i low01 = _mm512_unpacklo_epi32(records[0], records[1]);
    const __m512i low23 = _mm512_unpacklo_epi32(records[2], records[3]);
    const __m512i high01 = _mm512_unpackhi_epi32(records[0], records[1]);
    const __m512i high23 = _mm512_unpackhi_epi32(records[2], records[3]);
    const __m512i immediate = _mm512_unpacklo_epi64(low01, low23);
    const __m512i displacement = _mm512_unpackhi_epi64(low01, low23);
    const __m512i operands = _mm512_unpacklo_epi64(high01, high23);

    // With X86_GATHER_INSERTS_FORMS_FROM_RECORDS, each template's offset in x86_templates (form * 32, which fits in 16
    // bits) as words in lane order, four to a general register: slot j's for quads 0-3.
    uint64_t slot_offsets[4] = { 0 };
    if (gather == X86_GATHER_INSERTS_FORMS_FROM_RECORDS)
    {
        const __m256i template_offsets = _mm512_cvtepi32_epi16(_mm512_slli_epi32(_mm512_unpackhi_epi64(high01, high23), 5));
        slot_offsets[0] = (uint64_t)_mm256_extract_epi64(template_offsets, 0);
        slot_offsets[1] = (uint64_t)_mm256_extract_epi64(template_offsets, 1);
        slot_offsets[2] = (uint64_t)_mm256_extract_epi64(template_offsets, 2);
        slot_offsets[3] = (uint64_t)_mm256_extract_epi64(template_offsets, 3);
    }

    for (uint32_t q = 0; q < quads; q += 1)
    {
        const X86Instruction* quad = instructions + BUSTER_X86_QUAD_INSTRUCTIONS * q;
        const uint8_t* t[BUSTER_X86_QUAD_INSTRUCTIONS];
        for (uint32_t j = 0; j < BUSTER_X86_QUAD_INSTRUCTIONS; j += 1)
        {
            t[j] = gather == X86_GATHER_INSERTS_FORMS_FROM_RECORDS ? (const uint8_t*)x86_templates + (uint16_t)(slot_offsets[j] >> (16 * q))
                                                                   : (const uint8_t*)&x86_templates[quad[j].form];
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

    // Each template's flags (its fourth dword), in lane 4j + q.
    const __m512i flags = _mm512_unpackhi_epi64(_mm512_unpackhi_epi32(bytes[0], bytes[1]), _mm512_unpackhi_epi32(bytes[2], bytes[3]));

    // II. The lanes of the quads past `quads` hold zeros, which are valid; they're left out anyway.
    const X86Fields fields = compute_fields(operands, displacement, flags);
    const __mmask16 active = quads == 4 ? 0xffff : quads == 2 ? 0x3333 : 0x1111;
    *invalid |= (__mmask16)(fields.invalid & active);

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

    // III. Each quad's canonical slots: the immediate, the displacement, and canonical bytes 8-11 (bytes 12-15 repeat
    // them; no layout uses them). And its presence bits, in both quadwords of each slot, for vpshufbitqmb to pick from by
    // the layout.
    const __m512i immediate_displacement_low = _mm512_unpacklo_epi32(immediate, displacement);
    const __m512i immediate_displacement_high = _mm512_unpackhi_epi32(immediate, displacement);
    const __m512i canonical_low = _mm512_unpacklo_epi32(fields.canonical, fields.canonical);
    const __m512i canonical_high = _mm512_unpackhi_epi32(fields.canonical, fields.canonical);
    const __m512i presence_low = _mm512_unpacklo_epi32(fields.presence, _mm512_setzero_si512());
    const __m512i presence_high = _mm512_unpackhi_epi32(fields.presence, _mm512_setzero_si512());
    const __m512i canonicals[4] = {
        _mm512_unpacklo_epi64(immediate_displacement_low, canonical_low),
        _mm512_unpackhi_epi64(immediate_displacement_low, canonical_low),
        _mm512_unpacklo_epi64(immediate_displacement_high, canonical_high),
        _mm512_unpackhi_epi64(immediate_displacement_high, canonical_high),
    };
    const __m512i presences[4] = {
        _mm512_unpacklo_epi64(presence_low, presence_low),
        _mm512_unpackhi_epi64(presence_low, presence_low),
        _mm512_unpacklo_epi64(presence_high, presence_high),
        _mm512_unpackhi_epi64(presence_high, presence_high),
    };
    for (uint32_t q = 0; q < quads; q += 1)
    {
        const __m512i canonical = canonicals[q];
        const __m512i presence = presences[q];
        const __m512i data = _mm512_ternarylogic_epi64(_mm512_shuffle_epi8(canonical, layouts[q]), bytes[q], _mm512_setr_epi64(-1, 0, -1, 0, -1, 0, -1, 0),
                                                       BUSTER_X86_A_OR_B_AND_C);
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

// The vectorized encoder's loop: `groups` groups of 4 * `quads` instructions per iteration (all constants), storing
// with `store` while instructions remain after the iteration; then whole groups with masked stores; then the last
// partial group from a zeroed copy, so that no load reads past the instructions. Returns the number of bytes, or
// X86_ENCODER_ERROR_INVALID_INSTRUCTION. Writes nothing past output + BUSTER_X86_ENCODER_CAPACITY(count), but may write
// past the instructions' bytes.
//
// Why a full 64-byte store stays inside that capacity: a store of quad q is at most 15 bytes per instruction before
// it, so it ends by 15 * (i' - 4) + 64 bytes into the output, where i' counts the instructions up to the end of the
// quad; with i' < count, that's at most 15 * (count - 5) + 64 < 15 * count.
BUSTER_INLINE int64_t encode_batches(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint32_t* offsets, uint32_t quads,
                                     uint32_t groups, X86Gather gather, X86Store store)
{
    const uint64_t group_instructions = BUSTER_X86_QUAD_INSTRUCTIONS * quads;
    const uint64_t iteration_instructions = group_instructions * groups;
    const uint32_t group_lanes = (uint32_t)((1ull << group_instructions) - 1);
    __mmask16 invalid = 0;
    uint8_t* out = output;
    uint64_t i = 0;
    while (i + iteration_instructions < count)
    {
        for (uint32_t g = 0; g < groups; g += 1)
        {
            out = encode_group(instructions + i, quads, out, output, offsets != 0 ? offsets + i : 0, group_lanes, gather, store, &invalid);
            i += group_instructions;
        }
    }
    while (i + group_instructions <= count)
    {
        out = encode_group(instructions + i, quads, out, output, offsets != 0 ? offsets + i : 0, group_lanes, gather, X86_STORE_MASKED, &invalid);
        i += group_instructions;
    }
    if (i < count)
    {
        X86Instruction tail[BUSTER_X86_GROUP_INSTRUCTIONS] __attribute__((aligned(64))) = { 0 };
        memcpy(tail, instructions + i, (count - i) * sizeof(X86Instruction));
        out = encode_group(tail, quads, out, output, offsets != 0 ? offsets + i : 0, (uint32_t)((1ull << (count - i)) - 1), gather, X86_STORE_MASKED, &invalid);
    }
    return invalid != 0 ? X86_ENCODER_ERROR_INVALID_INSTRUCTION : (int64_t)(out - output);
}

// The instantiations, by instructions per iteration, and for the benchmark, with the other gathers and stores. All of
// them need room for BUSTER_X86_ENCODER_CAPACITY(count) bytes.
#define BUSTER_X86_ENCODE_VARIANT(name, quads, groups, gather, store) \
    __attribute__((noinline)) static int64_t name(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint32_t* offsets) \
    { \
        return encode_batches(instructions, count, output, offsets, quads, groups, gather, store); \
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

// The production entry point: 32 instructions per iteration, which the Zen 5 benchmark found the fastest
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
        result = x86_encode_32(instructions, count, output, offsets);
    }
    return result;
}
