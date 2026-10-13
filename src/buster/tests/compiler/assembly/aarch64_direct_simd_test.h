#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/assembly/aarch64_simd_semantics.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL UnitTestResult aarch64_direct_simd_tests(UnitTestArguments* arguments);
/* Shared with the complex-SIMD tests: an arrangement of the same register class that differs from `value`'s. */
BUSTER_F_DECL BusterA64SIMDArrangement aarch64_simd_test_alternate_arrangement(BusterA64SemanticVMValue value);
/* Shared by the direct and complex SIMD tests; `complex` selects the binding
 * whose typed decode and re-encode audit each candidate word. */
BUSTER_F_DECL Target aarch64_simd_test_target(void);
BUSTER_F_DECL bool aarch64_simd_test_audit_row(Target target, bool complex, u32 row_index, BusterA64SIMDRowInfo row, u32 canonical_index,
                                               BusterAarch64CanonicalFormInfo canonical, u32* legal_count, u32* first_word);
#endif
