#include <buster/lib/compiler/assembly/aarch64_semantic_vm.h>
#include <buster/lib/compiler/assembly/aarch64_encoding.h>

#if BUSTER_COMPILER_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverlength-strings"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#elif BUSTER_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverlength-strings"
#endif
#include <buster/lib/compiler/assembly/generated/aarch64-semantic-vm.generated.h>
#if BUSTER_COMPILER_CLANG
#pragma clang diagnostic pop
#elif BUSTER_COMPILER_GCC
#pragma GCC diagnostic pop
#endif

/* The canonical decoder is introduced by the decoder milestone.  Keeping the
 * include conditional lets this VM branch compile on the metadata base while
 * automatically selecting the canonical raw API when that milestone is
 * present in the integration branch. */
#if defined(__has_include)
#if __has_include(<buster/lib/compiler/assembly/generated/aarch64-canonical-decoder.generated.h>)
#include <buster/lib/compiler/assembly/generated/aarch64-canonical-decoder.generated.h>
#define BUSTER_A64_SEMANTIC_VM_HAS_CANONICAL_RAW 1
#endif
#endif
#ifndef BUSTER_A64_SEMANTIC_VM_HAS_CANONICAL_RAW
#define BUSTER_A64_SEMANTIC_VM_HAS_CANONICAL_RAW 0
#endif

static u64
buster_a64_semantic_vm_width_mask(u8 width)
{
    if (width == 0) return 0;
    if (width >= 64) return UINT64_MAX;
    return (UINT64_C(1) << width) - 1;
}

static u32
buster_a64_semantic_vm_hash_string(BusterA64SemanticString string)
{
    u32 result = UINT32_C(0x811c9dc5);
    for (u32 index = 0; index < string.length; index += 1)
    {
        result ^= (u8)buster_a64_semantic_string_byte(string, index);
        result *= UINT32_C(0x01000193);
    }
    return result;
}

static bool
buster_a64_semantic_vm_range(u32 first, u32 count, u32 total)
{
    return first <= total && count <= total - first;
}

u32
buster_a64_semantic_vm_schema_version(void)
{
    return BUSTER_AARCH64_SEMANTIC_VM_SCHEMA_VERSION;
}

u32
buster_a64_semantic_vm_form_count(void)
{
    return BUSTER_AARCH64_SEMANTIC_VM_FORM_COUNT;
}

u32
buster_a64_semantic_vm_transform_count(void)
{
    return BUSTER_AARCH64_SEMANTIC_VM_TRANSFORM_COUNT;
}

u32
buster_a64_semantic_vm_raw_codec_count(void)
{
    return BUSTER_AARCH64_SEMANTIC_VM_RAW_CODEC_COUNT;
}

u32
buster_a64_semantic_vm_transform_row_count(void)
{
    return BUSTER_AARCH64_SEMANTIC_VM_TRANSFORM_ROW_COUNT;
}

u32
buster_a64_semantic_vm_semantic_executable_count(void)
{
    return BUSTER_AARCH64_SEMANTIC_VM_SEMANTIC_EXECUTABLE_COUNT;
}

u8
buster_a64_semantic_vm_row_coverage(u32 form_id)
{
    if (form_id >= BUSTER_AARCH64_SEMANTIC_VM_FORM_COUNT) return 0;
    return buster_a64_semantic_vm_row_coverage_table[form_id];
}

u8
buster_a64_semantic_vm_row_gap_reason(u32 form_id)
{
    if (form_id >= BUSTER_AARCH64_SEMANTIC_VM_FORM_COUNT) return 0;
    return buster_a64_semantic_vm_row_gap_reason_table[form_id];
}

BusterA64SemanticVMValue
buster_a64_semantic_vm_value_invalid(void)
{
    return (BusterA64SemanticVMValue){0};
}

BusterA64SemanticVMValue
buster_a64_semantic_vm_value_unsigned(u64 value, u8 width)
{
    /* A typed value is never implicitly narrowed.  Callers that intend to
     * extract a bit pattern must mask it before constructing the value. */
    if (width == 0 || width > 64 || (width < 64 && (value & ~buster_a64_semantic_vm_width_mask(width)) != 0))
        return buster_a64_semantic_vm_value_invalid();
    return (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_UNSIGNED_INTEGER,
                                     .width = width,
                                     .payload = value};
}

BusterA64SemanticVMValue
buster_a64_semantic_vm_value_signed(s64 value, u8 width)
{
    if (width == 0 || width > 64) return buster_a64_semantic_vm_value_invalid();
    if (width < 64)
    {
        u64 sign_bit = UINT64_C(1) << (width - 1);
        s64 minimum = -(s64)sign_bit;
        s64 maximum = (s64)(sign_bit - 1);
        if (value < minimum || value > maximum) return buster_a64_semantic_vm_value_invalid();
    }
    return (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_SIGNED_INTEGER,
                                     .width = width,
                                     .payload = (u64)value};
}

BusterA64SemanticVMValue
buster_a64_semantic_vm_value_bits(u64 value, u64 mask, u8 width)
{
    if (width == 0 || width > 64 || (mask & ~buster_a64_semantic_vm_width_mask(width)) != 0) return buster_a64_semantic_vm_value_invalid();
    return (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_BITS,
                                     .width = width,
                                     .flags = (mask != buster_a64_semantic_vm_width_mask(width)) ? BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_WILDCARD : 0,
                                     .payload = value & buster_a64_semantic_vm_width_mask(width),
                                     .mask = mask & buster_a64_semantic_vm_width_mask(width)};
}

BusterA64SemanticVMValue
buster_a64_semantic_vm_value_gpr(u32 number, u8 width, bool sp, bool zr)
{
    if (number > 31 || (width != 32 && width != 64) || (sp && zr) || ((sp || zr) && number != 31)) return buster_a64_semantic_vm_value_invalid();
    return (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER,
                                     .width = width,
                                     .flags = (sp ? BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_SP : 0) |
                                              (zr ? BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR : 0),
                                     .payload = number};
}

BusterA64SemanticVMValue
buster_a64_semantic_vm_value_condition(u32 condition)
{
    if (condition > 15) return buster_a64_semantic_vm_value_invalid();
    return (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_CONDITION,
                                     .width = 4,
                                     .payload = condition};
}

static bool
buster_a64_semantic_vm_value_uint(BusterA64SemanticVMValue value, u64* result)
{
    if (!result) return false;
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
        case BUSTER_A64_SEMANTIC_VM_VALUE_FP_IMMEDIATE:
        case BUSTER_A64_SEMANTIC_VM_VALUE_CONDITION:
        case BUSTER_A64_SEMANTIC_VM_VALUE_NZCV:
        case BUSTER_A64_SEMANTIC_VM_VALUE_SHIFT:
        case BUSTER_A64_SEMANTIC_VM_VALUE_EXTEND:
        case BUSTER_A64_SEMANTIC_VM_VALUE_ROTATE:
        case BUSTER_A64_SEMANTIC_VM_VALUE_SYSTEM_REGISTER:
        case BUSTER_A64_SEMANTIC_VM_VALUE_SYSTEM_OPERATION:
        case BUSTER_A64_SEMANTIC_VM_VALUE_BARRIER_OPTION:
        case BUSTER_A64_SEMANTIC_VM_VALUE_PREFETCH_OPERATION:
        case BUSTER_A64_SEMANTIC_VM_VALUE_MODIFIER:
            *result = value.payload;
            return true;
        case BUSTER_A64_SEMANTIC_VM_VALUE_SIGNED_INTEGER:
        case BUSTER_A64_SEMANTIC_VM_VALUE_LABEL_FIXUP:
            if ((s64)value.payload < 0) return false;
            *result = value.payload;
            return true;
        default: return false;
    }
}

static bool
buster_a64_semantic_vm_value_sint(BusterA64SemanticVMValue value, s64* result)
{
    if (!result) return false;
    if (value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIGNED_INTEGER || value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INTEGER_IMMEDIATE ||
        value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_LABEL_FIXUP)
    {
        *result = (s64)value.payload;
        return true;
    }
    u64 unsigned_value = 0;
    if (buster_a64_semantic_vm_value_uint(value, &unsigned_value) && unsigned_value <= (u64)INT64_MAX)
    {
        *result = (s64)unsigned_value;
        return true;
    }
    return false;
}

static bool
buster_a64_semantic_vm_checked_add(s64 left, s64 right, s64* result)
{
    if (!result) return false;
    if ((right > 0 && left > INT64_MAX - right) || (right < 0 && left < INT64_MIN - right)) return false;
    *result = left + right;
    return true;
}

static bool
buster_a64_semantic_vm_checked_mul(s64 left, s64 right, s64* result)
{
    if (!result) return false;
    if (left == 0 || right == 0) { *result = 0; return true; }
    if (left == -1) { if (right == INT64_MIN) return false; *result = -right; return true; }
    if (right == -1) { if (left == INT64_MIN) return false; *result = -left; return true; }
    if (left > 0)
    {
        if (right > 0) { if (left > INT64_MAX / right) return false; }
        else if (right < INT64_MIN / left) return false;
    }
    else
    {
        if (right > 0) { if (left < INT64_MIN / right) return false; }
        else if (left < INT64_MAX / right) return false;
    }
    *result = left * right;
    return true;
}

static bool
buster_a64_semantic_vm_parse_bits(BusterA64SemanticString string, u64* value, u64* mask, u8* width)
{
    if (!value || !mask || !width || string.length == 0 || string.length > 64) return false;
    u64 parsed_value = 0;
    u64 parsed_mask = 0;
    for (u32 index = 0; index < string.length; index += 1)
    {
        char8 c = buster_a64_semantic_string_byte(string, index);
        if (c != '0' && c != '1' && c != 'x' && c != 'X') return false;
        parsed_value <<= 1;
        parsed_mask <<= 1;
        if (c == '1') { parsed_value |= 1; parsed_mask |= 1; }
        else if (c == '0') parsed_mask |= 1;
    }
    *value = parsed_value;
    *mask = parsed_mask;
    *width = (u8)string.length;
    return true;
}

static bool
buster_a64_semantic_vm_value_equal_text(BusterA64SemanticVMValue value, BusterA64SemanticString expected)
{
    if (value.text.length != expected.length || value.text.offset >= buster_a64_semantic_string_pool_size() ||
        expected.offset >= buster_a64_semantic_string_pool_size() || value.text.length > buster_a64_semantic_string_pool_size() - value.text.offset ||
        expected.length > buster_a64_semantic_string_pool_size() - expected.offset ||
        buster_a64_semantic_vm_hash_string(value.text) != buster_a64_semantic_vm_hash_string(expected)) return false;
    for (u32 index = 0; index < value.text.length; index += 1)
    {
        if (buster_a64_semantic_string_byte(value.text, index) != buster_a64_semantic_string_byte(expected, index)) return false;
    }
    return true;
}

static bool
buster_a64_semantic_vm_field_name_equal(BusterA64SemanticString field_name, BusterA64SemanticVMGeneratedFieldRef reference)
{
    if (field_name.length != reference.name_length || reference.name_offset >= BUSTER_AARCH64_SEMANTIC_VM_FIELD_NAME_BYTES ||
        reference.name_length > BUSTER_AARCH64_SEMANTIC_VM_FIELD_NAME_BYTES - reference.name_offset ||
        buster_a64_semantic_vm_hash_string(field_name) != reference.name_hash)
        return false;
    for (u32 index = 0; index < reference.name_length; index += 1)
    {
        if (buster_a64_semantic_string_byte(field_name, index) != buster_a64_semantic_vm_field_name_bytes[reference.name_offset + index]) return false;
    }
    return true;
}

static bool
buster_a64_semantic_vm_string_equal(BusterA64SemanticString left, BusterA64SemanticString right)
{
    if (left.length != right.length) return false;
    for (u32 index = 0; index < left.length; index += 1)
    {
        if (buster_a64_semantic_string_byte(left, index) != buster_a64_semantic_string_byte(right, index)) return false;
    }
    return true;
}

static bool
buster_a64_semantic_vm_find_field_name(u32 form_id, BusterA64SemanticVMFields const* fields,
                                       BusterA64SemanticString name, u32* value, u8* width)
{
    if (!fields || !value || !width) return false;
    BusterA64SemanticForm form = {0};
    if (!buster_a64_semantic_form(form_id, &form) || fields->count != form.field_count ||
        !buster_a64_semantic_vm_range(form.field_first, form.field_count, buster_a64_semantic_field_count())) return false;
    u32 match_count = 0;
    for (u32 index = 0; index < form.field_count; index += 1)
    {
        BusterA64SemanticField field = {0};
        if (!buster_a64_semantic_field(form.field_first + index, &field)) return false;
        if (buster_a64_semantic_vm_string_equal(field.name, name))
        {
            match_count += 1;
            *value = fields->values[index];
            *width = field.width;
        }
    }
    return match_count == 1;
}

static BusterA64SemanticVMStatus
buster_a64_semantic_vm_eval_typed_field(u32 form_id, BusterA64SemanticVMFields const* fields,
                                        BusterA64SemanticString name, u16 high, u16 low, u16 width,
                                        BusterA64SemanticVMValue* result)
{
    if (!result || !fields) return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
    u32 raw = 0;
    u8 field_width = 0;
    if (!buster_a64_semantic_vm_find_field_name(form_id, fields, name, &raw, &field_width) || field_width == 0 || field_width > 32)
        return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    if (high == UINT16_MAX || low == UINT16_MAX)
    {
        if (high != UINT16_MAX || low != UINT16_MAX || width != 0) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
        *result = buster_a64_semantic_vm_value_unsigned(raw, field_width);
        return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
    }
    if (high < low || high >= field_width || high >= 32 || width != (u16)(high - low + 1))
        return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    u8 slice_width = (u8)(high - low + 1);
    *result = buster_a64_semantic_vm_value_unsigned((raw >> low) & buster_a64_semantic_vm_width_mask(slice_width), slice_width);
    return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
}

static bool
buster_a64_semantic_vm_find_field(u32 form_id, BusterA64SemanticVMGeneratedFieldRef reference,
                                  BusterA64SemanticVMFields const* fields, u32* value)
{
    if (!fields || !value) return false;
    BusterA64SemanticForm form = {0};
    if (!buster_a64_semantic_form(form_id, &form) || fields->count != form.field_count ||
        !buster_a64_semantic_vm_range(form.field_first, form.field_count, buster_a64_semantic_field_count())) return false;
    if (reference.field_ordinal != UINT16_MAX)
    {
        if (reference.field_ordinal >= form.field_count) return false;
        BusterA64SemanticField field = {0};
        if (!buster_a64_semantic_field(form.field_first + reference.field_ordinal, &field) ||
            !buster_a64_semantic_vm_field_name_equal(field.name, reference)) return false;
        *value = fields->values[reference.field_ordinal];
        return true;
    }
    u32 match_count = 0;
    u32 matched_value = 0;
    for (u32 index = 0; index < form.field_count; index += 1)
    {
        BusterA64SemanticField field = {0};
        if (!buster_a64_semantic_field(form.field_first + index, &field)) return false;
        if (buster_a64_semantic_vm_field_name_equal(field.name, reference))
        {
            match_count += 1;
            matched_value = fields->values[index];
        }
    }
    if (match_count != 1) return false;
    *value = matched_value;
    return true;
}

static BusterA64SemanticVMStatus
buster_a64_semantic_vm_eval_field_ref(u32 form_id, BusterA64SemanticVMFields const* fields,
                                      BusterA64SemanticVMGeneratedFieldRef reference, BusterA64SemanticVMValue* result)
{
    u32 raw = 0;
    if (!buster_a64_semantic_vm_find_field(form_id, reference, fields, &raw)) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    u8 high = reference.high;
    u8 low = reference.low;
    if (high < low) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    u8 width = (u8)(high - low + 1);
    if (width > 32) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    *result = buster_a64_semantic_vm_value_unsigned((raw >> low) & buster_a64_semantic_vm_width_mask(width), width);
    return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
}

static BusterA64SemanticVMStatus
buster_a64_semantic_vm_eval_concat(u32 form_id, BusterA64SemanticVMFields const* fields,
                                   BusterA64SemanticVMGeneratedFieldRef const* refs, u32 ref_count,
                                   BusterA64SemanticVMValue* result)
{
    if (!refs || !result || ref_count == 0 || ref_count > 8) return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
    u64 value = 0;
    u8 width = 0;
    for (u32 index = 0; index < ref_count; index += 1)
    {
        BusterA64SemanticVMValue part = {0};
        BusterA64SemanticVMStatus status = buster_a64_semantic_vm_eval_field_ref(form_id, fields, refs[index], &part);
        if (status != BUSTER_A64_SEMANTIC_VM_STATUS_OK) return status;
        if (part.width > 64 - width) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
        if (part.width == 64)
        {
            if (width != 0) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
            value = part.payload;
        }
        else value = (value << part.width) | part.payload;
        width = (u8)(width + part.width);
    }
    BusterA64SemanticVMValue candidate = buster_a64_semantic_vm_value_unsigned(value, width);
    if (candidate.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
    *result = candidate;
    return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
}

static BusterA64SemanticVMStatus
buster_a64_semantic_vm_apply_affine(u8 operation, u16 constant, BusterA64SemanticVMValue input,
                                    BusterA64SemanticVMValue* output)
{
    if (!output) return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
    s64 value = 0;
    if (!buster_a64_semantic_vm_value_sint(input, &value)) return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
    s64 result = 0;
    if (operation == BUSTER_A64_SEMANTIC_VM_OP_ADD_CONST)
    {
        if (!buster_a64_semantic_vm_checked_add(value, (s64)constant, &result)) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
    }
    else if (operation == BUSTER_A64_SEMANTIC_VM_OP_SUB_CONST)
    {
        if (!buster_a64_semantic_vm_checked_add(value, -(s64)constant, &result)) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
    }
    else if (operation == BUSTER_A64_SEMANTIC_VM_OP_SUB_FROM_CONST)
    {
        if (value == INT64_MIN || !buster_a64_semantic_vm_checked_add(-value, (s64)constant, &result)) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
    }
    else if (operation == BUSTER_A64_SEMANTIC_VM_OP_SCALE_DIV)
    {
        if (!constant || value % (s64)constant != 0) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
        result = value / (s64)constant;
    }
    else if (operation == BUSTER_A64_SEMANTIC_VM_OP_SCALE_MUL)
    {
        if (!buster_a64_semantic_vm_checked_mul(value, (s64)constant, &result)) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
    }
    else return BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED;
    BusterA64SemanticVMValue candidate = buster_a64_semantic_vm_value_signed(result, 64);
    if (candidate.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
    candidate.kind = input.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INTEGER_IMMEDIATE ? input.kind : BUSTER_A64_SEMANTIC_VM_VALUE_SIGNED_INTEGER;
    *output = candidate;
    return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
}

static BusterA64SemanticVMStatus
buster_a64_semantic_vm_eval_typed_concat(u32 form_id, BusterA64SemanticVMFields const* fields,
                                         BusterA64SemanticProgramInstruction instruction,
                                         BusterA64SemanticVMValue* result)
{
    if (!result || instruction.operand_count == 0 || instruction.operand_count > 8) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    u64 value = 0;
    u8 width = 0;
    for (u32 index = 0; index < instruction.operand_count; index += 1)
    {
        BusterA64SemanticProgramOperand operand = {0};
        if (!buster_a64_semantic_program_operand(instruction.id, index, &operand)) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
        BusterA64SemanticVMValue part = buster_a64_semantic_vm_value_invalid();
        if (operand.kind == BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_FIELD)
        {
            BusterA64SemanticVMStatus status = buster_a64_semantic_vm_eval_typed_field(form_id, fields, operand.field,
                                                                                         operand.high, operand.low, operand.width, &part);
            if (status != BUSTER_A64_SEMANTIC_VM_STATUS_OK) return status;
        }
        else if (operand.kind == BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_LITERAL)
        {
            if (operand.value < 0) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
            /* Typed literal operands intentionally carry width zero: they
             * are neutral concat markers in the normalized corpus. */
            if (operand.width != 0) part = buster_a64_semantic_vm_value_unsigned((u64)operand.value, (u8)operand.width);
            else part = buster_a64_semantic_vm_value_invalid();
        }
        else return BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED;
        if (operand.kind == BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_LITERAL && operand.width == 0) continue;
        if (part.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID || part.width > 64 - width)
            return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
        if (part.width == 64)
        {
            if (width != 0) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
            value = part.payload;
        }
        else value = (value << part.width) | part.payload;
        width = (u8)(width + part.width);
    }
    if (width == 0) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
    BusterA64SemanticVMValue candidate = buster_a64_semantic_vm_value_unsigned(value, width);
    if (candidate.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
    *result = candidate;
    return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
}

static BusterA64SemanticVMStatus
buster_a64_semantic_vm_eval_typed_shared(u32 form_id, BusterA64SemanticVMFields const* fields,
                                         BusterA64SemanticProgramInstruction instruction,
                                         BusterA64SemanticVMValue* result)
{
    if (!result || instruction.operand_count < 2 || instruction.operand_count > 8) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    u64 selector = 0;
    bool have_selector = false;
    for (u32 index = 0; index < instruction.operand_count; index += 1)
    {
        BusterA64SemanticProgramOperand operand = {0};
        if (!buster_a64_semantic_program_operand(instruction.id, index, &operand)) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
        if (operand.kind == BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_FIELD)
        {
            BusterA64SemanticVMValue value = buster_a64_semantic_vm_value_invalid();
            BusterA64SemanticVMStatus status = buster_a64_semantic_vm_eval_typed_field(form_id, fields, operand.field,
                                                                                         operand.high, operand.low, operand.width, &value);
            if (status != BUSTER_A64_SEMANTIC_VM_STATUS_OK) return status;
            selector = value.payload;
            have_selector = true;
        }
    }
    if (!have_selector) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    for (u32 index = 0; index < instruction.operand_count; index += 1)
    {
        BusterA64SemanticProgramOperand operand = {0};
        if (!buster_a64_semantic_program_operand(instruction.id, index, &operand)) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
        if (operand.kind != BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_ARRANGEMENT || (u64)(u32)operand.value != selector) continue;
        u32 lanes = 0;
        for (u32 text_index = 0; text_index < operand.text.length; text_index += 1)
        {
            char8 character = buster_a64_semantic_string_byte(operand.text, text_index);
            if (character < '0' || character > '9') break;
            lanes = lanes * 10u + (u32)(character - '0');
        }
        if (lanes == 0) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
        *result = (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT,
                                             .width = 1,
                                             .payload = selector,
                                             .aux = lanes,
                                             .aux2 = buster_a64_semantic_vm_hash_string(operand.text)};
        return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
    }
    return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
}

static bool
buster_a64_semantic_vm_typed_instruction(bool value_atom, u32 id, u32 ordinal,
                                         BusterA64SemanticProgramInstruction* instruction)
{
    return value_atom ? buster_a64_semantic_value_atom_program_instruction(id, ordinal, instruction) :
                        buster_a64_semantic_transform_program_instruction(id, ordinal, instruction);
}

static BusterA64SemanticVMStatus
buster_a64_semantic_vm_eval_transform_input(u32 form_id, u32 transform_id,
                                            BusterA64SemanticVMFields const* fields,
                                            BusterA64SemanticVMValue* result)
{
    BusterA64SemanticForm form = {0};
    if (!result || !buster_a64_semantic_form(form_id, &form)) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
    {
        BusterA64SemanticOperand operand = {0};
        if (!buster_a64_semantic_operand(form.operand_first + operand_index, &operand)) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
        if (transform_id < operand.transform_first || transform_id >= operand.transform_first + operand.transform_count || operand.field_index_count == 0)
            continue;
        u32 field_id = 0;
        BusterA64SemanticField field = {0};
        if (!buster_a64_semantic_operand_field_index(operand.id, 0, &field_id) || !buster_a64_semantic_field(field_id, &field))
            return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
        return buster_a64_semantic_vm_eval_typed_field(form_id, fields, field.name, UINT16_MAX, UINT16_MAX, 0, result);
    }
    return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
}

static BusterA64SemanticVMStatus
buster_a64_semantic_vm_eval_typed_program(u32 form_id, BusterA64SemanticVMFields const* fields,
                                          bool value_atom, u32 id, u32 instruction_count,
                                          BusterA64SemanticVMValue* result)
{
    if (!result || !fields || instruction_count == 0 || instruction_count > 4) return BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED;
    BusterA64SemanticVMValue value = buster_a64_semantic_vm_value_invalid();
    bool have_value = false;
    for (u32 ordinal = 0; ordinal < instruction_count; ordinal += 1)
    {
        BusterA64SemanticProgramInstruction instruction = {0};
        if (!buster_a64_semantic_vm_typed_instruction(value_atom, id, ordinal, &instruction)) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
        if (!have_value && !value_atom && (instruction.op == BUSTER_A64_SEMANTIC_PROGRAM_SCALE_MUL ||
                                           instruction.op == BUSTER_A64_SEMANTIC_PROGRAM_SCALE_DIV ||
                                           instruction.op == BUSTER_A64_SEMANTIC_PROGRAM_ADD_CONST ||
                                           instruction.op == BUSTER_A64_SEMANTIC_PROGRAM_SUB_FROM_CONST ||
                                           instruction.op == BUSTER_A64_SEMANTIC_PROGRAM_SCALE_POW2))
        {
            BusterA64SemanticVMStatus input_status = buster_a64_semantic_vm_eval_transform_input(form_id, id, fields, &value);
            if (input_status != BUSTER_A64_SEMANTIC_VM_STATUS_OK) return input_status;
            have_value = true;
        }
        BusterA64SemanticVMStatus status = BUSTER_A64_SEMANTIC_VM_STATUS_OK;
        BusterA64SemanticVMValue candidate = buster_a64_semantic_vm_value_invalid();
        switch (instruction.op)
        {
            case BUSTER_A64_SEMANTIC_PROGRAM_FIELD:
                status = buster_a64_semantic_vm_eval_typed_field(form_id, fields, instruction.field,
                                                                  instruction.high, instruction.low, instruction.width, &candidate);
                break;
            case BUSTER_A64_SEMANTIC_PROGRAM_UINT_CONCAT:
                status = buster_a64_semantic_vm_eval_typed_concat(form_id, fields, instruction, &candidate);
                break;
            case BUSTER_A64_SEMANTIC_PROGRAM_SIGN_EXTEND:
                if (!have_value || instruction.width == 0 || instruction.width > 64) status = BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
                else status = buster_a64_semantic_vm_apply((BusterA64SemanticVMInstruction){.op = BUSTER_A64_SEMANTIC_VM_OP_SIGNED_EXTEND,
                                                                                              .width = (u8)instruction.width},
                                                            &value, 1, 0, 0, &candidate);
                break;
            case BUSTER_A64_SEMANTIC_PROGRAM_SCALE_MUL:
            case BUSTER_A64_SEMANTIC_PROGRAM_SCALE_DIV:
            case BUSTER_A64_SEMANTIC_PROGRAM_ADD_CONST:
            case BUSTER_A64_SEMANTIC_PROGRAM_SUB_FROM_CONST:
                if (!have_value || (instruction.op != BUSTER_A64_SEMANTIC_PROGRAM_ADD_CONST && instruction.value < 0) ||
                    instruction.value == INT32_MIN || (instruction.value < 0 ? -instruction.value : instruction.value) > UINT16_MAX)
                    status = BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
                else
                {
                    u8 operation = instruction.op == BUSTER_A64_SEMANTIC_PROGRAM_SCALE_MUL ? BUSTER_A64_SEMANTIC_VM_OP_SCALE_MUL :
                                   instruction.op == BUSTER_A64_SEMANTIC_PROGRAM_SCALE_DIV ? BUSTER_A64_SEMANTIC_VM_OP_SCALE_DIV :
                                   instruction.op == BUSTER_A64_SEMANTIC_PROGRAM_ADD_CONST ?
                                       (instruction.value < 0 ? BUSTER_A64_SEMANTIC_VM_OP_SUB_CONST : BUSTER_A64_SEMANTIC_VM_OP_ADD_CONST) :
                                       BUSTER_A64_SEMANTIC_VM_OP_SUB_FROM_CONST;
                    u32 magnitude = instruction.value < 0 ? (u32)(-instruction.value) : (u32)instruction.value;
                    status = buster_a64_semantic_vm_apply_affine(operation, (u16)magnitude, value, &candidate);
                }
                break;
            case BUSTER_A64_SEMANTIC_PROGRAM_SCALE_POW2:
                if (!have_value || instruction.value < 0 || instruction.value > 62) status = BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
                else status = buster_a64_semantic_vm_apply_affine(BUSTER_A64_SEMANTIC_VM_OP_SCALE_MUL,
                                                                  (u16)(UINT64_C(1) << instruction.value), value, &candidate);
                break;
            case BUSTER_A64_SEMANTIC_PROGRAM_REGISTER_ADD_MOD:
                if (instruction.modulus != 32 || instruction.value < 0 || instruction.value > UINT16_MAX)
                    status = BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
                else
                {
                    status = buster_a64_semantic_vm_eval_typed_field(form_id, fields, instruction.field,
                                                                      instruction.high, instruction.low, instruction.width, &candidate);
                    if (status == BUSTER_A64_SEMANTIC_VM_STATUS_OK)
                    {
                        candidate.kind = BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_REGISTER;
                        candidate.width = 5;
                        candidate.flags = 0;
                        status = buster_a64_semantic_vm_apply((BusterA64SemanticVMInstruction){.op = BUSTER_A64_SEMANTIC_VM_OP_REGISTER_ADD_MOD32,
                                                                                                  .constant = (u64)instruction.value},
                                                               &candidate, 1, 0, 0, &candidate);
                    }
                }
                break;
            case BUSTER_A64_SEMANTIC_PROGRAM_SHARED_DECODE:
                status = buster_a64_semantic_vm_eval_typed_shared(form_id, fields, instruction, &candidate);
                break;
            case BUSTER_A64_SEMANTIC_PROGRAM_LITERAL:
                if (instruction.value < 0) status = BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
                else { candidate = buster_a64_semantic_vm_value_unsigned((u64)instruction.value, 32); }
                break;
            case BUSTER_A64_SEMANTIC_PROGRAM_TEXT_FACTOR:
            default:
                status = BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED;
                break;
        }
        if (status != BUSTER_A64_SEMANTIC_VM_STATUS_OK) return status;
        value = candidate;
        have_value = true;
    }
    if (!have_value) return BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED;
    *result = value;
    return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
}


static BusterA64SemanticVMStatus
buster_a64_semantic_vm_atom_to_value(u32 atom_id, BusterA64SemanticVMFields const* fields, u32 form_id,
                                     BusterA64SemanticVMValue* result)
{
    if (!result || atom_id >= buster_a64_semantic_value_atom_count()) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    BusterA64SemanticValueAtom atom = {0};
    if (!buster_a64_semantic_value_atom(atom_id, &atom)) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_INTEGER)
    {
        if (atom.integer < 0) *result = buster_a64_semantic_vm_value_signed(atom.integer, 64);
        else *result = buster_a64_semantic_vm_value_unsigned((u64)atom.integer, 64);
        return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
    }
    if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_BITS)
    {
        u64 value = 0, mask = 0;
        u8 width = 0;
        if (!buster_a64_semantic_vm_parse_bits(atom.text, &value, &mask, &width)) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
        *result = buster_a64_semantic_vm_value_bits(value, mask, width);
        return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
    }
    if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_ENUM)
    {
        *result = (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION, .text = atom.text};
        return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
    }
    if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_PROGRAM)
    {
        bool listed = false;
        for (u32 index = 0; index < BUSTER_AARCH64_SEMANTIC_VM_ATOM_PROGRAM_COUNT; index += 1)
        {
            if (buster_a64_semantic_vm_atom_programs[index].atom_id == atom.id)
            {
                listed = true;
                break;
            }
        }
        if (!listed) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
        return buster_a64_semantic_vm_eval_typed_program(form_id, fields, true, atom.id, atom.program_count, result);
    }
    return BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED;
}

static bool
buster_a64_semantic_vm_atom_matches(BusterA64SemanticValueAtom atom, BusterA64SemanticVMValue actual)
{
    if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_INTEGER)
    {
        u64 value = 0;
        return buster_a64_semantic_vm_value_uint(actual, &value) && atom.integer >= 0 && value == (u64)atom.integer;
    }
    if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_BITS)
    {
        u64 value = 0, mask = 0;
        u8 width = 0;
        if (!buster_a64_semantic_vm_parse_bits(atom.text, &value, &mask, &width)) return false;
        u64 actual_value = 0;
        if (!buster_a64_semantic_vm_value_uint(actual, &actual_value) || actual.width < width) return false;
        return (actual_value & mask) == (value & mask);
    }
    if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_ENUM)
    {
        return actual.kind == BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION && buster_a64_semantic_vm_value_equal_text(actual, atom.text);
    }
    return false;
}

static BusterA64SemanticVMStatus
buster_a64_semantic_vm_eval_table(u32 form_id, u32 transform_id, BusterA64SemanticVMFields const* fields,
                                  BusterA64SemanticVMValue* result)
{
    if (!result || transform_id >= BUSTER_AARCH64_SEMANTIC_VM_TRANSFORM_COUNT) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    BusterA64SemanticVMGeneratedTransform const* generated = &buster_a64_semantic_vm_transforms[transform_id];
    if (generated->op != BUSTER_A64_SEMANTIC_VM_OP_TABLE_EXACT_WILDCARD || generated->key_ref_count == 0 ||
        !buster_a64_semantic_vm_range(generated->key_ref_first, generated->key_ref_count, BUSTER_AARCH64_SEMANTIC_VM_FIELD_REF_COUNT))
        return BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED;
    BusterA64SemanticTransform transform = {0};
    if (!buster_a64_semantic_transform(transform_id, &transform) || transform.kind != BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE)
        return BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED;
    u32 match_count = 0;
    BusterA64SemanticValue matched = {0};
    for (u32 entry_index = 0; entry_index < transform.value_count; entry_index += 1)
    {
        BusterA64SemanticValue entry = {0};
        if (!buster_a64_semantic_transform_value(transform_id, entry_index, &entry) || entry.key_count != generated->key_ref_count || entry.result_count != 1)
            return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
        bool matches = true;
        for (u32 key_index = 0; key_index < generated->key_ref_count; key_index += 1)
        {
            BusterA64SemanticVMValue actual = {0};
            BusterA64SemanticVMStatus status = buster_a64_semantic_vm_eval_field_ref(form_id, fields,
                                                                                       buster_a64_semantic_vm_field_refs[generated->key_ref_first + key_index], &actual);
            if (status != BUSTER_A64_SEMANTIC_VM_STATUS_OK) return status;
            BusterA64SemanticValueAtom atom = {0};
            if (!buster_a64_semantic_value_atom(entry.key_first + key_index, &atom) || !buster_a64_semantic_vm_atom_matches(atom, actual))
            {
                matches = false;
                break;
            }
        }
        if (matches)
        {
            match_count += 1;
            matched = entry;
            if (match_count > 1) return BUSTER_A64_SEMANTIC_VM_STATUS_AMBIGUOUS;
        }
    }
    if (match_count == 0) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
    BusterA64SemanticValueAtom result_atom = {0};
    if (!buster_a64_semantic_value_atom(matched.result_first, &result_atom)) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    if (result_atom.kind == BUSTER_A64_SEMANTIC_VALUE_ENUM && result_atom.text.length == 8)
    {
        char8 reserved[8] = {'R', 'E', 'S', 'E', 'R', 'V', 'E', 'D'};
        bool is_reserved = true;
        for (u32 index = 0; index < 8; index += 1)
        {
            is_reserved = is_reserved && buster_a64_semantic_string_byte(result_atom.text, index) == reserved[index];
        }
        if (is_reserved) return BUSTER_A64_SEMANTIC_VM_STATUS_RESERVED;
    }
    return buster_a64_semantic_vm_atom_to_value(matched.result_first, fields, form_id, result);
}

static BusterA64SemanticVMStatus
buster_a64_semantic_vm_logical_immediate(BusterA64SemanticVMInstruction instruction,
                                         BusterA64SemanticVMValue input, BusterA64SemanticVMValue* output)
{
    if (!output || input.kind != BUSTER_A64_SEMANTIC_VM_VALUE_BITS || (instruction.width != 32 && instruction.width != 64))
        return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
    u64 packed = input.payload;
    u32 n = (u32)((packed >> 12) & 1);
    u32 immr = (u32)((packed >> 6) & 0x3f);
    u32 imms = (u32)(packed & 0x3f);
    u32 width = instruction.width;
    if (width == 32 && n != 0) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
    u32 source = (n << 6) | ((~imms) & 0x3f);
    if (source == 0) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
    u32 len = 0;
    u32 bit_index = 1;
    for (; bit_index < 7; bit_index += 1)
    {
        if (source & (1u << bit_index)) len = bit_index;
    }
    if (len == 0) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
    u32 element_width = 1u << len;
    if (element_width > width) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
    u32 levels = element_width - 1;
    u32 ones = (imms & levels) + 1;
    if (ones == 0 || ones > element_width) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
    u64 element = buster_a64_semantic_vm_width_mask((u8)ones);
    u32 rotation = immr & levels;
    if (rotation) element = ((element >> rotation) | (element << (element_width - rotation))) & buster_a64_semantic_vm_width_mask((u8)element_width);
    u64 result = 0;
    u32 offset = 0;
    for (; offset < width; offset += element_width)
    {
        result |= element << offset;
    }
    *output = buster_a64_semantic_vm_value_bits(result, buster_a64_semantic_vm_width_mask((u8)width), (u8)width);
    return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
}

static BusterA64SemanticVMStatus
buster_a64_semantic_vm_apply_program(u32 form_id, u32 transform_id, BusterA64SemanticVMFields const* fields,
                                     BusterA64SemanticVMGeneratedTransform const* generated,
                                     BusterA64SemanticVMValue* output)
{
    if (!fields || !generated || !output || transform_id >= BUSTER_AARCH64_SEMANTIC_VM_TRANSFORM_COUNT ||
        generated->program_count == 0 || generated->program_count > 4)
        return BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED;
    BusterA64SemanticTransform transform = {0};
    if (!buster_a64_semantic_transform(transform_id, &transform) || transform.program_count != generated->program_count)
        return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    return buster_a64_semantic_vm_eval_typed_program(form_id, fields, false, transform_id, transform.program_count, output);
}

BusterA64SemanticVMStatus
buster_a64_semantic_vm_apply(BusterA64SemanticVMInstruction instruction,
                             BusterA64SemanticVMValue const* inputs, u32 input_count, u64 pc, u64 place,
                             BusterA64SemanticVMValue* output)
{
    if (!output || input_count > 8 || (input_count && !inputs) || instruction.op >= BUSTER_A64_SEMANTIC_VM_OP_COUNT)
        return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
    BusterA64SemanticVMValue first = input_count ? inputs[0] : buster_a64_semantic_vm_value_invalid();
    u64 unsigned_value = 0;
    s64 signed_value = 0;
    switch (instruction.op)
    {
        case BUSTER_A64_SEMANTIC_VM_OP_FIELD:
        case BUSTER_A64_SEMANTIC_VM_OP_EXTRACT:
            if (input_count != 1 || !buster_a64_semantic_vm_value_uint(first, &unsigned_value) || instruction.high < instruction.low || instruction.high >= 64)
                return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
            *output = buster_a64_semantic_vm_value_bits((unsigned_value >> instruction.low) & buster_a64_semantic_vm_width_mask((u8)(instruction.high - instruction.low + 1)),
                                                        buster_a64_semantic_vm_width_mask((u8)(instruction.high - instruction.low + 1)),
                                                        (u8)(instruction.high - instruction.low + 1));
            return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
        case BUSTER_A64_SEMANTIC_VM_OP_CONCAT:
        {
            if (input_count == 0 || input_count > 8) return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
            u64 result = 0;
            u8 width = 0;
            for (u32 index = 0; index < input_count; index += 1)
            {
                u64 value = 0;
                if (!buster_a64_semantic_vm_value_uint(inputs[index], &value) || inputs[index].width > 64 - width) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
                if (inputs[index].width == 64)
                {
                    if (width != 0) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
                    result = value;
                }
                else result = (result << inputs[index].width) | value;
                width = (u8)(width + inputs[index].width);
            }
            BusterA64SemanticVMValue candidate = buster_a64_semantic_vm_value_unsigned(result, width);
            if (candidate.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
            *output = candidate;
            return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
        }
        case BUSTER_A64_SEMANTIC_VM_OP_UINT_EXTEND:
            if (input_count != 1 || instruction.width == 0 || instruction.width > 64 || !buster_a64_semantic_vm_value_uint(first, &unsigned_value)) return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
            {
                BusterA64SemanticVMValue candidate = buster_a64_semantic_vm_value_unsigned(unsigned_value, instruction.width);
                if (candidate.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
                *output = candidate;
            }
            return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
        case BUSTER_A64_SEMANTIC_VM_OP_SIGNED_EXTEND:
            if (input_count != 1 || instruction.width == 0 || instruction.width > 64 || !buster_a64_semantic_vm_value_uint(first, &unsigned_value) || first.width == 0 || first.width > 64)
                return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
            if (first.width < 64 && (unsigned_value & (UINT64_C(1) << (first.width - 1)))) unsigned_value |= ~buster_a64_semantic_vm_width_mask(first.width);
            {
                BusterA64SemanticVMValue candidate = buster_a64_semantic_vm_value_signed((s64)unsigned_value, instruction.width);
                if (candidate.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
                *output = candidate;
            }
            return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
        case BUSTER_A64_SEMANTIC_VM_OP_ADD_CONST:
        case BUSTER_A64_SEMANTIC_VM_OP_SUB_CONST:
        case BUSTER_A64_SEMANTIC_VM_OP_SUB_FROM_CONST:
        case BUSTER_A64_SEMANTIC_VM_OP_SCALE_MUL:
        case BUSTER_A64_SEMANTIC_VM_OP_SCALE_DIV:
            if (input_count != 1) return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
            if (instruction.constant > UINT16_MAX) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
            return buster_a64_semantic_vm_apply_affine(instruction.op, (u16)instruction.constant, first, output);
        case BUSTER_A64_SEMANTIC_VM_OP_FIXED_LITERAL:
            if (input_count != 0) return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
            if (instruction.width > 64) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
            {
                BusterA64SemanticVMValue candidate = buster_a64_semantic_vm_value_unsigned(instruction.constant, instruction.width ? instruction.width : 64);
                if (candidate.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
                *output = candidate;
            }
            return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
        case BUSTER_A64_SEMANTIC_VM_OP_DEFAULT:
            if (input_count == 0 || first.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID)
            {
                if (instruction.width > 64) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
                BusterA64SemanticVMValue candidate = buster_a64_semantic_vm_value_unsigned(instruction.constant, instruction.width ? instruction.width : 64);
                if (candidate.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
                *output = candidate;
                return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
            }
            if (input_count != 1) return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
            *output = first;
            return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
        case BUSTER_A64_SEMANTIC_VM_OP_OPTIONAL:
            if (input_count > 1) return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
            *output = first;
            return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
        case BUSTER_A64_SEMANTIC_VM_OP_RESERVED_REJECT:
            if (input_count != 1) return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
            if (first.kind == BUSTER_A64_SEMANTIC_VM_VALUE_RESERVED) return BUSTER_A64_SEMANTIC_VM_STATUS_RESERVED;
            if (first.kind == BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION && first.text.length == 8)
            {
                char8 reserved[8] = {'R', 'E', 'S', 'E', 'R', 'V', 'E', 'D'};
                bool is_reserved = true;
                for (u32 index = 0; index < 8; index += 1)
                {
                    is_reserved = is_reserved && buster_a64_semantic_string_byte(first.text, index) == reserved[index];
                }
                if (is_reserved) return BUSTER_A64_SEMANTIC_VM_STATUS_RESERVED;
            }
            *output = first;
            return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
        case BUSTER_A64_SEMANTIC_VM_OP_PC_RELATIVE:
        case BUSTER_A64_SEMANTIC_VM_OP_PAGE_RELATIVE:
            if (input_count != 1 || !buster_a64_semantic_vm_value_sint(first, &signed_value)) return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
            {
                u64 base = instruction.op == BUSTER_A64_SEMANTIC_VM_OP_PAGE_RELATIVE ? place & ~UINT64_C(0xfff) : place;
                u64 target = base + (u64)signed_value;
                if ((signed_value > 0 && target < base) || (signed_value < 0 && target > base)) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
                *output = (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_LABEL_FIXUP,
                                                    .width = instruction.op == BUSTER_A64_SEMANTIC_VM_OP_PAGE_RELATIVE ? 52 : 64,
                                                    .flags = instruction.op == BUSTER_A64_SEMANTIC_VM_OP_PAGE_RELATIVE ? BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_PAGE : 0,
                                                    .payload = target,
                                                    .aux = (u32)pc};
            }
            return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
        case BUSTER_A64_SEMANTIC_VM_OP_CONDITION_INVERT:
            if (input_count != 1 || first.kind != BUSTER_A64_SEMANTIC_VM_VALUE_CONDITION || first.payload > 13) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
            *output = buster_a64_semantic_vm_value_condition((u32)first.payload ^ 1u);
            return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
        case BUSTER_A64_SEMANTIC_VM_OP_REGISTER_ADD_MOD32:
            if (input_count != 1 || first.payload > 31 ||
                (first.kind != BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER && first.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_REGISTER &&
                 first.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR && first.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR &&
                 first.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST && first.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE))
                return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
            if (first.kind == BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER &&
                ((first.width != 32 && first.width != 64) || (first.flags & (BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_SP | BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR)) ==
                                                              (BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_SP | BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR) ||
                 (first.flags & ~(BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_SP | BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR)) != 0 ||
                 ((first.flags & (BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_SP | BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR)) && first.payload != 31)))
                return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
            if (first.kind != BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER && (first.flags & (BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_SP | BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR)))
                return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
            *output = first;
            output->payload = (first.payload + instruction.constant) & 31;
            if (first.kind == BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER && instruction.constant != 0)
                output->flags &= (u16)~(BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_SP | BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR);
            if (output->kind == BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER &&
                (output->flags & (BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_SP | BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR)) && output->payload != 31)
                return BUSTER_A64_SEMANTIC_VM_STATUS_TARGET_MISMATCH;
            return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
        case BUSTER_A64_SEMANTIC_VM_OP_BITWISE_NOT:
        case BUSTER_A64_SEMANTIC_VM_OP_MOVN:
            if (input_count != 1 || !buster_a64_semantic_vm_value_uint(first, &unsigned_value) || first.width == 0 || first.width > 64) return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
            *output = buster_a64_semantic_vm_value_bits(~unsigned_value & buster_a64_semantic_vm_width_mask(first.width), buster_a64_semantic_vm_width_mask(first.width), first.width);
            return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
        case BUSTER_A64_SEMANTIC_VM_OP_LOGICAL_IMMEDIATE:
            if (input_count != 1) return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
            instruction.width = instruction.width ? instruction.width : 64;
            return buster_a64_semantic_vm_logical_immediate(instruction, first, output);
        case BUSTER_A64_SEMANTIC_VM_OP_FP_IMMEDIATE:
        case BUSTER_A64_SEMANTIC_VM_OP_ADVSIMD_IMMEDIATE:
            return BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED;
        case BUSTER_A64_SEMANTIC_VM_OP_SYSOP_LOOKUP:
            return BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED;
        case BUSTER_A64_SEMANTIC_VM_OP_ALIAS_MAP:
            if (input_count != 1) return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
            *output = first;
            return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
        case BUSTER_A64_SEMANTIC_VM_OP_ALIAS_INJECT:
            if (input_count != 0) return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
            if (instruction.width > 64) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
            {
                BusterA64SemanticVMValue candidate = buster_a64_semantic_vm_value_unsigned(instruction.constant, instruction.width ? instruction.width : 64);
                if (candidate.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
                *output = candidate;
            }
            return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
        case BUSTER_A64_SEMANTIC_VM_OP_ALIAS_CONDITION:
            if (input_count != 1 || !buster_a64_semantic_vm_value_uint(first, &unsigned_value)) return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
            if (!unsigned_value) return BUSTER_A64_SEMANTIC_VM_STATUS_TARGET_MISMATCH;
            *output = first;
            return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
        default:
            return BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED;
    }
}

BusterA64SemanticVMStatus
buster_a64_semantic_vm_decode_fields(u32 form_id, u32 word, BusterA64SemanticVMResult* result)
{
    if (!result || form_id >= BUSTER_AARCH64_SEMANTIC_VM_FORM_COUNT) return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
    BusterA64SemanticForm semantic_form = {0};
    if (!buster_a64_semantic_form(form_id, &semantic_form)) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    if (semantic_form.kind != BUSTER_A64_SEMANTIC_FORM_CANONICAL || semantic_form.status != BUSTER_A64_SEMANTIC_STATUS_DEFINED)
        return BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED;
#if BUSTER_A64_SEMANTIC_VM_HAS_CANONICAL_RAW
    u16 canonical_form_id = buster_a64_semantic_vm_canonical_raw_indices[form_id];
    if (canonical_form_id == UINT16_MAX || semantic_form.field_count > BUSTER_ARRAY_LENGTH(((BusterAarch64CanonicalDecodeResult*)0)->field_values))
        return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    u32 decoded[64] = {0};
    BusterAarch64CanonicalFormInfo canonical_form = {0};
    if (!buster_aarch64_canonical_form(canonical_form_id, &canonical_form) || canonical_form.field_count != semantic_form.field_count ||
        !buster_aarch64_canonical_raw_decode(canonical_form_id, word, decoded, canonical_form.field_count))
        return BUSTER_A64_SEMANTIC_VM_STATUS_TARGET_MISMATCH;
    BusterA64SemanticVMResult candidate = {.status = BUSTER_A64_SEMANTIC_VM_STATUS_OK,
                                           .form_id = form_id,
                                           .word = word,
                                           .field_count = semantic_form.field_count,
                                           .fields = {.count = semantic_form.field_count}};
    for (u32 index = 0; index < semantic_form.field_count; index += 1)
    {
        candidate.fields.values[index] = decoded[index];
    }
    *result = candidate;
    return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
#else
    BUSTER_UNUSED(word);
    return BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED;
#endif
}

BusterA64SemanticVMStatus
buster_a64_semantic_vm_encode_fields(u32 form_id, BusterA64SemanticVMFields const* fields, u32* word)
{
    if (!fields || !word || form_id >= BUSTER_AARCH64_SEMANTIC_VM_FORM_COUNT) return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
    BusterA64SemanticForm semantic_form = {0};
    if (!buster_a64_semantic_form(form_id, &semantic_form)) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    if (semantic_form.kind != BUSTER_A64_SEMANTIC_FORM_CANONICAL || semantic_form.status != BUSTER_A64_SEMANTIC_STATUS_DEFINED ||
        fields->count != semantic_form.field_count)
        return BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED;
#if BUSTER_A64_SEMANTIC_VM_HAS_CANONICAL_RAW
    u16 canonical_form_id = buster_a64_semantic_vm_canonical_raw_indices[form_id];
    if (canonical_form_id == UINT16_MAX) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    BusterAarch64CanonicalFormInfo canonical_form = {0};
    if (!buster_aarch64_canonical_form(canonical_form_id, &canonical_form) || canonical_form.field_count != fields->count)
        return BUSTER_A64_SEMANTIC_VM_STATUS_TARGET_MISMATCH;
    u32 candidate = 0;
    if (!buster_aarch64_canonical_raw_encode(canonical_form_id, fields->values, fields->count, &candidate)) return BUSTER_A64_SEMANTIC_VM_STATUS_RANGE;
    *word = candidate;
    return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
#else
    return BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED;
#endif
}

BusterA64SemanticVMStatus
buster_a64_semantic_vm_eval_transform(u32 form_id, u32 transform_id, BusterA64SemanticVMFields const* fields,
                                      BusterA64SemanticVMValue* output)
{
    if (!fields || !output || form_id >= BUSTER_AARCH64_SEMANTIC_VM_FORM_COUNT || transform_id >= BUSTER_AARCH64_SEMANTIC_VM_TRANSFORM_COUNT)
        return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
    BusterA64SemanticForm form = {0};
    if (!buster_a64_semantic_form(form_id, &form) || fields->count != form.field_count ||
        transform_id < form.transform_first || transform_id >= form.transform_first + form.transform_count)
        return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    BusterA64SemanticVMGeneratedTransform const* generated = &buster_a64_semantic_vm_transforms[transform_id];
    if (generated->semantic_transform_id != transform_id) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    if (!buster_a64_semantic_vm_range(generated->field_ref_first, generated->field_ref_count, BUSTER_AARCH64_SEMANTIC_VM_FIELD_REF_COUNT) ||
        !buster_a64_semantic_vm_range(generated->key_ref_first, generated->key_ref_count, BUSTER_AARCH64_SEMANTIC_VM_FIELD_REF_COUNT))
        return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    BusterA64SemanticTransform transform = {0};
    if (!buster_a64_semantic_transform(transform_id, &transform)) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    if (transform.kind > BUSTER_A64_SEMANTIC_TRANSFORM_SHARED_DECODE ||
        (transform.kind == BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE) !=
            (generated->op == BUSTER_A64_SEMANTIC_VM_OP_TABLE_EXACT_WILDCARD))
        return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    BusterA64SemanticVMValue candidate = buster_a64_semantic_vm_value_invalid();
    BusterA64SemanticVMStatus status = BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED;
    if (generated->program_count != 0)
    {
        status = buster_a64_semantic_vm_apply_program(form_id, transform_id, fields, generated, &candidate);
    }
    else if (generated->op == BUSTER_A64_SEMANTIC_VM_OP_TABLE_EXACT_WILDCARD)
    {
        status = buster_a64_semantic_vm_eval_table(form_id, transform_id, fields, &candidate);
    }
    else if (generated->op == BUSTER_A64_SEMANTIC_VM_OP_CONCAT || generated->op == BUSTER_A64_SEMANTIC_VM_OP_INTEGER_DECODE ||
             generated->op == BUSTER_A64_SEMANTIC_VM_OP_SLICE || generated->op == BUSTER_A64_SEMANTIC_VM_OP_SCALE_DIV)
    {
        if (generated->field_ref_count == 0) status = BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
        else if (generated->op == BUSTER_A64_SEMANTIC_VM_OP_SLICE)
        {
            status = buster_a64_semantic_vm_eval_field_ref(form_id, fields, buster_a64_semantic_vm_field_refs[generated->field_ref_first], &candidate);
        }
        else if (generated->op == BUSTER_A64_SEMANTIC_VM_OP_CONCAT || generated->op == BUSTER_A64_SEMANTIC_VM_OP_INTEGER_DECODE)
        {
            status = buster_a64_semantic_vm_eval_concat(form_id, fields, buster_a64_semantic_vm_field_refs + generated->field_ref_first,
                                                        generated->field_ref_count, &candidate);
            if (status == BUSTER_A64_SEMANTIC_VM_STATUS_OK && generated->op == BUSTER_A64_SEMANTIC_VM_OP_INTEGER_DECODE)
                status = buster_a64_semantic_vm_apply_affine(generated->affine_op, generated->constant, candidate, &candidate);
        }
        else
        {
            status = buster_a64_semantic_vm_eval_field_ref(form_id, fields, buster_a64_semantic_vm_field_refs[generated->field_ref_first], &candidate);
            if (status == BUSTER_A64_SEMANTIC_VM_STATUS_OK) status = buster_a64_semantic_vm_apply_affine(BUSTER_A64_SEMANTIC_VM_OP_SCALE_DIV,
                                                                                                             generated->constant, candidate, &candidate);
        }
    }
    else if (generated->op == BUSTER_A64_SEMANTIC_VM_OP_SHARED_DECODE)
    {
        status = BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED;
    }
    if (status == BUSTER_A64_SEMANTIC_VM_STATUS_OK) *output = candidate;
    return status;
}

BusterA64SemanticVMStatus
buster_a64_semantic_vm_alias(u32 form_id, BusterA64SemanticVMAliasInfo* result)
{
    if (!result || form_id >= BUSTER_AARCH64_SEMANTIC_VM_FORM_COUNT) return BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT;
    BusterA64SemanticForm form = {0};
    if (!buster_a64_semantic_form(form_id, &form) || form.kind != BUSTER_A64_SEMANTIC_FORM_ALIAS) return BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED;
    u32 alias_ordinal = 0;
    for (u32 index = 0; index < form_id; index += 1)
    {
        BusterA64SemanticForm previous = {0};
        if (buster_a64_semantic_form(index, &previous) && previous.kind == BUSTER_A64_SEMANTIC_FORM_ALIAS) alias_ordinal += 1;
    }
    if (alias_ordinal >= BUSTER_AARCH64_SEMANTIC_VM_ALIAS_COUNT) return BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS;
    BusterA64SemanticVMGeneratedAlias alias = buster_a64_semantic_vm_aliases[alias_ordinal];
    BusterA64SemanticVMAliasInfo candidate = {.alias_form_id = form_id,
                                              .target_form_id = alias.target_form,
                                              .injected_field_count = alias.injected_field_count,
                                              .same_field_count = alias.same_field_count,
                                              .preference_count = alias.preference_count,
                                              .condition_supported = alias.condition_supported,
                                              .condition_digest = alias.condition_digest};
    if (!candidate.condition_supported) return BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED;
    *result = candidate;
    return BUSTER_A64_SEMANTIC_VM_STATUS_OK;
}

bool
buster_a64_semantic_vm_validate(void)
{
    if (!buster_a64_semantic_validate() || buster_a64_semantic_schema_version() != 2u || BUSTER_AARCH64_SEMANTIC_VM_SCHEMA_VERSION != 2u ||
        BUSTER_AARCH64_SEMANTIC_VM_FORM_COUNT != 1695u ||
        BUSTER_AARCH64_SEMANTIC_VM_CANONICAL_COUNT != 1523u || BUSTER_AARCH64_SEMANTIC_VM_ALIAS_COUNT != 172u ||
        BUSTER_AARCH64_SEMANTIC_VM_TRANSFORM_COUNT != buster_a64_semantic_transform_count() ||
        BUSTER_AARCH64_SEMANTIC_VM_TRANSFORM_PROGRAM_COUNT != buster_a64_semantic_parsed_program_count() ||
        BUSTER_AARCH64_SEMANTIC_VM_ATOM_PROGRAM_COUNT != buster_a64_semantic_value_program_count()) return false;
    u32 transform_programs = 0;
    for (u32 index = 0; index < BUSTER_AARCH64_SEMANTIC_VM_TRANSFORM_COUNT; index += 1)
    {
        BusterA64SemanticVMGeneratedTransform const* transform = &buster_a64_semantic_vm_transforms[index];
        if (transform->semantic_transform_id != index || transform->op >= BUSTER_A64_SEMANTIC_VM_OP_COUNT || transform->program_count > 4 ||
            !buster_a64_semantic_vm_range(transform->field_ref_first, transform->field_ref_count, BUSTER_AARCH64_SEMANTIC_VM_FIELD_REF_COUNT) ||
            !buster_a64_semantic_vm_range(transform->key_ref_first, transform->key_ref_count, BUSTER_AARCH64_SEMANTIC_VM_FIELD_REF_COUNT)) return false;
        BusterA64SemanticTransform semantic = {0};
        if (!buster_a64_semantic_transform(index, &semantic)) return false;
        if (semantic.program_count != transform->program_count) return false;
        if (semantic.program_count != 0)
        {
            transform_programs += 1;
            if (transform->op == BUSTER_A64_SEMANTIC_VM_OP_INVALID || transform->field_ref_count != 0 || transform->key_ref_count != 0) return false;
            for (u32 program_index = 0; program_index < semantic.program_count; program_index += 1)
            {
                BusterA64SemanticProgramInstruction instruction = {0};
                if (!buster_a64_semantic_transform_program_instruction(index, program_index, &instruction) || instruction.op > BUSTER_A64_SEMANTIC_PROGRAM_SHARED_DECODE)
                    return false;
            }
        }
    }
    if (transform_programs != BUSTER_AARCH64_SEMANTIC_VM_TRANSFORM_PROGRAM_COUNT) return false;
    u32 atom_programs = 0;
    for (u32 atom_id = 0; atom_id < buster_a64_semantic_value_atom_count(); atom_id += 1)
    {
        BusterA64SemanticValueAtom atom = {0};
        if (!buster_a64_semantic_value_atom(atom_id, &atom)) return false;
        bool listed = false;
        for (u32 index = 0; index < BUSTER_AARCH64_SEMANTIC_VM_ATOM_PROGRAM_COUNT; index += 1)
        {
            if (buster_a64_semantic_vm_atom_programs[index].atom_id == atom_id)
            {
                listed = true;
                break;
            }
        }
        if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_PROGRAM)
        {
            atom_programs += 1;
            if (!listed || atom.program_count == 0 || atom.program_count > 4) return false;
            for (u32 program_index = 0; program_index < atom.program_count; program_index += 1)
            {
                BusterA64SemanticProgramInstruction instruction = {0};
                if (!buster_a64_semantic_value_atom_program_instruction(atom_id, program_index, &instruction) || instruction.op > BUSTER_A64_SEMANTIC_PROGRAM_SHARED_DECODE)
                    return false;
            }
        }
        else if (listed) return false;
    }
    if (atom_programs != BUSTER_AARCH64_SEMANTIC_VM_ATOM_PROGRAM_COUNT) return false;
    for (u32 index = 0; index < BUSTER_AARCH64_SEMANTIC_VM_FORM_COUNT; index += 1)
    {
        if (buster_a64_semantic_vm_row_coverage(index) == 0 && buster_a64_semantic_vm_row_gap_reason(index) == 0) return false;
    }
#if BUSTER_A64_SEMANTIC_VM_HAS_CANONICAL_RAW
    u32 canonical_ordinal = 0;
    for (u32 index = 0; index < BUSTER_AARCH64_SEMANTIC_VM_FORM_COUNT; index += 1)
    {
        BusterA64SemanticForm semantic = {0};
        if (!buster_a64_semantic_form(index, &semantic)) return false;
        if (semantic.kind != BUSTER_A64_SEMANTIC_FORM_CANONICAL) continue;
        BusterAarch64CanonicalFormInfo canonical = {0};
        if (!buster_aarch64_canonical_form(canonical_ordinal, &canonical) || canonical.arm_row_digest != semantic.source_digest) return false;
        canonical_ordinal += 1;
    }
    if (canonical_ordinal != BUSTER_AARCH64_SEMANTIC_VM_CANONICAL_COUNT) return false;
#endif
    return true;
}
