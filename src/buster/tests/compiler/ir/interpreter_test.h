#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/ir/interpreter_internal.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL UnitTestResult ir_interpreter_tests(UnitTestArguments* arguments);
#endif
