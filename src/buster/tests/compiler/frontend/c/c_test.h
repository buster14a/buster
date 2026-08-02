#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/string.h>

BUSTER_TEST_F_DECL CType* c_type_from_id(CParseResult* parse, CTypeId id);
BUSTER_TEST_F_DECL CEntityId c_parse_lookup_entity(CParseResult* result, CScopeId scope, String8 name);
BUSTER_TEST_F_DECL UnitTestResult c_frontend_tests(UnitTestArguments* arguments);
