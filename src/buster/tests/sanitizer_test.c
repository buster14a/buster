#include <buster/tests/sanitizer_test.h>
#include <buster/tests/sanitizer_test_internal.h>

#if BUSTER_INCLUDE_TESTS
#include <buster/lib/os.h>
#include <buster/lib/string.h>
#include <buster/lib/system_headers.h>

enum
{
    SANITIZER_TEST_ALLOCATION_SIZE = 4096,
};

// Debug and every sanitized build keep BUSTER_CHECK, BUSTER_ASSERT and
// BUSTER_UNREACHABLE as diagnostics. Only optimized unsanitized builds turn
// them into optimizer assumptions or unevaluated expressions (os.h, base.h),
// and a false raw assumption is undefined behavior, never a test oracle.
#define SANITIZER_TEST_CHECKED_CONTRACTS (!BUSTER_OPTIMIZE || BUSTER_SANITIZE)
// Mobile test payloads run inside an app and cannot relaunch themselves as
// `<argv0> test`, so the false-operand children run on desktop targets only.
#define SANITIZER_TEST_CONTRACT_CHILDREN (SANITIZER_TEST_CHECKED_CONTRACTS && !BUSTER_ANDROID && !BUSTER_IOS)

// The effective compiler flags, independently of the BUSTER_ defines that the
// build passes. MSVC exposes no optimization macro; its row reports unknown.
#if BUSTER_COMPILER_CLANG || BUSTER_COMPILER_GCC
#define SANITIZER_TEST_OPTIMIZATION_KNOWN 1
#if defined(__OPTIMIZE__)
#define SANITIZER_TEST_COMPILER_OPTIMIZES 1
#else
#define SANITIZER_TEST_COMPILER_OPTIMIZES 0
#endif
#else
#define SANITIZER_TEST_OPTIMIZATION_KNOWN 0
#define SANITIZER_TEST_COMPILER_OPTIMIZES 0
#endif

#if defined(__SANITIZE_ADDRESS__)
#define SANITIZER_TEST_COMPILER_SANITIZES 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SANITIZER_TEST_COMPILER_SANITIZES 1
#else
#define SANITIZER_TEST_COMPILER_SANITIZES 0
#endif
#else
#define SANITIZER_TEST_COMPILER_SANITIZES 0
#endif

BUSTER_GLOBAL_LOCAL bool sanitizer_test_is_registered_launch(void)
{
    bool result = false;
    SliceString8 arguments = program_state->input.arguments;
    for (u64 index = 0; index < arguments.length; index += 1)
    {
        if (string_equal(arguments.pointer[index], S8("--verbose=1")))
        {
            result = true;
        }
    }
    return result;
}

#if BUSTER_COMPILER_CLANG || BUSTER_COMPILER_GCC
__attribute__((noinline))
#endif
BUSTER_GLOBAL_LOCAL void sanitizer_test_lose_allocation(void)
{
    volatile u8* allocation = (volatile u8*)malloc(SANITIZER_TEST_ALLOCATION_SIZE);
    if (allocation)
    {
        allocation[0] = 0x51;
        allocation[SANITIZER_TEST_ALLOCATION_SIZE - 1] = 0xa7;
#if defined(__clang_analyzer__)
        free((void*)allocation);
#endif
    }
#if !defined(__clang_analyzer__)
    allocation = 0;
#endif
    BUSTER_UNUSED(allocation);
}

#if !BUSTER_SINGLE_THREADED
BUSTER_GLOBAL_LOCAL ThreadReturnType sanitizer_test_clean_thread(void* argument)
{
    BUSTER_UNUSED(argument);
    void* allocation = malloc(SANITIZER_TEST_ALLOCATION_SIZE);
    if (allocation)
    {
        memset(allocation, 0x35, SANITIZER_TEST_ALLOCATION_SIZE);
        free(allocation);
    }
}

BUSTER_GLOBAL_LOCAL ThreadReturnType sanitizer_test_leak_thread(void* argument)
{
    BUSTER_UNUSED(argument);
    sanitizer_test_lose_allocation();
}
#endif

#if BUSTER_COMPILER_CLANG || BUSTER_COMPILER_GCC
__attribute__((noinline))
#endif
BUSTER_GLOBAL_LOCAL bool sanitizer_test_count_operand(u32* count, bool value)
{
    *count += 1;
    return value;
}

ProcessResult sanitizer_test_canary_run(String8 mode)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (string_equal(mode, S8("main-thread-clean")))
    {
        void* allocation = malloc(SANITIZER_TEST_ALLOCATION_SIZE);
        if (allocation)
        {
            memset(allocation, 0x35, SANITIZER_TEST_ALLOCATION_SIZE);
            free(allocation);
            string_print(S8("SANITIZER_CANARY kind=main-thread-clean status=clean\n"));
            result = PROCESS_RESULT_SUCCESS;
        }
    }
    else if (string_equal(mode, S8("undefined-shift")))
    {
#if defined(__clang_analyzer__)
        volatile unsigned exponent = 1;
#else
        volatile unsigned exponent = 32;
#endif
        volatile unsigned value = 1;
        volatile unsigned shifted = value << exponent;
        BUSTER_UNUSED(shifted);
        result = PROCESS_RESULT_SUCCESS;
    }
    else if (string_equal(mode, S8("main-thread-leak")))
    {
        sanitizer_test_lose_allocation();
        string_print(S8("SANITIZER_CANARY kind=main-thread-leak status=returning\n"));
        result = PROCESS_RESULT_SUCCESS;
    }
#if !BUSTER_SINGLE_THREADED
    else if (string_equal(mode, S8("worker-thread-clean")))
    {
        OsThreadHandle* thread = os_thread_create((ThreadCreateOptions){.callback = &sanitizer_test_clean_thread});
        if (thread && os_thread_join(thread))
        {
            string_print(S8("SANITIZER_CANARY kind=worker-thread-clean status=clean\n"));
            result = PROCESS_RESULT_SUCCESS;
        }
    }
    else if (string_equal(mode, S8("worker-thread-leak")))
    {
        OsThreadHandle* thread = os_thread_create((ThreadCreateOptions){.callback = &sanitizer_test_leak_thread});
        if (thread && os_thread_join(thread))
        {
            string_print(S8("SANITIZER_CANARY kind=worker-thread-leak status=joined\n"));
            result = PROCESS_RESULT_SUCCESS;
        }
    }
#endif
#if SANITIZER_TEST_CHECKED_CONTRACTS
    else if (string_equal(mode, S8("contract-check-false")))
    {
        u32 count = 0;
        string_print(S8("SANITIZER_CANARY kind=contract-check-false status=reached\n"));
        BUSTER_CHECK(sanitizer_test_count_operand(&count, false));
        string_print(S8("SANITIZER_CANARY kind=contract-check-false status=survived count={u32}\n"), count);
    }
    else if (string_equal(mode, S8("contract-assert-false")))
    {
        u32 count = 0;
        string_print(S8("SANITIZER_CANARY kind=contract-assert-false status=reached\n"));
        BUSTER_ASSERT(sanitizer_test_count_operand(&count, false));
        string_print(S8("SANITIZER_CANARY kind=contract-assert-false status=survived count={u32}\n"), count);
    }
    else if (string_equal(mode, S8("contract-unreachable")))
    {
        string_print(S8("SANITIZER_CANARY kind=contract-unreachable status=reached\n"));
        BUSTER_UNREACHABLE();
    }
#endif
    else
    {
        string_print(S8("SANITIZER_CANARY kind={S8} status=unknown\n"), mode);
    }
    return result;
}

#if SANITIZER_TEST_CONTRACT_CHILDREN
BUSTER_GLOBAL_LOCAL String8 sanitizer_test_contract_stream(ProcessWaitResult wait, StandardStream stream)
{
    String8 result = {
        .pointer = (char8*)wait.streams[stream].pointer,
        .length = wait.streams[stream].length,
    };
    return result;
}

// The child inherits this environment, sanitizer options included, so its
// diagnostics policy is the registered launch's own.
BUSTER_GLOBAL_LOCAL ProcessWaitResult sanitizer_test_run_contract_child(Arena* arena, String8 mode)
{
    String8 child_arguments[] = {
        program_state->input.arguments.pointer[0],
        S8("test"),
    };
    SliceString8 inherited_keys = program_state->input.environment_keys;
    SliceString8 inherited_values = program_state->input.environment_values;
    u64 inherited_count = BUSTER_MIN(inherited_keys.length, inherited_values.length);
    String8* child_keys = arena_allocate(arena, String8, inherited_count + 1);
    String8* child_values = arena_allocate(arena, String8, inherited_count + 1);
    for (u64 index = 0; index < inherited_count; index += 1)
    {
        child_keys[index] = inherited_keys.pointer[index];
        child_values[index] = inherited_values.pointer[index];
    }
    child_keys[inherited_count] = S8("BUSTER_SANITIZER_CANARY_MODE");
    child_values[inherited_count] = mode;
    ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments),
                                                (SliceString8){.pointer = child_keys, .length = inherited_count + 1},
                                                (SliceString8){.pointer = child_values, .length = inherited_count + 1},
                                                (ProcessSpawnOptions){
                                                    .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                                });
    ProcessWaitResult result = {0};
    if (spawn.handle)
    {
        result = os_process_wait_deadline(arena, spawn, 30000000);
    }
    return result;
}

// os_fail_raw reports and then exits with status 1. Anything else -- a launch
// failure, a signal, a sanitizer's reserved exit code -- is not the diagnostic.
BUSTER_GLOBAL_LOCAL bool sanitizer_test_exited_with_failure_status(ProcessWaitResult wait)
{
#if BUSTER_WINDOWS
    bool result = wait.result == PROCESS_RESULT_FAILED && wait.platform_status == 1;
#else
    int status = (int)wait.platform_status;
    bool result = wait.result == PROCESS_RESULT_FAILED && WIFEXITED(status) && WEXITSTATUS(status) == 1;
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool sanitizer_test_stderr_has_no_sanitizer_report(String8 error)
{
    bool result = string_first_sequence(error, S8("runtime error:")) == BUSTER_STRING_NO_MATCH &&
                  string_first_sequence(error, S8("Sanitizer")) == BUSTER_STRING_NO_MATCH;
    return result;
}

BUSTER_GLOBAL_LOCAL void sanitizer_test_check_contract_failure(UnitTestArguments* arguments, UnitTestResult* result_pointer, String8 kind)
{
    UnitTestResult result = {0};
    u64 position = arguments->arena->position;
    String8 mode = string_format(arguments->arena, S8("contract-{S8}-false"), kind);
    String8 reached = string_format(arguments->arena, S8("SANITIZER_CANARY kind={S8} status=reached\n"), mode);
    ProcessWaitResult wait = sanitizer_test_run_contract_child(arguments->arena, mode);
    String8 output = sanitizer_test_contract_stream(wait, STANDARD_STREAM_OUTPUT);
    String8 error = sanitizer_test_contract_stream(wait, STANDARD_STREAM_ERROR);
    arguments->show(arguments, S8("CONTRACT_CANARY_CAPTURE mode={S8} stdout_begin\n{S8}stdout_end stderr_begin\n{S8}stderr_end\n"), mode, output,
                    error);
    BUSTER_TEST(arguments, !wait.timed_out);
    BUSTER_TEST(arguments, sanitizer_test_exited_with_failure_status(wait));
    BUSTER_TEST(arguments, string_first_sequence(output, reached) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(output, S8("status=survived")) == BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_starts_with_sequence(error, S8("assertion failed at ")));
    BUSTER_TEST(arguments, string_first_sequence(error, S8("sanitizer_test.c:")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(error, S8(" in sanitizer_test_canary_run\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, sanitizer_test_stderr_has_no_sanitizer_report(error));
    result_pointer->test_count += result.test_count;
    result_pointer->succeeded_test_count += result.succeeded_test_count;
    arena_set_position(arguments->arena, position);
}

#if !BUSTER_WINDOWS
// BUSTER_UNREACHABLE traps through __builtin_trap: SIGILL on x86-64, SIGTRAP
// on AArch64. A different signal, or an exit, is not this contract.
BUSTER_GLOBAL_LOCAL void sanitizer_test_check_contract_unreachable(UnitTestArguments* arguments, UnitTestResult* result_pointer)
{
    UnitTestResult result = {0};
    u64 position = arguments->arena->position;
    ProcessWaitResult wait = sanitizer_test_run_contract_child(arguments->arena, S8("contract-unreachable"));
    String8 output = sanitizer_test_contract_stream(wait, STANDARD_STREAM_OUTPUT);
    String8 error = sanitizer_test_contract_stream(wait, STANDARD_STREAM_ERROR);
    arguments->show(arguments, S8("CONTRACT_CANARY_CAPTURE mode=contract-unreachable stdout_begin\n{S8}stdout_end stderr_begin\n{S8}stderr_end\n"),
                    output, error);
    int status = (int)wait.platform_status;
    BUSTER_TEST(arguments, !wait.timed_out);
    BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_CRASH && WIFSIGNALED(status) &&
                               (WTERMSIG(status) == SIGILL || WTERMSIG(status) == SIGTRAP));
    BUSTER_TEST(arguments, string_first_sequence(output, S8("SANITIZER_CANARY kind=contract-unreachable status=reached\n")) !=
                               BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(error, S8("assertion failed")) == BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, sanitizer_test_stderr_has_no_sanitizer_report(error));
    result_pointer->test_count += result.test_count;
    result_pointer->succeeded_test_count += result.succeeded_test_count;
    arena_set_position(arguments->arena, position);
}
#endif
#endif

// Selection of the diagnostic macro definitions (issue 2656). Runs in every
// configuration: the in-process controls hold everywhere, and the false-operand
// children run only where the definitions are diagnostics.
BUSTER_GLOBAL_LOCAL void sanitizer_test_check_contracts(UnitTestArguments* arguments, UnitTestResult* result_pointer)
{
    UnitTestResult result = {0};
    arguments->show(arguments, S8("CONTRACT_CONFIGURATION optimize={u32} sanitize={u32} checked={u32} compiler_optimizes={u32} "
                                  "optimization_known={u32} compiler_sanitizes={u32}\n"),
                    (u32)BUSTER_OPTIMIZE, (u32)BUSTER_SANITIZE, (u32)SANITIZER_TEST_CHECKED_CONTRACTS, (u32)SANITIZER_TEST_COMPILER_OPTIMIZES,
                    (u32)SANITIZER_TEST_OPTIMIZATION_KNOWN, (u32)SANITIZER_TEST_COMPILER_SANITIZES);
#if SANITIZER_TEST_OPTIMIZATION_KNOWN
    BUSTER_TEST(arguments, SANITIZER_TEST_COMPILER_OPTIMIZES == BUSTER_OPTIMIZE);
#endif
    BUSTER_TEST(arguments, SANITIZER_TEST_COMPILER_SANITIZES == BUSTER_SANITIZE);

    u32 check_count = 0;
    BUSTER_CHECK(sanitizer_test_count_operand(&check_count, true));
    BUSTER_TEST(arguments, check_count == 1);

    u32 assert_count = 0;
    BUSTER_ASSERT(sanitizer_test_count_operand(&assert_count, true));
#if SANITIZER_TEST_CHECKED_CONTRACTS
    BUSTER_TEST(arguments, assert_count == 1);
#if SANITIZER_TEST_CONTRACT_CHILDREN
    sanitizer_test_check_contract_failure(arguments, &result, S8("check"));
    sanitizer_test_check_contract_failure(arguments, &result, S8("assert"));
#if !BUSTER_WINDOWS
    sanitizer_test_check_contract_unreachable(arguments, &result);
#endif
#else
    arguments->show(arguments, S8("CONTRACT_CANARY kind=false-operand-children status=unsupported reason=mobile-payload\n"));
#endif
#else
    // Optimized unsanitized builds never evaluate an assertion operand, so an
    // operand must not carry work the program needs.
    BUSTER_TEST(arguments, assert_count == 0);
#endif
    result_pointer->test_count += result.test_count;
    result_pointer->succeeded_test_count += result.succeeded_test_count;
}

#if BUSTER_SANITIZE && !BUSTER_WINDOWS
BUSTER_GLOBAL_LOCAL String8 sanitizer_test_stream(ProcessWaitResult wait, StandardStream stream)
{
    String8 result = {
        .pointer = (char8*)wait.streams[stream].pointer,
        .length = wait.streams[stream].length,
    };
    return result;
}

BUSTER_GLOBAL_LOCAL bool sanitizer_test_failed(ProcessWaitResult wait)
{
    int status = (int)wait.platform_status;
    bool result = (wait.result == PROCESS_RESULT_FAILED && WIFEXITED(status) && WEXITSTATUS(status) != 0) ||
                  (wait.result == PROCESS_RESULT_CRASH && WIFSIGNALED(status));
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessWaitResult sanitizer_test_run_child(Arena* arena, String8 mode)
{
    String8 child_arguments[] = {
        program_state->input.arguments.pointer[0],
        S8("test"),
    };
    String8 environment_keys[] = {
        S8("BUSTER_SANITIZER_CANARY_MODE"),
        S8("ASAN_OPTIONS"),
        S8("UBSAN_OPTIONS"),
        S8("LSAN_OPTIONS"),
        S8("ASAN_SYMBOLIZER_PATH"),
    };
    String8 environment_values[] = {
        mode,
        os_get_environment_variable(S8("ASAN_OPTIONS")),
        os_get_environment_variable(S8("UBSAN_OPTIONS")),
        os_get_environment_variable(S8("LSAN_OPTIONS")),
        os_get_environment_variable(S8("ASAN_SYMBOLIZER_PATH")),
    };
    ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments),
                                                 (SliceString8)BUSTER_ARRAY_TO_SLICE(environment_keys),
                                                 (SliceString8)BUSTER_ARRAY_TO_SLICE(environment_values),
                                                 (ProcessSpawnOptions){
                                                     .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                                 });
    ProcessWaitResult result = {0};
    if (spawn.handle)
    {
        result = os_process_wait_deadline(arena, spawn, 30000000);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void sanitizer_test_show_child(UnitTestArguments* arguments, String8 mode, String8 output, String8 error)
{
    arguments->show(arguments, S8("SANITIZER_CANARY_CAPTURE mode={S8} stdout_begin\n{S8}stdout_end stderr_begin\n{S8}stderr_end\n"), mode,
                    output, error);
}

BUSTER_GLOBAL_LOCAL void sanitizer_test_check_clean(UnitTestArguments* arguments, UnitTestResult* result_pointer, String8 mode, String8 marker)
{
    UnitTestResult result = {0};
    u64 position = arguments->arena->position;
    ProcessWaitResult wait = sanitizer_test_run_child(arguments->arena, mode);
    String8 output = sanitizer_test_stream(wait, STANDARD_STREAM_OUTPUT);
    String8 error = sanitizer_test_stream(wait, STANDARD_STREAM_ERROR);
    sanitizer_test_show_child(arguments, mode, output, error);
    BUSTER_TEST(arguments, !wait.timed_out);
    BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_SUCCESS);
    BUSTER_TEST(arguments, string_first_sequence(output, marker) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(error, S8("runtime error:")) == BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(error, S8("LeakSanitizer")) == BUSTER_STRING_NO_MATCH);
    result_pointer->test_count += result.test_count;
    result_pointer->succeeded_test_count += result.succeeded_test_count;
    arena_set_position(arguments->arena, position);
}

BUSTER_GLOBAL_LOCAL void sanitizer_test_check_ubsan(UnitTestArguments* arguments, UnitTestResult* result_pointer)
{
    UnitTestResult result = {0};
    u64 position = arguments->arena->position;
    ProcessWaitResult wait = sanitizer_test_run_child(arguments->arena, S8("undefined-shift"));
    String8 output = sanitizer_test_stream(wait, STANDARD_STREAM_OUTPUT);
    String8 error = sanitizer_test_stream(wait, STANDARD_STREAM_ERROR);
    sanitizer_test_show_child(arguments, S8("undefined-shift"), output, error);
    BUSTER_TEST(arguments, !wait.timed_out);
    BUSTER_TEST(arguments, sanitizer_test_failed(wait));
    BUSTER_TEST(arguments, string_first_sequence(error, S8("runtime error:")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(error, S8("shift exponent 32")) != BUSTER_STRING_NO_MATCH);
    result_pointer->test_count += result.test_count;
    result_pointer->succeeded_test_count += result.succeeded_test_count;
    arena_set_position(arguments->arena, position);
}

#if BUSTER_LSAN_SUPPORTED
BUSTER_GLOBAL_LOCAL void sanitizer_test_check_lsan(UnitTestArguments* arguments, UnitTestResult* result_pointer, String8 mode, String8 marker)
{
    UnitTestResult result = {0};
    u64 position = arguments->arena->position;
    ProcessWaitResult wait = sanitizer_test_run_child(arguments->arena, mode);
    String8 output = sanitizer_test_stream(wait, STANDARD_STREAM_OUTPUT);
    String8 error = sanitizer_test_stream(wait, STANDARD_STREAM_ERROR);
    sanitizer_test_show_child(arguments, mode, output, error);
    BUSTER_TEST(arguments, !wait.timed_out);
    BUSTER_TEST(arguments, sanitizer_test_failed(wait));
    BUSTER_TEST(arguments, string_first_sequence(output, marker) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(error, S8("ERROR: LeakSanitizer")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(error, S8("4096 byte(s) leaked")) != BUSTER_STRING_NO_MATCH);
    result_pointer->test_count += result.test_count;
    result_pointer->succeeded_test_count += result.succeeded_test_count;
    arena_set_position(arguments->arena, position);
}
#endif

#endif

UnitTestResult sanitizer_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 policy_mode = os_get_environment_variable(S8("BUSTER_SANITIZER_POLICY_MODE"));
    bool registered_launch = sanitizer_test_is_registered_launch();
    sanitizer_test_check_contracts(arguments, &result);
#if BUSTER_SANITIZE && !BUSTER_WINDOWS
    if (registered_launch && string_equal(policy_mode, S8("fatal")))
    {
        String8 asan_options = os_get_environment_variable(S8("ASAN_OPTIONS"));
        String8 ubsan_options = os_get_environment_variable(S8("UBSAN_OPTIONS"));
        String8 lsan_options = os_get_environment_variable(S8("LSAN_OPTIONS"));

        if (BUSTER_REQUIRE(arguments, asan_options.length && ubsan_options.length && lsan_options.length))
        {
            arguments->show(arguments, S8("SANITIZER_POLICY mode=fatal ASAN_OPTIONS={S8} UBSAN_OPTIONS={S8} LSAN_OPTIONS={S8}\n"),
                            asan_options, ubsan_options, lsan_options);
            BUSTER_TEST(arguments, string_first_sequence(asan_options, S8("halt_on_error=1")) != BUSTER_STRING_NO_MATCH);
            BUSTER_TEST(arguments, string_first_sequence(ubsan_options, S8("halt_on_error=1")) != BUSTER_STRING_NO_MATCH);
            BUSTER_TEST(arguments, string_first_sequence(ubsan_options, S8("halt_on_error=0")) == BUSTER_STRING_NO_MATCH);
            BUSTER_TEST(arguments, !string_starts_with_sequence(lsan_options, S8("suppressions=")) &&
                                       string_first_sequence(lsan_options, S8(":suppressions=")) == BUSTER_STRING_NO_MATCH);
            sanitizer_test_check_clean(arguments, &result, S8("main-thread-clean"),
                                       S8("SANITIZER_CANARY kind=main-thread-clean status=clean"));
#if !BUSTER_SINGLE_THREADED
            sanitizer_test_check_clean(arguments, &result, S8("worker-thread-clean"),
                                       S8("SANITIZER_CANARY kind=worker-thread-clean status=clean"));
#endif
            sanitizer_test_check_ubsan(arguments, &result);
#if BUSTER_LSAN_SUPPORTED
            BUSTER_TEST(arguments, string_first_sequence(lsan_options, S8("detect_leaks=1")) != BUSTER_STRING_NO_MATCH);
            sanitizer_test_check_lsan(arguments, &result, S8("main-thread-leak"),
                                      S8("SANITIZER_CANARY kind=main-thread-leak status=returning"));
#if !BUSTER_SINGLE_THREADED
            sanitizer_test_check_lsan(arguments, &result, S8("worker-thread-leak"),
                                      S8("SANITIZER_CANARY kind=worker-thread-leak status=joined"));
#else
            arguments->show(arguments, S8("SANITIZER_CANARY kind=worker-thread-leak status=unsupported reason=single-threaded-build\n"));
#endif
#else
            arguments->show(arguments, S8("SANITIZER_CANARY kind=leak status=unsupported reason=platform-or-toolchain\n"));
#endif
        }
    }
    else if (registered_launch && policy_mode.length)
    {
        BUSTER_TEST(arguments, string_equal(policy_mode, S8("unsupported")));
        arguments->show(arguments, S8("SANITIZER_POLICY mode=unsupported reason=platform-or-configuration\n"));
    }
#else
    if (registered_launch && policy_mode.length)
    {
        BUSTER_TEST(arguments, string_equal(policy_mode, S8("unsupported")));
        arguments->show(arguments, S8("SANITIZER_POLICY mode=unsupported reason=unsanitized-or-windows\n"));
    }
#endif
    return result;
}
#endif
