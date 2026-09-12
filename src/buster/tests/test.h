#pragma once

#include <buster/lib/base.h>
#include <buster/lib/arena.h>
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
            /* The stringified expression is data, not a format string. It can */                                                                              \
            /* legitimately contain formatting syntax such as S8("{S8}"). */                                                                                 \
            buster_test_error_arguments((args), __LINE__, BUSTER_FUNCTION, S8(__FILE__), S8("{S8}"), log);                                                   \
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
#if BUSTER_INCLUDE_TESTS
    String8 memory_module;
    u64 memory_fixture_index;
    String8 memory_top_retained_fixture;
    String8 memory_top_peak_fixture;
    u64 memory_top_retained_bytes;
    u64 memory_top_peak_bytes;
    bool memory_report;
    u8 reserved[7];
#endif
};

typedef struct UnitTestResult UnitTestResult;
struct UnitTestResult
{
    u64 succeeded_test_count;
    u64 test_count;
};

#if BUSTER_INCLUDE_TESTS
// Names are static tokens. Results contain counts only; show must consume or
// copy diagnostics synchronously before a fixture's arena can be rewound.
typedef struct TestArenaMark TestArenaMark;
struct TestArenaMark
{
    Arena* arena;
    u64 start;
    u64 previous_high_water;
};

typedef struct TestArenaScope TestArenaScope;
struct TestArenaScope
{
    // Slot zero is the supplied fixture arena; remaining slots are the
    // selected context's scratch arenas. Their cursors are observed, not rewound.
    TestArenaMark marks[1 + SCRATCH_ARENA_COUNT];
    String8 name;
    u64 index;
    bool module;
    u8 reserved[7];
};

BUSTER_F_DECL TestArenaScope buster_test_arena_begin(UnitTestArguments* arguments, Arena* arena, String8 name, bool module);
BUSTER_F_DECL void buster_test_arena_end(UnitTestArguments* arguments, TestArenaScope scope, bool rewind);

// Direct calls preserve normal test control flow and avoid callback dispatch.
#define BUSTER_TEST_FIXTURE(arguments, function) \
    do \
    { \
        TestArenaScope fixture_scope_ = buster_test_arena_begin((arguments), (arguments)->arena, S8(#function), false); \
        UnitTestResult fixture_result_ = function(arguments); \
        result.test_count += fixture_result_.test_count; \
        result.succeeded_test_count += fixture_result_.succeeded_test_count; \
        buster_test_arena_end((arguments), fixture_scope_, true); \
    } while (0)
#endif

typedef UnitTestResult TestFunction(UnitTestArguments*);

BUSTER_F_DECL bool batch_test_succeeded(BatchTestResult test);
BUSTER_F_DECL bool unit_test_succeeded(UnitTestResult result);
BUSTER_F_DECL void consume_unit_tests(BatchTestResult* batch, UnitTestResult unit_test);
BUSTER_F_DECL void consume_external_tests(BatchTestResult* batch, ProcessResult result);

BUSTER_F_DECL void buster_test_error(u32 line, String8 function, String8 file_path, String8 format, ...);
BUSTER_F_DECL void buster_test_error_arguments(UnitTestArguments* arguments, u32 line, String8 function, String8 file_path, String8 format, ...);
BUSTER_F_DECL String8 buster_test_temporary_path(Arena* arena, String8 name, String8 suffix);
// Limits test-internal parallel work to a validated positive matrix quota.
BUSTER_F_DECL u64 buster_test_worker_count(u64 requested);

BUSTER_F_DECL BatchTestResult library_tests(UnitTestArguments* arguments);

BUSTER_F_DECL void default_show(UnitTestArguments* arguments, String8 format, ...);
BUSTER_F_DECL bool batch_test_report(UnitTestArguments* arguments, BatchTestResult test);
