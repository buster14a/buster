#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/assembly/aarch64_simd_semantics.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL UnitTestResult aarch64_direct_simd_tests(UnitTestArguments* arguments);
/* Shared with the complex-SIMD tests: an arrangement of the same register class that differs from `value`'s. */
BUSTER_F_DECL BusterA64SIMDArrangement aarch64_simd_test_alternate_arrangement(BusterA64SemanticVMValue value);
#endif
