// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Normal C literal values for the semantic reader. Unlike #if evaluation, integer literals retain the LP64 rank
// selected by their radix and suffix. The reader supplies a bounded spelling after translation phases 1 and 2.
// Floating conversion uses the host's matching IEEE and x87 formats; unsupported suffixes are rejected explicitly.
// Character decoding receives the C23 mode because earlier standards forbid UCNs for most basic characters.

#include <errno.h>
#include <fenv.h>
#include "host_math.h"
#include <math.h>
#include <stdlib.h>

typedef enum CheckLiteralRange
{
    CHECK_LITERAL_RANGE_NONE,
    CHECK_LITERAL_RANGE_OVERFLOW,
    CHECK_LITERAL_RANGE_UNDERFLOW,
} CheckLiteralRange;

typedef struct CheckLiteral CheckLiteral;
struct CheckLiteral
{
    TypeId type;
    // A bit-precise constant's width (wb or uwb, #768), 0 for every other literal: its type is _BitInt(bit_width), or
    // unsigned _BitInt(bit_width) with bit_unsigned, which the reader makes in its table (check_input_literal), as the
    // literal leaves `type` 0. A width past BUSTER_CHECK_LITERAL_BIT_WIDTH, the most the checker's values hold, is one
    // the reader refuses by name; UINT32_MAX is a value past 128 bits.
    uint32_t bit_width;
    uint64_t integer;
    // Integer storage comes first so omitted payloads in aggregate initializers are zeroed without x87.
    // The floating view preserves the existing format, alignment and all arithmetic consumers.
    union
    {
        uint64_t floating_storage[2];
        long double floating;
    };
    bool real;
    bool imaginary;
    bool valid;
    bool resource;
    bool bit_unsigned;
    // CHECK_LITERAL_RANGE_*: whether the spelling's magnitude is past its type's largest or below its smallest, in
    // what was the literal's padding before `high`, so that every literal keeps its 48 bytes.
    uint8_t range;
    // A character constant's characters (#1447, at most 255): an ordinary one of more than one is clang's multi-character
    // constant (check_input_multichar), in what was the literal's padding.
    uint8_t characters;
    // A bit-precise constant's bits past 64, 0 for every other literal: in what was the literal's padding, so that it
    // keeps its 48 bytes, which every literal is zeroed and copied in.
    uint64_t high;
};

// The widest bit-precise constant the checker takes: its values (CheckValue.integer) hold 128 bits.
#define BUSTER_CHECK_LITERAL_BIT_WIDTH 128u

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
    // Whether a part is u8-prefixed, which makes the whole a UTF-8 literal and no ordinary one (clang's isOrdinary).
    bool utf8;
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

// A bit-precise constant (#768: C23's wb or WB, alone or with u or U before or after it) into `result`, which the caller
// zeroed: its value in 128 bits, the most the checker's values hold, and its width (CheckLiteral.bit_width: the smallest
// above 1 that holds the value and a sign bit, or with u the smallest above 0 that holds it, or UINT32_MAX past 128
// bits), or nothing for a spelling that isn't one. check_literal_integer reads every other integer constant and leaves
// to this the spellings it can't, so that an ordinary constant's path is as it was; this reads its own digits again,
// out of line and cold, as few constants are bit-precise.
static __attribute__((noinline, cold)) void check_literal_bit_precise(const uint8_t* bytes, uint32_t length, CheckLiteral* result)
{
    uint32_t radix = 10;
    uint32_t position = 0;
    if (length >= 2 && bytes[0] == '0')
    {
        // C2y's `0o` prefix, which clang takes in every mode, before an octal digit.
        const uint8_t next = bytes[1] | 32u;
        const bool octal = next == 'o' && length > 2 && bytes[2] >= '0' && bytes[2] <= '7';
        radix = next == 'x' ? 16 : next == 'b' ? 2 : 8;
        position = next == 'x' || next == 'b' || octal ? 2 : 0;
    }
    unsigned __int128 value = 0;
    uint32_t digits = 0;
    bool valid = length != 0;
    bool fits = true;
    bool previous_digit = false;
    while (position < length)
    {
        const uint8_t c = bytes[position];
        const uint32_t digit = check_literal_digit(c);
        if (digit < radix)
        {
            fits &= value <= (~(unsigned __int128)0 - digit) / radix;
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
    if (position < length && (bytes[position] | 32u) == 'u')
    {
        unsign = true;
        position += 1;
    }
    const bool suffix = position + 1 < length && ((bytes[position] == 'w' && bytes[position + 1] == 'b') || (bytes[position] == 'W' && bytes[position + 1] == 'B'));
    position += suffix ? 2 : 0;
    if (!unsign && position < length && (bytes[position] | 32u) == 'u')
    {
        unsign = true;
        position += 1;
    }
    if (valid && suffix && digits != 0 && previous_digit && position == length)
    {
        const uint64_t high = (uint64_t)(value >> 64);
        const uint32_t bits = high != 0 ? 128u - (uint32_t)__builtin_clzll(high) : (uint64_t)value != 0 ? 64u - (uint32_t)__builtin_clzll((uint64_t)value) : 0;
        const uint32_t width = unsign ? bits : bits + 1;
        result->bit_width = !fits ? UINT32_MAX : width > (unsign ? 1u : 2u) ? width : unsign ? 1u : 2u;
        result->bit_unsigned = unsign;
        result->integer = (uint64_t)value;
        result->high = high;
        result->valid = true;
    }
}

// An integer constant's type and value, into `result`, which the caller zeroed (a literal that isn't valid leaves it so).
// The literals' functions write into their caller's result rather than return a CheckLiteral: clang built each returned
// one in a temporary and copied it up with loads wider than the stores that wrote it, a failed store-to-load forward on
// every integer literal (#510). A spelling this can't read may be a bit-precise constant's (check_literal_bit_precise).
// Always inlined (#880), as profile-guided builds do; hot caller: check_input_run, once per integer literal.
static inline __attribute__((always_inline)) void check_literal_integer(const uint8_t* bytes, uint32_t length, CheckLiteral* result)
{
    uint32_t radix = 10;
    uint32_t position = 0;
    if (length >= 2 && bytes[0] == '0')
    {
        // C2y's `0o` prefix, which clang takes in every mode, before an octal digit.
        const uint8_t next = bytes[1] | 32u;
        const bool octal = next == 'o' && length > 2 && bytes[2] >= '0' && bytes[2] <= '7';
        radix = next == 'x' ? 16 : next == 'b' ? 2 : 8;
        position = next == 'x' || next == 'b' || octal ? 2 : 0;
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
        result->type = type;
        result->integer = value;
        result->valid = true;
    }
    else
    {
        check_literal_bit_precise(bytes, length, result);
    }
}

// A _Float16 constant's value (the f16 suffix), rounded to nearest with ties to even as clang rounds the spelling.
// strtod's correctly rounded double decides, but where it lands on a midpoint between two _Float16 values it may have
// rounded the spelling onto it: strtod rounded downward and upward then say on which side the spelling lies. Every
// floating operation here is exact, so the rounding mode changes only strtod's results.
static long double check_literal_half(const char* spelling, char** end)
{
    const double nearest = strtod(spelling, end);
    double result = nearest;
    if (isfinite(nearest) && nearest != 0)
    {
        // Steps of _Float16's precision at this magnitude: 2^-10 of its binade, and 2^-24 below the normals.
        const int binade = host_ilogb(nearest) > -14 ? host_ilogb(nearest) : -14;
        const double scaled = host_ldexp(nearest, 10 - binade);
        const uint64_t steps = (uint64_t)scaled;
        const double fraction = scaled - (double)steps;
        bool up = fraction > 0.5 || (fraction == 0.5 && (steps & 1) != 0);
        if (fraction == 0.5)
        {
            const int mode = host_fegetround();
            host_fesetround(FE_DOWNWARD);
            const double below = strtod(spelling, 0);
            host_fesetround(FE_UPWARD);
            const double above = strtod(spelling, 0);
            host_fesetround(mode);
            up = below != nearest ? false : above != nearest ? true : up;
        }
        result = host_ldexp((double)(steps + up), binade - 10);
        result = result > 65504.0 ? INFINITY : result;
    }
    return result;
}

#include "c_check_literal_quad.c"
// The escapes' diagnostics a refused literal's extended decoding needs (#1447), shared with the driver and the
// preprocessor.
#include "c_check_literal_diagnostic.c"

// A number without an imaginary suffix, into `result`, which the caller zeroed.
static void check_literal_number_plain(const uint8_t* bytes, uint32_t length, Arena* scratch, CheckLiteral* result)
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
    if (!real)
    {
        check_literal_integer(bytes, length, result);
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
            if (written >= 3 && (spelling[written - 3] == 'f' || spelling[written - 3] == 'F') && spelling[written - 2] == '1' && spelling[written - 1] == '6')
            {
                // f16, _Float16's suffix in every C mode, as clang has it on x86-64.
                type = BUSTER_TYPE_FLOAT16;
                written -= 3;
            }
            else if (written != 0 && (spelling[written - 1] == 'f' || spelling[written - 1] == 'F'))
            {
                type = BUSTER_TYPE_FLOAT;
                written -= 1;
            }
            else if (written != 0 && (spelling[written - 1] == 'l' || spelling[written - 1] == 'L'))
            {
                type = BUSTER_TYPE_LONG_DOUBLE;
                written -= 1;
            }
            else if (written != 0 && (spelling[written - 1] == 'q' || spelling[written - 1] == 'Q'))
            {
                type = BUSTER_TYPE_FLOAT128;
                written -= 1;
            }
            spelling[written] = 0;
            char* end = spelling;
            errno = 0;
            long double value = 0;
            bool represented = true;
            if (type == BUSTER_TYPE_FLOAT16)
            {
                value = check_literal_half(spelling, &end);
            }
            else if (type == BUSTER_TYPE_FLOAT)
            {
                value = strtof(spelling, &end);
            }
            else if (type == BUSTER_TYPE_DOUBLE)
            {
                value = strtod(spelling, &end);
            }
            else if (type == BUSTER_TYPE_FLOAT128)
            {
                // Its bits in `integer` and `high`, its mirror the value (c_check_quad.c), as check_literal_value reads
                // them.
                unsigned __int128 bits = 0;
                represented = check_literal_quad(spelling, &end, &bits);
                value = check_quad_mirror(bits);
                result->integer = (uint64_t)bits;
                result->high = (uint64_t)(bits >> 64);
            }
            else
            {
                value = strtold(spelling, &end);
            }
            // Range warnings do not make a literal a syntax error. Preserve the host target's rounded result.
            result->type = type;
            result->floating = value;
            result->real = true;
            result->valid = represented && separators && end != spelling && end == spelling + written;
            if (result->valid && isinf(value))
            {
                result->range = CHECK_LITERAL_RANGE_OVERFLOW;
            }
            else if (result->valid && value == 0)
            {
                // Only a nonzero spelling rounded all the way to zero is too small. Representable subnormals
                // are quiet, as are exact zero spellings regardless of exponent. Ignore exponent digits.
                bool nonzero = false;
                for (uint32_t i = hex ? 2 : 0; i < written && (spelling[i] | 32u) != (hex ? 'p' : 'e'); i += 1)
                {
                    const uint32_t digit = check_literal_digit((uint8_t)spelling[i]);
                    nonzero |= digit != 0 && digit < (hex ? 16u : 10u);
                }
                result->range = nonzero ? CHECK_LITERAL_RANGE_UNDERFLOW : CHECK_LITERAL_RANGE_NONE;
            }
        }
        else
        {
            result->resource = true;
        }
        arena_pop_to(scratch, mark);
    }
}

// A number's type and value, into `result`, which the caller zeroed.
static void check_literal_number(const uint8_t* bytes, uint32_t length, Arena* scratch, CheckLiteral* result)
{
    uint32_t imaginary = UINT32_MAX;
    uint32_t suffix = length;
    while (suffix != 0)
    {
        const uint8_t c = bytes[suffix - 1] | 32u;
        if (suffix >= 3 && (bytes[suffix - 3] | 32u) == 'f' && bytes[suffix - 2] == '1' && bytes[suffix - 1] == '6')
        {
            // f16 after the imaginary suffix (1.5if16), which clang takes as it takes 1.5f16i.
            suffix -= 3;
            continue;
        }
        if (c == 'i' || c == 'j')
        {
            imaginary = suffix - 1;
            suffix -= 1;
            break;
        }
        if (c != 'f' && c != 'l' && c != 'u' && c != 'q')
        {
            break;
        }
        suffix -= 1;
    }
    if (imaginary == UINT32_MAX)
    {
        check_literal_number_plain(bytes, length, scratch, result);
    }
    else if (imaginary != 0)
    {
        const uint64_t mark = scratch->length;
        uint8_t* spelling = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, length);
        result->resource = spelling == 0;
        if (spelling != 0)
        {
            memcpy(spelling, bytes, imaginary);
            memcpy(spelling + imaginary, bytes + imaginary + 1, length - imaginary - 1);
            check_literal_number_plain(spelling, length - 1, scratch, result);
            // A bit-precise constant has no imaginary form: clang refuses 5wbi's suffix.
            result->valid &= result->bit_width == 0;
            if (result->valid && result->type == BUSTER_TYPE_FLOAT128)
            {
                // A _Complex __float128 constant holds its components as long doubles (#1250): an imaginary q/Q literal
                // is taken where its long double mirror is its value exactly, and keeps no bits.
                const unsigned __int128 bits = (unsigned __int128)result->high << 64 | result->integer;
                result->valid = check_quad_widen(result->floating, false) == bits;
                result->integer = 0;
                result->high = 0;
            }
            if (result->valid)
            {
                result->type = BUSTER_TYPE_COMPLEX(type_index(result->type));
                result->imaginary = true;
            }
        }
        arena_pop_to(scratch, mark);
    }
}

static CheckValue check_literal_value(CheckLiteral literal)
{
    // Only the x87 representation's ten meaningful bytes are read. Its six padding bytes need not be
    // initialized after a floating assignment. Integer copies also preserve signed zero and NaN payloads
    // without asking x87 to load/store (or evaluate) a value on this otherwise integer-only handoff.
    uint64_t significand;
    uint16_t sign_exponent;
    memcpy(&significand, &literal.floating, sizeof(significand));
    memcpy(&sign_exponent, (const uint8_t*)&literal.floating + sizeof(significand), sizeof(sign_exponent));
    const CheckValue value = {
        .type = literal.type,
        .category = CHECK_RVALUE,
        .flags = literal.imaginary ? 0 : literal.real ? BUSTER_CHECK_FLOAT_LITERAL : literal.type == BUSTER_TYPE_NULLPTR ? 0 : BUSTER_CHECK_ICE,
        .constant_kind = literal.imaginary ? CHECK_CONSTANT_COMPLEX : literal.real ? CHECK_CONSTANT_FLOATING :
                         literal.type == BUSTER_TYPE_NULLPTR ? CHECK_CONSTANT_ADDRESS : CHECK_CONSTANT_INTEGER,
        .integer = literal.imaginary ? (unsigned __int128)literal.integer << 64 : (unsigned __int128)literal.high << 64 | literal.integer,
        .floating_storage = {
            literal.imaginary ? 0 : significand,
            literal.imaginary ? 0 : sign_exponent,
        },
        .imaginary_storage = {
            literal.imaginary ? significand : 0,
            literal.imaginary ? sign_exponent : 0,
        },
    };
    return value;
}

#include "c_check_literal_warning.c"

// Always inlined (#880), as profile-guided builds do; hot caller: check_literal_string_part, once per character.
// `extended` (#1447) is the refused literal's cold decoding's alone (check_literal_strings_extended), which every hot
// caller's constant false takes out: it reads what clang reads that the fast decoding refuses, a delimited escape
// (\x{...} and \o{...}, numeric, and \u{...}, a universal character name's), a named universal character name
// (\N{NAME}, its name exactly Unicode's: unicode_name_strict) and an unknown escape, whose character is its value, as
// clang's, which warns of it (check_literal_unknown_escape).
static inline __attribute__((always_inline)) bool check_literal_character_unit_by(const uint8_t* bytes, uint32_t end, uint32_t* position, uint32_t* value, bool c23,
                                                                                bool extended)
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
        else if (extended && (c == 'x' || c == 'o' || c == 'u') && i < end && bytes[i] == '{')
        {
            // Its digits up to its brace, one at least, of its radix, and its value as a plain one's.
            const uint32_t radix = c == 'o' ? 8 : 16;
            const bool name = c == 'u';
            uint32_t n = 0;
            uint32_t digits = 0;
            i += 1;
            while (valid && i < end && bytes[i] != '}')
            {
                const uint32_t digit = check_literal_digit(bytes[i++]);
                valid = digit < radix && digits <= (UINT32_MAX - digit) / radix;
                digits = valid ? digits * radix + digit : digits;
                n += 1;
            }
            valid &= n != 0 && i < end;
            i += valid ? 1 : 0;
            valid &= !name || (digits <= 0x10FFFFu && (digits < 0xD800 || digits > 0xDFFF) && (c23 || digits >= 0xA0 || digits == '$' || digits == '@' || digits == '`'));
            c = digits;
        }
        else if (extended && c == 'N')
        {
            // A name up to its brace (no line break: clang's ends there), one byte at least, and its character, as a
            // universal character name's.
            const uint32_t name = i + 1;
            uint32_t close = name;
            while (close < end && bytes[close] != '}' && bytes[close] != '\n' && bytes[close] != '\r')
            {
                close += 1;
            }
            uint32_t named = 0;
            valid = i < end && bytes[i] == '{' && close < end && bytes[close] == '}' && close != name && unicode_name_strict(bytes + name, close - name, &named);
            valid &= c23 || named >= 0xA0 || named == '$' || named == '@' || named == '`';
            i = valid ? close + 1 : i;
            c = named;
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
            // An unknown escape's character (a printable ASCII one's but a digit, which the octal escape reads, and the
            // letters of a universal character name's) is its value in the extended decoding alone.
            valid = extended && c >= 0x20 && c < 0x7F && c != 'N' && c != 'U' && c != 'o';
            c = c == 'E' ? 27 : c;
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

static inline __attribute__((always_inline)) bool check_literal_character_unit(const uint8_t* bytes, uint32_t end, uint32_t* position, uint32_t* value, bool c23)
{
    return check_literal_character_unit_by(bytes, end, position, value, c23, false);
}

static CheckLiteral check_literal_character_by(const uint8_t* bytes, uint32_t length, bool c23, bool char_unsigned, bool extended)
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
        valid = check_literal_character_unit_by(bytes, length - 1, &position, &value, c23, extended) && value <= maximum;
        const bool numeric_escape = bytes[begin] == '\\' && begin + 1 < length &&
                                    (bytes[begin + 1] == 'x' || (bytes[begin + 1] >= '0' && bytes[begin + 1] <= '7') || (extended && bytes[begin + 1] == 'o'));
        valid &= !(ordinary || utf8) || value <= 127 || numeric_escape;
        combined = ordinary ? (combined << 8) | value : value;
        count += 1;
        // (More than four is clang's warning that keeps the last four, in the extended decoding.)
        valid &= ordinary ? count <= 4 || extended : count == 1;
    }
    if (valid && count != 0)
    {
        // One ordinary character is its char's value as an int (clang's CharLiteralParser): -1 for '\xff' where char is
        // signed, 255 where it isn't.
        const uint64_t bits = ordinary && count == 1 && !char_unsigned ? (uint64_t)(int64_t)(int8_t)combined : (uint64_t)combined;
        result = (CheckLiteral)
        {
            .type = type,
            .integer = bits,
            .valid = true,
            .characters = (uint8_t)(count < 255 ? count : 255),
        };
    }
    return result;
}

static CheckLiteral check_literal_character(const uint8_t* bytes, uint32_t length, bool c23, bool char_unsigned)
{
    return check_literal_character_by(bytes, length, c23, char_unsigned, false);
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

// The length of a string literal's encoding prefix (L, u, U or u8; a raw one's R is no part of it), 0 for none: clang reads
// an unevaluated string literal past it, with -Winvalid-unevaluated-string's warning at it (#1447), as an ordinary one.
static __attribute__((unused)) uint32_t check_literal_encoding_prefix(const uint8_t* bytes, uint32_t length)
{
    const bool prefixed = length >= 2 && (bytes[0] == 'L' || bytes[0] == 'u' || bytes[0] == 'U');
    const bool utf8 = prefixed && bytes[0] == 'u' && bytes[1] == '8';
    return prefixed ? (utf8 ? 2u : 1u) : 0u;
}

// `count` string literal parts read as an unevaluated string literal's: each past its encoding prefix
// (check_literal_encoding_prefix).
static __attribute__((unused)) void check_literal_unevaluated_parts(CheckStringPart* parts, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t prefix = parts[i].bytes != NULL ? check_literal_encoding_prefix(parts[i].bytes, parts[i].length) : 0;
        parts[i].bytes += prefix;
        parts[i].length -= prefix;
    }
}

// Always inlined (#880), as profile-guided builds do; hot caller: check_literal_string_part, once per unit of a string.
static inline __attribute__((always_inline)) bool check_literal_string_unit(uint32_t value, bool numeric, CheckStringEncoding encoding, uint8_t* output, uint64_t* length)
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

// One unit of a part's characters at `*position` (span.begin <= *position < span.end): a character, an escape sequence
// or, in a raw string literal, a byte, appended to `output` (when not NULL) at `*length`. The scalar loop's body, which
// check_literal_string_part_scalar runs for every unit and check_literal_string_part for the units its blocks leave.
static inline __attribute__((always_inline)) bool check_literal_string_step(const CheckStringPart* part, const CheckStringSpan* span, CheckStringEncoding encoding, bool c23, uint32_t* position, uint8_t* output, uint64_t* length,
                                                                          bool extended)
{
    const uint32_t begin = *position;
    const uint8_t first = part->bytes[begin];
    uint32_t value = first;
    bool numeric = false;
    bool valid;
    if (span->raw)
    {
        const uint32_t delimiter_length = part->length - span->end - 2;
        const bool closes = first == ')' && span->end - begin >= delimiter_length + 2 &&
                            memcmp(part->bytes + begin + 1, part->bytes + span->end + 1, delimiter_length) == 0 && part->bytes[begin + delimiter_length + 1] == '"';
        valid = !closes && first != 0;
        if (valid && first >= 0x80)
        {
            valid = check_literal_character_unit_by(part->bytes, span->end, position, &value, c23, extended);
        }
        else
        {
            *position += 1;
        }
    }
    else if (first == '\'')
    {
        valid = true;
        *position += 1;
    }
    else
    {
        valid = first != '"' && check_literal_character_unit_by(part->bytes, span->end, position, &value, c23, extended);
        numeric = first == '\\' && begin + 1 < span->end &&
                  (part->bytes[begin + 1] == 'x' || (part->bytes[begin + 1] >= '0' && part->bytes[begin + 1] <= '7') || (extended && part->bytes[begin + 1] == 'o'));
    }
    if (valid)
    {
        valid = check_literal_string_unit(value, numeric, encoding, output, length);
    }
    return valid;
}

// The scalar decoder of one part: the twin and oracle of check_literal_string_part's blocks (--scalar).
static bool check_literal_string_part_scalar(const CheckStringPart* part, CheckStringEncoding encoding, bool c23, uint8_t* output, uint64_t* length, bool extended)
{
    const CheckStringSpan span = check_literal_string_span(part);
    bool valid = span.valid;
    uint32_t position = span.begin;
    while (valid && position < span.end)
    {
        valid = check_literal_string_step(part, &span, encoding, c23, &position, output, length, extended);
    }
    return valid;
}

#if BUSTER_HOST_VECTOR
// #1388: a part's characters 64 bytes at a time, for the 1-byte encodings (ordinary and u8) outside raw string literals,
// where every unit is a byte of its own or a few bytes that decode to one, and every valid UTF-8 character is the bytes
// it is written with. A block starts at a unit and holds `avail` bytes of the part (masked loads: never past its end).
// Its masks give each unit's place:
//
// - Escape sequences start at the backslashes that open one: in a run of them, the first, the third and so on (the
//   block's first byte starts a unit). code = (((bs << 1) | odd) - bs) ^ odd, simdjson's escape scan without a carry
//   in, holds the escaping backslashes and the characters they escape.
// - Simple escapes (\n, \", \\ and the others) give their table value at the escaped character; \xh and \xhh give their
//   digits' value at the last digit, and \o, \oo and \ooo theirs at the last digit, the octal value modulo 256 (an
//   escape whose value is past 255 is a hard unit). Digit values come from a 128-byte table (vpermi2b), and a digit's
//   predecessors from vpermb.
// - Every other byte is a unit of its own (or, from 0x80, part of a UTF-8 character, which copies through), unless it
//   is '"', '\n', '\r' or NUL, which no string literal holds as written.
//
// A hard unit is one the masks don't decide: an invalid or UCN escape, a \x of no digit or more than two, an octal
// value past 255, a byte a literal can't hold, or a UTF-8 character the validation (Keiser and Lemire's three
// nibble tables, as in simdjson) finds an error at or right after. The block stores the units before the first hard
// one or, if there is none, the units that start below 60 (every committed unit is at most 4 bytes, so all of its
// bytes and the one after are in the block) or below its end: the values at the units' kept places, compressed in the
// register (vpcompressb, 2 uops on Zen 5) and stored under a mask of their count. The next block starts at the next
// unit. It returns the position where it stopped: the part's end, or a hard unit, which the caller's
// check_literal_string_step reads as the scalar loop does, so that the bytes and the validity are the scalar loop's.
static uint32_t check_literal_string_blocks(const uint8_t* bytes, uint32_t position, uint32_t end, uint8_t* output, uint64_t* length)
{
    _Alignas(64) static const uint8_t digits[128] = {
        ['0'] = 0, ['1'] = 1, ['2'] = 2, ['3'] = 3, ['4'] = 4, ['5'] = 5, ['6'] = 6, ['7'] = 7, ['8'] = 8, ['9'] = 9,
        ['a'] = 10, ['b'] = 11, ['c'] = 12, ['d'] = 13, ['e'] = 14, ['f'] = 15,
        ['A'] = 10, ['B'] = 11, ['C'] = 12, ['D'] = 13, ['E'] = 14, ['F'] = 15,
    };
    _Alignas(64) static const uint8_t digit_flags[128] = {
        ['0'] = 1, ['1'] = 1, ['2'] = 1, ['3'] = 1, ['4'] = 1, ['5'] = 1, ['6'] = 1, ['7'] = 1, ['8'] = 1, ['9'] = 1,
        ['a'] = 1, ['b'] = 1, ['c'] = 1, ['d'] = 1, ['e'] = 1, ['f'] = 1,
        ['A'] = 1, ['B'] = 1, ['C'] = 1, ['D'] = 1, ['E'] = 1, ['F'] = 1,
    };
    _Alignas(64) static const uint8_t escapes[128] = {
        ['a'] = 7, ['b'] = 8, ['e'] = 27, ['f'] = 12, ['n'] = 10, ['r'] = 13, ['t'] = 9, ['v'] = 11,
        ['\\'] = '\\', ['\''] = '\'', ['"'] = '"', ['?'] = '?',
    };
    _Alignas(64) static const uint8_t lanes[64] = {
        0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
        31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62,
    };
    // Keiser and Lemire's tables ("Validating UTF-8 In Less Than One Instruction Per Byte", 2021), as simdjson has them:
    // the error classes of a byte's high nibble, its low nibble, and the next byte's high nibble.
    enum
    {
        TOO_SHORT = 1 << 0,
        TOO_LONG = 1 << 1,
        OVERLONG_3 = 1 << 2,
        TOO_LARGE = 1 << 3,
        SURROGATE = 1 << 4,
        OVERLONG_2 = 1 << 5,
        TOO_LARGE_1000 = 1 << 6,
        OVERLONG_4 = 1 << 6,
        TWO_CONTS = 1 << 7,
        CARRY = TOO_SHORT | TOO_LONG | TWO_CONTS,
    };
    _Alignas(16) static const uint8_t byte_1_high[16] = {
        TOO_LONG, TOO_LONG, TOO_LONG, TOO_LONG, TOO_LONG, TOO_LONG, TOO_LONG, TOO_LONG,
        TWO_CONTS, TWO_CONTS, TWO_CONTS, TWO_CONTS,
        TOO_SHORT | OVERLONG_2,
        TOO_SHORT,
        TOO_SHORT | OVERLONG_3 | SURROGATE,
        TOO_SHORT | TOO_LARGE | TOO_LARGE_1000 | OVERLONG_4,
    };
    _Alignas(16) static const uint8_t byte_1_low[16] = {
        CARRY | OVERLONG_3 | OVERLONG_2 | OVERLONG_4,
        CARRY | OVERLONG_2,
        CARRY,
        CARRY,
        CARRY | TOO_LARGE,
        CARRY | TOO_LARGE | TOO_LARGE_1000,
        CARRY | TOO_LARGE | TOO_LARGE_1000,
        CARRY | TOO_LARGE | TOO_LARGE_1000,
        CARRY | TOO_LARGE | TOO_LARGE_1000,
        CARRY | TOO_LARGE | TOO_LARGE_1000,
        CARRY | TOO_LARGE | TOO_LARGE_1000,
        CARRY | TOO_LARGE | TOO_LARGE_1000,
        CARRY | TOO_LARGE | TOO_LARGE_1000,
        CARRY | TOO_LARGE | TOO_LARGE_1000 | SURROGATE,
        CARRY | TOO_LARGE | TOO_LARGE_1000,
        CARRY | TOO_LARGE | TOO_LARGE_1000,
    };
    _Alignas(16) static const uint8_t byte_2_high[16] = {
        TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT,
        TOO_LONG | OVERLONG_2 | TWO_CONTS | OVERLONG_3 | TOO_LARGE_1000 | OVERLONG_4,
        TOO_LONG | OVERLONG_2 | TWO_CONTS | OVERLONG_3 | TOO_LARGE,
        TOO_LONG | OVERLONG_2 | TWO_CONTS | SURROGATE | TOO_LARGE,
        TOO_LONG | OVERLONG_2 | TWO_CONTS | SURROGATE | TOO_LARGE,
        TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT,
    };
    const __m512i digit_low = _mm512_load_si512(digits);
    const __m512i digit_high = _mm512_load_si512(digits + 64);
    const __m512i flag_low = _mm512_load_si512(digit_flags);
    const __m512i flag_high = _mm512_load_si512(digit_flags + 64);
    const __m512i escape_low = _mm512_load_si512(escapes);
    const __m512i escape_high = _mm512_load_si512(escapes + 64);
    const __m512i previous = _mm512_load_si512(lanes);
    const __m512i nibble = _mm512_set1_epi8(15);
    const uint64_t odd = 0xAAAAAAAAAAAAAAAAull;
    uint64_t written = *length;
    bool more = position < end;
    while (more)
    {
        const uint32_t avail = end - position;
        const uint64_t live = avail >= 64 ? ~0ull : _bzhi_u64(~0ull, avail);
        // Inactive lanes are 0: no digit, no escape, the end of the part.
        const __m512i v = _mm512_maskz_loadu_epi8(live, bytes + position);
        const uint64_t high = _mm512_movepi8_mask(v);
        const uint64_t bs = _mm512_cmpeq_epi8_mask(v, _mm512_set1_epi8('\\'));
        const uint64_t code = (((bs << 1) | odd) - bs) ^ odd;
        const uint64_t starts = code & bs;
        const uint64_t escaped = starts << 1;
        const __m512i flag = _mm512_permutex2var_epi8(flag_low, v, flag_high);
        const __m512i digit = _mm512_permutex2var_epi8(digit_low, v, digit_high);
        const __m512i escape = _mm512_permutex2var_epi8(escape_low, v, escape_high);
        const uint64_t hex = _mm512_mask_test_epi8_mask(~high, flag, flag);
        const uint64_t octal = _mm512_mask_cmplt_epu8_mask(hex, digit, _mm512_set1_epi8(8));
        const uint64_t large = _mm512_mask_cmpge_epu8_mask(octal, digit, _mm512_set1_epi8(4));
        const uint64_t simple = _mm512_mask_test_epi8_mask(~high, escape, escape);
        const uint64_t x = escaped & _mm512_cmpeq_epi8_mask(v, _mm512_set1_epi8('x'));
        const uint64_t bad = _mm512_cmpeq_epi8_mask(v, _mm512_set1_epi8('"')) | _mm512_cmpeq_epi8_mask(v, _mm512_set1_epi8('\n')) |
                             _mm512_cmpeq_epi8_mask(v, _mm512_set1_epi8('\r')) | _mm512_mask_testn_epi8_mask(live, v, v);
        const uint64_t next_hex = hex >> 1;
        const uint64_t next_octal = octal >> 1;
        const uint64_t x1 = (x << 1) & hex;
        const uint64_t x2 = (x1 << 1) & hex;
        const uint64_t o1 = escaped & octal;
        const uint64_t o2 = (o1 << 1) & octal;
        const uint64_t o3 = (o2 << 1) & octal;
        const uint64_t named = escaped & simple;
        const uint64_t consumed = escaped | x1 | x2 | o2 | o3;
        const uint64_t plain = live & ~starts & ~consumed;
        const uint64_t keep = (plain | named | o3) | (x1 & ~next_hex) | (x2 & ~next_hex) | (o1 & ~next_octal) | (o2 & ~next_octal);
        // The escapes the masks can't decode, at their backslashes: UCNs and invalid ones, \x with no digit or more
        // than two, octal values past 255; and the bytes no literal holds as written.
        uint64_t hard = ((escaped & ~(named | x | o1)) >> 1) | ((x & ~next_hex) >> 1) | ((x2 & next_hex) >> 3) | (((o3 >> 2) & large) >> 1) |
                        (bad & plain);
        uint64_t units = starts | plain | 1;
        if (high != 0)
        {
            const __m512i previous_1 = _mm512_maskz_permutexvar_epi8(~1ull, previous, v);
            const __m512i previous_2 = _mm512_maskz_permutexvar_epi8(~1ull, previous, previous_1);
            const __m512i previous_3 = _mm512_maskz_permutexvar_epi8(~1ull, previous, previous_2);
            const __m512i high_1 = _mm512_and_si512(_mm512_srli_epi16(previous_1, 4), nibble);
            const __m512i low_1 = _mm512_and_si512(previous_1, nibble);
            const __m512i high_2 = _mm512_and_si512(_mm512_srli_epi16(v, 4), nibble);
            const __m512i classes = _mm512_ternarylogic_epi64(_mm512_shuffle_epi8(_mm512_broadcast_i32x4(_mm_load_si128((const __m128i*)byte_1_high)), high_1),
                                                              _mm512_shuffle_epi8(_mm512_broadcast_i32x4(_mm_load_si128((const __m128i*)byte_1_low)), low_1),
                                                              _mm512_shuffle_epi8(_mm512_broadcast_i32x4(_mm_load_si128((const __m128i*)byte_2_high)), high_2), 0x80);
            const __m512i third = _mm512_subs_epu8(previous_2, _mm512_set1_epi8((char)(0xE0 - 0x80)));
            const __m512i fourth = _mm512_subs_epu8(previous_3, _mm512_set1_epi8((char)(0xF0 - 0x80)));
            const __m512i must = _mm512_and_si512(_mm512_or_si512(third, fourth), _mm512_set1_epi8((char)0x80));
            const uint64_t errors = _mm512_test_epi8_mask(_mm512_xor_si512(classes, must), _mm512_xor_si512(classes, must));
            // Continuation bytes start no unit; a stray one is an error at itself, which the hard unit before it
            // reaches.
            units &= ~_mm512_cmpeq_epi8_mask(_mm512_and_si512(v, _mm512_set1_epi8((char)0xC0)), _mm512_set1_epi8((char)0x80)) | 1;
            // An error at e belongs to the unit that holds e or ends right before it: the last unit start below e,
            // or the block's first. Every unit that starts before that one is valid.
            const uint64_t before = errors != 0 ? (units & _bzhi_u64(~0ull, (uint32_t)_tzcnt_u64(errors))) | 1 : 0;
            hard |= before != 0 ? 1ull << (63 - _lzcnt_u64(before)) : 0;
        }
        const uint32_t window = avail < 64 ? avail : 60;
        const uint32_t first_hard = hard != 0 ? (uint32_t)_tzcnt_u64(hard) : 64;
        const uint32_t stop = first_hard < window ? first_hard : window;
        units |= avail < 64 ? 1ull << avail : 0;
        const uint32_t advance = stop + (uint32_t)_tzcnt_u64(units >> stop);
        const uint64_t kept = keep & (advance >= 64 ? ~0ull : _bzhi_u64(~0ull, advance));
        const uint32_t count = (uint32_t)_mm_popcnt_u64(kept);
        if (output != NULL)
        {
            const __m512i previous_digit = _mm512_maskz_permutexvar_epi8(~1ull, previous, digit);
            const __m512i second_digit = _mm512_maskz_permutexvar_epi8(~1ull, previous, previous_digit);
            // Digits are below 16 where they are kept, so each shifted byte's low bits, which the masks clear, are
            // all that a 16-bit shift brings from its neighbor.
            const __m512i hex_2 = _mm512_ternarylogic_epi64(_mm512_slli_epi16(previous_digit, 4), _mm512_set1_epi8((char)0xF0), digit, 0xEA);
            const __m512i octal_2 = _mm512_ternarylogic_epi64(_mm512_slli_epi16(previous_digit, 3), _mm512_set1_epi8((char)0xF8), digit, 0xEA);
            const __m512i octal_3 = _mm512_ternarylogic_epi64(_mm512_slli_epi16(second_digit, 6), _mm512_set1_epi8((char)0xC0), octal_2, 0xEA);
            __m512i value = _mm512_mask_mov_epi8(v, named, escape);
            value = _mm512_mask_mov_epi8(value, x1 | o1, digit);
            value = _mm512_mask_mov_epi8(value, x2, hex_2);
            value = _mm512_mask_mov_epi8(value, o2, octal_2);
            value = _mm512_mask_mov_epi8(value, o3, octal_3);
            _mm512_mask_storeu_epi8(output + written, _bzhi_u64(~0ull, count), _mm512_maskz_compress_epi8(kept, value));
        }
        written += count;
        position += advance;
        more = stop == window && avail >= 64;
    }
    *length = written;
    return position;
}
#endif

// A part's characters in units (check_literal_string_step), appended to `output` (when not NULL) at `*length`. With a
// vector host and a 1-byte encoding outside a raw string literal, check_literal_string_blocks reads as many units as
// it decides at a time, and the scalar step only those it leaves (#1388); --scalar, and every other part, read every
// unit by the step (check_literal_string_part_scalar).
static bool check_literal_string_part(const CheckStringPart* part, CheckStringEncoding encoding, bool c23, bool scalar, uint8_t* output, uint64_t* length)
{
    bool valid;
#if BUSTER_HOST_VECTOR
    const CheckStringSpan span = check_literal_string_span(part);
    const bool blocks = !scalar && !span.raw && (encoding == CHECK_STRING_ORDINARY || encoding == CHECK_STRING_UTF8);
    if (blocks)
    {
        valid = span.valid;
        uint32_t position = span.begin;
        while (valid && position < span.end)
        {
            // A string's length can't near UINT64_MAX; the step checks it, as the scalar loop does, where it might.
            position = *length <= UINT64_MAX - 64 ? check_literal_string_blocks(part->bytes, position, span.end, output, length) : position;
            if (position < span.end)
            {
                valid = check_literal_string_step(part, &span, encoding, c23, &position, output, length, false);
            }
        }
    }
    else
    {
        valid = check_literal_string_part_scalar(part, encoding, c23, output, length, false);
    }
#else
    (void)scalar;
    valid = check_literal_string_part_scalar(part, encoding, c23, output, length, false);
#endif
    return valid;
}

// Decode one adjacent literal group after preprocessing. Input slices are immutable and unpadded; the result's
// contiguous little-endian target bytes include exactly one terminator. A validation/measurement pass precedes
// allocation, so invalid input or insufficient output capacity leaves the caller's arena unchanged. The caller
// interns the completed array type and owns first-use string identities; this decoder does no interning. Input
// slices may reside in the arena's existing used prefix, but must not overlap the storage appended by this call.
static inline __attribute__((always_inline)) CheckStringLiteral check_literal_strings_decode(const CheckStringPart* parts, uint32_t count, bool c23, bool scalar, bool extended, Arena* output)
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
        valid = extended ? check_literal_string_part_scalar(&parts[i], encoding, c23, NULL, &length, true) :
                           check_literal_string_part(&parts[i], encoding, c23, scalar, NULL, &length);
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
                valid = extended ? check_literal_string_part_scalar(&parts[i], encoding, c23, bytes, &written, true) :
                                   check_literal_string_part(&parts[i], encoding, c23, scalar, bytes, &written);
            }
            valid = valid && check_literal_string_unit(0, true, encoding, bytes, &written) && written == length;
            if (valid)
            {
                result.element_type = encoding == CHECK_STRING_UTF16 ? BUSTER_TYPE_USHORT : encoding == CHECK_STRING_UTF32 ? BUSTER_TYPE_UINT : encoding == CHECK_STRING_WIDE ? BUSTER_TYPE_INT : encoding == CHECK_STRING_UTF8 && c23 ? BUSTER_TYPE_UCHAR : BUSTER_TYPE_CHAR;
                result.bytes = bytes;
                result.byte_count = length;
                result.unit_count = length / width;
                result.valid = true;
                result.utf8 = encoding == CHECK_STRING_UTF8;
            }
            else
            {
                arena_pop_to(output, mark);
            }
        }
    }
    return result;
}

static CheckStringLiteral check_literal_strings_by(const CheckStringPart* parts, uint32_t count, bool c23, bool scalar, Arena* output)
{
    return check_literal_strings_decode(parts, count, c23, scalar, false, output);
}

// The extended decoding of a literal the fast one refused (#1447, check_literal_character_unit_by's `extended`): the
// scalar step's, on the cold path of a refused literal alone.
static __attribute__((noinline, cold)) CheckStringLiteral check_literal_strings_extended(const CheckStringPart* parts, uint32_t count, bool c23, Arena* output)
{
    return check_literal_strings_decode(parts, count, c23, true, true, output);
}

// check_literal_strings_by with the host's decoder: its blocks on a vector host (#1388).
static CheckStringLiteral check_literal_strings(const CheckStringPart* parts, uint32_t count, bool c23, Arena* output)
{
    return check_literal_strings_by(parts, count, c23, false, output);
}
