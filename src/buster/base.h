#pragma once

// This should be enough to achieve compilation of headers
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#ifndef BUSTER_KERNEL
#define BUSTER_KERNEL 0
#endif

#ifndef BUSTER_SINGLE_THREADED
#define BUSTER_SINGLE_THREADED 0
#endif

#ifndef BUSTER_OPTIMIZE
#define BUSTER_OPTIMIZE 0
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

#ifndef BUSTER_FUZZ
#define BUSTER_FUZZ 0
#endif

#if defined(__TINYC__)
#define BUSTER_COMPILER_TCC 1
#elif defined(__clang__)
#define BUSTER_COMPILER_CLANG 1
#elif defined(_MSC_VER)
#define BUSTER_COMPILER_MSVC 1
#elif defined(__GNUC__)
#define BUSTER_COMPILER_GCC 1
#else
#pragma error
#endif

#ifndef BUSTER_COMPILER_TCC
#define BUSTER_COMPILER_TCC 0
#endif

#ifndef BUSTER_COMPILER_CLANG
#define BUSTER_COMPILER_CLANG 0
#endif

#ifndef BUSTER_COMPILER_MSVC
#define BUSTER_COMPILER_MSVC 0
#endif

#ifndef BUSTER_COMPILER_GCC
#define BUSTER_COMPILER_GCC 0
#endif

#if (defined(_M_X64) || defined(_M_AMD64) || defined(__x86_64__)) && !defined(_M_ARM64EC)
#define BUSTER_CPU_ARCH_X86_64 1
#else
#define BUSTER_CPU_ARCH_X86_64 0
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#define BUSTER_CPU_ARCH_AARCH64 1
#else
#define BUSTER_CPU_ARCH_AARCH64 0
#endif

#if BUSTER_LINK_LIBC
#if BUSTER_SINGLE_THREADED
#define BUSTER_THREAD_LOCAL_DECL
#elif defined(__cplusplus)
#define BUSTER_THREAD_LOCAL_DECL thread_local
#elif defined(_MSC_VER)
#define BUSTER_THREAD_LOCAL_DECL __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#define BUSTER_THREAD_LOCAL_DECL __thread
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define BUSTER_THREAD_LOCAL_DECL _Thread_local
#else
#define BUSTER_THREAD_LOCAL_DECL
#endif
#endif

#if defined(__cplusplus)
#define restrict __restrict
#endif

#define BUSTER_TYPE_EQUAL(T1, T2) __is_same(T1, T2)
#define BUSTER_UNDERLYING_TYPE(E) __underlying_type(E)

#if BUSTER_OPTIMIZE
#if BUSTER_COMPILER_MSVC
#define BUSTER_INLINE __forceinline
#else
#define BUSTER_INLINE __attribute__((always_inline)) inline
#endif
#else
#define BUSTER_INLINE 
#endif

#if defined(__APPLE__)
#define BUSTER_APPLE 1
#else
#define BUSTER_APPLE 0
#endif

#ifndef BUSTER_INCLUDE_TESTS
#define BUSTER_INCLUDE_TESTS 0
#endif

#if defined(__cplusplus)
#define BUSTER_EXPORT extern "C"
#else
#define BUSTER_EXPORT
#endif

#define BUSTER_CONCAT_HELPER(a, b) a ## b
#define BUSTER_CONCAT(a, b) BUSTER_CONCAT_HELPER(a, b)
#define BUSTER_COUNTER_NAME(x) BUSTER_CONCAT(x, __COUNTER__)
#define BUSTER_STRINGIFY(x) #x

#if defined(__cplusplus)
template <typename F>
struct ScopeExit
{
    ScopeExit( F f_ ) : f( f_ ) { }
    ~ScopeExit() { f(); }
    F f;
};

struct DeferHelper
{
    template <typename F>
    ScopeExit<F> operator+(F f) { return f; }
};

#define defer [[maybe_unused]] const auto & COUNTER_NAME( DEFER_ ) = DeferHelper() + [&]()
#endif

#ifndef BUSTER_UNITY_BUILD
#define BUSTER_UNITY_BUILD 0
#endif

#if BUSTER_UNITY_BUILD
#define BUSTER_F_DECL static
#if defined __cplusplus
#define BUSTER_V_DECL "This is an error to be fixed" + 123 - 0.012312;
#else
#define BUSTER_V_DECL
#endif
#else
#define BUSTER_F_DECL
#define BUSTER_V_DECL extern
#endif

#ifndef BUSTER_USE_IO_RING
#define BUSTER_USE_IO_RING 0
#endif

#define BUSTER_PACKED __attribute__((packed))

#define BUSTER_GLOBAL_LOCAL static

#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))

#define BUSTER_OFFSET_OF(T, field) __builtin_offsetof(T, field)
#define BUSTER_FIELD_PARENT_POINTER(type, field, pointer) ((type*)((char8*)(pointer) - BUSTER_OFFSET_OF(T, field)))

#if __has_builtin(__builtin_unpredictable)
#define BUSTER_UNPREDICTABLE(cond) __builtin_unpredictable(cond)
#else
#define BUSTER_UNPREDICTABLE(cond) (cond)
#endif

#define BUSTER_SELECT(cond, a, b) (BUSTER_UNPREDICTABLE(cond) ? (a) : (b))

#if __has_builtin(__builtin_trap)
#define BUSTER_TRAP() __builtin_trap()
#elif defined(_MSC_VER)
#include <intrin.h>
#define BUSTER_TRAP() __fastfail(~0)
#elif defined(__TINYC__) && BUSTER_LINK_LIBC
#define BUSTER_TRAP() do { abort(); } while (1)
#elif BUSTER_CPU_ARCH_X86_64
#define BUSTER_TRAP() do { __asm__ __volatile__("ud2"); } while (1)
#elif BUSTER_CPU_ARCH_AARCH64
#define BUSTER_TRAP() do { __asm__ volatile("brk #0"); } while (1)
#endif

#if __has_builtin(__builtin_prefetch)
#define BUSTER_PREFETCH(pointer) __builtin_prefetch((pointer), 0 /* rw==read */, 3 /* locality */)
#elif defined(_MSC_VER)
#include <intrin.h>
#define BUSTER_PREFETCH(pointer) _mm_prefetch((pointer), _MM_HINT_T0)
#else
#define BUSTER_PREFETCH(pointer) (void)(pointer)
#endif

#if __has_builtin(__builtin_expect)
#define BUSTER_LIKELY(x) __builtin_expect(!!(x), 1)
#define BUSTER_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define BUSTER_LIKELY(x) (x)
#define BUSTER_UNLIKELY(x) (x)
#endif

#if __has_builtin(__builtin_debugtrap)
#define BUSTER_BREAKPOINT() __builtin_debugtrap()
#elif defined(_MSC_VER)
#define BUSTER_BREAKPOINT() __debugbreak()
#else
#define BUSTER_BREAKPOINT() *(int*)(void*)0 = 0
#endif

#if BUSTER_COMPILER_MSVC
#define BUSTER_UNUSED_DECL
#else
#define BUSTER_UNUSED_DECL __attribute__((unused))
#endif

#define BUSTER_UNUSED(x) ((void)(x))

#define BUSTER_MIN(a,b) (((a) < (b)) ? (a) : (b))
#define BUSTER_MAX(a,b) (((a) > (b)) ? (a) : (b))

#define BUSTER_CLAMP_TOP(a,x) BUSTER_MIN(a, x)
#define BUSTER_CLAMP_BOT(x,b) BUSTER_MAX(x, b)
#define BUSTER_CLAMP(a,x,b) (((x) < (a)) ? (a) : ((x) > (b)) ? (b) : (x))

#if defined(__APPLE__) && BUSTER_CPU_ARCH_AARCH64
#define BUSTER_CACHE_LINE_GUESS (128)
#else
#define BUSTER_CACHE_LINE_GUESS (64)
#endif

#if BUSTER_COMPILER_MSVC
#define BUSTER_RAW_UNREACHABLE() __assume(0)
#elif BUSTER_COMPILER_CLANG || BUSTER_COMPILER_GCC
#define BUSTER_RAW_UNREACHABLE() __builtin_unreachable()
#else
#define BUSTER_RAW_UNREACHABLE() BUSTER_TRAP()
#endif

#if BUSTER_COMPILER_MSVC
#define BUSTER_ASSUME(x) __assume(x)
#elif __has_builtin(__builtin_assume)
#define BUSTER_ASSUME(x) __builtin_assume(x)
#elif BUSTER_COMPILER_CLANG || BUSTER_COMPILER_GCC
#define BUSTER_ASSUME(x) do { if (!(x)) __builtin_unreachable(); } while (0)
#else
#define BUSTER_ASSUME(x) BUSTER_UNUSED(x)
#endif
#if BUSTER_OPTIMIZE
#define BUSTER_UNREACHABLE() BUSTER_RAW_UNREACHABLE()
#else
#define BUSTER_UNREACHABLE() BUSTER_TRAP()
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdalign.h>
#if BUSTER_KERNEL == 0
#include <string.h>
#include <stdlib.h>
#endif

#if defined __clang__
#define DECLARE_VECTOR(name, T, count) typedef T name __attribute__((ext_vector_type(count)))
#else
#define DECLARE_VECTOR(name, T, count) typedef struct name name; struct name { T v[(count)]; }
#endif

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#if defined(__clang__)
typedef unsigned __int128 u128;
#else
typedef struct u128 u128;
struct u128
{
    u64 v[2];
};
#endif

typedef int8_t  s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
#if defined(__clang__)
typedef signed __int128 s128;
#endif

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

#define float2_element(vector, index) (((f32*)&(vector))[(index)])
#define float4_element(vector, index) (((f32*)&(vector))[(index)])

BUSTER_GLOBAL_LOCAL BUSTER_INLINE float2 float2_make(f32 x, f32 y)
{
    float2 result = (float2){0};
    f32 elements[] = { x, y };
    memcpy(&result, elements, sizeof(result));
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE float4 float4_make(f32 x, f32 y, f32 z, f32 w)
{
    float4 result = (float4){0};
    f32 elements[] = { x, y, z, w };
    memcpy(&result, elements, sizeof(result));
    return result;
}

#define EACH_SLICE_INT(i, s) u64 i = 0; i < (s).length; i += 1
#define EACH_SLICE_REF(e, s) let & e : (s)
#define EACH_SLICE_VALUE(e, s) let e : (s)
#define EACH_ARRAY_INDEX(i, a) u64 i = 0; i < BUSTER_ARRAY_LENGTH(a); i += 1

typedef struct SliceU8 SliceU8;
struct SliceU8
{
    u8* pointer;
    u64 length;
};

typedef SliceU8 ByteSlice;

typedef struct SliceU16 SliceU16;
struct SliceU16
{
    u16* pointer;
    u64 length;

};

typedef struct SliceU32 SliceU32;
struct SliceU32
{
    u32* pointer;
    u64 length;
};

typedef struct SliceU64 SliceU64;
struct SliceU64
{
    u64* pointer;
    u64 length;
};

#define BUSTER_SLICE_SIZE(slice) ((slice).length * sizeof(*((slice).pointer)))
#define BUSTER_ARRAY_TO_SLICE(arr) { .pointer = (arr), .length = BUSTER_ARRAY_LENGTH(arr) }
#define BUSTER_ARRAY_TO_BYTE_SLICE(arr) ((ByteSlice) { .pointer = (u8*)(arr), .length = sizeof(arr) })

#define BUSTER_GB(x) (u64)(1024) * BUSTER_MB(x)
#define BUSTER_MB(x) (u64)(1024) * BUSTER_KB(x)
#define BUSTER_KB(x) (u64)(1024) * (x)

typedef struct IntegerParsingU64 IntegerParsingU64;
struct IntegerParsingU64
{
    u64 value;
    u64 length;
};

typedef enum IntegerFormat
{
    INTEGER_FORMAT_DECIMAL,
    INTEGER_FORMAT_HEXADECIMAL,
    INTEGER_FORMAT_OCTAL,
    INTEGER_FORMAT_BINARY,
    INTEGER_FORMAT_COUNT,
} IntegerFormat;

#define BUSTER_SLICE_TO_BYTE_SLICE(s) (ByteSlice){ .pointer = (u8*)((s).pointer), .length = BUSTER_SLICE_SIZE(s) }
#define BYTE_SLICE_TO_STRING(char_byte_count, bs) ((String ## char_byte_count) { .pointer = (char ## char_byte_count*)(bs).pointer, .length = ((bs).length / sizeof(char ## char_byte_count)) })
#define BUSTER_COMPILE_TIME_STRING_LENGTH(strlit) (BUSTER_ARRAY_LENGTH(strlit) - 1)
#define BUSTER_SLICE_START(s, start) ((__typeof__(s)) { (s).pointer + (start), (s).length - (start) })
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

#if BUSTER_COMPILER_MSVC
#define BUSTER_CT_CHECK(x) typedef u8 BUSTER_CONCAT(static_assert_failed_, __LINE__)[(x) ? 1 : -1]
#else
#define BUSTER_CT_CHECK(x) _Static_assert((x), "BUSTER_CT_CHECK failed")
#endif

typedef char char8;
BUSTER_CT_CHECK(sizeof(char8) == 1);
#if defined(_WIN32)
typedef wchar_t char16;
#else
typedef char16_t char16;
#endif
BUSTER_CT_CHECK(sizeof(char16) == 2);
#if defined(_WIN32)
#if defined(__TINYC__)
typedef uint32_t char32;
#else
typedef char32_t char32;
#endif
#else
typedef wchar_t char32;
#endif
BUSTER_CT_CHECK(sizeof(char32) == 4);

typedef struct String8 String8;
struct String8
{
    char8* pointer;
    u64 length;
};

typedef struct SliceString8 SliceString8;
struct SliceString8
{
    String8* pointer;
    u64 length;
};

typedef struct String16 String16;
struct String16
{
    char16* pointer;
    u64 length;
};

typedef struct SliceString16 SliceString16;
struct SliceString16
{
    String16* pointer;
    u64 length;
};

#define S8_INITIALIZER(strlit) { .pointer = (char8*)(strlit), .length = BUSTER_COMPILE_TIME_STRING_LENGTH(strlit) }
#define S16_INITIALIZER(strlit) { .pointer = (char16*)(u ## strlit), .length = BUSTER_COMPILE_TIME_STRING_LENGTH(strlit) }
#define S8(strlit) ((String8) S8_INITIALIZER(strlit))
#define S16(strlit) ((String16) S16_INITIALIZER(strlit))

// Math types and enums for UI
typedef enum Axis2
{
    AXIS2_X,
    AXIS2_Y,
    AXIS2_COUNT,
} Axis2;

typedef enum Corner
{
    CORNER_00,
    CORNER_01,
    CORNER_10,
    CORNER_11,
    CORNER_COUNT,
} Corner;

typedef union F32Interval2 F32Interval2;
union F32Interval2
{
    struct { float2 min; float2 max; };
    struct { float2 p0; float2 p1; };
    struct { f32 x0, y0, x1, y1; };
    float2 v[2];
};
BUSTER_CT_CHECK(sizeof(F32Interval2) == 4 * sizeof(f32));

#define BUSTER_IS_POWER_OF_TWO(value) ((value) && !((value) & ((value) - 1)))

typedef struct OsFileDescriptor OsFileDescriptor;
typedef struct OsProcessHandle OsProcessHandle;
typedef struct OsThreadHandle OsThreadHandle;
typedef struct OsModuleHandle OsModuleHandle;
typedef struct OsSymbol OsSymbol;
typedef struct OsBarrierHandle OsBarrierHandle;
typedef struct OsConditionVariableHandle OsConditionVariableHandle;
typedef struct OsMutexHandle OsMutexHandle;

typedef enum ProcessResult
{
    PROCESS_RESULT_SUCCESS,
    PROCESS_RESULT_FAILED,
    PROCESS_RESULT_FAILED_TRY_AGAIN,
    PROCESS_RESULT_CRASH,
    PROCESS_RESULT_NOT_EXISTENT,
    PROCESS_RESULT_RUNNING,
    PROCESS_RESULT_UNKNOWN,
    PROCESS_RESULT_COUNT,
} ProcessResult;

typedef struct Thread Thread;
typedef struct Arena Arena;
typedef ProcessResult ThreadEntryPoint(void);


#if defined(_WIN32)
typedef String16 StringOs;
typedef wchar_t CharOs;
BUSTER_CT_CHECK(sizeof(CharOs) == 2);
typedef CharOs* StringOsList;
typedef SliceString16 SliceStringOs;
#define SOs(x) S16(x)
#else
typedef String8 StringOs;
typedef char CharOs;
BUSTER_CT_CHECK(sizeof(CharOs) == 1);
typedef CharOs** StringOsList;
typedef SliceString8 SliceStringOs;
#define SOs(x) S8(x)
#endif

#define BUSTER_SLICE(p, l) (Slice<decltype(*(p))>){ .pointer = (typeof(*(p))*) (p), .length = (l) }

typedef struct TextureIndex TextureIndex;
struct TextureIndex
{
    u32 value;
};

typedef struct FontCharacter FontCharacter;
struct FontCharacter
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

typedef struct FontTextureAtlasDescription FontTextureAtlasDescription;
struct FontTextureAtlasDescription
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

typedef struct FontTextureAtlasCreate FontTextureAtlasCreate;
struct FontTextureAtlasCreate
{
    StringOs font_path;
    u32 text_height;
    u8 reserved[4];
};

typedef struct FontTextureAtlas FontTextureAtlas;
struct FontTextureAtlas
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

typedef enum OsWindowingEventKind
{
    OS_WINDOWING_EVENT_WINDOW_CLOSE,
    OS_WINDOWING_EVENT_COUNT,
} OsWindowingEventKind;

typedef struct OsWindowingEvent OsWindowingEvent;
struct OsWindowingEvent
{
    OsWindowingEvent* previous;
    OsWindowingEvent* next;
    OsWindowHandle* window;
    OsWindowingEventKind kind;
    u8 reserved[4];
};

typedef struct OsWindowingEventList OsWindowingEventList;
struct OsWindowingEventList
{
    OsWindowingEvent* first;
    OsWindowingEvent* last;
    u64 count;
};

#define FLAG_ARRAY_LENGTH(T, count) ((count) / sizeof(T) + ((count) % sizeof(T) != 0))
#define FLAG_ARRAY_GENERIC(T, N, count) T N[FLAG_ARRAY_LENGTH(T, count)]
#define FLAG_ARRAY_U64(N, E, Count) FLAG_ARRAY_GENERIC(u64, N, (u64)(Count))

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/lsan_interface.h>
#define BUSTER_LSAN_DISABLE() __lsan_disable()
#define BUSTER_LSAN_ENABLE()  __lsan_enable()
#else
#define BUSTER_LSAN_DISABLE()
#define BUSTER_LSAN_ENABLE()
#endif

typedef enum ScratchArenaId
{
    SCRATCH_ARENA_0,
    SCRATCH_ARENA_1,
    SCRATCH_ARENA_COUNT,
} ScratchArenaId;

#define EACH_ENUM_FREE(E, e) e = (E)0; e < E::Count; e = (E)((BUSTER_UNDERLYING_TYPE(E))e + 1)
#define EACH_ENUM(E, e) E EACH_ENUM_FREE(E, e)
#define EACH_ENUM_INT_FREE(E, e) e = 0; e < (BUSTER_UNDERLYING_TYPE(E))(E::Count); e += 1
#define EACH_ENUM_INT(E, e) BUSTER_UNDERLYING_TYPE(E) EACH_ENUM_INT_FREE(E, e)

typedef void ThreadReturnType;
typedef ThreadReturnType ThreadCallback(void*);

#if defined (__clang__)
#define BUSTER_FUNCTION ((String8){ .pointer = (char8*)__func__, .length = __builtin_strlen(__func__) })
#else
#define BUSTER_FUNCTION ((String8){ .pointer = (char8*)__func__, .length = strlen(__func__) })
#endif

#if BUSTER_OPTIMIZE
#define BUSTER_CHECK(ok) ((void)(BUSTER_UNLIKELY(!(ok)) ? (BUSTER_UNREACHABLE(), 0) : 0))
#else
#define BUSTER_CHECK(ok) ((void)(BUSTER_UNLIKELY(!(ok)) ? (buster_failed_assertion(__LINE__, BUSTER_FUNCTION, S8(__FILE__)), 0) : 0))
#endif

#if BUSTER_COMPILER_MSVC
#define BUSTER_NORETURN __declspec(noreturn)
#else
#define BUSTER_NORETURN __attribute__((noreturn))
#endif

#if BUSTER_COMPILER_MSVC
#define BUSTER_COLD
#else
#define BUSTER_COLD __attribute__((cold))
#endif

#if defined(__cplusplus)
#define BUSTER_ALIGN_OF(T) alignof(T)
#elif defined(_MSC_VER)
#define BUSTER_ALIGN_OF(T) __alignof(T)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define BUSTER_ALIGN_OF(T) alignof(T)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define BUSTER_ALIGN_OF(T) _Alignof(T)
#elif defined(__GNUC__) || defined(__clang__)
#define BUSTER_ALIGN_OF(T) __alignof__(T)
#else
#define BUSTER_ALIGN_OF(T) alignof(T)
#endif

BUSTER_F_DECL BUSTER_NORETURN BUSTER_COLD void buster_failed_assertion(u32 line, String8 function_name, String8 file_path);
