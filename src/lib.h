#pragma once

#ifndef BUSTER_KERNEL
#define BUSTER_KERNEL 0
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#define BUSTER_INCLUDE_TESTS 1

#if defined(__cplusplus)
#define BUSTER_EXPORT extern "C"
#else
#define BUSTER_EXPORT extern
#endif

#ifndef BUSTER_UNITY_BUILD
#define BUSTER_UNITY_BUILD 0
#endif

#if BUSTER_UNITY_BUILD
#define BUSTER_DECL static
#define BUSTER_IMPL static
#else
#define BUSTER_DECL extern
#define BUSTER_IMPL
#endif

#ifndef BUSTER_USE_IO_RING
#define BUSTER_USE_IO_RING 0
#endif

#ifndef BUSTER_USE_PTHREAD
#define BUSTER_USE_PTHREAD 0
#endif

#define BUSTER_PACKED __attribute__((packed))

#define BUSTER_LOCAL static

#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))

#define BUSTER_FIELD_PARENT_POINTER(type, field, pointer) ((type*)((char*)(pointer) - __builtin_offsetof(type, field)))

#define let __auto_type
#define STRUCT(n) typedef struct n n; struct n
#define UNION(n) typedef union n n; union n
#define BUSTER_TRAP() __builtin_trap()
#define BUSTER_BREAKPOINT() __builtin_debugtrap()
#define BUSTER_LIKELY(x) __builtin_expect(!!(x), 1)
#define BUSTER_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define BUSTER_UNUSED(x) ((void)(x))

#define BUSTER_MIN(a,b) (((a)<(b))?(a):(b))
#define BUSTER_MAX(a,b) (((a)>(b))?(a):(b))

#if __APPLE__ &&  __aarch64__
#define BUSTER_CACHE_LINE_GUESS (128)
#else
#define BUSTER_CACHE_LINE_GUESS (64)
#endif

#ifdef NDEBUG
#define BUSTER_UNREACHABLE() __builtin_unreachable()
#else
#define BUSTER_UNREACHABLE() __builtin_trap()
#endif

#define BUSTER_TEST(a, b) do\
{\
    let _b = b;\
    if (BUSTER_UNLIKELY(!_b))\
    {\
        test_error(S(#b), __LINE__, S(__FUNCTION__), S(__FILE__));\
    }\
    result = result & _b;\
} while (0)

#include <stdbool.h>
#include <stdint.h>
#include <uchar.h>

#if BUSTER_KERNEL == 0
#include <string.h>
#include <stdlib.h>
#endif

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef unsigned __int128 u128;

typedef int8_t  s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef signed __int128 s128;

typedef char16_t char16;
typedef char32_t char32;

typedef float f32;
typedef double f64;
#if defined (__SIZEOF_FLOAT128__)
typedef __float128 f128;
#endif

#ifndef BUSTER_STRING
#define BUSTER_STRING
STRUCT(String8)
{
    u8* pointer;
    u64 length;
};

STRUCT(String16)
{
    u16* pointer;
    u64 length;
};

STRUCT(String32)
{
    u32* pointer;
    u64 length;
};

typedef String8 String;
#endif

STRUCT(StringSlice)
{
    String* pointer;
    u64 length;
};

STRUCT(SliceOfStringSlice)
{
    StringSlice* pointer;
    u64 length;
};

#define S8(strlit) ((struct String8) { .pointer = (u8*)(strlit), .length = __builtin_strlen(strlit) })
#define S16(strlit) ((struct String16) { .pointer = (u ## strlit), .length = str16_length(u ## strlit) })
#define S32(strlit) ((struct String32) { .pointer = (U ## strlit), .length = str32_length(U ## strlit) })
#define S(strlit) S8(strlit)
#define BUSTER_ARRAY_TO_SLICE(T, arr) (T) { (arr), BUSTER_ARRAY_LENGTH(arr) }
#define BUSTER_STRING_ARRAY_TO_SLICE(arr) BUSTER_ARRAY_TO_SLICE(StringSlice, arr)
#define str_size(strlit) ((strlit).length * sizeof(*((strlit).pointer)))

[[noreturn]] [[gnu::cold]] BUSTER_DECL void _assert_failed(u32 line, String function_name, String file_path);
#ifdef NDEBUG
#define BUSTER_CHECK(ok) if (BUSTER_UNLIKELY(!(ok))) UNREACHABLE()
#else
#define BUSTER_CHECK(ok) if (BUSTER_UNLIKELY(!(ok))) _assert_failed(__LINE__, S(__FUNCTION__), S(__FILE__))
#endif

#define BUSTER_GB(x) 1024ull * BUSTER_MB(x)
#define BUSTER_MB(x) 1024ull * BUSTER_KB(x)
#define BUSTER_KB(x) 1024ull * (x)

typedef void ShowCallback(void*,String);

typedef struct Arena Arena;
#if BUSTER_INCLUDE_TESTS
STRUCT(TestArguments)
{
    Arena* arena;
    ShowCallback* show;
};
#endif

STRUCT(OpenFlags)
{
    u64 truncate:1;
    u64 execute:1;
    u64 write:1;
    u64 read:1;
    u64 create:1;
    u64 directory:1;
};

STRUCT(OpenPermissions)
{
    u64 read:1;
    u64 write:1;
    u64 execute:1;
};

STRUCT(Arena)
{
    u64 reserved_size;
    u64 position;
    u64 os_position;
    u64 granularity;
};

typedef enum ThreadSpawnPolicy
{
    THREAD_SPAWN_POLICY_SINGLE_THREADED,
    THREAD_SPAWN_POLICY_SPAWN_SINGLE_THREAD,
    THREAD_SPAWN_POLICY_SATURATE_LOGICAL_CORES,
    THREAD_SPAWN_POLICY_SATURATE_PHYSICAL_CORES,
} ThreadSpawnPolicy;

STRUCT(ProgramInput)
{
    int argc;
    char** argv;
    char** envp;
    ThreadSpawnPolicy thread_spawn_policy;
    u64 verbose:1;
};

STRUCT(ProgramState)
{
    ProgramInput input;
    Arena* arena;
    u64 is_debugger_present_called:1;
    u64 _is_debugger_present:1;
};

BUSTER_DECL ProgramState* program_state;

STRUCT(ArenaInitialization)
{
    u64 reserved_size;
    u64 granularity;
    u64 initial_size;
    u64 count;
};

STRUCT(FileReadOptions)
{
    u32 start_padding;
    u32 start_alignment;
    u32 end_padding;
    u32 end_alignment;
};

typedef enum IntegerFormat
{
    INTEGER_FORMAT_DECIMAL,
    INTEGER_FORMAT_HEXADECIMAL,
    INTEGER_FORMAT_OCTAL,
    INTEGER_FORMAT_BINARY,
} IntegerFormat;

STRUCT(FormatIntegerOptions)
{
    u64 value;
    IntegerFormat format;
    bool treat_as_signed;
    bool prefix;
};

typedef struct FileDescriptor FileDescriptor;
typedef struct ProcessHandle ProcessHandle;
typedef struct ThreadHandle ThreadHandle;

STRUCT(FileStats)
{
    u64 modified_time_s;
    u64 modified_time_ns;
    u64 size;
};

STRUCT(FileStatsOptions)
{
    union
    {
        u64 raw;
        struct
        {
            u64 size:1;
            u64 modified_time:1;
            u64 reserved:62;
        };
    };
};

STRUCT(ThreadCreateOptions)
{
};

#if BUSTER_KERNEL == 0
typedef
#if defined(__linux__) || defined(__APPLE__)
void*
#elif defined(_WIN32)
unsigned long
#endif
ThreadReturnType;

typedef ThreadReturnType ThreadCallback(void*);

typedef 
#ifdef _WIN32
u64
#else
u128
#endif
TimeDataType;

typedef enum TerminationKind : u8
{
    TERMINATION_KIND_UNKNOWN,
    TERMINATION_KIND_EXIT,
    TERMINATION_KIND_SIGNAL,
    TERMINATION_KIND_STOP,
} TerminationKind;

#define STREAM_COUNT (2)

STRUCT(ExecutionResult)
{
    String streams[STREAM_COUNT];
    u32 termination_code;
    TerminationKind termination_kind;
};

typedef enum StreamPolicy : u8
{
    STREAM_POLICY_INHERIT,
    STREAM_POLICY_PIPE,
    STREAM_POLICY_IGNORE,
} StreamPolicy;

STRUCT(ExecutionOptions)
{
    StreamPolicy policies[STREAM_COUNT];
    FileDescriptor* null_file_descriptor;
};
#endif

STRUCT(IntegerParsing)
{
    u64 value;
    u64 i;
};

STRUCT(ArgumentBuilder)
{
    char** argv;
    Arena* arena;
    u64 arena_offset;
};

STRUCT(ThreadInitialization)
{
};

typedef enum ProcessResult
{
    PROCESS_RESULT_SUCCESS,
    PROCESS_RESULT_FAILED,
    PROCESS_RESULT_FAILED_TRY_AGAIN,
    PROCESS_RESULT_CRASH,
    PROCESS_RESULT_NOT_EXISTENT,
    PROCESS_RESULT_RUNNING,
    PROCESS_RESULT_UNKNOWN,
} ProcessResult;

typedef struct Thread Thread;
BUSTER_DECL __thread Thread* thread;

typedef ProcessResult ThreadEntryPoint(Thread*);
BUSTER_DECL ProcessResult thread_entry_point(Thread* thread);

typedef ProcessResult ProcessArguments(Arena* arena, void* context, u64 argc, char** argv, char** envp);

// STRUCT(BusterInitialization)
// {
//     ProcessArguments* process_arguments;
//     void* context;
//     char** argv;
//     char** envp;
//     int argc;
//     ThreadSpawnPolicy thread_spawn_policy;
// };

STRUCT(IoRingCompletion)
{
    u64 user_data;
    int result;
};

STRUCT(IoRingSubmission)
{
#ifdef __linux__
    struct io_uring_sqe* sqe;
#endif
};

STRUCT(ProcessResources)
{
    struct rusage* linux_;
};

#if BUSTER_KERNEL == 0
// BUSTER_DECL ProcessResult buster_run(BusterInitialization initialization);
BUSTER_DECL Arena* arena_create(ArenaInitialization initialization);
BUSTER_DECL bool arena_destroy(Arena* arena, u64 count);
BUSTER_DECL void arena_set_position(Arena* arena, u64 position);
BUSTER_DECL void arena_reset_to_start(Arena* arena);
BUSTER_DECL void* arena_allocate_bytes(Arena* arena, u64 size, u64 alignment);
BUSTER_DECL String arena_duplicate_string(Arena* arena, String str, bool zero_terminate);
BUSTER_DECL String arena_join_string(Arena* arena, StringSlice strings, bool zero_terminate);
BUSTER_DECL void* arena_current_pointer(Arena* arena, u64 alignment);

BUSTER_DECL FileDescriptor* os_file_open(String path, OpenFlags flags, OpenPermissions permissions);
BUSTER_DECL u64 os_file_get_size(FileDescriptor* file_descriptor);
BUSTER_DECL FileStats os_file_get_stats(FileDescriptor* file_descriptor, FileStatsOptions options);
BUSTER_DECL void os_file_write(FileDescriptor* file_descriptor, String buffer);
BUSTER_DECL void os_file_read(FileDescriptor* file_descriptor, String buffer, u64 byte_count);
BUSTER_DECL bool os_file_close(FileDescriptor* file_descriptor);

#define arena_allocate(arena, T, count) (T*) arena_allocate_bytes(arena, sizeof(T) * (count), alignof(T))

BUSTER_DECL String file_read(Arena* arena, String path, FileReadOptions options);
BUSTER_DECL bool file_write(String path, String content);

BUSTER_DECL char** argument_add(ArgumentBuilder* builder, char* arg);
BUSTER_DECL void argument_builder_destroy(ArgumentBuilder* restrict builder);
BUSTER_DECL ArgumentBuilder* argument_builder_start(Arena* arena, char* s);
BUSTER_DECL char** argument_builder_end(ArgumentBuilder* restrict builder);

BUSTER_DECL TimeDataType take_timestamp();
BUSTER_DECL u64 ns_between(TimeDataType start, TimeDataType end);
BUSTER_DECL String os_path_absolute_stack(String buffer, const char* restrict relative_file_path);
BUSTER_DECL String path_absolute(Arena* arena, const char* restrict relative_file_path);
BUSTER_DECL ExecutionResult os_execute(Arena* arena, char** arguments, char** environment, ExecutionOptions options);
BUSTER_DECL FileDescriptor* os_get_stdout();
BUSTER_DECL ThreadHandle* os_thread_create(ThreadCallback* callback, ThreadCreateOptions options);
BUSTER_DECL u32 os_thread_join(ThreadHandle* handle);
#endif

BUSTER_DECL String format_integer_stack(String buffer, FormatIntegerOptions options);
BUSTER_DECL String format_integer(Arena* arena, FormatIntegerOptions options, bool zero_terminate);

BUSTER_DECL u64 parse_integer_decimal_assume_valid(String string);
BUSTER_DECL IntegerParsing parse_hexadecimal_scalar(const char* restrict p);
BUSTER_DECL IntegerParsing parse_decimal_scalar(const char* restrict p);
BUSTER_DECL IntegerParsing parse_octal_scalar(const char* restrict p);
BUSTER_DECL IntegerParsing parse_binary_scalar(const char* restrict p);
#ifdef __AVX512F__
BUSTER_DECL IntegerParsing parse_hexadecimal_vectorized(const char* restrict p);
BUSTER_DECL IntegerParsing parse_decimal_vectorized(const char* restrict p);
BUSTER_DECL IntegerParsing parse_octal_vectorized(const char* restrict p);
BUSTER_DECL IntegerParsing parse_binary_vectorized(const char* restrict p);
#endif

[[noreturn]] BUSTER_DECL void fail();

BUSTER_DECL u64 next_power_of_two(u64 n);

constexpr u64 string_no_match = UINT64_MAX;

BUSTER_DECL bool str_is_zero_terminated(String s);
BUSTER_DECL String str_from_pointers(char* start, char* end);
BUSTER_DECL String str_from_ptr_len(const char* ptr, u64 len);
BUSTER_DECL String str_from_ptr_start_end(char* ptr, u64 start, u64 end);
BUSTER_DECL String str_slice_start(String s, u64 start);
BUSTER_DECL String str_slice(String s, u64 start, u64 end);
BUSTER_DECL bool str_equal(String s1, String s2);
BUSTER_DECL u64 str_first_ch(String s, u8 ch);
BUSTER_DECL u64 str_last_ch(String s, u8 ch);

BUSTER_DECL u64 str16_length(const char16* s);
BUSTER_DECL u64 str16_size(String16 s);
BUSTER_DECL u64 str32_length(const char32* s);
BUSTER_DECL u64 str32_size(String32 s);

BUSTER_DECL bool memory_compare(void* a, void* b, u64 i);
BUSTER_DECL u64 align_forward(u64 n, u64 a);
BUSTER_DECL bool is_space(char ch);
BUSTER_DECL bool is_decimal(char ch);
BUSTER_DECL bool is_octal(char ch);
BUSTER_DECL bool is_binary(char ch);
BUSTER_DECL bool is_hexadecimal_alpha_lower(char ch);
BUSTER_DECL bool is_hexadecimal_alpha_upper(char ch);
BUSTER_DECL bool is_hexadecimal_alpha(char ch);
BUSTER_DECL bool is_hexadecimal(char ch);
BUSTER_DECL bool is_identifier_start(char ch);
BUSTER_DECL bool is_identifier(char ch);

BUSTER_DECL char* get_last_error_message();
BUSTER_DECL ProcessHandle* os_process_spawn(char* argv[], char* envp[]);
BUSTER_DECL ProcessResult os_process_wait_sync(ProcessHandle* handle, ProcessResources resources);
BUSTER_DECL ProcessResult buster_argument_process(u64 argument_count, char** argument_pointer, char** environment_pointer, u64 argument_index);

BUSTER_DECL void print(String str);

#if BUSTER_USE_IO_RING
BUSTER_DECL IoRingSubmission io_ring_prepare_open(char* path, u64 user_data);
BUSTER_DECL u32 io_ring_submit_and_wait_all();
#endif

#if BUSTER_INCLUDE_TESTS
BUSTER_DECL bool lib_tests(TestArguments* restrict arguments);
#endif
