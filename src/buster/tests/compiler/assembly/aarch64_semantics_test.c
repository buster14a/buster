#include <buster/tests/compiler/assembly/aarch64_semantics_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/assembly/aarch64_semantics.h>

static bool a64_semantic_string_is(BusterA64SemanticString actual, String8 expected)
{
    if (actual.length != expected.length) return false;
    for (u32 index = 0; index < actual.length; index += 1)
    {
        if (buster_a64_semantic_string_byte(actual, index) != expected.pointer[index]) return false;
    }
    return true;
}

static bool a64_semantic_string_contains(BusterA64SemanticString actual, String8 needle)
{
    if (needle.length > actual.length) return false;
    for (u32 start = 0; start + needle.length <= actual.length; start += 1)
    {
        bool equal = true;
        for (u32 index = 0; index < needle.length; index += 1)
            if (buster_a64_semantic_string_byte(actual, start + index) != needle.pointer[index]) equal = false;
        if (equal) return true;
    }
    return false;
}

static bool a64_semantic_value_text_is(u32 atom_id, String8 expected)
{
    BusterA64SemanticValueAtom atom;
    return buster_a64_semantic_value_atom(atom_id, &atom) && a64_semantic_string_is(atom.text, expected);
}

static bool a64_semantic_atom_equal(u32 left_id, u32 right_id)
{
    BusterA64SemanticValueAtom left = {0};
    BusterA64SemanticValueAtom right = {0};
    if (!buster_a64_semantic_value_atom(left_id, &left) || !buster_a64_semantic_value_atom(right_id, &right) ||
        left.kind != right.kind || left.integer != right.integer || left.text.length != right.text.length) return false;
    for (u32 index = 0; index < left.text.length; index += 1)
    {
        if (buster_a64_semantic_string_byte(left.text, index) != buster_a64_semantic_string_byte(right.text, index)) return false;
    }
    return true;
}

static bool a64_semantic_value_equal(BusterA64SemanticValue left, BusterA64SemanticValue right)
{
    if (left.key_count != right.key_count || left.result_count != right.result_count) return false;
    for (u32 index = 0; index < left.key_count; index += 1)
    {
        if (!a64_semantic_atom_equal(left.key_first + index, right.key_first + index)) return false;
    }
    for (u32 index = 0; index < left.result_count; index += 1)
    {
        if (!a64_semantic_atom_equal(left.result_first + index, right.result_first + index)) return false;
    }
    return true;
}

UnitTestResult aarch64_semantics_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    BUSTER_TEST(arguments, buster_a64_semantic_validate());
    BUSTER_TEST(arguments, buster_a64_semantic_schema_version() == 2);
    BUSTER_TEST(arguments, buster_a64_semantic_form_count() == 1695);
    BUSTER_TEST(arguments, buster_a64_semantic_parsed_program_count() == 372 && buster_a64_semantic_value_program_count() == 296);
    BUSTER_TEST(arguments, buster_a64_semantic_program_instruction_count() == 875 && buster_a64_semantic_program_operand_count() == 602);
    BUSTER_TEST(arguments, buster_a64_semantic_operand_field_index_count() >= buster_a64_semantic_operand_count());

    /* Every typed transform/value span must be bounded and independently
       readable; this guards the generated first/count contracts as well as
       the aggregate census constants above. */
    u32 transform_program_spans = 0;
    u32 value_program_spans = 0;
    bool typed_spans_valid = true;
    for (u32 transform_id = 0; transform_id < buster_a64_semantic_transform_count(); transform_id += 1)
    {
        BusterA64SemanticTransform transform = {0};
        if (!buster_a64_semantic_transform(transform_id, &transform))
        {
            typed_spans_valid = false;
            continue;
        }
        if (transform.program_count != 0)
        {
            transform_program_spans += 1;
            for (u32 ordinal = 0; ordinal < transform.program_count; ordinal += 1)
            {
                BusterA64SemanticProgramInstruction instruction = {0};
                typed_spans_valid = typed_spans_valid && buster_a64_semantic_transform_program_instruction(transform_id, ordinal, &instruction);
            }
        }
    }
    for (u32 atom_id = 0; atom_id < buster_a64_semantic_value_atom_count(); atom_id += 1)
    {
        BusterA64SemanticValueAtom atom = {0};
        if (!buster_a64_semantic_value_atom(atom_id, &atom))
        {
            typed_spans_valid = false;
            continue;
        }
        if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_PROGRAM)
        {
            value_program_spans += 1;
            for (u32 ordinal = 0; ordinal < atom.program_count; ordinal += 1)
            {
                BusterA64SemanticProgramInstruction instruction = {0};
                typed_spans_valid = typed_spans_valid && buster_a64_semantic_value_atom_program_instruction(atom_id, ordinal, &instruction);
            }
        }
    }
    BUSTER_TEST(arguments, typed_spans_valid && transform_program_spans == 372 && value_program_spans == 296);

    /* ABS's arrangement table is the binding canary: both 8B and 16B must
       survive normalization, and a checked value accessor must expose the
       same key/result shape for every direction. */
    u32 form_id = UINT32_MAX;
    BUSTER_TEST(arguments, buster_a64_semantic_find_form(S8("arm-a64@2026-06:ABS_asimdmisc_R"), 0, &form_id));
    BusterA64SemanticForm form = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_form(form_id, &form) && form.transform_count >= 2);
    BusterA64SemanticTransform table = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_transform(form.transform_first + 1, &table) && table.kind == BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE && table.value_count == 8);
    if (table.value_count == 8)
    {
        BusterA64SemanticValue first = {0};
        BusterA64SemanticValue second = {0};
        BUSTER_TEST(arguments, buster_a64_semantic_transform_value(table.id, 0, &first) && first.key_count == 2 && first.result_count == 1);
        BUSTER_TEST(arguments, buster_a64_semantic_transform_value(table.id, 1, &second) && second.key_count == 2 && second.result_count == 1);
        BusterA64SemanticValueAtom first_result = {0};
        BusterA64SemanticValueAtom second_result = {0};
        BUSTER_TEST(arguments, buster_a64_semantic_value_atom(first.result_first, &first_result) && first_result.kind == BUSTER_A64_SEMANTIC_VALUE_ENUM);
        BUSTER_TEST(arguments, buster_a64_semantic_value_atom(second.result_first, &second_result) && second_result.kind == BUSTER_A64_SEMANTIC_VALUE_ENUM);
        BUSTER_TEST(arguments, a64_semantic_value_text_is(first.result_first, S8("8B")));
        BUSTER_TEST(arguments, a64_semantic_value_text_is(second.result_first, S8("16B")));

        /* Decode -> encode canary: each unique result points back to exactly
           one key in the invertible ABS table. */
        for (u32 value_index = 0; value_index < table.value_count; value_index += 1)
        {
            BusterA64SemanticValue value = {0};
            BUSTER_TEST(arguments, buster_a64_semantic_transform_value(table.id, value_index, &value) && value.result_count == 1);
            for (u32 inverse_index = 0; inverse_index < table.value_count; inverse_index += 1)
            {
                BusterA64SemanticValue inverse = {0};
                BUSTER_TEST(arguments, buster_a64_semantic_transform_value(table.id, inverse_index, &inverse));
                if (a64_semantic_atom_equal(value.result_first, inverse.result_first))
                {
                    BUSTER_TEST(arguments, a64_semantic_value_equal(value, inverse));
                }
            }
        }
    }

    /* Value-table headers remain pointer-free and are tied to each table
       transform and its bounded key/result entry span. */
    BusterA64SemanticTableHeader table_header = {0};
    u32 table_header_id = UINT32_MAX;
    BUSTER_TEST(arguments, buster_a64_semantic_transform_table_header(table.id, &table_header_id) &&
                           buster_a64_semantic_table_header(table_header_id, &table_header) && table_header.key_header_count == 2);
    BusterA64SemanticString header_text = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_table_key_header(table_header_id, 0, &header_text) && a64_semantic_string_is(header_text, S8("size")));
    BUSTER_TEST(arguments, buster_a64_semantic_table_key_header(table_header_id, 1, &header_text) && a64_semantic_string_is(header_text, S8("Q")));
    BUSTER_TEST(arguments, buster_a64_semantic_table_result_header(table_header_id, &header_text) && a64_semantic_string_is(header_text, S8("<T>")));
    BUSTER_TEST(arguments, buster_a64_semantic_table_header_count() == 1528 && buster_a64_semantic_table_key_header_count() >= 2);

    /* Numeric relations are normalized programs, including signed PC
       displacement interpretation and affine fixed-point arithmetic. */
    u32 bl_id = UINT32_MAX;
    BUSTER_TEST(arguments, buster_a64_semantic_find_form(S8("arm-a64@2026-06:BL_only_branch_imm"), 0, &bl_id));
    BusterA64SemanticForm bl_form = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_form(bl_id, &bl_form) && bl_form.transform_count == 1);
    BusterA64SemanticTransform bl_transform = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_transform(bl_form.transform_first, &bl_transform) &&
                           a64_semantic_string_is(bl_transform.expression, S8("[{\"name\":\"imm26\",\"op\":\"field\"},{\"bits\":26,\"op\":\"sign_extend\"},{\"op\":\"scale_mul\",\"value\":4}]")));
    BusterA64SemanticProgramInstruction bl_program = {0};
    BUSTER_TEST(arguments, bl_transform.program_count == 3 &&
                           buster_a64_semantic_transform_program_instruction(bl_transform.id, 0, &bl_program) &&
                           bl_program.op == BUSTER_A64_SEMANTIC_PROGRAM_FIELD && a64_semantic_string_is(bl_program.field, S8("imm26")) &&
                           bl_program.width == 0 && bl_program.high == UINT16_MAX && bl_program.low == UINT16_MAX);
    BUSTER_TEST(arguments, buster_a64_semantic_transform_program_instruction(bl_transform.id, 1, &bl_program) &&
                           bl_program.op == BUSTER_A64_SEMANTIC_PROGRAM_SIGN_EXTEND && bl_program.width == 26);
    BUSTER_TEST(arguments, buster_a64_semantic_transform_program_instruction(bl_transform.id, 2, &bl_program) &&
                           bl_program.op == BUSTER_A64_SEMANTIC_PROGRAM_SCALE_MUL && bl_program.value == 4);
    u32 fcvt_id = UINT32_MAX;
    BUSTER_TEST(arguments, buster_a64_semantic_find_form(S8("arm-a64@2026-06:FCVTZS_32D_float2fix"), 0, &fcvt_id));
    BusterA64SemanticForm fcvt_form = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_form(fcvt_id, &fcvt_form) && fcvt_form.transform_count == 1);
    BusterA64SemanticTransform fcvt_transform = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_transform(fcvt_form.transform_first, &fcvt_transform) &&
                           a64_semantic_string_is(fcvt_transform.expression, S8("[{\"name\":\"scale\",\"op\":\"field\"},{\"op\":\"sub_from_const\",\"value\":64}]")));
    BUSTER_TEST(arguments, fcvt_transform.program_count == 2 &&
                           buster_a64_semantic_transform_program_instruction(fcvt_transform.id, 0, &bl_program) &&
                           bl_program.op == BUSTER_A64_SEMANTIC_PROGRAM_FIELD && a64_semantic_string_is(bl_program.field, S8("scale")));
    BUSTER_TEST(arguments, buster_a64_semantic_transform_program_instruction(fcvt_transform.id, 1, &bl_program) &&
                           bl_program.op == BUSTER_A64_SEMANTIC_PROGRAM_SUB_FROM_CONST && bl_program.value == 64);
    struct { String8 name; u32 bits; } branch_relations[] = {
        {S8("BL_only_branch_imm"), 26}, {S8("B_only_branch_imm"), 26}, {S8("B_only_condbranch"), 19},
        {S8("CBNZ_32_compbranch"), 19}, {S8("CBNZ_64_compbranch"), 19}, {S8("CBZ_32_compbranch"), 19}, {S8("CBZ_64_compbranch"), 19},
        {S8("LDRSW_64_loadlit"), 19}, {S8("LDR_32_loadlit"), 19}, {S8("LDR_64_loadlit"), 19}, {S8("LDR_D_loadlit"), 19},
        {S8("LDR_Q_loadlit"), 19}, {S8("LDR_S_loadlit"), 19}, {S8("PRFM_P_loadlit"), 19}, {S8("TBNZ_only_testbranch"), 14}, {S8("TBZ_only_testbranch"), 14},
    };
    for (u32 relation_index = 0; relation_index < sizeof(branch_relations) / sizeof(branch_relations[0]); relation_index += 1)
    {
        char8 full_name[96] = {0};
        const char8 prefix[] = "arm-a64@2026-06:";
        u32 prefix_length = (u32)(sizeof(prefix) - 1);
        memcpy(full_name, prefix, prefix_length);
        memcpy(full_name + prefix_length, branch_relations[relation_index].name.pointer, branch_relations[relation_index].name.length);
        u32 relation_form_id = UINT32_MAX;
        BUSTER_TEST(arguments, buster_a64_semantic_find_form((String8){.pointer = full_name, .length = prefix_length + branch_relations[relation_index].name.length}, 0, &relation_form_id));
        BusterA64SemanticForm relation_form = {0};
        String8 bits_text = branch_relations[relation_index].bits == 26 ? S8("\"bits\":26,\"op\":\"sign_extend\"") :
                            branch_relations[relation_index].bits == 19 ? S8("\"bits\":19,\"op\":\"sign_extend\"") : S8("\"bits\":14,\"op\":\"sign_extend\"");
        bool found_sign_extend = false;
        bool relation_form_ok = buster_a64_semantic_form(relation_form_id, &relation_form);
        if (relation_form_ok)
        {
            for (u32 transform_index = 0; transform_index < relation_form.transform_count; transform_index += 1)
            {
                BusterA64SemanticTransform relation_transform = {0};
                if (buster_a64_semantic_transform(relation_form.transform_first + transform_index, &relation_transform) &&
                    a64_semantic_string_contains(relation_transform.expression, bits_text)) found_sign_extend = true;
            }
        }
        BUSTER_TEST(arguments, relation_form_ok && found_sign_extend);
    }
    u32 shl_id = UINT32_MAX;
    BUSTER_TEST(arguments, buster_a64_semantic_find_form(S8("arm-a64@2026-06:SHL_asisdshf_R"), 0, &shl_id));
    BusterA64SemanticForm shl_form = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_form(shl_id, &shl_form));
    BusterA64SemanticTransform shl_transform = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_transform(shl_form.transform_first + 1, &shl_transform) &&
                           a64_semantic_string_is(shl_transform.expression, S8("[{\"op\":\"uint_concat\",\"parts\":[{\"name\":\"immh\",\"op\":\"field\"},{\"name\":\"immb\",\"op\":\"field\"}]},{\"op\":\"add_const\",\"value\":-64}]")));
    BusterA64SemanticProgramInstruction shl_program = {0};
    BUSTER_TEST(arguments, shl_transform.program_count == 2 &&
                           buster_a64_semantic_transform_program_instruction(shl_transform.id, 0, &shl_program) &&
                           shl_program.op == BUSTER_A64_SEMANTIC_PROGRAM_UINT_CONCAT && shl_program.operand_count == 2);
    BusterA64SemanticProgramOperand shl_operand = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_program_operand(shl_program.id, 0, &shl_operand) &&
                           shl_operand.kind == BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_FIELD && a64_semantic_string_is(shl_operand.field, S8("immh")));
    BUSTER_TEST(arguments, buster_a64_semantic_program_operand(shl_program.id, 1, &shl_operand) &&
                           shl_operand.kind == BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_FIELD && a64_semantic_string_is(shl_operand.field, S8("immb")));
    BUSTER_TEST(arguments, buster_a64_semantic_transform_program_instruction(shl_transform.id, 1, &shl_program) &&
                           shl_program.op == BUSTER_A64_SEMANTIC_PROGRAM_ADD_CONST && shl_program.value == -64);

    /* The non-affine normalized operations are typed too. */
    u32 register_form_id = UINT32_MAX;
    BUSTER_TEST(arguments, buster_a64_semantic_find_form(S8("arm-a64@2026-06:LD1_asisdlse_R2_2v"), 0, &register_form_id));
    BusterA64SemanticForm register_form = {0};
    bool found_register_add_mod = false;
    if (buster_a64_semantic_form(register_form_id, &register_form))
    {
        for (u32 transform_index = 0; transform_index < register_form.transform_count; transform_index += 1)
        {
            BusterA64SemanticTransform candidate = {0};
            if (!buster_a64_semantic_transform(register_form.transform_first + transform_index, &candidate)) continue;
            for (u32 program_index = 0; program_index < candidate.program_count; program_index += 1)
            {
                BusterA64SemanticProgramInstruction instruction = {0};
                if (buster_a64_semantic_transform_program_instruction(candidate.id, program_index, &instruction) &&
                    instruction.op == BUSTER_A64_SEMANTIC_PROGRAM_REGISTER_ADD_MOD && instruction.value == 1 && instruction.modulus == 32 &&
                    a64_semantic_string_is(instruction.field, S8("Rt"))) found_register_add_mod = true;
            }
        }
    }
    BUSTER_TEST(arguments, found_register_add_mod);

    u32 scale_form_id = UINT32_MAX;
    BUSTER_TEST(arguments, buster_a64_semantic_find_form(S8("arm-a64@2026-06:LDNP_32_ldstnapair_offs"), 0, &scale_form_id));
    BusterA64SemanticForm scale_form = {0};
    bool found_scale_div = false;
    if (buster_a64_semantic_form(scale_form_id, &scale_form))
    {
        for (u32 transform_index = 0; transform_index < scale_form.transform_count; transform_index += 1)
        {
            BusterA64SemanticTransform candidate = {0};
            if (!buster_a64_semantic_transform(scale_form.transform_first + transform_index, &candidate)) continue;
            for (u32 program_index = 0; program_index < candidate.program_count; program_index += 1)
            {
                BusterA64SemanticProgramInstruction instruction = {0};
                if (buster_a64_semantic_transform_program_instruction(candidate.id, program_index, &instruction) &&
                    instruction.op == BUSTER_A64_SEMANTIC_PROGRAM_SCALE_DIV && instruction.value == 4) found_scale_div = true;
            }
        }
    }
    BUSTER_TEST(arguments, found_scale_div);

    u32 shared_form_id = UINT32_MAX;
    BUSTER_TEST(arguments, buster_a64_semantic_find_form(S8("arm-a64@2026-06:FMAXNMV_asimdall_only_H"), 0, &shared_form_id));
    BusterA64SemanticForm shared_form = {0};
    bool found_shared_decode = false;
    if (buster_a64_semantic_form(shared_form_id, &shared_form))
    {
        for (u32 transform_index = 0; transform_index < shared_form.transform_count; transform_index += 1)
        {
            BusterA64SemanticTransform candidate = {0};
            if (!buster_a64_semantic_transform(shared_form.transform_first + transform_index, &candidate)) continue;
            for (u32 program_index = 0; program_index < candidate.program_count; program_index += 1)
            {
                BusterA64SemanticProgramInstruction instruction = {0};
                if (!buster_a64_semantic_transform_program_instruction(candidate.id, program_index, &instruction) ||
                    instruction.op != BUSTER_A64_SEMANTIC_PROGRAM_SHARED_DECODE) continue;
                bool saw_field = false;
                bool saw_arrangement = false;
                for (u32 operand_index = 0; operand_index < instruction.operand_count; operand_index += 1)
                {
                    BusterA64SemanticProgramOperand operand = {0};
                    if (!buster_a64_semantic_program_operand(instruction.id, operand_index, &operand)) continue;
                    saw_field = saw_field || operand.kind == BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_FIELD;
                    saw_arrangement = saw_arrangement || operand.kind == BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_ARRANGEMENT;
                }
                found_shared_decode = saw_field && saw_arrangement;
            }
        }
    }
    BUSTER_TEST(arguments, found_shared_decode);

    /* PROGRAM value atoms carry the same typed span rather than only JSON text. */
    u32 program_atom_form_id = UINT32_MAX;
    BUSTER_TEST(arguments, buster_a64_semantic_find_form(S8("arm-a64@2026-06:DUP_asimdins_DV_v"), 0, &program_atom_form_id));
    BusterA64SemanticForm program_atom_form = {0};
    bool found_program_atom = false;
    if (buster_a64_semantic_form(program_atom_form_id, &program_atom_form))
    {
        for (u32 transform_index = 0; transform_index < program_atom_form.transform_count; transform_index += 1)
        {
            BusterA64SemanticTransform candidate = {0};
            if (!buster_a64_semantic_transform(program_atom_form.transform_first + transform_index, &candidate) || candidate.kind != BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE) continue;
            for (u32 value_index = 0; value_index < candidate.value_count; value_index += 1)
            {
                BusterA64SemanticValue value = {0};
                if (!buster_a64_semantic_transform_value(candidate.id, value_index, &value)) continue;
                for (u32 atom_index = 0; atom_index < value.key_count + value.result_count; atom_index += 1)
                {
                    u32 atom_id = atom_index < value.key_count ? value.key_first + atom_index : value.result_first + atom_index - value.key_count;
                    BusterA64SemanticValueAtom atom = {0};
                    if (buster_a64_semantic_value_atom(atom_id, &atom) && atom.kind == BUSTER_A64_SEMANTIC_VALUE_PROGRAM && atom.program_count > 0)
                    {
                        BusterA64SemanticProgramInstruction instruction = {0};
                        found_program_atom = buster_a64_semantic_value_atom_program_instruction(atom.id, 0, &instruction) && instruction.op == BUSTER_A64_SEMANTIC_PROGRAM_UINT_CONCAT;
                    }
                    if (found_program_atom) break;
                }
                if (found_program_atom) break;
            }
            if (found_program_atom) break;
        }
    }
    BUSTER_TEST(arguments, found_program_atom);

    /* Alias target, condition, preference, and feature/constraint metadata
       are all exposed through checked spans. */
    const char8* alias_names[] = {"ASR_ASRV_32_dp_2src", "TST_ANDS_32S_log_imm", "CSET_CSINC_32_condsel", "AT_SYS_CR_systeminstrs", "MOV_ORR_32_log_imm"};
    const char8* alias_targets[] = {"ASRV", "ANDS_log_imm", "CSINC", "SYS", "ORR_log_imm"};
    for (u32 alias_index = 0; alias_index < sizeof(alias_names) / sizeof(alias_names[0]); alias_index += 1)
    {
        char8 full_name[96] = {0};
        const char8 prefix[] = "arm-a64@2026-06:";
        u32 prefix_length = (u32)(sizeof(prefix) - 1);
        u32 name_length = (u32)strlen(alias_names[alias_index]);
        memcpy(full_name, prefix, prefix_length);
        memcpy(full_name + prefix_length, alias_names[alias_index], name_length);
        u32 alias_form_id = UINT32_MAX;
        BUSTER_TEST(arguments, buster_a64_semantic_find_form((String8){.pointer = full_name, .length = prefix_length + name_length}, 0, &alias_form_id));
        BusterA64SemanticAlias alias = {0};
        BUSTER_TEST(arguments, buster_a64_semantic_alias(alias_form_id, &alias) && a64_semantic_string_is(alias.target_id, (String8){.pointer = (char8*)alias_targets[alias_index], .length = (u64)strlen(alias_targets[alias_index])}) && alias.condition_count > 0 && alias.preference_condition_count > 0);
        BusterA64SemanticString token = {0};
        BUSTER_TEST(arguments, buster_a64_semantic_alias_condition_token(alias_form_id, 0, &token));
        BUSTER_TEST(arguments, buster_a64_semantic_alias_preference_condition_token(alias_form_id, 0, &token));
    }
    BusterA64SemanticAlias first_alias = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_alias_by_ordinal(0, &first_alias) && first_alias.form_id < buster_a64_semantic_form_count());
    BusterA64SemanticConstraint constraint = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_constraint(fcvt_id, &constraint) && constraint.feature_count == 1 && constraint.program_count > 0);
    BUSTER_TEST(arguments, buster_a64_semantic_constraint_feature_tag(fcvt_id, 0, &header_text) && a64_semantic_string_is(header_text, S8("FEAT_FP")));
    BUSTER_TEST(arguments, buster_a64_semantic_constraint_program_token(fcvt_id, 0, &header_text) && a64_semantic_string_is(header_text, S8("sf")));
    u32 preference_form_id = UINT32_MAX;
    BUSTER_TEST(arguments, buster_a64_semantic_find_form(S8("arm-a64@2026-06:ADDS_32S_addsub_ext"), 0, &preference_form_id));
    BusterA64SemanticAlias preference_descriptor = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_alias_descriptor(preference_form_id, &preference_descriptor) && preference_descriptor.preference_condition_count == 0 && preference_descriptor.preference_count == 1);
    BusterA64SemanticAliasPreference preference = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_alias_preference(preference_form_id, 0, &preference) && preference.condition_count == 3);
    BUSTER_TEST(arguments, buster_a64_semantic_alias_preference_condition_token_by_id(preference.id, 0, &header_text) && a64_semantic_string_is(header_text, S8("Rd")));

    /* Keep non-injective value tables explicit: a result collision is valid
       for presentation metadata, but it must not be presented as invertible. */
    bool saw_non_injective_table = false;
    for (u32 transform_id = 0; transform_id < buster_a64_semantic_transform_count() && !saw_non_injective_table; transform_id += 1)
    {
        BusterA64SemanticTransform transform = {0};
        if (!buster_a64_semantic_transform(transform_id, &transform) || transform.kind != BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE || transform.invertible) continue;
        for (u32 left_index = 0; left_index < transform.value_count && !saw_non_injective_table; left_index += 1)
        {
            BusterA64SemanticValue left = {0};
            if (!buster_a64_semantic_transform_value(transform_id, left_index, &left)) continue;
            for (u32 right_index = left_index + 1; right_index < transform.value_count; right_index += 1)
            {
                BusterA64SemanticValue right = {0};
                if (buster_a64_semantic_transform_value(transform_id, right_index, &right) &&
                    a64_semantic_atom_equal(left.result_first, right.result_first))
                {
                    saw_non_injective_table = true;
                    break;
                }
            }
        }
    }
    BUSTER_TEST(arguments, saw_non_injective_table);

    /* Classification is deliberately presentation-only.  In particular the
       literal ``simm`` XML link is an integer memory offset, not a SIMD
       operand, and a store row must not claim a write direction without
       instruction-level data-flow semantics. */
    u32 store_id = UINT32_MAX;
    BUSTER_TEST(arguments, buster_a64_semantic_find_form(S8("arm-a64@2026-06:STR_32_ldst_immpost"), 0, &store_id));
    BusterA64SemanticForm store = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_form(store_id, &store));
    bool saw_simm = false;
    u32 simd_kind_mask = (UINT32_C(1) << BUSTER_A64_SEMANTIC_OPERAND_SIMD_REGISTER) |
                         (UINT32_C(1) << BUSTER_A64_SEMANTIC_OPERAND_SIMD_ARRANGEMENT) |
                         (UINT32_C(1) << BUSTER_A64_SEMANTIC_OPERAND_SIMD_WIDTH_SELECTOR) |
                         (UINT32_C(1) << BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST) |
                         (UINT32_C(1) << BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE) |
                         (UINT32_C(1) << BUSTER_A64_SEMANTIC_OPERAND_SIMD_PREFIX_SELECTOR);
    for (u32 operand_index = 0; operand_index < store.operand_count; operand_index += 1)
    {
        BusterA64SemanticOperand operand = {0};
        BUSTER_TEST(arguments, buster_a64_semantic_operand(store.operand_first + operand_index, &operand));
        BUSTER_TEST(arguments, operand.classification_status == BUSTER_A64_SEMANTIC_CLASSIFICATION_PRESENTATION_ONLY);
        BUSTER_TEST(arguments, a64_semantic_string_is(operand.direction, S8("unknown")));
        if (a64_semantic_string_is(operand.link, S8("simm__3")))
        {
            saw_simm = true;
            BUSTER_TEST(arguments, operand.kind == BUSTER_A64_SEMANTIC_OPERAND_INTEGER_IMMEDIATE);
            BUSTER_TEST(arguments, (operand.kind_mask & (UINT32_C(1) << BUSTER_A64_SEMANTIC_OPERAND_INTEGER_IMMEDIATE)) != 0);
            BUSTER_TEST(arguments, (operand.kind_mask & simd_kind_mask) == 0);
        }
    }
    BUSTER_TEST(arguments, saw_simm);

    /* Checked accessors reject bad IDs/ranges without clobbering caller
       storage. */
    BusterA64SemanticForm invalid_form = {.id = 0x12345678u, .name = {1, 2}, .mnemonic = {3, 4}, .assembly = {5, 6}, .source_digest = 7,
                                          .fixed_mask = 8, .fixed_value = 9, .field_first = 10, .operand_first = 11, .transform_first = 12,
                                          .field_count = 13, .operand_count = 14, .transform_count = 15, .owner = 16, .kind = 17,
                                          .raw_layout_resolved = 18, .status = 19};
    BusterA64SemanticForm invalid_form_before = invalid_form;
    BUSTER_TEST(arguments, !buster_a64_semantic_form(UINT32_MAX, &invalid_form) && memcmp(&invalid_form, &invalid_form_before, sizeof(invalid_form)) == 0);
    BusterA64SemanticTransform invalid_transform = {.id = 0x22334455u, .expression = {7, 8}, .source = 9, .p0 = 10, .p1 = 11, .table_id = 12,
                                                    .part_first = 12, .value_first = 13, .part_count = 14, .value_count = 15, .kind = 16,
                                                    .invertible = true, .reserved = 17};
    BusterA64SemanticTransform invalid_transform_before = invalid_transform;
    BUSTER_TEST(arguments, !buster_a64_semantic_transform(UINT32_MAX, &invalid_transform) && memcmp(&invalid_transform, &invalid_transform_before, sizeof(invalid_transform)) == 0);
    BusterA64SemanticProgramInstruction invalid_program = {.id = 0x31415926u, .field = {1, 2}, .text = {3, 4}, .operand_first = 5,
                                                           .operand_count = 6, .value = 7, .high = 8, .low = 9, .width = 10, .modulus = 11, .op = 12};
    BusterA64SemanticProgramInstruction invalid_program_before = invalid_program;
    BUSTER_TEST(arguments, !buster_a64_semantic_program_instruction(UINT32_MAX, &invalid_program) && memcmp(&invalid_program, &invalid_program_before, sizeof(invalid_program)) == 0);
    BusterA64SemanticProgramOperand invalid_program_operand = {.id = 0x27182818u, .field = {1, 2}, .text = {3, 4}, .value = 5,
                                                               .high = 6, .low = 7, .width = 8, .kind = 9};
    BusterA64SemanticProgramOperand invalid_program_operand_before = invalid_program_operand;
    BUSTER_TEST(arguments, !buster_a64_semantic_program_operand(UINT32_MAX, 0, &invalid_program_operand) && memcmp(&invalid_program_operand, &invalid_program_operand_before, sizeof(invalid_program_operand)) == 0);
    BusterA64SemanticValue invalid_value = {.id = 0x12345678u, .key_first = 1, .result_first = 2, .key_count = 3, .result_count = 4};
    BusterA64SemanticValue invalid_value_before = invalid_value;
    BUSTER_TEST(arguments, !buster_a64_semantic_transform_value(UINT32_MAX, 0, &invalid_value) && memcmp(&invalid_value, &invalid_value_before, sizeof(invalid_value)) == 0);
    BusterA64SemanticProgramInstruction invalid_value_program = invalid_program;
    BusterA64SemanticProgramInstruction invalid_value_program_before = invalid_value_program;
    BUSTER_TEST(arguments, !buster_a64_semantic_value_atom_program_instruction(UINT32_MAX, 0, &invalid_value_program) && memcmp(&invalid_value_program, &invalid_value_program_before, sizeof(invalid_value_program)) == 0);
    u32 invalid_field_id = 0xabcdef01u;
    BUSTER_TEST(arguments, !buster_a64_semantic_operand_field_index(UINT32_MAX, 0, &invalid_field_id) && invalid_field_id == 0xabcdef01u);
    BusterA64SemanticValueAtom invalid_atom = {.id = 1, .kind = 2, .integer = 3, .text = {4, 5}};
    BusterA64SemanticValueAtom invalid_atom_before = invalid_atom;
    BUSTER_TEST(arguments, !buster_a64_semantic_value_atom(UINT32_MAX, &invalid_atom) && memcmp(&invalid_atom, &invalid_atom_before, sizeof(invalid_atom)) == 0);
    BusterA64SemanticTableHeader invalid_table_header = {.id = 1, .key_header_first = 2, .key_header_count = 3, .result_header = {4, 5}};
    BusterA64SemanticTableHeader invalid_table_header_before = invalid_table_header;
    BUSTER_TEST(arguments, !buster_a64_semantic_table_header(UINT32_MAX, &invalid_table_header) && memcmp(&invalid_table_header, &invalid_table_header_before, sizeof(invalid_table_header)) == 0);
    BusterA64SemanticAlias invalid_alias = {.form_id = 1, .target_file = {2, 3}, .target_id = {4, 5}, .target_encoding_id = {6, 7}};
    BusterA64SemanticAlias invalid_alias_before = invalid_alias;
    BUSTER_TEST(arguments, !buster_a64_semantic_alias(UINT32_MAX, &invalid_alias) && memcmp(&invalid_alias, &invalid_alias_before, sizeof(invalid_alias)) == 0);
    BusterA64SemanticConstraint invalid_constraint = {.form_id = 1, .feature_first = 2, .program_first = 3, .feature_count = 4, .program_count = 5};
    BusterA64SemanticConstraint invalid_constraint_before = invalid_constraint;
    BUSTER_TEST(arguments, !buster_a64_semantic_constraint(UINT32_MAX, &invalid_constraint) && memcmp(&invalid_constraint, &invalid_constraint_before, sizeof(invalid_constraint)) == 0);
    BUSTER_TEST(arguments, buster_a64_semantic_string_byte((BusterA64SemanticString){1, UINT32_MAX}, UINT32_MAX - 1) == 0);
    return result;
}

#endif
