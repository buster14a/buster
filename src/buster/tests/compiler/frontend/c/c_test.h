#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL CType* c_type_from_id(CParseResult* parse, CTypeId id);
BUSTER_F_DECL CEntityId c_parse_lookup_entity(CParseResult* result, CScopeId scope, String8 name);
BUSTER_F_DECL UnitTestResult c_frontend_tests(UnitTestArguments* arguments);
#endif
