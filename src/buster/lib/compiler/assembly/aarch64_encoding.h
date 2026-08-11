#pragma once

#include <buster/lib/base.h>

// Exact architectural forms used at the AArch64 MC boundary. These IDs are
// deliberately distinct from MachineOpcode: machine rows may still be
// pseudos or semantic operations, while every value here denotes one real
// 32-bit instruction encoding.
typedef u16 A64Opcode;
enum
{
    A64_OPCODE_INVALID,
    A64_OPCODE_NOP,
    A64_OPCODE_B,
    A64_OPCODE_BL,
    A64_OPCODE_B_COND,
    A64_OPCODE_RET,
    A64_OPCODE_BR,
    A64_OPCODE_BLR,
    A64_OPCODE_LDR_LITERAL_64,
    A64_OPCODE_ADRP,
    A64_OPCODE_COUNT,
};

typedef enum A64MCOperandKind
{
    A64_MC_OPERAND_NONE,
    A64_MC_OPERAND_REGISTER,
    A64_MC_OPERAND_IMMEDIATE,
    A64_MC_OPERAND_PC_RELATIVE,
    A64_MC_OPERAND_COUNT,
} A64MCOperandKind;

typedef struct A64MCOperand A64MCOperand;
struct A64MCOperand
{
    s64 value;
    u8 kind;
    u8 reserved[7];
};
BUSTER_CT_CHECK(sizeof(A64MCOperand) == 16);

#define A64_MC_MAX_OPERANDS 2

typedef struct A64MCInst A64MCInst;
struct A64MCInst
{
    A64MCOperand operands[A64_MC_MAX_OPERANDS];
    A64Opcode opcode;
    u8 operand_count;
    u8 reserved[5];
};
BUSTER_CT_CHECK(sizeof(A64MCInst) == 40);

typedef enum A64PCRelativeLayout
{
    A64_PC_RELATIVE_NONE,
    A64_PC_RELATIVE_IMM26,
    A64_PC_RELATIVE_IMM19,
    A64_PC_RELATIVE_ADRP,
    A64_PC_RELATIVE_LAYOUT_COUNT,
} A64PCRelativeLayout;

typedef struct A64OpcodeDescriptor A64OpcodeDescriptor;
struct A64OpcodeDescriptor
{
    u32 fixed_mask;
    u32 fixed_value;
    u8 operand_count;
    u8 pc_relative_operand;
    u8 pc_relative_layout;
    u8 reserved;
};
BUSTER_CT_CHECK(sizeof(A64OpcodeDescriptor) == 12);

BUSTER_F_DECL A64OpcodeDescriptor const* a64_opcode_descriptor(A64Opcode opcode);

// Shared symmetric transform for A64's signed, scaled immediates. `bits`
// describes the encoded two's-complement field and `scale_log2` the number
// of implicit low zero bits in the architectural value.
BUSTER_F_DECL bool a64_signed_scaled_immediate_encode(s64 value, u8 bits, u8 scale_log2, u32* encoded);
BUSTER_F_DECL bool a64_signed_scaled_immediate_decode(u32 encoded, u8 bits, u8 scale_log2, s64* value);

// Encode or decode one exact real instruction. The current tranche covers
// the shared control-flow and PC-relative kernel; later generated opcodes can
// extend the descriptor table without changing this interface.
BUSTER_F_DECL bool a64_mc_encode(A64MCInst const* instruction, u32* word);
BUSTER_F_DECL bool a64_mc_decode(u32 word, A64MCInst* instruction);

// Patch only the PC-relative field of an already encoded exact form. Fixed
// bits and all non-PC-relative operands must already match `opcode`.
BUSTER_F_DECL bool a64_pc_relative_patch(A64Opcode opcode, u32 word, s64 displacement, u32* patched);

// Compute target + addend - place without requiring an intermediate term to
// fit in s64. Extreme unsigned addresses and signed addends may cancel to a
// small, valid architectural displacement.
BUSTER_F_DECL bool a64_pc_relative_displacement(u64 target, u64 place, s64 addend, s64* displacement);

// ADRP uses page addresses and a split signed imm21. This helper performs the
// page rounding and checked subtraction without relying on signed overflow.
BUSTER_F_DECL bool a64_adrp_encode(u32 destination_register, u64 instruction_address, u64 target_address, u32* word);

// Architectural condition values 0..13 form inverse pairs. AL/NV are valid
// four-bit fields but have no conditional inverse for relaxation.
BUSTER_F_DECL bool a64_condition_invert(u32 condition, u32* inverse);
