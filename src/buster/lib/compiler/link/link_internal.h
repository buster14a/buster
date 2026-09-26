#pragma once

#include <buster/lib/compiler/link/link.h>

// One function an image has to call before or after `main`, as the merged
// object spells it: the symbol its `.init_array`/`.fini_array` slot is
// relocated against, plus that relocation's addend.  Buster's own objects
// name the function itself with a zero addend; Clang's name the section
// symbol plus the function's offset inside it, which is why the addend is
// carried rather than assumed away.
typedef struct LinkInitializerEntry LinkInitializerEntry;
struct LinkInitializerEntry
{
    s64 addend;
    u32 symbol;
    u32 reserved;
};

#if BUSTER_INCLUDE_TESTS
// The exact production collector, with caller-owned output storage so tests
// can exercise dirty/reused slots, holes, duplicate relocations and bounds.
BUSTER_F_DECL u32 link_initializer_entries_collect_test(ObjectFile* object, ObjectSectionKind kind, LinkInitializerEntry* entries, bool reverse);
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
