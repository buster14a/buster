#include <buster/lib/string.h>
#include <buster/lib/arena.h>
#include <buster/lib/os.h>
#include <buster/lib/integer.h>

BUSTER_GLOBAL_LOCAL bool code_unit_is_binary(char8 code_unit)
{
    return (code_unit == '1') | (code_unit == '0');
}

bool code_unit_is_decimal(char8 code_unit)
{
    return (code_unit >= '0') & (code_unit <= '9');
}

BUSTER_GLOBAL_LOCAL bool code_unit_is_octal(char8 code_unit)
{
    return (code_unit >= '0') & (code_unit <= '7');
}
// #define code_unit_is_octal(code_unit) is_between_range_included(ch, '0', '7')

BUSTER_GLOBAL_LOCAL bool code_unit_is_hexadecimal_alpha_upper(char8 code_unit)
{
    return (code_unit >= 'A') & (code_unit <= 'F');
}

BUSTER_GLOBAL_LOCAL bool code_unit_is_hexadecimal_alpha_lower(char8 code_unit)
{
    return (code_unit >= 'a') & (code_unit <= 'f');
}

BUSTER_GLOBAL_LOCAL bool code_unit_is_hexadecimal_alpha(char8 code_unit)
{
    return (int)code_unit_is_hexadecimal_alpha_lower(code_unit) | code_unit_is_hexadecimal_alpha_upper(code_unit);
}

BUSTER_GLOBAL_LOCAL bool code_unit_is_hexadecimal(char8 code_unit)
{
    return (int)code_unit_is_decimal(code_unit) | code_unit_is_hexadecimal_alpha(code_unit);
}

bool code_unit8_is_decimal(char8 code_unit)
{
    return code_unit_is_decimal(code_unit);
}

BUSTER_GLOBAL_LOCAL u64 parsing_accumulate_binary(u64 accumulator, char8 code_unit)
{
    BUSTER_CHECK(code_unit_is_binary(code_unit));
    return ((accumulator) * 2) + ((code_unit) - '0');
}

BUSTER_GLOBAL_LOCAL u64 parsing_accumulate_octal(u64 accumulator, char8 code_unit)
{
    BUSTER_CHECK(code_unit_is_octal(code_unit));
    return ((accumulator) * 8) + ((code_unit) - '0');
}

BUSTER_GLOBAL_LOCAL u64 parsing_accumulate_decimal(u64 accumulator, char8 code_unit)
{
    BUSTER_CHECK(code_unit_is_decimal(code_unit));
    return accumulator * 10 + ((code_unit) - '0');
}

BUSTER_GLOBAL_LOCAL u64 parsing_accumulate_hexadecimal(u64 accumulator, char8 code_unit)
{
    BUSTER_CHECK(code_unit_is_hexadecimal(code_unit));
    return ((accumulator) * 16 + (code_unit) -
            (code_unit_is_decimal(code_unit) ? '0'
                                             : (code_unit_is_hexadecimal_alpha_upper(code_unit)   ? ('A' - 10)
                                                : code_unit_is_hexadecimal_alpha_lower(code_unit) ? ('a' - 10)
                                                                                                  : 0)));
}

BUSTER_GLOBAL_LOCAL IntegerParsingU64 string_parse_u64_decimal(const char8* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        char8 code_unit = p[i];

        if (!code_unit_is_decimal(code_unit))
        {
            break;
        }

        i += 1;
        value = parsing_accumulate_decimal(value, code_unit);
    }

    return (IntegerParsingU64){.value = value, .length = i};
}

IntegerParsingU64 string_parse_u64_hexadecimal(const char8* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        char8 code_unit = p[i];

        if (!code_unit_is_hexadecimal(code_unit))
        {
            break;
        }

        i += 1;
        value = parsing_accumulate_hexadecimal(value, code_unit);
    }

    return (IntegerParsingU64){.value = value, .length = i};
}

IntegerParsingU64 string_parse_u64_octal(const char8* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        char8 code_unit = p[i];

        if (!code_unit_is_octal(code_unit))
        {
            break;
        }

        i += 1;
        value = parsing_accumulate_octal(value, (u8)code_unit);
    }

    return (IntegerParsingU64){.value = value, .length = i};
}

IntegerParsingU64 string_parse_u64_binary(const char8* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        char8 code_unit = p[i];

        if (!code_unit_is_binary(code_unit))
        {
            break;
        }

        i += 1;
        value = parsing_accumulate_binary(value, code_unit);
    }

    return (IntegerParsingU64){.value = value, .length = i};
}

IntegerParsingU64 string8_parse_u64_hexadecimal(const char8* restrict p)
{
    return string_parse_u64_hexadecimal(p);
}

IntegerParsingU64 string8_parse_u64_decimal(const char8* restrict p)
{
    return string_parse_u64_decimal(p);
}

IntegerParsingU64 string8_parse_u64_octal(const char8* restrict p)
{
    return string_parse_u64_octal(p);
}

IntegerParsingU64 string8_parse_u64_binary(const char8* restrict p)
{
    return string_parse_u64_binary(p);
}

String8 string_slice(String8 slice, u64 start, u64 end)
{
    return (String8){.pointer = (slice).pointer + (start), .length = (end) - (start)};
}

String8 string_join_arena(Arena* arena, SliceString8 strings, bool zero_terminate)
{
    u64 length = 0;

    for (u64 i = 0; i < strings.length; i += 1)
    {
        String8 string = strings.pointer[i];
        length += string.length;
    }

    u64 char_size = sizeof(strings.pointer[0].pointer[0]);

    char8* restrict pointer = (char8*)arena_allocate_bytes(arena, (length + zero_terminate) * char_size, BUSTER_ALIGN_OF(char8));

    u64 i = 0;

    for (u64 index = 0; index < strings.length; index += 1)
    {
        String8 string = strings.pointer[index];
        memcpy(pointer + i, string.pointer, BUSTER_SLICE_SIZE(string));
        i += string.length;
    }

    BUSTER_CHECK(i == length);
    if (zero_terminate)
    {
        pointer[i] = 0;
    }

    return (String8){.pointer = pointer, .length = length};
}

bool string_equal(String8 s1, String8 s2)
{
    bool is_equal = s1.length == s2.length;
    if (is_equal & (s1.pointer != 0) & (s1.pointer != s2.pointer))
    {
#if BUSTER_OPTIMIZE
        is_equal = memory_compare(s1.pointer, s2.pointer, s1.length * sizeof(char8));
#else
        for (u64 i = 0; i < s1.length; i += 1)
        {
            if (s1.pointer[i] != s2.pointer[i])
            {
                is_equal = false;
                break;
            }
        }
#endif
    }
    return is_equal;
}

bool string16_equal(String16 s1, String16 s2)
{
    bool is_equal = s1.length == s2.length;
    if (is_equal & (s1.pointer != 0) & (s1.pointer != s2.pointer))
    {
        for (u64 i = 0; i < s1.length; i += 1)
        {
            if (s1.pointer[i] != s2.pointer[i])
            {
                is_equal = false;
                break;
            }
        }
    }
    return is_equal;
}

bool string_ends_with_sequence(String8 string, String8 ending)
{
    bool result = string.length >= ending.length;
    if (result)
    {
        String8 last_chunk = string_slice(string, string.length - ending.length, string.length);
        result = string_equal(last_chunk, ending);
    }
    return result;
}

u64 string_array_match(SliceString8 names, String8 name)
{
    u64 result = BUSTER_STRING_NO_MATCH;

    for (u64 i = 0; i < names.length; i += 1)
    {
        if (string_equal(name, names.pointer[i]))
        {
            result = i;
            break;
        }
    }

    return result;
}

bool string_starts_with_sequence(String8 string, String8 sequence)
{
    bool result = string.length >= sequence.length;

    if (result)
    {
        String8 first_chunk = string_slice(string, 0, sequence.length);
        result = string_equal(first_chunk, sequence);
    }

    return result;
}

u64 string_first_code_unit(String8 string, char8 code_unit)
{
    u64 result = BUSTER_STRING_NO_MATCH;

    for (EACH_SLICE_INT(i, string))
    {
        char8 cu = string.pointer[i];
        if (cu == code_unit)
        {
            result = i;
            break;
        }
    }

    return result;
}

u64 string16_first_code_unit(String16 string, char16 code_unit)
{
    u64 result = BUSTER_STRING_NO_MATCH;

    for (EACH_SLICE_INT(i, string))
    {
        char16 cu = string.pointer[i];
        if (cu == code_unit)
        {
            result = i;
            break;
        }
    }

    return result;
}

String8 string_from_pointer_length(const char8* pointer, u64 length)
{
    return (String8){.pointer = (char8*)pointer, .length = length};
}

BUSTER_TEST_F_DECL String16 string16_from_pointer_length(const char16* pointer, u64 length)
{
    return (String16){.pointer = (char16*)pointer, .length = length};
}

typedef struct Utf8Result Utf8Result;
struct Utf8Result
{
    char8 buffer[4];
    u32 count;
};

typedef struct Utf8DecodeResult Utf8DecodeResult;
struct Utf8DecodeResult
{
    u32 code_point;
    u64 advance;
};

BUSTER_GLOBAL_LOCAL Utf8DecodeResult utf8_decode(String8 string, u64 index);
BUSTER_GLOBAL_LOCAL u64 utf16_write_code_point(char16* destination, u32 code_point);
BUSTER_GLOBAL_LOCAL Utf8Result utf8_from_code_point(u32 code_point);

String8 string8_from_string16(Arena* arena, String16 s, bool null_terminate)
{
    u64 position = arena->position;

    for (u64 i = 0; i < s.length;)
    {
        u32 code_point = s.pointer[i];
        u64 advance = 1;

        if (code_point >= 0xD800u && code_point <= 0xDBFFu && i + 1 < s.length)
        {
            u32 second = s.pointer[i + 1];
            if (second >= 0xDC00u && second <= 0xDFFFu)
            {
                code_point = (((code_point - 0xD800u) << 10) | (second - 0xDC00u)) + 0x10000u;
                advance = 2;
            }
            else
            {
                code_point = 0xFFFDu;
            }
        }
        else if (code_point >= 0xDC00u && code_point <= 0xDFFFu)
        {
            code_point = 0xFFFDu;
        }

        Utf8Result encoding_result = utf8_from_code_point(code_point);
        char8* allocation = arena_allocate(arena, char8, encoding_result.count);

        for (u32 encoding_i = 0; encoding_i < encoding_result.count; encoding_i += 1)
        {
            allocation[encoding_i] = encoding_result.buffer[encoding_i];
        }

        i += advance;
    }

    u64 result_length = arena->position - position;
    if (null_terminate)
    {
        *arena_allocate(arena, char8, 1) = 0;
    }

    String8 result = string_from_pointer_length((char8*)arena_get_byte_pointer_at_position(arena, position), result_length);
    return result;
}

BUSTER_GLOBAL_LOCAL void string_reverse(String8 s)
{
    if (s.length < 2)
    {
        return;
    }

    char8* restrict pointer = s.pointer;
    for (u64 i = 0, reverse_i = s.length - 1; i < reverse_i; i += 1, reverse_i -= 1)
    {
        char8 ch = pointer[i];
        pointer[i] = pointer[reverse_i];
        pointer[reverse_i] = ch;
    }
}

typedef struct StringFormatU128Parts StringFormatU128Parts;
struct StringFormatU128Parts
{
    u64 low;
    u64 high;
};

typedef enum StringFormatIntegerKind
{
    STRING_FORMAT_INTEGER_KIND_DECIMAL,
    STRING_FORMAT_INTEGER_KIND_BINARY,
    STRING_FORMAT_INTEGER_KIND_OCTAL,
    STRING_FORMAT_INTEGER_KIND_HEXADECIMAL_LOWER,
    STRING_FORMAT_INTEGER_KIND_HEXADECIMAL_UPPER,
    STRING_FORMAT_INTEGER_KIND_COUNT,
} StringFormatIntegerKind;

BUSTER_GLOBAL_LOCAL void arena_append_string(Arena* arena, String8 string);
BUSTER_GLOBAL_LOCAL void string_append_repeated_code_unit(Arena* arena, char8 code_unit, u64 code_unit_count);

#define BUSTER_FORMAT_INTEGER_MAX_WIDTH ((u64)128)
#define BUSTER_FORMAT_INTEGER_BUFFER_LENGTH (BUSTER_FORMAT_INTEGER_MAX_WIDTH * 2 + 2)

BUSTER_GLOBAL_LOCAL bool string_format_u128_parts_is_zero(StringFormatU128Parts value)
{
    return (value.low == 0) & (value.high == 0);
}

BUSTER_GLOBAL_LOCAL u64 string_format_u128_parts_divide(StringFormatU128Parts* value, u64 divisor)
{
    BUSTER_CHECK(divisor >= 2 && divisor <= 16);

    if (!value->high)
    {
        u64 remainder = value->low % divisor;
        value->low /= divisor;
        return remainder;
    }

    StringFormatU128Parts quotient = {0};
    u64 remainder = 0;

    // Long division from the most significant bit keeps the implementation
    // independent of a compiler-provided 128-bit integer type.
    for (u64 bit_index = 128; bit_index != 0; bit_index -= 1)
    {
        u64 shift = bit_index - 1;
        u64 bit = shift >= 64 ? (value->high >> (shift - 64)) & 1u : (value->low >> shift) & 1u;
        remainder = remainder * 2 + bit;

        if (remainder >= divisor)
        {
            remainder -= divisor;
            if (shift >= 64)
            {
                quotient.high |= (u64)1 << (shift - 64);
            }
            else
            {
                quotient.low |= (u64)1 << shift;
            }
        }
    }

    *value = quotient;
    return remainder;
}

BUSTER_GLOBAL_LOCAL StringFormatU128Parts string_format_u128_parts_from_u128(u128 value)
{
#if defined(__clang__)
    return (StringFormatU128Parts){
        .low = (u64)value,
        .high = (u64)(value >> 64),
    };
#else
    return (StringFormatU128Parts){
        .low = value.v[0],
        .high = value.v[1],
    };
#endif
}

BUSTER_GLOBAL_LOCAL StringFormatU128Parts string_format_u128_parts_from_s128(s128 value)
{
#if defined(__clang__)
    return string_format_u128_parts_from_u128((u128)value);
#else
    return (StringFormatU128Parts){
        .low = value.v[0],
        .high = value.v[1],
    };
#endif
}

BUSTER_GLOBAL_LOCAL StringFormatU128Parts string_format_u128_parts_negate(StringFormatU128Parts value)
{
    u64 low = value.low;
    return (StringFormatU128Parts){
        .low = 0 - low,
        .high = ~value.high + (low == 0),
    };
}

BUSTER_GLOBAL_LOCAL StringFormatU128Parts string_format_u128_parts_mask(StringFormatU128Parts value, u64 bit_width)
{
    BUSTER_CHECK(bit_width != 0 && bit_width <= 128);

    if (bit_width < 64)
    {
        value.low &= ((u64)1 << bit_width) - 1;
        value.high = 0;
    }
    else if (bit_width == 64)
    {
        value.high = 0;
    }
    else if (bit_width < 128)
    {
        value.high &= ((u64)1 << (bit_width - 64)) - 1;
    }

    return value;
}

BUSTER_GLOBAL_LOCAL String8 string_format_u128_parts_radix(String8 buffer, StringFormatU128Parts value, u64 radix, bool upper)
{
    BUSTER_CHECK(radix == 2 || radix == 8 || radix == 10 || radix == 16);

    u64 length = 0;
    do
    {
        u64 digit = string_format_u128_parts_divide(&value, radix);
        char8 alpha_start = upper ? 'A' : 'a';
        char8 character = (char8)(digit > 9 ? digit - 10 + alpha_start : digit + '0');
        BUSTER_CHECK(length < buffer.length);
        buffer.pointer[length] = character;
        length += 1;
    } while (!string_format_u128_parts_is_zero(value));

    String8 result = (String8){.pointer = buffer.pointer, .length = length};
    string_reverse(result);
    return result;
}

BUSTER_GLOBAL_LOCAL u64 string_format_integer_max_width(u64 bit_width, StringFormatIntegerKind kind)
{
    switch (kind)
    {
        break;
    case STRING_FORMAT_INTEGER_KIND_DECIMAL:
        switch (bit_width)
        {
            break;
        case 8:
            return 3;
        case 16:
            return 5;
        case 32:
            return 10;
        case 64:
            return 20;
        case 128:
            return 39;
        default:
            BUSTER_UNREACHABLE();
        }
        break;
    case STRING_FORMAT_INTEGER_KIND_BINARY:
        return bit_width;
    case STRING_FORMAT_INTEGER_KIND_OCTAL:
        return (bit_width + 2) / 3;
    case STRING_FORMAT_INTEGER_KIND_HEXADECIMAL_LOWER:
    case STRING_FORMAT_INTEGER_KIND_HEXADECIMAL_UPPER:
        return (bit_width + 3) / 4;
    case STRING_FORMAT_INTEGER_KIND_COUNT:
        BUSTER_UNREACHABLE();
    }

    BUSTER_UNREACHABLE();
}

BUSTER_GLOBAL_LOCAL u64 string_format_integer_radix(StringFormatIntegerKind kind)
{
    switch (kind)
    {
        break;
    case STRING_FORMAT_INTEGER_KIND_DECIMAL:
        return 10;
    case STRING_FORMAT_INTEGER_KIND_BINARY:
        return 2;
    case STRING_FORMAT_INTEGER_KIND_OCTAL:
        return 8;
    case STRING_FORMAT_INTEGER_KIND_HEXADECIMAL_LOWER:
    case STRING_FORMAT_INTEGER_KIND_HEXADECIMAL_UPPER:
        return 16;
    case STRING_FORMAT_INTEGER_KIND_COUNT:
        BUSTER_UNREACHABLE();
    }

    BUSTER_UNREACHABLE();
}

BUSTER_GLOBAL_LOCAL char8 string_format_integer_prefix_character(StringFormatIntegerKind kind)
{
    switch (kind)
    {
        break;
    case STRING_FORMAT_INTEGER_KIND_DECIMAL:
        return 'd';
    case STRING_FORMAT_INTEGER_KIND_BINARY:
        return 'b';
    case STRING_FORMAT_INTEGER_KIND_OCTAL:
        return 'o';
    case STRING_FORMAT_INTEGER_KIND_HEXADECIMAL_LOWER:
    case STRING_FORMAT_INTEGER_KIND_HEXADECIMAL_UPPER:
        return 'x';
    case STRING_FORMAT_INTEGER_KIND_COUNT:
        BUSTER_UNREACHABLE();
    }

    BUSTER_UNREACHABLE();
}

BUSTER_GLOBAL_LOCAL u64 string_format_integer_digit_group_size(StringFormatIntegerKind kind)
{
    switch (kind)
    {
        break;
    case STRING_FORMAT_INTEGER_KIND_DECIMAL:
    case STRING_FORMAT_INTEGER_KIND_OCTAL:
        return 3;
    case STRING_FORMAT_INTEGER_KIND_BINARY:
        return 8;
    case STRING_FORMAT_INTEGER_KIND_HEXADECIMAL_LOWER:
    case STRING_FORMAT_INTEGER_KIND_HEXADECIMAL_UPPER:
        return 2;
    case STRING_FORMAT_INTEGER_KIND_COUNT:
        BUSTER_UNREACHABLE();
    }

    BUSTER_UNREACHABLE();
}

BUSTER_GLOBAL_LOCAL void string_append_integer_digits(Arena* arena, String8 digits, StringFormatIntegerKind kind, bool digit_group)
{
    u64 group_size = string_format_integer_digit_group_size(kind);
    bool separator_characters = digit_group && digits.length > group_size;

    if (!separator_characters)
    {
        arena_append_string(arena, digits);
        return;
    }

    char8 separator = kind == STRING_FORMAT_INTEGER_KIND_DECIMAL ? '.' : '_';
    u64 remainder = digits.length % group_size;
    if (remainder)
    {
        arena_append_string(arena, (String8){.pointer = digits.pointer, .length = remainder});
        *arena_allocate(arena, char8, 1) = separator;
    }

    for (u64 source_index = remainder; source_index < digits.length - group_size; source_index += group_size)
    {
        arena_append_string(arena, (String8){.pointer = digits.pointer + source_index, .length = group_size});
        *arena_allocate(arena, char8, 1) = separator;
    }

    arena_append_string(arena, (String8){.pointer = digits.pointer + digits.length - group_size, .length = group_size});
}

typedef struct StringFormatIntegerOptions StringFormatIntegerOptions;
struct StringFormatIntegerOptions
{
    StringFormatIntegerKind kind;
    bool prefix;
    bool digit_group;
    bool width_natural_extension;
    u64 width;
    char8 width_character;
};

BUSTER_GLOBAL_LOCAL void string_append_formatted_integer(Arena* arena, StringFormatU128Parts value, u64 bit_width, bool signed_value, bool negative,
                                                         StringFormatIntegerOptions options)
{
    StringFormatU128Parts format_value = value;
    bool decimal_negative = signed_value && negative && options.kind == STRING_FORMAT_INTEGER_KIND_DECIMAL;
    if (options.kind != STRING_FORMAT_INTEGER_KIND_DECIMAL)
    {
        format_value = string_format_u128_parts_mask(format_value, bit_width);
    }
    else if (decimal_negative)
    {
        format_value = string_format_u128_parts_negate(format_value);
    }

    char8 integer_format_buffer[BUSTER_FORMAT_INTEGER_BUFFER_LENGTH];
    String8 number_buffer = BUSTER_ARRAY_TO_SLICE(integer_format_buffer);
    String8 digit_buffer = decimal_negative ? (String8){.pointer = number_buffer.pointer + 1, .length = number_buffer.length - 1} : number_buffer;
    String8 digits = string_format_u128_parts_radix(digit_buffer, format_value, string_format_integer_radix(options.kind),
                                                    options.kind == STRING_FORMAT_INTEGER_KIND_HEXADECIMAL_UPPER);
    String8 sign = {0};
    if (decimal_negative)
    {
        integer_format_buffer[0] = '-';
        sign = (String8){.pointer = integer_format_buffer, .length = 1};
    }

    u64 number_length = sign.length + digits.length;
    u64 maximum_width = string_format_integer_max_width(bit_width, options.kind);
    u64 width = options.width ? (options.width_natural_extension ? maximum_width : options.width) : 0;
    u64 width_character_count = width > number_length ? width - number_length : 0;

    if (options.prefix)
    {
        char8 prefix[] = {'0', string_format_integer_prefix_character(options.kind)};
        arena_append_string(arena, (String8)BUSTER_ARRAY_TO_SLICE(prefix));
    }

    if (sign.length)
    {
        arena_append_string(arena, sign);
    }
    if (width_character_count)
    {
        string_append_repeated_code_unit(arena, options.width_character, width_character_count);
    }
    string_append_integer_digits(arena, digits, options.kind, options.digit_group);
}

BUSTER_GLOBAL_LOCAL void arena_append_string(Arena* arena, String8 string)
{
    // An empty String8 legitimately carries a null pointer, and memcpy is
    // declared nonnull even for a zero count.
    if (!string.length)
    {
        return;
    }
    char8* destination = arena_allocate(arena, char8, string.length);
    memcpy(destination, string.pointer, sizeof(char8) * string.length);
}

BUSTER_GLOBAL_LOCAL void string_append_repeated_code_unit(Arena* arena, char8 code_unit, u64 code_unit_count)
{
    if (code_unit_count != 0)
    {
        char8* destination = arena_allocate(arena, char8, code_unit_count);
        for (u64 i = 0; i < code_unit_count; i += 1)
        {
            destination[i] = code_unit;
        }
    }
}

BUSTER_GLOBAL_LOCAL bool utf8_code_unit_is_continuation(u8 code_unit)
{
    return (code_unit & 0xC0u) == 0x80u;
}

BUSTER_GLOBAL_LOCAL Utf8DecodeResult utf8_decode(String8 string, u64 index)
{
    Utf8DecodeResult result = {
        .code_point = (u8)string.pointer[index],
        .advance = 1,
    };

    u8 first = (u8)string.pointer[index];

    if ((first & 0x80u) == 0)
    {
    }
    else if ((first & 0xE0u) == 0xC0u && index + 1 < string.length)
    {
        u8 second = (u8)string.pointer[index + 1];
        if (utf8_code_unit_is_continuation(second))
        {
            u32 code_point = ((u32)(first & 0x1Fu) << 6) | (u32)(second & 0x3Fu);
            if (code_point >= 0x80u)
            {
                result.code_point = code_point;
                result.advance = 2;
            }
        }
    }
    else if ((first & 0xF0u) == 0xE0u && index + 2 < string.length)
    {
        u8 second = (u8)string.pointer[index + 1];
        u8 third = (u8)string.pointer[index + 2];
        if (utf8_code_unit_is_continuation(second) && utf8_code_unit_is_continuation(third))
        {
            u32 code_point = ((u32)(first & 0x0Fu) << 12) | ((u32)(second & 0x3Fu) << 6) | (u32)(third & 0x3Fu);
            if (code_point >= 0x800u && !(code_point >= 0xD800u && code_point <= 0xDFFFu))
            {
                result.code_point = code_point;
                result.advance = 3;
            }
        }
    }
    else if ((first & 0xF8u) == 0xF0u && index + 3 < string.length)
    {
        u8 second = (u8)string.pointer[index + 1];
        u8 third = (u8)string.pointer[index + 2];
        u8 fourth = (u8)string.pointer[index + 3];
        if (utf8_code_unit_is_continuation(second) && utf8_code_unit_is_continuation(third) && utf8_code_unit_is_continuation(fourth))
        {
            u32 code_point = ((u32)(first & 0x07u) << 18) | ((u32)(second & 0x3Fu) << 12) | ((u32)(third & 0x3Fu) << 6) | (u32)(fourth & 0x3Fu);
            if (code_point >= 0x10000u && code_point <= 0x10FFFFu)
            {
                result.code_point = code_point;
                result.advance = 4;
            }
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL u64 utf16_write_code_point(char16* destination, u32 code_point)
{
    if (code_point <= 0xFFFFu)
    {
        destination[0] = (char16)code_point;
        return 1;
    }

    code_point -= 0x10000u;
    destination[0] = (char16)(0xD800u + (code_point >> 10));
    destination[1] = (char16)(0xDC00u + (code_point & 0x3FFu));
    return 2;
}

BUSTER_GLOBAL_LOCAL Utf8Result utf8_from_code_point(u32 code_point)
{
    Utf8Result result = {0};

    if (code_point > 0x10FFFFu || (code_point >= 0xD800u && code_point <= 0xDFFFu))
    {
        code_point = 0xFFFDu;
    }

    if (code_point <= 0x7Fu)
    {
        result.buffer[0] = (char8)code_point;
        result.count = 1;
    }
    else if (code_point <= 0x7FFu)
    {
        result.buffer[0] = (char8)(0xC0u | (code_point >> 6));
        result.buffer[1] = (char8)(0x80u | (code_point & 0x3Fu));
        result.count = 2;
    }
    else if (code_point <= 0xFFFFu)
    {
        result.buffer[0] = (char8)(0xE0u | (code_point >> 12));
        result.buffer[1] = (char8)(0x80u | ((code_point >> 6) & 0x3Fu));
        result.buffer[2] = (char8)(0x80u | (code_point & 0x3Fu));
        result.count = 3;
    }
    else
    {
        result.buffer[0] = (char8)(0xF0u | (code_point >> 18));
        result.buffer[1] = (char8)(0x80u | ((code_point >> 12) & 0x3Fu));
        result.buffer[2] = (char8)(0x80u | ((code_point >> 6) & 0x3Fu));
        result.buffer[3] = (char8)(0x80u | (code_point & 0x3Fu));
        result.count = 4;
    }

    return result;
}

String8 string_format_va(Arena* arena, String8 format, va_list variable_arguments)
{
    u64 original_position = arena->position;
    u64 format_index = 0;

    while (format_index < format.length)
    {
        char8 format_character = format.pointer[format_index];
        if (format_character == '{' && format_index + 1 < format.length && format.pointer[format_index + 1] == '{')
        {
            *arena_allocate(arena, char8, 1) = '{';
            format_index += 2;
        }
        else if (format_character == '}')
        {
            *arena_allocate(arena, char8, 1) = '}';
            format_index += format_index + 1 < format.length && format.pointer[format_index + 1] == '}' ? 2 : 1;
        }
        else if (format_character != '{')
        {
            *arena_allocate(arena, char8, 1) = format_character;
            format_index += 1;
        }
        else
        {
            u64 right_brace_index = format_index + 1;
            while (right_brace_index < format.length && format.pointer[right_brace_index] != '}')
            {
                if (format.pointer[right_brace_index] == '{')
                {
                    os_fail();
                }
                right_brace_index += 1;
            }

            if (right_brace_index >= format.length)
            {
                os_fail();
            }

            String8 format_body = string_slice(format, format_index + 1, right_brace_index);

            typedef enum FormatTypeId
            {
                FORMAT_TYPE_STRING_SLICE,
                FORMAT_TYPE_STRING_OS_LIST,
                FORMAT_TYPE_STRING8,
                FORMAT_TYPE_STRING16,
                FORMAT_TYPE_CHAR_OS,
                FORMAT_TYPE_CHAR8,
                FORMAT_TYPE_UNSIGNED_INTEGER_8,
                FORMAT_TYPE_UNSIGNED_INTEGER_16,
                FORMAT_TYPE_UNSIGNED_INTEGER_32,
                FORMAT_TYPE_UNSIGNED_INTEGER_64,
                FORMAT_TYPE_UNSIGNED_INTEGER_128,
                FORMAT_TYPE_SIGNED_INTEGER_8,
                FORMAT_TYPE_SIGNED_INTEGER_16,
                FORMAT_TYPE_SIGNED_INTEGER_32,
                FORMAT_TYPE_SIGNED_INTEGER_64,
                FORMAT_TYPE_SIGNED_INTEGER_128,
                FORMAT_TYPE_OS_ERROR,
                FORMAT_TYPE_COUNT,
            } FormatTypeId;

            String8 possible_format_strings[] = {
                [FORMAT_TYPE_STRING_SLICE] = S8("[]S8"),
                [FORMAT_TYPE_STRING_OS_LIST] = S8("SOsL"),
                [FORMAT_TYPE_STRING8] = S8("S8"),
                [FORMAT_TYPE_STRING16] = S8("S16"),
                [FORMAT_TYPE_CHAR_OS] = S8("CharOs"),
                [FORMAT_TYPE_CHAR8] = S8("char8"),
                [FORMAT_TYPE_UNSIGNED_INTEGER_8] = S8("u8"),
                [FORMAT_TYPE_UNSIGNED_INTEGER_16] = S8("u16"),
                [FORMAT_TYPE_UNSIGNED_INTEGER_32] = S8("u32"),
                [FORMAT_TYPE_UNSIGNED_INTEGER_64] = S8("u64"),
                [FORMAT_TYPE_UNSIGNED_INTEGER_128] = S8("u128"),
                [FORMAT_TYPE_SIGNED_INTEGER_8] = S8("s8"),
                [FORMAT_TYPE_SIGNED_INTEGER_16] = S8("s16"),
                [FORMAT_TYPE_SIGNED_INTEGER_32] = S8("s32"),
                [FORMAT_TYPE_SIGNED_INTEGER_64] = S8("s64"),
                [FORMAT_TYPE_SIGNED_INTEGER_128] = S8("s128"),
                [FORMAT_TYPE_OS_ERROR] = S8("EOs"),
            };
            BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(possible_format_strings) == FORMAT_TYPE_COUNT);

            u64 colon_index = BUSTER_STRING_NO_MATCH;
            for (u64 body_index = 0; body_index < format_body.length; body_index += 1)
            {
                if (format_body.pointer[body_index] == ':')
                {
                    colon_index = body_index;
                    break;
                }
            }

            bool has_modifiers = colon_index != BUSTER_STRING_NO_MATCH;
            u64 type_name_length = has_modifiers ? colon_index : format_body.length;
            String8 type_name = string_slice(format_body, 0, type_name_length);
            u64 format_string_i = 0;
            while (format_string_i < BUSTER_ARRAY_LENGTH(possible_format_strings) &&
                   !string_equal(type_name, possible_format_strings[format_string_i]))
            {
                format_string_i += 1;
            }

            if (format_string_i >= BUSTER_ARRAY_LENGTH(possible_format_strings))
            {
                os_fail();
            }

            FormatTypeId format_type_id = (FormatTypeId)format_string_i;
            bool integer_type = (format_type_id >= FORMAT_TYPE_UNSIGNED_INTEGER_8 && format_type_id <= FORMAT_TYPE_SIGNED_INTEGER_128);
            bool prefix = false;
            bool prefix_set = false;
            bool digit_group = false;
            u64 width = 0;
            char8 width_character = '0';
            bool width_natural_extension = false;
            StringFormatIntegerKind integer_format_kind = STRING_FORMAT_INTEGER_KIND_DECIMAL;
            bool integer_format_set = false;

            if (has_modifiers)
            {
                u64 modifier_index = colon_index + 1;
                if (modifier_index >= format_body.length)
                {
                    os_fail();
                }

                while (modifier_index < format_body.length)
                {
                    u64 name_start = modifier_index;
                    while (modifier_index < format_body.length && format_body.pointer[modifier_index] != ',' &&
                           format_body.pointer[modifier_index] != '=')
                    {
                        modifier_index += 1;
                    }

                    String8 format_name = string_slice(format_body, name_start, modifier_index);
                    bool has_equal = modifier_index < format_body.length && format_body.pointer[modifier_index] == '=';

                    if (string_equal(format_name, S8("width")))
                    {
                        if (!has_equal)
                        {
                            os_fail();
                        }

                        modifier_index += 1;
                        if (modifier_index >= format_body.length || format_body.pointer[modifier_index] != '[')
                        {
                            os_fail();
                        }
                        modifier_index += 1;

                        if (modifier_index >= format_body.length || (format_body.pointer[modifier_index] != '0' && format_body.pointer[modifier_index] != ' '))
                        {
                            os_fail();
                        }
                        width_character = format_body.pointer[modifier_index];
                        modifier_index += 1;

                        if (modifier_index >= format_body.length || format_body.pointer[modifier_index] != ',')
                        {
                            os_fail();
                        }
                        modifier_index += 1;

                        u64 width_start = modifier_index;
                        while (modifier_index < format_body.length && format_body.pointer[modifier_index] != ']')
                        {
                            modifier_index += 1;
                        }
                        if (modifier_index >= format_body.length)
                        {
                            os_fail();
                        }

                        String8 width_string = string_slice(format_body, width_start, modifier_index);
                        if (width_string.length == 1 && width_string.pointer[0] == 'x')
                        {
                            width_natural_extension = true;
                            width = BUSTER_FORMAT_INTEGER_MAX_WIDTH;
                        }
                        else
                        {
                            IntegerParsingU64 width_parsing = string_parse_u64_decimal(width_string.pointer);
                            if (width_string.length == 0 || width_parsing.length != width_string.length || width_parsing.value == 0)
                            {
                                os_fail();
                            }
                            width = width_parsing.value;
                        }

                        modifier_index += 1;
                        if (modifier_index < format_body.length)
                        {
                            if (format_body.pointer[modifier_index] != ',')
                            {
                                os_fail();
                            }
                            modifier_index += 1;
                            if (modifier_index >= format_body.length)
                            {
                                os_fail();
                            }
                        }
                    }
                    else
                    {
                        if (has_equal || format_name.length == 0)
                        {
                            os_fail();
                        }

                        if (string_equal(format_name, S8("d")))
                        {
                            integer_format_kind = STRING_FORMAT_INTEGER_KIND_DECIMAL;
                            integer_format_set = true;
                        }
                        else if (string_equal(format_name, S8("X")))
                        {
                            integer_format_kind = STRING_FORMAT_INTEGER_KIND_HEXADECIMAL_UPPER;
                            integer_format_set = true;
                        }
                        else if (string_equal(format_name, S8("x")))
                        {
                            integer_format_kind = STRING_FORMAT_INTEGER_KIND_HEXADECIMAL_LOWER;
                            integer_format_set = true;
                        }
                        else if (string_equal(format_name, S8("o")))
                        {
                            integer_format_kind = STRING_FORMAT_INTEGER_KIND_OCTAL;
                            integer_format_set = true;
                        }
                        else if (string_equal(format_name, S8("b")))
                        {
                            integer_format_kind = STRING_FORMAT_INTEGER_KIND_BINARY;
                            integer_format_set = true;
                        }
                        else if (string_equal(format_name, S8("no_prefix")))
                        {
                            prefix = false;
                            prefix_set = true;
                        }
                        else if (string_equal(format_name, S8("digit_group")))
                        {
                            digit_group = true;
                        }
                        else
                        {
                            os_fail();
                        }

                        if (modifier_index < format_body.length)
                        {
                            if (format_body.pointer[modifier_index] != ',')
                            {
                                os_fail();
                            }
                            modifier_index += 1;
                            if (modifier_index >= format_body.length)
                            {
                                os_fail();
                            }
                        }
                    }
                }

                if (!integer_type)
                {
                    os_fail();
                }

                if (!prefix_set && integer_format_set)
                {
                    prefix = true;
                }
                if (!prefix_set && width && width_character == ' ')
                {
                    prefix = false;
                }
            }

            if (width > BUSTER_FORMAT_INTEGER_MAX_WIDTH)
            {
                width = BUSTER_FORMAT_INTEGER_MAX_WIDTH;
            }

            format_index = right_brace_index + 1;

            switch (format_type_id)
            {
                break;
            case FORMAT_TYPE_STRING_SLICE:
            {
                SliceString8 strings = va_arg(variable_arguments, SliceString8);
                for (u64 string_index = 0; string_index < strings.length; string_index += 1)
                {
                    if (string_index != 0)
                    {
                        *arena_allocate(arena, char8, 1) = ' ';
                    }
                    arena_append_string(arena, strings.pointer[string_index]);
                }
            }
            break;
            case FORMAT_TYPE_STRING_OS_LIST:
            {
                StringOsList string_os_list = va_arg(variable_arguments, StringOsList);
                TemporalArena list_scratch = scratch_begin(&arena, 1);
#if defined(_WIN32)
                SliceString8 strings = slice_string_from_windows_string_list(list_scratch.arena, string_os_list);
#else
                SliceString8 strings = slice_string_from_posix_string_list(list_scratch.arena, string_os_list);
#endif
                for (u64 string_index = 0; string_index < strings.length; string_index += 1)
                {
                    if (string_index != 0)
                    {
                        *arena_allocate(arena, char8, 1) = ' ';
                    }
                    arena_append_string(arena, strings.pointer[string_index]);
                }
                scratch_end(list_scratch);
            }
            break;
            case FORMAT_TYPE_STRING8:
            {
                String8 string = va_arg(variable_arguments, String8);
                arena_append_string(arena, string);
            }
            break;
            case FORMAT_TYPE_STRING16:
            {
                String16 string16 = va_arg(variable_arguments, String16);
                string8_from_string16(arena, string16, false);
            }
            break;
            case FORMAT_TYPE_CHAR_OS:
            {
#if defined(_WIN32)
                u32 code_point = (u32)(u16)va_arg(variable_arguments, int);
                Utf8Result encoding = utf8_from_code_point(code_point);
                arena_append_string(arena, (String8){.pointer = encoding.buffer, .length = encoding.count});
#else
                char8 character = (char8)va_arg(variable_arguments, int);
                *arena_allocate(arena, char8, 1) = character;
#endif
            }
            break;
            case FORMAT_TYPE_CHAR8:
            {
                char8 character = (char8)va_arg(variable_arguments, int);
                *arena_allocate(arena, char8, 1) = character;
            }
            break;
            case FORMAT_TYPE_UNSIGNED_INTEGER_8:
            case FORMAT_TYPE_UNSIGNED_INTEGER_16:
            case FORMAT_TYPE_UNSIGNED_INTEGER_32:
            case FORMAT_TYPE_UNSIGNED_INTEGER_64:
            case FORMAT_TYPE_UNSIGNED_INTEGER_128:
            case FORMAT_TYPE_SIGNED_INTEGER_8:
            case FORMAT_TYPE_SIGNED_INTEGER_16:
            case FORMAT_TYPE_SIGNED_INTEGER_32:
            case FORMAT_TYPE_SIGNED_INTEGER_64:
            case FORMAT_TYPE_SIGNED_INTEGER_128:
            {
                StringFormatU128Parts value = {0};
                u64 bit_width = 0;
                bool signed_value = false;
                bool negative = false;

                switch (format_type_id)
                {
                    break;
                case FORMAT_TYPE_UNSIGNED_INTEGER_8:
                    value.low = (u8)va_arg(variable_arguments, int);
                    bit_width = 8;
                    break;
                case FORMAT_TYPE_UNSIGNED_INTEGER_16:
                    value.low = (u16)va_arg(variable_arguments, int);
                    bit_width = 16;
                    break;
                case FORMAT_TYPE_UNSIGNED_INTEGER_32:
                    value.low = va_arg(variable_arguments, u32);
                    bit_width = 32;
                    break;
                case FORMAT_TYPE_UNSIGNED_INTEGER_64:
                    value.low = va_arg(variable_arguments, u64);
                    bit_width = 64;
                    break;
                case FORMAT_TYPE_UNSIGNED_INTEGER_128:
                {
                    u128 argument = va_arg(variable_arguments, u128);
                    value = string_format_u128_parts_from_u128(argument);
                    bit_width = 128;
                }
                break;
                case FORMAT_TYPE_SIGNED_INTEGER_8:
                {
                    s8 argument = (s8)va_arg(variable_arguments, int);
                    signed_value = true;
                    negative = argument < 0;
                    value.low = (u64)(s64)argument;
                    value.high = negative ? UINT64_MAX : 0;
                    bit_width = 8;
                }
                break;
                case FORMAT_TYPE_SIGNED_INTEGER_16:
                {
                    s16 argument = (s16)va_arg(variable_arguments, int);
                    signed_value = true;
                    negative = argument < 0;
                    value.low = (u64)(s64)argument;
                    value.high = negative ? UINT64_MAX : 0;
                    bit_width = 16;
                }
                break;
                case FORMAT_TYPE_SIGNED_INTEGER_32:
                {
                    s32 argument = va_arg(variable_arguments, s32);
                    signed_value = true;
                    negative = argument < 0;
                    value.low = (u64)(s64)argument;
                    value.high = negative ? UINT64_MAX : 0;
                    bit_width = 32;
                }
                break;
                case FORMAT_TYPE_SIGNED_INTEGER_64:
                {
                    s64 argument = va_arg(variable_arguments, s64);
                    signed_value = true;
                    negative = argument < 0;
                    value.low = (u64)argument;
                    value.high = negative ? UINT64_MAX : 0;
                    bit_width = 64;
                }
                break;
                case FORMAT_TYPE_SIGNED_INTEGER_128:
                {
                    s128 argument = va_arg(variable_arguments, s128);
                    value = string_format_u128_parts_from_s128(argument);
                    signed_value = true;
                    negative = (value.high >> 63) != 0;
                    bit_width = 128;
                }
                break;
                default:
                    BUSTER_UNREACHABLE();
                }

                string_append_formatted_integer(arena, value, bit_width, signed_value, negative,
                                                (StringFormatIntegerOptions){
                                                    .kind = integer_format_kind,
                                                    .prefix = prefix,
                                                    .digit_group = digit_group,
                                                    .width_natural_extension = width_natural_extension,
                                                    .width = width,
                                                    .width_character = width_character,
                                                });
            }
            break;
            case FORMAT_TYPE_OS_ERROR:
            {
                OsError os_error = va_arg(variable_arguments, OsError);
                string8_from_os_error(arena, os_error, false);
            }
            break;
            case FORMAT_TYPE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
    }

    return (String8){.pointer = (char8*)((u8*)arena + original_position), .length = (arena->position - original_position) / sizeof(char8)};
}

String8 string_duplicate_arena(Arena* arena, String8 string, bool zero_terminate)
{
    String8 result = {.pointer = arena_allocate(arena, char8, string.length + zero_terminate), .length = string.length};
    memcpy(result.pointer, string.pointer, sizeof(char8) * string.length);

    if (zero_terminate)
    {
        result.pointer[string.length] = 0;
    }

    return result;
}

SliceString8 string16_environment_block_to_slice_string(Arena* arena, const char16* environment_block)
{
    SliceString8 result = {0};
    u64 string_count = 0;

    if (!environment_block)
    {
        return result;
    }

    // Windows environment blocks are NUL-separated strings terminated by an extra NUL.
    for (const char16* it = environment_block; *it; it += string16_length(it) + 1)
    {
        string_count += 1;
    }

    String8* slices = arena_allocate(arena, String8, string_count);
    const char16* it = environment_block;
    for (u64 i = 0; i < string_count; i += 1)
    {
        u64 length = string16_length(it);
        slices[i] = string8_from_string16(arena, string16_from_pointer_length(it, length), true);
        it += length + 1;
    }

    result = (SliceString8){.pointer = slices, .length = string_count};
    return result;
}

String8 string_format(Arena* arena, String8 format, ...)
{
    va_list variable_arguments;
    va_start(variable_arguments, format);
    String8 result = string_format_va(arena, format, variable_arguments);
    va_end(variable_arguments);

    return result;
}

String8 string_format_z(Arena* arena, String8 format, ...)
{
    va_list variable_arguments;
    va_start(variable_arguments, format);
    String8 result = string_format_va(arena, format, variable_arguments);
    va_end(variable_arguments);
    *arena_allocate(arena, char8, 1) = 0;

    return result;
}

void string_write_to_file_va(OsFileDescriptor* file_handle, String8 format, va_list variable_arguments)
{
    if (file_handle)
    {
        TemporalArena scratch = scratch_begin(0, 0);
        String8 string = string_format_va(scratch.arena, format, variable_arguments);

        if (string.length)
        {
            *arena_allocate(scratch.arena, char8, 1) = 0;
            os_file_write(file_handle, BUSTER_SLICE_TO_BYTE_SLICE(string));
        }

        scratch_end(scratch);
    }
}

void string_print(String8 format, ...)
{
    va_list variable_arguments;
    va_start(variable_arguments, format);
    string_write_to_file_va(os_get_stdout(), format, variable_arguments);
    va_end(variable_arguments);
}

u64 string_first_sequence(String8 s, String8 sub)
{
    u64 result = BUSTER_STRING_NO_MATCH;

    if (sub.length == 0)
    {
        result = 0;
    }
    else if (s.length >= sub.length)
    {
        u64 end = s.length - sub.length + 1;
        for (u64 i = 0; i < end; i += 1)
        {
            String8 chunk = string_slice(s, i, i + sub.length);
            if (string_equal(chunk, sub))
            {
                result = i;
                break;
            }
        }
    }

    return result;
}

StringOsListIterator string_os_list_iterator_initialize(StringOsList list)
{
    return (StringOsListIterator){
        .list = list,
    };
}

u64 string8_length(const char8* pointer)
{
    u64 result = 0;

    if (pointer)
    {
#if __has_builtin(__builtin_strlen)
        result = __builtin_strlen(pointer);
#else
        result = strlen(pointer);
#endif
    }

    return result;
}

u64 string16_length(const char16* s)
{
    const char16* restrict it = s;
    while (*it)
    {
        it += 1;
    }
    return (u64)(it - s);
}

String8 string_from_pointer(const char8* pointer)
{
    return (String8){.pointer = (char8*)pointer, .length = string8_length(pointer)};
}

BUSTER_TEST_F_DECL String16 string16_from_pointer(const char16* pointer)
{
    return (String16){.pointer = (char16*)pointer, .length = string16_length(pointer)};
}

PosixStringList posix_string_list_from_slice_string(Arena* arena, SliceString8 parts)
{
    PosixChar** list = arena_allocate(arena, PosixChar*, parts.length + 1);

    for (u64 i = 0; i < parts.length; i += 1)
    {
        list[i] = parts.pointer[i].pointer;
    }

    list[parts.length] = 0;

    return list;
}

PosixStringList posix_environment_from_keys_and_values(Arena* arena, SliceString8 keys, SliceString8 values)
{
    BUSTER_CHECK(keys.length == values.length);

    // Always return a valid NULL-terminated array, even for zero keys: a NULL PosixStringList
    // (as opposed to an array containing just the terminator) is not a valid execve()/posix_spawn() envp.
    PosixStringList result = arena_allocate(arena, char8*, keys.length + 1);

    for (u64 i = 0; i < keys.length; i += 1)
    {
        String8 key = keys.pointer[i];
        String8 value = values.pointer[i];

        String8 parts[] = {
            key,
            S8("="),
            value,
        };

        result[i] = string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(parts), true).pointer;
    }

    result[keys.length] = 0;

    return result;
}

WindowsStringList windows_environment_from_keys_and_values(Arena* arena, SliceString8 keys, SliceString8 values)
{
    // Always return a valid, non-NULL environment block, even for zero keys: passing a NULL
    // lpEnvironment to CreateProcessW means "inherit the caller's full environment", which is
    // the opposite of an explicitly empty environment.
    WindowsStringList result = arena_get_pointer_at_position_align(arena, char16, arena->position);

    for (u64 i = 0; i < keys.length; i += 1)
    {
        String8 key = keys.pointer[i];
        String8 value = values.pointer[i];

        string16_from_string8(arena, key, false);
        *arena_allocate(arena, char16, 1) = '=';
        string16_from_string8(arena, value, false);
        *arena_allocate(arena, char16, 1) = 0;
    }

    *arena_allocate(arena, char16, 1) = 0;

    return result;
}

SliceString8 slice_string_from_posix_string_list(Arena* arena, PosixStringList string_list)
{
    SliceString8 result = {0};
    u64 string_count = 0;

    if (string_list)
    {
        while (string_list[string_count])
        {
            string_count += 1;
        }
    }

    if (string_count)
    {
        String8* strings = arena_allocate(arena, String8, string_count);
        for (u64 i = 0; i < string_count; i += 1)
        {
            strings[i] = string_from_pointer(string_list[i]);
        }

        result = (SliceString8){.pointer = strings, .length = string_count};
    }

    return result;
}

SliceString8 slice_string_from_windows_string_list(Arena* arena, WindowsStringList command_line)
{
    SliceString8 result = {0};
    if (!command_line)
    {
        return result;
    }

    u64 command_line_length = string16_length(command_line);
    String8* strings = arena_allocate(arena, String8, command_line_length + 1);
    u64 string_count = 0;

    for (u64 i = 0; i < command_line_length;)
    {
        while (i < command_line_length && (command_line[i] == ' ' || command_line[i] == '\t'))
        {
            i += 1;
        }

        if (i >= command_line_length)
        {
            break;
        }

        char16* argument = arena_allocate(arena, char16, command_line_length - i + 1);
        u64 argument_length = 0;
        bool in_quotes = false;

        while (i < command_line_length)
        {
            char16 c = command_line[i];
            if (!in_quotes && (c == ' ' || c == '\t'))
            {
                break;
            }

            if (c == '\\')
            {
                u64 backslash_count = 0;
                while (i < command_line_length && command_line[i] == '\\')
                {
                    backslash_count += 1;
                    i += 1;
                }

                if (i < command_line_length && command_line[i] == '"')
                {
                    for (u64 backslash_i = 0; backslash_i < backslash_count / 2; backslash_i += 1)
                    {
                        argument[argument_length] = '\\';
                        argument_length += 1;
                    }

                    if (backslash_count & 1)
                    {
                        argument[argument_length] = '"';
                        argument_length += 1;
                    }
                    else
                    {
                        in_quotes = !in_quotes;
                    }
                    i += 1;
                }
                else
                {
                    for (u64 backslash_i = 0; backslash_i < backslash_count; backslash_i += 1)
                    {
                        argument[argument_length] = '\\';
                        argument_length += 1;
                    }
                }
            }
            else if (c == '"')
            {
                in_quotes = !in_quotes;
                i += 1;
            }
            else
            {
                argument[argument_length] = c;
                argument_length += 1;
                i += 1;
            }
        }

        argument[argument_length] = 0;
        strings[string_count] = string8_from_string16(arena, (String16){.pointer = argument, .length = argument_length}, true);
        string_count += 1;
    }

    result = (SliceString8){.pointer = strings, .length = string_count};
    return result;
}

String16 string16_from_string8(Arena* arena, String8 string, bool null_terminate)
{
    char16* pointer = arena_allocate(arena, char16, string.length + null_terminate);
    u64 result_length = 0;

    for (u64 i = 0; i < string.length;)
    {
        Utf8DecodeResult decoded = utf8_decode(string, i);
        result_length += utf16_write_code_point(pointer + result_length, decoded.code_point);
        i += decoded.advance;
    }

    if (null_terminate)
    {
        pointer[result_length] = 0;
    }

    String16 result = (String16){.pointer = pointer, .length = result_length};
    return result;
}

BUSTER_GLOBAL_LOCAL bool windows_command_line_argument_needs_quotes(String8 string)
{
    bool result = string.length == 0;
    for (u64 i = 0; i < string.length; i += 1)
    {
        char8 c = string.pointer[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '"')
        {
            result = true;
            break;
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void append_byte_repeated(Arena* arena, char8 byte, u64 count)
{
    if (count)
    {
        char8* destination = arena_allocate(arena, char8, count);
        for (u64 i = 0; i < count; i += 1)
        {
            destination[i] = byte;
        }
    }
}

BUSTER_GLOBAL_LOCAL String8 windows_command_line_quote_argument(Arena* arena, String8 string)
{
    u64 position = arena->position;
    *arena_allocate(arena, char8, 1) = '"';

    u64 backslash_count = 0;
    for (u64 i = 0; i < string.length; i += 1)
    {
        char8 c = string.pointer[i];
        if (c == '\\')
        {
            backslash_count += 1;
        }
        else if (c == '"')
        {
            append_byte_repeated(arena, '\\', backslash_count * 2 + 1);
            *arena_allocate(arena, char8, 1) = '"';
            backslash_count = 0;
        }
        else
        {
            append_byte_repeated(arena, '\\', backslash_count);
            *arena_allocate(arena, char8, 1) = c;
            backslash_count = 0;
        }
    }

    append_byte_repeated(arena, '\\', backslash_count * 2);
    *arena_allocate(arena, char8, 1) = '"';

    String8 result = string_from_pointer_length((char8*)arena_get_byte_pointer_at_position(arena, position), arena->position - position);
    return result;
}

WindowsStringList windows_string_list_from_slice_string(Arena* arena, SliceString8 strings)
{
    TemporalArena temp = scratch_begin(&arena, 1);
    char16* result = arena_get_current_pointer(arena, char16);

    for (u64 string_i = 0; string_i < strings.length; string_i += 1)
    {
        if (string_i > 0)
        {
            *arena_allocate(arena, char16, 1) = ' ';
        }

        String8 string8 = strings.pointer[string_i];
        if (windows_command_line_argument_needs_quotes(string8))
        {
            string8 = windows_command_line_quote_argument(temp.arena, string8);
            string16_from_string8(arena, string8, false);
        }
        else
        {
            string16_from_string8(arena, string8, false);
        }
    }

    *arena_allocate(arena, char16, 1) = 0;
    scratch_end(temp);
    return result;
}

WindowsStringList windows_environment_block_from_slice_string(Arena* arena, SliceString8 environment)
{
    char16* result = arena_get_current_pointer(arena, char16);
    for (u64 environment_i = 0; environment_i < environment.length; environment_i += 1)
    {
        string16_from_string8(arena, environment.pointer[environment_i], false);
        *arena_allocate(arena, char16, 1) = 0;
    }

    *arena_allocate(arena, char16, 1) = 0;
    return result;
}

char** slice_string8_to_null_terminated_array_char(Arena* arena, SliceString8 strings)
{
    char** result = arena_allocate(arena, char*, strings.length + 1);
    for (u64 i = 0; i < strings.length; i += 1)
    {
        result[i] = string_duplicate_arena(arena, strings.pointer[i], true).pointer;
    }
    result[strings.length] = 0;
    return result;
}

OsArgumentBuilder os_argument_builder_start(Arena* arena)
{
    OsArgumentBuilder result = {
        .arena = arena,
        .position = align_forward(arena->position, BUSTER_ALIGN_OF(String8)),
    };
    return result;
}

void os_argument_builder_append(OsArgumentBuilder* builder, String8 string)
{
    *arena_allocate(builder->arena, String8, 1) = string;
}

SliceString8 os_argument_builder_flush(OsArgumentBuilder* const builder)
{
    u64 original_position = builder->position;
    u64 current_position = builder->arena->position;
    u64 byte_count = current_position - original_position;
    BUSTER_CHECK(byte_count % sizeof(String8) == 0);
    u64 argument_count = byte_count / sizeof(String8);
    String8* pointer = arena_get_pointer_at_position_align(builder->arena, String8, builder->position);
    SliceString8 result = {.pointer = pointer, .length = argument_count};
    return result;
}
