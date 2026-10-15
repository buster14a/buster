#pragma once

#include <buster/tests/test.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL UnitTestResult c_macro_conditional_tests(UnitTestArguments* arguments);
BUSTER_F_DECL void c_macro_expansion_rlimit_child_run(UnitTestArguments* arguments);
#endif
