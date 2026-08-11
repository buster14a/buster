#include <buster/lib/compiler/assembly/aarch64_system_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/compiler/assembly/generated/aarch64-system-semantics.generated.h>
#include <buster/lib/string.h>

BUSTER_CT_CHECK(BUSTER_AARCH64_SYSTEM_SEMANTICS_GENERATED_ROW_COUNT == BUSTER_AARCH64_SYSTEM_SEMANTIC_ROW_COUNT);
BUSTER_CT_CHECK(BUSTER_AARCH64_SYSTEM_SEMANTICS_GENERATED_FIELD_COUNT == 39u);
BUSTER_CT_CHECK(BUSTER_AARCH64_SYSTEM_SEMANTICS_GENERATED_SCHEMA_VERSION == 2u);

BUSTER_GLOBAL_LOCAL BusterAarch64SystemGeneratedRow const* a64_system_generated_row(u32 row)
{
    if (row >= BUSTER_AARCH64_SYSTEM_SEMANTICS_GENERATED_ROW_COUNT) return 0;
    return buster_aarch64_system_generated_rows + row;
}

BUSTER_GLOBAL_LOCAL BusterAarch64SystemString a64_system_string_from_generated(BusterAarch64SystemGeneratedString string)
{
    return (BusterAarch64SystemString){.offset = string.offset, .length = string.length};
}

BUSTER_GLOBAL_LOCAL bool a64_system_string_valid(BusterAarch64SystemString string)
{
    return string.offset < BUSTER_AARCH64_SYSTEM_SEMANTICS_GENERATED_STRING_POOL_SIZE &&
           string.length < BUSTER_AARCH64_SYSTEM_SEMANTICS_GENERATED_STRING_POOL_SIZE - string.offset &&
           buster_aarch64_system_generated_string_pool[string.offset + string.length] == 0;
}

bool buster_aarch64_system_semantic_string(BusterAarch64SystemString string, String8* result)
{
    if (!result || !a64_system_string_valid(string)) return false;
    *result = (String8){.pointer = (char8*)buster_aarch64_system_generated_string_pool + string.offset, .length = string.length};
    return true;
}

u8 buster_aarch64_system_semantic_string_byte(BusterAarch64SystemString string, u32 index)
{
    if (!a64_system_string_valid(string) || index >= string.length) return 0;
    return buster_aarch64_system_generated_string_pool[string.offset + index];
}

u32 buster_aarch64_system_semantic_count(void)
{
    return BUSTER_AARCH64_SYSTEM_SEMANTIC_ROW_COUNT;
}

BUSTER_GLOBAL_LOCAL u32 a64_system_field_max(u8 width)
{
    if (!width || width > 31) return width == 32 ? UINT32_MAX : 0;
    return (UINT32_C(1) << width) - 1;
}

BUSTER_GLOBAL_LOCAL bool a64_system_field_schema(u32 row, u32 field, BusterAarch64SystemFieldSchema* result)
{
    BusterAarch64SystemGeneratedRow const* generated = a64_system_generated_row(row);
    if (!generated || !result) return false;
    u32 field_first = generated->field_first;
    u32 field_count = generated->field_count;
    u32 field_total = BUSTER_AARCH64_SYSTEM_SEMANTICS_GENERATED_FIELD_COUNT;
    if (field_first > field_total || field_count > field_total - field_first || field >= field_count)
    {
        return false;
    }
    BusterAarch64SystemGeneratedField const* source = buster_aarch64_system_generated_fields + generated->field_first + field;
    if (!a64_system_string_valid((BusterAarch64SystemString){.offset = source->name.offset, .length = source->name.length}) ||
        source->kind >= BUSTER_AARCH64_SYSTEM_FIELD_COUNT || !source->width || source->width > 32 ||
        source->instruction_lsb >= 32 || source->width > 32 - source->instruction_lsb || source->value_lsb >= 32 ||
        source->width > 32 - source->value_lsb)
    {
        return false;
    }
    u32 maximum = a64_system_field_max(source->width);
    if (source->width == 32) maximum = UINT32_MAX;
    *result = (BusterAarch64SystemFieldSchema){
        .name = (BusterAarch64SystemString){.offset = source->name.offset, .length = source->name.length},
        .kind = source->kind,
        .width = source->width,
        .instruction_lsb = source->instruction_lsb,
        .value_lsb = source->value_lsb,
        .minimum = 0,
        .maximum = maximum,
    };
    return true;
}

bool buster_aarch64_system_semantic_field(u32 row, u32 field, BusterAarch64SystemFieldSchema* result)
{
    return a64_system_field_schema(row, field, result);
}

bool buster_aarch64_system_semantic_row(u32 row, BusterAarch64SystemSemanticRecord* result)
{
    BusterAarch64SystemGeneratedRow const* source = a64_system_generated_row(row);
    if (!source || !result ||
        !a64_system_string_valid((BusterAarch64SystemString){.offset = source->id.offset, .length = source->id.length}) ||
        !a64_system_string_valid((BusterAarch64SystemString){.offset = source->encoding_name.offset, .length = source->encoding_name.length}) ||
        !a64_system_string_valid((BusterAarch64SystemString){.offset = source->mnemonic.offset, .length = source->mnemonic.length}) ||
        !a64_system_string_valid((BusterAarch64SystemString){.offset = source->assembly.offset, .length = source->assembly.length}))
    {
        return false;
    }
    *result = (BusterAarch64SystemSemanticRecord){
        .id = {.offset = source->id.offset, .length = source->id.length},
        .encoding_name = {.offset = source->encoding_name.offset, .length = source->encoding_name.length},
        .mnemonic = {.offset = source->mnemonic.offset, .length = source->mnemonic.length},
        .assembly = {.offset = source->assembly.offset, .length = source->assembly.length},
        .row_digest = source->row_digest,
        .fixed_mask = source->fixed_mask,
        .fixed_value = source->fixed_value,
        .field_mask = source->field_mask,
        .form = (u8)row,
        .field_count = source->field_count,
        .optional_field_mask = source->optional_mask,
        .default_value = source->default_value,
        .flags = source->flags,
        .constraint_field = source->constraint_field,
        .constraint_mask = source->constraint_mask,
    };
    return true;
}

bool buster_aarch64_system_semantic_lookup(String8 id, u32* row)
{
    if (!row || !id.pointer || !id.length) return false;
    for (u32 index = 0; index < BUSTER_AARCH64_SYSTEM_SEMANTIC_ROW_COUNT; index += 1)
    {
        BusterAarch64SystemGeneratedRow const* candidate = a64_system_generated_row(index);
        String8 candidate_id = {0};
        if (candidate && buster_aarch64_system_semantic_string(a64_system_string_from_generated(candidate->id), &candidate_id) &&
            string_equal(candidate_id, id))
        {
            *row = index;
            return true;
        }
    }
    return false;
}

bool buster_aarch64_system_op0_encode(u32 op0, u32* o0)
{
    if (!o0 || op0 < 2 || op0 > 3) return false;
    *o0 = op0 - 2;
    return true;
}

bool buster_aarch64_system_op0_decode(u32 o0, u32* op0)
{
    if (!op0 || o0 > 1) return false;
    *op0 = o0 + 2;
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_system_target_is_m1_profile(Target target)
{
    if (target.cpu_model == CPU_MODEL_A64_APPLE_M1) return true;
    return target.cpu_model == CPU_MODEL_NATIVE && target_native.cpu_arch == CPU_ARCH_AARCH64 &&
           target_native.cpu_model == CPU_MODEL_A64_APPLE_M1;
}

bool buster_aarch64_system_semantic_target_supported(Target target)
{
    return target.cpu_arch == CPU_ARCH_AARCH64 && a64_system_target_is_m1_profile(target) && target_cpu_features_are_valid(target);
}

BUSTER_GLOBAL_LOCAL bool a64_system_hint_allowed(Target target, u32 immediate)
{
    // Values without a canonical Arm row are architecturally reserved for
    // this HINT encoding and must not be accepted as arbitrary raw words.
    bool allowed = (immediate <= 5) || immediate == 7 || immediate == 8 || immediate == 10 || immediate == 12 ||
                   immediate == 14 || immediate == 16 || immediate == 18 || immediate == 20 || (immediate >= 24 && immediate <= 31);
    if (!allowed) return false;
    TargetCpuFeature feature = TARGET_CPU_FEATURE_NONE;
    if (immediate == 7 || immediate == 8 || immediate == 10 || immediate == 12 || immediate == 14 ||
        (immediate >= 24 && immediate <= 31))
    {
        feature = TARGET_CPU_FEATURE_AARCH64_PAUTH;
    }
    else if (immediate == 16)
    {
        feature = TARGET_CPU_FEATURE_AARCH64_RAS;
    }
    else if (immediate == 18)
    {
        feature = TARGET_CPU_FEATURE_AARCH64_TRACEV8_4;
    }
    return feature == TARGET_CPU_FEATURE_NONE || target_cpu_feature_has(target, feature);
}

BUSTER_GLOBAL_LOCAL bool a64_system_row_fixed(BusterAarch64SystemGeneratedRow const* row, u32 word)
{
    return row && row->fixed_value == (row->fixed_value & row->fixed_mask) && (word & row->fixed_mask) == row->fixed_value;
}

BUSTER_GLOBAL_LOCAL bool a64_system_operand_valid(BusterAarch64SystemFieldSchema schema, BusterAarch64SystemOperandValue value)
{
    if (schema.kind != value.kind || schema.width != value.width || value.value < schema.minimum || value.value > schema.maximum ||
        value.reserved[0] || value.reserved[1] || value.reserved[2] || value.reserved[3] || value.reserved[4] || value.reserved[5])
    {
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_system_values_valid(Target target, u32 row_index, BusterAarch64SystemGeneratedRow const* row,
                                                  BusterAarch64SystemOperandValue const* fields, u8 defaulted_mask)
{
    if (!row || !fields || row->field_count > 6 || defaulted_mask & ~row->optional_mask) return false;
    if ((row->optional_mask & defaulted_mask) != defaulted_mask) return false;
    for (u32 field = 0; field < row->field_count; field += 1)
    {
        BusterAarch64SystemFieldSchema schema = {0};
        if (!a64_system_field_schema(row_index, field, &schema) || !a64_system_operand_valid(schema, fields[field])) return false;
    }
    if (row->constraint_field != UINT8_MAX)
    {
        if (row->constraint_field >= row->field_count) return false;
        u32 value = (u32)fields[row->constraint_field].value;
        if (value >= 16 || !(row->constraint_mask & (UINT16_C(1) << value))) return false;
    }
    if (row->flags & BUSTER_AARCH64_SYSTEM_ROW_HINT)
    {
        // HINT's architectural immediate is op2:CRm in the Arm field table.
        u32 immediate = ((u32)fields[0].value & 7u) | (((u32)fields[1].value & 15u) << 3);
        if (!a64_system_hint_allowed(target, immediate)) return false;
    }
    if (row->flags & BUSTER_AARCH64_SYSTEM_ROW_PSTATE)
    {
        // Arm's machine-readable constraint is !(op1 == 000 && op2 in
        // {00x,010}); the source fields are op2, CRm, op1.
        u32 op2 = (u32)fields[0].value;
        u32 op1 = (u32)fields[2].value;
        if (op1 == 0 && op2 <= 2) return false;
    }
    if (row_index == BUSTER_AARCH64_SYSTEM_FORM_CLREX || row_index == BUSTER_AARCH64_SYSTEM_FORM_ISB)
    {
        if (fields[0].value < 0 || fields[0].value > 15) return false;
        if ((defaulted_mask & 1u) && fields[0].value != row->default_value) return false;
    }
    if (row_index == BUSTER_AARCH64_SYSTEM_FORM_SYS)
    {
        if ((defaulted_mask & 1u) && fields[0].value != row->default_value) return false;
    }
    return true;
}

bool buster_aarch64_system_semantic_validate(void)
{
    if (BUSTER_AARCH64_SYSTEM_SEMANTICS_GENERATED_ROW_COUNT != BUSTER_AARCH64_SYSTEM_SEMANTIC_ROW_COUNT) return false;
    for (u32 index = 0; index < BUSTER_AARCH64_SYSTEM_SEMANTIC_ROW_COUNT; index += 1)
    {
        BusterAarch64SystemGeneratedRow const* row = a64_system_generated_row(index);
        if (!row || !a64_system_string_valid(a64_system_string_from_generated(row->id)) ||
            !a64_system_string_valid(a64_system_string_from_generated(row->encoding_name)) ||
            !a64_system_string_valid(a64_system_string_from_generated(row->mnemonic)) ||
            !a64_system_string_valid(a64_system_string_from_generated(row->assembly)) ||
            row->field_count > 6 || row->optional_mask & ~((UINT32_C(1) << row->field_count) - 1u) ||
            row->fixed_value != (row->fixed_value & row->fixed_mask) || (row->fixed_mask & row->field_mask) != 0 ||
            (row->fixed_mask | row->field_mask) != UINT32_MAX || !row->field_count ||
            row->reserved || (row->flags & ~(BUSTER_AARCH64_SYSTEM_ROW_HINT | BUSTER_AARCH64_SYSTEM_ROW_PSTATE |
                                             BUSTER_AARCH64_SYSTEM_ROW_SYSTEM_REGISTER)) ||
            (row->constraint_field == UINT8_MAX && row->constraint_mask) ||
            (row->constraint_field != UINT8_MAX && (row->constraint_field >= row->field_count || !row->constraint_mask)) ||
            (!row->optional_mask && row->default_value != 0) || (row->optional_mask && row->default_value > 31))
        {
            return false;
        }
        for (u32 prior = 0; prior < index; prior += 1)
        {
            String8 id = {0}, prior_id = {0};
            if (!buster_aarch64_system_semantic_string(a64_system_string_from_generated(row->id), &id) ||
                !buster_aarch64_system_semantic_string(a64_system_string_from_generated(a64_system_generated_row(prior)->id), &prior_id) ||
                string_equal(id, prior_id))
            {
                return false;
            }
            u32 shared = row->fixed_mask & a64_system_generated_row(prior)->fixed_mask;
            if (((row->fixed_value ^ a64_system_generated_row(prior)->fixed_value) & shared) == 0 ||
                row->row_digest == a64_system_generated_row(prior)->row_digest)
            {
                return false;
            }
        }
        for (u32 field = 0; field < row->field_count; field += 1)
        {
            BusterAarch64SystemFieldSchema schema = {0};
            if (!a64_system_field_schema(index, field, &schema)) return false;
            u32 field_mask = ((schema.width == 32 ? UINT32_MAX : a64_system_field_max(schema.width)) << schema.instruction_lsb);
            if ((field_mask & row->fixed_mask) != 0 || (field_mask & row->field_mask) != field_mask) return false;
        }
        if (row->constraint_field != UINT8_MAX)
        {
            BusterAarch64SystemFieldSchema schema = {0};
            if (!a64_system_field_schema(index, row->constraint_field, &schema) || schema.width > 4) return false;
            u32 value_mask = (UINT32_C(1) << (UINT32_C(1) << schema.width)) - 1u;
            if (((u32)row->constraint_mask & ~value_mask) != 0) return false;
        }
    }
    return true;
}

char const* buster_aarch64_system_semantic_digest(void)
{
    return BUSTER_AARCH64_SYSTEM_SEMANTIC_DIGEST;
}

BUSTER_GLOBAL_LOCAL bool a64_system_encode_fields(BusterAarch64SystemGeneratedRow const* row,
                                                   BusterAarch64SystemInstruction const* instruction, Target target, u32* result)
{
    if (!row || !instruction || !result || instruction->field_count != row->field_count ||
        instruction->defaulted_mask & ~row->optional_mask)
        return false;
    u32 word = row->fixed_value;
    for (u32 field = 0; field < row->field_count; field += 1)
    {
        BusterAarch64SystemFieldSchema schema = {0};
        if (!a64_system_field_schema((u32)instruction->row, field, &schema)) return false;
        BusterAarch64SystemOperandValue value = instruction->fields[field];
        if (!a64_system_operand_valid(schema, value)) return false;
        if ((instruction->defaulted_mask & (1u << field)) && value.value != row->default_value) return false;
        u32 raw = (u32)value.value;
        u32 mask = schema.width == 32 ? UINT32_MAX : a64_system_field_max(schema.width);
        word |= ((raw & mask) << schema.instruction_lsb);
    }
    if (!a64_system_values_valid(target, instruction->row, row, instruction->fields, instruction->defaulted_mask) ||
        !a64_system_row_fixed(row, word))
        return false;
    *result = word;
    return true;
}

bool buster_aarch64_system_semantic_encode(Target target, BusterAarch64SystemInstruction const* instruction, u32* word)
{
    if (!buster_aarch64_system_semantic_target_supported(target) || !instruction || !word ||
        instruction->row >= BUSTER_AARCH64_SYSTEM_SEMANTIC_ROW_COUNT)
        return false;
    u32 encoded = 0;
    if (!a64_system_encode_fields(a64_system_generated_row(instruction->row), instruction, target, &encoded)) return false;
    *word = encoded;
    return true;
}

bool buster_aarch64_system_semantic_decode_form(Target target, u32 row_index, u32 word,
                                                 BusterAarch64SystemInstruction* instruction)
{
    if (!buster_aarch64_system_semantic_target_supported(target) || !instruction || row_index >= BUSTER_AARCH64_SYSTEM_SEMANTIC_ROW_COUNT)
    {
        return false;
    }
    BusterAarch64SystemGeneratedRow const* row = a64_system_generated_row(row_index);
    if (!a64_system_row_fixed(row, word)) return false;
    BusterAarch64SystemInstruction decoded = {.row = (u16)row_index, .field_count = row->field_count};
    for (u32 field = 0; field < row->field_count; field += 1)
    {
        BusterAarch64SystemFieldSchema schema = {0};
        if (!a64_system_field_schema(row_index, field, &schema)) return false;
        u32 mask = schema.width == 32 ? UINT32_MAX : a64_system_field_max(schema.width);
        u32 raw = (word >> schema.instruction_lsb) & mask;
        decoded.fields[field] = (BusterAarch64SystemOperandValue){.value = raw, .kind = schema.kind, .width = schema.width};
        if ((row->optional_mask & (1u << field)) && raw == row->default_value) decoded.defaulted_mask |= (u8)(1u << field);
    }
    if (!a64_system_values_valid(target, row_index, row, decoded.fields, decoded.defaulted_mask)) return false;
    *instruction = decoded;
    return true;
}

bool buster_aarch64_system_semantic_decode(Target target, u32 word, BusterAarch64SystemInstruction* instruction)
{
    if (!buster_aarch64_system_semantic_target_supported(target) || !instruction) return false;
    BusterAarch64CanonicalDecodeResult canonical = {0};
    if (buster_aarch64_canonical_decode(target, word, &canonical) != BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS) return false;
    u32 row_index = UINT32_MAX;
    for (u32 row = 0; row < BUSTER_AARCH64_SYSTEM_SEMANTIC_ROW_COUNT; row += 1)
    {
        if (a64_system_generated_row(row)->row_digest == canonical.arm_row_digest)
        {
            if (row_index != UINT32_MAX) return false;
            row_index = row;
        }
    }
    if (row_index == UINT32_MAX) return false;
    return buster_aarch64_system_semantic_decode_form(target, row_index, word, instruction);
}

u32 buster_aarch64_system_semantic_fixed_canonical_count(void)
{
    return BUSTER_AARCH64_SYSTEM_FIXED_CANONICAL_COUNT;
}

u32 buster_aarch64_system_semantic_canonical_count(void)
{
    return BUSTER_AARCH64_SYSTEM_CANONICAL_COUNT;
}
