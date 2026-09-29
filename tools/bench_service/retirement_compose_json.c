/* #881-E canonical JSON for the result composer (Linux service code only).
 *
 * Ownership: lane E. A strict, non-recursive JSON reader and Python-exact
 * canonical writer, used to derive the validator's `_execution_context`
 * from the post-A/A binding (so the receipt's context is never a
 * caller-supplied template) and to check the adapter's output structurally.
 * The canonical form is json.dumps(value, sort_keys=True,
 * separators=(",", ":"), ensure_ascii=False).encode("utf-8"), as the
 * validator's _canonical_json_digest writes it.
 *
 * Map (searchable symbols):
 *   helpers    tp_retirement_compose_allocate, tp_retirement_compose_float_repr,
 *              tp_compose_json_utf8, tp_compose_json_encode
 *   reader     TpComposeJsonParser, tp_compose_json_structure,
 *              tp_compose_json_string, tp_compose_json_number,
 *              tp_compose_json_node, tp_compose_json_sort,
 *              tp_retirement_compose_json_parse
 *   queries    tp_retirement_compose_json_member, tp_retirement_compose_json_keys
 *   writer     TpComposeJsonWriter, tp_compose_json_put_string,
 *              tp_compose_json_emit, tp_retirement_compose_json_canonical
 *   context    tp_retirement_compose_execution_context
 *
 * Refused (the validator refuses or cannot encode them): duplicate keys,
 * lone surrogates, control bytes in strings, malformed UTF-8, NaN/Infinity,
 * numbers that overflow a double, integers beyond Python's 4300-digit
 * conversion limit and nesting beyond TP_COMPOSE_JSON_DEPTH.
 */
#define _GNU_SOURCE 1
#include "retirement_compose_json.h"
#ifdef __linux__
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Containers nested deeper than this are refused (CPython's own decoder
 * stops near its recursion limit). */
#define TP_COMPOSE_JSON_DEPTH 256u
/* CPython's default int max_str_digits. */
#define TP_COMPOSE_JSON_INTEGER_DIGITS 4300u
/* A float's text is copied for strtod up to this length. */
#define TP_COMPOSE_JSON_NUMBER_BYTES 4400u
#define TP_COMPOSE_JSON_ALIGNMENT 16u
/* The canonical execution context is bounded like its template was. */
#define TP_COMPOSE_JSON_CONTEXT_BYTES (UINT64_C(1) << 20)
/* Canonical output per input byte: strings never grow, and the shortest
 * float text (`1e5`, three bytes) has a repr of at most 22 bytes. */
#define TP_COMPOSE_JSON_GROWTH 8u
/* A UTF-8 sequence is at most four bytes. */
#define TP_COMPOSE_JSON_UTF8_MAX 4u

/* ---------------------------------------------------------------- helpers */

void* tp_retirement_compose_allocate(Arena* arena, uint64_t bytes)
{
    void* result = NULL;
    uint64_t aligned = (bytes + TP_COMPOSE_JSON_ALIGNMENT - 1) & ~(uint64_t)(TP_COMPOSE_JSON_ALIGNMENT - 1);
    int valid = arena && bytes && bytes <= UINT64_MAX - TP_COMPOSE_JSON_ALIGNMENT &&
                arena->position <= arena->reserved_size &&
                arena->reserved_size - arena->position >= TP_COMPOSE_JSON_ALIGNMENT &&
                aligned <= arena->reserved_size - arena->position - TP_COMPOSE_JSON_ALIGNMENT;
    if (valid) result = arena_allocate_zeroed_bytes(arena, aligned, TP_COMPOSE_JSON_ALIGNMENT);
    return result;
}

/* Python's repr(float): the shortest round-tripping digits, fixed notation
 * for -4 < decpt <= 16, otherwise d.ddde+XX; negative values (and -0.0) are
 * the repr of their magnitude with a leading '-'. */
int tp_retirement_compose_float_repr(double value, char output[TP_RETIREMENT_COMPOSE_REPR_BYTES])
{
    int negative = signbit(value) != 0;
    double magnitude = negative ? -value : value;
    int valid = isfinite(value);
    uint64_t mantissa = 0;
    int exponent = 0;
    unsigned digits = 0;
    size_t used = 0;
    if (valid && negative) output[used++] = '-';
    if (valid && magnitude == 0.0) used += (size_t)snprintf(output + used, TP_RETIREMENT_COMPOSE_REPR_BYTES - used, "0.0");
    else if (valid)
    {
        int found = 0;
        for (unsigned precision = 1; !found && precision <= 17; ++precision)
        {
            char text[TP_RETIREMENT_COMPOSE_REPR_BYTES], candidate[TP_RETIREMENT_COMPOSE_REPR_BYTES + 8];
            snprintf(text, sizeof(text), "%.*e", (int)precision - 1, magnitude);
            uint64_t parsed = 0;
            int exp10 = 0;
            char const* cursor = text;
            while (*cursor && *cursor != 'e')
            {
                if (*cursor >= '0' && *cursor <= '9') parsed = parsed * 10 + (uint64_t)(*cursor - '0');
                ++cursor;
            }
            if (*cursor == 'e') exp10 = atoi(cursor + 1);
            uint64_t low = 1;
            for (unsigned i = 1; i < precision; ++i) low *= 10;
            uint64_t high = low * 10;
            for (int delta = 0; !found && delta < 3; ++delta)
            {
                uint64_t trial = delta == 0 ? parsed : delta == 1 ? parsed - 1 : parsed + 1;
                if (trial >= low && trial < high)
                {
                    snprintf(candidate, sizeof(candidate), "%" PRIu64 "e%d", trial, exp10 - (int)(precision - 1));
                    if (strtod(candidate, NULL) == magnitude)
                    {
                        found = 1;
                        mantissa = trial;
                        exponent = exp10;
                        digits = precision;
                    }
                }
            }
        }
        valid = found;
        if (valid)
        {
            char text[24];
            snprintf(text, sizeof(text), "%" PRIu64, mantissa);
            while (digits > 1 && text[digits - 1] == '0') text[--digits] = 0;
            int point = exponent + 1;
            size_t room = TP_RETIREMENT_COMPOSE_REPR_BYTES;
            if (point > -4 && point <= 16)
            {
                if (point <= 0)
                {
                    used += (size_t)snprintf(output + used, room - used, "0.");
                    for (int i = 0; i < -point; ++i) output[used++] = '0';
                    used += (size_t)snprintf(output + used, room - used, "%s", text);
                }
                else if ((unsigned)point >= digits)
                {
                    used += (size_t)snprintf(output + used, room - used, "%s", text);
                    for (unsigned i = digits; i < (unsigned)point; ++i) output[used++] = '0';
                    used += (size_t)snprintf(output + used, room - used, ".0");
                }
                else used += (size_t)snprintf(output + used, room - used, "%.*s.%s", point, text, text + point);
            }
            else if (digits > 1)
                used += (size_t)snprintf(output + used, room - used, "%c.%se%c%02d", text[0], text + 1,
                                         exponent < 0 ? '-' : '+', exponent < 0 ? -exponent : exponent);
            else
                used += (size_t)snprintf(output + used, room - used, "%ce%c%02d", text[0], exponent < 0 ? '-' : '+',
                                         exponent < 0 ? -exponent : exponent);
        }
    }
    if (valid) output[used] = 0;
    else output[0] = 0;
    return valid;
}

/* The length of one well-formed UTF-8 sequence (no overlong form, no
 * surrogate, at most U+10FFFF) at bytes[offset], or 0. */
BUSTER_GLOBAL_LOCAL unsigned tp_compose_json_utf8(unsigned char const* bytes, size_t length, size_t offset)
{
    unsigned char lead = bytes[offset];
    unsigned size = lead < 0x80 ? 1u : lead >= 0xc2 && lead <= 0xdf ? 2u : lead >= 0xe0 && lead <= 0xef ? 3u :
                    lead >= 0xf0 && lead <= 0xf4 ? 4u : 0u;
    int valid = size && size <= length - offset;
    for (unsigned i = 1; valid && i < size; ++i) valid = (bytes[offset + i] & 0xc0) == 0x80;
    unsigned char second = valid && size > 1 ? bytes[offset + 1] : 0;
    if (valid && size == 3) valid = !(lead == 0xe0 && second < 0xa0) && !(lead == 0xed && second >= 0xa0);
    if (valid && size == 4) valid = !(lead == 0xf0 && second < 0x90) && !(lead == 0xf4 && second >= 0x90);
    return valid ? size : 0u;
}

/* UTF-8 bytes of one scalar value (never a surrogate). */
BUSTER_GLOBAL_LOCAL unsigned tp_compose_json_encode(uint32_t point, char output[TP_COMPOSE_JSON_UTF8_MAX])
{
    unsigned size = 0;
    if (point < 0x80) output[size++] = (char)point;
    else if (point < 0x800)
    {
        output[size++] = (char)(0xc0 | (point >> 6));
        output[size++] = (char)(0x80 | (point & 0x3f));
    }
    else if (point < 0x10000)
    {
        output[size++] = (char)(0xe0 | (point >> 12));
        output[size++] = (char)(0x80 | ((point >> 6) & 0x3f));
        output[size++] = (char)(0x80 | (point & 0x3f));
    }
    else
    {
        output[size++] = (char)(0xf0 | (point >> 18));
        output[size++] = (char)(0x80 | ((point >> 12) & 0x3f));
        output[size++] = (char)(0x80 | ((point >> 6) & 0x3f));
        output[size++] = (char)(0x80 | (point & 0x3f));
    }
    return size;
}

/* ----------------------------------------------------------------- reader */

typedef struct TpComposeJsonParser
{
    unsigned char const* bytes;
    size_t length, offset;
    TpComposeJson* json;
    unsigned capacity;
    char* strings;
    size_t strings_used, strings_capacity;
    char* number;
    int valid;
} TpComposeJsonParser;

/* An upper bound on the value count: every value after the first follows a
 * comma or opens a container. Strings are skipped with their escapes. */
BUSTER_GLOBAL_LOCAL uint64_t tp_compose_json_structure(unsigned char const* bytes, size_t length)
{
    uint64_t values = 1;
    int quoted = 0;
    for (size_t i = 0; i < length; ++i)
    {
        unsigned char c = bytes[i];
        if (quoted && c == '\\') ++i;
        else if (c == '"') quoted = !quoted;
        else if (!quoted && (c == ',' || c == '[' || c == '{')) ++values;
    }
    return values;
}

BUSTER_GLOBAL_LOCAL void tp_compose_json_space(TpComposeJsonParser* parser)
{
    while (parser->offset < parser->length &&
           (parser->bytes[parser->offset] == ' ' || parser->bytes[parser->offset] == '\t' ||
            parser->bytes[parser->offset] == '\n' || parser->bytes[parser->offset] == '\r'))
        ++parser->offset;
}

BUSTER_GLOBAL_LOCAL int tp_compose_json_hex4(TpComposeJsonParser* parser, uint32_t* point)
{
    uint32_t value = 0;
    int valid = parser->offset <= parser->length && parser->length - parser->offset >= 4;
    for (unsigned i = 0; valid && i < 4; ++i)
    {
        unsigned char c = parser->bytes[parser->offset + i];
        unsigned digit = c >= '0' && c <= '9' ? (unsigned)(c - '0') : c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10) :
                         c >= 'A' && c <= 'F' ? (unsigned)(c - 'A' + 10) : 16u;
        valid = digit < 16;
        value = value * 16 + digit;
    }
    if (valid) parser->offset += 4;
    *point = valid ? value : 0;
    return valid;
}

BUSTER_GLOBAL_LOCAL int tp_compose_json_append(TpComposeJsonParser* parser, char const* data, size_t length)
{
    int valid = length <= parser->strings_capacity - parser->strings_used;
    if (valid)
    {
        memcpy(parser->strings + parser->strings_used, data, length);
        parser->strings_used += length;
    }
    return valid;
}

/* One string at the opening quote, decoded (escapes resolved, surrogate
 * pairs joined) into the string store. */
BUSTER_GLOBAL_LOCAL int tp_compose_json_string(TpComposeJsonParser* parser, char const** text, uint32_t* length)
{
    size_t start = parser->strings_used;
    int valid = parser->valid && parser->offset < parser->length && parser->bytes[parser->offset] == '"';
    int closed = 0;
    if (valid) ++parser->offset;
    while (valid && !closed)
    {
        valid = parser->offset < parser->length;
        unsigned char c = valid ? parser->bytes[parser->offset] : 0;
        if (!valid) break;
        if (c == '"')
        {
            closed = 1;
            ++parser->offset;
        }
        else if (c == '\\')
        {
            valid = parser->length - parser->offset >= 2;
            unsigned char escape = valid ? parser->bytes[parser->offset + 1] : 0;
            parser->offset += 2;
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
                valid = tp_compose_json_hex4(parser, &point);
                if (valid && point >= 0xd800 && point <= 0xdbff)
                {
                    uint32_t low = 0;
                    valid = parser->length - parser->offset >= 2 && parser->bytes[parser->offset] == '\\' &&
                            parser->bytes[parser->offset + 1] == 'u';
                    if (valid)
                    {
                        parser->offset += 2;
                        valid = tp_compose_json_hex4(parser, &low) && low >= 0xdc00 && low <= 0xdfff;
                    }
                    if (valid) point = 0x10000 + ((point - 0xd800) << 10) + (low - 0xdc00);
                }
                else if (valid) valid = point < 0xdc00 || point > 0xdfff;
                break;
            default:
                valid = 0;
                break;
            }
            char encoded[TP_COMPOSE_JSON_UTF8_MAX];
            valid = valid && tp_compose_json_append(parser, encoded, tp_compose_json_encode(point, encoded));
        }
        else if (c < 0x20) valid = 0;
        else
        {
            unsigned size = tp_compose_json_utf8(parser->bytes, parser->length, parser->offset);
            valid = size && tp_compose_json_append(parser, (char const*)parser->bytes + parser->offset, size);
            parser->offset += size;
        }
    }
    valid = valid && parser->strings_used - start <= UINT32_MAX;
    *text = valid ? parser->strings + start : NULL;
    *length = valid ? (uint32_t)(parser->strings_used - start) : 0;
    parser->valid = valid;
    return valid;
}

BUSTER_GLOBAL_LOCAL unsigned tp_compose_json_node(TpComposeJsonParser* parser, unsigned kind, unsigned parent,
                                                  char const* key, uint32_t key_length)
{
    TpComposeJson* json = parser->json;
    unsigned index = json->count;
    parser->valid = parser->valid && index < parser->capacity;
    if (parser->valid)
    {
        json->nodes[index] = (TpComposeJsonNode){kind, parent, TP_COMPOSE_JSON_NONE, TP_COMPOSE_JSON_NONE,
                                                 TP_COMPOSE_JSON_NONE, 0, NULL, key, 0, key_length, 0.0};
        if (parent != TP_COMPOSE_JSON_NONE)
        {
            TpComposeJsonNode* container = json->nodes + parent;
            if (container->last == TP_COMPOSE_JSON_NONE) container->first = index;
            else json->nodes[container->last].next = index;
            container->last = index;
            ++container->count;
        }
        ++json->count;
    }
    return parser->valid ? index : TP_COMPOSE_JSON_NONE;
}

/* -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)?: an integer keeps its digits
 * (`-0` is Python's 0); anything with a fraction or exponent is a finite
 * double parsed with the correctly rounded strtod, as float() parses it. */
BUSTER_GLOBAL_LOCAL void tp_compose_json_number(TpComposeJsonParser* parser, unsigned parent, char const* key,
                                                uint32_t key_length)
{
    size_t start = parser->offset, i = start;
    unsigned char const* bytes = parser->bytes;
    size_t length = parser->length;
    int negative = i < length && bytes[i] == '-';
    if (negative) ++i;
    size_t digits_start = i;
    int valid = i < length && bytes[i] >= '0' && bytes[i] <= '9';
    if (valid && bytes[i] == '0') ++i;
    else while (i < length && bytes[i] >= '0' && bytes[i] <= '9') ++i;
    size_t integer_digits = i - digits_start;
    int real = 0;
    if (valid && i < length && bytes[i] == '.')
    {
        size_t fraction = ++i;
        while (i < length && bytes[i] >= '0' && bytes[i] <= '9') ++i;
        valid = i > fraction;
        real = 1;
    }
    if (valid && i < length && (bytes[i] == 'e' || bytes[i] == 'E'))
    {
        ++i;
        if (i < length && (bytes[i] == '+' || bytes[i] == '-')) ++i;
        size_t exponent = i;
        while (i < length && bytes[i] >= '0' && bytes[i] <= '9') ++i;
        valid = i > exponent;
        real = 1;
    }
    size_t text_length = i - start;
    valid = valid && text_length < TP_COMPOSE_JSON_NUMBER_BYTES && (real || integer_digits <= TP_COMPOSE_JSON_INTEGER_DIGITS);
    double value = 0.0;
    if (valid)
    {
        memcpy(parser->number, bytes + start, text_length);
        parser->number[text_length] = 0;
        char* end = NULL;
        value = strtod(parser->number, &end);
        valid = end == parser->number + text_length && (!real || isfinite(value));
    }
    parser->valid = parser->valid && valid;
    unsigned node = tp_compose_json_node(parser, real ? TP_COMPOSE_JSON_FLOAT : TP_COMPOSE_JSON_INTEGER, parent, key,
                                         key_length);
    if (parser->valid)
    {
        TpComposeJsonNode* entry = parser->json->nodes + node;
        int zero = negative && integer_digits == 1 && bytes[digits_start] == '0';
        entry->text = real ? NULL : zero ? "0" : (char const*)bytes + start;
        entry->length = real ? 0u : zero ? 1u : (uint32_t)text_length;
        entry->number = value;
        parser->offset = i;
    }
}

BUSTER_GLOBAL_LOCAL int tp_compose_json_literal(TpComposeJsonParser* parser, char const* literal)
{
    size_t length = strlen(literal);
    int valid = parser->offset <= parser->length && parser->length - parser->offset >= length &&
                !memcmp(parser->bytes + parser->offset, literal, length);
    if (valid) parser->offset += length;
    return valid;
}

/* `"key"` `:` with surrounding whitespace, before an object member's value. */
BUSTER_GLOBAL_LOCAL void tp_compose_json_key(TpComposeJsonParser* parser, char const** key, uint32_t* key_length)
{
    tp_compose_json_space(parser);
    tp_compose_json_string(parser, key, key_length);
    tp_compose_json_space(parser);
    parser->valid = parser->valid && tp_compose_json_literal(parser, ":");
    tp_compose_json_space(parser);
}

BUSTER_GLOBAL_LOCAL int tp_compose_json_key_order(TpComposeJsonNode const* left, TpComposeJsonNode const* right)
{
    uint32_t shorter = left->key_length < right->key_length ? left->key_length : right->key_length;
    int order = shorter ? memcmp(left->key, right->key, shorter) : 0;
    if (!order) order = left->key_length < right->key_length ? -1 : left->key_length > right->key_length ? 1 : 0;
    return order;
}

/* Relink every object's members in code-point key order (UTF-8 byte order),
 * refusing duplicate keys, with an iterative merge sort. */
BUSTER_GLOBAL_LOCAL int tp_compose_json_sort(TpComposeJson* json, unsigned* order, unsigned* scratch)
{
    int valid = 1;
    for (unsigned n = 0; valid && n < json->count; ++n)
    {
        TpComposeJsonNode* object = json->nodes + n;
        unsigned count = 0;
        if (object->kind == TP_COMPOSE_JSON_OBJECT)
            for (unsigned child = object->first; child != TP_COMPOSE_JSON_NONE; child = json->nodes[child].next)
                order[count++] = child;
        for (unsigned width = 1; width < count; width *= 2)
        {
            for (unsigned left = 0; left < count; left += 2 * width)
            {
                unsigned middle = left + width < count ? left + width : count;
                unsigned right = left + 2 * width < count ? left + 2 * width : count;
                unsigned a = left, b = middle, out = left;
                while (a < middle || b < right)
                {
                    int take_left = b >= right ||
                        (a < middle && tp_compose_json_key_order(json->nodes + order[a], json->nodes + order[b]) <= 0);
                    scratch[out++] = take_left ? order[a++] : order[b++];
                }
            }
            memcpy(order, scratch, (size_t)count * sizeof(*order));
        }
        for (unsigned i = 1; valid && i < count; ++i)
            valid = tp_compose_json_key_order(json->nodes + order[i - 1], json->nodes + order[i]) < 0;
        if (valid && count)
        {
            object->first = order[0];
            object->last = order[count - 1];
            for (unsigned i = 0; i < count; ++i)
                json->nodes[order[i]].next = i + 1 < count ? order[i + 1] : TP_COMPOSE_JSON_NONE;
        }
    }
    return valid;
}

int tp_retirement_compose_json_parse(unsigned char const* bytes, size_t length, Arena* arena, TpComposeJson* json)
{
    *json = (TpComposeJson){0};
    uint64_t capacity = bytes && length ? tp_compose_json_structure(bytes, length) : 0;
    TpComposeJsonParser parser = {.bytes = bytes, .length = length, .json = json, .strings_capacity = length,
                                  .valid = capacity && capacity < TP_COMPOSE_JSON_NONE && length < UINT32_MAX};
    parser.capacity = parser.valid ? (unsigned)capacity : 0;
    json->nodes = parser.valid ? (TpComposeJsonNode*)tp_retirement_compose_allocate(arena,
                      capacity * sizeof(TpComposeJsonNode)) : NULL;
    parser.strings = parser.valid ? (char*)tp_retirement_compose_allocate(arena, length) : NULL;
    parser.number = parser.valid ? (char*)tp_retirement_compose_allocate(arena, TP_COMPOSE_JSON_NUMBER_BYTES) : NULL;
    unsigned* order = parser.valid ? (unsigned*)tp_retirement_compose_allocate(arena, capacity * sizeof(unsigned)) : NULL;
    unsigned* scratch = parser.valid ? (unsigned*)tp_retirement_compose_allocate(arena, capacity * sizeof(unsigned)) : NULL;
    parser.valid = parser.valid && json->nodes && parser.strings && parser.number && order && scratch;
    unsigned current = TP_COMPOSE_JSON_NONE, depth = 0;
    char const* key = NULL;
    uint32_t key_length = 0;
    int expect_value = 1, done = 0;
    tp_compose_json_space(&parser);
    while (parser.valid && !done)
    {
        unsigned char c = parser.offset < parser.length ? bytes[parser.offset] : 0;
        if (expect_value)
        {
            if (c == '{' || c == '[')
            {
                unsigned kind = c == '{' ? TP_COMPOSE_JSON_OBJECT : TP_COMPOSE_JSON_ARRAY;
                unsigned node = tp_compose_json_node(&parser, kind, current, key, key_length);
                ++parser.offset;
                parser.valid = parser.valid && depth < TP_COMPOSE_JSON_DEPTH;
                ++depth;
                current = node;
                key = NULL;
                key_length = 0;
                tp_compose_json_space(&parser);
                unsigned char next = parser.offset < parser.length ? bytes[parser.offset] : 0;
                if (parser.valid && next == (kind == TP_COMPOSE_JSON_OBJECT ? '}' : ']'))
                {
                    ++parser.offset;
                    current = json->nodes[current].parent;
                    --depth;
                    expect_value = 0;
                }
                else if (parser.valid && kind == TP_COMPOSE_JSON_OBJECT) tp_compose_json_key(&parser, &key, &key_length);
            }
            else
            {
                if (c == '"')
                {
                    char const* text = NULL;
                    uint32_t text_length = 0;
                    tp_compose_json_string(&parser, &text, &text_length);
                    unsigned node = tp_compose_json_node(&parser, TP_COMPOSE_JSON_STRING, current, key, key_length);
                    if (parser.valid)
                    {
                        json->nodes[node].text = text;
                        json->nodes[node].length = text_length;
                    }
                }
                else if (c == '-' || (c >= '0' && c <= '9')) tp_compose_json_number(&parser, current, key, key_length);
                else if (c == 't' && tp_compose_json_literal(&parser, "true"))
                    tp_compose_json_node(&parser, TP_COMPOSE_JSON_TRUE, current, key, key_length);
                else if (c == 'f' && tp_compose_json_literal(&parser, "false"))
                    tp_compose_json_node(&parser, TP_COMPOSE_JSON_FALSE, current, key, key_length);
                else if (c == 'n' && tp_compose_json_literal(&parser, "null"))
                    tp_compose_json_node(&parser, TP_COMPOSE_JSON_NULL, current, key, key_length);
                else parser.valid = 0;
                expect_value = 0;
            }
        }
        else
        {
            tp_compose_json_space(&parser);
            c = parser.offset < parser.length ? bytes[parser.offset] : 0;
            if (current == TP_COMPOSE_JSON_NONE) done = 1;
            else if (c == ',')
            {
                ++parser.offset;
                tp_compose_json_space(&parser);
                key = NULL;
                key_length = 0;
                if (json->nodes[current].kind == TP_COMPOSE_JSON_OBJECT) tp_compose_json_key(&parser, &key, &key_length);
                expect_value = 1;
            }
            else if (c == (json->nodes[current].kind == TP_COMPOSE_JSON_OBJECT ? '}' : ']'))
            {
                ++parser.offset;
                current = json->nodes[current].parent;
                --depth;
            }
            else parser.valid = 0;
        }
    }
    tp_compose_json_space(&parser);
    int valid = parser.valid && done && parser.offset == length && json->count &&
                tp_compose_json_sort(json, order, scratch);
    if (!valid) *json = (TpComposeJson){0};
    return valid;
}

/* ---------------------------------------------------------------- queries */

unsigned tp_retirement_compose_json_member(TpComposeJson const* json, unsigned node, char const* key)
{
    unsigned found = TP_COMPOSE_JSON_NONE;
    size_t length = strlen(key);
    int object = node < json->count && json->nodes[node].kind == TP_COMPOSE_JSON_OBJECT;
    for (unsigned child = object ? json->nodes[node].first : TP_COMPOSE_JSON_NONE; child != TP_COMPOSE_JSON_NONE;
         child = json->nodes[child].next)
        if (json->nodes[child].key_length == length && !memcmp(json->nodes[child].key, key, length)) found = child;
    return found;
}

int tp_retirement_compose_json_keys(TpComposeJson const* json, unsigned node, char const* const* keys, unsigned count)
{
    int valid = node < json->count && json->nodes[node].kind == TP_COMPOSE_JSON_OBJECT &&
                json->nodes[node].count == count;
    for (unsigned i = 0; valid && i < count; ++i)
        valid = tp_retirement_compose_json_member(json, node, keys[i]) != TP_COMPOSE_JSON_NONE;
    return valid;
}

/* ----------------------------------------------------------------- writer */

typedef struct TpComposeJsonWriter
{
    char* bytes;
    size_t length, capacity;
    int valid;
} TpComposeJsonWriter;

BUSTER_GLOBAL_LOCAL void tp_compose_json_put(TpComposeJsonWriter* writer, char const* data, size_t length)
{
    writer->valid = writer->valid && length <= writer->capacity - writer->length;
    if (writer->valid)
    {
        memcpy(writer->bytes + writer->length, data, length);
        writer->length += length;
    }
}

/* json's ensure_ascii=False string form: `"`, `\` and the C0 controls are
 * escaped (\b \f \n \r \t, otherwise \u00xx); everything else is copied. */
BUSTER_GLOBAL_LOCAL void tp_compose_json_put_string(TpComposeJsonWriter* writer, char const* text, size_t length)
{
    tp_compose_json_put(writer, "\"", 1);
    for (size_t i = 0; writer->valid && i < length; ++i)
    {
        unsigned char c = (unsigned char)text[i];
        char escape[8];
        char const* replacement = c == '"' ? "\\\"" : c == '\\' ? "\\\\" : c == '\b' ? "\\b" : c == '\f' ? "\\f" :
                                  c == '\n' ? "\\n" : c == '\r' ? "\\r" : c == '\t' ? "\\t" : NULL;
        if (!replacement && c < 0x20)
        {
            snprintf(escape, sizeof(escape), "\\u%04x", c);
            replacement = escape;
        }
        if (replacement) tp_compose_json_put(writer, replacement, strlen(replacement));
        else tp_compose_json_put(writer, text + i, 1);
    }
    tp_compose_json_put(writer, "\"", 1);
}

/* The canonical bytes of one value, with an explicit container stack. */
BUSTER_GLOBAL_LOCAL void tp_compose_json_emit(TpComposeJsonWriter* writer, TpComposeJson const* json, unsigned root)
{
    unsigned stack[TP_COMPOSE_JSON_DEPTH + 1], cursor[TP_COMPOSE_JSON_DEPTH + 1];
    unsigned depth = 0, pending = root;
    writer->valid = writer->valid && root < json->count;
    while (writer->valid && (pending != TP_COMPOSE_JSON_NONE || depth))
    {
        if (pending != TP_COMPOSE_JSON_NONE)
        {
            TpComposeJsonNode const* node = json->nodes + pending;
            char repr[TP_RETIREMENT_COMPOSE_REPR_BYTES];
            switch (node->kind)
            {
            case TP_COMPOSE_JSON_NULL: tp_compose_json_put(writer, "null", 4); break;
            case TP_COMPOSE_JSON_FALSE: tp_compose_json_put(writer, "false", 5); break;
            case TP_COMPOSE_JSON_TRUE: tp_compose_json_put(writer, "true", 4); break;
            case TP_COMPOSE_JSON_INTEGER: tp_compose_json_put(writer, node->text, node->length); break;
            case TP_COMPOSE_JSON_FLOAT:
                writer->valid = writer->valid && tp_retirement_compose_float_repr(node->number, repr);
                tp_compose_json_put(writer, repr, strlen(repr));
                break;
            case TP_COMPOSE_JSON_STRING: tp_compose_json_put_string(writer, node->text, node->length); break;
            default:
                writer->valid = writer->valid && depth <= TP_COMPOSE_JSON_DEPTH;
                tp_compose_json_put(writer, node->kind == TP_COMPOSE_JSON_OBJECT ? "{" : "[", 1);
                if (writer->valid)
                {
                    stack[depth] = pending;
                    cursor[depth] = node->first;
                    ++depth;
                }
                break;
            }
            pending = TP_COMPOSE_JSON_NONE;
        }
        else
        {
            TpComposeJsonNode const* container = json->nodes + stack[depth - 1];
            unsigned child = cursor[depth - 1];
            int object = container->kind == TP_COMPOSE_JSON_OBJECT;
            if (child == TP_COMPOSE_JSON_NONE)
            {
                tp_compose_json_put(writer, object ? "}" : "]", 1);
                --depth;
            }
            else
            {
                if (child != container->first) tp_compose_json_put(writer, ",", 1);
                if (object)
                {
                    tp_compose_json_put_string(writer, json->nodes[child].key, json->nodes[child].key_length);
                    tp_compose_json_put(writer, ":", 1);
                }
                cursor[depth - 1] = json->nodes[child].next;
                pending = child;
            }
        }
    }
}

int tp_retirement_compose_json_canonical(unsigned char const* bytes, size_t length, Arena* arena, char** output,
                                         size_t* output_length)
{
    TpComposeJson json = {0};
    uint64_t capacity = (uint64_t)length * TP_COMPOSE_JSON_GROWTH + TP_RETIREMENT_COMPOSE_REPR_BYTES;
    int valid = output && output_length && tp_retirement_compose_json_parse(bytes, length, arena, &json);
    TpComposeJsonWriter writer = {.capacity = (size_t)capacity, .valid = valid};
    writer.bytes = valid ? (char*)tp_retirement_compose_allocate(arena, capacity) : NULL;
    writer.valid = valid && writer.bytes;
    tp_compose_json_emit(&writer, &json, 0);
    valid = writer.valid;
    if (output) *output = valid ? writer.bytes : NULL;
    if (output_length) *output_length = valid ? writer.length : 0;
    return valid;
}

/* ---------------------------------------------------------------- context */

BUSTER_GLOBAL_LOCAL unsigned tp_compose_json_path(TpComposeJson const* json, char const* const* keys, unsigned count)
{
    unsigned node = json->count ? 0u : TP_COMPOSE_JSON_NONE;
    for (unsigned i = 0; node != TP_COMPOSE_JSON_NONE && i < count; ++i)
        node = tp_retirement_compose_json_member(json, node, keys[i]);
    return node;
}

int tp_retirement_compose_execution_context(unsigned char const* binding, size_t length,
    char const* raw_measurements_sha256, Arena* arena, char** output, size_t* output_length)
{
    /* _execution_context's keys in sorted order and each value's path. */
    static char const* const names[] = {"admission_sha256", "baseline", "candidate", "execution", "measurement",
        "oracle_sha256", "post_aa_binding_sha256", "pre_sample_plan_sha256", "raw_measurements_sha256",
        "support_root_sha256"};
    static char const* const paths[][4] = {
        {"workflow", "records", "admission", "sha256"}, {"subjects", "baseline"}, {"subjects", "candidate"},
        {"execution"}, {"measurement"}, {"workflow", "records", "oracle", "sha256"},
        {"workflow", "phases", "post_aa_binding", "sha256"}, {"workflow", "phases", "pre_sample_plan", "sha256"},
        {NULL}, {"support", "root_sha256"}};
    static unsigned const depths[] = {4, 2, 2, 1, 1, 4, 4, 4, 0, 2};
    TpComposeJson json = {0};
    size_t raw_length = raw_measurements_sha256 ? strnlen(raw_measurements_sha256, 65) : 0;
    int valid = raw_length == 64 && output && output_length;
    for (size_t i = 0; valid && i < raw_length; ++i)
        valid = (raw_measurements_sha256[i] >= '0' && raw_measurements_sha256[i] <= '9') ||
                (raw_measurements_sha256[i] >= 'a' && raw_measurements_sha256[i] <= 'f');
    valid = valid && tp_retirement_compose_json_parse(binding, length, arena, &json) &&
            json.nodes[0].kind == TP_COMPOSE_JSON_OBJECT;
    TpComposeJsonWriter writer = {.capacity = TP_COMPOSE_JSON_CONTEXT_BYTES, .valid = valid};
    writer.bytes = valid ? (char*)tp_retirement_compose_allocate(arena, TP_COMPOSE_JSON_CONTEXT_BYTES) : NULL;
    writer.valid = valid && writer.bytes;
    tp_compose_json_put(&writer, "{", 1);
    for (unsigned i = 0; writer.valid && i < BUSTER_ARRAY_LENGTH(names); ++i)
    {
        if (i) tp_compose_json_put(&writer, ",", 1);
        tp_compose_json_put_string(&writer, names[i], strlen(names[i]));
        tp_compose_json_put(&writer, ":", 1);
        if (!depths[i]) tp_compose_json_put_string(&writer, raw_measurements_sha256, raw_length);
        else
        {
            unsigned node = tp_compose_json_path(&json, paths[i], depths[i]);
            writer.valid = writer.valid && node != TP_COMPOSE_JSON_NONE;
            tp_compose_json_emit(&writer, &json, node);
        }
    }
    tp_compose_json_put(&writer, "}", 1);
    valid = writer.valid;
    if (output) *output = valid ? writer.bytes : NULL;
    if (output_length) *output_length = valid ? writer.length : 0;
    return valid;
}
#endif
