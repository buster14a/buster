#pragma once
#include <buster/path.h>
#include <buster/os.h>
#include <buster/system_headers.h>

BUSTER_IMPL StringOs path_absolute_arena(Arena* arena, StringOs relative_file_path)
{
    CharOs buffer[BUSTER_MAX_PATH_LENGTH];
    let stack_slice = os_path_absolute(string_os_from_pointer_length(buffer, BUSTER_ARRAY_LENGTH(buffer)), relative_file_path);
    let result = string_os_duplicate_arena(arena, stack_slice, true);
    return result;
}

