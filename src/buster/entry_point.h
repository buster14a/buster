#pragma once

typedef struct ProgramInput ProgramInput;
#include <buster/lib.h>

#if BUSTER_LINK_LIBC
#if BUSTER_FUZZING
BUSTER_DECL s32 buster_fuzz(const u8* pointer, size_t size);
#else
BUSTER_DECL ProcessResult thread_entry_point();
BUSTER_DECL ProcessResult process_arguments();
#endif
#endif

#if BUSTER_UNITY_BUILD
#include <buster/entry_point.c>
#endif
