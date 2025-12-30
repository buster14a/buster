#pragma once
#include <buster/string.h>
#include <buster/arena.h>
#include <buster/string8.h>
#include <buster/string16.h>

BUSTER_IMPL bool string_generic_equal(void* p1, void* p2, u64 l1, u64 l2, u64 element_size)
{
    bool is_equal = l1 == l2;
    if (is_equal & (l1 != 0) & (p1 != p2))
    {
        is_equal = memory_compare(p1, p2, l1 * element_size);
    }

    return is_equal;
}

BUSTER_IMPL String8 string16_to_string8_arena(Arena* arena, String16 s, bool null_terminate)
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

BUSTER_IMPL String16 string8_to_string16_arena(Arena* arena, String8 s, bool null_terminate)
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
