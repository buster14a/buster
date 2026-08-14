#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL UnitTestResult ir_tests(UnitTestArguments* arguments);
#endif
