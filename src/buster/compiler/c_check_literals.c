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

static CheckLiteral check_literal_number(const uint8_t* bytes, uint32_t length, Arena* scratch)
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
        arena_pop_to(scratch, mark);
    }
    return result;
}

static bool check_literal_character_unit(const uint8_t* bytes, uint32_t end, uint32_t* position, uint32_t* value, bool c23)
{
    uint32_t i = *position;
    bool valid = i < end;
    uint32_t c = valid ? bytes[i++] : 0;
    if (c == '\\' && i < end)
    {
        c = bytes[i++];
        const char* escapes = "abfnrtv\\'\"?";
        const uint8_t values[] =
        {
            7,
            8,
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
