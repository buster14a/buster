#pragma once
#include <buster/base.h>

#define BUSTER_TEST_ERROR(format, ...) buster_test_error(__LINE__, S8(__FUNCTION__), S8(__FILE__), (format), __VA_ARGS__);
#define BUSTER_STRING_TEST(args, a, b) do\
{\
    let string_a = (a);\
    let string_b = (b);\
    let success_ = string_equal(string_a, string_b);\
    if (BUSTER_UNLIKELY(!(success_)))\
    {\
        buster_test_error(__LINE__, S8(__FUNCTION__), S8(__FILE__), S8(#a "!=" #b));\
    }\
    result.succeeded_test_count += (success_);\
    result.test_count += 1;\
} while (0)

#if defined(_WIN32)
#define BUSTER_OS_STRING_TEST(args, a, b) BUSTER_STRING16_TEST(args, a, b)
#else
#define BUSTER_OS_STRING_TEST(args, a, b) BUSTER_STRING8_TEST(args, a, b)
#endif

#if BUSTER_INCLUDE_TESTS
typedef struct Arena Arena;

STRUCT(BatchTestResult)
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

typedef struct UnitTestArguments UnitTestArguments;
typedef void ShowCallback(UnitTestArguments*,String8, ...);
STRUCT(UnitTestArguments)
{
    Arena* arena;
    ShowCallback* show;
};

STRUCT(UnitTestResult)
{
    u64 succeeded_test_count;
    u64 test_count;
};

typedef UnitTestResult TestFunction(UnitTestArguments*);

BUSTER_DECL bool batch_test_succeeded(BatchTestResult test);
BUSTER_DECL bool unit_test_succeeded(UnitTestResult result);
BUSTER_DECL void consume_unit_tests(BatchTestResult* batch, UnitTestResult unit_test);
BUSTER_DECL void consume_external_tests(BatchTestResult* batch, ProcessResult result);

BUSTER_DECL void buster_test_error(u32 line, String8 function, String8 file_path, String8 format, ...);

BUSTER_DECL BatchTestResult library_tests(UnitTestArguments* arguments);

BUSTER_DECL void default_show(UnitTestArguments* arguments, String8 format, ...);
#endif
BUSTER_IMPL bool batch_test_report(UnitTestArguments* arguments, BatchTestResult test);
