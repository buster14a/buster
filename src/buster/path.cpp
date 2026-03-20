#pragma once
#include <buster/path.h>
#include <buster/os.h>
#include <buster/system_headers.h>
#include <buster/string.h>

BUSTER_F_IMPL StringOs path_absolute_arena(Arena* arena, StringOs relative_file_path)
{
    let bytes = arena_allocate(arena, CharOs, BUSTER_MAX_PATH_LENGTH);
    let result = os_path_absolute(string_os_from_pointer_length(bytes, BUSTER_MAX_PATH_LENGTH), relative_file_path);
    arena->position = (u64)((u8*)(result.pointer + result.length) - (u8*)arena);
    return result;
}

