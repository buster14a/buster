#pragma once

#include <buster/lib/compiler/link/link.h>

#if BUSTER_INCLUDE_TESTS
// The ordinary executable writer with caller-owned temporary storage, so
// replay tests can measure its complete scratch high water independently.
BUSTER_F_DECL NativeExecutableLinkResult link_elf_test_executable(Arena* arena, Arena* temporary, ObjectFile* object,
                                                                 NativeExecutableLinkOptions options);
#endif
