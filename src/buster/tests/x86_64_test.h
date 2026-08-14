#pragma once

#include <buster/tests/test.h>
#include <buster/lib/x86_64.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL UnitTestResult x86_64_tests(UnitTestArguments* arguments);
#endif
