#pragma once
#include <buster/lib/compiler/object/object.h>

#if BUSTER_INCLUDE_TESTS
// Compare indexed printer queries against original-table scan semantics.
BUSTER_F_DECL bool object_assembly_test_index_queries(Arena* arena, ObjectFile* object);
// The ELF64 plan alone. It never reads payloads, and only compares bounded
// canonical names for empty slots. It can size hostile nonempty shapes --
// lengths near 2^64, names past 4 GiB, section counts
// at SHN_LORESERVE -- that no test could allocate. `size` is the planned file
// size, or zero with the error the writer would have returned.
BUSTER_F_DECL ObjectError object_test_elf64_plan(Arena* arena, ObjectFile* object, u64* size);
// Append-writer reservation checks only; never reads payload/name bytes.
BUSTER_F_DECL ObjectError object_test_32_capacity(ObjectFile* object, ObjectFormat format, u64* capacity);
#endif
