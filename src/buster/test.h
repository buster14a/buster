#pragma once
#include <buster/base.h>

#define BUSTER_TEST_ERROR(format, ...) buster_test_error(__LINE__, S8(__FUNCTION__), S8(__FILE__), (format), __VA_ARGS__);
#define BUSTER_STRING8_TEST(args, a, b) do\
{\
    let string_a = (a);\
    let string_b = (b);\
    let _b = string8_equal(string_a, string_b);\
    if (BUSTER_UNLIKELY(!(_b)))\
    {\
        buster_test_error(__LINE__, S8(__FUNCTION__), S8(__FILE__), S8(#a "!=" #b));\
    }\
    result = result & (_b);\
} while (0)
#define BUSTER_STRING16_TEST(args, a, b) do\
{\
    let string_a = (a);\
    let string_b = (b);\
    let _b = string16_equal(string_a, string_b);\
    if (BUSTER_UNLIKELY(!(_b)))\
    {\
        buster_test_error(__LINE__, S8(__FUNCTION__), S8(__FILE__), S8(#a "!=" #b));\
    }\
    result = result & (_b);\
} while (0)

#if defined(_WIN32)
#define BUSTER_OS_STRING_TEST(args, a, b) BUSTER_STRING16_TEST(args, a, b)
#else
#define BUSTER_OS_STRING_TEST(args, a, b) BUSTER_STRING8_TEST(args, a, b)
#endif

#if BUSTER_INCLUDE_TESTS
typedef void ShowCallback(void*,String8);

typedef struct Arena Arena;
STRUCT(TestArguments)
{
    Arena* arena;
    ShowCallback* show;
};
#endif

