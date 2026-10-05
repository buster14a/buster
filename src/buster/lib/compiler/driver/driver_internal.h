#pragma once
// Private driver classification seams shared with focused regression tests.
#include <buster/lib/compiler/driver/driver.h>

BUSTER_F_DECL bool compiler_driver_language_is_native(CompilerDriverLanguage language);
#if BUSTER_INCLUDE_TESTS
// Lowers the per-input function-record cap so a test can reach it; zero
// restores COMPILER_DRIVER_INPUT_FUNCTION_LIMIT. Set only between invocations.
BUSTER_F_DECL void compiler_driver_test_set_function_limit(u32 limit);
// A calling-thread observer for focused serial metrics tests. Completion
// events follow the real setup calls; input events bracket the real clocks.
typedef enum CompilerDriverTestSetupEvent
{
    COMPILER_DRIVER_TEST_SETUP_COMPILER = 1,
    COMPILER_DRIVER_TEST_SETUP_TARGET = 2,
    COMPILER_DRIVER_TEST_SETUP_ARENAS = 4,
    COMPILER_DRIVER_TEST_SETUP_INPUT_BEGIN = 8,
    COMPILER_DRIVER_TEST_SETUP_INPUT_END = 16,
} CompilerDriverTestSetupEvent;

#define BUSTER_COMPILER_DRIVER_TEST_SETUP_COMPLETE ((u32)(COMPILER_DRIVER_TEST_SETUP_COMPILER | COMPILER_DRIVER_TEST_SETUP_TARGET | COMPILER_DRIVER_TEST_SETUP_ARENAS))

typedef struct CompilerDriverTestSetupOrder
{
    u32 completed_setup;
    u32 input_starts;
    u32 input_ends;
    u32 order_errors;
    bool input_open;
} CompilerDriverTestSetupOrder;

// Begin resets and arms; end snapshots and disarms, including after refused
// invocations. Explicit events exercise the same classifier's negative cases.
BUSTER_F_DECL void compiler_driver_test_setup_order_begin(void);
BUSTER_F_DECL void compiler_driver_test_setup_order_event(CompilerDriverTestSetupEvent event);
BUSTER_F_DECL CompilerDriverTestSetupOrder compiler_driver_test_setup_order_end(void);
#endif
BUSTER_F_DECL bool compiler_driver_elf_linker_script(ByteSlice bytes);
