/* Generated structurally from the pinned Arm A64 XML; do not edit. */
#ifndef BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_GENERATED_H
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_GENERATED_H
#include <buster/lib/base.h>
#include <buster/lib/target.h>

typedef enum BusterAarch64ArmM1ScalarIntegerRecipe {
    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_EXT,
    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_IMM,
    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_SHIFT,
    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_IMM,
    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT,
    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_BITFIELD,
    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_EXTRACT,
    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_MOVEWIDE,
    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COND_CMP_IMM,
    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COND_CMP_REG,
    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_RMIF,
    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_UDF,
    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COUNT,
} BusterAarch64ArmM1ScalarIntegerRecipe;

typedef enum BusterAarch64ArmM1ScalarIntegerOperandKind {
    BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER,
    BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE,
} BusterAarch64ArmM1ScalarIntegerOperandKind;

typedef enum BusterAarch64ArmM1ScalarIntegerRegister31Role {
    BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR,
    BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP,
    BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY,
} BusterAarch64ArmM1ScalarIntegerRegister31Role;

typedef struct BusterAarch64ArmM1ScalarIntegerGeneratedOperand BusterAarch64ArmM1ScalarIntegerGeneratedOperand;
struct BusterAarch64ArmM1ScalarIntegerGeneratedOperand {
    u8 kind;
    u8 width;
    u8 register31_role;
    u8 reserved;
};

typedef struct BusterAarch64ArmM1ScalarIntegerGeneratedForm BusterAarch64ArmM1ScalarIntegerGeneratedForm;
struct BusterAarch64ArmM1ScalarIntegerGeneratedForm {
    const char* mnemonic;
    const char* arm_row_id;
    u64 arm_row_digest;
    u32 fixed_mask;
    u32 fixed_value;
    TargetCpuFeature required_feature;
    u8 recipe;
    u8 width;
    u8 operand_count;
    u8 reserved;
    BusterAarch64ArmM1ScalarIntegerGeneratedOperand operands[4];
};

#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_FORM_COUNT 72u
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_MNEMONIC_COUNT 23u
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_ARITY_1_COUNT 1u
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_ARITY_2_COUNT 6u
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_ARITY_3_COUNT 49u
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_ARITY_4_COUNT 16u
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_BASELINE_COUNT 71u
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_FLAGM_COUNT 1u
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_ADD_SUB_EXT_COUNT 8u
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_ADD_SUB_IMM_COUNT 8u
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_ADD_SUB_SHIFT_COUNT 8u
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_LOGICAL_IMM_COUNT 8u
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_LOGICAL_SHIFT_COUNT 16u
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_BITFIELD_COUNT 6u
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_EXTRACT_COUNT 2u
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_MOVEWIDE_COUNT 6u
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_COND_CMP_IMM_COUNT 4u
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_COND_CMP_REG_COUNT 4u
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_RMIF_COUNT 1u
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_UDF_COUNT 1u
#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_IDENTITY_SHA256 "4429d9ab064a8e98561c794e8c5408a3922bc6a7f07a32015caaa9932ba2c484"

static const BusterAarch64ArmM1ScalarIntegerGeneratedForm buster_aarch64_arm_m1_scalar_integer_generated_forms[] = {
    {
        .mnemonic = "adds", .arm_row_id = "arm-a64@2026-06:ADDS_32S_addsub_ext", .arm_row_digest = UINT64_C(0xa71af383cea60476),
        .fixed_mask = UINT32_C(0xffe00000), .fixed_value = UINT32_C(0x2b200000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_EXT, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "adds", .arm_row_id = "arm-a64@2026-06:ADDS_32S_addsub_imm", .arm_row_digest = UINT64_C(0xbe8986664349bfe),
        .fixed_mask = UINT32_C(0xff800000), .fixed_value = UINT32_C(0x31000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_IMM, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "adds", .arm_row_id = "arm-a64@2026-06:ADDS_32_addsub_shift", .arm_row_digest = UINT64_C(0x8b82dbb7d8a4e298),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0x2b000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_SHIFT, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "adds", .arm_row_id = "arm-a64@2026-06:ADDS_64S_addsub_ext", .arm_row_digest = UINT64_C(0x3821df32689b697d),
        .fixed_mask = UINT32_C(0xffe00000), .fixed_value = UINT32_C(0xab200000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_EXT, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "adds", .arm_row_id = "arm-a64@2026-06:ADDS_64S_addsub_imm", .arm_row_digest = UINT64_C(0x6ea7fcad54d12ebd),
        .fixed_mask = UINT32_C(0xff800000), .fixed_value = UINT32_C(0xb1000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_IMM, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "adds", .arm_row_id = "arm-a64@2026-06:ADDS_64_addsub_shift", .arm_row_digest = UINT64_C(0x29133d7c52d8b701),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0xab000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_SHIFT, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "add", .arm_row_id = "arm-a64@2026-06:ADD_32_addsub_ext", .arm_row_digest = UINT64_C(0x47d5b403895fc83d),
        .fixed_mask = UINT32_C(0xffe00000), .fixed_value = UINT32_C(0x0b200000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_EXT, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "add", .arm_row_id = "arm-a64@2026-06:ADD_32_addsub_imm", .arm_row_digest = UINT64_C(0x5e7caceee06a10e5),
        .fixed_mask = UINT32_C(0xff800000), .fixed_value = UINT32_C(0x11000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_IMM, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "add", .arm_row_id = "arm-a64@2026-06:ADD_32_addsub_shift", .arm_row_digest = UINT64_C(0x3db6a0f7ab970653),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0x0b000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_SHIFT, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "add", .arm_row_id = "arm-a64@2026-06:ADD_64_addsub_ext", .arm_row_digest = UINT64_C(0xc569a3af8d48a994),
        .fixed_mask = UINT32_C(0xffe00000), .fixed_value = UINT32_C(0x8b200000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_EXT, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "add", .arm_row_id = "arm-a64@2026-06:ADD_64_addsub_imm", .arm_row_digest = UINT64_C(0xb815431cc046237),
        .fixed_mask = UINT32_C(0xff800000), .fixed_value = UINT32_C(0x91000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_IMM, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "add", .arm_row_id = "arm-a64@2026-06:ADD_64_addsub_shift", .arm_row_digest = UINT64_C(0x95cdafd65d4b52d3),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0x8b000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_SHIFT, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "ands", .arm_row_id = "arm-a64@2026-06:ANDS_32S_log_imm", .arm_row_digest = UINT64_C(0x9028b2c02d7401af),
        .fixed_mask = UINT32_C(0xffc00000), .fixed_value = UINT32_C(0x72000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_IMM, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "ands", .arm_row_id = "arm-a64@2026-06:ANDS_32_log_shift", .arm_row_digest = UINT64_C(0xcbcb270fe062626d),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0x6a000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "ands", .arm_row_id = "arm-a64@2026-06:ANDS_64S_log_imm", .arm_row_digest = UINT64_C(0x68e9a7d1721d31df),
        .fixed_mask = UINT32_C(0xff800000), .fixed_value = UINT32_C(0xf2000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_IMM, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "ands", .arm_row_id = "arm-a64@2026-06:ANDS_64_log_shift", .arm_row_digest = UINT64_C(0x45d357b988b835ba),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0xea000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "and", .arm_row_id = "arm-a64@2026-06:AND_32_log_imm", .arm_row_digest = UINT64_C(0x30d08a0ccbf1e093),
        .fixed_mask = UINT32_C(0xffc00000), .fixed_value = UINT32_C(0x12000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_IMM, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "and", .arm_row_id = "arm-a64@2026-06:AND_32_log_shift", .arm_row_digest = UINT64_C(0xda4b6f89ecdd5d9e),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0x0a000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "and", .arm_row_id = "arm-a64@2026-06:AND_64_log_imm", .arm_row_digest = UINT64_C(0x3477519d56e61d89),
        .fixed_mask = UINT32_C(0xff800000), .fixed_value = UINT32_C(0x92000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_IMM, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "and", .arm_row_id = "arm-a64@2026-06:AND_64_log_shift", .arm_row_digest = UINT64_C(0xbb364e6fb62b965e),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0x8a000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "bfm", .arm_row_id = "arm-a64@2026-06:BFM_32M_bitfield", .arm_row_digest = UINT64_C(0x9db3844fb790776b),
        .fixed_mask = UINT32_C(0xffc00000), .fixed_value = UINT32_C(0x33000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_BITFIELD, .width = 32u, .operand_count = 4u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "bfm", .arm_row_id = "arm-a64@2026-06:BFM_64M_bitfield", .arm_row_digest = UINT64_C(0xd05652b1b01482ee),
        .fixed_mask = UINT32_C(0xffc00000), .fixed_value = UINT32_C(0xb3400000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_BITFIELD, .width = 64u, .operand_count = 4u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "bics", .arm_row_id = "arm-a64@2026-06:BICS_32_log_shift", .arm_row_digest = UINT64_C(0xd4c1578e4cf9e2c8),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0x6a200000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "bics", .arm_row_id = "arm-a64@2026-06:BICS_64_log_shift", .arm_row_digest = UINT64_C(0x6a36380e37c46727),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0xea200000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "bic", .arm_row_id = "arm-a64@2026-06:BIC_32_log_shift", .arm_row_digest = UINT64_C(0x471ff66f47009207),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0x0a200000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "bic", .arm_row_id = "arm-a64@2026-06:BIC_64_log_shift", .arm_row_digest = UINT64_C(0x44f3f55b33b72201),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0x8a200000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "ccmn", .arm_row_id = "arm-a64@2026-06:CCMN_32_condcmp_imm", .arm_row_digest = UINT64_C(0xf9089f38ab0532d6),
        .fixed_mask = UINT32_C(0xffe00c10), .fixed_value = UINT32_C(0x3a400800),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COND_CMP_IMM, .width = 32u, .operand_count = 4u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "ccmn", .arm_row_id = "arm-a64@2026-06:CCMN_32_condcmp_reg", .arm_row_digest = UINT64_C(0x8f187ae800d5ae0a),
        .fixed_mask = UINT32_C(0xffe00c10), .fixed_value = UINT32_C(0x3a400000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COND_CMP_REG, .width = 32u, .operand_count = 4u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "ccmn", .arm_row_id = "arm-a64@2026-06:CCMN_64_condcmp_imm", .arm_row_digest = UINT64_C(0xc45b6597fc555de5),
        .fixed_mask = UINT32_C(0xffe00c10), .fixed_value = UINT32_C(0xba400800),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COND_CMP_IMM, .width = 64u, .operand_count = 4u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "ccmn", .arm_row_id = "arm-a64@2026-06:CCMN_64_condcmp_reg", .arm_row_digest = UINT64_C(0xd5842099c2ba8b89),
        .fixed_mask = UINT32_C(0xffe00c10), .fixed_value = UINT32_C(0xba400000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COND_CMP_REG, .width = 64u, .operand_count = 4u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "ccmp", .arm_row_id = "arm-a64@2026-06:CCMP_32_condcmp_imm", .arm_row_digest = UINT64_C(0x3fba66f7837876c6),
        .fixed_mask = UINT32_C(0xffe00c10), .fixed_value = UINT32_C(0x7a400800),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COND_CMP_IMM, .width = 32u, .operand_count = 4u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "ccmp", .arm_row_id = "arm-a64@2026-06:CCMP_32_condcmp_reg", .arm_row_digest = UINT64_C(0x17ed3c19e67154fc),
        .fixed_mask = UINT32_C(0xffe00c10), .fixed_value = UINT32_C(0x7a400000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COND_CMP_REG, .width = 32u, .operand_count = 4u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "ccmp", .arm_row_id = "arm-a64@2026-06:CCMP_64_condcmp_imm", .arm_row_digest = UINT64_C(0xc68d41831d382f1e),
        .fixed_mask = UINT32_C(0xffe00c10), .fixed_value = UINT32_C(0xfa400800),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COND_CMP_IMM, .width = 64u, .operand_count = 4u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "ccmp", .arm_row_id = "arm-a64@2026-06:CCMP_64_condcmp_reg", .arm_row_digest = UINT64_C(0x1cf1100f47e781d5),
        .fixed_mask = UINT32_C(0xffe00c10), .fixed_value = UINT32_C(0xfa400000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COND_CMP_REG, .width = 64u, .operand_count = 4u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "eon", .arm_row_id = "arm-a64@2026-06:EON_32_log_shift", .arm_row_digest = UINT64_C(0x88a2f6019c2fc306),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0x4a200000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "eon", .arm_row_id = "arm-a64@2026-06:EON_64_log_shift", .arm_row_digest = UINT64_C(0x1a26038e2ba61a36),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0xca200000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "eor", .arm_row_id = "arm-a64@2026-06:EOR_32_log_imm", .arm_row_digest = UINT64_C(0x79b20d31fe949610),
        .fixed_mask = UINT32_C(0xffc00000), .fixed_value = UINT32_C(0x52000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_IMM, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "eor", .arm_row_id = "arm-a64@2026-06:EOR_32_log_shift", .arm_row_digest = UINT64_C(0x8705a182207a8d5d),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0x4a000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "eor", .arm_row_id = "arm-a64@2026-06:EOR_64_log_imm", .arm_row_digest = UINT64_C(0xf09e2c1f64b90f63),
        .fixed_mask = UINT32_C(0xff800000), .fixed_value = UINT32_C(0xd2000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_IMM, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "eor", .arm_row_id = "arm-a64@2026-06:EOR_64_log_shift", .arm_row_digest = UINT64_C(0x4241d3930d5db462),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0xca000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "extr", .arm_row_id = "arm-a64@2026-06:EXTR_32_extract", .arm_row_digest = UINT64_C(0xc126aa8d0a7bc2c8),
        .fixed_mask = UINT32_C(0xffe08000), .fixed_value = UINT32_C(0x13800000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_EXTRACT, .width = 32u, .operand_count = 4u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "extr", .arm_row_id = "arm-a64@2026-06:EXTR_64_extract", .arm_row_digest = UINT64_C(0xf072a81f920087eb),
        .fixed_mask = UINT32_C(0xffe00000), .fixed_value = UINT32_C(0x93c00000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_EXTRACT, .width = 64u, .operand_count = 4u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "movk", .arm_row_id = "arm-a64@2026-06:MOVK_32_movewide", .arm_row_digest = UINT64_C(0x62eba6a5071d6447),
        .fixed_mask = UINT32_C(0xffc00000), .fixed_value = UINT32_C(0x72800000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_MOVEWIDE, .width = 32u, .operand_count = 2u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "movk", .arm_row_id = "arm-a64@2026-06:MOVK_64_movewide", .arm_row_digest = UINT64_C(0xd681db80285ce914),
        .fixed_mask = UINT32_C(0xff800000), .fixed_value = UINT32_C(0xf2800000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_MOVEWIDE, .width = 64u, .operand_count = 2u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "movn", .arm_row_id = "arm-a64@2026-06:MOVN_32_movewide", .arm_row_digest = UINT64_C(0x7eb0dc2d75ccb8d3),
        .fixed_mask = UINT32_C(0xffc00000), .fixed_value = UINT32_C(0x12800000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_MOVEWIDE, .width = 32u, .operand_count = 2u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "movn", .arm_row_id = "arm-a64@2026-06:MOVN_64_movewide", .arm_row_digest = UINT64_C(0xcd945a609482a676),
        .fixed_mask = UINT32_C(0xff800000), .fixed_value = UINT32_C(0x92800000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_MOVEWIDE, .width = 64u, .operand_count = 2u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "movz", .arm_row_id = "arm-a64@2026-06:MOVZ_32_movewide", .arm_row_digest = UINT64_C(0xabfe0e3166fce3f4),
        .fixed_mask = UINT32_C(0xffc00000), .fixed_value = UINT32_C(0x52800000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_MOVEWIDE, .width = 32u, .operand_count = 2u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "movz", .arm_row_id = "arm-a64@2026-06:MOVZ_64_movewide", .arm_row_digest = UINT64_C(0xa93150f49cd4b39a),
        .fixed_mask = UINT32_C(0xff800000), .fixed_value = UINT32_C(0xd2800000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_MOVEWIDE, .width = 64u, .operand_count = 2u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "orn", .arm_row_id = "arm-a64@2026-06:ORN_32_log_shift", .arm_row_digest = UINT64_C(0xc8cfda2c3ed0f2aa),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0x2a200000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "orn", .arm_row_id = "arm-a64@2026-06:ORN_64_log_shift", .arm_row_digest = UINT64_C(0xa22a007d6d85a5a0),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0xaa200000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "orr", .arm_row_id = "arm-a64@2026-06:ORR_32_log_imm", .arm_row_digest = UINT64_C(0x4e1c6f727a3e162),
        .fixed_mask = UINT32_C(0xffc00000), .fixed_value = UINT32_C(0x32000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_IMM, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "orr", .arm_row_id = "arm-a64@2026-06:ORR_32_log_shift", .arm_row_digest = UINT64_C(0xd75c5866b6a4cc10),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0x2a000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "orr", .arm_row_id = "arm-a64@2026-06:ORR_64_log_imm", .arm_row_digest = UINT64_C(0xd8988a68d03c5007),
        .fixed_mask = UINT32_C(0xff800000), .fixed_value = UINT32_C(0xb2000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_IMM, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "orr", .arm_row_id = "arm-a64@2026-06:ORR_64_log_shift", .arm_row_digest = UINT64_C(0xa19032e89cf52a3a),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0xaa000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "rmif", .arm_row_id = "arm-a64@2026-06:RMIF_only_rmif", .arm_row_digest = UINT64_C(0xee7951165fd5f2e1),
        .fixed_mask = UINT32_C(0xffe07c10), .fixed_value = UINT32_C(0xba000400),
        .required_feature = TARGET_CPU_FEATURE_AARCH64_FLAGM, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_RMIF, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "sbfm", .arm_row_id = "arm-a64@2026-06:SBFM_32M_bitfield", .arm_row_digest = UINT64_C(0xbe5d6a710b8d62fd),
        .fixed_mask = UINT32_C(0xffc00000), .fixed_value = UINT32_C(0x13000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_BITFIELD, .width = 32u, .operand_count = 4u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "sbfm", .arm_row_id = "arm-a64@2026-06:SBFM_64M_bitfield", .arm_row_digest = UINT64_C(0xdbaa9ee202964116),
        .fixed_mask = UINT32_C(0xffc00000), .fixed_value = UINT32_C(0x93400000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_BITFIELD, .width = 64u, .operand_count = 4u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "subs", .arm_row_id = "arm-a64@2026-06:SUBS_32S_addsub_ext", .arm_row_digest = UINT64_C(0xb94f3bc7a03c063f),
        .fixed_mask = UINT32_C(0xffe00000), .fixed_value = UINT32_C(0x6b200000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_EXT, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "subs", .arm_row_id = "arm-a64@2026-06:SUBS_32S_addsub_imm", .arm_row_digest = UINT64_C(0xfea9fce6af95c18b),
        .fixed_mask = UINT32_C(0xff800000), .fixed_value = UINT32_C(0x71000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_IMM, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "subs", .arm_row_id = "arm-a64@2026-06:SUBS_32_addsub_shift", .arm_row_digest = UINT64_C(0xdf1fd98cb8142ebb),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0x6b000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_SHIFT, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "subs", .arm_row_id = "arm-a64@2026-06:SUBS_64S_addsub_ext", .arm_row_digest = UINT64_C(0x1cc5b8a884afe71d),
        .fixed_mask = UINT32_C(0xffe00000), .fixed_value = UINT32_C(0xeb200000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_EXT, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "subs", .arm_row_id = "arm-a64@2026-06:SUBS_64S_addsub_imm", .arm_row_digest = UINT64_C(0x7411f93ed6c3ae85),
        .fixed_mask = UINT32_C(0xff800000), .fixed_value = UINT32_C(0xf1000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_IMM, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "subs", .arm_row_id = "arm-a64@2026-06:SUBS_64_addsub_shift", .arm_row_digest = UINT64_C(0x70dbe564a6773af7),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0xeb000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_SHIFT, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "sub", .arm_row_id = "arm-a64@2026-06:SUB_32_addsub_ext", .arm_row_digest = UINT64_C(0x6f811ba2a3f19d02),
        .fixed_mask = UINT32_C(0xffe00000), .fixed_value = UINT32_C(0x4b200000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_EXT, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "sub", .arm_row_id = "arm-a64@2026-06:SUB_32_addsub_imm", .arm_row_digest = UINT64_C(0xdb7c3d044b1b9158),
        .fixed_mask = UINT32_C(0xff800000), .fixed_value = UINT32_C(0x51000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_IMM, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "sub", .arm_row_id = "arm-a64@2026-06:SUB_32_addsub_shift", .arm_row_digest = UINT64_C(0xa9d4441fa060f1f3),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0x4b000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_SHIFT, .width = 32u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "sub", .arm_row_id = "arm-a64@2026-06:SUB_64_addsub_ext", .arm_row_digest = UINT64_C(0xcc69ef752ed02b7b),
        .fixed_mask = UINT32_C(0xffe00000), .fixed_value = UINT32_C(0xcb200000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_EXT, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "sub", .arm_row_id = "arm-a64@2026-06:SUB_64_addsub_imm", .arm_row_digest = UINT64_C(0x62005dcca839d7fd),
        .fixed_mask = UINT32_C(0xff800000), .fixed_value = UINT32_C(0xd1000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_IMM, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "sub", .arm_row_id = "arm-a64@2026-06:SUB_64_addsub_shift", .arm_row_digest = UINT64_C(0xc58481f22403910c),
        .fixed_mask = UINT32_C(0xff200000), .fixed_value = UINT32_C(0xcb000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_SHIFT, .width = 64u, .operand_count = 3u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
        },
    },
    {
        .mnemonic = "ubfm", .arm_row_id = "arm-a64@2026-06:UBFM_32M_bitfield", .arm_row_digest = UINT64_C(0x5201dd19a58544ce),
        .fixed_mask = UINT32_C(0xffc00000), .fixed_value = UINT32_C(0x53000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_BITFIELD, .width = 32u, .operand_count = 4u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 32u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "ubfm", .arm_row_id = "arm-a64@2026-06:UBFM_64M_bitfield", .arm_row_digest = UINT64_C(0x941b2bd68d1da23b),
        .fixed_mask = UINT32_C(0xffc00000), .fixed_value = UINT32_C(0xd3400000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_BITFIELD, .width = 64u, .operand_count = 4u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER, .width = 64u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
    {
        .mnemonic = "udf", .arm_row_id = "arm-a64@2026-06:UDF_only_perm_undef", .arm_row_digest = UINT64_C(0x5ffb6564871ca54a),
        .fixed_mask = UINT32_C(0xffff0000), .fixed_value = UINT32_C(0x00000000),
        .required_feature = TARGET_CPU_FEATURE_NONE, .recipe = BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_UDF, .width = 32u, .operand_count = 1u,
        .operands = {
            {.kind = BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE, .width = 0u, .register31_role = BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY},
        },
    },
};
BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(buster_aarch64_arm_m1_scalar_integer_generated_forms) == BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_FORM_COUNT);
#endif
