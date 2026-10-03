#pragma once
// Private driver classification seams shared with focused regression tests.
#include <buster/lib/compiler/driver/driver.h>

BUSTER_F_DECL bool compiler_driver_language_is_native(CompilerDriverLanguage language);
#if BUSTER_INCLUDE_TESTS
// Lowers the per-input function-record cap so a test can reach it; zero
// restores COMPILER_DRIVER_INPUT_FUNCTION_LIMIT. Set only between invocations.
BUSTER_F_DECL void compiler_driver_test_set_function_limit(u32 limit);
#endif
