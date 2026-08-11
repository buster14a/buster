/* Generated from arm-a64-canonical.generated.jsonl; do not edit by hand. */
#ifndef BUSTER_AARCH64_CONTROL_SEMANTICS_GENERATED_H
#define BUSTER_AARCH64_CONTROL_SEMANTICS_GENERATED_H

#include <buster/lib/compiler/assembly/aarch64_control_semantics.h>

static const char8 buster_aarch64_control_semantic_string_pool[] =
    "arm-a64@2026-06:ADRP_only_pcreladdr\0arm-a64@2026-06:ADR_only_pcreladdr\0arm-a64@2026-06:BL_only_b"
    "ranch_imm\0arm-a64@2026-06:B_only_branch_imm\0arm-a64@2026-06:B_only_condbranch\0arm-a64@2026-06:CB"
    "NZ_32_compbranch\0arm-a64@2026-06:CBNZ_64_compbranch\0arm-a64@2026-06:CBZ_32_compbranch\0arm-a64@20"
    "26-06:CBZ_64_compbranch\0arm-a64@2026-06:CSEL_32_condsel\0arm-a64@2026-06:CSEL_64_condsel\0arm-a64@"
    "2026-06:CSINC_32_condsel\0arm-a64@2026-06:CSINC_64_condsel\0arm-a64@2026-06:CSINV_32_condsel\0arm-a"
    "64@2026-06:CSINV_64_condsel\0arm-a64@2026-06:CSNEG_32_condsel\0arm-a64@2026-06:CSNEG_64_condsel\0ar"
    "m-a64@2026-06:LDRSW_64_loadlit\0arm-a64@2026-06:LDR_32_loadlit\0arm-a64@2026-06:LDR_64_loadlit\0arm"
    "-a64@2026-06:LDR_D_loadlit\0arm-a64@2026-06:LDR_Q_loadlit\0arm-a64@2026-06:LDR_S_loadlit\0arm-a64@2"
    "026-06:PRFM_P_loadlit\0arm-a64@2026-06:RET_64R_branch_reg\0arm-a64@2026-06:TBNZ_only_testbranch\0ar"
    "m-a64@2026-06:TBZ_only_testbranch\0ADRP_only_pcreladdr\0ADR_only_pcreladdr\0BL_only_branch_imm\0B_on"
    "ly_branch_imm\0B_only_condbranch\0CBNZ_32_compbranch\0CBNZ_64_compbranch\0CBZ_32_compbranch\0CBZ_64_c"
    "ompbranch\0CSEL_32_condsel\0CSEL_64_condsel\0CSINC_32_condsel\0CSINC_64_condsel\0CSINV_32_condsel\0CSI"
    "NV_64_condsel\0CSNEG_32_condsel\0CSNEG_64_condsel\0LDRSW_64_loadlit\0LDR_32_loadlit\0LDR_64_loadlit\0L"
    "DR_D_loadlit\0LDR_Q_loadlit\0LDR_S_loadlit\0PRFM_P_loadlit\0RET_64R_branch_reg\0TBNZ_only_testbranch\0"
    "TBZ_only_testbranch\0adrp\0adr\0bl\0b\0b.cond\0cbnz\0cbz\0csel\0csinc\0csinv\0csneg\0ldrsw\0ldr\0prfm\0ret\0tbnz"
    "\0tbz\0ADRP <Xd>, <label>\0ADR <Xd>, <label>\0BL <label>\0B <label>\0B.<cond> <label>\0CBNZ <Wt>, <labe"
    "l>\0CBNZ <Xt>, <label>\0CBZ <Wt>, <label>\0CBZ <Xt>, <label>\0CSEL <Wd>, <Wn>, <Wm>, <cond>\0CSEL <Xd"
    ">, <Xn>, <Xm>, <cond>\0CSINC <Wd>, <Wn>, <Wm>, <cond>\0CSINC <Xd>, <Xn>, <Xm>, <cond>\0CSINV <Wd>, "
    "<Wn>, <Wm>, <cond>\0CSINV <Xd>, <Xn>, <Xm>, <cond>\0CSNEG <Wd>, <Wn>, <Wm>, <cond>\0CSNEG <Xd>, <Xn"
    ">, <Xm>, <cond>\0LDRSW <Xt>, <label>\0LDR <Wt>, <label>\0LDR <Xt>, <label>\0LDR <Dt>, <label>\0LDR <Q"
    "t>, <label>\0LDR <St>, <label>\0PRFM (<prfop>|#<imm5>), <label>\0RET {<Xn>}\0TBNZ <R><t>, #<imm>, <l"
    "abel>\0TBZ <R><t>, #<imm>, <label>\0"
    "eq\0ne\0cs\0cc\0mi\0pl\0vs\0vc\0hi\0ls\0ge\0lt\0gt\0le\0al\0nv\0"
;
#define BUSTER_AARCH64_CONTROL_SEMANTIC_STRING_POOL_SIZE UINT32_C(2098)

#define A64C_STR(offset_value, length_value) \
    {(u32)(offset_value), (u32)(length_value)}
#define A64C_REG(role_value, width_value) \
    {.role = (u8)(role_value), .kind = BUSTER_AARCH64_CONTROL_OPERAND_REGISTER, .width = (u8)(width_value), \
     .register31_role = BUSTER_AARCH64_CONTROL_REGISTER31_ZR, .register_class = BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR, .minimum = 0, .maximum = 31}
#define A64C_REG_FP(role_value, width_value) \
    {.role = (u8)(role_value), .kind = BUSTER_AARCH64_CONTROL_OPERAND_REGISTER, .width = (u8)(width_value), \
     .register31_role = BUSTER_AARCH64_CONTROL_REGISTER31_NONE, .register_class = BUSTER_AARCH64_CONTROL_REGISTER_CLASS_FP_SIMD, .minimum = 0, .maximum = 31}
#define A64C_REG_POLY(role_value) \
    {.role = (u8)(role_value), .kind = BUSTER_AARCH64_CONTROL_OPERAND_REGISTER, .width = 0, \
     .register31_role = BUSTER_AARCH64_CONTROL_REGISTER31_ZR, .register_class = BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR, .minimum = 0, .maximum = 31}
#define A64C_IMM(role_value, width_value, min_value, max_value) \
    {.role = (u8)(role_value), .kind = BUSTER_AARCH64_CONTROL_OPERAND_IMMEDIATE, .width = (u8)(width_value), \
     .register31_role = BUSTER_AARCH64_CONTROL_REGISTER31_NONE, .register_class = BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR, .minimum = (s64)(min_value), .maximum = (s64)(max_value)}
#define A64C_COND(role_value) \
    {.role = (u8)(role_value), .kind = BUSTER_AARCH64_CONTROL_OPERAND_CONDITION, .width = 4, \
     .register31_role = BUSTER_AARCH64_CONTROL_REGISTER31_NONE, .register_class = BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR, .minimum = 0, .maximum = 15}
#define A64C_PC(role_value) \
    {.role = (u8)(role_value), .kind = BUSTER_AARCH64_CONTROL_OPERAND_PC_RELATIVE, .width = 64, \
     .register31_role = BUSTER_AARCH64_CONTROL_REGISTER31_NONE, .register_class = BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR, .minimum = INT64_MIN, .maximum = INT64_MAX}
#define A64C_PC_NONE \
    {.layout = BUSTER_AARCH64_CONTROL_PC_NONE, .bits = 0, .scale_log2 = 0, .split = 0, .minimum = 0, .maximum = 0, .alignment = 0}
#define A64C_PC_IMM26 \
    {BUSTER_AARCH64_CONTROL_PC_IMM26, 26, 2, 0, -INT64_C(134217728), INT64_C(134217724), 4}
#define A64C_PC_IMM19 \
    {BUSTER_AARCH64_CONTROL_PC_IMM19, 19, 2, 0, -INT64_C(1048576), INT64_C(1048572), 4}
#define A64C_PC_IMM14 \
    {BUSTER_AARCH64_CONTROL_PC_IMM14, 14, 2, 0, -INT64_C(32768), INT64_C(32764), 4}
#define A64C_PC_ADRP \
    {BUSTER_AARCH64_CONTROL_PC_ADRP, 21, 12, 1, -INT64_C(4294967296), INT64_C(4294963200), 4096}
#define A64C_PC_ADR \
    {BUSTER_AARCH64_CONTROL_PC_ADR, 21, 0, 1, -INT64_C(1048576), INT64_C(1048575), 1}

static const BusterAarch64ControlCondition buster_aarch64_control_conditions[] = {
    {.value = 0, .inverse = 1, .valid = 1, .inverse_valid = 1, .name = A64C_STR(2050, 2)},
    {.value = 1, .inverse = 0, .valid = 1, .inverse_valid = 1, .name = A64C_STR(2053, 2)},
    {.value = 2, .inverse = 3, .valid = 1, .inverse_valid = 1, .name = A64C_STR(2056, 2)},
    {.value = 3, .inverse = 2, .valid = 1, .inverse_valid = 1, .name = A64C_STR(2059, 2)},
    {.value = 4, .inverse = 5, .valid = 1, .inverse_valid = 1, .name = A64C_STR(2062, 2)},
    {.value = 5, .inverse = 4, .valid = 1, .inverse_valid = 1, .name = A64C_STR(2065, 2)},
    {.value = 6, .inverse = 7, .valid = 1, .inverse_valid = 1, .name = A64C_STR(2068, 2)},
    {.value = 7, .inverse = 6, .valid = 1, .inverse_valid = 1, .name = A64C_STR(2071, 2)},
    {.value = 8, .inverse = 9, .valid = 1, .inverse_valid = 1, .name = A64C_STR(2074, 2)},
    {.value = 9, .inverse = 8, .valid = 1, .inverse_valid = 1, .name = A64C_STR(2077, 2)},
    {.value = 10, .inverse = 11, .valid = 1, .inverse_valid = 1, .name = A64C_STR(2080, 2)},
    {.value = 11, .inverse = 10, .valid = 1, .inverse_valid = 1, .name = A64C_STR(2083, 2)},
    {.value = 12, .inverse = 13, .valid = 1, .inverse_valid = 1, .name = A64C_STR(2086, 2)},
    {.value = 13, .inverse = 12, .valid = 1, .inverse_valid = 1, .name = A64C_STR(2089, 2)},
    {.value = 14, .inverse = UINT8_MAX, .valid = 1, .inverse_valid = 0, .name = A64C_STR(2092, 2)},
    {.value = 15, .inverse = UINT8_MAX, .valid = 1, .inverse_valid = 0, .name = A64C_STR(2095, 2)},
};

#define BUSTER_AARCH64_CONTROL_CONDITION_COUNT \
    ((u32)BUSTER_ARRAY_LENGTH(buster_aarch64_control_conditions))

static const BusterAarch64ControlSemanticRecord buster_aarch64_control_semantic_generated_rows[] = {
    {
        .id = A64C_STR(0, 35), .encoding_name = A64C_STR(898, 19), .mnemonic = A64C_STR(1364, 4),
        .assembly = A64C_STR(1445, 18), .row_digest = UINT64_C(0xd7d70d13c8dc068d), .fixed_mask = UINT32_C(0x9f000000),
        .fixed_value = UINT32_C(0x90000000), .oracle_word = UINT32_C(0x90000000), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_ADRP, .operand_count = 2, .optional_operand_mask = 0,
        .operands = {A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_DESTINATION, 64), A64C_PC(BUSTER_AARCH64_CONTROL_ROLE_TARGET)},
        .pc_relative = A64C_PC_ADRP, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_ADRP_PAGE21,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_LOCAL_ONLY, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(36, 34), .encoding_name = A64C_STR(918, 18), .mnemonic = A64C_STR(1369, 3),
        .assembly = A64C_STR(1464, 17), .row_digest = UINT64_C(0x25523e69dae2dcd2), .fixed_mask = UINT32_C(0x9f000000),
        .fixed_value = UINT32_C(0x10000000), .oracle_word = UINT32_C(0x10000000), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_ADR, .operand_count = 2, .optional_operand_mask = 0,
        .operands = {A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_DESTINATION, 64), A64C_PC(BUSTER_AARCH64_CONTROL_ROLE_TARGET)},
        .pc_relative = A64C_PC_ADR, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_ADR_BYTE21,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_LOCAL_ONLY, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(71, 34), .encoding_name = A64C_STR(937, 18), .mnemonic = A64C_STR(1373, 2),
        .assembly = A64C_STR(1482, 10), .row_digest = UINT64_C(0x2e065d453ddb5bb4), .fixed_mask = UINT32_C(0xfc000000),
        .fixed_value = UINT32_C(0x94000000), .oracle_word = UINT32_C(0x94000000), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_BL, .operand_count = 1, .operands = {A64C_PC(BUSTER_AARCH64_CONTROL_ROLE_TARGET)},
        .pc_relative = A64C_PC_IMM26, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_CALL26,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_DARWIN_EXTERNAL_BRANCH26, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(106, 33), .encoding_name = A64C_STR(956, 17), .mnemonic = A64C_STR(1376, 1),
        .assembly = A64C_STR(1493, 9), .row_digest = UINT64_C(0x1c3f5d16e7f47c7b), .fixed_mask = UINT32_C(0xfc000000),
        .fixed_value = UINT32_C(0x14000000), .oracle_word = UINT32_C(0x14000000), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_B, .operand_count = 1, .operands = {A64C_PC(BUSTER_AARCH64_CONTROL_ROLE_TARGET)},
        .pc_relative = A64C_PC_IMM26, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_BRANCH26,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_DARWIN_EXTERNAL_BRANCH26, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(140, 33), .encoding_name = A64C_STR(974, 17), .mnemonic = A64C_STR(1378, 6),
        .assembly = A64C_STR(1503, 16), .row_digest = UINT64_C(0x493dad8e510dddab), .fixed_mask = UINT32_C(0xff000010),
        .fixed_value = UINT32_C(0x54000000), .oracle_word = UINT32_C(0x54000000), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_B_COND, .operand_count = 2,
        .operands = {A64C_PC(BUSTER_AARCH64_CONTROL_ROLE_TARGET), A64C_COND(BUSTER_AARCH64_CONTROL_ROLE_CONDITION)},
        .pc_relative = A64C_PC_IMM19, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_B_COND19,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_LOCAL_ONLY, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(174, 34), .encoding_name = A64C_STR(992, 18), .mnemonic = A64C_STR(1385, 4),
        .assembly = A64C_STR(1520, 18), .row_digest = UINT64_C(0x929f58d3f6754da2), .fixed_mask = UINT32_C(0xff000000),
        .fixed_value = UINT32_C(0x35000000), .oracle_word = UINT32_C(0x35000000), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_CBNZ_W, .operand_count = 2,
        .operands = {A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_TEST, 32), A64C_PC(BUSTER_AARCH64_CONTROL_ROLE_TARGET)},
        .pc_relative = A64C_PC_IMM19, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_COMPARE19,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_LOCAL_ONLY, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(209, 34), .encoding_name = A64C_STR(1011, 18), .mnemonic = A64C_STR(1385, 4),
        .assembly = A64C_STR(1539, 18), .row_digest = UINT64_C(0x3f3ae4ea929693c8), .fixed_mask = UINT32_C(0xff000000),
        .fixed_value = UINT32_C(0xb5000000), .oracle_word = UINT32_C(0xb5000000), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_CBNZ_X, .operand_count = 2,
        .operands = {A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_TEST, 64), A64C_PC(BUSTER_AARCH64_CONTROL_ROLE_TARGET)},
        .pc_relative = A64C_PC_IMM19, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_COMPARE19,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_LOCAL_ONLY, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(244, 33), .encoding_name = A64C_STR(1030, 17), .mnemonic = A64C_STR(1390, 3),
        .assembly = A64C_STR(1558, 17), .row_digest = UINT64_C(0x509f5dc8050a5eb), .fixed_mask = UINT32_C(0xff000000),
        .fixed_value = UINT32_C(0x34000000), .oracle_word = UINT32_C(0x34000000), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_CBZ_W, .operand_count = 2,
        .operands = {A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_TEST, 32), A64C_PC(BUSTER_AARCH64_CONTROL_ROLE_TARGET)},
        .pc_relative = A64C_PC_IMM19, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_COMPARE19,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_LOCAL_ONLY, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(278, 33), .encoding_name = A64C_STR(1048, 17), .mnemonic = A64C_STR(1390, 3),
        .assembly = A64C_STR(1576, 17), .row_digest = UINT64_C(0xd01729aadc71e687), .fixed_mask = UINT32_C(0xff000000),
        .fixed_value = UINT32_C(0xb4000000), .oracle_word = UINT32_C(0xb4000000), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_CBZ_X, .operand_count = 2,
        .operands = {A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_TEST, 64), A64C_PC(BUSTER_AARCH64_CONTROL_ROLE_TARGET)},
        .pc_relative = A64C_PC_IMM19, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_COMPARE19,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_LOCAL_ONLY, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(312, 31), .encoding_name = A64C_STR(1066, 15), .mnemonic = A64C_STR(1394, 4),
        .assembly = A64C_STR(1594, 29), .row_digest = UINT64_C(0xbb0b933d1b2bf78e), .fixed_mask = UINT32_C(0xffe00c00),
        .fixed_value = UINT32_C(0x1a800000), .oracle_word = UINT32_C(0x1a820020), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_CSEL_W, .operand_count = 4,
        .operands = {A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_DESTINATION, 32), A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_SOURCE_N, 32),
                     A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_SOURCE_M, 32), A64C_COND(BUSTER_AARCH64_CONTROL_ROLE_CONDITION)},
        .pc_relative = A64C_PC_NONE, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_NONE,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_NONE, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(344, 31), .encoding_name = A64C_STR(1082, 15), .mnemonic = A64C_STR(1394, 4),
        .assembly = A64C_STR(1624, 29), .row_digest = UINT64_C(0x92b646113cee0d7e), .fixed_mask = UINT32_C(0xffe00c00),
        .fixed_value = UINT32_C(0x9a800000), .oracle_word = UINT32_C(0x9a820020), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_CSEL_X, .operand_count = 4,
        .operands = {A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_DESTINATION, 64), A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_SOURCE_N, 64),
                     A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_SOURCE_M, 64), A64C_COND(BUSTER_AARCH64_CONTROL_ROLE_CONDITION)},
        .pc_relative = A64C_PC_NONE, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_NONE,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_NONE, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(376, 32), .encoding_name = A64C_STR(1098, 16), .mnemonic = A64C_STR(1399, 5),
        .assembly = A64C_STR(1654, 30), .row_digest = UINT64_C(0x2b052232cd75d008), .fixed_mask = UINT32_C(0xffe00c00),
        .fixed_value = UINT32_C(0x1a800400), .oracle_word = UINT32_C(0x1a820420), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_CSINC_W, .operand_count = 4,
        .operands = {A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_DESTINATION, 32), A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_SOURCE_N, 32),
                     A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_SOURCE_M, 32), A64C_COND(BUSTER_AARCH64_CONTROL_ROLE_CONDITION)},
        .pc_relative = A64C_PC_NONE, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_NONE,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_NONE, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(409, 32), .encoding_name = A64C_STR(1115, 16), .mnemonic = A64C_STR(1399, 5),
        .assembly = A64C_STR(1685, 30), .row_digest = UINT64_C(0xb984d9b8eb17ebbf), .fixed_mask = UINT32_C(0xffe00c00),
        .fixed_value = UINT32_C(0x9a800400), .oracle_word = UINT32_C(0x9a820420), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_CSINC_X, .operand_count = 4,
        .operands = {A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_DESTINATION, 64), A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_SOURCE_N, 64),
                     A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_SOURCE_M, 64), A64C_COND(BUSTER_AARCH64_CONTROL_ROLE_CONDITION)},
        .pc_relative = A64C_PC_NONE, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_NONE,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_NONE, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(442, 32), .encoding_name = A64C_STR(1132, 16), .mnemonic = A64C_STR(1405, 5),
        .assembly = A64C_STR(1716, 30), .row_digest = UINT64_C(0xd5f34dfe86160a7a), .fixed_mask = UINT32_C(0xffe00c00),
        .fixed_value = UINT32_C(0x5a800000), .oracle_word = UINT32_C(0x5a820020), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_CSINV_W, .operand_count = 4,
        .operands = {A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_DESTINATION, 32), A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_SOURCE_N, 32),
                     A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_SOURCE_M, 32), A64C_COND(BUSTER_AARCH64_CONTROL_ROLE_CONDITION)},
        .pc_relative = A64C_PC_NONE, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_NONE,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_NONE, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(475, 32), .encoding_name = A64C_STR(1149, 16), .mnemonic = A64C_STR(1405, 5),
        .assembly = A64C_STR(1747, 30), .row_digest = UINT64_C(0x2674f1de193b7d25), .fixed_mask = UINT32_C(0xffe00c00),
        .fixed_value = UINT32_C(0xda800000), .oracle_word = UINT32_C(0xda820020), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_CSINV_X, .operand_count = 4,
        .operands = {A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_DESTINATION, 64), A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_SOURCE_N, 64),
                     A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_SOURCE_M, 64), A64C_COND(BUSTER_AARCH64_CONTROL_ROLE_CONDITION)},
        .pc_relative = A64C_PC_NONE, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_NONE,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_NONE, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(508, 32), .encoding_name = A64C_STR(1166, 16), .mnemonic = A64C_STR(1411, 5),
        .assembly = A64C_STR(1778, 30), .row_digest = UINT64_C(0x77eb53be7e4cedc0), .fixed_mask = UINT32_C(0xffe00c00),
        .fixed_value = UINT32_C(0x5a800400), .oracle_word = UINT32_C(0x5a820420), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_CSNEG_W, .operand_count = 4,
        .operands = {A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_DESTINATION, 32), A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_SOURCE_N, 32),
                     A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_SOURCE_M, 32), A64C_COND(BUSTER_AARCH64_CONTROL_ROLE_CONDITION)},
        .pc_relative = A64C_PC_NONE, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_NONE,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_NONE, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(541, 32), .encoding_name = A64C_STR(1183, 16), .mnemonic = A64C_STR(1411, 5),
        .assembly = A64C_STR(1809, 30), .row_digest = UINT64_C(0xffa16a3978da4452), .fixed_mask = UINT32_C(0xffe00c00),
        .fixed_value = UINT32_C(0xda800400), .oracle_word = UINT32_C(0xda820420), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_CSNEG_X, .operand_count = 4,
        .operands = {A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_DESTINATION, 64), A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_SOURCE_N, 64),
                     A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_SOURCE_M, 64), A64C_COND(BUSTER_AARCH64_CONTROL_ROLE_CONDITION)},
        .pc_relative = A64C_PC_NONE, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_NONE,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_NONE, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(574, 32), .encoding_name = A64C_STR(1200, 16), .mnemonic = A64C_STR(1417, 5),
        .assembly = A64C_STR(1840, 19), .row_digest = UINT64_C(0xca197147f8b7c99f), .fixed_mask = UINT32_C(0xff000000),
        .fixed_value = UINT32_C(0x98000000), .oracle_word = UINT32_C(0x98000000), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_LDRSW_X, .operand_count = 2,
        .operands = {A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_DESTINATION, 64), A64C_PC(BUSTER_AARCH64_CONTROL_ROLE_TARGET)},
        .pc_relative = A64C_PC_IMM19, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_LITERAL19,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_LOCAL_ONLY, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(607, 30), .encoding_name = A64C_STR(1217, 14), .mnemonic = A64C_STR(1423, 3),
        .assembly = A64C_STR(1860, 17), .row_digest = UINT64_C(0x19a0849fc1368185), .fixed_mask = UINT32_C(0xff000000),
        .fixed_value = UINT32_C(0x18000000), .oracle_word = UINT32_C(0x18000000), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_LDR_W, .operand_count = 2,
        .operands = {A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_DESTINATION, 32), A64C_PC(BUSTER_AARCH64_CONTROL_ROLE_TARGET)},
        .pc_relative = A64C_PC_IMM19, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_LITERAL19,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_LOCAL_ONLY, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(638, 30), .encoding_name = A64C_STR(1232, 14), .mnemonic = A64C_STR(1423, 3),
        .assembly = A64C_STR(1878, 17), .row_digest = UINT64_C(0x6445455f3888f467), .fixed_mask = UINT32_C(0xff000000),
        .fixed_value = UINT32_C(0x58000000), .oracle_word = UINT32_C(0x58000000), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_LDR_X, .operand_count = 2,
        .operands = {A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_DESTINATION, 64), A64C_PC(BUSTER_AARCH64_CONTROL_ROLE_TARGET)},
        .pc_relative = A64C_PC_IMM19, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_LITERAL19,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_LOCAL_ONLY, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(669, 29), .encoding_name = A64C_STR(1247, 13), .mnemonic = A64C_STR(1423, 3),
        .assembly = A64C_STR(1896, 17), .row_digest = UINT64_C(0x29a16412f15fef9e), .fixed_mask = UINT32_C(0xff000000),
        .fixed_value = UINT32_C(0x5c000000), .oracle_word = UINT32_C(0x5c000000), .owner = BUSTER_AARCH64_CONTROL_OWNER_COMPLEX_LITERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_LDR_D, .operand_count = 2,
        .operands = {A64C_REG_FP(BUSTER_AARCH64_CONTROL_ROLE_DESTINATION, 64), A64C_PC(BUSTER_AARCH64_CONTROL_ROLE_TARGET)},
        .pc_relative = A64C_PC_IMM19, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_LITERAL19,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_LOCAL_ONLY, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(699, 29), .encoding_name = A64C_STR(1261, 13), .mnemonic = A64C_STR(1423, 3),
        .assembly = A64C_STR(1914, 17), .row_digest = UINT64_C(0xed4683e2f659a7f8), .fixed_mask = UINT32_C(0xff000000),
        .fixed_value = UINT32_C(0x9c000000), .oracle_word = UINT32_C(0x9c000000), .owner = BUSTER_AARCH64_CONTROL_OWNER_COMPLEX_LITERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_LDR_Q, .operand_count = 2,
        .operands = {A64C_REG_FP(BUSTER_AARCH64_CONTROL_ROLE_DESTINATION, 128), A64C_PC(BUSTER_AARCH64_CONTROL_ROLE_TARGET)},
        .pc_relative = A64C_PC_IMM19, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_LITERAL19,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_LOCAL_ONLY, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(729, 29), .encoding_name = A64C_STR(1275, 13), .mnemonic = A64C_STR(1423, 3),
        .assembly = A64C_STR(1932, 17), .row_digest = UINT64_C(0x15b43bbc17d930ee), .fixed_mask = UINT32_C(0xff000000),
        .fixed_value = UINT32_C(0x1c000000), .oracle_word = UINT32_C(0x1c000000), .owner = BUSTER_AARCH64_CONTROL_OWNER_COMPLEX_LITERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_LDR_S, .operand_count = 2,
        .operands = {A64C_REG_FP(BUSTER_AARCH64_CONTROL_ROLE_DESTINATION, 32), A64C_PC(BUSTER_AARCH64_CONTROL_ROLE_TARGET)},
        .pc_relative = A64C_PC_IMM19, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_LITERAL19,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_LOCAL_ONLY, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(759, 30), .encoding_name = A64C_STR(1289, 14), .mnemonic = A64C_STR(1427, 4),
        .assembly = A64C_STR(1950, 31), .row_digest = UINT64_C(0xe8617a435eefc338), .fixed_mask = UINT32_C(0xff000000),
        .fixed_value = UINT32_C(0xd8000000), .oracle_word = UINT32_C(0xd8000000), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_PRFM, .operand_count = 2,
        .operands = {A64C_IMM(BUSTER_AARCH64_CONTROL_ROLE_PREFETCH, 5, 0, 31), A64C_PC(BUSTER_AARCH64_CONTROL_ROLE_TARGET)},
        .pc_relative = A64C_PC_IMM19, .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_LITERAL19,
        .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_LOCAL_ONLY, .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(790, 34), .encoding_name = A64C_STR(1304, 18), .mnemonic = A64C_STR(1432, 3),
        .assembly = A64C_STR(1982, 10), .row_digest = UINT64_C(0x27aab29ac0c28070), .fixed_mask = UINT32_C(0xfffffc1f),
        .fixed_value = UINT32_C(0xd65f0000), .oracle_word = UINT32_C(0xd65f03c0), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_RET, .operand_count = 1, .optional_operand_mask = 1,
        .operands = {A64C_REG(BUSTER_AARCH64_CONTROL_ROLE_TARGET, 64)}, .pc_relative = A64C_PC_NONE,
        .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_NONE, .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_NONE,
        .default_operand = 30,
    },
    {
        .id = A64C_STR(825, 36), .encoding_name = A64C_STR(1323, 20), .mnemonic = A64C_STR(1436, 4),
        .assembly = A64C_STR(1993, 28), .row_digest = UINT64_C(0xde51e9636e9f1654), .fixed_mask = UINT32_C(0x7f000000),
        .fixed_value = UINT32_C(0x37000000), .oracle_word = UINT32_C(0x37000000), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_TBNZ, .operand_count = 3,
        .operands = {A64C_REG_POLY(BUSTER_AARCH64_CONTROL_ROLE_TEST), A64C_IMM(BUSTER_AARCH64_CONTROL_ROLE_BIT, 6, 0, 63),
                     A64C_PC(BUSTER_AARCH64_CONTROL_ROLE_TARGET)}, .pc_relative = A64C_PC_IMM14,
        .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_TEST14, .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_LOCAL_ONLY,
        .default_operand = UINT8_MAX,
    },
    {
        .id = A64C_STR(862, 35), .encoding_name = A64C_STR(1344, 19), .mnemonic = A64C_STR(1441, 3),
        .assembly = A64C_STR(2022, 27), .row_digest = UINT64_C(0xdb8b9b82be4eb846), .fixed_mask = UINT32_C(0x7f000000),
        .fixed_value = UINT32_C(0x36000000), .oracle_word = UINT32_C(0x36000000), .owner = BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
        .form = BUSTER_AARCH64_CONTROL_FORM_TBZ, .operand_count = 3,
        .operands = {A64C_REG_POLY(BUSTER_AARCH64_CONTROL_ROLE_TEST), A64C_IMM(BUSTER_AARCH64_CONTROL_ROLE_BIT, 6, 0, 63),
                     A64C_PC(BUSTER_AARCH64_CONTROL_ROLE_TARGET)}, .pc_relative = A64C_PC_IMM14,
        .fixup_kind = BUSTER_AARCH64_CONTROL_FIXUP_TEST14, .relocation_policy = BUSTER_AARCH64_CONTROL_RELOCATION_LOCAL_ONLY,
        .default_operand = UINT8_MAX,
    },
};

#define BUSTER_AARCH64_CONTROL_SEMANTIC_GENERATED_ROW_COUNT \
    ((u32)BUSTER_ARRAY_LENGTH(buster_aarch64_control_semantic_generated_rows))

 #undef A64C_STR
#undef A64C_REG
#undef A64C_REG_FP
#undef A64C_REG_POLY
#undef A64C_IMM
#undef A64C_COND
#undef A64C_PC
#undef A64C_PC_NONE
#undef A64C_PC_IMM26
#undef A64C_PC_IMM19
#undef A64C_PC_IMM14
#undef A64C_PC_ADRP
#undef A64C_PC_ADR

#endif
