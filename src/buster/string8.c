#pragma once
#include <buster/string8.h>
#include <buster/assertion.h>
#include <buster/arena.h>
#include <buster/os.h>

BUSTER_GLOBAL_LOCAL void string8_reverse(String8 s)
{
    let restrict pointer = s.pointer;
    for (u64 i = 0, reverse_i = s.length - 1; i < reverse_i; i += 1, reverse_i -= 1)
    {
        let ch = pointer[i];
        pointer[i] = pointer[reverse_i];
        pointer[reverse_i] = ch;
    }
}

BUSTER_GLOBAL_LOCAL String8 string8_format_u64_hexadecimal(String8 buffer, u64 value, bool upper)
{
    String8 result = {};

    if (value == 0)
    {
        buffer.pointer[0] = '0';
        result = (String8) { buffer.pointer, 1};
    }
    else
    {
        let v = value;
        u64 i = 0;
        char8 alpha_start = upper ? 'A' : 'a';

        while (v != 0)
        {
            let digit = v % 16;
            let ch = (char8)(digit > 9 ? (digit - 10 + alpha_start) : (digit + '0'));
            BUSTER_CHECK(i < buffer.length);
            buffer.pointer[i] = ch;
            i += 1;
            v = v / 16;
        }

        let length = i;

        result = (String8) { buffer.pointer , length };
        string8_reverse(result);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL String8 string8_format_i64_decimal(String8 buffer, u64 value, bool treat_as_signed)
{
    String8 result = {};

    if (value == 0)
    {
        buffer.pointer[0] = '0';
        result = (String8) { buffer.pointer, 1};
    }
    else
    {
        u64 i = treat_as_signed;

        buffer.pointer[0] = '-';
        let v = value;

        while (v != 0)
        {
            let digit = v % 10;
            let ch = (u8)(digit + '0');
            BUSTER_CHECK(i < buffer.length);
            buffer.pointer[i] = ch;
            i += 1;
            v = v / 10;
        }

        let length = i;

        result = (String8) { buffer.pointer + treat_as_signed, length - treat_as_signed };
        string8_reverse(result);
        result.pointer -= treat_as_signed;
        result.length += treat_as_signed;
    }

    return result;
}


BUSTER_GLOBAL_LOCAL String8 string8_format_u64_octal(String8 buffer, u64 value)
{
    String8 result = {};

    if (value == 0)
    {
        buffer.pointer[0] = '0';
        result = (String8) { buffer.pointer, 1};
    }
    else
    {
        u64 i = 0;
        let v = value;

        while (v != 0)
        {
            let digit = v % 8;
            let ch = (u8)(digit + '0');
            BUSTER_CHECK(i < buffer.length);
            buffer.pointer[i] = ch;
            i += 1;
            v = v / 8;
        }

        let length = i;

        result = (String8) { buffer.pointer, length };
        string8_reverse(result);
    }

    return result;
}

BUSTER_IMPL IntegerParsingU64 string8_parse_u64_decimal(const char8* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let ch = p[i];

        if (!code_point_is_decimal(ch))
        {
            break;
        }

        i += 1;
        value = parsing_accumulate_decimal(value, ch);
    }

    return (IntegerParsingU64){ .value = value, .length = i };
}

BUSTER_IMPL IntegerParsingU64 string8_parse_u64_hexadecimal(const char* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let ch = p[i];

        if (!code_point_is_hexadecimal(ch))
        {
            break;
        }

        i += 1;
        value = parsing_accumulate_hexadecimal(value, ch);
    }

    return (IntegerParsingU64){ .value = value, .length = i };
}

BUSTER_IMPL IntegerParsingU64 string8_parse_u64_octal(const char8* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let ch = p[i];

        if (!code_point_is_octal(ch))
        {
            break;
        }

        i += 1;
        value = parsing_accumulate_octal(value, (u8)ch);
    }

    return (IntegerParsingU64) { .value = value, .length = i };
}

BUSTER_IMPL IntegerParsingU64 string8_parse_u64_binary(const char8* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let ch = p[i];

        if (!code_point_is_binary(ch))
        {
            break;
        }

        i += 1;
        value = parsing_accumulate_binary(value, ch);
    }

    return (IntegerParsingU64){ .value = value, .length = i };
}

BUSTER_GLOBAL_LOCAL String8 string8_format_u64_binary(String8 buffer, u64 value)
{
    String8 result = {};

    if (value == 0)
    {
        buffer.pointer[0] = '0';
        result = (String8) { buffer.pointer, 1};
    }
    else
    {
        u64 i = 0;
        let v = value;

        while (v != 0)
        {
            let digit = v % 2;
            let ch = (u8)(digit + '0');
            BUSTER_CHECK(i < buffer.length);
            buffer.pointer[i] = ch;
            i += 1;
            v = v / 2;
        }

        let length = i;

        result = (String8) { buffer.pointer, length };
        string8_reverse(result);
    }

    return result;
}

BUSTER_IMPL StringFormatResult string8_format_va(String8 buffer_slice, String8 format, va_list variable_arguments)
{
    StringFormatResult result = {};
    bool is_illformed_string = buffer_slice.pointer == 0 && buffer_slice.length != 0;
    u64 format_index = 0;

    if (is_illformed_string)
    {
        result.real_buffer_index = buffer_slice.length;
    }

    while (format_index < format.length)
    {
        while (format_index != format.length && format.pointer[format_index] != '{')
        {
            if (result.real_buffer_index < buffer_slice.length)
            {
                buffer_slice.pointer[result.real_buffer_index] = format.pointer[result.real_format_index];
                result.real_buffer_index += 1;
                result.real_format_index += 1;
            }

            result.needed_code_unit_count += 1;
            format_index += 1;
        }

        if (format.pointer[format_index] == '{')
        {
            char8 format_buffer[128];
            u64 format_buffer_i;

            for (format_buffer_i = 0; format.pointer[format_index] != '}'; format_buffer_i += 1, format_index += 1)
            {
                format_buffer[format_buffer_i] = format.pointer[format_index];
            }

            format_buffer[format_buffer_i] = '}';
            format_buffer_i += 1;
            format_index += 1;

            let whole_format_string = string8_from_pointer_length(format_buffer, format_buffer_i);
            ENUM(FormatTypeId, 
                    FORMAT_TYPE_CHAR_OS,
                    FORMAT_TYPE_STRING_OS,
                    FORMAT_TYPE_STRING_OS_LIST,
                    FORMAT_TYPE_STRING8,
                    FORMAT_TYPE_STRING16,
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
                    FORMAT_TYPE_SPECIFIER_COUNT,
                );
            String8 possible_format_strings[FORMAT_TYPE_SPECIFIER_COUNT] = {
                [FORMAT_TYPE_CHAR_OS] = S8("COs"),
                [FORMAT_TYPE_STRING_OS] = S8("SOs"),
                [FORMAT_TYPE_STRING_OS_LIST] = S8("SOsL"),
                [FORMAT_TYPE_STRING8] = S8("S8"),
                [FORMAT_TYPE_STRING16] = S8("S16"),
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

            let first_format = string_first_code_point(whole_format_string, ':');
            bool there_is_format_modifiers = first_format != BUSTER_STRING_NO_MATCH;
            let this_format_string_length = there_is_format_modifiers ? first_format : whole_format_string.length - 1; // Avoid final right brace
            let this_format_string = string8_slice(whole_format_string,
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
                FORMAT_KIND_HEXADECIMAL_UPPER,
                FORMAT_KIND_COUNT,
            );

            ENUM(FormatSpecifier,
                FORMAT_SPECIFIER_D,
                FORMAT_SPECIFIER_X_UPPER,
                FORMAT_SPECIFIER_X_LOWER,
                FORMAT_SPECIFIER_O,
                FORMAT_SPECIFIER_B,
                FORMAT_SPECIFIER_WIDTH,
                FORMAT_SPECIFIER_NO_PREFIX,
                FORMAT_SPECIFIER_DIGIT_GROUP,

                FORMAT_SPECIFIER_COUNT,
            );

            let format_type_id = (FormatTypeId)i;
            bool prefix = false;
            bool prefix_set = false;
            bool digit_group = false;
            u64 width = 0;
            char8 width_character = '0';
            bool width_natural_extension = false;
#define BUSTER_FORMAT_INTEGER_MAX_WIDTH (u64)(64)

            IntegerFormatKind format_kind = FORMAT_KIND_DECIMAL;
            bool integer_format_set = false;

            if (there_is_format_modifiers)
            {
                String8 possible_format_specifier_strings[] = {
                    [FORMAT_SPECIFIER_D] = S8("d"),
                    [FORMAT_SPECIFIER_X_UPPER] = S8("X"),
                    [FORMAT_SPECIFIER_X_LOWER] = S8("x"),
                    [FORMAT_SPECIFIER_O] = S8("o"),
                    [FORMAT_SPECIFIER_B] = S8("b"),
                    [FORMAT_SPECIFIER_WIDTH] = S8("width"),
                    [FORMAT_SPECIFIER_NO_PREFIX] = S8("no_prefix"),
                    [FORMAT_SPECIFIER_DIGIT_GROUP] = S8("digit_group"),
                };
                static_assert(BUSTER_ARRAY_LENGTH(possible_format_specifier_strings) == FORMAT_SPECIFIER_COUNT);

                let whole_format_specifiers_string = string8_slice(whole_format_string, first_format + 1, whole_format_string.length - 1);
                BUSTER_CHECK(whole_format_specifiers_string.length <= whole_format_string.length);
                u64 format_specifier_string_i = 0;

                while (format_specifier_string_i < whole_format_specifiers_string.length && whole_format_specifiers_string.pointer[format_specifier_string_i] != '}')
                {
                    let iteration_left_format_specifiers_string = BUSTER_SLICE_START(whole_format_specifiers_string, format_specifier_string_i);
                    BUSTER_CHECK(iteration_left_format_specifiers_string.length <= whole_format_specifiers_string.length);
                    let equal_index = string_first_code_point(iteration_left_format_specifiers_string, '=');
                    let comma_index = string_first_code_point(iteration_left_format_specifiers_string, ',');
                    let format_specifier_name_end = BUSTER_MIN(equal_index, comma_index);
                    let string_left = format_specifier_name_end == BUSTER_STRING_NO_MATCH;
                    format_specifier_name_end = string_left ? iteration_left_format_specifiers_string.length : format_specifier_name_end;
                    let next_character = string_left ? 0 : (equal_index < comma_index ? '=' : ',');

                    let format_name = string8_slice(iteration_left_format_specifiers_string, 0, format_specifier_name_end);
                    format_specifier_string_i += format_name.length + !string_left;
                    let left_format_specifiers_string = BUSTER_SLICE_START(iteration_left_format_specifiers_string, format_name.length + !string_left);
                    BUSTER_CHECK(left_format_specifiers_string.length <= iteration_left_format_specifiers_string.length);

                    u64 format_i;
                    for (format_i = 0; format_i < FORMAT_SPECIFIER_COUNT; format_i += 1)
                    {
                        let candidate_format_specifier = possible_format_specifier_strings[format_i];
                        if (string_equal(format_name, candidate_format_specifier))
                        {
                            break;
                        }
                    }

                    let format_specifier = (FormatSpecifier)format_i;
                    switch (format_specifier)
                    {
                        break; case FORMAT_SPECIFIER_D:
                        {
                            format_kind = FORMAT_KIND_DECIMAL;
                            integer_format_set = true;
                        }
                        break; case FORMAT_SPECIFIER_X_UPPER:
                        {
                            format_kind = FORMAT_KIND_HEXADECIMAL_UPPER;
                            integer_format_set = true;
                        }
                        break; case FORMAT_SPECIFIER_X_LOWER:
                        {
                            format_kind = FORMAT_KIND_HEXADECIMAL_LOWER;
                            integer_format_set = true;
                        }
                        break; case FORMAT_SPECIFIER_O:
                        {
                            format_kind = FORMAT_KIND_OCTAL;
                            integer_format_set = true;
                        }
                        break; case FORMAT_SPECIFIER_B:
                        {
                            format_kind = FORMAT_KIND_BINARY;
                            integer_format_set = true;
                        }
                        break; case FORMAT_SPECIFIER_WIDTH:
                        {
                            if (next_character == '=')
                            {
                                if (left_format_specifiers_string.pointer[0] == '[')
                                {
                                    width_character = left_format_specifiers_string.pointer[1];

                                    if (left_format_specifiers_string.pointer[2] == ',')
                                    {
                                        let right_bracket_index = string_first_code_point(left_format_specifiers_string, ']');

                                        if (right_bracket_index != BUSTER_STRING_NO_MATCH)
                                        {
                                            u64 width_start = 3;
                                            let width_count_string = string8_slice(left_format_specifiers_string, width_start, right_bracket_index);
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
                                                let width_count_parsing = string8_parse_u64_decimal(width_count_string.pointer);

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
                        break; case FORMAT_SPECIFIER_NO_PREFIX:
                        {
                            prefix = false;
                            prefix_set = true;
                        }
                        break; case FORMAT_SPECIFIER_DIGIT_GROUP:
                        {
                            digit_group = true;
                        }
                        break; case FORMAT_SPECIFIER_COUNT:
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

            bool written = false;
            if (width > BUSTER_FORMAT_INTEGER_MAX_WIDTH)
            {
                width = BUSTER_FORMAT_INTEGER_MAX_WIDTH;
            }

            switch (format_type_id)
            {
                break; case FORMAT_TYPE_STRING_OS_LIST:
                {
                    let string_os_list = va_arg(variable_arguments, StringOsList);
                    let it = string_os_list_iterator_initialize(string_os_list);

                    StringOs string;
                    u64 full_length = 0;
                    while ((string = string_os_list_iterator_next(&it)).pointer)
                    {
                        full_length += string.length + 1; // space
                    }

                    full_length -= full_length != 0;

                    written = result.real_buffer_index + full_length <= buffer_slice.length;
                    if (written)
                    {
                        it = string_os_list_iterator_initialize(string_os_list);
                        let original_index = result.real_buffer_index;

                        while ((string = string_os_list_iterator_next(&it)).pointer)
                        {
                            for (u64 string_i = 0; string_i < string.length; string_i += 1)
                            {
                                buffer_slice.pointer[result.real_buffer_index + string_i] = (char8)string.pointer[string_i];
                            }

                            result.real_buffer_index += string.length;

                            if (result.real_buffer_index < original_index + full_length - 1)
                            {
                                buffer_slice.pointer[result.real_buffer_index] = ' ';
                                result.real_buffer_index += 1;
                            }
                        }
                    }

                    result.needed_code_unit_count += full_length;
                }
                break; case FORMAT_TYPE_STRING_OS:
                {
                    // TODO:
                    let string = va_arg(variable_arguments, StringOs);
                    // TODO: compute proper size
                    let size = BUSTER_SLICE_SIZE(string);
                    written = result.real_buffer_index + size <= buffer_slice.length;
                    if (written)
                    {
                        for (u64 string_i = 0; string_i < string.length; string_i += 1)
                        {
                            buffer_slice.pointer[result.real_buffer_index + string_i] = (char8)string.pointer[string_i];
                        }

                        result.real_buffer_index += string.length;
                    }

                    result.needed_code_unit_count += string.length;
                }
                break; case FORMAT_TYPE_CHAR_OS:
                {
                    // TODO:
                    let os_char = (CharOs)va_arg(variable_arguments, u32);
                    written = result.real_buffer_index < buffer_slice.length;
                    if (written)
                    {
                        buffer_slice.pointer[result.real_buffer_index] = (char8)os_char;
                        result.real_buffer_index += 1;
                    }

                    result.needed_code_unit_count += 1;
                }
                break; case FORMAT_TYPE_STRING8:
                {
                    let string = va_arg(variable_arguments, String8);
                    written = result.real_buffer_index + string.length <= buffer_slice.length;
                    if (written)
                    {
                        memcpy(&buffer_slice.pointer[result.real_buffer_index], string.pointer, string.length);
                        result.real_buffer_index += string.length;
                    }

                    result.needed_code_unit_count += string.length;
                }
                break; case FORMAT_TYPE_STRING16:
                {
                    // TODO:
                    let string = va_arg(variable_arguments, String16);
                    written = result.real_buffer_index + BUSTER_SLICE_SIZE(string) <= buffer_slice.length;
                    if (written)
                    {
                        for (u64 string_i = 0; string_i < string.length; string_i += 1, result.needed_code_unit_count += 1)
                        {
                            buffer_slice.pointer[result.real_buffer_index + string_i] = (char8)string.pointer[string_i];
                        }

                        result.real_buffer_index += string.length;
                    }

                    result.needed_code_unit_count += string.length;
                }
                break; case FORMAT_TYPE_UNSIGNED_INTEGER_8: case FORMAT_TYPE_UNSIGNED_INTEGER_16: case FORMAT_TYPE_UNSIGNED_INTEGER_32: case FORMAT_TYPE_UNSIGNED_INTEGER_64:
                {
                    u8 prefix_buffer[2] = {};
                    prefix = prefix && format_kind != FORMAT_KIND_COUNT;

                    char8 prefix_second_character;
                    switch (format_kind)
                    {
                        break; case FORMAT_KIND_DECIMAL: prefix_second_character = 'd';
                        break; case FORMAT_KIND_BINARY: prefix_second_character = 'b';
                        break; case FORMAT_KIND_OCTAL: prefix_second_character = 'o';
                        break; case FORMAT_KIND_HEXADECIMAL_LOWER: case FORMAT_KIND_HEXADECIMAL_UPPER: prefix_second_character = 'x';
                        break; case FORMAT_KIND_COUNT: BUSTER_UNREACHABLE();
                    }

                    if (prefix)
                    {
                        prefix_buffer[0] = '0';


                        prefix_buffer[1] = prefix_second_character;
                    }

                    if (format_kind == FORMAT_KIND_COUNT)
                    {
                        format_kind = FORMAT_KIND_DECIMAL;
                    }

                    u64 value;
                    u64 value_size;
                    switch (format_type_id)
                    {
                        break; case FORMAT_TYPE_UNSIGNED_INTEGER_8:
                        {
                            value = (u8)(va_arg(variable_arguments, u32) & UINT8_MAX);
                            value_size = sizeof(u8);
                        }
                        break; case FORMAT_TYPE_UNSIGNED_INTEGER_16:
                        {
                            value = (u16)(va_arg(variable_arguments, u32) & UINT16_MAX);
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

                    let prefix_character_count = (u64)prefix << 1;
                    char8 integer_format_buffer[(sizeof(u64) * 8) + BUSTER_FORMAT_INTEGER_MAX_WIDTH + 2];
                    let number_string_buffer = BUSTER_ARRAY_TO_SLICE(String8, integer_format_buffer);

                    String8 format_result;

                    switch (format_kind)
                    {
                        break; case FORMAT_KIND_DECIMAL: format_result = string8_format_i64_decimal(number_string_buffer, value, false);
                        break; case FORMAT_KIND_BINARY: format_result = string8_format_u64_binary(number_string_buffer, value);
                        break; case FORMAT_KIND_OCTAL: format_result = string8_format_u64_octal(number_string_buffer, value);
                        break; case FORMAT_KIND_HEXADECIMAL_LOWER: case FORMAT_KIND_HEXADECIMAL_UPPER: format_result = string8_format_u64_hexadecimal(number_string_buffer, value, format_kind == FORMAT_KIND_HEXADECIMAL_UPPER);
                        break; case FORMAT_KIND_COUNT: BUSTER_UNREACHABLE();
                    }

                    number_string_buffer.length = format_result.length;

                    u64 integer_max_width = 0;

                    u64 digit_group_character_count;

                    switch (format_kind)
                    {
                        break; case FORMAT_KIND_DECIMAL:
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
                        break; case FORMAT_KIND_BINARY:
                        {
                            prefix_second_character = 'b';
                            integer_max_width = value_size * 8;
                            digit_group_character_count = 8;
                        }
                        break; case FORMAT_KIND_OCTAL:
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
                        break; case FORMAT_KIND_HEXADECIMAL_LOWER: case FORMAT_KIND_HEXADECIMAL_UPPER:
                        {
                            prefix_second_character = 'x';
                            integer_max_width = value_size * 2;
                            digit_group_character_count = 2;
                        }
                        break; case FORMAT_KIND_COUNT: BUSTER_UNREACHABLE();
                    }

                    width = width ? (width_natural_extension ? integer_max_width : width) : 0;

                    u64 width_character_count = width ? (width > number_string_buffer.length ? (width - number_string_buffer.length) : 0) : 0;
                    bool separator_characters = digit_group && digit_group_character_count && number_string_buffer.length > digit_group_character_count;
                    u64 separator_character_count = separator_characters ? (number_string_buffer.length / digit_group_character_count) + (number_string_buffer.length % digit_group_character_count != 0) - 1: 0;

                    u64 character_to_write_count = prefix_character_count + width_character_count + number_string_buffer.length + separator_character_count;

                    written = result.real_buffer_index + character_to_write_count <= buffer_slice.length;

                    if (written)
                    {
                        if (prefix)
                        {
                            buffer_slice.pointer[result.real_buffer_index + 0] = prefix_buffer[0];
                            buffer_slice.pointer[result.real_buffer_index + 1] = prefix_buffer[1];

                            result.real_buffer_index += 2;
                        }

                        if (width_character_count)
                        {
                            memset(buffer_slice.pointer + result.real_buffer_index, width_character, width_character_count);
                            result.real_buffer_index += width_character_count;
                        }

                        char8 separator_character = format_kind == FORMAT_KIND_DECIMAL ? '.' : '_';
                        if (separator_character_count)
                        {
                            let remainder = number_string_buffer.length % digit_group_character_count;
                            if (remainder)
                            {
                                memcpy(buffer_slice.pointer + result.real_buffer_index, number_string_buffer.pointer, remainder * sizeof(number_string_buffer.pointer[0]));
                                buffer_slice.pointer[result.real_buffer_index + remainder] = separator_character;
                                result.real_buffer_index += remainder + 1;
                            }

                            u64 source_i;
                            for (source_i = remainder; source_i < number_string_buffer.length - digit_group_character_count; source_i += digit_group_character_count)
                            {
                                memcpy(buffer_slice.pointer + result.real_buffer_index, number_string_buffer.pointer + source_i, digit_group_character_count * sizeof(number_string_buffer.pointer[0]));
                                result.real_buffer_index += digit_group_character_count;

                                buffer_slice.pointer[result.real_buffer_index] = separator_character;
                                result.real_buffer_index += 1;
                            }

                            memcpy(&buffer_slice.pointer[result.real_buffer_index], number_string_buffer.pointer + number_string_buffer.length - digit_group_character_count, digit_group_character_count * sizeof(number_string_buffer.pointer[0]));
                            result.real_buffer_index += digit_group_character_count;
                        }
                        else
                        {
                            memcpy(buffer_slice.pointer + result.real_buffer_index, number_string_buffer.pointer, BUSTER_SLICE_SIZE(number_string_buffer));
                            result.real_buffer_index += number_string_buffer.length;
                        }
                    }

                    result.needed_code_unit_count += character_to_write_count;
                }
                break; case FORMAT_TYPE_SIGNED_INTEGER_8: case FORMAT_TYPE_SIGNED_INTEGER_16: case FORMAT_TYPE_SIGNED_INTEGER_32: case FORMAT_TYPE_SIGNED_INTEGER_64:
                {
                    if (format_kind == FORMAT_KIND_COUNT)
                    {
                        format_kind = FORMAT_KIND_DECIMAL;
                    }

                    s64 value;
                    switch (format_type_id)
                    {
                        break; case FORMAT_TYPE_SIGNED_INTEGER_8: value = (s8)(va_arg(variable_arguments, s32) & INT8_MAX);
                        break; case FORMAT_TYPE_SIGNED_INTEGER_16: value = (s16)(va_arg(variable_arguments, s32) & INT16_MAX);
                        break; case FORMAT_TYPE_SIGNED_INTEGER_32: value = va_arg(variable_arguments, s32);
                        break; case FORMAT_TYPE_SIGNED_INTEGER_64: value = va_arg(variable_arguments, s64);
                        break; default: BUSTER_UNREACHABLE();
                    }

                    char8 integer_format_buffer[sizeof(u64) * 8 + 1]; // 1 for the sign (needed?)
                    let string_buffer = BUSTER_ARRAY_TO_SLICE(String8, integer_format_buffer);
                    String8 format_result;

                    switch (format_kind)
                    {
                        break; case FORMAT_KIND_DECIMAL: format_result = string8_format_i64_decimal(string_buffer, (u64)((value < 0) ? (-value) : value), value < 0);
                        break; case FORMAT_KIND_BINARY: format_result = string8_format_u64_binary(string_buffer, (u64)value);
                        break; case FORMAT_KIND_OCTAL: format_result = string8_format_u64_octal(string_buffer, (u64)value);
                        break; case FORMAT_KIND_HEXADECIMAL_LOWER: case FORMAT_KIND_HEXADECIMAL_UPPER: format_result = string8_format_u64_hexadecimal(string_buffer, (u64)value, format_kind == FORMAT_KIND_HEXADECIMAL_UPPER);
                        break; case FORMAT_KIND_COUNT: BUSTER_UNREACHABLE();
                    }

                    written = result.real_buffer_index + format_result.length <= buffer_slice.length;

                    if (written)
                    {
                        memcpy(buffer_slice.pointer + result.real_buffer_index, format_result.pointer, BUSTER_SLICE_SIZE(format_result));
                        result.real_buffer_index += format_result.length;
                    }

                    result.needed_code_unit_count += format_result.length;
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
                    let os_error = va_arg(variable_arguments, OsError);
                    CharOs error_buffer[BUSTER_OS_ERROR_BUFFER_MAX_LENGTH];
                    let error_string = os_error_write_message(BUSTER_ARRAY_TO_SLICE(StringOs, error_buffer), os_error);

                    written = result.real_buffer_index + error_string.length <= buffer_slice.length;

                    if (written)
                    {
#if defined(_WIN32)
                        for (u64 error_i = 0; error_i < error_string.length; error_i += 1)
                        {
                            buffer_slice.pointer[result.real_buffer_index + error_i] = (char8)error_string.pointer[error_i];
                        }
#else
                        memcpy(buffer_slice.pointer + result.real_buffer_index, error_string.pointer, BUSTER_SLICE_SIZE(error_string));
#endif
                        result.real_buffer_index += error_string.length;
                    }

                    result.needed_code_unit_count += error_string.length;
                }
                break; case FORMAT_TYPE_SPECIFIER_COUNT:
                {
                    if (result.real_buffer_index < buffer_slice.length)
                    {
                        buffer_slice.pointer[result.real_buffer_index] = '{';
                        result.real_buffer_index += 1;
                    }

                    result.needed_code_unit_count += 1;

                    let code_unit_to_write_count = whole_format_string.length + 1;

                    if (result.real_buffer_index + code_unit_to_write_count <= buffer_slice.length)
                    {
                        memcpy(buffer_slice.pointer + result.real_buffer_index, whole_format_string.pointer, BUSTER_SLICE_SIZE(whole_format_string));
                        buffer_slice.pointer[result.real_buffer_index + whole_format_string.length] = '}';
                        result.real_buffer_index += code_unit_to_write_count;
                    }

                    result.needed_code_unit_count += code_unit_to_write_count;
                }
            }

            if (written)
            {
                result.real_format_index += whole_format_string.length;
            }
        }
    }

    if (is_illformed_string)
    {
        result.real_buffer_index = buffer_slice.length;
    }

    return result;
}

BUSTER_IMPL String8 string8_format_arena(Arena* arena, bool null_terminate, String8 format, ...)
{
    String8 result = {};
    va_list variable_arguments;

    va_start(variable_arguments);
    StringFormatResult buffer_result = string8_format_va((String8){}, format, variable_arguments);
    va_end(variable_arguments);

    let code_unit_count = buffer_result.needed_code_unit_count;
    let buffer = string8_from_pointer_length(arena_allocate(arena, char8, code_unit_count + null_terminate), code_unit_count);

    if (buffer.pointer)
    {
        va_start(variable_arguments);
        StringFormatResult final_result = string8_format_va(buffer, format, variable_arguments);
        va_end(variable_arguments);

        if (final_result.needed_code_unit_count == code_unit_count)
        {
            if (null_terminate)
            {
                buffer.pointer[code_unit_count] = 0;
            }

            result = buffer;
        }
    }

    return result;
}

BUSTER_IMPL String8 string8_format(String8 buffer_slice, String8 format, ...)
{
    String8 result = {};

    va_list variable_arguments;
    va_start(variable_arguments);
    let format_result = string8_format_va(buffer_slice, format, variable_arguments);
    va_end(variable_arguments);

    if (format_result.real_buffer_index == format_result.needed_code_unit_count)
    {
        result.pointer = buffer_slice.pointer;
        result.length = format_result.real_buffer_index;
    }

    return result;
}

BUSTER_IMPL void string8_print(String8 format, ...)
{
    char8 buffer[8192];
    let buffer_slice = BUSTER_ARRAY_TO_SLICE(String8, buffer);
    buffer_slice.length -= 1;
    va_list variable_arguments;
    va_start(variable_arguments);
    let format_result = string8_format_va(buffer_slice, format, variable_arguments);
    va_end(variable_arguments);
    if (format_result.real_buffer_index == format_result.needed_code_unit_count)
    {
        let string = string8_from_pointer_length(buffer, format_result.real_buffer_index);
        string.pointer[string.length] = 0;
        os_file_write(os_get_stdout(), BUSTER_SLICE_TO_BYTE_SLICE(string));
    }
}

BUSTER_IMPL String8 string8_duplicate_arena(Arena* arena, String8 str, bool zero_terminate)
{
    let pointer = arena_allocate(arena, char8, str.length + zero_terminate);
    let result = (String8){pointer, str.length};
    memcpy(pointer, str.pointer, BUSTER_SLICE_SIZE(str));
    if (zero_terminate)
    {
        pointer[str.length] = 0;
    }
    return result;
}

BUSTER_IMPL String8 string8_join_arena(Arena* arena, String8Slice strings, bool zero_terminate)
{
    u64 length = 0;

    for (u64 i = 0; i < strings.length; i += 1)
    {
        let string = strings.pointer[i];
        length += string.length;
    }

    let char_size = sizeof(*strings.pointer[0].pointer);

     let pointer = (typeof(strings.pointer[0].pointer))arena_allocate_bytes(arena, (length + zero_terminate) * char_size, alignof(typeof(*strings.pointer[0].pointer)));

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

    return (String8){pointer, length};
}

BUSTER_IMPL u64 string8_copy(String8 destination, String8 source)
{
    u64 result = 0;

    if (source.length <= destination.length)
    {
        result = BUSTER_SLICE_SIZE(source);
        memcpy(destination.pointer, source.pointer, result);
    }

    return result;
}
