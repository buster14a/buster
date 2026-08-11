#include <buster/lib/compiler/assembly/aarch64_control_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/compiler/assembly/generated/aarch64-control-semantics.generated.h>
#include <buster/lib/string.h>

BUSTER_CT_CHECK(BUSTER_AARCH64_CONTROL_SEMANTIC_GENERATED_ROW_COUNT == BUSTER_AARCH64_CONTROL_SEMANTIC_ROW_COUNT);

BUSTER_GLOBAL_LOCAL BusterAarch64ControlSemanticRecord const* a64_control_row(u32 row)
{
    if (row >= BUSTER_AARCH64_CONTROL_SEMANTIC_ROW_COUNT) return 0;
    return buster_aarch64_control_semantic_generated_rows + row;
}

BUSTER_GLOBAL_LOCAL bool a64_control_string_valid(BusterAarch64ControlString string)
{
    return string.offset < BUSTER_AARCH64_CONTROL_SEMANTIC_STRING_POOL_SIZE && string.length <
                                                                    BUSTER_AARCH64_CONTROL_SEMANTIC_STRING_POOL_SIZE - string.offset &&
           buster_aarch64_control_semantic_string_pool[string.offset + string.length] == 0;
}

bool buster_aarch64_control_semantic_string(BusterAarch64ControlString string, String8* result)
{
    if (!result || !a64_control_string_valid(string)) return false;
    *result = (String8){.pointer = (char8*)buster_aarch64_control_semantic_string_pool + string.offset, .length = string.length};
    return true;
}

u8 buster_aarch64_control_semantic_string_byte(BusterAarch64ControlString string, u32 index)
{
    if (!a64_control_string_valid(string) || index >= string.length) return 0;
    return buster_aarch64_control_semantic_string_pool[string.offset + index];
}

u32 buster_aarch64_control_condition_count(void)
{
    return BUSTER_AARCH64_CONTROL_CONDITION_COUNT;
}

bool buster_aarch64_control_condition(u8 value, BusterAarch64ControlCondition* condition)
{
    if (!condition || value >= BUSTER_AARCH64_CONTROL_CONDITION_COUNT) return false;
    BusterAarch64ControlCondition candidate = buster_aarch64_control_conditions[value];
    if (!candidate.valid || candidate.value != value || !a64_control_string_valid(candidate.name)) return false;
    *condition = candidate;
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_control_fixed_word(BusterAarch64ControlSemanticRecord const* row, u32 word)
{
    return row && (row->fixed_value & ~row->fixed_mask) == 0 && (word & row->fixed_mask) == row->fixed_value;
}

BUSTER_GLOBAL_LOCAL bool a64_control_fixed_overlap(BusterAarch64ControlSemanticRecord const* first,
                                                    BusterAarch64ControlSemanticRecord const* second)
{
    if (!first || !second) return false;
    u32 shared_mask = first->fixed_mask & second->fixed_mask;
    return ((first->fixed_value ^ second->fixed_value) & shared_mask) == 0;
}

BUSTER_GLOBAL_LOCAL bool a64_control_operand_schema_matches(BusterAarch64ControlOperandSchema schema,
                                                              BusterAarch64ControlOperandValue value)
{
    if (schema.kind != value.kind) return false;
    if (schema.kind == BUSTER_AARCH64_CONTROL_OPERAND_REGISTER)
    {
        if (schema.register_class >= BUSTER_AARCH64_CONTROL_REGISTER_CLASS_COUNT || value.register_class != schema.register_class)
        {
            return false;
        }
        if (schema.width && schema.width != value.width) return false;
        if (!schema.width && value.width != 32 && value.width != 64) return false;
        if (value.value < schema.minimum || value.value > schema.maximum) return false;
        if (value.value == 31 && value.register31_role != schema.register31_role) return false;
        if (value.value != 31 && value.register31_role != BUSTER_AARCH64_CONTROL_REGISTER31_NONE) return false;
        return true;
    }
    if (schema.kind == BUSTER_AARCH64_CONTROL_OPERAND_PC_RELATIVE)
    {
        return value.width == 64;
    }
    if (schema.width && schema.width != value.width) return false;
    return value.value >= schema.minimum && value.value <= schema.maximum;
}

BUSTER_GLOBAL_LOCAL bool a64_control_pc_encode(BusterAarch64ControlPcRelative pc, s64 displacement, u32 word, u32* result)
{
    if (!result || pc.layout == BUSTER_AARCH64_CONTROL_PC_NONE || !pc.bits || pc.bits > 32 ||
        (pc.alignment && displacement % (s64)pc.alignment != 0) || displacement < pc.minimum || displacement > pc.maximum)
    {
        return false;
    }
    u32 immediate = 0;
    if (!a64_signed_scaled_immediate_encode(displacement, pc.bits, pc.scale_log2, &immediate)) return false;
    switch ((BusterAarch64ControlPcRelativeLayout)pc.layout)
    {
    case BUSTER_AARCH64_CONTROL_PC_IMM26:
        *result = (word & ~UINT32_C(0x03ffffff)) | immediate;
        return true;
    case BUSTER_AARCH64_CONTROL_PC_IMM19:
        *result = (word & ~UINT32_C(0x00ffffe0)) | (immediate << 5);
        return true;
    case BUSTER_AARCH64_CONTROL_PC_IMM14:
        *result = (word & ~UINT32_C(0x0007ffe0)) | (immediate << 5);
        return true;
    case BUSTER_AARCH64_CONTROL_PC_ADRP:
    case BUSTER_AARCH64_CONTROL_PC_ADR:
        *result = (word & ~UINT32_C(0x60ffffe0)) | ((immediate & 3u) << 29) | (((immediate >> 2) & UINT32_C(0x7ffff)) << 5);
        return true;
    case BUSTER_AARCH64_CONTROL_PC_NONE:
    case BUSTER_AARCH64_CONTROL_PC_COUNT:
        break;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool a64_control_pc_decode(BusterAarch64ControlPcRelative pc, u32 word, s64* displacement)
{
    if (!displacement || pc.layout == BUSTER_AARCH64_CONTROL_PC_NONE) return false;
    u32 immediate = 0;
    switch ((BusterAarch64ControlPcRelativeLayout)pc.layout)
    {
    case BUSTER_AARCH64_CONTROL_PC_IMM26:
        immediate = word & UINT32_C(0x03ffffff);
        break;
    case BUSTER_AARCH64_CONTROL_PC_IMM19:
        immediate = (word >> 5) & UINT32_C(0x7ffff);
        break;
    case BUSTER_AARCH64_CONTROL_PC_IMM14:
        immediate = (word >> 5) & UINT32_C(0x3fff);
        break;
    case BUSTER_AARCH64_CONTROL_PC_ADRP:
    case BUSTER_AARCH64_CONTROL_PC_ADR:
        immediate = ((word >> 29) & 3u) | (((word >> 5) & UINT32_C(0x7ffff)) << 2);
        break;
    case BUSTER_AARCH64_CONTROL_PC_NONE:
    case BUSTER_AARCH64_CONTROL_PC_COUNT:
        return false;
    }
    return a64_signed_scaled_immediate_decode(immediate, pc.bits, pc.scale_log2, displacement);
}

BUSTER_GLOBAL_LOCAL void a64_control_reg_value(BusterAarch64ControlOperandValue* value, u8 width, u32 reg, u8 register_class)
{
    *value = (BusterAarch64ControlOperandValue){
        .value = (s64)reg, .kind = BUSTER_AARCH64_CONTROL_OPERAND_REGISTER, .width = width,
        .register31_role = register_class == BUSTER_AARCH64_CONTROL_REGISTER_CLASS_FP_SIMD
                               ? BUSTER_AARCH64_CONTROL_REGISTER31_NONE
                               : (reg == 31 ? BUSTER_AARCH64_CONTROL_REGISTER31_ZR : BUSTER_AARCH64_CONTROL_REGISTER31_NONE),
        .register_class = register_class,
    };
}

BUSTER_GLOBAL_LOCAL void a64_control_pc_value(BusterAarch64ControlOperandValue* value, s64 displacement)
{
    *value = (BusterAarch64ControlOperandValue){.value = displacement, .kind = BUSTER_AARCH64_CONTROL_OPERAND_PC_RELATIVE, .width = 64};
}

BUSTER_GLOBAL_LOCAL void a64_control_imm_value(BusterAarch64ControlOperandValue* value, u8 kind, u8 width, s64 immediate)
{
    *value = (BusterAarch64ControlOperandValue){.value = immediate, .kind = kind, .width = width};
}

BUSTER_GLOBAL_LOCAL bool a64_control_row_encode(BusterAarch64ControlSemanticRecord const* row,
                                                 BusterAarch64ControlInstruction const* instruction, u32* result)
{
    if (!row || !instruction || !result || instruction->operand_count > 4) return false;
    u8 expected = row->operand_count;
    if (row->optional_operand_mask && instruction->operand_count == 0)
    {
        expected = 0;
    }
    else if (instruction->operand_count != expected)
    {
        return false;
    }
    for (u32 index = 0; index < instruction->operand_count; index += 1)
    {
        if (!a64_control_operand_schema_matches(row->operands[index], instruction->operands[index])) return false;
    }
    u32 word = row->fixed_value;
    s64 value = 0;
    switch ((BusterAarch64ControlForm)row->form)
    {
    case BUSTER_AARCH64_CONTROL_FORM_ADRP:
    case BUSTER_AARCH64_CONTROL_FORM_ADR:
        if (instruction->operand_count != expected && expected != 0) return false;
        if (instruction->operand_count == 0) return false;
        word |= (u32)instruction->operands[0].value;
        value = instruction->operands[1].value;
        if (!a64_control_pc_encode(row->pc_relative, value, word, &word)) return false;
        break;
    case BUSTER_AARCH64_CONTROL_FORM_B:
    case BUSTER_AARCH64_CONTROL_FORM_BL:
        value = instruction->operands[0].value;
        // B/BL are the exact forms for which the existing checked patch helper
        // is also the relocation kernel; use it when possible.
        if ((row->form == BUSTER_AARCH64_CONTROL_FORM_B &&
             !a64_pc_relative_patch(A64_OPCODE_B, word, value, &word)) ||
            (row->form == BUSTER_AARCH64_CONTROL_FORM_BL &&
             !a64_pc_relative_patch(A64_OPCODE_BL, word, value, &word)))
        {
            return false;
        }
        break;
    case BUSTER_AARCH64_CONTROL_FORM_B_COND:
        if (!a64_pc_relative_patch(A64_OPCODE_B_COND, word, instruction->operands[0].value, &word)) return false;
        word |= (u32)instruction->operands[1].value;
        break;
    case BUSTER_AARCH64_CONTROL_FORM_CBZ_W:
    case BUSTER_AARCH64_CONTROL_FORM_CBNZ_W:
    case BUSTER_AARCH64_CONTROL_FORM_CBZ_X:
    case BUSTER_AARCH64_CONTROL_FORM_CBNZ_X:
    {
        A64Opcode opcode = A64_OPCODE_CBZ_W;
        if (row->form == BUSTER_AARCH64_CONTROL_FORM_CBNZ_W) opcode = A64_OPCODE_CBNZ_W;
        if (row->form == BUSTER_AARCH64_CONTROL_FORM_CBZ_X) opcode = A64_OPCODE_CBZ_X;
        if (row->form == BUSTER_AARCH64_CONTROL_FORM_CBNZ_X) opcode = A64_OPCODE_CBNZ_X;
        word |= (u32)instruction->operands[0].value;
        if (!a64_pc_relative_patch(opcode, word, instruction->operands[1].value, &word)) return false;
    }
    break;
    case BUSTER_AARCH64_CONTROL_FORM_TBZ:
    case BUSTER_AARCH64_CONTROL_FORM_TBNZ:
        if ((instruction->operands[0].width == 32 && instruction->operands[1].value > 31) ||
            (instruction->operands[0].width == 64 && instruction->operands[1].value > 63))
        {
            return false;
        }
        word |= (u32)instruction->operands[0].value;
        word |= ((u32)instruction->operands[1].value & 31u) << 19;
        word |= ((u32)instruction->operands[1].value >> 5) << 31;
        if (!a64_pc_relative_patch(row->form == BUSTER_AARCH64_CONTROL_FORM_TBZ ? A64_OPCODE_TBZ : A64_OPCODE_TBNZ, word,
                                   instruction->operands[2].value, &word))
        {
            return false;
        }
        break;
    case BUSTER_AARCH64_CONTROL_FORM_LDR_W:
    case BUSTER_AARCH64_CONTROL_FORM_LDR_X:
    case BUSTER_AARCH64_CONTROL_FORM_LDRSW_X:
    case BUSTER_AARCH64_CONTROL_FORM_LDR_S:
    case BUSTER_AARCH64_CONTROL_FORM_LDR_D:
    case BUSTER_AARCH64_CONTROL_FORM_LDR_Q:
    case BUSTER_AARCH64_CONTROL_FORM_PRFM:
        word |= (u32)instruction->operands[0].value;
        if (!a64_control_pc_encode(row->pc_relative, instruction->operands[1].value, word, &word)) return false;
        break;
    case BUSTER_AARCH64_CONTROL_FORM_RET:
        if (instruction->operand_count == 0)
        {
            value = row->default_operand;
        }
        else
        {
            value = instruction->operands[0].value;
        }
        word |= (u32)value << 5;
        break;
    case BUSTER_AARCH64_CONTROL_FORM_CSEL_W:
    case BUSTER_AARCH64_CONTROL_FORM_CSEL_X:
    case BUSTER_AARCH64_CONTROL_FORM_CSINC_W:
    case BUSTER_AARCH64_CONTROL_FORM_CSINC_X:
    case BUSTER_AARCH64_CONTROL_FORM_CSINV_W:
    case BUSTER_AARCH64_CONTROL_FORM_CSINV_X:
    case BUSTER_AARCH64_CONTROL_FORM_CSNEG_W:
    case BUSTER_AARCH64_CONTROL_FORM_CSNEG_X:
        word |= (u32)instruction->operands[0].value;
        word |= (u32)instruction->operands[1].value << 5;
        word |= ((u32)instruction->operands[3].value & 15u) << 12;
        word |= (u32)instruction->operands[2].value << 16;
        break;
    case BUSTER_AARCH64_CONTROL_FORM_COUNT:
        return false;
    }
    *result = word;
    return true;
}

u32 buster_aarch64_control_semantic_count(void)
{
    return BUSTER_AARCH64_CONTROL_SEMANTIC_ROW_COUNT;
}

bool buster_aarch64_control_semantic_row(u32 row, BusterAarch64ControlSemanticRecord* result)
{
    BusterAarch64ControlSemanticRecord const* source = a64_control_row(row);
    if (!source || !result) return false;
    *result = *source;
    return true;
}

bool buster_aarch64_control_semantic_lookup(String8 id, u32* row)
{
    if (!row) return false;
    for (u32 index = 0; index < BUSTER_AARCH64_CONTROL_SEMANTIC_ROW_COUNT; index += 1)
    {
        BusterAarch64ControlSemanticRecord const* candidate = a64_control_row(index);
        String8 candidate_id = {0};
        if (buster_aarch64_control_semantic_string(candidate->id, &candidate_id) && string_equal(candidate_id, id))
        {
            *row = index;
            return true;
        }
    }
    return false;
}

bool buster_aarch64_control_semantic_validate(void)
{
    if (BUSTER_AARCH64_CONTROL_CONDITION_COUNT != 16u) return false;
    for (u32 condition_index = 0; condition_index < BUSTER_AARCH64_CONTROL_CONDITION_COUNT; condition_index += 1)
    {
        BusterAarch64ControlCondition condition = buster_aarch64_control_conditions[condition_index];
        if (!condition.valid || condition.value != condition_index || !a64_control_string_valid(condition.name)) return false;
        if (condition.inverse_valid)
        {
            if (condition.inverse >= BUSTER_AARCH64_CONTROL_CONDITION_COUNT ||
                !buster_aarch64_control_conditions[condition.inverse].inverse_valid ||
                buster_aarch64_control_conditions[condition.inverse].inverse != condition.value)
            {
                return false;
            }
        }
        else if (condition.inverse != UINT8_MAX)
        {
            return false;
        }
    }
    u32 owner_counts[BUSTER_AARCH64_CONTROL_OWNER_COUNT] = {0};
    for (u32 index = 0; index < BUSTER_AARCH64_CONTROL_SEMANTIC_ROW_COUNT; index += 1)
    {
        BusterAarch64ControlSemanticRecord const* row = a64_control_row(index);
        if (!row || !a64_control_string_valid(row->id) || !a64_control_string_valid(row->encoding_name) ||
            !a64_control_string_valid(row->mnemonic) || !a64_control_string_valid(row->assembly) ||
            row->owner >= BUSTER_AARCH64_CONTROL_OWNER_COUNT || row->form >= BUSTER_AARCH64_CONTROL_FORM_COUNT ||
            row->operand_count > 4 || row->fixed_value & ~row->fixed_mask || row->fixup_kind >= BUSTER_AARCH64_CONTROL_FIXUP_COUNT ||
            row->relocation_policy >= BUSTER_AARCH64_CONTROL_RELOCATION_COUNT)
        {
            return false;
        }
        owner_counts[row->owner] += 1;
        for (u32 prior = 0; prior < index; prior += 1)
        {
            String8 row_id = {0};
            String8 prior_id = {0};
            if (!buster_aarch64_control_semantic_string(row->id, &row_id) ||
                !buster_aarch64_control_semantic_string(a64_control_row(prior)->id, &prior_id))
            {
                return false;
            }
            if (string_equal(row_id, prior_id)) return false;
            // A fixed-bit intersection must identify at most one row.  Keep
            // this invariant explicit so decoder selection cannot silently
            // depend on table order as the generated slice grows.
            if (a64_control_fixed_overlap(row, a64_control_row(prior))) return false;
        }
        for (u32 operand_index = 0; operand_index < row->operand_count; operand_index += 1)
        {
            BusterAarch64ControlOperandSchema operand = row->operands[operand_index];
            if (operand.role >= BUSTER_AARCH64_CONTROL_ROLE_COUNT || operand.kind >= BUSTER_AARCH64_CONTROL_OPERAND_COUNT ||
                operand.minimum > operand.maximum)
            {
                return false;
            }
            if (operand.kind == BUSTER_AARCH64_CONTROL_OPERAND_REGISTER &&
                operand.register_class >= BUSTER_AARCH64_CONTROL_REGISTER_CLASS_COUNT)
            {
                return false;
            }
        }
        if (row->pc_relative.layout >= BUSTER_AARCH64_CONTROL_PC_COUNT) return false;
        if (row->pc_relative.layout != BUSTER_AARCH64_CONTROL_PC_NONE &&
            (!row->pc_relative.bits || !row->pc_relative.alignment || row->pc_relative.minimum > row->pc_relative.maximum))
        {
            return false;
        }
    }
    return owner_counts[BUSTER_AARCH64_CONTROL_OWNER_GENERAL] == 24 && owner_counts[BUSTER_AARCH64_CONTROL_OWNER_COMPLEX_LITERAL] == 3;
}

char const* buster_aarch64_control_semantic_digest(void)
{
    return BUSTER_AARCH64_CONTROL_SEMANTIC_DIGEST;
}

char const* buster_aarch64_control_semantic_group_digest(BusterAarch64ControlOwner owner)
{
    // The owner-level query is intentionally conservative because GENERAL is
    // split into label/fixup, condition-select, and optional RET subgroups.
    // Callers needing those exact cross-lens values use the four named macros.
    return owner == BUSTER_AARCH64_CONTROL_OWNER_COMPLEX_LITERAL ? BUSTER_AARCH64_CONTROL_LITERAL_DIGEST : 0;
}

bool buster_aarch64_control_semantic_encode(BusterAarch64ControlInstruction const* instruction, u32* word)
{
    if (!instruction || !word || instruction->row >= BUSTER_AARCH64_CONTROL_SEMANTIC_ROW_COUNT) return false;
    u32 encoded = 0;
    if (!a64_control_row_encode(a64_control_row(instruction->row), instruction, &encoded)) return false;
    *word = encoded;
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_control_decode_row(u32 word, u32* row_out)
{
    if (!row_out) return false;
    u32 match_count = 0;
    u32 matching_row = 0;
    for (u32 index = 0; index < BUSTER_AARCH64_CONTROL_SEMANTIC_ROW_COUNT; index += 1)
    {
        if (a64_control_fixed_word(a64_control_row(index), word))
        {
            matching_row = index;
            match_count += 1;
        }
    }
    if (match_count != 1) return false;
    *row_out = matching_row;
    return true;
}

bool buster_aarch64_control_semantic_decode(u32 word, BusterAarch64ControlInstruction* instruction)
{
    if (!instruction) return false;
    u32 row_index = 0;
    if (!a64_control_decode_row(word, &row_index)) return false;
    BusterAarch64ControlSemanticRecord const* row = a64_control_row(row_index);
    BusterAarch64ControlInstruction decoded = {.row = (u16)row_index, .operand_count = row->operand_count};
    s64 displacement = 0;
    switch ((BusterAarch64ControlForm)row->form)
    {
    case BUSTER_AARCH64_CONTROL_FORM_ADRP:
    case BUSTER_AARCH64_CONTROL_FORM_ADR:
        a64_control_reg_value(&decoded.operands[0], 64, word & 31u, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        if (!a64_control_pc_decode(row->pc_relative, word, &displacement)) return false;
        a64_control_pc_value(&decoded.operands[1], displacement);
        break;
    case BUSTER_AARCH64_CONTROL_FORM_B:
    case BUSTER_AARCH64_CONTROL_FORM_BL:
        if (!a64_control_pc_decode(row->pc_relative, word, &displacement)) return false;
        a64_control_pc_value(&decoded.operands[0], displacement);
        break;
    case BUSTER_AARCH64_CONTROL_FORM_B_COND:
        if (!a64_control_pc_decode(row->pc_relative, word, &displacement)) return false;
        a64_control_pc_value(&decoded.operands[0], displacement);
        a64_control_imm_value(&decoded.operands[1], BUSTER_AARCH64_CONTROL_OPERAND_CONDITION, 4, word & 15u);
        break;
    case BUSTER_AARCH64_CONTROL_FORM_CBZ_W:
    case BUSTER_AARCH64_CONTROL_FORM_CBNZ_W:
    case BUSTER_AARCH64_CONTROL_FORM_CBZ_X:
    case BUSTER_AARCH64_CONTROL_FORM_CBNZ_X:
        a64_control_reg_value(&decoded.operands[0], row->form == BUSTER_AARCH64_CONTROL_FORM_CBZ_W ||
                                                         row->form == BUSTER_AARCH64_CONTROL_FORM_CBNZ_W
                                                     ? 32
                                                     : 64,
                              word & 31u, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        if (!a64_control_pc_decode(row->pc_relative, word, &displacement)) return false;
        a64_control_pc_value(&decoded.operands[1], displacement);
        break;
    case BUSTER_AARCH64_CONTROL_FORM_TBZ:
    case BUSTER_AARCH64_CONTROL_FORM_TBNZ:
        a64_control_reg_value(&decoded.operands[0], ((word >> 31) & 1u) ? 64 : 32, word & 31u,
                              BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        a64_control_imm_value(&decoded.operands[1], BUSTER_AARCH64_CONTROL_OPERAND_IMMEDIATE, 6,
                              ((word >> 31) & 1u) << 5 | ((word >> 19) & 31u));
        if (!a64_control_pc_decode(row->pc_relative, word, &displacement)) return false;
        a64_control_pc_value(&decoded.operands[2], displacement);
        break;
    case BUSTER_AARCH64_CONTROL_FORM_LDR_W:
    case BUSTER_AARCH64_CONTROL_FORM_LDR_S:
        a64_control_reg_value(&decoded.operands[0], 32, word & 31u,
                              row->form == BUSTER_AARCH64_CONTROL_FORM_LDR_S ? BUSTER_AARCH64_CONTROL_REGISTER_CLASS_FP_SIMD
                                                                                : BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        goto decode_literal;
    case BUSTER_AARCH64_CONTROL_FORM_LDR_X:
    case BUSTER_AARCH64_CONTROL_FORM_LDRSW_X:
    case BUSTER_AARCH64_CONTROL_FORM_LDR_D:
        a64_control_reg_value(&decoded.operands[0], 64, word & 31u,
                              row->form == BUSTER_AARCH64_CONTROL_FORM_LDR_D ? BUSTER_AARCH64_CONTROL_REGISTER_CLASS_FP_SIMD
                                                                                : BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        goto decode_literal;
    case BUSTER_AARCH64_CONTROL_FORM_LDR_Q:
        a64_control_reg_value(&decoded.operands[0], 128, word & 31u, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_FP_SIMD);
        goto decode_literal;
    case BUSTER_AARCH64_CONTROL_FORM_PRFM:
        a64_control_imm_value(&decoded.operands[0], BUSTER_AARCH64_CONTROL_OPERAND_IMMEDIATE, 5, word & 31u);
    decode_literal:
        if (!a64_control_pc_decode(row->pc_relative, word, &displacement)) return false;
        a64_control_pc_value(&decoded.operands[1], displacement);
        break;
    case BUSTER_AARCH64_CONTROL_FORM_RET:
        a64_control_reg_value(&decoded.operands[0], 64, (word >> 5) & 31u, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        decoded.defaulted_mask = ((word >> 5) & 31u) == row->default_operand ? 1 : 0;
        break;
    case BUSTER_AARCH64_CONTROL_FORM_CSEL_W:
    case BUSTER_AARCH64_CONTROL_FORM_CSINC_W:
    case BUSTER_AARCH64_CONTROL_FORM_CSINV_W:
    case BUSTER_AARCH64_CONTROL_FORM_CSNEG_W:
        a64_control_reg_value(&decoded.operands[0], 32, word & 31u, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        a64_control_reg_value(&decoded.operands[1], 32, (word >> 5) & 31u, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        a64_control_reg_value(&decoded.operands[2], 32, (word >> 16) & 31u, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        a64_control_imm_value(&decoded.operands[3], BUSTER_AARCH64_CONTROL_OPERAND_CONDITION, 4, (word >> 12) & 15u);
        break;
    case BUSTER_AARCH64_CONTROL_FORM_CSEL_X:
    case BUSTER_AARCH64_CONTROL_FORM_CSINC_X:
    case BUSTER_AARCH64_CONTROL_FORM_CSINV_X:
    case BUSTER_AARCH64_CONTROL_FORM_CSNEG_X:
        a64_control_reg_value(&decoded.operands[0], 64, word & 31u, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        a64_control_reg_value(&decoded.operands[1], 64, (word >> 5) & 31u, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        a64_control_reg_value(&decoded.operands[2], 64, (word >> 16) & 31u, BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR);
        a64_control_imm_value(&decoded.operands[3], BUSTER_AARCH64_CONTROL_OPERAND_CONDITION, 4, (word >> 12) & 15u);
        break;
    case BUSTER_AARCH64_CONTROL_FORM_COUNT:
        return false;
    }
    *instruction = decoded;
    return true;
}

bool buster_aarch64_control_semantic_patch(u32 row_index, u32 word, s64 displacement, u32* patched)
{
    BusterAarch64ControlSemanticRecord const* row = a64_control_row(row_index);
    if (!a64_control_fixed_word(row, word) || !patched || row->pc_relative.layout == BUSTER_AARCH64_CONTROL_PC_NONE) return false;
    u32 result = 0;
    if (!a64_control_pc_encode(row->pc_relative, displacement, word, &result)) return false;
    *patched = result;
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_control_fixup_is_darwin(Target target)
{
    return target.cpu_arch == CPU_ARCH_AARCH64 &&
           (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS);
}

BUSTER_GLOBAL_LOCAL u8 a64_control_relocation_kind(BusterAarch64ControlFixupKind kind)
{
    if (kind == BUSTER_AARCH64_CONTROL_FIXUP_BRANCH26) return BUSTER_AARCH64_CONTROL_RELOCATION_KIND_BRANCH26;
    if (kind == BUSTER_AARCH64_CONTROL_FIXUP_CALL26) return BUSTER_AARCH64_CONTROL_RELOCATION_KIND_CALL26;
    return BUSTER_AARCH64_CONTROL_RELOCATION_KIND_NONE;
}

BUSTER_GLOBAL_LOCAL bool a64_control_add_signed_u64(u64 base, s64 addend, u64* result)
{
    if (!result) return false;
    if (addend >= 0)
    {
        u64 magnitude = (u64)addend;
        if (base > UINT64_MAX - magnitude) return false;
        *result = base + magnitude;
        return true;
    }
    u64 magnitude = (u64)(-(addend + 1)) + 1;
    if (base < magnitude) return false;
    *result = base - magnitude;
    return true;
}

bool buster_aarch64_control_semantic_fixup(u32 row_index, u32 word, BusterAarch64ControlFixupRequest request, u32* patched,
                                           BusterAarch64ControlFixupResult* result)
{
    BusterAarch64ControlSemanticRecord const* row = a64_control_row(row_index);
    if (!row || !patched || !result || !a64_control_fixed_word(row, word) || row->pc_relative.layout == BUSTER_AARCH64_CONTROL_PC_NONE)
    {
        return false;
    }
    BusterAarch64ControlFixupResult resolved = {
        .fixup_kind = row->fixup_kind,
        .relocation_kind = BUSTER_AARCH64_CONTROL_RELOCATION_KIND_NONE,
        .external = 0,
        .resolved = 0,
        .displacement = 0,
    };
    if (!request.symbol_defined && request.symbol_external)
    {
        if (row->relocation_policy != BUSTER_AARCH64_CONTROL_RELOCATION_DARWIN_EXTERNAL_BRANCH26 ||
            !a64_control_fixup_is_darwin(request.target))
        {
            return false;
        }
        resolved.external = 1;
        resolved.relocation_kind = a64_control_relocation_kind((BusterAarch64ControlFixupKind)row->fixup_kind);
        // Keep the fixed word untouched; the object writer owns relocation
        // insertion and must not receive a fabricated local displacement.
        *patched = word;
        *result = resolved;
        return true;
    }
    if (!request.symbol_defined) return false;
    u64 target = request.target_address;
    u64 place = request.place_address;
    s64 addend = request.addend;
    if (row->pc_relative.layout == BUSTER_AARCH64_CONTROL_PC_ADRP)
    {
        // ADRP addresses pages after applying the relocation addend.  The
        // addend may be unaligned and can cross a page boundary (for example
        // target=0x1fff,+1); only the resulting target is page-truncated.
        if (!a64_control_add_signed_u64(target, addend, &target)) return false;
        target &= ~UINT64_C(4095);
        place &= ~UINT64_C(4095);
        addend = 0;
    }
    s64 displacement = 0;
    if (!a64_pc_relative_displacement(target, place, addend, &displacement)) return false;
    u32 local_word = 0;
    if (!buster_aarch64_control_semantic_patch(row_index, word, displacement, &local_word)) return false;
    resolved.resolved = 1;
    resolved.displacement = displacement;
    *patched = local_word;
    *result = resolved;
    return true;
}
