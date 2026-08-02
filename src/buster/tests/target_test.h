#pragma once

#include <buster/tests/test.h>
#include <buster/lib/target.h>
#include <buster/lib/string.h>

BUSTER_TEST_F_DECL CpuModel cpu_model_resolve_detected(CpuModel model);
BUSTER_TEST_F_DECL UnitTestResult target_tests(UnitTestArguments* arguments);
