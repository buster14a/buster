#pragma once

#include <buster/base.h>
#include <buster/os.h>

typedef struct Thread Thread;

#if BUSTER_LINK_LIBC
#if BUSTER_FUZZING
BUSTER_DECL s32 buster_fuzz(const u8* pointer, size_t size);
#else
BUSTER_DECL ProcessResult process_arguments();
typedef ProcessResult ThreadEntryPoint(void);
BUSTER_DECL ProcessResult thread_entry_point();
BUSTER_DECL ProcessResult buster_argument_process(StringOsList argument_pointer, StringOsList environment_pointer, u64 argument_index, StringOs argument);
#endif
#endif
