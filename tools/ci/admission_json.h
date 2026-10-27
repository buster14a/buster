// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Small, bounded JSON reader/writer for GitHub metadata. No external implementation is incorporated.
// Tokens are preorder rows; next skips a complete value. Nesting uses an explicit stack.

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUSTER_CI_BYTES (16u * 1024u * 1024u)
#define BUSTER_CI_JSON_DEPTH 64

typedef struct Buffer Buffer;
struct Buffer
{
    char* data;
    size_t length;
    size_t capacity;
};

static void ci_die(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    fputs("9700X admission: ", stderr);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    va_end(args);
    exit(2);
}

static void buffer_reserve(Buffer* buffer, size_t extra)
{
    if (extra > BUSTER_CI_BYTES || buffer->length > BUSTER_CI_BYTES - extra)
    {
        ci_die("metadata exceeds the %u-byte limit", BUSTER_CI_BYTES);
    }
    size_t required = buffer->length + extra + 1;
    if (required > buffer->capacity)
    {
        size_t capacity = required * 2;
        char* data = realloc(buffer->data, capacity);
        if (data == NULL)
        {
            ci_die("out of memory");
        }
        buffer->data = data;
        buffer->capacity = capacity;
    }
}

static void buffer_add(Buffer* buffer, const char* data, size_t length)
{
    buffer_reserve(buffer, length);
    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    buffer->data[buffer->length] = 0;
}

static void buffer_printf(Buffer* buffer, const char* format, ...)
{
    va_list args;
    va_list copy;
    va_start(args, format);
    va_copy(copy, args);
    int count = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (count < 0)
    {
        ci_die("formatting failed");
    }
    buffer_reserve(buffer, (size_t)count);
    vsnprintf(buffer->data + buffer->length, buffer->capacity - buffer->length, format, args);
    buffer->length += (size_t)count;
    va_end(args);
}

static void json_quote(Buffer* buffer, const char* string)
{
    buffer_add(buffer, "\"", 1);
    for (const unsigned char* at = (const unsigned char*)string; *at != 0; ++at)
    {
        if (*at == '"' || *at == '\\')
        {
            buffer_printf(buffer, "\\%c", *at);
        }
        else if (*at < 32)
        {
            buffer_printf(buffer, "\\u%04x", *at);
        }
        else
        {
            buffer_add(buffer, (const char*)at, 1);
        }
    }
    buffer_add(buffer, "\"", 1);
}

typedef struct JsonRow JsonRow;
struct JsonRow
{
    char type;
    char* text;
    size_t next;
};

typedef struct Json Json;
struct Json
{
    char* data;
    JsonRow* rows;
    size_t count;
};

static unsigned json_hex(char value)
{
    unsigned result = 16;
    if (value >= '0' && value <= '9')
    {
        result = (unsigned)(value - '0');
    }
    else if (value >= 'a' && value <= 'f')
    {
        result = (unsigned)(value - 'a') + 10;
    }
    else if (value >= 'A' && value <= 'F')
    {
        result = (unsigned)(value - 'A') + 10;
    }
    return result;
}

static unsigned json_codepoint(char** source)
{
    unsigned value = 0;
    for (unsigned digit = 0; digit < 4; ++digit)
    {
        unsigned hex = json_hex(**source);
        if (hex == 16)
        {
            ci_die("invalid JSON Unicode escape");
        }
        value = value * 16 + hex;
        ++*source;
    }
    return value;
}

static char* json_string(char** source)
{
    char* result = ++*source;
    char* out = result;
    bool ended = false;
    while (**source != 0 && !ended)
    {
        unsigned char value = (unsigned char)*(*source)++;
        if (value == '"')
        {
            ended = true;
        }
        else if (value < 32)
        {
            ci_die("control byte in JSON string");
        }
        else if (value == '\\')
        {
            char escaped = *(*source)++;
            if (escaped == 'u')
            {
                unsigned code = json_codepoint(source);
                if (code >= 0xd800 && code <= 0xdbff)
                {
                    if ((*source)[0] != '\\' || (*source)[1] != 'u')
                    {
                        ci_die("unpaired JSON surrogate");
                    }
                    *source += 2;
                    unsigned low = json_codepoint(source);
                    if (low < 0xdc00 || low > 0xdfff)
                    {
                        ci_die("unpaired JSON surrogate");
                    }
                    code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                }
                if (code >= 0xdc00 && code <= 0xdfff)
                {
                    ci_die("unpaired surrogate in JSON string");
                }
                if (code == 0)
                {
                    // NUL in a conversation body must neither terminate a command nor wedge all reconciliation.
                    code = 0xfffd;
                }
                if (code < 0x80)
                {
                    *out++ = (char)code;
                }
                else if (code < 0x800)
                {
                    *out++ = (char)(0xc0 | (code >> 6));
                    *out++ = (char)(0x80 | (code & 63));
                }
                else if (code < 0x10000)
                {
                    *out++ = (char)(0xe0 | (code >> 12));
                    *out++ = (char)(0x80 | ((code >> 6) & 63));
                    *out++ = (char)(0x80 | (code & 63));
                }
                else
                {
                    *out++ = (char)(0xf0 | (code >> 18));
                    *out++ = (char)(0x80 | ((code >> 12) & 63));
                    *out++ = (char)(0x80 | ((code >> 6) & 63));
                    *out++ = (char)(0x80 | (code & 63));
                }
            }
            else
            {
                const char* escapes = "\"\\/bfnrt";
                const char* values = "\"\\/\b\f\n\r\t";
                const char* found = escaped != 0 ? strchr(escapes, escaped) : NULL;
                if (found == NULL)
                {
                    ci_die("invalid JSON escape");
                }
                *out++ = values[found - escapes];
            }
        }
        else
        {
            *out++ = (char)value;
        }
    }
    if (!ended)
    {
        ci_die("unterminated JSON string");
    }
    *out = 0;
    return result;
}

static Json json_parse(const char* text)
{
    size_t length = strlen(text);
    if (length == 0 || length > BUSTER_CI_BYTES)
    {
        ci_die("empty or oversized JSON response");
    }
    Json json = {0};
    json.data = calloc(length + 8, 1);
    json.rows = calloc(length + 1, sizeof(*json.rows));
    if (json.data == NULL || json.rows == NULL)
    {
        ci_die("out of memory");
    }
    memcpy(json.data, text, length);
    size_t stack[BUSTER_CI_JSON_DEPTH];
    unsigned states[BUSTER_CI_JSON_DEPTH];
    size_t depth = 0;
    char* at = json.data;
    bool root_done = false;
    while (*at != 0)
    {
        while (isspace((unsigned char)*at))
        {
            ++at;
        }
        if (*at == 0)
        {
            break;
        }
        bool key = false;
        if (depth != 0)
        {
            size_t parent = stack[depth - 1];
            unsigned state = states[depth - 1];
            char closing = json.rows[parent].type == '{' ? '}' : ']';
            if (*at == closing && (state == 0 || state == 3))
            {
                json.rows[parent].next = json.count;
                --depth;
                ++at;
                continue;
            }
            if (state == 3)
            {
                if (*at++ != ',')
                {
                    ci_die("missing JSON comma");
                }
                states[depth - 1] = 4;
                continue;
            }
            if (state == 1)
            {
                if (*at++ != ':')
                {
                    ci_die("missing JSON colon");
                }
                states[depth - 1] = 2;
                continue;
            }
            key = json.rows[parent].type == '{' && (state == 0 || state == 4);
            if (key && *at != '"')
            {
                ci_die("JSON object key is not a string");
            }
            states[depth - 1] = key ? 1 : 3;
        }
        else if (root_done)
        {
            ci_die("trailing JSON data");
        }
        else
        {
            root_done = true;
        }
        size_t index = json.count++;
        JsonRow* row = &json.rows[index];
        row->type = *at;
        row->next = index + 1;
        if (*at == '{' || *at == '[')
        {
            if (key || depth == BUSTER_CI_JSON_DEPTH)
            {
                ci_die("JSON nesting limit exceeded");
            }
            stack[depth] = index;
            states[depth++] = 0;
            ++at;
        }
        else if (*at == '"')
        {
            row->text = json_string(&at);
        }
        else
        {
            char* begin = at;
            while (*at != 0 && !isspace((unsigned char)*at) && *at != ',' && *at != ']' && *at != '}')
            {
                ++at;
            }
            size_t bytes = (size_t)(at - begin);
            row->text = strndup(begin, bytes);
            if (row->text == NULL)
            {
                ci_die("out of memory");
            }
            bool literal = strcmp(row->text, "null") == 0 || strcmp(row->text, "true") == 0 || strcmp(row->text, "false") == 0;
            bool number = bytes != 0;
            for (size_t i = 0; i < bytes; ++i)
            {
                number = number && ((begin[i] >= '0' && begin[i] <= '9') || (i == 0 && begin[i] == '-'));
            }
            if (!literal && !number)
            {
                ci_die("invalid JSON primitive");
            }
        }
    }
    if (depth != 0 || !root_done)
    {
        ci_die("incomplete JSON response");
    }
    return json;
}

static void json_free(Json* json)
{
    for (size_t i = 0; i < json->count; ++i)
    {
        char type = json->rows[i].type;
        if (type != '{' && type != '[' && type != '"')
        {
            free(json->rows[i].text);
        }
    }
    free(json->rows);
    free(json->data);
    memset(json, 0, sizeof(*json));
}

static size_t json_get(const Json* json, size_t object, const char* name)
{
    size_t result = SIZE_MAX;
    if (object < json->count && json->rows[object].type == '{')
    {
        size_t end = json->rows[object].next;
        for (size_t at = object + 1; at < end; at = json->rows[at + 1].next)
        {
            if (strcmp(json->rows[at].text, name) == 0)
            {
                if (result != SIZE_MAX)
                {
                    ci_die("duplicate JSON field: %s", name);
                }
                result = at + 1;
            }
        }
    }
    return result;
}

static const char* json_text(const Json* json, size_t row)
{
    const char* result = "";
    if (row < json->count && json->rows[row].text != NULL && json->rows[row].type != 'n')
    {
        result = json->rows[row].text;
    }
    return result;
}

static const char* json_field(const Json* json, size_t object, const char* name)
{
    return json_text(json, json_get(json, object, name));
}

static uint64_t ci_number(const char* text)
{
    uint64_t value = 0;
    if (*text == 0)
    {
        ci_die("missing unsigned integer");
    }
    for (const char* at = text; *at != 0; ++at)
    {
        if (*at < '0' || *at > '9' || value > (UINT64_MAX - (unsigned)(*at - '0')) / 10)
        {
            ci_die("invalid unsigned integer");
        }
        value = value * 10 + (unsigned)(*at - '0');
    }
    return value;
}

static uint64_t json_number(const Json* json, size_t object, const char* name)
{
    const char* text = json_field(json, object, name);
    return *text != 0 ? ci_number(text) : 0;
}

static void ci_copy(char* destination, size_t capacity, const char* source)
{
    size_t length = strlen(source);
    if (length >= capacity)
    {
        ci_die("metadata string exceeds its field limit");
    }
    memcpy(destination, source, length + 1);
}
