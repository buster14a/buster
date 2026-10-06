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

// Link-local decisions: VISITING belongs only to an unresolved associative
// path, never to a published ObjectFile or a retained plan after success.
typedef enum LinkComdatState
{
    LINK_COMDAT_STATE_UNKNOWN,
    LINK_COMDAT_STATE_KEEP,
    LINK_COMDAT_STATE_DISCARD,
    LINK_COMDAT_STATE_VISITING,
} LinkComdatState;

#if BUSTER_INCLUDE_TESTS
typedef struct LinkSectionSetCounts LinkSectionSetCounts;
struct LinkSectionSetCounts
{
    u64 input_rows;
    u64 members;
    u64 name_comparisons;
    u64 group_rows;
    u64 placements;
    u64 bound_queries;
    u64 bound_comparisons;
};

// Exact public merge path; counters cover the named-section index and bound
// lookup only, excluding ordinary symbol tables, byte copies and image IO.
BUSTER_F_DECL LinkObjectResult link_objects_section_sets_test(Arena* arena, ObjectFile* objects, u32 object_count,
                                                             LinkOptions options, LinkSectionSetCounts* counts);
// Exact production TLS-site membership with work counters, independent of image IO.
BUSTER_F_DECL bool link_elf_test_tls_membership(Arena* temporary, ObjectFile* object, bool* matches,
                                             u64* build_rows, u64* queries, u64* probes);
typedef struct LinkComdatAssociationCounts LinkComdatAssociationCounts;
struct LinkComdatAssociationCounts
{
    u64 row_scans;
    u64 parent_reads;
    u64 path_marks;
    u64 resolved_writes;
};

// Validates records and caller seeds before invoking the exact production
// association resolver. Seed each associative row UNKNOWN, every other row
// KEEP or DISCARD after group arbitration; storage is comdat_count bytes.
// Counts describe only resolution, excluding this test wrapper's validation.
// Resolution mutates states only, requires no allocator or auxiliary storage,
// and may leave VISITING in caller storage on a refused cycle.
BUSTER_F_DECL LinkError link_comdat_associations_resolve_test(ObjectFile* object, u8* states, LinkComdatAssociationCounts* counts);
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
