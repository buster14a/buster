// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Normal C literal values for the semantic reader. Unlike #if evaluation, integer literals retain the LP64 rank
// selected by their radix and suffix. The reader supplies a bounded spelling after translation phases 1 and 2.
// Floating conversion uses the host's matching IEEE and x87 formats; unsupported suffixes are rejected explicitly.
// Character decoding receives the C23 mode because earlier standards forbid UCNs for most basic characters.

#include <errno.h>
#include <stdlib.h>

typedef struct CheckLiteral CheckLiteral;
struct CheckLiteral
{
    TypeId type;
    uint64_t integer;
    long double floating;
    bool real;
    bool imaginary;
    bool valid;
    bool resource;
};

typedef struct CheckStringPart CheckStringPart;
struct CheckStringPart
{
    const uint8_t* bytes;
    uint32_t length;
};

typedef struct CheckStringLiteral CheckStringLiteral;
struct CheckStringLiteral
{
    TypeId element_type;
    const uint8_t* bytes;
    uint64_t byte_count;
    uint64_t unit_count;
    uint32_t failed_part;
    bool valid;
    bool resource;
};

typedef enum CheckStringEncoding
{
    CHECK_STRING_ORDINARY,
    CHECK_STRING_UTF8,
    CHECK_STRING_UTF16,
    CHECK_STRING_UTF32,
    CHECK_STRING_WIDE,
} CheckStringEncoding;

typedef struct CheckStringSpan CheckStringSpan;
struct CheckStringSpan
{
    uint32_t begin;
    uint32_t end;
    CheckStringEncoding encoding;
    bool raw;
    bool valid;
};

static uint32_t check_literal_digit(uint8_t c)
{
    uint32_t value = UINT32_MAX;
    if (c >= '0' && c <= '9')
    {
        value = c - '0';
    }
    else if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
    {
        value = (c | 32u) - 'a' + 10u;
    }
    return value;
}

static CheckLiteral check_literal_integer(const uint8_t* bytes, uint32_t length)
{
    CheckLiteral result = { 0 };
    uint32_t radix = 10;
    uint32_t position = 0;
    if (length >= 2 && bytes[0] == '0')
    {
        const uint8_t next = bytes[1] | 32u;
        radix = next == 'x' ? 16 : next == 'b' ? 2 : 8;
        position = next == 'x' || next == 'b' ? 2 : 0;
    }
    uint64_t value = 0;
    uint32_t digits = 0;
    bool valid = length != 0;
    bool previous_digit = false;
    while (position < length)
    {
        const uint8_t c = bytes[position];
        const uint32_t digit = check_literal_digit(c);
        if (digit < radix)
        {
            valid &= value <= (UINT64_MAX - digit) / radix;
            value = value * radix + digit;
            digits += 1;
            previous_digit = true;
            position += 1;
        }
        else if (c == '\'')
        {
            valid &= previous_digit && position + 1 < length && check_literal_digit(bytes[position + 1]) < radix;
            previous_digit = false;
            position += 1;
        }
        else
        {
            break;
        }
    }
    bool unsign = false;
    uint32_t longs = 0;
    if (position < length && (bytes[position] | 32u) == 'u')
    {
        unsign = true;
        position += 1;
    }
    if (position < length && (bytes[position] | 32u) == 'l')
    {
        const uint8_t first = bytes[position++];
        longs = 1;
        if (position < length && bytes[position] == first)
        {
            longs = 2;
            position += 1;
        }
    }
    if (!unsign && position < length && (bytes[position] | 32u) == 'u')
    {
        unsign = true;
        position += 1;
    }
    valid &= digits != 0 && previous_digit && position == length;
    TypeId type = 0;
    if (valid)
    {
        if (longs == 0 && value <= (unsign ? UINT32_MAX : INT32_MAX))
        {
            type = unsign ? BUSTER_TYPE_UINT : BUSTER_TYPE_INT;
        }
        else if (longs == 0 && radix != 10 && value <= UINT32_MAX)
        {
            type = BUSTER_TYPE_UINT;
        }
        else
        {
            const bool wide_unsigned = unsign || value > INT64_MAX;
            type = longs == 2 ? (wide_unsigned ? BUSTER_TYPE_ULLONG : BUSTER_TYPE_LLONG) :
                               (wide_unsigned ? BUSTER_TYPE_ULONG : BUSTER_TYPE_LONG);
            // An unsuffixed decimal that needs the unsigned extension is unsigned long long, as Clang diagnoses.
            type = !unsign && radix == 10 && value > INT64_MAX ? BUSTER_TYPE_ULLONG : type;
        }
        result = (CheckLiteral)
        {
            .type = type,
            .integer = value,
            .valid = true,
        };
    }
    return result;
}

static CheckLiteral check_literal_number_plain(const uint8_t* bytes, uint32_t length, Arena* scratch)
{
    const bool hex = length > 2 && bytes[0] == '0' && (bytes[1] | 32u) == 'x';
    bool real = false;
    bool exponent = false;
    for (uint32_t i = 0; i < length; i += 1)
    {
        const uint8_t c = bytes[i] | 32u;
        exponent |= c == (hex ? 'p' : 'e');
        real |= bytes[i] == '.' || c == (hex ? 'p' : 'e');
    }
    CheckLiteral result = { 0 };
    if (!real)
    {
        result = check_literal_integer(bytes, length);
    }
    else if (!hex || exponent)
    {
        const uint64_t mark = scratch->length;
        char* spelling = arena_push(scratch, (uint64_t)length + 1, 1);
        if (spelling != 0)
        {
            uint32_t written = 0;
            bool separators = true;
            for (uint32_t i = 0; i < length; i += 1)
            {
                if (bytes[i] == '\'')
                {
                    separators &= i != 0 && i + 1 < length && check_literal_digit(bytes[i - 1]) < (hex ? 16u : 10u) &&
                                  check_literal_digit(bytes[i + 1]) < (hex ? 16u : 10u);
                }
                else
                {
                    spelling[written++] = (char)bytes[i];
                }
            }
            TypeId type = BUSTER_TYPE_DOUBLE;
            if (written != 0 && (spelling[written - 1] == 'f' || spelling[written - 1] == 'F'))
            {
                type = BUSTER_TYPE_FLOAT;
                written -= 1;
            }
            else if (written != 0 && (spelling[written - 1] == 'l' || spelling[written - 1] == 'L'))
            {
                type = BUSTER_TYPE_LONG_DOUBLE;
                written -= 1;
            }
            spelling[written] = 0;
            char* end = spelling;
            errno = 0;
            long double value;
            if (type == BUSTER_TYPE_FLOAT)
            {
                value = strtof(spelling, &end);
            }
            else if (type == BUSTER_TYPE_DOUBLE)
            {
                value = strtod(spelling, &end);
            }
            else
            {
                value = strtold(spelling, &end);
            }
            // Range warnings do not make a literal a syntax error. Preserve the host target's rounded result.
            result = (CheckLiteral)
            {
                .type = type,
                .floating = value,
                .real = true,
                .valid = separators && end != spelling && end == spelling + written,
            };
        }
        else
        {
            result.resource = true;
        }
        arena_pop_to(scratch, mark);
    }
    return result;
}

static CheckLiteral check_literal_number(const uint8_t* bytes, uint32_t length, Arena* scratch)
{
    uint32_t imaginary = UINT32_MAX;
    uint32_t suffix = length;
    while (suffix != 0)
    {
        const uint8_t c = bytes[suffix - 1] | 32u;
        if (c == 'i' || c == 'j')
        {
            imaginary = suffix - 1;
            suffix -= 1;
            break;
        }
        if (c != 'f' && c != 'l' && c != 'u')
        {
            break;
        }
        suffix -= 1;
    }
    CheckLiteral result = { 0 };
    if (imaginary == UINT32_MAX)
    {
        result = check_literal_number_plain(bytes, length, scratch);
    }
    else if (imaginary != 0)
    {
        const uint64_t mark = scratch->length;
        uint8_t* spelling = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, length);
        result.resource = spelling == 0;
        if (spelling != 0)
        {
            memcpy(spelling, bytes, imaginary);
            memcpy(spelling + imaginary, bytes + imaginary + 1, length - imaginary - 1);
            result = check_literal_number_plain(spelling, length - 1, scratch);
            if (result.valid)
            {
                result.type = BUSTER_TYPE_COMPLEX(type_index(result.type));
                result.imaginary = true;
            }
        }
        arena_pop_to(scratch, mark);
    }
    return result;
}

static CheckValue check_literal_value(CheckLiteral literal)
{
    const CheckValue value = {
        .type = literal.type,
        .category = CHECK_RVALUE,
        .flags = literal.imaginary ? 0 : literal.real ? BUSTER_CHECK_FLOAT_LITERAL : literal.type == BUSTER_TYPE_NULLPTR ? 0 : BUSTER_CHECK_ICE,
        .constant_kind = literal.imaginary ? CHECK_CONSTANT_COMPLEX : literal.real ? CHECK_CONSTANT_FLOATING :
                         literal.type == BUSTER_TYPE_NULLPTR ? CHECK_CONSTANT_ADDRESS : CHECK_CONSTANT_INTEGER,
        .integer = literal.imaginary ? (unsigned __int128)literal.integer << 64 : literal.integer,
        .floating = literal.imaginary ? 0 : literal.floating,
        .imaginary = literal.imaginary ? literal.floating : 0,
    };
    return value;
}

static bool check_literal_character_unit(const uint8_t* bytes, uint32_t end, uint32_t* position, uint32_t* value, bool c23)
{
    uint32_t i = *position;
    bool valid = i < end;
    uint32_t c = valid ? bytes[i++] : 0;
    if (c == '\\' && i < end)
    {
        c = bytes[i++];
        const char* escapes = "abefnrtv\\'\"?";
        const uint8_t values[] =
        {
            7,
            8,
            27,
            12,
            10,
            13,
            9,
            11,
            '\\',
            '\'',
            '"',
            '?',
        };
        const char* found = c != 0 ? strchr(escapes, (int)c) : NULL;
        if (found != 0)
        {
            c = values[found - escapes];
        }
        else if (c >= '0' && c <= '7')
        {
            uint32_t n = 1;
            c -= '0';
            while (n < 3 && i < end && bytes[i] >= '0' && bytes[i] <= '7')
            {
                c = c * 8 + bytes[i++] - '0';
                n += 1;
            }
        }
        else if (c == 'x' || c == 'u' || c == 'U')
        {
            const uint32_t maximum = c == 'u' ? 4 : c == 'U' ? 8 : UINT32_MAX;
            uint32_t n = 0;
            c = 0;
            while (n < maximum && i < end && check_literal_digit(bytes[i]) < 16)
            {
                const uint32_t digit = check_literal_digit(bytes[i++]);
                valid &= c <= (UINT32_MAX - digit) / 16;
                c = c * 16 + digit;
                n += 1;
            }
            valid &= n != 0 && (maximum == UINT32_MAX || (n == maximum && c <= 0x10FFFFu && (c < 0xD800 || c > 0xDFFF)));
            valid &= maximum == UINT32_MAX || c23 || c >= 0xA0 || c == '$' || c == '@' || c == '`';
        }
        else
        {
            valid = false;
        }
    }
    else if (c == '\\')
    {
        valid = false;
    }
    else if (c == 0 || c == '\'' || c == '\n' || c == '\r')
    {
        valid = false;
    }
    else if (c >= 0x80)
    {
        const uint32_t continuation = c >= 0xF0 && c <= 0xF4 ? 3 : c >= 0xE0 && c <= 0xEF ? 2 : c >= 0xC2 && c <= 0xDF ? 1 : 0;
        const uint32_t minimum = continuation == 3 ? 0x10000 : continuation == 2 ? 0x800 : 0x80;
        valid &= continuation != 0 && continuation <= end - i;
        c &= continuation == 3 ? 7 : continuation == 2 ? 15 : 31;
        for (uint32_t n = 0; valid && n < continuation; n += 1)
        {
            valid &= (bytes[i] & 0xC0u) == 0x80u;
            c = (c << 6) | (bytes[i++] & 63u);
        }
        valid &= c >= minimum && c <= 0x10FFFFu && (c < 0xD800 || c > 0xDFFF);
    }
    *position = i;
    *value = c;
    return valid;
}

static CheckLiteral check_literal_character(const uint8_t* bytes, uint32_t length, bool c23)
{
    CheckLiteral result = { 0 };
    uint32_t start = 0;
    TypeId type = BUSTER_TYPE_INT;
    uint32_t maximum = 255;
    bool ordinary = true;
    bool utf8 = false;
    if (length != 0 && (bytes[0] == 'L' || bytes[0] == 'u' || bytes[0] == 'U'))
    {
        ordinary = false;
        start = 1;
        type = bytes[0] == 'u' ? BUSTER_TYPE_USHORT : bytes[0] == 'U' ? BUSTER_TYPE_UINT : BUSTER_TYPE_INT;
        maximum = bytes[0] == 'u' ? UINT16_MAX : UINT32_MAX;
        if (bytes[0] == 'u' && length > 1 && bytes[1] == '8')
        {
            start = 2;
            type = BUSTER_TYPE_UCHAR;
            maximum = 255;
            utf8 = true;
        }
    }
    bool valid = start < length && bytes[start] == '\'' && length > start + 2 && bytes[length - 1] == '\'';
    uint32_t position = start + 1;
    uint32_t count = 0;
    uint32_t combined = 0;
    while (valid && position < length - 1)
    {
        const uint32_t begin = position;
        uint32_t value = 0;
        valid = check_literal_character_unit(bytes, length - 1, &position, &value, c23) && value <= maximum;
        const bool numeric_escape = bytes[begin] == '\\' && begin + 1 < length && (bytes[begin + 1] == 'x' || (bytes[begin + 1] >= '0' && bytes[begin + 1] <= '7'));
        valid &= !(ordinary || utf8) || value <= 127 || numeric_escape;
        combined = ordinary ? (combined << 8) | value : value;
        count += 1;
        valid &= ordinary ? count <= 4 : count == 1;
    }
    if (valid && count != 0)
    {
        const uint64_t bits = ordinary && count == 1 ? (uint64_t)(int64_t)(int8_t)combined : (uint64_t)combined;
        result = (CheckLiteral)
        {
            .type = type,
            .integer = bits,
            .valid = true,
        };
    }
    return result;
}

static CheckStringSpan check_literal_string_span(const CheckStringPart* part)
{
    CheckStringSpan result = { 0 };
    const uint8_t* bytes = part->bytes;
    const uint32_t length = part->length;
    uint32_t quote = 0;
    bool valid = bytes != NULL && length >= 2;
    if (valid && (bytes[0] == 'L' || bytes[0] == 'u' || bytes[0] == 'U'))
    {
        result.encoding = bytes[0] == 'L' ? CHECK_STRING_WIDE : bytes[0] == 'u' ? CHECK_STRING_UTF16 : CHECK_STRING_UTF32;
        quote = 1;
        if (bytes[0] == 'u' && bytes[1] == '8')
        {
            result.encoding = CHECK_STRING_UTF8;
            quote = 2;
        }
    }
    if (valid && quote < length && bytes[quote] == 'R')
    {
        result.raw = true;
        quote += 1;
    }
    valid = valid && quote < length && bytes[quote] == '"' && length - quote >= 2 && bytes[length - 1] == '"';
    if (valid)
    {
        result.begin = quote + 1;
        result.end = length - 1;
        if (result.raw)
        {
            const uint32_t delimiter = result.begin;
            uint32_t opening = delimiter;
            while (valid && opening < length && bytes[opening] != '(')
            {
                const uint8_t c = bytes[opening];
                valid = opening - delimiter < 16 && c > 32 && c < 127 && c != ')' && c != '\\';
                opening += 1;
            }
            const uint32_t delimiter_length = opening - delimiter;
            valid = valid && opening < length && length - opening >= delimiter_length + 3;
            if (valid)
            {
                result.begin = opening + 1;
                result.end = length - delimiter_length - 2;
                valid = bytes[result.end] == ')' && memcmp(bytes + delimiter, bytes + result.end + 1, delimiter_length) == 0;
            }
        }
    }
    result.valid = valid;
    return result;
}

static bool check_literal_string_unit(uint32_t value, bool numeric, CheckStringEncoding encoding, uint8_t* output, uint64_t* length)
{
    const uint32_t width = encoding == CHECK_STRING_UTF16 ? 2 : encoding == CHECK_STRING_UTF32 || encoding == CHECK_STRING_WIDE ? 4 : 1;
    uint32_t units[4] = { value, 0, 0, 0 };
    uint32_t count = 1;
    bool valid = true;
    if (numeric)
    {
        valid = value <= (width == 1 ? UINT8_MAX : width == 2 ? UINT16_MAX : UINT32_MAX);
    }
    else if (width == 1 && value >= 0x80)
    {
        count = value < 0x800 ? 2 : value < 0x10000 ? 3 : 4;
        units[0] = (count == 2 ? 0xC0 : count == 3 ? 0xE0 : 0xF0) | (value >> (6 * (count - 1)));
        for (uint32_t i = 1; i < count; i += 1)
        {
            units[i] = 0x80 | ((value >> (6 * (count - i - 1))) & 63);
        }
    }
    else if (width == 2 && value >= 0x10000)
    {
        count = 2;
        units[0] = 0xD800 | ((value - 0x10000) >> 10);
        units[1] = 0xDC00 | ((value - 0x10000) & 1023);
    }
    const uint32_t bytes = count * width;
    valid = valid && *length <= UINT64_MAX - bytes;
    if (valid)
    {
        if (output != NULL)
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                for (uint32_t j = 0; j < width; j += 1)
                {
                    output[*length + i * width + j] = (uint8_t)(units[i] >> (j * 8));
                }
            }
        }
        *length += bytes;
    }
    return valid;
}

static bool check_literal_string_part(const CheckStringPart* part, CheckStringEncoding encoding, bool c23, uint8_t* output, uint64_t* length)
{
    const CheckStringSpan span = check_literal_string_span(part);
    bool valid = span.valid;
    uint32_t position = span.begin;
    while (valid && position < span.end)
    {
        const uint32_t begin = position;
        const uint8_t first = part->bytes[position];
        uint32_t value = first;
        bool numeric = false;
        if (span.raw)
        {
            const uint32_t delimiter_length = part->length - span.end - 2;
            const bool closes = first == ')' && span.end - position >= delimiter_length + 2 &&
                                memcmp(part->bytes + position + 1, part->bytes + span.end + 1, delimiter_length) == 0 && part->bytes[position + delimiter_length + 1] == '"';
            valid = !closes && first != 0;
            if (valid && first >= 0x80)
            {
                valid = check_literal_character_unit(part->bytes, span.end, &position, &value, c23);
            }
            else
            {
                position += 1;
            }
        }
        else if (first == '\'')
        {
            position += 1;
        }
        else
        {
            valid = first != '"' && check_literal_character_unit(part->bytes, span.end, &position, &value, c23);
            numeric = first == '\\' && begin + 1 < span.end && (part->bytes[begin + 1] == 'x' || (part->bytes[begin + 1] >= '0' && part->bytes[begin + 1] <= '7'));
        }
        if (valid)
        {
            valid = check_literal_string_unit(value, numeric, encoding, output, length);
        }
    }
    return valid;
}

// Decode one adjacent literal group after preprocessing. Input slices are immutable and unpadded; the result's
// contiguous little-endian target bytes include exactly one terminator. A validation/measurement pass precedes
// allocation, so invalid input or insufficient output capacity leaves the caller's arena unchanged. The caller
// interns the completed array type and owns first-use string identities; this decoder does no interning. Input
// slices may reside in the arena's existing used prefix, but must not overlap the storage appended by this call.
static CheckStringLiteral check_literal_strings(const CheckStringPart* parts, uint32_t count, bool c23, Arena* output)
{
    CheckStringLiteral result = { .failed_part = UINT32_MAX };
    CheckStringEncoding encoding = CHECK_STRING_ORDINARY;
    bool valid = parts != NULL && count != 0;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const CheckStringSpan span = check_literal_string_span(&parts[i]);
        valid = span.valid && (span.encoding == CHECK_STRING_ORDINARY || encoding == CHECK_STRING_ORDINARY || span.encoding == encoding);
        if (span.encoding != CHECK_STRING_ORDINARY)
        {
            encoding = span.encoding;
        }
        if (!valid)
        {
            result.failed_part = i;
        }
    }
    uint64_t length = 0;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        valid = check_literal_string_part(&parts[i], encoding, c23, NULL, &length);
        if (!valid)
        {
            result.failed_part = i;
        }
    }
    valid = valid && check_literal_string_unit(0, true, encoding, NULL, &length);
    if (valid)
    {
        const uint32_t width = encoding == CHECK_STRING_UTF16 ? 2 : encoding == CHECK_STRING_UTF32 || encoding == CHECK_STRING_WIDE ? 4 : 1;
        const uint64_t mark = output != NULL ? output->length : 0;
        uint8_t* bytes = output != NULL ? arena_push(output, length, width) : NULL;
        result.resource = bytes == NULL;
        if (bytes != NULL)
        {
            uint64_t written = 0;
            for (uint32_t i = 0; valid && i < count; i += 1)
            {
                valid = check_literal_string_part(&parts[i], encoding, c23, bytes, &written);
            }
            valid = valid && check_literal_string_unit(0, true, encoding, bytes, &written) && written == length;
            if (valid)
            {
                result.element_type = encoding == CHECK_STRING_UTF16 ? BUSTER_TYPE_USHORT : encoding == CHECK_STRING_UTF32 ? BUSTER_TYPE_UINT : encoding == CHECK_STRING_WIDE ? BUSTER_TYPE_INT : encoding == CHECK_STRING_UTF8 && c23 ? BUSTER_TYPE_UCHAR : BUSTER_TYPE_CHAR;
                result.bytes = bytes;
                result.byte_count = length;
                result.unit_count = length / width;
                result.valid = true;
            }
            else
            {
                arena_pop_to(output, mark);
            }
        }
    }
    return result;
}
