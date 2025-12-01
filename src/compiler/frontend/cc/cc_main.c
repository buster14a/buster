#pragma once
#include <lib.h>

STRUCT(CCProgramState)
{
    ProgramState general_program_state;
};

BUSTER_LOCAL CCProgramState cc_program_state = {

};

BUSTER_IMPL ProgramState* program_state = &cc_program_state.general_program_state;

BUSTER_IMPL ProcessResult process_arguments()
{
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_IMPL ProcessResult thread_entry_point(Thread* thread)
{
    BUSTER_UNUSED(thread);
    return PROCESS_RESULT_SUCCESS;
}
