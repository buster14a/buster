#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/assembly/assembly.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL UnitTestResult assembly_tests(UnitTestArguments* arguments);
#endif
