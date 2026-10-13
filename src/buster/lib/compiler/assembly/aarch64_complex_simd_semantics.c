/*
 * Typed complex AdvSIMD/FP encoder/decoder over the generated complex-SIMD
 * rows.
 *
 * Ownership: the complex-SIMD public API (aarch64_complex_simd_semantics.h):
 * multi-kind operands, concatenated register fields, lane transforms and raw
 * field round trips.  Row access, arrangement vocabulary and value-table
 * interpreters come from aarch64_simd_semantics through
 * buster_a64_complex_simd_catalog.
 *
 * Map:
 * - buster_a64_complex_simd_operand_*: operand kind-mask predicates.
 * - buster_a64_complex_simd_operand_concat_parts and its users: M:Rm style
 *   concatenated register fields.
 * - buster_a64_complex_simd_inverse_lane_transform,
 *   buster_a64_complex_simd_encode_operand, buster_a64_complex_simd_encode:
 *   operand values to a canonical-checked word.
 * - buster_a64_complex_simd_decode_register,
 *   buster_a64_complex_simd_decode_internal: word to typed operands.
 * - buster_a64_complex_simd_validate: generated-catalog invariants.
 */

#include <buster/lib/compiler/assembly/aarch64_complex_simd_semantics.h>
#include <buster/lib/compiler/assembly/generated/aarch64-complex-simd.generated.h>

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(buster_a64_complex_simd_generated_rows) == BUSTER_A64_COMPLEX_SIMD_ROW_COUNT);
BUSTER_CT_CHECK(BUSTER_A64_COMPLEX_SIMD_MAX_OPERANDS == BUSTER_A64_SIMD_MAX_OPERANDS);

BUSTER_GLOBAL_LOCAL BusterA64SIMDCatalog const buster_a64_complex_simd_catalog = {
    .rows = buster_a64_complex_simd_generated_rows,
    .bindings = buster_a64_complex_simd_generated_arrangement_bindings,
    .string_pool = buster_a64_complex_simd_generated_string_pool,
    .row_count = BUSTER_A64_COMPLEX_SIMD_ROW_COUNT,
};

BUSTER_GLOBAL_LOCAL bool buster_a64_complex_simd_operand_has_kind(BusterA64SemanticOperand operand, BusterA64SemanticOperandKind kind)
{
    u32 bit = (u32)kind;
    return bit < 32 && (operand.kind_mask & (UINT32_C(1) << bit)) != 0;
}

BUSTER_GLOBAL_LOCAL bool buster_a64_complex_simd_operand_has_arrangement_kind(BusterA64SemanticOperand operand)
{
    return buster_a64_complex_simd_operand_has_kind(operand, BUSTER_A64_SEMANTIC_OPERAND_SIMD_ARRANGEMENT) ||
           buster_a64_complex_simd_operand_has_kind(operand, BUSTER_A64_SEMANTIC_OPERAND_SIMD_WIDTH_SELECTOR) ||
           buster_a64_complex_simd_operand_has_kind(operand, BUSTER_A64_SEMANTIC_OPERAND_SIMD_PREFIX_SELECTOR);
}

BUSTER_GLOBAL_LOCAL bool buster_a64_complex_simd_operand_is_arrangement_selector(BusterA64SemanticOperand operand)
{
    return (operand.flags & BUSTER_A64_SEMANTIC_FLAG_ARRANGEMENT_SELECTOR) != 0 &&
           !buster_a64_complex_simd_operand_has_kind(operand, BUSTER_A64_SEMANTIC_OPERAND_FIXED_CONSTANT) &&
           buster_a64_complex_simd_operand_has_arrangement_kind(operand);
}

BUSTER_GLOBAL_LOCAL bool buster_a64_complex_simd_operand_has_register_kind(BusterA64SemanticOperand operand)
{
    /* ``simd_lane`` covers both encoded lane registers and presentation-only
     * element indices.  An immediate index is marked ``simd_lane_index`` but
     * not ``simd_index_register``; its transform owns the encoded mapping.
     * Keep all other lane metadata on the register path. */
    return buster_a64_complex_simd_operand_has_kind(operand, BUSTER_A64_SEMANTIC_OPERAND_SIMD_REGISTER) ||
           buster_a64_complex_simd_operand_has_kind(operand, BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST) ||
           (buster_a64_complex_simd_operand_has_kind(operand, BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE) &&
            ((operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LANE_INDEX) == 0 ||
             (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_INDEX_REGISTER) != 0));
}

u32 buster_a64_complex_simd_schema_version(void)
{
    return BUSTER_A64_COMPLEX_SIMD_SCHEMA_VERSION;
}
u32 buster_a64_complex_simd_row_count(void)
{
    return BUSTER_A64_COMPLEX_SIMD_ROW_COUNT;
}
u32 buster_a64_complex_simd_transform_row_count(void)
{
    return BUSTER_A64_COMPLEX_SIMD_TRANSFORM_ROW_COUNT;
}
u32 buster_a64_complex_simd_executable_row_count(void)
{
    return BUSTER_A64_COMPLEX_SIMD_EXECUTABLE_ROW_COUNT;
}
u32 buster_a64_complex_simd_max_operands(void)
{
    return BUSTER_A64_COMPLEX_SIMD_MAX_OPERANDS;
}
u32 buster_a64_complex_simd_arrangement_binding_count(void)
{
    return BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_BINDING_COUNT;
}

bool buster_a64_complex_simd_row(u32 row_index, BusterA64SIMDRowInfo* result)
{
    return buster_a64_simd_catalog_row_info(&buster_a64_complex_simd_catalog, row_index, result);
}

bool buster_a64_complex_simd_find_source_digest(u64 source_digest, u32* row_index)
{
    return buster_a64_simd_catalog_find_source_digest(&buster_a64_complex_simd_catalog, source_digest, row_index);
}

bool buster_a64_complex_simd_arrangement_binding(u32 row_index, u32 operand_index, BusterA64SIMDArrangementBinding* result)
{
    return buster_a64_simd_catalog_arrangement_binding(&buster_a64_complex_simd_catalog, row_index, operand_index, result);
}

BUSTER_GLOBAL_LOCAL u8 buster_a64_complex_simd_scalar_aux_vector_width(u32 row_index, BusterA64SemanticForm form, u32 operand_index,
                                                                       BusterA64SemanticVMValue const* values)
{
    bool dup_scalar_aux = row_index == 14 && form.id == 209 && operand_index == 2;
    u8 selected_width = 0;
    for (u32 index = 0; values && selected_width == 0 && index < form.operand_count; index += 1)
    {
        BusterA64SemanticVMValue candidate = values[index];
        if (candidate.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT)
        {
            u8 width = buster_a64_simd_register_width((BusterA64SIMDArrangement)candidate.aux);
            if (width == 64 || width == 128)
            {
                selected_width = width;
            }
        }
    }
    u8 result;
    if (selected_width != 0)
    {
        result = selected_width;
    }
    else if ((form.fixed_mask & UINT32_C(0x40000000)) != 0)
    {
        result = (form.fixed_value & UINT32_C(0x40000000)) != 0 ? 128 : 64;
    }
    else if (dup_scalar_aux)
    {
        result = 128;
    }
    else
    {
        result = 0;
    }

    return result;
}

/* Some encoded SIMD registers are concatenations of multiple semantic
 * fields, such as M:Rm for by-element operands. Keep their field layout
 * authoritative in the existing transform metadata. */
BUSTER_GLOBAL_LOCAL bool buster_a64_complex_simd_operand_concat_parts(BusterA64SemanticForm form, BusterA64SemanticOperand operand, bool* has_concat,
                                                                      u32 locals[8], u8 widths[8], u32* part_count, u32* total_width)
{
    bool valid = has_concat && part_count && total_width;
    bool concat = false;
    BusterA64SemanticTransform transform = {0};
    if (valid)
    {
        *has_concat = false;
        *part_count = 0;
        *total_width = 0;
        if (operand.transform_count != 0)
        {
            valid = buster_a64_semantic_transform(operand.transform_first, &transform);
            concat = valid && transform.kind == BUSTER_A64_SEMANTIC_TRANSFORM_CONCAT;
        }
    }
    if (concat)
    {
        *has_concat = true;
        valid = transform.part_count != 0 && transform.part_count <= 8 && transform.part_count == operand.field_index_count && locals && widths;
        u64 seen = 0;
        for (u32 index = 0; valid && index < transform.part_count; index += 1)
        {
            BusterA64SemanticString part_name = {0};
            u32 local = 0;
            BusterA64SemanticField field = {0};
            valid = buster_a64_semantic_transform_part(transform.id, index, &part_name) && buster_a64_simd_find_field(form, part_name, &local) && local < 64 &&
                    buster_a64_semantic_field(form.field_first + local, &field) && field.width != 0 && field.width <= 32;
            bool operand_field = false;
            for (u32 field_index = 0; valid && field_index < operand.field_index_count; field_index += 1)
            {
                u32 field_id = 0, operand_local = 0;
                valid = buster_a64_semantic_operand_field_index(operand.id, field_index, &field_id) && buster_a64_simd_field_local(form, field_id, &operand_local);
                operand_field = operand_field || (valid && operand_local == local);
            }
            u64 bit = valid ? UINT64_C(1) << local : 0;
            valid = valid && operand_field && (seen & bit) == 0 && *total_width <= 32u - (u32)field.width;
            if (valid)
            {
                seen |= bit;
                locals[index] = local;
                widths[index] = field.width;
                *total_width += field.width;
            }
        }
        if (valid)
        {
            *part_count = transform.part_count;
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool buster_a64_complex_simd_assign_concat_operand_fields(BusterA64SemanticForm form, BusterA64SemanticOperand operand, u32 raw,
                                                                              u32* fields, u64* assigned)
{
    bool valid = fields && assigned;
    bool has_concat = false;
    u32 locals[8] = {0}, part_count = 0, total_width = 0;
    u8 widths[8] = {0};
    if (valid)
    {
        valid = buster_a64_complex_simd_operand_concat_parts(form, operand, &has_concat, locals, widths, &part_count, &total_width) &&
                has_concat && part_count > 0u && part_count <= BUSTER_ARRAY_LENGTH(widths) &&
                total_width > 0u && total_width <= 32u &&
                (total_width == 32u || raw < (UINT32_C(1) << total_width));
    }
    u32 remaining_width = valid ? total_width : 0;
    for (u32 index = 0; valid && index < part_count; index += 1)
    {
        u32 width = widths[index];
        if (width == 0u || width > 32u || width > remaining_width)
        {
            valid = false;
        }
        else
        {
            remaining_width -= width;
            if (remaining_width >= 32u)
            {
                valid = false;
            }
            else
            {
                u32 mask = width == 32u ? UINT32_MAX : (UINT32_C(1) << width) - 1;
                u32 part = (raw >> remaining_width) & mask;
                valid = buster_a64_simd_assign_field(form, locals[index], part, fields, assigned);
            }
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool buster_a64_complex_simd_decode_concat_operand_number(BusterA64SemanticForm form, BusterA64SemanticOperand operand,
                                                                              BusterA64SemanticVMFields const* fields, bool* has_concat, u32* number)
{
    u32 locals[8] = {0}, part_count = 0, total_width = 0;
    u8 widths[8] = {0};
    bool valid = fields && has_concat && number &&
                 buster_a64_complex_simd_operand_concat_parts(form, operand, has_concat, locals, widths, &part_count, &total_width);
    if (valid && *has_concat)
    {
        u32 decoded = 0;
        for (u32 index = 0; valid && index < part_count; index += 1)
        {
            u32 width = widths[index];
            u32 mask = width == 32 ? UINT32_MAX : (UINT32_C(1) << width) - 1;
            u32 part = fields->values[locals[index]];
            valid = (part & ~mask) == 0 && (width != 32 || index == 0);
            if (valid && width == 32)
            {
                decoded = part;
            }
            else if (valid)
            {
                decoded = (decoded << width) | part;
            }
        }
        if (valid)
        {
            *number = decoded;
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool buster_a64_complex_simd_assign_operand_fields(BusterA64SemanticForm form, BusterA64SemanticOperand operand,
                                                                       BusterA64SemanticVMValue value, u32* fields, u64* assigned)
{
    u64 payload = 0;
    u32 offset = 0;
    bool is_list = value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST;
    bool payload_ok = buster_a64_simd_value_uint(value, true, &payload);
    bool offset_ok = buster_a64_simd_member_offset(operand.symbol, &offset);
    bool valid = payload_ok && payload <= 31 && offset_ok && (is_list || payload >= offset) && (!is_list || offset < value.aux2);
    u32 raw = is_list ? (u32)payload : (u32)((payload - offset) & 31u);
    bool simd_register = value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_REGISTER ||
                         value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR ||
                         value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR ||
                         value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE;
    bool has_concat = false;
    u32 concat_locals[8] = {0}, concat_count = 0, concat_width = 0;
    u8 concat_widths[8] = {0};
    if (valid && !is_list && simd_register)
    {
        valid = buster_a64_complex_simd_operand_concat_parts(form, operand, &has_concat, concat_locals, concat_widths, &concat_count, &concat_width);
    }
    if (valid && has_concat)
    {
        valid = buster_a64_complex_simd_assign_concat_operand_fields(form, operand, raw, fields, assigned);
    }
    else
    {
        for (u32 index = 0; valid && index < operand.field_index_count; index += 1)
        {
            u32 field_id = 0, local = 0;
            valid = buster_a64_semantic_operand_field_index(operand.id, index, &field_id) && buster_a64_simd_field_local(form, field_id, &local) &&
                    buster_a64_simd_assign_field(form, local, raw, fields, assigned);
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool buster_a64_complex_simd_register_value_ok(u32 row_index, BusterA64SemanticForm form, u32 operand_index,
                                                                   BusterA64SemanticOperand operand, BusterA64SemanticVMValue value)
{
    bool valid = value.payload <= 31;
    bool index_vector = buster_a64_complex_simd_operand_has_kind(operand, BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE) &&
                        (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_INDEX_REGISTER) != 0 &&
                        (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LANE_INDEX) != 0 && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_VECTOR) != 0;
    if (buster_a64_complex_simd_operand_has_kind(operand, BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST) ||
        (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LIST_MEMBER) != 0)
    {
        valid = valid && value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST;
    }
    else if (index_vector)
    {
        /* TBL/TBX's presentation `simd_lane` kind is a metadata error: Vm is
         * an index vector and the encoding has no immediate lane selector. */
        valid = valid && value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR;
    }
    else if (buster_a64_complex_simd_operand_has_kind(operand, BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE))
    {
        valid = valid && value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE;
    }
    else
    {
        valid = valid && (value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_REGISTER || value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR ||
                          value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR);
    }
    valid = valid && ((operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_SCALAR) == 0 || value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR);
    valid = valid && ((operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_VECTOR) == 0 || (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_SCALAR) != 0 ||
                      value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR || value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST || index_vector ||
                      value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE);
    BusterA64SIMDArrangement arrangement = (BusterA64SIMDArrangement)value.aux;
    valid = valid && (value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR || buster_a64_simd_arrangement_is_scalar(arrangement));
    BusterA64SIMDGeneratedArrangementBinding const* binding =
        valid ? buster_a64_simd_catalog_binding(&buster_a64_complex_simd_catalog, row_index, operand_index) : 0;
    bool scalar_aux_binding = binding && binding->selector_index != BUSTER_A64_SIMD_BINDING_NONE;
    bool scalar_aux_targeted = row_index == 14 && form.id == 209 && operand_index == 2;
    bool scalar_aux_vector = value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_VECTOR) != 0 &&
                             (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_SCALAR) == 0 && (scalar_aux_binding || scalar_aux_targeted) &&
                             buster_a64_simd_arrangement_is_scalar(arrangement) && (value.width == 64 || value.width == 128);
    valid = valid && ((value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR && value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST &&
                       value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE) ||
                      buster_a64_simd_arrangement_is_vector(arrangement) || scalar_aux_vector);
    return valid;
}

BUSTER_GLOBAL_LOCAL bool buster_a64_complex_simd_assign_scalar_operand(BusterA64SemanticForm form, BusterA64SemanticOperand operand,
                                                                       BusterA64SemanticVMValue value, u32* fields, u64* assigned)
{
    u64 raw = 0;
    bool valid = buster_a64_simd_value_uint(value, true, &raw) && raw <= UINT32_MAX && operand.field_index_count != 0;
    for (u32 index = 0; valid && index < operand.field_index_count; index += 1)
    {
        u32 field_id = 0, local = 0;
        valid = buster_a64_semantic_operand_field_index(operand.id, index, &field_id) && buster_a64_simd_field_local(form, field_id, &local) &&
                buster_a64_simd_assign_field(form, local, (u32)raw, fields, assigned);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL BusterA64SIMDStatus buster_a64_complex_simd_typed_operands_match(Target target, BusterA64ComplexSIMDInstruction const* instruction,
                                                                                     u32 word);

/* Lane indices in the generated semantic snapshot can be transform
 * programs rather than a directly invertible value table. Search only the
 * small encoded field set attached to that operand, preserving fields already
 * selected by its arrangement and register operands. */
BUSTER_GLOBAL_LOCAL bool buster_a64_complex_simd_inverse_lane_transform(Target target, u32 row_index, BusterA64SemanticForm form,
                                                                        BusterA64SemanticOperand operand, BusterA64SemanticVMValue desired,
                                                                        BusterA64SemanticVMValue const* values, u32* fields, u64* assigned)
{
    u64 desired_value = 0;
    bool valid = fields && assigned && desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INTEGER_IMMEDIATE && operand.field_index_count != 0 &&
                 operand.field_index_count <= 8 && operand.transform_count != 0 && form.field_count <= 64 &&
                 buster_a64_simd_value_uint(desired, true, &desired_value) && desired_value <= UINT32_MAX;

    u32 transform_id = operand.transform_first + operand.transform_count - 1;
    u32 local_fields[8] = {0};
    u8 field_widths[8] = {0};
    u64 seen = 0;
    u32 free_bits = 0;
    for (u32 index = 0; valid && index < operand.field_index_count; index += 1)
    {
        u32 field_id = 0, local = 0;
        BusterA64SemanticField field = {0};
        valid = buster_a64_semantic_operand_field_index(operand.id, index, &field_id) && buster_a64_simd_field_local(form, field_id, &local) && local < 64 &&
                buster_a64_semantic_field(field_id, &field) && field.width != 0 && field.width <= 32;
        u64 bit = valid ? UINT64_C(1) << local : 0;
        valid = valid && (seen & bit) == 0;
        if (valid)
        {
            seen |= bit;
            local_fields[index] = local;
            field_widths[index] = field.width;
            if ((*assigned & bit) == 0)
            {
                valid = field.width <= 8u && free_bits <= 8u - (u32)field.width;
                free_bits += valid ? field.width : 0u;
            }
        }
    }

    /* Lane transforms can leave encoded fields unassigned, and those bits
     * can produce duplicate inverse matches. Keep the complete instruction
     * for row decoding. Only a candidate whose fixed selectors, arrangement,
     * registers, lane and scalar/vector values all round trip is eligible.
     * Free-bit combinations are visited from zero upward. In form 283,
     * field 6 can remain unassigned by the requested selectors, registers and
     * rotate operand; equivalent values are legal only after the complete
     * typed row decode above matches, and the zero-first value is canonical. */
    bool verify_lane_candidate = form.id == 283u || form.id == 492u || form.id == 496u;
    BusterA64ComplexSIMDInstruction requested = {.row_index = row_index};
    if (valid && verify_lane_candidate)
    {
        valid = values && form.operand_count <= BUSTER_A64_COMPLEX_SIMD_MAX_OPERANDS;
        if (valid)
        {
            requested.operand_count = (u8)form.operand_count;
            for (u32 index = 0; index < form.operand_count; index += 1)
            {
                requested.operands[index] = values[index];
            }
        }
    }

    u32 combinations = UINT32_C(1) << free_bits;
    u32 selected[64] = {0};
    u32 match_count = 0;
    for (u32 combination = 0; valid && combination < combinations; combination += 1)
    {
        u32 candidate_values[64] = {0};
        for (u32 index = 0; index < form.field_count; index += 1)
        {
            candidate_values[index] = fields[index];
        }
        u32 remaining = combination;
        for (u32 index = 0; index < operand.field_index_count; index += 1)
        {
            u32 local = local_fields[index];
            if ((*assigned & (UINT64_C(1) << local)) == 0)
            {
                u32 width = field_widths[index];
                u32 mask = width == 32 ? UINT32_MAX : (UINT32_C(1) << width) - 1;
                candidate_values[local] = remaining & mask;
                remaining >>= width;
            }
        }
        BusterA64SemanticVMFields candidate_fields = {.count = form.field_count};
        for (u32 index = 0; index < form.field_count; index += 1)
        {
            candidate_fields.values[index] = candidate_values[index];
        }
        BusterA64SemanticVMValue transformed = buster_a64_semantic_vm_value_invalid();
        u64 transformed_value = 0;
        bool matched = buster_a64_semantic_vm_eval_transform(form.id, transform_id, &candidate_fields, &transformed) == BUSTER_A64_SEMANTIC_VM_STATUS_OK &&
                       transformed.kind != BUSTER_A64_SEMANTIC_VM_VALUE_INVALID && buster_a64_simd_value_uint(transformed, true, &transformed_value) &&
                       transformed_value == desired_value;
        if (matched && verify_lane_candidate)
        {
            /* Require the full selector, arrangement, register, lane and
             * scalar/vector operands to round trip before accepting this inverse. */
            u32 candidate_word = 0;
            matched = buster_a64_semantic_vm_encode_fields(form.id, &candidate_fields, &candidate_word) == BUSTER_A64_SEMANTIC_VM_STATUS_OK &&
                      buster_a64_complex_simd_typed_operands_match(target, &requested, candidate_word) == BUSTER_A64_SIMD_STATUS_OK;
        }
        if (matched)
        {
            if (match_count == 0)
            {
                for (u32 index = 0; index < form.field_count; index += 1)
                {
                    selected[index] = candidate_values[index];
                }
            }
            else if (!verify_lane_candidate)
            {
                valid = false;
            }
            match_count += 1;
        }
    }
    valid = valid && match_count != 0 && (verify_lane_candidate || match_count == 1);
    for (u32 index = 0; valid && index < operand.field_index_count; index += 1)
    {
        u32 local = local_fields[index];
        valid = buster_a64_simd_assign_field(form, local, selected[local], fields, assigned);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool buster_a64_complex_simd_encode_transform_operand(BusterA64SemanticForm form, BusterA64SemanticOperand operand,
                                                                          BusterA64SemanticVMValue value, u32* fields, u64* assigned)
{
    /* Transforms own the representational mapping for immediate/shift/rotate
     * values.  Inverse-table lookup is also used for these scalar kinds; a
     * direct field assignment is reserved for operands with no transform. */
    bool result;
    if (operand.transform_count != 0)
    {
        result = buster_a64_simd_inverse_tables(form, operand, value, true, fields, assigned);
    }
    else
    {
        result = buster_a64_complex_simd_assign_scalar_operand(form, operand, value, fields, assigned);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool buster_a64_complex_simd_assign_zero_constant(BusterA64SemanticForm form, BusterA64SemanticOperand operand, u32* fields, u64* assigned)
{
    return buster_a64_complex_simd_assign_scalar_operand(form, operand,
                                                         buster_a64_semantic_vm_value_unsigned(0, 32), fields, assigned);
}

BUSTER_GLOBAL_LOCAL bool buster_a64_complex_simd_encode_register_operand(u32 row_index, BusterA64SemanticForm form, u32 operand_index,
                                                                         BusterA64SemanticOperand operand, BusterA64SemanticVMValue const* values,
                                                                         BusterA64SemanticVMValue value, u32* fields, u64* assigned)
{
    return buster_a64_complex_simd_register_value_ok(row_index, form, operand_index, operand, value) &&
           buster_a64_simd_catalog_register_arrangement_ok(&buster_a64_complex_simd_catalog, row_index, form, operand_index, values, value) &&
           buster_a64_complex_simd_assign_operand_fields(form, operand, value, fields, assigned);
}

BUSTER_GLOBAL_LOCAL bool buster_a64_complex_simd_encode_operand(Target target, u32 row_index, BusterA64SemanticForm form, u32 operand_index,
                                                                BusterA64SemanticOperand operand, BusterA64SemanticVMValue const* values,
                                                                BusterA64SemanticVMValue value, u32* fields, u64* assigned)
{
    bool valid = value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_INVALID;
    if (valid && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LANE_INDEX) != 0 &&
        (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_INDEX_REGISTER) == 0)
    {
        operand.kind = BUSTER_A64_SEMANTIC_OPERAND_INTEGER_IMMEDIATE;
        valid = buster_a64_complex_simd_inverse_lane_transform(target, row_index, form, operand, value, values,
                                                                fields, assigned);
    }
    else if (valid && buster_a64_complex_simd_operand_is_arrangement_selector(operand))
    {
        valid = buster_a64_simd_inverse_tables(form, operand, value, true, fields, assigned);
    }
    else if (valid && buster_a64_complex_simd_operand_has_register_kind(operand))
    {
        valid = buster_a64_complex_simd_encode_register_operand(row_index, form, operand_index, operand, values, value, fields, assigned);
    }
    else if (valid)
    {
        switch (operand.kind)
        {
        case BUSTER_A64_SEMANTIC_OPERAND_SIMD_ARRANGEMENT:
        case BUSTER_A64_SEMANTIC_OPERAND_SIMD_WIDTH_SELECTOR:
        case BUSTER_A64_SEMANTIC_OPERAND_SIMD_PREFIX_SELECTOR:
            valid = buster_a64_simd_inverse_tables(form, operand, value, true, fields, assigned);
            break;
        case BUSTER_A64_SEMANTIC_OPERAND_SIMD_REGISTER:
        case BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST:
        case BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE:
            valid = buster_a64_complex_simd_encode_register_operand(row_index, form, operand_index, operand, values, value, fields, assigned);
            break;
        case BUSTER_A64_SEMANTIC_OPERAND_GPR_REGISTER:
            valid = value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER && value.payload <= 31 &&
                    (value.flags & ~(BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR | BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_SP)) == 0 &&
                    ((value.flags & BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR) == 0 || (operand.flags & BUSTER_A64_SEMANTIC_FLAG_ZR_ALLOWED) != 0) &&
                    buster_a64_complex_simd_assign_operand_fields(form, operand, value, fields, assigned);
            break;
        case BUSTER_A64_SEMANTIC_OPERAND_GPR_WIDTH_SELECTOR:
            valid = (value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION || value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_UNSIGNED_INTEGER ||
                     value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INTEGER_IMMEDIATE) &&
                    buster_a64_simd_inverse_tables(form, operand, value, true, fields, assigned);
            break;
        case BUSTER_A64_SEMANTIC_OPERAND_INTEGER_IMMEDIATE:
        case BUSTER_A64_SEMANTIC_OPERAND_FP_IMMEDIATE:
        case BUSTER_A64_SEMANTIC_OPERAND_NZCV_FLAGS:
        case BUSTER_A64_SEMANTIC_OPERAND_SHIFT:
        case BUSTER_A64_SEMANTIC_OPERAND_ROTATE:
            valid = buster_a64_complex_simd_encode_transform_operand(form, operand, value, fields, assigned);
            break;
        case BUSTER_A64_SEMANTIC_OPERAND_CONDITION:
            valid = value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_CONDITION && value.payload <= 15 &&
                    buster_a64_complex_simd_assign_operand_fields(form, operand, value, fields, assigned);
            break;
        case BUSTER_A64_SEMANTIC_OPERAND_FIXED_CONSTANT:
        {
            u64 constant = 0;
            valid = operand.symbol.length == 1 && buster_a64_semantic_string_byte(operand.symbol, 0) == '2' &&
                    buster_a64_simd_value_uint(value, true, &constant) && constant == 2;
        }
        break;
        case BUSTER_A64_SEMANTIC_OPERAND_OTHER:
            valid = buster_a64_complex_simd_assign_zero_constant(form, operand, fields, assigned);
            break;
        default:
            valid = false;
            break;
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool buster_a64_complex_simd_decode_register(u32 row_index, BusterA64SemanticForm form, u32 operand_index, BusterA64SemanticOperand operand,
                                                                 BusterA64SemanticVMFields const* fields, BusterA64SemanticVMValue const* values,
                                                                 BusterA64SemanticVMValue* result)
{
    u32 field_id = 0, local = 0, offset = 0;
    bool valid = fields && values && result && operand.field_index_count != 0 && buster_a64_semantic_operand_field_index(operand.id, 0, &field_id) &&
                 buster_a64_simd_field_local(form, field_id, &local) && buster_a64_simd_member_offset(operand.symbol, &offset);
    bool is_list = buster_a64_complex_simd_operand_has_kind(operand, BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST) ||
                   (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LIST_MEMBER) != 0;
    u32 number = valid ? fields->values[local] : 0;
    if (valid && !is_list)
    {
        bool has_concat = false;
        u32 concatenated = 0;
        valid = buster_a64_complex_simd_decode_concat_operand_number(form, operand, fields, &has_concat, &concatenated);
        if (valid && has_concat)
        {
            number = concatenated;
        }
        number = (number + offset) & 31u;
    }
    BusterA64SIMDArrangement arrangement = BUSTER_A64_SIMD_ARRANGEMENT_INVALID;
    bool arrangement_selected = false;
    BusterA64SIMDGeneratedArrangementBinding const* binding =
        valid ? buster_a64_simd_catalog_binding(&buster_a64_complex_simd_catalog, row_index, operand_index) : 0;
    if (binding && binding->selector_index != BUSTER_A64_SIMD_BINDING_NONE)
    {
        valid = binding->selector_index < form.operand_count && values[binding->selector_index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT;
        if (valid)
        {
            arrangement = (BusterA64SIMDArrangement)values[binding->selector_index].aux;
            valid = arrangement != BUSTER_A64_SIMD_ARRANGEMENT_INVALID && arrangement < BUSTER_A64_SIMD_ARRANGEMENT_COUNT;
        }
        arrangement_selected = valid;
    }
    if (valid && !arrangement_selected)
    {
        arrangement_selected = buster_a64_simd_infer_assembly_arrangement(form, operand_index, operand, true, &arrangement);
    }
    if (valid && !arrangement_selected && row_index == 14 && form.id == 209 && operand_index == 2 &&
        values[3].kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT)
    {
        arrangement = (BusterA64SIMDArrangement)values[3].aux;
        arrangement_selected = arrangement != BUSTER_A64_SIMD_ARRANGEMENT_INVALID && arrangement < BUSTER_A64_SIMD_ARRANGEMENT_COUNT;
    }
    if (valid && !arrangement_selected && !buster_a64_simd_infer_scalar(operand.symbol, &arrangement))
    {
        arrangement = BUSTER_A64_SIMD_ARRANGEMENT_Q;
    }
    bool scalar = (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_SCALAR) != 0;
    bool vector = !scalar && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_VECTOR) != 0;
    if (!scalar && !vector && arrangement_selected && buster_a64_simd_arrangement_is_scalar(arrangement))
    {
        scalar = true;
    }
    if (!scalar && !vector && !arrangement_selected && operand.symbol.length > 1)
    {
        char8 first = buster_a64_semantic_string_byte(operand.symbol, 1);
        scalar = first == 'd' || first == 'n' || first == 'm' || first == 'a' || first == 'b' || first == 'H' || first == 'S' || first == 'D';
    }
    if (valid && is_list)
    {
        u32 count = offset + 1;
        for (u32 index = 0; valid && index < form.operand_count; index += 1)
        {
            BusterA64SemanticOperand candidate = {0};
            u32 candidate_offset = 0;
            valid = buster_a64_semantic_operand(form.operand_first + index, &candidate) && buster_a64_simd_member_offset(candidate.symbol, &candidate_offset);
            if (valid && buster_a64_complex_simd_operand_has_kind(candidate, BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST) && candidate_offset + 1 > count)
            {
                count = candidate_offset + 1;
            }
        }
        if (valid)
        {
            *result = buster_a64_simd_value_list(number, count, arrangement);
        }
    }
    else if (valid && buster_a64_complex_simd_operand_has_kind(operand, BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE) &&
             (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_INDEX_REGISTER) != 0 &&
             (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LANE_INDEX) != 0 && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_VECTOR) != 0)
    {
        /* TBL/TBX Vm is an index vector; the presentation metadata calls it
         * simd_lane but there is no encoded element lane to recover. */
        *result = buster_a64_simd_value_vector(number, arrangement);
    }
    else if (valid && buster_a64_complex_simd_operand_has_kind(operand, BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE))
    {
        *result = buster_a64_simd_value_lane(number, arrangement, 0);
    }
    else if (valid && vector && buster_a64_simd_arrangement_is_scalar(arrangement))
    {
        u8 width = buster_a64_complex_simd_scalar_aux_vector_width(row_index, form, operand_index, values);
        valid = width != 0;
        if (valid)
        {
            *result = (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR,
                                                 .width = width,
                                                 .aux = (u32)arrangement,
                                                 .payload = number};
        }
    }
    else if (valid)
    {
        *result = buster_a64_simd_value_register(number, arrangement, scalar);
    }
    return valid && result->kind != BUSTER_A64_SEMANTIC_VM_VALUE_INVALID;
}

BUSTER_GLOBAL_LOCAL bool buster_a64_complex_simd_decode_scalar_operand(BusterA64SemanticForm form, BusterA64SemanticOperand operand,
                                                                       BusterA64SemanticVMFields const* fields, BusterA64SemanticVMValue* result)
{
    u32 field_id = 0, local = 0;
    bool valid = fields && result && operand.field_index_count != 0 && buster_a64_semantic_operand_field_index(operand.id, 0, &field_id) &&
                 buster_a64_simd_field_local(form, field_id, &local);
    u32 raw = valid ? fields->values[local] : 0;
    if (valid && operand.transform_count != 0)
    {
        u32 transform_id = operand.transform_first + operand.transform_count - 1;
        BusterA64SemanticVMValue transformed = buster_a64_semantic_vm_value_invalid();
        valid = buster_a64_semantic_vm_eval_transform(form.id, transform_id, fields, &transformed) == BUSTER_A64_SEMANTIC_VM_STATUS_OK &&
                transformed.kind != BUSTER_A64_SEMANTIC_VM_VALUE_INVALID;
        raw = valid ? (u32)transformed.payload : raw;
    }
    if (valid && operand.kind == BUSTER_A64_SEMANTIC_OPERAND_NZCV_FLAGS)
    {
        *result = (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_NZCV, .width = 4, .payload = raw};
    }
    else if (valid && operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SHIFT)
    {
        *result = (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_SHIFT, .width = 8, .payload = raw};
    }
    else if (valid && operand.kind == BUSTER_A64_SEMANTIC_OPERAND_ROTATE)
    {
        *result = (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_ROTATE, .width = 8, .payload = raw};
    }
    else if (valid)
    {
        *result = (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_INTEGER_IMMEDIATE, .width = 32, .payload = raw};
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL BusterA64SIMDStatus buster_a64_complex_simd_decode_internal(Target target, u32 requested_row, bool row_requested, u32 word,
                                                                                BusterA64ComplexSIMDResult* result);

BUSTER_GLOBAL_LOCAL bool buster_a64_complex_simd_typed_value_equal(BusterA64SemanticVMValue left, BusterA64SemanticVMValue right)
{
    bool equal = left.kind == right.kind && left.width == right.width && left.flags == right.flags && left.aux == right.aux && left.aux2 == right.aux2 &&
                 left.payload == right.payload && left.mask == right.mask && left.text.length == right.text.length;
    for (u32 index = 0; equal && index < left.text.length; index += 1)
    {
        equal = buster_a64_semantic_string_byte(left.text, index) == buster_a64_semantic_string_byte(right.text, index);
    }
    return equal;
}

BUSTER_GLOBAL_LOCAL BusterA64SIMDStatus buster_a64_complex_simd_typed_operands_match(Target target, BusterA64ComplexSIMDInstruction const* instruction,
                                                                                     u32 word)
{
    BusterA64SIMDStatus status = instruction ? BUSTER_A64_SIMD_STATUS_OK : BUSTER_A64_SIMD_STATUS_INVALID_ARGUMENT;
    BusterA64ComplexSIMDResult decoded = {0};
    if (status == BUSTER_A64_SIMD_STATUS_OK)
    {
        status = buster_a64_complex_simd_decode_internal(target, instruction->row_index, true, word, &decoded);
    }
    if (status == BUSTER_A64_SIMD_STATUS_OK && decoded.operand_count != instruction->operand_count)
    {
        status = BUSTER_A64_SIMD_STATUS_TARGET_MISMATCH;
    }
    for (u32 index = 0; status == BUSTER_A64_SIMD_STATUS_OK && index < instruction->operand_count; index += 1)
    {
        if (!buster_a64_complex_simd_typed_value_equal(decoded.operands[index], instruction->operands[index]))
        {
            status = BUSTER_A64_SIMD_STATUS_RANGE;
        }
    }
    return status;
}

/* Shared tail of the raw and typed encoders: encode the field values, require
 * the canonical decoder to land on this row, prove the typed operands round
 * trip, and only then publish the word. */
BUSTER_GLOBAL_LOCAL BusterA64SIMDStatus buster_a64_complex_simd_encode_fields(Target target, BusterA64ComplexSIMDInstruction const* instruction,
                                                                              BusterA64SemanticForm form, BusterA64SIMDGeneratedRow const* row,
                                                                              BusterA64SemanticVMFields const* fields, u32* word)
{
    u32 candidate = 0;
    BusterA64SIMDStatus status = buster_a64_simd_vm_status(buster_a64_semantic_vm_encode_fields(form.id, fields, &candidate));
    if (status == BUSTER_A64_SIMD_STATUS_OK)
    {
        BusterAarch64CanonicalDecodeResult canonical = {0};
        status = buster_a64_simd_canonical_status(buster_aarch64_canonical_decode(target, candidate, &canonical));
        if (status == BUSTER_A64_SIMD_STATUS_OK && canonical.arm_row_digest != row->source_digest)
        {
            status = BUSTER_A64_SIMD_STATUS_TARGET_MISMATCH;
        }
    }
    if (status == BUSTER_A64_SIMD_STATUS_OK)
    {
        status = buster_a64_complex_simd_typed_operands_match(target, instruction, candidate);
    }
    if (status == BUSTER_A64_SIMD_STATUS_OK)
    {
        *word = candidate;
    }
    return status;
}

BUSTER_GLOBAL_LOCAL BusterA64SIMDStatus buster_a64_complex_simd_encode_raw(Target target, BusterA64ComplexSIMDInstruction const* instruction,
                                                                           BusterA64SemanticForm form, BusterA64SIMDGeneratedRow const* row, u32* word)
{
    BusterA64SIMDStatus status;
    if (!instruction || !row || !word || !instruction->raw_fields_valid || instruction->raw_fields.count != form.field_count || form.field_count > 64)
    {
        status = BUSTER_A64_SIMD_STATUS_RANGE;
    }
    else
    {
        status = buster_a64_complex_simd_encode_fields(target, instruction, form, row, &instruction->raw_fields, word);
    }
    return status;
}

BUSTER_GLOBAL_LOCAL BusterA64SIMDStatus buster_a64_complex_simd_encode_typed(Target target, BusterA64ComplexSIMDInstruction const* instruction,
                                                                             BusterA64SemanticForm form, BusterA64SIMDGeneratedRow const* row, u32* word)
{
    BusterA64SIMDStatus status = BUSTER_A64_SIMD_STATUS_OK;
    u32 fields[64] = {0};
    u64 assigned = 0;
    /* Form 283's lane transform shares fields with later selectors and
     * rotate values. Encode its non-lane operands first so the inverse sees
     * those fixed bits before testing a lane candidate. */
    u32 encode_pass_count = form.id == 283u ? 2 : 1;
    for (u32 pass = 0; status == BUSTER_A64_SIMD_STATUS_OK && pass < encode_pass_count; pass += 1)
    {
        for (u32 index = 0; status == BUSTER_A64_SIMD_STATUS_OK && index < form.operand_count; index += 1)
        {
            BusterA64SemanticOperand operand = {0};
            if (!buster_a64_semantic_operand(form.operand_first + index, &operand))
            {
                status = BUSTER_A64_SIMD_STATUS_BOUNDS;
            }
            else
            {
                bool lane_index = (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LANE_INDEX) != 0 &&
                    (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_INDEX_REGISTER) == 0;
                bool deferred = form.id == 283u && ((pass == 0 && lane_index) || (pass == 1 && !lane_index));
                if (!deferred && !buster_a64_complex_simd_encode_operand(target, instruction->row_index, form, index, operand,
                                                                         instruction->operands, instruction->operands[index], fields, &assigned))
                {
                    status = BUSTER_A64_SIMD_STATUS_RANGE;
                }
            }
        }
    }
    if (status == BUSTER_A64_SIMD_STATUS_OK)
    {
        BusterA64SemanticVMFields vm_fields = {.count = form.field_count};
        for (u32 index = 0; index < form.field_count; index += 1)
        {
            vm_fields.values[index] = fields[index];
        }
        status = buster_a64_complex_simd_encode_fields(target, instruction, form, row, &vm_fields, word);
    }
    return status;
}

BusterA64SIMDStatus buster_a64_complex_simd_encode(Target target, BusterA64ComplexSIMDInstruction const* instruction, u32* word)
{
    BusterA64SIMDStatus status;
    BusterA64SIMDGeneratedRow const* row = 0;
    BusterA64SemanticForm form = {0};
    if (!instruction || !word || instruction->operand_count > BUSTER_A64_COMPLEX_SIMD_MAX_OPERANDS)
    {
        status = BUSTER_A64_SIMD_STATUS_INVALID_ARGUMENT;
    }
    else if (!buster_a64_simd_catalog_row(&buster_a64_complex_simd_catalog, instruction->row_index, &row))
    {
        status = BUSTER_A64_SIMD_STATUS_BOUNDS;
    }
    else if (!row->executable)
    {
        status = BUSTER_A64_SIMD_STATUS_UNSUPPORTED;
    }
    else if (!buster_a64_semantic_form(row->semantic_form_id, &form) || form.operand_count != instruction->operand_count ||
             form.operand_count > BUSTER_A64_COMPLEX_SIMD_MAX_OPERANDS)
    {
        status = BUSTER_A64_SIMD_STATUS_INVALID_ARGUMENT;
    }
    else if (instruction->raw_fields_valid)
    {
        status = buster_a64_complex_simd_encode_raw(target, instruction, form, row, word);
    }
    else
    {
        status = buster_a64_complex_simd_encode_typed(target, instruction, form, row, word);
    }
    return status;
}

BUSTER_GLOBAL_LOCAL BusterA64SIMDStatus buster_a64_complex_simd_decode_internal(Target target, u32 requested_row, bool row_requested, u32 word,
                                                                                BusterA64ComplexSIMDResult* result)
{
    BusterA64SIMDStatus status = result ? BUSTER_A64_SIMD_STATUS_OK : BUSTER_A64_SIMD_STATUS_INVALID_ARGUMENT;
    BusterAarch64CanonicalDecodeResult canonical = {0};
    if (status == BUSTER_A64_SIMD_STATUS_OK)
    {
        status = buster_a64_simd_canonical_status(buster_aarch64_canonical_decode(target, word, &canonical));
    }
    u32 row_index = 0;
    if (status == BUSTER_A64_SIMD_STATUS_OK &&
        (!buster_a64_simd_catalog_find_source_digest(&buster_a64_complex_simd_catalog, canonical.arm_row_digest, &row_index) ||
         (row_requested && row_index != requested_row)))
    {
        status = BUSTER_A64_SIMD_STATUS_TARGET_MISMATCH;
    }
    BusterA64SIMDGeneratedRow const* row = 0;
    BusterA64SemanticForm form = {0};
    if (status == BUSTER_A64_SIMD_STATUS_OK &&
        (!buster_a64_simd_catalog_row(&buster_a64_complex_simd_catalog, row_index, &row) || !row->executable ||
         !buster_a64_semantic_form(row->semantic_form_id, &form)))
    {
        status = BUSTER_A64_SIMD_STATUS_UNSUPPORTED;
    }
    BusterA64SemanticVMResult decoded = {0};
    if (status == BUSTER_A64_SIMD_STATUS_OK)
    {
        status = buster_a64_simd_vm_status(buster_a64_semantic_vm_decode_fields(form.id, word, &decoded));
    }
    BusterA64SemanticVMValue values[BUSTER_A64_COMPLEX_SIMD_MAX_OPERANDS] = {0};
    for (u32 index = 0; status == BUSTER_A64_SIMD_STATUS_OK && index < form.operand_count; index += 1)
    {
        BusterA64SemanticOperand operand = {0};
        if (!buster_a64_semantic_operand(form.operand_first + index, &operand))
        {
            status = BUSTER_A64_SIMD_STATUS_BOUNDS;
        }
        else if (buster_a64_complex_simd_operand_is_arrangement_selector(operand) &&
                 !buster_a64_simd_decode_arrangement(form, operand, &decoded.fields, &values[index]))
        {
            status = BUSTER_A64_SIMD_STATUS_RESERVED;
        }
    }
    for (u32 index = 0; status == BUSTER_A64_SIMD_STATUS_OK && index < form.operand_count; index += 1)
    {
        BusterA64SemanticOperand operand = {0};
        if (!buster_a64_semantic_operand(form.operand_first + index, &operand))
        {
            status = BUSTER_A64_SIMD_STATUS_BOUNDS;
        }
        else if (buster_a64_complex_simd_operand_is_arrangement_selector(operand))
        {
            /* Arrangement selectors were evaluated in the first pass. */
        }
        else if (buster_a64_complex_simd_operand_has_register_kind(operand))
        {
            if (!buster_a64_complex_simd_decode_register(row_index, form, index, operand, &decoded.fields, values, &values[index]))
            {
                status = BUSTER_A64_SIMD_STATUS_RANGE;
            }
        }
        else if ((operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LANE_INDEX) != 0 &&
                 (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_INDEX_REGISTER) == 0)
        {
            if (!buster_a64_complex_simd_decode_scalar_operand(form, operand, &decoded.fields, &values[index]))
            {
                status = BUSTER_A64_SIMD_STATUS_RANGE;
            }
        }
        else
        {
            switch (operand.kind)
            {
            case BUSTER_A64_SEMANTIC_OPERAND_SIMD_ARRANGEMENT:
            case BUSTER_A64_SEMANTIC_OPERAND_SIMD_WIDTH_SELECTOR:
            case BUSTER_A64_SEMANTIC_OPERAND_SIMD_PREFIX_SELECTOR:
                /* A selector without the arrangement-selector flag is not
                 * currently emitted by the canonical corpus, but retain the
                 * primary-kind fallback for forward-compatible rows. */
                if (!buster_a64_simd_decode_arrangement(form, operand, &decoded.fields, &values[index]))
                {
                    status = BUSTER_A64_SIMD_STATUS_RESERVED;
                }
                break;
            case BUSTER_A64_SEMANTIC_OPERAND_SIMD_REGISTER:
            case BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST:
            case BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE:
                if (!buster_a64_complex_simd_decode_register(row_index, form, index, operand, &decoded.fields, values, &values[index]))
                {
                    status = BUSTER_A64_SIMD_STATUS_RANGE;
                }
                break;
            case BUSTER_A64_SEMANTIC_OPERAND_GPR_REGISTER:
            {
                u32 field_id = 0, local = 0;
                if (operand.field_index_count == 0 || !buster_a64_semantic_operand_field_index(operand.id, 0, &field_id) ||
                    !buster_a64_simd_field_local(form, field_id, &local))
                {
                    status = BUSTER_A64_SIMD_STATUS_BOUNDS;
                }
                else
                {
                    values[index] =
                        buster_a64_simd_value_gpr(decoded.fields.values[local], (operand.flags & BUSTER_A64_SEMANTIC_FLAG_GPR_WIDTH_X64) != 0 ? 64 : 32,
                                                  decoded.fields.values[local] == 31 && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_ZR_ALLOWED) != 0);
                }
            }
            break;
            case BUSTER_A64_SEMANTIC_OPERAND_GPR_WIDTH_SELECTOR:
            {
                values[index] = buster_a64_semantic_vm_value_invalid();
                bool found = false;
                for (u32 transform_index = 0; !found && transform_index < operand.transform_count; transform_index += 1)
                {
                    BusterA64SemanticVMValue candidate = buster_a64_semantic_vm_value_invalid();
                    if (buster_a64_semantic_vm_eval_transform(form.id, operand.transform_first + transform_index, &decoded.fields, &candidate) ==
                            BUSTER_A64_SEMANTIC_VM_STATUS_OK &&
                        candidate.kind == BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION)
                    {
                        values[index] = candidate;
                        found = true;
                    }
                }
                if (values[index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID)
                {
                    status = BUSTER_A64_SIMD_STATUS_UNSUPPORTED;
                }
            }
            break;
            case BUSTER_A64_SEMANTIC_OPERAND_INTEGER_IMMEDIATE:
            case BUSTER_A64_SEMANTIC_OPERAND_FP_IMMEDIATE:
            case BUSTER_A64_SEMANTIC_OPERAND_NZCV_FLAGS:
            case BUSTER_A64_SEMANTIC_OPERAND_SHIFT:
            case BUSTER_A64_SEMANTIC_OPERAND_ROTATE:
                if (!buster_a64_complex_simd_decode_scalar_operand(form, operand, &decoded.fields, &values[index]))
                {
                    status = BUSTER_A64_SIMD_STATUS_RANGE;
                }
                break;
            case BUSTER_A64_SEMANTIC_OPERAND_OTHER:
                values[index] = buster_a64_semantic_vm_value_unsigned(0, 32);
                break;
            case BUSTER_A64_SEMANTIC_OPERAND_CONDITION:
            {
                u32 field_id = 0, local = 0;
                if (operand.field_index_count == 0 || !buster_a64_semantic_operand_field_index(operand.id, 0, &field_id) ||
                    !buster_a64_simd_field_local(form, field_id, &local))
                {
                    status = BUSTER_A64_SIMD_STATUS_BOUNDS;
                }
                else
                {
                    values[index] = buster_a64_semantic_vm_value_condition(decoded.fields.values[local]);
                }
            }
            break;
            case BUSTER_A64_SEMANTIC_OPERAND_FIXED_CONSTANT:
                values[index] = buster_a64_semantic_vm_value_unsigned(2, 2);
                break;
            default:
                status = BUSTER_A64_SIMD_STATUS_UNSUPPORTED;
                break;
            }
            if (status == BUSTER_A64_SIMD_STATUS_OK && values[index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID)
            {
                status = BUSTER_A64_SIMD_STATUS_RANGE;
            }
        }
    }
    if (status == BUSTER_A64_SIMD_STATUS_OK)
    {
        u32 reencoded = 0;
        BusterA64SemanticVMStatus vm_status = buster_a64_semantic_vm_encode_fields(form.id, &decoded.fields, &reencoded);
        if (vm_status != BUSTER_A64_SEMANTIC_VM_STATUS_OK || reencoded != word)
        {
            status = BUSTER_A64_SIMD_STATUS_TARGET_MISMATCH;
        }
    }
    if (status == BUSTER_A64_SIMD_STATUS_OK)
    {
        BusterA64ComplexSIMDResult candidate = {.status = BUSTER_A64_SIMD_STATUS_OK, .row_index = row_index, .word = word, .operand_count = form.operand_count};
        candidate.raw_fields_valid = 1;
        candidate.raw_fields = decoded.fields;
        for (u32 index = 0; index < form.operand_count; index += 1)
        {
            candidate.operands[index] = values[index];
        }
        *result = candidate;
    }
    return status;
}

BusterA64SIMDStatus buster_a64_complex_simd_decode(Target target, u32 word, BusterA64ComplexSIMDResult* result)
{
    return buster_a64_complex_simd_decode_internal(target, 0, false, word, result);
}

BusterA64SIMDStatus buster_a64_complex_simd_decode_row(Target target, u32 row_index, u32 word, BusterA64ComplexSIMDResult* result)
{
    BusterA64SIMDStatus status;
    if (row_index >= BUSTER_A64_COMPLEX_SIMD_ROW_COUNT)
    {
        status = BUSTER_A64_SIMD_STATUS_BOUNDS;
    }
    else
    {
        status = buster_a64_complex_simd_decode_internal(target, row_index, true, word, result);
    }
    return status;
}

bool buster_a64_complex_simd_validate(void)
{
    bool valid = BUSTER_A64_COMPLEX_SIMD_ROW_COUNT == 345u && BUSTER_A64_COMPLEX_SIMD_TRANSFORM_ROW_COUNT == 206u && BUSTER_A64_COMPLEX_SIMD_MAX_OPERANDS == 8u;
    u32 previous_form = 0;
    for (u32 index = 0; valid && index < BUSTER_A64_COMPLEX_SIMD_ROW_COUNT; index += 1)
    {
        BusterA64SIMDGeneratedRow const* row = 0;
        BusterA64SemanticForm form = {0};
        valid = buster_a64_simd_catalog_row(&buster_a64_complex_simd_catalog, index, &row) && buster_a64_semantic_form(row->semantic_form_id, &form) &&
                form.owner == BUSTER_A64_SEMANTIC_OWNER_COMPLEX_SIMD_FP && form.kind == BUSTER_A64_SEMANTIC_FORM_CANONICAL &&
                form.status == BUSTER_A64_SEMANTIC_STATUS_DEFINED && (index == 0 || row->semantic_form_id > previous_form) &&
                row->operand_count == form.operand_count;
        for (u32 operand_index = 0; valid && operand_index < form.operand_count; operand_index += 1)
        {
            BusterA64SIMDGeneratedArrangementBinding const* binding = buster_a64_simd_catalog_binding(&buster_a64_complex_simd_catalog, index, operand_index);
            if (binding && binding->selector_index != BUSTER_A64_SIMD_BINDING_NONE)
            {
                BusterA64SemanticOperand selector = {0};
                valid = binding->selector_index < form.operand_count && (binding->direction == -1 || binding->direction == 1) &&
                        buster_a64_semantic_operand(form.operand_first + binding->selector_index, &selector) &&
                        buster_a64_complex_simd_operand_has_arrangement_kind(selector) &&
                        (binding->direction != 1 || (u32)binding->selector_index == operand_index + 1) &&
                        (binding->direction != -1 || (operand_index != 0 && (u32)binding->selector_index + 1 == operand_index));
            }
        }
        if (valid)
        {
            previous_form = row->semantic_form_id;
        }
    }
    return valid;
}
