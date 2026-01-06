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

#ifndef BUSTER_LINK_LIBC
#define BUSTER_LINK_LIBC 1
#endif

#ifndef BUSTER_FUZZING
#define BUSTER_FUZZING 0
#endif

#if BUSTER_LINK_LIBC
#define THREAD_LOCAL_DECL __thread
#else
#if defined(_WIN32)
#define THREAD_LOCAL_DECL
#else
#pragma error
#endif
#endif

#if BUSTER_LINK_LIBC
#define thread_local_get(x) (x)
#else
#define thread_local_get(x) TlsGetValue(x)
#endif

#if defined(__APPLE__)
#define BUSTER_APPLE 1
#else
#define BUSTER_APPLE 0
#endif

#define BUSTER_INCLUDE_TESTS 1

#if defined(__cplusplus)
#define BUSTER_EXPORT extern "C"
#else
#define BUSTER_EXPORT
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

#define BUSTER_UNPREDICTABLE(cond) __builtin_unpredictable(cond)
#define BUSTER_SELECT(cond, a, b) BUSTER_UNPREDICTABLE(cond) ? (a) : (b)

#define let __auto_type
#define STRUCT(n) typedef struct n n; struct n
#define UNION(n) typedef union n n; union n
#define BUSTER_TRAP() __builtin_trap()
#define BUSTER_BREAKPOINT() __builtin_debugtrap()
#define BUSTER_LIKELY(x) __builtin_expect(!!(x), 1)
#define BUSTER_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define BUSTER_UNUSED(x) ((void)(x))

#define BUSTER_MIN(a,b) (((a) < (b)) ? (a) : (b))
#define BUSTER_MAX(a,b) (((a) > (b)) ? (a) : (b))

#if defined(__APPLE__) &&  defined(__aarch64__)
#define BUSTER_CACHE_LINE_GUESS (128)
#else
#define BUSTER_CACHE_LINE_GUESS (64)
#endif

#define BUSTER_ASSUME(x) __builtin_assume(x)
#ifdef NDEBUG
#define BUSTER_UNREACHABLE() __builtin_unreachable()
#else
#define BUSTER_UNREACHABLE() __builtin_trap()
#endif

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

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
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

#if BUSTER_APPLE == 0
#include <uchar.h>
#else
typedef char char8_t;
typedef u16 char16_t;
typedef u32 char32_t;
#endif

#if defined(_WIN32)
typedef char char8_t;
#endif

typedef char char8;
static_assert(sizeof(char8) == 1);
#if _WIN32
typedef wchar_t char16;
#else
typedef char16_t char16;
#endif
static_assert(sizeof(char16) == 2);
#if _WIN32
typedef char32_t char32;
#else
typedef wchar_t char32;
#endif
static_assert(sizeof(char32) == 4);

typedef float f32;
typedef double f64;
#if defined (__SIZEOF_FLOAT128__)
typedef __float128 f128;
#endif

#if defined(_WIN32)
#define ENUM_START(E, T) enum E
#define ENUM_END(E, T); typedef T E
#else
#define ENUM_START(E, T) typedef enum E : T
#define ENUM_END(E, T) E
#endif
#define ENUM_T(E, T, ...) ENUM_START(E, T) { __VA_ARGS__ } ENUM_END(E, T)
#define ENUM(E, ...) typedef enum E { __VA_ARGS__ } E

STRUCT(ByteSlice)
{
    u8* pointer;
    u64 length;
};

#ifndef BUSTER_STRING
#define BUSTER_STRING
STRUCT(String8)
{
    char8* pointer;
    u64 length;
};

STRUCT(String16)
{
    char16* pointer;
    u64 length;
};

STRUCT(String32)
{
    u32* pointer;
    u64 length;
};

#if defined(_WIN32)
typedef String16 OsString;
typedef wchar_t OsChar;
#else
typedef String8 OsString;
typedef char OsChar;
#endif

#if defined(_WIN32)
typedef OsChar* OsStringList;
#else
typedef OsChar** OsStringList;
#endif

STRUCT(String8Slice)
{
    String8* pointer;
    u64 length;
};

STRUCT(SliceOfString8Slice)
{
    String8Slice* pointer;
    u64 length;
};

STRUCT(String16Slice)
{
    String16* pointer;
    u64 length;
};

STRUCT(SliceOfString16Slice)
{
    String16Slice* pointer;
    u64 length;
};

STRUCT(String32Slice)
{
    String32* pointer;
    u64 length;
};

STRUCT(SliceOfString32Slice)
{
    String32Slice* pointer;
    u64 length;
};

#if defined(_WIN32)
typedef String16Slice OsStringSlice;
typedef SliceOfString16Slice SliceOfOsStringSlice;
#else
typedef String8Slice OsStringSlice;
typedef SliceOfString8Slice SliceOfOsStringSlice;
#endif

#define compile_time_string_length(strlit) ((sizeof(strlit) / sizeof(strlit[0])) - 1)
#define string8_length(strlit) ((strlit) ? __builtin_strlen(strlit) : 0)

#define S8(strlit) ((struct String8) { .pointer = (char8*)(strlit), .length = compile_time_string_length(strlit) })
#define S16(strlit) ((struct String16) { .pointer = (char16*)(u ## strlit), .length = compile_time_string_length(u ## strlit) })
#define S32(strlit) ((struct String32) { .pointer = (char32*)(U ## strlit), .length = compile_time_string_length(U ## strlit) })

#if defined(_WIN32)
#define OsS(strlit) S16(strlit)
#define OS_STRING_DOUBLE_QUOTE "\\\""
#else
#define OsS(strlit) S8(strlit)
#define OS_STRING_DOUBLE_QUOTE "\""
#endif
#endif

#define BUSTER_ARRAY_TO_SLICE(T, arr) (T) { (arr), BUSTER_ARRAY_LENGTH(arr) }
#define string_size(strlit) ((strlit).length * sizeof(*((strlit).pointer)))

[[noreturn]] [[gnu::cold]] BUSTER_DECL void buster_failed_assertion(u32 line, String8 function_name, String8 file_path);
#ifdef NDEBUG
#define BUSTER_CHECK(ok) if (BUSTER_UNLIKELY(!(ok))) UNREACHABLE()
#else
#define BUSTER_CHECK(ok) if (BUSTER_UNLIKELY(!(ok))) buster_failed_assertion(__LINE__, S8(__FUNCTION__), S8(__FILE__))
#endif

#define BUSTER_GB(x) 1024ull * BUSTER_MB(x)
#define BUSTER_MB(x) 1024ull * BUSTER_KB(x)
#define BUSTER_KB(x) 1024ull * (x)

typedef void ShowCallback(void*,String8);

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
    u64 reserved:58;
};

STRUCT(OpenPermissions)
{
    u64 read:1;
    u64 write:1;
    u64 execute:1;
    u64 reserved:61;
};

STRUCT(Arena)
{
    u64 reserved_size;
    u64 position;
    u64 os_position;
    u64 granularity;
};

ENUM(ThreadSpawnPolicy,
    THREAD_SPAWN_POLICY_SINGLE_THREADED,
    THREAD_SPAWN_POLICY_SPAWN_SINGLE_THREAD,
    THREAD_SPAWN_POLICY_SATURATE_LOGICAL_CORES,
    THREAD_SPAWN_POLICY_SATURATE_PHYSICAL_CORES,
);

STRUCT(ProgramInput)
{
    OsStringList argv;
    OsStringList envp;
    ThreadSpawnPolicy thread_spawn_policy;
    u32 verbose:1;
    u32 reserved:31;
};

STRUCT(ProgramState)
{
    ProgramInput input;
    Arena* arena;
    u64 is_debugger_present_called:1;
    u64 _is_debugger_present:1;
    u64 reserved:62;
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

ENUM(IntegerFormat,
    INTEGER_FORMAT_DECIMAL,
    INTEGER_FORMAT_HEXADECIMAL,
    INTEGER_FORMAT_OCTAL,
    INTEGER_FORMAT_BINARY,
);

STRUCT(FormatIntegerOptions)
{
    u64 value;
    IntegerFormat format;
    bool treat_as_signed;
    bool prefix;
    u16 reserved;
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
#endif

STRUCT(IntegerParsing)
{
    u64 value;
    u64 i;
};

STRUCT(ArgumentBuilder)
{
    OsStringList argv;
    Arena* arena;
    u64 arena_offset;
};

STRUCT(ThreadInitialization)
{
};

ENUM(ProcessResult,
    PROCESS_RESULT_SUCCESS,
    PROCESS_RESULT_FAILED,
    PROCESS_RESULT_FAILED_TRY_AGAIN,
    PROCESS_RESULT_CRASH,
    PROCESS_RESULT_NOT_EXISTENT,
    PROCESS_RESULT_RUNNING,
    PROCESS_RESULT_UNKNOWN,
);

typedef struct Thread Thread;
BUSTER_DECL THREAD_LOCAL_DECL Thread* thread;

typedef ProcessResult ThreadEntryPoint(void);
BUSTER_DECL ProcessResult thread_entry_point();
BUSTER_DECL Arena* thread_arena();

typedef ProcessResult ProcessArguments(Arena* arena, void* context, u64 argc, char** argv, char** envp);

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

STRUCT(OsStringListIterator)
{
    OsStringList list;
    u64 position;
};

#if BUSTER_KERNEL == 0
// BUSTER_DECL ProcessResult buster_run(BusterInitialization initialization);
BUSTER_DECL Arena* arena_create(ArenaInitialization initialization);
BUSTER_DECL bool arena_destroy(Arena* arena, u64 count);
BUSTER_DECL void arena_set_position(Arena* arena, u64 position);
BUSTER_DECL void arena_reset_to_start(Arena* arena);
BUSTER_DECL void* arena_allocate_bytes(Arena* arena, u64 size, u64 alignment);

BUSTER_DECL String8 arena_join_string8(Arena* arena, String8Slice strings, bool zero_terminate);
BUSTER_DECL String16 arena_join_string16(Arena* arena, String16Slice strings, bool zero_terminate);
#if defined(_WIN32)
#define arena_join_os_string arena_join_string16
#else
#define arena_join_os_string arena_join_string8
#endif

BUSTER_DECL String8 arena_duplicate_string8(Arena* arena, String8 string, bool zero_terminate);
BUSTER_DECL String16 arena_duplicate_string16(Arena* arena, String16 string, bool zero_terminate);
#if defined(_WIN32)
#define arena_duplicate_os_string arena_duplicate_string16
#else
#define arena_duplicate_os_string arena_duplicate_string8
#endif

BUSTER_DECL void* arena_current_pointer(Arena* arena, u64 alignment);

BUSTER_DECL void os_make_directory(OsString path);
BUSTER_DECL FileDescriptor* os_file_open(OsString path, OpenFlags flags, OpenPermissions permissions);
BUSTER_DECL u64 os_file_get_size(FileDescriptor* file_descriptor);
BUSTER_DECL FileStats os_file_get_stats(FileDescriptor* file_descriptor, FileStatsOptions options);
BUSTER_DECL void os_file_write(FileDescriptor* file_descriptor, ByteSlice buffer);
BUSTER_DECL u64 os_file_read(FileDescriptor* file_descriptor, ByteSlice buffer, u64 byte_count);
BUSTER_DECL bool os_file_close(FileDescriptor* file_descriptor);

#define arena_allocate(arena, T, count) (T*) arena_allocate_bytes(arena, sizeof(T) * (count), alignof(T))

BUSTER_DECL ByteSlice file_read(Arena* arena, OsString path, FileReadOptions options);
BUSTER_DECL bool file_write(OsString path, ByteSlice content);

BUSTER_DECL OsStringList argument_add(ArgumentBuilder* builder, OsString arg);
BUSTER_DECL void argument_builder_destroy(ArgumentBuilder* restrict builder);
BUSTER_DECL ArgumentBuilder* argument_builder_start(Arena* arena, OsString s);
BUSTER_DECL OsStringList argument_builder_end(ArgumentBuilder* restrict builder);

BUSTER_DECL TimeDataType take_timestamp();
BUSTER_DECL u64 ns_between(TimeDataType start, TimeDataType end);
BUSTER_DECL OsString os_path_absolute_stack(OsString buffer, OsString relative_file_path);
BUSTER_DECL OsString path_absolute(Arena* arena, OsString relative_file_path);
BUSTER_DECL FileDescriptor* os_get_stdout();
BUSTER_DECL ThreadHandle* os_thread_create(ThreadCallback* callback, ThreadCreateOptions options);
BUSTER_DECL u32 os_thread_join(ThreadHandle* handle);
#endif

BUSTER_DECL String8 arena_string8_format(Arena* arena, bool null_terminate, String8 format, ...);
BUSTER_DECL String8 string8_format_integer_stack(String8 buffer, FormatIntegerOptions options);
BUSTER_DECL String8 string8_format_integer(Arena* arena, FormatIntegerOptions options, bool zero_terminate);

#if defined(_WIN32)
#define arena_os_string_format(arena, format, ...) arena_string16_format(arena, true, format, __VA_ARGS__)
#else
#define arena_os_string_format(arena, format, ...) arena_string8_format((arena), true, (format), __VA_ARGS__)
#endif

BUSTER_DECL u64 parse_integer_decimal_assume_valid(String8 string);
BUSTER_DECL IntegerParsing string8_parse_hexadecimal(const char* restrict p);
BUSTER_DECL IntegerParsing string8_parse_decimal(const char* restrict p);
BUSTER_DECL IntegerParsing string8_parse_octal(const char* restrict p);
BUSTER_DECL IntegerParsing string8_parse_binary(const char* restrict p);
BUSTER_DECL IntegerParsing string16_parse_hexadecimal(const char16* restrict p);
BUSTER_DECL IntegerParsing string16_parse_decimal(const char16* restrict p);
BUSTER_DECL IntegerParsing string16_parse_octal(const char16* restrict p);
BUSTER_DECL IntegerParsing string16_parse_binary(const char16* restrict p);
#ifdef __AVX512F__
BUSTER_DECL IntegerParsing parse_hexadecimal_vectorized(const char* restrict p);
BUSTER_DECL IntegerParsing parse_decimal_vectorized(const char* restrict p);
BUSTER_DECL IntegerParsing parse_octal_vectorized(const char* restrict p);
BUSTER_DECL IntegerParsing parse_binary_vectorized(const char* restrict p);
#endif

[[noreturn]] BUSTER_DECL void fail();

BUSTER_DECL u64 next_power_of_two(u64 n);

constexpr u64 string_no_match = UINT64_MAX;

#define string_is_zero_terminated(s) (((s).pointer[(s).length]) == 0)

#define string_slice_start(s, start) ((typeof(s)) { (s).pointer + (start), (s).length - (start) })

BUSTER_DECL String8 string8_from_pointer(const char8* start);
BUSTER_DECL String8 string8_from_pointers(const char8* start, const char8* end);
BUSTER_DECL String8 string8_from_pointer_length(const char* pointer, u64 len);
BUSTER_DECL String8 string8_from_pointer_start_end(const char8* pointer, u64 start, u64 end);
BUSTER_DECL String8 string8_slice(String8 s, u64 start, u64 end);
BUSTER_DECL bool string8_equal(String8 s1, String8 s2);
BUSTER_DECL u64 string8_first_character(String8 s, char8 ch);
BUSTER_DECL u64 string8_last_character(String8 s, char8 ch);
BUSTER_DECL u64 string8_first_ocurrence(String8 s, String8 sub);
BUSTER_DECL bool string8_starts_with(String8 s, String8 beginning);
BUSTER_DECL bool string8_ends_with(String8 s, String8 ending);
BUSTER_DECL u64 string8_occurrence_count(String8 s, u8 ch);

BUSTER_DECL String16 string16_from_pointer(const char16* start);
BUSTER_DECL String16 string16_from_pointers(const char16* start, const char16* end);
BUSTER_DECL String16 string16_from_pointer_length(const char16* pointer, u64 len);
BUSTER_DECL String16 string16_from_pointer_start_end(const char16* pointer, u64 start, u64 end);
BUSTER_DECL String16 string16_slice(String16 s, u64 start, u64 end);
BUSTER_DECL bool string16_equal(String16 s1, String16 s2);
BUSTER_DECL u64 string16_first_character(String16 s, char16 ch);
BUSTER_DECL u64 string16_last_character(String16 s, char16 ch);
BUSTER_DECL u64 string16_first_ocurrence(String16 s, String16 sub);
BUSTER_DECL bool string16_starts_with(String16 s, String16 beginning);
BUSTER_DECL bool string16_ends_with(String16 s, String16 ending);

BUSTER_DECL u64 string16_length(const char16* s);
BUSTER_DECL u64 string32_length(const char32* s);

BUSTER_DECL String8 string16_to_string8(Arena* arena, String16 s);

#define os_string_to_c(s) (OsChar*)((s).pointer)
#if defined(_WIN32)
#define os_string_from_pointer(pointer) string16_from_pointer(pointer)
#define os_string_first_character string16_first_character
#define os_string_to_string8(a, s) string16_to_string8(a, s)
#define os_string_equal(a, b) string16_equal(a, b)
#define os_string_starts_with(a, b) string16_starts_with(a, b)
#define os_string_from_pointer_length(pointer, length) string16_from_pointer_length(pointer, length)
#define os_string_first_ocurrence(s, sub) string16_first_ocurrence(s, sub)
#define os_string_parse_hexadecimal(p) string16_parse_hexadecimal(p)
#define os_string_parse_decimal(p) string16_parse_decimal(p)
#define os_string_parse_octal(p) string16_parse_octal(p)
#define os_string_parse_binary(p) string16_parse_binary(p)
#else
#define os_string_from_pointer(pointer) string8_from_pointer(pointer)
#define os_string_first_character string8_first_character
#define os_string_to_string8(a, s) s
#define os_string_equal(a, b) string8_equal(a, b)
#define os_string_starts_with(a, b) string8_starts_with(a, b)
#define os_string_from_pointer_length(pointer, length) string8_from_pointer_length(pointer, length)
#define os_string_first_ocurrence(s, sub) string8_first_ocurrence(s, sub)
#define os_string_parse_hexadecimal(p) string8_parse_hexadecimal(p)
#define os_string_parse_decimal(p) string8_parse_decimal(p)
#define os_string_parse_octal(p) string8_parse_octal(p)
#define os_string_parse_binary(p) string8_parse_binary(p)
#endif

#if defined(_WIN32)
#define os_string_length(s) string16_length(s)
#else
#define os_string_length(s) string8_length(s)
#endif

#define string_to_byte_slice(s) (ByteSlice){ .pointer = (u8*)((s).pointer), .length = string_size(s) }
#define byte_slice_to_string(char_byte_count, bs) ((String ## char_byte_count) { .pointer = (char ## char_byte_count*)(bs).pointer, .length = ((bs).length / sizeof(char ## char_byte_count)) })

BUSTER_DECL bool memory_compare(void* a, void* b, u64 i);
BUSTER_DECL u64 align_forward(u64 n, u64 a);

#define is_one_or_another(i, a, b) (((i) == (a)) | ((i) == (b)))
#define is_between_range_included(i, a, b) (((i) >= (a)) & ((i) <= (b)))

#define character_is_space(ch) (is_one_or_another(ch, ' ', '\t') | is_one_or_another(ch, '\r', '\n'))
#define character_is_decimal(ch) is_between_range_included(ch, '0', '9')
#define character_is_octal(ch) is_between_range_included(ch, '0', '7')
#define character_is_binary(ch) is_one_or_another(ch, '0', '1')
#define character_is_hexadecimal_alpha_lower(ch) is_between_range_included(ch, 'a', 'f')
#define character_is_hexadecimal_alpha_upper(ch) is_between_range_included(ch, 'A', 'F')
#define character_is_hexadecimal_alpha(ch) (character_is_hexadecimal_alpha_lower(ch) | character_is_hexadecimal_alpha_upper(ch))
#define character_is_hexadecimal(ch) (character_is_decimal(ch) | character_is_hexadecimal_alpha(ch))
#define character_is_alpha_lower(ch) is_between_range_included(ch, 'a', 'z')
#define character_is_alpha_upper(ch) is_between_range_included(ch, 'A', 'Z')
#define character_is_identifier_start(ch) (character_is_alpha_upper(ch) | character_is_alpha_lower(ch) | ((ch) == '_'))
#define character_is_identifier(ch) (character_is_identifier_start(ch) | character_is_decimal(ch))

BUSTER_DECL OsString get_last_error_message(Arena* arena);
BUSTER_DECL ProcessHandle* os_process_spawn(OsChar* name, OsStringList argv, OsStringList envp);
BUSTER_DECL ProcessResult os_process_wait_sync(ProcessHandle* handle, ProcessResources resources);
BUSTER_DECL ProcessResult buster_argument_process(OsStringList argument_pointer, OsStringList environment_pointer, u64 argument_index, OsString argument);
BUSTER_DECL OsString os_get_environment_variable(OsString variable);

BUSTER_DECL OsStringList os_string_list_create(Arena* arena, OsStringSlice arguments);
BUSTER_DECL OsStringListIterator os_string_list_iterator_initialize(OsStringList list);
BUSTER_DECL OsString os_string_list_iterator_next(OsStringListIterator* iterator);

BUSTER_DECL void print_raw(String8 str);
BUSTER_DECL void print(String8 str, ...);

STRUCT(CopyFileArguments)
{
    OsString original_path;
    OsString new_path;
};
BUSTER_DECL bool copy_file(CopyFileArguments arguments);

[[gnu::noreturn]] BUSTER_DECL void os_exit(u32 code);
BUSTER_DECL bool os_initialize_time();

#if BUSTER_USE_IO_RING
BUSTER_DECL IoRingSubmission io_ring_prepare_open(char* path, u64 user_data);
BUSTER_DECL u32 io_ring_submit_and_wait_all();
#endif

#if BUSTER_INCLUDE_TESTS
BUSTER_DECL bool lib_tests(TestArguments* restrict arguments);
#endif

#if BUSTER_UNITY_BUILD
#include <buster/lib.c>
#endif
