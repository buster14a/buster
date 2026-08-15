#include <buster/lib/compiler/assembly/aarch64_semantics.h>
#include <buster/lib/os.h>

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

BUSTER_CT_CHECK(BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_BLOB_SIZE ==
                BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT * BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_RECORD_BYTES);
BUSTER_CT_CHECK(BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_BLOB_SIZE ==
                BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_COUNT * BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_RECORD_BYTES);

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

// The generated semantic tables ship base64-encoded in the source, so reading
// one byte used to decode a whole four-character group -- a divide and a
// modulo by three plus four character lookups -- and the u16/u32/u64 readers
// redecoded the same group two, four and eight times over. That made
// buster_a64_semantic_blob_byte 16.1% of the test suite.
//
// Each blob is now decoded once into plain bytes and read by index. The
// decode is a pure function of `static const` generated data, so the result
// is fixed for the life of the process; see the module rule in AGENTS.md.
// Costs about 0.91 MB of zero-initialized storage, paid only by callers that
// prewarm.
BUSTER_GLOBAL_LOCAL u8 buster_a64_semantic_segment_bytes[BUSTER_AARCH64_SEMANTIC_SEGMENT_BLOB_SIZE];
BUSTER_GLOBAL_LOCAL u8 buster_a64_semantic_field_bytes[BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE];
BUSTER_GLOBAL_LOCAL u8 buster_a64_semantic_operand_bytes[BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE];
BUSTER_GLOBAL_LOCAL u8 buster_a64_semantic_value_atom_bytes[BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_BLOB_SIZE];
BUSTER_GLOBAL_LOCAL u8 buster_a64_semantic_value_entry_bytes[BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_BLOB_SIZE];
// Read last by every accessor and written last by the prewarm. While it is
// false the decoded arrays are still zeroed, so the accessors must not touch
// them -- an unprewarmed caller takes the original decode and gets the same
// answer at the old cost.
BUSTER_GLOBAL_LOCAL bool buster_a64_semantic_bytes_ready;

static u8 buster_a64_semantic_blob_decode(char8 const* blob, u32 blob_size, u32 index)
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

static u8 buster_a64_semantic_blob_byte(char8 const* blob, u8 const* bytes, u32 blob_size, u32 index)
{
    if (index >= blob_size) return 0;
    if (buster_a64_semantic_bytes_ready) return bytes[index];
    return buster_a64_semantic_blob_decode(blob, blob_size, index);
}

BUSTER_GLOBAL_LOCAL void buster_a64_semantic_blob_fill(char8 const* blob, u8* bytes, u32 blob_size)
{
    for (u32 index = 0; index < blob_size; index += 1)
    {
        bytes[index] = buster_a64_semantic_blob_decode(blob, blob_size, index);
    }
}

// Decode every semantic blob on the calling thread and publish the ready flag
// last. Call before `lane_run`: the aarch64 semantic suites read these from
// every lane, and the fill is an unsynchronized write to shared state.
void buster_aarch64_semantics_prewarm(void)
{
    if (buster_a64_semantic_bytes_ready)
    {
        return;
    }
    BUSTER_CHECK_SERIAL_INITIALIZATION();
    buster_a64_semantic_blob_fill(buster_a64_semantic_segment_blob, buster_a64_semantic_segment_bytes,
                                  BUSTER_AARCH64_SEMANTIC_SEGMENT_BLOB_SIZE);
    buster_a64_semantic_blob_fill(buster_a64_semantic_field_blob, buster_a64_semantic_field_bytes,
                                  BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE);
    buster_a64_semantic_blob_fill(buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes,
                                  BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE);
    buster_a64_semantic_blob_fill(buster_a64_semantic_value_atom_blob, buster_a64_semantic_value_atom_bytes,
                                  BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_BLOB_SIZE);
    buster_a64_semantic_blob_fill(buster_a64_semantic_value_entry_blob, buster_a64_semantic_value_entry_bytes,
                                  BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_BLOB_SIZE);
    buster_a64_semantic_bytes_ready = true;
}

static u16 buster_a64_semantic_blob_u16(char8 const* blob, u8 const* bytes, u32 blob_size, u32 offset)
{
    return (u16)((u32)buster_a64_semantic_blob_byte(blob, bytes, blob_size, offset) |
                 ((u32)buster_a64_semantic_blob_byte(blob, bytes, blob_size, offset + 1) << 8));
}

static u32 buster_a64_semantic_blob_u32(char8 const* blob, u8 const* bytes, u32 blob_size, u32 offset)
{
    return (u32)buster_a64_semantic_blob_byte(blob, bytes, blob_size, offset) |
           (u32)buster_a64_semantic_blob_byte(blob, bytes, blob_size, offset + 1) << 8 |
           (u32)buster_a64_semantic_blob_byte(blob, bytes, blob_size, offset + 2) << 16 |
           (u32)buster_a64_semantic_blob_byte(blob, bytes, blob_size, offset + 3) << 24;
}

static u64 buster_a64_semantic_blob_u64(char8 const* blob, u8 const* bytes, u32 blob_size, u32 offset)
{
    u64 value = 0;
    u32 byte_index = 0;
    for (; byte_index < 8; byte_index += 1)
        value |= (u64)buster_a64_semantic_blob_byte(blob, bytes, blob_size, offset + byte_index) << (8 * byte_index);
    return value;
}

static bool buster_a64_semantic_generated_operand(u32 id, BusterA64SemanticGeneratedOperand* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_OPERAND_COUNT) return false;
    u32 offset = id * BUSTER_AARCH64_SEMANTIC_OPERAND_RECORD_BYTES;
    *result = (BusterA64SemanticGeneratedOperand){
        .form_id = buster_a64_semantic_blob_u32(buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 0),
        .link_offset = buster_a64_semantic_blob_u32(buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 4),
        .symbol_offset = buster_a64_semantic_blob_u32(buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 8),
        .field_first = buster_a64_semantic_blob_u32(buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 12),
        .field_index_first = buster_a64_semantic_blob_u32(buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 16),
        .transform_first = buster_a64_semantic_blob_u32(buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 20),
        .kind_mask = buster_a64_semantic_blob_u32(buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 24),
        .flags = buster_a64_semantic_blob_u64(buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 28),
        .field_count = buster_a64_semantic_blob_u16(buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 36),
        .field_index_count = buster_a64_semantic_blob_u16(buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 38),
        .transform_count = buster_a64_semantic_blob_u16(buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 40),
        .kind = buster_a64_semantic_blob_byte(buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 42),
        .position = buster_a64_semantic_blob_byte(buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 43),
        .classification_status = buster_a64_semantic_blob_byte(buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 44),
        .reserved = buster_a64_semantic_blob_byte(buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 45),
        .role_offset = buster_a64_semantic_blob_u32(buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 46),
        .direction_offset = buster_a64_semantic_blob_u32(buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 50),
    };
    return true;
}

static bool buster_a64_semantic_generated_segment(u32 id, BusterA64SemanticGeneratedSegment* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_SEGMENT_COUNT) return false;
    u32 offset = id * BUSTER_AARCH64_SEMANTIC_SEGMENT_RECORD_BYTES;
    result->instruction_lsb = buster_a64_semantic_blob_byte(buster_a64_semantic_segment_blob, buster_a64_semantic_segment_bytes, BUSTER_AARCH64_SEMANTIC_SEGMENT_BLOB_SIZE, offset + 0);
    result->width = buster_a64_semantic_blob_byte(buster_a64_semantic_segment_blob, buster_a64_semantic_segment_bytes, BUSTER_AARCH64_SEMANTIC_SEGMENT_BLOB_SIZE, offset + 1);
    result->value_lsb = buster_a64_semantic_blob_byte(buster_a64_semantic_segment_blob, buster_a64_semantic_segment_bytes, BUSTER_AARCH64_SEMANTIC_SEGMENT_BLOB_SIZE, offset + 2);
    result->reserved = buster_a64_semantic_blob_byte(buster_a64_semantic_segment_blob, buster_a64_semantic_segment_bytes, BUSTER_AARCH64_SEMANTIC_SEGMENT_BLOB_SIZE, offset + 3);
    return true;
}

static bool buster_a64_semantic_generated_field(u32 id, BusterA64SemanticGeneratedField* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_FIELD_COUNT) return false;
    u32 offset = id * BUSTER_AARCH64_SEMANTIC_FIELD_RECORD_BYTES;
    result->name_offset = buster_a64_semantic_blob_u32(buster_a64_semantic_field_blob, buster_a64_semantic_field_bytes, BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE, offset + 0);
    result->source_mask = buster_a64_semantic_blob_u32(buster_a64_semantic_field_blob, buster_a64_semantic_field_bytes, BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE, offset + 4);
    result->segment_first = buster_a64_semantic_blob_u32(buster_a64_semantic_field_blob, buster_a64_semantic_field_bytes, BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE, offset + 8);
    result->segment_count = buster_a64_semantic_blob_u16(buster_a64_semantic_field_blob, buster_a64_semantic_field_bytes, BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE, offset + 12);
    result->width = buster_a64_semantic_blob_byte(buster_a64_semantic_field_blob, buster_a64_semantic_field_bytes, BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE, offset + 14);
    result->reserved = buster_a64_semantic_blob_byte(buster_a64_semantic_field_blob, buster_a64_semantic_field_bytes, BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE, offset + 15);
    return true;
}

static bool buster_a64_semantic_generated_value_atom(u32 id, BusterA64SemanticGeneratedValueAtom* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT) return false;
    u32 offset = id * BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_RECORD_BYTES;
    result->text_offset = buster_a64_semantic_blob_u32(buster_a64_semantic_value_atom_blob, buster_a64_semantic_value_atom_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_BLOB_SIZE, offset + 0);
    result->integer = (s64)buster_a64_semantic_blob_u64(buster_a64_semantic_value_atom_blob, buster_a64_semantic_value_atom_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_BLOB_SIZE, offset + 4);
    result->program_first = buster_a64_semantic_blob_u32(buster_a64_semantic_value_atom_blob, buster_a64_semantic_value_atom_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_BLOB_SIZE, offset + 12);
    result->program_count = buster_a64_semantic_blob_u16(buster_a64_semantic_value_atom_blob, buster_a64_semantic_value_atom_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_BLOB_SIZE, offset + 16);
    result->type = buster_a64_semantic_blob_byte(buster_a64_semantic_value_atom_blob, buster_a64_semantic_value_atom_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_BLOB_SIZE, offset + 18);
    result->reserved[0] = buster_a64_semantic_blob_byte(buster_a64_semantic_value_atom_blob, buster_a64_semantic_value_atom_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_BLOB_SIZE, offset + 19);
    return true;
}

static bool buster_a64_semantic_generated_value_entry(u32 id, BusterA64SemanticGeneratedValueEntry* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_COUNT) return false;
    u32 offset = id * BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_RECORD_BYTES;
    result->key_first = buster_a64_semantic_blob_u32(buster_a64_semantic_value_entry_blob, buster_a64_semantic_value_entry_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_BLOB_SIZE, offset + 0);
    result->result_first = buster_a64_semantic_blob_u32(buster_a64_semantic_value_entry_blob, buster_a64_semantic_value_entry_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_BLOB_SIZE, offset + 4);
    result->key_count = buster_a64_semantic_blob_u16(buster_a64_semantic_value_entry_blob, buster_a64_semantic_value_entry_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_BLOB_SIZE, offset + 8);
    result->result_count = buster_a64_semantic_blob_u16(buster_a64_semantic_value_entry_blob, buster_a64_semantic_value_entry_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_BLOB_SIZE, offset + 10);
    return true;
}

static bool buster_a64_semantic_generated_table_header(u32 id, BusterA64SemanticGeneratedTableHeader* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_TABLE_COUNT) return false;
    *result = buster_a64_semantic_table_headers[id];
    return true;
}

static bool buster_a64_semantic_generated_alias(u32 form_id, BusterA64SemanticGeneratedAlias* result)
{
    if (!result || form_id >= BUSTER_AARCH64_SEMANTIC_FORM_COUNT) return false;
    *result = buster_a64_semantic_aliases[form_id];
    return true;
}

static bool buster_a64_semantic_generated_constraint(u32 form_id, BusterA64SemanticGeneratedConstraint* result)
{
    if (!result || form_id >= BUSTER_AARCH64_SEMANTIC_CONSTRAINT_COUNT) return false;
    *result = buster_a64_semantic_constraints[form_id];
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
u32 buster_a64_semantic_program_instruction_count(void) { return BUSTER_AARCH64_SEMANTIC_PROGRAM_INSTRUCTION_COUNT; }
u32 buster_a64_semantic_program_operand_count(void) { return BUSTER_AARCH64_SEMANTIC_PROGRAM_OPERAND_COUNT; }
u32 buster_a64_semantic_parsed_program_count(void) { return BUSTER_AARCH64_SEMANTIC_PARSED_PROGRAM_COUNT; }
u32 buster_a64_semantic_value_program_count(void) { return BUSTER_AARCH64_SEMANTIC_VALUE_PROGRAM_COUNT; }
u32 buster_a64_semantic_value_count(void) { return BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_COUNT; }
u32 buster_a64_semantic_value_atom_count(void) { return BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT; }
u32 buster_a64_semantic_string_pool_size(void) { return BUSTER_AARCH64_SEMANTIC_STRING_POOL_SIZE; }
u32 buster_a64_semantic_table_header_count(void) { return BUSTER_AARCH64_SEMANTIC_TABLE_COUNT; }
u32 buster_a64_semantic_table_key_header_count(void) { return BUSTER_AARCH64_SEMANTIC_TABLE_KEY_HEADER_COUNT; }
u32 buster_a64_semantic_alias_count(void) { return BUSTER_AARCH64_SEMANTIC_ALIAS_COUNT; }
u32 buster_a64_semantic_alias_condition_token_count(void) { return BUSTER_AARCH64_SEMANTIC_ALIAS_CONDITION_TOKEN_COUNT; }
u32 buster_a64_semantic_alias_preference_condition_token_count(void) { return BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_CONDITION_TOKEN_COUNT; }
u32 buster_a64_semantic_alias_preference_count(void) { return BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_COUNT; }
u32 buster_a64_semantic_constraint_count(void) { return BUSTER_AARCH64_SEMANTIC_CONSTRAINT_COUNT; }
u32 buster_a64_semantic_constraint_feature_tag_count(void) { return BUSTER_AARCH64_SEMANTIC_CONSTRAINT_FEATURE_TAG_COUNT; }
u32 buster_a64_semantic_constraint_program_token_count(void) { return BUSTER_AARCH64_SEMANTIC_CONSTRAINT_PROGRAM_TOKEN_COUNT; }

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

static bool buster_a64_semantic_program_string(u32 offset, BusterA64SemanticString* result, bool require_nonempty)
{
    if (!buster_a64_semantic_string_offset(offset, result)) return false;
    return !require_nonempty || result->length != 0;
}

static bool buster_a64_semantic_program_slice(u16 high, u16 low, u16 width)
{
    if ((high == UINT16_MAX) != (low == UINT16_MAX)) return false;
    if (high == UINT16_MAX) return true;
    return high < 64 && low <= high && width == (u16)(high - low + 1);
}

static bool buster_a64_semantic_validate_program_operand(BusterA64SemanticGeneratedProgramOperand const* operand)
{
    if (!operand || operand->kind > BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_LITERAL ||
        !buster_a64_semantic_program_slice(operand->high, operand->low, operand->width) || operand->reserved != 0) return false;
    BusterA64SemanticString ignored;
    if (!buster_a64_semantic_string_offset(operand->text_offset, &ignored)) return false;
    if (operand->kind == BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_FIELD)
    {
        return buster_a64_semantic_program_string(operand->field_offset, &ignored, true) &&
               (operand->high != UINT16_MAX || operand->width == 0);
    }
    if (operand->kind == BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_ARRANGEMENT)
    {
        return operand->high == UINT16_MAX && operand->low == UINT16_MAX && operand->width == 0 &&
               operand->field_offset == UINT32_MAX && ignored.length != 0;
    }
    return operand->high == UINT16_MAX && operand->low == UINT16_MAX && operand->width == 0 && operand->field_offset == UINT32_MAX;
}

static bool buster_a64_semantic_validate_program_instruction(u32 id)
{
    if (id >= BUSTER_AARCH64_SEMANTIC_PROGRAM_INSTRUCTION_COUNT) return false;
    BusterA64SemanticGeneratedProgramInstruction const* instruction = buster_a64_semantic_program_instructions + id;
    if (instruction->op > BUSTER_A64_SEMANTIC_PROGRAM_SHARED_DECODE || instruction->reserved[0] != 0 || instruction->reserved[1] != 0 || instruction->reserved[2] != 0 ||
        !buster_a64_semantic_range(instruction->operand_first, instruction->operand_count, BUSTER_AARCH64_SEMANTIC_PROGRAM_OPERAND_COUNT) ||
        !buster_a64_semantic_program_slice(instruction->high, instruction->low, instruction->width)) return false;
    BusterA64SemanticString ignored;
    if (!buster_a64_semantic_string_offset(instruction->text_offset, &ignored)) return false;
    bool field_op = instruction->op == BUSTER_A64_SEMANTIC_PROGRAM_FIELD || instruction->op == BUSTER_A64_SEMANTIC_PROGRAM_REGISTER_ADD_MOD;
    if (field_op)
    {
        if (!buster_a64_semantic_program_string(instruction->field_offset, &ignored, true)) return false;
    }
    else if (instruction->field_offset != UINT32_MAX) return false;
    switch (instruction->op)
    {
        case BUSTER_A64_SEMANTIC_PROGRAM_FIELD:
            return instruction->operand_count == 0 && instruction->value == 0 && instruction->modulus == 0 &&
                   (instruction->high != UINT16_MAX || instruction->width == 0);
        case BUSTER_A64_SEMANTIC_PROGRAM_UINT_CONCAT:
            if (instruction->operand_count == 0) return false;
            for (u32 index = 0; index < instruction->operand_count; index += 1)
            {
                BusterA64SemanticGeneratedProgramOperand const* operand = buster_a64_semantic_program_operands + instruction->operand_first + index;
                if (!buster_a64_semantic_validate_program_operand(operand) || operand->kind == BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_ARRANGEMENT) return false;
            }
            return instruction->width == 0 && instruction->value == 0 && instruction->modulus == 0;
        case BUSTER_A64_SEMANTIC_PROGRAM_SIGN_EXTEND:
            return instruction->operand_count == 0 && instruction->high == UINT16_MAX && instruction->low == UINT16_MAX && instruction->width > 0 && instruction->width <= 64 && instruction->value == 0 && instruction->modulus == 0;
        case BUSTER_A64_SEMANTIC_PROGRAM_SCALE_MUL:
        case BUSTER_A64_SEMANTIC_PROGRAM_SCALE_DIV:
            return instruction->operand_count == 0 && instruction->width == 0 && instruction->modulus == 0 && instruction->value > 0;
        case BUSTER_A64_SEMANTIC_PROGRAM_SCALE_POW2:
        case BUSTER_A64_SEMANTIC_PROGRAM_ADD_CONST:
        case BUSTER_A64_SEMANTIC_PROGRAM_SUB_FROM_CONST:
        case BUSTER_A64_SEMANTIC_PROGRAM_LITERAL:
            return instruction->operand_count == 0 && instruction->width == 0 && instruction->modulus == 0;
        case BUSTER_A64_SEMANTIC_PROGRAM_REGISTER_ADD_MOD:
            return instruction->operand_count == 0 && instruction->high == UINT16_MAX && instruction->low == UINT16_MAX && instruction->width == 0 && instruction->modulus == 32 && instruction->value > 0 && instruction->value <= 3;
        case BUSTER_A64_SEMANTIC_PROGRAM_TEXT_FACTOR:
            return instruction->operand_count == 0 && instruction->text_offset != 0 && ignored.length != 0 && instruction->width == 0 && instruction->value == 0 && instruction->modulus == 0;
        case BUSTER_A64_SEMANTIC_PROGRAM_SHARED_DECODE:
            if (instruction->operand_count == 0 || instruction->width != 0 || instruction->value != 0 || instruction->modulus != 0) return false;
            bool saw_field = false;
            bool saw_arrangement = false;
            for (u32 index = 0; index < instruction->operand_count; index += 1)
            {
                BusterA64SemanticGeneratedProgramOperand const* operand = buster_a64_semantic_program_operands + instruction->operand_first + index;
                if (!buster_a64_semantic_validate_program_operand(operand) || operand->kind == BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_LITERAL) return false;
                saw_field = saw_field || operand->kind == BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_FIELD;
                saw_arrangement = saw_arrangement || operand->kind == BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_ARRANGEMENT;
            }
            return saw_field && saw_arrangement;
    }
    return false;
}

static bool buster_a64_semantic_program_span(u32 first, u32 count)
{
    return count == 0 ? first == UINT32_MAX : buster_a64_semantic_range(first, count, BUSTER_AARCH64_SEMANTIC_PROGRAM_INSTRUCTION_COUNT);
}

bool buster_a64_semantic_program_instruction(u32 id, BusterA64SemanticProgramInstruction* result)
{
    if (!result || !buster_a64_semantic_validate_program_instruction(id)) return false;
    BusterA64SemanticGeneratedProgramInstruction const* source = buster_a64_semantic_program_instructions + id;
    BusterA64SemanticString field = {0}, text = {0};
    if (!buster_a64_semantic_string_offset(source->text_offset, &text) ||
        (source->field_offset != UINT32_MAX && !buster_a64_semantic_string_offset(source->field_offset, &field))) return false;
    if (source->field_offset == UINT32_MAX) field = (BusterA64SemanticString){0};
    *result = (BusterA64SemanticProgramInstruction){.id = id, .field = field, .text = text, .operand_first = source->operand_first,
                                                    .operand_count = source->operand_count, .value = source->value, .high = source->high,
                                                    .low = source->low, .width = source->width, .modulus = source->modulus, .op = source->op};
    return true;
}

bool buster_a64_semantic_program_operand(u32 id, u32 ordinal, BusterA64SemanticProgramOperand* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_PROGRAM_INSTRUCTION_COUNT || !buster_a64_semantic_validate_program_instruction(id)) return false;
    BusterA64SemanticGeneratedProgramInstruction const* instruction = buster_a64_semantic_program_instructions + id;
    if (ordinal >= instruction->operand_count) return false;
    u32 operand_id = instruction->operand_first + ordinal;
    BusterA64SemanticGeneratedProgramOperand const* source = buster_a64_semantic_program_operands + operand_id;
    BusterA64SemanticString field = {0}, text = {0};
    if (!buster_a64_semantic_validate_program_operand(source) || !buster_a64_semantic_string_offset(source->text_offset, &text)) return false;
    if (source->field_offset != UINT32_MAX && !buster_a64_semantic_string_offset(source->field_offset, &field)) return false;
    *result = (BusterA64SemanticProgramOperand){.id = operand_id, .field = field, .text = text, .value = source->value,
                                                .high = source->high, .low = source->low, .width = source->width, .kind = source->kind};
    return true;
}

bool buster_a64_semantic_transform_program_instruction(u32 transform_id, u32 ordinal, BusterA64SemanticProgramInstruction* result)
{
    if (!result || transform_id >= BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT) return false;
    BusterA64SemanticGeneratedTransform const* transform = buster_a64_semantic_transforms + transform_id;
    if (ordinal >= transform->program_count || !buster_a64_semantic_program_span(transform->program_first, transform->program_count)) return false;
    return buster_a64_semantic_program_instruction(transform->program_first + ordinal, result);
}

bool buster_a64_semantic_transform(u32 id, BusterA64SemanticTransform* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT) return false;
    BusterA64SemanticGeneratedTransform const* source = buster_a64_semantic_transforms + id;
    BusterA64SemanticString expression;
    if (!buster_a64_semantic_string_offset(source->expression_offset, &expression)) return false;
    if (source->kind == BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE && source->table_id >= BUSTER_AARCH64_SEMANTIC_TABLE_COUNT) return false;
    if (source->kind != BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE && source->table_id != 0xffffffffu) return false;
    if (!buster_a64_semantic_program_span(source->program_first, source->program_count)) return false;
    if (!buster_a64_semantic_range(source->part_first, source->part_count, BUSTER_AARCH64_SEMANTIC_TRANSFORM_PART_COUNT) ||
        !buster_a64_semantic_range(source->value_first, source->value_count, BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_COUNT)) return false;
    *result = (BusterA64SemanticTransform){.id = id, .expression = expression, .source = source->source,
                                           .p0 = source->p0, .p1 = source->p1, .table_id = source->table_id, .program_first = source->program_first,
                                           .part_first = source->part_first, .value_first = source->value_first,
                                           .program_count = source->program_count, .part_count = source->part_count, .value_count = source->value_count, .kind = source->kind,
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
    BusterA64SemanticGeneratedValueEntry value = {0};
    if (!buster_a64_semantic_generated_value_entry(transform->value_first + ordinal, &value) ||
        !buster_a64_semantic_range(value.key_first, value.key_count, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT) ||
        !buster_a64_semantic_range(value.result_first, value.result_count, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT) || value.result_count != 1) return false;
    *result = (BusterA64SemanticValue){.id = transform->value_first + ordinal, .key_first = value.key_first, .result_first = value.result_first, .key_count = value.key_count, .result_count = value.result_count};
    return true;
}

bool buster_a64_semantic_value_atom(u32 id, BusterA64SemanticValueAtom* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT) return false;
    BusterA64SemanticGeneratedValueAtom atom = {0};
    if (!buster_a64_semantic_generated_value_atom(id, &atom)) return false;
    BusterA64SemanticString text;
    bool string_ok = buster_a64_semantic_string_offset(atom.text_offset, &text);
    bool type_ok = atom.type <= BUSTER_A64_SEMANTIC_VALUE_PROGRAM;
    bool span_ok = buster_a64_semantic_program_span(atom.program_first, atom.program_count);
    bool kind_span_ok = atom.type == BUSTER_A64_SEMANTIC_VALUE_PROGRAM ? atom.program_count != 0 : atom.program_count == 0;
    if (!string_ok || !type_ok || !span_ok || !kind_span_ok)
        return false;
    *result = (BusterA64SemanticValueAtom){.id = id, .kind = atom.type, .integer = atom.integer, .text = text,
                                           .program_first = atom.program_first, .program_count = atom.program_count};
    return true;
}

bool buster_a64_semantic_value_atom_program_instruction(u32 atom_id, u32 ordinal, BusterA64SemanticProgramInstruction* result)
{
    if (!result || atom_id >= BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT) return false;
    BusterA64SemanticGeneratedValueAtom atom = {0};
    if (!buster_a64_semantic_generated_value_atom(atom_id, &atom) || atom.type != BUSTER_A64_SEMANTIC_VALUE_PROGRAM || ordinal >= atom.program_count ||
        !buster_a64_semantic_program_span(atom.program_first, atom.program_count)) return false;
    return buster_a64_semantic_program_instruction(atom.program_first + ordinal, result);
}

bool buster_a64_semantic_table_header(u32 id, BusterA64SemanticTableHeader* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_TABLE_COUNT) return false;
    BusterA64SemanticGeneratedTableHeader source;
    if (!buster_a64_semantic_generated_table_header(id, &source) ||
        !buster_a64_semantic_range(source.key_header_first, source.key_header_count, BUSTER_AARCH64_SEMANTIC_TABLE_KEY_HEADER_COUNT)) return false;
    BusterA64SemanticString result_header;
    if (!buster_a64_semantic_string_offset(source.result_header_offset, &result_header)) return false;
    *result = (BusterA64SemanticTableHeader){.id = id, .key_header_first = source.key_header_first,
                                              .key_header_count = source.key_header_count, .result_header = result_header};
    return true;
}

bool buster_a64_semantic_table_key_header(u32 id, u32 ordinal, BusterA64SemanticString* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_TABLE_COUNT) return false;
    BusterA64SemanticGeneratedTableHeader source;
    if (!buster_a64_semantic_generated_table_header(id, &source) || ordinal >= source.key_header_count ||
        !buster_a64_semantic_range(source.key_header_first, source.key_header_count, BUSTER_AARCH64_SEMANTIC_TABLE_KEY_HEADER_COUNT)) return false;
    return buster_a64_semantic_string_offset(buster_a64_semantic_table_key_headers[source.key_header_first + ordinal], result);
}

bool buster_a64_semantic_table_result_header(u32 id, BusterA64SemanticString* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_TABLE_COUNT) return false;
    BusterA64SemanticGeneratedTableHeader source;
    if (!buster_a64_semantic_generated_table_header(id, &source)) return false;
    return buster_a64_semantic_string_offset(source.result_header_offset, result);
}

bool buster_a64_semantic_transform_table_header(u32 transform_id, u32* table_id)
{
    if (!table_id || transform_id >= BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT) return false;
    BusterA64SemanticGeneratedTransform const* transform = buster_a64_semantic_transforms + transform_id;
    if (transform->kind != BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE || transform->table_id >= BUSTER_AARCH64_SEMANTIC_TABLE_COUNT) return false;
    *table_id = transform->table_id;
    return true;
}

static bool buster_a64_semantic_alias_source(u32 form_id, BusterA64SemanticAlias* result)
{
    if (!result || form_id >= BUSTER_AARCH64_SEMANTIC_FORM_COUNT) return false;
    BusterA64SemanticGeneratedAlias source;
    if (!buster_a64_semantic_generated_alias(form_id, &source) ||
        !buster_a64_semantic_range(source.condition_first, source.condition_count, BUSTER_AARCH64_SEMANTIC_ALIAS_CONDITION_TOKEN_COUNT) ||
        !buster_a64_semantic_range(source.preference_condition_first, source.preference_condition_count, BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_CONDITION_TOKEN_COUNT) ||
        !buster_a64_semantic_range(source.preference_first, source.preference_count, BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_COUNT)) return false;
    BusterA64SemanticString target_file, target_id, target_encoding_id;
    if (!buster_a64_semantic_string_offset(source.target_file_offset, &target_file) ||
        !buster_a64_semantic_string_offset(source.target_id_offset, &target_id) ||
        !buster_a64_semantic_string_offset(source.target_encoding_id_offset, &target_encoding_id)) return false;
    *result = (BusterA64SemanticAlias){.form_id = form_id, .target_file = target_file, .target_id = target_id,
                                       .target_encoding_id = target_encoding_id, .condition_first = source.condition_first,
                                       .preference_condition_first = source.preference_condition_first, .preference_first = source.preference_first,
                                       .condition_count = source.condition_count, .preference_condition_count = source.preference_condition_count,
                                       .preference_count = source.preference_count, .preference_rank = source.preference_rank};
    return true;
}

bool buster_a64_semantic_alias(u32 form_id, BusterA64SemanticAlias* result)
{
    if (!result || form_id >= BUSTER_AARCH64_SEMANTIC_FORM_COUNT || buster_a64_semantic_forms[form_id].kind != BUSTER_A64_SEMANTIC_FORM_ALIAS) return false;
    return buster_a64_semantic_alias_source(form_id, result);
}

bool buster_a64_semantic_alias_descriptor(u32 form_id, BusterA64SemanticAlias* result)
{
    return buster_a64_semantic_alias_source(form_id, result);
}

bool buster_a64_semantic_alias_by_ordinal(u32 ordinal, BusterA64SemanticAlias* result)
{
    if (!result || ordinal >= BUSTER_AARCH64_SEMANTIC_ALIAS_COUNT) return false;
    u32 form_id = buster_a64_semantic_alias_form_ids[ordinal];
    return buster_a64_semantic_alias(form_id, result);
}

bool buster_a64_semantic_alias_condition_token(u32 form_id, u32 ordinal, BusterA64SemanticString* result)
{
    if (!result || form_id >= BUSTER_AARCH64_SEMANTIC_FORM_COUNT || buster_a64_semantic_forms[form_id].kind != BUSTER_A64_SEMANTIC_FORM_ALIAS) return false;
    BusterA64SemanticGeneratedAlias source;
    if (!buster_a64_semantic_generated_alias(form_id, &source) || ordinal >= source.condition_count ||
        !buster_a64_semantic_range(source.condition_first, source.condition_count, BUSTER_AARCH64_SEMANTIC_ALIAS_CONDITION_TOKEN_COUNT)) return false;
    return buster_a64_semantic_string_offset(buster_a64_semantic_alias_condition_tokens[source.condition_first + ordinal], result);
}

bool buster_a64_semantic_alias_preference_condition_token(u32 form_id, u32 ordinal, BusterA64SemanticString* result)
{
    if (!result || form_id >= BUSTER_AARCH64_SEMANTIC_FORM_COUNT) return false;
    BusterA64SemanticGeneratedAlias source;
    if (!buster_a64_semantic_generated_alias(form_id, &source) || ordinal >= source.preference_condition_count ||
        !buster_a64_semantic_range(source.preference_condition_first, source.preference_condition_count, BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_CONDITION_TOKEN_COUNT)) return false;
    return buster_a64_semantic_string_offset(buster_a64_semantic_alias_preference_condition_tokens[source.preference_condition_first + ordinal], result);
}

bool buster_a64_semantic_alias_preference(u32 form_id, u32 ordinal, BusterA64SemanticAliasPreference* result)
{
    if (!result || form_id >= BUSTER_AARCH64_SEMANTIC_FORM_COUNT) return false;
    BusterA64SemanticGeneratedAlias alias_source;
    if (!buster_a64_semantic_generated_alias(form_id, &alias_source) || ordinal >= alias_source.preference_count ||
        !buster_a64_semantic_range(alias_source.preference_first, alias_source.preference_count, BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_COUNT)) return false;
    BusterA64SemanticGeneratedAliasPreference const* source = buster_a64_semantic_alias_preferences + alias_source.preference_first + ordinal;
    if (!buster_a64_semantic_range(source->condition_first, source->condition_count, BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_CONDITION_TOKEN_COUNT)) return false;
    BusterA64SemanticString alias_file, alias_id;
    if (!buster_a64_semantic_string_offset(source->alias_file_offset, &alias_file) || !buster_a64_semantic_string_offset(source->alias_id_offset, &alias_id)) return false;
    *result = (BusterA64SemanticAliasPreference){.id = alias_source.preference_first + ordinal, .alias_file = alias_file, .alias_id = alias_id,
                                                  .condition_first = source->condition_first, .condition_count = source->condition_count, .rank = source->rank};
    return true;
}

bool buster_a64_semantic_alias_preference_condition_token_by_id(u32 preference_id, u32 ordinal, BusterA64SemanticString* result)
{
    if (!result || preference_id >= BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_COUNT) return false;
    BusterA64SemanticGeneratedAliasPreference const* preference = buster_a64_semantic_alias_preferences + preference_id;
    if (ordinal >= preference->condition_count || !buster_a64_semantic_range(preference->condition_first, preference->condition_count, BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_CONDITION_TOKEN_COUNT)) return false;
    return buster_a64_semantic_string_offset(buster_a64_semantic_alias_preference_condition_tokens[preference->condition_first + ordinal], result);
}

bool buster_a64_semantic_constraint(u32 form_id, BusterA64SemanticConstraint* result)
{
    if (!result || form_id >= BUSTER_AARCH64_SEMANTIC_CONSTRAINT_COUNT) return false;
    BusterA64SemanticGeneratedConstraint source;
    if (!buster_a64_semantic_generated_constraint(form_id, &source) ||
        !buster_a64_semantic_range(source.feature_first, source.feature_count, BUSTER_AARCH64_SEMANTIC_CONSTRAINT_FEATURE_TAG_COUNT) ||
        !buster_a64_semantic_range(source.program_first, source.program_count, BUSTER_AARCH64_SEMANTIC_CONSTRAINT_PROGRAM_TOKEN_COUNT)) return false;
    *result = (BusterA64SemanticConstraint){.form_id = form_id, .feature_first = source.feature_first, .program_first = source.program_first,
                                            .feature_count = source.feature_count, .program_count = source.program_count};
    return true;
}

bool buster_a64_semantic_constraint_feature_tag(u32 form_id, u32 ordinal, BusterA64SemanticString* result)
{
    if (!result || form_id >= BUSTER_AARCH64_SEMANTIC_CONSTRAINT_COUNT) return false;
    BusterA64SemanticGeneratedConstraint source;
    if (!buster_a64_semantic_generated_constraint(form_id, &source) || ordinal >= source.feature_count ||
        !buster_a64_semantic_range(source.feature_first, source.feature_count, BUSTER_AARCH64_SEMANTIC_CONSTRAINT_FEATURE_TAG_COUNT)) return false;
    return buster_a64_semantic_string_offset(buster_a64_semantic_constraint_feature_tags[source.feature_first + ordinal], result);
}

bool buster_a64_semantic_constraint_program_token(u32 form_id, u32 ordinal, BusterA64SemanticString* result)
{
    if (!result || form_id >= BUSTER_AARCH64_SEMANTIC_CONSTRAINT_COUNT) return false;
    BusterA64SemanticGeneratedConstraint source;
    if (!buster_a64_semantic_generated_constraint(form_id, &source) || ordinal >= source.program_count ||
        !buster_a64_semantic_range(source.program_first, source.program_count, BUSTER_AARCH64_SEMANTIC_CONSTRAINT_PROGRAM_TOKEN_COUNT)) return false;
    return buster_a64_semantic_string_offset(buster_a64_semantic_constraint_program_tokens[source.program_first + ordinal], result);
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
    BusterA64SemanticString expression;
    if (transform->kind > BUSTER_A64_SEMANTIC_TRANSFORM_SHARED_DECODE || !buster_a64_semantic_string_offset(transform->expression_offset, &expression) ||
        !buster_a64_semantic_program_span(transform->program_first, transform->program_count) ||
        !buster_a64_semantic_range(transform->part_first, transform->part_count, BUSTER_AARCH64_SEMANTIC_TRANSFORM_PART_COUNT) ||
        !buster_a64_semantic_range(transform->value_first, transform->value_count, BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_COUNT) ||
        (transform->kind == BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE ? transform->table_id >= BUSTER_AARCH64_SEMANTIC_TABLE_COUNT : transform->table_id != 0xffffffffu)) return false;
    for (u32 part = 0; part < transform->part_count; part += 1)
    {
        BusterA64SemanticString ignored;
        if (!buster_a64_semantic_string_offset(buster_a64_semantic_transform_parts[transform->part_first + part].offset, &ignored)) return false;
    }
    for (u32 value_index = 0; value_index < transform->value_count; value_index += 1)
    {
        BusterA64SemanticGeneratedValueEntry value = {0};
        if (!buster_a64_semantic_generated_value_entry(transform->value_first + value_index, &value) || value.key_count == 0 || value.result_count != 1 ||
            !buster_a64_semantic_range(value.key_first, value.key_count, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT) ||
            !buster_a64_semantic_range(value.result_first, value.result_count, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT)) return false;
        for (u32 atom_index = 0; atom_index < value.key_count; atom_index += 1)
        {
            BusterA64SemanticGeneratedValueAtom atom = {0};
            if (!buster_a64_semantic_generated_value_atom(value.key_first + atom_index, &atom)) return false;
            BusterA64SemanticString ignored;
            if (atom.type > BUSTER_A64_SEMANTIC_VALUE_PROGRAM || !buster_a64_semantic_string_offset(atom.text_offset, &ignored) ||
                !buster_a64_semantic_program_span(atom.program_first, atom.program_count) ||
                (atom.type == BUSTER_A64_SEMANTIC_VALUE_PROGRAM ? atom.program_count == 0 : atom.program_count != 0)) return false;
        }
        BusterA64SemanticGeneratedValueAtom result_atom = {0};
        if (!buster_a64_semantic_generated_value_atom(value.result_first, &result_atom)) return false;
        BusterA64SemanticString ignored;
        if (result_atom.type > BUSTER_A64_SEMANTIC_VALUE_PROGRAM || !buster_a64_semantic_string_offset(result_atom.text_offset, &ignored) ||
            !buster_a64_semantic_program_span(result_atom.program_first, result_atom.program_count) ||
            (result_atom.type == BUSTER_A64_SEMANTIC_VALUE_PROGRAM ? result_atom.program_count == 0 : result_atom.program_count != 0)) return false;
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
    for (u32 index = 0; index < BUSTER_AARCH64_SEMANTIC_PROGRAM_OPERAND_COUNT; index += 1)
    {
        valid = valid && buster_a64_semantic_validate_program_operand(buster_a64_semantic_program_operands + index);
    }
    for (u32 index = 0; index < BUSTER_AARCH64_SEMANTIC_PROGRAM_INSTRUCTION_COUNT; index += 1)
    {
        valid = valid && buster_a64_semantic_validate_program_instruction(index);
    }
    u32 transform_index = 0;
    for (; transform_index < BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT; transform_index += 1)
    {
        valid = valid && buster_a64_semantic_validate_transform(transform_index);
    }
    for (u32 table_id = 0; table_id < BUSTER_AARCH64_SEMANTIC_TABLE_COUNT; table_id += 1)
    {
        BusterA64SemanticGeneratedTableHeader const* table = buster_a64_semantic_table_headers + table_id;
        valid = valid && table->key_header_count > 0 && buster_a64_semantic_range(table->key_header_first, table->key_header_count, BUSTER_AARCH64_SEMANTIC_TABLE_KEY_HEADER_COUNT);
        BusterA64SemanticString ignored;
        valid = valid && buster_a64_semantic_string_offset(table->result_header_offset, &ignored);
        for (u32 header = 0; header < table->key_header_count; header += 1)
        {
            valid = valid && buster_a64_semantic_string_offset(buster_a64_semantic_table_key_headers[table->key_header_first + header], &ignored);
        }
    }
    for (u32 token_id = 0; token_id < BUSTER_AARCH64_SEMANTIC_ALIAS_CONDITION_TOKEN_COUNT; token_id += 1)
    {
        BusterA64SemanticString ignored;
        valid = valid && buster_a64_semantic_string_offset(buster_a64_semantic_alias_condition_tokens[token_id], &ignored);
    }
    for (u32 token_id = 0; token_id < BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_CONDITION_TOKEN_COUNT; token_id += 1)
    {
        BusterA64SemanticString ignored;
        valid = valid && buster_a64_semantic_string_offset(buster_a64_semantic_alias_preference_condition_tokens[token_id], &ignored);
    }
    for (u32 form_id = 0; form_id < BUSTER_AARCH64_SEMANTIC_FORM_COUNT; form_id += 1)
    {
        BusterA64SemanticGeneratedAlias const* alias = buster_a64_semantic_aliases + form_id;
        valid = valid && buster_a64_semantic_range(alias->condition_first, alias->condition_count, BUSTER_AARCH64_SEMANTIC_ALIAS_CONDITION_TOKEN_COUNT) &&
                buster_a64_semantic_range(alias->preference_condition_first, alias->preference_condition_count, BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_CONDITION_TOKEN_COUNT) &&
                buster_a64_semantic_range(alias->preference_first, alias->preference_count, BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_COUNT);
        BusterA64SemanticString ignored;
        valid = valid && buster_a64_semantic_string_offset(alias->target_file_offset, &ignored) && buster_a64_semantic_string_offset(alias->target_id_offset, &ignored) && buster_a64_semantic_string_offset(alias->target_encoding_id_offset, &ignored);
        BusterA64SemanticGeneratedConstraint const* constraint = buster_a64_semantic_constraints + form_id;
        valid = valid && buster_a64_semantic_range(constraint->feature_first, constraint->feature_count, BUSTER_AARCH64_SEMANTIC_CONSTRAINT_FEATURE_TAG_COUNT) &&
                buster_a64_semantic_range(constraint->program_first, constraint->program_count, BUSTER_AARCH64_SEMANTIC_CONSTRAINT_PROGRAM_TOKEN_COUNT);
        for (u32 token = 0; token < constraint->feature_count; token += 1)
        {
            valid = valid && buster_a64_semantic_string_offset(buster_a64_semantic_constraint_feature_tags[constraint->feature_first + token], &ignored);
        }
        for (u32 token = 0; token < constraint->program_count; token += 1)
        {
            valid = valid && buster_a64_semantic_string_offset(buster_a64_semantic_constraint_program_tokens[constraint->program_first + token], &ignored);
        }
    }
    for (u32 preference_id = 0; preference_id < BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_COUNT; preference_id += 1)
    {
        BusterA64SemanticGeneratedAliasPreference const* preference = buster_a64_semantic_alias_preferences + preference_id;
        BusterA64SemanticString ignored;
        valid = valid && buster_a64_semantic_range(preference->condition_first, preference->condition_count, BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_CONDITION_TOKEN_COUNT) &&
                buster_a64_semantic_string_offset(preference->alias_file_offset, &ignored) && buster_a64_semantic_string_offset(preference->alias_id_offset, &ignored);
    }
    u32 alias_ids_seen = 0;
    for (u32 ordinal = 0; ordinal < BUSTER_AARCH64_SEMANTIC_ALIAS_COUNT; ordinal += 1)
    {
        u32 form_id = buster_a64_semantic_alias_form_ids[ordinal];
        valid = valid && form_id < BUSTER_AARCH64_SEMANTIC_FORM_COUNT && buster_a64_semantic_forms[form_id].kind == BUSTER_A64_SEMANTIC_FORM_ALIAS;
        for (u32 previous = 0; previous < ordinal; previous += 1)
        {
            valid = valid && buster_a64_semantic_alias_form_ids[previous] != form_id;
        }
        alias_ids_seen += form_id < BUSTER_AARCH64_SEMANTIC_FORM_COUNT && buster_a64_semantic_forms[form_id].kind == BUSTER_A64_SEMANTIC_FORM_ALIAS;
    }
    valid = valid && alias_ids_seen == BUSTER_AARCH64_SEMANTIC_ALIAS_COUNT;
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
