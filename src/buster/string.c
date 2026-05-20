#include <buster/string.h>
#include <buster/arena.h>
#include <buster/os.h>

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

BUSTER_GLOBAL_LOCAL u64 string_first_code_unit(String8 string, char8 code_unit)
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

String8 string16_to_string8_arena(Arena* arena, String16 s, bool null_terminate)
{
    char8* restrict pointer = arena_allocate(arena, char8, s.length + null_terminate);
    for (u64 i = 0; i < s.length; i += 1)
    {
        // TODO
        pointer[i] = (u8)s.pointer[i];
    }

    if (null_terminate)
    {
        pointer[s.length] = 0;
    }

    String8 result = string_from_pointer_length(pointer, s.length);
    return result;
}

String16 string8_to_string16_arena(Arena* arena, String8 s, bool null_terminate)
{
    char16* pointer = arena_allocate(arena, char16, s.length + null_terminate);
    for (u64 i = 0; i < s.length; i += 1)
    {
        pointer[i] = s.pointer[i];
    }

    if (null_terminate)
    {
        pointer[s.length] = 0;
    }

    String16 result = string16_from_pointer_length(pointer, s.length);
    return result;
}

// String8 string_os_to_string8_arena(Arena* arena, StringOs string)
// {
// #if defined(_WIN32)
//     return string16_to_string8_arena(arena, s, true);
// #else
//     BUSTER_UNUSED(arena);
//     return string;
// #endif
// }

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

// template<typename DestinationChar, typename SourceChar>
// BUSTER_GLOBAL_LOCAL void string_append_slice_different(Arena* arena, String<SourceChar> string)
// {
//
//     if constexpr (BUSTER_TYPE_EQUAL(DestinationChar, SourceChar))
//     {
//     let destination = arena_allocate(arena, DestinationChar, string.length);
//         memcpy(destination, string.pointer, sizeof(SourceChar) * string.length);
//     }
//     else
//     {
//     let destination = arena_allocate(arena, DestinationChar, string.length);
//         for (u64 i = 0; i < string.length; i += 1)
//         {
//             destination[i] = (DestinationChar)string.pointer[i];
//         }
//     }
// }

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

String8 string8_slice(String8 slice, u64 start, u64 end)
{
    return string_slice(slice, start, end);
}

typedef struct Utf8Result Utf8Result;
struct Utf8Result
{
    char8 buffer[4];
    u32 count;
};

BUSTER_GLOBAL_LOCAL Utf8Result utf8_from_other(u32 ch)
{
    Utf8Result result = {0};
    result.buffer[0] = (char8)ch;
    result.count = 1;
    return result;
}

String8 string8_duplicate_from_string_os(Arena* arena, StringOs string, bool null_terminate)
{
    u64 position = arena->position;

    for (u64 i = 0; i < string.length; i += 1)
    {
        char16 ch16 = string.pointer[i];
        Utf8Result encoding_result = utf8_from_other(ch16);
        char8* allocation = arena_allocate(arena, char8, encoding_result.count);

        for (u32 encoding_i = 0; encoding_i < encoding_result.count; encoding_i += 1)
        {
            allocation[encoding_i] = encoding_result.buffer[encoding_i];
        }
    }

    u64 result_length = arena->position - position;
    if (null_terminate) *arena_allocate(arena, char8, 1) = 0;

    String8 result = string_from_pointer_length((char8*)arena_get_byte_pointer(arena, position), result_length);
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
                    FORMAT_TYPE_STRING_OS,
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
                    [FORMAT_TYPE_STRING_OS] = S8("SOs"),
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

                u64 i;
                for (i = 0; i < BUSTER_ARRAY_LENGTH(possible_format_strings); i += 1)
                {
                    String8 possible_format_string = possible_format_strings[i];
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

                FormatTypeId format_type_id = (FormatTypeId)i;
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
                    break; case FORMAT_TYPE_STRING_OS_LIST:
                    {
                        StringOsList string_os_list = va_arg(variable_arguments, StringOsList);
                        StringOsListIterator it = string_os_list_iterator_initialize(string_os_list);

                        StringOs string_os;

                        it = string_os_list_iterator_initialize(string_os_list);
                        while ((string_os = string_os_list_iterator_next(&it)).pointer)
                        {
                            string8_duplicate_from_string_os(arena, string_os, false);
                            *arena_allocate(arena, char8, 1) = ' ';
                        }

                        // Remove trailing space
                        arena->position -= 1;
                    }
                    break; case FORMAT_TYPE_STRING_OS:
                    {
                        StringOs string = va_arg(variable_arguments, StringOs);
                        string8_duplicate_from_string_os(arena, string, false);
                    }
                    break; case FORMAT_TYPE_STRING8:
                    {
                        String8 string = va_arg(variable_arguments, String8);
                        char8* destination = arena_allocate(arena, char8, string.length);
                        memcpy(destination, string.pointer, sizeof(char8) * string.length);
                    }
                    break; case FORMAT_TYPE_STRING16:
                    {
                        String16 string16 = va_arg(variable_arguments, String16);
                        BUSTER_UNUSED(string16);
                        BUSTER_TRAP();
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
                        BUSTER_UNUSED(prefix_second_character);

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
                                prefix_second_character = 'd';
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
                                prefix_second_character = 'b';
                                integer_max_width = value_size * 8;
                                digit_group_character_count = 8;
                            }
                            break; case INTEGER_FORMAT_KIND_OCTAL:
                            {
                                prefix_second_character = 'o';
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
                                prefix_second_character = 'x';
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
                            break; case INTEGER_FORMAT_KIND_DECIMAL: format_result = string_format_i64_decimal(string_buffer, (u64)((value < 0) ? (-value) : value), value < 0);
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
                        CharOs error_buffer[BUSTER_OS_ERROR_BUFFER_MAX_LENGTH];
                        StringOs error_string = os_error_write_message((StringOs)BUSTER_ARRAY_TO_SLICE(error_buffer), os_error);

                        string8_duplicate_from_string_os(arena, error_string, false);
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
                BUSTER_TRAP();
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

StringOs string_os_duplicate_arena(Arena* arena, StringOs string, bool zero_terminate)
{
    StringOs result = { .pointer = arena_allocate(arena, CharOs, string.length + zero_terminate), .length = string.length };
    memcpy(result.pointer, string.pointer, sizeof(CharOs) * string.length);

    if (zero_terminate)
    {
        result.pointer[string.length] = 0;
    }

    return result;
}

SliceString8 os_string_list_to_slice_string(Arena* arena, StringOsList string_os_list)
{
    StringOsListIterator iterator = string_os_list_iterator_initialize(string_os_list);
    StringOs s;
    u64 string_count = 0;

    while ((s = string_os_list_iterator_next(&iterator)).pointer)
    {
        string_count += 1;
    }

    String8* slices = arena_allocate(arena, String8, string_count);
    iterator = string_os_list_iterator_initialize(string_os_list);

    for (u64 i = 0; i < string_count; i += 1)
    {
        s = string_os_list_iterator_next(&iterator);
        slices[i] = string8_duplicate_from_string_os(arena, s, true);
    }

    SliceString8 result = (SliceString8){ .pointer = slices, .length = string_count };
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

StringOs string_os_from_pointer(const CharOs* pointer)
{
#if defined(_WIN32)
    return string16_from_pointer(pointer);
#else
    return string_from_pointer(pointer);
#endif
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

StringOs string_os_list_iterator_next(StringOsListIterator* iterator)
{
    StringOs result = {0};
    StringOsList list = iterator->list;
    u64 original_position = iterator->position;
    u64 position = original_position;

    CharOs* current;
#if defined(_WIN32)
    current = &list[position];
    if (*current)
#else
    current = list[position];
    if (current)
#endif
    {
#if defined(_WIN32)
        CharOs* original_pointer = &list[position];
        CharOs* pointer = original_pointer;
        if (*pointer == '"')
        {
            // TODO: handle escape
            u64 double_quote = raw_string16_first_code_unit(pointer + 1, '"');
            if (double_quote == BUSTER_STRING_NO_MATCH)
            {
                return result;
            }

            position += double_quote + 1 + 1;
            pointer = &list[position];
        }

        u64 space = raw_string16_first_code_unit(pointer, ' ');
        bool is_space = space != BUSTER_STRING_NO_MATCH;
        space = is_space ? space : 0;
        position += space;
        position += is_space ? 0 : string16_length(pointer);
        u64 length = position - original_position;

        if (is_space)
        {
            while (list[position] == ' ')
            {
                position += 1;
            }
        }

        result = string_os_from_pointer_length(original_pointer, length);
#else
        position += 1;
        result = string_os_from_pointer(current);
#endif
        iterator->position = position;
    }

    return result;
}


StringOs string_os_from_pointer_length(CharOs* pointer, u64 length)
{
#if defined(_WIN32)
    return string16_from_pointer_length(pointer, length);
#else
    return string_from_pointer_length(pointer, length);
#endif
}

StringOsList string_os_list_builder_append(OsArgumentBuilder* builder, StringOs arg)
{
#if defined(_WIN32)
    StringOs result = string_os_duplicate_arena(builder->arena, arg, true);
    if (result.pointer)
    {
        result.pointer[arg.length] = ' ';
    }
    return result.pointer;
#else
    CharOs** result = arena_allocate(builder->arena, CharOs*, 1);
    if (result)
    {
        *result = (CharOs*)arg.pointer;
    }
    return result;
#endif
}

OsArgumentBuilder* string_os_list_builder_create(Arena* arena, StringOs s)
{
    u64 position = arena->position;
    OsArgumentBuilder* argument_builder = arena_allocate(arena, OsArgumentBuilder, 1);
    if (argument_builder)
    {
        *argument_builder = (OsArgumentBuilder) {
            .argv = 0,
                .arena = arena,
                .arena_offset = position,
        };
        argument_builder->argv = string_os_list_builder_append(argument_builder, s);
    }
    return argument_builder;
}

StringOsList string_os_list_builder_end(OsArgumentBuilder* restrict builder)
{
#if defined(_WIN32)
    *(CharOs*)((u8*)builder->arena + builder->arena->position - sizeof(CharOs)) = 0;
#else
    string_os_list_builder_append(builder, (StringOs){0});
#endif
    return builder->argv;
}

StringOsList string_os_list_create_from(Arena* arena, SliceStringOs arguments)
{
#if defined(_WIN32)
    u64 allocation_length = 0;

    for (u64 i = 0; i < arguments.length; i += 1)
    {
        allocation_length += arguments.pointer[i].length + 1;
    }

    CharOs* allocation = arena_allocate(arena, CharOs, allocation_length);

    for (u64 source_i = 0, destination_i = 0; source_i < arguments.length; source_i += 1)
    {
        StringOs source_argument = arguments.pointer[source_i];
        memcpy(&allocation[destination_i], source_argument.pointer, BUSTER_SLICE_SIZE(source_argument));
        destination_i += source_argument.length;
        allocation[destination_i] = ' ';
        destination_i += 1;
    }

    allocation[allocation_length - 1] = 0;

    return allocation;
#else
    CharOs** list = arena_allocate(arena, CharOs*, arguments.length + 1);

    for (u64 i = 0; i < arguments.length; i += 1)
    {
        list[i] = arguments.pointer[i].pointer;
    }

    list[arguments.length] = 0;

    return list;
#endif
}

// TODO: make this better
String16 string16_from_string8(Arena* arena, String8 string, bool null_terminate)
{
    char16* pointer = arena_allocate(arena, char16, string.length + null_terminate);

    for (u64 i = 0; i < string.length; i += 1)
    {
        pointer[i] = string.pointer[i];
    }

    if (null_terminate)
    {
        pointer[string.length] = 0;
    }

    String16 result = (String16) { .pointer = pointer, .length = string.length };
    return result;
}

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>

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
        {
            // Basic match at start
            bool success = string_first_sequence(S8("hello world"), S8("hello")) == 0;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // Match in middle
            bool success = string_first_sequence(S8("hello world"), S8("world")) == 6;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // Match at end
            bool success = string_first_sequence(S8("hello.txt"), S8(".txt")) == 5;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // No match
            bool success = string_first_sequence(S8("hello world"), S8("foo")) == BUSTER_STRING_NO_MATCH;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // Empty substring matches at 0
            bool success = string_first_sequence(S8("hello"), S8("")) == 0;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // Empty string with empty substring
            bool success = string_first_sequence(S8(""), S8("")) == 0;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // Empty string with non-empty substring
            bool success = string_first_sequence(S8(""), S8("a")) == BUSTER_STRING_NO_MATCH;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // Substring longer than string
            bool success = string_first_sequence(S8("hi"), S8("hello")) == BUSTER_STRING_NO_MATCH;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // Exact match
            bool success = string_first_sequence(S8("abc"), S8("abc")) == 0;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // Multiple occurrences - should return first
            bool success = string_first_sequence(S8("abcabc"), S8("abc")) == 0;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // Single character match
            bool success = string_first_sequence(S8("hello"), S8("l")) == 2;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // Partial match should not count
            bool success = string_first_sequence(S8("abcd"), S8("abd")) == BUSTER_STRING_NO_MATCH;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
    }

    // string_ends_with_sequence
    {
        {
            bool success = string_ends_with_sequence(S8("hello.txt"), S8(".txt"));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            bool success = string_ends_with_sequence(S8("test.vert.spv"), S8(".vert.spv"));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            bool success = string_ends_with_sequence(S8("abc"), S8("abc"));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            bool success = string_ends_with_sequence(S8("hello"), S8(""));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            bool success = !string_ends_with_sequence(S8("hello.txt"), S8(".c"));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            bool success = !string_ends_with_sequence(S8("ab"), S8("abc"));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            bool success = !string_ends_with_sequence(S8("hi"), S8("hello"));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            bool success = string_ends_with_sequence(S8(""), S8(""));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            bool success = !string_ends_with_sequence(S8(""), S8("a"));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            bool success = !string_ends_with_sequence(S8("abcde"), S8("cdf"));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            bool success = !string_ends_with_sequence(S8("txtfile"), S8("txt"));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
    }

    return result;
}

#endif
