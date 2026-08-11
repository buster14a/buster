#include <buster/tests/compiler/assembly/aarch64_control_semantics_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/assembly/aarch64_control_semantics.h>
#include <buster/lib/string.h>

BUSTER_GLOBAL_LOCAL BusterAarch64ControlOperandValue a64_control_test_reg(u8 width, u32 index, u8 register_class)
{
    return (BusterAarch64ControlOperandValue){
        .value = (s64)index,
        .kind = BUSTER_AARCH64_CONTROL_OPERAND_REGISTER,
        .width = width,
        .register31_role = register_class == BUSTER_AARCH64_CONTROL_REGISTER_CLASS_FP_SIMD
                               ? BUSTER_AARCH64_CONTROL_REGISTER31_NONE
                               : (index == 31 ? BUSTER_AARCH64_CONTROL_REGISTER31_ZR : BUSTER_AARCH64_CONTROL_REGISTER31_NONE),
        .register_class = register_class,
    };
}

BUSTER_GLOBAL_LOCAL BusterAarch64ControlOperandValue a64_control_test_pc(s64 displacement)
{
    return (BusterAarch64ControlOperandValue){.value = displacement, .kind = BUSTER_AARCH64_CONTROL_OPERAND_PC_RELATIVE, .width = 64};
}

BUSTER_GLOBAL_LOCAL BusterAarch64ControlOperandValue a64_control_test_imm(u8 kind, u8 width, s64 value)
{
    return (BusterAarch64ControlOperandValue){.value = value, .kind = kind, .width = width};
}

BUSTER_GLOBAL_LOCAL BusterAarch64ControlInstruction a64_control_test_fixture(u32 row_index)
{
    BusterAarch64ControlSemanticRecord row_storage = {0};
    if (!buster_aarch64_control_semantic_row(row_index, &row_storage)) return (BusterAarch64ControlInstruction){0};
    BusterAarch64ControlSemanticRecord const* row = &row_storage;
    BusterAarch64ControlInstruction instruction = {.row = (u16)row_index, .operand_count = row->operand_count};
    switch ((BusterAarch64ControlForm)row->form)
    {
    case BUSTER_AARCH64_CONTROL_FORM_ADRP:
    case BUSTER_AARCH64_CONTROL_FORM_ADR:
        instruction.operands[0] = a64_control_test_reg(64, 0, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        instruction.operands[1] = a64_control_test_pc(0);
        break;
    case BUSTER_AARCH64_CONTROL_FORM_B:
    case BUSTER_AARCH64_CONTROL_FORM_BL:
        instruction.operands[0] = a64_control_test_pc(0);
        break;
    case BUSTER_AARCH64_CONTROL_FORM_B_COND:
        instruction.operands[0] = a64_control_test_pc(0);
        instruction.operands[1] = a64_control_test_imm(BUSTER_AARCH64_CONTROL_OPERAND_CONDITION, 4, 0);
        break;
    case BUSTER_AARCH64_CONTROL_FORM_CBZ_W:
    case BUSTER_AARCH64_CONTROL_FORM_CBNZ_W:
    case BUSTER_AARCH64_CONTROL_FORM_CBZ_X:
    case BUSTER_AARCH64_CONTROL_FORM_CBNZ_X:
        instruction.operands[0] = a64_control_test_reg(row->form == BUSTER_AARCH64_CONTROL_FORM_CBZ_W ||
                                                                row->form == BUSTER_AARCH64_CONTROL_FORM_CBNZ_W
                                                            ? 32
                                                            : 64,
                                                        0, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        instruction.operands[1] = a64_control_test_pc(0);
        break;
    case BUSTER_AARCH64_CONTROL_FORM_TBZ:
    case BUSTER_AARCH64_CONTROL_FORM_TBNZ:
        instruction.operands[0] = a64_control_test_reg(32, 0, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        instruction.operands[1] = a64_control_test_imm(BUSTER_AARCH64_CONTROL_OPERAND_IMMEDIATE, 6, 0);
        instruction.operands[2] = a64_control_test_pc(0);
        break;
    case BUSTER_AARCH64_CONTROL_FORM_LDR_W:
    case BUSTER_AARCH64_CONTROL_FORM_LDR_S:
        instruction.operands[0] = a64_control_test_reg(32, 0,
                                                        row->form == BUSTER_AARCH64_CONTROL_FORM_LDR_S
                                                            ? BUSTER_AARCH64_CONTROL_REGISTER_CLASS_FP_SIMD
                                                            : BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        instruction.operands[1] = a64_control_test_pc(0);
        break;
    case BUSTER_AARCH64_CONTROL_FORM_LDR_X:
    case BUSTER_AARCH64_CONTROL_FORM_LDRSW_X:
    case BUSTER_AARCH64_CONTROL_FORM_LDR_D:
        instruction.operands[0] = a64_control_test_reg(64, 0,
                                                        row->form == BUSTER_AARCH64_CONTROL_FORM_LDR_D
                                                            ? BUSTER_AARCH64_CONTROL_REGISTER_CLASS_FP_SIMD
                                                            : BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        instruction.operands[1] = a64_control_test_pc(0);
        break;
    case BUSTER_AARCH64_CONTROL_FORM_LDR_Q:
        instruction.operands[0] = a64_control_test_reg(128, 0, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_FP_SIMD);
        instruction.operands[1] = a64_control_test_pc(0);
        break;
    case BUSTER_AARCH64_CONTROL_FORM_PRFM:
        instruction.operands[0] = a64_control_test_imm(BUSTER_AARCH64_CONTROL_OPERAND_IMMEDIATE, 5, 0);
        instruction.operands[1] = a64_control_test_pc(0);
        break;
    case BUSTER_AARCH64_CONTROL_FORM_RET:
        instruction.operand_count = 0;
        break;
    case BUSTER_AARCH64_CONTROL_FORM_CSEL_W:
    case BUSTER_AARCH64_CONTROL_FORM_CSINC_W:
    case BUSTER_AARCH64_CONTROL_FORM_CSINV_W:
    case BUSTER_AARCH64_CONTROL_FORM_CSNEG_W:
        instruction.operands[0] = a64_control_test_reg(32, 0, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        instruction.operands[1] = a64_control_test_reg(32, 1, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        instruction.operands[2] = a64_control_test_reg(32, 2, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        instruction.operands[3] = a64_control_test_imm(BUSTER_AARCH64_CONTROL_OPERAND_CONDITION, 4, 0);
        break;
    case BUSTER_AARCH64_CONTROL_FORM_CSEL_X:
    case BUSTER_AARCH64_CONTROL_FORM_CSINC_X:
    case BUSTER_AARCH64_CONTROL_FORM_CSINV_X:
    case BUSTER_AARCH64_CONTROL_FORM_CSNEG_X:
        instruction.operands[0] = a64_control_test_reg(64, 0, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        instruction.operands[1] = a64_control_test_reg(64, 1, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        instruction.operands[2] = a64_control_test_reg(64, 2, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        instruction.operands[3] = a64_control_test_imm(BUSTER_AARCH64_CONTROL_OPERAND_CONDITION, 4, 0);
        break;
    case BUSTER_AARCH64_CONTROL_FORM_COUNT:
        break;
    }
    return instruction;
}

UnitTestResult aarch64_control_semantics_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    BUSTER_TEST(arguments, buster_aarch64_control_semantic_count() == 27);
    BUSTER_TEST(arguments, buster_aarch64_control_semantic_validate());
    BUSTER_STRING_TEST(arguments, string_from_pointer((char8*)buster_aarch64_control_semantic_digest()),
                       S8("981200c3a2350be0f18eb8785929b30de5f468b5a8faa6419d96b7fa3b5e42a4"));
    BUSTER_STRING_TEST(arguments, string_from_pointer((char8*)BUSTER_AARCH64_CONTROL_RET_DIGEST),
                       S8("babf7e807a273b459d0fb4caa94e41b9779c9949ec2f7bf4eda1803aa7a734d6"));
    String8 literal_digest = S8("2a29a4d0e6b7d537dde6523e1859678169a4c93c973e4bd1d4f6e87fa6d6e25e");
    BUSTER_STRING_TEST(arguments, string_from_pointer((char8*)buster_aarch64_control_semantic_group_digest(BUSTER_AARCH64_CONTROL_OWNER_COMPLEX_LITERAL)), literal_digest);
    BUSTER_TEST(arguments, literal_digest.length == 64 &&
                               string_from_pointer((char8*)buster_aarch64_control_semantic_group_digest(BUSTER_AARCH64_CONTROL_OWNER_COMPLEX_LITERAL)).length ==
                                   literal_digest.length);

    BusterAarch64ControlSemanticRecord invalid_row = {.id = {.offset = UINT32_C(0x12345678), .length = UINT32_C(0xabcdef01)}};
    BUSTER_TEST(arguments, !buster_aarch64_control_semantic_row(UINT32_MAX, &invalid_row) &&
                           invalid_row.id.offset == UINT32_C(0x12345678) && invalid_row.id.length == UINT32_C(0xabcdef01));
    BUSTER_TEST(arguments, !buster_aarch64_control_semantic_row(0, 0));

    u32 general_count = 0;
    u32 complex_count = 0;
    for (u32 row_index = 0; row_index < buster_aarch64_control_semantic_count(); row_index += 1)
    {
        BusterAarch64ControlSemanticRecord row_storage = {0};
        BUSTER_TEST(arguments, buster_aarch64_control_semantic_row(row_index, &row_storage));
        BusterAarch64ControlSemanticRecord const* row = &row_storage;
        BUSTER_TEST(arguments, row->row_digest != 0 && row->fixed_value == (row->fixed_value & row->fixed_mask));
        if (row->owner == BUSTER_AARCH64_CONTROL_OWNER_GENERAL) general_count += 1;
        if (row->owner == BUSTER_AARCH64_CONTROL_OWNER_COMPLEX_LITERAL) complex_count += 1;
        u32 lookup = UINT32_MAX;
        String8 row_id = {0};
        BUSTER_TEST(arguments, buster_aarch64_control_semantic_string(row->id, &row_id) &&
                               buster_aarch64_control_semantic_lookup(row_id, &lookup) && lookup == row_index);

        BusterAarch64ControlInstruction fixture = a64_control_test_fixture(row_index);
        u32 word = UINT32_C(0xfeedface);
        BUSTER_TEST(arguments, buster_aarch64_control_semantic_encode(&fixture, &word) && word == row->oracle_word);
        BusterAarch64ControlInstruction decoded = {0};
        BUSTER_TEST(arguments, buster_aarch64_control_semantic_decode(word, &decoded) && decoded.row == row_index);
        u32 round_trip_word = 0;
        BUSTER_TEST(arguments, buster_aarch64_control_semantic_encode(&decoded, &round_trip_word) && round_trip_word == word);
        BusterAarch64ControlInstruction decode_unchanged = {.row = UINT16_MAX, .operand_count = UINT8_MAX};
        BUSTER_TEST(arguments, !buster_aarch64_control_semantic_decode(UINT32_MAX, &decode_unchanged) &&
                               decode_unchanged.row == UINT16_MAX && decode_unchanged.operand_count == UINT8_MAX);

        // Exercise every architectural register index for every register
        // operand.  Polymorphic TBZ/TBNZ operands cover both W and X widths;
        // FP/SIMD literal loads keep S31/D31/Q31 distinct from GPR X31.
        for (u32 operand_index = 0; operand_index < row->operand_count; operand_index += 1)
        {
            BusterAarch64ControlOperandSchema schema = row->operands[operand_index];
            if (schema.kind != BUSTER_AARCH64_CONTROL_OPERAND_REGISTER) continue;
            u8 width_values[2] = {schema.width ? schema.width : 32, schema.width ? schema.width : 64};
            u32 width_count = schema.width ? 1 : 2;
            for (u32 width_index = 0; width_index < width_count; width_index += 1)
            {
                for (u32 register_index = 0; register_index < 32; register_index += 1)
                {
                    BusterAarch64ControlInstruction trial = fixture;
                    trial.operand_count = row->operand_count;
                    trial.operands[operand_index] = a64_control_test_reg(width_values[width_index], register_index, schema.register_class);
                    if (!schema.width) trial.operands[1].value = width_values[width_index] == 64 ? 32 : 0;
                    u32 trial_word = 0;
                    BUSTER_TEST(arguments, buster_aarch64_control_semantic_encode(&trial, &trial_word));
                    BusterAarch64ControlInstruction trial_decoded = {0};
                    BUSTER_TEST(arguments, buster_aarch64_control_semantic_decode(trial_word, &trial_decoded) &&
                                           trial_decoded.row == row_index &&
                                           trial_decoded.operands[operand_index].value == register_index &&
                                           trial_decoded.operands[operand_index].width == width_values[width_index] &&
                                           trial_decoded.operands[operand_index].register_class == schema.register_class);
                }
            }
            BusterAarch64ControlInstruction wrong_class = fixture;
            wrong_class.operand_count = row->operand_count;
            wrong_class.operands[operand_index].register_class =
                schema.register_class == BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR
                    ? BUSTER_AARCH64_CONTROL_REGISTER_CLASS_FP_SIMD
                    : BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR;
            word = UINT32_C(0x13579bdf);
            BUSTER_TEST(arguments, !buster_aarch64_control_semantic_encode(&wrong_class, &word) && word == UINT32_C(0x13579bdf));
        }

        if (row->pc_relative.layout != BUSTER_AARCH64_CONTROL_PC_NONE)
        {
            u32 patched = UINT32_C(0xa5a5a5a5);
            BUSTER_TEST(arguments, buster_aarch64_control_semantic_patch(row_index, row->fixed_value, row->pc_relative.minimum, &patched));
            BUSTER_TEST(arguments, buster_aarch64_control_semantic_patch(row_index, row->fixed_value, row->pc_relative.maximum, &patched));
            u32 unchanged = UINT32_C(0xa5a5a5a5);
            BUSTER_TEST(arguments, !buster_aarch64_control_semantic_patch(row_index, row->fixed_value,
                                                                            row->pc_relative.minimum - row->pc_relative.alignment, &unchanged) &&
                                   unchanged == UINT32_C(0xa5a5a5a5));
            if (row->pc_relative.alignment > 1)
            {
                unchanged = UINT32_C(0xa5a5a5a5);
                BUSTER_TEST(arguments, !buster_aarch64_control_semantic_patch(row_index, row->fixed_value, 1, &unchanged) &&
                                       unchanged == UINT32_C(0xa5a5a5a5));
            }
        }
    }
    BUSTER_TEST(arguments, general_count == 24 && complex_count == 3);

    // Every architectural condition value is encodable for B.cond and CSEL;
    // inversion is intentionally a separate operation and excludes AL/NV.
    u32 b_cond = 0;
    BUSTER_TEST(arguments, buster_aarch64_control_semantic_lookup(S8("arm-a64@2026-06:B_only_condbranch"), &b_cond));
    for (u32 condition = 0; condition < 16; condition += 1)
    {
        BusterAarch64ControlInstruction instruction = a64_control_test_fixture(b_cond);
        instruction.operands[1].value = condition;
        u32 condition_word = 0;
        BUSTER_TEST(arguments, buster_aarch64_control_semantic_encode(&instruction, &condition_word));
    }

    u32 ret_row = 0;
    BUSTER_TEST(arguments, buster_aarch64_control_semantic_lookup(S8("arm-a64@2026-06:RET_64R_branch_reg"), &ret_row));
    BusterAarch64ControlInstruction ret_default = a64_control_test_fixture(ret_row);
    u32 ret_default_word = 0;
    BUSTER_TEST(arguments, buster_aarch64_control_semantic_encode(&ret_default, &ret_default_word) && ret_default_word == UINT32_C(0xd65f03c0));
    BusterAarch64ControlInstruction ret_explicit = ret_default;
    ret_explicit.operand_count = 1;
        ret_explicit.operands[0] = a64_control_test_reg(64, 0, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
    u32 ret_explicit_word = 0;
    BUSTER_TEST(arguments, buster_aarch64_control_semantic_encode(&ret_explicit, &ret_explicit_word) && ret_explicit_word == UINT32_C(0xd65f0000));

    // Conditions are a compact, inverse-paired table.  AL/NV are valid
    // encodings but intentionally have no inverse entry.
    BUSTER_TEST(arguments, buster_aarch64_control_condition_count() == 16);
    for (u32 condition_value = 0; condition_value < buster_aarch64_control_condition_count(); condition_value += 1)
    {
        BusterAarch64ControlCondition condition = {0};
        String8 condition_name = {0};
        BUSTER_TEST(arguments, buster_aarch64_control_condition((u8)condition_value, &condition) && condition.valid &&
                               condition.value == condition_value && buster_aarch64_control_semantic_string(condition.name, &condition_name) &&
                               condition_name.length == 2);
        if (condition.inverse_valid)
        {
            BusterAarch64ControlCondition inverse = {0};
            BUSTER_TEST(arguments, buster_aarch64_control_condition(condition.inverse, &inverse) && inverse.inverse_valid &&
                                   inverse.inverse == condition.value);
        }
        else
        {
            BUSTER_TEST(arguments, condition.inverse == UINT8_MAX);
        }
    }

    // Generated fixed-bit patterns must be pairwise disjoint.  Decode also
    // checks this dynamically and rejects an ambiguous table if one appears.
    u32 fixed_overlap_count = 0;
    for (u32 first = 0; first < buster_aarch64_control_semantic_count(); first += 1)
    {
        BusterAarch64ControlSemanticRecord first_storage = {0};
        BUSTER_TEST(arguments, buster_aarch64_control_semantic_row(first, &first_storage));
        BusterAarch64ControlSemanticRecord const* first_row = &first_storage;
        for (u32 second = 0; second < first; second += 1)
        {
            BusterAarch64ControlSemanticRecord second_storage = {0};
            BUSTER_TEST(arguments, buster_aarch64_control_semantic_row(second, &second_storage));
            BusterAarch64ControlSemanticRecord const* second_row = &second_storage;
            u32 shared_mask = first_row->fixed_mask & second_row->fixed_mask;
            if (((first_row->fixed_value ^ second_row->fixed_value) & shared_mask) == 0) fixed_overlap_count += 1;
        }
    }
    BUSTER_TEST(arguments, fixed_overlap_count == 0);

    // ADRP relocation applies a signed addend before page truncation.  These
    // edge cases cross page boundaries and exercise the signed-u64 overflow
    // guard as well as the architectural +/-4 GiB page range.
    u32 adrp_row = 0;
    BUSTER_TEST(arguments, buster_aarch64_control_semantic_lookup(S8("arm-a64@2026-06:ADRP_only_pcreladdr"), &adrp_row));
    BusterAarch64ControlFixupRequest adrp_request = {.symbol_defined = true};
    BusterAarch64ControlFixupResult adrp_result = {0};
    u32 adrp_word = 0;
    adrp_request.place_address = 0;
    adrp_request.target_address = UINT64_C(0x1fff);
    adrp_request.addend = 1;
    BUSTER_TEST(arguments, buster_aarch64_control_semantic_fixup(adrp_row, UINT32_C(0x90000000), adrp_request, &adrp_word, &adrp_result) &&
                           adrp_result.displacement == INT64_C(0x2000));
    adrp_request.target_address = UINT64_C(0x2000);
    adrp_request.addend = -1;
    BUSTER_TEST(arguments, buster_aarch64_control_semantic_fixup(adrp_row, UINT32_C(0x90000000), adrp_request, &adrp_word, &adrp_result) &&
                           adrp_result.displacement == INT64_C(0x1000));
    adrp_request.target_address = 0;
    adrp_request.place_address = UINT64_C(0x100000000);
    adrp_request.addend = 0;
    BUSTER_TEST(arguments, buster_aarch64_control_semantic_fixup(adrp_row, UINT32_C(0x90000000), adrp_request, &adrp_word, &adrp_result) &&
                           adrp_result.displacement == -INT64_C(0x100000000));
    adrp_request.target_address = UINT64_C(0xfffff000);
    adrp_request.place_address = 0;
    BUSTER_TEST(arguments, buster_aarch64_control_semantic_fixup(adrp_row, UINT32_C(0x90000000), adrp_request, &adrp_word, &adrp_result) &&
                           adrp_result.displacement == INT64_C(0xfffff000));
    adrp_request.target_address = UINT64_C(0xfffff000) + 4096;
    u32 adrp_unchanged = UINT32_C(0xdeadbeef);
    BusterAarch64ControlFixupResult adrp_failure = {.fixup_kind = UINT8_C(0xaa), .displacement = INT64_C(0x1234)};
    BUSTER_TEST(arguments, !buster_aarch64_control_semantic_fixup(adrp_row, UINT32_C(0x90000000), adrp_request, &adrp_unchanged,
                                                                    &adrp_failure) &&
                           adrp_unchanged == UINT32_C(0xdeadbeef) && adrp_failure.fixup_kind == UINT8_C(0xaa) &&
                           adrp_failure.displacement == INT64_C(0x1234));
    adrp_request.target_address = 0;
    adrp_request.place_address = UINT64_C(0x100001000);
    adrp_request.addend = 0;
    adrp_unchanged = UINT32_C(0xbad0bad0);
    BUSTER_TEST(arguments, !buster_aarch64_control_semantic_fixup(adrp_row, UINT32_C(0x90000000), adrp_request, &adrp_unchanged,
                                                                    &adrp_failure) && adrp_unchanged == UINT32_C(0xbad0bad0));
    adrp_request.place_address = UINT64_C(0x100000000);
    adrp_request.addend = -1;
    adrp_unchanged = UINT32_C(0xcafebabe);
    BUSTER_TEST(arguments, !buster_aarch64_control_semantic_fixup(adrp_row, UINT32_C(0x90000000), adrp_request, &adrp_unchanged,
                                                                    &adrp_failure) &&
                           adrp_unchanged == UINT32_C(0xcafebabe));
    adrp_request.target_address = UINT64_MAX;
    adrp_request.place_address = 0;
    adrp_request.addend = 1;
    adrp_unchanged = UINT32_C(0xa5a5a5a5);
    BUSTER_TEST(arguments, !buster_aarch64_control_semantic_fixup(adrp_row, UINT32_C(0x90000000), adrp_request, &adrp_unchanged,
                                                                    &adrp_failure) &&
                           adrp_unchanged == UINT32_C(0xa5a5a5a5));

    // Unresolved Darwin relocations are legal only for B/BL and preserve the
    // fixed word.  Conditional/literal/ADR rows remain local-only.
    Target darwin = {.cpu_arch = CPU_ARCH_AARCH64, .cpu_model = CPU_MODEL_A64_APPLE_M1, .os = OPERATING_SYSTEM_MACOS};
    BusterAarch64ControlFixupRequest external = {.target = darwin, .symbol_external = true};
    BusterAarch64ControlFixupResult fixup = {0};
    u32 external_word = 0;
    u32 b_row = 0;
    BUSTER_TEST(arguments, buster_aarch64_control_semantic_lookup(S8("arm-a64@2026-06:B_only_branch_imm"), &b_row) &&
                               buster_aarch64_control_semantic_fixup(b_row, UINT32_C(0x14000000), external, &external_word, &fixup) &&
                               external_word == UINT32_C(0x14000000) && fixup.external &&
                               fixup.relocation_kind == BUSTER_AARCH64_CONTROL_RELOCATION_KIND_BRANCH26);
    BUSTER_TEST(arguments, !buster_aarch64_control_semantic_fixup(b_cond, UINT32_C(0x54000000), external, &external_word, &fixup));

    return result;
}

#endif
