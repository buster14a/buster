#pragma once

typedef struct ProgramInput ProgramInput;
#include <buster/lib.h>

BUSTER_DECL ProcessResult process_arguments();

#if BUSTER_UNITY_BUILD
#include <buster/entry_point.c>
#endif
