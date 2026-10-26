/* Original RFC 8259 parser: flat token/subtree arrays, exact numeric source
 * slices and decoded UTF-8 strings. Duplicate keys and malformed Unicode fail.
 * Input-dependent nesting never consumes the C call stack.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
typedef enum PerfJsonKind PerfJsonKind;
enum PerfJsonKind
{
    PERF_JSON_NULL,
    PERF_JSON_BOOLEAN,
    PERF_JSON_NUMBER,
    PERF_JSON_STRING,
    PERF_JSON_ARRAY,
    PERF_JSON_OBJECT
};

typedef struct PerfJsonNode PerfJsonNode;
struct PerfJsonNode
{
    PerfJsonKind kind;
    uint32_t next;
    uint32_t children;
    uint32_t text_offset;
    uint32_t text_size;
    uint32_t raw_offset;
    uint32_t raw_size;
    bool boolean;
};

typedef struct PerfJson PerfJson;
struct PerfJson
{
    PerfBuffer source;
    PerfBuffer strings;
    PerfJsonNode *nodes;
    size_t node_count;
    size_t node_capacity;
    size_t error_offset;
};

static PerfBytes perf_json_text(const PerfJson *json, uint32_t index)
{
    PerfBytes result = {0};
    if (index < json->node_count)
    {
        const PerfJsonNode *node = &json->nodes[index];
        if (node->kind == PERF_JSON_STRING)
        {
            result.data = json->strings.data == NULL ? (const unsigned char *)"" : json->strings.data + node->text_offset;
            result.size = node->text_size;
        }
        else
        {
            result.data = json->source.data + node->raw_offset;
            result.size = node->raw_size;
        }
    }
    return result;
}

static bool perf_json_is(const PerfJson *json, uint32_t index, const char *text)
{
    bool result = index < json->node_count && perf_bytes_equal(perf_json_text(json, index), perf_literal(text));
    return result;
}

static uint32_t perf_json_find_bytes(const PerfJson *json, uint32_t object, PerfBytes name)
{
    uint32_t result = UINT32_MAX;
    if (object < json->node_count && json->nodes[object].kind == PERF_JSON_OBJECT)
    {
        uint32_t cursor = object + 1;
        for (uint32_t i = 0; i < json->nodes[object].children; ++i)
        {
            if (perf_bytes_equal(perf_json_text(json, cursor), name))
            {
                result = cursor + 1;
            }
            cursor = json->nodes[cursor + 1].next;
        }
    }
    return result;
}

static uint32_t perf_json_find(const PerfJson *json, uint32_t object, const char *name)
{
    uint32_t result = perf_json_find_bytes(json, object, perf_literal(name));
    return result;
}

static int perf_json_key_compare(const void *left, const void *right)
{
    const PerfBytes *a = left;
    const PerfBytes *b = right;
    size_t size = a->size < b->size ? a->size : b->size;
    int result = size == 0 ? 0 : memcmp(a->data, b->data, size);
    if (result == 0)
    {
        result = a->size < b->size ? -1 : a->size > b->size ? 1 : 0;
    }
    return result;
}

static bool perf_json_unique_keys(const PerfJson *json)
{
    PerfBytes *keys = NULL;
    size_t capacity = 0;
    bool result = true;
    for (size_t n = 0; n < json->node_count && result; ++n)
    {
        const PerfJsonNode *node = &json->nodes[n];
        if (node->kind == PERF_JSON_OBJECT && node->children > 1)
        {
            if (node->children > capacity)
            {
                PerfBytes *grown = realloc(keys, (size_t)node->children * sizeof(*keys));
                result = grown != NULL;
                if (result)
                {
                    keys = grown;
                    capacity = node->children;
                }
            }
            result = result && keys != NULL && capacity >= node->children;
            uint32_t cursor = (uint32_t)n + 1;
            for (size_t i = 0; i < node->children && result; ++i)
            {
                keys[i] = perf_json_text(json, cursor);
                cursor = json->nodes[cursor + 1].next;
            }
            if (result)
            {
                qsort(keys, node->children, sizeof(*keys), perf_json_key_compare);
                for (size_t i = 1; i < node->children && result; ++i)
                {
                    result = !perf_bytes_equal(keys[i - 1], keys[i]);
                }
            }
        }
    }
    free(keys);
    return result;
}

static uint32_t perf_json_node(PerfJson *json, PerfJsonKind kind, size_t offset)
{
    bool valid = json->node_count < BUSTER_PERF_NODE_LIMIT;
    if (valid && json->node_count == json->node_capacity)
    {
        size_t capacity = json->node_capacity == 0 ? 256 : json->node_capacity * 2;
        if (capacity > BUSTER_PERF_NODE_LIMIT)
        {
            capacity = BUSTER_PERF_NODE_LIMIT;
        }
        PerfJsonNode *grown = realloc(json->nodes, capacity * sizeof(*grown));
        valid = grown != NULL;
        if (valid)
        {
            json->nodes = grown;
            json->node_capacity = capacity;
        }
    }
    uint32_t result = UINT32_MAX;
    if (valid)
    {
        result = (uint32_t)json->node_count++;
        json->nodes[result] = (PerfJsonNode){
            .kind = kind,
            .next = result + 1,
            .raw_offset = (uint32_t)offset
        };
    }
    return result;
}

static int perf_json_hex(unsigned char byte)
{
    int result = byte >= '0' && byte <= '9' ? byte - '0' : byte >= 'a' && byte <= 'f' ? byte - 'a' + 10 : byte >= 'A' && byte <= 'F' ? byte - 'A' + 10 : -1;
    return result;
}

static bool perf_json_hex4(PerfBytes input, size_t *position, uint32_t *point)
{
    bool result = *position <= input.size && input.size - *position >= 4;
    uint32_t value = 0;
    for (size_t i = 0; i < 4 && result; ++i)
    {
        int digit = perf_json_hex(input.data[*position + i]);
        result = digit >= 0;
        value = (value << 4) | (uint32_t)(digit < 0 ? 0 : digit);
    }
    if (result)
    {
        *position += 4;
        *point = value;
    }
    return result;
}

static bool perf_json_utf8(PerfBuffer *output, uint32_t point)
{
    unsigned char encoded[4];
    size_t size;
    bool valid = point <= 0x10ffff && !(point >= 0xd800 && point <= 0xdfff);
    if (point < 0x80)
    {
        encoded[0] = (unsigned char)point;
        size = 1;
    }
    else if (point < 0x800)
    {
        encoded[0] = 0xc0 | (unsigned char)(point >> 6);
        encoded[1] = 0x80 | (point & 63);
        size = 2;
    }
    else if (point < 0x10000)
    {
        encoded[0] = 0xe0 | (unsigned char)(point >> 12);
        encoded[1] = 0x80 | ((point >> 6) & 63);
        encoded[2] = 0x80 | (point & 63);
        size = 3;
    }
    else
    {
        encoded[0] = 0xf0 | (unsigned char)(point >> 18);
        encoded[1] = 0x80 | ((point >> 12) & 63);
        encoded[2] = 0x80 | ((point >> 6) & 63);
        encoded[3] = 0x80 | (point & 63);
        size = 4;
    }
    PerfBytes bytes = {
        .data = encoded,
        .size = size
    };
    bool result = valid && perf_buffer_append(output, bytes);
    return result;
}

static bool perf_json_string(PerfJson *json, size_t *position, uint32_t *node_index)
{
    PerfBytes input = perf_buffer_bytes(&json->source);
    size_t start = *position;
    uint32_t node = perf_json_node(json, PERF_JSON_STRING, start);
    size_t string_start = json->strings.size;
    bool result = node != UINT32_MAX && start < input.size && input.data[start] == '"';
    size_t cursor = start + 1;
    bool done = false;
    while (result && !done && cursor < input.size)
    {
        unsigned char byte = input.data[cursor++];
        if (byte == '"')
        {
            done = true;
        }
        else if (byte == '\\')
        {
            result = cursor < input.size;
            unsigned char escape = result ? input.data[cursor++] : 0;
            uint32_t point = 0;
            switch (escape)
            {
                case '"': point = '"'; break;
                case '\\': point = '\\'; break;
                case '/': point = '/'; break;
                case 'b': point = '\b'; break;
                case 'f': point = '\f'; break;
                case 'n': point = '\n'; break;
                case 'r': point = '\r'; break;
                case 't': point = '\t'; break;
                case 'u':
                    result = result && perf_json_hex4(input, &cursor, &point);
                    if (result && point >= 0xd800 && point <= 0xdbff)
                    {
                        result = input.size - cursor >= 6 && input.data[cursor] == '\\' && input.data[cursor + 1] == 'u';
                        uint32_t low = 0;
                        if (result)
                        {
                            cursor += 2;
                            result = perf_json_hex4(input, &cursor, &low) && low >= 0xdc00 && low <= 0xdfff;
                            point = 0x10000 + ((point - 0xd800) << 10) + (low - 0xdc00);
                        }
                    }
                    break;
                default:
                    result = false;
                    break;
            }
            result = result && perf_json_utf8(&json->strings, point);
        }
        else if (byte < 0x20)
        {
            result = false;
        }
        else if (byte < 0x80)
        {
            result = perf_buffer_byte(&json->strings, byte);
        }
        else
        {
            uint32_t point = 0;
            size_t continuation = 0;
            uint32_t minimum = 0;
            if (byte >= 0xc2 && byte <= 0xdf)
            {
                point = byte & 31;
                continuation = 1;
                minimum = 0x80;
            }
            else if (byte >= 0xe0 && byte <= 0xef)
            {
                point = byte & 15;
                continuation = 2;
                minimum = 0x800;
            }
            else if (byte >= 0xf0 && byte <= 0xf4)
            {
                point = byte & 7;
                continuation = 3;
                minimum = 0x10000;
            }
            else
            {
                result = false;
            }
            result = result && continuation <= input.size - cursor;
            for (size_t i = 0; i < continuation && result; ++i)
            {
                unsigned char next = input.data[cursor++];
                result = (next & 0xc0) == 0x80;
                point = (point << 6) | (next & 63);
            }
            result = result && point >= minimum && perf_json_utf8(&json->strings, point);
        }
    }
    result = result && done;
    if (result)
    {
        json->nodes[node].text_offset = (uint32_t)string_start;
        json->nodes[node].text_size = (uint32_t)(json->strings.size - string_start);
        json->nodes[node].raw_size = (uint32_t)(cursor - start);
        result = perf_buffer_byte(&json->strings, 0);
        *node_index = node;
        *position = cursor;
    }
    return result;
}

static bool perf_json_number(PerfJson *json, size_t *position, uint32_t *node_index)
{
    PerfBytes input = perf_buffer_bytes(&json->source);
    size_t start = *position;
    size_t cursor = start;
    if (cursor < input.size && input.data[cursor] == '-')
    {
        ++cursor;
    }
    bool result = cursor < input.size;
    if (result && input.data[cursor] == '0')
    {
        ++cursor;
    }
    else if (result)
    {
        result = input.data[cursor] >= '1' && input.data[cursor] <= '9';
        while (result && cursor < input.size && input.data[cursor] >= '0' && input.data[cursor] <= '9')
        {
            ++cursor;
        }
    }
    if (result && cursor < input.size && input.data[cursor] == '.')
    {
        ++cursor;
        size_t first = cursor;
        while (cursor < input.size && input.data[cursor] >= '0' && input.data[cursor] <= '9')
        {
            ++cursor;
        }
        result = cursor != first;
    }
    if (result && cursor < input.size && (input.data[cursor] == 'e' || input.data[cursor] == 'E'))
    {
        ++cursor;
        if (cursor < input.size && (input.data[cursor] == '+' || input.data[cursor] == '-'))
        {
            ++cursor;
        }
        size_t first = cursor;
        while (cursor < input.size && input.data[cursor] >= '0' && input.data[cursor] <= '9')
        {
            ++cursor;
        }
        result = cursor != first;
    }
    if (result)
    {
        uint32_t node = perf_json_node(json, PERF_JSON_NUMBER, start);
        result = node != UINT32_MAX;
        if (result)
        {
            json->nodes[node].raw_size = (uint32_t)(cursor - start);
            *node_index = node;
            *position = cursor;
        }
    }
    return result;
}

typedef struct PerfJsonFrame PerfJsonFrame;
struct PerfJsonFrame
{
    uint32_t node;
    unsigned int state;
};

static bool perf_json_parse(PerfJson *json, PerfBytes input)
{
    bool result = perf_buffer_append(&json->source, input);
    PerfJsonFrame frames[BUSTER_PERF_DEPTH_LIMIT + 1] = {0};
    frames[0].node = UINT32_MAX;
    size_t depth = 0;
    size_t cursor = 0;
    while (result)
    {
        while (cursor < input.size && (input.data[cursor] == ' ' || input.data[cursor] == '\t' || input.data[cursor] == '\r' || input.data[cursor] == '\n'))
        {
            ++cursor;
        }
        PerfJsonFrame *frame = &frames[depth];
        if (depth == 0 && frame->state == 1)
        {
            result = cursor == input.size;
            break;
        }
        if (cursor == input.size)
        {
            result = false;
            break;
        }
        PerfJsonKind kind = depth == 0 ? PERF_JSON_NULL : json->nodes[frame->node].kind;
        unsigned char byte = input.data[cursor];
        bool close_array = kind == PERF_JSON_ARRAY && (frame->state == 0 || frame->state == 1) && byte == ']';
        bool close_object = kind == PERF_JSON_OBJECT && (frame->state == 0 || frame->state == 3) && byte == '}';
        if (close_array || close_object)
        {
            ++cursor;
            json->nodes[frame->node].next = (uint32_t)json->node_count;
            json->nodes[frame->node].raw_size = (uint32_t)(cursor - json->nodes[frame->node].raw_offset);
            --depth;
            continue;
        }
        if (kind == PERF_JSON_OBJECT && (frame->state == 0 || frame->state == 4))
        {
            uint32_t key = UINT32_MAX;
            result = byte == '"' && perf_json_string(json, &cursor, &key);
            frame->state = 1;
            continue;
        }
        if (kind == PERF_JSON_OBJECT && frame->state == 1)
        {
            result = byte == ':';
            frame->state = 2;
            ++cursor;
            continue;
        }
        if ((kind == PERF_JSON_ARRAY && frame->state == 1) || (kind == PERF_JSON_OBJECT && frame->state == 3))
        {
            result = byte == ',';
            frame->state = kind == PERF_JSON_ARRAY ? 2 : 4;
            ++cursor;
            continue;
        }
        bool value_expected = depth == 0 || (kind == PERF_JSON_ARRAY && (frame->state == 0 || frame->state == 2)) || (kind == PERF_JSON_OBJECT && frame->state == 2);
        if (!value_expected)
        {
            result = false;
            break;
        }
        uint32_t value = UINT32_MAX;
        bool container = byte == '[' || byte == '{';
        if (container)
        {
            value = perf_json_node(json, byte == '[' ? PERF_JSON_ARRAY : PERF_JSON_OBJECT, cursor++);
            result = value != UINT32_MAX;
        }
        else if (byte == '"')
        {
            result = perf_json_string(json, &cursor, &value);
        }
        else if (byte == '-' || (byte >= '0' && byte <= '9'))
        {
            result = perf_json_number(json, &cursor, &value);
        }
        else
        {
            const char *word = byte == 't' ? "true" : byte == 'f' ? "false" : "null";
            size_t size = strlen(word);
            result = size <= input.size - cursor && memcmp(input.data + cursor, word, size) == 0;
            if (result)
            {
                value = perf_json_node(json, byte == 'n' ? PERF_JSON_NULL : PERF_JSON_BOOLEAN, cursor);
                result = value != UINT32_MAX;
                if (result)
                {
                    json->nodes[value].boolean = byte == 't';
                    json->nodes[value].raw_size = (uint32_t)size;
                    cursor += size;
                }
            }
        }
        if (result)
        {
            if (depth != 0)
            {
                ++json->nodes[frame->node].children;
            }
            frame->state = kind == PERF_JSON_OBJECT ? 3 : 1;
            if (container)
            {
                result = depth < BUSTER_PERF_DEPTH_LIMIT;
                if (result)
                {
                    frames[++depth] = (PerfJsonFrame){
                        .node = value
                    };
                }
            }
        }
    }
    result = result && perf_json_unique_keys(json);
    json->error_offset = cursor;
    return result;
}

static void perf_json_free(PerfJson *json)
{
    free(json->source.data);
    free(json->strings.data);
    free(json->nodes);
    *json = (PerfJson){0};
}
