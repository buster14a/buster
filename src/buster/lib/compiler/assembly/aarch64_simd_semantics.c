/*
 * Helpers shared by the AArch64 semantic bindings.
 *
 * Ownership: the arrangement vocabulary and value constructors declared in
 * aarch64_simd_semantics.h, plus the private seam in
 * aarch64_simd_semantics_internal.h used by aarch64_direct_simd_semantics,
 * aarch64_complex_simd_semantics and aarch64_alias_projection.  Bindings pass
 * their generated tables as a BusterA64SIMDCatalog; nothing here dispatches
 * through callbacks.
 *
 * Map:
 * - buster_a64_simd_arrangement_shapes: name, lane count and widths of every
 *   arrangement; buster_a64_simd_arrangement_shape bounds the lookup.
 * - buster_a64_simd_value_*: public value constructors.
 * - buster_a64_simd_catalog_*: generated-row access and selector relations.
 * - buster_a64_simd_semantic_equal* through buster_a64_simd_value_uint:
 *   semantic metadata helpers.
 * - buster_a64_simd_infer_*: arrangement recovery from presentation text.
 * - buster_a64_simd_result_matches, _apply_table_entry, _inverse_tables,
 *   _decode_arrangement: value-table interpreters.
 * - buster_a64_simd_vm_status, _canonical_status: status translation.
 */

#include <buster/lib/compiler/assembly/aarch64_simd_semantics_internal.h>
#include <buster/lib/string.h>

typedef struct BusterA64SIMDArrangementShape BusterA64SIMDArrangementShape;
struct BusterA64SIMDArrangementShape
{
    String8 name;
    u8 lane_count;
    /* Nonzero only for the B/H/S/D scalar element arrangements. */
    u8 scalar_width;
    /* Nonzero only for Q and the vector arrangements. */
    u8 register_width;
};

BUSTER_GLOBAL_LOCAL BusterA64SIMDArrangementShape const buster_a64_simd_arrangement_shapes[BUSTER_A64_SIMD_ARRANGEMENT_COUNT] = {
    [BUSTER_A64_SIMD_ARRANGEMENT_B] = {S8_INITIALIZER("B"), 1, 8, 0},
    [BUSTER_A64_SIMD_ARRANGEMENT_H] = {S8_INITIALIZER("H"), 1, 16, 0},
    [BUSTER_A64_SIMD_ARRANGEMENT_S] = {S8_INITIALIZER("S"), 1, 32, 0},
    [BUSTER_A64_SIMD_ARRANGEMENT_D] = {S8_INITIALIZER("D"), 1, 64, 0},
    [BUSTER_A64_SIMD_ARRANGEMENT_Q] = {S8_INITIALIZER("Q"), 1, 0, 128},
    [BUSTER_A64_SIMD_ARRANGEMENT_1B] = {S8_INITIALIZER("1B"), 1, 0, 8},
    [BUSTER_A64_SIMD_ARRANGEMENT_2B] = {S8_INITIALIZER("2B"), 2, 0, 16},
    [BUSTER_A64_SIMD_ARRANGEMENT_4B] = {S8_INITIALIZER("4B"), 4, 0, 32},
    [BUSTER_A64_SIMD_ARRANGEMENT_8B] = {S8_INITIALIZER("8B"), 8, 0, 64},
    [BUSTER_A64_SIMD_ARRANGEMENT_16B] = {S8_INITIALIZER("16B"), 16, 0, 128},
    [BUSTER_A64_SIMD_ARRANGEMENT_1H] = {S8_INITIALIZER("1H"), 1, 0, 16},
    [BUSTER_A64_SIMD_ARRANGEMENT_2H] = {S8_INITIALIZER("2H"), 2, 0, 32},
    [BUSTER_A64_SIMD_ARRANGEMENT_4H] = {S8_INITIALIZER("4H"), 4, 0, 64},
    [BUSTER_A64_SIMD_ARRANGEMENT_8H] = {S8_INITIALIZER("8H"), 8, 0, 128},
    [BUSTER_A64_SIMD_ARRANGEMENT_1S] = {S8_INITIALIZER("1S"), 1, 0, 32},
    [BUSTER_A64_SIMD_ARRANGEMENT_2S] = {S8_INITIALIZER("2S"), 2, 0, 64},
    [BUSTER_A64_SIMD_ARRANGEMENT_4S] = {S8_INITIALIZER("4S"), 4, 0, 128},
    [BUSTER_A64_SIMD_ARRANGEMENT_1D] = {S8_INITIALIZER("1D"), 1, 0, 64},
    [BUSTER_A64_SIMD_ARRANGEMENT_2D] = {S8_INITIALIZER("2D"), 2, 0, 128},
};

BUSTER_GLOBAL_LOCAL BusterA64SIMDArrangementShape buster_a64_simd_arrangement_shape(BusterA64SIMDArrangement arrangement)
{
    BusterA64SIMDArrangementShape result = {0};
    if ((u32)arrangement < BUSTER_A64_SIMD_ARRANGEMENT_COUNT)
    {
        result = buster_a64_simd_arrangement_shapes[arrangement];
    }

    return result;
}

bool buster_a64_simd_arrangement_from_string(String8 text, BusterA64SIMDArrangement* result)
{
    bool found = false;
    for (u32 index = 1; result && !found && index < BUSTER_A64_SIMD_ARRANGEMENT_COUNT; index += 1)
    {
        if (string_equal(text, buster_a64_simd_arrangement_shapes[index].name))
        {
            *result = (BusterA64SIMDArrangement)index;
            found = true;
        }
    }

    return found;
}

String8 buster_a64_simd_arrangement_string(BusterA64SIMDArrangement arrangement)
{
    return buster_a64_simd_arrangement_shape(arrangement).name;
}

u8 buster_a64_simd_scalar_width(BusterA64SIMDArrangement arrangement)
{
    return buster_a64_simd_arrangement_shape(arrangement).scalar_width;
}

u8 buster_a64_simd_lane_count(BusterA64SIMDArrangement arrangement)
{
    return buster_a64_simd_arrangement_shape(arrangement).lane_count;
}

u8 buster_a64_simd_register_width(BusterA64SIMDArrangement arrangement)
{
    return buster_a64_simd_arrangement_shape(arrangement).register_width;
}

bool buster_a64_simd_arrangement_is_scalar(BusterA64SIMDArrangement arrangement)
{
    return buster_a64_simd_scalar_width(arrangement) != 0;
}

bool buster_a64_simd_arrangement_is_vector(BusterA64SIMDArrangement arrangement)
{
    return buster_a64_simd_register_width(arrangement) != 0;
}

BusterA64SemanticVMValue buster_a64_simd_value_arrangement(BusterA64SIMDArrangement arrangement)
{
    BusterA64SemanticVMValue result = buster_a64_semantic_vm_value_invalid();
    if (arrangement != BUSTER_A64_SIMD_ARRANGEMENT_INVALID && arrangement < BUSTER_A64_SIMD_ARRANGEMENT_COUNT)
    {
        result =
            (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT, .width = 8, .aux = (u32)arrangement, .payload = (u64)arrangement};
    }

    return result;
}

BusterA64SemanticVMValue buster_a64_simd_value_register(u32 number, BusterA64SIMDArrangement arrangement, bool scalar)
{
    BusterA64SemanticVMValue result = buster_a64_semantic_vm_value_invalid();
    u8 width = scalar ? buster_a64_simd_scalar_width(arrangement) : buster_a64_simd_register_width(arrangement);
    if (number <= 31 && width != 0)
    {
        result = (BusterA64SemanticVMValue){.kind = scalar ? BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR : BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR,
                                            .width = width,
                                            .aux = (u32)arrangement,
                                            .payload = number};
    }

    return result;
}

BusterA64SemanticVMValue buster_a64_simd_value_vector(u32 number, BusterA64SIMDArrangement arrangement)
{
    return buster_a64_simd_value_register(number, arrangement, false);
}

BusterA64SemanticVMValue buster_a64_simd_value_scalar(u32 number, BusterA64SIMDArrangement arrangement)
{
    return buster_a64_simd_value_register(number, arrangement, true);
}

BusterA64SemanticVMValue buster_a64_simd_value_list(u32 first, u32 count, BusterA64SIMDArrangement arrangement)
{
    BusterA64SemanticVMValue result = buster_a64_semantic_vm_value_invalid();
    u8 width = buster_a64_simd_register_width(arrangement);
    if (first <= 31 && count != 0 && count <= 4 && width != 0)
    {
        result = (BusterA64SemanticVMValue){
            .kind = BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST, .width = width, .aux = (u32)arrangement, .aux2 = count, .payload = first};
    }

    return result;
}

BusterA64SemanticVMValue buster_a64_simd_value_lane(u32 number, BusterA64SIMDArrangement arrangement, u32 lane)
{
    BusterA64SemanticVMValue result = buster_a64_semantic_vm_value_invalid();
    u8 width = buster_a64_simd_register_width(arrangement);
    u32 count = buster_a64_simd_lane_count(arrangement);
    if (number <= 31 && width != 0 && lane < count)
    {
        result = (BusterA64SemanticVMValue){
            .kind = BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE, .width = width, .aux = (u32)arrangement, .aux2 = lane, .payload = number};
    }

    return result;
}

BusterA64SemanticVMValue buster_a64_simd_value_gpr(u32 number, u8 width, bool zr)
{
    return buster_a64_semantic_vm_value_gpr(number, width, false, zr);
}

BusterA64SemanticVMValue buster_a64_simd_value_gpr_width(u8 width)
{
    return (width == 32 || width == 64) ? buster_a64_semantic_vm_value_unsigned(width, 7) : buster_a64_semantic_vm_value_invalid();
}

BusterA64SemanticVMValue buster_a64_simd_value_immediate(u64 value, u8 width)
{
    BusterA64SemanticVMValue result = buster_a64_semantic_vm_value_unsigned(value, width);
    if (result.kind != BUSTER_A64_SEMANTIC_VM_VALUE_INVALID)
    {
        result.kind = BUSTER_A64_SEMANTIC_VM_VALUE_INTEGER_IMMEDIATE;
    }

    return result;
}

bool buster_a64_simd_catalog_row(BusterA64SIMDCatalog const* catalog, u32 row_index, BusterA64SIMDGeneratedRow const** result)
{
    BusterA64SIMDGeneratedRow const* row = catalog && row_index < catalog->row_count ? catalog->rows + row_index : 0;
    bool valid = result && row && row->row_index == row_index && row->semantic_form_id < buster_a64_semantic_form_count() &&
                 row->operand_count <= BUSTER_A64_SIMD_MAX_OPERANDS;
    if (valid)
    {
        *result = row;
    }

    return valid;
}

bool buster_a64_simd_catalog_row_info(BusterA64SIMDCatalog const* catalog, u32 row_index, BusterA64SIMDRowInfo* result)
{
    BusterA64SIMDGeneratedRow const* row = 0;
    BusterA64SemanticForm form = {0};
    bool valid = result && buster_a64_simd_catalog_row(catalog, row_index, &row) && buster_a64_semantic_form(row->semantic_form_id, &form);
    if (valid)
    {
        *result = (BusterA64SIMDRowInfo){.row_index = row_index,
                                         .semantic_form_id = row->semantic_form_id,
                                         .source_digest = row->source_digest,
                                         .id = {(char8*)catalog->string_pool + row->id_offset, row->id_length},
                                         .assembly = {(char8*)catalog->string_pool + row->assembly_offset, row->assembly_length},
                                         .operand_count = row->operand_count,
                                         .executable = row->executable,
                                         .transform_bearing = (u8)(form.transform_count != 0)};
    }

    return valid;
}

bool buster_a64_simd_catalog_find_source_digest(BusterA64SIMDCatalog const* catalog, u64 source_digest, u32* row_index)
{
    u32 count = 0, selected = 0;
    for (u32 index = 0; catalog && index < catalog->row_count; index += 1)
    {
        if (catalog->rows[index].source_digest == source_digest)
        {
            count += 1;
            selected = index;
        }
    }
    bool result = row_index && count == 1;
    if (result)
    {
        *row_index = selected;
    }

    return result;
}

BusterA64SIMDGeneratedArrangementBinding const* buster_a64_simd_catalog_binding(BusterA64SIMDCatalog const* catalog, u32 row_index, u32 operand_index)
{
    BusterA64SIMDGeneratedArrangementBinding const* result = 0;
    if (catalog && row_index < catalog->row_count && operand_index < BUSTER_A64_SIMD_MAX_OPERANDS)
    {
        result = &catalog->bindings[row_index][operand_index];
    }

    return result;
}

bool buster_a64_simd_catalog_arrangement_binding(BusterA64SIMDCatalog const* catalog, u32 row_index, u32 operand_index, BusterA64SIMDArrangementBinding* result)
{
    BusterA64SIMDGeneratedRow const* row = 0;
    BusterA64SIMDGeneratedArrangementBinding const* binding = buster_a64_simd_catalog_binding(catalog, row_index, operand_index);
    bool valid = result && binding && buster_a64_simd_catalog_row(catalog, row_index, &row) && operand_index < row->operand_count &&
                 binding->selector_index != BUSTER_A64_SIMD_BINDING_NONE;
    if (valid)
    {
        *result = (BusterA64SIMDArrangementBinding){.selector_index = binding->selector_index, .direction = binding->direction};
    }

    return valid;
}

bool buster_a64_simd_catalog_register_arrangement_ok(BusterA64SIMDCatalog const* catalog, u32 row_index, BusterA64SemanticForm form, u32 operand_index,
                                                     BusterA64SemanticVMValue const* values, BusterA64SemanticVMValue value)
{
    bool result = values && operand_index < form.operand_count;
    BusterA64SIMDGeneratedArrangementBinding const* binding = result ? buster_a64_simd_catalog_binding(catalog, row_index, operand_index) : 0;
    if (binding && binding->selector_index != BUSTER_A64_SIMD_BINDING_NONE)
    {
        result = binding->selector_index < form.operand_count && values[binding->selector_index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT &&
                 values[binding->selector_index].aux == value.aux;
    }

    return result;
}

bool buster_a64_simd_semantic_equal_cstr(BusterA64SemanticString left, char8 const* right)
{
    bool result = right != 0;
    u32 length = 0;
    while (result && right[length] != 0)
    {
        length += 1;
    }
    result = result && left.length == length;
    for (u32 index = 0; result && index < length; index += 1)
    {
        result = buster_a64_semantic_string_byte(left, index) == right[index];
    }

    return result;
}

bool buster_a64_simd_semantic_equal(BusterA64SemanticString left, BusterA64SemanticString right)
{
    bool result = left.length == right.length;
    for (u32 index = 0; result && index < left.length; index += 1)
    {
        result = buster_a64_semantic_string_byte(left, index) == buster_a64_semantic_string_byte(right, index);
    }

    return result;
}

bool buster_a64_simd_field_local(BusterA64SemanticForm form, u32 field_id, u32* local)
{
    bool result = local && field_id >= form.field_first && field_id < form.field_first + form.field_count;
    if (result)
    {
        *local = field_id - form.field_first;
    }

    return result;
}

bool buster_a64_simd_field_name(BusterA64SemanticForm form, u32 local, BusterA64SemanticString* name)
{
    BusterA64SemanticField field = {0};
    bool result = name && local < form.field_count && buster_a64_semantic_field(form.field_first + local, &field);
    if (result)
    {
        *name = field.name;
    }

    return result;
}

bool buster_a64_simd_find_field(BusterA64SemanticForm form, BusterA64SemanticString name, u32* local)
{
    bool valid = local != 0;
    u32 count = 0;
    for (u32 index = 0; valid && index < form.field_count; index += 1)
    {
        BusterA64SemanticString candidate = {0};
        valid = buster_a64_simd_field_name(form, index, &candidate);
        if (valid && buster_a64_simd_semantic_equal(name, candidate))
        {
            count += 1;
            *local = index;
        }
    }

    return valid && count == 1;
}

bool buster_a64_simd_assign_field(BusterA64SemanticForm form, u32 local, u32 value, u32* fields, u64* assigned)
{
    BusterA64SemanticField field = {0};
    bool result = fields && assigned && local < form.field_count && local < 64 && buster_a64_semantic_field(form.field_first + local, &field) &&
                  field.width != 0 && field.width <= 32;
    if (result)
    {
        u32 mask = field.width == 32 ? UINT32_MAX : ((UINT32_C(1) << field.width) - 1);
        u64 bit = UINT64_C(1) << local;
        if ((value & ~mask) != 0)
        {
            result = false;
        }
        else if ((*assigned & bit) != 0)
        {
            result = fields[local] == value;
        }
        else
        {
            fields[local] = value;
            *assigned |= bit;
        }
    }

    return result;
}

/* A list member symbol such as <Vt+2> names the register `offset` slots
 * after the list head. */
bool buster_a64_simd_member_offset(BusterA64SemanticString symbol, u32* offset)
{
    bool result = offset != 0;
    u32 plus = UINT32_MAX;
    if (result)
    {
        *offset = 0;
        for (u32 index = 0; plus == UINT32_MAX && index < symbol.length; index += 1)
        {
            if (buster_a64_semantic_string_byte(symbol, index) == '+')
            {
                plus = index;
            }
        }
    }
    if (result && plus != UINT32_MAX)
    {
        u32 end = symbol.length;
        if (end != 0 && buster_a64_semantic_string_byte(symbol, end - 1) == '>')
        {
            end -= 1;
        }
        u32 value = 0;
        result = plus + 1 < end;
        for (u32 index = plus + 1; result && index < end; index += 1)
        {
            char8 digit = buster_a64_semantic_string_byte(symbol, index);
            result = digit >= '0' && digit <= '9';
            value = value * 10u + (u32)(digit - '0');
        }
        result = result && value <= 31;
        if (result)
        {
            *offset = value;
        }
    }

    return result;
}

bool buster_a64_simd_parse_bits(BusterA64SemanticString text, u32* value, u32* mask, u8* width)
{
    bool valid = value && mask && width && text.length != 0 && text.length <= 32;
    u32 parsed_value = 0, parsed_mask = 0;
    for (u32 index = 0; valid && index < text.length; index += 1)
    {
        char8 digit = buster_a64_semantic_string_byte(text, index);
        u32 bit = UINT32_C(1) << (text.length - index - 1);
        if (digit == '0' || digit == '1')
        {
            parsed_mask |= bit;
            parsed_value |= digit == '1' ? bit : 0;
        }
        else
        {
            valid = digit == 'x' || digit == 'X';
        }
    }
    if (valid)
    {
        *value = parsed_value;
        *mask = parsed_mask;
        *width = (u8)text.length;
    }

    return valid;
}

bool buster_a64_simd_symbol_matches(BusterA64SemanticString text, u32 offset, BusterA64SemanticString symbol)
{
    bool result = offset <= text.length && symbol.length <= text.length - offset;
    for (u32 index = 0; result && index < symbol.length; index += 1)
    {
        result = buster_a64_semantic_string_byte(text, offset + index) == buster_a64_semantic_string_byte(symbol, index);
    }

    return result;
}

bool buster_a64_simd_value_uint(BusterA64SemanticVMValue value, bool rotate_allowed, u64* result)
{
    bool valid = result != 0;
    if (valid)
    {
        switch (value.kind)
        {
        case BUSTER_A64_SEMANTIC_VM_VALUE_UNSIGNED_INTEGER:
        case BUSTER_A64_SEMANTIC_VM_VALUE_BITS:
        case BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION:
        case BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER:
        case BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_REGISTER:
        case BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR:
        case BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR:
        case BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT:
        case BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST:
        case BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE:
        case BUSTER_A64_SEMANTIC_VM_VALUE_INTEGER_IMMEDIATE:
        case BUSTER_A64_SEMANTIC_VM_VALUE_CONDITION:
            break;
        case BUSTER_A64_SEMANTIC_VM_VALUE_ROTATE:
            valid = rotate_allowed;
            break;
        default:
            valid = false;
            break;
        }
    }
    if (valid)
    {
        *result = value.payload;
    }

    return valid;
}

bool buster_a64_simd_infer_scalar(BusterA64SemanticString symbol, BusterA64SIMDArrangement* arrangement)
{
    BusterA64SIMDArrangement inferred = BUSTER_A64_SIMD_ARRANGEMENT_INVALID;
    if (arrangement && symbol.length >= 2)
    {
        char8 first = buster_a64_semantic_string_byte(symbol, 1);
        inferred = first == 'H'                   ? BUSTER_A64_SIMD_ARRANGEMENT_H
                   : first == 'S'                 ? BUSTER_A64_SIMD_ARRANGEMENT_S
                   : first == 'D' || first == 'd' ? BUSTER_A64_SIMD_ARRANGEMENT_D
                   : first == 'Q'                 ? BUSTER_A64_SIMD_ARRANGEMENT_Q
                                                  : BUSTER_A64_SIMD_ARRANGEMENT_INVALID;
    }
    bool result = inferred != BUSTER_A64_SIMD_ARRANGEMENT_INVALID;
    if (result)
    {
        *arrangement = inferred;
    }

    return result;
}

/* Read the arrangement fixed around one occurrence of an operand symbol in
 * the presentation text: a B/H/S/D prefix (B<n>) or a dotted suffix
 * (<Vn>.2D). */
BUSTER_GLOBAL_LOCAL bool buster_a64_simd_infer_occurrence_arrangement(BusterA64SemanticString assembly, u32 offset, u32 symbol_length, bool lane_suffix,
                                                                      BusterA64SIMDArrangement* arrangement)
{
    bool result = false;
    char8 prefix = offset > 0 ? buster_a64_semantic_string_byte(assembly, offset - 1) : 0;
    u32 suffix_offset = offset + symbol_length;
    if (prefix == 'B' || prefix == 'H' || prefix == 'S' || prefix == 'D')
    {
        *arrangement = prefix == 'B'   ? BUSTER_A64_SIMD_ARRANGEMENT_B
                       : prefix == 'H' ? BUSTER_A64_SIMD_ARRANGEMENT_H
                       : prefix == 'S' ? BUSTER_A64_SIMD_ARRANGEMENT_S
                                       : BUSTER_A64_SIMD_ARRANGEMENT_D;
        result = true;
    }
    else if (suffix_offset < assembly.length && buster_a64_semantic_string_byte(assembly, suffix_offset) == '.')
    {
        u32 end = suffix_offset + 1;
        bool stop = false;
        while (!stop && end < assembly.length)
        {
            char8 byte = buster_a64_semantic_string_byte(assembly, end);
            stop = byte == ',' || byte == ' ' || byte == '}' || byte == '\t' || (lane_suffix && byte == '[');
            end += stop ? 0 : 1;
        }
        /* The semantic string pool is immutable; point into it only after
         * translating through a bounded local buffer. */
        char8 buffer[8] = {0};
        u32 length = end - suffix_offset - 1;
        if (length != 0 && length < BUSTER_ARRAY_LENGTH(buffer))
        {
            for (u32 index = 0; index < length; index += 1)
            {
                buffer[index] = buster_a64_semantic_string_byte(assembly, suffix_offset + 1 + index);
            }
            result = buster_a64_simd_arrangement_from_string((String8){buffer, length}, arrangement);
        }
    }

    return result;
}

/* Recover fixed presentation suffixes/prefixes when a form has no explicit
 * arrangement selector (for example D<n> or <Vn>.2D). */
bool buster_a64_simd_infer_assembly_arrangement(BusterA64SemanticForm form, u32 operand_index, BusterA64SemanticOperand operand, bool lane_suffix,
                                                BusterA64SIMDArrangement* arrangement)
{
    bool valid = arrangement && operand_index < form.operand_count;
    bool result = false;
    u32 ordinal = 0;
    for (u32 index = 0; valid && index < operand_index; index += 1)
    {
        BusterA64SemanticOperand previous = {0};
        valid = buster_a64_semantic_operand(form.operand_first + index, &previous);
        ordinal += valid && buster_a64_simd_semantic_equal(previous.symbol, operand.symbol) ? 1 : 0;
    }
    u32 occurrence = 0;
    bool located = false;
    for (u32 offset = 0; valid && !located && offset + operand.symbol.length <= form.assembly.length; offset += 1)
    {
        if (buster_a64_simd_symbol_matches(form.assembly, offset, operand.symbol))
        {
            located = occurrence == ordinal;
            occurrence += 1;
            if (located)
            {
                result = buster_a64_simd_infer_occurrence_arrangement(form.assembly, offset, operand.symbol.length, lane_suffix, arrangement);
            }
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool buster_a64_simd_result_matches(BusterA64SemanticValueAtom atom, BusterA64SemanticVMValue desired, bool rotate_allowed)
{
    bool result = false;
    u64 value = 0;
    if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_ENUM)
    {
        bool integer_kind = desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_UNSIGNED_INTEGER || desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INTEGER_IMMEDIATE;
        bool arrangement_kind = desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT || desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR ||
                                desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR || desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST ||
                                desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE;
        if (desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION)
        {
            result = buster_a64_simd_semantic_equal(atom.text, desired.text);
        }
        else if (integer_kind && buster_a64_simd_semantic_equal_cstr(atom.text, "W"))
        {
            result = desired.payload == 32;
        }
        else if (integer_kind && buster_a64_simd_semantic_equal_cstr(atom.text, "X"))
        {
            result = desired.payload == 64;
        }
        else if (arrangement_kind)
        {
            String8 text = buster_a64_simd_arrangement_string((BusterA64SIMDArrangement)desired.aux);
            result = text.length == atom.text.length;
            for (u64 index = 0; result && index < text.length; index += 1)
            {
                result = text.pointer[index] == buster_a64_semantic_string_byte(atom.text, (u32)index);
            }
        }
    }
    else if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_INTEGER)
    {
        result = atom.integer >= 0 && buster_a64_simd_value_uint(desired, rotate_allowed, &value) && value == (u64)atom.integer;
    }
    else if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_BITS)
    {
        u32 expected = 0, mask = 0;
        u8 width = 0;
        result = buster_a64_simd_parse_bits(atom.text, &expected, &mask, &width) && buster_a64_simd_value_uint(desired, rotate_allowed, &value) &&
                 (value & mask) == (expected & mask);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool buster_a64_simd_apply_table_entry(BusterA64SemanticForm form, BusterA64SemanticTransform transform, BusterA64SemanticValue entry,
                                                           u32* fields, u64* assigned)
{
    BusterA64SemanticTableHeader table = {0};
    bool valid = buster_a64_semantic_table_header(transform.table_id, &table) && entry.key_count == table.key_header_count;
    for (u32 index = 0; valid && index < entry.key_count; index += 1)
    {
        BusterA64SemanticString key_name = {0};
        BusterA64SemanticValueAtom atom = {0};
        u32 local = 0;
        valid = buster_a64_semantic_table_key_header(transform.table_id, index, &key_name) && buster_a64_semantic_value_atom(entry.key_first + index, &atom) &&
                buster_a64_simd_find_field(form, key_name, &local);
        if (valid && atom.kind == BUSTER_A64_SEMANTIC_VALUE_INTEGER)
        {
            valid = atom.integer >= 0 && atom.integer <= UINT32_MAX && buster_a64_simd_assign_field(form, local, (u32)atom.integer, fields, assigned);
        }
        else if (valid && atom.kind == BUSTER_A64_SEMANTIC_VALUE_BITS)
        {
            u32 expected = 0, mask = 0;
            u8 width = 0;
            BusterA64SemanticField field = {0};
            valid = buster_a64_simd_parse_bits(atom.text, &expected, &mask, &width) && buster_a64_semantic_field(form.field_first + local, &field) &&
                    width <= field.width;
            if (valid && (*assigned & (UINT64_C(1) << local)) != 0)
            {
                valid = (fields[local] & mask) == (expected & mask);
            }
            else if (valid)
            {
                valid = buster_a64_simd_assign_field(form, local, expected, fields, assigned);
            }
        }
        else
        {
            valid = false;
        }
    }

    return valid;
}

/* Solve every value-table transform of `operand` for the entry whose result
 * matches `desired`.  Entries that agree on all fields are one solution;
 * disagreeing matches are ambiguous and fail. */
bool buster_a64_simd_inverse_tables(BusterA64SemanticForm form, BusterA64SemanticOperand operand, BusterA64SemanticVMValue desired, bool rotate_allowed,
                                    u32* fields, u64* assigned)
{
    bool valid = true;
    for (u32 index = 0; valid && index < operand.transform_count; index += 1)
    {
        u32 transform_id = operand.transform_first + index;
        BusterA64SemanticTransform transform = {0};
        BusterA64SemanticString result_header = {0};
        valid = buster_a64_semantic_transform(transform_id, &transform);
        if (valid && transform.kind == BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE)
        {
            valid = buster_a64_semantic_table_result_header(transform.table_id, &result_header);
            u32 matches = 0;
            u32 selected[64] = {0};
            u64 selected_mask = 0;
            for (u32 entry_index = 0; valid && entry_index < transform.value_count; entry_index += 1)
            {
                BusterA64SemanticValue entry = {0};
                BusterA64SemanticValueAtom result_atom = {0};
                valid = buster_a64_semantic_transform_value(transform_id, entry_index, &entry) && entry.result_count == 1 &&
                        buster_a64_semantic_value_atom(entry.result_first, &result_atom);
                bool reserved =
                    valid && result_atom.kind == BUSTER_A64_SEMANTIC_VALUE_ENUM && buster_a64_simd_semantic_equal_cstr(result_atom.text, "RESERVED");
                if (valid && !reserved && buster_a64_simd_result_matches(result_atom, desired, rotate_allowed))
                {
                    u32 candidate[64] = {0};
                    u64 candidate_mask = *assigned;
                    for (u32 field_index = 0; field_index < form.field_count; field_index += 1)
                    {
                        candidate[field_index] = fields[field_index];
                    }
                    if (buster_a64_simd_apply_table_entry(form, transform, entry, candidate, &candidate_mask))
                    {
                        matches += 1;
                        for (u32 field_index = 0; valid && field_index < form.field_count; field_index += 1)
                        {
                            if (matches == 1)
                            {
                                selected[field_index] = candidate[field_index];
                            }
                            else
                            {
                                valid =
                                    selected[field_index] == candidate[field_index] && ((selected_mask ^ candidate_mask) & (UINT64_C(1) << field_index)) == 0;
                            }
                        }
                        selected_mask = matches == 1 ? candidate_mask : selected_mask;
                    }
                }
            }
            valid = valid && matches != 0;
            if (valid)
            {
                for (u32 field_index = 0; field_index < form.field_count; field_index += 1)
                {
                    fields[field_index] = selected[field_index];
                }
                *assigned = selected_mask;
            }
        }
    }

    return valid;
}

bool buster_a64_simd_decode_arrangement(BusterA64SemanticForm form, BusterA64SemanticOperand operand, BusterA64SemanticVMFields const* fields,
                                        BusterA64SemanticVMValue* result)
{
    bool valid = true;
    bool found = false;
    for (u32 index = 0; valid && !found && index < operand.transform_count; index += 1)
    {
        u32 transform_id = operand.transform_first + index;
        BusterA64SemanticTransform transform = {0};
        BusterA64SemanticVMValue value = buster_a64_semantic_vm_value_invalid();
        valid = buster_a64_semantic_transform(transform_id, &transform);
        if (valid && (transform.kind == BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE || transform.kind == BUSTER_A64_SEMANTIC_TRANSFORM_SHARED_DECODE))
        {
            valid = buster_a64_semantic_vm_eval_transform(form.id, transform_id, fields, &value) == BUSTER_A64_SEMANTIC_VM_STATUS_OK;
            if (valid && value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT)
            {
                *result = value;
                found = true;
            }
            else if (valid && value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION)
            {
                char8 buffer[8] = {0};
                BusterA64SIMDArrangement arrangement = BUSTER_A64_SIMD_ARRANGEMENT_INVALID;
                valid = value.text.length < BUSTER_ARRAY_LENGTH(buffer);
                for (u32 char_index = 0; valid && char_index < value.text.length; char_index += 1)
                {
                    buffer[char_index] = buster_a64_semantic_string_byte(value.text, char_index);
                }
                valid = valid && buster_a64_simd_arrangement_from_string((String8){buffer, value.text.length}, &arrangement);
                if (valid)
                {
                    *result = buster_a64_simd_value_arrangement(arrangement);
                    valid = result->kind != BUSTER_A64_SEMANTIC_VM_VALUE_INVALID;
                    found = true;
                }
            }
        }
    }

    return valid && found;
}

BusterA64SIMDStatus buster_a64_simd_vm_status(BusterA64SemanticVMStatus status)
{
    BusterA64SIMDStatus result;
    switch (status)
    {
    case BUSTER_A64_SEMANTIC_VM_STATUS_OK:
        result = BUSTER_A64_SIMD_STATUS_OK;
        break;
    case BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT:
        result = BUSTER_A64_SIMD_STATUS_INVALID_ARGUMENT;
        break;
    case BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS:
        result = BUSTER_A64_SIMD_STATUS_BOUNDS;
        break;
    case BUSTER_A64_SEMANTIC_VM_STATUS_RESERVED:
        result = BUSTER_A64_SIMD_STATUS_RESERVED;
        break;
    case BUSTER_A64_SEMANTIC_VM_STATUS_AMBIGUOUS:
        result = BUSTER_A64_SIMD_STATUS_AMBIGUOUS;
        break;
    case BUSTER_A64_SEMANTIC_VM_STATUS_RANGE:
        result = BUSTER_A64_SIMD_STATUS_RANGE;
        break;
    case BUSTER_A64_SEMANTIC_VM_STATUS_TARGET_MISMATCH:
        result = BUSTER_A64_SIMD_STATUS_TARGET_MISMATCH;
        break;
    default:
        result = BUSTER_A64_SIMD_STATUS_UNSUPPORTED;
        break;
    }

    return result;
}

BusterA64SIMDStatus buster_a64_simd_canonical_status(BusterAarch64CanonicalDecodeStatus status)
{
    BusterA64SIMDStatus result;
    switch (status)
    {
    case BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS:
        result = BUSTER_A64_SIMD_STATUS_OK;
        break;
    case BUSTER_AARCH64_CANONICAL_DECODE_UNALLOCATED:
        result = BUSTER_A64_SIMD_STATUS_RANGE;
        break;
    case BUSTER_AARCH64_CANONICAL_DECODE_UNSUPPORTED_FEATURE:
        result = BUSTER_A64_SIMD_STATUS_TARGET_MISMATCH;
        break;
    case BUSTER_AARCH64_CANONICAL_DECODE_AMBIGUOUS:
        result = BUSTER_A64_SIMD_STATUS_AMBIGUOUS;
        break;
    default:
        result = BUSTER_A64_SIMD_STATUS_UNSUPPORTED;
        break;
    }

    return result;
}
