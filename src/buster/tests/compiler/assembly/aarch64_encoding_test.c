#include <buster/tests/compiler/assembly/aarch64_encoding_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/assembly/aarch64_encoding.h>

typedef struct A64EncodingCase A64EncodingCase;
struct A64EncodingCase
{
    A64MCInst instruction;
    u32 word;
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

    // The checked-in packed snapshot is part of the runtime ABI. Keep the
    // exact shape counts and M1 policy census here so a regenerated table
    // cannot silently change the denominator.
    BusterAarch64MetadataCounts metadata_counts = buster_aarch64_metadata_counts();
    BUSTER_TEST(arguments, buster_aarch64_metadata_schema_version() == 4);
    BUSTER_TEST(arguments, metadata_counts.form_count == 7491 && metadata_counts.field_count == 22631 && metadata_counts.segment_count == 23037 &&
                              metadata_counts.operand_count == 26262 && metadata_counts.predicate_count == 7854 && metadata_counts.string_pool_size == 337490);
    BUSTER_TEST(arguments, metadata_counts.apple_m1_supported_count == 2899 && metadata_counts.apple_m1_raw_layout_complete_count == 2873 &&
                              metadata_counts.apple_m1_raw_layout_incomplete_count == 26);

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
    BUSTER_TEST(arguments, raw_layout_complete_count == 7320 && m1_count == 2899 && m1_raw_layout_complete_count == 2873);
    BUSTER_TEST(arguments, found_in_profile_unsupported_token_raw_layout);

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

    // Bounded rejection coverage for null, wrong-count, overflow, fixed-bit,
    // unsupported-target, raw-layout-incomplete, and out-of-range metadata requests.
    BusterAarch64MetadataForm metadata_form = {0};
    BUSTER_TEST(arguments, !buster_aarch64_metadata_form(metadata_counts.form_count, &metadata_form));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_form(0, 0));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_field(0, UINT32_MAX, 0));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_segment(0, 0, UINT32_MAX, 0));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_operand(0, UINT32_MAX, 0));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_predicate(0, UINT32_MAX, 0));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_form_supported(0, BUSTER_AARCH64_METADATA_TARGET_COUNT));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_form_provisionally_apple_m1_supported(metadata_counts.form_count));
    BUSTER_TEST(arguments, !buster_aarch64_metadata_form_has_complete_raw_layout(3854));
    u32 rejection_decoded_values[4] = {0};
    BUSTER_TEST(arguments, !buster_aarch64_metadata_raw_encode(3854, 0, 2, &encoded));
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

    return result;
}

#endif
