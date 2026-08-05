#pragma once

#include <buster/tests/test.h>
#include <buster/lib/target.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL CpuModel cpu_model_resolve_detected(CpuModel model);
BUSTER_F_DECL UnitTestResult target_tests(UnitTestArguments* arguments);
#endif
