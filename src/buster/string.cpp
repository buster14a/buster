#pragma once
#include <buster/string.h>
#include <buster/arena.h>
#include <buster/assertion.h>
#include <buster/os.h>

#define STRING_OF_CHAR(strlit, Char) ((String<Char>) { .pointer = (Char*)(BUSTER_TYPE_EQUAL(Char, char16) ? (void const*)(u ## strlit) : (void const*)(strlit)), .length = BUSTER_COMPILE_TIME_STRING_LENGTH(strlit) })

template<typename Char>
BUSTER_GLOBAL_LOCAL bool code_unit_is_binary(Char code_unit)
{
    return (code_unit == '1') | (code_unit == '0');
}

template<typename Char>
BUSTER_GLOBAL_LOCAL bool code_unit_is_decimal(Char code_unit)
{
    return (code_unit >= '0') & (code_unit <= '9');
}

template<typename Char>
BUSTER_GLOBAL_LOCAL bool code_unit_is_octal(Char code_unit)
{
    return (code_unit >= '0') & (code_unit <= '7');
}
// #define code_unit_is_octal(code_unit) is_between_range_included(ch, '0', '7')

template<typename Char>
BUSTER_GLOBAL_LOCAL bool code_unit_is_hexadecimal_alpha_upper(Char code_unit)
{
    return (code_unit >= 'A') & (code_unit <= 'F');
}

template<typename Char>
BUSTER_GLOBAL_LOCAL bool code_unit_is_hexadecimal_alpha_lower(Char code_unit)
{
    return (code_unit >= 'a') & (code_unit <= 'f');
}

template<typename Char>
BUSTER_GLOBAL_LOCAL bool code_unit_is_hexadecimal_alpha(Char code_unit)
{
    return (int)code_unit_is_hexadecimal_alpha_lower(code_unit) | code_unit_is_hexadecimal_alpha_upper(code_unit);
}

template<typename Char>
BUSTER_GLOBAL_LOCAL bool code_unit_is_hexadecimal(Char code_unit)
{
    return (int)code_unit_is_decimal(code_unit) | code_unit_is_hexadecimal_alpha(code_unit);
}

BUSTER_F_IMPL bool code_unit8_is_decimal(char8 code_unit)
{
    return code_unit_is_decimal(code_unit);
}

template<typename Char>
BUSTER_GLOBAL_LOCAL u64 parsing_accumulate_binary(u64 accumulator, Char code_unit)
{
    BUSTER_CHECK(code_unit_is_binary(code_unit));
    return ((accumulator) * 2) + ((code_unit) - '0');
}

template<typename Char>
BUSTER_GLOBAL_LOCAL u64 parsing_accumulate_octal(u64 accumulator, Char code_unit)
{
    BUSTER_CHECK(code_unit_is_octal(code_unit));
    return ((accumulator) * 8) + ((code_unit) - '0');
}

template<typename Char>
BUSTER_GLOBAL_LOCAL u64 parsing_accumulate_decimal(u64 accumulator, Char code_unit)
{
    BUSTER_CHECK(code_unit_is_decimal(code_unit));
    return accumulator * 10 + ((code_unit) - '0');
}

template<typename Char>
BUSTER_GLOBAL_LOCAL u64 parsing_accumulate_hexadecimal(u64 accumulator, Char code_unit)
{
    BUSTER_CHECK(code_unit_is_hexadecimal(code_unit));
    return ((accumulator) * 16 + (code_unit) - (code_unit_is_decimal(code_unit) ? '0' : (code_unit_is_hexadecimal_alpha_upper(code_unit) ? ('A' - 10) : code_unit_is_hexadecimal_alpha_lower(code_unit) ? ('a' - 10) : 0)));
}

template<typename Char>
BUSTER_GLOBAL_LOCAL IntegerParsingU64 string_parse_u64_decimal(const Char* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let code_unit = p[i];

        if (!code_unit_is_decimal(code_unit))
        {
            break;
        }

        i += 1;
        value = parsing_accumulate_decimal(value, code_unit);
    }

    return (IntegerParsingU64){ .value = value, .length = i };
}

template <typename Char>
BUSTER_F_IMPL IntegerParsingU64 string_parse_u64_hexadecimal(const Char* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let code_unit = p[i];

        if (!code_unit_is_hexadecimal(code_unit))
        {
            break;
        }

        i += 1;
        value = parsing_accumulate_hexadecimal(value, code_unit);
    }

    return (IntegerParsingU64){ .value = value, .length = i };
}

template <typename Char>
BUSTER_F_IMPL IntegerParsingU64 string_parse_u64_octal(const Char* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let code_unit = p[i];

        if (!code_unit_is_octal(code_unit))
        {
            break;
        }

        i += 1;
        value = parsing_accumulate_octal(value, (u8)code_unit);
    }

    return (IntegerParsingU64) { .value = value, .length = i };
}

template <typename Char>
BUSTER_F_IMPL IntegerParsingU64 string_parse_u64_binary(const Char* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let code_unit = p[i];

        if (!code_unit_is_binary(code_unit))
        {
            break;
        }

        i += 1;
        value = parsing_accumulate_binary(value, code_unit);
    }

    return (IntegerParsingU64){ .value = value, .length = i };
}

BUSTER_F_IMPL IntegerParsingU64 string8_parse_u64_hexadecimal(const char8* restrict p)
{
    return string_parse_u64_hexadecimal(p);
}

BUSTER_F_IMPL IntegerParsingU64 string8_parse_u64_decimal(const char8* restrict p)
{
    return string_parse_u64_decimal(p);
}

BUSTER_F_IMPL IntegerParsingU64 string8_parse_u64_octal(const char8* restrict p)
{
    return string_parse_u64_octal(p);
}

BUSTER_F_IMPL IntegerParsingU64 string8_parse_u64_binary(const char8* restrict p)
{
    return string_parse_u64_binary(p);
}

BUSTER_F_IMPL IntegerParsingU64 string_os_parse_u64_hexadecimal(const CharOs* pointer)
{
    return string_parse_u64_hexadecimal(pointer);
}

BUSTER_F_IMPL IntegerParsingU64 string_os_parse_u64_decimal(const CharOs* pointer)
{
    return string_parse_u64_decimal(pointer);
}

BUSTER_F_IMPL IntegerParsingU64 string_os_parse_u64_octal(const CharOs* pointer)
{
    return string_parse_u64_binary(pointer);
}

BUSTER_F_IMPL IntegerParsingU64 string_os_parse_u64_binary(const CharOs* pointer)
{
    return string_parse_u64_binary(pointer);
}

template <typename Char>
BUSTER_F_IMPL String<Char> string_join_arena(Arena* arena, Slice<String<Char>> strings, bool zero_terminate)
{
    u64 length = 0;

    for (u64 i = 0; i < strings.length; i += 1)
    {
        let string = strings.pointer[i];
        length += string.length;
    }

    let char_size = sizeof(Char);

     let pointer = (Char*)arena_allocate_bytes(arena, (length + zero_terminate) * char_size, alignof(Char));

    u64 i = 0;

    for (u64 index = 0; index < strings.length; index += 1)
    {
        let string = strings.pointer[index];
        memcpy(pointer + i, string.pointer, BUSTER_SLICE_SIZE(string));
        i += string.length;
    }

    BUSTER_CHECK(i == length);
    if (zero_terminate)
    {
        pointer[i] = 0;
    }

    return { .pointer = pointer, .length = length };
}

BUSTER_F_IMPL StringOs string_os_join_arena(Arena* arena, Slice<StringOs> strings, bool zero_terminate)
{
    return string_join_arena(arena, strings, zero_terminate);
}

BUSTER_F_IMPL String8 string8_join_arena(Arena* arena, Slice<String8> strings, bool zero_terminate)
{
    return string_join_arena(arena, strings, zero_terminate);
}

template <typename Char>
BUSTER_F_IMPL bool string_ends_with_sequence(String<Char> string, String<Char> ending)
{
    
    bool result = string.length >= ending.length;
    if (result)
    {
        let last_chunk = string_slice(string, string.length - ending.length, string.length);
        result = string_equal(last_chunk, ending);
    }
    return result;
}

BUSTER_F_IMPL bool string8_ends_with_sequence(String8 string, String8 ending)
{
    return string_ends_with_sequence(string, ending);
}

BUSTER_F_IMPL bool string16_ends_with_sequence(String16 string, String16 ending)
{
    return string_ends_with_sequence(string, ending);
}

BUSTER_F_IMPL bool string_os_ends_with_sequence(StringOs string, StringOs ending)
{
    return string_ends_with_sequence(string, ending);
}

BUSTER_F_IMPL u64 string8_array_match(Slice<String8> names, String8 name)
{
    u64 result = BUSTER_STRING_NO_MATCH;

    for (u64 i = 0; i < names.length; i += 1)
    {
        if (string8_equal(name, names.pointer[i]))
        {
            result = i;
            break;
        }
    }

    return result;
}


template <typename Char>
BUSTER_F_IMPL bool string_starts_with_sequence(String<Char> string, String<Char> sequence)
{
    bool result = string.length >= sequence.length;

    if (result)
    {
        let first_chunk = string_slice(string, 0, sequence.length);
        result = string_equal(first_chunk, sequence);
    }

    return result;
}

BUSTER_F_IMPL bool string_os_starts_with_sequence(StringOs string, StringOs sequence)
{
    return string_starts_with_sequence(string, sequence);
}

BUSTER_F_IMPL bool string8_starts_with_sequence(String8 string, String8 sequence)
{
    return string_starts_with_sequence(string, sequence);
}

template <typename Char>
BUSTER_GLOBAL_LOCAL bool string_equal(String<Char> s1, String<Char> s2)
{
    bool is_equal = s1.length == s2.length;
    if (is_equal & (s1.pointer != 0) & (s1.pointer != s2.pointer))
    {
#if BUSTER_OPTIMIZE
        is_equal = memory_compare(s1.pointer, s2.pointer, s1.length * sizeof(Char));
#else
        is_equal = slice_compare(s1, s2);
#endif
    }
    return is_equal;
}

BUSTER_F_IMPL bool string8_equal(String8 s1, String8 s2)
{
    return string_equal(s1, s2);
}

BUSTER_F_IMPL bool string16_equal(String16 s1, String16 s2)
{
    return string_equal(s1, s2);
}

BUSTER_F_IMPL bool string_os_equal(StringOs s1, StringOs s2)
{
    return string_equal(s1, s2);
}

template <typename Char>
BUSTER_GLOBAL_LOCAL u64 string_first_code_unit(String<Char> string, Char code_unit)
{
    u64 result = BUSTER_STRING_NO_MATCH;

    for (EACH_SLICE_INT(i, string))
    {
        let cu = string.pointer[i];
        if (cu == code_unit)
        {
            result = i;
            break;
        }
    }

    return result;
}

BUSTER_F_IMPL u64 string8_first_code_unit(String8 string, char8 code_unit)
{
    return string_first_code_unit(string, code_unit);
}

BUSTER_F_IMPL u64 string16_first_code_unit(String16 string, char16 code_unit)
{
    return string_first_code_unit(string, code_unit);
}

BUSTER_F_IMPL u64 string_os_first_code_unit(StringOs string, CharOs code_unit)
{
    return string_first_code_unit(string, code_unit);
}

BUSTER_F_IMPL String8 string8_from_pointer_length(const char8* pointer, u64 length)
{
    return (String8){ .pointer = (char8*)pointer, .length = length };
}

BUSTER_F_IMPL String16 string16_from_pointer_length(const char16* pointer, u64 length)
{
    return (String16){ .pointer = (char16*)pointer, .length = length };
}

BUSTER_F_IMPL String8 string16_to_string8_arena(Arena* arena, String16 s, bool null_terminate)
{
    let pointer = arena_allocate(arena, char8, s.length + null_terminate);
    for (u64 i = 0; i < s.length; i += 1)
    {
        // TODO
        pointer[i] = (u8)s.pointer[i];
    }

    if (null_terminate)
    {
        pointer[s.length] = 0;
    }

    let result = string8_from_pointer_length(pointer, s.length);
    return result;
}

BUSTER_F_IMPL String16 string8_to_string16_arena(Arena* arena, String8 s, bool null_terminate)
{
    let pointer = arena_allocate(arena, char16, s.length + null_terminate);
    for (u64 i = 0; i < s.length; i += 1)
    {
        pointer[i] = s.pointer[i];
    }

    if (null_terminate)
    {
        pointer[s.length] = 0;
    }

    let result = string16_from_pointer_length(pointer, s.length);
    return result;
}

BUSTER_F_IMPL String8 string_os_to_string8_arena(Arena* arena, StringOs string)
{
#if defined(_WIN32)
    return string16_to_string8_arena(a, s, true);
#else
    BUSTER_UNUSED(arena);
    return string;
#endif
}

BUSTER_GLOBAL_LOCAL void string_reverse(String8 s)
{
    let restrict pointer = s.pointer;
    for (u64 i = 0, reverse_i = s.length - 1; i < reverse_i; i += 1, reverse_i -= 1)
    {
        let ch = pointer[i];
        pointer[i] = pointer[reverse_i];
        pointer[reverse_i] = ch;
    }
}

template <typename Char>
BUSTER_GLOBAL_LOCAL String<Char> string_format_u64_hexadecimal(String<Char> buffer, u64 value, bool upper)
{
    String<Char> result = {};

    if (value == 0)
    {
        buffer.pointer[0] = '0';
        result = { .pointer = buffer.pointer, .length = 1};
    }
    else
    {
        let v = value;
        u64 i = 0;
        Char alpha_start = upper ? 'A' : 'a';

        while (v != 0)
        {
            let digit = v % 16;
            let ch = (Char)(digit > 9 ? (digit - 10 + alpha_start) : (digit + '0'));
            BUSTER_CHECK(i < buffer.length);
            buffer.pointer[i] = ch;
            i += 1;
            v = v / 16;
        }

        let length = i;

        result = { .pointer = buffer.pointer, .length = length };
        string_reverse(result);
    }

    return result;
}

template <typename Char>
BUSTER_GLOBAL_LOCAL String<Char> string_format_i64_decimal(String<Char> buffer, u64 value, bool treat_as_signed)
{
    String<Char> result = {};

    if (value == 0)
    {
        buffer.pointer[0] = '0';
        result = { buffer.pointer, 1};
    }
    else
    {
        u64 i = treat_as_signed;

        buffer.pointer[0] = '-';
        let v = value;

        while (v != 0)
        {
            let digit = v % 10;
            let ch = (Char)(digit + '0');
            BUSTER_CHECK(i < buffer.length);
            buffer.pointer[i] = ch;
            i += 1;
            v = v / 10;
        }

        let length = i;

        result = { buffer.pointer + treat_as_signed, length - treat_as_signed };
        string_reverse(result);
        result.pointer -= treat_as_signed;
        result.length += treat_as_signed;
    }

    return result;
}

template <typename Char>
BUSTER_GLOBAL_LOCAL String<Char> string_format_u64_octal(String<Char> buffer, u64 value)
{
    String<Char> result = {};

    if (value == 0)
    {
        buffer.pointer[0] = '0';
        result = { .pointer = buffer.pointer, .length = 1 };
    }
    else
    {
        u64 i = 0;
        let v = value;

        while (v != 0)
        {
            let digit = v % 8;
            let ch = (Char)(digit + '0');
            BUSTER_CHECK(i < buffer.length);
            buffer.pointer[i] = ch;
            i += 1;
            v = v / 8;
        }

        let length = i;

        result = { .pointer = buffer.pointer, .length = length };
        string_reverse(result);
    }

    return result;
}

template<typename Char>
BUSTER_GLOBAL_LOCAL String<Char> string_format_u64_binary(String<Char> buffer, u64 value)
{
    String<Char> result = {};

    if (value == 0)
    {
        buffer.pointer[0] = '0';
        result = { .pointer = buffer.pointer, .length = 1};
    }
    else
    {
        u64 i = 0;
        let v = value;

        while (v != 0)
        {
            let digit = v % 2;
            let ch = (Char)(digit + '0');
            BUSTER_CHECK(i < buffer.length);
            buffer.pointer[i] = ch;
            i += 1;
            v = v / 2;
        }

        let length = i;

        result = { .pointer = buffer.pointer, .length = length };
        string_reverse(result);
    }

    return result;
}

template<typename Char>
BUSTER_GLOBAL_LOCAL String<Char> string_slice(String<Char> slice, u64 start, u64 end)
{
    return ((String<Char>){ .pointer = (slice).pointer + (start), .length = (end) - (start) });
}

template<typename Char>
BUSTER_GLOBAL_LOCAL void string_append_slice(Arena* arena, String<Char> string)
{
    let destination = arena_allocate(arena, Char, string.length);
    memcpy(destination, string.pointer, sizeof(Char) * string.length);
}

template<typename DestinationChar, typename SourceChar>
BUSTER_GLOBAL_LOCAL void string_append_slice_different(Arena* arena, String<SourceChar> string)
{
    let destination = arena_allocate(arena, DestinationChar, string.length);

    if constexpr (BUSTER_TYPE_EQUAL(DestinationChar, SourceChar))
    {
        memcpy(destination, string.pointer, sizeof(SourceChar) * string.length);
    }
    else
    {
        for (u64 i = 0; i < string.length; i += 1)
        {
            destination[i] = (DestinationChar)string.pointer[i];
        }
    }
}

template<typename Char>
BUSTER_GLOBAL_LOCAL void string_append_repeated_code_unit(Arena* arena, Char code_unit, u64 code_unit_count)
{
    if (code_unit_count != 0)
    {
        let destination = arena_allocate(arena, Char, code_unit_count);
        for (u64 i = 0; i < code_unit_count; i += 1)
        {
            destination[i] = code_unit;
        }
    }
}

BUSTER_F_IMPL String8 string8_slice(String8 slice, u64 start, u64 end)
{
    return string_slice(slice, start, end);
}

BUSTER_F_IMPL String16 string16_slice(String16 slice, u64 start, u64 end)
{
    return string_slice(slice, start, end);
}

BUSTER_F_IMPL StringOs string_os_slice(StringOs slice, u64 start, u64 end)
{
    return string_slice(slice, start, end);
}

template<typename Char>
BUSTER_GLOBAL_LOCAL String<Char> string_format_va(Arena* arena, String<Char> format, va_list variable_arguments)
{
    let original_position = arena->position;

    u64 format_index = 0;

    while (format_index < format.length)
    {
        bool escaped_left_brace = format_index + 1 < format.length && format.pointer[format_index] == '{' && format.pointer[format_index + 1] == '{';
        bool escaped_right_brace = format_index + 1 < format.length && format.pointer[format_index] == '}' && format.pointer[format_index + 1] == '}';

        if (escaped_left_brace || escaped_right_brace)
        {
            *arena_allocate(arena, Char, 1) = format.pointer[format_index];
            format_index += 2;
        }
        else if (format.pointer[format_index] != '{')
        {
            *arena_allocate(arena, Char, 1) = format.pointer[format_index];
            format_index += 1;
        }
        else
        {
            // '{' is found
            let iteration_left_format_string = string_slice(format, format_index, format.length);
            let iteration_left_format_string_plus_one = string_slice(iteration_left_format_string, 1, iteration_left_format_string.length);
            let left_brace_index = string_first_code_unit(iteration_left_format_string_plus_one, (Char)'{');
            let right_brace_index = string_first_code_unit(iteration_left_format_string, (Char)'}');

            bool has_right_brace = right_brace_index != BUSTER_STRING_NO_MATCH;
            bool nested_left_brace_before_right_brace = left_brace_index != BUSTER_STRING_NO_MATCH && right_brace_index > left_brace_index;

            if (has_right_brace && !nested_left_brace_before_right_brace)
            {
                let whole_format_string = string_slice(iteration_left_format_string, 0, right_brace_index + 1);
                format_index += whole_format_string.length;

                ENUM(FormatTypeId, 
                        STRING_OS,
                        STRING_OS_LIST,
                        STRING8,
                        STRING16,
                        CHAR_OS,
                        CHAR8,
                        UNSIGNED_INTEGER_8,
                        UNSIGNED_INTEGER_16,
                        UNSIGNED_INTEGER_32,
                        UNSIGNED_INTEGER_64,
                        UNSIGNED_INTEGER_128,
                        SIGNED_INTEGER_8,
                        SIGNED_INTEGER_16,
                        SIGNED_INTEGER_32,
                        SIGNED_INTEGER_64,
                        SIGNED_INTEGER_128,
                        OS_ERROR);
                String<Char> possible_format_strings[(u64)FormatTypeId::Count] = {
                    [(u64)FormatTypeId::STRING_OS] = STRING_OF_CHAR("SOs", Char),
                    [(u64)FormatTypeId::STRING_OS_LIST] = STRING_OF_CHAR("SOsL", Char),
                    [(u64)FormatTypeId::STRING8] = STRING_OF_CHAR("S8", Char),
                    [(u64)FormatTypeId::STRING16] = STRING_OF_CHAR("S16", Char),
                    [(u64)FormatTypeId::CHAR_OS] = STRING_OF_CHAR("CharOs", Char),
                    [(u64)FormatTypeId::CHAR8] = STRING_OF_CHAR("char8", Char),
                    [(u64)FormatTypeId::UNSIGNED_INTEGER_8] = STRING_OF_CHAR("u8", Char),
                    [(u64)FormatTypeId::UNSIGNED_INTEGER_16] = STRING_OF_CHAR("u16", Char),
                    [(u64)FormatTypeId::UNSIGNED_INTEGER_32] = STRING_OF_CHAR("u32", Char),
                    [(u64)FormatTypeId::UNSIGNED_INTEGER_64] = STRING_OF_CHAR("u64", Char),
                    [(u64)FormatTypeId::UNSIGNED_INTEGER_128] = STRING_OF_CHAR("u128", Char),
                    [(u64)FormatTypeId::SIGNED_INTEGER_8] = STRING_OF_CHAR("s8", Char),
                    [(u64)FormatTypeId::SIGNED_INTEGER_16] = STRING_OF_CHAR("s16", Char),
                    [(u64)FormatTypeId::SIGNED_INTEGER_32] = STRING_OF_CHAR("s32", Char),
                    [(u64)FormatTypeId::SIGNED_INTEGER_64] = STRING_OF_CHAR("s64", Char),
                    [(u64)FormatTypeId::SIGNED_INTEGER_128] = STRING_OF_CHAR("s128", Char),
                    [(u64)FormatTypeId::OS_ERROR] = STRING_OF_CHAR("EOs", Char),
                };

                let first_format = string_first_code_unit(whole_format_string, (Char)':');
                bool there_is_format_modifiers = first_format != BUSTER_STRING_NO_MATCH;
                let this_format_string_length = there_is_format_modifiers ? first_format : whole_format_string.length - 1; // Avoid final right brace
                let this_format_string = string_slice(whole_format_string,
                        1, // Avoid starting left brace
                        this_format_string_length);

                u64 i;
                for (i = 0; i < BUSTER_ARRAY_LENGTH(possible_format_strings); i += 1)
                {
                    let possible_format_string = possible_format_strings[i];
                    if (string_equal(this_format_string, possible_format_string))
                    {
                        break;
                    }
                }

                ENUM(IntegerFormatKind,
                        FORMAT_KIND_DECIMAL,
                        FORMAT_KIND_BINARY,
                        FORMAT_KIND_OCTAL,
                        FORMAT_KIND_HEXADECIMAL_LOWER,
                        FORMAT_KIND_HEXADECIMAL_UPPER);

                ENUM(FormatSpecifier,
                        FORMAT_SPECIFIER_D,
                        FORMAT_SPECIFIER_X_UPPER,
                        FORMAT_SPECIFIER_X_LOWER,
                        FORMAT_SPECIFIER_O,
                        FORMAT_SPECIFIER_B,
                        FORMAT_SPECIFIER_WIDTH,
                        FORMAT_SPECIFIER_NO_PREFIX,
                        FORMAT_SPECIFIER_DIGIT_GROUP);

                let format_type_id = (FormatTypeId)i;
                bool prefix = false;
                bool prefix_set = false;
                bool digit_group = false;
                u64 width = 0;
                Char width_character = '0';
                bool width_natural_extension = false;
#define BUSTER_FORMAT_INTEGER_MAX_WIDTH (u64)(64)

                let format_kind = IntegerFormatKind::FORMAT_KIND_DECIMAL;
                bool integer_format_set = false;

                if (there_is_format_modifiers)
                {
                    String<Char> possible_format_specifier_strings[] = {
                        [(u64)FormatSpecifier::FORMAT_SPECIFIER_D] = STRING_OF_CHAR("d", Char),
                        [(u64)FormatSpecifier::FORMAT_SPECIFIER_X_UPPER] = STRING_OF_CHAR("X", Char),
                        [(u64)FormatSpecifier::FORMAT_SPECIFIER_X_LOWER] = STRING_OF_CHAR("x", Char),
                        [(u64)FormatSpecifier::FORMAT_SPECIFIER_O] = STRING_OF_CHAR("o", Char),
                        [(u64)FormatSpecifier::FORMAT_SPECIFIER_B] = STRING_OF_CHAR("b", Char),
                        [(u64)FormatSpecifier::FORMAT_SPECIFIER_WIDTH] = STRING_OF_CHAR("width", Char),
                        [(u64)FormatSpecifier::FORMAT_SPECIFIER_NO_PREFIX] = STRING_OF_CHAR("no_prefix", Char),
                        [(u64)FormatSpecifier::FORMAT_SPECIFIER_DIGIT_GROUP] = STRING_OF_CHAR("digit_group", Char),
                    };
                    static_assert(BUSTER_ARRAY_LENGTH(possible_format_specifier_strings) == (u64)FormatSpecifier::Count);

                    let whole_format_specifiers_string = string_slice(whole_format_string, first_format + 1, whole_format_string.length - 1);
                    BUSTER_CHECK(whole_format_specifiers_string.length <= whole_format_string.length);
                    u64 format_specifier_string_i = 0;

                    while (format_specifier_string_i < whole_format_specifiers_string.length && whole_format_specifiers_string.pointer[format_specifier_string_i] != '}')
                    {
                        let iteration_left_format_specifiers_string = BUSTER_SLICE_START(whole_format_specifiers_string, format_specifier_string_i);
                        BUSTER_CHECK(iteration_left_format_specifiers_string.length <= whole_format_specifiers_string.length);
                        let equal_index = string_first_code_unit(iteration_left_format_specifiers_string, (Char)'=');
                        let comma_index = string_first_code_unit(iteration_left_format_specifiers_string, (Char)',');
                        let format_specifier_name_end = BUSTER_MIN(equal_index, comma_index);
                        let string_left = format_specifier_name_end == BUSTER_STRING_NO_MATCH;
                        format_specifier_name_end = string_left ? iteration_left_format_specifiers_string.length : format_specifier_name_end;
                        let next_character = string_left ? 0 : (equal_index < comma_index ? '=' : ',');

                        let format_name = string_slice(iteration_left_format_specifiers_string, 0, format_specifier_name_end);
                        format_specifier_string_i += format_name.length + !string_left;
                        let left_format_specifiers_string = BUSTER_SLICE_START(iteration_left_format_specifiers_string, format_name.length + !string_left);
                        BUSTER_CHECK(left_format_specifiers_string.length <= iteration_left_format_specifiers_string.length);

                        FormatSpecifier format_i;
                        for (EACH_ENUM_FREE(FormatSpecifier, format_i))
                        {
                            let candidate_format_specifier = possible_format_specifier_strings[(u64)format_i];
                            if (string_equal(format_name, candidate_format_specifier))
                            {
                                break;
                            }
                        }

                        let format_specifier = (FormatSpecifier)format_i;
                        switch (format_specifier)
                        {
                            break; case FormatSpecifier::FORMAT_SPECIFIER_D:
                            {
                                format_kind = IntegerFormatKind::FORMAT_KIND_DECIMAL;
                                integer_format_set = true;
                            }
                            break; case FormatSpecifier::FORMAT_SPECIFIER_X_UPPER:
                            {
                                format_kind = IntegerFormatKind::FORMAT_KIND_HEXADECIMAL_UPPER;
                                integer_format_set = true;
                            }
                            break; case FormatSpecifier::FORMAT_SPECIFIER_X_LOWER:
                            {
                                format_kind = IntegerFormatKind::FORMAT_KIND_HEXADECIMAL_LOWER;
                                integer_format_set = true;
                            }
                            break; case FormatSpecifier::FORMAT_SPECIFIER_O:
                            {
                                format_kind = IntegerFormatKind::FORMAT_KIND_OCTAL;
                                integer_format_set = true;
                            }
                            break; case FormatSpecifier::FORMAT_SPECIFIER_B:
                            {
                                format_kind = IntegerFormatKind::FORMAT_KIND_BINARY;
                                integer_format_set = true;
                            }
                            break; case FormatSpecifier::FORMAT_SPECIFIER_WIDTH:
                            {
                                if (next_character == '=')
                                {
                                    if (left_format_specifiers_string.pointer[0] == '[')
                                    {
                                        width_character = left_format_specifiers_string.pointer[1];

                                        if (left_format_specifiers_string.pointer[2] == ',')
                                        {
                                            let right_bracket_index = string_first_code_unit(left_format_specifiers_string, (Char)']');

                                            if (right_bracket_index != BUSTER_STRING_NO_MATCH)
                                            {
                                                u64 width_start = 3;
                                                let width_count_string = string_slice(left_format_specifiers_string, width_start, right_bracket_index);
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
                                                    let width_count_parsing = string_parse_u64_decimal(width_count_string.pointer);

                                                    if (width_count_parsing.length == width_count_string.length && width_count_parsing.value != 0)
                                                    {
                                                        width = width_count_parsing.value;

                                                        bool more_characters = right_bracket_index + 1 < left_format_specifiers_string.length;
                                                        if (more_characters)
                                                        {
                                                            let next_ch = left_format_specifiers_string.pointer[character_to_advance_count];
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
                            break; case FormatSpecifier::FORMAT_SPECIFIER_NO_PREFIX:
                            {
                                prefix = false;
                                prefix_set = true;
                            }
                            break; case FormatSpecifier::FORMAT_SPECIFIER_DIGIT_GROUP:
                            {
                                digit_group = true;
                            }
                            break; case FormatSpecifier::Count:
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
                    break; case FormatTypeId::STRING_OS_LIST:
                    {
                        let string_os_list = va_arg(variable_arguments, StringOsList);
                        let it = string_os_list_iterator_initialize(string_os_list);

                        u64 full_length = 0;

                        // TODO: support Unicode
                        StringOs string;
                        while ((string = string_os_list_iterator_next(&it)).pointer)
                        {
                            full_length += string.length + 1; // space
                        }

                        full_length -= full_length != 0;

                        let destination = arena_allocate(arena, Char, full_length);

                        u64 offset = 0;

                        it = string_os_list_iterator_initialize(string_os_list);
                        while ((string = string_os_list_iterator_next(&it)).pointer)
                        {
                            for (u64 string_i = 0; string_i < string.length; string_i += 1)
                            {
                                destination[offset + string_i] = (Char)string.pointer[string_i];
                            }

                            offset += string.length;
                            if (BUSTER_LIKELY(offset < full_length))
                            {
                                destination[offset] = (Char)' ';
                                offset += 1;
                            }
                        }
                    }
                    break; case FormatTypeId::STRING_OS:
                    {
                        let string = va_arg(variable_arguments, StringOs);
                        string_append_slice_different<Char, CharOs>(arena, string);
                    }
                    break; case FormatTypeId::STRING8:
                    {
                        let string = va_arg(variable_arguments, String8);
                        string_append_slice_different<Char, char8>(arena, string);
                    }
                    break; case FormatTypeId::STRING16:
                    {
                        let string16 = va_arg(variable_arguments, String16);
                        string_append_slice_different<Char, char16>(arena, string16);
                    }
                    break; case FormatTypeId::CHAR_OS:
                    {
                        let os_char = (CharOs)va_arg(variable_arguments, int);
                        *arena_allocate(arena, Char, 1) = (Char)os_char;
                    }
                    break; case FormatTypeId::CHAR8:
                    {
                        let ch = (char8)va_arg(variable_arguments, int);
                        *arena_allocate(arena, Char, 1) = (Char)ch;
                    }
                    break; case FormatTypeId::UNSIGNED_INTEGER_8: case FormatTypeId::UNSIGNED_INTEGER_16: case FormatTypeId::UNSIGNED_INTEGER_32: case FormatTypeId::UNSIGNED_INTEGER_64:
                    {
                        prefix = prefix && format_kind != IntegerFormatKind::Count;

                        Char prefix_second_character;
                        switch (format_kind)
                        {
                            break; case IntegerFormatKind::FORMAT_KIND_DECIMAL: prefix_second_character = 'd';
                            break; case IntegerFormatKind::FORMAT_KIND_BINARY: prefix_second_character = 'b';
                            break; case IntegerFormatKind::FORMAT_KIND_OCTAL: prefix_second_character = 'o';
                            break; case IntegerFormatKind::FORMAT_KIND_HEXADECIMAL_LOWER: case IntegerFormatKind::FORMAT_KIND_HEXADECIMAL_UPPER: prefix_second_character = 'x';
                            break; case IntegerFormatKind::Count: BUSTER_UNREACHABLE();
                        }

                        Char prefix_buffer[] =
                        {
                            '0',
                            prefix_second_character,
                        };

                        if (format_kind == IntegerFormatKind::Count)
                        {
                            format_kind = IntegerFormatKind::FORMAT_KIND_DECIMAL;
                        }

                        u64 value;
                        u64 value_size;
                        switch (format_type_id)
                        {
                            break; case FormatTypeId::UNSIGNED_INTEGER_8:
                            {
                                value = (u8)va_arg(variable_arguments, u32);
                                value_size = sizeof(u8);
                            }
                            break; case FormatTypeId::UNSIGNED_INTEGER_16:
                            {
                                value = (u16)va_arg(variable_arguments, u32);
                                value_size = sizeof(u16);
                            }
                            break; case FormatTypeId::UNSIGNED_INTEGER_32:
                            {
                                value = va_arg(variable_arguments, u32);
                                value_size = sizeof(u32);
                            }
                            break; case FormatTypeId::UNSIGNED_INTEGER_64:
                            {
                                value = va_arg(variable_arguments, u64);
                                value_size = sizeof(u64);
                            }
                            break; default: BUSTER_UNREACHABLE();
                        }

                        // let prefix_character_count = (u64)prefix << 1;
                        Char integer_format_buffer[(sizeof(u64) * 8) + BUSTER_FORMAT_INTEGER_MAX_WIDTH + 2];
                        String<Char> number_string_buffer = BUSTER_ARRAY_TO_SLICE(integer_format_buffer);

                        String<Char> format_result;

                        switch (format_kind)
                        {
                            break; case IntegerFormatKind::FORMAT_KIND_DECIMAL: format_result = string_format_i64_decimal(number_string_buffer, value, false);
                            break; case IntegerFormatKind::FORMAT_KIND_BINARY: format_result = string_format_u64_binary(number_string_buffer, value);
                            break; case IntegerFormatKind::FORMAT_KIND_OCTAL: format_result = string_format_u64_octal(number_string_buffer, value);
                            break; case IntegerFormatKind::FORMAT_KIND_HEXADECIMAL_LOWER: case IntegerFormatKind::FORMAT_KIND_HEXADECIMAL_UPPER: format_result = string_format_u64_hexadecimal(number_string_buffer, value, format_kind == IntegerFormatKind::FORMAT_KIND_HEXADECIMAL_UPPER);
                            break; case IntegerFormatKind::Count: BUSTER_UNREACHABLE();
                        }

                        number_string_buffer.length = format_result.length;

                        u64 integer_max_width = 0;

                        u64 digit_group_character_count;

                        switch (format_kind)
                        {
                            break; case IntegerFormatKind::FORMAT_KIND_DECIMAL:
                            {
                                prefix_second_character = 'd';
                                switch (format_type_id)
                                {
                                    break; case FormatTypeId::UNSIGNED_INTEGER_8: integer_max_width = 3;
                                    break; case FormatTypeId::UNSIGNED_INTEGER_16: integer_max_width = 5;
                                    break; case FormatTypeId::UNSIGNED_INTEGER_32: integer_max_width = 10;
                                    break; case FormatTypeId::UNSIGNED_INTEGER_64: integer_max_width = 20;
                                    break; default: BUSTER_UNREACHABLE();
                                }
                                digit_group_character_count = 3;
                            }
                            break; case IntegerFormatKind::FORMAT_KIND_BINARY:
                            {
                                prefix_second_character = 'b';
                                integer_max_width = value_size * 8;
                                digit_group_character_count = 8;
                            }
                            break; case IntegerFormatKind::FORMAT_KIND_OCTAL:
                            {
                                prefix_second_character = 'o';
                                switch (format_type_id)
                                {
                                    break; case FormatTypeId::UNSIGNED_INTEGER_8: integer_max_width = 3;
                                    break; case FormatTypeId::UNSIGNED_INTEGER_16: integer_max_width = 6;
                                    break; case FormatTypeId::UNSIGNED_INTEGER_32: integer_max_width = 11;
                                    break; case FormatTypeId::UNSIGNED_INTEGER_64: integer_max_width = 22;
                                    break; default: BUSTER_UNREACHABLE();
                                }
                                digit_group_character_count = 3;
                            }
                            break; case IntegerFormatKind::FORMAT_KIND_HEXADECIMAL_LOWER: case IntegerFormatKind::FORMAT_KIND_HEXADECIMAL_UPPER:
                            {
                                prefix_second_character = 'x';
                                integer_max_width = value_size * 2;
                                digit_group_character_count = 2;
                            }
                            break; case IntegerFormatKind::Count: BUSTER_UNREACHABLE();
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
                                string_append_slice<Char>(arena, BUSTER_ARRAY_TO_SLICE(prefix_buffer));
                            }

                            if (width_character_count)
                            {
                                string_append_repeated_code_unit(arena, (Char)width_character, width_character_count);
                            }

                            if (separator_character_count)
                            {
                                Char separator_character = format_kind == IntegerFormatKind::FORMAT_KIND_DECIMAL ? (Char)'.' : (Char)'_';
                                let remainder = number_string_buffer.length % digit_group_character_count;
                                if (remainder)
                                {
                                    string_append_slice<Char>(arena, (String<Char>){ .pointer = number_string_buffer.pointer, .length = remainder });
                                    *arena_allocate(arena, Char, 1) = separator_character;
                                }

                                u64 source_i;
                                for (source_i = remainder; source_i < number_string_buffer.length - digit_group_character_count; source_i += digit_group_character_count)
                                {
                                    string_append_slice<Char>(arena, (String<Char>){ .pointer = number_string_buffer.pointer + source_i, .length = digit_group_character_count });
                                    *arena_allocate(arena, Char, 1) = separator_character;
                                }

                                string_append_slice<Char>(arena, (String<Char>){ .pointer = number_string_buffer.pointer + number_string_buffer.length - digit_group_character_count, .length = digit_group_character_count });
                            }
                            else
                            {
                                string_append_slice<Char>(arena, number_string_buffer);
                            }
                        }
                    }
                    break; case FormatTypeId::SIGNED_INTEGER_8: case FormatTypeId::SIGNED_INTEGER_16: case FormatTypeId::SIGNED_INTEGER_32: case FormatTypeId::SIGNED_INTEGER_64:
                    {
                        if (format_kind == IntegerFormatKind::Count)
                        {
                            format_kind = IntegerFormatKind::FORMAT_KIND_DECIMAL;
                        }

                        s64 value;
                        switch (format_type_id)
                        {
                            break; case FormatTypeId::SIGNED_INTEGER_8: value = (s8)va_arg(variable_arguments, int);
                            break; case FormatTypeId::SIGNED_INTEGER_16: value = (s16)va_arg(variable_arguments, int);
                            break; case FormatTypeId::SIGNED_INTEGER_32: value = va_arg(variable_arguments, s32);
                            break; case FormatTypeId::SIGNED_INTEGER_64: value = va_arg(variable_arguments, s64);
                            break; default: BUSTER_UNREACHABLE();
                        }

                        Char integer_format_buffer[sizeof(u64) * 8 + 1]; // 1 for the sign (needed?)
                        String<Char> string_buffer = BUSTER_ARRAY_TO_SLICE(integer_format_buffer);
                        String<Char> format_result;

                        switch (format_kind)
                        {
                            break; case IntegerFormatKind::FORMAT_KIND_DECIMAL: format_result = string_format_i64_decimal(string_buffer, (u64)((value < 0) ? (-value) : value), value < 0);
                            break; case IntegerFormatKind::FORMAT_KIND_BINARY: format_result = string_format_u64_binary(string_buffer, (u64)value);
                            break; case IntegerFormatKind::FORMAT_KIND_OCTAL: format_result = string_format_u64_octal(string_buffer, (u64)value);
                            break; case IntegerFormatKind::FORMAT_KIND_HEXADECIMAL_LOWER: case IntegerFormatKind::FORMAT_KIND_HEXADECIMAL_UPPER: format_result = string_format_u64_hexadecimal(string_buffer, (u64)value, format_kind == IntegerFormatKind::FORMAT_KIND_HEXADECIMAL_UPPER);
                            break; case IntegerFormatKind::Count: BUSTER_UNREACHABLE();
                        }

                        string_append_slice<Char>(arena, format_result);
                    }
                    break; case FormatTypeId::UNSIGNED_INTEGER_128:
                    {
                        // TODO:
                    }
                    break; case FormatTypeId::SIGNED_INTEGER_128:
                    {
                        // TODO:
                    }
                    break; case FormatTypeId::OS_ERROR:
                    {
                        let os_error = va_arg(variable_arguments, OsError);
                        CharOs error_buffer[BUSTER_OS_ERROR_BUFFER_MAX_LENGTH];
                        let error_string = os_error_write_message((StringOs)BUSTER_ARRAY_TO_SLICE(error_buffer), os_error);

                        // written = result.real_buffer_index + error_string.length <= buffer_slice.length;
                        //
                        // if (written)
                        {
                            string_append_slice_different<Char, CharOs>(arena, error_string);
                        }
                    }
                    break; case FormatTypeId::Count:
                    {
                        // if (result.real_buffer_index < buffer_slice.length)
                        {
                            *arena_allocate(arena, Char, 1) = (Char)'{';
                        }

                        {
                            let pointer = arena_allocate(arena, Char, whole_format_string.length + 1);
                            memcpy(pointer, whole_format_string.pointer, sizeof(Char) * whole_format_string.length);
                            pointer[whole_format_string.length] = (Char)'}';
                        }
                    }
                }
            }
            else
            {
                __builtin_trap();
            }
        }
    }

    return (String<Char>){ .pointer = (Char*)((u8*)arena + original_position), .length = (arena->position - original_position) / sizeof(Char) };
}

template<typename Char>
BUSTER_F_IMPL String<Char> string_duplicate_arena(Arena* arena, String<Char> string, bool zero_terminate)
{
    String<Char> result = { .pointer = arena_allocate(arena, Char, string.length + zero_terminate), .length = string.length };
    memcpy(result.pointer, string.pointer, sizeof(Char) * string.length);

    if (zero_terminate)
    {
        result.pointer[string.length] = 0;
    }

    return result;
}

BUSTER_F_IMPL String8 string8_duplicate_arena(Arena* arena, String8 string, bool zero_terminate)
{
    return string_duplicate_arena(arena, string, zero_terminate);
}

BUSTER_F_IMPL String16 string16_duplicate_arena(Arena* arena, String16 string, bool zero_terminate)
{
    return string_duplicate_arena(arena, string, zero_terminate);
}

BUSTER_F_IMPL StringOs string_os_duplicate_arena(Arena* arena, StringOs string, bool zero_terminate)
{
    return string_duplicate_arena(arena, string, zero_terminate);
}

BUSTER_F_IMPL String8 string8_format_va(Arena* arena, String8 format, va_list variable_arguments)
{
    return string_format_va(arena, format, variable_arguments);
}

BUSTER_F_IMPL String16 string16_format_va(Arena* arena, String16 format, va_list variable_arguments)
{
    return string_format_va(arena, format, variable_arguments);
}

BUSTER_F_IMPL StringOs string_os_format_va(Arena* arena, StringOs format, va_list variable_arguments)
{
#if defined(_WIN32)
    return string16_format_va(arena, format, variable_arguments);
#else
    return string8_format_va(arena, format, variable_arguments);
#endif
}

template <typename Char>
BUSTER_GLOBAL_LOCAL String<Char> string_format(Arena* arena, String<Char> format, ...)
{
    va_list variable_arguments;
    va_start(variable_arguments, format);
    let result = string_format_va(arena, format, variable_arguments);
    va_end(variable_arguments);

    return result;
}

BUSTER_F_IMPL String8 string8_format(Arena* arena, String8 format, ...)
{
    va_list variable_arguments;
    va_start(variable_arguments, format);
    let result = string8_format_va(arena, format, variable_arguments);
    va_end(variable_arguments);

    return result;
}

BUSTER_F_IMPL String8 string8_format_z(Arena* arena, String8 format, ...)
{
    va_list variable_arguments;
    va_start(variable_arguments, format);
    let result = string8_format_va(arena, format, variable_arguments);
    va_end(variable_arguments);
    *arena_allocate(arena, char8, 1) = 0;

    return result;
}

BUSTER_F_IMPL String16 string16_format(Arena* arena, String16 format, ...)
{
    va_list variable_arguments;
    va_start(variable_arguments, format);
    let result = string16_format_va(arena, format, variable_arguments);
    va_end(variable_arguments);

    return result;
}

BUSTER_F_IMPL StringOs string_os_format(Arena* arena, StringOs format, ...)
{
    va_list variable_arguments;
    va_start(variable_arguments, format);
    let result = string_os_format_va(arena, format, variable_arguments);
    va_end(variable_arguments);

    return result;
}

BUSTER_F_IMPL void string8_print(String8 format, ...)
{
    let scratch = scratch_begin(0, 0);
    va_list variable_arguments;
    va_start(variable_arguments, format);
    let string = string8_format_va(scratch.arena, format, variable_arguments);
    va_end(variable_arguments);

    if (string.length)
    {
        *arena_allocate(scratch.arena, char8, 1) = 0;
        os_file_write(os_get_stdout(), BUSTER_SLICE_TO_BYTE_SLICE(string));
    }
}

template<typename Char>
BUSTER_F_IMPL u64 string_first_sequence(String<Char> s, String<Char> sub)
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
            let chunk = string_slice(s, i, i + sub.length);
            if (string_equal(chunk, sub))
            {
                result = i;
                break;
            }
        }
    }

    return result;
}

BUSTER_F_IMPL u64 string8_first_sequence(String8 string, String8 sequence)
{
    return string_first_sequence(string, sequence);
}

BUSTER_F_IMPL u64 string16_first_sequence(String16 string, String16 sequence)
{
    return string_first_sequence(string, sequence);
}

BUSTER_F_IMPL u64 string_os_first_sequence(StringOs string, StringOs sequence)
{
    return string_first_sequence(string, sequence);
}

template <typename Char>
BUSTER_GLOBAL_LOCAL void string_reverse(String<Char> string)
{
    let restrict pointer = string.pointer;
    for (u64 i = 0, reverse_i = string.length - 1; i < reverse_i; i += 1, reverse_i -= 1)
    {
        let ch = pointer[i];
        pointer[i] = pointer[reverse_i];
        pointer[reverse_i] = ch;
    }
}

BUSTER_F_IMPL u64 string16_length(const char16* s)
{
    let it = s;
    while (*it++){};
    return (u64)(it - s);
}

BUSTER_F_IMPL IntegerParsingU64 string16_parse_u64_hexadecimal(const char16* restrict p)
{
    return string_parse_u64_hexadecimal(p);
}

BUSTER_F_IMPL IntegerParsingU64 string16_parse_u64_decimal(const char16* restrict p)
{
    return string_parse_u64_decimal(p);
}

BUSTER_F_IMPL IntegerParsingU64 string16_parse_u64_octal(const char16* restrict p)
{
    return string_parse_u64_octal(p);
}

BUSTER_F_IMPL IntegerParsingU64 string16_parse_u64_binary(const char16* restrict p)
{
    return string_parse_u64_binary(p);
}

BUSTER_F_IMPL StringOsListIterator string_os_list_iterator_initialize(StringOsList list)
{
    return (StringOsListIterator) {
        .list = list,
    };
}

BUSTER_F_IMPL u64 string8_length(const char8* pointer)
{
    u64 result = 0;

    if (pointer)
    {
        result = __builtin_strlen(pointer);
    }

    return result;
}

template <typename Char>
BUSTER_F_IMPL u64 string_length(const Char* pointer)
{
    u64 result;
    if constexpr (BUSTER_TYPE_EQUAL(Char, char8))
    {
        result = string8_length(pointer);
    }
    else
    {
        result = string16_length(pointer);
    }

    return result;
}

BUSTER_F_IMPL String8 string8_from_pointer(const char8* pointer)
{
    return (String8){ .pointer = (char8*)pointer, .length = string8_length(pointer) };
}

BUSTER_F_IMPL StringOs string_os_from_pointer(const CharOs* pointer)
{
#if defined(_WIN32)
    return string16_from_pointer(pointer);
#else
    return string8_from_pointer(pointer);
#endif
}

BUSTER_F_IMPL StringOs string_os_list_iterator_next(StringOsListIterator* iterator)
{
    StringOs result = {};
    let list = iterator->list;
    let original_position = iterator->position;
    let position = original_position;

    let current = list[position];
    if (current)
    {
#if defined(_WIN32)
        let original_pointer = &list[position];
        let pointer = original_pointer;
        if (*pointer == '"')
        {
            // TODO: handle escape
            let double_quote = raw_string16_first_code_unit(pointer + 1, '"');
            if (double_quote == BUSTER_STRING_NO_MATCH)
            {
                return result;
            }

            position += double_quote + 1 + 1;
            pointer = &list[position];
        }

        let space = raw_string16_first_code_unit(pointer, ' ');
        let is_space = space != BUSTER_STRING_NO_MATCH;
        space = is_space ? space : 0;
        position += space;
        position += is_space ? 0 : string16_length(pointer);
        let length = position - original_position;

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


BUSTER_F_IMPL StringOs string_os_from_pointer_length(CharOs* pointer, u64 length)
{
#if defined(_WIN32)
    return string16_from_pointer_length(pointer, length);
#else
    return string8_from_pointer_length(pointer, length);
#endif
}

BUSTER_F_IMPL StringOsList string_os_list_builder_append(OsArgumentBuilder* builder, StringOs arg)
{
#if defined(_WIN32)
    let result = string_os_duplicate_arena(builder->arena, arg, true);
    if (result.pointer)
    {
        result.pointer[arg.length] = ' ';
    }
    return result.pointer;
#else
    let result = arena_allocate(builder->arena, CharOs*, 1);
    if (result)
    {
        *result = (CharOs*)arg.pointer;
    }
    return result;
#endif
}

BUSTER_F_IMPL OsArgumentBuilder* string_os_list_builder_create(Arena* arena, StringOs s)
{
    let position = arena->position;
    let argument_builder = arena_allocate(arena, OsArgumentBuilder, 1);
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

BUSTER_F_IMPL StringOsList string_os_list_builder_end(OsArgumentBuilder* restrict builder)
{
#if defined(_WIN32)
    *(CharOs*)((u8*)builder->arena + builder->arena->position - sizeof(CharOs)) = 0;
#else
    string_os_list_builder_append(builder, (StringOs){});
#endif
    return builder->argv;
}

BUSTER_F_IMPL StringOsList string_os_list_create_from(Arena* arena, Slice<StringOs> arguments)
{
#if defined(_WIN32)
    u64 allocation_length = 0;

    for (u64 i = 0; i < arguments.length; i += 1)
    {
        allocation_length += arguments.pointer[i].length + 1;
    }

    let allocation = arena_allocate(arena, CharOs, allocation_length);

    for (u64 source_i = 0, destination_i = 0; source_i < arguments.length; source_i += 1)
    {
        let source_argument = arguments.pointer[source_i];
        memcpy(&allocation[destination_i], source_argument.pointer, BUSTER_SLICE_SIZE(source_argument));
        destination_i += source_argument.length;
        allocation[destination_i] = ' ';
        destination_i += 1;
    }

    allocation[allocation_length - 1] = 0;

    return allocation;
#else
    let list = arena_allocate(arena, CharOs*, arguments.length + 1);

    for (u64 i = 0; i < arguments.length; i += 1)
    {
        list[i] = arguments.pointer[i].pointer;
    }

    list[arguments.length] = 0;

    return list;
#endif
}

BUSTER_F_IMPL StringOsList string_os_list_duplicate_and_substitute_first_argument(Arena* arena, StringOsList old_arguments, StringOs new_first_argument, Slice<StringOs> extra_arguments)
{
#if defined(_WIN32)
    let space_index = raw_string16_first_code_unit(old_arguments, ' ');
    let old_argument_length = string16_length(old_arguments);
    let first_argument_end = space_index == BUSTER_STRING_NO_MATCH ? old_argument_length : space_index;
    u64 extra_length = 0;
    for (u64 i = 0; i < extra_arguments.length; i += 1)
    {
        let extra_argument = extra_arguments.pointer[i];
        extra_length += extra_argument.length + 1;
    }

    extra_length -= extra_length != 0;

    let new_length = new_first_argument.length + (old_argument_length - first_argument_end + (extra_length == 0)) + extra_length;
    let new_arguments = arena_allocate(arena, CharOs, new_length + 1);

    let char_size = sizeof(new_first_argument.pointer[0]);
    u64 i = 0;
    let copy_length = new_first_argument.length;
    memcpy(new_arguments + i, new_first_argument.pointer, copy_length * char_size);
    i += copy_length;

    new_arguments[i] = ' ';
    i += 1;

    if (first_argument_end != old_argument_length)
    {
        copy_length = old_argument_length - first_argument_end;
        memcpy(new_arguments + i, old_arguments + first_argument_end, char_size * copy_length);
        i += copy_length;

        new_arguments[i] = ' ';
        i += 1;
    }

    for (u64 extra_i = 0; extra_i < extra_arguments.length; extra_i += 1)
    {
        let extra_argument = extra_arguments.pointer[extra_i];
        memcpy(new_arguments + i, extra_argument.pointer, char_size * extra_argument.length);
        i += extra_argument.length;

        new_arguments[i] = ' ';
        i += 1;
    }

    new_arguments[new_length] = 0;

    return new_arguments;
#else
    let it = string_os_list_iterator_initialize(old_arguments);
    StringOs arg;
    u64 old_argument_count = 0;
    while ((arg = string_os_list_iterator_next(&it)).pointer)
    {
        old_argument_count += 1;
    }

    let total_argument_count = old_argument_count + extra_arguments.length;
    let new_arguments = arena_allocate(arena, CharOs*, total_argument_count + 1);
    new_arguments[0] = new_first_argument.pointer;

    if (old_argument_count > 1)
    {
        memcpy(new_arguments + 1, old_arguments + 1, sizeof(new_arguments[0]) * old_argument_count - 1); 
    }

    for (u64 i = 0; i < extra_arguments.length; i += 1)
    {
        let incoming_argument = extra_arguments.pointer[i];
        new_arguments[old_argument_count + i] = incoming_argument.pointer;
    }

    new_arguments[total_argument_count] = 0;

    return new_arguments;
#endif
}

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>

#define STRING_TEST_LITERAL(s) STRING_OF_CHAR(s, Char)

template<typename Char>
BUSTER_F_IMPL UnitTestResult string_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {};
    let arena = arguments->arena;
    // string8_format
    {
        {
            String<Char> formatted;
            if constexpr (BUSTER_TYPE_EQUAL(Char, char16))
            {
                formatted = string_format(arena, STRING_TEST_LITERAL("{{ {S16} }}"), STRING_TEST_LITERAL("value"));
            }
            else
            {
                formatted = string_format(arena, STRING_TEST_LITERAL("{{ {S8} }}"), STRING_TEST_LITERAL("value"));
            }
            BUSTER_STRING_TEST(arguments, formatted, STRING_TEST_LITERAL("{ value }"));
        }
        {
            let formatted = string_format(arena, STRING_TEST_LITERAL("async_thread_{u64}"), (u64)7);
            BUSTER_STRING_TEST(arguments, formatted, STRING_TEST_LITERAL("async_thread_7"));
        }

        ENUM_T(UnsignedFormatTestCase, u8, 
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
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP);

        // u8
        {
            ENUM_T(UnsignedTestCaseId, u8,
                u8,
                u16,
                u32,
                u64);

            String<Char> format_strings[(u64)UnsignedTestCaseId::Count][(u64)UnsignedFormatTestCase::Count] = {
                [(u64)UnsignedTestCaseId::u8] =
                {
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           STRING_TEST_LITERAL("{u8}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           STRING_TEST_LITERAL("{u8:d}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 STRING_TEST_LITERAL("{u8:x}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 STRING_TEST_LITERAL("{u8:X}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             STRING_TEST_LITERAL("{u8:o}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            STRING_TEST_LITERAL("{u8:b}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 STRING_TEST_LITERAL("{u8:no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 STRING_TEST_LITERAL("{u8:d,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       STRING_TEST_LITERAL("{u8:x,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       STRING_TEST_LITERAL("{u8:X,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   STRING_TEST_LITERAL("{u8:o,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  STRING_TEST_LITERAL("{u8:b,no_prefix}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             STRING_TEST_LITERAL("{u8:width=[ ,2]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             STRING_TEST_LITERAL("{u8:d,width=[ ,4]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   STRING_TEST_LITERAL("{u8:x,width=[ ,8]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  STRING_TEST_LITERAL("{u8:X,width=[ ,16]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              STRING_TEST_LITERAL("{u8:o,width=[ ,32]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             STRING_TEST_LITERAL("{u8:b,width=[ ,64]}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              STRING_TEST_LITERAL("{u8:width=[0,2]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              STRING_TEST_LITERAL("{u8:d,width=[0,4]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    STRING_TEST_LITERAL("{u8:x,width=[0,8]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   STRING_TEST_LITERAL("{u8:X,width=[0,16]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               STRING_TEST_LITERAL("{u8:o,width=[0,32]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              STRING_TEST_LITERAL("{u8:b,width=[0,64]}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("{u8:width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("{u8:d,width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("{u8:x,width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("{u8:X,width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                STRING_TEST_LITERAL("{u8:o,width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               STRING_TEST_LITERAL("{u8:b,width=[0,x]}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("{u8:width=[ ,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("{u8:d,width=[ ,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("{u8:x,width=[ ,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("{u8:X,width=[ ,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     STRING_TEST_LITERAL("{u8:o,width=[ ,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    STRING_TEST_LITERAL("{u8:b,width=[ ,x],no_prefix}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("{u8:width=[0,2],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("{u8:d,width=[0,4],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("{u8:x,width=[0,8],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         STRING_TEST_LITERAL("{u8:X,width=[0,16],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("{u8:o,width=[0,32],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("{u8:b,width=[0,64],no_prefix}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("{u8:width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("{u8:d,width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("{u8:x,width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("{u8:X,width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      STRING_TEST_LITERAL("{u8:o,width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("{u8:b,width=[0,x],no_prefix}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("{u8:digit_group}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("{u8:digit_group,d}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("{u8:digit_group,x}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("{u8:digit_group,X}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    STRING_TEST_LITERAL("{u8:digit_group,o}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   STRING_TEST_LITERAL("{u8:digit_group,b}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("{u8:digit_group,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("{u8:digit_group,no_prefix,d}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("{u8:digit_group,no_prefix,x}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("{u8:digit_group,no_prefix,X}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          STRING_TEST_LITERAL("{u8:digit_group,no_prefix,o}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         STRING_TEST_LITERAL("{u8:digit_group,no_prefix,b}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("{u8:digit_group,width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("{u8:digit_group,width=[0,x],d}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("{u8:digit_group,width=[0,x],x}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("{u8:digit_group,width=[0,x],X}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       STRING_TEST_LITERAL("{u8:digit_group,width=[0,x],o}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      STRING_TEST_LITERAL("{u8:digit_group,width=[0,x],b}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("{u8:digit_group,width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("{u8:digit_group,width=[0,x],d,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("{u8:digit_group,width=[0,x],x,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("{u8:digit_group,width=[0,x],X,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             STRING_TEST_LITERAL("{u8:digit_group,width=[0,x],o,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            STRING_TEST_LITERAL("{u8:digit_group,width=[0,x],b,no_prefix}"),
                },
                [(u64)UnsignedTestCaseId::u16] =
                {
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           STRING_TEST_LITERAL("{u16}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           STRING_TEST_LITERAL("{u16:d}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 STRING_TEST_LITERAL("{u16:x}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 STRING_TEST_LITERAL("{u16:X}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             STRING_TEST_LITERAL("{u16:o}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            STRING_TEST_LITERAL("{u16:b}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 STRING_TEST_LITERAL("{u16:no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 STRING_TEST_LITERAL("{u16:d,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       STRING_TEST_LITERAL("{u16:x,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       STRING_TEST_LITERAL("{u16:X,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   STRING_TEST_LITERAL("{u16:o,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  STRING_TEST_LITERAL("{u16:b,no_prefix}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             STRING_TEST_LITERAL("{u16:width=[ ,2]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             STRING_TEST_LITERAL("{u16:d,width=[ ,4]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   STRING_TEST_LITERAL("{u16:x,width=[ ,8]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  STRING_TEST_LITERAL("{u16:X,width=[ ,16]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              STRING_TEST_LITERAL("{u16:o,width=[ ,32]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             STRING_TEST_LITERAL("{u16:b,width=[ ,64]}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              STRING_TEST_LITERAL("{u16:width=[0,2]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              STRING_TEST_LITERAL("{u16:d,width=[0,4]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    STRING_TEST_LITERAL("{u16:x,width=[0,8]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   STRING_TEST_LITERAL("{u16:X,width=[0,16]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               STRING_TEST_LITERAL("{u16:o,width=[0,32]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              STRING_TEST_LITERAL("{u16:b,width=[0,64]}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("{u16:width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("{u16:d,width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("{u16:x,width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("{u16:X,width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                STRING_TEST_LITERAL("{u16:o,width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               STRING_TEST_LITERAL("{u16:b,width=[0,x]}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("{u16:width=[ ,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("{u16:d,width=[ ,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("{u16:x,width=[ ,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("{u16:X,width=[ ,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     STRING_TEST_LITERAL("{u16:o,width=[ ,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    STRING_TEST_LITERAL("{u16:b,width=[ ,x],no_prefix}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("{u16:width=[0,2],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("{u16:d,width=[0,4],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("{u16:x,width=[0,8],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         STRING_TEST_LITERAL("{u16:X,width=[0,16],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("{u16:o,width=[0,32],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("{u16:b,width=[0,64],no_prefix}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("{u16:width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("{u16:d,width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("{u16:x,width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("{u16:X,width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      STRING_TEST_LITERAL("{u16:o,width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("{u16:b,width=[0,x],no_prefix}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("{u16:digit_group}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("{u16:digit_group,d}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("{u16:digit_group,x}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("{u16:digit_group,X}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    STRING_TEST_LITERAL("{u16:digit_group,o}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   STRING_TEST_LITERAL("{u16:digit_group,b}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("{u16:digit_group,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("{u16:digit_group,no_prefix,d}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("{u16:digit_group,no_prefix,x}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("{u16:digit_group,no_prefix,X}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          STRING_TEST_LITERAL("{u16:digit_group,no_prefix,o}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         STRING_TEST_LITERAL("{u16:digit_group,no_prefix,b}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("{u16:digit_group,width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("{u16:digit_group,width=[0,x],d}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("{u16:digit_group,width=[0,x],x}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("{u16:digit_group,width=[0,x],X}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       STRING_TEST_LITERAL("{u16:digit_group,width=[0,x],o}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      STRING_TEST_LITERAL("{u16:digit_group,width=[0,x],b}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("{u16:digit_group,width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("{u16:digit_group,width=[0,x],d,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("{u16:digit_group,width=[0,x],x,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("{u16:digit_group,width=[0,x],X,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             STRING_TEST_LITERAL("{u16:digit_group,width=[0,x],o,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            STRING_TEST_LITERAL("{u16:digit_group,width=[0,x],b,no_prefix}"),
                },
                [(u64)UnsignedTestCaseId::u32] =
                {
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           STRING_TEST_LITERAL("{u32}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           STRING_TEST_LITERAL("{u32:d}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 STRING_TEST_LITERAL("{u32:x}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 STRING_TEST_LITERAL("{u32:X}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             STRING_TEST_LITERAL("{u32:o}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            STRING_TEST_LITERAL("{u32:b}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 STRING_TEST_LITERAL("{u32:no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 STRING_TEST_LITERAL("{u32:d,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       STRING_TEST_LITERAL("{u32:x,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       STRING_TEST_LITERAL("{u32:X,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   STRING_TEST_LITERAL("{u32:o,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  STRING_TEST_LITERAL("{u32:b,no_prefix}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             STRING_TEST_LITERAL("{u32:width=[ ,2]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             STRING_TEST_LITERAL("{u32:d,width=[ ,4]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   STRING_TEST_LITERAL("{u32:x,width=[ ,8]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  STRING_TEST_LITERAL("{u32:X,width=[ ,16]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              STRING_TEST_LITERAL("{u32:o,width=[ ,32]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             STRING_TEST_LITERAL("{u32:b,width=[ ,64]}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              STRING_TEST_LITERAL("{u32:width=[0,2]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              STRING_TEST_LITERAL("{u32:d,width=[0,4]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    STRING_TEST_LITERAL("{u32:x,width=[0,8]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   STRING_TEST_LITERAL("{u32:X,width=[0,16]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               STRING_TEST_LITERAL("{u32:o,width=[0,32]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              STRING_TEST_LITERAL("{u32:b,width=[0,64]}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("{u32:width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("{u32:d,width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("{u32:x,width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("{u32:X,width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                STRING_TEST_LITERAL("{u32:o,width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               STRING_TEST_LITERAL("{u32:b,width=[0,x]}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("{u32:width=[ ,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("{u32:d,width=[ ,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("{u32:x,width=[ ,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("{u32:X,width=[ ,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     STRING_TEST_LITERAL("{u32:o,width=[ ,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    STRING_TEST_LITERAL("{u32:b,width=[ ,x],no_prefix}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("{u32:width=[0,2],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("{u32:d,width=[0,4],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("{u32:x,width=[0,8],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         STRING_TEST_LITERAL("{u32:X,width=[0,16],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("{u32:o,width=[0,32],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("{u32:b,width=[0,64],no_prefix}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("{u32:width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("{u32:d,width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("{u32:x,width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("{u32:X,width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      STRING_TEST_LITERAL("{u32:o,width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("{u32:b,width=[0,x],no_prefix}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("{u32:digit_group}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("{u32:digit_group,d}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("{u32:digit_group,x}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("{u32:digit_group,X}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    STRING_TEST_LITERAL("{u32:digit_group,o}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   STRING_TEST_LITERAL("{u32:digit_group,b}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("{u32:digit_group,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("{u32:digit_group,no_prefix,d}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("{u32:digit_group,no_prefix,x}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("{u32:digit_group,no_prefix,X}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          STRING_TEST_LITERAL("{u32:digit_group,no_prefix,o}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         STRING_TEST_LITERAL("{u32:digit_group,no_prefix,b}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("{u32:digit_group,width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("{u32:digit_group,width=[0,x],d}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("{u32:digit_group,width=[0,x],x}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("{u32:digit_group,width=[0,x],X}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       STRING_TEST_LITERAL("{u32:digit_group,width=[0,x],o}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      STRING_TEST_LITERAL("{u32:digit_group,width=[0,x],b}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("{u32:digit_group,width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("{u32:digit_group,width=[0,x],d,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("{u32:digit_group,width=[0,x],x,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("{u32:digit_group,width=[0,x],X,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             STRING_TEST_LITERAL("{u32:digit_group,width=[0,x],o,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            STRING_TEST_LITERAL("{u32:digit_group,width=[0,x],b,no_prefix}"),
                },
                [(u64)UnsignedTestCaseId::u64] =
                {
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           STRING_TEST_LITERAL("{u64}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           STRING_TEST_LITERAL("{u64:d}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 STRING_TEST_LITERAL("{u64:x}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 STRING_TEST_LITERAL("{u64:X}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             STRING_TEST_LITERAL("{u64:o}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            STRING_TEST_LITERAL("{u64:b}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 STRING_TEST_LITERAL("{u64:no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 STRING_TEST_LITERAL("{u64:d,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       STRING_TEST_LITERAL("{u64:x,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       STRING_TEST_LITERAL("{u64:X,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   STRING_TEST_LITERAL("{u64:o,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  STRING_TEST_LITERAL("{u64:b,no_prefix}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             STRING_TEST_LITERAL("{u64:width=[ ,2]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             STRING_TEST_LITERAL("{u64:d,width=[ ,4]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   STRING_TEST_LITERAL("{u64:x,width=[ ,8]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  STRING_TEST_LITERAL("{u64:X,width=[ ,16]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              STRING_TEST_LITERAL("{u64:o,width=[ ,32]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             STRING_TEST_LITERAL("{u64:b,width=[ ,64]}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              STRING_TEST_LITERAL("{u64:width=[0,2]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              STRING_TEST_LITERAL("{u64:d,width=[0,4]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    STRING_TEST_LITERAL("{u64:x,width=[0,8]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   STRING_TEST_LITERAL("{u64:X,width=[0,16]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               STRING_TEST_LITERAL("{u64:o,width=[0,32]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              STRING_TEST_LITERAL("{u64:b,width=[0,64]}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("{u64:width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("{u64:d,width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("{u64:x,width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("{u64:X,width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                STRING_TEST_LITERAL("{u64:o,width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               STRING_TEST_LITERAL("{u64:b,width=[0,x]}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("{u64:width=[ ,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("{u64:d,width=[ ,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("{u64:x,width=[ ,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("{u64:X,width=[ ,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     STRING_TEST_LITERAL("{u64:o,width=[ ,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    STRING_TEST_LITERAL("{u64:b,width=[ ,x],no_prefix}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("{u64:width=[0,2],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("{u64:d,width=[0,4],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("{u64:x,width=[0,8],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         STRING_TEST_LITERAL("{u64:X,width=[0,16],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("{u64:o,width=[0,32],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("{u64:b,width=[0,64],no_prefix}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("{u64:width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("{u64:d,width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("{u64:x,width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("{u64:X,width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      STRING_TEST_LITERAL("{u64:o,width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("{u64:b,width=[0,x],no_prefix}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("{u64:digit_group}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("{u64:digit_group,d}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("{u64:digit_group,x}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("{u64:digit_group,X}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    STRING_TEST_LITERAL("{u64:digit_group,o}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   STRING_TEST_LITERAL("{u64:digit_group,b}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("{u64:digit_group,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("{u64:digit_group,no_prefix,d}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("{u64:digit_group,no_prefix,x}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("{u64:digit_group,no_prefix,X}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          STRING_TEST_LITERAL("{u64:digit_group,no_prefix,o}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         STRING_TEST_LITERAL("{u64:digit_group,no_prefix,b}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("{u64:digit_group,width=[0,x]}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("{u64:digit_group,width=[0,x],d}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("{u64:digit_group,width=[0,x],x}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("{u64:digit_group,width=[0,x],X}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       STRING_TEST_LITERAL("{u64:digit_group,width=[0,x],o}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      STRING_TEST_LITERAL("{u64:digit_group,width=[0,x],b}"),

                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("{u64:digit_group,width=[0,x],no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("{u64:digit_group,width=[0,x],d,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("{u64:digit_group,width=[0,x],x,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("{u64:digit_group,width=[0,x],X,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             STRING_TEST_LITERAL("{u64:digit_group,width=[0,x],o,no_prefix}"),
                    [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            STRING_TEST_LITERAL("{u64:digit_group,width=[0,x],b,no_prefix}"),
                },
            };

            // 0, 1, 2, 4, 8, 16, UINT_MAX / 2, UINT_MAX

            STRUCT(UnsignedTestCase)
            {
                String<Char> expected_results[(u64)UnsignedFormatTestCase::Count];
                u64 value;
            };

            ENUM_T(UnsignedTestCaseNumber, u8,
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
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX);

            UnsignedTestCase cases[(u64)UnsignedTestCaseId::Count][(u64)UnsignedTestCaseNumber::Count] =
            {
                [(u64)UnsignedTestCaseId::u8] =
                {
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_ZERO] =
                    {
                        .value = 0,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           STRING_TEST_LITERAL("0d0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 STRING_TEST_LITERAL("0x0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 STRING_TEST_LITERAL("0x0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             STRING_TEST_LITERAL("0o0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            STRING_TEST_LITERAL("0b0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  STRING_TEST_LITERAL("0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             STRING_TEST_LITERAL(" 0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             STRING_TEST_LITERAL("   0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   STRING_TEST_LITERAL("       0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  STRING_TEST_LITERAL("               0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              STRING_TEST_LITERAL("                               0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             STRING_TEST_LITERAL("                                                               0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              STRING_TEST_LITERAL("00"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              STRING_TEST_LITERAL("0d0000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    STRING_TEST_LITERAL("0x00000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   STRING_TEST_LITERAL("0x0000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               STRING_TEST_LITERAL("0o00000000000000000000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("0d000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0x00"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0x00"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                STRING_TEST_LITERAL("0o000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               STRING_TEST_LITERAL("0b00000000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("  0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("  0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL(" 0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL(" 0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     STRING_TEST_LITERAL("  0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    STRING_TEST_LITERAL("       0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("00"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("00000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         STRING_TEST_LITERAL("0000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("00000000000000000000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("00"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("00"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      STRING_TEST_LITERAL("000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("00000000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("0d0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0x0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0x0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    STRING_TEST_LITERAL("0o0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   STRING_TEST_LITERAL("0b0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         STRING_TEST_LITERAL("0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("0d000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0x00"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0x00"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       STRING_TEST_LITERAL("0o000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      STRING_TEST_LITERAL("0b00000000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("00"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("00"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             STRING_TEST_LITERAL("000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            STRING_TEST_LITERAL("00000000"),
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_ONE] =
                    {
                        .value = 1,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           STRING_TEST_LITERAL("0d1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 STRING_TEST_LITERAL("0x1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 STRING_TEST_LITERAL("0x1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             STRING_TEST_LITERAL("0o1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            STRING_TEST_LITERAL("0b1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  STRING_TEST_LITERAL("1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             STRING_TEST_LITERAL(" 1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             STRING_TEST_LITERAL("   1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   STRING_TEST_LITERAL("       1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  STRING_TEST_LITERAL("               1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              STRING_TEST_LITERAL("                               1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             STRING_TEST_LITERAL("                                                               1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              STRING_TEST_LITERAL("01"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              STRING_TEST_LITERAL("0d0001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    STRING_TEST_LITERAL("0x00000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   STRING_TEST_LITERAL("0x0000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               STRING_TEST_LITERAL("0o00000000000000000000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("0d001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0x01"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0x01"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                STRING_TEST_LITERAL("0o001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               STRING_TEST_LITERAL("0b00000001"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("  1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("  1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL(" 1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL(" 1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     STRING_TEST_LITERAL("  1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    STRING_TEST_LITERAL("       1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("01"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("00000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         STRING_TEST_LITERAL("0000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("00000000000000000000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("01"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("01"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      STRING_TEST_LITERAL("001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("00000001"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("0d1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0x1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0x1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    STRING_TEST_LITERAL("0o1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   STRING_TEST_LITERAL("0b1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         STRING_TEST_LITERAL("1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("0d001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0x01"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0x01"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       STRING_TEST_LITERAL("0o001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      STRING_TEST_LITERAL("0b00000001"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("01"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("01"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             STRING_TEST_LITERAL("001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            STRING_TEST_LITERAL("00000001"),
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_TWO] =
                    {
                        .value = 2,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           STRING_TEST_LITERAL("0d2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 STRING_TEST_LITERAL("0x2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 STRING_TEST_LITERAL("0x2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             STRING_TEST_LITERAL("0o2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            STRING_TEST_LITERAL("0b10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  STRING_TEST_LITERAL("10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             STRING_TEST_LITERAL(" 2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             STRING_TEST_LITERAL("   2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   STRING_TEST_LITERAL("       2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  STRING_TEST_LITERAL("               2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              STRING_TEST_LITERAL("                               2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             STRING_TEST_LITERAL("                                                              10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              STRING_TEST_LITERAL("02"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              STRING_TEST_LITERAL("0d0002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    STRING_TEST_LITERAL("0x00000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   STRING_TEST_LITERAL("0x0000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               STRING_TEST_LITERAL("0o00000000000000000000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("0d002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0x02"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0x02"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                STRING_TEST_LITERAL("0o002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               STRING_TEST_LITERAL("0b00000010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("  2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("  2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL(" 2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL(" 2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     STRING_TEST_LITERAL("  2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    STRING_TEST_LITERAL("      10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("02"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("00000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         STRING_TEST_LITERAL("0000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("00000000000000000000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("02"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("02"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      STRING_TEST_LITERAL("002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("00000010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("0d2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0x2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0x2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    STRING_TEST_LITERAL("0o2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   STRING_TEST_LITERAL("0b10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         STRING_TEST_LITERAL("10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("0d002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0x02"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0x02"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       STRING_TEST_LITERAL("0o002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      STRING_TEST_LITERAL("0b00000010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("02"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("02"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             STRING_TEST_LITERAL("002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            STRING_TEST_LITERAL("00000010"),
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_FOUR] =
                    {
                        .value = 4,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           STRING_TEST_LITERAL("0d4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 STRING_TEST_LITERAL("0x4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 STRING_TEST_LITERAL("0x4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             STRING_TEST_LITERAL("0o4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            STRING_TEST_LITERAL("0b100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  STRING_TEST_LITERAL("100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             STRING_TEST_LITERAL(" 4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             STRING_TEST_LITERAL("   4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   STRING_TEST_LITERAL("       4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  STRING_TEST_LITERAL("               4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              STRING_TEST_LITERAL("                               4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             STRING_TEST_LITERAL("                                                             100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              STRING_TEST_LITERAL("04"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              STRING_TEST_LITERAL("0d0004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    STRING_TEST_LITERAL("0x00000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   STRING_TEST_LITERAL("0x0000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               STRING_TEST_LITERAL("0o00000000000000000000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("0d004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0x04"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0x04"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                STRING_TEST_LITERAL("0o004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               STRING_TEST_LITERAL("0b00000100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("  4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("  4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL(" 4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL(" 4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     STRING_TEST_LITERAL("  4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    STRING_TEST_LITERAL("     100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("04"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("00000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         STRING_TEST_LITERAL("0000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("00000000000000000000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("04"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("04"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      STRING_TEST_LITERAL("004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("00000100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("0d4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0x4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0x4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    STRING_TEST_LITERAL("0o4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   STRING_TEST_LITERAL("0b100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         STRING_TEST_LITERAL("100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("0d004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0x04"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0x04"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       STRING_TEST_LITERAL("0o004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      STRING_TEST_LITERAL("0b00000100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("04"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("04"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             STRING_TEST_LITERAL("004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            STRING_TEST_LITERAL("00000100"),
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_EIGHT] =
                    {
                        .value = 8,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           STRING_TEST_LITERAL("0d8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 STRING_TEST_LITERAL("0x8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 STRING_TEST_LITERAL("0x8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             STRING_TEST_LITERAL("0o10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            STRING_TEST_LITERAL("0b1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  STRING_TEST_LITERAL("1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             STRING_TEST_LITERAL(" 8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             STRING_TEST_LITERAL("   8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   STRING_TEST_LITERAL("       8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  STRING_TEST_LITERAL("               8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              STRING_TEST_LITERAL("                              10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             STRING_TEST_LITERAL("                                                            1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              STRING_TEST_LITERAL("08"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              STRING_TEST_LITERAL("0d0008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    STRING_TEST_LITERAL("0x00000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   STRING_TEST_LITERAL("0x0000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               STRING_TEST_LITERAL("0o00000000000000000000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("0d008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0x08"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0x08"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                STRING_TEST_LITERAL("0o010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               STRING_TEST_LITERAL("0b00001000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("  8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("  8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL(" 8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL(" 8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     STRING_TEST_LITERAL(" 10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    STRING_TEST_LITERAL("    1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("08"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("00000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         STRING_TEST_LITERAL("0000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("00000000000000000000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("08"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("08"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      STRING_TEST_LITERAL("010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("00001000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("0d8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0x8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0x8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    STRING_TEST_LITERAL("0o10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   STRING_TEST_LITERAL("0b1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         STRING_TEST_LITERAL("1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("0d008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0x08"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0x08"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       STRING_TEST_LITERAL("0o010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      STRING_TEST_LITERAL("0b00001000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("08"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("08"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             STRING_TEST_LITERAL("010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            STRING_TEST_LITERAL("00001000"),
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_SIXTEEN] =
                    {
                        .value = 16,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           STRING_TEST_LITERAL("0d16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 STRING_TEST_LITERAL("0x10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 STRING_TEST_LITERAL("0x10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             STRING_TEST_LITERAL("0o20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            STRING_TEST_LITERAL("0b10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   STRING_TEST_LITERAL("20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  STRING_TEST_LITERAL("10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             STRING_TEST_LITERAL("  16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   STRING_TEST_LITERAL("      10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  STRING_TEST_LITERAL("              10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              STRING_TEST_LITERAL("                              20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             STRING_TEST_LITERAL("                                                           10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              STRING_TEST_LITERAL("0d0016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    STRING_TEST_LITERAL("0x00000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   STRING_TEST_LITERAL("0x0000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               STRING_TEST_LITERAL("0o00000000000000000000000000000020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("0d016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0x10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0x10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                STRING_TEST_LITERAL("0o020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               STRING_TEST_LITERAL("0b00010000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL(" 16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL(" 16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     STRING_TEST_LITERAL(" 20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    STRING_TEST_LITERAL("   10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("00000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         STRING_TEST_LITERAL("0000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("00000000000000000000000000000020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      STRING_TEST_LITERAL("020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("00010000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("0d16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0x10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0x10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    STRING_TEST_LITERAL("0o20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   STRING_TEST_LITERAL("0b10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          STRING_TEST_LITERAL("20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         STRING_TEST_LITERAL("10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("0d016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0x10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0x10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       STRING_TEST_LITERAL("0o020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      STRING_TEST_LITERAL("0b00010000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             STRING_TEST_LITERAL("020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            STRING_TEST_LITERAL("00010000"),
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2] =
                    {
                        .value = UINT8_MAX / 2,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           STRING_TEST_LITERAL("127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           STRING_TEST_LITERAL("0d127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 STRING_TEST_LITERAL("0x7f"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 STRING_TEST_LITERAL("0x7F"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             STRING_TEST_LITERAL("0o177"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            STRING_TEST_LITERAL("0b1111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 STRING_TEST_LITERAL("127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 STRING_TEST_LITERAL("127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       STRING_TEST_LITERAL("7f"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       STRING_TEST_LITERAL("7F"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   STRING_TEST_LITERAL("177"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  STRING_TEST_LITERAL("1111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             STRING_TEST_LITERAL("127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             STRING_TEST_LITERAL(" 127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   STRING_TEST_LITERAL("      7f"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  STRING_TEST_LITERAL("              7F"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              STRING_TEST_LITERAL("                             177"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             STRING_TEST_LITERAL("                                                         1111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              STRING_TEST_LITERAL("127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              STRING_TEST_LITERAL("0d0127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    STRING_TEST_LITERAL("0x0000007f"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   STRING_TEST_LITERAL("0x000000000000007F"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               STRING_TEST_LITERAL("0o00000000000000000000000000000177"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000001111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("0d127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0x7f"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0x7F"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                STRING_TEST_LITERAL("0o177"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               STRING_TEST_LITERAL("0b01111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("7f"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("7F"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     STRING_TEST_LITERAL("177"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    STRING_TEST_LITERAL(" 1111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("0000007f"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         STRING_TEST_LITERAL("000000000000007F"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("00000000000000000000000000000177"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000001111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("7f"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("7F"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      STRING_TEST_LITERAL("177"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("01111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("0d127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0x7f"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0x7F"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    STRING_TEST_LITERAL("0o177"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   STRING_TEST_LITERAL("0b1111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("7f"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("7F"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          STRING_TEST_LITERAL("177"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         STRING_TEST_LITERAL("1111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("0d127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0x7f"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0x7F"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       STRING_TEST_LITERAL("0o177"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      STRING_TEST_LITERAL("0b01111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("127"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("7f"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("7F"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             STRING_TEST_LITERAL("177"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            STRING_TEST_LITERAL("01111111"),
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5] =
                    {
                        .value = UINT8_MAX - 5,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           STRING_TEST_LITERAL("250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           STRING_TEST_LITERAL("0d250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 STRING_TEST_LITERAL("0xfa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 STRING_TEST_LITERAL("0xFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             STRING_TEST_LITERAL("0o372"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            STRING_TEST_LITERAL("0b11111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 STRING_TEST_LITERAL("250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 STRING_TEST_LITERAL("250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       STRING_TEST_LITERAL("fa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       STRING_TEST_LITERAL("FA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   STRING_TEST_LITERAL("372"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  STRING_TEST_LITERAL("11111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             STRING_TEST_LITERAL("250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             STRING_TEST_LITERAL(" 250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   STRING_TEST_LITERAL("      fa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  STRING_TEST_LITERAL("              FA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              STRING_TEST_LITERAL("                             372"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             STRING_TEST_LITERAL("                                                        11111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              STRING_TEST_LITERAL("250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              STRING_TEST_LITERAL("0d0250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    STRING_TEST_LITERAL("0x000000fa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   STRING_TEST_LITERAL("0x00000000000000FA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               STRING_TEST_LITERAL("0o00000000000000000000000000000372"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000011111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("0d250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0xfa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0xFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                STRING_TEST_LITERAL("0o372"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               STRING_TEST_LITERAL("0b11111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("fa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("FA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     STRING_TEST_LITERAL("372"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    STRING_TEST_LITERAL("11111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("000000fa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         STRING_TEST_LITERAL("00000000000000FA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("00000000000000000000000000000372"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000011111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("fa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("FA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      STRING_TEST_LITERAL("372"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("11111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("0d250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0xfa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0xFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    STRING_TEST_LITERAL("0o372"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   STRING_TEST_LITERAL("0b11111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("fa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("FA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          STRING_TEST_LITERAL("372"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         STRING_TEST_LITERAL("11111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("0d250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0xfa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0xFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       STRING_TEST_LITERAL("0o372"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      STRING_TEST_LITERAL("0b11111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("250"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("fa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             STRING_TEST_LITERAL("372"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            STRING_TEST_LITERAL("11111010"),
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4] =
                    {
                        .value = UINT8_MAX - 4,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           STRING_TEST_LITERAL("251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           STRING_TEST_LITERAL("0d251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 STRING_TEST_LITERAL("0xfb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 STRING_TEST_LITERAL("0xFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             STRING_TEST_LITERAL("0o373"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            STRING_TEST_LITERAL("0b11111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 STRING_TEST_LITERAL("251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 STRING_TEST_LITERAL("251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       STRING_TEST_LITERAL("fb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       STRING_TEST_LITERAL("FB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   STRING_TEST_LITERAL("373"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  STRING_TEST_LITERAL("11111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             STRING_TEST_LITERAL("251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             STRING_TEST_LITERAL(" 251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   STRING_TEST_LITERAL("      fb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  STRING_TEST_LITERAL("              FB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              STRING_TEST_LITERAL("                             373"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             STRING_TEST_LITERAL("                                                        11111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              STRING_TEST_LITERAL("251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              STRING_TEST_LITERAL("0d0251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    STRING_TEST_LITERAL("0x000000fb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   STRING_TEST_LITERAL("0x00000000000000FB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               STRING_TEST_LITERAL("0o00000000000000000000000000000373"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000011111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("0d251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0xfb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0xFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                STRING_TEST_LITERAL("0o373"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               STRING_TEST_LITERAL("0b11111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("fb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("FB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     STRING_TEST_LITERAL("373"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    STRING_TEST_LITERAL("11111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("000000fb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         STRING_TEST_LITERAL("00000000000000FB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("00000000000000000000000000000373"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000011111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("fb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("FB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      STRING_TEST_LITERAL("373"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("11111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("0d251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0xfb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0xFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    STRING_TEST_LITERAL("0o373"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   STRING_TEST_LITERAL("0b11111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("fb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("FB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          STRING_TEST_LITERAL("373"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         STRING_TEST_LITERAL("11111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("0d251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0xfb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0xFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       STRING_TEST_LITERAL("0o373"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      STRING_TEST_LITERAL("0b11111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("251"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("fb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             STRING_TEST_LITERAL("373"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            STRING_TEST_LITERAL("11111011"),
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3] =
                    {
                        .value = UINT8_MAX - 3,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           STRING_TEST_LITERAL("252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           STRING_TEST_LITERAL("0d252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 STRING_TEST_LITERAL("0xfc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 STRING_TEST_LITERAL("0xFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             STRING_TEST_LITERAL("0o374"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            STRING_TEST_LITERAL("0b11111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 STRING_TEST_LITERAL("252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 STRING_TEST_LITERAL("252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       STRING_TEST_LITERAL("fc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       STRING_TEST_LITERAL("FC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   STRING_TEST_LITERAL("374"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  STRING_TEST_LITERAL("11111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             STRING_TEST_LITERAL("252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             STRING_TEST_LITERAL(" 252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   STRING_TEST_LITERAL("      fc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  STRING_TEST_LITERAL("              FC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              STRING_TEST_LITERAL("                             374"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             STRING_TEST_LITERAL("                                                        11111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              STRING_TEST_LITERAL("252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              STRING_TEST_LITERAL("0d0252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    STRING_TEST_LITERAL("0x000000fc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   STRING_TEST_LITERAL("0x00000000000000FC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               STRING_TEST_LITERAL("0o00000000000000000000000000000374"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000011111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("0d252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0xfc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0xFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                STRING_TEST_LITERAL("0o374"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               STRING_TEST_LITERAL("0b11111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("fc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("FC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     STRING_TEST_LITERAL("374"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    STRING_TEST_LITERAL("11111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("000000fc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         STRING_TEST_LITERAL("00000000000000FC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("00000000000000000000000000000374"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000011111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("fc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("FC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      STRING_TEST_LITERAL("374"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("11111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("0d252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0xfc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0xFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    STRING_TEST_LITERAL("0o374"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   STRING_TEST_LITERAL("0b11111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("fc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("FC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          STRING_TEST_LITERAL("374"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         STRING_TEST_LITERAL("11111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("0d252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0xfc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0xFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       STRING_TEST_LITERAL("0o374"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      STRING_TEST_LITERAL("0b11111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("252"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("fc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             STRING_TEST_LITERAL("374"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            STRING_TEST_LITERAL("11111100"),
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2] =
                    {
                        .value = UINT8_MAX - 2,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           STRING_TEST_LITERAL("253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           STRING_TEST_LITERAL("0d253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 STRING_TEST_LITERAL("0xfd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 STRING_TEST_LITERAL("0xFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             STRING_TEST_LITERAL("0o375"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            STRING_TEST_LITERAL("0b11111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 STRING_TEST_LITERAL("253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 STRING_TEST_LITERAL("253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       STRING_TEST_LITERAL("fd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       STRING_TEST_LITERAL("FD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   STRING_TEST_LITERAL("375"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  STRING_TEST_LITERAL("11111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             STRING_TEST_LITERAL("253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             STRING_TEST_LITERAL(" 253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   STRING_TEST_LITERAL("      fd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  STRING_TEST_LITERAL("              FD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              STRING_TEST_LITERAL("                             375"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             STRING_TEST_LITERAL("                                                        11111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              STRING_TEST_LITERAL("253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              STRING_TEST_LITERAL("0d0253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    STRING_TEST_LITERAL("0x000000fd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   STRING_TEST_LITERAL("0x00000000000000FD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               STRING_TEST_LITERAL("0o00000000000000000000000000000375"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000011111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("0d253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0xfd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0xFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                STRING_TEST_LITERAL("0o375"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               STRING_TEST_LITERAL("0b11111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("fd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("FD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     STRING_TEST_LITERAL("375"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    STRING_TEST_LITERAL("11111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("000000fd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         STRING_TEST_LITERAL("00000000000000FD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("00000000000000000000000000000375"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000011111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("fd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("FD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      STRING_TEST_LITERAL("375"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("11111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("0d253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0xfd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0xFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    STRING_TEST_LITERAL("0o375"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   STRING_TEST_LITERAL("0b11111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("fd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("FD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          STRING_TEST_LITERAL("375"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         STRING_TEST_LITERAL("11111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("0d253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0xfd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0xFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       STRING_TEST_LITERAL("0o375"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      STRING_TEST_LITERAL("0b11111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("253"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("fd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             STRING_TEST_LITERAL("375"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            STRING_TEST_LITERAL("11111101"),
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1] =
                    {
                        .value = UINT8_MAX - 1,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           STRING_TEST_LITERAL("254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           STRING_TEST_LITERAL("0d254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 STRING_TEST_LITERAL("0xfe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 STRING_TEST_LITERAL("0xFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             STRING_TEST_LITERAL("0o376"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            STRING_TEST_LITERAL("0b11111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 STRING_TEST_LITERAL("254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 STRING_TEST_LITERAL("254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       STRING_TEST_LITERAL("fe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       STRING_TEST_LITERAL("FE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   STRING_TEST_LITERAL("376"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  STRING_TEST_LITERAL("11111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             STRING_TEST_LITERAL("254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             STRING_TEST_LITERAL(" 254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   STRING_TEST_LITERAL("      fe") ,
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  STRING_TEST_LITERAL("              FE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              STRING_TEST_LITERAL("                             376"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             STRING_TEST_LITERAL("                                                        11111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              STRING_TEST_LITERAL("254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              STRING_TEST_LITERAL("0d0254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    STRING_TEST_LITERAL("0x000000fe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   STRING_TEST_LITERAL("0x00000000000000FE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               STRING_TEST_LITERAL("0o00000000000000000000000000000376"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000011111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("0d254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0xfe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0xFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                STRING_TEST_LITERAL("0o376"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               STRING_TEST_LITERAL("0b11111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("fe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("FE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     STRING_TEST_LITERAL("376"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    STRING_TEST_LITERAL("11111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("000000fe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         STRING_TEST_LITERAL("00000000000000FE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("00000000000000000000000000000376"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000011111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("fe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("FE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      STRING_TEST_LITERAL("376"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("11111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("0d254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0xfe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0xFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    STRING_TEST_LITERAL("0o376"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   STRING_TEST_LITERAL("0b11111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("fe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("FE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          STRING_TEST_LITERAL("376"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         STRING_TEST_LITERAL("11111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("0d254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0xfe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0xFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       STRING_TEST_LITERAL("0o376"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      STRING_TEST_LITERAL("0b11111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("254"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("fe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             STRING_TEST_LITERAL("376"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            STRING_TEST_LITERAL("11111110"),
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX] =
                    {
                        .value = UINT8_MAX,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           STRING_TEST_LITERAL("255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           STRING_TEST_LITERAL("0d255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 STRING_TEST_LITERAL("0xff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 STRING_TEST_LITERAL("0xFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             STRING_TEST_LITERAL("0o377"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            STRING_TEST_LITERAL("0b11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 STRING_TEST_LITERAL("255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 STRING_TEST_LITERAL("255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       STRING_TEST_LITERAL("ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       STRING_TEST_LITERAL("FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   STRING_TEST_LITERAL("377"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  STRING_TEST_LITERAL("11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             STRING_TEST_LITERAL("255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             STRING_TEST_LITERAL(" 255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   STRING_TEST_LITERAL("      ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  STRING_TEST_LITERAL("              FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              STRING_TEST_LITERAL("                             377"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             STRING_TEST_LITERAL("                                                        11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              STRING_TEST_LITERAL("255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              STRING_TEST_LITERAL("0d0255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    STRING_TEST_LITERAL("0x000000ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   STRING_TEST_LITERAL("0x00000000000000FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               STRING_TEST_LITERAL("0o00000000000000000000000000000377"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000011111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              STRING_TEST_LITERAL("0d255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0xff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    STRING_TEST_LITERAL("0xFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                STRING_TEST_LITERAL("0o377"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               STRING_TEST_LITERAL("0b11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   STRING_TEST_LITERAL("255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         STRING_TEST_LITERAL("FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     STRING_TEST_LITERAL("377"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    STRING_TEST_LITERAL("11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("000000ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         STRING_TEST_LITERAL("00000000000000FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("00000000000000000000000000000377"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000011111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    STRING_TEST_LITERAL("255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          STRING_TEST_LITERAL("FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      STRING_TEST_LITERAL("377"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     STRING_TEST_LITERAL("11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  STRING_TEST_LITERAL("0d255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0xff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        STRING_TEST_LITERAL("0xFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    STRING_TEST_LITERAL("0o377"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   STRING_TEST_LITERAL("0b11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        STRING_TEST_LITERAL("255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              STRING_TEST_LITERAL("FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          STRING_TEST_LITERAL("377"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         STRING_TEST_LITERAL("11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     STRING_TEST_LITERAL("0d255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0xff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           STRING_TEST_LITERAL("0xFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       STRING_TEST_LITERAL("0o377"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      STRING_TEST_LITERAL("0b11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           STRING_TEST_LITERAL("255"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             STRING_TEST_LITERAL("377"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            STRING_TEST_LITERAL("11111111"),
                        },
                    },
                },
                
// ==================== U16 ====================

                [(u64)UnsignedTestCaseId::u16] =
                {
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_ZERO] =
                    {
                        .value = 0,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL(" 0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("   0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("       0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("               0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                               0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                               0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("00"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d0000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x00000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x0000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("00000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d00000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b0000000000000000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("    0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("    0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("   0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("   0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("     0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("               0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("0000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("0000000000000000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("00000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d00000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b0000000000000000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("0000000000000000")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_ONE] =
                    {
                        .value = 1,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL(" 1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("   1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("       1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("               1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                               1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                               1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("01"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d0001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x00000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x0000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("00001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d00001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b0000000000000001"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("    1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("    1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("   1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("   1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("     1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("               1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("01"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("0000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("0000000000000001"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("00001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d00001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b0000000000000001"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("0000000000000001")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_TWO] =
                    {
                        .value = 2,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL(" 2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("   2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("       2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("               2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                               2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                              10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("02"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d0002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x00000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x0000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("00002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d00002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b0000000000000010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("    2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("    2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("   2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("   2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("     2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("              10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("02"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("0000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("0000000000000010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("00002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d00002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b0000000000000010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("0000000000000010")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_FOUR] =
                    {
                        .value = 4,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL(" 4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("   4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("       4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("               4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                               4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                             100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("04"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d0004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x00000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x0000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("00004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d00004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b0000000000000100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("    4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("    4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("   4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("   4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("     4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("             100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("04"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("0000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("0000000000000100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("00004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d00004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b0000000000000100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("0000000000000100")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_EIGHT] =
                    {
                        .value = 8,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL(" 8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("   8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("       8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("               8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                              10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                            1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("08"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d0008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x00000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x0000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("00008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d00008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b0000000000001000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("    8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("    8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("   8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("   8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("    10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("            1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("08"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("0000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("0000000000001000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("00008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d00008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b0000000000001000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("0000000000001000")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_SIXTEEN] =
                    {
                        .value = 16,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("  16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("      10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("              10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                              20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                           10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d0016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x00000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x0000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000000020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("00016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d00016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o000020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b0000000000010000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("   16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("   16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("  10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("  10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("    20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("           10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("0000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("000020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("0000000000010000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("00016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d00016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o000020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b0000000000010000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("000020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("0000000000010000")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2] =
                    {
                        .value = UINT16_MAX / 2,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("32767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d32767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x7fff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x7FFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o77777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("32767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("32767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("7fff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("7FFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("77777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("32767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("32767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("    7fff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("            7FFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                           77777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                 111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("32767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d32767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x00007fff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x0000000000007FFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000077777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("32767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d32767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x7fff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x7FFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o077777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b0111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("32767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("32767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("7fff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("7FFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL(" 77777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL(" 111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("32767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("32767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00007fff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("0000000000007FFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000077777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("32767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("32767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("7fff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("7FFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("077777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("0111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("32.767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d32.767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x7f_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x7F_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o77_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b1111111_11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("32.767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("32.767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("7f_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("7F_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("77_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("1111111_11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("32.767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d32.767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x7f_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x7F_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o077_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b01111111_11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("32.767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("32.767"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("7f_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("7F_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("077_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("01111111_11111111")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5] =
                    {
                        .value = UINT16_MAX - 5,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("65530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d65530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0xfffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0xFFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o177772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b1111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("65530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("65530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("fffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("FFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("177772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("1111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("65530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("65530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("    fffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("            FFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                          177772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                1111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("65530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d65530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x0000fffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x000000000000FFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000177772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000001111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("65530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d65530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xfffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xFFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o177772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b1111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("65530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("65530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("fffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("FFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("177772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("1111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0000fffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("000000000000FFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000177772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000001111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("FFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("177772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("1111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("65.530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d65.530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xff_fa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xFF_FA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o177_772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b11111111_11111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("65.530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("65.530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("ff_fa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("FF_FA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("177_772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("11111111_11111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("65.530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d65.530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xff_fa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xFF_FA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o177_772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b11111111_11111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("65.530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("65.530"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("ff_fa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FF_FA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("177_772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("11111111_11111010")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4] =
                    {
                        .value = UINT16_MAX - 4,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("65531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d65531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0xfffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0xFFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o177773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b1111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("65531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("65531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("fffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("FFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("177773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("1111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("65531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("65531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("    fffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("            FFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                          177773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                1111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("65531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d65531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x0000fffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x000000000000FFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000177773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000001111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("65531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d65531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xfffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xFFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o177773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b1111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("65531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("65531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("fffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("FFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("177773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("1111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0000fffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("000000000000FFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000177773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000001111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("FFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("177773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("1111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("65.531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d65.531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xff_fb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xFF_FB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o177_773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b11111111_11111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("65.531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("65.531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("ff_fb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("FF_FB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("177_773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("11111111_11111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("65.531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d65.531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xff_fb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xFF_FB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o177_773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b11111111_11111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("65.531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("65.531"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("ff_fb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FF_FB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("177_773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("11111111_11111011")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3] =
                    {
                        .value = UINT16_MAX - 3,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("65532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d65532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0xfffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0xFFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o177774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b1111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("65532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("65532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("fffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("FFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("177774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("1111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("65532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("65532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("    fffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("            FFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                          177774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                1111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("65532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d65532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x0000fffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x000000000000FFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000177774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000001111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("65532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d65532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xfffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xFFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o177774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b1111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("65532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("65532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("fffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("FFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("177774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("1111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0000fffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("000000000000FFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000177774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000001111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("FFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("177774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("1111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("65.532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d65.532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xff_fc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xFF_FC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o177_774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b11111111_11111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("65.532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("65.532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("ff_fc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("FF_FC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("177_774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("11111111_11111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("65.532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d65.532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xff_fc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xFF_FC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o177_774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b11111111_11111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("65.532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("65.532"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("ff_fc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FF_FC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("177_774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("11111111_11111100")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2] =
                    {
                        .value = UINT16_MAX - 2,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("65533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d65533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0xfffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0xFFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o177775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b1111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("65533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("65533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("fffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("FFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("177775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("1111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("65533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("65533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("    fffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("            FFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                          177775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                1111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("65533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d65533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x0000fffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x000000000000FFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000177775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000001111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("65533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d65533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xfffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xFFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o177775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b1111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("65533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("65533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("fffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("FFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("177775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("1111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0000fffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("000000000000FFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000177775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000001111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("FFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("177775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("1111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("65.533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d65.533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xff_fd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xFF_FD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o177_775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b11111111_11111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("65.533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("65.533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("ff_fd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("FF_FD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("177_775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("11111111_11111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("65.533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d65.533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xff_fd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xFF_FD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o177_775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b11111111_11111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("65.533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("65.533"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("ff_fd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FF_FD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("177_775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("11111111_11111101")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1] =
                    {
                        .value = UINT16_MAX - 1,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("65534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d65534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0xfffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0xFFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o177776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b1111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("65534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("65534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("fffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("FFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("177776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("1111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("65534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("65534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("    fffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("            FFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                          177776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                1111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("65534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d65534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x0000fffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x000000000000FFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000177776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000001111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("65534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d65534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xfffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xFFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o177776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b1111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("65534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("65534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("fffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("FFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("177776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("1111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0000fffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("000000000000FFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000177776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000001111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("FFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("177776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("1111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("65.534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d65.534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xff_fe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xFF_FE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o177_776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b11111111_11111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("65.534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("65.534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("ff_fe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("FF_FE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("177_776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("11111111_11111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("65.534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d65.534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xff_fe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xFF_FE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o177_776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b11111111_11111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("65.534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("65.534"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("ff_fe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FF_FE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("177_776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("11111111_11111110")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX] =
                    {
                        .value = UINT16_MAX,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("65535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d65535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0xffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0xFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o177777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b1111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("65535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("65535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("ffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("FFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("177777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("1111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("65535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("65535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("    ffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("            FFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                          177777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                1111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("65535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d65535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x0000ffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x000000000000FFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000177777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000001111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("65535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d65535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o177777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b1111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("65535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("65535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("ffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("FFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("177777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("1111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0000ffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("000000000000FFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000177777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000001111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("65535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("ffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("FFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("177777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("1111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("65.535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d65.535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xff_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xFF_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o177_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b11111111_11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("65.535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("65.535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("ff_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("FF_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("177_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("11111111_11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("65.535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d65.535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xff_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xFF_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o177_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b11111111_11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("65.535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("65.535"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("ff_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FF_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("177_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("11111111_11111111")
                        },
                    },
                },

// ==================== U32 ====================

                [(u64)UnsignedTestCaseId::u32] =
                {
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_ZERO] =
                    {
                        .value = 0,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL(" 0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("   0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("       0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("               0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                               0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                               0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("00"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d0000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x00000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x0000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d0000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x00000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x00000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o00000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b00000000000000000000000000000000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("         0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("         0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("       0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("       0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("          0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("                               0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("0000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("00000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d0000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x00000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x00000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o00000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b00000000000000000000000000000000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("0000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("0000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("00000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("00000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("00000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("00000000000000000000000000000000")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_ONE] =
                    {
                        .value = 1,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL(" 1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("   1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("       1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("               1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                               1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                               1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("01"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d0001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x00000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x0000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d0000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x00000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x00000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o00000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b00000000000000000000000000000001"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("         1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("         1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("       1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("       1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("          1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("                               1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("01"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("0000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("00000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000001"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d0000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x00000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x00000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o00000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b00000000000000000000000000000001"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("0000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("0000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("00000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("00000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("00000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("00000000000000000000000000000001")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_TWO] =
                    {
                        .value = 2,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL(" 2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("   2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("       2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("               2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                               2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                              10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("02"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d0002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x00000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x0000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d0000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x00000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x00000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o00000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b00000000000000000000000000000010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("         2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("         2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("       2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("       2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("          2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("                              10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("02"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("0000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("00000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d0000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x00000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x00000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o00000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b00000000000000000000000000000010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("0000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("0000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("00000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("00000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("00000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("00000000000000000000000000000010")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_FOUR] =
                    {
                        .value = 4,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL(" 4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("   4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("       4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("               4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                               4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                             100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("04"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d0004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x00000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x0000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d0000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x00000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x00000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o00000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b00000000000000000000000000000100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("         4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("         4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("       4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("       4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("          4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("                             100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("04"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("0000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("00000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d0000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x00000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x00000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o00000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b00000000000000000000000000000100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("0000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("0000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("00000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("00000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("00000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("00000000000000000000000000000100")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_EIGHT] =
                    {
                        .value = 8,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL(" 8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("   8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("       8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("               8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                              10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                            1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("08"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d0008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x00000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x0000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d0000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x00000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x00000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o00000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b00000000000000000000000000001000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("         8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("         8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("       8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("       8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("         10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("                            1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("08"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("0000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("00000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000001000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d0000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x00000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x00000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o00000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b00000000000000000000000000001000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("0000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("0000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("00000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("00000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("00000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("00000000000000000000000000001000")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_SIXTEEN] =
                    {
                        .value = 16,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("  16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("      10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("              10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                              20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                           10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d0016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x00000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x0000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000000020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0000000016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d0000000016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x00000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x00000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o00000000020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b00000000000000000000000000010000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("        16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("        16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("      10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("      10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("         20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("                           10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("0000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("00000000020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000010000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0000000016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d0000000016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x00000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x00000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o00000000020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b00000000000000000000000000010000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("0000000016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("0000000016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("00000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("00000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("00000000020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("00000000000000000000000000010000")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2] =
                    {
                        .value = UINT32_MAX / 2,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("2147483647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d2147483647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x7fffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x7FFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o17777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b1111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("2147483647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("2147483647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("7fffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("7FFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("17777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("1111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("2147483647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("2147483647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("7fffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("        7FFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                     17777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                 1111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("2147483647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d2147483647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x7fffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x000000007FFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000017777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000001111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("2147483647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d2147483647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x7fffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x7FFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o17777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b01111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("2147483647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("2147483647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("7fffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("7FFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("17777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL(" 1111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("2147483647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("2147483647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("7fffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("000000007FFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000017777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000001111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("2147483647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("2147483647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("7fffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("7FFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("17777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("01111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("2.147.483.647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d2.147.483.647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x7f_ff_ff_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x7F_FF_FF_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o17_777_777_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b1111111_11111111_11111111_11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("2.147.483.647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("2.147.483.647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("7f_ff_ff_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("7F_FF_FF_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("17_777_777_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("1111111_11111111_11111111_11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("2.147.483.647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d2.147.483.647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x7f_ff_ff_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x7F_FF_FF_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o17_777_777_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b01111111_11111111_11111111_11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("2.147.483.647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("2.147.483.647"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("7f_ff_ff_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("7F_FF_FF_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("17_777_777_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("01111111_11111111_11111111_11111111")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5] =
                    {
                        .value = UINT32_MAX - 5,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("4294967290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d4294967290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0xfffffffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0xFFFFFFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o37777777772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b11111111111111111111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("4294967290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("4294967290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("fffffffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("FFFFFFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("37777777772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("11111111111111111111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("4294967290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("4294967290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("fffffffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("        FFFFFFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                     37777777772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                11111111111111111111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("4294967290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d4294967290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0xfffffffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x00000000FFFFFFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000037777777772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000011111111111111111111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("4294967290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d4294967290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xfffffffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xFFFFFFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o37777777772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b11111111111111111111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("4294967290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("4294967290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("fffffffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("FFFFFFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("37777777772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("11111111111111111111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffffffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("00000000FFFFFFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000037777777772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000011111111111111111111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffffffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("FFFFFFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("37777777772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("11111111111111111111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("4.294.967.290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d4.294.967.290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xff_ff_ff_fa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xFF_FF_FF_FA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o37_777_777_772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("4.294.967.290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("4.294.967.290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("ff_ff_ff_fa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("FF_FF_FF_FA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("37_777_777_772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("11111111_11111111_11111111_11111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("4.294.967.290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d4.294.967.290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xff_ff_ff_fa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xFF_FF_FF_FA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o37_777_777_772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("4.294.967.290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("4.294.967.290"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("ff_ff_ff_fa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FF_FF_FF_FA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("37_777_777_772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("11111111_11111111_11111111_11111010")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4] =
                    {
                        .value = UINT32_MAX - 4,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("4294967291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d4294967291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0xfffffffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0xFFFFFFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o37777777773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b11111111111111111111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("4294967291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("4294967291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("fffffffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("FFFFFFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("37777777773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("11111111111111111111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("4294967291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("4294967291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("fffffffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("        FFFFFFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                     37777777773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                11111111111111111111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("4294967291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d4294967291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0xfffffffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x00000000FFFFFFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000037777777773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000011111111111111111111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("4294967291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d4294967291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xfffffffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xFFFFFFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o37777777773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b11111111111111111111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("4294967291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("4294967291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("fffffffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("FFFFFFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("37777777773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("11111111111111111111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffffffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("00000000FFFFFFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000037777777773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000011111111111111111111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffffffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("FFFFFFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("37777777773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("11111111111111111111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("4.294.967.291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d4.294.967.291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xff_ff_ff_fb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xFF_FF_FF_FB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o37_777_777_773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("4.294.967.291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("4.294.967.291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("ff_ff_ff_fb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("FF_FF_FF_FB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("37_777_777_773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("11111111_11111111_11111111_11111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("4.294.967.291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d4.294.967.291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xff_ff_ff_fb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xFF_FF_FF_FB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o37_777_777_773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("4.294.967.291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("4.294.967.291"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("ff_ff_ff_fb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FF_FF_FF_FB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("37_777_777_773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("11111111_11111111_11111111_11111011")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3] =
                    {
                        .value = UINT32_MAX - 3,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("4294967292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d4294967292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0xfffffffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0xFFFFFFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o37777777774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b11111111111111111111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("4294967292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("4294967292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("fffffffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("FFFFFFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("37777777774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("11111111111111111111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("4294967292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("4294967292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("fffffffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("        FFFFFFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                     37777777774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                11111111111111111111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("4294967292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d4294967292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0xfffffffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x00000000FFFFFFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000037777777774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000011111111111111111111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("4294967292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d4294967292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xfffffffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xFFFFFFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o37777777774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b11111111111111111111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("4294967292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("4294967292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("fffffffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("FFFFFFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("37777777774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("11111111111111111111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffffffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("00000000FFFFFFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000037777777774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000011111111111111111111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffffffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("FFFFFFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("37777777774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("11111111111111111111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("4.294.967.292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d4.294.967.292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xff_ff_ff_fc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xFF_FF_FF_FC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o37_777_777_774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("4.294.967.292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("4.294.967.292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("ff_ff_ff_fc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("FF_FF_FF_FC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("37_777_777_774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("11111111_11111111_11111111_11111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("4.294.967.292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d4.294.967.292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xff_ff_ff_fc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xFF_FF_FF_FC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o37_777_777_774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("4.294.967.292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("4.294.967.292"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("ff_ff_ff_fc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FF_FF_FF_FC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("37_777_777_774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("11111111_11111111_11111111_11111100")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2] =
                    {
                        .value = UINT32_MAX - 2,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("4294967293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d4294967293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0xfffffffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0xFFFFFFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o37777777775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b11111111111111111111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("4294967293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("4294967293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("fffffffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("FFFFFFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("37777777775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("11111111111111111111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("4294967293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("4294967293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("fffffffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("        FFFFFFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                     37777777775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                11111111111111111111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("4294967293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d4294967293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0xfffffffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x00000000FFFFFFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000037777777775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000011111111111111111111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("4294967293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d4294967293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xfffffffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xFFFFFFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o37777777775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b11111111111111111111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("4294967293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("4294967293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("fffffffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("FFFFFFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("37777777775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("11111111111111111111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffffffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("00000000FFFFFFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000037777777775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000011111111111111111111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffffffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("FFFFFFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("37777777775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("11111111111111111111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("4.294.967.293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d4.294.967.293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xff_ff_ff_fd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xFF_FF_FF_FD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o37_777_777_775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("4.294.967.293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("4.294.967.293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("ff_ff_ff_fd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("FF_FF_FF_FD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("37_777_777_775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("11111111_11111111_11111111_11111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("4.294.967.293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d4.294.967.293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xff_ff_ff_fd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xFF_FF_FF_FD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o37_777_777_775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("4.294.967.293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("4.294.967.293"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("ff_ff_ff_fd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FF_FF_FF_FD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("37_777_777_775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("11111111_11111111_11111111_11111101")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1] =
                    {
                        .value = UINT32_MAX - 1,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("4294967294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d4294967294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0xfffffffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0xFFFFFFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o37777777776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b11111111111111111111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("4294967294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("4294967294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("fffffffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("FFFFFFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("37777777776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("11111111111111111111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("4294967294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("4294967294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("fffffffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("        FFFFFFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                     37777777776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                11111111111111111111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("4294967294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d4294967294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0xfffffffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x00000000FFFFFFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000037777777776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000011111111111111111111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("4294967294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d4294967294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xfffffffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xFFFFFFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o37777777776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b11111111111111111111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("4294967294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("4294967294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("fffffffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("FFFFFFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("37777777776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("11111111111111111111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffffffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("00000000FFFFFFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000037777777776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000011111111111111111111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffffffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("FFFFFFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("37777777776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("11111111111111111111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("4.294.967.294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d4.294.967.294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xff_ff_ff_fe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xFF_FF_FF_FE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o37_777_777_776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("4.294.967.294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("4.294.967.294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("ff_ff_ff_fe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("FF_FF_FF_FE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("37_777_777_776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("11111111_11111111_11111111_11111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("4.294.967.294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d4.294.967.294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xff_ff_ff_fe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xFF_FF_FF_FE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o37_777_777_776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("4.294.967.294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("4.294.967.294"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("ff_ff_ff_fe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FF_FF_FF_FE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("37_777_777_776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("11111111_11111111_11111111_11111110")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX] =
                    {
                        .value = UINT32_MAX,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("4294967295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d4294967295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0xffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0xFFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o37777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b11111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("4294967295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("4294967295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("ffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("FFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("37777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("11111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("4294967295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("4294967295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("ffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("        FFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                     37777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                11111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("4294967295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d4294967295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0xffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x00000000FFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000037777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000011111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("4294967295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d4294967295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xFFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o37777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b11111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("4294967295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("4294967295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("ffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("FFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("37777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("11111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("ffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("00000000FFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000037777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000011111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("4294967295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("ffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("FFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("37777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("11111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("4.294.967.295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d4.294.967.295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xff_ff_ff_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xFF_FF_FF_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o37_777_777_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("4.294.967.295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("4.294.967.295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("ff_ff_ff_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("FF_FF_FF_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("37_777_777_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("11111111_11111111_11111111_11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("4.294.967.295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d4.294.967.295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xff_ff_ff_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xFF_FF_FF_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o37_777_777_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("4.294.967.295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("4.294.967.295"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("ff_ff_ff_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FF_FF_FF_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("37_777_777_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("11111111_11111111_11111111_11111111")
                        },
                    },
                },

// ==================== U64 ====================

                [(u64)UnsignedTestCaseId::u64] =
                {
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_ZERO] =
                    {
                        .value = 0,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL(" 0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("   0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("       0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("               0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                               0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                               0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("00"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d0000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x00000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x0000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("00000000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d00000000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o0000000000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("                   0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("                   0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("               0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("               0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("                     0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("                                                               0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("0000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00000000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00000000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("0000000000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("0"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("0"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("00000000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d00000000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o0000000000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00000000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00000000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("0000000000000000000000"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000000")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_ONE] =
                    {
                        .value = 1,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL(" 1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("   1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("       1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("               1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                               1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                               1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("01"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d0001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x00000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x0000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("00000000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d00000000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o0000000000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("                   1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("                   1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("               1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("               1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("                     1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("                                                               1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("01"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("0000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00000000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00000000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("0000000000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("1"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("1"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("00000000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d00000000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o0000000000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00000000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00000000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("0000000000000000000001"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000001")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_TWO] =
                    {
                        .value = 2,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL(" 2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("   2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("       2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("               2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                               2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                              10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("02"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d0002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x00000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x0000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("00000000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d00000000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o0000000000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("                   2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("                   2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("               2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("               2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("                     2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("                                                              10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("02"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("0000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00000000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00000000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("0000000000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("2"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("10"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("00000000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d00000000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o0000000000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00000000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00000000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("0000000000000000000002"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000010")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_FOUR] =
                    {
                        .value = 4,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL(" 4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("   4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("       4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("               4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                               4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                             100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("04"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d0004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x00000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x0000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("00000000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d00000000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o0000000000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("                   4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("                   4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("               4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("               4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("                     4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("                                                             100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("04"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("0000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00000000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00000000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("0000000000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("4"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("00000000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d00000000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o0000000000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00000000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00000000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("0000000000000000000004"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000000100")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_EIGHT] =
                    {
                        .value = 8,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL(" 8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("   8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("       8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("               8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                              10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                            1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("08"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d0008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x00000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x0000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("00000000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d00000000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o0000000000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("                   8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("                   8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("               8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("               8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("                    10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("                                                            1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("08"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("0000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00000000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00000000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("0000000000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("8"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("1000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("00000000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d00000000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o0000000000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00000000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00000000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0000000000000008"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("0000000000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000001000")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_SIXTEEN] =
                    {
                        .value = 16,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("  16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("      10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("              10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("                              20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("                                                           10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d0016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x00000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x0000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000000000000000000000020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("00000000000000000016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d00000000000000000016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x0000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o0000000000000000000020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("                  16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("                  16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("              10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("              10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("                    20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("                                                           10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("00000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("0000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000000000000000000000020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00000000000000000016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("00000000000000000016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("0000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("0000000000000000000020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("16"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("10"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("20"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("10000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("00000000000000000016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d00000000000000000016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x0000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o0000000000000000000020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00000000000000000016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("00000000000000000016"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("0000000000000010"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("0000000000000000000020"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("0000000000000000000000000000000000000000000000000000000000010000")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2] =
                    {
                        .value = UINT64_MAX / 2,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("9223372036854775807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d9223372036854775807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0x7fffffffffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0x7FFFFFFFFFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o777777777777777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("9223372036854775807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("9223372036854775807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("7fffffffffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("7FFFFFFFFFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("777777777777777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("9223372036854775807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("9223372036854775807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("7fffffffffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("7FFFFFFFFFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("           777777777777777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL(" 111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("9223372036854775807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d9223372036854775807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0x7fffffffffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0x7FFFFFFFFFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000000777777777777777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b0111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("09223372036854775807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d09223372036854775807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x7fffffffffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0x7FFFFFFFFFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o0777777777777777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b0111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL(" 9223372036854775807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL(" 9223372036854775807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("7fffffffffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("7FFFFFFFFFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL(" 777777777777777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL(" 111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("9223372036854775807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("9223372036854775807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("7fffffffffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("7FFFFFFFFFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000000777777777777777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("0111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("09223372036854775807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("09223372036854775807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("7fffffffffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("7FFFFFFFFFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("0777777777777777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("0111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("9.223.372.036.854.775.807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d9.223.372.036.854.775.807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x7f_ff_ff_ff_ff_ff_ff_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0x7F_FF_FF_FF_FF_FF_FF_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o777_777_777_777_777_777_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b1111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("9.223.372.036.854.775.807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("9.223.372.036.854.775.807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("7f_ff_ff_ff_ff_ff_ff_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("7F_FF_FF_FF_FF_FF_FF_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("777_777_777_777_777_777_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("1111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("09.223.372.036.854.775.807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d09.223.372.036.854.775.807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x7f_ff_ff_ff_ff_ff_ff_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0x7F_FF_FF_FF_FF_FF_FF_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o0777_777_777_777_777_777_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b01111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("09.223.372.036.854.775.807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("09.223.372.036.854.775.807"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("7f_ff_ff_ff_ff_ff_ff_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("7F_FF_FF_FF_FF_FF_FF_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("0777_777_777_777_777_777_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("01111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5] =
                    {
                        .value = UINT64_MAX - 5,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("18446744073709551610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d18446744073709551610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0xfffffffffffffffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0xFFFFFFFFFFFFFFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o1777777777777777777772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b1111111111111111111111111111111111111111111111111111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("18446744073709551610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("18446744073709551610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("fffffffffffffffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("FFFFFFFFFFFFFFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("1777777777777777777772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("18446744073709551610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("18446744073709551610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("fffffffffffffffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("FFFFFFFFFFFFFFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("          1777777777777777777772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("18446744073709551610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d18446744073709551610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0xfffffffffffffffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0xFFFFFFFFFFFFFFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000001777777777777777777772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b1111111111111111111111111111111111111111111111111111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("18446744073709551610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d18446744073709551610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xfffffffffffffffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xFFFFFFFFFFFFFFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o1777777777777777777772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b1111111111111111111111111111111111111111111111111111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("18446744073709551610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("18446744073709551610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("fffffffffffffffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("FFFFFFFFFFFFFFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("1777777777777777777772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffffffffffffffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("FFFFFFFFFFFFFFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000001777777777777777777772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffffffffffffffa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("FFFFFFFFFFFFFFFA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("1777777777777777777772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("18.446.744.073.709.551.610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d18.446.744.073.709.551.610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xff_ff_ff_ff_ff_ff_ff_fa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xFF_FF_FF_FF_FF_FF_FF_FA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o1_777_777_777_777_777_777_772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("18.446.744.073.709.551.610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("18.446.744.073.709.551.610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("ff_ff_ff_ff_ff_ff_ff_fa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("FF_FF_FF_FF_FF_FF_FF_FA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("1_777_777_777_777_777_777_772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("18.446.744.073.709.551.610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d18.446.744.073.709.551.610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xff_ff_ff_ff_ff_ff_ff_fa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xFF_FF_FF_FF_FF_FF_FF_FA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o1_777_777_777_777_777_777_772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111010"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("18.446.744.073.709.551.610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("18.446.744.073.709.551.610"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("ff_ff_ff_ff_ff_ff_ff_fa"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FF_FF_FF_FF_FF_FF_FF_FA"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("1_777_777_777_777_777_777_772"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111010")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4] =
                    {
                        .value = UINT64_MAX - 4,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("18446744073709551611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d18446744073709551611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0xfffffffffffffffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0xFFFFFFFFFFFFFFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o1777777777777777777773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b1111111111111111111111111111111111111111111111111111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("18446744073709551611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("18446744073709551611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("fffffffffffffffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("FFFFFFFFFFFFFFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("1777777777777777777773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("18446744073709551611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("18446744073709551611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("fffffffffffffffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("FFFFFFFFFFFFFFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("          1777777777777777777773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("18446744073709551611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d18446744073709551611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0xfffffffffffffffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0xFFFFFFFFFFFFFFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000001777777777777777777773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b1111111111111111111111111111111111111111111111111111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("18446744073709551611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d18446744073709551611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xfffffffffffffffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xFFFFFFFFFFFFFFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o1777777777777777777773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b1111111111111111111111111111111111111111111111111111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("18446744073709551611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("18446744073709551611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("fffffffffffffffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("FFFFFFFFFFFFFFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("1777777777777777777773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffffffffffffffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("FFFFFFFFFFFFFFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000001777777777777777777773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffffffffffffffb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("FFFFFFFFFFFFFFFB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("1777777777777777777773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("18.446.744.073.709.551.611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d18.446.744.073.709.551.611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xff_ff_ff_ff_ff_ff_ff_fb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xFF_FF_FF_FF_FF_FF_FF_FB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o1_777_777_777_777_777_777_773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("18.446.744.073.709.551.611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("18.446.744.073.709.551.611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("ff_ff_ff_ff_ff_ff_ff_fb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("FF_FF_FF_FF_FF_FF_FF_FB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("1_777_777_777_777_777_777_773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("18.446.744.073.709.551.611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d18.446.744.073.709.551.611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xff_ff_ff_ff_ff_ff_ff_fb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xFF_FF_FF_FF_FF_FF_FF_FB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o1_777_777_777_777_777_777_773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111011"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("18.446.744.073.709.551.611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("18.446.744.073.709.551.611"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("ff_ff_ff_ff_ff_ff_ff_fb"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FF_FF_FF_FF_FF_FF_FF_FB"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("1_777_777_777_777_777_777_773"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111011")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3] =
                    {
                        .value = UINT64_MAX - 3,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("18446744073709551612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d18446744073709551612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0xfffffffffffffffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0xFFFFFFFFFFFFFFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o1777777777777777777774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b1111111111111111111111111111111111111111111111111111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("18446744073709551612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("18446744073709551612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("fffffffffffffffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("FFFFFFFFFFFFFFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("1777777777777777777774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("18446744073709551612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("18446744073709551612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("fffffffffffffffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("FFFFFFFFFFFFFFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("          1777777777777777777774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("18446744073709551612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d18446744073709551612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0xfffffffffffffffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0xFFFFFFFFFFFFFFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000001777777777777777777774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b1111111111111111111111111111111111111111111111111111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("18446744073709551612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d18446744073709551612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xfffffffffffffffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xFFFFFFFFFFFFFFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o1777777777777777777774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b1111111111111111111111111111111111111111111111111111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("18446744073709551612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("18446744073709551612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("fffffffffffffffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("FFFFFFFFFFFFFFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("1777777777777777777774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffffffffffffffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("FFFFFFFFFFFFFFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000001777777777777777777774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffffffffffffffc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("FFFFFFFFFFFFFFFC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("1777777777777777777774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("18.446.744.073.709.551.612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d18.446.744.073.709.551.612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xff_ff_ff_ff_ff_ff_ff_fc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xFF_FF_FF_FF_FF_FF_FF_FC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o1_777_777_777_777_777_777_774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("18.446.744.073.709.551.612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("18.446.744.073.709.551.612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("ff_ff_ff_ff_ff_ff_ff_fc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("FF_FF_FF_FF_FF_FF_FF_FC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("1_777_777_777_777_777_777_774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("18.446.744.073.709.551.612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d18.446.744.073.709.551.612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xff_ff_ff_ff_ff_ff_ff_fc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xFF_FF_FF_FF_FF_FF_FF_FC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o1_777_777_777_777_777_777_774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111100"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("18.446.744.073.709.551.612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("18.446.744.073.709.551.612"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("ff_ff_ff_ff_ff_ff_ff_fc"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FF_FF_FF_FF_FF_FF_FF_FC"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("1_777_777_777_777_777_777_774"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111100")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2] =
                    {
                        .value = UINT64_MAX - 2,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("18446744073709551613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d18446744073709551613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0xfffffffffffffffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0xFFFFFFFFFFFFFFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o1777777777777777777775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b1111111111111111111111111111111111111111111111111111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("18446744073709551613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("18446744073709551613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("fffffffffffffffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("FFFFFFFFFFFFFFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("1777777777777777777775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("18446744073709551613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("18446744073709551613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("fffffffffffffffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("FFFFFFFFFFFFFFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("          1777777777777777777775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("18446744073709551613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d18446744073709551613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0xfffffffffffffffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0xFFFFFFFFFFFFFFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000001777777777777777777775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b1111111111111111111111111111111111111111111111111111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("18446744073709551613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d18446744073709551613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xfffffffffffffffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xFFFFFFFFFFFFFFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o1777777777777777777775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b1111111111111111111111111111111111111111111111111111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("18446744073709551613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("18446744073709551613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("fffffffffffffffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("FFFFFFFFFFFFFFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("1777777777777777777775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffffffffffffffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("FFFFFFFFFFFFFFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000001777777777777777777775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffffffffffffffd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("FFFFFFFFFFFFFFFD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("1777777777777777777775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("18.446.744.073.709.551.613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d18.446.744.073.709.551.613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xff_ff_ff_ff_ff_ff_ff_fd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xFF_FF_FF_FF_FF_FF_FF_FD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o1_777_777_777_777_777_777_775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("18.446.744.073.709.551.613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("18.446.744.073.709.551.613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("ff_ff_ff_ff_ff_ff_ff_fd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("FF_FF_FF_FF_FF_FF_FF_FD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("1_777_777_777_777_777_777_775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("18.446.744.073.709.551.613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d18.446.744.073.709.551.613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xff_ff_ff_ff_ff_ff_ff_fd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xFF_FF_FF_FF_FF_FF_FF_FD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o1_777_777_777_777_777_777_775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111101"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("18.446.744.073.709.551.613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("18.446.744.073.709.551.613"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("ff_ff_ff_ff_ff_ff_ff_fd"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FF_FF_FF_FF_FF_FF_FF_FD"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("1_777_777_777_777_777_777_775"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111101")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1] =
                    {
                        .value = UINT64_MAX - 1,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("18446744073709551614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d18446744073709551614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0xfffffffffffffffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0xFFFFFFFFFFFFFFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o1777777777777777777776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b1111111111111111111111111111111111111111111111111111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("18446744073709551614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("18446744073709551614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("fffffffffffffffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("FFFFFFFFFFFFFFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("1777777777777777777776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("18446744073709551614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("18446744073709551614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("fffffffffffffffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("FFFFFFFFFFFFFFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("          1777777777777777777776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("18446744073709551614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d18446744073709551614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0xfffffffffffffffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0xFFFFFFFFFFFFFFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000001777777777777777777776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b1111111111111111111111111111111111111111111111111111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("18446744073709551614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d18446744073709551614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xfffffffffffffffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xFFFFFFFFFFFFFFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o1777777777777777777776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b1111111111111111111111111111111111111111111111111111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("18446744073709551614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("18446744073709551614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("fffffffffffffffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("FFFFFFFFFFFFFFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("1777777777777777777776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffffffffffffffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("FFFFFFFFFFFFFFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000001777777777777777777776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("fffffffffffffffe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("FFFFFFFFFFFFFFFE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("1777777777777777777776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("18.446.744.073.709.551.614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d18.446.744.073.709.551.614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xff_ff_ff_ff_ff_ff_ff_fe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xFF_FF_FF_FF_FF_FF_FF_FE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o1_777_777_777_777_777_777_776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("18.446.744.073.709.551.614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("18.446.744.073.709.551.614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("ff_ff_ff_ff_ff_ff_ff_fe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("FF_FF_FF_FF_FF_FF_FF_FE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("1_777_777_777_777_777_777_776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("18.446.744.073.709.551.614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d18.446.744.073.709.551.614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xff_ff_ff_ff_ff_ff_ff_fe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xFF_FF_FF_FF_FF_FF_FF_FE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o1_777_777_777_777_777_777_776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111110"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("18.446.744.073.709.551.614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("18.446.744.073.709.551.614"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("ff_ff_ff_ff_ff_ff_ff_fe"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FF_FF_FF_FF_FF_FF_FF_FE"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("1_777_777_777_777_777_777_776"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111110")
                        },
                    },
                    [(u64)UnsignedTestCaseNumber::UNSIGNED_TEST_CASE_NUMBER_UINT_MAX] =
                    {
                        .value = UINT64_MAX,
                        .expected_results =
                        {
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       STRING_TEST_LITERAL("18446744073709551615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       STRING_TEST_LITERAL("0d18446744073709551615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             STRING_TEST_LITERAL("0xffffffffffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             STRING_TEST_LITERAL("0xFFFFFFFFFFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         STRING_TEST_LITERAL("0o1777777777777777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        STRING_TEST_LITERAL("0b1111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             STRING_TEST_LITERAL("18446744073709551615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             STRING_TEST_LITERAL("18446744073709551615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   STRING_TEST_LITERAL("ffffffffffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   STRING_TEST_LITERAL("FFFFFFFFFFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               STRING_TEST_LITERAL("1777777777777777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         STRING_TEST_LITERAL("18446744073709551615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         STRING_TEST_LITERAL("18446744073709551615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               STRING_TEST_LITERAL("ffffffffffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              STRING_TEST_LITERAL("FFFFFFFFFFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          STRING_TEST_LITERAL("          1777777777777777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          STRING_TEST_LITERAL("18446744073709551615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          STRING_TEST_LITERAL("0d18446744073709551615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                STRING_TEST_LITERAL("0xffffffffffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               STRING_TEST_LITERAL("0xFFFFFFFFFFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           STRING_TEST_LITERAL("0o00000000001777777777777777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          STRING_TEST_LITERAL("0b1111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("18446744073709551615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          STRING_TEST_LITERAL("0d18446744073709551615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xffffffffffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                STRING_TEST_LITERAL("0xFFFFFFFFFFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            STRING_TEST_LITERAL("0o1777777777777777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           STRING_TEST_LITERAL("0b1111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("18446744073709551615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               STRING_TEST_LITERAL("18446744073709551615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("ffffffffffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     STRING_TEST_LITERAL("FFFFFFFFFFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 STRING_TEST_LITERAL("1777777777777777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("ffffffffffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     STRING_TEST_LITERAL("FFFFFFFFFFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("00000000001777777777777777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                STRING_TEST_LITERAL("18446744073709551615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("ffffffffffffffff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      STRING_TEST_LITERAL("FFFFFFFFFFFFFFFF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  STRING_TEST_LITERAL("1777777777777777777777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 STRING_TEST_LITERAL("1111111111111111111111111111111111111111111111111111111111111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              STRING_TEST_LITERAL("18.446.744.073.709.551.615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              STRING_TEST_LITERAL("0d18.446.744.073.709.551.615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xff_ff_ff_ff_ff_ff_ff_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    STRING_TEST_LITERAL("0xFF_FF_FF_FF_FF_FF_FF_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                STRING_TEST_LITERAL("0o1_777_777_777_777_777_777_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("18.446.744.073.709.551.615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    STRING_TEST_LITERAL("18.446.744.073.709.551.615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("ff_ff_ff_ff_ff_ff_ff_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          STRING_TEST_LITERAL("FF_FF_FF_FF_FF_FF_FF_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      STRING_TEST_LITERAL("1_777_777_777_777_777_777_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     STRING_TEST_LITERAL("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("18.446.744.073.709.551.615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 STRING_TEST_LITERAL("0d18.446.744.073.709.551.615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xff_ff_ff_ff_ff_ff_ff_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       STRING_TEST_LITERAL("0xFF_FF_FF_FF_FF_FF_FF_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   STRING_TEST_LITERAL("0o1_777_777_777_777_777_777_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  STRING_TEST_LITERAL("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("18.446.744.073.709.551.615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       STRING_TEST_LITERAL("18.446.744.073.709.551.615"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("ff_ff_ff_ff_ff_ff_ff_ff"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = STRING_TEST_LITERAL("FF_FF_FF_FF_FF_FF_FF_FF"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         STRING_TEST_LITERAL("1_777_777_777_777_777_777_777"),
                            [(u64)UnsignedFormatTestCase::UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        STRING_TEST_LITERAL("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111")
                        },
                    },
                },
            };

            for (EACH_ENUM_INT(UnsignedTestCaseId, type_i))
            {
                for (EACH_ENUM_INT(UnsignedTestCaseNumber, case_value_i))
                {
                    let uint_case = &cases[type_i][case_value_i];
                    let value = uint_case->value;

                    for (EACH_ENUM_INT(UnsignedFormatTestCase, case_i))
                    {
                        let format_string = format_strings[type_i][case_i];
                        let expected_string = uint_case->expected_results[case_i];
                        let test_type = (UnsignedTestCaseId)type_i;

                        String<Char> result_string;
                        switch (test_type)
                        {
                            break; case UnsignedTestCaseId::u8: result_string =  string_format(arena, format_string, (u8)value);
                            break; case UnsignedTestCaseId::u16: result_string = string_format(arena, format_string, (u16)value);
                            break; case UnsignedTestCaseId::u32: result_string = string_format(arena, format_string, (u32)value);
                            break; case UnsignedTestCaseId::u64: result_string = string_format(arena, format_string, (u64)value);
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
            bool success = string_first_sequence(STRING_TEST_LITERAL("hello world"), STRING_TEST_LITERAL("hello")) == 0;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // Match in middle
            bool success = string_first_sequence(STRING_TEST_LITERAL("hello world"), STRING_TEST_LITERAL("world")) == 6;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // Match at end
            bool success = string_first_sequence(STRING_TEST_LITERAL("hello.txt"), STRING_TEST_LITERAL(".txt")) == 5;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // No match
            bool success = string_first_sequence(STRING_TEST_LITERAL("hello world"), STRING_TEST_LITERAL("foo")) == BUSTER_STRING_NO_MATCH;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // Empty substring matches at 0
            bool success = string_first_sequence(STRING_TEST_LITERAL("hello"), STRING_TEST_LITERAL("")) == 0;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // Empty string with empty substring
            bool success = string_first_sequence(STRING_TEST_LITERAL(""), STRING_TEST_LITERAL("")) == 0;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // Empty string with non-empty substring
            bool success = string_first_sequence(STRING_TEST_LITERAL(""), STRING_TEST_LITERAL("a")) == BUSTER_STRING_NO_MATCH;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // Substring longer than string
            bool success = string_first_sequence(STRING_TEST_LITERAL("hi"), STRING_TEST_LITERAL("hello")) == BUSTER_STRING_NO_MATCH;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // Exact match
            bool success = string_first_sequence(STRING_TEST_LITERAL("abc"), STRING_TEST_LITERAL("abc")) == 0;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // Multiple occurrences - should return first
            bool success = string_first_sequence(STRING_TEST_LITERAL("abcabc"), STRING_TEST_LITERAL("abc")) == 0;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // Single character match
            bool success = string_first_sequence(STRING_TEST_LITERAL("hello"), STRING_TEST_LITERAL("l")) == 2;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            // Partial match should not count
            bool success = string_first_sequence(STRING_TEST_LITERAL("abcd"), STRING_TEST_LITERAL("abd")) == BUSTER_STRING_NO_MATCH;
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
    }

    // string_ends_with_sequence
    {
        {
            bool success = string_ends_with_sequence(STRING_TEST_LITERAL("hello.txt"), STRING_TEST_LITERAL(".txt"));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            bool success = string_ends_with_sequence(STRING_TEST_LITERAL("test.vert.spv"), STRING_TEST_LITERAL(".vert.spv"));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            bool success = string_ends_with_sequence(STRING_TEST_LITERAL("abc"), STRING_TEST_LITERAL("abc"));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            bool success = string_ends_with_sequence(STRING_TEST_LITERAL("hello"), STRING_TEST_LITERAL(""));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            bool success = !string_ends_with_sequence(STRING_TEST_LITERAL("hello.txt"), STRING_TEST_LITERAL(".c"));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            bool success = !string_ends_with_sequence(STRING_TEST_LITERAL("ab"), STRING_TEST_LITERAL("abc"));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            bool success = !string_ends_with_sequence(STRING_TEST_LITERAL("hi"), STRING_TEST_LITERAL("hello"));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            bool success = string_ends_with_sequence(STRING_TEST_LITERAL(""), STRING_TEST_LITERAL(""));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            bool success = !string_ends_with_sequence(STRING_TEST_LITERAL(""), STRING_TEST_LITERAL("a"));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            bool success = !string_ends_with_sequence(STRING_TEST_LITERAL("abcde"), STRING_TEST_LITERAL("cdf"));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
        {
            bool success = !string_ends_with_sequence(STRING_TEST_LITERAL("txtfile"), STRING_TEST_LITERAL("txt"));
            result.succeeded_test_count += success;
            result.test_count += 1;
        }
    }

    return result;
}

BUSTER_F_IMPL UnitTestResult string_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {};
    {
        let character8_result = string_tests<char8>(arguments);
        result.succeeded_test_count += character8_result.succeeded_test_count;
        result.test_count += character8_result.test_count;
    }
    {
        let character16_result = string_tests<char16>(arguments);
        result.succeeded_test_count += character16_result.succeeded_test_count;
        result.test_count += character16_result.test_count;
    }

    return result;
}
#endif
