#pragma once
#include <lib.h>
#include <system_headers.h>

STRUCT(AsmProgramState)
{
    ProgramState general_program_state;
    String cwd;
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
    printf("Hello world from assembler\n");
    return PROCESS_RESULT_SUCCESS;
}
