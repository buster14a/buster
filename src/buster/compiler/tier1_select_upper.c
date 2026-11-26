// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Upper vector-register qualification for the CFG consumer (#3625). Include after tier1_select_holes.
// The output target and each selected kernel own the register-number limit; the host ISA does not.

// A row can keep vector values in registers 16..31 only where its templates and the moves into its roles can name
// them. AVX-512F without VL still admits a row whose vectors are all 64 bytes. Floating scalar roles have their own
// low-register restriction (tier1_select_row_low_floating), and allocated scalar values always stay below xmm15.
static bool tier1_select_row_upper_vectors(const Tier0Operation* row, uint32_t encoding, bool zmm)
{
    const Tier0Reach reach = tier0_operation_reach(row);
    bool narrow = false;
    for (uint32_t j = 0; j <= row->operand_count; j += 1)
    {
        const uint32_t class = j < row->operand_count ? row->operand_classes[j] : row->result_class;
        const uint32_t bytes = j < row->operand_count ? row->operand_bytes[j] : row->result_bytes;
        narrow |= tier0_class_vector(class) && bytes < 64;
    }
    return reach.vex_vectors == 0 && (encoding == BUSTER_TIER1_ENCODING_EVEX || (!narrow && zmm));
}

// All transport of an allocated value, including copies through the class scratch, must admit its upper register.
// Scalar floating values remain low: their ordinary kernels and spill forms are legacy SSE's.
static bool tier1_select_upper_vector_type(uint8_t type, const uint8_t* allowed_forms, uint32_t encoding)
{
    const uint32_t width = tier1_select_vector_width(type);
    const Tier1SelectFloatForms* memory = tier1_select_class_forms(width, encoding);
    const uint16_t move = tier1_select_vector_forms(width, encoding)->move;
    const bool wide = width == 16 || width == 32 || width == 64;
    return wide && x86_target_form_allowed(allowed_forms, memory->load) && x86_target_form_allowed(allowed_forms, memory->store) &&
           x86_target_form_allowed(allowed_forms, move) && x86_form_description(memory->load)->family == BUSTER_X86_FAMILY_EVEX &&
           x86_form_description(memory->store)->family == BUSTER_X86_FAMILY_EVEX && x86_form_description(move)->family == BUSTER_X86_FAMILY_EVEX;
}

// The node's upper-register roles: bit 0 its result and bits 1..4 pattern.values[0..3]. Each role first passes its
// complete transport contract. Expand the selected kernel with distinct probe registers, then constrain only the
// roles a legacy/VEX record reads or writes. This uses the actual form choices (lane extraction, mixed-width
// conversions and paired loads among them), rather than a second hand-maintained opcode allowlist. Alias repairs
// use the already-qualified full-width copies through xmm15. Rows use their own placement contract below.
static uint32_t tier1_select_upper_vector_roles(const Tier1SelectPlan* plan, uint32_t node, const uint8_t* allowed_forms, uint32_t encoding, bool avx2)
{
    const IrFunction* function = plan->function;
    const Tier1SelectPattern* pattern = &plan->patterns[node];
    uint32_t upper = 0;
    uint8_t registers[BUSTER_TIER1_SELECT_VALUES + 1];
    for (uint32_t role = 0; role <= BUSTER_TIER1_SELECT_VALUES; role += 1)
    {
        const uint32_t value = role == 0 ? node : pattern->values[role - 1];
        const uint8_t type = value < function->instruction_count ? function->type[value] : IR_VOID;
        const bool vector = tier1_select_vector_width(type) != 0;
        upper |= tier1_select_upper_vector_type(type, allowed_forms, encoding) ? 1u << role : 0;
        registers[role] = (uint8_t)(vector ? 24 + role : role + 1);
    }
    if (upper != 0 && pattern->rule == TIER1_RULE_ROW)
    {
        const IrCall* call = &function->calls[function->b[node]];
        const Tier0Operation* row = &function->rows[call->row - 1];
        const bool zmm = x86_target_form_allowed(allowed_forms, X86_FORM_VMOVDQA64_Z_ZM512);
        upper = tier1_select_row_upper_vectors(row, encoding, zmm) ? upper : 0;
    }
    else if (upper != 0 && (pattern->rule == TIER1_RULE_CALL || pattern->rule == TIER1_RULE_ASM))
    {
        upper = 0;
    }
    else if (upper != 0 && function->op[node] != IR_PHI)
    {
        Tier1SelectHoles holes = {
            .rule = (Tier1SelectRule)pattern->rule,
            .operation = (X86Operation)pattern->operation,
            .immediate = pattern->immediate,
            .memory = { .mode = BUSTER_X86_MEMORY_BASE },
            .width = pattern->width,
            .source_width = pattern->source_width,
            .destination = registers[0],
            .left = registers[1],
            .right = registers[2],
            .third = registers[3],
            .fourth = registers[4],
            .has_fourth = pattern->values[3] != BUSTER_IR_NONE,
            .condition = pattern->condition,
            .scale = pattern->scale,
            .compare_immediate = pattern->compare_immediate,
            .compare_memory = pattern->compare_memory,
            .sign_result = pattern->sign_result,
            .vex = encoding != BUSTER_TIER1_ENCODING_EVEX,
            .sse = encoding == BUSTER_TIER1_ENCODING_SSE,
            .avx2 = avx2,
        };
        if ((pattern->rule == TIER1_RULE_FLOAT_STORE || pattern->rule == TIER1_RULE_FLOAT_STORE_ADDRESS) && pattern->condition == 0)
        {
            holes.right = holes.third;
        }
        const Tier1SelectRecords selected = tier1_select_expand(&holes);
        upper = selected.overflowed ? 0 : upper;
        for (uint32_t r = 0; upper != 0 && r < selected.count; r += 1)
        {
            X86Instruction record = selected.records[r];
            const X86FormDescription* form = x86_form_description(record.form);
            const Tier0Fields fields = tier0_operation_fields(&record);
            if (!x86_target_form_allowed(allowed_forms, record.form) || fields.is4)
            {
                // /is4 has only four register bits. It cannot name an upper mask-vector operand either.
                upper = 0;
            }
            for (uint32_t field = 0; field < fields.count; field += 1)
            {
                const uint8_t reg = *fields.bytes[field];
                if (form->family != BUSTER_X86_FAMILY_EVEX && tier0_vector_register_class(fields.classes[field]) && reg >= 24 && reg <= 24 + BUSTER_TIER1_SELECT_VALUES)
                {
                    upper &= ~(1u << (reg - 24));
                }
            }
        }
    }
    return upper;
}
