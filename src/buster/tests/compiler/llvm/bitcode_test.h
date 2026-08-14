#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/llvm/bitcode.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL UnitTestResult llvm_bitcode_tests(UnitTestArguments* arguments);
#endif
