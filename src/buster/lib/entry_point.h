#pragma once

#include <buster/lib/base.h>
#include <buster/lib/os.h>

typedef struct Thread Thread;

#if BUSTER_LINK_LIBC
#if BUSTER_FUZZ_AVAILABLE
BUSTER_F_DECL s32 buster_fuzz_test_input(const u8* pointer, size_t size);
BUSTER_F_DECL ProcessResult buster_fuzz_run(SliceString8 fuzz_arguments);
#endif
BUSTER_F_DECL ProcessResult process_arguments(void);
BUSTER_F_DECL ProcessResult entry_point(void);
BUSTER_F_DECL void entry_point_exit_code_set(s32 exit_code);
BUSTER_F_DECL ProcessResult buster_argument_process(u64 argument_index);
BUSTER_F_DECL bool update(void);
BUSTER_F_DECL bool frame(void);
#endif
