#pragma once

// Private test seam for the production constexpr type query. No parse-machine
// dependency escapes; these tests use leaves and already resolved array bounds.
#include <buster/lib/compiler/frontend/c/c.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL bool c_test_validate_constexpr_declaration(Arena* arena, CParseResult* result, CPreprocessResult preprocess,
                                                          CDeclaration* declaration);
#endif
