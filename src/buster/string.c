#include <buster/string.h>
#include <buster/arena.h>
#include <buster/os.h>
#include <buster/integer.h>

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
    return ((accumulator) * 16 + (code_unit) - (code_unit_is_decimal(code_unit) ? '0' : (code_unit_is_hexadecimal_alpha_upper(code_unit) ? ('A' - 10) : code_unit_is_hexadecimal_alpha_lower(code_unit) ? ('a' - 10) : 0)));
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

    return (IntegerParsingU64){ .value = value, .length = i };
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

    return (IntegerParsingU64){ .value = value, .length = i };
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

    return (IntegerParsingU64) { .value = value, .length = i };
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

    return (IntegerParsingU64){ .value = value, .length = i };
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
    return (String8){ .pointer = (slice).pointer + (start), .length = (end) - (start) };
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

    return (String8){ .pointer = pointer, .length = length };
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
    return (String8){ .pointer = (char8*)pointer, .length = length };
}

String16 string16_from_pointer_length(const char16* pointer, u64 length)
{
    return (String16){ .pointer = (char16*)pointer, .length = length };
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
    char8* restrict pointer = s.pointer;
    for (u64 i = 0, reverse_i = s.length - 1; i < reverse_i; i += 1, reverse_i -= 1)
    {
        char8 ch = pointer[i];
        pointer[i] = pointer[reverse_i];
        pointer[reverse_i] = ch;
    }
}

BUSTER_GLOBAL_LOCAL String8 string_format_u64_hexadecimal(String8 buffer, u64 value, bool upper)
{
    String8 result = {0};

    if (value == 0)
    {
        buffer.pointer[0] = '0';
        result = (String8){ .pointer = buffer.pointer, .length = 1};
    }
    else
    {
        u64 v = value;
        u64 i = 0;
        char8 alpha_start = upper ? 'A' : 'a';

        while (v != 0)
        {
            u64 digit = v % 16;
            char8 ch = (char8)(digit > 9 ? (digit - 10 + alpha_start) : (digit + '0'));
            BUSTER_CHECK(i < buffer.length);
            buffer.pointer[i] = ch;
            i += 1;
            v = v / 16;
        }

        u64 length = i;

        result = (String8){ .pointer = buffer.pointer, .length = length };
        string_reverse(result);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL String8 string_format_i64_decimal(String8 buffer, u64 value, bool treat_as_signed)
{
    String8 result = {0};

    if (value == 0)
    {
        buffer.pointer[0] = '0';
        result = (String8){ buffer.pointer, 1};
    }
    else
    {
        u64 i = treat_as_signed;

        buffer.pointer[0] = '-';
        u64 v = value;

        while (v != 0)
        {
            u64 digit = v % 10;
            char8 ch = (char8)(digit + '0');
            BUSTER_CHECK(i < buffer.length);
            buffer.pointer[i] = ch;
            i += 1;
            v = v / 10;
        }

        u64 length = i;

        result = (String8){ buffer.pointer + treat_as_signed, length - treat_as_signed };
        string_reverse(result);
        result.pointer -= treat_as_signed;
        result.length += treat_as_signed;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL u64 string_format_s64_decimal_magnitude(s64 value)
{
    u64 result = (u64)value;
    if (value < 0)
    {
        result = 0 - result;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 string_format_u64_octal(String8 buffer, u64 value)
{
    String8 result = {0};

    if (value == 0)
    {
        buffer.pointer[0] = '0';
        result = (String8){ .pointer = buffer.pointer, .length = 1 };
    }
    else
    {
        u64 i = 0;
        u64 v = value;

        while (v != 0)
        {
            u64 digit = v % 8;
            char8 ch = (char8)(digit + '0');
            BUSTER_CHECK(i < buffer.length);
            buffer.pointer[i] = ch;
            i += 1;
            v = v / 8;
        }

        u64 length = i;

        result = (String8){ .pointer = buffer.pointer, .length = length };
        string_reverse(result);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL String8 string_format_u64_binary(String8 buffer, u64 value)
{
    String8 result = {0};

    if (value == 0)
    {
        buffer.pointer[0] = '0';
        result = (String8){ .pointer = buffer.pointer, .length = 1};
    }
    else
    {
        u64 i = 0;
        u64 v = value;

        while (v != 0)
        {
            u64 digit = v % 2;
            char8 ch = (char8)(digit + '0');
            BUSTER_CHECK(i < buffer.length);
            buffer.pointer[i] = ch;
            i += 1;
            v = v / 2;
        }

        u64 length = i;

        result = (String8){ .pointer = buffer.pointer, .length = length };
        string_reverse(result);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void arena_append_string(Arena* arena, String8 string)
{
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
        bool escaped_left_brace = format_index + 1 < format.length && format.pointer[format_index] == '{' && format.pointer[format_index + 1] == '{';
        bool escaped_right_brace = format_index + 1 < format.length && format.pointer[format_index] == '}' && format.pointer[format_index + 1] == '}';

        if (escaped_left_brace || escaped_right_brace)
        {
            *arena_allocate(arena, char8, 1) = format.pointer[format_index];
            format_index += 2;
        }
        else if (format.pointer[format_index] != '{')
        {
            *arena_allocate(arena, char8, 1) = format.pointer[format_index];
            format_index += 1;
        }
        else
        {
            // '{' is found
            String8 iteration_left_format_string = string_slice(format, format_index, format.length);
            String8 iteration_left_format_string_plus_one = string_slice(iteration_left_format_string, 1, iteration_left_format_string.length);
            u64 left_brace_index = string_first_code_unit(iteration_left_format_string_plus_one, '{');
            u64 right_brace_index = string_first_code_unit(iteration_left_format_string, '}');

            bool has_right_brace = right_brace_index != BUSTER_STRING_NO_MATCH;
            bool nested_left_brace_before_right_brace = left_brace_index != BUSTER_STRING_NO_MATCH && right_brace_index > left_brace_index;

            if (has_right_brace && !nested_left_brace_before_right_brace)
            {
                String8 whole_format_string = string_slice(iteration_left_format_string, 0, right_brace_index + 1);
                format_index += whole_format_string.length;

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

                u64 first_format = string_first_code_unit(whole_format_string, ':');
                bool there_is_format_modifiers = first_format != BUSTER_STRING_NO_MATCH;
                u64 this_format_string_length = there_is_format_modifiers ? first_format : whole_format_string.length - 1; // Avoid final right brace
                String8 this_format_string = string_slice(whole_format_string,
                        1, // Avoid starting left brace
                        this_format_string_length);

                u64 format_string_i;
                for (format_string_i = 0; format_string_i < BUSTER_ARRAY_LENGTH(possible_format_strings); format_string_i += 1)
                {
                    String8 possible_format_string = possible_format_strings[format_string_i];
                    if (string_equal(this_format_string, possible_format_string))
                    {
                        break;
                    }
                }

                typedef enum IntegerFormatKind
                {
                    INTEGER_FORMAT_KIND_DECIMAL,
                    INTEGER_FORMAT_KIND_BINARY,
                    INTEGER_FORMAT_KIND_OCTAL,
                    INTEGER_FORMAT_KIND_HEXADECIMAL_LOWER,
                    INTEGER_FORMAT_KIND_HEXADECIMAL_UPPER,
                    INTEGER_FORMAT_KIND_COUNT,
                } IntegerFormatKind;

                typedef enum IntegerFormatSpecifier
                {
                    INTEGER_FORMAT_SPECIFIER_D,
                    INTEGER_FORMAT_SPECIFIER_X_UPPER,
                    INTEGER_FORMAT_SPECIFIER_X_LOWER,
                    INTEGER_FORMAT_SPECIFIER_O,
                    INTEGER_FORMAT_SPECIFIER_B,
                    INTEGER_FORMAT_SPECIFIER_WIDTH,
                    INTEGER_FORMAT_SPECIFIER_NO_PREFIX,
                    INTEGER_FORMAT_SPECIFIER_DIGIT_GROUP,
                    INTEGER_FORMAT_SPECIFIER_COUNT,
                } IntegerFormatSpecifier;

                FormatTypeId format_type_id = (FormatTypeId)format_string_i;
                bool prefix = false;
                bool prefix_set = false;
                bool digit_group = false;
                u64 width = 0;
                char8 width_character = '0';
                bool width_natural_extension = false;
#define BUSTER_FORMAT_INTEGER_MAX_WIDTH (u64)(64)

                IntegerFormatKind integer_format_kind = INTEGER_FORMAT_KIND_DECIMAL;
                bool integer_format_set = false;

                if (there_is_format_modifiers)
                {
                    String8 possible_format_specifier_strings[] = {
                        [INTEGER_FORMAT_SPECIFIER_D] = S8("d"),
                        [INTEGER_FORMAT_SPECIFIER_X_UPPER] = S8("X"),
                        [INTEGER_FORMAT_SPECIFIER_X_LOWER] = S8("x"),
                        [INTEGER_FORMAT_SPECIFIER_O] = S8("o"),
                        [INTEGER_FORMAT_SPECIFIER_B] = S8("b"),
                        [INTEGER_FORMAT_SPECIFIER_WIDTH] = S8("width"),
                        [INTEGER_FORMAT_SPECIFIER_NO_PREFIX] = S8("no_prefix"),
                        [INTEGER_FORMAT_SPECIFIER_DIGIT_GROUP] = S8("digit_group"),
                    };
                    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(possible_format_specifier_strings) == (u64)INTEGER_FORMAT_SPECIFIER_COUNT);

                    String8 whole_format_specifiers_string = string_slice(whole_format_string, first_format + 1, whole_format_string.length - 1);
                    BUSTER_CHECK(whole_format_specifiers_string.length <= whole_format_string.length);
                    u64 format_specifier_string_i = 0;

                    while (format_specifier_string_i < whole_format_specifiers_string.length && whole_format_specifiers_string.pointer[format_specifier_string_i] != '}')
                    {
                        String8 iteration_left_format_specifiers_string = BUSTER_SLICE_START(whole_format_specifiers_string, format_specifier_string_i);
                        BUSTER_CHECK(iteration_left_format_specifiers_string.length <= whole_format_specifiers_string.length);
                        u64 equal_index = string_first_code_unit(iteration_left_format_specifiers_string, '=');
                        u64 comma_index = string_first_code_unit(iteration_left_format_specifiers_string, ',');
                        u64 format_specifier_name_end = BUSTER_MIN(equal_index, comma_index);
                        bool string_left = format_specifier_name_end == BUSTER_STRING_NO_MATCH;
                        format_specifier_name_end = string_left ? iteration_left_format_specifiers_string.length : format_specifier_name_end;
                        char8 next_character = string_left ? 0 : (equal_index < comma_index ? '=' : ',');

                        String8 format_name = string_slice(iteration_left_format_specifiers_string, 0, format_specifier_name_end);
                        format_specifier_string_i += format_name.length + !string_left;
                        String8 left_format_specifiers_string = BUSTER_SLICE_START(iteration_left_format_specifiers_string, format_name.length + !string_left);
                        BUSTER_CHECK(left_format_specifiers_string.length <= iteration_left_format_specifiers_string.length);

                        IntegerFormatSpecifier format_i;
                        for (format_i = 0; format_i < INTEGER_FORMAT_SPECIFIER_COUNT; format_i += 1)
                        {
                            String8 candidate_format_specifier = possible_format_specifier_strings[(u64)format_i];
                            if (string_equal(format_name, candidate_format_specifier))
                            {
                                break;
                            }
                        }

                        IntegerFormatSpecifier format_specifier = (IntegerFormatSpecifier)format_i;
                        switch (format_specifier)
                        {
                            break; case INTEGER_FORMAT_SPECIFIER_D:
                            {
                                integer_format_kind = INTEGER_FORMAT_KIND_DECIMAL;
                                integer_format_set = true;
                            }
                            break; case INTEGER_FORMAT_SPECIFIER_X_UPPER:
                            {
                                integer_format_kind = INTEGER_FORMAT_KIND_HEXADECIMAL_UPPER;
                                integer_format_set = true;
                            }
                            break; case INTEGER_FORMAT_SPECIFIER_X_LOWER:
                            {
                                integer_format_kind = INTEGER_FORMAT_KIND_HEXADECIMAL_LOWER;
                                integer_format_set = true;
                            }
                            break; case INTEGER_FORMAT_SPECIFIER_O:
                            {
                                integer_format_kind = INTEGER_FORMAT_KIND_OCTAL;
                                integer_format_set = true;
                            }
                            break; case INTEGER_FORMAT_SPECIFIER_B:
                            {
                                integer_format_kind = INTEGER_FORMAT_KIND_BINARY;
                                integer_format_set = true;
                            }
                            break; case INTEGER_FORMAT_SPECIFIER_WIDTH:
                            {
                                if (next_character == '=')
                                {
                                    if (left_format_specifiers_string.pointer[0] == '[')
                                    {
                                        width_character = left_format_specifiers_string.pointer[1];

                                        if (left_format_specifiers_string.pointer[2] == ',')
                                        {
                                            u64 right_bracket_index = string_first_code_unit(left_format_specifiers_string, ']');

                                            if (right_bracket_index != BUSTER_STRING_NO_MATCH)
                                            {
                                                u64 width_start = 3;
                                                String8 width_count_string = string_slice(left_format_specifiers_string, width_start, right_bracket_index);
                                                u64 character_to_advance_count = right_bracket_index + 1;

                                                bool success = false;

                                                if (width_count_string.length == 1 && width_count_string.pointer[0] == 'x')
                                                {
                                                    width_natural_extension = true;
                                                    success = true;
                                                    width = BUSTER_FORMAT_INTEGER_MAX_WIDTH;
                                                }
                                                else
                                                {
                                                    IntegerParsingU64 width_count_parsing = string_parse_u64_decimal(width_count_string.pointer);

                                                    if (width_count_parsing.length == width_count_string.length && width_count_parsing.value != 0)
                                                    {
                                                        width = width_count_parsing.value;

                                                        bool more_characters = right_bracket_index + 1 < left_format_specifiers_string.length;
                                                        if (more_characters)
                                                        {
                                                            char8 next_ch = left_format_specifiers_string.pointer[character_to_advance_count];
                                                            if (next_ch == ',')
                                                            {
                                                                character_to_advance_count += 1;
                                                                success = true;
                                                            }
                                                            else
                                                            {
                                                                os_fail();
                                                            }
                                                        }
                                                        else
                                                        {
                                                            success = true;
                                                        }

                                                        if (!success)
                                                        {
                                                            os_fail();
                                                        }
                                                    }
                                                }

                                                if (success)
                                                {
                                                    format_specifier_string_i += character_to_advance_count;
                                                }
                                                else
                                                {
                                                    os_fail();
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            break; case INTEGER_FORMAT_SPECIFIER_NO_PREFIX:
                            {
                                prefix = false;
                                prefix_set = true;
                            }
                            break; case INTEGER_FORMAT_SPECIFIER_DIGIT_GROUP:
                            {
                                digit_group = true;
                            }
                            break; case INTEGER_FORMAT_SPECIFIER_COUNT:
                            {
                            }
                        }
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

                switch (format_type_id)
                {
                    break; case FORMAT_TYPE_STRING_SLICE:
                    {
                        SliceString8 strings = va_arg(variable_arguments, SliceString8);
                        if (strings.length)
                        {
                            for (u64 i = 0; i < strings.length; i += 1)
                            {
                                String8 string = strings.pointer[i];
                                arena_append_string(arena, string);
                                *arena_allocate(arena, char8, 1) = ' ';
                            }

                            arena->position -= 1;
                        }
                    }
                    break; case FORMAT_TYPE_STRING_OS_LIST:
                    {
                        BUSTER_TODO();
                        // StringOsList string_os_list = va_arg(variable_arguments, StringOsList);
                        // StringOsListIterator it = string_os_list_iterator_initialize(string_os_list);
                        //
                        // StringOs string_os;
                        //
                        // it = string_os_list_iterator_initialize(string_os_list);
                        // while ((string_os = string_os_list_iterator_next(&it)).pointer)
                        // {
                        //     string8_duplicate_from_string_os(arena, string_os, false);
                        //     *arena_allocate(arena, char8, 1) = ' ';
                        // }
                        //
                        // // Remove trailing space
                        // arena->position -= 1;
                    }
                    // break; case FORMAT_TYPE_STRING_OS:
                    // {
                    //     BUSTER_TODO();
                    //     // StringOs string = va_arg(variable_arguments, StringOs);
                    //     // string8_duplicate_from_string_os(arena, string, false);
                    // }
                    break; case FORMAT_TYPE_STRING8:
                    {
                        String8 string = va_arg(variable_arguments, String8);
                        arena_append_string(arena, string);
                    }
                    break; case FORMAT_TYPE_STRING16:
                    {
                        String16 string16 = va_arg(variable_arguments, String16);
                        BUSTER_UNUSED(string16);
                        BUSTER_TODO();
                        // string_append_slice_different<Char, char16>(arena, string16);
                    }
                    break; case FORMAT_TYPE_CHAR_OS:
                    {
                        CharOs os_char = (CharOs)va_arg(variable_arguments, int);
                        *arena_allocate(arena, char8, 1) = (char8)os_char;
                    }
                    break; case FORMAT_TYPE_CHAR8:
                    {
                        char8 ch = (char8)va_arg(variable_arguments, int);
                        *arena_allocate(arena, char8, 1) = ch;
                    }
                    break; case FORMAT_TYPE_UNSIGNED_INTEGER_8: case FORMAT_TYPE_UNSIGNED_INTEGER_16: case FORMAT_TYPE_UNSIGNED_INTEGER_32: case FORMAT_TYPE_UNSIGNED_INTEGER_64:
                    {
                        prefix = prefix && integer_format_kind != INTEGER_FORMAT_KIND_COUNT;

                        char8 prefix_second_character;
                        switch (integer_format_kind)
                        {
                            break; case INTEGER_FORMAT_KIND_DECIMAL: prefix_second_character = 'd';
                            break; case INTEGER_FORMAT_KIND_BINARY: prefix_second_character = 'b';
                            break; case INTEGER_FORMAT_KIND_OCTAL: prefix_second_character = 'o';
                            break; case INTEGER_FORMAT_KIND_HEXADECIMAL_LOWER: case INTEGER_FORMAT_KIND_HEXADECIMAL_UPPER: prefix_second_character = 'x';
                            break; case INTEGER_FORMAT_KIND_COUNT: BUSTER_UNREACHABLE();
                        }

                        char8 prefix_buffer[] =
                        {
                            '0',
                            prefix_second_character,
                        };

                        if (integer_format_kind == INTEGER_FORMAT_KIND_COUNT)
                        {
                            integer_format_kind = INTEGER_FORMAT_KIND_DECIMAL;
                        }

                        u64 value;
                        u64 value_size;
                        switch (format_type_id)
                        {
                            break; case FORMAT_TYPE_UNSIGNED_INTEGER_8:
                            {
                                value = (u8)va_arg(variable_arguments, u32);
                                value_size = sizeof(u8);
                            }
                            break; case FORMAT_TYPE_UNSIGNED_INTEGER_16:
                            {
                                value = (u16)va_arg(variable_arguments, u32);
                                value_size = sizeof(u16);
                            }
                            break; case FORMAT_TYPE_UNSIGNED_INTEGER_32:
                            {
                                value = va_arg(variable_arguments, u32);
                                value_size = sizeof(u32);
                            }
                            break; case FORMAT_TYPE_UNSIGNED_INTEGER_64:
                            {
                                value = va_arg(variable_arguments, u64);
                                value_size = sizeof(u64);
                            }
                            break; default: BUSTER_UNREACHABLE();
                        }

                        // let prefix_character_count = (u64)prefix << 1;
                        char8 integer_format_buffer[(sizeof(u64) * 8) + BUSTER_FORMAT_INTEGER_MAX_WIDTH + 2];
                        String8 number_string_buffer = BUSTER_ARRAY_TO_SLICE(integer_format_buffer);

                        String8 format_result;

                        switch (integer_format_kind)
                        {
                            break; case INTEGER_FORMAT_KIND_DECIMAL: format_result = string_format_i64_decimal(number_string_buffer, value, false);
                            break; case INTEGER_FORMAT_KIND_BINARY: format_result = string_format_u64_binary(number_string_buffer, value);
                            break; case INTEGER_FORMAT_KIND_OCTAL: format_result = string_format_u64_octal(number_string_buffer, value);
                            break; case INTEGER_FORMAT_KIND_HEXADECIMAL_LOWER: case INTEGER_FORMAT_KIND_HEXADECIMAL_UPPER: format_result = string_format_u64_hexadecimal(number_string_buffer, value, integer_format_kind == INTEGER_FORMAT_KIND_HEXADECIMAL_UPPER);
                            break; case INTEGER_FORMAT_KIND_COUNT: BUSTER_UNREACHABLE();
                        }

                        number_string_buffer.length = format_result.length;

                        u64 integer_max_width = 0;

                        u64 digit_group_character_count;

                        switch (integer_format_kind)
                        {
                            break; case INTEGER_FORMAT_KIND_DECIMAL:
                            {
                                switch (format_type_id)
                                {
                                    break; case FORMAT_TYPE_UNSIGNED_INTEGER_8: integer_max_width = 3;
                                    break; case FORMAT_TYPE_UNSIGNED_INTEGER_16: integer_max_width = 5;
                                    break; case FORMAT_TYPE_UNSIGNED_INTEGER_32: integer_max_width = 10;
                                    break; case FORMAT_TYPE_UNSIGNED_INTEGER_64: integer_max_width = 20;
                                    break; default: BUSTER_UNREACHABLE();
                                }
                                digit_group_character_count = 3;
                            }
                            break; case INTEGER_FORMAT_KIND_BINARY:
                            {
                                integer_max_width = value_size * 8;
                                digit_group_character_count = 8;
                            }
                            break; case INTEGER_FORMAT_KIND_OCTAL:
                            {
                                switch (format_type_id)
                                {
                                    break; case FORMAT_TYPE_UNSIGNED_INTEGER_8: integer_max_width = 3;
                                    break; case FORMAT_TYPE_UNSIGNED_INTEGER_16: integer_max_width = 6;
                                    break; case FORMAT_TYPE_UNSIGNED_INTEGER_32: integer_max_width = 11;
                                    break; case FORMAT_TYPE_UNSIGNED_INTEGER_64: integer_max_width = 22;
                                    break; default: BUSTER_UNREACHABLE();
                                }
                                digit_group_character_count = 3;
                            }
                            break; case INTEGER_FORMAT_KIND_HEXADECIMAL_LOWER: case INTEGER_FORMAT_KIND_HEXADECIMAL_UPPER:
                            {
                                integer_max_width = value_size * 2;
                                digit_group_character_count = 2;
                            }
                            break; case INTEGER_FORMAT_KIND_COUNT: BUSTER_UNREACHABLE();
                        }

                        width = width ? (width_natural_extension ? integer_max_width : width) : 0;

                        u64 width_character_count = width ? (width > number_string_buffer.length ? (width - number_string_buffer.length) : 0) : 0;
                        bool separator_characters = digit_group && digit_group_character_count && number_string_buffer.length > digit_group_character_count;
                        u64 separator_character_count = separator_characters ? (number_string_buffer.length / digit_group_character_count) + (number_string_buffer.length % digit_group_character_count != 0) - 1: 0;

                        // TODO: allocate only once?
                        // u64 character_to_write_count = prefix_character_count + width_character_count + number_string_buffer.length + separator_character_count;
                        {
                            if (prefix)
                            {
                                arena_append_string(arena, (String8)BUSTER_ARRAY_TO_SLICE(prefix_buffer));
                            }

                            if (width_character_count)
                            {
                                string_append_repeated_code_unit(arena, width_character, width_character_count);
                            }

                            if (separator_character_count)
                            {
                                char8 separator_character = integer_format_kind == INTEGER_FORMAT_KIND_DECIMAL ? '.' : '_';
                                u64 remainder = number_string_buffer.length % digit_group_character_count;
                                if (remainder)
                                {
                                    arena_append_string(arena, (String8){ .pointer = number_string_buffer.pointer, .length = remainder });
                                    *arena_allocate(arena, char8, 1) = separator_character;
                                }

                                u64 source_i;
                                for (source_i = remainder; source_i < number_string_buffer.length - digit_group_character_count; source_i += digit_group_character_count)
                                {
                                    arena_append_string(arena, (String8){ .pointer = number_string_buffer.pointer + source_i, .length = digit_group_character_count });
                                    *arena_allocate(arena, char8, 1) = separator_character;
                                }

                                arena_append_string(arena, (String8){ .pointer = number_string_buffer.pointer + number_string_buffer.length - digit_group_character_count, .length = digit_group_character_count });
                            }
                            else
                            {
                                arena_append_string(arena, number_string_buffer);
                            }
                        }
                    }
                    break; case FORMAT_TYPE_SIGNED_INTEGER_8: case FORMAT_TYPE_SIGNED_INTEGER_16: case FORMAT_TYPE_SIGNED_INTEGER_32: case FORMAT_TYPE_SIGNED_INTEGER_64:
                    {
                        if (integer_format_kind == INTEGER_FORMAT_KIND_COUNT)
                        {
                            integer_format_kind = INTEGER_FORMAT_KIND_DECIMAL;
                        }

                        s64 value;
                        switch (format_type_id)
                        {
                            break; case FORMAT_TYPE_SIGNED_INTEGER_8: value = (s8)va_arg(variable_arguments, int);
                            break; case FORMAT_TYPE_SIGNED_INTEGER_16: value = (s16)va_arg(variable_arguments, int);
                            break; case FORMAT_TYPE_SIGNED_INTEGER_32: value = va_arg(variable_arguments, s32);
                            break; case FORMAT_TYPE_SIGNED_INTEGER_64: value = va_arg(variable_arguments, s64);
                            break; default: BUSTER_UNREACHABLE();
                        }

                        char8 integer_format_buffer[sizeof(u64) * 8 + 1]; // 1 for the sign (needed?)
                        String8 string_buffer = BUSTER_ARRAY_TO_SLICE(integer_format_buffer);
                        String8 format_result;

                        switch (integer_format_kind)
                        {
                            break; case INTEGER_FORMAT_KIND_DECIMAL: format_result = string_format_i64_decimal(string_buffer, string_format_s64_decimal_magnitude(value), value < 0);
                            break; case INTEGER_FORMAT_KIND_BINARY: format_result = string_format_u64_binary(string_buffer, (u64)value);
                            break; case INTEGER_FORMAT_KIND_OCTAL: format_result = string_format_u64_octal(string_buffer, (u64)value);
                            break; case INTEGER_FORMAT_KIND_HEXADECIMAL_LOWER: case INTEGER_FORMAT_KIND_HEXADECIMAL_UPPER: format_result = string_format_u64_hexadecimal(string_buffer, (u64)value, integer_format_kind == INTEGER_FORMAT_KIND_HEXADECIMAL_UPPER);
                            break; case INTEGER_FORMAT_KIND_COUNT: BUSTER_UNREACHABLE();
                        }

                        arena_append_string(arena, format_result);
                    }
                    break; case FORMAT_TYPE_UNSIGNED_INTEGER_128:
                    {
                        // TODO:
                    }
                    break; case FORMAT_TYPE_SIGNED_INTEGER_128:
                    {
                        // TODO:
                    }
                    break; case FORMAT_TYPE_OS_ERROR:
                    {
                        OsError os_error = va_arg(variable_arguments, OsError);
                        string8_from_os_error(arena, os_error, false);
                    }
                    break; case FORMAT_TYPE_COUNT:
                    {
                        // if (result.real_buffer_index < buffer_slice.length)
                        {
                            *arena_allocate(arena, char8, 1) = '{';
                        }

                        {
                            char8* pointer = arena_allocate(arena, char8, whole_format_string.length + 1);
                            memcpy(pointer, whole_format_string.pointer, sizeof(char8) * whole_format_string.length);
                            pointer[whole_format_string.length] = '}';
                        }
                    }
                }
            }
            else
            {
                BUSTER_TODO();
            }
        }
    }

    return (String8){ .pointer = (char8*)((u8*)arena + original_position), .length = (arena->position - original_position) / sizeof(char8) };
}

String8 string_duplicate_arena(Arena* arena, String8 string, bool zero_terminate)
{
    String8 result = { .pointer = arena_allocate(arena, char8, string.length + zero_terminate), .length = string.length };
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

    result = (SliceString8){ .pointer = slices, .length = string_count };
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

void string_print(String8 format, ...)
{
    TemporalArena scratch = scratch_begin(0, 0);
    va_list variable_arguments;
    va_start(variable_arguments, format);
    String8 string = string_format_va(scratch.arena, format, variable_arguments);
    va_end(variable_arguments);

    if (string.length)
    {
        *arena_allocate(scratch.arena, char8, 1) = 0;
        os_file_write(os_get_stdout(), BUSTER_SLICE_TO_BYTE_SLICE(string));
    }

    scratch_end(scratch);
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
    return (StringOsListIterator) {
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
    return (String8){ .pointer = (char8*)pointer, .length = string8_length(pointer) };
}

String16 string16_from_pointer(const char16* pointer)
{
    return (String16){ .pointer = (char16*)pointer, .length = string16_length(pointer) };
}

BUSTER_GLOBAL_LOCAL u64 raw_string16_first_code_unit(const char16* pointer, char16 code_unit)
{
    u64 result = BUSTER_STRING_NO_MATCH;

    if (pointer)
    {
        for (char16* it = (char16*)pointer; *it; it += 1)
        {
            if (*it == code_unit)
            {
                result = (u64)(it - pointer);
                break;
            }
        }
    }

    return result;
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
    PosixStringList result = {0};
    BUSTER_CHECK(keys.length == values.length);

    if (keys.length)
    {
        result = arena_allocate(arena, char8*, keys.length + 1);

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
    }

    return result;
}

WindowsStringList windows_environment_from_keys_and_values(Arena* arena, SliceString8 keys, SliceString8 values)
{
    WindowsStringList result = {0};

    if (keys.length)
    {
        result = arena_get_pointer_at_position_align(arena, char16, arena->position);

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
    }

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

        result = (SliceString8){ .pointer = strings, .length = string_count };
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
        strings[string_count] = string8_from_string16(arena, (String16){ .pointer = argument, .length = argument_length }, true);
        string_count += 1;
    }

    result = (SliceString8){ .pointer = strings, .length = string_count };
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

    String16 result = (String16) { .pointer = pointer, .length = result_length };
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
    SliceString8 result = { .pointer = pointer, .length = argument_count };
    return result;
}

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>

#if defined(_WIN32)
#define BUSTER_UNICODE_OS_TO_UTF8_TEST(args, arena_value, utf8_value, utf16_value) do \
{ \
    String8 unicode_utf8_from_os = string8_from_string16((arena_value), (utf16_value), true); \
    BUSTER_STRING_TEST((args), unicode_utf8_from_os, (utf8_value)); \
    BUSTER_TEST_RAW((args), unicode_utf8_from_os.pointer[unicode_utf8_from_os.length] == 0, S8("string8_duplicate_from_string_os did not write a terminator")); \
} while (0)
#else
#define BUSTER_UNICODE_OS_TO_UTF8_TEST(args, arena_value, utf8_value, utf16_value) do \
{ \
    BUSTER_UNUSED(utf16_value); \
    String8 unicode_utf8_from_os = (utf8_value); \
    BUSTER_STRING_TEST((args), unicode_utf8_from_os, (utf8_value)); \
    BUSTER_TEST_RAW((args), unicode_utf8_from_os.pointer[unicode_utf8_from_os.length] == 0, S8("string8_duplicate_from_string_os did not write a terminator")); \
} while (0)
#endif

#define BUSTER_UNICODE_ROUND_TRIP_TEST(args, arena_value, utf8_value, utf16_value) do \
{ \
    String8 unicode_utf8 = (utf8_value); \
    String16 unicode_expected_utf16 = (utf16_value); \
    String16 unicode_utf16 = string16_from_string8((arena_value), unicode_utf8, true); \
    BUSTER_TEST_RAW((args), string16_equal(unicode_utf16, unicode_expected_utf16), S8("string16_from_string8 Unicode decode mismatch")); \
    BUSTER_TEST_RAW((args), unicode_utf16.pointer[unicode_utf16.length] == 0, S8("string16_from_string8 did not write a terminator")); \
    String8 unicode_utf8_round_trip = string8_from_string16((arena_value), unicode_utf16, true); \
    BUSTER_STRING_TEST((args), unicode_utf8_round_trip, unicode_utf8); \
    BUSTER_TEST_RAW((args), unicode_utf8_round_trip.pointer[unicode_utf8_round_trip.length] == 0, S8("string16_to_string8_arena did not write a terminator")); \
    BUSTER_UNICODE_OS_TO_UTF8_TEST((args), (arena_value), unicode_utf8, unicode_expected_utf16); \
} while (0)

#define BUSTER_UTF16_TO_UTF8_TEST(args, arena_value, utf16_value, expected_utf8_value) do \
{ \
    String16 unicode_utf16 = (utf16_value); \
    String8 unicode_expected_utf8 = (expected_utf8_value); \
    String8 unicode_utf8 = string8_from_string16((arena_value), unicode_utf16, true); \
    BUSTER_STRING_TEST((args), unicode_utf8, unicode_expected_utf8); \
    BUSTER_TEST_RAW((args), unicode_utf8.pointer[unicode_utf8.length] == 0, S8("string16_to_string8_arena did not write a terminator")); \
} while (0)

UnitTestResult string_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    // string8_format
    {
        {
            String8 formatted = string_format(arena, S8("{{ {S8} }}"), S8("value"));
            BUSTER_STRING_TEST(arguments, formatted, S8("{ value }"));
        }
        {
            String8 formatted = string_format(arena, S8("async_thread_{u64}"), (u64)7);
            BUSTER_STRING_TEST(arguments, formatted, S8("async_thread_7"));
        }
        {
            String8 formatted = string_format(arena, S8("{s64}"), (s64)INT64_MIN);
            BUSTER_STRING_TEST(arguments, formatted, S8("-9223372036854775808"));
        }
        {
            char8 utf8_bytes[] = {
                'J', 'o', 's', (char8)0xC3, (char8)0xA9, '/',
                (char8)0xF0, (char8)0x9F, (char8)0x98, (char8)0x80, 0
            };
            char16 utf16_bytes[] = { 'J', 'o', 's', 0x00E9, '/', 0xD83D, 0xDE00, 0 };
            BUSTER_UNICODE_ROUND_TRIP_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 10), string16_from_pointer_length(utf16_bytes, 7));
        }
        {
            char8 utf8_bytes[] = {
                'A',
                (char8)0xC3, (char8)0xA9,
                (char8)0xE2, (char8)0x82, (char8)0xAC,
                (char8)0xF0, (char8)0x9F, (char8)0x98, (char8)0x80,
                0
            };
            char16 utf16_bytes[] = { 'A', 0x00E9, 0x20AC, 0xD83D, 0xDE00, 0 };
            BUSTER_UNICODE_ROUND_TRIP_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 10), string16_from_pointer_length(utf16_bytes, 5));
        }
        {
            char8 utf8_bytes[] = {
                '[',
                (char8)0xF4, (char8)0x8F, (char8)0xBF, (char8)0xBF,
                ']',
                0
            };
            char16 utf16_bytes[] = { '[', 0xDBFF, 0xDFFF, ']', 0 };
            BUSTER_UNICODE_ROUND_TRIP_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 6), string16_from_pointer_length(utf16_bytes, 4));
        }
        {
            char16 utf16_bytes[] = { 'A', 0xD83D, 'B', 0 };
            char8 utf8_bytes[] = { 'A', (char8)0xEF, (char8)0xBF, (char8)0xBD, 'B', 0 };
            BUSTER_UTF16_TO_UTF8_TEST(arguments, arena, string16_from_pointer_length(utf16_bytes, 3), string_from_pointer_length(utf8_bytes, 5));
        }
        {
            char16 utf16_bytes[] = { 'A', 0xDE00, 'B', 0 };
            char8 utf8_bytes[] = { 'A', (char8)0xEF, (char8)0xBF, (char8)0xBD, 'B', 0 };
            BUSTER_UTF16_TO_UTF8_TEST(arguments, arena, string16_from_pointer_length(utf16_bytes, 3), string_from_pointer_length(utf8_bytes, 5));
        }

        enum UnsignedFormatTestCase
        {
            UNSIGNED_FORMAT_TEST_CASE_DEFAULT,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL,
            UNSIGNED_FORMAT_TEST_CASE_BINARY,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP,

            UNSIGNED_FORMAT_TEST_CASE_COUNT,
        };

        // u8
        {
            typedef enum UnsignedTestCaseId
            {
                UNSIGNED_TEST_CASE_U8,
                UNSIGNED_TEST_CASE_U16,
                UNSIGNED_TEST_CASE_U32,
                UNSIGNED_TEST_CASE_U64,
                UNSIGNED_TEST_CASE_COUNT,
            } UnsignedTestCaseId;

#undef S8
#define S8(strlit) S8_INITIALIZER(strlit)

            BUSTER_GLOBAL_LOCAL const String8 format_strings[(u64)UNSIGNED_TEST_CASE_COUNT][(u64)UNSIGNED_FORMAT_TEST_CASE_COUNT] = {
                [(u64)UNSIGNED_TEST_CASE_U8] =
                {
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("{u8}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("{u8:d}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("{u8:x}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("{u8:X}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("{u8:o}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("{u8:b}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("{u8:no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("{u8:d,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("{u8:x,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("{u8:X,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("{u8:o,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("{u8:b,no_prefix}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("{u8:width=[ ,2]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("{u8:d,width=[ ,4]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("{u8:x,width=[ ,8]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("{u8:X,width=[ ,16]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("{u8:o,width=[ ,32]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("{u8:b,width=[ ,64]}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("{u8:width=[0,2]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("{u8:d,width=[0,4]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("{u8:x,width=[0,8]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("{u8:X,width=[0,16]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("{u8:o,width=[0,32]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("{u8:b,width=[0,64]}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("{u8:width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("{u8:d,width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("{u8:x,width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("{u8:X,width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("{u8:o,width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("{u8:b,width=[0,x]}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u8:width=[ ,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u8:d,width=[ ,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u8:x,width=[ ,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u8:X,width=[ ,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("{u8:o,width=[ ,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("{u8:b,width=[ ,x],no_prefix}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("{u8:width=[0,2],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("{u8:d,width=[0,4],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("{u8:x,width=[0,8],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("{u8:X,width=[0,16],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("{u8:o,width=[0,32],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("{u8:b,width=[0,64],no_prefix}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u8:width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u8:d,width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u8:x,width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u8:X,width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("{u8:o,width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("{u8:b,width=[0,x],no_prefix}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("{u8:digit_group}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("{u8:digit_group,d}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("{u8:digit_group,x}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("{u8:digit_group,X}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("{u8:digit_group,o}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("{u8:digit_group,b}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("{u8:digit_group,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("{u8:digit_group,no_prefix,d}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("{u8:digit_group,no_prefix,x}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("{u8:digit_group,no_prefix,X}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("{u8:digit_group,no_prefix,o}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("{u8:digit_group,no_prefix,b}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u8:digit_group,width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u8:digit_group,width=[0,x],d}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u8:digit_group,width=[0,x],x}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u8:digit_group,width=[0,x],X}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("{u8:digit_group,width=[0,x],o}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("{u8:digit_group,width=[0,x],b}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u8:digit_group,width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u8:digit_group,width=[0,x],d,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u8:digit_group,width=[0,x],x,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u8:digit_group,width=[0,x],X,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("{u8:digit_group,width=[0,x],o,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("{u8:digit_group,width=[0,x],b,no_prefix}"),
                },
                [(u64)UNSIGNED_TEST_CASE_U16] =
                {
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("{u16}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("{u16:d}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("{u16:x}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("{u16:X}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("{u16:o}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("{u16:b}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("{u16:no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("{u16:d,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("{u16:x,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("{u16:X,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("{u16:o,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("{u16:b,no_prefix}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("{u16:width=[ ,2]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("{u16:d,width=[ ,4]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("{u16:x,width=[ ,8]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("{u16:X,width=[ ,16]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("{u16:o,width=[ ,32]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("{u16:b,width=[ ,64]}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("{u16:width=[0,2]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("{u16:d,width=[0,4]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("{u16:x,width=[0,8]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("{u16:X,width=[0,16]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("{u16:o,width=[0,32]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("{u16:b,width=[0,64]}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("{u16:width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("{u16:d,width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("{u16:x,width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("{u16:X,width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("{u16:o,width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("{u16:b,width=[0,x]}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u16:width=[ ,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u16:d,width=[ ,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u16:x,width=[ ,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u16:X,width=[ ,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("{u16:o,width=[ ,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("{u16:b,width=[ ,x],no_prefix}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("{u16:width=[0,2],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("{u16:d,width=[0,4],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("{u16:x,width=[0,8],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("{u16:X,width=[0,16],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("{u16:o,width=[0,32],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("{u16:b,width=[0,64],no_prefix}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u16:width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u16:d,width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u16:x,width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u16:X,width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("{u16:o,width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("{u16:b,width=[0,x],no_prefix}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("{u16:digit_group}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("{u16:digit_group,d}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("{u16:digit_group,x}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("{u16:digit_group,X}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("{u16:digit_group,o}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("{u16:digit_group,b}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("{u16:digit_group,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("{u16:digit_group,no_prefix,d}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("{u16:digit_group,no_prefix,x}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("{u16:digit_group,no_prefix,X}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("{u16:digit_group,no_prefix,o}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("{u16:digit_group,no_prefix,b}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u16:digit_group,width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u16:digit_group,width=[0,x],d}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u16:digit_group,width=[0,x],x}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u16:digit_group,width=[0,x],X}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("{u16:digit_group,width=[0,x],o}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("{u16:digit_group,width=[0,x],b}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u16:digit_group,width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u16:digit_group,width=[0,x],d,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u16:digit_group,width=[0,x],x,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u16:digit_group,width=[0,x],X,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("{u16:digit_group,width=[0,x],o,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("{u16:digit_group,width=[0,x],b,no_prefix}"),
                },
                [(u64)UNSIGNED_TEST_CASE_U32] =
                {
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("{u32}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("{u32:d}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("{u32:x}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("{u32:X}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("{u32:o}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("{u32:b}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("{u32:no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("{u32:d,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("{u32:x,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("{u32:X,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("{u32:o,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("{u32:b,no_prefix}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("{u32:width=[ ,2]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("{u32:d,width=[ ,4]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("{u32:x,width=[ ,8]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("{u32:X,width=[ ,16]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("{u32:o,width=[ ,32]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("{u32:b,width=[ ,64]}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("{u32:width=[0,2]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("{u32:d,width=[0,4]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("{u32:x,width=[0,8]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("{u32:X,width=[0,16]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("{u32:o,width=[0,32]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("{u32:b,width=[0,64]}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("{u32:width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("{u32:d,width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("{u32:x,width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("{u32:X,width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("{u32:o,width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("{u32:b,width=[0,x]}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u32:width=[ ,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u32:d,width=[ ,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u32:x,width=[ ,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u32:X,width=[ ,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("{u32:o,width=[ ,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("{u32:b,width=[ ,x],no_prefix}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("{u32:width=[0,2],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("{u32:d,width=[0,4],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("{u32:x,width=[0,8],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("{u32:X,width=[0,16],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("{u32:o,width=[0,32],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("{u32:b,width=[0,64],no_prefix}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u32:width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u32:d,width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u32:x,width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u32:X,width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("{u32:o,width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("{u32:b,width=[0,x],no_prefix}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("{u32:digit_group}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("{u32:digit_group,d}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("{u32:digit_group,x}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("{u32:digit_group,X}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("{u32:digit_group,o}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("{u32:digit_group,b}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("{u32:digit_group,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("{u32:digit_group,no_prefix,d}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("{u32:digit_group,no_prefix,x}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("{u32:digit_group,no_prefix,X}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("{u32:digit_group,no_prefix,o}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("{u32:digit_group,no_prefix,b}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u32:digit_group,width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u32:digit_group,width=[0,x],d}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u32:digit_group,width=[0,x],x}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u32:digit_group,width=[0,x],X}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("{u32:digit_group,width=[0,x],o}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("{u32:digit_group,width=[0,x],b}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u32:digit_group,width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u32:digit_group,width=[0,x],d,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u32:digit_group,width=[0,x],x,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u32:digit_group,width=[0,x],X,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("{u32:digit_group,width=[0,x],o,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("{u32:digit_group,width=[0,x],b,no_prefix}"),
                },
                [(u64)UNSIGNED_TEST_CASE_U64] =
                {
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("{u64}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("{u64:d}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("{u64:x}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("{u64:X}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("{u64:o}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("{u64:b}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("{u64:no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("{u64:d,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("{u64:x,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("{u64:X,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("{u64:o,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("{u64:b,no_prefix}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("{u64:width=[ ,2]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("{u64:d,width=[ ,4]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("{u64:x,width=[ ,8]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("{u64:X,width=[ ,16]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("{u64:o,width=[ ,32]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("{u64:b,width=[ ,64]}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("{u64:width=[0,2]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("{u64:d,width=[0,4]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("{u64:x,width=[0,8]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("{u64:X,width=[0,16]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("{u64:o,width=[0,32]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("{u64:b,width=[0,64]}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("{u64:width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("{u64:d,width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("{u64:x,width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("{u64:X,width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("{u64:o,width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("{u64:b,width=[0,x]}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u64:width=[ ,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u64:d,width=[ ,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u64:x,width=[ ,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u64:X,width=[ ,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("{u64:o,width=[ ,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("{u64:b,width=[ ,x],no_prefix}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("{u64:width=[0,2],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("{u64:d,width=[0,4],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("{u64:x,width=[0,8],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("{u64:X,width=[0,16],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("{u64:o,width=[0,32],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("{u64:b,width=[0,64],no_prefix}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u64:width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u64:d,width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u64:x,width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u64:X,width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("{u64:o,width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("{u64:b,width=[0,x],no_prefix}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("{u64:digit_group}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("{u64:digit_group,d}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("{u64:digit_group,x}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("{u64:digit_group,X}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("{u64:digit_group,o}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("{u64:digit_group,b}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("{u64:digit_group,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("{u64:digit_group,no_prefix,d}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("{u64:digit_group,no_prefix,x}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("{u64:digit_group,no_prefix,X}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("{u64:digit_group,no_prefix,o}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("{u64:digit_group,no_prefix,b}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u64:digit_group,width=[0,x]}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u64:digit_group,width=[0,x],d}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u64:digit_group,width=[0,x],x}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u64:digit_group,width=[0,x],X}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("{u64:digit_group,width=[0,x],o}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("{u64:digit_group,width=[0,x],b}"),

                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u64:digit_group,width=[0,x],no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u64:digit_group,width=[0,x],d,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u64:digit_group,width=[0,x],x,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u64:digit_group,width=[0,x],X,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("{u64:digit_group,width=[0,x],o,no_prefix}"),
                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("{u64:digit_group,width=[0,x],b,no_prefix}"),
                },
            };

            // 0, 1, 2, 4, 8, 16, UINT_MAX / 2, UINT_MAX

            typedef struct UnsignedTestCase UnsignedTestCase;
            struct UnsignedTestCase
            {
                String8 expected_results[(u64)UNSIGNED_FORMAT_TEST_CASE_COUNT];
                u64 value;
            };

            enum UnsignedTestCaseNumber
            {
                UNSIGNED_TEST_CASE_NUMBER_ZERO,
                UNSIGNED_TEST_CASE_NUMBER_ONE,
                UNSIGNED_TEST_CASE_NUMBER_TWO,
                UNSIGNED_TEST_CASE_NUMBER_FOUR,
                UNSIGNED_TEST_CASE_NUMBER_EIGHT,
                UNSIGNED_TEST_CASE_NUMBER_SIXTEEN,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX,
                UNSIGNED_TEST_CASE_NUMBER_COUNT,
            };

            BUSTER_GLOBAL_LOCAL const UnsignedTestCase cases[(u64)UNSIGNED_TEST_CASE_COUNT][(u64)UNSIGNED_TEST_CASE_NUMBER_COUNT] =
            {
                [(u64)UNSIGNED_TEST_CASE_U8] =
                {
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_ZERO] =
                    {
                        .value = 0,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                               0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("00"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x00"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x00"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("  0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("       0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("00"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("00"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("00"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x00"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x00"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b00000000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("00000000"),
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_ONE] =
                    {
                        .value = 1,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                               1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("01"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x01"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x01"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000001"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("  1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("       1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("01"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("01"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("01"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000001"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x01"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x01"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b00000001"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("01"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("01"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("00000001"),
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_TWO] =
                    {
                        .value = 2,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                              10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("02"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x02"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x02"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("  2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("      10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("02"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("02"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("02"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x02"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x02"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b00000010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("02"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("02"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("00000010"),
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_FOUR] =
                    {
                        .value = 4,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                             100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("04"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x04"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x04"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("  4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("     100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("04"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("04"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("04"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x04"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x04"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b00000100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("04"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("04"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("00000100"),
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_EIGHT] =
                    {
                        .value = 8,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                              10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                            1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("08"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x08"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x08"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00001000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8(" 10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("    1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("08"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("08"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("08"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00001000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x08"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x08"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b00001000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("08"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("08"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("00001000"),
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_SIXTEEN] =
                    {
                        .value = 16,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("  16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                              20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                           10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00010000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8(" 16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8(" 16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8(" 20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("   10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00010000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b00010000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("00010000"),
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2] =
                    {
                        .value = UINT8_MAX / 2,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x7f"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x7F"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o177"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b1111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("7f"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("7F"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("177"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("1111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      7f"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              7F"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             177"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                         1111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x0000007f"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x000000000000007F"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000177"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000001111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x7f"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x7F"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o177"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b01111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("7f"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("7F"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("177"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8(" 1111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("0000007f"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("000000000000007F"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000177"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000001111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("7f"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("7F"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("177"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("01111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x7f"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x7F"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o177"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b1111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("7f"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("7F"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("177"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("1111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x7f"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x7F"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o177"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b01111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("127"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7f"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7F"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("177"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("01111111"),
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5] =
                    {
                        .value = UINT8_MAX - 5,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0xfa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0xFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o372"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b11111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("fa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("FA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("372"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("11111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      fa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              FA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             372"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                        11111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x000000fa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x00000000000000FA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000372"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000011111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0xfa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0xFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o372"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b11111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("fa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("FA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("372"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("11111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("000000fa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("00000000000000FA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000372"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000011111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("fa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("FA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("372"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("11111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0xfa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0xFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o372"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b11111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("fa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("FA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("372"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("11111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xfa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o372"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b11111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("250"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("fa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("372"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("11111010"),
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4] =
                    {
                        .value = UINT8_MAX - 4,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0xfb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0xFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o373"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b11111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("fb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("FB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("373"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("11111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      fb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              FB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             373"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                        11111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x000000fb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x00000000000000FB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000373"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000011111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0xfb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0xFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o373"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b11111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("fb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("FB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("373"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("11111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("000000fb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("00000000000000FB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000373"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000011111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("fb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("FB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("373"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("11111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0xfb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0xFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o373"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b11111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("fb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("FB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("373"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("11111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xfb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o373"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b11111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("251"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("fb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("373"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("11111011"),
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3] =
                    {
                        .value = UINT8_MAX - 3,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0xfc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0xFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o374"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b11111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("fc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("FC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("374"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("11111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      fc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              FC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             374"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                        11111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x000000fc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x00000000000000FC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000374"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000011111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0xfc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0xFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o374"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b11111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("fc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("FC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("374"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("11111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("000000fc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("00000000000000FC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000374"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000011111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("fc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("FC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("374"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("11111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0xfc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0xFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o374"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b11111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("fc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("FC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("374"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("11111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xfc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o374"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b11111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("252"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("fc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("374"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("11111100"),
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2] =
                    {
                        .value = UINT8_MAX - 2,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0xfd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0xFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o375"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b11111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("fd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("FD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("375"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("11111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      fd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              FD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             375"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                        11111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x000000fd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x00000000000000FD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000375"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000011111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0xfd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0xFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o375"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b11111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("fd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("FD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("375"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("11111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("000000fd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("00000000000000FD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000375"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000011111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("fd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("FD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("375"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("11111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0xfd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0xFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o375"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b11111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("fd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("FD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("375"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("11111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xfd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o375"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b11111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("253"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("fd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("375"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("11111101"),
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1] =
                    {
                        .value = UINT8_MAX - 1,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0xfe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0xFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o376"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b11111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("fe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("FE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("376"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("11111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      fe") ,
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              FE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             376"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                        11111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x000000fe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x00000000000000FE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000376"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000011111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0xfe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0xFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o376"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b11111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("fe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("FE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("376"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("11111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("000000fe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("00000000000000FE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000376"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000011111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("fe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("FE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("376"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("11111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0xfe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0xFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o376"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b11111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("fe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("FE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("376"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("11111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xfe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o376"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b11111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("254"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("fe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("376"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("11111110"),
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX] =
                    {
                        .value = UINT8_MAX,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0xff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0xFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o377"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("377"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             377"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                        11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x000000ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x00000000000000FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000377"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000011111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0xff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0xFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o377"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("377"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("000000ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("00000000000000FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000377"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000011111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("377"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0xff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0xFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o377"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("377"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o377"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("255"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("377"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("11111111"),
                        },
                    },
                },
                
// ==================== U16 ====================

                [(u64)UNSIGNED_TEST_CASE_U16] =
                {
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_ZERO] =
                    {
                        .value = 0,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                               0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("00"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("    0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("    0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("     0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("               0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("00"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_ONE] =
                    {
                        .value = 1,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                               1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("01"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000001"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("    1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("    1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("     1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("               1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("01"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000001"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000001"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000001")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_TWO] =
                    {
                        .value = 2,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                              10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("02"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("    2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("    2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("     2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("              10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("02"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000010")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_FOUR] =
                    {
                        .value = 4,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                             100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("04"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("    4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("    4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("     4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("             100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("04"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000100")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_EIGHT] =
                    {
                        .value = 8,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                              10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                            1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("08"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000001000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("    8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("    8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("    10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("            1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("08"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000001000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000001000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000001000")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_SIXTEEN] =
                    {
                        .value = 16,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("  16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("      10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("              10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                              20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                           10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o000020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000010000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("   16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("   16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("  10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("  10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("    20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("           10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("000020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000010000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o000020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000010000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("000020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000010000")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2] =
                    {
                        .value = UINT16_MAX / 2,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("32767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d32767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x7fff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x7FFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o77777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("32767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("32767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("7fff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("7FFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("77777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("32767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("32767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    7fff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            7FFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                           77777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                 111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("32767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d32767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00007fff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000007FFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000077777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("32767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d32767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x7fff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x7FFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o077777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("32767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("32767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("7fff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("7FFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8(" 77777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8(" 111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("32767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("32767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00007fff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000007FFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000077777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("32767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("32767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("7fff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("7FFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("077777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("32.767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d32.767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x7f_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x7F_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o77_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1111111_11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("32.767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("32.767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("7f_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("7F_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("77_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1111111_11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("32.767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d32.767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x7f_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x7F_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o077_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b01111111_11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("32.767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("32.767"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7f_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7F_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("077_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("01111111_11111111")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5] =
                    {
                        .value = UINT16_MAX - 5,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("65530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d65530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o177772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("65530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("65530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("177772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("65530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("65530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    fffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            FFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                          177772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                1111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("65530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d65530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x0000fffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000000000FFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000177772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000001111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("65530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d65530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o177772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("65530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("65530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("177772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("65530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("65530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("0000fffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000000000FFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000177772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000001111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("65530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("65530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("177772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("65.530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d65.530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_fa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o177_772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("65.530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("65.530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_fa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("177_772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("65.530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d65.530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_fa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o177_772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.530"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_fa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("177_772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111010")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4] =
                    {
                        .value = UINT16_MAX - 4,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("65531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d65531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o177773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("65531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("65531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("177773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("65531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("65531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    fffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            FFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                          177773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                1111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("65531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d65531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x0000fffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000000000FFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000177773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000001111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("65531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d65531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o177773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("65531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("65531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("177773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("65531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("65531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("0000fffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000000000FFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000177773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000001111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("65531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("65531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("177773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("65.531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d65.531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_fb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o177_773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("65.531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("65.531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_fb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("177_773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("65.531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d65.531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_fb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o177_773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.531"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_fb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("177_773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111011")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3] =
                    {
                        .value = UINT16_MAX - 3,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("65532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d65532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o177774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("65532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("65532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("177774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("65532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("65532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    fffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            FFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                          177774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                1111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("65532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d65532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x0000fffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000000000FFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000177774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000001111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("65532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d65532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o177774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("65532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("65532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("177774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("65532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("65532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("0000fffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000000000FFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000177774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000001111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("65532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("65532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("177774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("65.532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d65.532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_fc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o177_774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("65.532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("65.532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_fc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("177_774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("65.532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d65.532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_fc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o177_774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.532"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_fc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("177_774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111100")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2] =
                    {
                        .value = UINT16_MAX - 2,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("65533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d65533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o177775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("65533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("65533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("177775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("65533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("65533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    fffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            FFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                          177775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                1111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("65533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d65533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x0000fffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000000000FFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000177775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000001111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("65533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d65533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o177775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("65533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("65533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("177775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("65533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("65533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("0000fffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000000000FFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000177775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000001111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("65533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("65533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("177775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("65.533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d65.533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_fd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o177_775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("65.533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("65.533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_fd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("177_775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("65.533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d65.533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_fd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o177_775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.533"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_fd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("177_775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111101")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1] =
                    {
                        .value = UINT16_MAX - 1,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("65534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d65534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o177776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("65534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("65534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("177776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("65534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("65534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    fffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            FFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                          177776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                1111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("65534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d65534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x0000fffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000000000FFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000177776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000001111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("65534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d65534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o177776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("65534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("65534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("177776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("65534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("65534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("0000fffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000000000FFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000177776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000001111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("65534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("65534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("177776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("65.534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d65.534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_fe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o177_776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("65.534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("65.534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_fe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("177_776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("65.534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d65.534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_fe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o177_776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.534"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_fe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("177_776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111110")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX] =
                    {
                        .value = UINT16_MAX,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("65535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d65535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o177777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("65535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("65535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("ffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("177777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("65535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("65535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    ffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            FFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                          177777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                1111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("65535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d65535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x0000ffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000000000FFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000177777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000001111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("65535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d65535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o177777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("65535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("65535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("ffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("177777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("65535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("65535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("0000ffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000000000FFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000177777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000001111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("65535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("65535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("ffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("177777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("65.535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d65.535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o177_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("65.535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("65.535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("177_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("65.535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d65.535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o177_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.535"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("177_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111")
                        },
                    },
                },

// ==================== U32 ====================

                [(u64)UNSIGNED_TEST_CASE_U32] =
                {
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_ZERO] =
                    {
                        .value = 0,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                               0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("00"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("0000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d0000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x00000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x00000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o00000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b00000000000000000000000000000000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("         0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("         0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("          0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                               0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("00"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("00000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d0000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o00000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b00000000000000000000000000000000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("00000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("00000000000000000000000000000000")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_ONE] =
                    {
                        .value = 1,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                               1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("01"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("0000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d0000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x00000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x00000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o00000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b00000000000000000000000000000001"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("         1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("         1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("          1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                               1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("01"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("00000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000001"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d0000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o00000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b00000000000000000000000000000001"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("00000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("00000000000000000000000000000001")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_TWO] =
                    {
                        .value = 2,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                              10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("02"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("0000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d0000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x00000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x00000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o00000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b00000000000000000000000000000010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("         2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("         2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("          2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                              10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("02"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("00000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d0000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o00000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b00000000000000000000000000000010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("00000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("00000000000000000000000000000010")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_FOUR] =
                    {
                        .value = 4,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                             100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("04"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("0000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d0000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x00000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x00000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o00000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b00000000000000000000000000000100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("         4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("         4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("          4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                             100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("04"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("00000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d0000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o00000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b00000000000000000000000000000100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("00000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("00000000000000000000000000000100")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_EIGHT] =
                    {
                        .value = 8,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                              10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                            1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("08"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("0000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d0000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x00000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x00000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o00000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b00000000000000000000000000001000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("         8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("         8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("         10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                            1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("08"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("00000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000001000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d0000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o00000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b00000000000000000000000000001000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("00000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("00000000000000000000000000001000")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_SIXTEEN] =
                    {
                        .value = 16,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("  16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("      10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("              10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                              20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                           10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("0000000016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d0000000016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x00000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x00000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o00000000020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b00000000000000000000000000010000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("        16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("        16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("      10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("      10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("         20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                           10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("00000000020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000010000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0000000016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d0000000016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o00000000020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b00000000000000000000000000010000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("00000000020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("00000000000000000000000000010000")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2] =
                    {
                        .value = UINT32_MAX / 2,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("2147483647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d2147483647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x7fffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x7FFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o17777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("2147483647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("2147483647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("7fffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("7FFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("17777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("2147483647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("2147483647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("7fffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        7FFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     17777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                 1111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("2147483647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d2147483647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x7fffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000007FFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000017777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000001111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("2147483647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d2147483647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x7fffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x7FFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o17777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b01111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("2147483647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("2147483647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("7fffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("7FFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("17777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8(" 1111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("2147483647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("2147483647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("7fffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000007FFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000017777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000001111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("2147483647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("2147483647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("7fffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("7FFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("17777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("01111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("2.147.483.647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d2.147.483.647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x7f_ff_ff_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x7F_FF_FF_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o17_777_777_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1111111_11111111_11111111_11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("2.147.483.647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("2.147.483.647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("7f_ff_ff_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("7F_FF_FF_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("17_777_777_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1111111_11111111_11111111_11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("2.147.483.647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d2.147.483.647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x7f_ff_ff_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x7F_FF_FF_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o17_777_777_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b01111111_11111111_11111111_11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("2.147.483.647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("2.147.483.647"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7f_ff_ff_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7F_FF_FF_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("17_777_777_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("01111111_11111111_11111111_11111111")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5] =
                    {
                        .value = UINT32_MAX - 5,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4294967290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4294967290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o37777777772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b11111111111111111111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4294967290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4294967290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("37777777772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("11111111111111111111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("4294967290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("4294967290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        FFFFFFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     37777777772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                11111111111111111111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("4294967290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d4294967290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x00000000FFFFFFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000037777777772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000011111111111111111111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("4294967290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d4294967290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o37777777772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b11111111111111111111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("37777777772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("11111111111111111111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("4294967290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("4294967290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("00000000FFFFFFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000037777777772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000011111111111111111111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("37777777772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("11111111111111111111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4.294.967.290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4.294.967.290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_fa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o37_777_777_772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_fa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("37_777_777_772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("4.294.967.290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d4.294.967.290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_fa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o37_777_777_772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.290"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("37_777_777_772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111010")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4] =
                    {
                        .value = UINT32_MAX - 4,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4294967291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4294967291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o37777777773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b11111111111111111111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4294967291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4294967291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("37777777773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("11111111111111111111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("4294967291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("4294967291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        FFFFFFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     37777777773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                11111111111111111111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("4294967291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d4294967291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x00000000FFFFFFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000037777777773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000011111111111111111111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("4294967291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d4294967291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o37777777773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b11111111111111111111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("37777777773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("11111111111111111111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("4294967291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("4294967291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("00000000FFFFFFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000037777777773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000011111111111111111111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("37777777773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("11111111111111111111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4.294.967.291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4.294.967.291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_fb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o37_777_777_773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_fb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("37_777_777_773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("4.294.967.291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d4.294.967.291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_fb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o37_777_777_773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.291"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("37_777_777_773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111011")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3] =
                    {
                        .value = UINT32_MAX - 3,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4294967292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4294967292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o37777777774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b11111111111111111111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4294967292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4294967292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("37777777774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("11111111111111111111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("4294967292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("4294967292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        FFFFFFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     37777777774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                11111111111111111111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("4294967292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d4294967292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x00000000FFFFFFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000037777777774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000011111111111111111111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("4294967292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d4294967292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o37777777774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b11111111111111111111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("37777777774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("11111111111111111111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("4294967292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("4294967292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("00000000FFFFFFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000037777777774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000011111111111111111111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("37777777774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("11111111111111111111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4.294.967.292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4.294.967.292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_fc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o37_777_777_774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_fc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("37_777_777_774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("4.294.967.292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d4.294.967.292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_fc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o37_777_777_774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.292"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("37_777_777_774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111100")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2] =
                    {
                        .value = UINT32_MAX - 2,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4294967293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4294967293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o37777777775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b11111111111111111111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4294967293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4294967293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("37777777775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("11111111111111111111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("4294967293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("4294967293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        FFFFFFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     37777777775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                11111111111111111111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("4294967293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d4294967293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x00000000FFFFFFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000037777777775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000011111111111111111111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("4294967293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d4294967293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o37777777775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b11111111111111111111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("37777777775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("11111111111111111111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("4294967293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("4294967293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("00000000FFFFFFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000037777777775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000011111111111111111111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("37777777775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("11111111111111111111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4.294.967.293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4.294.967.293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_fd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o37_777_777_775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_fd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("37_777_777_775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("4.294.967.293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d4.294.967.293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_fd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o37_777_777_775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.293"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("37_777_777_775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111101")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1] =
                    {
                        .value = UINT32_MAX - 1,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4294967294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4294967294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o37777777776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b11111111111111111111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4294967294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4294967294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("37777777776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("11111111111111111111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("4294967294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("4294967294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        FFFFFFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     37777777776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                11111111111111111111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("4294967294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d4294967294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x00000000FFFFFFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000037777777776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000011111111111111111111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("4294967294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d4294967294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o37777777776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b11111111111111111111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("37777777776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("11111111111111111111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("4294967294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("4294967294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("00000000FFFFFFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000037777777776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000011111111111111111111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("37777777776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("11111111111111111111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4.294.967.294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4.294.967.294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_fe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o37_777_777_776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_fe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("37_777_777_776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("4.294.967.294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d4.294.967.294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_fe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o37_777_777_776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.294"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("37_777_777_776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111110")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX] =
                    {
                        .value = UINT32_MAX,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4294967295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4294967295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o37777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b11111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4294967295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4294967295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("ffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("37777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("11111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("4294967295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("4294967295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("ffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        FFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     37777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                11111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("4294967295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d4294967295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x00000000FFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000037777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000011111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("4294967295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d4294967295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o37777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b11111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("ffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("37777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("11111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("4294967295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("4294967295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("ffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("00000000FFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000037777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000011111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("ffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("37777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("11111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4.294.967.295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4.294.967.295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o37_777_777_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("37_777_777_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("4.294.967.295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d4.294.967.295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o37_777_777_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.295"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("37_777_777_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111")
                        },
                    },
                },

// ==================== U64 ====================

                [(u64)UNSIGNED_TEST_CASE_U64] =
                {
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_ZERO] =
                    {
                        .value = 0,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                               0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("00"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0000000000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("                     0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                                                               0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("00"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0000000000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("0"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("0"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0000000000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0000000000000000000000"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000000000000000000000000000000000000000000000000000")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_ONE] =
                    {
                        .value = 1,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                               1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("01"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0000000000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("                     1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                                                               1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("01"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0000000000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0000000000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0000000000000000000001"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000000000000000000000000000000000000000000000000001")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_TWO] =
                    {
                        .value = 2,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                              10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("02"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0000000000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("                     2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                                                              10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("02"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0000000000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("2"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("10"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0000000000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0000000000000000000002"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000000000000000000000000000000000000000000000000010")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_FOUR] =
                    {
                        .value = 4,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                             100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("04"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0000000000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("                     4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                                                             100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("04"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0000000000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("4"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0000000000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0000000000000000000004"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000000000000000000000000000000000000000000000000100")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_EIGHT] =
                    {
                        .value = 8,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                              10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                            1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("08"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0000000000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("                    10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                                                            1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("08"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0000000000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("8"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0000000000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000008"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0000000000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000000000000000000000000000000000000000000000001000")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_SIXTEEN] =
                    {
                        .value = 16,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("  16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("      10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("              10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                              20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                           10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000000000000000016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000000000000000016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0000000000000000000020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("                  16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("                  16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("              10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("              10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("                    20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                                                           10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0000000000000000000020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("16"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("10"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("20"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("10000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000000000000000016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000000000000000016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0000000000000000000020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000016"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000010"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0000000000000000000020"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000000000000000000000000000000000000000000000010000")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2] =
                    {
                        .value = UINT64_MAX / 2,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("9223372036854775807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d9223372036854775807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x7fffffffffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x7FFFFFFFFFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o777777777777777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("9223372036854775807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("9223372036854775807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("7fffffffffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("7FFFFFFFFFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("777777777777777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("9223372036854775807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("9223372036854775807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("7fffffffffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("7FFFFFFFFFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("           777777777777777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8(" 111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("9223372036854775807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d9223372036854775807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x7fffffffffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x7FFFFFFFFFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000777777777777777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("09223372036854775807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d09223372036854775807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x7fffffffffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x7FFFFFFFFFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0777777777777777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8(" 9223372036854775807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8(" 9223372036854775807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("7fffffffffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("7FFFFFFFFFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8(" 777777777777777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8(" 111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("9223372036854775807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("9223372036854775807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("7fffffffffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("7FFFFFFFFFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000777777777777777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("09223372036854775807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("09223372036854775807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("7fffffffffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("7FFFFFFFFFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0777777777777777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("9.223.372.036.854.775.807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d9.223.372.036.854.775.807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x7f_ff_ff_ff_ff_ff_ff_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x7F_FF_FF_FF_FF_FF_FF_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o777_777_777_777_777_777_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("9.223.372.036.854.775.807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("9.223.372.036.854.775.807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("7f_ff_ff_ff_ff_ff_ff_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("7F_FF_FF_FF_FF_FF_FF_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("777_777_777_777_777_777_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("09.223.372.036.854.775.807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d09.223.372.036.854.775.807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x7f_ff_ff_ff_ff_ff_ff_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x7F_FF_FF_FF_FF_FF_FF_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0777_777_777_777_777_777_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b01111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("09.223.372.036.854.775.807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("09.223.372.036.854.775.807"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7f_ff_ff_ff_ff_ff_ff_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7F_FF_FF_FF_FF_FF_FF_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0777_777_777_777_777_777_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("01111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5] =
                    {
                        .value = UINT64_MAX - 5,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("18446744073709551610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d18446744073709551610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffffffffffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFFFFFFFFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1777777777777777777772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111111111111111111111111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("18446744073709551610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("18446744073709551610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffffffffffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFFFFFFFFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1777777777777777777772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111111111111111111111111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("18446744073709551610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("18446744073709551610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffffffffffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("FFFFFFFFFFFFFFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("          1777777777777777777772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("1111111111111111111111111111111111111111111111111111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("18446744073709551610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d18446744073709551610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffffffffffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0xFFFFFFFFFFFFFFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000001777777777777777777772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b1111111111111111111111111111111111111111111111111111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("18446744073709551610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d18446744073709551610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffffffffffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFFFFFFFFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o1777777777777777777772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111111111111111111111111111111111111111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffffffffffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("1777777777777777777772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("18446744073709551610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("18446744073709551610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffffffffffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000001777777777777777777772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffffffffffa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFFFFFFFFFA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("1777777777777777777772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111111111111111111111111111111111111111111111111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("18.446.744.073.709.551.610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d18.446.744.073.709.551.610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff_ff_ff_ff_fa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF_FF_FF_FF_FA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1_777_777_777_777_777_777_772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff_ff_ff_ff_fa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF_FF_FF_FF_FA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1_777_777_777_777_777_777_772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("18.446.744.073.709.551.610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d18.446.744.073.709.551.610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff_ff_ff_ff_fa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF_FF_FF_FF_FA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o1_777_777_777_777_777_777_772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111010"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.610"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_fa"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FA"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("1_777_777_777_777_777_777_772"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111010")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4] =
                    {
                        .value = UINT64_MAX - 4,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("18446744073709551611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d18446744073709551611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffffffffffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFFFFFFFFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1777777777777777777773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111111111111111111111111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("18446744073709551611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("18446744073709551611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffffffffffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFFFFFFFFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1777777777777777777773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111111111111111111111111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("18446744073709551611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("18446744073709551611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffffffffffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("FFFFFFFFFFFFFFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("          1777777777777777777773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("1111111111111111111111111111111111111111111111111111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("18446744073709551611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d18446744073709551611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffffffffffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0xFFFFFFFFFFFFFFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000001777777777777777777773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b1111111111111111111111111111111111111111111111111111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("18446744073709551611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d18446744073709551611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffffffffffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFFFFFFFFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o1777777777777777777773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111111111111111111111111111111111111111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffffffffffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("1777777777777777777773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("18446744073709551611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("18446744073709551611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffffffffffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000001777777777777777777773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffffffffffb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFFFFFFFFFB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("1777777777777777777773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111111111111111111111111111111111111111111111111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("18.446.744.073.709.551.611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d18.446.744.073.709.551.611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff_ff_ff_ff_fb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF_FF_FF_FF_FB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1_777_777_777_777_777_777_773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff_ff_ff_ff_fb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF_FF_FF_FF_FB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1_777_777_777_777_777_777_773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("18.446.744.073.709.551.611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d18.446.744.073.709.551.611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff_ff_ff_ff_fb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF_FF_FF_FF_FB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o1_777_777_777_777_777_777_773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111011"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.611"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_fb"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FB"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("1_777_777_777_777_777_777_773"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111011")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3] =
                    {
                        .value = UINT64_MAX - 3,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("18446744073709551612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d18446744073709551612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffffffffffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFFFFFFFFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1777777777777777777774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111111111111111111111111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("18446744073709551612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("18446744073709551612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffffffffffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFFFFFFFFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1777777777777777777774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111111111111111111111111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("18446744073709551612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("18446744073709551612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffffffffffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("FFFFFFFFFFFFFFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("          1777777777777777777774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("1111111111111111111111111111111111111111111111111111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("18446744073709551612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d18446744073709551612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffffffffffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0xFFFFFFFFFFFFFFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000001777777777777777777774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b1111111111111111111111111111111111111111111111111111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("18446744073709551612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d18446744073709551612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffffffffffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFFFFFFFFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o1777777777777777777774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111111111111111111111111111111111111111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffffffffffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("1777777777777777777774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("18446744073709551612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("18446744073709551612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffffffffffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000001777777777777777777774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffffffffffc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFFFFFFFFFC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("1777777777777777777774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111111111111111111111111111111111111111111111111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("18.446.744.073.709.551.612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d18.446.744.073.709.551.612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff_ff_ff_ff_fc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF_FF_FF_FF_FC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1_777_777_777_777_777_777_774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff_ff_ff_ff_fc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF_FF_FF_FF_FC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1_777_777_777_777_777_777_774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("18.446.744.073.709.551.612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d18.446.744.073.709.551.612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff_ff_ff_ff_fc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF_FF_FF_FF_FC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o1_777_777_777_777_777_777_774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111100"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.612"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_fc"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FC"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("1_777_777_777_777_777_777_774"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111100")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2] =
                    {
                        .value = UINT64_MAX - 2,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("18446744073709551613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d18446744073709551613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffffffffffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFFFFFFFFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1777777777777777777775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111111111111111111111111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("18446744073709551613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("18446744073709551613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffffffffffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFFFFFFFFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1777777777777777777775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111111111111111111111111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("18446744073709551613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("18446744073709551613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffffffffffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("FFFFFFFFFFFFFFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("          1777777777777777777775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("1111111111111111111111111111111111111111111111111111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("18446744073709551613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d18446744073709551613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffffffffffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0xFFFFFFFFFFFFFFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000001777777777777777777775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b1111111111111111111111111111111111111111111111111111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("18446744073709551613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d18446744073709551613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffffffffffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFFFFFFFFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o1777777777777777777775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111111111111111111111111111111111111111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffffffffffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("1777777777777777777775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("18446744073709551613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("18446744073709551613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffffffffffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000001777777777777777777775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffffffffffd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFFFFFFFFFD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("1777777777777777777775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111111111111111111111111111111111111111111111111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("18.446.744.073.709.551.613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d18.446.744.073.709.551.613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff_ff_ff_ff_fd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF_FF_FF_FF_FD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1_777_777_777_777_777_777_775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff_ff_ff_ff_fd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF_FF_FF_FF_FD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1_777_777_777_777_777_777_775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("18.446.744.073.709.551.613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d18.446.744.073.709.551.613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff_ff_ff_ff_fd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF_FF_FF_FF_FD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o1_777_777_777_777_777_777_775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111101"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.613"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_fd"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FD"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("1_777_777_777_777_777_777_775"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111101")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1] =
                    {
                        .value = UINT64_MAX - 1,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("18446744073709551614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d18446744073709551614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffffffffffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFFFFFFFFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1777777777777777777776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111111111111111111111111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("18446744073709551614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("18446744073709551614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffffffffffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFFFFFFFFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1777777777777777777776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111111111111111111111111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("18446744073709551614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("18446744073709551614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffffffffffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("FFFFFFFFFFFFFFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("          1777777777777777777776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("1111111111111111111111111111111111111111111111111111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("18446744073709551614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d18446744073709551614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffffffffffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0xFFFFFFFFFFFFFFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000001777777777777777777776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b1111111111111111111111111111111111111111111111111111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("18446744073709551614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d18446744073709551614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffffffffffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFFFFFFFFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o1777777777777777777776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111111111111111111111111111111111111111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffffffffffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("1777777777777777777776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("18446744073709551614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("18446744073709551614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffffffffffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000001777777777777777777776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffffffffffe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFFFFFFFFFE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("1777777777777777777776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111111111111111111111111111111111111111111111111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("18.446.744.073.709.551.614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d18.446.744.073.709.551.614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff_ff_ff_ff_fe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF_FF_FF_FF_FE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1_777_777_777_777_777_777_776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff_ff_ff_ff_fe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF_FF_FF_FF_FE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1_777_777_777_777_777_777_776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("18.446.744.073.709.551.614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d18.446.744.073.709.551.614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff_ff_ff_ff_fe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF_FF_FF_FF_FE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o1_777_777_777_777_777_777_776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111110"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.614"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_fe"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FE"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("1_777_777_777_777_777_777_776"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111110")
                        },
                    },
                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX] =
                    {
                        .value = UINT64_MAX,
                        .expected_results =
                        {
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("18446744073709551615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d18446744073709551615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xffffffffffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFFFFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1777777777777777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("18446744073709551615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("18446744073709551615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("ffffffffffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFFFFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1777777777777777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("18446744073709551615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("18446744073709551615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("ffffffffffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("FFFFFFFFFFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("          1777777777777777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("1111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("18446744073709551615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d18446744073709551615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xffffffffffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0xFFFFFFFFFFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000001777777777777777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b1111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("18446744073709551615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d18446744073709551615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xffffffffffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFFFFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o1777777777777777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("ffffffffffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("1777777777777777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("18446744073709551615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("18446744073709551615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("ffffffffffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000001777777777777777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("ffffffffffffffff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFFFFFFFFFF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("1777777777777777777777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("18.446.744.073.709.551.615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d18.446.744.073.709.551.615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff_ff_ff_ff_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF_FF_FF_FF_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1_777_777_777_777_777_777_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff_ff_ff_ff_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF_FF_FF_FF_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1_777_777_777_777_777_777_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("18.446.744.073.709.551.615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d18.446.744.073.709.551.615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff_ff_ff_ff_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF_FF_FF_FF_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o1_777_777_777_777_777_777_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.615"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_ff"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FF"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("1_777_777_777_777_777_777_777"),
                            [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111")
                        },
                    },
                },
            };

#undef S8
#define S8(strlit) ((String8) S8_INITIALIZER(strlit))

            for (u64 type_i = 0; type_i < UNSIGNED_TEST_CASE_COUNT; type_i += 1)
            {
                for (u64 case_value_i = 0; case_value_i < UNSIGNED_TEST_CASE_NUMBER_COUNT; case_value_i += 1)
                {
                    const UnsignedTestCase* uint_case = &cases[type_i][case_value_i];
                    u64 value = uint_case->value;

                    for (u64 case_i = 0; case_i < UNSIGNED_FORMAT_TEST_CASE_COUNT; case_i += 1)
                    {
                        String8 format_string = format_strings[type_i][case_i];
                        String8 expected_string = uint_case->expected_results[case_i];
                        UnsignedTestCaseId test_type = (UnsignedTestCaseId)type_i;

                        String8 result_string;
                        switch (test_type)
                        {
                            break; case UNSIGNED_TEST_CASE_U8: result_string =  string_format(arena, format_string, (u8)value);
                            break; case UNSIGNED_TEST_CASE_U16: result_string = string_format(arena, format_string, (u16)value);
                            break; case UNSIGNED_TEST_CASE_U32: result_string = string_format(arena, format_string, (u32)value);
                            break; case UNSIGNED_TEST_CASE_U64: result_string = string_format(arena, format_string, (u64)value);
                            break; default: BUSTER_UNREACHABLE();
                        }

                        BUSTER_STRING_TEST(arguments, result_string, expected_string);
                    }
                }
            }
        }
    }

    // string_first_sequence
    {
        // Basic match at start
        BUSTER_TEST(arguments, string_first_sequence(S8("hello world"), S8("hello")) == 0);
        // Match in middle
        BUSTER_TEST(arguments, string_first_sequence(S8("hello world"), S8("world")) == 6);
        // Match at end
        BUSTER_TEST(arguments, string_first_sequence(S8("hello.txt"), S8(".txt")) == 5);
        // No match
        BUSTER_TEST(arguments, string_first_sequence(S8("hello world"), S8("foo")) == BUSTER_STRING_NO_MATCH);
        // Empty substring matches at 0
        BUSTER_TEST(arguments, string_first_sequence(S8("hello"), S8("")) == 0);
        // Empty string with empty substring
        BUSTER_TEST(arguments, string_first_sequence(S8(""), S8("")) == 0);
        // Empty string with non-empty substring
        BUSTER_TEST(arguments, string_first_sequence(S8(""), S8("a")) == BUSTER_STRING_NO_MATCH);
        // Substring longer than string
        BUSTER_TEST(arguments, string_first_sequence(S8("hi"), S8("hello")) == BUSTER_STRING_NO_MATCH);
        // Exact match
        BUSTER_TEST(arguments, string_first_sequence(S8("abc"), S8("abc")) == 0);
        // Multiple occurrences - should return first
        BUSTER_TEST(arguments, string_first_sequence(S8("abcabc"), S8("abc")) == 0);
        // Single character match
        BUSTER_TEST(arguments, string_first_sequence(S8("hello"), S8("l")) == 2);
        // Partial match should not count
        BUSTER_TEST(arguments, string_first_sequence(S8("abcd"), S8("abd")) == BUSTER_STRING_NO_MATCH);
    }

    // string_ends_with_sequence
    {
            BUSTER_TEST(arguments, string_ends_with_sequence(S8("hello.txt"), S8(".txt")));
            BUSTER_TEST(arguments, string_ends_with_sequence(S8("test.vert.spv"), S8(".vert.spv")));
            BUSTER_TEST(arguments, string_ends_with_sequence(S8("abc"), S8("abc")));
            BUSTER_TEST(arguments, string_ends_with_sequence(S8("hello"), S8("")));
            BUSTER_TEST(arguments, !string_ends_with_sequence(S8("hello.txt"), S8(".c")));
            BUSTER_TEST(arguments, !string_ends_with_sequence(S8("ab"), S8("abc")));
            BUSTER_TEST(arguments, !string_ends_with_sequence(S8("hi"), S8("hello")));
            BUSTER_TEST(arguments, string_ends_with_sequence(S8(""), S8("")));
            BUSTER_TEST(arguments, !string_ends_with_sequence(S8(""), S8("a")));
            BUSTER_TEST(arguments, !string_ends_with_sequence(S8("abcde"), S8("cdf")));
            BUSTER_TEST(arguments, !string_ends_with_sequence(S8("txtfile"), S8("txt")));
    }

    {
        PosixChar first[] = "faa";
        PosixChar second[] = "fee";
        PosixChar empty[] = "";
        PosixChar* posix_string_list[] = { first, second, empty, 0 };

        SliceString8 strings = slice_string_from_posix_string_list(arena, posix_string_list);
        BUSTER_TEST(arguments, strings.length == 3);
        BUSTER_TEST(arguments, string_equal(strings.pointer[0], S8("faa")));
        BUSTER_TEST(arguments, string_equal(strings.pointer[1], S8("fee")));
        BUSTER_TEST(arguments, string_equal(strings.pointer[2], S8("")));
    }

    {
        {
            String8 parts[] = {
                S8("faa"),
                S8("fee"),
                S8("fii"),
                S8("foo"),
                S8("fuu"),
            };
            const char16* win32_string_raw = windows_string_list_from_slice_string(arena, (SliceString8) BUSTER_ARRAY_TO_SLICE(parts));
            String16 win32_string = string16_from_pointer(win32_string_raw);
            char16 expected_win32_string_raw[] = {
                'f', 'a', 'a', ' ',
                'f', 'e', 'e', ' ',
                'f', 'i', 'i', ' ',
                'f', 'o', 'o', ' ',
                'f', 'u', 'u', 0
            };
            String16 expected_win32_string = (String16) {
                .pointer = expected_win32_string_raw,
                .length = BUSTER_ARRAY_LENGTH(expected_win32_string_raw) - 1,
            };
            BUSTER_TEST(arguments, string16_equal(win32_string, expected_win32_string));
        }
        {
            String8 parts[] = {
                S8("program"),
                S8("two words"),
                S8("quote\"arg"),
                S8("slash\\"),
                S8("trail\\"),
                S8(""),
                S8("C:\\Program Files\\"),
                S8("a\\\"b"),
            };
            const char16* win32_string_raw = windows_string_list_from_slice_string(arena, (SliceString8) BUSTER_ARRAY_TO_SLICE(parts));
            String16 win32_string = string16_from_pointer(win32_string_raw);
            String16 expected_win32_string = string16_from_string8(
                arena,
                S8("program \"two words\" \"quote\\\"arg\" slash\\ trail\\ \"\" \"C:\\Program Files\\\\\" \"a\\\\\\\"b\""),
                false
            );
            BUSTER_TEST(arguments, string16_equal(win32_string, expected_win32_string));
        }
        {
            String16 command_line = string16_from_string8(
                arena,
                S8("program \"two words\" \"quote\\\"arg\" slash\\ trail\\ \"\" \"C:\\Program Files\\\\\" \"a\\\\\\\"b\""),
                true
            );
            SliceString8 parts = slice_string_from_windows_string_list(arena, command_line.pointer);
            BUSTER_TEST(arguments, parts.length == 8);
            BUSTER_TEST(arguments, string_equal(parts.pointer[0], S8("program")));
            BUSTER_TEST(arguments, string_equal(parts.pointer[1], S8("two words")));
            BUSTER_TEST(arguments, string_equal(parts.pointer[2], S8("quote\"arg")));
            BUSTER_TEST(arguments, string_equal(parts.pointer[3], S8("slash\\")));
            BUSTER_TEST(arguments, string_equal(parts.pointer[4], S8("trail\\")));
            BUSTER_TEST(arguments, string_equal(parts.pointer[5], S8("")));
            BUSTER_TEST(arguments, string_equal(parts.pointer[6], S8("C:\\Program Files\\")));
            BUSTER_TEST(arguments, string_equal(parts.pointer[7], S8("a\\\"b")));
        }
        {
            String16 command_line = string16_from_string8(arena, S8(" \t program\targ  \"\"  final"), true);
            SliceString8 parts = slice_string_from_windows_string_list(arena, command_line.pointer);
            BUSTER_TEST(arguments, parts.length == 4);
            BUSTER_TEST(arguments, string_equal(parts.pointer[0], S8("program")));
            BUSTER_TEST(arguments, string_equal(parts.pointer[1], S8("arg")));
            BUSTER_TEST(arguments, string_equal(parts.pointer[2], S8("")));
            BUSTER_TEST(arguments, string_equal(parts.pointer[3], S8("final")));
        }
    }

    {
        const char16 environment_block[] = {
            'U', 'S', 'E', 'R', '=', 'd', 'a', 'v', 'i', 'd', 0,
            'P', 'R', 'O', 'G', 'R', 'A', 'M', '_', 'F', 'I', 'L', 'E', 'S', '=',
            'C', ':', '\\', 'P', 'r', 'o', 'g', 'r', 'a', 'm', ' ', 'F', 'i', 'l', 'e', 's', 0,
            'E', 'M', 'P', 'T', 'Y', '=', 0,
            0,
        };
        SliceString8 environment = string16_environment_block_to_slice_string(arena, environment_block);
        BUSTER_TEST(arguments, environment.length == 3);
        BUSTER_TEST(arguments, string_equal(environment.pointer[0], S8("USER=david")));
        BUSTER_TEST(arguments, string_equal(environment.pointer[1], S8("PROGRAM_FILES=C:\\Program Files")));
        BUSTER_TEST(arguments, string_equal(environment.pointer[2], S8("EMPTY=")));
    }
    {
        String8 environment[] = {
            S8("USER=david"),
            S8("PROGRAM_FILES=C:\\Program Files"),
            S8("EMPTY="),
        };
        const char16* environment_block = windows_environment_block_from_slice_string(arena, (SliceString8) BUSTER_ARRAY_TO_SLICE(environment));
        const char16 expected_environment_block[] = {
            'U', 'S', 'E', 'R', '=', 'd', 'a', 'v', 'i', 'd', 0,
            'P', 'R', 'O', 'G', 'R', 'A', 'M', '_', 'F', 'I', 'L', 'E', 'S', '=',
            'C', ':', '\\', 'P', 'r', 'o', 'g', 'r', 'a', 'm', ' ', 'F', 'i', 'l', 'e', 's', 0,
            'E', 'M', 'P', 'T', 'Y', '=', 0,
            0,
        };
        String16 environment_block_string = string16_from_pointer_length(environment_block, BUSTER_ARRAY_LENGTH(expected_environment_block) - 1);
        String16 expected_environment_block_string = string16_from_pointer_length(expected_environment_block, BUSTER_ARRAY_LENGTH(expected_environment_block) - 1);
        BUSTER_TEST(arguments, string16_equal(environment_block_string, expected_environment_block_string));

        SliceString8 round_trip = string16_environment_block_to_slice_string(arena, environment_block);
        BUSTER_TEST(arguments, round_trip.length == 3);
        BUSTER_TEST(arguments, string_equal(round_trip.pointer[0], S8("USER=david")));
        BUSTER_TEST(arguments, string_equal(round_trip.pointer[1], S8("PROGRAM_FILES=C:\\Program Files")));
        BUSTER_TEST(arguments, string_equal(round_trip.pointer[2], S8("EMPTY=")));
    }
    {
        SliceString8 environment = {0};
        const char16* environment_block = windows_environment_block_from_slice_string(arena, environment);
        BUSTER_TEST(arguments, environment_block[0] == 0);
    }

    {
        {
            String8 s = S8("hello");
            BUSTER_TEST(arguments, s.length == 5);
        }
        {
            String8 s = S8("ñ");
            BUSTER_TEST(arguments, s.length == 2);
        }
        {
            String8 s = S8("€");
            BUSTER_TEST(arguments, s.length == 3);
        }
        {
            String8 s = S8("😀");
            BUSTER_TEST(arguments, s.length == 4);
        }
    }

    {
        {
            char16 hello_raw[] = { 'h', 'e', 'l', 'l', 'o', 0 };
            String16 s = (String16) { .pointer = hello_raw, .length = BUSTER_ARRAY_LENGTH(hello_raw) - 1 };
            BUSTER_TEST(arguments, s.length == 5);
        }
        {
            char16 enye_raw[] = { 0x00F1, 0 };
            String16 s = (String16) { .pointer = enye_raw, .length = BUSTER_ARRAY_LENGTH(enye_raw) - 1 };
            BUSTER_TEST(arguments, s.length == 1);
        }
        {
            char16 euro_raw[] = { 0x20AC, 0 };
            String16 s = (String16) { .pointer = euro_raw, .length = BUSTER_ARRAY_LENGTH(euro_raw) - 1 };
            BUSTER_TEST(arguments, s.length == 1);
        }
        {
            char16 grinning_face_raw[] = { 0xD83D, 0xDE00, 0 };
            String16 s = (String16) { .pointer = grinning_face_raw, .length = BUSTER_ARRAY_LENGTH(grinning_face_raw) - 1 };
            BUSTER_TEST(arguments, s.length == 2);
        }
    }

    return result;
}

#undef BUSTER_UTF16_TO_UTF8_TEST
#undef BUSTER_UNICODE_ROUND_TRIP_TEST
#undef BUSTER_UNICODE_OS_TO_UTF8_TEST

#endif
