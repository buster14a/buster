#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
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

#ifndef BUSTER_FUZZING
#define BUSTER_FUZZING 0
#endif

#if BUSTER_LINK_LIBC
#define BUSTER_THREAD_LOCAL_DECL __thread
#else
#pragma error
#endif

#if defined(__cplusplus)
#define restrict __restrict
#endif

#define BUSTER_TYPE_EQUAL(T1, T2) __is_same(T1, T2)
#define BUSTER_UNDERLYING_TYPE(E) __underlying_type(E)

#if BUSTER_OPTIMIZE
#define BUSTER_INLINE __attribute__((always_inline))
#else
#define BUSTER_INLINE inline
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

#define CONCAT_HELPER(a, b) a##b
#define CONCAT(a, b) CONCAT_HELPER(a, b)
#define COUNTER_NAME(x) CONCAT(x, __COUNTER__)

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
#else
#include <stddefer.h>
#endif

#ifndef BUSTER_UNITY_BUILD
#define BUSTER_UNITY_BUILD 0
#endif

#if BUSTER_UNITY_BUILD
#define BUSTER_F_DECL static
#define BUSTER_F_IMPL 

#define BUSTER_V_DECL extern
#define BUSTER_V_IMPL 
#else
#define BUSTER_F_DECL extern
#define BUSTER_F_IMPL 

#define BUSTER_V_DECL extern
#define BUSTER_V_IMPL 
#endif

#ifndef BUSTER_USE_IO_RING
#define BUSTER_USE_IO_RING 0
#endif

#define BUSTER_PACKED __attribute__((packed))

#define BUSTER_GLOBAL_LOCAL static

#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))

#define BUSTER_OFFSET_OF(T, field) __builtin_offsetof(T, field)
#define BUSTER_FIELD_PARENT_POINTER(type, field, pointer) ((type*)((char8*)(pointer) - BUSTER_OFFSET_OF(T, field)))

#define BUSTER_UNPREDICTABLE(cond) __builtin_unpredictable(cond)
#define BUSTER_SELECT(cond, a, b) BUSTER_UNPREDICTABLE(cond) ? (a) : (b)

#define let __auto_type
#define FORWARD_DECLARE(T, N) typedef T N N
#define STRUCT(n) FORWARD_DECLARE(struct, n); struct n
#define UNION(n) FORWARD_DECLARE(union, n); union n
#define OPAQUE(n) FORWARD_DECLARE(struct, n)
#define BUSTER_TRAP() __builtin_trap()
#define BUSTER_BREAKPOINT() __builtin_debugtrap()
#define BUSTER_LIKELY(x) __builtin_expect(!!(x), 1)
#define BUSTER_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define BUSTER_UNUSED(x) ((void)(x))

#define BUSTER_MIN(a,b) (((a) < (b)) ? (a) : (b))
#define BUSTER_MAX(a,b) (((a) > (b)) ? (a) : (b))

#define BUSTER_CLAMP_TOP(a,x) BUSTER_MIN(a, x)
#define BUSTER_CLAMP_BOT(x,b) BUSTER_MAX(x, b)
#define BUSTER_CLAMP(a,x,b) (((x) < (a)) ? (a) : ((x) > (b)) ? (b) : (x))

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
#define ENUM_START(E, T) enum class E : T
#define ENUM_END(E, T)
#endif
#define ENUM_T(E, T, ...) ENUM_START(E, T) { __VA_ARGS__, Count } ENUM_END(E, T)
#define ENUM(E, ...) ENUM_T(E, u32, __VA_ARGS__)

template <typename T>
struct Slice
{
    T* pointer;
    u64 length;

    T* begin();
    T* end();
};

#define EACH_SLICE(i, s) u64 i = 0; i < (s).length; i += 1

typedef Slice<u8> ByteSlice;

#define BUSTER_SLICE_SIZE(slice) ((slice).length * sizeof(*((slice).pointer)))
#define BUSTER_ARRAY_TO_SLICE(arr) { .pointer = (arr), .length = BUSTER_ARRAY_LENGTH(arr) }
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
    Decimal,
    Hexadecimal,
    Octal,
    Binary
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

template<typename T>
using String = Slice<T>;

using String8 = String<char8>;
using String16 = String<char16>;

#define S8(strlit) ((String8) { .pointer = (char8*)(strlit), .length = BUSTER_COMPILE_TIME_STRING_LENGTH(strlit) })
#define S16(strlit) ((String16) { .pointer = (char16*)(u ## strlit), .length = BUSTER_COMPILE_TIME_STRING_LENGTH(strlit) })

// Math types and enums for UI
ENUM(Axis2,
    X,
    Y);

ENUM(Corner,
    CORNER_00,
    CORNER_01,
    CORNER_10,
    CORNER_11);

UNION(F32Interval2)
{
    struct { float2 min; float2 max; };
    struct { float2 p0; float2 p1; };
    struct { f32 x0, y0, x1, y1; };
    float2 v[2];
};
static_assert(sizeof(F32Interval2) == 4 * sizeof(f32));

static BUSTER_INLINE bool is_power_of_two(u64 value)
{
    return value && !(value & (value - 1));
}

OPAQUE(OsFileDescriptor);
OPAQUE(OsProcessHandle);
OPAQUE(OsThreadHandle);
OPAQUE(OsModuleHandle);
OPAQUE(OsSymbol);
OPAQUE(OsBarrierHandle);
OPAQUE(OsConditionVariableHandle);
OPAQUE(OsMutexHandle);

ENUM(ProcessResult,
    Success,
    Failed,
    Failed_try_again,
    Crash,
    Not_existent,
    Running,
    Unknown);

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
#define SOs(strlit) S16(strlit)
#else
typedef String8 StringOs;
typedef char CharOs;
static_assert(sizeof(CharOs) == 1);
typedef CharOs** StringOsList;
#define SOs(strlit) S8(strlit)
#endif

#define BUSTER_SLICE(p, l) (Slice<decltype(*(p))>){ .pointer = (typeof(*(p))*) (p), .length = (l) }

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
    OS_WINDOWING_EVENT_WINDOW_CLOSE);

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
#define FLAG_ARRAY_U64(N, E) FLAG_ARRAY_GENERIC(u64, N, (u64)E::Count)

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/lsan_interface.h>
#define BUSTER_LSAN_DISABLE() __lsan_disable()
#define BUSTER_LSAN_ENABLE()  __lsan_enable()
#else
#define BUSTER_LSAN_DISABLE()
#define BUSTER_LSAN_ENABLE()
#endif

ENUM(ScratchArenaId,
    SCRATCH_ARENA_0,
    SCRATCH_ARENA_1);

#define EACH_ENUM_FREE(E, e) e = (E)0; e < E::Count; e = (E)((BUSTER_UNDERLYING_TYPE(E))e + 1)
#define EACH_ENUM(E, e) E EACH_ENUM_FREE(E, e)
#define EACH_ENUM_INT_FREE(E, e) e = 0; e < (BUSTER_UNDERLYING_TYPE(E))(E::Count); e += 1
#define EACH_ENUM_INT(E, e) BUSTER_UNDERLYING_TYPE(E) EACH_ENUM_INT_FREE(E, e)
