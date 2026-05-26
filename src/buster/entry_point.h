#pragma once

#include <buster/base.h>
#include <buster/os.h>

typedef struct Thread Thread;

#if BUSTER_LINK_LIBC
#if BUSTER_FUZZ
BUSTER_F_DECL s32 buster_fuzz(const u8* pointer, size_t size);
#else
BUSTER_F_DECL ProcessResult process_arguments(void);
BUSTER_F_DECL ProcessResult entry_point(void);
BUSTER_F_DECL ProcessResult buster_argument_process(u64 argument_index);
#endif
#endif
