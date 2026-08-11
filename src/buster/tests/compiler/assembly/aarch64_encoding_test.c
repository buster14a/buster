#include <buster/tests/compiler/assembly/aarch64_encoding_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/string.h>
#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/compiler/assembly/generated/aarch64-form-ids.generated.h>
#include <buster/lib/compiler/assembly/generated/arm-a64-m1-scalar-integer.generated.h>
#if BUSTER_COMPILER_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wignored-attributes"
#endif
#include <buster/lib/compiler/assembly/generated/aarch64-production-plan.generated.h>
#include <buster/lib/compiler/assembly/generated/aarch64-canonical-decoder.generated.h>
#if BUSTER_COMPILER_CLANG
#pragma clang diagnostic pop
#endif
#include <buster/tests/compiler/assembly/generated/aarch64_scalar_integer_corpus.generated.h>

typedef struct A64EncodingCase A64EncodingCase;
struct A64EncodingCase
{
    A64MCInst instruction;
    u32 word;
};

// Independent llvm-mc 22.1.8 byte oracle for the direct Arm XML projection.
// The production rows intentionally contain no oracle words; this table is
// keyed by both the pinned Arm row ID and source digest so row reordering or a
// digest collision cannot silently turn the structural encoder into its own
// oracle.
typedef struct A64M1GprOracle A64M1GprOracle;
struct A64M1GprOracle
{
    String8 arm_row_id;
    u64 arm_row_digest;
    u32 word;
};

static A64M1GprOracle const a64_m1_gpr_oracles[] = {
    {S8_INITIALIZER("arm-a64@2026-06:ADCS_32_addsub_carry"), UINT64_C(0xd1551f6f480968d2), UINT32_C(0x3a030041)},
    {S8_INITIALIZER("arm-a64@2026-06:ADCS_64_addsub_carry"), UINT64_C(0xfcb081d1d648e435), UINT32_C(0xba030041)},
    {S8_INITIALIZER("arm-a64@2026-06:ADC_32_addsub_carry"), UINT64_C(0xc0a727c601476e45), UINT32_C(0x1a030041)},
    {S8_INITIALIZER("arm-a64@2026-06:ADC_64_addsub_carry"), UINT64_C(0x18e0df3f58428a04), UINT32_C(0x9a030041)},
    {S8_INITIALIZER("arm-a64@2026-06:ASRV_32_dp_2src"), UINT64_C(0x9c17d31983922fbf), UINT32_C(0x1ac32841)},
    {S8_INITIALIZER("arm-a64@2026-06:ASRV_64_dp_2src"), UINT64_C(0xf6c86b3ce111a358), UINT32_C(0x9ac32841)},
    {S8_INITIALIZER("arm-a64@2026-06:AUTDA_64P_dp_1src"), UINT64_C(0x37f3371997683e7c), UINT32_C(0xdac11be1)},
    {S8_INITIALIZER("arm-a64@2026-06:AUTDB_64P_dp_1src"), UINT64_C(0x5c21067cadb97f83), UINT32_C(0xdac11fe1)},
    {S8_INITIALIZER("arm-a64@2026-06:AUTDZA_64Z_dp_1src"), UINT64_C(0xf40872610f95ef93), UINT32_C(0xdac13be1)},
    {S8_INITIALIZER("arm-a64@2026-06:AUTDZB_64Z_dp_1src"), UINT64_C(0x6137df9b25844a68), UINT32_C(0xdac13fe1)},
    {S8_INITIALIZER("arm-a64@2026-06:AUTIA_64P_dp_1src"), UINT64_C(0x3e1b3b5148461c7c), UINT32_C(0xdac113e1)},
    {S8_INITIALIZER("arm-a64@2026-06:AUTIB_64P_dp_1src"), UINT64_C(0xd2b63c0598cd60a4), UINT32_C(0xdac117e1)},
    {S8_INITIALIZER("arm-a64@2026-06:AUTIZA_64Z_dp_1src"), UINT64_C(0x7267e4a85985f225), UINT32_C(0xdac133e1)},
    {S8_INITIALIZER("arm-a64@2026-06:AUTIZB_64Z_dp_1src"), UINT64_C(0xf0c58377089a64f5), UINT32_C(0xdac137e1)},
    {S8_INITIALIZER("arm-a64@2026-06:BLRAAZ_64_branch_reg"), UINT64_C(0x427d7e0c95b50b65), UINT32_C(0xd63f083f)},
    {S8_INITIALIZER("arm-a64@2026-06:BLRAA_64P_branch_reg"), UINT64_C(0x4eb447fc25dcb4c9), UINT32_C(0xd73f083f)},
    {S8_INITIALIZER("arm-a64@2026-06:BLRABZ_64_branch_reg"), UINT64_C(0x5ec3a3c52c9f01df), UINT32_C(0xd63f0c3f)},
    {S8_INITIALIZER("arm-a64@2026-06:BLRAB_64P_branch_reg"), UINT64_C(0x86bba71a204b1199), UINT32_C(0xd73f0c3f)},
    {S8_INITIALIZER("arm-a64@2026-06:BLR_64_branch_reg"), UINT64_C(0xcf15e3f6a408975c), UINT32_C(0xd63f0020)},
    {S8_INITIALIZER("arm-a64@2026-06:BRAAZ_64_branch_reg"), UINT64_C(0xd8258b43a9f952cf), UINT32_C(0xd61f083f)},
    {S8_INITIALIZER("arm-a64@2026-06:BRAA_64P_branch_reg"), UINT64_C(0x6da23f3365bdf9a1), UINT32_C(0xd71f083f)},
    {S8_INITIALIZER("arm-a64@2026-06:BRABZ_64_branch_reg"), UINT64_C(0x361c7914e93866ad), UINT32_C(0xd61f0c3f)},
    {S8_INITIALIZER("arm-a64@2026-06:BRAB_64P_branch_reg"), UINT64_C(0xbfeea1c25a0e1bd2), UINT32_C(0xd71f0c3f)},
    {S8_INITIALIZER("arm-a64@2026-06:BR_64_branch_reg"), UINT64_C(0xb20f7a587332bc0c), UINT32_C(0xd61f0020)},
    {S8_INITIALIZER("arm-a64@2026-06:CLS_32_dp_1src"), UINT64_C(0xdd5c61303d5b0bd7), UINT32_C(0x5ac01441)},
    {S8_INITIALIZER("arm-a64@2026-06:CLS_64_dp_1src"), UINT64_C(0x605d789d67b247e5), UINT32_C(0xdac01441)},
    {S8_INITIALIZER("arm-a64@2026-06:CLZ_32_dp_1src"), UINT64_C(0xe9d5b47786509875), UINT32_C(0x5ac01041)},
    {S8_INITIALIZER("arm-a64@2026-06:CLZ_64_dp_1src"), UINT64_C(0x3874a9867eb2bd4a), UINT32_C(0xdac01041)},
    {S8_INITIALIZER("arm-a64@2026-06:CRC32B_32C_dp_2src"), UINT64_C(0x2c200a4733aa2d05), UINT32_C(0x1ac34041)},
    {S8_INITIALIZER("arm-a64@2026-06:CRC32CB_32C_dp_2src"), UINT64_C(0xc13f8adf90f58f49), UINT32_C(0x1ac35041)},
    {S8_INITIALIZER("arm-a64@2026-06:CRC32CH_32C_dp_2src"), UINT64_C(0xc5e4e9cf1d3283a0), UINT32_C(0x1ac35441)},
    {S8_INITIALIZER("arm-a64@2026-06:CRC32CW_32C_dp_2src"), UINT64_C(0xc2d70b2d26125e1b), UINT32_C(0x1ac35841)},
    {S8_INITIALIZER("arm-a64@2026-06:CRC32CX_64C_dp_2src"), UINT64_C(0x7221b56c58861bcc), UINT32_C(0x9ac35c41)},
    {S8_INITIALIZER("arm-a64@2026-06:CRC32H_32C_dp_2src"), UINT64_C(0x97606a60a8dae441), UINT32_C(0x1ac34441)},
    {S8_INITIALIZER("arm-a64@2026-06:CRC32W_32C_dp_2src"), UINT64_C(0xfb5adce732938f9), UINT32_C(0x1ac34841)},
    {S8_INITIALIZER("arm-a64@2026-06:CRC32X_64C_dp_2src"), UINT64_C(0x577efbbeabd91a91), UINT32_C(0x9ac34c41)},
    {S8_INITIALIZER("arm-a64@2026-06:LSLV_32_dp_2src"), UINT64_C(0xb43c04ea8527c479), UINT32_C(0x1ac32041)},
    {S8_INITIALIZER("arm-a64@2026-06:LSLV_64_dp_2src"), UINT64_C(0x807e017963c88882), UINT32_C(0x9ac32041)},
    {S8_INITIALIZER("arm-a64@2026-06:LSRV_32_dp_2src"), UINT64_C(0xae90e1861e1e3432), UINT32_C(0x1ac32441)},
    {S8_INITIALIZER("arm-a64@2026-06:LSRV_64_dp_2src"), UINT64_C(0x466906b42af8104e), UINT32_C(0x9ac32441)},
    {S8_INITIALIZER("arm-a64@2026-06:MADD_32A_dp_3src"), UINT64_C(0x3a1e6468f861d589), UINT32_C(0x1b031041)},
    {S8_INITIALIZER("arm-a64@2026-06:MADD_64A_dp_3src"), UINT64_C(0x337e0a030c060b4d), UINT32_C(0x9b031041)},
    {S8_INITIALIZER("arm-a64@2026-06:MSUB_32A_dp_3src"), UINT64_C(0xde2077b187f931ea), UINT32_C(0x1b039041)},
    {S8_INITIALIZER("arm-a64@2026-06:MSUB_64A_dp_3src"), UINT64_C(0xe46508a1977d55fb), UINT32_C(0x9b039041)},
    {S8_INITIALIZER("arm-a64@2026-06:PACDA_64P_dp_1src"), UINT64_C(0xbebaacea783a689e), UINT32_C(0xdac10be1)},
    {S8_INITIALIZER("arm-a64@2026-06:PACDB_64P_dp_1src"), UINT64_C(0xd5236ec08e09812d), UINT32_C(0xdac10fe1)},
    {S8_INITIALIZER("arm-a64@2026-06:PACDZA_64Z_dp_1src"), UINT64_C(0xc83b224304fda4a0), UINT32_C(0xdac12be1)},
    {S8_INITIALIZER("arm-a64@2026-06:PACDZB_64Z_dp_1src"), UINT64_C(0x82d17a60e72ac2ea), UINT32_C(0xdac12fe1)},
    {S8_INITIALIZER("arm-a64@2026-06:PACGA_64P_dp_2src"), UINT64_C(0xddc151453b0a5f2b), UINT32_C(0x9adf3041)},
    {S8_INITIALIZER("arm-a64@2026-06:PACIA_64P_dp_1src"), UINT64_C(0xec20eefbd54d577), UINT32_C(0xdac103e1)},
    {S8_INITIALIZER("arm-a64@2026-06:PACIB_64P_dp_1src"), UINT64_C(0x2ee8cbc37762d41a), UINT32_C(0xdac107e1)},
    {S8_INITIALIZER("arm-a64@2026-06:PACIZA_64Z_dp_1src"), UINT64_C(0xff8b1e3fbdc86aec), UINT32_C(0xdac123e1)},
    {S8_INITIALIZER("arm-a64@2026-06:PACIZB_64Z_dp_1src"), UINT64_C(0x1b0ef85cb6dcf078), UINT32_C(0xdac127e1)},
    {S8_INITIALIZER("arm-a64@2026-06:RBIT_32_dp_1src"), UINT64_C(0xa445e0a3a42bfa1e), UINT32_C(0x5ac00041)},
    {S8_INITIALIZER("arm-a64@2026-06:RBIT_64_dp_1src"), UINT64_C(0x9daabf3e434d80d8), UINT32_C(0xdac00041)},
    {S8_INITIALIZER("arm-a64@2026-06:REV16_32_dp_1src"), UINT64_C(0xdbb0dfaabdeb7a76), UINT32_C(0x5ac00441)},
    {S8_INITIALIZER("arm-a64@2026-06:REV16_64_dp_1src"), UINT64_C(0x1414b425a944fa65), UINT32_C(0xdac00441)},
    {S8_INITIALIZER("arm-a64@2026-06:REV32_64_dp_1src"), UINT64_C(0x573247d13b39e10b), UINT32_C(0xdac00841)},
    {S8_INITIALIZER("arm-a64@2026-06:REV_32_dp_1src"), UINT64_C(0xeb95cd3e80209f25), UINT32_C(0x5ac00841)},
    {S8_INITIALIZER("arm-a64@2026-06:REV_64_dp_1src"), UINT64_C(0xf81c77ef89c62607), UINT32_C(0xdac00c41)},
    {S8_INITIALIZER("arm-a64@2026-06:RORV_32_dp_2src"), UINT64_C(0x29121d282296d4a4), UINT32_C(0x1ac32c41)},
    {S8_INITIALIZER("arm-a64@2026-06:RORV_64_dp_2src"), UINT64_C(0xbf9de012e36567b9), UINT32_C(0x9ac32c41)},
    {S8_INITIALIZER("arm-a64@2026-06:SBCS_32_addsub_carry"), UINT64_C(0x7b0707c2a50d31c4), UINT32_C(0x7a030041)},
    {S8_INITIALIZER("arm-a64@2026-06:SBCS_64_addsub_carry"), UINT64_C(0xf5e747731966b76), UINT32_C(0xfa030041)},
    {S8_INITIALIZER("arm-a64@2026-06:SBC_32_addsub_carry"), UINT64_C(0xdee58d39ff6b0e38), UINT32_C(0x5a030041)},
    {S8_INITIALIZER("arm-a64@2026-06:SBC_64_addsub_carry"), UINT64_C(0x9a7e8f67f51c9d6f), UINT32_C(0xda030041)},
    {S8_INITIALIZER("arm-a64@2026-06:SDIV_32_dp_2src"), UINT64_C(0xf36d7cf80886295c), UINT32_C(0x1ac30c41)},
    {S8_INITIALIZER("arm-a64@2026-06:SDIV_64_dp_2src"), UINT64_C(0xd133fbacf31cfb6a), UINT32_C(0x9ac30c41)},
    {S8_INITIALIZER("arm-a64@2026-06:SETF16_only_setf"), UINT64_C(0x336d4f0ea92ae737), UINT32_C(0x3a00482d)},
    {S8_INITIALIZER("arm-a64@2026-06:SETF8_only_setf"), UINT64_C(0xb69158ca23ff9ea9), UINT32_C(0x3a00082d)},
    {S8_INITIALIZER("arm-a64@2026-06:SMADDL_64WA_dp_3src"), UINT64_C(0x4fd4647e75534a62), UINT32_C(0x9b231041)},
    {S8_INITIALIZER("arm-a64@2026-06:SMSUBL_64WA_dp_3src"), UINT64_C(0x7f2bf7de683c5bf7), UINT32_C(0x9b239041)},
    {S8_INITIALIZER("arm-a64@2026-06:SMULH_64_dp_3src"), UINT64_C(0x6e03844dbe92c7d), UINT32_C(0x9b437c41)},
    {S8_INITIALIZER("arm-a64@2026-06:UDIV_32_dp_2src"), UINT64_C(0xc8006fe4f008ffae), UINT32_C(0x1ac30841)},
    {S8_INITIALIZER("arm-a64@2026-06:UDIV_64_dp_2src"), UINT64_C(0x5808cb4a9fb4596a), UINT32_C(0x9ac30841)},
    {S8_INITIALIZER("arm-a64@2026-06:UMADDL_64WA_dp_3src"), UINT64_C(0x2e11ff73e2b1ae99), UINT32_C(0x9ba31041)},
    {S8_INITIALIZER("arm-a64@2026-06:UMSUBL_64WA_dp_3src"), UINT64_C(0xa53489734bf5453b), UINT32_C(0x9ba39041)},
    {S8_INITIALIZER("arm-a64@2026-06:UMULH_64_dp_3src"), UINT64_C(0xdd77d750117334ca), UINT32_C(0x9bc37c41)},
    {S8_INITIALIZER("arm-a64@2026-06:XPACD_64Z_dp_1src"), UINT64_C(0x93035be96f05987b), UINT32_C(0xdac147e1)},
    {S8_INITIALIZER("arm-a64@2026-06:XPACI_64Z_dp_1src"), UINT64_C(0x4f27d053e6e24a94), UINT32_C(0xdac143e1)},
};

BUSTER_GLOBAL_LOCAL bool a64_encoding_metadata_string_equal(BusterAarch64MetadataString string, char const* expected)
{
    if (!expected)
    {
        return false;
    }
    u32 index = 0;
    for (;; index += 1)
    {
        char actual = (char)buster_aarch64_metadata_string_byte(string, index);
        if (actual != expected[index])
        {
            return false;
        }
        if (!expected[index])
        {
            return true;
        }
    }
}

BUSTER_GLOBAL_LOCAL bool a64_encoding_round_trip(A64EncodingCase test)
{
    u32 encoded = 0;
    A64MCInst decoded = {0};
    u32 reencoded = 0;
    if (!a64_mc_encode(&test.instruction, &encoded) || encoded != test.word || !a64_mc_decode(encoded, &decoded) || !a64_mc_encode(&decoded, &reencoded) ||
        reencoded != encoded || decoded.opcode != test.instruction.opcode || decoded.operand_count != test.instruction.operand_count)
    {
        return false;
    }
    for (u32 operand = 0; operand < test.instruction.operand_count; operand += 1)
    {
        if (decoded.operands[operand].kind != test.instruction.operands[operand].kind ||
            decoded.operands[operand].value != test.instruction.operands[operand].value)
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL Target a64_encoding_m1_target(bool explicit_features)
{
    Target result = {
        .cpu_arch = CPU_ARCH_AARCH64,
        .cpu_model = CPU_MODEL_A64_APPLE_M1,
        .os = OPERATING_SYSTEM_MACOS,
        .cpu_features_explicit = explicit_features,
    };
    if (explicit_features)
    {
        result.cpu_features = target_cpu_features_default(CPU_ARCH_AARCH64, CPU_MODEL_A64_APPLE_M1);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void aarch64_scalar_test_set_register(A64ScalarIntOperand* operand,
                                                            BusterAarch64ArmM1ScalarIntegerOperand descriptor,
                                                            u8 index, u8 width)
{
    *operand = (A64ScalarIntOperand){
        .kind = A64_SCALAR_INT_OPERAND_REGISTER,
        .width = width,
        .index = index,
        .stack_pointer = index == 31 && descriptor.register31_role == A64_SCALAR_INT_REGISTER31_SP,
    };
}

BUSTER_GLOBAL_LOCAL bool aarch64_scalar_test_fixture_operands(BusterAarch64ArmM1ScalarIntegerForm form,
                                                               A64ScalarIntOperand operands[4],
                                                               A64ScalarIntModifier* modifier,
                                                               u32* modifier_count)
{
    if (!operands || !modifier || !modifier_count) return false;
    for (u32 index = 0; index < 4; index += 1) operands[index] = (A64ScalarIntOperand){0};
    *modifier = (A64ScalarIntModifier){0};
    *modifier_count = 0;
    switch ((BusterAarch64ArmM1ScalarIntegerRecipe)form.recipe)
    {
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_EXT:
        if (form.operand_count != 3) return false;
        aarch64_scalar_test_set_register(operands + 0, form.operands[0], 31, form.operands[0].width);
        aarch64_scalar_test_set_register(operands + 1, form.operands[1], 31, form.operands[1].width);
        aarch64_scalar_test_set_register(operands + 2, form.operands[2], 2, 32);
        *modifier = (A64ScalarIntModifier){
            .kind = A64_SCALAR_INT_MODIFIER_EXTEND,
            .value = A64_SCALAR_INT_EXTEND_UXTW,
            .amount = 1,
            .present = true,
        };
        *modifier_count = 1;
        return true;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_IMM:
        if (form.operand_count != 3) return false;
        aarch64_scalar_test_set_register(operands + 0, form.operands[0], 31, form.operands[0].width);
        aarch64_scalar_test_set_register(operands + 1, form.operands[1], 31, form.operands[1].width);
        operands[2] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 123};
        return true;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_SHIFT:
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT:
        if (form.operand_count != 3) return false;
        aarch64_scalar_test_set_register(operands + 0, form.operands[0], 31, form.operands[0].width);
        aarch64_scalar_test_set_register(operands + 1, form.operands[1], 1, form.operands[1].width);
        aarch64_scalar_test_set_register(operands + 2, form.operands[2], 2, form.operands[2].width);
        *modifier = (A64ScalarIntModifier){
            .kind = A64_SCALAR_INT_MODIFIER_SHIFT,
            .value = form.recipe == BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT ? A64_SCALAR_INT_SHIFT_LSR : A64_SCALAR_INT_SHIFT_LSL,
            .amount = 3,
            .present = true,
        };
        *modifier_count = 1;
        return true;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_IMM:
        if (form.operand_count != 3) return false;
        aarch64_scalar_test_set_register(operands + 0, form.operands[0], 31, form.operands[0].width);
        aarch64_scalar_test_set_register(operands + 1, form.operands[1], 1, form.operands[1].width);
        operands[2] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 0xff};
        return true;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_BITFIELD:
        if (form.operand_count != 4) return false;
        aarch64_scalar_test_set_register(operands + 0, form.operands[0], 31, form.operands[0].width);
        aarch64_scalar_test_set_register(operands + 1, form.operands[1], 1, form.operands[1].width);
        operands[2] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 3};
        operands[3] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 12};
        return true;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_EXTRACT:
        if (form.operand_count != 4) return false;
        aarch64_scalar_test_set_register(operands + 0, form.operands[0], 31, form.operands[0].width);
        aarch64_scalar_test_set_register(operands + 1, form.operands[1], 1, form.operands[1].width);
        aarch64_scalar_test_set_register(operands + 2, form.operands[2], 2, form.operands[2].width);
        operands[3] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 3};
        return true;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_MOVEWIDE:
        if (form.operand_count != 2) return false;
        aarch64_scalar_test_set_register(operands + 0, form.operands[0], 0, form.operands[0].width);
        operands[1] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 0x1234};
        return true;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COND_CMP_IMM:
        if (form.operand_count != 4) return false;
        aarch64_scalar_test_set_register(operands + 0, form.operands[0], 1, form.operands[0].width);
        operands[1] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 7};
        operands[2] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 5};
        operands[3] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 0};
        return true;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COND_CMP_REG:
        if (form.operand_count != 4) return false;
        aarch64_scalar_test_set_register(operands + 0, form.operands[0], 1, form.operands[0].width);
        aarch64_scalar_test_set_register(operands + 1, form.operands[1], 2, form.operands[1].width);
        operands[2] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 5};
        operands[3] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 0};
        return true;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_RMIF:
        if (form.operand_count != 3) return false;
        aarch64_scalar_test_set_register(operands + 0, form.operands[0], 1, form.operands[0].width);
        operands[1] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 3};
        operands[2] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 5};
        return true;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_UDF:
        if (form.operand_count != 1) return false;
        operands[0] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 0x1234};
        return true;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COUNT:
        return false;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool aarch64_scalar_test_decode_logical_immediate(u8 width, u32 n, u32 immr, u32 imms, u64* value)
{
    if (!value || (width != 32 && width != 64) || n > 1 || immr > 63 || imms > 63) return false;
    u32 packed = (n << 6) | ((~imms) & 63u);
    s32 len = -1;
    for (s32 bit = 6; bit >= 0; bit -= 1)
    {
        if (packed & (1u << bit))
        {
            len = bit;
            break;
        }
    }
    if (len < 1 || (width == 32 && (n || len >= 6)) || (width == 64 && len > 6)) return false;
    u32 levels = (1u << len) - 1u;
    u32 s = imms & levels;
    u32 r = immr & levels;
    if (s == levels) return false;
    u32 element_width = 1u << len;
    u64 element_mask = element_width == 64 ? UINT64_MAX : ((UINT64_C(1) << element_width) - 1);
    u64 element = (UINT64_C(1) << (s + 1u)) - 1;
    u32 rotation = element_width ? (r % element_width) : 0;
    if (rotation) element = ((element >> rotation) | (element << (element_width - rotation))) & element_mask;
    u64 result = 0;
    for (u32 offset = 0; offset < width; offset += element_width)
    {
        result |= element << offset;
    }
    *value = result & (width == 32 ? UINT64_C(0xffffffff) : UINT64_MAX);
    return true;
}

UnitTestResult aarch64_encoding_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    u32 encoded = 0;
    s64 decoded = 0;

    BUSTER_TEST(arguments, a64_signed_scaled_immediate_encode(-(INT64_C(1) << 27), 26, 2, &encoded) && encoded == UINT32_C(0x02000000));
    BUSTER_TEST(arguments, a64_signed_scaled_immediate_decode(encoded, 26, 2, &decoded) && decoded == -(INT64_C(1) << 27));
    BUSTER_TEST(arguments, a64_signed_scaled_immediate_encode((INT64_C(1) << 27) - 4, 26, 2, &encoded) && encoded == UINT32_C(0x01ffffff));
    BUSTER_TEST(arguments, a64_signed_scaled_immediate_decode(encoded, 26, 2, &decoded) && decoded == (INT64_C(1) << 27) - 4);
    BUSTER_TEST(arguments, !a64_signed_scaled_immediate_encode(-(INT64_C(1) << 27) - 4, 26, 2, &encoded));
    BUSTER_TEST(arguments, !a64_signed_scaled_immediate_encode(INT64_C(1) << 27, 26, 2, &encoded));
    BUSTER_TEST(arguments, !a64_signed_scaled_immediate_encode(2, 26, 2, &encoded));

    BUSTER_TEST(arguments, a64_signed_scaled_immediate_encode(-(INT64_C(1) << 20), 19, 2, &encoded) && encoded == UINT32_C(0x00040000));
    BUSTER_TEST(arguments, a64_signed_scaled_immediate_encode((INT64_C(1) << 20) - 4, 19, 2, &encoded) && encoded == UINT32_C(0x0003ffff));
    BUSTER_TEST(arguments, !a64_signed_scaled_immediate_encode(-(INT64_C(1) << 20) - 4, 19, 2, &encoded));
    BUSTER_TEST(arguments, !a64_signed_scaled_immediate_encode(INT64_C(1) << 20, 19, 2, &encoded));

    BUSTER_TEST(arguments, a64_signed_scaled_immediate_encode(-INT64_C(0x100000000), 21, 12, &encoded) && encoded == UINT32_C(0x00100000));
    BUSTER_TEST(arguments, a64_signed_scaled_immediate_encode(INT64_C(0xfffff000), 21, 12, &encoded) && encoded == UINT32_C(0x000fffff));
    BUSTER_TEST(arguments, !a64_signed_scaled_immediate_encode(-INT64_C(0x100001000), 21, 12, &encoded));
    BUSTER_TEST(arguments, !a64_signed_scaled_immediate_encode(INT64_C(0x100000000), 21, 12, &encoded));
    BUSTER_TEST(arguments, !a64_signed_scaled_immediate_encode(1, 21, 12, &encoded));

    BUSTER_TEST(arguments, !a64_signed_scaled_immediate_encode(0, 0, 0, &encoded));
    BUSTER_TEST(arguments, !a64_signed_scaled_immediate_encode(0, 33, 0, &encoded));
    BUSTER_TEST(arguments, !a64_signed_scaled_immediate_encode(0, 32, 32, &encoded));
    BUSTER_TEST(arguments, !a64_signed_scaled_immediate_encode(0, 1, 0, 0));
    BUSTER_TEST(arguments, !a64_signed_scaled_immediate_decode(0, 0, 0, &decoded));
    BUSTER_TEST(arguments, !a64_signed_scaled_immediate_decode(2, 1, 0, &decoded));
    BUSTER_TEST(arguments, !a64_signed_scaled_immediate_decode(0, 1, 0, 0));

    s64 displacement = 0;
    BUSTER_TEST(arguments, a64_pc_relative_displacement(4, 0, 0, &displacement) && displacement == 4);
    BUSTER_TEST(arguments, a64_pc_relative_displacement(0, 4, 0, &displacement) && displacement == -4);
    BUSTER_TEST(arguments, a64_pc_relative_displacement((u64)INT64_MAX + 5, 0, INT64_MIN, &displacement) && displacement == 4);
    BUSTER_TEST(arguments, a64_pc_relative_displacement(0, (u64)INT64_MAX + 5, INT64_MAX, &displacement) && displacement == -5);
    BUSTER_TEST(arguments, a64_pc_relative_displacement(UINT64_MAX, (u64)INT64_MAX, INT64_MIN, &displacement) && displacement == 0);
    BUSTER_TEST(arguments, a64_pc_relative_displacement(0, UINT64_MAX, INT64_MAX, &displacement) && displacement == INT64_MIN);
    BUSTER_TEST(arguments, a64_pc_relative_displacement(UINT64_MAX, 0, INT64_MIN, &displacement) && displacement == INT64_MAX);
    BUSTER_TEST(arguments, a64_pc_relative_displacement(0, 0, INT64_MIN, &displacement) && displacement == INT64_MIN);
    BUSTER_TEST(arguments, a64_pc_relative_displacement(0, 0, INT64_MAX, &displacement) && displacement == INT64_MAX);
    BUSTER_TEST(arguments, a64_pc_relative_displacement(0, (u64)INT64_MAX + 1, 0, &displacement) && displacement == INT64_MIN);
    BUSTER_TEST(arguments, !a64_pc_relative_displacement(UINT64_MAX, 0, 0, &displacement));
    BUSTER_TEST(arguments, !a64_pc_relative_displacement(0, UINT64_MAX, 0, &displacement));
    BUSTER_TEST(arguments, !a64_pc_relative_displacement((u64)INT64_MAX, 0, 1, &displacement));
    BUSTER_TEST(arguments, !a64_pc_relative_displacement(0, 0, 0, 0));

    static A64EncodingCase const cases[] = {
        {
            .instruction = {.opcode = A64_OPCODE_NOP},
            .word = UINT32_C(0xd503201f),
        },
        {
            .instruction =
                {
                    .operands = {{.value = 8, .kind = A64_MC_OPERAND_PC_RELATIVE}},
                    .opcode = A64_OPCODE_B,
                    .operand_count = 1,
                },
            .word = UINT32_C(0x14000002),
        },
        {
            .instruction =
                {
                    .operands = {{.value = -4, .kind = A64_MC_OPERAND_PC_RELATIVE}},
                    .opcode = A64_OPCODE_B,
                    .operand_count = 1,
                },
            .word = UINT32_C(0x17ffffff),
        },
        {
            .instruction =
                {
                    .operands = {{.value = 8, .kind = A64_MC_OPERAND_PC_RELATIVE}},
                    .opcode = A64_OPCODE_BL,
                    .operand_count = 1,
                },
            .word = UINT32_C(0x94000002),
        },
        {
            .instruction =
            {
                .operands =
                        {
                            {.value = 4, .kind = A64_MC_OPERAND_PC_RELATIVE},
                            {.value = 0, .kind = A64_MC_OPERAND_IMMEDIATE},
                        },
                    .opcode = A64_OPCODE_B_COND,
                    .operand_count = 2,
                },
            .word = UINT32_C(0x54000020),
        },
        {
            .instruction =
                {
                    .operands = {{.value = 30, .kind = A64_MC_OPERAND_REGISTER}},
                    .opcode = A64_OPCODE_RET,
                    .operand_count = 1,
                },
            .word = UINT32_C(0xd65f03c0),
        },
        {
            .instruction =
                {
                    .operands = {{.value = 16, .kind = A64_MC_OPERAND_REGISTER}},
                    .opcode = A64_OPCODE_BR,
                    .operand_count = 1,
                },
            .word = UINT32_C(0xd61f0200),
        },
        {
            .instruction =
                {
                    .operands = {{.value = 16, .kind = A64_MC_OPERAND_REGISTER}},
                    .opcode = A64_OPCODE_BLR,
                    .operand_count = 1,
                },
            .word = UINT32_C(0xd63f0200),
        },
        {
            .instruction =
                {
                    .operands = {{.value = 31, .kind = A64_MC_OPERAND_REGISTER}},
                    .opcode = A64_OPCODE_RET,
                    .operand_count = 1,
                },
            .word = UINT32_C(0xd65f03e0),
        },
        {
            .instruction =
                {
                    .operands = {{.value = 31, .kind = A64_MC_OPERAND_REGISTER}},
                    .opcode = A64_OPCODE_BR,
                    .operand_count = 1,
                },
            .word = UINT32_C(0xd61f03e0),
        },
        {
            .instruction =
                {
                    .operands = {{.value = 31, .kind = A64_MC_OPERAND_REGISTER}},
                    .opcode = A64_OPCODE_BLR,
                    .operand_count = 1,
                },
            .word = UINT32_C(0xd63f03e0),
        },
        {
            .instruction =
                {
                    .operands =
                        {
                            {.value = -(INT64_C(1) << 20), .kind = A64_MC_OPERAND_PC_RELATIVE},
                            {.kind = A64_MC_OPERAND_IMMEDIATE},
                        },
                    .opcode = A64_OPCODE_B_COND,
                    .operand_count = 2,
                },
            .word = UINT32_C(0x54800000),
        },
        {
            .instruction =
                {
                    .operands =
                        {
                            {.kind = A64_MC_OPERAND_REGISTER},
                            {.value = (INT64_C(1) << 20) - 4, .kind = A64_MC_OPERAND_PC_RELATIVE},
                        },
                    .opcode = A64_OPCODE_LDR_LITERAL_64,
                    .operand_count = 2,
                },
            .word = UINT32_C(0x587fffe0),
        },
        {
            .instruction =
                {
                    .operands =
                        {
                            {.kind = A64_MC_OPERAND_REGISTER},
                            {.value = -INT64_C(0x100000000), .kind = A64_MC_OPERAND_PC_RELATIVE},
                        },
                    .opcode = A64_OPCODE_ADRP,
                    .operand_count = 2,
                },
            .word = UINT32_C(0x90800000),
        },
        {
            .instruction =
                {
                    .operands =
                        {
                            {.kind = A64_MC_OPERAND_REGISTER},
                            {.value = INT64_C(0xfffff000), .kind = A64_MC_OPERAND_PC_RELATIVE},
                        },
                    .opcode = A64_OPCODE_ADRP,
                    .operand_count = 2,
                },
            .word = UINT32_C(0xf07fffe0),
        },
        {
            .instruction =
                {
                    .operands =
                        {
                            {.value = 0, .kind = A64_MC_OPERAND_REGISTER},
                            {.value = 8, .kind = A64_MC_OPERAND_PC_RELATIVE},
                        },
                    .opcode = A64_OPCODE_LDR_LITERAL_64,
                    .operand_count = 2,
                },
            .word = UINT32_C(0x58000040),
        },
        {
            .instruction =
                {
                    .operands =
                        {
                            {.value = 9, .kind = A64_MC_OPERAND_REGISTER},
                            {.value = 8192, .kind = A64_MC_OPERAND_PC_RELATIVE},
                        },
                    .opcode = A64_OPCODE_ADRP,
                    .operand_count = 2,
                },
            .word = UINT32_C(0xd0000009),
        },
        {
            .instruction =
                {
                    .operands =
                        {
                            {.value = 0, .kind = A64_MC_OPERAND_REGISTER},
                            {.value = 0, .kind = A64_MC_OPERAND_PC_RELATIVE},
                        },
                    .opcode = A64_OPCODE_ADR,
                    .operand_count = 2,
                },
            .word = UINT32_C(0x10000000),
        },
        {
            .instruction =
                {
                    .operands =
                        {
                            {.value = 1, .kind = A64_MC_OPERAND_REGISTER},
                            {.value = 4, .kind = A64_MC_OPERAND_PC_RELATIVE},
                        },
                    .opcode = A64_OPCODE_ADR,
                    .operand_count = 2,
                },
            .word = UINT32_C(0x10000021),
        },
        {
            .instruction =
                {
                    .operands =
                        {
                            {.value = 31, .kind = A64_MC_OPERAND_REGISTER},
                            {.value = -1048576, .kind = A64_MC_OPERAND_PC_RELATIVE},
                        },
                    .opcode = A64_OPCODE_ADR,
                    .operand_count = 2,
                },
            .word = UINT32_C(0x1080001f),
        },
        {
            .instruction =
                {
                    .operands =
                        {
                            {.value = 0, .kind = A64_MC_OPERAND_REGISTER},
                            {.value = 1048572, .kind = A64_MC_OPERAND_PC_RELATIVE},
                        },
                    .opcode = A64_OPCODE_CBZ_W,
                    .operand_count = 2,
                },
            .word = UINT32_C(0x347fffe0),
        },
        {
            .instruction =
                {
                    .operands =
                        {
                            {.value = 31, .kind = A64_MC_OPERAND_REGISTER},
                            {.value = -1048576, .kind = A64_MC_OPERAND_PC_RELATIVE},
                        },
                    .opcode = A64_OPCODE_CBNZ_W,
                    .operand_count = 2,
                },
            .word = UINT32_C(0x3580001f),
        },
        {
            .instruction =
                {
                    .operands =
                        {
                            {.value = 2, .kind = A64_MC_OPERAND_REGISTER},
                            {.value = 1048572, .kind = A64_MC_OPERAND_PC_RELATIVE},
                        },
                    .opcode = A64_OPCODE_CBZ_X,
                    .operand_count = 2,
                },
            .word = UINT32_C(0xb47fffe2),
        },
        {
            .instruction =
                {
                    .operands =
                        {
                            {.value = 3, .kind = A64_MC_OPERAND_REGISTER},
                            {.value = -1048576, .kind = A64_MC_OPERAND_PC_RELATIVE},
                        },
                    .opcode = A64_OPCODE_CBNZ_X,
                    .operand_count = 2,
                },
            .word = UINT32_C(0xb5800003),
        },
        {
            .instruction =
                {
                    .operands =
                        {
                            {.value = 0, .kind = A64_MC_OPERAND_REGISTER},
                            {.value = 0, .kind = A64_MC_OPERAND_IMMEDIATE},
                            {.value = 32764, .kind = A64_MC_OPERAND_PC_RELATIVE},
                        },
                    .opcode = A64_OPCODE_TBZ,
                    .operand_count = 3,
                },
            .word = UINT32_C(0x3603ffe0),
        },
        {
            .instruction =
                {
                    .operands =
                        {
                            {.value = 31, .kind = A64_MC_OPERAND_REGISTER},
                            {.value = 31, .kind = A64_MC_OPERAND_IMMEDIATE},
                            {.value = -32768, .kind = A64_MC_OPERAND_PC_RELATIVE},
                        },
                    .opcode = A64_OPCODE_TBZ,
                    .operand_count = 3,
                },
            .word = UINT32_C(0x36fc001f),
        },
        {
            .instruction =
                {
                    .operands =
                        {
                            {.value = 0, .kind = A64_MC_OPERAND_REGISTER},
                            {.value = 32, .kind = A64_MC_OPERAND_IMMEDIATE},
                            {.value = 4, .kind = A64_MC_OPERAND_PC_RELATIVE},
                        },
                    .opcode = A64_OPCODE_TBZ,
                    .operand_count = 3,
                },
            .word = UINT32_C(0xb6000020),
        },
        {
            .instruction =
                {
                    .operands =
                        {
                            {.value = 31, .kind = A64_MC_OPERAND_REGISTER},
                            {.value = 63, .kind = A64_MC_OPERAND_IMMEDIATE},
                            {.value = -4, .kind = A64_MC_OPERAND_PC_RELATIVE},
                        },
                    .opcode = A64_OPCODE_TBNZ,
                    .operand_count = 3,
                },
            .word = UINT32_C(0xb7ffffff),
        },
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        BUSTER_TEST(arguments, a64_encoding_round_trip(cases[index]));
    }
    // Exercise every legal TBZ/TBNZ bit selector and register spelling. The
    // high bit is the architectural width discriminator, so this also covers
    // the W (0..31) and X (32..63) domains without a second opcode family.
    static A64Opcode const bit_branch_opcodes[] = {A64_OPCODE_TBZ, A64_OPCODE_TBNZ};
    for (u32 bit = 0; bit < 64; bit += 1)
    {
        for (u32 opcode_index = 0; opcode_index < BUSTER_ARRAY_LENGTH(bit_branch_opcodes); opcode_index += 1)
        {
            A64MCInst exhaustive = {
                .operands =
                    {
                        {.value = bit & 31, .kind = A64_MC_OPERAND_REGISTER},
                        {.value = bit, .kind = A64_MC_OPERAND_IMMEDIATE},
                        {.value = 0, .kind = A64_MC_OPERAND_PC_RELATIVE},
                    },
                .opcode = bit_branch_opcodes[opcode_index],
                .operand_count = 3,
            };
            u32 exhaustive_word = 0;
            A64EncodingCase exhaustive_case = {.instruction = exhaustive, .word = 0};
            bool exhaustive_encoded = a64_mc_encode(&exhaustive, &exhaustive_word);
            exhaustive_case.word = exhaustive_word;
            BUSTER_TEST(arguments, exhaustive_encoded && a64_encoding_round_trip(exhaustive_case));
        }
    }
    // Every architectural register is accepted by the four CBZ/CBNZ width
    // forms and ADR; the descriptors must not accidentally reserve XZR/WZR.
    static A64Opcode const register_branch_opcodes[] = {A64_OPCODE_CBZ_W, A64_OPCODE_CBNZ_W, A64_OPCODE_CBZ_X, A64_OPCODE_CBNZ_X};
    for (u32 register_number = 0; register_number < 32; register_number += 1)
    {
        for (u32 opcode_index = 0; opcode_index < BUSTER_ARRAY_LENGTH(register_branch_opcodes); opcode_index += 1)
        {
            A64MCInst exhaustive = {
                .operands =
                    {
                        {.value = register_number, .kind = A64_MC_OPERAND_REGISTER},
                        {.value = 0, .kind = A64_MC_OPERAND_PC_RELATIVE},
                    },
                .opcode = register_branch_opcodes[opcode_index],
                .operand_count = 2,
            };
            A64MCInst exhaustive_decoded = {0};
            u32 exhaustive_encoded = 0;
            u32 exhaustive_reencoded = 0;
            BUSTER_TEST(arguments, a64_mc_encode(&exhaustive, &exhaustive_encoded) && a64_mc_decode(exhaustive_encoded, &exhaustive_decoded) &&
                                      a64_mc_encode(&exhaustive_decoded, &exhaustive_reencoded) && exhaustive_encoded == exhaustive_reencoded &&
                                      exhaustive_decoded.operands[0].value == (s64)register_number);
        }
        A64MCInst exhaustive = {
            .operands =
                {
                    {.value = register_number, .kind = A64_MC_OPERAND_REGISTER},
                    {.value = 0, .kind = A64_MC_OPERAND_PC_RELATIVE},
                },
            .opcode = A64_OPCODE_ADR,
            .operand_count = 2,
        };
        A64MCInst exhaustive_decoded = {0};
        u32 exhaustive_encoded = 0;
        u32 exhaustive_reencoded = 0;
        BUSTER_TEST(arguments, a64_mc_encode(&exhaustive, &exhaustive_encoded) && a64_mc_decode(exhaustive_encoded, &exhaustive_decoded) &&
                                  a64_mc_encode(&exhaustive_decoded, &exhaustive_reencoded) && exhaustive_encoded == exhaustive_reencoded &&
                                  exhaustive_decoded.operands[0].value == (s64)register_number);
    }
    BUSTER_TEST(arguments, !a64_opcode_descriptor(A64_OPCODE_INVALID));
    BUSTER_TEST(arguments, !a64_opcode_descriptor(A64_OPCODE_COUNT));
    for (A64Opcode opcode = A64_OPCODE_NOP; opcode < A64_OPCODE_COUNT; opcode += 1)
    {
        A64OpcodeDescriptor const* descriptor = a64_opcode_descriptor(opcode);
        BUSTER_TEST(arguments, descriptor != 0);
        for (A64Opcode other = opcode + 1; other < A64_OPCODE_COUNT; other += 1)
        {
            A64OpcodeDescriptor const* other_descriptor = a64_opcode_descriptor(other);
            u32 shared_mask = descriptor->fixed_mask & other_descriptor->fixed_mask;
            BUSTER_TEST(arguments, ((descriptor->fixed_value ^ other_descriptor->fixed_value) & shared_mask) != 0);
        }
    }

    A64MCInst invalid = {
        .operands = {{.value = INT64_C(1) << 27, .kind = A64_MC_OPERAND_PC_RELATIVE}},
        .opcode = A64_OPCODE_B,
        .operand_count = 1,
    };
    BUSTER_TEST(arguments, !a64_mc_encode(&invalid, &encoded));
    invalid.operands[0].value = 2;
    BUSTER_TEST(arguments, !a64_mc_encode(&invalid, &encoded));
    invalid = (A64MCInst){
        .operands = {{.value = 32, .kind = A64_MC_OPERAND_REGISTER}},
        .opcode = A64_OPCODE_RET,
        .operand_count = 1,
    };
    BUSTER_TEST(arguments, !a64_mc_encode(&invalid, &encoded));
    invalid.operands[0].value = 30;
    invalid.operands[0].kind = A64_MC_OPERAND_IMMEDIATE;
    BUSTER_TEST(arguments, !a64_mc_encode(&invalid, &encoded));
    invalid.opcode = A64_OPCODE_INVALID;
    BUSTER_TEST(arguments, !a64_mc_encode(&invalid, &encoded));
    BUSTER_TEST(arguments, !a64_mc_encode(0, &encoded));
    BUSTER_TEST(arguments, !a64_mc_encode(&invalid, 0));
    BUSTER_TEST(arguments, !a64_mc_decode(0, &invalid));
    BUSTER_TEST(arguments, !a64_mc_decode(UINT32_C(0xd503201f), 0));

    u32 patched = 0;
    BUSTER_TEST(arguments, a64_pc_relative_patch(A64_OPCODE_B, UINT32_C(0x14000000), -4, &patched) && patched == UINT32_C(0x17ffffff));
    BUSTER_TEST(arguments, a64_pc_relative_patch(A64_OPCODE_BL, UINT32_C(0x94000000), 8, &patched) && patched == UINT32_C(0x94000002));
    BUSTER_TEST(arguments, a64_pc_relative_patch(A64_OPCODE_B_COND, UINT32_C(0x5400000d), 4, &patched) && patched == UINT32_C(0x5400002d));
    BUSTER_TEST(arguments, a64_pc_relative_patch(A64_OPCODE_LDR_LITERAL_64, UINT32_C(0x5800001f), -4, &patched) && patched == UINT32_C(0x58ffffff));
    BUSTER_TEST(arguments, a64_pc_relative_patch(A64_OPCODE_ADRP, UINT32_C(0x90000009), 8192, &patched) && patched == UINT32_C(0xd0000009));
    BUSTER_TEST(arguments, !a64_pc_relative_patch(A64_OPCODE_B, UINT32_C(0x94000000), 0, &patched));
    BUSTER_TEST(arguments, !a64_pc_relative_patch(A64_OPCODE_RET, UINT32_C(0xd65f03c0), 0, &patched));
    BUSTER_TEST(arguments, !a64_pc_relative_patch(A64_OPCODE_B, UINT32_C(0x14000000), INT64_C(1) << 27, &patched));
    BUSTER_TEST(arguments, !a64_pc_relative_patch(A64_OPCODE_B, UINT32_C(0x14000000), 2, &patched));
    BUSTER_TEST(arguments, !a64_pc_relative_patch(A64_OPCODE_B, UINT32_C(0x14000000), 0, 0));

    // The new M1 branch forms use the same checked insertion path. Patching
    // must retain every fixed and non-relocation bit (register, TBZ bit, and
    // the opcode's width/condition bits).
    BUSTER_TEST(arguments, a64_pc_relative_patch(A64_OPCODE_ADR, UINT32_C(0x1000001f), -4, &patched) && patched == UINT32_C(0x10ffffff));
    BUSTER_TEST(arguments, a64_pc_relative_patch(A64_OPCODE_CBZ_W, UINT32_C(0x3400001f), 4, &patched) && patched == UINT32_C(0x3400003f));
    BUSTER_TEST(arguments, a64_pc_relative_patch(A64_OPCODE_CBNZ_X, UINT32_C(0xb5000003), -4, &patched) && patched == UINT32_C(0xb5ffffe3));
    BUSTER_TEST(arguments, a64_pc_relative_patch(A64_OPCODE_TBZ, UINT32_C(0x360c001f), 32764, &patched) && patched == UINT32_C(0x360bffff));
    BUSTER_TEST(arguments, a64_pc_relative_patch(A64_OPCODE_TBNZ, UINT32_C(0xb7fc001f), -32768, &patched) && patched == UINT32_C(0xb7fc001f));
    BUSTER_TEST(arguments, !a64_pc_relative_patch(A64_OPCODE_ADR, UINT32_C(0x90000000), 0, &patched));
    BUSTER_TEST(arguments, !a64_pc_relative_patch(A64_OPCODE_CBZ_W, UINT32_C(0xb4000000), 0, &patched));
    BUSTER_TEST(arguments, !a64_pc_relative_patch(A64_OPCODE_TBZ, UINT32_C(0x37000000), 0, &patched));
    BUSTER_TEST(arguments, !a64_pc_relative_patch(A64_OPCODE_ADR, UINT32_C(0x10000000), INT64_C(1) << 20, &patched));
    BUSTER_TEST(arguments, !a64_pc_relative_patch(A64_OPCODE_ADR, UINT32_C(0x10000000), -((INT64_C(1) << 20) + 1), &patched));
    BUSTER_TEST(arguments, !a64_pc_relative_patch(A64_OPCODE_CBZ_W, UINT32_C(0x34000000), INT64_C(1) << 20, &patched));
    BUSTER_TEST(arguments, !a64_pc_relative_patch(A64_OPCODE_CBZ_W, UINT32_C(0x34000000), -(INT64_C(1) << 20) - 4, &patched));
    BUSTER_TEST(arguments, !a64_pc_relative_patch(A64_OPCODE_TBZ, UINT32_C(0x36000000), INT64_C(32768), &patched));
    BUSTER_TEST(arguments, !a64_pc_relative_patch(A64_OPCODE_TBZ, UINT32_C(0x36000000), -INT64_C(32772), &patched));

    invalid = (A64MCInst){
        .operands =
            {
                {.value = 32, .kind = A64_MC_OPERAND_REGISTER},
                {.value = 0, .kind = A64_MC_OPERAND_PC_RELATIVE},
            },
        .opcode = A64_OPCODE_ADR,
        .operand_count = 2,
    };
    BUSTER_TEST(arguments, !a64_mc_encode(&invalid, &encoded));
    invalid.operands[0].value = 0;
    invalid.operands[1].value = INT64_C(1) << 20;
    BUSTER_TEST(arguments, !a64_mc_encode(&invalid, &encoded));
    invalid = (A64MCInst){
        .operands =
            {
                {.value = 0, .kind = A64_MC_OPERAND_REGISTER},
                {.value = 0, .kind = A64_MC_OPERAND_PC_RELATIVE},
            },
        .opcode = A64_OPCODE_CBZ_X,
        .operand_count = 2,
    };
    invalid.operands[0].value = -1;
    BUSTER_TEST(arguments, !a64_mc_encode(&invalid, &encoded));
    invalid.operands[0].value = 0;
    invalid.operands[1].value = 1;
    BUSTER_TEST(arguments, !a64_mc_encode(&invalid, &encoded));
    invalid = (A64MCInst){
        .operands =
            {
                {.value = 0, .kind = A64_MC_OPERAND_REGISTER},
                {.value = 64, .kind = A64_MC_OPERAND_IMMEDIATE},
                {.value = 0, .kind = A64_MC_OPERAND_PC_RELATIVE},
            },
        .opcode = A64_OPCODE_TBZ,
        .operand_count = 3,
    };
    BUSTER_TEST(arguments, !a64_mc_encode(&invalid, &encoded));
    invalid.operands[1].value = -1;
    BUSTER_TEST(arguments, !a64_mc_encode(&invalid, &encoded));
    invalid.operands[1].value = 0;
    invalid.operands[2].value = 2;
    BUSTER_TEST(arguments, !a64_mc_encode(&invalid, &encoded));
    invalid.operands[2].value = 0;
    invalid.operands[0].value = 32;
    BUSTER_TEST(arguments, !a64_mc_encode(&invalid, &encoded));
    invalid.operands[0].value = 0;
    invalid.operand_count = 2;
    BUSTER_TEST(arguments, !a64_mc_encode(&invalid, &encoded));

    BUSTER_TEST(arguments, a64_adr_encode(1, UINT64_C(0x1000), UINT64_C(0x1004), &encoded) && encoded == UINT32_C(0x10000021));
    BUSTER_TEST(arguments, a64_adr_encode(31, UINT64_C(0x100000), 0, &encoded) && encoded == UINT32_C(0x1080001f));
    BUSTER_TEST(arguments, a64_adr_encode(0, 0, UINT64_C(0xfffff), &encoded) && encoded == UINT32_C(0x707fffe0));
    BUSTER_TEST(arguments, a64_adr_encode(0, UINT64_MAX - 3, 0, &encoded) && encoded == UINT32_C(0x10000020));
    BUSTER_TEST(arguments, a64_adr_encode(0, 0, UINT64_MAX - 3, &encoded) && encoded == UINT32_C(0x10ffffe0));
    BUSTER_TEST(arguments, !a64_adr_encode(0, 0, UINT64_C(0x100000), &encoded));
    BUSTER_TEST(arguments, !a64_adr_encode(0, UINT64_C(0x100001), 0, &encoded));
    BUSTER_TEST(arguments, !a64_adr_encode(32, 0, 0, &encoded));
    BUSTER_TEST(arguments, !a64_adr_encode(0, 0, 0, 0));

    BUSTER_TEST(arguments, a64_adrp_encode(9, UINT64_C(0x1000), UINT64_C(0x3000), &encoded) && encoded == UINT32_C(0xd0000009));
    BUSTER_TEST(arguments, a64_adrp_encode(9, UINT64_C(0x3000), UINT64_C(0x1000), &encoded) && encoded == UINT32_C(0xd0ffffe9));
    BUSTER_TEST(arguments, a64_adrp_encode(31, UINT64_C(0x1fff), UINT64_C(0x3abc), &encoded) && encoded == UINT32_C(0xd000001f));
    BUSTER_TEST(arguments, a64_adrp_encode(0, UINT64_C(0xfffffffffffff000), 0, &encoded) && encoded == UINT32_C(0xb0000000));
    BUSTER_TEST(arguments, a64_adrp_encode(0, 0, UINT64_C(0xfffffffffffff000), &encoded) && encoded == UINT32_C(0xf0ffffe0));
    BUSTER_TEST(arguments, !a64_adrp_encode(32, 0, 0, &encoded));
    BUSTER_TEST(arguments, !a64_adrp_encode(0, 0, UINT64_C(0x100000000), &encoded));
    BUSTER_TEST(arguments, !a64_adrp_encode(0, UINT64_C(0x100001000), 0, &encoded));
    BUSTER_TEST(arguments, !a64_adrp_encode(0, 0, 0, 0));

    u32 inverse = 0;
    BUSTER_TEST(arguments, a64_condition_invert(0, &inverse) && inverse == 1);
    BUSTER_TEST(arguments, a64_condition_invert(1, &inverse) && inverse == 0);
    BUSTER_TEST(arguments, a64_condition_invert(12, &inverse) && inverse == 13);
    BUSTER_TEST(arguments, !a64_condition_invert(14, &inverse));
    BUSTER_TEST(arguments, !a64_condition_invert(15, &inverse));
    BUSTER_TEST(arguments, !a64_condition_invert(0, 0));

    // The checked-in packed snapshot is part of the runtime ABI. Keep the
    // exact shape counts and M1 policy census here so a regenerated table
    // cannot silently change the denominator.
    BusterAarch64MetadataCounts metadata_counts = buster_aarch64_metadata_counts();
    BUSTER_TEST(arguments, buster_aarch64_metadata_schema_version() == 6);
    BUSTER_TEST(arguments, metadata_counts.form_count == 7491 && metadata_counts.field_count == 22631 && metadata_counts.segment_count == 23039 &&
                              metadata_counts.operand_count == 26262 && metadata_counts.predicate_count == 7855 && metadata_counts.string_pool_size == 337490);
    BUSTER_TEST(arguments, metadata_counts.apple_m1_supported_count == 2898 && metadata_counts.apple_m1_raw_layout_complete_count == 2898 &&
                              metadata_counts.apple_m1_raw_layout_incomplete_count == 0);

    // The packed LLVM mnemonic index is a candidate catalog, not an encoder
    // denominator. Exercise every range, candidate bound, case-folded lookup,
    // and malformed caller-supplied descriptor through the public ABI.
    u32 mnemonic_range_count = buster_aarch64_metadata_mnemonic_range_count();
    BUSTER_TEST(arguments, mnemonic_range_count == 1557);
    BUSTER_TEST(arguments, !buster_aarch64_metadata_mnemonic_range(mnemonic_range_count, 0));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_mnemonic_range(0, 0));
    BusterAarch64MetadataCandidateRange first_mnemonic_range = {0};
    for (u32 range_index = 0; range_index < mnemonic_range_count; range_index += 1)
    {
        BusterAarch64MetadataCandidateRange range = {0};
        BUSTER_TEST(arguments, buster_aarch64_metadata_mnemonic_range(range_index, &range));
        if (!range.key.length)
        {
            continue;
        }
        if (!first_mnemonic_range.key.length)
        {
            first_mnemonic_range = range;
        }
        BUSTER_TEST(arguments, range.candidate_count != 0);
        BUSTER_TEST(arguments, range.candidate_first <= UINT32_MAX - range.candidate_count);
        char8* mixed_key = arena_allocate(arguments->arena, char8, range.key.length);
        for (u32 key_index = 0; key_index < range.key.length; key_index += 1)
        {
            char8 key_byte = (char8)buster_aarch64_metadata_string_byte(range.key, key_index);
            mixed_key[key_index] = (key_index & 1) && key_byte >= 'a' && key_byte <= 'z' ? (char8)(key_byte - ('a' - 'A')) : key_byte;
        }
        BusterAarch64MetadataCandidateRange looked_up = {0};
        BUSTER_TEST(arguments, buster_aarch64_metadata_mnemonic_lookup((String8){.pointer = mixed_key, .length = range.key.length}, &looked_up) &&
                                  looked_up.key.offset == range.key.offset && looked_up.key.length == range.key.length &&
                                  looked_up.candidate_first == range.candidate_first && looked_up.candidate_count == range.candidate_count);
        for (u32 candidate_index = 0; candidate_index < range.candidate_count; candidate_index += 1)
        {
            u32 form_id = UINT32_MAX;
            BUSTER_TEST(arguments, buster_aarch64_metadata_mnemonic_candidate(range, candidate_index, &form_id) && form_id < metadata_counts.form_count);
        }
        u32 ignored_form = 0;
        BUSTER_TEST(arguments, !buster_aarch64_metadata_mnemonic_candidate(range, range.candidate_count, &ignored_form));
        BUSTER_TEST(arguments, !buster_aarch64_metadata_mnemonic_candidate(range, 0, 0));
    }
    BUSTER_TEST(arguments, first_mnemonic_range.key.length != 0);
    BusterAarch64MetadataCandidateRange malformed_range = first_mnemonic_range;
    malformed_range.key.offset = metadata_counts.string_pool_size - 1;
    malformed_range.key.length = 2;
    u32 ignored_form = 0;
    BUSTER_TEST(arguments, !buster_aarch64_metadata_mnemonic_candidate(malformed_range, 0, &ignored_form));
    malformed_range.key.offset = UINT32_MAX;
    malformed_range.key.length = 1;
    BUSTER_TEST(arguments, !buster_aarch64_metadata_mnemonic_candidate(malformed_range, 0, &ignored_form));
    malformed_range = first_mnemonic_range;
    malformed_range.candidate_first = UINT32_MAX;
    malformed_range.candidate_count = 2;
    BUSTER_TEST(arguments, !buster_aarch64_metadata_mnemonic_candidate(malformed_range, 0, &ignored_form));
    BusterAarch64MetadataCandidateRange nop_range = {0};
    BUSTER_TEST(arguments, buster_aarch64_metadata_mnemonic_lookup(S8("aDd"), &nop_range));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_mnemonic_lookup(S8("definitely-not-a-mnemonic"), &nop_range));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_mnemonic_lookup(S8("add"), 0));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_mnemonic_lookup((String8){0}, &nop_range));

    // Arm XML fixed-mask closure: every exported row is checked against the
    // literal 32-bit word, provenance digest, spelling, and census metadata.
    typedef struct A64FixedExpected A64FixedExpected;
    struct A64FixedExpected
    {
        char const* spelling;
        u32 word;
        u64 digest;
        TargetCpuFeature required_feature;
        bool canonical;
        bool alias;
        bool system;
    };
    static A64FixedExpected const fixed_expected[] = {
        {"AUTIA1716", UINT32_C(0xd503219f), UINT64_C(0xefb27a8829041858), TARGET_CPU_FEATURE_AARCH64_PAUTH, true, false, false},
        {"AUTIASP", UINT32_C(0xd50323bf), UINT64_C(0x563e5337e95037c8), TARGET_CPU_FEATURE_AARCH64_PAUTH, true, false, false},
        {"AUTIAZ", UINT32_C(0xd503239f), UINT64_C(0x3b2162463284163b), TARGET_CPU_FEATURE_AARCH64_PAUTH, true, false, false},
        {"AUTIB1716", UINT32_C(0xd50321df), UINT64_C(0xef413b33019b5154), TARGET_CPU_FEATURE_AARCH64_PAUTH, true, false, false},
        {"AUTIBSP", UINT32_C(0xd50323ff), UINT64_C(0xc24e0c31db8c07b6), TARGET_CPU_FEATURE_AARCH64_PAUTH, true, false, false},
        {"AUTIBZ", UINT32_C(0xd50323df), UINT64_C(0xff7a557019ed0c15), TARGET_CPU_FEATURE_AARCH64_PAUTH, true, false, false},
        {"AXFLAG", UINT32_C(0xd500405f), UINT64_C(0x9a1098969eadbf1b), TARGET_CPU_FEATURE_AARCH64_ALTNZCV, true, false, true},
        {"CFINV", UINT32_C(0xd500401f), UINT64_C(0x7c6494a34efc7074), TARGET_CPU_FEATURE_AARCH64_FLAGM, true, false, true},
        {"CSDB", UINT32_C(0xd503229f), UINT64_C(0xeb5fc198be338b4a), TARGET_CPU_FEATURE_NONE, true, false, true},
        {"DRPS", UINT32_C(0xd6bf03e0), UINT64_C(0x2ec2197eddbee02e), TARGET_CPU_FEATURE_NONE, true, false, true},
        {"ERETAA", UINT32_C(0xd69f0bff), UINT64_C(0x49992ba5e5de1e00), TARGET_CPU_FEATURE_AARCH64_PAUTH, true, false, false},
        {"ERETAB", UINT32_C(0xd69f0fff), UINT64_C(0xb72a8d3c14e2390c), TARGET_CPU_FEATURE_AARCH64_PAUTH, true, false, false},
        {"ERET", UINT32_C(0xd69f03e0), UINT64_C(0xf9516304bbfb8c5e), TARGET_CPU_FEATURE_NONE, true, false, true},
        {"ESB", UINT32_C(0xd503221f), UINT64_C(0xbfde8544ad7fd2fa), TARGET_CPU_FEATURE_AARCH64_RAS, true, false, true},
        {"NOP", UINT32_C(0xd503201f), UINT64_C(0x3b01271fe0dfcf46), TARGET_CPU_FEATURE_NONE, true, false, true},
        {"PACIA1716", UINT32_C(0xd503211f), UINT64_C(0xfa1150156926b746), TARGET_CPU_FEATURE_AARCH64_PAUTH, true, false, false},
        {"PACIASP", UINT32_C(0xd503233f), UINT64_C(0x10a9ef48de927cf7), TARGET_CPU_FEATURE_AARCH64_PAUTH, true, false, false},
        {"PACIAZ", UINT32_C(0xd503231f), UINT64_C(0xf316663792336172), TARGET_CPU_FEATURE_AARCH64_PAUTH, true, false, false},
        {"PACIB1716", UINT32_C(0xd503215f), UINT64_C(0xff545796c103918b), TARGET_CPU_FEATURE_AARCH64_PAUTH, true, false, false},
        {"PACIBSP", UINT32_C(0xd503237f), UINT64_C(0xf9b7fd3f3db149ac), TARGET_CPU_FEATURE_AARCH64_PAUTH, true, false, false},
        {"PACIBZ", UINT32_C(0xd503235f), UINT64_C(0x55f9cf2a51d6529b), TARGET_CPU_FEATURE_AARCH64_PAUTH, true, false, false},
        {"PSSBB", UINT32_C(0xd503349f), UINT64_C(0x9e6e759013b7466e), TARGET_CPU_FEATURE_NONE, false, true, true},
        {"RETAA", UINT32_C(0xd65f0bff), UINT64_C(0x954058a92951882), TARGET_CPU_FEATURE_AARCH64_PAUTH, true, false, false},
        {"RETAB", UINT32_C(0xd65f0fff), UINT64_C(0x36a796e9b30d7c67), TARGET_CPU_FEATURE_AARCH64_PAUTH, true, false, false},
        {"SB", UINT32_C(0xd50330ff), UINT64_C(0xc86254b066d190fe), TARGET_CPU_FEATURE_AARCH64_SB, true, false, true},
        {"SEVL", UINT32_C(0xd50320bf), UINT64_C(0xd906cce54ea67a0), TARGET_CPU_FEATURE_NONE, true, false, true},
        {"SEV", UINT32_C(0xd503209f), UINT64_C(0xff8b2b9798e3a6f5), TARGET_CPU_FEATURE_NONE, true, false, true},
        {"SSBB", UINT32_C(0xd503309f), UINT64_C(0xa367b96ae742f2a8), TARGET_CPU_FEATURE_NONE, false, true, true},
        {"TSB CSYNC", UINT32_C(0xd503225f), UINT64_C(0xcd0cf0852ff7a7dc), TARGET_CPU_FEATURE_AARCH64_TRACEV8_4, true, false, true},
        {"WFE", UINT32_C(0xd503205f), UINT64_C(0xe8e98d89711dbbdc), TARGET_CPU_FEATURE_NONE, true, false, true},
        {"WFI", UINT32_C(0xd503207f), UINT64_C(0x210a4c58e84c8a08), TARGET_CPU_FEATURE_NONE, true, false, true},
        {"XAFLAG", UINT32_C(0xd500403f), UINT64_C(0x26c8f3cddcd55c54), TARGET_CPU_FEATURE_AARCH64_ALTNZCV, true, false, true},
        {"XPACLRI", UINT32_C(0xd50320ff), UINT64_C(0x2329fb25fbc74c54), TARGET_CPU_FEATURE_AARCH64_PAUTH, true, false, false},
        {"YIELD", UINT32_C(0xd503203f), UINT64_C(0x9605e6ab9956281b), TARGET_CPU_FEATURE_NONE, true, false, true},
    };
    BUSTER_TEST(arguments, buster_aarch64_arm_m1_fixed_spelling_count() == BUSTER_ARRAY_LENGTH(fixed_expected));
    u32 fixed_canonical_count = 0;
    u32 fixed_alias_count = 0;
    u32 fixed_system_count = 0;
    for (u32 fixed_index = 0; fixed_index < BUSTER_ARRAY_LENGTH(fixed_expected); fixed_index += 1)
    {
        BusterAarch64ArmM1FixedSpelling fixed = {0};
        BUSTER_TEST(arguments, buster_aarch64_arm_m1_fixed_spelling(fixed_index, &fixed));
        BUSTER_TEST(arguments, string_equal(fixed.spelling, string_from_pointer((char8*)fixed_expected[fixed_index].spelling)) &&
                                  fixed.word == fixed_expected[fixed_index].word && fixed.arm_row_digest == fixed_expected[fixed_index].digest &&
                                  fixed.required_feature == fixed_expected[fixed_index].required_feature &&
                                  fixed.canonical == fixed_expected[fixed_index].canonical && fixed.alias == fixed_expected[fixed_index].alias &&
                                  fixed.system == fixed_expected[fixed_index].system);
        BusterAarch64ArmM1FixedSpelling looked_up_fixed = {0};
        BUSTER_TEST(arguments, buster_aarch64_arm_m1_fixed_lookup(fixed.spelling, &looked_up_fixed) && looked_up_fixed.word == fixed.word);
        fixed_canonical_count += fixed.canonical;
        fixed_alias_count += fixed.alias;
        fixed_system_count += fixed.system;
    }
    BUSTER_TEST(arguments, fixed_canonical_count == 32 && fixed_alias_count == 2 && fixed_system_count == 17);
    BusterAarch64ArmM1FixedSpelling fixed = {0};
    BUSTER_TEST(arguments, buster_aarch64_arm_m1_fixed_lookup(S8("aUtIaSp"), &fixed) && fixed.word == UINT32_C(0xd50323bf));
    BUSTER_TEST(arguments, buster_aarch64_arm_m1_fixed_lookup(S8("tsb   csync"), &fixed) && fixed.word == UINT32_C(0xd503225f));
    BUSTER_TEST(arguments, !buster_aarch64_arm_m1_fixed_lookup(S8("TSB C"), &fixed));
    BUSTER_TEST(arguments, !buster_aarch64_arm_m1_fixed_lookup(S8("NOP extra"), &fixed));
    BUSTER_TEST(arguments, !buster_aarch64_arm_m1_fixed_lookup(S8("AUTIASP, x0"), &fixed));
    BUSTER_TEST(arguments, !buster_aarch64_arm_m1_fixed_spelling(buster_aarch64_arm_m1_fixed_spelling_count(), &fixed));
    BUSTER_TEST(arguments, !buster_aarch64_arm_m1_fixed_spelling(0, 0));
    Target fixed_m1_target = a64_encoding_m1_target(false);
    BUSTER_TEST(arguments, buster_aarch64_arm_m1_fixed_target(fixed_m1_target));
    Target generic_m1_arch = fixed_m1_target;
    generic_m1_arch.cpu_model = CPU_MODEL_A64_GENERIC;
    BUSTER_TEST(arguments, !buster_aarch64_arm_m1_fixed_target(generic_m1_arch));
    Target x86_m1_arch = fixed_m1_target;
    x86_m1_arch.cpu_arch = CPU_ARCH_X86_64;
    BUSTER_TEST(arguments, !buster_aarch64_arm_m1_fixed_target(x86_m1_arch));
    Target fixed_explicit_target = a64_encoding_m1_target(true);
    TargetCpuFeature removable_fixed_features[] = {
        TARGET_CPU_FEATURE_AARCH64_ALTNZCV,
        TARGET_CPU_FEATURE_AARCH64_PAUTH,
        TARGET_CPU_FEATURE_AARCH64_RAS,
        TARGET_CPU_FEATURE_AARCH64_SB,
        TARGET_CPU_FEATURE_AARCH64_TRACEV8_4,
    };
    for (u32 feature_index = 0; feature_index < BUSTER_ARRAY_LENGTH(removable_fixed_features); feature_index += 1)
    {
        Target without_feature = fixed_explicit_target;
        without_feature.cpu_features = target_cpu_features_remove(without_feature.cpu_features, removable_fixed_features[feature_index]);
        BUSTER_TEST(arguments, target_cpu_features_are_valid(without_feature));
        for (u32 fixed_index = 0; fixed_index < BUSTER_ARRAY_LENGTH(fixed_expected); fixed_index += 1)
        {
            BusterAarch64ArmM1FixedSpelling candidate = {0};
            BUSTER_TEST(arguments, buster_aarch64_arm_m1_fixed_spelling(fixed_index, &candidate));
            BUSTER_TEST(arguments, buster_aarch64_arm_m1_fixed_supported_for_target(candidate, without_feature) ==
                                      (candidate.required_feature != removable_fixed_features[feature_index]));
        }
    }
    Target without_flagm = fixed_explicit_target;
    without_flagm.cpu_features = target_cpu_features_remove(without_flagm.cpu_features, TARGET_CPU_FEATURE_AARCH64_ALTNZCV);
    without_flagm.cpu_features = target_cpu_features_remove(without_flagm.cpu_features, TARGET_CPU_FEATURE_AARCH64_FLAGM);
    BUSTER_TEST(arguments, target_cpu_features_are_valid(without_flagm));
    for (u32 fixed_index = 0; fixed_index < BUSTER_ARRAY_LENGTH(fixed_expected); fixed_index += 1)
    {
        BusterAarch64ArmM1FixedSpelling candidate = {0};
        BUSTER_TEST(arguments, buster_aarch64_arm_m1_fixed_spelling(fixed_index, &candidate));
        bool feature_available = candidate.required_feature != TARGET_CPU_FEATURE_AARCH64_FLAGM &&
                                 candidate.required_feature != TARGET_CPU_FEATURE_AARCH64_ALTNZCV;
        BUSTER_TEST(arguments, buster_aarch64_arm_m1_fixed_supported_for_target(candidate, without_flagm) == feature_available);
    }

    // Target-aware predicate evaluation is the authority behind the legacy
    // Apple-M1 enum classifier. Explicit feature subtraction remains valid and
    // changes only forms gated by the removed extension.
    Target m1_target = a64_encoding_m1_target(false);
    Target m1_explicit_target = a64_encoding_m1_target(true);
    BUSTER_TEST(arguments, buster_aarch64_metadata_form_supported_for_target(85, m1_target));
    BUSTER_TEST(arguments, buster_aarch64_metadata_form_supported_for_target(85, m1_explicit_target));
    BUSTER_TEST(arguments, buster_aarch64_metadata_form_supported_for_target(162, m1_target));
    BUSTER_TEST(arguments, buster_aarch64_metadata_form_supported_for_target(229, m1_target));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_form_supported_for_target(162,
                                                                                (Target){
                                                                                    .cpu_arch = CPU_ARCH_AARCH64,
                                                                                    .cpu_model = CPU_MODEL_BASELINE,
                                                                                    .os = OPERATING_SYSTEM_MACOS,
                                                                                }));
    Target no_lor_target = m1_explicit_target;
    no_lor_target.cpu_features = target_cpu_features_remove(no_lor_target.cpu_features, TARGET_CPU_FEATURE_AARCH64_LOR);
    Target no_trace_target = m1_explicit_target;
    no_trace_target.cpu_features = target_cpu_features_remove(no_trace_target.cpu_features, TARGET_CPU_FEATURE_AARCH64_TRACEV8_4);
    u32 no_lor_count = 0;
    u32 no_trace_count = 0;
    for (u32 form_id = 0; form_id < metadata_counts.form_count; form_id += 1)
    {
        bool m1_supported = buster_aarch64_metadata_form_supported_for_target(form_id, m1_explicit_target);
        bool no_lor_supported = buster_aarch64_metadata_form_supported_for_target(form_id, no_lor_target);
        bool no_trace_supported = buster_aarch64_metadata_form_supported_for_target(form_id, no_trace_target);
        if (m1_supported && !no_lor_supported)
        {
            no_lor_count += 1;
        }
        if (m1_supported && !no_trace_supported)
        {
            no_trace_count += 1;
        }
    }
    BUSTER_TEST(arguments, no_lor_count == 8 && no_trace_count == 1);
    Target invalid_target = m1_explicit_target;
    invalid_target.cpu_features = target_cpu_features_singleton(TARGET_CPU_FEATURE_AARCH64_AES);
    BUSTER_TEST(arguments, !buster_aarch64_metadata_form_supported_for_target(162, invalid_target));
    Target non_aarch64_target = m1_target;
    non_aarch64_target.cpu_arch = CPU_ARCH_X86_64;
    BUSTER_TEST(arguments, !buster_aarch64_metadata_form_supported_for_target(85, non_aarch64_target));
    Target unknown_feature_target = m1_explicit_target;
    unknown_feature_target.cpu_features.words[3] |= UINT64_C(1) << 63;
    BUSTER_TEST(arguments, !buster_aarch64_metadata_form_supported_for_target(85, unknown_feature_target));

    // Unknown predicate expressions are omitted from the retained predicate
    // list by the importer.  The generated reason byte must therefore reject
    // them for a generic AArch64 target as well as for the M1 membership path.
    Target generic_aarch64_target = m1_explicit_target;
    generic_aarch64_target.cpu_model = CPU_MODEL_BASELINE;
    BUSTER_TEST(arguments, buster_aarch64_metadata_test_predicate_parse_error_fails_closed(generic_aarch64_target));
    u32 unknown_predicate_form_count = 0;
    for (u32 form_id = 0; form_id < metadata_counts.form_count; form_id += 1)
    {
        BusterAarch64MetadataForm form = {0};
        BUSTER_TEST(arguments, buster_aarch64_metadata_form(form_id, &form));
        if (form.predicate_parse_error)
        {
            unknown_predicate_form_count += 1;
            BUSTER_TEST(arguments, !buster_aarch64_metadata_form_supported_for_target(form_id, generic_aarch64_target));
        }
    }
    BUSTER_TEST(arguments, unknown_predicate_form_count == 0);

    u32 raw_layout_complete_count = 0;
    u32 m1_count = 0;
    u32 m1_raw_layout_complete_count = 0;
    bool found_in_profile_unsupported_token_raw_layout = false;
    for (u32 form_id = 0; form_id < metadata_counts.form_count; form_id += 1)
    {
        BusterAarch64MetadataForm form = {0};
        BUSTER_TEST(arguments, buster_aarch64_metadata_form(form_id, &form) && form.id == form_id && form.normalized_form_id < metadata_counts.form_count);
        BUSTER_TEST(arguments, form.name.length != 0 && form.mnemonic.length != 0);
        if (form.raw_layout_complete)
        {
            raw_layout_complete_count += 1;
        }
        if (form.provisionally_apple_m1)
        {
            m1_count += 1;
            if (form.raw_layout_complete)
            {
                m1_raw_layout_complete_count += 1;
            }
            if (form.raw_layout_complete && form.coverage_class == BUSTER_AARCH64_METADATA_COVERAGE_UNSUPPORTED_TOKEN)
            {
                // A structurally complete raw layout is not semantic encoder
                // coverage. Keep unsupported-token rows in the audit census,
                // but do not mistake them for accepted instruction forms.
                found_in_profile_unsupported_token_raw_layout = true;
                BUSTER_TEST(arguments, form.coverage_class != BUSTER_AARCH64_METADATA_COVERAGE_DIRECT &&
                                          form.coverage_class != BUSTER_AARCH64_METADATA_COVERAGE_NORMALIZED &&
                                          form.coverage_class != BUSTER_AARCH64_METADATA_COVERAGE_ALIAS);
            }
        }
        for (u32 field_index = 0; field_index < form.field_count; field_index += 1)
        {
            BusterAarch64MetadataField field = {0};
            BUSTER_TEST(arguments, buster_aarch64_metadata_field(form_id, field_index, &field) && field.id == form.field_first + field_index);
            for (u32 segment_index = 0; segment_index < field.segment_count; segment_index += 1)
            {
                BusterAarch64MetadataSegment segment = {0};
                BUSTER_TEST(arguments, buster_aarch64_metadata_segment(form_id, field_index, segment_index, &segment) &&
                                          segment.id == field.segment_first + segment_index);
            }
        }
        for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
        {
            BusterAarch64MetadataOperand operand = {0};
            BUSTER_TEST(arguments, buster_aarch64_metadata_operand(form_id, operand_index, &operand) && operand.id == form.operand_first + operand_index);
        }
        for (u32 predicate_index = 0; predicate_index < form.predicate_count; predicate_index += 1)
        {
            BusterAarch64MetadataString predicate = {0};
            BUSTER_TEST(arguments, buster_aarch64_metadata_predicate(form_id, predicate_index, &predicate) && predicate.length != 0);
        }
        if (form.raw_layout_complete)
        {
            u32 values[8] = {0};
            u32 decoded_values[8] = {0};
            for (u32 field_index = 0; field_index < form.field_count; field_index += 1)
            {
                BusterAarch64MetadataField field = {0};
                BUSTER_TEST(arguments, buster_aarch64_metadata_field(form_id, field_index, &field));
                values[field_index] = field.source_mask & (0x9e3779b9u ^ (form_id * 0x45d9f3bu) ^ (field_index * 0x27d4eb2du));
            }
            u32 raw_word = 0;
            BUSTER_TEST(arguments, buster_aarch64_metadata_raw_encode(form_id, values, form.field_count, &raw_word));
            BUSTER_TEST(arguments, buster_aarch64_metadata_raw_decode(form_id, raw_word, decoded_values, form.field_count));
            for (u32 field_index = 0; field_index < form.field_count; field_index += 1)
            {
                BUSTER_TEST(arguments, decoded_values[field_index] == values[field_index]);
            }
            BUSTER_TEST(arguments, (raw_word & form.fixed_mask) == form.fixed_value);
        }
    }
    BUSTER_TEST(arguments, raw_layout_complete_count == 7346 && m1_count == 2898 && m1_raw_layout_complete_count == 2898);
    BUSTER_TEST(arguments, found_in_profile_unsupported_token_raw_layout);

    // Every raw-layout gap in the pinned LLVM snapshot is covered by an
    // exact name allowlist in the importer.  Keep this regression list in the
    // runtime test so a regenerated table cannot silently reintroduce a null
    // or scalar-var hole (or drop the importer-derived M1 membership bit).
    static char const* const m1_raw_closure_names[] = {
        "DUPv16i8gpr", "DUPv2i32gpr", "DUPv2i64gpr", "DUPv4i16gpr", "DUPv4i32gpr", "DUPv8i16gpr", "DUPv8i8gpr",
        "FCMPDri",     "FCMPEDri",     "FCMPEHri",     "FCMPESri",     "FCMPHri",     "FCMPSri",
        "INSvi16lane", "INSvi32lane", "INSvi64lane",
        "STLXRB",      "STLXRH",       "STLXRW",       "STLXRX",       "STXRB",       "STXRH",       "STXRW",       "STXRX",
        "MSRpstateImm1", "MSRpstatesvcrImm1",
    };
    bool m1_raw_closure_found[BUSTER_ARRAY_LENGTH(m1_raw_closure_names)] = {0};
    for (u32 form_id = 0; form_id < metadata_counts.form_count; form_id += 1)
    {
        BusterAarch64MetadataForm form = {0};
        BUSTER_TEST(arguments, buster_aarch64_metadata_form(form_id, &form));
        for (u32 closure_index = 0; closure_index < BUSTER_ARRAY_LENGTH(m1_raw_closure_names); closure_index += 1)
        {
            if (!a64_encoding_metadata_string_equal(form.name, m1_raw_closure_names[closure_index]))
            {
                continue;
            }
            BUSTER_TEST(arguments, !m1_raw_closure_found[closure_index]);
            m1_raw_closure_found[closure_index] = true;
            bool expected_m1_member = !a64_encoding_metadata_string_equal(form.name, "MSRpstatesvcrImm1");
            BUSTER_TEST(arguments, form.apple_m1_profile_member == expected_m1_member && form.raw_layout_complete);
            if (closure_index >= 16 && closure_index < 24)
            {
                BUSTER_TEST(arguments, (form.fixed_mask & UINT32_C(0x00007c00)) == UINT32_C(0x00007c00) &&
                                          (form.fixed_value & UINT32_C(0x00007c00)) == UINT32_C(0x00007c00));
            }
            if (closure_index >= 24)
            {
                BusterAarch64MetadataField scalar_field = {0};
                bool scalar_found = false;
                for (u32 field_index = 0; field_index < form.field_count; field_index += 1)
                {
                    BusterAarch64MetadataField candidate = {0};
                    BUSTER_TEST(arguments, buster_aarch64_metadata_field(form_id, field_index, &candidate));
                    if (a64_encoding_metadata_string_equal(candidate.name, "imm"))
                    {
                        scalar_field = candidate;
                        scalar_found = true;
                        break;
                    }
                }
                BUSTER_TEST(arguments, scalar_found && scalar_field.width == 1 && scalar_field.source_mask == 1 && scalar_field.segment_count == 1);
                BusterAarch64MetadataSegment scalar_segment = {0};
                BUSTER_TEST(arguments, scalar_found && buster_aarch64_metadata_segment(form_id, scalar_field.id - form.field_first, 0, &scalar_segment) &&
                                          scalar_segment.instruction_lsb == 8 && scalar_segment.width == 1 && scalar_segment.value_lsb == 0);
            }
        }
    }
    for (u32 closure_index = 0; closure_index < BUSTER_ARRAY_LENGTH(m1_raw_closure_names); closure_index += 1)
    {
        BUSTER_TEST(arguments, m1_raw_closure_found[closure_index]);
    }

    // Differential raw words for the corrected Rt2=XZR/WZR fixed field.
    static struct
    {
        char const* name;
        u32 word;
    } const exclusive_store_cases[] = {
        {"STLXRB", UINT32_C(0x0802fc83)},
        {"STXRX", UINT32_C(0xc8027c83)},
    };
    for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(exclusive_store_cases); case_index += 1)
    {
        bool found = false;
        for (u32 form_id = 0; form_id < metadata_counts.form_count; form_id += 1)
        {
            BusterAarch64MetadataForm form = {0};
            BUSTER_TEST(arguments, buster_aarch64_metadata_form(form_id, &form));
            if (!a64_encoding_metadata_string_equal(form.name, exclusive_store_cases[case_index].name))
            {
                continue;
            }
            found = true;
            u32 values[] = {3, 4, 2};
            u32 word = 0;
            BUSTER_TEST(arguments, form.field_count == BUSTER_ARRAY_LENGTH(values) &&
                                      buster_aarch64_metadata_raw_encode(form_id, values, BUSTER_ARRAY_LENGTH(values), &word) &&
                                      word == exclusive_store_cases[case_index].word);
            u32 raw_decoded[3] = {0};
            BUSTER_TEST(arguments, buster_aarch64_metadata_raw_decode(form_id, word, raw_decoded, BUSTER_ARRAY_LENGTH(raw_decoded)) &&
                                      raw_decoded[0] == values[0] && raw_decoded[1] == values[1] && raw_decoded[2] == values[2]);
            break;
        }
        BUSTER_TEST(arguments, found);
    }

    bool found_svcr = false;
    for (u32 form_id = 0; form_id < metadata_counts.form_count; form_id += 1)
    {
        BusterAarch64MetadataForm form = {0};
        BUSTER_TEST(arguments, buster_aarch64_metadata_form(form_id, &form));
        if (a64_encoding_metadata_string_equal(form.name, "MSRpstatesvcrImm1"))
        {
            found_svcr = true;
            Target generic_no_sme = {
                .cpu_arch = CPU_ARCH_AARCH64,
                .cpu_model = CPU_MODEL_A64_GENERIC,
                .os = OPERATING_SYSTEM_MACOS,
            };
            Target generic_with_sme = generic_no_sme;
            generic_with_sme.cpu_features_explicit = true;
            generic_with_sme.cpu_features = target_cpu_features_add(target_cpu_features_default(CPU_ARCH_AARCH64, CPU_MODEL_A64_GENERIC),
                                                                     TARGET_CPU_FEATURE_AARCH64_SME);
            BusterAarch64MetadataString custom_predicate = {0};
            BUSTER_TEST(arguments, !form.apple_m1_profile_member && !form.provisionally_apple_m1 &&
                                      !buster_aarch64_metadata_form_supported_for_target(form_id, m1_target) && form.raw_layout_complete &&
                                      form.predicate_count == 1 && buster_aarch64_metadata_predicate(form_id, 0, &custom_predicate) &&
                                      a64_encoding_metadata_string_equal(custom_predicate, "HasSME"));
            BUSTER_TEST(arguments, target_cpu_features_are_valid(generic_with_sme) &&
                                      !buster_aarch64_metadata_form_supported_for_target(form_id, generic_no_sme) &&
                                      buster_aarch64_metadata_form_supported_for_target(form_id, generic_with_sme));
            break;
        }
    }
    BUSTER_TEST(arguments, found_svcr);

    // Differential words checked against llvm-mc 22.1.8. The values are raw
    // source-field values in generated field order, not semantic operands.
    static struct
    {
        u32 form_id;
        u32 field_count;
        u32 values[4];
        u32 word;
        char const* name;
    } const llvm_mc_cases[] = {
        {85, 4, {3, 4, 0, 5}, UINT32_C(0x0b050083), "ADDWrs"},
        {463, 2, {0, 2}, UINT32_C(0x54000040), "Bcc"},
        {3463, 3, {1, 2, 2}, UINT32_C(0xf9400841), "LDRXui"},
        {1162, 3, {0, 1, 2}, UINT32_C(0x1e222820), "FADDSrr"},
        {162, 2, {0, 1}, UINT32_C(0x4e284820), "AESErr"},
        {3114, 3, {4, 5, 3}, UINT32_C(0xb82300a4), "LDADDW"},
        {229, 0, {0}, UINT32_C(0xd50323bf), "AUTIASP"},
        {3124, 3, {1, 2, UINT32_C(0x1f8)}, UINT32_C(0x195f8041), "LDAPURBi"},
        {3131, 3, {1, 2, 0x10}, UINT32_C(0xd9410041), "LDAPURXi"},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(llvm_mc_cases); index += 1)
    {
        u32 metadata_encoded = 0;
        BusterAarch64MetadataForm form = {0};
        BUSTER_TEST(arguments, buster_aarch64_metadata_form(llvm_mc_cases[index].form_id, &form) &&
                              a64_encoding_metadata_string_equal(form.name, llvm_mc_cases[index].name));
        BUSTER_TEST(arguments, buster_aarch64_metadata_raw_encode(llvm_mc_cases[index].form_id, llvm_mc_cases[index].values,
                                                                   llvm_mc_cases[index].field_count, &metadata_encoded) &&
                              metadata_encoded == llvm_mc_cases[index].word);
        u32 decoded_values[4] = {0};
        BUSTER_TEST(arguments, buster_aarch64_metadata_raw_decode(llvm_mc_cases[index].form_id, metadata_encoded, decoded_values,
                                                                   llvm_mc_cases[index].field_count));
        for (u32 field_index = 0; field_index < llvm_mc_cases[index].field_count; field_index += 1)
        {
            BUSTER_TEST(arguments, decoded_values[field_index] == llvm_mc_cases[index].values[field_index]);
        }
    }

    // The machine AArch64 backend's first generated memory tranche consists
    // of the eight scalar unsigned imm12 forms. Check both ends of each
    // scaled range against the pre-generator bit layout, then exercise the
    // same alignment/range rejections the machine helpers preserve.
    static struct
    {
        u32 form_id;
        u32 size;
        bool store;
        char const* name;
    } const memory_forms[] = {
        {BUSTER_AARCH64_GENERATED_FORM_LDRBBUI, 1, false, "LDRBBui"},
        {BUSTER_AARCH64_GENERATED_FORM_LDRHHUI, 2, false, "LDRHHui"},
        {BUSTER_AARCH64_GENERATED_FORM_LDRWUI, 4, false, "LDRWui"},
        {BUSTER_AARCH64_GENERATED_FORM_LDRXUI, 8, false, "LDRXui"},
        {BUSTER_AARCH64_GENERATED_FORM_STRBBUI, 1, true, "STRBBui"},
        {BUSTER_AARCH64_GENERATED_FORM_STRHHUI, 2, true, "STRHHui"},
        {BUSTER_AARCH64_GENERATED_FORM_STRWUI, 4, true, "STRWui"},
        {BUSTER_AARCH64_GENERATED_FORM_STRXUI, 8, true, "STRXui"},
    };
    for (u32 form_index = 0; form_index < BUSTER_ARRAY_LENGTH(memory_forms); form_index += 1)
    {
        u32 form_id = memory_forms[form_index].form_id;
        u32 size = memory_forms[form_index].size;
        BusterAarch64MetadataForm form = {0};
        BUSTER_TEST(arguments, a64_generated_form(form_id, &form) && form.raw_layout_complete && form.field_count == 3 && form.provisionally_apple_m1);
        BUSTER_TEST(arguments, a64_encoding_metadata_string_equal(form.name, memory_forms[form_index].name));
        u32 base = memory_forms[form_index].store ? (size == 1   ? UINT32_C(0x39000000)
                                                       : size == 2 ? UINT32_C(0x79000000)
                                                       : size == 4 ? UINT32_C(0xb9000000)
                                                                   : UINT32_C(0xf9000000))
                                                   : (size == 1   ? UINT32_C(0x39400000)
                                                       : size == 2 ? UINT32_C(0x79400000)
                                                       : size == 4 ? UINT32_C(0xb9400000)
                                                                   : UINT32_C(0xf9400000));
        u32 offsets[] = {0, 4095u * size};
        for (u32 offset_index = 0; offset_index < BUSTER_ARRAY_LENGTH(offsets); offset_index += 1)
        {
            u32 offset = offsets[offset_index];
            u32 values[] = {17, 28, offset / size};
            u32 word = 0;
            u32 expected = base | (17u) | (28u << 5) | ((offset / size) << 10);
            BUSTER_TEST(arguments, a64_generated_raw_encode(form_id, values, BUSTER_ARRAY_LENGTH(values), &word) && word == expected);
            u32 memory_decoded[3] = {0};
            BUSTER_TEST(arguments, a64_generated_raw_decode(form_id, word, memory_decoded, BUSTER_ARRAY_LENGTH(memory_decoded)) &&
                                      memory_decoded[0] == values[0] && memory_decoded[1] == values[1] && memory_decoded[2] == values[2]);
        }
        u32 out_of_range[] = {17, 28, 4096};
        u32 ignored = 0;
        BUSTER_TEST(arguments, !a64_generated_raw_encode(form_id, out_of_range, BUSTER_ARRAY_LENGTH(out_of_range), &ignored));
    }

    // The machine backend's bounded production set is generated from the
    // same normalized records as the packed metadata.  Exercise every named
    // form through both encoders, at deterministic and all-ones field values,
    // and reject malformed fast-path requests without entering the packed
    // decoder.  Keeping this list in the test makes a missing generated plan
    // entry fail loudly when the importer set changes.
    static struct
    {
        u32 form_id;
        char const* name;
    } const production_forms[] = {
        {BUSTER_AARCH64_GENERATED_FORM_LDRBBUI, "LDRBBui"},
        {BUSTER_AARCH64_GENERATED_FORM_LDRHHUI, "LDRHHui"},
        {BUSTER_AARCH64_GENERATED_FORM_LDRWUI, "LDRWui"},
        {BUSTER_AARCH64_GENERATED_FORM_LDRXUI, "LDRXui"},
        {BUSTER_AARCH64_GENERATED_FORM_STRBBUI, "STRBBui"},
        {BUSTER_AARCH64_GENERATED_FORM_STRHHUI, "STRHHui"},
        {BUSTER_AARCH64_GENERATED_FORM_STRWUI, "STRWui"},
        {BUSTER_AARCH64_GENERATED_FORM_STRXUI, "STRXui"},
        {BUSTER_AARCH64_GENERATED_FORM_ORRWRS, "ORRWrs"},
        {BUSTER_AARCH64_GENERATED_FORM_ORRXRS, "ORRXrs"},
        {BUSTER_AARCH64_GENERATED_FORM_ADDWRS, "ADDWrs"},
        {BUSTER_AARCH64_GENERATED_FORM_ADDXRS, "ADDXrs"},
        {BUSTER_AARCH64_GENERATED_FORM_SUBWRS, "SUBWrs"},
        {BUSTER_AARCH64_GENERATED_FORM_SUBXRS, "SUBXrs"},
        {BUSTER_AARCH64_GENERATED_FORM_ANDWRS, "ANDWrs"},
        {BUSTER_AARCH64_GENERATED_FORM_ANDXRS, "ANDXrs"},
        {BUSTER_AARCH64_GENERATED_FORM_EORWRS, "EORWrs"},
        {BUSTER_AARCH64_GENERATED_FORM_EORXRS, "EORXrs"},
        {BUSTER_AARCH64_GENERATED_FORM_MADDWRRR, "MADDWrrr"},
        {BUSTER_AARCH64_GENERATED_FORM_MADDXRRR, "MADDXrrr"},
        {BUSTER_AARCH64_GENERATED_FORM_MSUBWRRR, "MSUBWrrr"},
        {BUSTER_AARCH64_GENERATED_FORM_MSUBXRRR, "MSUBXrrr"},
        {BUSTER_AARCH64_GENERATED_FORM_SDIVWR, "SDIVWr"},
        {BUSTER_AARCH64_GENERATED_FORM_SDIVXR, "SDIVXr"},
        {BUSTER_AARCH64_GENERATED_FORM_UDIVWR, "UDIVWr"},
        {BUSTER_AARCH64_GENERATED_FORM_UDIVXR, "UDIVXr"},
        {BUSTER_AARCH64_GENERATED_FORM_LSLVWR, "LSLVWr"},
        {BUSTER_AARCH64_GENERATED_FORM_LSLVXR, "LSLVXr"},
        {BUSTER_AARCH64_GENERATED_FORM_ASRVWR, "ASRVWr"},
        {BUSTER_AARCH64_GENERATED_FORM_ASRVXR, "ASRVXr"},
        {BUSTER_AARCH64_GENERATED_FORM_LSRVWR, "LSRVWr"},
        {BUSTER_AARCH64_GENERATED_FORM_LSRVXR, "LSRVXr"},
        {BUSTER_AARCH64_GENERATED_FORM_SBFMXRI, "SBFMXri"},
        {BUSTER_AARCH64_GENERATED_FORM_UBFMWRI, "UBFMWri"},
        {BUSTER_AARCH64_GENERATED_FORM_ORNWRS, "ORNWrs"},
        {BUSTER_AARCH64_GENERATED_FORM_ORNXRS, "ORNXrs"},
        {BUSTER_AARCH64_GENERATED_FORM_SUBSWRS, "SUBSWrs"},
        {BUSTER_AARCH64_GENERATED_FORM_SUBSXRS, "SUBSXrs"},
        {BUSTER_AARCH64_GENERATED_FORM_SUBSXRI, "SUBSXri"},
        {BUSTER_AARCH64_GENERATED_FORM_CSINCWR, "CSINCWr"},
        {BUSTER_AARCH64_GENERATED_FORM_FMOVXDR, "FMOVXDr"},
        {BUSTER_AARCH64_GENERATED_FORM_FMOVDXR, "FMOVDXr"},
        {BUSTER_AARCH64_GENERATED_FORM_ADDXRI, "ADDXri"},
        {BUSTER_AARCH64_GENERATED_FORM_RET, "RET"},
    };
    u32 production_field_total = 0;
    u32 production_segment_total = 0;
    BUSTER_TEST(arguments, buster_aarch64_production_plan_form_count() == BUSTER_ARRAY_LENGTH(production_forms));
    for (u32 form_index = 0; form_index < BUSTER_ARRAY_LENGTH(production_forms); form_index += 1)
    {
        u32 form_id = production_forms[form_index].form_id;
        BusterAarch64MetadataForm form = {0};
        BUSTER_TEST(arguments, buster_aarch64_metadata_form(form_id, &form) && form.id == form_id && form.raw_layout_complete &&
                              form.provisionally_apple_m1 && a64_encoding_metadata_string_equal(form.name, production_forms[form_index].name));
        BUSTER_TEST(arguments, form.field_count <= 8);
        production_field_total += form.field_count;
        u32 values[8] = {0};
        u32 all_ones[8] = {0};
        for (u32 field_index = 0; field_index < form.field_count; field_index += 1)
        {
            BusterAarch64MetadataField field = {0};
            BUSTER_TEST(arguments, buster_aarch64_metadata_field(form_id, field_index, &field));
            production_segment_total += field.segment_count;
            values[field_index] = field.source_mask & (UINT32_C(0x9e3779b9) ^ form_id * UINT32_C(0x45d9f3b) ^ field_index * UINT32_C(0x27d4eb2d));
            all_ones[field_index] = field.source_mask;
            u32 invalid_values[8] = {0};
            invalid_values[field_index] = field.source_mask | ~field.source_mask;
            u32 ignored = 0;
            BUSTER_TEST(arguments, !buster_aarch64_production_raw_encode(form_id, invalid_values, form.field_count, &ignored));
        }
        u32 generic_word = 0;
        u32 production_word = 0;
        u32 alias_word = 0;
        BUSTER_TEST(arguments, buster_aarch64_metadata_raw_encode(form_id, values, form.field_count, &generic_word) &&
                              buster_aarch64_production_raw_encode(form_id, values, form.field_count, &production_word) &&
                              a64_generated_production_raw_encode(form_id, values, form.field_count, &alias_word) && generic_word == production_word &&
                              production_word == alias_word && (production_word & form.fixed_mask) == form.fixed_value);
        BUSTER_TEST(arguments, buster_aarch64_metadata_raw_encode(form_id, all_ones, form.field_count, &generic_word) &&
                              buster_aarch64_production_raw_encode(form_id, all_ones, form.field_count, &production_word) && generic_word == production_word);
    }
    BUSTER_TEST(arguments, buster_aarch64_production_plan_field_count() == production_field_total &&
                              buster_aarch64_production_plan_segment_count() == production_segment_total);

    // Positive control: prove the test-only counter is live before relying on
    // it to reject packed-metadata access in the production wrapper below.
    BusterAarch64MetadataForm packed_control_form = {0};
    u32 packed_control_values[8] = {0};
    u32 packed_control_word = 0;
    BUSTER_TEST(arguments, buster_aarch64_metadata_form(production_forms[0].form_id, &packed_control_form));
    buster_aarch64_metadata_test_reset_packed_access_counter();
    BUSTER_TEST(arguments, buster_aarch64_metadata_raw_encode(production_forms[0].form_id, packed_control_values,
                                                               packed_control_form.field_count, &packed_control_word));
    BUSTER_TEST(arguments, buster_aarch64_metadata_test_packed_access_count() > 0);

    // The fast production path must remain independent of the packed/base64
    // metadata accessors. Build inputs directly from the generated plan and
    // encode every named form after resetting the test-only counter; a future
    // fallback into the audit tables turns this into a deterministic failure.
    buster_aarch64_metadata_test_reset_packed_access_counter();
    for (u32 form_index = 0; form_index < BUSTER_ARRAY_LENGTH(production_forms); form_index += 1)
    {
        u16 plan_index = buster_aarch64_generated_production_plan_index(production_forms[form_index].form_id);
        BUSTER_TEST(arguments, plan_index != UINT16_MAX);
        if (plan_index == UINT16_MAX)
        {
            continue;
        }
        BusterAarch64GeneratedProductionForm const* plan = buster_aarch64_generated_production_form_at(plan_index);
        BUSTER_TEST(arguments, plan != 0 && plan->field_count <= 8);
        if (!plan || plan->field_count > 8)
        {
            continue;
        }
        u32 values[8] = {0};
        for (u32 field_index = 0; field_index < plan->field_count; field_index += 1)
        {
            BusterAarch64GeneratedProductionField const* field =
                buster_aarch64_generated_production_field_at(plan->field_first + field_index);
            BUSTER_TEST(arguments, field != 0);
            if (field)
            {
                values[field_index] = field->source_mask & (UINT32_C(0x13579bdf) ^ production_forms[form_index].form_id * UINT32_C(0x9e3779b9) ^
                                                              field_index * UINT32_C(0x45d9f3b));
            }
        }
        u32 word = 0;
        BUSTER_TEST(arguments, a64_generated_production_raw_encode(plan->form_id, values, plan->field_count, &word));
    }
    BUSTER_TEST(arguments, buster_aarch64_metadata_test_packed_access_count() == 0);
    u32 fast_values[8] = {0};
    u32 fast_word = 0;
    BUSTER_TEST(arguments, !buster_aarch64_production_raw_encode(UINT32_MAX, fast_values, 0, &fast_word) &&
                          !buster_aarch64_production_raw_encode(metadata_counts.form_count, fast_values, 0, &fast_word) &&
                          !buster_aarch64_production_raw_encode(production_forms[0].form_id, fast_values, 2, &fast_word) &&
                          !buster_aarch64_production_raw_encode(production_forms[0].form_id, 0, 3, &fast_word) &&
                          !buster_aarch64_production_raw_encode(production_forms[0].form_id, fast_values, 3, 0));

    // Bounded rejection coverage for null, wrong-count, overflow, fixed-bit,
    // unsupported-target, and out-of-range metadata requests.
    BusterAarch64MetadataForm metadata_form = {0};
    BUSTER_TEST(arguments, !buster_aarch64_metadata_form(metadata_counts.form_count, &metadata_form));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_form(0, 0));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_field(0, UINT32_MAX, 0));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_segment(0, 0, UINT32_MAX, 0));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_operand(0, UINT32_MAX, 0));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_predicate(0, UINT32_MAX, 0));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_form_supported(0, BUSTER_AARCH64_METADATA_TARGET_COUNT));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_form_provisionally_apple_m1_supported(metadata_counts.form_count));
    BUSTER_TEST(arguments, buster_aarch64_metadata_form_has_complete_raw_layout(3854));
    u32 rejection_decoded_values[4] = {0};
    BUSTER_TEST(arguments, buster_aarch64_metadata_raw_encode(3854, (u32[]){0, 1}, 2, &encoded));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_raw_encode(85, 0, 4, &encoded));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_raw_encode(85, (u32 const[]){32, 0, 0, 0}, 4, &encoded));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_raw_encode(85, (u32 const[]){3, 4, 0, 5}, 3, &encoded));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_raw_encode(85, (u32 const[]){3, 4, 0, 5}, 4, 0));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_raw_decode(85,
                                                              UINT32_C(0x0b050083) ^ UINT32_C(0x80000000),
                                                              rejection_decoded_values, 4));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_raw_decode(85, UINT32_C(0x0b050083), rejection_decoded_values, 3));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_raw_decode(85, UINT32_C(0x0b050083), 0, 4));

    // Zero-field fixed forms are symmetric: no field buffer is needed in
    // either direction.
    u32 zero_field_word = 0;
    BUSTER_TEST(arguments, buster_aarch64_metadata_raw_encode(229, 0, 0, &zero_field_word) && zero_field_word == UINT32_C(0xd50323bf));
    BUSTER_TEST(arguments, buster_aarch64_metadata_raw_decode(229, zero_field_word, 0, 0));

    // Every direct-GPR projection row is checked against an independent
    // llvm-mc literal keyed by its Arm row identity and source digest.  The
    // production encoder must remain independent of the packed metadata path.
    Target gpr_target = a64_encoding_m1_target(true);
    BUSTER_TEST(arguments, buster_aarch64_arm_m1_gpr_target(gpr_target));
    BUSTER_TEST(arguments, buster_aarch64_arm_m1_gpr_form_count() == BUSTER_ARRAY_LENGTH(a64_m1_gpr_oracles) &&
                               buster_aarch64_arm_m1_gpr_form_count() == 80);
    buster_aarch64_metadata_test_reset_packed_access_counter();
    u32 direct_operand_positions = 0;
    for (u32 form_index = 0; form_index < buster_aarch64_arm_m1_gpr_form_count(); form_index += 1)
    {
        BusterAarch64ArmM1GprForm form = {0};
        BUSTER_TEST(arguments, buster_aarch64_arm_m1_gpr_form(form_index, &form));
        if (!form.mnemonic.pointer || !form.arm_row_id.pointer)
        {
            continue;
        }
        A64M1GprOracle const* oracle = 0;
        for (u32 oracle_index = 0; oracle_index < BUSTER_ARRAY_LENGTH(a64_m1_gpr_oracles); oracle_index += 1)
        {
            A64M1GprOracle const* candidate = a64_m1_gpr_oracles + oracle_index;
            if (candidate->arm_row_digest == form.arm_row_digest && string_equal(candidate->arm_row_id, form.arm_row_id))
            {
                oracle = candidate;
                break;
            }
        }
        BUSTER_TEST(arguments, oracle != 0 && form.operand_count >= 1 && form.operand_count <= 4);
        if (!oracle || form.operand_count > 4)
        {
            continue;
        }
        A64GprOperand operands[4] = {0};
        u32 sp_operand_index = UINT32_MAX;
        for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
        {
            BusterAarch64ArmM1GprOperand descriptor = form.operands[operand_index];
            operands[operand_index] = (A64GprOperand){
                .index = (u8)(operand_index + 1),
                .width = descriptor.width,
            };
            if (descriptor.register31_role == A64_GPR_REGISTER31_SP)
            {
                operands[operand_index].index = 31;
                operands[operand_index].stack_pointer = true;
                sp_operand_index = operand_index;
            }
        }
        direct_operand_positions += form.operand_count;
        u32 found_form = UINT32_MAX;
        u32 word = 0;
        BUSTER_TEST(arguments, buster_aarch64_arm_m1_gpr_find_form(form.mnemonic, operands, form.operand_count, &found_form) &&
                               found_form == form_index && buster_aarch64_arm_m1_gpr_encode(gpr_target, form_index, operands,
                                                                                              form.operand_count, &word) &&
                               word == oracle->word);
        u32 mnemonic_word = 0;
        BUSTER_TEST(arguments, buster_aarch64_arm_m1_gpr_encode_mnemonic(gpr_target, form.mnemonic, operands, form.operand_count,
                                                                          &mnemonic_word) &&
                               mnemonic_word == oracle->word);
        // Exhaustive register-31 matrix: each visible operand is tested as
        // its architectural ZR/SP role, the opposite role is rejected, and a
        // W/X width flip is rejected. Other operands retain the ordinary
        // values used by the independent oracle vector above.
        for (u32 position = 0; position < form.operand_count; position += 1)
        {
            A64GprOperand register31_operands[4] = {0};
            for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1) register31_operands[operand_index] = operands[operand_index];
            bool stack_pointer = form.operands[position].register31_role == A64_GPR_REGISTER31_SP;
            register31_operands[position].index = 31;
            register31_operands[position].stack_pointer = stack_pointer;
            u32 expected_register31_word = form.fixed_value;
            for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
            {
                expected_register31_word |= (u32)register31_operands[operand_index].index << form.operands[operand_index].bit_lsb;
            }
            BUSTER_TEST(arguments, buster_aarch64_arm_m1_gpr_encode(gpr_target, form_index, register31_operands,
                                                                      form.operand_count, &word) && word == expected_register31_word);
            A64GprOperand opposite_role_operands[4] = {0};
            for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1) opposite_role_operands[operand_index] = register31_operands[operand_index];
            opposite_role_operands[position].stack_pointer = !stack_pointer;
            BUSTER_TEST(arguments, !buster_aarch64_arm_m1_gpr_encode(gpr_target, form_index, opposite_role_operands,
                                                                       form.operand_count, &word));
            A64GprOperand width_flip_operands[4] = {0};
            for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1) width_flip_operands[operand_index] = register31_operands[operand_index];
            width_flip_operands[position].width = width_flip_operands[position].width == 32 ? 64 : 32;
            BUSTER_TEST(arguments, !buster_aarch64_arm_m1_gpr_encode(gpr_target, form_index, width_flip_operands,
                                                                       form.operand_count, &word));
        }
        if (sp_operand_index != UINT32_MAX)
        {
            A64GprOperand wrong_role_operands[4] = {0};
            for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1) wrong_role_operands[operand_index] = operands[operand_index];
            wrong_role_operands[sp_operand_index].index = 31;
            wrong_role_operands[sp_operand_index].stack_pointer = false;
            BUSTER_TEST(arguments, !buster_aarch64_arm_m1_gpr_encode(gpr_target, form_index, wrong_role_operands, form.operand_count, &word));
        }
        A64GprOperand bad_reserved = operands[0];
        bad_reserved.reserved = 1;
        A64GprOperand invalid_operands[4] = {0};
        for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1) invalid_operands[operand_index] = operands[operand_index];
        invalid_operands[0] = bad_reserved;
        BUSTER_TEST(arguments, !buster_aarch64_arm_m1_gpr_encode(gpr_target, form_index, invalid_operands, form.operand_count, &word));
        if (form.required_feature != TARGET_CPU_FEATURE_NONE)
        {
            Target missing = gpr_target;
            missing.cpu_features = target_cpu_features_remove(missing.cpu_features, form.required_feature);
            BUSTER_TEST(arguments, !buster_aarch64_arm_m1_gpr_encode(missing, form_index, operands, form.operand_count, &word));
        }
    }
    Target non_m1_target = gpr_target;
    non_m1_target.cpu_model = CPU_MODEL_BASELINE;
    BUSTER_TEST(arguments, !buster_aarch64_arm_m1_gpr_target(non_m1_target));
    BUSTER_TEST(arguments, direct_operand_positions == 189);
    BUSTER_TEST(arguments, buster_aarch64_metadata_test_packed_access_count() == 0);

    // The generated scalar-integer projection is structurally bounded and
    // uses the same fixed-mask discipline as the direct-GPR slice.  Exercise
    // representative immediate, extension, shift, logical, conditional, and
    // reserved/undefined forms through the typed public API.
    BUSTER_TEST(arguments, buster_aarch64_arm_m1_scalar_integer_form_count() == 72);
    BUSTER_TEST(arguments, buster_aarch64_arm_m1_scalar_integer_target(gpr_target));
    for (u32 scalar_index = 0; scalar_index < buster_aarch64_arm_m1_scalar_integer_form_count(); scalar_index += 1)
    {
        BusterAarch64ArmM1ScalarIntegerForm scalar_form = {0};
        BUSTER_TEST(arguments, buster_aarch64_arm_m1_scalar_integer_form(scalar_index, &scalar_form) && scalar_form.arm_row_id.length &&
                                   scalar_form.arm_row_digest && scalar_form.operand_count >= 1 && scalar_form.operand_count <= 4);
    }
    A64ScalarIntOperand scalar_operands[4] = {
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 64, .index = 0},
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 64, .index = 1},
        {.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 4095},
    };
    u32 scalar_word = 0;
    BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("add"), scalar_operands, 3, 0, 0, &scalar_word) &&
                               scalar_word == UINT32_C(0x913ffc20));
    A64ScalarIntModifier scalar_shift = {
        .kind = A64_SCALAR_INT_MODIFIER_SHIFT, .value = A64_SCALAR_INT_SHIFT_LSL, .amount = 31, .present = true,
    };
    scalar_operands[0] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 32, .index = 0};
    scalar_operands[1] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 32, .index = 1};
    scalar_operands[2] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 32, .index = 2};
    BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("add"), scalar_operands, 3, &scalar_shift, 1,
                                                                       &scalar_word) &&
                               scalar_word == UINT32_C(0x0b027c20));
    scalar_operands[0] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 64, .index = 0};
    scalar_operands[1] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 64, .index = 1};
    scalar_operands[2] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = UINT64_C(0xff00ff00ff00ff)};
    BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("and"), scalar_operands, 3, 0, 0, &scalar_word) &&
                               scalar_word == UINT32_C(0x92009c20));
    scalar_operands[0] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 64, .index = 0};
    scalar_operands[1] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 64, .index = 1};
    scalar_operands[2] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 64, .index = 2};
    scalar_shift = (A64ScalarIntModifier){.kind = A64_SCALAR_INT_MODIFIER_SHIFT, .value = A64_SCALAR_INT_SHIFT_ROR, .amount = 31, .present = true};
    scalar_operands[0].width = 32;
    scalar_operands[1].width = 32;
    scalar_operands[2].width = 32;
    BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("orr"), scalar_operands, 3, &scalar_shift, 1,
                                                                       &scalar_word) &&
                               scalar_word == UINT32_C(0x2ac27c20));
    scalar_operands[2] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 15};
    scalar_operands[3] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 0};
    scalar_operands[1] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 31};
    scalar_operands[0] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 64, .index = 1};
    bool scalar_ccmn_ok = a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("ccmn"), scalar_operands, 4, 0, 0, &scalar_word);
    BUSTER_TEST(arguments, scalar_ccmn_ok && scalar_word == UINT32_C(0xba5f082f));
    scalar_operands[0] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 64, .index = 1};
    scalar_operands[1] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 63};
    scalar_operands[2] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 15};
    BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("rmif"), scalar_operands, 3, 0, 0, &scalar_word) &&
                               scalar_word == UINT32_C(0xba1f842f));
    scalar_operands[0] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 65535};
    BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("udf"), scalar_operands, 1, 0, 0, &scalar_word) &&
                               scalar_word == UINT32_C(0x0000ffff));
    u32 unchanged = UINT32_C(0xa5a5a5a5);
    BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("and"), scalar_operands, 1, 0, 0, &unchanged) &&
                               unchanged == UINT32_C(0xa5a5a5a5));

    // Every generated scalar row has an independent llvm-mc spelling/word
    // fixture. Exercise both lookup and indexed typed encoding, then mutate
    // each operand's role/width and metadata to prove the public boundary is
    // fail-closed without touching the output word on rejection.
    BUSTER_TEST(arguments, BUSTER_AARCH64_SCALAR_INTEGER_CORPUS_COUNT ==
                               buster_aarch64_arm_m1_scalar_integer_form_count());
    bool scalar_fixture_seen[BUSTER_AARCH64_SCALAR_INTEGER_CORPUS_COUNT] = {0};
    buster_aarch64_metadata_test_reset_packed_access_counter();
    for (u32 corpus_index = 0; corpus_index < BUSTER_AARCH64_SCALAR_INTEGER_CORPUS_COUNT; corpus_index += 1)
    {
        BusterAarch64ScalarIntegerCorpusCase const* test_case = buster_aarch64_scalar_integer_corpus + corpus_index;
        u32 form_index = UINT32_MAX;
        BusterAarch64ArmM1ScalarIntegerForm form = {0};
        for (u32 candidate_index = 0; candidate_index < buster_aarch64_arm_m1_scalar_integer_form_count(); candidate_index += 1)
        {
            BusterAarch64ArmM1ScalarIntegerForm candidate = {0};
            if (buster_aarch64_arm_m1_scalar_integer_form(candidate_index, &candidate) &&
                string_ends_with_sequence(candidate.arm_row_id, test_case->arm_encoding_name))
            {
                form_index = candidate_index;
                form = candidate;
                break;
            }
        }
        BUSTER_TEST(arguments, form_index < buster_aarch64_arm_m1_scalar_integer_form_count() && !scalar_fixture_seen[form_index]);
        if (form_index >= buster_aarch64_arm_m1_scalar_integer_form_count()) continue;
        scalar_fixture_seen[form_index] = true;
        A64ScalarIntOperand operands[4] = {0};
        A64ScalarIntModifier modifier = {0};
        u32 modifier_count = 0;
        BUSTER_TEST(arguments, aarch64_scalar_test_fixture_operands(form, operands, &modifier, &modifier_count));
        u32 found_form = UINT32_MAX;
        u32 word = UINT32_C(0xa5a5a5a5);
        BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_find_form(form.mnemonic, operands, form.operand_count,
                                                                    modifier_count ? &modifier : 0, modifier_count, &found_form) &&
                               found_form == form_index &&
                               a64_arm_m1_scalar_integer_encode(gpr_target, form_index, operands, form.operand_count,
                                                                modifier_count ? &modifier : 0, modifier_count, &word) &&
                               word == test_case->word);
        u32 mnemonic_word = 0;
        BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, form.mnemonic, operands, form.operand_count,
                                                                           modifier_count ? &modifier : 0, modifier_count,
                                                                           &mnemonic_word) &&
                               mnemonic_word == test_case->word);

        for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
        {
            BusterAarch64ArmM1ScalarIntegerOperand descriptor = form.operands[operand_index];
            if (descriptor.kind == BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER)
            {
                A64ScalarIntOperand width_flip[4] = {0};
                for (u32 index = 0; index < form.operand_count; index += 1) width_flip[index] = operands[index];
                width_flip[operand_index].width = width_flip[operand_index].width == 32 ? 64 : 32;
                u32 unchanged_word = UINT32_C(0x5a5aa5a5);
                BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode(gpr_target, form_index, width_flip, form.operand_count,
                                                                            modifier_count ? &modifier : 0, modifier_count,
                                                                            &unchanged_word) &&
                                       unchanged_word == UINT32_C(0x5a5aa5a5));
                A64ScalarIntOperand nonzero_value[4] = {0};
                for (u32 index = 0; index < form.operand_count; index += 1) nonzero_value[index] = operands[index];
                nonzero_value[operand_index].value = 1;
                unchanged_word = UINT32_C(0x5a5aa5a5);
                BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode(gpr_target, form_index, nonzero_value,
                                                                            form.operand_count, modifier_count ? &modifier : 0,
                                                                            modifier_count, &unchanged_word) &&
                                       unchanged_word == UINT32_C(0x5a5aa5a5));
                if (descriptor.register31_role != BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY)
                {
                    A64ScalarIntOperand role_operands[4] = {0};
                    for (u32 index = 0; index < form.operand_count; index += 1) role_operands[index] = operands[index];
                    role_operands[operand_index].index = 31;
                    role_operands[operand_index].stack_pointer = descriptor.register31_role == BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP;
                    BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_encode(gpr_target, form_index, role_operands,
                                                                              form.operand_count, modifier_count ? &modifier : 0,
                                                                              modifier_count, &word));
                    role_operands[operand_index].stack_pointer = !role_operands[operand_index].stack_pointer;
                    unchanged_word = UINT32_C(0x5a5aa5a5);
                    BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode(gpr_target, form_index, role_operands,
                                                                               form.operand_count, modifier_count ? &modifier : 0,
                                                                               modifier_count, &unchanged_word) &&
                                           unchanged_word == UINT32_C(0x5a5aa5a5));
                }
            }
            else
            {
                A64ScalarIntOperand malformed[4] = {0};
                for (u32 index = 0; index < form.operand_count; index += 1) malformed[index] = operands[index];
                malformed[operand_index].width = 1;
                u32 unchanged_word = UINT32_C(0x5a5aa5a5);
                BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode(gpr_target, form_index, malformed,
                                                                            form.operand_count, modifier_count ? &modifier : 0,
                                                                            modifier_count, &unchanged_word) &&
                                       unchanged_word == UINT32_C(0x5a5aa5a5));
                malformed[operand_index] = operands[operand_index];
                malformed[operand_index].index = 1;
                unchanged_word = UINT32_C(0x5a5aa5a5);
                BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode(gpr_target, form_index, malformed,
                                                                            form.operand_count, modifier_count ? &modifier : 0,
                                                                            modifier_count, &unchanged_word) &&
                                       unchanged_word == UINT32_C(0x5a5aa5a5));
            }
        }
        if (form.required_feature != TARGET_CPU_FEATURE_NONE)
        {
            Target missing = gpr_target;
            missing.cpu_features = target_cpu_features_remove(missing.cpu_features, form.required_feature);
            u32 unchanged_word = UINT32_C(0x5a5aa5a5);
            BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode(missing, form_index, operands, form.operand_count,
                                                                       modifier_count ? &modifier : 0, modifier_count,
                                                                       &unchanged_word) &&
                                   unchanged_word == UINT32_C(0x5a5aa5a5));
        }
    }
    for (u32 form_index = 0; form_index < buster_aarch64_arm_m1_scalar_integer_form_count(); form_index += 1)
    {
        BUSTER_TEST(arguments, scalar_fixture_seen[form_index]);
    }
    BUSTER_TEST(arguments, buster_aarch64_metadata_test_packed_access_count() == 0);

    // Null/count/reserved entry-point hardening is checked against a valid
    // three-register shift form so every pointer/count path is exercised.
    A64ScalarIntOperand scalar_harden_operands[3] = {
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 32, .index = 0},
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 32, .index = 1},
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 32, .index = 2},
    };
    A64ScalarIntModifier scalar_harden_modifier = {
        .kind = A64_SCALAR_INT_MODIFIER_SHIFT, .value = A64_SCALAR_INT_SHIFT_LSL, .amount = 0, .present = true,
    };
    u32 scalar_harden_form = UINT32_MAX;
    BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_find_form(S8("add"), scalar_harden_operands, 3,
                                                               &scalar_harden_modifier, 1, &scalar_harden_form));
    u32 hard_word = UINT32_C(0x5a5aa5a5);
    BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode(gpr_target, scalar_harden_form, scalar_harden_operands, 3,
                                                              &scalar_harden_modifier, 2, &hard_word) &&
                               hard_word == UINT32_C(0x5a5aa5a5));
    BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode(gpr_target, scalar_harden_form, 0, 3,
                                                              &scalar_harden_modifier, 1, &hard_word));
    BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode(gpr_target, scalar_harden_form, scalar_harden_operands, 3,
                                                              0, 1, &hard_word));
    BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode(gpr_target, scalar_harden_form, scalar_harden_operands, 3,
                                                              &scalar_harden_modifier, 1, 0));
    A64ScalarIntModifier reserved_modifier = scalar_harden_modifier;
    reserved_modifier.reserved[0] = 1;
    BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode(gpr_target, scalar_harden_form, scalar_harden_operands, 3,
                                                              &reserved_modifier, 1, &hard_word));
    BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode((Target){0}, scalar_harden_form, scalar_harden_operands, 3,
                                                              &scalar_harden_modifier, 1, &hard_word));

    // Ensure no two generated rows share the same semantic key. Fixed bits
    // are included because aliases with different encodings are intentional.
    bool duplicate_scalar_key = false;
    for (u32 left_index = 0; left_index < buster_aarch64_arm_m1_scalar_integer_form_count(); left_index += 1)
    {
        BusterAarch64ArmM1ScalarIntegerForm left = {0};
        buster_aarch64_arm_m1_scalar_integer_form(left_index, &left);
        for (u32 right_index = left_index + 1; right_index < buster_aarch64_arm_m1_scalar_integer_form_count(); right_index += 1)
        {
            BusterAarch64ArmM1ScalarIntegerForm right = {0};
            buster_aarch64_arm_m1_scalar_integer_form(right_index, &right);
            bool same = string_equal(left.mnemonic, right.mnemonic) && left.fixed_mask == right.fixed_mask &&
                        left.fixed_value == right.fixed_value && left.required_feature == right.required_feature &&
                        left.recipe == right.recipe && left.width == right.width && left.operand_count == right.operand_count;
            for (u32 operand_index = 0; same && operand_index < left.operand_count; operand_index += 1)
            {
                same = memcmp(left.operands + operand_index, right.operands + operand_index,
                              sizeof(left.operands[operand_index])) == 0;
            }
            duplicate_scalar_key |= same;
        }
    }
    BUSTER_TEST(arguments, !duplicate_scalar_key);

    // Exhaustively enumerate the architectural logical-immediate fields for
    // both widths. Every legal tuple must decode, re-encode to the same
    // immediate value, and decode again; reserved all-ones encodings and
    // zero/all-ones semantic values are rejected.
    u32 logical_32_count = 0;
    u32 logical_64_n0_count = 0;
    u32 logical_64_n1_count = 0;
    for (u8 width = 32; width <= 64; width += 32)
    {
        for (u32 n = 0; n <= 1; n += 1)
        {
            for (u32 immr = 0; immr < 64; immr += 1)
            {
                for (u32 imms = 0; imms < 64; imms += 1)
                {
                    u64 immediate = 0;
                    if (!aarch64_scalar_test_decode_logical_immediate(width, n, immr, imms, &immediate)) continue;
                    if (width == 32) logical_32_count += 1;
                    else if (n == 0) logical_64_n0_count += 1;
                    else logical_64_n1_count += 1;
                    A64ScalarIntOperand logical_operands[3] = {
                        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = width, .index = 0},
                        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = width, .index = 1},
                        {.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = immediate},
                    };
                    u32 logical_word = UINT32_C(0x5a5aa5a5);
                    bool encoded_logical = a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("and"), logical_operands, 3,
                                                                                        0, 0, &logical_word);
                    BUSTER_TEST(arguments, encoded_logical);
                    if (!encoded_logical) continue;
                    u32 encoded_n = (logical_word >> 22) & 1u;
                    u32 encoded_immr = (logical_word >> 16) & 63u;
                    u32 encoded_imms = (logical_word >> 10) & 63u;
                    u64 round_trip_immediate = 0;
                    BUSTER_TEST(arguments, aarch64_scalar_test_decode_logical_immediate(width, encoded_n, encoded_immr,
                                                                                         encoded_imms, &round_trip_immediate) &&
                                           round_trip_immediate == immediate);
                }
            }
        }
    }
    BUSTER_TEST(arguments, logical_32_count == 3648 && logical_64_n0_count == 3648 && logical_64_n1_count == 4032);
    for (u8 width = 32; width <= 64; width += 32)
    {
        A64ScalarIntOperand logical_operands[3] = {
            {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = width, .index = 0},
            {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = width, .index = 1},
            {.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 0},
        };
        u32 logical_word = UINT32_C(0x5a5aa5a5);
        BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("and"), logical_operands, 3, 0, 0,
                                                                            &logical_word) &&
                               logical_word == UINT32_C(0x5a5aa5a5));
        logical_operands[2].value = width == 32 ? UINT64_C(0xffffffff) : UINT64_MAX;
        logical_word = UINT32_C(0x5a5aa5a5);
        BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("and"), logical_operands, 3, 0, 0,
                                                                            &logical_word) &&
                               logical_word == UINT32_C(0x5a5aa5a5));
    }
    BUSTER_TEST(arguments, buster_aarch64_metadata_test_packed_access_count() == 0);

    // Modifier and field boundaries across every scalar recipe.
    A64ScalarIntOperand boundary_shift_operands[3] = {
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 32, .index = 0},
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 32, .index = 1},
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 32, .index = 2},
    };
    A64ScalarIntModifier boundary_shift = {
        .kind = A64_SCALAR_INT_MODIFIER_SHIFT, .value = A64_SCALAR_INT_SHIFT_LSL, .present = true,
    };
    u32 boundary_word = 0;
    for (u8 width = 32; width <= 64; width += 32)
    {
        for (u32 amount = 0; amount <= (width == 32 ? 31u : 63u); amount += (width == 32 ? 31u : 63u))
        {
            boundary_shift_operands[0].width = width;
            boundary_shift_operands[1].width = width;
            boundary_shift_operands[2].width = width;
            boundary_shift.amount = amount;
            BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("add"), boundary_shift_operands, 3,
                                                                              &boundary_shift, 1, &boundary_word));
            BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("and"), boundary_shift_operands, 3,
                                                                              &boundary_shift, 1, &boundary_word));
        }
        boundary_shift_operands[0].width = width;
        boundary_shift_operands[1].width = width;
        boundary_shift_operands[2].width = width;
        boundary_shift.amount = width == 32 ? 32 : 64;
        boundary_word = UINT32_C(0x5a5aa5a5);
        BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("add"), boundary_shift_operands, 3,
                                                                            &boundary_shift, 1, &boundary_word) &&
                               boundary_word == UINT32_C(0x5a5aa5a5));
    }
    boundary_shift_operands[0].width = 32;
    boundary_shift_operands[1].width = 32;
    boundary_shift_operands[2].width = 32;
    boundary_shift.amount = 0;
    boundary_shift.value = A64_SCALAR_INT_SHIFT_ROR;
    BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("add"), boundary_shift_operands, 3,
                                                                        &boundary_shift, 1, &boundary_word));
    BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("orr"), boundary_shift_operands, 3,
                                                                       &boundary_shift, 1, &boundary_word));

    A64ScalarIntOperand boundary_ext_operands[3] = {
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 32, .index = 0},
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 32, .index = 1},
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 32, .index = 2},
    };
    A64ScalarIntModifier boundary_extend = {
        .kind = A64_SCALAR_INT_MODIFIER_EXTEND, .value = A64_SCALAR_INT_EXTEND_UXTW, .present = true,
    };
    for (u8 width = 32; width <= 64; width += 32)
    {
        boundary_ext_operands[0].width = width;
        boundary_ext_operands[1].width = width;
        for (u32 extend = A64_SCALAR_INT_EXTEND_UXTB; extend <= A64_SCALAR_INT_EXTEND_SXTX; extend += 1)
        {
            boundary_extend.value = (u8)extend;
            boundary_ext_operands[2].width = (extend == A64_SCALAR_INT_EXTEND_UXTX || extend == A64_SCALAR_INT_EXTEND_SXTX) ? 64 : 32;
            bool x_extension = extend == A64_SCALAR_INT_EXTEND_UXTX || extend == A64_SCALAR_INT_EXTEND_SXTX;
            bool extension_ok = a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("add"), boundary_ext_operands, 3,
                                                                            &boundary_extend, 1, &boundary_word);
            BUSTER_TEST(arguments, extension_ok == (width == 64 || !x_extension));
        }
        boundary_extend.value = A64_SCALAR_INT_EXTEND_UXTW;
        boundary_extend.amount = 5;
        boundary_ext_operands[2].width = 32;
        boundary_word = UINT32_C(0x5a5aa5a5);
        BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("add"), boundary_ext_operands, 3,
                                                                            &boundary_extend, 1, &boundary_word) &&
                               boundary_word == UINT32_C(0x5a5aa5a5));
        boundary_extend.amount = 0;
    }
    A64ScalarIntOperand boundary_imm_operands[3] = {
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 64, .index = 0},
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 64, .index = 1},
        {.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 4095},
    };
    A64ScalarIntModifier boundary_imm_shift = {
        .kind = A64_SCALAR_INT_MODIFIER_SHIFT, .value = A64_SCALAR_INT_SHIFT_LSL, .present = true,
    };
    boundary_imm_shift.amount = 0;
    BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("add"), boundary_imm_operands, 3,
                                                                       &boundary_imm_shift, 1, &boundary_word));
    boundary_imm_shift.amount = 12;
    boundary_imm_operands[2].value = 1;
    BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("add"), boundary_imm_operands, 3,
                                                                       &boundary_imm_shift, 1, &boundary_word));
    boundary_imm_shift.amount = 1;
    boundary_word = UINT32_C(0x5a5aa5a5);
    BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("add"), boundary_imm_operands, 3,
                                                                        &boundary_imm_shift, 1, &boundary_word) &&
                               boundary_word == UINT32_C(0x5a5aa5a5));

    A64ScalarIntOperand boundary_bitfield_operands[4] = {
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 64, .index = 0},
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 64, .index = 1},
        {.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 63},
        {.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 63},
    };
    BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("sbfm"), boundary_bitfield_operands, 4,
                                                                       0, 0, &boundary_word));
    boundary_bitfield_operands[2].value = 64;
    boundary_word = UINT32_C(0x5a5aa5a5);
    BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("sbfm"), boundary_bitfield_operands, 4,
                                                                        0, 0, &boundary_word) &&
                               boundary_word == UINT32_C(0x5a5aa5a5));
    boundary_bitfield_operands[2].value = 63;
    A64ScalarIntOperand boundary_extract_operands[4] = {
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 64, .index = 0},
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 64, .index = 1},
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 64, .index = 2},
        {.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 63},
    };
    BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("extr"), boundary_extract_operands, 4,
                                                                       0, 0, &boundary_word));
    boundary_extract_operands[3].value = 64;
    boundary_word = UINT32_C(0x5a5aa5a5);
    BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("extr"), boundary_extract_operands, 4,
                                                                        0, 0, &boundary_word) &&
                               boundary_word == UINT32_C(0x5a5aa5a5));

    A64ScalarIntOperand boundary_move_operands[2] = {
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 32, .index = 0},
        {.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 0xffff},
    };
    A64ScalarIntModifier boundary_move_shift = {
        .kind = A64_SCALAR_INT_MODIFIER_SHIFT, .value = A64_SCALAR_INT_SHIFT_LSL, .present = true,
    };
    for (u32 amount = 0; amount <= 16; amount += 16)
    {
        boundary_move_shift.amount = amount;
        BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("movz"), boundary_move_operands, 2,
                                                                           &boundary_move_shift, 1, &boundary_word));
    }
    boundary_move_shift.amount = 32;
    boundary_word = UINT32_C(0x5a5aa5a5);
    BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("movz"), boundary_move_operands, 2,
                                                                        &boundary_move_shift, 1, &boundary_word) &&
                               boundary_word == UINT32_C(0x5a5aa5a5));

    A64ScalarIntOperand boundary_ccmp_imm[4] = {
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 64, .index = 0},
        {.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 31},
        {.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 15},
        {.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 15},
    };
    BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("ccmp"), boundary_ccmp_imm, 4, 0, 0,
                                                                       &boundary_word));
    boundary_ccmp_imm[1].value = 32;
    boundary_word = UINT32_C(0x5a5aa5a5);
    BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("ccmp"), boundary_ccmp_imm, 4, 0, 0,
                                                                        &boundary_word) &&
                               boundary_word == UINT32_C(0x5a5aa5a5));
    A64ScalarIntOperand boundary_rmif[3] = {
        {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = 64, .index = 0},
        {.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 63},
        {.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 15},
    };
    BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("rmif"), boundary_rmif, 3, 0, 0,
                                                                       &boundary_word));
    boundary_rmif[1].value = 64;
    boundary_word = UINT32_C(0x5a5aa5a5);
    BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("rmif"), boundary_rmif, 3, 0, 0,
                                                                        &boundary_word) &&
                               boundary_word == UINT32_C(0x5a5aa5a5));
    A64ScalarIntOperand boundary_udf[1] = {{.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 0xffff}};
    BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("udf"), boundary_udf, 1, 0, 0, &boundary_word));
    boundary_udf[0].value = 0x10000;
    boundary_word = UINT32_C(0x5a5aa5a5);
    BUSTER_TEST(arguments, !a64_arm_m1_scalar_integer_encode_mnemonic(gpr_target, S8("udf"), boundary_udf, 1, 0, 0,
                                                                        &boundary_word) &&
                               boundary_word == UINT32_C(0x5a5aa5a5));
    BUSTER_TEST(arguments, buster_aarch64_metadata_test_packed_access_count() == 0);

    // Fourteen rows are also present in the pre-existing named production
    // plan. Match those plans by instruction bit position (not field order)
    // and require bit-for-bit equality with the direct projection.
    static struct
    {
        u32 production_form_id;
        String8 mnemonic;
        u8 width;
        u8 operand_count;
    } const direct_production_overlaps[] = {
        {BUSTER_AARCH64_GENERATED_FORM_ASRVWR, S8_INITIALIZER("asrv"), 32, 3},
        {BUSTER_AARCH64_GENERATED_FORM_ASRVXR, S8_INITIALIZER("asrv"), 64, 3},
        {BUSTER_AARCH64_GENERATED_FORM_LSLVWR, S8_INITIALIZER("lslv"), 32, 3},
        {BUSTER_AARCH64_GENERATED_FORM_LSLVXR, S8_INITIALIZER("lslv"), 64, 3},
        {BUSTER_AARCH64_GENERATED_FORM_LSRVWR, S8_INITIALIZER("lsrv"), 32, 3},
        {BUSTER_AARCH64_GENERATED_FORM_LSRVXR, S8_INITIALIZER("lsrv"), 64, 3},
        {BUSTER_AARCH64_GENERATED_FORM_MADDWRRR, S8_INITIALIZER("madd"), 32, 4},
        {BUSTER_AARCH64_GENERATED_FORM_MADDXRRR, S8_INITIALIZER("madd"), 64, 4},
        {BUSTER_AARCH64_GENERATED_FORM_MSUBWRRR, S8_INITIALIZER("msub"), 32, 4},
        {BUSTER_AARCH64_GENERATED_FORM_MSUBXRRR, S8_INITIALIZER("msub"), 64, 4},
        {BUSTER_AARCH64_GENERATED_FORM_SDIVWR, S8_INITIALIZER("sdiv"), 32, 3},
        {BUSTER_AARCH64_GENERATED_FORM_SDIVXR, S8_INITIALIZER("sdiv"), 64, 3},
        {BUSTER_AARCH64_GENERATED_FORM_UDIVWR, S8_INITIALIZER("udiv"), 32, 3},
        {BUSTER_AARCH64_GENERATED_FORM_UDIVXR, S8_INITIALIZER("udiv"), 64, 3},
    };
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(direct_production_overlaps) == 14);
    buster_aarch64_metadata_test_reset_packed_access_counter();
    for (u32 overlap_index = 0; overlap_index < BUSTER_ARRAY_LENGTH(direct_production_overlaps); overlap_index += 1)
    {
        u32 production_form_id = direct_production_overlaps[overlap_index].production_form_id;
        u8 width = direct_production_overlaps[overlap_index].width;
        u32 operand_count = direct_production_overlaps[overlap_index].operand_count;
        A64GprOperand direct_operands[4] = {0};
        for (u32 operand_index = 0; operand_index < operand_count; operand_index += 1)
        {
            direct_operands[operand_index] = (A64GprOperand){.index = (u8)(operand_index + 1), .width = width};
        }
        u32 direct_form_index = UINT32_MAX;
        BusterAarch64ArmM1GprForm direct_form = {0};
        BUSTER_TEST(arguments, buster_aarch64_arm_m1_gpr_find_form(direct_production_overlaps[overlap_index].mnemonic, direct_operands,
                                                                    operand_count, &direct_form_index) &&
                                   buster_aarch64_arm_m1_gpr_form(direct_form_index, &direct_form));
        u16 plan_index = buster_aarch64_generated_production_plan_index(production_form_id);
        BusterAarch64GeneratedProductionForm const* plan = buster_aarch64_generated_production_form_at(plan_index);
        u32 field_values[8] = {0};
        bool mapped = plan != 0 && plan->field_count == operand_count;
        if (mapped)
        {
            for (u32 field_index = 0; field_index < plan->field_count; field_index += 1)
            {
                BusterAarch64GeneratedProductionField const* field =
                    buster_aarch64_generated_production_field_at(plan->field_first + field_index);
                BusterAarch64GeneratedProductionSegment const* segment =
                    field && field->segment_count ? buster_aarch64_generated_production_segment_at(field->segment_first) : 0;
                u32 operand_index = UINT32_MAX;
                for (u32 candidate = 0; candidate < operand_count; candidate += 1)
                {
                    if (direct_form.operands[candidate].bit_lsb == (segment ? segment->instruction_lsb : UINT8_MAX)) operand_index = candidate;
                }
                if (!field || !segment || operand_index == UINT32_MAX) mapped = false;
                else field_values[field_index] = direct_operands[operand_index].index;
            }
        }
        u32 production_word = 0, direct_word = 0;
        BUSTER_TEST(arguments, mapped && buster_aarch64_production_raw_encode(production_form_id, field_values, plan->field_count, &production_word) &&
                                   buster_aarch64_arm_m1_gpr_encode(gpr_target, direct_form_index, direct_operands, operand_count, &direct_word) &&
                                   production_word == direct_word);
    }
    BUSTER_TEST(arguments, buster_aarch64_metadata_test_packed_access_count() == 0);

    static struct
    {
        u32 production_form_id;
        String8 mnemonic;
        u8 width;
        bool bitfield;
    } const scalar_production_overlaps[] = {
        {BUSTER_AARCH64_GENERATED_FORM_ADDWRS, S8_INITIALIZER("add"), 32, false},
        {BUSTER_AARCH64_GENERATED_FORM_ADDXRS, S8_INITIALIZER("add"), 64, false},
        {BUSTER_AARCH64_GENERATED_FORM_ANDWRS, S8_INITIALIZER("and"), 32, false},
        {BUSTER_AARCH64_GENERATED_FORM_ANDXRS, S8_INITIALIZER("and"), 64, false},
        {BUSTER_AARCH64_GENERATED_FORM_EORWRS, S8_INITIALIZER("eor"), 32, false},
        {BUSTER_AARCH64_GENERATED_FORM_EORXRS, S8_INITIALIZER("eor"), 64, false},
        {BUSTER_AARCH64_GENERATED_FORM_ORNWRS, S8_INITIALIZER("orn"), 32, false},
        {BUSTER_AARCH64_GENERATED_FORM_ORNXRS, S8_INITIALIZER("orn"), 64, false},
        {BUSTER_AARCH64_GENERATED_FORM_ORRWRS, S8_INITIALIZER("orr"), 32, false},
        {BUSTER_AARCH64_GENERATED_FORM_ORRXRS, S8_INITIALIZER("orr"), 64, false},
        {BUSTER_AARCH64_GENERATED_FORM_SBFMXRI, S8_INITIALIZER("sbfm"), 64, true},
        {BUSTER_AARCH64_GENERATED_FORM_SUBSWRS, S8_INITIALIZER("subs"), 32, false},
        {BUSTER_AARCH64_GENERATED_FORM_SUBSXRS, S8_INITIALIZER("subs"), 64, false},
        {BUSTER_AARCH64_GENERATED_FORM_SUBWRS, S8_INITIALIZER("sub"), 32, false},
        {BUSTER_AARCH64_GENERATED_FORM_SUBXRS, S8_INITIALIZER("sub"), 64, false},
    };
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(scalar_production_overlaps) == 15);
    for (u32 overlap_index = 0; overlap_index < BUSTER_ARRAY_LENGTH(scalar_production_overlaps); overlap_index += 1)
    {
        u8 width = scalar_production_overlaps[overlap_index].width;
        A64ScalarIntOperand overlap_operands[4] = {
            {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = width, .index = 1},
            {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = width, .index = 2},
            {.kind = A64_SCALAR_INT_OPERAND_REGISTER, .width = width, .index = 3},
            {.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 42},
        };
        u32 operand_count = scalar_production_overlaps[overlap_index].bitfield ? 4 : 3;
        if (scalar_production_overlaps[overlap_index].bitfield) overlap_operands[2] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = 7};
        u32 scalar_form_index = UINT32_MAX;
        u32 overlap_word = 0;
        BUSTER_TEST(arguments, a64_arm_m1_scalar_integer_find_form(scalar_production_overlaps[overlap_index].mnemonic, overlap_operands,
                                                                    operand_count, 0, 0, &scalar_form_index) &&
                                   a64_arm_m1_scalar_integer_encode(gpr_target, scalar_form_index, overlap_operands, operand_count, 0, 0, &overlap_word));
        u16 plan_index = buster_aarch64_generated_production_plan_index(scalar_production_overlaps[overlap_index].production_form_id);
        BusterAarch64GeneratedProductionForm const* plan = buster_aarch64_generated_production_form_at(plan_index);
        u32 field_values[8] = {0};
        bool mapped = plan != 0 && plan->field_count == 4;
        if (mapped && scalar_production_overlaps[overlap_index].bitfield)
        {
            field_values[0] = overlap_operands[0].index;
            field_values[1] = overlap_operands[1].index;
            field_values[2] = (u32)overlap_operands[3].value;
            field_values[3] = (u32)overlap_operands[2].value;
        }
        else if (mapped)
        {
            field_values[0] = overlap_operands[0].index;
            field_values[1] = overlap_operands[1].index;
            field_values[2] = 0;
            field_values[3] = overlap_operands[2].index;
        }
        u32 production_word = 0;
        bool overlap_equal = mapped && buster_aarch64_production_raw_encode(scalar_production_overlaps[overlap_index].production_form_id,
                                                                              field_values, plan->field_count, &production_word) &&
                             production_word == overlap_word;
        BUSTER_TEST(arguments, overlap_equal);
    }
    BUSTER_TEST(arguments, buster_aarch64_metadata_test_packed_access_count() == 0);

    // The Arm canonical decoder owns the complete 1,523-form Apple-M1
    // closure.  Exercise every deterministic representative through the
    // pointer-free decoder and raw field round trip, then probe malformed
    // calls and feature filtering without allowing output mutation.
    bool canonical_all_representatives = buster_aarch64_canonical_form_count() == 1523 &&
                                         buster_aarch64_canonical_form_count() == BUSTER_AARCH64_CANONICAL_DECODER_FORM_COUNT;
    bool canonical_all_round_trips = true;
    bool canonical_negative_immutability = true;
    Target canonical_target = a64_encoding_m1_target(true);
    for (u32 form_index = 0; form_index < buster_aarch64_canonical_form_count(); form_index += 1)
    {
        BusterAarch64CanonicalFormInfo info = {0};
        BusterAarch64CanonicalDecodeResult canonical_decoded = {0};
        canonical_all_representatives = canonical_all_representatives && buster_aarch64_canonical_form(form_index, &info);
        BusterAarch64CanonicalDecodeStatus status =
            buster_aarch64_canonical_decode(canonical_target, info.representative_word, &canonical_decoded);
        canonical_all_representatives = canonical_all_representatives && status == BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS &&
                                         canonical_decoded.form_index == form_index && canonical_decoded.arm_row_digest == info.arm_row_digest &&
                                         canonical_decoded.field_count == info.field_count;
        u32 reencoded = 0;
        canonical_all_round_trips = canonical_all_round_trips &&
                                    buster_aarch64_canonical_raw_encode(form_index, canonical_decoded.field_values, canonical_decoded.field_count, &reencoded) &&
                                    reencoded == info.representative_word;
        u32 field_values[32] = {0};
        u32 sentinel_values[32];
        for (u32 value_index = 0; value_index < BUSTER_ARRAY_LENGTH(sentinel_values); value_index += 1)
        {
            sentinel_values[value_index] = UINT32_C(0xa5a50000) + value_index;
        }
        memcpy(field_values, sentinel_values, sizeof(field_values));
        bool malformed_decode = !buster_aarch64_canonical_raw_decode(form_index, info.representative_word, field_values,
                                                                      info.field_count + 1);
        canonical_negative_immutability = canonical_negative_immutability && malformed_decode &&
                                           memcmp(field_values, sentinel_values, sizeof(field_values)) == 0;
        if (info.field_count)
        {
            BusterAarch64CanonicalFieldInfo field = {0};
            if (buster_aarch64_canonical_field(form_index, 0, &field) && field.source_mask != UINT32_MAX)
            {
                u32 invalid_values[32] = {0};
                invalid_values[0] = ~field.source_mask;
                u32 sentinel_word = UINT32_C(0xdeadbeef);
                u32 saved_word = sentinel_word;
                bool malformed_encode = !buster_aarch64_canonical_raw_encode(form_index, invalid_values, info.field_count, &sentinel_word);
                canonical_negative_immutability = canonical_negative_immutability && malformed_encode && sentinel_word == saved_word;
            }
        }
    }
    BUSTER_TEST(arguments, canonical_all_representatives);
    BUSTER_TEST(arguments, canonical_all_round_trips);
    BUSTER_TEST(arguments, canonical_negative_immutability);

    // Empty explicit features reject extension-only rows, while baseline
    // forms remain decodable.  This also verifies feature filtering occurs
    // before the fixed-mask specificity ranking.
    Target no_features = canonical_target;
    no_features.cpu_features_explicit = true;
    no_features.cpu_features = target_cpu_features_empty();
    bool saw_unsupported = false;
    bool saw_baseline = false;
    for (u32 form_index = 0; form_index < buster_aarch64_canonical_form_count(); form_index += 1)
    {
        BusterAarch64CanonicalFormInfo info = {0};
        if (!buster_aarch64_canonical_form(form_index, &info)) continue;
        BusterAarch64CanonicalDecodeResult canonical_feature_result = {0};
        BusterAarch64CanonicalDecodeStatus status =
            buster_aarch64_canonical_decode(no_features, info.representative_word, &canonical_feature_result);
        if (status == BUSTER_AARCH64_CANONICAL_DECODE_UNSUPPORTED_FEATURE) saw_unsupported = true;
        if (status == BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS) saw_baseline = true;
    }
    BUSTER_TEST(arguments, saw_unsupported && saw_baseline);

    BusterAarch64CanonicalDecodeResult immutable_result;
    for (u32 value_index = 0; value_index < BUSTER_ARRAY_LENGTH(immutable_result.field_values); value_index += 1)
    {
        immutable_result.field_values[value_index] = UINT32_C(0x5a5a0000) + value_index;
    }
    immutable_result.form_index = UINT32_C(0x12345678);
    immutable_result.arm_row_digest = UINT64_C(0x0123456789abcdef);
    immutable_result.field_count = 31;
    BusterAarch64CanonicalDecodeResult saved_result = immutable_result;
    BusterAarch64CanonicalDecodeStatus random_status = buster_aarch64_canonical_decode(canonical_target, UINT32_C(0xffffffff), &immutable_result);
    BUSTER_TEST(arguments, random_status != BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS ||
                                   memcmp(&immutable_result, &saved_result, sizeof(saved_result)) == 0);

    // Typed direct-GPR and scalar projections decode only through canonical
    // Arm row identity.  Every executable generated form must round-trip its
    // canonical representative through both form-directed and word-first
    // paths; the permanent UDF row is deliberately fail-closed.
    bool gpr_typed_round_trips = true;
    for (u32 direct_index = 0; direct_index < buster_aarch64_arm_m1_gpr_form_count(); direct_index += 1)
    {
        BusterAarch64ArmM1GprForm direct_form = {0};
        gpr_typed_round_trips = gpr_typed_round_trips && buster_aarch64_arm_m1_gpr_form(direct_index, &direct_form);
        u32 representative_word = 0;
        bool representative_found = false;
        for (u32 canonical_index = 0; canonical_index < buster_aarch64_canonical_form_count(); canonical_index += 1)
        {
            BusterAarch64CanonicalFormInfo info = {0};
            if (buster_aarch64_canonical_form(canonical_index, &info) && info.arm_row_digest == direct_form.arm_row_digest)
            {
                representative_word = info.representative_word;
                representative_found = true;
                break;
            }
        }
        gpr_typed_round_trips = gpr_typed_round_trips && representative_found;
        A64GprOperand decoded_operands[4] = {0};
        u32 decoded_count = 0;
        gpr_typed_round_trips = gpr_typed_round_trips &&
                                buster_aarch64_arm_m1_gpr_decode_form(canonical_target, direct_index, representative_word,
                                                                       decoded_operands, BUSTER_ARRAY_LENGTH(decoded_operands), &decoded_count) &&
                                decoded_count == direct_form.operand_count;
        u32 reencoded_word = 0;
        gpr_typed_round_trips = gpr_typed_round_trips &&
                                buster_aarch64_arm_m1_gpr_encode(canonical_target, direct_index, decoded_operands, decoded_count,
                                                                  &reencoded_word) &&
                                reencoded_word == representative_word;
        A64GprOperand word_first_operands[4] = {0};
        u32 word_first_form = UINT32_MAX;
        u32 word_first_count = 0;
        gpr_typed_round_trips = gpr_typed_round_trips &&
                                buster_aarch64_arm_m1_gpr_decode(canonical_target, representative_word, &word_first_form,
                                                                  word_first_operands, BUSTER_ARRAY_LENGTH(word_first_operands),
                                                                  &word_first_count) &&
                                word_first_form == direct_index && word_first_count == decoded_count &&
                                memcmp(word_first_operands, decoded_operands, sizeof(decoded_operands)) == 0;
    }
    BUSTER_TEST(arguments, gpr_typed_round_trips);

    bool scalar_typed_round_trips = true;
    for (u32 scalar_index = 0; scalar_index < buster_aarch64_arm_m1_scalar_integer_form_count(); scalar_index += 1)
    {
        BusterAarch64ArmM1ScalarIntegerForm scalar_form = {0};
        scalar_typed_round_trips = scalar_typed_round_trips &&
                                   buster_aarch64_arm_m1_scalar_integer_form(scalar_index, &scalar_form);
        if (scalar_form.recipe == BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_UDF)
        {
            A64ScalarIntOperand udf_operands[4] = {0};
            A64ScalarIntModifier udf_modifier = {0};
            u32 udf_operand_count = UINT32_C(0x12345678);
            u32 udf_modifier_count = UINT32_C(0x87654321);
            scalar_typed_round_trips = scalar_typed_round_trips &&
                                       !buster_aarch64_arm_m1_scalar_integer_decode_form(
                                           canonical_target, scalar_index, UINT32_C(0x00001234), udf_operands,
                                           BUSTER_ARRAY_LENGTH(udf_operands), &udf_operand_count, &udf_modifier, 1,
                                           &udf_modifier_count) &&
                                       udf_operand_count == UINT32_C(0x12345678) && udf_modifier_count == UINT32_C(0x87654321);
            continue;
        }
        u32 representative_word = 0;
        bool representative_found = false;
        for (u32 canonical_index = 0; canonical_index < buster_aarch64_canonical_form_count(); canonical_index += 1)
        {
            BusterAarch64CanonicalFormInfo info = {0};
            if (buster_aarch64_canonical_form(canonical_index, &info) && info.arm_row_digest == scalar_form.arm_row_digest)
            {
                representative_word = info.representative_word;
                representative_found = true;
                break;
            }
        }
        scalar_typed_round_trips = scalar_typed_round_trips && representative_found;
        A64ScalarIntOperand decoded_operands[4] = {0};
        A64ScalarIntModifier decoded_modifier = {0};
        u32 decoded_operand_count = 0;
        u32 decoded_modifier_count = 0;
        scalar_typed_round_trips = scalar_typed_round_trips &&
                                   buster_aarch64_arm_m1_scalar_integer_decode_form(
                                       canonical_target, scalar_index, representative_word, decoded_operands,
                                       BUSTER_ARRAY_LENGTH(decoded_operands), &decoded_operand_count, &decoded_modifier, 1,
                                       &decoded_modifier_count);
        u32 reencoded_word = 0;
        scalar_typed_round_trips = scalar_typed_round_trips &&
                                   buster_aarch64_arm_m1_scalar_integer_encode(
                                       canonical_target, scalar_index, decoded_operands, decoded_operand_count,
                                       decoded_modifier_count ? &decoded_modifier : 0, decoded_modifier_count, &reencoded_word) &&
                                   reencoded_word == representative_word;
        A64ScalarIntOperand word_first_operands[4] = {0};
        A64ScalarIntModifier word_first_modifier = {0};
        u32 word_first_form = UINT32_MAX;
        u32 word_first_operand_count = 0;
        u32 word_first_modifier_count = 0;
        scalar_typed_round_trips = scalar_typed_round_trips &&
                                   buster_aarch64_arm_m1_scalar_integer_decode(
                                       canonical_target, representative_word, &word_first_form, word_first_operands,
                                       BUSTER_ARRAY_LENGTH(word_first_operands), &word_first_operand_count,
                                       &word_first_modifier, 1, &word_first_modifier_count) &&
                                   word_first_form == scalar_index && word_first_operand_count == decoded_operand_count &&
                                   word_first_modifier_count == decoded_modifier_count &&
                                   memcmp(word_first_operands, decoded_operands, sizeof(decoded_operands)) == 0 &&
                                   memcmp(&word_first_modifier, &decoded_modifier, sizeof(decoded_modifier)) == 0;
    }
    BUSTER_TEST(arguments, scalar_typed_round_trips);

    A64GprOperand immutable_gpr_operands[4];
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(immutable_gpr_operands); index += 1)
    {
        immutable_gpr_operands[index] = (A64GprOperand){.index = 7, .width = 64, .stack_pointer = true, .reserved = 1};
    }
    A64GprOperand saved_gpr_operands[4] = {0};
    memcpy(saved_gpr_operands, immutable_gpr_operands, sizeof(saved_gpr_operands));
    u32 immutable_gpr_count = UINT32_C(0x13579bdf);
    BUSTER_TEST(arguments, !buster_aarch64_arm_m1_gpr_decode_form(canonical_target, 0, UINT32_C(0xffffffff), immutable_gpr_operands, 0,
                                                                   &immutable_gpr_count) &&
                               immutable_gpr_count == UINT32_C(0x13579bdf) &&
                               memcmp(immutable_gpr_operands, saved_gpr_operands, sizeof(saved_gpr_operands)) == 0);

    A64ScalarIntOperand immutable_scalar_operands[4] = {0};
    A64ScalarIntModifier immutable_scalar_modifier = {.kind = A64_SCALAR_INT_MODIFIER_EXTEND, .value = A64_SCALAR_INT_EXTEND_SXTX,
                                                       .amount = 7, .present = true, .reserved = {1, 2, 3, 4, 5}};
    u32 immutable_scalar_form = UINT32_C(0xabcdef01);
    u32 immutable_scalar_operand_count = UINT32_C(0x2468ace0);
    u32 immutable_scalar_modifier_count = UINT32_C(0xdeadbeef);
    BUSTER_TEST(arguments, !buster_aarch64_arm_m1_scalar_integer_decode(
                               canonical_target, UINT32_C(0xffffffff), &immutable_scalar_form, immutable_scalar_operands,
                               BUSTER_ARRAY_LENGTH(immutable_scalar_operands), &immutable_scalar_operand_count,
                               &immutable_scalar_modifier, 1, &immutable_scalar_modifier_count) &&
                               immutable_scalar_form == UINT32_C(0xabcdef01) &&
                               immutable_scalar_operand_count == UINT32_C(0x2468ace0) &&
                               immutable_scalar_modifier_count == UINT32_C(0xdeadbeef));

    // Count outputs are independent transactional destinations.  Identical
    // or partially overlapping count pointers must be rejected before either
    // count (or any other output) is read or written.
    u32 overlap_scalar_form = UINT32_MAX;
    u32 overlap_scalar_word = 0;
    bool overlap_scalar_representative_found = false;
    for (u32 scalar_index = 0; scalar_index < buster_aarch64_arm_m1_scalar_integer_form_count() &&
                                      !overlap_scalar_representative_found;
         scalar_index += 1)
    {
        BusterAarch64ArmM1ScalarIntegerForm scalar_form = {0};
        if (!buster_aarch64_arm_m1_scalar_integer_form(scalar_index, &scalar_form) ||
            scalar_form.recipe == BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_UDF)
        {
            continue;
        }
        for (u32 canonical_index = 0; canonical_index < buster_aarch64_canonical_form_count(); canonical_index += 1)
        {
            BusterAarch64CanonicalFormInfo info = {0};
            if (buster_aarch64_canonical_form(canonical_index, &info) && info.arm_row_digest == scalar_form.arm_row_digest)
            {
                overlap_scalar_form = scalar_index;
                overlap_scalar_word = info.representative_word;
                overlap_scalar_representative_found = true;
                break;
            }
        }
    }
    bool scalar_count_pointer_alias_rejected = overlap_scalar_representative_found;
    if (overlap_scalar_representative_found)
    {
        A64ScalarIntOperand alias_operands[4];
        A64ScalarIntOperand alias_operands_saved[4];
        A64ScalarIntModifier alias_modifier;
        A64ScalarIntModifier alias_modifier_saved;
        memset(alias_operands, 0xa5, sizeof(alias_operands));
        memset(alias_modifier.reserved, 0x5a, sizeof(alias_modifier.reserved));
        alias_modifier.amount = UINT64_C(0x1122334455667788);
        alias_modifier.kind = A64_SCALAR_INT_MODIFIER_EXTEND;
        alias_modifier.value = A64_SCALAR_INT_EXTEND_SXTX;
        alias_modifier.present = true;
        memcpy(alias_operands_saved, alias_operands, sizeof(alias_operands));
        alias_modifier_saved = alias_modifier;

        u32 identical_count = UINT32_C(0x13579bdf);
        u32 identical_count_saved = identical_count;
        u32 form_sentinel = UINT32_C(0x2468ace0);
        u32 form_sentinel_saved = form_sentinel;
        scalar_count_pointer_alias_rejected = scalar_count_pointer_alias_rejected &&
                                              !buster_aarch64_arm_m1_scalar_integer_decode_form(
                                                  canonical_target, overlap_scalar_form, overlap_scalar_word, alias_operands,
                                                  BUSTER_ARRAY_LENGTH(alias_operands), &identical_count, &alias_modifier, 1,
                                                  &identical_count) &&
                                              identical_count == identical_count_saved && form_sentinel == form_sentinel_saved &&
                                              memcmp(alias_operands, alias_operands_saved, sizeof(alias_operands)) == 0 &&
                                              memcmp(&alias_modifier, &alias_modifier_saved, sizeof(alias_modifier)) == 0;

        u8 partial_count_bytes[sizeof(u32) * 2 + 1];
        u8 partial_count_bytes_saved[sizeof(partial_count_bytes)];
        for (u32 byte_index = 0; byte_index < BUSTER_ARRAY_LENGTH(partial_count_bytes); byte_index += 1)
        {
            partial_count_bytes[byte_index] = (u8)(0x30 + byte_index);
        }
        memcpy(partial_count_bytes_saved, partial_count_bytes, sizeof(partial_count_bytes));
        u32* partial_operand_count = (u32*)(void*)(partial_count_bytes + 0);
        u32* partial_modifier_count = (u32*)(void*)(partial_count_bytes + 1);
        form_sentinel = UINT32_C(0x89abcdef);
        form_sentinel_saved = form_sentinel;
        scalar_count_pointer_alias_rejected = scalar_count_pointer_alias_rejected &&
                                              !buster_aarch64_arm_m1_scalar_integer_decode_form(
                                                  canonical_target, overlap_scalar_form, overlap_scalar_word, alias_operands,
                                                  BUSTER_ARRAY_LENGTH(alias_operands), partial_operand_count, &alias_modifier, 1,
                                                  partial_modifier_count) &&
                                              form_sentinel == form_sentinel_saved &&
                                              memcmp(alias_operands, alias_operands_saved, sizeof(alias_operands)) == 0 &&
                                              memcmp(&alias_modifier, &alias_modifier_saved, sizeof(alias_modifier)) == 0 &&
                                              memcmp(partial_count_bytes, partial_count_bytes_saved, sizeof(partial_count_bytes)) == 0;

        u32 word_first_form_sentinel = UINT32_C(0xabcdef01);
        u32 word_first_form_saved = word_first_form_sentinel;
        identical_count = UINT32_C(0x10203040);
        identical_count_saved = identical_count;
        scalar_count_pointer_alias_rejected = scalar_count_pointer_alias_rejected &&
                                              !buster_aarch64_arm_m1_scalar_integer_decode(
                                                  canonical_target, overlap_scalar_word, &word_first_form_sentinel, alias_operands,
                                                  BUSTER_ARRAY_LENGTH(alias_operands), &identical_count, &alias_modifier, 1,
                                                  &identical_count) &&
                                              word_first_form_sentinel == word_first_form_saved && identical_count == identical_count_saved &&
                                              memcmp(alias_operands, alias_operands_saved, sizeof(alias_operands)) == 0 &&
                                              memcmp(&alias_modifier, &alias_modifier_saved, sizeof(alias_modifier)) == 0;

        for (u32 byte_index = 0; byte_index < BUSTER_ARRAY_LENGTH(partial_count_bytes); byte_index += 1)
        {
            partial_count_bytes[byte_index] = (u8)(0x60 + byte_index);
        }
        memcpy(partial_count_bytes_saved, partial_count_bytes, sizeof(partial_count_bytes));
        partial_operand_count = (u32*)(void*)(partial_count_bytes + 0);
        partial_modifier_count = (u32*)(void*)(partial_count_bytes + 1);
        word_first_form_sentinel = UINT32_C(0x76543210);
        word_first_form_saved = word_first_form_sentinel;
        scalar_count_pointer_alias_rejected = scalar_count_pointer_alias_rejected &&
                                              !buster_aarch64_arm_m1_scalar_integer_decode(
                                                  canonical_target, overlap_scalar_word, &word_first_form_sentinel, alias_operands,
                                                  BUSTER_ARRAY_LENGTH(alias_operands), partial_operand_count, &alias_modifier, 1,
                                                  partial_modifier_count) &&
                                              word_first_form_sentinel == word_first_form_saved &&
                                              memcmp(alias_operands, alias_operands_saved, sizeof(alias_operands)) == 0 &&
                                              memcmp(&alias_modifier, &alias_modifier_saved, sizeof(alias_modifier)) == 0 &&
                                              memcmp(partial_count_bytes, partial_count_bytes_saved, sizeof(partial_count_bytes)) == 0;
    }
    BUSTER_TEST(arguments, scalar_count_pointer_alias_rejected);

    return result;
}

#endif
