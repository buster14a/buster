#pragma once
#include <buster/lib/compiler/object/object.h>

#if BUSTER_INCLUDE_TESTS
// Compare indexed printer queries against original-table scan semantics.
BUSTER_F_DECL bool object_assembly_test_index_queries(Arena* arena, ObjectFile* object);
// object_write for ELF64 through the writer that preceded planned emission,
// kept as a differential oracle: the same validation, and statistics counted
// the same way, around the original zero-fill-then-patch serializer.
BUSTER_F_DECL ObjectArtifact object_test_write_elf64_reference(Arena* arena, ObjectFile* object);
// The ELF64 plan alone. It never reads a payload or a name's bytes, so it can
// size hostile shapes -- lengths near 2^64, names past 4 GiB, section counts
// at SHN_LORESERVE -- that no test could allocate. `size` is the planned file
// size, or zero with the error the writer would have returned.
BUSTER_F_DECL ObjectError object_test_elf64_plan(Arena* arena, ObjectFile* object, u64* size);
#endif
