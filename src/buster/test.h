#pragma once

#include <buster/base.h>
#include <buster/arena.h>
typedef struct BatchTestResult BatchTestResult;
struct BatchTestResult
{
    u64 succeeded_unit_test_count;
    u64 unit_test_count;
    u64 succeeded_module_test_count;
    u64 module_test_count;
    u64 external_test_count;
    u64 succeeded_external_test_count;
    ProcessResult process;
    u8 reserved[4];
};

#define BUSTER_TEST_ERROR(format, ...) buster_test_error(__LINE__, BUSTER_FUNCTION, S8(__FILE__), (format), __VA_ARGS__);
#define BUSTER_TEST_RAW(args, boolean, log)                                                                                                                    \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        bool success_ = (boolean);                                                                                                                             \
        if (BUSTER_UNLIKELY(!(success_)))                                                                                                                      \
        {                                                                                                                                                      \
            buster_test_error(__LINE__, BUSTER_FUNCTION, S8(__FILE__), log);                                                                                   \
        }                                                                                                                                                      \
        result.succeeded_test_count += (success_);                                                                                                             \
        result.test_count += 1;                                                                                                                                \
    } while (0)

#define BUSTER_TEST(args, boolean) BUSTER_TEST_RAW((args), (boolean), S8(#boolean))

#define BUSTER_STRING_TEST(args, a, b)                                                                                                                         \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        String8 string_a = (a);                                                                                                                                \
        String8 string_b = (b);                                                                                                                                \
        BUSTER_TEST_RAW((args), string_equal(string_a, string_b), S8(#a " != " #b));                                                                           \
    } while (0)

#if defined(_WIN32)
#define BUSTER_OS_STRING_TEST(args, a, b) BUSTER_STRING16_TEST(args, a, b)
#else
#define BUSTER_OS_STRING_TEST(args, a, b) BUSTER_STRING8_TEST(args, a, b)
#endif

typedef struct UnitTestArguments UnitTestArguments;
typedef void ShowCallback(UnitTestArguments*, String8, ...);
struct UnitTestArguments
{
    Arena* arena;
    ShowCallback* show;
};

typedef struct UnitTestResult UnitTestResult;
struct UnitTestResult
{
    u64 succeeded_test_count;
    u64 test_count;
};

typedef UnitTestResult TestFunction(UnitTestArguments*);

// Declared here rather than in arena.h: test.h includes arena.h, so arena.h
// cannot include test.h back for the UnitTest* types.
BUSTER_F_DECL UnitTestResult arena_tests(UnitTestArguments* arguments);
BUSTER_F_DECL UnitTestResult codegen_tests(UnitTestArguments* arguments);

BUSTER_F_DECL bool batch_test_succeeded(BatchTestResult test);
BUSTER_F_DECL bool unit_test_succeeded(UnitTestResult result);
BUSTER_F_DECL void consume_unit_tests(BatchTestResult* batch, UnitTestResult unit_test);
BUSTER_F_DECL void consume_external_tests(BatchTestResult* batch, ProcessResult result);

BUSTER_F_DECL void buster_test_error(u32 line, String8 function, String8 file_path, String8 format, ...);
BUSTER_F_DECL String8 buster_test_temporary_path(Arena* arena, String8 name, String8 suffix);

BUSTER_F_DECL BatchTestResult library_tests(UnitTestArguments* arguments);

BUSTER_F_DECL void default_show(UnitTestArguments* arguments, String8 format, ...);
BUSTER_F_DECL bool batch_test_report(UnitTestArguments* arguments, BatchTestResult test);
