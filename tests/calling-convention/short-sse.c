// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original cross-compiler regression for #2113. Compile twice, with BUSTER_SHORT_SSE_CALLEE selecting the other side.
// At x86-64-v3 and v4, clang 23.1.4 coerces 17--23-byte SSE/SSEUP records to <2 x double> in a register, while
// exhausted arguments and va_arg's overflow copies retain all their bytes in 24-byte slots. At x86-64 they are MEMORY.
// Only those 16 defined bytes are checked after register transport; stack copies check every byte of the C object.

typedef float Vector __attribute__((vector_size(16)));
typedef union Small16 Small16;
union __attribute__((packed)) Small16
{
    Vector vector;
    unsigned char bytes[16];
};
typedef union Small17 Small17;
union __attribute__((packed)) Small17
{
    Vector vector;
    unsigned char bytes[17];
};
typedef union Small23 Small23;
union __attribute__((packed)) Small23
{
    Vector vector;
    unsigned char bytes[23];
};
typedef union Aligned20 Aligned20;
union __attribute__((packed, aligned(4))) Aligned20
{
    Vector vector;
    unsigned char bytes[20];
};
typedef struct Wrapped23 Wrapped23;
struct Wrapped23
{
    Small23 values[1];
};

_Static_assert(sizeof(Small16) == 16 && sizeof(Small17) == 17 && sizeof(Small23) == 23, "packed sizes");
_Static_assert(sizeof(Aligned20) == 20 && _Alignof(Aligned20) == 4 && sizeof(Wrapped23) == 23, "wrapped sizes");

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

Small16 round16(Small16 value);
Small17 round17(Small17 value);
Small23 round23(Small23 value);
Aligned20 round20(Aligned20 value);
Wrapped23 round_wrapped(Wrapped23 value);
Small17 give17(unsigned seed);
Small23 give23(unsigned seed);
int stack17(long a, long b, long c, long d, long e, long f, double g, double h, double i, double j, double k, double l, double m, double n, Small17 value, long tail);
int stack23(long a, long b, long c, long d, long e, long f, double g, double h, double i, double j, double k, double l, double m, double n, Small23 value, long tail);
int registers(int tag, ...);
int overflow(int tag, ...);

#if BUSTER_SHORT_SSE_CALLEE
Small16 round16(Small16 value)
{
    return value;
}

Small17 round17(Small17 value)
{
    return value;
}

Small23 round23(Small23 value)
{
    return value;
}

Aligned20 round20(Aligned20 value)
{
    return value;
}

Wrapped23 round_wrapped(Wrapped23 value)
{
    return value;
}

Small17 give17(unsigned seed)
{
    Small17 value;
    fill(value.bytes, sizeof(value), seed);
    return value;
}

Small23 give23(unsigned seed)
{
    Small23 value;
    fill(value.bytes, sizeof(value), seed);
    return value;
}

int stack17(long a, long b, long c, long d, long e, long f, double g, double h, double i, double j, double k, double l, double m, double n, Small17 value, long tail)
{
    return check(value.bytes, sizeof(value), 17) + (a + b + c + d + e + f != 21) + (g + h + i + j + k + l + m + n != 36) + (tail != 257);
}

int stack23(long a, long b, long c, long d, long e, long f, double g, double h, double i, double j, double k, double l, double m, double n, Small23 value, long tail)
{
    return check(value.bytes, sizeof(value), 23) + (a + b + c + d + e + f != 21) + (g + h + i + j + k + l + m + n != 36) + (tail != 513);
}

int registers(int tag, ...)
{
    __builtin_va_list arguments;
    __builtin_va_start(arguments, tag);
    Small17 a = __builtin_va_arg(arguments, Small17);
    Small23 b = __builtin_va_arg(arguments, Small23);
    Aligned20 c = __builtin_va_arg(arguments, Aligned20);
    Wrapped23 d = __builtin_va_arg(arguments, Wrapped23);
    double next = __builtin_va_arg(arguments, double);
    long tail = __builtin_va_arg(arguments, long);
    __builtin_va_end(arguments);
    return check(a.bytes, BUSTER_SHORT_SSE_REGISTERS ? 16 : sizeof(a), 17) + check(b.bytes, BUSTER_SHORT_SSE_REGISTERS ? 16 : sizeof(b), 23) +
           check(c.bytes, BUSTER_SHORT_SSE_REGISTERS ? 16 : sizeof(c), 20) + check(d.values[0].bytes, BUSTER_SHORT_SSE_REGISTERS ? 16 : sizeof(d), 71) +
           (tag != 1) + (next != 41.5) + (tail != 1025);
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
    Small17 a = __builtin_va_arg(arguments, Small17);
    Small23 b = __builtin_va_arg(arguments, Small23);
    long tail = __builtin_va_arg(arguments, long);
    double next = __builtin_va_arg(arguments, double);
    __builtin_va_end(arguments);
    return check(a.bytes, sizeof(a), 17) + check(b.bytes, sizeof(b), 23) + (tag != 2) + (integers != 15) + (doubles != 36) + (tail != 2049) + (next != 99.5);
}
#else
int main(void)
{
    Small16 a;
    Small17 b;
    Small23 c;
    Aligned20 d;
    Wrapped23 e;
    fill(a.bytes, sizeof(a), 16);
    fill(b.bytes, sizeof(b), 17);
    fill(c.bytes, sizeof(c), 23);
    fill(d.bytes, sizeof(d), 20);
    fill(e.values[0].bytes, sizeof(e), 71);
    Small16 result16 = round16(a);
    Small17 result17 = round17(b);
    Small23 result23 = round23(c);
    Aligned20 result20 = round20(d);
    Wrapped23 result_wrapped = round_wrapped(e);
    Small17 made17 = give17(57);
    Small23 made23 = give23(63);
    int failures = check(result16.bytes, sizeof(result16), 16);
    failures += check(result17.bytes, BUSTER_SHORT_SSE_REGISTERS ? 16 : sizeof(result17), 17);
    failures += check(result23.bytes, BUSTER_SHORT_SSE_REGISTERS ? 16 : sizeof(result23), 23);
    failures += check(result20.bytes, BUSTER_SHORT_SSE_REGISTERS ? 16 : sizeof(result20), 20);
    failures += check(result_wrapped.values[0].bytes, BUSTER_SHORT_SSE_REGISTERS ? 16 : sizeof(result_wrapped), 71);
    failures += check(made17.bytes, BUSTER_SHORT_SSE_REGISTERS ? 16 : sizeof(made17), 57);
    failures += check(made23.bytes, BUSTER_SHORT_SSE_REGISTERS ? 16 : sizeof(made23), 63);
    failures += stack17(1, 2, 3, 4, 5, 6, 1, 2, 3, 4, 5, 6, 7, 8, b, 257);
    failures += stack23(1, 2, 3, 4, 5, 6, 1, 2, 3, 4, 5, 6, 7, 8, c, 513);
    failures += registers(1, b, c, d, e, 41.5, 1025L);
    failures += overflow(2, 1L, 2L, 3L, 4L, 5L, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, b, c, 2049L, 99.5);
    return failures != 0;
}
#endif
