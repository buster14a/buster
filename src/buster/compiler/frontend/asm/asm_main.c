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

BUSTER_IMPL ProcessResult process_arguments()
{
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_IMPL ProcessResult thread_entry_point()
{
    print(S8("Hello world from assembler\n"));
    return PROCESS_RESULT_SUCCESS;
}
