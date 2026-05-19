#include <buster/string.h>
#include <buster/os.h>
#include <buster/arena.h>

BUSTER_NORETURN BUSTER_COLD void buster_failed_assertion(u32 line, String8 function_name, String8 file_path)
{
    string_print(S8("Assert failed at "));
    string_print(function_name);
    string_print(S8(" in "));
    string_print(file_path);
    string_print(S8(":"));

    TemporalArena scratch = scratch_begin(0, 0);
    string_format(scratch.arena, S8("{u32}"), line);

    // TODO
    // char8 buffer[128];
    // let buffer_slice = (String8){ buffer, BUSTER_ARRAY_LENGTH(buffer) };
    // let line_format = string8_format(buffer_slice, S8("{u32}"), line);
    // string8_print(line_format);
    // string8_print(S8("\n"));
        
    os_fail();
}

