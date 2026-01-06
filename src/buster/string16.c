#pragma once
#include <buster/string16.h>

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

