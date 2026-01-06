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

#ifndef BUSTER_INCLUDE_TESTS
#define BUSTER_INCLUDE_TESTS 1
#endif

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

#define BUSTER_SLICE_SIZE(slice) ((slice).length * sizeof(*((slice).pointer)))
#define BUSTER_ARRAY_TO_SLICE(T, arr) (T) { (arr), BUSTER_ARRAY_LENGTH(arr) }

#define BUSTER_GB(x) (u64)(1024) * BUSTER_MB(x)
#define BUSTER_MB(x) (u64)(1024) * BUSTER_KB(x)
#define BUSTER_KB(x) (u64)(1024) * (x)

STRUCT(IntegerParsingU64)
{
    u64 value;
    u64 length;
};

ENUM(IntegerFormat,
    INTEGER_FORMAT_DECIMAL,
    INTEGER_FORMAT_HEXADECIMAL,
    INTEGER_FORMAT_OCTAL,
    INTEGER_FORMAT_BINARY,
);

#define BUSTER_SLICE_TO_BYTE_SLICE(s) (ByteSlice){ .pointer = (u8*)((s).pointer), .length = BUSTER_SLICE_SIZE(s) }
// #define BYTE_SLICE_TO_STRING(char_byte_count, bs) ((String ## char_byte_count) { .pointer = (char ## char_byte_count*)(bs).pointer, .length = ((bs).length / sizeof(char ## char_byte_count)) })
#define BUSTER_COMPILE_TIME_STRING_LENGTH(strlit) (BUSTER_ARRAY_LENGTH(strlit) - 1)
#define BUSTER_SLICE_START(s, start) ((typeof(s)) { (s).pointer + (start), (s).length - (start) })
#define BUSTER_STRING_NO_MATCH UINT64_MAX

#define BUSTER_SLICE_IS_ZERO_TERMINATED(s) (((s).pointer[(s).length]) == 0)

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

STRUCT(String8)
{
    char8* pointer;
    u64 length;
};

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

STRUCT(String16)
{
    char16* pointer;
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

#define is_one_or_another(i, a, b) (((i) == (a)) | ((i) == (b)))
#define is_between_range_included(i, a, b) (((i) >= (a)) & ((i) <= (b)))

#define code_point_is_space(ch) (is_one_or_another(ch, ' ', '\t') | is_one_or_another(ch, '\r', '\n'))
#define code_point_is_decimal(ch) is_between_range_included(ch, '0', '9')
#define code_point_is_octal(ch) is_between_range_included(ch, '0', '7')
#define code_point_is_binary(ch) is_one_or_another(ch, '0', '1')
#define code_point_is_hexadecimal_alpha_lower(ch) is_between_range_included(ch, 'a', 'f')
#define code_point_is_hexadecimal_alpha_upper(ch) is_between_range_included(ch, 'A', 'F')
#define code_point_is_hexadecimal_alpha(ch) (code_point_is_hexadecimal_alpha_lower(ch) | code_point_is_hexadecimal_alpha_upper(ch))
#define code_point_is_hexadecimal(ch) (code_point_is_decimal(ch) | code_point_is_hexadecimal_alpha(ch))
#define code_point_is_alpha_lower(ch) is_between_range_included(ch, 'a', 'z')
#define code_point_is_alpha_upper(ch) is_between_range_included(ch, 'A', 'Z')
#define code_point_is_identifier_start(ch) (code_point_is_alpha_upper(ch) | code_point_is_alpha_lower(ch) | ((ch) == '_'))
#define code_point_is_identifier(ch) (code_point_is_identifier_start(ch) | code_point_is_decimal(ch))

typedef struct FileDescriptor FileDescriptor;
typedef struct ProcessHandle ProcessHandle;
typedef struct ThreadHandle ThreadHandle;

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
typedef struct Arena Arena;
typedef ProcessResult ThreadEntryPoint(void);

#ifdef NDEBUG
#define BUSTER_CHECK(ok) if (BUSTER_UNLIKELY(!(ok))) UNREACHABLE()
#else
#define BUSTER_CHECK(ok) if (BUSTER_UNLIKELY(!(ok))) buster_failed_assertion(__LINE__, S8(__FUNCTION__), S8(__FILE__))
#endif

#if defined(_WIN32)
#include <buster/string16.h>
typedef String16 StringOs;
typedef wchar_t CharOs;
static_assert(sizeof(CharOs) == 2);
typedef CharOs* StringOsList;
typedef String16Slice StringOsSlice;
typedef SliceOfString16Slice SliceOfStringOsSlice;
#else
typedef String8 StringOs;
typedef char CharOs;
static_assert(sizeof(CharOs) == 1);
typedef CharOs** StringOsList;
typedef String8Slice StringOsSlice;
typedef SliceOfString8Slice SliceOfStringOsSlice;
#endif

