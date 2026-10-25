/* First-party validation of the canonical v1 manifest schema vocabulary.
 * Numeric comparisons use exact decimal digits, never a rounded double.
 * The finite published regex vocabulary is implemented directly; an unknown
 * schema pattern is refused rather than delegated to an external regex engine.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
typedef struct PerfNumber PerfNumber;
struct PerfNumber
{
    unsigned char digits[160];
    size_t size;
    int64_t exponent;
    bool negative;
};

static bool perf_number_syntax(PerfBytes source, bool decimal_only)
{
    size_t cursor = source.size != 0 && source.data[0] == '-' ? 1 : 0;
    size_t start = cursor;
    while (cursor < source.size && source.data[cursor] >= '0' && source.data[cursor] <= '9')
    {
        ++cursor;
    }
    bool result = cursor > start && (cursor - start == 1 || source.data[start] != '0');
    if (cursor < source.size && source.data[cursor] == '.')
    {
        start = ++cursor;
        while (cursor < source.size && source.data[cursor] >= '0' && source.data[cursor] <= '9')
        {
            ++cursor;
        }
        result = result && cursor > start;
    }
    if (!decimal_only && cursor < source.size && (source.data[cursor] == 'e' || source.data[cursor] == 'E'))
    {
        ++cursor;
        cursor += cursor < source.size && (source.data[cursor] == '+' || source.data[cursor] == '-');
        start = cursor;
        while (cursor < source.size && source.data[cursor] >= '0' && source.data[cursor] <= '9')
        {
            ++cursor;
        }
        result = result && cursor > start;
    }
    result = result && cursor == source.size;
    return result;
}

static bool perf_number(PerfBytes source, PerfNumber *number)
{
    bool result = perf_number_syntax(source, false);
    size_t cursor = 0;
    if (result && source.data[cursor] == '-')
    {
        number->negative = true;
        ++cursor;
    }
    bool fraction = false;
    int64_t fractional_digits = 0;
    while (cursor < source.size && source.data[cursor] != 'e' && source.data[cursor] != 'E' && result)
    {
        unsigned char byte = source.data[cursor++];
        if (byte == '.')
        {
            result = !fraction;
            fraction = true;
        }
        else
        {
            result = byte >= '0' && byte <= '9' && number->size < sizeof(number->digits);
            if (result)
            {
                number->digits[number->size++] = byte;
                fractional_digits += fraction;
            }
        }
    }
    int64_t exponent = 0;
    bool negative_exponent = false;
    if (cursor < source.size && result)
    {
        ++cursor;
        if (cursor < source.size && (source.data[cursor] == '+' || source.data[cursor] == '-'))
        {
            negative_exponent = source.data[cursor++] == '-';
        }
        size_t first = cursor;
        while (cursor < source.size && result)
        {
            unsigned char byte = source.data[cursor++];
            result = byte >= '0' && byte <= '9' && exponent <= 100000;
            if (result)
            {
                exponent = exponent * 10 + byte - '0';
            }
        }
        result = result && cursor != first;
    }
    result = result && number->size != 0;
    if (result)
    {
        number->exponent = (negative_exponent ? -exponent : exponent) - fractional_digits;
        size_t first = 0;
        while (first + 1 < number->size && number->digits[first] == '0')
        {
            ++first;
        }
        memmove(number->digits, number->digits + first, number->size - first);
        number->size -= first;
        while (number->size > 1 && number->digits[number->size - 1] == '0')
        {
            --number->size;
            ++number->exponent;
        }
        if (number->size == 1 && number->digits[0] == '0')
        {
            number->negative = false;
            number->exponent = 0;
        }
    }
    return result;
}

static int perf_number_compare(const PerfNumber *left, const PerfNumber *right)
{
    int result = 0;
    if (left->negative != right->negative)
    {
        result = left->negative ? -1 : 1;
    }
    else
    {
        bool left_zero = left->size == 1 && left->digits[0] == '0';
        bool right_zero = right->size == 1 && right->digits[0] == '0';
        if (left_zero || right_zero)
        {
            result = left_zero == right_zero ? 0 : left_zero ? -1 : 1;
        }
        else
        {
            int64_t left_length = (int64_t)left->size + left->exponent;
            int64_t right_length = (int64_t)right->size + right->exponent;
            result = left_length < right_length ? -1 : left_length > right_length ? 1 : 0;
            size_t size = left->size > right->size ? left->size : right->size;
            for (size_t i = 0; i < size && result == 0; ++i)
            {
                unsigned char a = i < left->size ? left->digits[i] : '0';
                unsigned char b = i < right->size ? right->digits[i] : '0';
                result = a < b ? -1 : a > b ? 1 : 0;
            }
        }
        if (left->negative)
        {
            result = -result;
        }
    }
    return result;
}

static bool perf_digits(PerfBytes bytes, bool positive)
{
    bool result = bytes.size != 0 && (bytes.data[0] != '0' || (!positive && bytes.size == 1));
    for (size_t i = 0; i < bytes.size && result; ++i)
    {
        result = bytes.data[i] >= '0' && bytes.data[i] <= '9';
    }
    return result;
}

static bool perf_uint64(PerfBytes bytes, uint64_t *output)
{
    bool result = perf_digits(bytes, false);
    uint64_t number = 0;
    for (size_t i = 0; i < bytes.size && result; ++i)
    {
        unsigned int digit = bytes.data[i] - '0';
        result = number <= (UINT64_MAX - digit) / 10;
        if (result)
        {
            number = number * 10 + digit;
        }
    }
    *output = number;
    return result;
}

static bool perf_ascii_alnum(unsigned char byte)
{
    bool result = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9');
    return result;
}

static bool perf_identifier(PerfBytes value)
{
    bool result = value.size != 0 && value.size <= 160 && perf_ascii_alnum(value.data[0]);
    for (size_t i = 1; i < value.size && result; ++i)
    {
        unsigned char byte = value.data[i];
        result = perf_ascii_alnum(byte) || byte == '.' || byte == '_' || byte == ':' || byte == '-';
    }
    return result;
}

static bool perf_resource_path(PerfBytes value)
{
    bool result = value.size != 0 && perf_ascii_alnum(value.data[0]);
    size_t start = 0;
    for (size_t i = 0; i <= value.size && result; ++i)
    {
        if (i == value.size || value.data[i] == '/')
        {
            size_t length = i - start;
            result = length != 0 && !(length == 1 && value.data[start] == '.') && !(length == 2 && value.data[start] == '.' && value.data[start + 1] == '.');
            start = i + 1;
        }
        else
        {
            unsigned char byte = value.data[i];
            result = perf_ascii_alnum(byte) || byte == '.' || byte == '_' || byte == '-';
        }
    }
    return result;
}

static bool perf_https_url(PerfBytes value)
{
    bool result = value.size > 8 && memcmp(value.data, "https://", 8) == 0;
    size_t authority_end = 8;
    while (authority_end < value.size && value.data[authority_end] != '/' && value.data[authority_end] != '?' && value.data[authority_end] != '#')
    {
        ++authority_end;
    }
    result = result && authority_end > 8;
    for (size_t i = 8; i < value.size && result; ++i)
    {
        unsigned char byte = value.data[i];
        result = byte > 0x20 && byte != 0x7f && byte != '\\' && byte != '#';
        if (i < authority_end)
        {
            result = result && byte != '@';
        }
    }
    return result;
}

static bool perf_timestamp(PerfBytes value)
{
    bool result = value.size >= 20;
    size_t digit_positions[] = {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18};
    for (size_t i = 0; i < sizeof(digit_positions) / sizeof(digit_positions[0]) && result; ++i)
    {
        unsigned char byte = value.data[digit_positions[i]];
        result = byte >= '0' && byte <= '9';
    }
    if (result)
    {
        result = value.data[4] == '-' && value.data[7] == '-' && (value.data[10] == 'T' || value.data[10] == 't') && value.data[13] == ':' && value.data[16] == ':';
        unsigned int year = (value.data[0] - '0') * 1000 + (value.data[1] - '0') * 100 + (value.data[2] - '0') * 10 + value.data[3] - '0';
        unsigned int month = (value.data[5] - '0') * 10 + value.data[6] - '0';
        unsigned int day = (value.data[8] - '0') * 10 + value.data[9] - '0';
        unsigned int hour = (value.data[11] - '0') * 10 + value.data[12] - '0';
        unsigned int minute = (value.data[14] - '0') * 10 + value.data[15] - '0';
        unsigned int second = (value.data[17] - '0') * 10 + value.data[18] - '0';
        static const unsigned int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        unsigned int maximum = month >= 1 && month <= 12 ? days[month] + (month == 2 && leap) : 0;
        result = result && day >= 1 && day <= maximum && hour <= 23 && minute <= 59 && second <= 59;
        size_t cursor = 19;
        if (cursor < value.size && value.data[cursor] == '.')
        {
            size_t first = ++cursor;
            while (cursor < value.size && value.data[cursor] >= '0' && value.data[cursor] <= '9')
            {
                ++cursor;
            }
            result = result && cursor != first;
        }
        if (cursor + 1 == value.size && (value.data[cursor] == 'Z' || value.data[cursor] == 'z'))
        {
            ++cursor;
        }
        else
        {
            result = result && value.size - cursor == 6 && (value.data[cursor] == '+' || value.data[cursor] == '-');
            if (result)
            {
                unsigned int positions[] = {1, 2, 4, 5};
                for (size_t i = 0; i < 4 && result; ++i)
                {
                    unsigned char byte = value.data[cursor + positions[i]];
                    result = byte >= '0' && byte <= '9';
                }
                result = result && value.data[cursor + 3] == ':' && (value.data[cursor + 1] - '0') * 10 + value.data[cursor + 2] - '0' <= 23 && (value.data[cursor + 4] - '0') * 10 + value.data[cursor + 5] - '0' <= 59;
                cursor += 6;
            }
        }
        result = result && cursor == value.size;
    }
    return result;
}

static bool perf_schema_pattern(PerfBytes pattern, PerfBytes value)
{
    bool result = false;
    if (perf_bytes_equal(pattern, perf_literal("^[A-Za-z0-9][A-Za-z0-9._:-]{0,159}$")))
    {
        result = perf_identifier(value);
    }
    else if (perf_bytes_equal(pattern, perf_literal("^[1-9][0-9]*$")))
    {
        result = perf_digits(value, true);
    }
    else if (perf_bytes_equal(pattern, perf_literal("^(0|[1-9][0-9]{0,19})$")))
    {
        result = value.size <= 20 && perf_digits(value, false);
    }
    else if (perf_bytes_equal(pattern, perf_literal("^(0|[1-9][0-9]*)(?![\\s\\S])")))
    {
        result = perf_digits(value, false);
    }
    else if (perf_bytes_equal(pattern, perf_literal("^[0-9a-f]{64}$")))
    {
        result = value.size == 64;
        for (size_t i = 0; i < value.size && result; ++i)
        {
            result = (value.data[i] >= '0' && value.data[i] <= '9') || (value.data[i] >= 'a' && value.data[i] <= 'f');
        }
    }
    else if (perf_bytes_equal(pattern, perf_literal("^-?(0|[1-9][0-9]*)(\\.[0-9]+)?$")))
    {
        size_t cursor = value.size != 0 && value.data[0] == '-' ? 1 : 0;
        size_t integer = cursor;
        while (cursor < value.size && value.data[cursor] >= '0' && value.data[cursor] <= '9')
        {
            ++cursor;
        }
        PerfBytes whole = {
            .data = value.data + integer,
            .size = cursor - integer
        };
        result = perf_digits(whole, false);
        if (cursor < value.size && value.data[cursor] == '.')
        {
            size_t start = ++cursor;
            while (cursor < value.size && value.data[cursor] >= '0' && value.data[cursor] <= '9')
            {
                ++cursor;
            }
            result = result && cursor != start;
        }
        result = result && cursor == value.size;
    }
    else if (perf_bytes_equal(pattern, perf_literal("^(?!/)(?!.*(?:^|/)\\.\\.?(?:/|$))(?!.*//)[A-Za-z0-9][A-Za-z0-9._/-]*$")))
    {
        result = perf_resource_path(value);
    }
    else if (perf_bytes_equal(pattern, perf_literal("^https://[^/?#\\s]+(?:[/?#][^\\s]*)?(?![\\s\\S])")))
    {
        result = perf_https_url(value);
    }
    return result;
}

static bool perf_json_primitive_equal(const PerfJson *left, uint32_t a, const PerfJson *right, uint32_t b)
{
    bool result = a < left->node_count && b < right->node_count && left->nodes[a].kind == right->nodes[b].kind;
    if (result && left->nodes[a].kind == PERF_JSON_NUMBER)
    {
        PerfNumber first = {0};
        PerfNumber second = {0};
        result = perf_number(perf_json_text(left, a), &first) && perf_number(perf_json_text(right, b), &second) && perf_number_compare(&first, &second) == 0;
    }
    else if (result)
    {
        result = left->nodes[a].kind != PERF_JSON_ARRAY && left->nodes[a].kind != PERF_JSON_OBJECT && perf_bytes_equal(perf_json_text(left, a), perf_json_text(right, b));
    }
    return result;
}

static bool perf_schema_type(const PerfJson *data, uint32_t value, PerfBytes type)
{
    PerfJsonKind kind = data->nodes[value].kind;
    bool result = (kind == PERF_JSON_OBJECT && perf_bytes_equal(type, perf_literal("object"))) || (kind == PERF_JSON_ARRAY && perf_bytes_equal(type, perf_literal("array"))) || (kind == PERF_JSON_STRING && perf_bytes_equal(type, perf_literal("string"))) || (kind == PERF_JSON_BOOLEAN && perf_bytes_equal(type, perf_literal("boolean"))) || (kind == PERF_JSON_NULL && perf_bytes_equal(type, perf_literal("null"))) || (kind == PERF_JSON_NUMBER && perf_bytes_equal(type, perf_literal("number")));
    if (!result && kind == PERF_JSON_NUMBER && perf_bytes_equal(type, perf_literal("integer")))
    {
        PerfNumber number = {0};
        result = perf_number(perf_json_text(data, value), &number) && number.exponent >= 0;
    }
    return result;
}

typedef struct PerfSchemaTask PerfSchemaTask;
struct PerfSchemaTask
{
    uint32_t schema;
    uint32_t value;
    uint32_t parent;
    uint32_t children;
    uint32_t passing;
    unsigned int operation;
    bool valid;
};

static bool perf_schema_push(PerfSchemaTask *tasks, size_t *count, uint32_t parent, uint32_t schema, uint32_t value, unsigned int operation)
{
    bool result = *count < BUSTER_PERF_NODE_LIMIT && schema != UINT32_MAX && value != UINT32_MAX;
    if (result)
    {
        tasks[*count] = (PerfSchemaTask){
            .schema = schema,
            .value = value,
            .parent = parent,
            .operation = operation,
            .valid = true
        };
        if (*count != 0)
        {
            ++tasks[parent].children;
        }
        ++*count;
    }
    return result;
}

static bool perf_schema_validate(const PerfJson *schema, const PerfJson *data)
{
    PerfSchemaTask *tasks = calloc(BUSTER_PERF_NODE_LIMIT, sizeof(*tasks));
    size_t count = 0;
    bool result = tasks != NULL && schema->node_count != 0 && data->node_count != 0 && perf_schema_push(tasks, &count, 0, 0, 0, 0);
    for (size_t i = 0; i < count && result; ++i)
    {
        PerfSchemaTask *task = &tasks[i];
        if (task->operation != 0)
        {
            continue;
        }
        uint32_t definition = task->schema;
        uint32_t value = task->value;
        PerfJsonKind kind = data->nodes[value].kind;
        if (schema->nodes[definition].kind == PERF_JSON_BOOLEAN)
        {
            task->valid = schema->nodes[definition].boolean;
            continue;
        }
        uint32_t reference = perf_json_find(schema, definition, "$ref");
        if (reference != UINT32_MAX)
        {
            PerfBytes text = perf_json_text(schema, reference);
            result = text.size > 8 && memcmp(text.data, "#/$defs/", 8) == 0;
            if (result)
            {
                PerfBytes name = {
                    .data = text.data + 8,
                    .size = text.size - 8
                };
                uint32_t target = perf_json_find_bytes(schema, perf_json_find(schema, 0, "$defs"), name);
                result = perf_schema_push(tasks, &count, (uint32_t)i, target, value, 0);
            }
        }
        uint32_t type = perf_json_find(schema, definition, "type");
        task->valid = task->valid && (type == UINT32_MAX || perf_schema_type(data, value, perf_json_text(schema, type)));
        uint32_t constant = perf_json_find(schema, definition, "const");
        task->valid = task->valid && (constant == UINT32_MAX || perf_json_primitive_equal(schema, constant, data, value));
        uint32_t options = perf_json_find(schema, definition, "enum");
        if (options != UINT32_MAX)
        {
            bool found = false;
            for (uint32_t c = options + 1; c < schema->nodes[options].next; c = schema->nodes[c].next)
            {
                found |= perf_json_primitive_equal(schema, c, data, value);
            }
            task->valid = task->valid && found;
        }
        if (kind == PERF_JSON_NUMBER)
        {
            const char *bounds[] = {"minimum", "maximum", "exclusiveMinimum", "exclusiveMaximum"};
            for (size_t b = 0; b < 4; ++b)
            {
                uint32_t bound = perf_json_find(schema, definition, bounds[b]);
                if (bound != UINT32_MAX)
                {
                    PerfNumber number = {0};
                    PerfNumber limit = {0};
                    bool valid = perf_number(perf_json_text(data, value), &number) && perf_number(perf_json_text(schema, bound), &limit);
                    int comparison = valid ? perf_number_compare(&number, &limit) : 0;
                    task->valid = task->valid && valid && (b == 0 ? comparison >= 0 : b == 1 ? comparison <= 0 : b == 2 ? comparison > 0 : comparison < 0);
                }
            }
        }
        if (kind == PERF_JSON_STRING || kind == PERF_JSON_ARRAY)
        {
            uint64_t size = data->nodes[value].children;
            if (kind == PERF_JSON_STRING)
            {
                size = 0;
                PerfBytes text = perf_json_text(data, value);
                for (size_t c = 0; c < text.size; ++c)
                {
                    size += (text.data[c] & 0xc0) != 0x80;
                }
                uint32_t pattern = perf_json_find(schema, definition, "pattern");
                task->valid = task->valid && (pattern == UINT32_MAX || perf_schema_pattern(perf_json_text(schema, pattern), text));
                uint32_t format = perf_json_find(schema, definition, "format");
                task->valid = task->valid && (format == UINT32_MAX || !perf_json_is(schema, format, "date-time") || perf_timestamp(text));
            }
            const char *minimum = kind == PERF_JSON_STRING ? "minLength" : "minItems";
            const char *maximum = kind == PERF_JSON_STRING ? "maxLength" : "maxItems";
            uint32_t lower = perf_json_find(schema, definition, minimum);
            uint32_t upper = perf_json_find(schema, definition, maximum);
            uint64_t limit;
            if (lower != UINT32_MAX)
            {
                task->valid = task->valid && perf_uint64(perf_json_text(schema, lower), &limit) && size >= limit;
            }
            if (upper != UINT32_MAX)
            {
                task->valid = task->valid && perf_uint64(perf_json_text(schema, upper), &limit) && size <= limit;
            }
            if (kind == PERF_JSON_ARRAY)
            {
                uint32_t items = perf_json_find(schema, definition, "items");
                for (uint32_t c = value + 1; items != UINT32_MAX && c < data->nodes[value].next && result; c = data->nodes[c].next)
                {
                    result = perf_schema_push(tasks, &count, (uint32_t)i, items, c, 0);
                }
            }
        }
        if (kind == PERF_JSON_OBJECT)
        {
            uint32_t required = perf_json_find(schema, definition, "required");
            for (uint32_t c = required == UINT32_MAX ? 0 : required + 1; required != UINT32_MAX && c < schema->nodes[required].next; c = schema->nodes[c].next)
            {
                task->valid = task->valid && perf_json_find_bytes(data, value, perf_json_text(schema, c)) != UINT32_MAX;
            }
            uint32_t properties = perf_json_find(schema, definition, "properties");
            uint32_t additional = perf_json_find(schema, definition, "additionalProperties");
            for (uint32_t key = value + 1; key < data->nodes[value].next && result; key = data->nodes[key + 1].next)
            {
                uint32_t property = perf_json_find_bytes(schema, properties, perf_json_text(data, key));
                property = property == UINT32_MAX ? additional : property;
                if (property != UINT32_MAX)
                {
                    result = perf_schema_push(tasks, &count, (uint32_t)i, property, key + 1, 0);
                }
            }
        }
        uint32_t one_of = perf_json_find(schema, definition, "oneOf");
        if (one_of != UINT32_MAX && result)
        {
            uint32_t group = (uint32_t)count;
            result = perf_schema_push(tasks, &count, (uint32_t)i, one_of, value, 1);
            for (uint32_t c = one_of + 1; c < schema->nodes[one_of].next && result; c = schema->nodes[c].next)
            {
                result = perf_schema_push(tasks, &count, group, c, value, 0);
            }
        }
        uint32_t not = perf_json_find(schema, definition, "not");
        if (not != UINT32_MAX && result)
        {
            uint32_t group = (uint32_t)count;
            result = perf_schema_push(tasks, &count, (uint32_t)i, not, value, 2) && perf_schema_push(tasks, &count, group, not, value, 0);
        }
    }
    for (size_t remaining = count; remaining != 0 && result; --remaining)
    {
        PerfSchemaTask *task = &tasks[remaining - 1];
        task->valid = task->valid && (task->operation == 1 ? task->passing == 1 : task->operation == 2 ? task->passing == 0 : task->passing == task->children);
        if (remaining > 1)
        {
            tasks[task->parent].passing += task->valid;
        }
        else
        {
            result = task->valid;
        }
    }
    free(tasks);
    return result;
}
