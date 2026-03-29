#pragma once
#include <buster/string.h>
#include <buster/os.h>
#include <buster/arena.h>

[[noreturn]] [[gnu::cold]] BUSTER_F_IMPL void buster_failed_assertion(u32 line, String8 function_name, String8 file_path)
{
    string8_print(S8("Assert failed at "));
    string8_print(function_name);
    string8_print(S8(" in "));
    string8_print(file_path);
    string8_print(S8(":"));

    let scratch = scratch_begin(0, 0);
    string8_format(scratch.arena, S8("{u32}"), line);

    // TODO
    // char8 buffer[128];
    // let buffer_slice = (String8){ buffer, BUSTER_ARRAY_LENGTH(buffer) };
    // let line_format = string8_format(buffer_slice, S8("{u32}"), line);
    // string8_print(line_format);
    // string8_print(S8("\n"));
        
    os_fail();
}

