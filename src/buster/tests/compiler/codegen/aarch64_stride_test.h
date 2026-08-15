#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/codegen/codegen_internal.h>
#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL UnitTestResult aarch64_stride_tests(UnitTestArguments* arguments);
#endif
