/* Original, bounded Arrow IPC stream/FlatBuffers metadata reader. The wire
 * format remains Apache Arrow; no library, generated header or private codec
 * is imported. Uncompressed little-endian streams have explicit support.
 * Layout: perf_flat_* bounds checks; perf_arrow_schema; record/value readers.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#define BUSTER_PERF_FIELD_LIMIT 4096u
#define BUSTER_PERF_ROW_LIMIT 1000000u

typedef struct PerfFlat PerfFlat;
struct PerfFlat
{
    PerfBytes bytes;
    bool valid;
};

static bool perf_range(PerfBytes bytes, uint64_t offset, uint64_t size)
{
    bool result = offset <= bytes.size && size <= bytes.size - offset;
    return result;
}

static uint64_t perf_little(PerfFlat *flat, uint64_t offset, unsigned int width)
{
    uint64_t result = 0;
    flat->valid = flat->valid && width <= 8 && perf_range(flat->bytes, offset, width);
    for (unsigned int i = 0; i < width && flat->valid; ++i)
    {
        result |= (uint64_t)flat->bytes.data[offset + i] << (8 * i);
    }
    return result;
}

static uint32_t perf_flat_target(PerfFlat *flat, uint32_t position)
{
    uint64_t relative = perf_little(flat, position, 4);
    uint64_t target = position + relative;
    flat->valid = flat->valid && relative >= 4 && target <= UINT32_MAX && perf_range(flat->bytes, target, 4);
    uint32_t result = flat->valid ? (uint32_t)target : 0;
    return result;
}

static uint32_t perf_flat_field(PerfFlat *flat, uint32_t table, unsigned int field, unsigned int width)
{
    uint32_t raw_distance = (uint32_t)perf_little(flat, table, 4);
    int64_t distance = raw_distance <= INT32_MAX ? raw_distance : (int64_t)raw_distance - INT64_C(4294967296);
    int64_t vtable_signed = (int64_t)table - distance;
    flat->valid = flat->valid && vtable_signed >= 0 && (uint64_t)vtable_signed <= UINT32_MAX;
    uint32_t vtable = flat->valid ? (uint32_t)vtable_signed : 0;
    uint64_t vtable_size = perf_little(flat, vtable, 2);
    uint64_t object_size = perf_little(flat, (uint64_t)vtable + 2, 2);
    flat->valid = flat->valid && vtable_size >= 4 && (vtable_size & 1) == 0 && object_size >= 4 && perf_range(flat->bytes, vtable, vtable_size) && perf_range(flat->bytes, table, object_size);
    uint32_t result = 0;
    if (flat->valid && (uint64_t)field * 2 + 4 < vtable_size)
    {
        uint64_t offset = perf_little(flat, (uint64_t)vtable + 4 + (uint64_t)field * 2, 2);
        if (offset != 0)
        {
            flat->valid = offset >= 4 && offset <= object_size && width <= object_size - offset;
            result = flat->valid ? table + (uint32_t)offset : 0;
        }
    }
    return result;
}

static uint64_t perf_flat_scalar(PerfFlat *flat, uint32_t table, unsigned int field, unsigned int width, uint64_t default_value)
{
    uint32_t position = perf_flat_field(flat, table, field, width);
    uint64_t result = position == 0 ? default_value : perf_little(flat, position, width);
    return result;
}

static uint32_t perf_flat_object(PerfFlat *flat, uint32_t table, unsigned int field)
{
    uint32_t position = perf_flat_field(flat, table, field, 4);
    uint32_t result = position == 0 ? 0 : perf_flat_target(flat, position);
    return result;
}

static PerfBytes perf_flat_string_at(PerfFlat *flat, uint32_t position)
{
    uint64_t size = perf_little(flat, position, 4);
    flat->valid = flat->valid && perf_range(flat->bytes, (uint64_t)position + 4, size + 1);
    PerfBytes result = {0};
    if (flat->valid)
    {
        flat->valid = flat->bytes.data[position + 4 + size] == 0;
        result.data = flat->bytes.data + position + 4;
        result.size = (size_t)size;
    }
    return result;
}

static PerfBytes perf_flat_string(PerfFlat *flat, uint32_t table, unsigned int field)
{
    uint32_t position = perf_flat_object(flat, table, field);
    PerfBytes result = {0};
    if (position != 0 && flat->valid)
    {
        result = perf_flat_string_at(flat, position);
    }
    return result;
}

typedef struct PerfFlatVector PerfFlatVector;
struct PerfFlatVector
{
    uint32_t start;
    uint32_t count;
};

static PerfFlatVector perf_flat_vector(PerfFlat *flat, uint32_t table, unsigned int field, unsigned int item_size)
{
    uint32_t position = perf_flat_object(flat, table, field);
    PerfFlatVector result = {0};
    if (position != 0 && flat->valid)
    {
        uint64_t count = perf_little(flat, position, 4);
        flat->valid = flat->valid && perf_range(flat->bytes, (uint64_t)position + 4, count * item_size);
        if (flat->valid)
        {
            result.start = position + 4;
            result.count = (uint32_t)count;
        }
    }
    return result;
}

typedef struct PerfArrowField PerfArrowField;
struct PerfArrowField
{
    PerfBytes name;
    char metadata[65];
    uint32_t next;
    uint32_t children;
    unsigned int type;
    unsigned int bit_width;
    unsigned int precision;
    int32_t scale;
    uint32_t fixed_size;
    bool is_signed;
    bool nullable;
};

typedef struct PerfArrowSchema PerfArrowSchema;
struct PerfArrowSchema
{
    PerfArrowField *fields;
    char metadata[65];
    size_t count;
    size_t roots;
};

typedef struct PerfArrowKeyValue PerfArrowKeyValue;
struct PerfArrowKeyValue
{
    PerfBytes key;
    PerfBytes value;
};

static int perf_arrow_metadata_compare(const void *left, const void *right)
{
    const PerfArrowKeyValue *a = left;
    const PerfArrowKeyValue *b = right;
    int result = perf_json_key_compare(&a->key, &b->key);
    return result;
}

static bool perf_arrow_metadata(PerfFlat *flat, uint32_t table, unsigned int field, char digest[65])
{
    PerfFlatVector vector = perf_flat_vector(flat, table, field, 4);
    bool result = flat->valid && vector.count <= BUSTER_PERF_FIELD_LIMIT;
    PerfArrowKeyValue *entries = result ? calloc((size_t)vector.count + 1, sizeof(*entries)) : NULL;
    result = result && entries != NULL;
    for (uint32_t i = 0; i < vector.count && result; ++i)
    {
        uint32_t entry = perf_flat_target(flat, vector.start + i * 4);
        entries[i].key = perf_flat_string(flat, entry, 0);
        entries[i].value = perf_flat_string(flat, entry, 1);
        result = flat->valid && perf_utf8(entries[i].key) && perf_utf8(entries[i].value);
    }
    PerfBuffer encoded = {0};
    if (result)
    {
        qsort(entries, vector.count, sizeof(*entries), perf_arrow_metadata_compare);
    }
    for (uint32_t i = 0; i < vector.count && result; ++i)
    {
        result = (i == 0 || !perf_bytes_equal(entries[i - 1].key, entries[i].key)) && perf_json_quote(&encoded, entries[i].key) && perf_json_quote(&encoded, entries[i].value);
    }
    result = result && perf_sha256(perf_buffer_bytes(&encoded), digest);
    free(entries);
    free(encoded.data);
    return result;
}

typedef struct PerfArrowSchemaFrame PerfArrowSchemaFrame;
struct PerfArrowSchemaFrame
{
    PerfFlatVector fields;
    uint32_t cursor;
    uint32_t parent;
};

static bool perf_arrow_schema(PerfFlat *flat, uint32_t table, PerfArrowSchema *schema)
{
    bool result = perf_flat_scalar(flat, table, 0, 2, 0) == 0;
    result = perf_arrow_metadata(flat, table, 2, schema->metadata) && result;
    PerfFlatVector features = perf_flat_vector(flat, table, 3, 8);
    result = result && features.count == 0;
    PerfFlatVector root = perf_flat_vector(flat, table, 1, 4);
    schema->fields = calloc(BUSTER_PERF_FIELD_LIMIT, sizeof(*schema->fields));
    result = result && schema->fields != NULL && root.count <= BUSTER_PERF_FIELD_LIMIT;
    schema->roots = root.count;
    PerfArrowSchemaFrame frames[BUSTER_PERF_DEPTH_LIMIT] = {0};
    frames[0].fields = root;
    frames[0].parent = UINT32_MAX;
    size_t depth = 0;
    while (result && flat->valid)
    {
        PerfArrowSchemaFrame *frame = &frames[depth];
        if (frame->cursor == frame->fields.count)
        {
            if (frame->parent != UINT32_MAX)
            {
                schema->fields[frame->parent].next = (uint32_t)schema->count;
            }
            if (depth == 0)
            {
                break;
            }
            --depth;
            continue;
        }
        uint32_t position = perf_flat_target(flat, frame->fields.start + frame->cursor * 4);
        ++frame->cursor;
        result = schema->count < BUSTER_PERF_FIELD_LIMIT;
        if (!result)
        {
            break;
        }
        uint32_t index = (uint32_t)schema->count++;
        PerfArrowField *field = &schema->fields[index];
        field->name = perf_flat_string(flat, position, 0);
        field->nullable = perf_flat_scalar(flat, position, 1, 1, 0) != 0;
        field->type = (unsigned int)perf_flat_scalar(flat, position, 2, 1, 0);
        uint32_t type = perf_flat_object(flat, position, 3);
        uint32_t dictionary = perf_flat_object(flat, position, 4);
        PerfFlatVector children = perf_flat_vector(flat, position, 5, 4);
        result = flat->valid && dictionary == 0 && type != 0 && field->name.data != NULL && perf_utf8(field->name) && perf_arrow_metadata(flat, position, 6, field->metadata);
        field->children = children.count;
        field->next = index + 1;
        switch (field->type)
        {
            case 1:
            case 4:
            case 5:
            case 6:
            case 19:
            case 20:
                result = result && children.count == 0;
                break;
            case 2:
                field->bit_width = (unsigned int)perf_flat_scalar(flat, type, 0, 4, 0);
                field->is_signed = perf_flat_scalar(flat, type, 1, 1, 0) != 0;
                result = result && children.count == 0 && (field->bit_width == 8 || field->bit_width == 16 || field->bit_width == 32 || field->bit_width == 64);
                break;
            case 3:
                field->precision = (unsigned int)perf_flat_scalar(flat, type, 0, 2, 0);
                field->bit_width = field->precision == 0 ? 16 : field->precision == 1 ? 32 : field->precision == 2 ? 64 : 0;
                result = result && children.count == 0 && field->bit_width != 0;
                break;
            case 7:
            {
                field->precision = (unsigned int)perf_flat_scalar(flat, type, 0, 4, 0);
                uint32_t raw_scale = (uint32_t)perf_flat_scalar(flat, type, 1, 4, 0);
                field->scale = raw_scale <= INT32_MAX ? (int32_t)raw_scale : (int32_t)((int64_t)raw_scale - INT64_C(4294967296));
                field->bit_width = (unsigned int)perf_flat_scalar(flat, type, 2, 4, 128);
                unsigned int maximum = field->bit_width == 32 ? 9 : field->bit_width == 64 ? 18 : field->bit_width == 128 ? 38 : field->bit_width == 256 ? 76 : 0;
                result = result && children.count == 0 && field->precision > 0 && field->precision <= maximum && field->scale >= -76 && field->scale <= 76;
            } break;
            case 12:
            case 21:
                result = result && children.count == 1;
                break;
            case 13:
                break;
            case 15:
                field->fixed_size = (uint32_t)perf_flat_scalar(flat, type, 0, 4, 0);
                result = result && children.count == 0 && field->fixed_size <= BUSTER_PERF_BYTE_LIMIT;
                break;
            case 16:
                field->fixed_size = (uint32_t)perf_flat_scalar(flat, type, 0, 4, 0);
                result = result && children.count == 1 && field->fixed_size <= BUSTER_PERF_ROW_LIMIT;
                break;
            default:
                result = false;
                break;
        }
        if (children.count != 0 && result)
        {
            result = depth + 1 < BUSTER_PERF_DEPTH_LIMIT;
            if (result)
            {
                frames[++depth] = (PerfArrowSchemaFrame){
                    .fields = children,
                    .parent = index
                };
            }
        }
    }
    for (uint32_t parent = 0; parent <= schema->count && result; ++parent)
    {
        uint32_t start = parent == schema->count ? 0 : parent + 1;
        uint32_t end = parent == schema->count ? (uint32_t)schema->count : schema->fields[parent].next;
        if (parent < schema->count && schema->fields[parent].children == 0)
        {
            continue;
        }
        for (uint32_t a = start; a < end && result; a = schema->fields[a].next)
        {
            for (uint32_t b = schema->fields[a].next; b < end && result; b = schema->fields[b].next)
            {
                result = !perf_bytes_equal(schema->fields[a].name, schema->fields[b].name);
            }
        }
    }
    result = result && flat->valid;
    return result;
}

static bool perf_arrow_schemas_equal(const PerfArrowSchema *left, const PerfArrowSchema *right)
{
    bool result = left->count == right->count && left->roots == right->roots && strcmp(left->metadata, right->metadata) == 0;
    for (size_t i = 0; i < left->count && result; ++i)
    {
        const PerfArrowField *a = &left->fields[i];
        const PerfArrowField *b = &right->fields[i];
        result = perf_bytes_equal(a->name, b->name) && strcmp(a->metadata, b->metadata) == 0 && a->type == b->type && a->next == b->next && a->children == b->children && a->bit_width == b->bit_width && a->precision == b->precision && a->scale == b->scale && a->fixed_size == b->fixed_size && a->is_signed == b->is_signed && a->nullable == b->nullable;
    }
    return result;
}

typedef struct PerfArrowArray PerfArrowArray;
struct PerfArrowArray
{
    uint64_t length;
    uint64_t nulls;
    PerfBytes validity;
    PerfBytes offsets;
    PerfBytes data;
};

typedef struct PerfArrowBatch PerfArrowBatch;
struct PerfArrowBatch
{
    PerfArrowArray *arrays;
    uint64_t rows;
    bool json_mapping;
};

static PerfBytes perf_arrow_buffer(PerfFlat *flat, PerfFlatVector buffers, uint32_t *index, PerfBytes body)
{
    PerfBytes result = {0};
    flat->valid = flat->valid && *index < buffers.count;
    if (flat->valid)
    {
        uint64_t entry = (uint64_t)buffers.start + (uint64_t)(*index)++ * 16;
        uint64_t offset = perf_little(flat, entry, 8);
        uint64_t size = perf_little(flat, entry + 8, 8);
        flat->valid = flat->valid && perf_range(body, offset, size);
        if (flat->valid)
        {
            result.data = body.data + offset;
            result.size = (size_t)size;
        }
    }
    return result;
}

static bool perf_arrow_batch(PerfFlat *flat, uint32_t table, PerfBytes body, const PerfArrowSchema *schema, PerfArrowBatch *batch)
{
    batch->json_mapping = true;
    batch->rows = perf_flat_scalar(flat, table, 0, 8, 0);
    PerfFlatVector nodes = perf_flat_vector(flat, table, 1, 16);
    PerfFlatVector buffers = perf_flat_vector(flat, table, 2, 16);
    uint32_t compression = perf_flat_object(flat, table, 3);
    batch->arrays = calloc(schema->count + 1, sizeof(*batch->arrays));
    bool result = batch->arrays != NULL && flat->valid && batch->rows <= BUSTER_PERF_ROW_LIMIT && nodes.count == schema->count && compression == 0;
    uint32_t buffer = 0;
    for (size_t i = 0; i < schema->count && result; ++i)
    {
        const PerfArrowField *field = &schema->fields[i];
        PerfArrowArray *array = &batch->arrays[i];
        array->length = perf_little(flat, (uint64_t)nodes.start + i * 16, 8);
        array->nulls = perf_little(flat, (uint64_t)nodes.start + i * 16 + 8, 8);
        result = flat->valid && array->length <= BUSTER_PERF_ROW_LIMIT && array->nulls <= array->length && (field->nullable || array->nulls == 0);
        if (field->type == 1)
        {
            result = result && array->nulls == array->length;
            continue;
        }
        array->validity = perf_arrow_buffer(flat, buffers, &buffer, body);
        result = result && (array->validity.size == 0 ? array->nulls == 0 : array->validity.size >= (array->length + 7) / 8);
        if (result && array->validity.size != 0)
        {
            uint64_t nulls = 0;
            for (uint64_t r = 0; r < array->length; ++r)
            {
                nulls += ((array->validity.data[r / 8] >> (r % 8)) & 1) == 0;
            }
            result = nulls == array->nulls;
        }
        if (field->type == 13)
        {
            for (uint32_t c = (uint32_t)i + 1; c < field->next && result; c = schema->fields[c].next)
            {
                uint64_t child_length = perf_little(flat, (uint64_t)nodes.start + (uint64_t)c * 16, 8);
                result = child_length == array->length;
            }
        }
        else if (field->type == 16)
        {
            uint64_t child_length = perf_little(flat, (uint64_t)nodes.start + (i + 1) * 16, 8);
            result = result && child_length == array->length * field->fixed_size;
        }
        else if (field->type == 4 || field->type == 5 || field->type == 12 || field->type == 19 || field->type == 20 || field->type == 21)
        {
            unsigned int width = field->type == 19 || field->type == 20 || field->type == 21 ? 8 : 4;
            array->offsets = perf_arrow_buffer(flat, buffers, &buffer, body);
            result = result && array->offsets.size >= (array->length + 1) * width;
            if (field->type != 12 && field->type != 21)
            {
                array->data = perf_arrow_buffer(flat, buffers, &buffer, body);
            }
            PerfFlat offsets = {
                .bytes = array->offsets,
                .valid = result
            };
            uint64_t previous = 0;
            uint64_t maximum = field->type == 12 || field->type == 21 ? perf_little(flat, (uint64_t)nodes.start + (i + 1) * 16, 8) : array->data.size;
            for (uint64_t r = 0; r <= array->length && result; ++r)
            {
                uint64_t offset = perf_little(&offsets, r * width, width);
                result = offsets.valid && offset >= previous && offset <= maximum;
                previous = offset;
            }
        }
        else
        {
            array->data = perf_arrow_buffer(flat, buffers, &buffer, body);
            uint64_t bytes = field->type == 6 ? (array->length + 7) / 8 : field->type == 15 ? array->length * field->fixed_size : array->length * (field->bit_width / 8);
            result = result && array->data.size >= bytes;
        }
        result = result && flat->valid;
    }
    for (uint32_t i = 0; i < schema->count && result; i = schema->fields[i].next)
    {
        result = batch->arrays[i].length == batch->rows;
    }
    result = result && flat->valid && buffer == buffers.count;
    return result;
}

#include "arrow_values.c"

typedef struct PerfArrowInfo PerfArrowInfo;
struct PerfArrowInfo
{
    PerfArrowSchema schema;
    uint64_t rows;
    uint64_t batches;
    bool json_mapping;
};

static bool perf_arrow_read(PerfBytes bytes, PerfArrowInfo *information, PerfBuffer *rows)
{
    information->json_mapping = true;
    bool result = true;
    bool done = false;
    size_t cursor = 0;
    while (cursor < bytes.size && result && !done)
    {
        PerfFlat stream = {
            .bytes = bytes,
            .valid = true
        };
        uint64_t metadata_size = perf_little(&stream, cursor, 4);
        cursor += 4;
        if (metadata_size == UINT32_MAX)
        {
            metadata_size = perf_little(&stream, cursor, 4);
            cursor += 4;
        }
        result = stream.valid && metadata_size <= BUSTER_PERF_BYTE_LIMIT && perf_range(bytes, cursor, metadata_size);
        if (metadata_size == 0 && result)
        {
            done = true;
            result = cursor == bytes.size;
            continue;
        }
        if (!result)
        {
            break;
        }
        PerfFlat metadata = {
            .bytes = {
                .data = bytes.data + cursor,
                .size = (size_t)metadata_size
            },
            .valid = true
        };
        uint32_t message = perf_flat_target(&metadata, 0);
        unsigned int version = (unsigned int)perf_flat_scalar(&metadata, message, 0, 2, 0);
        unsigned int type = (unsigned int)perf_flat_scalar(&metadata, message, 1, 1, 0);
        uint32_t header = perf_flat_object(&metadata, message, 2);
        uint64_t body_size = perf_flat_scalar(&metadata, message, 3, 8, 0);
        cursor += (size_t)metadata_size;
        result = metadata.valid && version >= 3 && version <= 4 && header != 0 && body_size <= BUSTER_PERF_BYTE_LIMIT && perf_range(bytes, cursor, body_size);
        PerfBytes body = {
            .data = result ? bytes.data + cursor : NULL,
            .size = result ? (size_t)body_size : 0
        };
        if (result && type == 1)
        {
            result = information->schema.fields == NULL && body_size == 0 && perf_arrow_schema(&metadata, header, &information->schema);
        }
        else if (result && type == 3 && information->schema.fields != NULL)
        {
            PerfArrowBatch batch = {0};
            result = perf_arrow_batch(&metadata, header, body, &information->schema, &batch);
            information->rows += batch.rows;
            ++information->batches;
            result = result && information->rows <= BUSTER_PERF_ROW_LIMIT && (rows == NULL || perf_arrow_batch_json(&information->schema, &batch, rows));
            information->json_mapping = information->json_mapping && batch.json_mapping;
            free(batch.arrays);
        }
        else
        {
            result = false;
        }
        result = result && metadata.valid;
        cursor += (size_t)body_size;
    }
    result = result && cursor == bytes.size && information->schema.fields != NULL;
    return result;
}

static bool perf_arrow_inspect(PerfBytes bytes, PerfArrowInfo *information)
{
    bool result = perf_arrow_read(bytes, information, NULL);
    return result;
}
