#include <buster/string.h>
#include <buster/os.h>
#include <buster/arena.h>

BUSTER_NORETURN BUSTER_COLD void buster_failed_assertion(u32 line, String8 function_name, String8 file_path)
{
    os_fail_ex(line, function_name, file_path);
}

