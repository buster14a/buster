#include <buster/lib/compiler/assembly/aarch64_semantics.h>

#if BUSTER_COMPILER_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverlength-strings"
#elif BUSTER_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverlength-strings"
#endif
#include <buster/lib/compiler/assembly/generated/arm-a64-semantic.generated.h>
#if BUSTER_COMPILER_CLANG
#pragma clang diagnostic pop
#elif BUSTER_COMPILER_GCC
#pragma GCC diagnostic pop
#endif

static bool
buster_a64_semantic_range(u32 first, u32 count, u32 total)
{
    return first <= total && count <= total - first;
}

static bool
buster_a64_semantic_string_offset(u32 offset, BusterA64SemanticString* result)
{
    if (!result || offset >= BUSTER_AARCH64_SEMANTIC_STRING_POOL_SIZE) return false;
    u32 length = 0;
    while (offset + length < BUSTER_AARCH64_SEMANTIC_STRING_POOL_SIZE &&
           buster_a64_semantic_string_pool[offset + length] != 0)
    {
        length += 1;
    }
    if (offset + length >= BUSTER_AARCH64_SEMANTIC_STRING_POOL_SIZE) return false;
    *result = (BusterA64SemanticString){.offset = offset, .length = length};
    return true;
}

static u8 buster_a64_semantic_base64_value(char8 value)
{
    if (value >= 'A' && value <= 'Z') return (u8)(value - 'A');
    if (value >= 'a' && value <= 'z') return (u8)(value - 'a' + 26);
    if (value >= '0' && value <= '9') return (u8)(value - '0' + 52);
    if (value == '+') return 62;
    if (value == '/') return 63;
    return 0;
}

static u8 buster_a64_semantic_blob_byte(char8 const* blob, u32 blob_size, u32 index)
{
    if (index >= blob_size) return 0;
    u32 group = index / 3;
    u32 within = index % 3;
    u32 encoded = group * 4;
    u32 value = ((u32)buster_a64_semantic_base64_value(blob[encoded]) << 18) |
                ((u32)buster_a64_semantic_base64_value(blob[encoded + 1]) << 12) |
                ((u32)buster_a64_semantic_base64_value(blob[encoded + 2]) << 6) |
                (u32)buster_a64_semantic_base64_value(blob[encoded + 3]);
    return (u8)((value >> (16 - 8 * within)) & 0xff);
}

static u16 buster_a64_semantic_blob_u16(char8 const* blob, u32 blob_size, u32 offset)
{
    return (u16)((u32)buster_a64_semantic_blob_byte(blob, blob_size, offset) |
                 ((u32)buster_a64_semantic_blob_byte(blob, blob_size, offset + 1) << 8));
}

static u32 buster_a64_semantic_blob_u32(char8 const* blob, u32 blob_size, u32 offset)
{
    return (u32)buster_a64_semantic_blob_byte(blob, blob_size, offset) | (u32)buster_a64_semantic_blob_byte(blob, blob_size, offset + 1) << 8 |
           (u32)buster_a64_semantic_blob_byte(blob, blob_size, offset + 2) << 16 | (u32)buster_a64_semantic_blob_byte(blob, blob_size, offset + 3) << 24;
}

static u64 buster_a64_semantic_blob_u64(char8 const* blob, u32 blob_size, u32 offset)
{
    u64 value = 0;
    u32 byte_index = 0;
    for (; byte_index < 8; byte_index += 1) value |= (u64)buster_a64_semantic_blob_byte(blob, blob_size, offset + byte_index) << (8 * byte_index);
    return value;
}

static bool buster_a64_semantic_generated_operand(u32 id, BusterA64SemanticGeneratedOperand* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_OPERAND_COUNT) return false;
    u32 offset = id * BUSTER_AARCH64_SEMANTIC_OPERAND_RECORD_BYTES;
    *result = (BusterA64SemanticGeneratedOperand){
        .form_id = buster_a64_semantic_blob_u32(buster_a64_semantic_operand_blob, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 0),
        .link_offset = buster_a64_semantic_blob_u32(buster_a64_semantic_operand_blob, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 4),
        .symbol_offset = buster_a64_semantic_blob_u32(buster_a64_semantic_operand_blob, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 8),
        .field_first = buster_a64_semantic_blob_u32(buster_a64_semantic_operand_blob, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 12),
        .field_index_first = buster_a64_semantic_blob_u32(buster_a64_semantic_operand_blob, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 16),
        .transform_first = buster_a64_semantic_blob_u32(buster_a64_semantic_operand_blob, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 20),
        .kind_mask = buster_a64_semantic_blob_u32(buster_a64_semantic_operand_blob, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 24),
        .flags = buster_a64_semantic_blob_u64(buster_a64_semantic_operand_blob, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 28),
        .field_count = buster_a64_semantic_blob_u16(buster_a64_semantic_operand_blob, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 36),
        .field_index_count = buster_a64_semantic_blob_u16(buster_a64_semantic_operand_blob, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 38),
        .transform_count = buster_a64_semantic_blob_u16(buster_a64_semantic_operand_blob, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 40),
        .kind = buster_a64_semantic_blob_byte(buster_a64_semantic_operand_blob, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 42),
        .position = buster_a64_semantic_blob_byte(buster_a64_semantic_operand_blob, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 43),
        .classification_status = buster_a64_semantic_blob_byte(buster_a64_semantic_operand_blob, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 44),
        .reserved = buster_a64_semantic_blob_byte(buster_a64_semantic_operand_blob, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 45),
        .role_offset = buster_a64_semantic_blob_u32(buster_a64_semantic_operand_blob, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 46),
        .direction_offset = buster_a64_semantic_blob_u32(buster_a64_semantic_operand_blob, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 50),
    };
    return true;
}

static bool buster_a64_semantic_generated_segment(u32 id, BusterA64SemanticGeneratedSegment* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_SEGMENT_COUNT) return false;
    u32 offset = id * BUSTER_AARCH64_SEMANTIC_SEGMENT_RECORD_BYTES;
    result->instruction_lsb = buster_a64_semantic_blob_byte(buster_a64_semantic_segment_blob, BUSTER_AARCH64_SEMANTIC_SEGMENT_BLOB_SIZE, offset + 0);
    result->width = buster_a64_semantic_blob_byte(buster_a64_semantic_segment_blob, BUSTER_AARCH64_SEMANTIC_SEGMENT_BLOB_SIZE, offset + 1);
    result->value_lsb = buster_a64_semantic_blob_byte(buster_a64_semantic_segment_blob, BUSTER_AARCH64_SEMANTIC_SEGMENT_BLOB_SIZE, offset + 2);
    result->reserved = buster_a64_semantic_blob_byte(buster_a64_semantic_segment_blob, BUSTER_AARCH64_SEMANTIC_SEGMENT_BLOB_SIZE, offset + 3);
    return true;
}

static bool buster_a64_semantic_generated_field(u32 id, BusterA64SemanticGeneratedField* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_FIELD_COUNT) return false;
    u32 offset = id * BUSTER_AARCH64_SEMANTIC_FIELD_RECORD_BYTES;
    result->name_offset = buster_a64_semantic_blob_u32(buster_a64_semantic_field_blob, BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE, offset + 0);
    result->source_mask = buster_a64_semantic_blob_u32(buster_a64_semantic_field_blob, BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE, offset + 4);
    result->segment_first = buster_a64_semantic_blob_u32(buster_a64_semantic_field_blob, BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE, offset + 8);
    result->segment_count = buster_a64_semantic_blob_u16(buster_a64_semantic_field_blob, BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE, offset + 12);
    result->width = buster_a64_semantic_blob_byte(buster_a64_semantic_field_blob, BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE, offset + 14);
    result->reserved = buster_a64_semantic_blob_byte(buster_a64_semantic_field_blob, BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE, offset + 15);
    return true;
}

u32 buster_a64_semantic_schema_version(void) { return BUSTER_AARCH64_SEMANTIC_SCHEMA_VERSION; }
u32 buster_a64_semantic_form_count(void) { return BUSTER_AARCH64_SEMANTIC_FORM_COUNT; }
u32 buster_a64_semantic_field_count(void) { return BUSTER_AARCH64_SEMANTIC_FIELD_COUNT; }
u32 buster_a64_semantic_segment_count(void) { return BUSTER_AARCH64_SEMANTIC_SEGMENT_COUNT; }
u32 buster_a64_semantic_operand_count(void) { return BUSTER_AARCH64_SEMANTIC_OPERAND_COUNT; }
u32 buster_a64_semantic_operand_field_index_count(void) { return BUSTER_AARCH64_SEMANTIC_OPERAND_FIELD_INDEX_COUNT; }
u32 buster_a64_semantic_transform_count(void) { return BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT; }
u32 buster_a64_semantic_transform_part_count(void) { return BUSTER_AARCH64_SEMANTIC_TRANSFORM_PART_COUNT; }
u32 buster_a64_semantic_value_count(void) { return BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_COUNT; }
u32 buster_a64_semantic_value_atom_count(void) { return BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT; }
u32 buster_a64_semantic_string_pool_size(void) { return BUSTER_AARCH64_SEMANTIC_STRING_POOL_SIZE; }

bool buster_a64_semantic_string(u32 offset, BusterA64SemanticString* result)
{
    return buster_a64_semantic_string_offset(offset, result);
}

char8 buster_a64_semantic_string_byte(BusterA64SemanticString string, u32 index)
{
    if (string.offset >= BUSTER_AARCH64_SEMANTIC_STRING_POOL_SIZE || index >= string.length) return 0;
    if (index >= BUSTER_AARCH64_SEMANTIC_STRING_POOL_SIZE - string.offset) return 0;
    return buster_a64_semantic_string_pool[string.offset + index];
}

bool buster_a64_semantic_form(u32 id, BusterA64SemanticForm* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_FORM_COUNT) return false;
    BusterA64SemanticGeneratedForm const* source = buster_a64_semantic_forms + id;
    BusterA64SemanticString name, mnemonic, assembly;
    if (!buster_a64_semantic_string_offset(source->name_offset, &name) ||
        !buster_a64_semantic_string_offset(source->mnemonic_offset, &mnemonic) ||
        !buster_a64_semantic_string_offset(source->assembly_offset, &assembly) ||
        !buster_a64_semantic_range(source->field_first, source->field_count, BUSTER_AARCH64_SEMANTIC_FIELD_COUNT) ||
        !buster_a64_semantic_range(source->operand_first, source->operand_count, BUSTER_AARCH64_SEMANTIC_OPERAND_COUNT) ||
        !buster_a64_semantic_range(source->transform_first, source->transform_count, BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT)) return false;
    *result = (BusterA64SemanticForm){
        .id = id, .name = name, .mnemonic = mnemonic, .assembly = assembly,
        .source_digest = source->source_digest, .fixed_mask = source->fixed_mask, .fixed_value = source->fixed_value,
        .field_first = source->field_first, .operand_first = source->operand_first, .transform_first = source->transform_first,
        .field_count = source->field_count, .operand_count = source->operand_count, .transform_count = source->transform_count,
        .owner = source->owner, .kind = source->kind, .raw_layout_resolved = source->raw_layout_resolved, .status = source->status,
    };
    return true;
}

bool buster_a64_semantic_field(u32 id, BusterA64SemanticField* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_FIELD_COUNT) return false;
    BusterA64SemanticGeneratedField source_value;
    if (!buster_a64_semantic_generated_field(id, &source_value)) return false;
    BusterA64SemanticGeneratedField const* source = &source_value;
    BusterA64SemanticString name;
    if (!buster_a64_semantic_string_offset(source->name_offset, &name) ||
        !buster_a64_semantic_range(source->segment_first, source->segment_count, BUSTER_AARCH64_SEMANTIC_SEGMENT_COUNT)) return false;
    *result = (BusterA64SemanticField){.id = id, .name = name, .source_mask = source->source_mask,
                                      .segment_first = source->segment_first, .segment_count = source->segment_count,
                                      .width = source->width};
    return true;
}

bool buster_a64_semantic_segment(u32 id, BusterA64SemanticSegment* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_SEGMENT_COUNT) return false;
    BusterA64SemanticGeneratedSegment source_value;
    if (!buster_a64_semantic_generated_segment(id, &source_value)) return false;
    BusterA64SemanticGeneratedSegment const* source = &source_value;
    *result = (BusterA64SemanticSegment){.id = id, .instruction_lsb = source->instruction_lsb, .width = source->width,
                                         .value_lsb = source->value_lsb, .reserved = source->reserved};
    return true;
}

bool buster_a64_semantic_operand(u32 id, BusterA64SemanticOperand* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_OPERAND_COUNT) return false;
    BusterA64SemanticGeneratedOperand source_value;
    if (!buster_a64_semantic_generated_operand(id, &source_value)) return false;
    BusterA64SemanticGeneratedOperand const* source = &source_value;
    BusterA64SemanticString link, symbol, role, direction;
    if (!buster_a64_semantic_string_offset(source->link_offset, &link) ||
        !buster_a64_semantic_string_offset(source->symbol_offset, &symbol) ||
        !buster_a64_semantic_string_offset(source->role_offset, &role) ||
        !buster_a64_semantic_string_offset(source->direction_offset, &direction) ||
        !buster_a64_semantic_range(source->field_index_first, source->field_index_count, BUSTER_AARCH64_SEMANTIC_OPERAND_FIELD_INDEX_COUNT) ||
        !buster_a64_semantic_range(source->transform_first, source->transform_count, BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT)) return false;
    *result = (BusterA64SemanticOperand){
        .id = id, .form_id = source->form_id, .link = link, .symbol = symbol,
        .field_first = source->field_first, .field_indices_first = source->field_index_first, .transform_first = source->transform_first,
        .field_count = source->field_count, .field_index_count = source->field_index_count, .transform_count = source->transform_count,
        .kind = source->kind, .kind_mask = source->kind_mask, .flags = source->flags, .position = source->position,
        .classification_status = source->classification_status,
        .role = role, .direction = direction,
    };
    return true;
}

bool buster_a64_semantic_operand_field_index(u32 operand_id, u32 ordinal, u32* field_id)
{
    if (!field_id || operand_id >= BUSTER_AARCH64_SEMANTIC_OPERAND_COUNT) return false;
    BusterA64SemanticGeneratedOperand operand_value;
    if (!buster_a64_semantic_generated_operand(operand_id, &operand_value)) return false;
    BusterA64SemanticGeneratedOperand const* operand = &operand_value;
    if (ordinal >= operand->field_index_count || !buster_a64_semantic_range(operand->field_index_first, operand->field_index_count, BUSTER_AARCH64_SEMANTIC_OPERAND_FIELD_INDEX_COUNT)) return false;
    u32 value = buster_a64_semantic_operand_field_indices[operand->field_index_first + ordinal];
    if (value >= BUSTER_AARCH64_SEMANTIC_FIELD_COUNT) return false;
    *field_id = value;
    return true;
}

bool buster_a64_semantic_transform(u32 id, BusterA64SemanticTransform* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT) return false;
    BusterA64SemanticGeneratedTransform const* source = buster_a64_semantic_transforms + id;
    BusterA64SemanticString expression;
    if (!buster_a64_semantic_string_offset(source->expression_offset, &expression)) return false;
    if (!buster_a64_semantic_range(source->part_first, source->part_count, BUSTER_AARCH64_SEMANTIC_TRANSFORM_PART_COUNT) ||
        !buster_a64_semantic_range(source->value_first, source->value_count, BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_COUNT)) return false;
    *result = (BusterA64SemanticTransform){.id = id, .expression = expression, .source = source->source,
                                           .p0 = source->p0, .p1 = source->p1, .part_first = source->part_first, .value_first = source->value_first,
                                           .part_count = source->part_count, .value_count = source->value_count, .kind = source->kind,
                                           .invertible = source->invertible != 0, .reserved = source->reserved};
    return true;
}

bool buster_a64_semantic_transform_part(u32 id, u32 ordinal, BusterA64SemanticString* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT) return false;
    BusterA64SemanticGeneratedTransform const* transform = buster_a64_semantic_transforms + id;
    if (ordinal >= transform->part_count || !buster_a64_semantic_range(transform->part_first, transform->part_count, BUSTER_AARCH64_SEMANTIC_TRANSFORM_PART_COUNT)) return false;
    return buster_a64_semantic_string_offset(buster_a64_semantic_transform_parts[transform->part_first + ordinal].offset, result);
}

bool buster_a64_semantic_transform_value(u32 id, u32 ordinal, BusterA64SemanticValue* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT) return false;
    BusterA64SemanticGeneratedTransform const* transform = buster_a64_semantic_transforms + id;
    if (ordinal >= transform->value_count || !buster_a64_semantic_range(transform->value_first, transform->value_count, BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_COUNT)) return false;
    BusterA64SemanticGeneratedValueEntry const* value = buster_a64_semantic_value_entries + transform->value_first + ordinal;
    if (!buster_a64_semantic_range(value->key_first, value->key_count, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT) ||
        !buster_a64_semantic_range(value->result_first, value->result_count, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT) || value->result_count != 1) return false;
    *result = (BusterA64SemanticValue){.id = transform->value_first + ordinal, .key_first = value->key_first, .result_first = value->result_first, .key_count = value->key_count, .result_count = value->result_count};
    return true;
}

bool buster_a64_semantic_value_atom(u32 id, BusterA64SemanticValueAtom* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT) return false;
    BusterA64SemanticGeneratedValueAtom const* atom = buster_a64_semantic_value_atoms + id;
    BusterA64SemanticString text;
    if (!buster_a64_semantic_string_offset(atom->text_offset, &text) || atom->type > BUSTER_A64_SEMANTIC_VALUE_PROGRAM) return false;
    *result = (BusterA64SemanticValueAtom){.id = id, .kind = atom->type, .integer = atom->integer, .text = text};
    return true;
}

static bool buster_a64_semantic_string_equals(BusterA64SemanticString actual, String8 expected)
{
    if (actual.length != expected.length) return false;
    for (u32 index = 0; index < actual.length; index += 1)
    {
        if (buster_a64_semantic_string_pool[actual.offset + index] != expected.pointer[index]) return false;
    }
    return true;
}

bool buster_a64_semantic_find_form(String8 name, u32 ordinal, u32* id)
{
    if (!id || (!name.pointer && name.length)) return false;
    u32 found = 0;
    for (u32 index = 0; index < BUSTER_AARCH64_SEMANTIC_FORM_COUNT; index += 1)
    {
        BusterA64SemanticString candidate;
        if (buster_a64_semantic_string_offset(buster_a64_semantic_forms[index].name_offset, &candidate) &&
            buster_a64_semantic_string_equals(candidate, name))
        {
            if (found == ordinal) { *id = index; return true; }
            found += 1;
        }
    }
    return false;
}

bool buster_a64_semantic_find_mnemonic(String8 value, u32 ordinal, u32* id)
{
    if (!id || (!value.pointer && value.length)) return false;
    u32 found = 0;
    for (u32 index = 0; index < BUSTER_AARCH64_SEMANTIC_FORM_COUNT; index += 1)
    {
        BusterA64SemanticString candidate;
        if (buster_a64_semantic_string_offset(buster_a64_semantic_forms[index].mnemonic_offset, &candidate) &&
            buster_a64_semantic_string_equals(candidate, value))
        {
            if (found == ordinal) { *id = index; return true; }
            found += 1;
        }
    }
    return false;
}

static bool buster_a64_semantic_validate_segment(BusterA64SemanticGeneratedSegment const* segment)
{
    return segment->width > 0 && segment->width <= 32 && segment->instruction_lsb < 32 &&
           segment->value_lsb < 32 && segment->instruction_lsb + segment->width <= 32 && segment->value_lsb + segment->width <= 32;
}

static bool buster_a64_semantic_validate_transform(u32 index)
{
    BusterA64SemanticGeneratedTransform const* transform = buster_a64_semantic_transforms + index;
    if (transform->kind > BUSTER_A64_SEMANTIC_TRANSFORM_SHARED_DECODE ||
        !buster_a64_semantic_range(transform->part_first, transform->part_count, BUSTER_AARCH64_SEMANTIC_TRANSFORM_PART_COUNT) ||
        !buster_a64_semantic_range(transform->value_first, transform->value_count, BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_COUNT)) return false;
    for (u32 part = 0; part < transform->part_count; part += 1)
    {
        BusterA64SemanticString ignored;
        if (!buster_a64_semantic_string_offset(buster_a64_semantic_transform_parts[transform->part_first + part].offset, &ignored)) return false;
    }
    for (u32 value_index = 0; value_index < transform->value_count; value_index += 1)
    {
        BusterA64SemanticGeneratedValueEntry const* value = buster_a64_semantic_value_entries + transform->value_first + value_index;
        if (value->key_count == 0 || value->result_count != 1 ||
            !buster_a64_semantic_range(value->key_first, value->key_count, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT) ||
            !buster_a64_semantic_range(value->result_first, value->result_count, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT)) return false;
        for (u32 atom_index = 0; atom_index < value->key_count; atom_index += 1)
        {
            BusterA64SemanticGeneratedValueAtom const* atom = buster_a64_semantic_value_atoms + value->key_first + atom_index;
            BusterA64SemanticString ignored;
            if (atom->type > BUSTER_A64_SEMANTIC_VALUE_PROGRAM || !buster_a64_semantic_string_offset(atom->text_offset, &ignored)) return false;
        }
        BusterA64SemanticGeneratedValueAtom const* result_atom = buster_a64_semantic_value_atoms + value->result_first;
        BusterA64SemanticString ignored;
        if (result_atom->type > BUSTER_A64_SEMANTIC_VALUE_PROGRAM || !buster_a64_semantic_string_offset(result_atom->text_offset, &ignored)) return false;
    }
    return true;
}

bool buster_a64_semantic_validate(void)
{
    u32 owner_counts[BUSTER_A64_SEMANTIC_OWNER_COUNT] = {0};
    u32 undefined_count = 0;
    bool valid = true;
    for (u32 index = 0; index < BUSTER_AARCH64_SEMANTIC_SEGMENT_COUNT; index += 1)
    {
        BusterA64SemanticGeneratedSegment segment = {0};
        valid = valid && buster_a64_semantic_generated_segment(index, &segment) && buster_a64_semantic_validate_segment(&segment);
    }
    u32 transform_index = 0;
    for (; transform_index < BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT; transform_index += 1) valid = valid && buster_a64_semantic_validate_transform(transform_index);
    for (u32 field_id_index = 0; field_id_index < BUSTER_AARCH64_SEMANTIC_FIELD_COUNT; field_id_index += 1)
    {
        BusterA64SemanticGeneratedField field_value = {0};
        valid = valid && buster_a64_semantic_generated_field(field_id_index, &field_value);
        BusterA64SemanticGeneratedField const* field = &field_value;
        valid = valid && field->segment_count > 0 && field->width <= 32 && buster_a64_semantic_range(field->segment_first, field->segment_count, BUSTER_AARCH64_SEMANTIC_SEGMENT_COUNT);
        u32 source_mask = 0;
        for (u32 segment_index = 0; segment_index < field->segment_count; segment_index += 1)
        {
            BusterA64SemanticGeneratedSegment segment = {0};
            valid = valid && buster_a64_semantic_generated_segment(field->segment_first + segment_index, &segment);
            u32 mask = segment.width == 32 ? UINT32_MAX : ((UINT32_C(1) << segment.width) - 1);
            source_mask |= mask << segment.value_lsb;
        }
        valid = valid && source_mask == field->source_mask;
    }
    for (u32 index = 0; index < BUSTER_AARCH64_SEMANTIC_FORM_COUNT; index += 1)
    {
        BusterA64SemanticGeneratedForm const* form = buster_a64_semantic_forms + index;
        valid = valid && form->fixed_value == (form->fixed_value & form->fixed_mask) &&
                buster_a64_semantic_range(form->field_first, form->field_count, BUSTER_AARCH64_SEMANTIC_FIELD_COUNT) &&
                buster_a64_semantic_range(form->operand_first, form->operand_count, BUSTER_AARCH64_SEMANTIC_OPERAND_COUNT) &&
                buster_a64_semantic_range(form->transform_first, form->transform_count, BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT);
        if (form->owner < BUSTER_A64_SEMANTIC_OWNER_COUNT) owner_counts[form->owner] += 1; else valid = false;
        if (form->status == BUSTER_A64_SEMANTIC_STATUS_UNDEFINED) undefined_count += 1;
        for (u32 field_index = 0; field_index < form->field_count; field_index += 1)
        {
            BusterA64SemanticGeneratedField field_value = {0};
            valid = valid && buster_a64_semantic_generated_field(form->field_first + field_index, &field_value);
            BusterA64SemanticGeneratedField const* field = &field_value;
            BusterA64SemanticString ignored;
            valid = valid && buster_a64_semantic_string_offset(field->name_offset, &ignored);
        }
        for (u32 operand_index = 0; operand_index < form->operand_count; operand_index += 1)
        {
            BusterA64SemanticGeneratedOperand operand_value = {0};
            valid = valid && buster_a64_semantic_generated_operand(form->operand_first + operand_index, &operand_value);
            BusterA64SemanticGeneratedOperand const* operand = &operand_value;
            valid = valid && operand->form_id == index && operand->kind_mask != 0 && (operand->kind_mask & ~((UINT32_C(1) << 25) - 1)) == 0 &&
                    operand->classification_status == BUSTER_A64_SEMANTIC_CLASSIFICATION_PRESENTATION_ONLY && operand->reserved == 0 &&
                    buster_a64_semantic_range(operand->field_index_first, operand->field_index_count, BUSTER_AARCH64_SEMANTIC_OPERAND_FIELD_INDEX_COUNT) &&
                    buster_a64_semantic_range(operand->transform_first, operand->transform_count, BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT);
            for (u32 ref = 0; ref < operand->field_index_count; ref += 1)
            {
                valid = valid && buster_a64_semantic_operand_field_indices[operand->field_index_first + ref] < BUSTER_AARCH64_SEMANTIC_FIELD_COUNT;
            }
        }
    }
    valid = valid && owner_counts[BUSTER_A64_SEMANTIC_OWNER_ALIAS] == 172 &&
            owner_counts[BUSTER_A64_SEMANTIC_OWNER_FIXED32] == 32 && owner_counts[BUSTER_A64_SEMANTIC_OWNER_DIRECT_GPR] == 80 &&
            owner_counts[BUSTER_A64_SEMANTIC_OWNER_SCALAR_INTEGER] == 72 && owner_counts[BUSTER_A64_SEMANTIC_OWNER_DIRECT_SIMD] == 390 &&
            owner_counts[BUSTER_A64_SEMANTIC_OWNER_SYSTEM] == 18 && owner_counts[BUSTER_A64_SEMANTIC_OWNER_MEMORY] == 559 &&
            owner_counts[BUSTER_A64_SEMANTIC_OWNER_GENERAL_NONMEMORY] == 24 && owner_counts[BUSTER_A64_SEMANTIC_OWNER_COMPLEX_SIMD_FP] == 348 &&
            undefined_count == 1;
    return valid;
}

bool arm_a64_semantic_import_validate(void)
{
    return buster_a64_semantic_validate();
}
