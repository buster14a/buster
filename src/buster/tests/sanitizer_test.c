#include <buster/tests/sanitizer_test.h>
#include <buster/tests/sanitizer_test_internal.h>

#if BUSTER_INCLUDE_TESTS
#include <buster/lib/os.h>
#include <buster/lib/string.h>

enum
{
    SANITIZER_TEST_ALLOCATION_SIZE = 4096,
};

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
    else
    {
        string_print(S8("SANITIZER_CANARY kind={S8} status=unknown\n"), mode);
    }
    return result;
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
