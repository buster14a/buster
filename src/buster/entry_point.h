#pragma once

#include <buster/base.h>
#include <buster/os.h>

typedef struct Thread Thread;

#if BUSTER_LINK_LIBC
#if BUSTER_FUZZING
BUSTER_F_DECL s32 buster_fuzz(const u8* pointer, size_t size);
#else
BUSTER_F_DECL ProcessResult process_arguments();
BUSTER_F_DECL ProcessResult entry_point();
BUSTER_F_DECL ProcessResult buster_argument_process(StringOsList argument_pointer, StringOsList environment_pointer, u64 argument_index, StringOs argument);
#endif
#endif

#if !BUSTER_SINGLE_THREADED
BUSTER_F_DECL void async_user_tick();
#endif
