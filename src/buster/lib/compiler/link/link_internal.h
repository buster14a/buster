#pragma once

#include <buster/lib/compiler/link/link.h>

#if BUSTER_INCLUDE_TESTS
// The ordinary executable writer with caller-owned temporary storage, so
// replay tests can measure its complete scratch high water independently.
BUSTER_F_DECL NativeExecutableLinkResult link_elf_test_executable(Arena* arena, Arena* temporary, ObjectFile* object,
                                                                 NativeExecutableLinkOptions options);
// The production ELF section-table append over an image the caller placed, so
// tests can choose its growth path and the arena's dirty high-water mark. The
// image must be at least an ELF header long; the loaded sections sit at offset
// zero and the layout is static.
BUSTER_F_DECL NativeExecutableLinkResult link_elf_test_section_table_append(Arena* arena, ByteSlice image, ObjectFile* object);
#endif
