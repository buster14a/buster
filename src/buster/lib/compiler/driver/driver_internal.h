#pragma once
// Private driver classification seams shared with focused regression tests.
#include <buster/lib/compiler/driver/driver.h>

BUSTER_F_DECL bool compiler_driver_language_is_native(CompilerDriverLanguage language);
BUSTER_F_DECL String8 compiler_driver_native_link_report(Arena* arena, CompilerDriverInvocation invocation);
typedef struct CompilerDriverDependencyQuoteResult CompilerDriverDependencyQuoteResult;
struct CompilerDriverDependencyQuoteResult
{
    String8 text;
    bool representable;
};
#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL void compiler_driver_test_merge_fast_declines(Arena* arena, IrFastStatistics* total, IrFastStatistics const* unit,
                                                            String8 source, u64* capacity);
// GCC Makefile escaping and object-derived depfile path helpers.
BUSTER_F_DECL CompilerDriverDependencyQuoteResult compiler_driver_dependency_quote(Arena* arena, String8 value);
BUSTER_F_DECL String8 compiler_driver_dependency_path(Arena* arena, String8 object_path);
// Lowers the per-input function-record cap so a test can reach it; zero
// restores COMPILER_DRIVER_INPUT_FUNCTION_LIMIT. Set only between invocations.
BUSTER_F_DECL void compiler_driver_test_set_function_limit(u32 limit);
// Replaces COMPILER_DRIVER_LANE_MIN_WORK_BYTES,
// COMPILER_DRIVER_LANE_INPUT_WORK_BYTES and
// COMPILER_DRIVER_LANE_WORK_PER_WORKER_BYTES for invocations on the calling
// thread; a zero minimum always permits lanes and a zero per-worker floor never
// caps them. Restore after the invocation.
BUSTER_F_DECL void compiler_driver_test_set_lane_work(u64 minimum_bytes, u64 per_input_bytes, u64 per_worker_bytes);
BUSTER_F_DECL void compiler_driver_test_restore_lane_work(void);
// Fail one coordinator-owned TU arena creation, counting only TU creations.
BUSTER_F_DECL void compiler_driver_test_fail_unit_arena_reservation(u32 ordinal);
BUSTER_F_DECL bool compiler_driver_test_unit_arena_reservation_pending(void);
// Releases the caller's parked gang and forgets which native families the
// driver prepared, only when the caller is then the only live thread; returns
// whether it did. The prepared mask has bit 0 for x86-64, bit 1 for AArch64.
#define BUSTER_COMPILER_DRIVER_TEST_FAMILY_X86_64 ((u32)1)
#define BUSTER_COMPILER_DRIVER_TEST_FAMILY_AARCH64 ((u32)2)
BUSTER_F_DECL bool compiler_driver_test_native_families_reset(void);
BUSTER_F_DECL u32 compiler_driver_test_native_families_prepared(void);
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
typedef enum CompilerDriverMachDylibStatus
{
    COMPILER_DRIVER_MACH_DYLIB_UNUSABLE,
    COMPILER_DRIVER_MACH_DYLIB_INCOMPATIBLE,
    COMPILER_DRIVER_MACH_DYLIB_USABLE,
} CompilerDriverMachDylibStatus;

typedef struct CompilerDriverMachDylibIdentity
{
    String8 install_name;
    u32 current_version;
    u32 compatibility_version;
} CompilerDriverMachDylibIdentity;

enum
{
    COMPILER_DRIVER_ELF_SCRIPT_BYTES = 65536,
    COMPILER_DRIVER_ELF_SCRIPT_DEPTH = 32,
    COMPILER_DRIVER_ELF_SCRIPT_MEMBERS = 4096,
};
BUSTER_F_DECL bool compiler_driver_elf_linker_script(ByteSlice bytes);
BUSTER_F_DECL bool compiler_driver_elf_script_members(ByteSlice bytes, String8* members, u32* count);
BUSTER_F_DECL CompilerDriverMachDylibStatus compiler_driver_mach_dylib_identity(ByteSlice bytes, Target target,
                                                                                 CompilerDriverMachDylibIdentity* identity);
BUSTER_F_DECL CompilerDriverMachDylibStatus compiler_driver_mach_tbd_identity(Arena* arena, ByteSlice bytes, Target target,
                                                                                CompilerDriverMachDylibIdentity* identity);
