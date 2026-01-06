#pragma once
#include <buster/path.h>
#include <buster/os.h>

BUSTER_IMPL StringOs path_absolute_arena(Arena* arena, StringOs relative_file_path)
{
    CharOs buffer[max_path_length];
    let stack_slice = os_path_absolute(os_string_from_pointer_length(buffer, BUSTER_ARRAY_LENGTH(buffer)), relative_file_path);
    let result = arena_duplicate_os_string(arena, stack_slice, true);
    return result;
}

