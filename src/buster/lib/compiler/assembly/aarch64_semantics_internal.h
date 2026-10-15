#ifndef BUSTER_AARCH64_SEMANTICS_INTERNAL_H
#define BUSTER_AARCH64_SEMANTICS_INTERNAL_H

#include <buster/lib/arena.h>

#if BUSTER_INCLUDE_TESTS
typedef struct BusterA64SemanticStorageTestResult BusterA64SemanticStorageTestResult;
struct BusterA64SemanticStorageTestResult
{
    u32 checks;
    u32 failures;
};
BUSTER_F_DECL BusterA64SemanticStorageTestResult buster_aarch64_semantics_test_storage(Arena* arena);
#endif

#endif
