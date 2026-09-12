#pragma once
#include <buster/lib/compiler/object/object.h>

#if BUSTER_INCLUDE_TESTS
// Compare indexed printer queries against original-table scan semantics.
BUSTER_F_DECL bool object_assembly_test_index_queries(Arena* arena, ObjectFile* object);
#endif
