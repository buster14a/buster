#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/jit/jit.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL UnitTestResult jit_tests(UnitTestArguments* arguments);
#endif
