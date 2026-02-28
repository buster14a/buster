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
#define BUSTER_THREAD_LOCAL_DECL __thread
#else
#if defined(_WIN32)
#define BUSTER_THREAD_LOCAL_DECL
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

#define BUSTER_GLOBAL_LOCAL static

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

#define DECLARE_VECTOR(name, T, count) typedef T name __attribute__((ext_vector_type(count)))

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

typedef unsigned int uint;

DECLARE_VECTOR(uint2, uint, 2);
DECLARE_VECTOR(uint3, uint, 3);
DECLARE_VECTOR(uint4, uint, 4);
DECLARE_VECTOR(uint8, uint, 8);
DECLARE_VECTOR(uint16, uint, 16);

typedef float f32;
typedef double f64;

DECLARE_VECTOR(float2, f32, 2);
DECLARE_VECTOR(float3, f32, 3);
DECLARE_VECTOR(float4, f32, 4);
#if defined (__SIZEOF_FLOAT128__)
typedef __float128 f128;
#endif

typedef float2 vec2;
typedef float3 vec3;
typedef float4 vec4;

#if defined(_WIN32)
#define ENUM_START(E, T) enum E
#define ENUM_END(E, T); typedef T E
#else
#define ENUM_START(E, T) typedef enum E : T
#define ENUM_END(E, T) E
#endif
#define ENUM_T(E, T, ...) ENUM_START(E, T) { __VA_ARGS__ } ENUM_END(E, T)
#define ENUM(E, ...) typedef enum E { __VA_ARGS__ } E

#define SLICE(name, T) STRUCT(name) { T* pointer; u64 length; }

SLICE(u8Slice,  u8);
SLICE(u16Slice, u16);
SLICE(u32Slice, u32);
SLICE(u64Slice, u64);

SLICE(s8Slice,  s8);
SLICE(s16Slice, s16);
SLICE(s32Slice, s32);
SLICE(s64Slice, s64);

typedef u8Slice ByteSlice;

#define BUSTER_SLICE_SIZE(slice) ((slice).length * sizeof(*((slice).pointer)))
#define BUSTER_ARRAY_TO_SLICE(arr) { (arr), BUSTER_ARRAY_LENGTH(arr) }
#define BUSTER_ARRAY_TO_BYTE_SLICE(arr) ((ByteSlice) { .pointer = (u8*)(arr), .length = sizeof(arr) })

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
#define BYTE_SLICE_TO_STRING(char_byte_count, bs) ((String ## char_byte_count) { .pointer = (char ## char_byte_count*)(bs).pointer, .length = ((bs).length / sizeof(char ## char_byte_count)) })
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
#if defined(_WIN32)
typedef wchar_t char16;
#else
typedef char16_t char16;
#endif
static_assert(sizeof(char16) == 2);
#if defined(_WIN32)
typedef char32_t char32;
#else
typedef wchar_t char32;
#endif
static_assert(sizeof(char32) == 4);

SLICE(String8, char8);
SLICE(String8Slice, String8);
SLICE(SliceOfString8Slice, String8Slice);

SLICE(String16, char16);
SLICE(String16Slice, String16);
SLICE(SliceOfString16Slice, String16Slice);

SLICE(float4Slice, float4);

// Math types and enums for UI
ENUM(Axis2,
    AXIS2_X,
    AXIS2_Y,
    AXIS2_COUNT,
);

ENUM(Corner,
    CORNER_00,
    CORNER_01,
    CORNER_10,
    CORNER_11,
    CORNER_COUNT,
);

UNION(F32Interval2)
{
    struct { float2 min; float2 max; };
    struct { float2 p0; float2 p1; };
    struct { f32 x0, y0, x1, y1; };
    float2 v[2];
};
static_assert(sizeof(F32Interval2) == 4 * sizeof(f32));

#define BUSTER_CLAMP(a, x, b) (((a) > (x)) ? (a) : ((b) < (x)) ? (b) : (x))

static inline bool is_power_of_two(u64 value)
{
    return value && !(value & (value - 1));
}

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

typedef struct OsFileDescriptor OsFileDescriptor;
typedef struct OsProcessHandle OsProcessHandle;
typedef struct OsThreadHandle OsThreadHandle;
typedef struct OsModule OsModule;
typedef struct OsSymbol OsSymbol;

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

#if defined(_WIN32)
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

STRUCT(TextureIndex)
{
    u32 value;
};

STRUCT(FontCharacter)
{
    u32 advance;
    u32 left_bearing;
    u32 x;
    u32 y;
    u32 width;
    u32 height;
    s32 x_offset;
    s32 y_offset;
};

STRUCT(FontTextureAtlasDescription)
{
    u32* pointer;
    FontCharacter* characters;
    s32* kerning_tables;
    u32 width;
    u32 height;
    s32 ascent;
    s32 descent;
    s32 line_gap;
    u8 reserved[4];
};

STRUCT(FontTextureAtlasCreate)
{
    StringOs font_path;
    u32 text_height;
    u8 reserved[4];
};

STRUCT(FontTextureAtlas)
{
    FontTextureAtlasDescription description;
    TextureIndex texture;
    u8 reserved[4];
};

// typedef enum OS_EventKind
// {
//   OS_EventKind_Null,
//   OS_EventKind_Press,
//   OS_EventKind_Release,
//   OS_EventKind_MouseMove,
//   OS_EventKind_Text,
//   OS_EventKind_Scroll,
//   OS_EventKind_WindowLoseFocus,
//   OS_EventKind_WindowClose,
//   OS_EventKind_FileDrop,
//   OS_EventKind_Wakeup,
//   OS_EventKind_COUNT
// }
// OS_EventKind;
//
// typedef U32 OS_Modifiers;
// enum
// {
//   OS_Modifier_Ctrl  = (1<<0),
//   OS_Modifier_Shift = (1<<1),
//   OS_Modifier_Alt   = (1<<2),
// };
//
// typedef struct OS_Event OS_Event;
// struct OS_Event
// {
//   OS_Event *next;
//   OS_Event *prev;
//   U64 timestamp_us;
//   OS_Handle window;
//   OS_EventKind kind;
//   OS_Modifiers modifiers;
//   OS_Key key;
//   B32 is_repeat;
//   B32 right_sided;
//   U32 character;
//   U32 repeat_count;
//   Vec2F32 pos;
//   Vec2F32 delta;
//   String8List strings;
// };

typedef struct OsWindowHandle OsWindowHandle;

ENUM(OsWindowingEventKind,
    OS_WINDOWING_EVENT_WINDOW_CLOSE,
);

STRUCT(OsWindowingEvent)
{
    OsWindowingEvent* previous;
    OsWindowingEvent* next;
    OsWindowHandle* window;
    OsWindowingEventKind kind;
    u8 reserved[4];
};

STRUCT(OsWindowingEventList)
{
    OsWindowingEvent* first;
    OsWindowingEvent* last;
    u64 count;
};

#define FLAG_ARRAY_LENGTH(T, count) ((count) / sizeof(T) + ((count) % sizeof(T) != 0))
#define FLAG_ARRAY_GENERIC(T, N, count) T N[FLAG_ARRAY_LENGTH(T, count)]
#define FLAG_ARRAY_U64(N, count) FLAG_ARRAY_GENERIC(u64, N, (count))

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/lsan_interface.h>
#define BUSTER_LSAN_DISABLE() __lsan_disable()
#define BUSTER_LSAN_ENABLE()  __lsan_enable()
#else
#define BUSTER_LSAN_DISABLE()
#define BUSTER_LSAN_ENABLE()
#endif

