#include <buster/tests/compiler/assembly/aarch64_encoding_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/assembly/aarch64_encoding.h>

typedef struct A64EncodingCase A64EncodingCase;
struct A64EncodingCase
{
    A64MCInst instruction;
    u32 word;
};

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
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        BUSTER_TEST(arguments, a64_encoding_round_trip(cases[index]));
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

    return result;
}

#endif
