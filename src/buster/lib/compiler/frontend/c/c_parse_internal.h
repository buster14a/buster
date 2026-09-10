#pragma once

// Private test seams for the production constexpr query and binding undo log.
// Tests own their storage and observe production behavior, not a duplicate
// implementation. No declarations enter production builds.
#include <buster/lib/compiler/frontend/c/c.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL bool c_test_validate_constexpr_declaration(Arena* arena, CParseResult* result, CPreprocessResult preprocess,
                                                       CDeclaration* declaration);
BUSTER_F_DECL u32 c_test_parse_binding_bind(CParseResult* result, CScopeId scope, CEntityId entity, u32 symbol);
BUSTER_F_DECL void c_test_parse_binding_unwind(CParseResult* result, u32 mark);
#endif
