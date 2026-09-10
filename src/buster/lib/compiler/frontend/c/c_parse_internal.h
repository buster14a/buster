#pragma once

// Private test seam for the production binding array and its undo log. Tests
// supply owned storage and observe the existing search cursor, not a global
// counter or a second implementation. No declarations enter production builds.
#include <buster/lib/compiler/frontend/c/c.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL u32 c_test_parse_binding_bind(CParseResult* result, CScopeId scope, CEntityId entity, u32 symbol);
BUSTER_F_DECL void c_test_parse_binding_unwind(CParseResult* result, u32 mark);
#endif
