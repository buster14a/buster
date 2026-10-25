/* Original forward FlatBuffers writer for ordinary zero-row Arrow schemas.
 * It is a native producer API, not a new physical schema exchange language.
 * Every reference points forward; vtables precede their aligned table objects.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
typedef struct PerfFlatWriter PerfFlatWriter;
struct PerfFlatWriter
{
    PerfBuffer bytes;
    bool valid;
};

static uint32_t perf_write_zero(PerfFlatWriter *writer, size_t count)
{
    uint32_t result = (uint32_t)writer->bytes.size;
    static const unsigned char zeros[64] = {0};
    while (count != 0 && writer->valid)
    {
        size_t size = count < sizeof(zeros) ? count : sizeof(zeros);
        writer->valid = perf_buffer_append(&writer->bytes, (PerfBytes){.data = zeros, .size = size});
        count -= size;
    }
    return result;
}

static void perf_write_align(PerfFlatWriter *writer, unsigned int alignment)
{
    size_t padding = (alignment - writer->bytes.size % alignment) % alignment;
    perf_write_zero(writer, padding);
}

static void perf_write_little(PerfFlatWriter *writer, uint32_t position, uint64_t value, unsigned int width)
{
    writer->valid = writer->valid && width <= 8 && perf_range(perf_buffer_bytes(&writer->bytes), position, width);
    for (unsigned int i = 0; i < width && writer->valid; ++i)
    {
        writer->bytes.data[position + i] = (unsigned char)(value >> (i * 8));
    }
}

static void perf_write_reference(PerfFlatWriter *writer, uint32_t position, uint32_t target)
{
    writer->valid = writer->valid && target >= position + 4;
    perf_write_little(writer, position, (uint64_t)target - position, 4);
}

static uint32_t perf_write_table(PerfFlatWriter *writer, unsigned int fields, unsigned int present)
{
    perf_write_align(writer, 2);
    uint32_t vtable = perf_write_zero(writer, 4 + fields * 2);
    perf_write_align(writer, 8);
    uint32_t table = perf_write_zero(writer, 8 + fields * 8);
    perf_write_little(writer, vtable, 4 + fields * 2, 2);
    perf_write_little(writer, vtable + 2, 8 + fields * 8, 2);
    perf_write_little(writer, table, table - vtable, 4);
    for (unsigned int i = 0; i < fields; ++i)
    {
        perf_write_little(writer, vtable + 4 + i * 2, (present & (1u << i)) != 0 ? 8 + i * 8 : 0, 2);
    }
    return table;
}

static uint32_t perf_write_slot(uint32_t table, unsigned int field)
{
    uint32_t result = table + 8 + field * 8;
    return result;
}

static uint32_t perf_write_vector(PerfFlatWriter *writer, uint32_t count)
{
    perf_write_align(writer, 4);
    uint32_t result = perf_write_zero(writer, 4 + (size_t)count * 4);
    perf_write_little(writer, result, count, 4);
    return result;
}

static uint32_t perf_write_string(PerfFlatWriter *writer, PerfBytes string)
{
    writer->valid = writer->valid && perf_utf8(string) && string.size < BUSTER_PERF_BYTE_LIMIT;
    perf_write_align(writer, 4);
    uint32_t result = perf_write_zero(writer, 4);
    writer->valid = writer->valid && perf_buffer_append(&writer->bytes, string) && perf_buffer_byte(&writer->bytes, 0);
    perf_write_little(writer, result, string.size, 4);
    return result;
}

static uint32_t perf_write_metadata(PerfFlatWriter *writer, PerfBytes source, PerfFlatVector entries)
{
    writer->valid = writer->valid && entries.count <= BUSTER_PERF_FIELD_LIMIT;
    uint32_t result = perf_write_vector(writer, entries.count);
    PerfFlat flat = {.bytes = source, .valid = true};
    for (uint32_t i = 0; i < entries.count && writer->valid; ++i)
    {
        uint32_t original = perf_flat_target(&flat, entries.start + i * 4);
        PerfBytes key = perf_flat_string(&flat, original, 0);
        PerfBytes value = perf_flat_string(&flat, original, 1);
        writer->valid = flat.valid;
        uint32_t table = perf_write_table(writer, 2, 3);
        perf_write_reference(writer, result + 4 + i * 4, table);
        uint32_t name = perf_write_string(writer, key);
        perf_write_reference(writer, perf_write_slot(table, 0), name);
        uint32_t content = perf_write_string(writer, value);
        perf_write_reference(writer, perf_write_slot(table, 1), content);
    }
    return result;
}

static uint32_t perf_write_type(PerfFlatWriter *writer, const PerfArrowField *field)
{
    unsigned int fields = field->type == 2 ? 2 : field->type == 3 || field->type == 15 || field->type == 16 ? 1 : field->type == 7 ? 3 : 0;
    uint32_t result = perf_write_table(writer, fields, (1u << fields) - 1);
    if (field->type == 2)
    {
        perf_write_little(writer, perf_write_slot(result, 0), field->bit_width, 4);
        perf_write_little(writer, perf_write_slot(result, 1), field->is_signed, 1);
    }
    else if (field->type == 3)
    {
        perf_write_little(writer, perf_write_slot(result, 0), field->precision, 2);
    }
    else if (field->type == 7)
    {
        perf_write_little(writer, perf_write_slot(result, 0), field->precision, 4);
        perf_write_little(writer, perf_write_slot(result, 1), (uint32_t)field->scale, 4);
        perf_write_little(writer, perf_write_slot(result, 2), field->bit_width, 4);
    }
    else if (field->type == 15 || field->type == 16)
    {
        perf_write_little(writer, perf_write_slot(result, 0), field->fixed_size, 4);
    }
    return result;
}

typedef struct PerfSchemaWriteFrame PerfSchemaWriteFrame;
struct PerfSchemaWriteFrame
{
    uint32_t vector;
    uint32_t field;
    uint32_t cursor;
    uint32_t count;
};

static bool perf_arrow_schema_write(const PerfArrowSchema *schema, PerfBuffer *stream)
{
    PerfFlatWriter writer = {.valid = true};
    writer.valid = schema->count <= BUSTER_PERF_FIELD_LIMIT && schema->roots <= schema->count;
    perf_write_zero(&writer, 4);
    uint32_t message = perf_write_table(&writer, 4, 15);
    perf_write_reference(&writer, 0, message);
    perf_write_little(&writer, perf_write_slot(message, 0), 4, 2);
    perf_write_little(&writer, perf_write_slot(message, 1), 1, 1);
    uint32_t native_schema = perf_write_table(&writer, 3, schema->metadata_vector.count == 0 ? 3 : 7);
    perf_write_reference(&writer, perf_write_slot(message, 2), native_schema);
    uint32_t roots = perf_write_vector(&writer, (uint32_t)schema->roots);
    perf_write_reference(&writer, perf_write_slot(native_schema, 1), roots);
    if (schema->metadata_vector.count != 0)
    {
        uint32_t metadata = perf_write_metadata(&writer, schema->metadata_source, schema->metadata_vector);
        perf_write_reference(&writer, perf_write_slot(native_schema, 2), metadata);
    }
    PerfSchemaWriteFrame frames[BUSTER_PERF_DEPTH_LIMIT] = {0};
    frames[0].vector = roots;
    frames[0].count = (uint32_t)schema->roots;
    size_t depth = 0;
    size_t written = 0;
    while (writer.valid)
    {
        PerfSchemaWriteFrame *frame = &frames[depth];
        if (frame->cursor == frame->count)
        {
            if (depth == 0)
            {
                break;
            }
            --depth;
            continue;
        }
        writer.valid = frame->field < schema->count && written++ < schema->count;
        if (!writer.valid)
        {
            break;
        }
        uint32_t index = frame->field;
        const PerfArrowField *field = &schema->fields[index];
        writer.valid = field->next > index && field->next <= schema->count && field->children <= BUSTER_PERF_FIELD_LIMIT;
        frame->field = field->next;
        uint32_t table = perf_write_table(&writer, 7, field->metadata_vector.count == 0 ? 47 : 111);
        perf_write_reference(&writer, frame->vector + 4 + frame->cursor++ * 4, table);
        perf_write_little(&writer, perf_write_slot(table, 1), field->nullable, 1);
        perf_write_little(&writer, perf_write_slot(table, 2), field->type, 1);
        uint32_t type = perf_write_type(&writer, field);
        perf_write_reference(&writer, perf_write_slot(table, 3), type);
        uint32_t name = perf_write_string(&writer, field->name);
        perf_write_reference(&writer, perf_write_slot(table, 0), name);
        uint32_t children = perf_write_vector(&writer, field->children);
        perf_write_reference(&writer, perf_write_slot(table, 5), children);
        if (field->metadata_vector.count != 0)
        {
            uint32_t metadata = perf_write_metadata(&writer, field->metadata_source, field->metadata_vector);
            perf_write_reference(&writer, perf_write_slot(table, 6), metadata);
        }
        if (field->children != 0)
        {
            writer.valid = writer.valid && depth + 1 < BUSTER_PERF_DEPTH_LIMIT;
            if (writer.valid)
            {
                frames[++depth] = (PerfSchemaWriteFrame){
                    .vector = children,
                    .field = index + 1,
                    .count = field->children
                };
            }
        }
    }
    writer.valid = writer.valid && written == schema->count;
    perf_write_align(&writer, 8);
    unsigned char framing[8] = {255, 255, 255, 255};
    for (unsigned int i = 0; i < 4; ++i)
    {
        framing[4 + i] = (unsigned char)(writer.bytes.size >> (8 * i));
    }
    bool result = writer.valid && perf_buffer_append(stream, (PerfBytes){.data = framing, .size = 8}) && perf_buffer_append(stream, perf_buffer_bytes(&writer.bytes));
    memset(framing + 4, 0, 4);
    result = result && perf_buffer_append(stream, (PerfBytes){.data = framing, .size = 8});
    free(writer.bytes.data);
    return result;
}
