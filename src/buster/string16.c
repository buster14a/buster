#pragma once
#include <buster/string16.h>
#include <buster/arena.h>
#include <buster/assertion.h>

BUSTER_IMPL u64 string16_length(const char16* s)
{
    u64 result = 0;

    if (s)
    {
        let it = s;

        while (*it)
        {
            it += 1;
        }

        result = (u64)(it - s);
    }

    return result;
}

BUSTER_IMPL String16 string16_duplicate_arena(Arena* arena, String16 str, bool zero_terminate)
{
    let length = str.length;
    let pointer = arena_allocate(arena, char16, length + zero_terminate);
    let result = string16_from_pointer_length(pointer, length);
    memcpy(pointer, str.pointer, BUSTER_SLICE_SIZE(str));
    if (zero_terminate)
    {
        pointer[str.length] = 0;
    }
    return result;
}

BUSTER_IMPL u64 raw_string16_first_code_point(const char16* s, char16 ch)
{
    let result = BUSTER_STRING_NO_MATCH;

    for (u64 i = 0; s[i]; i += 1)
    {
        if (ch == s[i])
        {
            result = i;
            break;
        }
    }

    return result;
}

BUSTER_IMPL IntegerParsingU64 string16_parse_u64_hexadecimal(const char16* restrict p)
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
        value = parsing_accumulate_hexadecimal(value, (u8)ch);
    }

    return (IntegerParsingU64){ .value = value, .length = i };
}

BUSTER_IMPL IntegerParsingU64 string16_parse_u64_decimal(const char16* restrict p)
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
        value = parsing_accumulate_decimal(value, (u8)ch);
    }

    return (IntegerParsingU64){ .value = value, .length = i };
}

BUSTER_IMPL IntegerParsingU64 string16_parse_u64_octal(const char16* restrict p)
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
        value = parsing_accumulate_octal(value, ch);
    }

    return (IntegerParsingU64) { .value = value, .length = i };
}

BUSTER_IMPL IntegerParsingU64 string16_parse_u64_binary(const char16* restrict p)
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
        value = parsing_accumulate_octal(value, ch);
    }

    return (IntegerParsingU64) { .value = value, .length = i };
}

BUSTER_IMPL String16 string16_join_arena(Arena* arena, String16Slice strings, bool zero_terminate)
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

    return string16_from_pointer_length(pointer, length);
}

BUSTER_IMPL String16 string16_format_arena(Arena* arena, bool null_terminate, String16 format, ...)
{
    String16 result = {};
    // TODO: undo this shit
    char8 format_buffer[8192];
    char8 out_buffer[16 * 1024];
    let format8 = BUSTER_ARRAY_TO_SLICE(String8, format_buffer);
    let out8 = BUSTER_ARRAY_TO_SLICE(String8, out_buffer);

    if (format.length <= format8.length)
    {
        for (u64 i = 0; i < format.length; i += 1)
        {
            format8.pointer[i] = (u8)format.pointer[i];
        }

        format8.length = format.length;

        va_list variable_arguments;

        va_start(variable_arguments);
        StringFormatResult format_result = string8_format_va(out8, format8, variable_arguments);
        va_end(variable_arguments);

        if (format_result.needed_code_unit_count == format_result.real_buffer_index)
        {
            out8.length = format_result.needed_code_unit_count;
            result = string8_to_string16_arena(arena, out8, null_terminate);
        }
    }

    return result;
}

BUSTER_IMPL u64 string16_copy(String16 destination, String16 source)
{
    u64 result = 0;

    if (source.length <= destination.length)
    {
        result = BUSTER_SLICE_SIZE(source);
        memcpy(destination.pointer, source.pointer, result);
    }

    return result;
}
