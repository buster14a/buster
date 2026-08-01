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

#ifndef BUSTER_SANITIZE
#define BUSTER_SANITIZE 0
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif

#if defined(_WIN32)
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef UNICODE
#define UNICODE
#endif
#endif

#ifndef BUSTER_LINK_LIBC
#define BUSTER_LINK_LIBC 1
#endif

#ifndef BUSTER_FUZZ_AVAILABLE
#define BUSTER_FUZZ_AVAILABLE 0
#endif

#if defined(__ANDROID__)
#define BUSTER_ANDROID 1
#else
#define BUSTER_ANDROID 0
#endif

#if defined(__linux__) && !defined(__ANDROID__)
#define BUSTER_LINUX 1
#else
#define BUSTER_LINUX 0
#endif

#if defined(_WIN32)
#define BUSTER_WINDOWS 1
#else
#define BUSTER_WINDOWS 0
#endif

#if defined(__APPLE__)
#define BUSTER_APPLE 1
#if defined(__TINYC__) || defined(__BUSTER_TARGET_MACOS__)
// TCC and the buster macOS target do not preprocess TargetConditionals.h,
// which relies on clang __is_target_* builtins.
#define BUSTER_IOS 0
#define BUSTER_MACOS 1
#define BUSTER_IOS_SIMULATOR 0
#else
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
#define BUSTER_IOS 1
#define BUSTER_MACOS 0
#if TARGET_OS_SIMULATOR
#define BUSTER_IOS_SIMULATOR 1
#else
#define BUSTER_IOS_SIMULATOR 0
#endif
#else
#define BUSTER_IOS 0
#define BUSTER_MACOS 1
#define BUSTER_IOS_SIMULATOR 0
#endif
#endif
#else
#define BUSTER_APPLE 0
#define BUSTER_IOS 0
#define BUSTER_MACOS 0
#define BUSTER_IOS_SIMULATOR 0
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
#error unsupported compiler
#endif

#ifndef BUSTER_COMPILER_TCC
#define BUSTER_COMPILER_TCC 0
#endif

#ifndef BUSTER_COMPILER_CLANG
#define BUSTER_COMPILER_CLANG 0
#endif

#ifndef BUSTER_USE_VULKAN
#define BUSTER_USE_VULKAN 0
#endif

#ifndef BUSTER_USE_D3D12
#define BUSTER_USE_D3D12 0
#endif

#ifndef BUSTER_USE_METAL
#define BUSTER_USE_METAL 0
#endif

#ifndef BUSTER_USE_GRAPHICS
#define BUSTER_USE_GRAPHICS (BUSTER_USE_VULKAN || BUSTER_USE_D3D12 || BUSTER_USE_METAL)
#endif

#ifndef BUSTER_COMPILER_ZIG
#define BUSTER_COMPILER_ZIG 0
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

#ifndef BUSTER_INCLUDE_TESTS
#define BUSTER_INCLUDE_TESTS 0
#endif

#if defined(__cplusplus)
#define BUSTER_EXPORT extern "C"
#else
#define BUSTER_EXPORT
#endif

#define BUSTER_CONCAT_HELPER(a, b) a##b
#define BUSTER_CONCAT(a, b) BUSTER_CONCAT_HELPER(a, b)
#define BUSTER_COUNTER_NAME(x) BUSTER_CONCAT(x, __COUNTER__)
#define BUSTER_STRINGIFY(x) #x

#ifndef BUSTER_UNITY_BUILD
#define BUSTER_UNITY_BUILD 0
#endif

#if BUSTER_UNITY_BUILD
#define BUSTER_F_DECL static
#if defined __cplusplus
#define BUSTER_V_DECL "This is an error to be fixed" + 123 - 0.012312;
#else
#define BUSTER_V_DECL static
#define BUSTER_V_IMPL static
#endif
#else
#define BUSTER_F_DECL
#define BUSTER_V_DECL extern
#define BUSTER_V_IMPL
#endif

#define BUSTER_PACKED __attribute__((packed))

#define BUSTER_GLOBAL_LOCAL static

#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))

#if BUSTER_COMPILER_MSVC
#include <stddef.h>
#define BUSTER_OFFSET_OF(T, field) offsetof(T, field)
#else
#define BUSTER_OFFSET_OF(T, field) __builtin_offsetof(T, field)
#endif
#define BUSTER_FIELD_PARENT_POINTER(type, field, pointer) ((type*)((char8*)(pointer) - BUSTER_OFFSET_OF(type, field)))

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
#define BUSTER_TRAP()                                                                                                                                          \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        abort();                                                                                                                                               \
    } while (1)
#elif BUSTER_CPU_ARCH_X86_64
#define BUSTER_TRAP()                                                                                                                                          \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        __asm__ __volatile__("ud2");                                                                                                                           \
    } while (1)
#elif BUSTER_CPU_ARCH_AARCH64
#define BUSTER_TRAP()                                                                                                                                          \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        __asm__ volatile("brk #0");                                                                                                                            \
    } while (1)
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

#define BUSTER_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define BUSTER_MAX(a, b) (((a) > (b)) ? (a) : (b))

#define BUSTER_CLAMP_TOP(a, x) BUSTER_MIN(a, x)
#define BUSTER_CLAMP_BOT(x, b) BUSTER_MAX(x, b)
#define BUSTER_CLAMP(a, x, b) (((x) < (a)) ? (a) : ((x) > (b)) ? (b) : (x))

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
#define BUSTER_ASSUME(x)                                                                                                                                       \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        if (!(x))                                                                                                                                              \
            __builtin_unreachable();                                                                                                                           \
    } while (0)
#else
#define BUSTER_ASSUME(x) BUSTER_UNUSED(x)
#endif
#if BUSTER_OPTIMIZE
#define BUSTER_UNREACHABLE() BUSTER_RAW_UNREACHABLE()
#else
#define BUSTER_UNREACHABLE()                                                                                                                                   \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        BUSTER_TRAP();                                                                                                                                         \
        BUSTER_RAW_UNREACHABLE();                                                                                                                              \
    } while (0)
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdalign.h>
#if BUSTER_KERNEL == 0
#include <string.h>
#include <stdlib.h>
#endif

#define memory_compare(a, b, count) (memcmp((a), (b), (count)) == 0)

#if defined __clang__
#define DECLARE_VECTOR(name, T, count) typedef T name __attribute__((ext_vector_type(count)))
#else
#define DECLARE_VECTOR(name, T, count)                                                                                                                         \
    typedef struct name name;                                                                                                                                  \
    struct name                                                                                                                                                \
    {                                                                                                                                                          \
        T v[(count)];                                                                                                                                          \
    }
#endif

typedef uint8_t u8;
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

typedef int8_t s8;
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
#if defined(__SIZEOF_FLOAT128__)
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
    f32 elements[] = {x, y};
    memcpy(&result, elements, sizeof(result));
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE float4 float4_make(f32 x, f32 y, f32 z, f32 w)
{
    float4 result = (float4){0};
    f32 elements[] = {x, y, z, w};
    memcpy(&result, elements, sizeof(result));
    return result;
}

#define EACH_SLICE_INT(i, s)                                                                                                                                   \
    u64 i = 0;                                                                                                                                                 \
    i < (s).length;                                                                                                                                            \
    i += 1
#define EACH_ARRAY_INDEX(i, a)                                                                                                                                 \
    u64 i = 0;                                                                                                                                                 \
    i < BUSTER_ARRAY_LENGTH(a);                                                                                                                                \
    i += 1

typedef struct Sliceu8 Sliceu8;
struct Sliceu8
{
    u8* pointer;
    u64 length;
};

typedef Sliceu8 ByteSlice;

typedef struct Sliceu16 Sliceu16;
struct Sliceu16
{
    u16* pointer;
    u64 length;
};

typedef struct Sliceu32 Sliceu32;
struct Sliceu32
{
    u32* pointer;
    u64 length;
};

typedef struct Sliceu64 Sliceu64;
struct Sliceu64
{
    u64* pointer;
    u64 length;
};

#define BUSTER_SLICE_SIZE(slice) ((slice).length * sizeof(*((slice).pointer)))
#define BUSTER_ARRAY_TO_SLICE(arr) {.pointer = (arr), .length = BUSTER_ARRAY_LENGTH(arr)}
#define BUSTER_ARRAY_TO_BYTE_SLICE(arr) ((ByteSlice){.pointer = (u8*)(arr), .length = sizeof(arr)})

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

#define BUSTER_SLICE_TO_BYTE_SLICE(s)                                                                                                                          \
    (ByteSlice)                                                                                                                                                \
    {                                                                                                                                                          \
        .pointer = (u8*)((s).pointer), .length = BUSTER_SLICE_SIZE(s)                                                                                          \
    }
#define BYTE_SLICE_TO_STRING(char_byte_count, bs)                                                                                                              \
    ((String##char_byte_count){.pointer = (char##char_byte_count*)(bs).pointer, .length = ((bs).length / sizeof(char##char_byte_count))})
#define BUSTER_COMPILE_TIME_STRING_LENGTH(strlit) (BUSTER_ARRAY_LENGTH(strlit) - 1)
#define BUSTER_SLICE_START(s, start) ((__typeof__(s)){(s).pointer + (start), (s).length - (start)})
#define BUSTER_STRING_NO_MATCH UINT64_MAX

#define BUSTER_SLICE_IS_ZERO_TERMINATED(s) (((s).pointer[(s).length]) == 0)

#if BUSTER_APPLE == 0
#include <uchar.h>
#else
typedef char char8_t;
typedef u16 char16_t;
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

#define S8_INITIALIZER(strlit) {.pointer = (char8*)(strlit), .length = BUSTER_COMPILE_TIME_STRING_LENGTH(strlit)}
#define S8(strlit) ((String8)S8_INITIALIZER(strlit))

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
    struct
    {
        float2 min;
        float2 max;
    };
    struct
    {
        float2 p0;
        float2 p1;
    };
    struct
    {
        f32 x0, y0, x1, y1;
    };
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
typedef wchar_t WindowsChar;
#else
typedef char16 WindowsChar;
#endif
BUSTER_CT_CHECK(sizeof(WindowsChar) == 2);

typedef char8 PosixChar;
BUSTER_CT_CHECK(sizeof(PosixChar) == 1);

#if defined(_WIN32)
typedef WindowsChar CharOs;
#else
typedef PosixChar CharOs;
#endif

typedef PosixChar** PosixStringList;
typedef WindowsChar* WindowsStringList;

#if defined(_WIN32)
typedef WindowsStringList StringOsList;
#else
typedef PosixStringList StringOsList;
#endif

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
    String8 font_path;
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

#define FLAG_ARRAY_LENGTH(T, count) ((count) / (sizeof(T) * 8) + ((count) % (sizeof(T) * 8) != 0))
#define FLAG_ARRAY_GENERIC(T, N, count) T N[FLAG_ARRAY_LENGTH(T, count)]
#define FLAG_ARRAY_U64(N, E, Count) FLAG_ARRAY_GENERIC(u64, N, (u64)(Count))

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/lsan_interface.h>
#define BUSTER_LSAN_DISABLE() __lsan_disable()
#define BUSTER_LSAN_ENABLE() __lsan_enable()
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

#define EACH_ENUM_FREE(E, e)                                                                                                                                   \
    e = (E)0;                                                                                                                                                  \
    e < E::Count;                                                                                                                                              \
    e = (E)((BUSTER_UNDERLYING_TYPE(E))e + 1)
#define EACH_ENUM(E, e) E EACH_ENUM_FREE(E, e)
#define EACH_ENUM_INT_FREE(E, e)                                                                                                                               \
    e = 0;                                                                                                                                                     \
    e < (BUSTER_UNDERLYING_TYPE(E))(E::Count);                                                                                                                 \
    e += 1
#define EACH_ENUM_INT(E, e) BUSTER_UNDERLYING_TYPE(E) EACH_ENUM_INT_FREE(E, e)

typedef void ThreadReturnType;
typedef ThreadReturnType ThreadCallback(void*);

#if defined(__clang__)
#define BUSTER_FUNCTION ((String8){.pointer = (char8*)__func__, .length = __builtin_strlen(__func__)})
#else
#define BUSTER_FUNCTION ((String8){.pointer = (char8*)__func__, .length = strlen(__func__)})
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

#define BUSTER_SWITCH_ALPHA_UPPER                                                                                                                              \
    case 'A':                                                                                                                                                  \
    case 'B':                                                                                                                                                  \
    case 'C':                                                                                                                                                  \
    case 'D':                                                                                                                                                  \
    case 'E':                                                                                                                                                  \
    case 'F':                                                                                                                                                  \
    case 'G':                                                                                                                                                  \
    case 'H':                                                                                                                                                  \
    case 'I':                                                                                                                                                  \
    case 'J':                                                                                                                                                  \
    case 'K':                                                                                                                                                  \
    case 'L':                                                                                                                                                  \
    case 'M':                                                                                                                                                  \
    case 'N':                                                                                                                                                  \
    case 'O':                                                                                                                                                  \
    case 'P':                                                                                                                                                  \
    case 'Q':                                                                                                                                                  \
    case 'R':                                                                                                                                                  \
    case 'S':                                                                                                                                                  \
    case 'T':                                                                                                                                                  \
    case 'U':                                                                                                                                                  \
    case 'V':                                                                                                                                                  \
    case 'W':                                                                                                                                                  \
    case 'X':                                                                                                                                                  \
    case 'Y':                                                                                                                                                  \
    case 'Z'

#define BUSTER_SWITCH_ALPHA_LOWER                                                                                                                              \
    case 'a':                                                                                                                                                  \
    case 'b':                                                                                                                                                  \
    case 'c':                                                                                                                                                  \
    case 'd':                                                                                                                                                  \
    case 'e':                                                                                                                                                  \
    case 'f':                                                                                                                                                  \
    case 'g':                                                                                                                                                  \
    case 'h':                                                                                                                                                  \
    case 'i':                                                                                                                                                  \
    case 'j':                                                                                                                                                  \
    case 'k':                                                                                                                                                  \
    case 'l':                                                                                                                                                  \
    case 'm':                                                                                                                                                  \
    case 'n':                                                                                                                                                  \
    case 'o':                                                                                                                                                  \
    case 'p':                                                                                                                                                  \
    case 'q':                                                                                                                                                  \
    case 'r':                                                                                                                                                  \
    case 's':                                                                                                                                                  \
    case 't':                                                                                                                                                  \
    case 'u':                                                                                                                                                  \
    case 'v':                                                                                                                                                  \
    case 'w':                                                                                                                                                  \
    case 'x':                                                                                                                                                  \
    case 'y':                                                                                                                                                  \
    case 'z'

#define BUSTER_SWITCH_DECIMAL_DIGIT                                                                                                                            \
    case '0':                                                                                                                                                  \
    case '1':                                                                                                                                                  \
    case '2':                                                                                                                                                  \
    case '3':                                                                                                                                                  \
    case '4':                                                                                                                                                  \
    case '5':                                                                                                                                                  \
    case '6':                                                                                                                                                  \
    case '7':                                                                                                                                                  \
    case '8':                                                                                                                                                  \
    case '9'

#define BUSTER_SWITCH_HEX_ALPHA_LOWER                                                                                                                          \
    case 'a':                                                                                                                                                  \
    case 'b':                                                                                                                                                  \
    case 'c':                                                                                                                                                  \
    case 'd':                                                                                                                                                  \
    case 'e':                                                                                                                                                  \
    case 'f'

#define BUSTER_SWITCH_HEX_ALPHA_UPPER                                                                                                                          \
    case 'A':                                                                                                                                                  \
    case 'B':                                                                                                                                                  \
    case 'C':                                                                                                                                                  \
    case 'D':                                                                                                                                                  \
    case 'E':                                                                                                                                                  \
    case 'F'

#define BUSTER_SWITCH_HEX_ALPHA                                                                                                                                \
    BUSTER_SWITCH_HEX_ALPHA_UPPER:                                                                                                                             \
    BUSTER_SWITCH_HEX_ALPHA_LOWER
