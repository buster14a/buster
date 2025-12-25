#pragma once
#include <buster/lib.h>
#include <buster/system_headers.h>

STRUCT(AsmProgramState)
{
    ProgramState general_program_state;
    String8 cwd;
};

BUSTER_LOCAL AsmProgramState asm_program_state = {

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
    print(S8("Hello world from assembler\n"));
    return PROCESS_RESULT_SUCCESS;
}
#endif
