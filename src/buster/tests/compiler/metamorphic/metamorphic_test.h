#pragma once
#include <buster/tests/test.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL UnitTestResult metamorphic_tests(UnitTestArguments* arguments);
BUSTER_F_DECL ProcessResult metamorphic_campaign(Arena* arena);
#endif
