// GNU vector_size keeps the requested logical lane count but rounds the
// object image to the next power of two. This fixture crosses every x86-64
// call shape affected by that distinction: scalarized Win64 lanes, native
// AVX/AVX-512 registers, System V memory arguments, three-part integer and
// floating results, and over-aligned hidden-pointer results. The driver also
// builds provider and consumer with different compilers in both directions.

typedef unsigned char U8x3 __attribute__((vector_size(3)));
typedef unsigned short U16x3 __attribute__((vector_size(6)));
typedef unsigned int U32x3 __attribute__((vector_size(12)));
typedef unsigned long long U64x3 __attribute__((vector_size(24)));
typedef float F32x3 __attribute__((vector_size(12)));
typedef double F64x3 __attribute__((vector_size(24)));
typedef double F64x6 __attribute__((vector_size(48)));
typedef double F64x12 __attribute__((vector_size(96)));

#if defined(__aarch64__) || defined(_M_ARM64)
#define NON_POWER_VECTOR_ALIGNMENT(size) ((size) > 16 ? 16 : (size))
#elif defined(__APPLE__) && defined(__AVX512F__)
#define NON_POWER_VECTOR_ALIGNMENT(size) ((size) > 64 ? 64 : (size))
#elif defined(__APPLE__) && defined(__AVX__)
#define NON_POWER_VECTOR_ALIGNMENT(size) ((size) > 32 ? 32 : (size))
#elif defined(__APPLE__)
#define NON_POWER_VECTOR_ALIGNMENT(size) ((size) > 16 ? 16 : (size))
#else
#define NON_POWER_VECTOR_ALIGNMENT(size) (size)
#endif

_Static_assert(sizeof(U8x3) == 4 && _Alignof(U8x3) == NON_POWER_VECTOR_ALIGNMENT(4), "U8x3 layout");
_Static_assert(sizeof(U16x3) == 8 && _Alignof(U16x3) == NON_POWER_VECTOR_ALIGNMENT(8), "U16x3 layout");
_Static_assert(sizeof(U32x3) == 16 && _Alignof(U32x3) == NON_POWER_VECTOR_ALIGNMENT(16), "U32x3 layout");
_Static_assert(sizeof(U64x3) == 32 && _Alignof(U64x3) == NON_POWER_VECTOR_ALIGNMENT(32), "U64x3 layout");
_Static_assert(sizeof(F32x3) == 16 && _Alignof(F32x3) == NON_POWER_VECTOR_ALIGNMENT(16), "F32x3 layout");
_Static_assert(sizeof(F64x3) == 32 && _Alignof(F64x3) == NON_POWER_VECTOR_ALIGNMENT(32), "F64x3 layout");
_Static_assert(sizeof(F64x6) == 64 && _Alignof(F64x6) == NON_POWER_VECTOR_ALIGNMENT(64), "F64x6 layout");
_Static_assert(sizeof(F64x12) == 128 && _Alignof(F64x12) == NON_POWER_VECTOR_ALIGNMENT(128), "F64x12 layout");

U8x3 non_power_u8(U8x3 value);
U16x3 non_power_u16(U16x3 value);
U32x3 non_power_u32(U32x3 value);
U64x3 non_power_u64(U64x3 value);
F32x3 non_power_f32(F32x3 value);
F64x3 non_power_f64(F64x3 value);
F64x6 non_power_f64x6(F64x6 value);
F64x12 non_power_f64x12(F64x12 value);
F64x3 non_power_add(F64x3 left, F64x3 right);
double non_power_positioned(int before, F64x3 value, int after);
unsigned long long non_power_positioned_u64(unsigned before, U64x3 value, unsigned after);

#if !defined(NON_POWER_VECTOR_CONSUMER_ONLY)
U8x3 non_power_u8(U8x3 value)
{
    return (U8x3){(unsigned char)(value[0] + 1), (unsigned char)(value[1] + 2), (unsigned char)(value[2] + 3)};
}

U16x3 non_power_u16(U16x3 value)
{
    return (U16x3){(unsigned short)(value[0] + 1), (unsigned short)(value[1] + 2), (unsigned short)(value[2] + 3)};
}

U32x3 non_power_u32(U32x3 value)
{
    return (U32x3){value[0] + 1, value[1] + 2, value[2] + 3};
}

U64x3 non_power_u64(U64x3 value)
{
    return (U64x3){value[0] + 1, value[1] + 2, value[2] + 3};
}

F32x3 non_power_f32(F32x3 value)
{
    return (F32x3){value[0] + 1.0f, value[1] + 2.0f, value[2] + 3.0f};
}

F64x3 non_power_f64(F64x3 value)
{
    return (F64x3){value[0] + 1.0, value[1] + 2.0, value[2] + 3.0};
}

F64x6 non_power_f64x6(F64x6 value)
{
    for (int lane = 0; lane < 6; lane += 1)
    {
        value[lane] += (double)(lane + 1);
    }
    return value;
}

F64x12 non_power_f64x12(F64x12 value)
{
    for (int lane = 0; lane < 12; lane += 1)
    {
        value[lane] += (double)(lane + 1);
    }
    return value;
}

F64x3 non_power_add(F64x3 left, F64x3 right)
{
    return left + right;
}

double non_power_positioned(int before, F64x3 value, int after)
{
    return (double)before + value[0] + value[1] + value[2] + (double)after;
}

unsigned long long non_power_positioned_u64(unsigned before, U64x3 value, unsigned after)
{
    return before + value[0] + value[1] + value[2] + after;
}
#endif

#if !defined(NON_POWER_VECTOR_PROVIDER_ONLY)
int main(void)
{
    U8x3 (*volatile call_u8)(U8x3) = non_power_u8;
    U16x3 (*volatile call_u16)(U16x3) = non_power_u16;
    U32x3 (*volatile call_u32)(U32x3) = non_power_u32;
    U64x3 (*volatile call_u64)(U64x3) = non_power_u64;
    F32x3 (*volatile call_f32)(F32x3) = non_power_f32;
    F64x3 (*volatile call_f64)(F64x3) = non_power_f64;
    F64x6 (*volatile call_f64x6)(F64x6) = non_power_f64x6;
    F64x12 (*volatile call_f64x12)(F64x12) = non_power_f64x12;
    F64x3 (*volatile call_add)(F64x3, F64x3) = non_power_add;

    U8x3 u8 = call_u8((U8x3){1, 2, 3});
    if (u8[0] != 2 || u8[1] != 4 || u8[2] != 6)
        return 1;
    U16x3 u16 = call_u16((U16x3){10, 20, 30});
    if (u16[0] != 11 || u16[1] != 22 || u16[2] != 33)
        return 2;
    U32x3 u32 = call_u32((U32x3){100, 200, 300});
    if (u32[0] != 101 || u32[1] != 202 || u32[2] != 303)
        return 3;
    U64x3 u64 = call_u64((U64x3){1000, 2000, 3000});
    if (u64[0] != 1001 || u64[1] != 2002 || u64[2] != 3003)
        return 4;
    F32x3 f32 = call_f32((F32x3){1.0f, 2.0f, 3.0f});
    if (f32[0] != 2.0f || f32[1] != 4.0f || f32[2] != 6.0f)
        return 5;
    F64x3 f64 = call_f64((F64x3){10.0, 20.0, 30.0});
    if (f64[0] != 11.0 || f64[1] != 22.0 || f64[2] != 33.0)
        return 6;
    F64x3 sum = call_add((F64x3){1.0, 2.0, 3.0}, (F64x3){10.0, 20.0, 30.0});
    if (sum[0] != 11.0 || sum[1] != 22.0 || sum[2] != 33.0)
        return 7;
    if (non_power_positioned(7, (F64x3){1.0, 2.0, 3.0}, 11) != 24.0)
        return 8;
    if (non_power_positioned_u64(7, (U64x3){1, 2, 3}, 11) != 24)
        return 9;

    F64x6 f64x6 = call_f64x6((F64x6){1, 2, 3, 4, 5, 6});
    if (f64x6[0] != 2.0 || f64x6[2] != 6.0 || f64x6[5] != 12.0)
        return 10;

    struct GuardedResult
    {
        unsigned long long before[2];
        F64x12 value;
        unsigned long long after[2];
    } guarded = {
        .before = {0x51c0ffee51c0ffeeull, 0x0ddf00d50ddf00d5ull},
        .after = {0xdecaf00ddecaf00dull, 0x123456789abcdef0ull},
    };
    guarded.value = call_f64x12((F64x12){1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
    if (guarded.value[0] != 2.0 || guarded.value[5] != 12.0 || guarded.value[11] != 24.0)
        return 11;
    if (guarded.before[0] != 0x51c0ffee51c0ffeeull || guarded.before[1] != 0x0ddf00d50ddf00d5ull ||
        guarded.after[0] != 0xdecaf00ddecaf00dull || guarded.after[1] != 0x123456789abcdef0ull)
        return 12;

    // The callee still writes through a correctly aligned hidden pointer when
    // the source expression discards the over-aligned result.
    (void)call_f64x12((F64x12){0});
    return 0;
}
#endif
