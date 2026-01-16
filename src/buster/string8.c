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

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_IMPL UnitTestResult string8_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {};
    let arena = arguments->arena;
    // string8_format
    {
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
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP,

            UNSIGNED_FORMAT_TEST_CASE_COUNT,
        );

        // u8
        {
            ENUM_T(UnsignedTestCaseId, u8,
                UNSIGNED_TEST_CASE_U8,
                UNSIGNED_TEST_CASE_U16,
                UNSIGNED_TEST_CASE_U32,
                UNSIGNED_TEST_CASE_U64,
                UNSIGNED_TEST_CASE_COUNT,
            );

            String8 format_strings[UNSIGNED_TEST_CASE_COUNT][UNSIGNED_FORMAT_TEST_CASE_COUNT] = {
                [UNSIGNED_TEST_CASE_U8] =
                {
                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("{u8}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("{u8:d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("{u8:x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("{u8:X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("{u8:o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("{u8:b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("{u8:no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("{u8:d,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("{u8:x,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("{u8:X,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("{u8:o,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("{u8:b,no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("{u8:width=[ ,2]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("{u8:d,width=[ ,4]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("{u8:x,width=[ ,8]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("{u8:X,width=[ ,16]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("{u8:o,width=[ ,32]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("{u8:b,width=[ ,64]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("{u8:width=[0,2]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("{u8:d,width=[0,4]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("{u8:x,width=[0,8]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("{u8:X,width=[0,16]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("{u8:o,width=[0,32]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("{u8:b,width=[0,64]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("{u8:width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("{u8:d,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("{u8:x,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("{u8:X,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("{u8:o,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("{u8:b,width=[0,x]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u8:width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u8:d,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u8:x,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u8:X,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("{u8:o,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("{u8:b,width=[ ,x],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("{u8:width=[0,2],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("{u8:d,width=[0,4],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("{u8:x,width=[0,8],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("{u8:X,width=[0,16],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("{u8:o,width=[0,32],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("{u8:b,width=[0,64],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u8:width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u8:d,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u8:x,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u8:X,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("{u8:o,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("{u8:b,width=[0,x],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("{u8:digit_group}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("{u8:digit_group,d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("{u8:digit_group,x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("{u8:digit_group,X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("{u8:digit_group,o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("{u8:digit_group,b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("{u8:digit_group,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("{u8:digit_group,no_prefix,d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("{u8:digit_group,no_prefix,x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("{u8:digit_group,no_prefix,X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("{u8:digit_group,no_prefix,o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("{u8:digit_group,no_prefix,b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u8:digit_group,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u8:digit_group,width=[0,x],d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u8:digit_group,width=[0,x],x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u8:digit_group,width=[0,x],X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("{u8:digit_group,width=[0,x],o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("{u8:digit_group,width=[0,x],b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u8:digit_group,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u8:digit_group,width=[0,x],d,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u8:digit_group,width=[0,x],x,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u8:digit_group,width=[0,x],X,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("{u8:digit_group,width=[0,x],o,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("{u8:digit_group,width=[0,x],b,no_prefix}"),
                },
                [UNSIGNED_TEST_CASE_U16] =
                {
                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("{u16}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("{u16:d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("{u16:x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("{u16:X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("{u16:o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("{u16:b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("{u16:no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("{u16:d,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("{u16:x,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("{u16:X,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("{u16:o,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("{u16:b,no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("{u16:width=[ ,2]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("{u16:d,width=[ ,4]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("{u16:x,width=[ ,8]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("{u16:X,width=[ ,16]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("{u16:o,width=[ ,32]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("{u16:b,width=[ ,64]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("{u16:width=[0,2]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("{u16:d,width=[0,4]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("{u16:x,width=[0,8]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("{u16:X,width=[0,16]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("{u16:o,width=[0,32]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("{u16:b,width=[0,64]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("{u16:width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("{u16:d,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("{u16:x,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("{u16:X,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("{u16:o,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("{u16:b,width=[0,x]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u16:width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u16:d,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u16:x,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u16:X,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("{u16:o,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("{u16:b,width=[ ,x],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("{u16:width=[0,2],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("{u16:d,width=[0,4],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("{u16:x,width=[0,8],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("{u16:X,width=[0,16],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("{u16:o,width=[0,32],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("{u16:b,width=[0,64],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u16:width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u16:d,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u16:x,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u16:X,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("{u16:o,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("{u16:b,width=[0,x],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("{u16:digit_group}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("{u16:digit_group,d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("{u16:digit_group,x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("{u16:digit_group,X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("{u16:digit_group,o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("{u16:digit_group,b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("{u16:digit_group,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("{u16:digit_group,no_prefix,d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("{u16:digit_group,no_prefix,x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("{u16:digit_group,no_prefix,X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("{u16:digit_group,no_prefix,o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("{u16:digit_group,no_prefix,b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u16:digit_group,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u16:digit_group,width=[0,x],d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u16:digit_group,width=[0,x],x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u16:digit_group,width=[0,x],X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("{u16:digit_group,width=[0,x],o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("{u16:digit_group,width=[0,x],b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u16:digit_group,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u16:digit_group,width=[0,x],d,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u16:digit_group,width=[0,x],x,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u16:digit_group,width=[0,x],X,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("{u16:digit_group,width=[0,x],o,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("{u16:digit_group,width=[0,x],b,no_prefix}"),
                },
                [UNSIGNED_TEST_CASE_U32] =
                {
                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("{u32}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("{u32:d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("{u32:x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("{u32:X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("{u32:o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("{u32:b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("{u32:no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("{u32:d,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("{u32:x,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("{u32:X,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("{u32:o,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("{u32:b,no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("{u32:width=[ ,2]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("{u32:d,width=[ ,4]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("{u32:x,width=[ ,8]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("{u32:X,width=[ ,16]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("{u32:o,width=[ ,32]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("{u32:b,width=[ ,64]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("{u32:width=[0,2]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("{u32:d,width=[0,4]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("{u32:x,width=[0,8]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("{u32:X,width=[0,16]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("{u32:o,width=[0,32]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("{u32:b,width=[0,64]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("{u32:width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("{u32:d,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("{u32:x,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("{u32:X,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("{u32:o,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("{u32:b,width=[0,x]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u32:width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u32:d,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u32:x,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u32:X,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("{u32:o,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("{u32:b,width=[ ,x],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("{u32:width=[0,2],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("{u32:d,width=[0,4],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("{u32:x,width=[0,8],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("{u32:X,width=[0,16],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("{u32:o,width=[0,32],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("{u32:b,width=[0,64],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u32:width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u32:d,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u32:x,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u32:X,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("{u32:o,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("{u32:b,width=[0,x],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("{u32:digit_group}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("{u32:digit_group,d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("{u32:digit_group,x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("{u32:digit_group,X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("{u32:digit_group,o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("{u32:digit_group,b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("{u32:digit_group,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("{u32:digit_group,no_prefix,d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("{u32:digit_group,no_prefix,x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("{u32:digit_group,no_prefix,X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("{u32:digit_group,no_prefix,o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("{u32:digit_group,no_prefix,b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u32:digit_group,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u32:digit_group,width=[0,x],d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u32:digit_group,width=[0,x],x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u32:digit_group,width=[0,x],X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("{u32:digit_group,width=[0,x],o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("{u32:digit_group,width=[0,x],b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u32:digit_group,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u32:digit_group,width=[0,x],d,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u32:digit_group,width=[0,x],x,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u32:digit_group,width=[0,x],X,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("{u32:digit_group,width=[0,x],o,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("{u32:digit_group,width=[0,x],b,no_prefix}"),
                },
                [UNSIGNED_TEST_CASE_U64] =
                {
                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("{u64}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("{u64:d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("{u64:x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("{u64:X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("{u64:o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("{u64:b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("{u64:no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("{u64:d,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("{u64:x,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("{u64:X,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("{u64:o,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("{u64:b,no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("{u64:width=[ ,2]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("{u64:d,width=[ ,4]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("{u64:x,width=[ ,8]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("{u64:X,width=[ ,16]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("{u64:o,width=[ ,32]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("{u64:b,width=[ ,64]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("{u64:width=[0,2]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("{u64:d,width=[0,4]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("{u64:x,width=[0,8]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("{u64:X,width=[0,16]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("{u64:o,width=[0,32]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("{u64:b,width=[0,64]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("{u64:width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("{u64:d,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("{u64:x,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("{u64:X,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("{u64:o,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("{u64:b,width=[0,x]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u64:width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u64:d,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u64:x,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u64:X,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("{u64:o,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("{u64:b,width=[ ,x],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("{u64:width=[0,2],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("{u64:d,width=[0,4],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("{u64:x,width=[0,8],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("{u64:X,width=[0,16],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("{u64:o,width=[0,32],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("{u64:b,width=[0,64],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u64:width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u64:d,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u64:x,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u64:X,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("{u64:o,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("{u64:b,width=[0,x],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("{u64:digit_group}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("{u64:digit_group,d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("{u64:digit_group,x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("{u64:digit_group,X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("{u64:digit_group,o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("{u64:digit_group,b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("{u64:digit_group,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("{u64:digit_group,no_prefix,d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("{u64:digit_group,no_prefix,x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("{u64:digit_group,no_prefix,X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("{u64:digit_group,no_prefix,o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("{u64:digit_group,no_prefix,b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u64:digit_group,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u64:digit_group,width=[0,x],d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u64:digit_group,width=[0,x],x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u64:digit_group,width=[0,x],X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("{u64:digit_group,width=[0,x],o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("{u64:digit_group,width=[0,x],b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u64:digit_group,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u64:digit_group,width=[0,x],d,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u64:digit_group,width=[0,x],x,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u64:digit_group,width=[0,x],X,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("{u64:digit_group,width=[0,x],o,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("{u64:digit_group,width=[0,x],b,no_prefix}"),
                },
            };

            // 0, 1, 2, 4, 8, 16, UINT_MAX / 2, UINT_MAX

            STRUCT(UnsignedTestCase)
            {
                String8 expected_results[UNSIGNED_FORMAT_TEST_CASE_COUNT];
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
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX,
                UNSIGNED_TEST_CASE_NUMBER_COUNT,
            );

            UnsignedTestCase cases[UNSIGNED_TEST_CASE_COUNT][UNSIGNED_TEST_CASE_NUMBER_COUNT] =
            {
                [UNSIGNED_TEST_CASE_U8] =
                {
                    [UNSIGNED_TEST_CASE_NUMBER_ZERO] =
                    {
                        .value = 0,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                               0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x00"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x00"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("  0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("       0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x00"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x00"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b00000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("00000000"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_ONE] =
                    {
                        .value = 1,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                               1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x01"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x01"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("  1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("       1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x01"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x01"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b00000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("00000001"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_TWO] =
                    {
                        .value = 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                              10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x02"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x02"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("  2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("      10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x02"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x02"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b00000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("00000010"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_FOUR] =
                    {
                        .value = 4,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                             100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x04"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x04"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("  4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("     100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x04"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x04"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b00000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("00000100"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_EIGHT] =
                    {
                        .value = 8,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                              10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                            1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x08"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x08"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8(" 10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("    1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x08"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x08"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b00001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("00001000"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_SIXTEEN] =
                    {
                        .value = 16,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("  16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                              20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                           10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8(" 16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8(" 16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8(" 20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("   10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b00010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("00010000"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2] =
                    {
                        .value = UINT8_MAX / 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x7f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x7F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b1111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("7f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("7F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("1111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      7f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              7F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                         1111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x0000007f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x000000000000007F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000001111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x7f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x7F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b01111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("7f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("7F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8(" 1111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("0000007f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("000000000000007F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000001111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("7f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("7F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("01111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x7f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x7F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b1111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("7f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("7F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("1111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x7f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x7F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b01111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("01111111"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5] =
                    {
                        .value = UINT8_MAX - 5,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0xfa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0xFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                        11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x000000fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x00000000000000FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000011111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0xfa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0xFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("000000fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("00000000000000FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000011111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0xfa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0xFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xfa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("11111010"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4] =
                    {
                        .value = UINT8_MAX - 4,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0xfb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0xFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                        11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x000000fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x00000000000000FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000011111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0xfb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0xFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("000000fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("00000000000000FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000011111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0xfb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0xFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xfb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("11111011"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3] =
                    {
                        .value = UINT8_MAX - 3,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0xfc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0xFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                        11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x000000fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x00000000000000FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000011111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0xfc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0xFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("000000fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("00000000000000FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000011111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0xfc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0xFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xfc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("11111100"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2] =
                    {
                        .value = UINT8_MAX - 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0xfd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0xFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                        11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x000000fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x00000000000000FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000011111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0xfd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0xFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("000000fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("00000000000000FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000011111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0xfd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0xFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xfd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("11111101"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1] =
                    {
                        .value = UINT8_MAX - 1,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0xfe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0xFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      fe") ,
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                        11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x000000fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x00000000000000FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000011111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0xfe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0xFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("000000fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("00000000000000FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000011111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0xfe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0xFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xfe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("11111110"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX] =
                    {
                        .value = UINT8_MAX,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0xff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0xFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                        11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x000000ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x00000000000000FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000011111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0xff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0xFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("000000ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("00000000000000FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000011111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0xff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0xFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("11111111"),
                        },
                    },
                },
                
// ==================== U16 ====================

                [UNSIGNED_TEST_CASE_U16] =
                {
                    [UNSIGNED_TEST_CASE_NUMBER_ZERO] =
                    {
                        .value = 0,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                               0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("    0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("    0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("     0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("               0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_ONE] =
                    {
                        .value = 1,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                               1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("    1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("    1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("     1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("               1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000001")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_TWO] =
                    {
                        .value = 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                              10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("    2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("    2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("     2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("              10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000010")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_FOUR] =
                    {
                        .value = 4,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                             100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("    4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("    4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("     4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("             100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000100")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_EIGHT] =
                    {
                        .value = 8,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                              10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                            1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("    8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("    8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("    10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("            1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000001000")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_SIXTEEN] =
                    {
                        .value = 16,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("  16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("      10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("              10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                              20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                           10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("   16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("   16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("  10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("  10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("    20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("           10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000010000")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2] =
                    {
                        .value = UINT16_MAX / 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x7fff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x7FFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o77777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("7fff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("7FFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("77777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    7fff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            7FFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                           77777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                 111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00007fff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000007FFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000077777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x7fff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x7FFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o077777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("7fff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("7FFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8(" 77777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8(" 111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00007fff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000007FFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000077777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("7fff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("7FFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("077777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("32.767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d32.767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x7f_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x7F_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o77_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("32.767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("32.767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("7f_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("7F_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("77_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("32.767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d32.767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x7f_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x7F_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o077_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b01111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("32.767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("32.767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7f_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7F_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("077_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("01111111_11111111")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5] =
                    {
                        .value = UINT16_MAX - 5,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o177772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("177772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    fffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            FFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                          177772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                1111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x0000fffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000000000FFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000177772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000001111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o177772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("177772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("0000fffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000000000FFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000177772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000001111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("177772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("65.530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d65.530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o177_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("65.530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("65.530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("177_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("65.530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d65.530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o177_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("177_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111010")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4] =
                    {
                        .value = UINT16_MAX - 4,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o177773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("177773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    fffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            FFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                          177773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                1111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x0000fffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000000000FFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000177773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000001111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o177773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("177773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("0000fffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000000000FFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000177773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000001111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("177773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("65.531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d65.531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o177_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("65.531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("65.531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("177_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("65.531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d65.531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o177_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("177_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111011")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3] =
                    {
                        .value = UINT16_MAX - 3,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o177774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("177774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    fffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            FFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                          177774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                1111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x0000fffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000000000FFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000177774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000001111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o177774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("177774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("0000fffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000000000FFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000177774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000001111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("177774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("65.532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d65.532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o177_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("65.532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("65.532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("177_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("65.532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d65.532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o177_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("177_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111100")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2] =
                    {
                        .value = UINT16_MAX - 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o177775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("177775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    fffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            FFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                          177775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                1111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x0000fffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000000000FFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000177775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000001111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o177775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("177775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("0000fffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000000000FFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000177775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000001111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("177775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("65.533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d65.533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o177_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("65.533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("65.533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("177_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("65.533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d65.533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o177_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("177_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111101")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1] =
                    {
                        .value = UINT16_MAX - 1,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o177776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("177776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    fffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            FFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                          177776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                1111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x0000fffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000000000FFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000177776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000001111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o177776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("177776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("0000fffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000000000FFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000177776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000001111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("177776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("65.534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d65.534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o177_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("65.534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("65.534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("177_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("65.534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d65.534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o177_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("177_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111110")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX] =
                    {
                        .value = UINT16_MAX,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o177777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("ffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("177777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    ffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            FFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                          177777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                1111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x0000ffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000000000FFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000177777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000001111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o177777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("ffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("177777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("0000ffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000000000FFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000177777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000001111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("ffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("177777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("65.535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d65.535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o177_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("65.535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("65.535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("177_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("65.535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d65.535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o177_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("177_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111")
                        },
                    },
                },

// ==================== U32 ====================

                [UNSIGNED_TEST_CASE_U32] =
                {
                    [UNSIGNED_TEST_CASE_NUMBER_ZERO] =
                    {
                        .value = 0,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                               0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("0000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d0000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o00000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b00000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("         0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("         0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("          0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                               0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("00000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d0000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o00000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b00000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("00000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("00000000000000000000000000000000")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_ONE] =
                    {
                        .value = 1,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                               1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("0000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d0000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o00000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b00000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("         1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("         1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("          1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                               1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("00000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d0000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o00000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b00000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("00000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("00000000000000000000000000000001")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_TWO] =
                    {
                        .value = 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                              10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("0000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d0000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o00000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b00000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("         2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("         2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("          2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                              10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("00000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d0000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o00000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b00000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("00000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("00000000000000000000000000000010")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_FOUR] =
                    {
                        .value = 4,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                             100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("0000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d0000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o00000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b00000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("         4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("         4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("          4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                             100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("00000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d0000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o00000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b00000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("00000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("00000000000000000000000000000100")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_EIGHT] =
                    {
                        .value = 8,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                              10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                            1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("0000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d0000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o00000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b00000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("         8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("         8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("         10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                            1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("00000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d0000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o00000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b00000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("00000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("00000000000000000000000000001000")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_SIXTEEN] =
                    {
                        .value = 16,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("  16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("      10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("              10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                              20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                           10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("0000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d0000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o00000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b00000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("        16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("        16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("      10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("      10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("         20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                           10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("00000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d0000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o00000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b00000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("00000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("00000000000000000000000000010000")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2] =
                    {
                        .value = UINT32_MAX / 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x7fffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x7FFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o17777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("7fffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("7FFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("17777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("7fffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        7FFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     17777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                 1111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x7fffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000007FFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000017777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000001111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x7fffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x7FFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o17777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b01111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("7fffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("7FFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("17777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8(" 1111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("7fffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000007FFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000017777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000001111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("7fffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("7FFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("17777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("01111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("2.147.483.647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d2.147.483.647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x7f_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x7F_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o17_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("2.147.483.647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("2.147.483.647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("7f_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("7F_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("17_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("2.147.483.647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d2.147.483.647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x7f_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x7F_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o17_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b01111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("2.147.483.647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("2.147.483.647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7f_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7F_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("17_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("01111111_11111111_11111111_11111111")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5] =
                    {
                        .value = UINT32_MAX - 5,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o37777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b11111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("37777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("11111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        FFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     37777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                11111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x00000000FFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000037777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000011111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o37777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b11111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("37777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("11111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("00000000FFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000037777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000011111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("37777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("11111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4.294.967.290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4.294.967.290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o37_777_777_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("37_777_777_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("4.294.967.290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d4.294.967.290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o37_777_777_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("37_777_777_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111010")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4] =
                    {
                        .value = UINT32_MAX - 4,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o37777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b11111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("37777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("11111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        FFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     37777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                11111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x00000000FFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000037777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000011111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o37777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b11111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("37777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("11111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("00000000FFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000037777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000011111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("37777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("11111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4.294.967.291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4.294.967.291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o37_777_777_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("37_777_777_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("4.294.967.291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d4.294.967.291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o37_777_777_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("37_777_777_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111011")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3] =
                    {
                        .value = UINT32_MAX - 3,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o37777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b11111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("37777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("11111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        FFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     37777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                11111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x00000000FFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000037777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000011111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o37777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b11111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("37777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("11111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("00000000FFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000037777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000011111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("37777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("11111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4.294.967.292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4.294.967.292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o37_777_777_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("37_777_777_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("4.294.967.292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d4.294.967.292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o37_777_777_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("37_777_777_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111100")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2] =
                    {
                        .value = UINT32_MAX - 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o37777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b11111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("37777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("11111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        FFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     37777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                11111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x00000000FFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000037777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000011111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o37777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b11111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("37777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("11111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("00000000FFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000037777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000011111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("37777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("11111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4.294.967.293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4.294.967.293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o37_777_777_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("37_777_777_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("4.294.967.293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d4.294.967.293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o37_777_777_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("37_777_777_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111101")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1] =
                    {
                        .value = UINT32_MAX - 1,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o37777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b11111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("37777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("11111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        FFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     37777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                11111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x00000000FFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000037777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000011111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o37777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b11111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("37777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("11111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("00000000FFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000037777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000011111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("37777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("11111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4.294.967.294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4.294.967.294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o37_777_777_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("37_777_777_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("4.294.967.294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d4.294.967.294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o37_777_777_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("37_777_777_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111110")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX] =
                    {
                        .value = UINT32_MAX,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o37777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b11111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("ffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("37777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("11111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("ffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        FFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     37777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                11111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x00000000FFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000037777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000011111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o37777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b11111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("ffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("37777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("11111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("ffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("00000000FFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000037777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000011111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("ffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("37777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("11111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4.294.967.295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4.294.967.295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o37_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("37_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("4.294.967.295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d4.294.967.295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o37_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("37_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111")
                        },
                    },
                },

// ==================== U64 ====================

                [UNSIGNED_TEST_CASE_U64] =
                {
                    [UNSIGNED_TEST_CASE_NUMBER_ZERO] =
                    {
                        .value = 0,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                               0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("                     0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                                                               0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000000000000000000000000000000000000000000000000000")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_ONE] =
                    {
                        .value = 1,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                               1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("                     1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                                                               1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000000000000000000000000000000000000000000000000001")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_TWO] =
                    {
                        .value = 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                              10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("                     2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                                                              10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000000000000000000000000000000000000000000000000010")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_FOUR] =
                    {
                        .value = 4,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                             100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("                     4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                                                             100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000000000000000000000000000000000000000000000000100")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_EIGHT] =
                    {
                        .value = 8,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                              10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                            1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("                    10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                                                            1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000000000000000000000000000000000000000000000001000")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_SIXTEEN] =
                    {
                        .value = 16,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("  16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("      10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("              10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                              20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                           10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000000000000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000000000000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("                  16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("                  16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("              10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("              10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("                    20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                                                           10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000000000000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000000000000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000000000000000000000000000000000000000000000010000")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2] =
                    {
                        .value = UINT64_MAX / 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x7fffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x7FFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("7fffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("7FFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("7fffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("7FFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("           777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8(" 111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x7fffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x7FFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("09223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d09223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x7fffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x7FFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8(" 9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8(" 9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("7fffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("7FFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8(" 777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8(" 111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("7fffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("7FFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("09223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("09223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("7fffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("7FFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("9.223.372.036.854.775.807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d9.223.372.036.854.775.807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x7f_ff_ff_ff_ff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x7F_FF_FF_FF_FF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o777_777_777_777_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("9.223.372.036.854.775.807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("9.223.372.036.854.775.807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("7f_ff_ff_ff_ff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("7F_FF_FF_FF_FF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("777_777_777_777_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("09.223.372.036.854.775.807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d09.223.372.036.854.775.807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x7f_ff_ff_ff_ff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x7F_FF_FF_FF_FF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0777_777_777_777_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b01111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("09.223.372.036.854.775.807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("09.223.372.036.854.775.807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7f_ff_ff_ff_ff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7F_FF_FF_FF_FF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0777_777_777_777_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("01111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5] =
                    {
                        .value = UINT64_MAX - 5,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffffffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFFFFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1777777777777777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111111111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffffffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFFFFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1777777777777777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111111111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffffffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("FFFFFFFFFFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("          1777777777777777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("1111111111111111111111111111111111111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffffffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0xFFFFFFFFFFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000001777777777777777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b1111111111111111111111111111111111111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffffffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFFFFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o1777777777777777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111111111111111111111111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffffffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("1777777777777777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffffffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000001777777777777777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffffffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFFFFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("1777777777777777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111111111111111111111111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("18.446.744.073.709.551.610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d18.446.744.073.709.551.610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff_ff_ff_ff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF_FF_FF_FF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1_777_777_777_777_777_777_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff_ff_ff_ff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF_FF_FF_FF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1_777_777_777_777_777_777_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("18.446.744.073.709.551.610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d18.446.744.073.709.551.610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff_ff_ff_ff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF_FF_FF_FF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o1_777_777_777_777_777_777_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("1_777_777_777_777_777_777_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111010")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4] =
                    {
                        .value = UINT64_MAX - 4,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffffffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFFFFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1777777777777777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111111111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffffffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFFFFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1777777777777777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111111111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffffffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("FFFFFFFFFFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("          1777777777777777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("1111111111111111111111111111111111111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffffffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0xFFFFFFFFFFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000001777777777777777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b1111111111111111111111111111111111111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffffffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFFFFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o1777777777777777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111111111111111111111111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffffffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("1777777777777777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffffffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000001777777777777777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffffffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFFFFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("1777777777777777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111111111111111111111111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("18.446.744.073.709.551.611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d18.446.744.073.709.551.611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff_ff_ff_ff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF_FF_FF_FF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1_777_777_777_777_777_777_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff_ff_ff_ff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF_FF_FF_FF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1_777_777_777_777_777_777_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("18.446.744.073.709.551.611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d18.446.744.073.709.551.611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff_ff_ff_ff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF_FF_FF_FF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o1_777_777_777_777_777_777_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("1_777_777_777_777_777_777_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111011")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3] =
                    {
                        .value = UINT64_MAX - 3,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffffffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFFFFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1777777777777777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111111111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffffffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFFFFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1777777777777777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111111111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffffffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("FFFFFFFFFFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("          1777777777777777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("1111111111111111111111111111111111111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffffffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0xFFFFFFFFFFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000001777777777777777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b1111111111111111111111111111111111111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffffffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFFFFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o1777777777777777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111111111111111111111111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffffffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("1777777777777777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffffffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000001777777777777777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffffffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFFFFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("1777777777777777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111111111111111111111111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("18.446.744.073.709.551.612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d18.446.744.073.709.551.612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff_ff_ff_ff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF_FF_FF_FF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1_777_777_777_777_777_777_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff_ff_ff_ff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF_FF_FF_FF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1_777_777_777_777_777_777_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("18.446.744.073.709.551.612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d18.446.744.073.709.551.612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff_ff_ff_ff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF_FF_FF_FF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o1_777_777_777_777_777_777_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("1_777_777_777_777_777_777_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111100")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2] =
                    {
                        .value = UINT64_MAX - 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffffffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFFFFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1777777777777777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111111111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffffffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFFFFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1777777777777777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111111111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffffffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("FFFFFFFFFFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("          1777777777777777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("1111111111111111111111111111111111111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffffffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0xFFFFFFFFFFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000001777777777777777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b1111111111111111111111111111111111111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffffffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFFFFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o1777777777777777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111111111111111111111111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffffffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("1777777777777777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffffffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000001777777777777777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffffffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFFFFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("1777777777777777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111111111111111111111111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("18.446.744.073.709.551.613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d18.446.744.073.709.551.613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff_ff_ff_ff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF_FF_FF_FF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1_777_777_777_777_777_777_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff_ff_ff_ff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF_FF_FF_FF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1_777_777_777_777_777_777_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("18.446.744.073.709.551.613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d18.446.744.073.709.551.613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff_ff_ff_ff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF_FF_FF_FF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o1_777_777_777_777_777_777_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("1_777_777_777_777_777_777_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111101")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1] =
                    {
                        .value = UINT64_MAX - 1,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffffffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFFFFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1777777777777777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111111111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffffffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFFFFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1777777777777777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111111111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffffffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("FFFFFFFFFFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("          1777777777777777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("1111111111111111111111111111111111111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffffffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0xFFFFFFFFFFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000001777777777777777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b1111111111111111111111111111111111111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffffffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFFFFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o1777777777777777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111111111111111111111111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffffffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("1777777777777777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffffffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000001777777777777777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffffffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFFFFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("1777777777777777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111111111111111111111111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("18.446.744.073.709.551.614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d18.446.744.073.709.551.614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff_ff_ff_ff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF_FF_FF_FF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1_777_777_777_777_777_777_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff_ff_ff_ff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF_FF_FF_FF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1_777_777_777_777_777_777_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("18.446.744.073.709.551.614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d18.446.744.073.709.551.614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff_ff_ff_ff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF_FF_FF_FF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o1_777_777_777_777_777_777_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("1_777_777_777_777_777_777_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111110")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX] =
                    {
                        .value = UINT64_MAX,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xffffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("ffffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("ffffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("FFFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("          1777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("1111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xffffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0xFFFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000001777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b1111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xffffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o1777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("ffffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("1777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("ffffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000001777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("ffffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("1777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("18.446.744.073.709.551.615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d18.446.744.073.709.551.615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff_ff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF_FF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1_777_777_777_777_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff_ff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF_FF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1_777_777_777_777_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("18.446.744.073.709.551.615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d18.446.744.073.709.551.615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff_ff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF_FF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o1_777_777_777_777_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("1_777_777_777_777_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111")
                        },
                    },
                },
            };

            for (UnsignedTestCaseId type_i = 0; type_i < UNSIGNED_TEST_CASE_COUNT; type_i += 1)
            {
                for (UnsignedTestCaseNumber case_value_i = 0; case_value_i < UNSIGNED_TEST_CASE_NUMBER_COUNT; case_value_i += 1)
                {
                    let uint_case = &cases[type_i][case_value_i];
                    let value = uint_case->value;

                    for (UnsignedFormatTestCase case_i = 0; case_i < UNSIGNED_FORMAT_TEST_CASE_COUNT; case_i += 1)
                    {
                        let format_string = format_strings[type_i][case_i];
                        let expected_string = uint_case->expected_results[case_i];
                        let test_type = (UnsignedTestCaseId)type_i;

                        String8 result_string;
                        switch (test_type)
                        {
                            break; case UNSIGNED_TEST_CASE_U8: result_string =  string8_format_arena(arena, 0, format_string, (u8)value);
                            break; case UNSIGNED_TEST_CASE_U16: result_string = string8_format_arena(arena, 0, format_string, (u16)value);
                            break; case UNSIGNED_TEST_CASE_U32: result_string = string8_format_arena(arena, 0, format_string, (u32)value);
                            break; case UNSIGNED_TEST_CASE_U64: result_string = string8_format_arena(arena, 0, format_string, (u64)value);
                            break; default: BUSTER_UNREACHABLE();
                        }

                        BUSTER_STRING_TEST(arguments, result_string, expected_string);
                    }
                }
            }
        }
    }

    return result;
}
#endif
