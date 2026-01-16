#pragma once

#include <buster/base.h>
#include <buster/entry_point.h>
#include <buster/ui_core.h>
#include <buster/render_core.h>
#include <buster/window.h>

STRUCT(ToyGuiGlobalState)
{
    ProgramState general;
};

BUSTER_GLOBAL_LOCAL ToyGuiGlobalState toy_global_state = {};

BUSTER_IMPL ProgramState* program_state = &toy_global_state.general;

BUSTER_IMPL ProcessResult process_arguments()
{
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_IMPL ProcessResult thread_entry_point()
{
    toy();
    return PROCESS_RESULT_SUCCESS;
}
