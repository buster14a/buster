// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original cross-compiler regression for #2113's larger records. Compile twice, with BUSTER_WIDE_SSE_CALLEE selecting
// the other side, and BUSTER_WIDE_SSE_VECTOR_BYTES the target's vector width (16 at x86-64, 32 at x86-64-v3, 64 at
// x86-64-v4). clang 23 classifies a union of a 16-byte vector and a longer array, packed or aligned below its size, SSE,
// SSEUP up to the target's vector width and passes it as GetByteVectorType's <size / 8 x double>, which LLVM widens to the
// next power of two: 24 to 39 bytes in a ymm register, 40 to 63 in a zmm one, the bytes past those doubles lost. A
// variadic call's named one, and one LLVM has no vector register left for, goes on the stack as that vector, in a slot of
// its register's width; byval copies (no vector register left in clang's count, va_arg's overflow area) keep every byte.
// Past the target's width the records are MEMORY. Only the bytes each convention defines are checked.

typedef float Vector __attribute__((vector_size(16)));
#define BUSTER_WIDE_SSE_UNION(name, count, ...) \
    typedef union name name;                    \
    union __VA_ARGS__ name                      \
    {                                           \
        Vector vector;                          \
        unsigned char bytes[count];             \
    }
BUSTER_WIDE_SSE_UNION(Short20, 20, __attribute__((packed)));
BUSTER_WIDE_SSE_UNION(Wide24, 24, __attribute__((packed)));
BUSTER_WIDE_SSE_UNION(Wide31, 31, __attribute__((packed)));
BUSTER_WIDE_SSE_UNION(Wide32, 32, __attribute__((packed)));
BUSTER_WIDE_SSE_UNION(Wide33, 33, __attribute__((packed)));
BUSTER_WIDE_SSE_UNION(Wide40, 40, __attribute__((packed)));
BUSTER_WIDE_SSE_UNION(Wide47, 47, __attribute__((packed)));
BUSTER_WIDE_SSE_UNION(Wide48, 48);
BUSTER_WIDE_SSE_UNION(Wide56, 52, __attribute__((packed, aligned(8))));
BUSTER_WIDE_SSE_UNION(Wide63, 63, __attribute__((packed)));
typedef struct Wrapped31 Wrapped31;
struct Wrapped31
{
    Wide31 values[1];
};
typedef struct Wrapped47 Wrapped47;
struct Wrapped47
{
    Wide47 values[1];
};

_Static_assert(sizeof(Short20) == 20 && sizeof(Wide24) == 24 && sizeof(Wide31) == 31 && sizeof(Wide32) == 32 && sizeof(Wide33) == 33, "packed sizes");
_Static_assert(sizeof(Wide40) == 40 && sizeof(Wide47) == 47 && sizeof(Wide63) == 63 && sizeof(Wrapped31) == 31 && sizeof(Wrapped47) == 47, "packed sizes");
_Static_assert(sizeof(Wide48) == 48 && _Alignof(Wide48) == 16 && sizeof(Wide56) == 56 && _Alignof(Wide56) == 8, "aligned sizes");

// The bytes of a record of `size` bytes a register (or the stack slot of its vector) carries: <size / 8 x double>'s up to
// the target's vector width, two doubles below 24 bytes; past it, MEMORY, all of them.
#define BUSTER_WIDE_SSE_PASSED(size) \
    ((size) <= BUSTER_WIDE_SSE_VECTOR_BYTES && (size) > 16 ? ((size) < 24 ? 16u : (unsigned)(size) & ~7u) : (unsigned)(size))

static unsigned char sample(unsigned seed, unsigned index)
{
    return (unsigned char)(seed + 13 * index);
}

static void fill(unsigned char* bytes, unsigned size, unsigned seed)
{
    for (unsigned i = 0; i < size; i += 1)
    {
        bytes[i] = sample(seed, i);
    }
}

static int check(const unsigned char* bytes, unsigned size, unsigned seed)
{
    int failures = 0;
    for (unsigned i = 0; i < size; i += 1)
    {
        failures += bytes[i] != sample(seed, i);
    }
    return failures;
}

Wide24 round24(Wide24 value);
Wide31 round31(Wide31 value);
Wide32 round32(Wide32 value);
Wide33 round33(Wide33 value);
Wide40 round40(Wide40 value);
Wide48 round48(Wide48 value);
Wide56 round56(Wide56 value);
Wide63 round63(Wide63 value);
Wrapped31 round_wrapped31(Wrapped31 value);
Wrapped47 round_wrapped47(Wrapped47 value);
Wide31 give31(unsigned seed);
Wide47 give47(unsigned seed);
int stack31(long a, double b, double c, double d, double e, double f, double g, double h, double i, Wide31 value, long tail);
int stack63(long a, double b, double c, double d, double e, double f, double g, double h, double i, Wide63 value, long tail);
int named24(Wide24 value, ...);
int named33(Wide33 value, ...);
int named47(Wide47 value, ...);
int atomic20(_Atomic double a, _Atomic double b, _Atomic double c, _Atomic double d, _Atomic double e, _Atomic double f, _Atomic double g, _Atomic double h,
             Short20 value, long i, long j, long k, long l, long m, long n, long tail);
int atomic31(_Atomic double a, _Atomic double b, _Atomic double c, _Atomic double d, _Atomic double e, _Atomic double f, _Atomic double g, _Atomic double h,
             Wide31 value, long i, long j, long k, long l, long m, long n, long tail);
int overflow(int tag, ...);

#if BUSTER_WIDE_SSE_CALLEE
Wide24 round24(Wide24 value)
{
    return value;
}

Wide31 round31(Wide31 value)
{
    return value;
}

Wide32 round32(Wide32 value)
{
    return value;
}

Wide33 round33(Wide33 value)
{
    return value;
}

Wide40 round40(Wide40 value)
{
    return value;
}

Wide48 round48(Wide48 value)
{
    return value;
}

Wide56 round56(Wide56 value)
{
    return value;
}

Wide63 round63(Wide63 value)
{
    return value;
}

Wrapped31 round_wrapped31(Wrapped31 value)
{
    return value;
}

Wrapped47 round_wrapped47(Wrapped47 value)
{
    return value;
}

Wide31 give31(unsigned seed)
{
    Wide31 value;
    fill(value.bytes, sizeof(value), seed);
    return value;
}

Wide47 give47(unsigned seed)
{
    Wide47 value;
    fill(value.bytes, sizeof(value), seed);
    return value;
}

int stack31(long a, double b, double c, double d, double e, double f, double g, double h, double i, Wide31 value, long tail)
{
    return check(value.bytes, sizeof(value), 31) + (a != 1) + (b + c + d + e + f + g + h + i != 36) + (tail != 257);
}

int stack63(long a, double b, double c, double d, double e, double f, double g, double h, double i, Wide63 value, long tail)
{
    return check(value.bytes, sizeof(value), 63) + (a != 1) + (b + c + d + e + f + g + h + i != 36) + (tail != 513);
}

int named24(Wide24 value, ...)
{
    __builtin_va_list arguments;
    __builtin_va_start(arguments, value);
    double next = __builtin_va_arg(arguments, double);
    long tail = __builtin_va_arg(arguments, long);
    __builtin_va_end(arguments);
    return check(value.bytes, BUSTER_WIDE_SSE_PASSED(sizeof(value)), 24) + (next != 41.5) + (tail != 1025);
}

int named33(Wide33 value, ...)
{
    __builtin_va_list arguments;
    __builtin_va_start(arguments, value);
    double next = __builtin_va_arg(arguments, double);
    long tail = __builtin_va_arg(arguments, long);
    __builtin_va_end(arguments);
    return check(value.bytes, BUSTER_WIDE_SSE_PASSED(sizeof(value)), 33) + (next != 42.5) + (tail != 2049);
}

int named47(Wide47 value, ...)
{
    __builtin_va_list arguments;
    __builtin_va_start(arguments, value);
    double next = __builtin_va_arg(arguments, double);
    long tail = __builtin_va_arg(arguments, long);
    __builtin_va_end(arguments);
    return check(value.bytes, BUSTER_WIDE_SSE_PASSED(sizeof(value)), 47) + (next != 43.5) + (tail != 4097);
}

// clang doesn't count the registers LLVM gives the _Atomic doubles' values: it leaves the record to LLVM as its vector,
// which LLVM puts on the stack in a slot of its register's width, aligned to it, the tail on the stack after it.
int atomic20(_Atomic double a, _Atomic double b, _Atomic double c, _Atomic double d, _Atomic double e, _Atomic double f, _Atomic double g, _Atomic double h,
             Short20 value, long i, long j, long k, long l, long m, long n, long tail)
{
    return check(value.bytes, BUSTER_WIDE_SSE_PASSED(sizeof(value)), 20) + (a + b + c + d + e + f + g + h != 36) + (i + j + k + l + m + n != 21) + (tail != 8193);
}

int atomic31(_Atomic double a, _Atomic double b, _Atomic double c, _Atomic double d, _Atomic double e, _Atomic double f, _Atomic double g, _Atomic double h,
             Wide31 value, long i, long j, long k, long l, long m, long n, long tail)
{
    return check(value.bytes, BUSTER_WIDE_SSE_PASSED(sizeof(value)), 31) + (a + b + c + d + e + f + g + h != 36) + (i + j + k + l + m + n != 21) + (tail != 16385);
}

int overflow(int tag, ...)
{
    __builtin_va_list arguments;
    __builtin_va_start(arguments, tag);
    long integers = 0;
    double doubles = 0;
    for (unsigned i = 0; i < 5; i += 1)
    {
        integers += __builtin_va_arg(arguments, long);
    }
    for (unsigned i = 0; i < 8; i += 1)
    {
        doubles += __builtin_va_arg(arguments, double);
    }
    Wide24 a = __builtin_va_arg(arguments, Wide24);
    Wide31 b = __builtin_va_arg(arguments, Wide31);
    Wide48 c = __builtin_va_arg(arguments, Wide48);
    Wide63 d = __builtin_va_arg(arguments, Wide63);
    long tail = __builtin_va_arg(arguments, long);
    double next = __builtin_va_arg(arguments, double);
    __builtin_va_end(arguments);
    return check(a.bytes, sizeof(a), 24) + check(b.bytes, sizeof(b), 31) + check(c.bytes, sizeof(c), 48) + check(d.bytes, sizeof(d), 63) + (tag != 2) +
           (integers != 15) + (doubles != 36) + (tail != 2049) + (next != 99.5);
}
#else
#define BUSTER_WIDE_SSE_ROUND(type, function, seed)                                                     \
    do                                                                                                  \
    {                                                                                                   \
        type value;                                                                                     \
        fill((unsigned char*)&value, sizeof(value), seed);                                              \
        type result = function(value);                                                                  \
        failures += check((unsigned char*)&result, BUSTER_WIDE_SSE_PASSED(sizeof(result)), seed);       \
    } while (0)

int main(void)
{
    int failures = 0;
    BUSTER_WIDE_SSE_ROUND(Wide24, round24, 24);
    BUSTER_WIDE_SSE_ROUND(Wide31, round31, 31);
    BUSTER_WIDE_SSE_ROUND(Wide32, round32, 32);
    BUSTER_WIDE_SSE_ROUND(Wide33, round33, 33);
    BUSTER_WIDE_SSE_ROUND(Wide40, round40, 40);
    BUSTER_WIDE_SSE_ROUND(Wide48, round48, 48);
    BUSTER_WIDE_SSE_ROUND(Wide56, round56, 56);
    BUSTER_WIDE_SSE_ROUND(Wide63, round63, 63);
    BUSTER_WIDE_SSE_ROUND(Wrapped31, round_wrapped31, 71);
    BUSTER_WIDE_SSE_ROUND(Wrapped47, round_wrapped47, 77);
    Wide31 made31 = give31(57);
    Wide47 made47 = give47(63);
    failures += check(made31.bytes, BUSTER_WIDE_SSE_PASSED(sizeof(made31)), 57);
    failures += check(made47.bytes, BUSTER_WIDE_SSE_PASSED(sizeof(made47)), 63);
    Short20 s20;
    Wide24 w24;
    Wide31 w31;
    Wide33 w33;
    Wide47 w47;
    Wide48 w48;
    Wide63 w63;
    fill(s20.bytes, sizeof(s20), 20);
    fill(w24.bytes, sizeof(w24), 24);
    fill(w31.bytes, sizeof(w31), 31);
    fill(w33.bytes, sizeof(w33), 33);
    fill(w47.bytes, sizeof(w47), 47);
    fill(w48.bytes, sizeof(w48), 48);
    fill(w63.bytes, sizeof(w63), 63);
    failures += stack31(1, 1, 2, 3, 4, 5, 6, 7, 8, w31, 257);
    failures += stack63(1, 1, 2, 3, 4, 5, 6, 7, 8, w63, 513);
    failures += named24(w24, 41.5, 1025L);
    failures += named33(w33, 42.5, 2049L);
    failures += named47(w47, 43.5, 4097L);
    failures += atomic20(1, 2, 3, 4, 5, 6, 7, 8, s20, 1, 2, 3, 4, 5, 6, 8193);
    failures += atomic31(1, 2, 3, 4, 5, 6, 7, 8, w31, 1, 2, 3, 4, 5, 6, 16385);
    failures += overflow(2, 1L, 2L, 3L, 4L, 5L, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, w24, w31, w48, w63, 2049L, 99.5);
    return failures != 0;
}
#endif
