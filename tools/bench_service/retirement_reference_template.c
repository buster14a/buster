/* #881/#1020 installed reference-template and inventory codec.
 * Ownership: byte formats only. No file access, pins or A/toolchain joins;
 * the service importer (bq_retirement_reference_policy_import) owns those.
 * Map:
 *   BqReferenceWriter, bq_reference_writer_*   bounded template emitter
 *   BqReferenceReader, bq_reference_reader_*   bounded little-endian cursor
 *   bq_retirement_reference_template_write     canonical template bytes
 *   bq_retirement_reference_template_decode    exact template_hash stream
 *   bq_reference_inventory_field               length-prefixed text copy
 *   bq_retirement_reference_inventory_decode   v1 inventory stream
 * Template stream (it is exactly what bq_retirement_oracle_template_hash
 * hashes): the ASCII domain, per side commit(40) tree(40) manifest(64), then
 * support, census, population and toolchain digests (64 each), u32 population
 * rows, object rows, native target and reference count, then per row u32 row,
 * census row and target, four digests (64 each), u32 name length and name.
 * Every digest has a fixed width, every count precedes its items and the one
 * variable string is length-prefixed, so it decodes without added framing.
 * Both decoders finish by re-deriving the canonical digest through the
 * existing hash/encoder; SHA-256 equality with the input bytes is the same
 * collision assumption every installed pin already relies on.
 */
#include "retirement_reference_template.h"
#include <string.h>

typedef struct BqReferenceWriter
{
    uint8_t* bytes;
    uint64_t capacity, length;
    bool ok;
} BqReferenceWriter;

typedef struct BqReferenceReader
{
    uint8_t const* bytes;
    uint64_t length, offset;
    bool ok;
} BqReferenceReader;

BUSTER_GLOBAL_LOCAL void bq_reference_writer_add(BqReferenceWriter* writer,
    void const* bytes, uint64_t size)
{
    if (writer->ok) writer->ok = size <= writer->capacity - writer->length;
    if (writer->ok)
    {
        memcpy(writer->bytes + writer->length, bytes, (size_t)size);
        writer->length += size;
    }
}

BUSTER_GLOBAL_LOCAL void bq_reference_writer_u32(BqReferenceWriter* writer, uint32_t value)
{
    uint8_t bytes[4];
    for (unsigned i = 0; i < 4; i += 1) bytes[i] = (uint8_t)(value >> (i * 8));
    bq_reference_writer_add(writer, bytes, sizeof(bytes));
}

BUSTER_GLOBAL_LOCAL void bq_reference_reader_take(BqReferenceReader* reader,
    void* output, uint64_t size)
{
    if (reader->ok) reader->ok = size <= reader->length - reader->offset;
    if (reader->ok)
    {
        memcpy(output, reader->bytes + reader->offset, (size_t)size);
        reader->offset += size;
    }
}

BUSTER_GLOBAL_LOCAL void bq_reference_reader_literal(BqReferenceReader* reader,
    char const* literal, uint64_t size)
{
    if (reader->ok) reader->ok = size <= reader->length - reader->offset &&
        !memcmp(reader->bytes + reader->offset, literal, (size_t)size);
    if (reader->ok) reader->offset += size;
}

BUSTER_GLOBAL_LOCAL uint32_t bq_reference_reader_u32(BqReferenceReader* reader)
{
    uint8_t bytes[4] = {0};
    uint32_t value = 0;
    bq_reference_reader_take(reader, bytes, sizeof(bytes));
    for (unsigned i = 0; reader->ok && i < 4; i += 1) value |= (uint32_t)bytes[i] << (i * 8);
    return value;
}

/* Fixed-width lowercase hex; output has size + 1 bytes. */
BUSTER_GLOBAL_LOCAL void bq_reference_reader_hex(BqReferenceReader* reader,
    char* output, uint64_t size)
{
    bq_reference_reader_take(reader, output, size);
    for (uint64_t i = 0; reader->ok && i < size; i += 1)
        reader->ok = (output[i] >= '0' && output[i] <= '9') ||
            (output[i] >= 'a' && output[i] <= 'f');
    output[size] = 0;
}

/* u32 length then 1..capacity-1 printable ASCII bytes, NUL-terminated. */
BUSTER_GLOBAL_LOCAL void bq_reference_reader_text(BqReferenceReader* reader,
    char* output, uint64_t capacity)
{
    uint32_t size = bq_reference_reader_u32(reader);
    if (reader->ok) reader->ok = size && size < capacity;
    if (reader->ok) bq_reference_reader_take(reader, output, size);
    for (uint32_t i = 0; reader->ok && i < size; i += 1)
        reader->ok = (unsigned char)output[i] >= 32 && (unsigned char)output[i] <= 126;
    output[reader->ok ? size : 0] = 0;
}

BUSTER_GLOBAL_LOCAL void bq_reference_digest(uint8_t const* bytes, uint64_t length,
    char digest[65])
{
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, bytes, length);
    sha256_finish_hex(&hash, digest);
}

bool bq_retirement_reference_template_write(BqRetirementOracleTemplate const* template,
    uint8_t* output, uint64_t capacity, uint64_t* length)
{
    static char const domain[] = "bq-retirement-oracle-template-v1";
    char expected[65] = {0}, actual[65] = {0};
    BqReferenceWriter writer = {output, capacity, 0, output && length &&
        bq_retirement_oracle_template_hash(template, expected)};
    if (length) *length = 0;
    bq_reference_writer_add(&writer, domain, sizeof(domain) - 1);
    for (unsigned side = 0; writer.ok && side < 2; side += 1)
    {
        bq_reference_writer_add(&writer, template->source_commit[side], 40);
        bq_reference_writer_add(&writer, template->source_tree[side], 40);
        bq_reference_writer_add(&writer, template->source_sha256[side], 64);
    }
    if (writer.ok)
    {
        bq_reference_writer_add(&writer, template->support_sha256, 64);
        bq_reference_writer_add(&writer, template->census_sha256, 64);
        bq_reference_writer_add(&writer, template->population_sha256, 64);
        bq_reference_writer_add(&writer, template->toolchain_identity_sha256, 64);
        bq_reference_writer_u32(&writer, template->population_rows);
        bq_reference_writer_u32(&writer, template->object_rows);
        bq_reference_writer_u32(&writer, template->native_target);
        bq_reference_writer_u32(&writer, template->reference_count);
    }
    for (uint32_t i = 0; writer.ok && i < template->reference_count; i += 1)
    {
        BqRetirementOracleTemplateRow const* row = template->references + i;
        uint32_t name = (uint32_t)strlen(row->output_name);
        bq_reference_writer_u32(&writer, row->row);
        bq_reference_writer_u32(&writer, row->census_row);
        bq_reference_writer_u32(&writer, row->target);
        bq_reference_writer_add(&writer, row->source_sha256, 64);
        bq_reference_writer_add(&writer, row->configuration_sha256, 64);
        bq_reference_writer_add(&writer, row->build_command_sha256, 64);
        bq_reference_writer_add(&writer, row->logical_command_sha256, 64);
        bq_reference_writer_u32(&writer, name);
        bq_reference_writer_add(&writer, row->output_name, name);
    }
    if (writer.ok)
    {
        bq_reference_digest(output, writer.length, actual);
        writer.ok = !strcmp(actual, expected);
    }
    if (writer.ok) *length = writer.length;
    return writer.ok;
}

bool bq_retirement_reference_template_decode(uint8_t const* bytes, uint64_t length,
    BqRetirementOracleTemplateRow* rows, uint32_t row_slots,
    BqRetirementOracleTemplate* template, char digest[65])
{
    static char const domain[] = "bq-retirement-oracle-template-v1";
    BqRetirementOracleTemplate decoded = {0};
    char raw[65] = {0}, canonical[65] = {0};
    BqReferenceReader reader = {bytes, length, 0, bytes && rows && template &&
        digest && length <= BQ_RETIREMENT_REFERENCE_TEMPLATE_CAP};
    if (template) *template = (BqRetirementOracleTemplate){0};
    if (digest) digest[0] = 0;
    bq_reference_reader_literal(&reader, domain, sizeof(domain) - 1);
    for (unsigned side = 0; side < 2; side += 1)
    {
        bq_reference_reader_hex(&reader, decoded.source_commit[side], 40);
        bq_reference_reader_hex(&reader, decoded.source_tree[side], 40);
        bq_reference_reader_hex(&reader, decoded.source_sha256[side], 64);
    }
    bq_reference_reader_hex(&reader, decoded.support_sha256, 64);
    bq_reference_reader_hex(&reader, decoded.census_sha256, 64);
    bq_reference_reader_hex(&reader, decoded.population_sha256, 64);
    bq_reference_reader_hex(&reader, decoded.toolchain_identity_sha256, 64);
    decoded.population_rows = bq_reference_reader_u32(&reader);
    decoded.object_rows = bq_reference_reader_u32(&reader);
    decoded.native_target = bq_reference_reader_u32(&reader);
    decoded.reference_count = bq_reference_reader_u32(&reader);
    /* Reject an impossible count before touching row storage. */
    if (reader.ok) reader.ok = decoded.reference_count &&
        decoded.reference_count <= row_slots &&
        decoded.reference_count <= decoded.population_rows &&
        decoded.population_rows <= BQ_RETIREMENT_CORRECTNESS_ROWS_CAP &&
        (reader.length - reader.offset) / BQ_RETIREMENT_REFERENCE_TEMPLATE_ROW_MIN >=
            decoded.reference_count;
    for (uint32_t i = 0; reader.ok && i < decoded.reference_count; i += 1)
    {
        BqRetirementOracleTemplateRow* row = rows + i;
        *row = (BqRetirementOracleTemplateRow){0};
        row->row = bq_reference_reader_u32(&reader);
        row->census_row = bq_reference_reader_u32(&reader);
        row->target = bq_reference_reader_u32(&reader);
        bq_reference_reader_hex(&reader, row->source_sha256, 64);
        bq_reference_reader_hex(&reader, row->configuration_sha256, 64);
        bq_reference_reader_hex(&reader, row->build_command_sha256, 64);
        bq_reference_reader_hex(&reader, row->logical_command_sha256, 64);
        bq_reference_reader_text(&reader, row->output_name, BQ_RETIREMENT_OUTPUT_NAME_CAP);
        if (reader.ok) reader.ok = row->row < decoded.population_rows &&
            row->target == decoded.native_target && (!i || row->row > rows[i - 1].row);
    }
    bool ok = reader.ok && reader.offset == reader.length;
    if (ok)
    {
        decoded.references = rows;
        bq_reference_digest(bytes, length, raw);
        ok = bq_retirement_oracle_template_hash(&decoded, canonical) &&
            !strcmp(canonical, raw);
    }
    if (ok)
    {
        *template = decoded;
        memcpy(digest, raw, 65);
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL char const* bq_reference_inventory_field(BqReferenceReader* reader,
    char* text, uint64_t capacity, uint64_t* used)
{
    char* output = NULL;
    uint32_t size = bq_reference_reader_u32(reader);
    if (reader->ok) reader->ok = size && size < BQ_RETIREMENT_REFERENCE_FIELD_CAP &&
        *used <= capacity && (uint64_t)size < capacity - *used;
    if (reader->ok)
    {
        output = text + *used;
        bq_reference_reader_take(reader, output, size);
    }
    for (uint32_t i = 0; reader->ok && i < size; i += 1)
        reader->ok = (unsigned char)output[i] >= 32 && (unsigned char)output[i] <= 126;
    if (reader->ok)
    {
        output[size] = 0;
        *used += (uint64_t)size + 1u;
    }
    return reader->ok ? output : NULL;
}

bool bq_retirement_reference_inventory_decode(uint8_t const* bytes, uint64_t length,
    BqRetirementOracleTemplate const* template,
    BqRetirementReferencePlanRow* rows, uint32_t row_slots,
    char* text, uint64_t text_capacity, BqRetirementReferencePlan* plan,
    BqRetirementReferenceSourceIdentity source[2],
    char toolchain_identity_sha256[65], char digest[65])
{
    static char const domain[] = "BQ-RETIREMENT-REFERENCE-INVENTORY-V1\n";
    BqRetirementReferencePlan decoded = {0};
    BqRetirementReferenceSourceIdentity identity[2] = {0};
    char template_sha256[65] = {0}, embedded[65] = {0}, toolchain[65] = {0};
    char raw[65] = {0}, canonical[65] = {0};
    uint64_t used = 0;
    BqReferenceReader reader = {bytes, length, 0, bytes && rows && text && plan &&
        source && toolchain_identity_sha256 && digest &&
        length <= BQ_RETIREMENT_REFERENCE_INVENTORY_CAP &&
        bq_retirement_oracle_template_hash(template, template_sha256)};
    if (plan) *plan = (BqRetirementReferencePlan){0};
    if (source) memset(source, 0, 2 * sizeof(*source));
    if (toolchain_identity_sha256) toolchain_identity_sha256[0] = 0;
    if (digest) digest[0] = 0;
    bq_reference_reader_literal(&reader, domain, sizeof(domain) - 1);
    bq_reference_reader_hex(&reader, embedded, 64);
    if (reader.ok) reader.ok = !strcmp(embedded, template_sha256);
    for (unsigned side = 0; side < 2; side += 1)
    {
        bq_reference_reader_hex(&reader, identity[side].commit, 40);
        bq_reference_reader_hex(&reader, identity[side].tree, 40);
        bq_reference_reader_hex(&reader, identity[side].manifest_sha256, 64);
    }
    bq_reference_reader_hex(&reader, toolchain, 64);
    bq_reference_reader_hex(&reader, decoded.clang_sha256, 64);
    decoded.count = bq_reference_reader_u32(&reader);
    if (reader.ok) reader.ok = decoded.count == template->reference_count &&
        decoded.count <= row_slots;
    for (uint32_t i = 0; reader.ok && i < decoded.count; i += 1)
    {
        BqRetirementReferencePlanRow* row = rows + i;
        *row = (BqRetirementReferencePlanRow){0};
        row->row = bq_reference_reader_u32(&reader);
        row->source_side = bq_reference_reader_u32(&reader);
        bq_reference_reader_text(&reader, row->source_path, BQ_RETIREMENT_REFERENCE_FIELD_CAP);
        bq_reference_reader_hex(&reader, row->source_sha256, 64);
        /* Held source roots are indexed by side; policy (side 0 only) is
         * enforced again by the canonical encoder below. */
        if (reader.ok) reader.ok = row->source_side < 2 && (!i || row->row > rows[i - 1].row);
        row->flag_count = bq_reference_reader_u32(&reader);
        if (reader.ok) reader.ok = row->flag_count <= BQ_RETIREMENT_REFERENCE_FLAGS_CAP;
        for (uint32_t j = 0; reader.ok && j < row->flag_count; j += 1)
            row->flags[j] = bq_reference_inventory_field(&reader, text, text_capacity, &used);
        row->build_environment_count = bq_reference_reader_u32(&reader);
        if (reader.ok) reader.ok = row->build_environment_count <= BQ_RETIREMENT_REFERENCE_ENV_CAP;
        for (uint32_t j = 0; reader.ok && j < row->build_environment_count; j += 1)
            row->build_environment[j] = bq_reference_inventory_field(&reader, text,
                text_capacity, &used);
        row->runtime_argument_count = bq_reference_reader_u32(&reader);
        if (reader.ok) reader.ok = row->runtime_argument_count >= 1 &&
            row->runtime_argument_count <= BQ_RETIREMENT_REFERENCE_ARGS_CAP;
        for (uint32_t j = 1; reader.ok && j < row->runtime_argument_count; j += 1)
            row->runtime_arguments[j - 1] = bq_reference_inventory_field(&reader, text,
                text_capacity, &used);
        row->runtime_environment_count = bq_reference_reader_u32(&reader);
        if (reader.ok) reader.ok = row->runtime_environment_count <= BQ_RETIREMENT_REFERENCE_ENV_CAP;
        for (uint32_t j = 0; reader.ok && j < row->runtime_environment_count; j += 1)
            row->runtime_environment[j] = bq_reference_inventory_field(&reader, text,
                text_capacity, &used);
    }
    bool ok = reader.ok && reader.offset == reader.length;
    if (ok)
    {
        decoded.template = template;
        decoded.rows = rows;
        bq_reference_digest(bytes, length, raw);
        ok = bq_retirement_reference_inventory_encode(&decoded, identity, toolchain,
                -1, canonical) && !strcmp(canonical, raw);
    }
    if (ok)
    {
        *plan = decoded;
        memcpy(source, identity, sizeof(identity));
        memcpy(toolchain_identity_sha256, toolchain, 65);
        memcpy(digest, raw, 65);
    }
    return ok;
}
