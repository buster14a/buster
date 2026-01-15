#pragma once
#include <buster/base.h>
#include <buster/system_headers.h>
#include <buster/string8.h>

#include <buster/entry_point.h>

#if BUSTER_UNITY_BUILD
#include <buster/entry_point.c>
#include <buster/target.c>
#if defined (__x86_64__)
#include <buster/x86_64.c>
#endif
#if defined (__aarch64__)
#include <buster/aarch64.c>
#endif
#include <buster/string.c>
#include <buster/string8.c>
#include <buster/string_os.c>
#if _WIN32
#include <buster/string16.c>
#endif
#include <buster/memory.c>
#include <buster/assertion.c>
#include <buster/os.c>
#include <buster/arena.c>
#include <buster/integer.c>
#endif

STRUCT(AsmProgramState)
{
    ProgramState general_program_state;
    String8 cwd;
};

BUSTER_GLOBAL_LOCAL AsmProgramState asm_program_state = {
};

BUSTER_IMPL ProgramState* program_state = &asm_program_state.general_program_state;

#if BUSTER_FUZZING
BUSTER_DECL s32 buster_fuzz(const u8* pointer, size_t size)
{
    BUSTER_UNUSED(pointer);
    BUSTER_UNUSED(size);
    return 0;
}
#else
BUSTER_IMPL ProcessResult process_arguments()
{
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_IMPL ProcessResult thread_entry_point()
{
    string8_print(S8("Hello world from assembler\n"));
    return PROCESS_RESULT_SUCCESS;
}
#endif
