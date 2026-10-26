/* Exact Arrow/JSONL mapping v1, implemented with bounded iterative frames.
 * The canonical text is an internal comparison/export representation; the
 * Arrow schema remains the authority for physical types and decimal scales.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
static bool perf_integer(PerfBytes text, unsigned int width, bool is_signed, bool *negative, uint64_t *magnitude)
{
    PerfNumber number = {0};
    bool result = perf_number(text, &number) && number.exponent >= 0 && number.size + (size_t)number.exponent <= 20;
    uint64_t value = 0;
    for (size_t i = 0; i < number.size + (result ? (size_t)number.exponent : 0) && result; ++i)
    {
        unsigned int digit = i < number.size ? number.digits[i] - '0' : 0;
        result = digit <= 9 && value <= (UINT64_MAX - digit) / 10;
        if (result)
        {
            value = value * 10 + digit;
        }
    }
    uint64_t maximum = is_signed ? UINT64_C(1) << (width - 1) : width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
    maximum -= is_signed && !number.negative;
    result = result && (!number.negative || is_signed) && value <= maximum;
    *negative = number.negative;
    *magnitude = value;
    return result;
}

static bool perf_integer_emit(PerfBuffer *output, uint64_t magnitude, bool negative, bool quoted)
{
    char text[24];
    int size = snprintf(text, sizeof(text), "%s%llu", negative && magnitude != 0 ? "-" : "", (unsigned long long)magnitude);
    PerfBytes bytes = {
        .data = (const unsigned char *)text,
        .size = size > 0 ? (size_t)size : 0
    };
    bool result = size > 0 && (size_t)size < sizeof(text) && (quoted ? perf_json_quote(output, bytes) : perf_buffer_append(output, bytes));
    return result;
}

static bool perf_decimal_emit(PerfBuffer *output, PerfBytes digits, bool negative, int32_t scale)
{
    PerfBuffer text = {0};
    bool zero = digits.size == 1 && digits.data[0] == '0';
    bool result = !negative || zero || perf_buffer_byte(&text, '-');
    if (scale > 0)
    {
        size_t fractional = (size_t)scale;
        if (digits.size <= fractional)
        {
            result = result && perf_buffer_append(&text, perf_literal("0."));
            for (size_t i = digits.size; i < fractional && result; ++i)
            {
                result = perf_buffer_byte(&text, '0');
            }
            result = result && perf_buffer_append(&text, digits);
        }
        else
        {
            result = result && perf_buffer_append(&text, (PerfBytes){.data = digits.data, .size = digits.size - fractional}) && perf_buffer_byte(&text, '.') && perf_buffer_append(&text, (PerfBytes){.data = digits.data + digits.size - fractional, .size = fractional});
        }
    }
    else
    {
        result = result && perf_buffer_append(&text, digits);
        for (int32_t i = 0; i < -scale && result; ++i)
        {
            result = perf_buffer_byte(&text, '0');
        }
    }
    result = result && perf_json_quote(output, perf_buffer_bytes(&text));
    free(text.data);
    return result;
}

static bool perf_decimal_arrow(PerfBuffer *output, const PerfArrowField *field, PerfBytes bytes)
{
    unsigned char magnitude[32] = {0};
    size_t width = field->bit_width / 8;
    bool result = width > 0 && width <= sizeof(magnitude) && bytes.size == width;
    bool negative = result && (bytes.data[width - 1] & 128) != 0;
    unsigned int carry = negative;
    for (size_t i = 0; i < width && result; ++i)
    {
        unsigned int value = (negative ? (unsigned int)(bytes.data[i] ^ 255) : bytes.data[i]) + carry;
        magnitude[i] = (unsigned char)value;
        carry = value >> 8;
    }
    unsigned char reversed[80] = {0};
    size_t count = 0;
    bool remaining = result;
    while (remaining && count < sizeof(reversed))
    {
        unsigned int remainder = 0;
        remaining = false;
        for (size_t i = width; i-- > 0;)
        {
            unsigned int value = remainder * 256 + magnitude[i];
            magnitude[i] = (unsigned char)(value / 10);
            remainder = value % 10;
            remaining = remaining || magnitude[i] != 0;
        }
        reversed[count++] = (unsigned char)('0' + remainder);
    }
    unsigned char digits[80];
    for (size_t i = 0; i < count; ++i)
    {
        digits[i] = reversed[count - i - 1];
    }
    result = result && !remaining && count <= field->precision && perf_decimal_emit(output, (PerfBytes){.data = digits, .size = count}, negative, field->scale);
    return result;
}

static bool perf_decimal_json(PerfBuffer *output, const PerfArrowField *field, PerfBytes text)
{
    PerfNumber number = {0};
    bool result = perf_number_syntax(text, true) && perf_number(text, &number);
    int64_t zeros = (int64_t)number.exponent + field->scale;
    bool zero = number.size == 1 && number.digits[0] == '0';
    result = result && (zero || (zeros >= 0 && (uint64_t)number.size + (uint64_t)zeros <= field->precision));
    unsigned char digits[80];
    size_t count = zero ? 1 : result ? number.size + (size_t)zeros : 0;
    for (size_t i = 0; i < count && result; ++i)
    {
        digits[i] = i < number.size ? number.digits[i] : '0';
    }
    result = result && perf_decimal_emit(output, (PerfBytes){.data = digits, .size = count}, number.negative, field->scale);
    return result;
}

static bool perf_base64(PerfBuffer *output, PerfBytes bytes)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    bool result = perf_buffer_byte(output, '"');
    for (size_t i = 0; i < bytes.size && result; i += 3)
    {
        unsigned int value = (unsigned int)bytes.data[i] << 16;
        value |= i + 1 < bytes.size ? (unsigned int)bytes.data[i + 1] << 8 : 0;
        value |= i + 2 < bytes.size ? bytes.data[i + 2] : 0;
        unsigned char encoded[] = {
            (unsigned char)alphabet[(value >> 18) & 63],
            (unsigned char)alphabet[(value >> 12) & 63],
            (unsigned char)(i + 1 < bytes.size ? alphabet[(value >> 6) & 63] : '='),
            (unsigned char)(i + 2 < bytes.size ? alphabet[value & 63] : '=')
        };
        result = perf_buffer_append(output, (PerfBytes){.data = encoded, .size = 4});
    }
    result = result && perf_buffer_byte(output, '"');
    return result;
}

static int perf_base64_digit(unsigned char byte)
{
    int result = byte >= 'A' && byte <= 'Z' ? byte - 'A' : byte >= 'a' && byte <= 'z' ? byte - 'a' + 26 : byte >= '0' && byte <= '9' ? byte - '0' + 52 : byte == '+' ? 62 : byte == '/' ? 63 : -1;
    return result;
}

static bool perf_base64_decode(PerfBytes text, PerfBuffer *bytes)
{
    bool result = text.size % 4 == 0;
    for (size_t i = 0; i < text.size && result; i += 4)
    {
        int a = perf_base64_digit(text.data[i]);
        int b = perf_base64_digit(text.data[i + 1]);
        int c = perf_base64_digit(text.data[i + 2]);
        int d = perf_base64_digit(text.data[i + 3]);
        unsigned int padding = text.data[i + 2] == '=' ? 2 : text.data[i + 3] == '=' ? 1 : 0;
        result = a >= 0 && b >= 0 && (padding == 2 ? text.data[i + 3] == '=' && (b & 15) == 0 : c >= 0) && (padding != 0 || d >= 0) && (padding != 1 || (c & 3) == 0) && (padding == 0 || i + 4 == text.size);
        if (result)
        {
            uint32_t value = (uint32_t)a << 18 | (uint32_t)b << 12 | (uint32_t)(c >= 0 ? c : 0) << 6 | (uint32_t)(d >= 0 ? d : 0);
            unsigned char decoded[] = {(unsigned char)(value >> 16), (unsigned char)(value >> 8), (unsigned char)value};
            result = perf_buffer_append(bytes, (PerfBytes){.data = decoded, .size = 3 - padding});
        }
    }
    return result;
}

static double perf_float_decode(uint64_t raw, unsigned int width)
{
    double result = 0;
    if (width == 64)
    {
        memcpy(&result, &raw, sizeof(result));
    }
    else if (width == 32)
    {
        uint32_t bits = (uint32_t)raw;
        float value;
        memcpy(&value, &bits, sizeof(value));
        result = value;
    }
    else
    {
        unsigned int exponent = ((unsigned int)raw >> 10) & 31;
        unsigned int fraction = (unsigned int)raw & 1023;
        result = exponent == 0 ? fraction : 1024 + fraction;
        int power = exponent == 0 ? -24 : (int)exponent - 25;
        for (int i = 0; i < power; ++i)
        {
            result *= 2;
        }
        for (int i = 0; i > power; --i)
        {
            result /= 2;
        }
        result = (raw & 32768) != 0 ? -result : result;
    }
    return result;
}

static bool perf_float_finite(double value)
{
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    bool result = ((bits >> 52) & 2047) != 2047;
    return result;
}

typedef struct PerfValueFrame PerfValueFrame;
struct PerfValueFrame
{
    uint32_t field;
    uint32_t json;
    uint64_t row;
    uint64_t cursor;
    uint64_t end;
    uint32_t next_field;
    uint32_t next_json;
    bool started;
    bool structure;
};

static bool perf_arrow_primitive(PerfBuffer *output, const PerfArrowField *field, const PerfArrowArray *array, uint64_t row, const PerfJson *json, uint32_t value, bool *mapped)
{
    bool arrow = array != NULL;
    const PerfJsonNode *node = arrow ? NULL : &json->nodes[value];
    PerfBytes text = arrow ? (PerfBytes){0} : perf_json_text(json, value);
    PerfFlat data = {
        .bytes = arrow ? array->data : (PerfBytes){0},
        .valid = true
    };
    bool result = true;
    if (field->type == 2)
    {
        bool negative = false;
        uint64_t magnitude = 0;
        if (arrow)
        {
            unsigned int width = field->bit_width;
            uint64_t raw = perf_little(&data, row * (width / 8), width / 8);
            negative = field->is_signed && (raw & (UINT64_C(1) << (width - 1))) != 0;
            uint64_t mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
            magnitude = negative ? (~raw + 1) & mask : raw;
            result = data.valid;
        }
        else
        {
            PerfBytes digits = text;
            if (digits.size != 0 && digits.data[0] == '-')
            {
                ++digits.data;
                --digits.size;
            }
            result = node->kind == (field->bit_width == 64 ? PERF_JSON_STRING : PERF_JSON_NUMBER) && perf_digits(digits, false) && perf_integer(text, field->bit_width, field->is_signed, &negative, &magnitude);
        }
        result = result && perf_integer_emit(output, magnitude, negative, field->bit_width == 64);
    }
    else if (field->type == 7)
    {
        size_t width = field->bit_width / 8;
        result = arrow ? perf_range(array->data, row * width, width) && perf_decimal_arrow(output, field, (PerfBytes){.data = array->data.data + row * width, .size = width}) : node->kind == PERF_JSON_STRING && perf_decimal_json(output, field, text);
    }
    else if (field->type == 3)
    {
        double number = 0;
        if (arrow)
        {
            uint64_t raw = perf_little(&data, row * (field->bit_width / 8), field->bit_width / 8);
            result = data.valid;
            number = perf_float_decode(raw, field->bit_width);
            *mapped = *mapped && field->bit_width != 16 && perf_float_finite(number);
            if (field->bit_width == 16 && (raw & 0x7c00) == 0x7c00)
            {
                *mapped = false;
            }
        }
        else
        {
            char source[256];
            result = node->kind == PERF_JSON_NUMBER && text.size < sizeof(source) && field->bit_width != 16;
            if (result)
            {
                memcpy(source, text.data, text.size);
                source[text.size] = 0;
                char *end = NULL;
                number = strtod(source, &end);
                result = end == source + text.size;
                number = field->bit_width == 32 ? (double)(float)number : number;
            }
        }
        char formatted[64];
        int size = snprintf(formatted, sizeof(formatted), "%.17g", number);
        bool finite = perf_float_finite(number);
        result = result && (arrow || finite) && size > 0 && (size_t)size < sizeof(formatted) && (finite ? perf_buffer_append(output, (PerfBytes){.data = (const unsigned char *)formatted, .size = (size_t)size}) : perf_buffer_append(output, perf_literal("null")));
    }
    else if (field->type == 6)
    {
        bool boolean = arrow ? ((array->data.data[row / 8] >> (row % 8)) & 1) != 0 : node->boolean;
        result = (arrow || node->kind == PERF_JSON_BOOLEAN) && perf_buffer_append(output, perf_literal(boolean ? "true" : "false"));
    }
    else if (field->type == 4 || field->type == 5 || field->type == 15 || field->type == 19 || field->type == 20)
    {
        PerfBytes bytes = text;
        PerfBuffer decoded = {0};
        bool binary = field->type == 4 || field->type == 15 || field->type == 19;
        if (arrow)
        {
            uint64_t start = row * field->fixed_size;
            uint64_t end = start + field->fixed_size;
            if (field->type != 15)
            {
                unsigned int width = field->type == 19 || field->type == 20 ? 8 : 4;
                PerfFlat offsets = {.bytes = array->offsets, .valid = true};
                start = perf_little(&offsets, row * width, width);
                end = perf_little(&offsets, (row + 1) * width, width);
                result = offsets.valid;
            }
            result = result && end >= start && perf_range(array->data, start, end - start);
            bytes = result ? (PerfBytes){.data = array->data.data + start, .size = (size_t)(end - start)} : (PerfBytes){0};
        }
        else
        {
            result = node->kind == PERF_JSON_STRING;
            if (binary && result)
            {
                result = perf_base64_decode(text, &decoded);
                bytes = perf_buffer_bytes(&decoded);
            }
            result = result && (field->type != 15 || bytes.size == field->fixed_size);
        }
        result = result && (binary ? perf_base64(output, bytes) : perf_json_quote(output, bytes));
        free(decoded.data);
    }
    else
    {
        result = false;
    }
    return result;
}

static bool perf_arrow_row_json(const PerfArrowSchema *schema, const PerfArrowBatch *batch, uint64_t row, const PerfJson *json, uint32_t root, PerfBuffer *output, bool *mapped)
{
    PerfValueFrame frames[BUSTER_PERF_DEPTH_LIMIT] = {0};
    frames[0].field = UINT32_MAX;
    frames[0].json = root;
    frames[0].row = row;
    size_t depth = 0;
    bool result = true;
    bool done = false;
    while (result && !done)
    {
        PerfValueFrame *frame = &frames[depth];
        const PerfArrowField *field = frame->field == UINT32_MAX ? NULL : &schema->fields[frame->field];
        const PerfArrowArray *array = batch == NULL || field == NULL ? NULL : &batch->arrays[frame->field];
        bool structure = field == NULL || field->type == 13;
        if (!frame->started)
        {
            bool null = false;
            if (batch == NULL)
            {
                result = frame->json < json->node_count;
                null = result && json->nodes[frame->json].kind == PERF_JSON_NULL;
            }
            else if (field != NULL)
            {
                result = frame->row < array->length;
                null = field->type == 1 || (array->validity.size != 0 && ((array->validity.data[frame->row / 8] >> (frame->row % 8)) & 1) == 0);
            }
            if (!result)
            {
                break;
            }
            if (null || (field != NULL && !structure && field->type != 12 && field->type != 16 && field->type != 21))
            {
                result = null ? field != NULL && field->nullable && perf_buffer_append(output, perf_literal("null")) : perf_arrow_primitive(output, field, array, frame->row, json, frame->json, mapped);
                if (depth == 0)
                {
                    done = true;
                }
                else
                {
                    --depth;
                }
                continue;
            }
            frame->started = true;
            frame->structure = structure;
            frame->next_field = field == NULL ? 0 : frame->field + 1;
            frame->end = structure ? field == NULL ? schema->roots : field->children : 0;
            if (batch == NULL)
            {
                result = json->nodes[frame->json].kind == (structure ? PERF_JSON_OBJECT : PERF_JSON_ARRAY);
                result = result && (!structure || json->nodes[frame->json].children == frame->end);
                frame->end = structure ? frame->end : json->nodes[frame->json].children;
                frame->next_json = frame->json + 1;
                result = result && (field == NULL || field->type != 16 || frame->end == field->fixed_size);
            }
            else if (!structure)
            {
                frame->cursor = frame->row * field->fixed_size;
                frame->end = frame->cursor + field->fixed_size;
                if (field->type != 16)
                {
                    unsigned int width = field->type == 21 ? 8 : 4;
                    PerfFlat offsets = {.bytes = array->offsets, .valid = true};
                    frame->cursor = perf_little(&offsets, frame->row * width, width);
                    frame->end = perf_little(&offsets, (frame->row + 1) * width, width);
                    result = offsets.valid;
                }
            }
            result = result && perf_buffer_byte(output, structure ? '{' : '[');
        }
        if (!result)
        {
            break;
        }
        if (frame->cursor == frame->end)
        {
            result = perf_buffer_byte(output, frame->structure ? '}' : ']');
            if (depth == 0)
            {
                done = true;
            }
            else
            {
                --depth;
            }
            continue;
        }
        uint32_t child = frame->next_field;
        /* List offsets can start at a nonzero child index. Track separator
         * state independently of that physical offset. */
        bool first = frame->structure ? frame->cursor == 0 : frame->next_json == (batch == NULL ? frame->json + 1 : 0);
        result = (first || perf_buffer_byte(output, ',')) && child < schema->count;
        uint32_t child_json = UINT32_MAX;
        if (frame->structure && result)
        {
            result = perf_json_quote(output, schema->fields[child].name) && perf_buffer_byte(output, ':');
            child_json = batch == NULL ? perf_json_find_bytes(json, frame->json, schema->fields[child].name) : UINT32_MAX;
            frame->next_field = schema->fields[child].next;
        }
        else if (batch == NULL)
        {
            child_json = frame->next_json;
            frame->next_json = json->nodes[child_json].next;
        }
        if (batch != NULL)
        {
            ++frame->next_json;
        }
        uint64_t child_row = frame->structure ? frame->row : frame->cursor;
        ++frame->cursor;
        result = result && depth + 1 < BUSTER_PERF_DEPTH_LIMIT;
        if (result)
        {
            frames[++depth] = (PerfValueFrame){
                .field = child,
                .json = child_json,
                .row = child_row
            };
        }
    }
    return result;
}

static bool perf_arrow_batch_json(const PerfArrowSchema *schema, PerfArrowBatch *batch, PerfBuffer *rows)
{
    bool result = true;
    for (uint64_t row = 0; row < batch->rows && result; ++row)
    {
        result = perf_arrow_row_json(schema, batch, row, NULL, UINT32_MAX, rows, &batch->json_mapping) && perf_buffer_byte(rows, '\n');
    }
    return result;
}

static bool perf_jsonl_read(PerfBytes bytes, const PerfArrowSchema *schema, PerfBuffer *rows, uint64_t *count)
{
    bool result = true;
    size_t cursor = 0;
    while (cursor < bytes.size && result)
    {
        size_t start = cursor;
        while (cursor < bytes.size && bytes.data[cursor] != '\n')
        {
            ++cursor;
        }
        PerfBytes line = {.data = bytes.data + start, .size = cursor - start};
        cursor += cursor < bytes.size;
        bool blank = true;
        for (size_t i = 0; i < line.size; ++i)
        {
            blank = blank && (line.data[i] == ' ' || line.data[i] == '\t' || line.data[i] == '\r');
        }
        if (!blank)
        {
            PerfJson document = {0};
            bool mapped = true;
            result = *count < BUSTER_PERF_ROW_LIMIT && perf_json_parse(&document, line) && perf_arrow_row_json(schema, NULL, 0, &document, 0, rows, &mapped) && mapped && perf_buffer_byte(rows, '\n');
            ++*count;
            perf_json_free(&document);
        }
    }
    return result;
}
