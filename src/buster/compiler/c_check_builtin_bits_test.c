// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's bit builtins' actions (c_check_builtin_bits.c), test-check-builtins-bits, against the host compiler's own
// builtins (clang 23's, which builds this): every generic builtin of every type it takes (bool, the unsigned character
// and integer types, unsigned __int128 and unsigned _BitInts of 1 to 128 bits, and for bitreverseg and bswapg the signed
// ones too), rotations by counts of seven types, and the fixed ones of converted values, on boundary and random values:
// each fold, its result's type and the arguments' conversions. Then arguments that aren't constants or have effects, the
// zeros clzs, ctzs, clzg and ctzg leave undefined, the carry chains' conversions, pointer rules and unfolded effect, each
// type and argument count a builtin refuses, the arguments' slice ending at a protected page, and clang's acceptance of
// the source fixtures behind those rules. The lowering's recipes run natively in test-checker's programs
// (c_check_program_bits_test.c). Include after c_check_builtin_misc_test.c.
#ifndef BUSTER_C_CHECK_BUILTIN_BITS_TEST_INCLUDED
#define BUSTER_C_CHECK_BUILTIN_BITS_TEST_INCLUDED

typedef struct CheckBuiltinBitsTest CheckBuiltinBitsTest;
struct CheckBuiltinBitsTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t random;
};

static void check_builtin_bits_test_expect(CheckBuiltinBitsTest* test, bool condition, const char* description)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 24)
        {
            fprintf(stderr, "test-check-builtins-bits: %s\n", description);
        }
    }
}

static uint64_t check_builtin_bits_test_random(CheckBuiltinBitsTest* test)
{
    test->random ^= test->random << 13;
    test->random ^= test->random >> 7;
    test->random ^= test->random << 17;
    return test->random;
}

// The operand types of the folds: bool, the unsigned standard types, unsigned _BitInts (of widths bswapg takes, and of
// widths it doesn't), then the signed ones bitreverseg and bswapg take.
typedef enum CheckBuiltinBitsTestType
{
    CHECK_BUILTIN_BITS_TEST_BOOL,
    CHECK_BUILTIN_BITS_TEST_UCHAR,
    CHECK_BUILTIN_BITS_TEST_USHORT,
    CHECK_BUILTIN_BITS_TEST_UINT,
    CHECK_BUILTIN_BITS_TEST_ULONG,
    CHECK_BUILTIN_BITS_TEST_ULLONG,
    CHECK_BUILTIN_BITS_TEST_UINT128,
    CHECK_BUILTIN_BITS_TEST_U1,
    CHECK_BUILTIN_BITS_TEST_U48,
    CHECK_BUILTIN_BITS_TEST_U64,
    CHECK_BUILTIN_BITS_TEST_U112,
    CHECK_BUILTIN_BITS_TEST_U128,
    CHECK_BUILTIN_BITS_TEST_U2,
    CHECK_BUILTIN_BITS_TEST_U5,
    CHECK_BUILTIN_BITS_TEST_U37,
    CHECK_BUILTIN_BITS_TEST_U65,
    CHECK_BUILTIN_BITS_TEST_U100,
    CHECK_BUILTIN_BITS_TEST_SCHAR,
    CHECK_BUILTIN_BITS_TEST_SHORT,
    CHECK_BUILTIN_BITS_TEST_INT,
    CHECK_BUILTIN_BITS_TEST_LONG,
    CHECK_BUILTIN_BITS_TEST_INT128,
    CHECK_BUILTIN_BITS_TEST_S48,
    CHECK_BUILTIN_BITS_TEST_S128,
    CHECK_BUILTIN_BITS_TEST_S2,
    CHECK_BUILTIN_BITS_TEST_S7,
    CHECK_BUILTIN_BITS_TEST_S100,
    CHECK_BUILTIN_BITS_TEST_TYPES,
} CheckBuiltinBitsTestType;

// A rotation's count types.
typedef enum CheckBuiltinBitsTestCount
{
    CHECK_BUILTIN_BITS_TEST_COUNT_INT,
    CHECK_BUILTIN_BITS_TEST_COUNT_UCHAR,
    CHECK_BUILTIN_BITS_TEST_COUNT_LLONG,
    CHECK_BUILTIN_BITS_TEST_COUNT_INT128,
    CHECK_BUILTIN_BITS_TEST_COUNT_S5,
    CHECK_BUILTIN_BITS_TEST_COUNT_U9,
    CHECK_BUILTIN_BITS_TEST_COUNT_BOOL,
    CHECK_BUILTIN_BITS_TEST_COUNTS,
} CheckBuiltinBitsTestCount;

typedef struct CheckBuiltinBitsTestShape CheckBuiltinBitsTestShape;
struct CheckBuiltinBitsTestShape
{
    TypeId type;
    uint32_t width;
    bool is_signed;
    bool bit_int;
};

static const CheckBuiltinBitsTestShape check_builtin_bits_test_shapes[CHECK_BUILTIN_BITS_TEST_TYPES] =
{
    [CHECK_BUILTIN_BITS_TEST_BOOL] = { .type = BUSTER_TYPE_BOOL, .width = 1 },
    [CHECK_BUILTIN_BITS_TEST_UCHAR] = { .type = BUSTER_TYPE_UCHAR, .width = 8 },
    [CHECK_BUILTIN_BITS_TEST_USHORT] = { .type = BUSTER_TYPE_USHORT, .width = 16 },
    [CHECK_BUILTIN_BITS_TEST_UINT] = { .type = BUSTER_TYPE_UINT, .width = 32 },
    [CHECK_BUILTIN_BITS_TEST_ULONG] = { .type = BUSTER_TYPE_ULONG, .width = 64 },
    [CHECK_BUILTIN_BITS_TEST_ULLONG] = { .type = BUSTER_TYPE_ULLONG, .width = 64 },
    [CHECK_BUILTIN_BITS_TEST_UINT128] = { .type = BUSTER_TYPE_UINT128, .width = 128 },
    [CHECK_BUILTIN_BITS_TEST_U1] = { .width = 1, .bit_int = true },
    [CHECK_BUILTIN_BITS_TEST_U48] = { .width = 48, .bit_int = true },
    [CHECK_BUILTIN_BITS_TEST_U64] = { .width = 64, .bit_int = true },
    [CHECK_BUILTIN_BITS_TEST_U112] = { .width = 112, .bit_int = true },
    [CHECK_BUILTIN_BITS_TEST_U128] = { .width = 128, .bit_int = true },
    [CHECK_BUILTIN_BITS_TEST_U2] = { .width = 2, .bit_int = true },
    [CHECK_BUILTIN_BITS_TEST_U5] = { .width = 5, .bit_int = true },
    [CHECK_BUILTIN_BITS_TEST_U37] = { .width = 37, .bit_int = true },
    [CHECK_BUILTIN_BITS_TEST_U65] = { .width = 65, .bit_int = true },
    [CHECK_BUILTIN_BITS_TEST_U100] = { .width = 100, .bit_int = true },
    [CHECK_BUILTIN_BITS_TEST_SCHAR] = { .type = BUSTER_TYPE_SCHAR, .width = 8, .is_signed = true },
    [CHECK_BUILTIN_BITS_TEST_SHORT] = { .type = BUSTER_TYPE_SHORT, .width = 16, .is_signed = true },
    [CHECK_BUILTIN_BITS_TEST_INT] = { .type = BUSTER_TYPE_INT, .width = 32, .is_signed = true },
    [CHECK_BUILTIN_BITS_TEST_LONG] = { .type = BUSTER_TYPE_LONG, .width = 64, .is_signed = true },
    [CHECK_BUILTIN_BITS_TEST_INT128] = { .type = BUSTER_TYPE_INT128, .width = 128, .is_signed = true },
    [CHECK_BUILTIN_BITS_TEST_S48] = { .width = 48, .is_signed = true, .bit_int = true },
    [CHECK_BUILTIN_BITS_TEST_S128] = { .width = 128, .is_signed = true, .bit_int = true },
    [CHECK_BUILTIN_BITS_TEST_S2] = { .width = 2, .is_signed = true, .bit_int = true },
    [CHECK_BUILTIN_BITS_TEST_S7] = { .width = 7, .is_signed = true, .bit_int = true },
    [CHECK_BUILTIN_BITS_TEST_S100] = { .width = 100, .is_signed = true, .bit_int = true },
};

static const CheckBuiltinBitsTestShape check_builtin_bits_test_counts[CHECK_BUILTIN_BITS_TEST_COUNTS] =
{
    [CHECK_BUILTIN_BITS_TEST_COUNT_INT] = { .type = BUSTER_TYPE_INT, .width = 32, .is_signed = true },
    [CHECK_BUILTIN_BITS_TEST_COUNT_UCHAR] = { .type = BUSTER_TYPE_UCHAR, .width = 8 },
    [CHECK_BUILTIN_BITS_TEST_COUNT_LLONG] = { .type = BUSTER_TYPE_LLONG, .width = 64, .is_signed = true },
    [CHECK_BUILTIN_BITS_TEST_COUNT_INT128] = { .type = BUSTER_TYPE_INT128, .width = 128, .is_signed = true },
    [CHECK_BUILTIN_BITS_TEST_COUNT_S5] = { .width = 5, .is_signed = true, .bit_int = true },
    [CHECK_BUILTIN_BITS_TEST_COUNT_U9] = { .width = 9, .bit_int = true },
    [CHECK_BUILTIN_BITS_TEST_COUNT_BOOL] = { .type = BUSTER_TYPE_BOOL, .width = 1 },
};

static TypeId check_builtin_bits_test_type(TypeTable* table, const CheckBuiltinBitsTestShape* shape)
{
    return shape->bit_int ? type_bit_int(table, shape->width, !shape->is_signed) : shape->type;
}

// The host's rotation of `x` by the count `b` of a count type: the builtin's result zero-extended.
#define BUSTER_CHECK_BITS_TEST_ROTATE(T, name)                                                                                              \
    static __attribute__((noinline)) unsigned __int128 check_builtin_bits_test_rotate_##name(T x, uint32_t count, __int128 b, bool right) \
    {                                                                                                                                        \
        unsigned __int128 result = 0;                                                                                                        \
        switch (count)                                                                                                                       \
        {                                                                                                                                    \
            case CHECK_BUILTIN_BITS_TEST_COUNT_INT:                                                                                          \
                result = right ? __builtin_stdc_rotate_right(x, (int)b) : __builtin_stdc_rotate_left(x, (int)b);                             \
                break;                                                                                                                       \
            case CHECK_BUILTIN_BITS_TEST_COUNT_UCHAR:                                                                                        \
                result = right ? __builtin_stdc_rotate_right(x, (unsigned char)b) : __builtin_stdc_rotate_left(x, (unsigned char)b);         \
                break;                                                                                                                       \
            case CHECK_BUILTIN_BITS_TEST_COUNT_LLONG:                                                                                        \
                result = right ? __builtin_stdc_rotate_right(x, (long long)b) : __builtin_stdc_rotate_left(x, (long long)b);                 \
                break;                                                                                                                       \
            case CHECK_BUILTIN_BITS_TEST_COUNT_INT128:                                                                                       \
                result = right ? __builtin_stdc_rotate_right(x, b) : __builtin_stdc_rotate_left(x, b);                                       \
                break;                                                                                                                       \
            case CHECK_BUILTIN_BITS_TEST_COUNT_S5:                                                                                           \
                result = right ? __builtin_stdc_rotate_right(x, (_BitInt(5))b) : __builtin_stdc_rotate_left(x, (_BitInt(5))b);               \
                break;                                                                                                                       \
            case CHECK_BUILTIN_BITS_TEST_COUNT_U9:                                                                                           \
                result = right ? __builtin_stdc_rotate_right(x, (unsigned _BitInt(9))b) : __builtin_stdc_rotate_left(x, (unsigned _BitInt(9))b); \
                break;                                                                                                                       \
            case CHECK_BUILTIN_BITS_TEST_COUNT_BOOL:                                                                                         \
                result = right ? __builtin_stdc_rotate_right(x, (_Bool)b) : __builtin_stdc_rotate_left(x, (_Bool)b);                         \
                break;                                                                                                                       \
            default: break;                                                                                                                  \
        }                                                                                                                                    \
        return result;                                                                                                                       \
    }

#define BUSTER_CHECK_BITS_TEST_SWAP                                                                                                          \
    case CBUILTIN_BSWAPG: r = (unsigned __int128)__builtin_bswapg(x); break;

#define BUSTER_CHECK_BITS_TEST_NO_SWAP

// The counts and rotations every unsigned type takes, bool among them.
#define BUSTER_CHECK_BITS_TEST_COUNTED(name)                                                                                                \
    case CBUILTIN_POPCOUNTG: r = (unsigned)__builtin_popcountg(x); break;                                                                   \
    case CBUILTIN_CLZG:                                                                                                                      \
        defined = second || x != 0;                                                                                                          \
        r = second ? (unsigned)__builtin_clzg(x, (int)b) : x != 0 ? (unsigned)__builtin_clzg(x) : 0;                                        \
        break;                                                                                                                               \
    case CBUILTIN_CTZG:                                                                                                                      \
        defined = second || x != 0;                                                                                                          \
        r = second ? (unsigned)__builtin_ctzg(x, (int)b) : x != 0 ? (unsigned)__builtin_ctzg(x) : 0;                                        \
        break;                                                                                                                               \
    case CBUILTIN_BITREVERSEG: r = __builtin_bitreverseg(x); break;                                                                          \
    case CBUILTIN_STDC_ROTATE_LEFT: r = check_builtin_bits_test_rotate_##name(x, count, b, false); break;                                   \
    case CBUILTIN_STDC_ROTATE_RIGHT: r = check_builtin_bits_test_rotate_##name(x, count, b, true); break;

// The host's builtin `id` of an operand of an unsigned type, `a`'s bits, with the second argument `b` (clzg's and ctzg's,
// where `second` says it is given, or a rotation's of count type `count`): its result's bits, or false where the host
// leaves it undefined or the builtin doesn't take the type.
#define BUSTER_CHECK_BITS_TEST_UNSIGNED(T, name, SWAP)                                                                                      \
    BUSTER_CHECK_BITS_TEST_ROTATE(T, name)                                                                                                   \
    static __attribute__((noinline)) bool check_builtin_bits_test_host_##name(CBuiltinId id, unsigned __int128 a, uint32_t count, __int128 b, \
                                                                              bool second, unsigned __int128* result)                       \
    {                                                                                                                                        \
        const T x = (T)a;                                                                                                                    \
        bool defined = true;                                                                                                                 \
        unsigned __int128 r = 0;                                                                                                             \
        switch (id)                                                                                                                          \
        {                                                                                                                                    \
            BUSTER_CHECK_BITS_TEST_COUNTED(name)                                                                                             \
            SWAP                                                                                                                             \
            case CBUILTIN_STDC_LEADING_ZEROS: r = __builtin_stdc_leading_zeros(x); break;                                                    \
            case CBUILTIN_STDC_LEADING_ONES: r = __builtin_stdc_leading_ones(x); break;                                                      \
            case CBUILTIN_STDC_TRAILING_ZEROS: r = __builtin_stdc_trailing_zeros(x); break;                                                  \
            case CBUILTIN_STDC_TRAILING_ONES: r = __builtin_stdc_trailing_ones(x); break;                                                    \
            case CBUILTIN_STDC_FIRST_LEADING_ZERO: r = __builtin_stdc_first_leading_zero(x); break;                                          \
            case CBUILTIN_STDC_FIRST_LEADING_ONE: r = __builtin_stdc_first_leading_one(x); break;                                            \
            case CBUILTIN_STDC_FIRST_TRAILING_ZERO: r = __builtin_stdc_first_trailing_zero(x); break;                                        \
            case CBUILTIN_STDC_FIRST_TRAILING_ONE: r = __builtin_stdc_first_trailing_one(x); break;                                          \
            case CBUILTIN_STDC_COUNT_ZEROS: r = __builtin_stdc_count_zeros(x); break;                                                        \
            case CBUILTIN_STDC_COUNT_ONES: r = __builtin_stdc_count_ones(x); break;                                                          \
            case CBUILTIN_STDC_HAS_SINGLE_BIT: r = __builtin_stdc_has_single_bit(x); break;                                                  \
            case CBUILTIN_STDC_BIT_WIDTH: r = __builtin_stdc_bit_width(x); break;                                                            \
            case CBUILTIN_STDC_BIT_FLOOR: r = __builtin_stdc_bit_floor(x); break;                                                            \
            case CBUILTIN_STDC_BIT_CEIL: r = __builtin_stdc_bit_ceil(x); break;                                                              \
            default: defined = false; break;                                                                                                 \
        }                                                                                                                                    \
        *result = r;                                                                                                                         \
        return defined;                                                                                                                      \
    }

// bitreverseg and bswapg of a signed type, their results' bits.
#define BUSTER_CHECK_BITS_TEST_SIGNED(T, name, SWAP)                                                                                        \
    static __attribute__((noinline)) bool check_builtin_bits_test_host_##name(CBuiltinId id, unsigned __int128 a, uint32_t count, __int128 b, \
                                                                              bool second, unsigned __int128* result)                       \
    {                                                                                                                                        \
        (void)count;                                                                                                                         \
        (void)b;                                                                                                                             \
        (void)second;                                                                                                                        \
        const T x = (T)a;                                                                                                                    \
        bool defined = true;                                                                                                                 \
        unsigned __int128 r = 0;                                                                                                             \
        switch (id)                                                                                                                          \
        {                                                                                                                                    \
            case CBUILTIN_BITREVERSEG: r = (unsigned __int128)__builtin_bitreverseg(x); break;                                               \
            SWAP                                                                                                                             \
            default: defined = false; break;                                                                                                 \
        }                                                                                                                                    \
        *result = r;                                                                                                                         \
        return defined;                                                                                                                      \
    }

BUSTER_CHECK_BITS_TEST_ROTATE(_Bool, bool)

static __attribute__((noinline)) bool check_builtin_bits_test_host_bool(CBuiltinId id, unsigned __int128 a, uint32_t count, __int128 b, bool second,
                                                                        unsigned __int128* result)
{
    const _Bool x = (_Bool)(a & 1);
    bool defined = true;
    unsigned __int128 r = 0;
    switch (id)
    {
        BUSTER_CHECK_BITS_TEST_COUNTED(bool)
        BUSTER_CHECK_BITS_TEST_SWAP
        default: defined = false; break;
    }
    *result = r;
    return defined;
}

BUSTER_CHECK_BITS_TEST_UNSIGNED(unsigned char, uchar, BUSTER_CHECK_BITS_TEST_SWAP)
BUSTER_CHECK_BITS_TEST_UNSIGNED(unsigned short, ushort, BUSTER_CHECK_BITS_TEST_SWAP)
BUSTER_CHECK_BITS_TEST_UNSIGNED(unsigned int, uint, BUSTER_CHECK_BITS_TEST_SWAP)
BUSTER_CHECK_BITS_TEST_UNSIGNED(unsigned long, ulong, BUSTER_CHECK_BITS_TEST_SWAP)
BUSTER_CHECK_BITS_TEST_UNSIGNED(unsigned long long, ullong, BUSTER_CHECK_BITS_TEST_SWAP)
BUSTER_CHECK_BITS_TEST_UNSIGNED(unsigned __int128, uint128, BUSTER_CHECK_BITS_TEST_SWAP)
BUSTER_CHECK_BITS_TEST_UNSIGNED(unsigned _BitInt(1), u1, BUSTER_CHECK_BITS_TEST_SWAP)
BUSTER_CHECK_BITS_TEST_UNSIGNED(unsigned _BitInt(48), u48, BUSTER_CHECK_BITS_TEST_SWAP)
BUSTER_CHECK_BITS_TEST_UNSIGNED(unsigned _BitInt(64), u64, BUSTER_CHECK_BITS_TEST_SWAP)
BUSTER_CHECK_BITS_TEST_UNSIGNED(unsigned _BitInt(112), u112, BUSTER_CHECK_BITS_TEST_SWAP)
BUSTER_CHECK_BITS_TEST_UNSIGNED(unsigned _BitInt(128), u128, BUSTER_CHECK_BITS_TEST_SWAP)
BUSTER_CHECK_BITS_TEST_UNSIGNED(unsigned _BitInt(2), u2, BUSTER_CHECK_BITS_TEST_NO_SWAP)
BUSTER_CHECK_BITS_TEST_UNSIGNED(unsigned _BitInt(5), u5, BUSTER_CHECK_BITS_TEST_NO_SWAP)
BUSTER_CHECK_BITS_TEST_UNSIGNED(unsigned _BitInt(37), u37, BUSTER_CHECK_BITS_TEST_NO_SWAP)
BUSTER_CHECK_BITS_TEST_UNSIGNED(unsigned _BitInt(65), u65, BUSTER_CHECK_BITS_TEST_NO_SWAP)
BUSTER_CHECK_BITS_TEST_UNSIGNED(unsigned _BitInt(100), u100, BUSTER_CHECK_BITS_TEST_NO_SWAP)
BUSTER_CHECK_BITS_TEST_SIGNED(signed char, schar, BUSTER_CHECK_BITS_TEST_SWAP)
BUSTER_CHECK_BITS_TEST_SIGNED(short, short, BUSTER_CHECK_BITS_TEST_SWAP)
BUSTER_CHECK_BITS_TEST_SIGNED(int, int, BUSTER_CHECK_BITS_TEST_SWAP)
BUSTER_CHECK_BITS_TEST_SIGNED(long, long, BUSTER_CHECK_BITS_TEST_SWAP)
BUSTER_CHECK_BITS_TEST_SIGNED(__int128, int128, BUSTER_CHECK_BITS_TEST_SWAP)
BUSTER_CHECK_BITS_TEST_SIGNED(_BitInt(48), s48, BUSTER_CHECK_BITS_TEST_SWAP)
BUSTER_CHECK_BITS_TEST_SIGNED(_BitInt(128), s128, BUSTER_CHECK_BITS_TEST_SWAP)
BUSTER_CHECK_BITS_TEST_SIGNED(_BitInt(2), s2, BUSTER_CHECK_BITS_TEST_NO_SWAP)
BUSTER_CHECK_BITS_TEST_SIGNED(_BitInt(7), s7, BUSTER_CHECK_BITS_TEST_NO_SWAP)
BUSTER_CHECK_BITS_TEST_SIGNED(_BitInt(100), s100, BUSTER_CHECK_BITS_TEST_NO_SWAP)

static bool check_builtin_bits_test_host(uint32_t type, CBuiltinId id, unsigned __int128 a, uint32_t count, __int128 b, bool second, unsigned __int128* result)
{
    bool defined = false;
    switch (type)
    {
        case CHECK_BUILTIN_BITS_TEST_BOOL: defined = check_builtin_bits_test_host_bool(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_UCHAR: defined = check_builtin_bits_test_host_uchar(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_USHORT: defined = check_builtin_bits_test_host_ushort(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_UINT: defined = check_builtin_bits_test_host_uint(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_ULONG: defined = check_builtin_bits_test_host_ulong(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_ULLONG: defined = check_builtin_bits_test_host_ullong(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_UINT128: defined = check_builtin_bits_test_host_uint128(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_U1: defined = check_builtin_bits_test_host_u1(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_U48: defined = check_builtin_bits_test_host_u48(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_U64: defined = check_builtin_bits_test_host_u64(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_U112: defined = check_builtin_bits_test_host_u112(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_U128: defined = check_builtin_bits_test_host_u128(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_U2: defined = check_builtin_bits_test_host_u2(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_U5: defined = check_builtin_bits_test_host_u5(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_U37: defined = check_builtin_bits_test_host_u37(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_U65: defined = check_builtin_bits_test_host_u65(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_U100: defined = check_builtin_bits_test_host_u100(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_SCHAR: defined = check_builtin_bits_test_host_schar(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_SHORT: defined = check_builtin_bits_test_host_short(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_INT: defined = check_builtin_bits_test_host_int(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_LONG: defined = check_builtin_bits_test_host_long(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_INT128: defined = check_builtin_bits_test_host_int128(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_S48: defined = check_builtin_bits_test_host_s48(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_S128: defined = check_builtin_bits_test_host_s128(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_S2: defined = check_builtin_bits_test_host_s2(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_S7: defined = check_builtin_bits_test_host_s7(id, a, count, b, second, result); break;
        case CHECK_BUILTIN_BITS_TEST_S100: defined = check_builtin_bits_test_host_s100(id, a, count, b, second, result); break;
        default: break;
    }
    return defined;
}

// The host's fixed-prototype builtins of a value converted to each one's parameter.
static __attribute__((noinline)) bool check_builtin_bits_test_host_fixed(CBuiltinId id, unsigned long long a, unsigned __int128* result)
{
    bool defined = true;
    unsigned __int128 r = 0;
    switch (id)
    {
        case CBUILTIN_BITREVERSE8: r = __builtin_bitreverse8((unsigned char)a); break;
        case CBUILTIN_BITREVERSE16: r = __builtin_bitreverse16((unsigned short)a); break;
        case CBUILTIN_BITREVERSE32: r = __builtin_bitreverse32((unsigned)a); break;
        case CBUILTIN_BITREVERSE64: r = __builtin_bitreverse64((unsigned long)a); break;
        case CBUILTIN_CLRSB: r = (unsigned)__builtin_clrsb((int)a); break;
        case CBUILTIN_CLRSBL: r = (unsigned)__builtin_clrsbl((long)a); break;
        case CBUILTIN_CLRSBLL: r = (unsigned)__builtin_clrsbll((long long)a); break;
        case CBUILTIN_CLZS:
            defined = (unsigned short)a != 0;
            r = defined ? (unsigned)__builtin_clzs((unsigned short)a) : 0;
            break;
        case CBUILTIN_CTZS:
            defined = (unsigned short)a != 0;
            r = defined ? (unsigned)__builtin_ctzs((unsigned short)a) : 0;
            break;
        default: defined = false; break;
    }
    *result = r;
    return defined;
}

static const CBuiltinId check_builtin_bits_test_generic[] =
{
    CBUILTIN_POPCOUNTG,
    CBUILTIN_CLZG,
    CBUILTIN_CTZG,
    CBUILTIN_BITREVERSEG,
    CBUILTIN_BSWAPG,
    CBUILTIN_STDC_LEADING_ZEROS,
    CBUILTIN_STDC_LEADING_ONES,
    CBUILTIN_STDC_TRAILING_ZEROS,
    CBUILTIN_STDC_TRAILING_ONES,
    CBUILTIN_STDC_FIRST_LEADING_ZERO,
    CBUILTIN_STDC_FIRST_LEADING_ONE,
    CBUILTIN_STDC_FIRST_TRAILING_ZERO,
    CBUILTIN_STDC_FIRST_TRAILING_ONE,
    CBUILTIN_STDC_COUNT_ZEROS,
    CBUILTIN_STDC_COUNT_ONES,
    CBUILTIN_STDC_HAS_SINGLE_BIT,
    CBUILTIN_STDC_BIT_WIDTH,
    CBUILTIN_STDC_BIT_FLOOR,
    CBUILTIN_STDC_BIT_CEIL,
    CBUILTIN_STDC_ROTATE_LEFT,
    CBUILTIN_STDC_ROTATE_RIGHT,
};

// Whether a generic builtin takes an operand type, by clang's rules: an unsigned one but the stdc ones' bool,
// bitreverseg and bswapg any (bswapg a _BitInt of a multiple of 16 bits, 8 or 1).
static bool check_builtin_bits_test_takes(uint32_t type, CBuiltinId id)
{
    const CheckBuiltinBitsTestShape* shape = check_builtin_bits_test_shapes + type;
    const bool stdc = id >= CBUILTIN_STDC_LEADING_ZEROS && id <= CBUILTIN_STDC_BIT_CEIL;
    const bool swap = !shape->bit_int || shape->width % 16 == 0 || shape->width == 8 || shape->width == 1;
    return id == CBUILTIN_BITREVERSEG || (id == CBUILTIN_BSWAPG && swap) ||
           (id != CBUILTIN_BSWAPG && !shape->is_signed && !(stdc && type == CHECK_BUILTIN_BITS_TEST_BOOL));
}

static TypeId check_builtin_bits_test_result(CBuiltinId id, TypeId operand)
{
    return id == CBUILTIN_POPCOUNTG || id == CBUILTIN_CLZG || id == CBUILTIN_CTZG ? BUSTER_TYPE_INT :
           id == CBUILTIN_STDC_HAS_SINGLE_BIT ? BUSTER_TYPE_BOOL :
           id >= CBUILTIN_STDC_LEADING_ZEROS && id <= CBUILTIN_STDC_BIT_WIDTH ? BUSTER_TYPE_UINT : operand;
}

static CheckBuiltinMiscResult check_builtin_bits_test_action(TypeTable* table, CBuiltinId id, const CheckValue* arguments, uint32_t count)
{
    const CheckBuiltinMiscAction action =
    {
        .id = id,
        .arguments = arguments,
        .argument_count = count,
        .node = 148,
        .context.source_position = UINT32_MAX,
    };
    CheckBuiltinMiscResult result;
    check_builtin_misc(table, &action, &result, 1);
    return result;
}

// The values of a width the folds take: zero, one, two, three, the top bit and the ones around it, alternating bits, and
// random values of random lengths.
static uint32_t check_builtin_bits_test_values(CheckBuiltinBitsTest* test, uint32_t width, unsigned __int128 values[24])
{
    const unsigned __int128 mask = check_action_mask(width);
    const unsigned __int128 fives = ((unsigned __int128)UINT64_C(0x5555555555555555) << 64) | UINT64_C(0x5555555555555555);
    uint32_t count = 0;
    values[count++] = 0;
    values[count++] = 1 & mask;
    values[count++] = 2 & mask;
    values[count++] = 3 & mask;
    values[count++] = mask;
    values[count++] = mask - 1;
    values[count++] = mask >> 1;
    values[count++] = ((mask >> 1) + 1) & mask;
    values[count++] = fives & mask;
    values[count++] = ~fives & mask;
    while (count < 24)
    {
        const unsigned __int128 bits = ((unsigned __int128)check_builtin_bits_test_random(test) << 64) | check_builtin_bits_test_random(test);
        values[count++] = bits & (mask >> (check_builtin_bits_test_random(test) % width));
    }
    return count;
}

// A count value as a count type holds it, sign-extended for a signed one.
static __int128 check_builtin_bits_test_count(uint32_t count, __int128 raw)
{
    const CheckBuiltinBitsTestShape* shape = check_builtin_bits_test_counts + count;
    const unsigned __int128 bits = (unsigned __int128)raw & check_action_mask(shape->width);
    const bool negative = shape->is_signed && ((bits >> (shape->width - 1)) & 1) != 0;
    return count == CHECK_BUILTIN_BITS_TEST_COUNT_BOOL ? raw != 0 : negative ? (__int128)(bits | ~check_action_mask(shape->width)) : (__int128)bits;
}

static void check_builtin_bits_test_fold(CheckBuiltinBitsTest* test, TypeTable* table, uint32_t type, CBuiltinId id, unsigned __int128 a, uint32_t count,
                                         __int128 b, uint32_t argument_count)
{
    const CheckBuiltinBitsTestShape* shape = check_builtin_bits_test_shapes + type;
    const TypeId operand = check_builtin_bits_test_type(table, shape);
    const bool rotate = id == CBUILTIN_STDC_ROTATE_LEFT || id == CBUILTIN_STDC_ROTATE_RIGHT;
    const TypeId second_type = rotate ? check_builtin_bits_test_type(table, check_builtin_bits_test_counts + count) : BUSTER_TYPE_INT;
    const uint32_t second_width = rotate ? check_builtin_bits_test_counts[count].width : 32;
    const CheckValue arguments[2] =
    {
        check_test_integer(operand, a),
        check_test_integer(second_type, (unsigned __int128)b & check_action_mask(second_width)),
    };
    unsigned __int128 expected = 0;
    const bool defined = check_builtin_bits_test_host(type, id, a, count, b, argument_count == 2, &expected);
    const CheckBuiltinMiscResult result = check_builtin_bits_test_action(table, id, arguments, argument_count);
    const TypeId result_type = check_builtin_bits_test_result(id, operand);
    uint32_t result_width = 0;
    bool result_signed = false;
    check_action_integer_type(table, result_type, &result_width, &result_signed);
    const CheckValue* value = &result.action.value;
    bool equal = result.action.diagnostic == CHECK_DIAG_NONE && value->type == result_type && result.runtime_builtin == id &&
                 result.evaluated_operands == (1u << argument_count) - 1 && result.argument_conversions[0].type == operand && !result.writes_memory;
    if (defined)
    {
        equal = equal && value->constant_kind == CHECK_CONSTANT_INTEGER && value->fault == CHECK_FAULT_NONE && (value->flags & BUSTER_CHECK_ICE) != 0 &&
                (unsigned __int128)value->integer == (expected & check_action_mask(result_width));
    }
    else
    {
        equal = equal && value->constant_kind == CHECK_CONSTANT_NONE && value->fault == CHECK_FAULT_UNSUPPORTED_CONSTANT &&
                (value->flags & (BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED)) == BUSTER_CHECK_ICE_UNEVALUATED;
    }
    char description[256];
    snprintf(description, sizeof(description), "builtin 0x%x of operand type %u, 0x%016llx%016llx, second %lld (count type %u): the host's fold and result type",
             (unsigned)id, type, (unsigned long long)(uint64_t)(a >> 64), (unsigned long long)(uint64_t)a, (long long)b, count);
    check_builtin_bits_test_expect(test, equal, description);
}

// Every generic builtin of every operand type it takes against the host, and a diagnostic for each type it doesn't.
static void check_builtin_bits_test_generic_folds(CheckBuiltinBitsTest* test, TypeTable* table)
{
    static const __int128 fallbacks[] = { -1, 0, 7, INT32_MIN, INT32_MAX };
    for (uint32_t type = 0; type < CHECK_BUILTIN_BITS_TEST_TYPES; type += 1)
    {
        const CheckBuiltinBitsTestShape* shape = check_builtin_bits_test_shapes + type;
        const uint32_t width = shape->width;
        unsigned __int128 values[24];
        const uint32_t value_count = check_builtin_bits_test_values(test, width, values);
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(check_builtin_bits_test_generic); i += 1)
        {
            const CBuiltinId id = check_builtin_bits_test_generic[i];
            const bool rotate = id == CBUILTIN_STDC_ROTATE_LEFT || id == CBUILTIN_STDC_ROTATE_RIGHT;
            if (!check_builtin_bits_test_takes(type, id))
            {
                const CheckValue arguments[2] =
                {
                    check_test_integer(check_builtin_bits_test_type(table, shape), 1),
                    check_test_integer(BUSTER_TYPE_INT, 1),
                };
                const CheckBuiltinMiscResult result = check_builtin_bits_test_action(table, id, arguments, rotate ? 2 : 1);
                char description[160];
                snprintf(description, sizeof(description), "builtin 0x%x refuses operand type %u, as clang does", (unsigned)id, type);
                check_builtin_bits_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE && result.action.diagnostic_operand == 0 &&
                                                         result.action.value.constant_kind == CHECK_CONSTANT_NONE && result.evaluated_operands == 0, description);
            }
            for (uint32_t v = 0; check_builtin_bits_test_takes(type, id) && v < value_count; v += 1)
            {
                if (rotate)
                {
                    for (uint32_t count = 0; count < CHECK_BUILTIN_BITS_TEST_COUNTS; count += 1)
                    {
                        const __int128 raws[] = { 0, 1, -1, width - 1, width, width + 1, -(__int128)width, -(__int128)width - 1, 2 * (__int128)width + 3,
                                                  1000, -1000, (__int128)check_builtin_bits_test_random(test), -(__int128)check_builtin_bits_test_random(test),
                                                  (__int128)(((unsigned __int128)1) << 127), (__int128)(~(unsigned __int128)0 >> 1) };
                        for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(raws); r += 1)
                        {
                            check_builtin_bits_test_fold(test, table, type, id, values[v], count, check_builtin_bits_test_count(count, raws[r]), 2);
                        }
                    }
                }
                else
                {
                    check_builtin_bits_test_fold(test, table, type, id, values[v], 0, 0, 1);
                    for (uint32_t f = 0; (id == CBUILTIN_CLZG || id == CBUILTIN_CTZG) && f < BUSTER_ARRAY_LENGTH(fallbacks); f += 1)
                    {
                        check_builtin_bits_test_fold(test, table, type, id, values[v], 0, fallbacks[f], 2);
                    }
                }
            }
        }
    }
}

// The fixed-prototype builtins of values converted to their parameters: from unsigned long long and long long bits,
// from a signed char and from a double.
static void check_builtin_bits_test_fixed(CheckBuiltinBitsTest* test, TypeTable* table)
{
    static const CBuiltinId ids[] =
    {
        CBUILTIN_BITREVERSE8,
        CBUILTIN_BITREVERSE16,
        CBUILTIN_BITREVERSE32,
        CBUILTIN_BITREVERSE64,
        CBUILTIN_CLRSB,
        CBUILTIN_CLRSBL,
        CBUILTIN_CLRSBLL,
        CBUILTIN_CLZS,
        CBUILTIN_CTZS,
    };
    static const TypeId parameters[] =
    {
        BUSTER_TYPE_UCHAR,
        BUSTER_TYPE_USHORT,
        BUSTER_TYPE_UINT,
        BUSTER_TYPE_ULONG,
        BUSTER_TYPE_INT,
        BUSTER_TYPE_LONG,
        BUSTER_TYPE_LLONG,
        BUSTER_TYPE_USHORT,
        BUSTER_TYPE_USHORT,
    };
    unsigned __int128 values[24];
    const uint32_t value_count = check_builtin_bits_test_values(test, 64, values);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(ids); i += 1)
    {
        const TypeId result_type = ids[i] <= CBUILTIN_BITREVERSE64 ? parameters[i] : BUSTER_TYPE_INT;
        uint32_t result_width = 0;
        bool result_signed = false;
        check_action_integer_type(table, result_type, &result_width, &result_signed);
        for (uint32_t v = 0; v < value_count + 2; v += 1)
        {
            // Past the values: a signed char's -3, and a double's 100.75, which the conversion truncates.
            const unsigned long long a = v < value_count ? (unsigned long long)values[v] : v == value_count ? (unsigned long long)-3 : 100;
            const CheckValue argument = v < value_count ? check_test_integer(v % 2 == 0 ? BUSTER_TYPE_ULLONG : BUSTER_TYPE_LLONG, a) :
                                        v == value_count ? check_test_integer(BUSTER_TYPE_SCHAR, 0xfd) : check_test_float(BUSTER_TYPE_DOUBLE, 100.75L);
            unsigned __int128 expected = 0;
            const bool defined = check_builtin_bits_test_host_fixed(ids[i], a, &expected);
            const CheckBuiltinMiscResult result = check_builtin_bits_test_action(table, ids[i], &argument, 1);
            const CheckValue* value = &result.action.value;
            bool equal = result.action.diagnostic == CHECK_DIAG_NONE && value->type == result_type && result.argument_conversions[0].type == parameters[i] &&
                         (result.argument_conversions[0].flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 && result.evaluated_operands == 1;
            equal = equal && (defined ? value->constant_kind == CHECK_CONSTANT_INTEGER && (unsigned __int128)value->integer == (expected & check_action_mask(result_width)) :
                                        value->constant_kind == CHECK_CONSTANT_NONE && value->fault == CHECK_FAULT_UNSUPPORTED_CONSTANT);
            char description[160];
            snprintf(description, sizeof(description), "fixed builtin 0x%x of 0x%llx (argument %u): the host's fold of the converted argument", (unsigned)ids[i], a, v);
            check_builtin_bits_test_expect(test, equal, description);
        }
    }
}

// Arguments that aren't constants, or whose expressions have effects, and the folds clang leaves undefined.
static void check_builtin_bits_test_runtime(CheckBuiltinBitsTest* test, TypeTable* table)
{
    CheckValue runtime = { .type = BUSTER_TYPE_UINT, .category = CHECK_RVALUE };
    CheckBuiltinMiscResult result = check_builtin_bits_test_action(table, CBUILTIN_STDC_BIT_CEIL, &runtime, 1);
    check_builtin_bits_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.constant_kind == CHECK_CONSTANT_NONE &&
                                             result.action.value.type == BUSTER_TYPE_UINT && (result.action.value.flags & BUSTER_CHECK_ICE) == 0 &&
                                             result.evaluated_operands == 1, "a runtime operand gives a runtime action");
    CheckValue effect = check_test_integer(BUSTER_TYPE_UINT, 6);
    effect.flags |= BUSTER_CHECK_SIDE_EFFECT;
    result = check_builtin_bits_test_action(table, CBUILTIN_POPCOUNTG, &effect, 1);
    check_builtin_bits_test_expect(test, result.action.value.constant_kind == CHECK_CONSTANT_INTEGER && result.action.value.integer == 2 &&
                                             (result.action.value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_ICE)) == BUSTER_CHECK_SIDE_EFFECT,
                                             "a folded operand with an effect folds, keeps the effect and is no integer constant expression");
    const CheckValue fallback[2] =
    {
        check_test_integer(BUSTER_TYPE_UINT, 0),
        { .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE },
    };
    result = check_builtin_bits_test_action(table, CBUILTIN_CLZG, fallback, 2);
    check_builtin_bits_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.constant_kind == CHECK_CONSTANT_NONE &&
                                             result.action.value.fault == CHECK_FAULT_NONE, "clzg of a constant zero with a runtime fallback is a runtime action");
    const CheckValue zero = check_test_integer(BUSTER_TYPE_USHORT, 0);
    const CBuiltinId undefined[] = { CBUILTIN_CLZS, CBUILTIN_CTZS };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(undefined); i += 1)
    {
        result = check_builtin_bits_test_action(table, undefined[i], &zero, 1);
        check_builtin_bits_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.fault == CHECK_FAULT_UNSUPPORTED_CONSTANT &&
                                                 result.action.value.fault_node == 148 &&
                                                 (result.action.value.flags & (BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED)) == BUSTER_CHECK_ICE_UNEVALUATED,
                                                 "clzs and ctzs of zero are undefined: a fault, an integer constant expression only unevaluated");
    }
    effect = check_test_integer(BUSTER_TYPE_USHORT, 0);
    effect.flags |= BUSTER_CHECK_SIDE_EFFECT;
    result = check_builtin_bits_test_action(table, CBUILTIN_CLZS, &effect, 1);
    check_builtin_bits_test_expect(test, result.action.value.fault == CHECK_FAULT_UNSUPPORTED_CONSTANT && (result.action.value.flags & BUSTER_CHECK_ICE_UNEVALUATED) == 0,
                                   "an undefined fold of an operand with an effect isn't an integer constant expression even unevaluated");
}

// The carry chains: their conversions, their effect and unfolded value, and clang's pointer rules for the carry.
static void check_builtin_bits_test_carries(CheckBuiltinBitsTest* test, TypeTable* table)
{
    static const CBuiltinId ids[] =
    {
        CBUILTIN_ADDCB,
        CBUILTIN_ADDCS,
        CBUILTIN_ADDC,
        CBUILTIN_ADDCL,
        CBUILTIN_ADDCLL,
        CBUILTIN_SUBCB,
        CBUILTIN_SUBCS,
        CBUILTIN_SUBC,
        CBUILTIN_SUBCL,
        CBUILTIN_SUBCLL,
    };
    static const TypeId types[] =
    {
        BUSTER_TYPE_UCHAR,
        BUSTER_TYPE_USHORT,
        BUSTER_TYPE_UINT,
        BUSTER_TYPE_ULONG,
        BUSTER_TYPE_ULLONG,
    };
    static const TypeId signed_types[] =
    {
        BUSTER_TYPE_SCHAR,
        BUSTER_TYPE_SHORT,
        BUSTER_TYPE_INT,
        BUSTER_TYPE_LONG,
        BUSTER_TYPE_LLONG,
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(ids); i += 1)
    {
        const TypeId type = types[i % 5];
        const TypeId pointer = type_pointer(table, type);
        CheckValue arguments[4] =
        {
            check_test_integer(BUSTER_TYPE_ULLONG, 200),
            check_test_integer(BUSTER_TYPE_INT, 1),
            check_test_integer(BUSTER_TYPE_INT, 1),
            { .type = pointer, .category = CHECK_RVALUE },
        };
        CheckBuiltinMiscResult result = check_builtin_bits_test_action(table, ids[i], arguments, 4);
        bool converted = true;
        for (uint32_t a = 0; a < 3; a += 1)
        {
            converted = converted && result.argument_conversions[a].type == type && (result.argument_conversions[a].flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0;
        }
        check_builtin_bits_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && converted && result.argument_conversions[3].type == pointer &&
                                                 result.action.value.type == type && result.action.value.constant_kind == CHECK_CONSTANT_NONE &&
                                                 (result.action.value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_ICE)) == BUSTER_CHECK_SIDE_EFFECT &&
                                                 result.writes_memory && result.evaluated_operands == 15 && result.runtime_builtin == ids[i] && result.action.warnings == 0,
                                                 "a carry chain converts to its prototype, stores its carry and never folds");
        // A pointer to the type's signed counterpart warns (-Wpointer-sign), and so does one that loses a qualifier.
        arguments[3].type = type_pointer(table, signed_types[i % 5]);
        result = check_builtin_bits_test_action(table, ids[i], arguments, 4);
        check_builtin_bits_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && (result.action.warnings & CHECK_WARNING_POINTER_SIGN) != 0,
                                       "a carry's pointer of the other signedness warns, as clang's");
        arguments[3].type = type_pointer(table, type_qualify(table, type, BUSTER_TYPE_CONST));
        result = check_builtin_bits_test_action(table, ids[i], arguments, 4);
        check_builtin_bits_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && (result.action.warnings & CHECK_WARNING_DISCARDED_QUALIFIERS) != 0,
                                       "a carry's pointer to a const T warns that it discards the qualifier");
        arguments[3].type = type_pointer(table, BUSTER_TYPE_VOID);
        result = check_builtin_bits_test_action(table, ids[i], arguments, 4);
        check_builtin_bits_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.warnings == 0, "a carry's void pointer converts");
        arguments[3] = check_test_integer(BUSTER_TYPE_INT, 0);
        result = check_builtin_bits_test_action(table, ids[i], arguments, 4);
        check_builtin_bits_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE, "a carry's null pointer constant converts");
        // Another pointee, and an integer that is no null pointer constant, are errors.
        arguments[3] = (CheckValue){ .type = type_pointer(table, types[(i + 1) % 5]), .category = CHECK_RVALUE };
        result = check_builtin_bits_test_action(table, ids[i], arguments, 4);
        check_builtin_bits_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE && result.action.diagnostic_operand == 3 && result.evaluated_operands == 0,
                                       "a carry's pointer to another type is an error, as clang 23's");
        arguments[3] = check_test_integer(BUSTER_TYPE_INT, 5);
        result = check_builtin_bits_test_action(table, ids[i], arguments, 4);
        check_builtin_bits_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE && result.action.diagnostic_operand == 3, "a carry of an integer is an error");
        arguments[0] = (CheckValue){ .type = pointer, .category = CHECK_RVALUE };
        arguments[3] = (CheckValue){ .type = pointer, .category = CHECK_RVALUE };
        result = check_builtin_bits_test_action(table, ids[i], arguments, 4);
        check_builtin_bits_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE && result.action.diagnostic_operand == 0, "a carry chain's pointer value is an error");
        result = check_builtin_bits_test_action(table, ids[i], arguments + 1, 3);
        check_builtin_bits_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "a carry chain takes four arguments");
    }
}

// The argument counts and types the builtins refuse, beside each operand type's (check_builtin_bits_test_generic_folds).
static void check_builtin_bits_test_constraints(CheckBuiltinBitsTest* test, TypeTable* table)
{
    const CheckValue unsigned_value = check_test_integer(BUSTER_TYPE_UINT, 5);
    const CheckValue arguments[3] =
    {
        unsigned_value,
        unsigned_value,
        unsigned_value,
    };
    static const struct
    {
        CBuiltinId id;
        uint32_t count;
    } counts[] =
    {
        { CBUILTIN_POPCOUNTG, 0 },
        { CBUILTIN_POPCOUNTG, 2 },
        { CBUILTIN_CLZG, 0 },
        { CBUILTIN_CTZG, 3 },
        { CBUILTIN_STDC_ROTATE_LEFT, 1 },
        { CBUILTIN_STDC_ROTATE_RIGHT, 3 },
        { CBUILTIN_STDC_BIT_CEIL, 2 },
        { CBUILTIN_BITREVERSE32, 2 },
        { CBUILTIN_CLRSB, 0 },
        { CBUILTIN_BSWAPG, 2 },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(counts); i += 1)
    {
        const CheckBuiltinMiscResult result = check_builtin_bits_test_action(table, counts[i].id, counts[i].count != 0 ? arguments : NULL, counts[i].count);
        check_builtin_bits_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "a bit builtin's argument count");
    }
    // clzg's and ctzg's second argument's promotions must give int: a char's, a short's, an unsigned short's, a bool's and
    // an int bit-field's do; a long's, an unsigned int's, an unsigned 32-bit bit-field's and a _BitInt's don't.
    CheckValue bit_field = check_test_integer(BUSTER_TYPE_UINT, 1);
    bit_field.flags |= BUSTER_CHECK_BIT_FIELD;
    bit_field.bit_width = 31;
    CheckValue whole_field = bit_field;
    whole_field.bit_width = 32;
    const struct
    {
        CheckValue value;
        bool accepted;
    } seconds[] =
    {
        { check_test_integer(BUSTER_TYPE_SCHAR, 1), true },
        { check_test_integer(BUSTER_TYPE_SHORT, 1), true },
        { check_test_integer(BUSTER_TYPE_USHORT, 1), true },
        { check_test_integer(BUSTER_TYPE_BOOL, 1), true },
        { check_test_integer(BUSTER_TYPE_INT, 1), true },
        { bit_field, true },
        { check_test_integer(BUSTER_TYPE_LONG, 1), false },
        { check_test_integer(BUSTER_TYPE_UINT, 1), false },
        { whole_field, false },
        { check_test_integer(type_bit_int(table, 8, false), 1), false },
        { check_test_float(BUSTER_TYPE_DOUBLE, 1.0L), false },
        { { .type = type_pointer(table, BUSTER_TYPE_INT), .category = CHECK_RVALUE }, false },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(seconds); i += 1)
    {
        const CheckValue pair[2] =
        {
            check_test_integer(BUSTER_TYPE_UINT, 0),
            seconds[i].value,
        };
        const CheckBuiltinMiscResult result = check_builtin_bits_test_action(table, i % 2 == 0 ? CBUILTIN_CLZG : CBUILTIN_CTZG, pair, 2);
        const bool accepted = result.action.diagnostic == CHECK_DIAG_NONE;
        check_builtin_bits_test_expect(test, accepted == seconds[i].accepted && (accepted ? result.argument_conversions[1].type == BUSTER_TYPE_INT &&
                                                                                                result.action.value.integer == 1 : result.action.diagnostic_operand == 1),
                                       "clzg's and ctzg's second argument's promotions must give int");
    }
    // A rotation's count takes any integer type, and nothing else; its value an unsigned one, bool and enumerations of
    // unsigned types among them.
    const CheckValue counts_refused[] =
    {
        check_test_float(BUSTER_TYPE_DOUBLE, 1.0L),
        { .type = type_pointer(table, BUSTER_TYPE_INT), .category = CHECK_RVALUE },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(counts_refused); i += 1)
    {
        const CheckValue pair[2] =
        {
            unsigned_value,
            counts_refused[i],
        };
        const CheckBuiltinMiscResult result = check_builtin_bits_test_action(table, CBUILTIN_STDC_ROTATE_LEFT, pair, 2);
        check_builtin_bits_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE && result.action.diagnostic_operand == 1, "a rotation's count must be an integer");
    }
    // A float, a pointer and a complex value aren't integers; a _BitInt past 128 bits is one clang takes, refused by name.
    const CheckValue refused[] =
    {
        check_test_float(BUSTER_TYPE_DOUBLE, 3.0L),
        { .type = type_pointer(table, BUSTER_TYPE_UINT), .category = CHECK_RVALUE },
        { .type = type_complex(table, BUSTER_TYPE_DOUBLE), .category = CHECK_RVALUE },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(check_builtin_bits_test_generic); i += 1)
    {
        const CBuiltinId id = check_builtin_bits_test_generic[i];
        const bool rotate = id == CBUILTIN_STDC_ROTATE_LEFT || id == CBUILTIN_STDC_ROTATE_RIGHT;
        for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(refused); r += 1)
        {
            const CheckValue pair[2] =
            {
                refused[r],
                unsigned_value,
            };
            const CheckBuiltinMiscResult result = check_builtin_bits_test_action(table, id, pair, rotate ? 2 : 1);
            check_builtin_bits_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE && result.action.diagnostic != CHECK_DIAG_UNSUPPORTED &&
                                                     result.action.diagnostic_operand == 0, "a generic bit builtin of a non-integer is an error");
        }
        const CheckValue wide[2] =
        {
            check_test_integer(type_bit_int(table, 200, true), 1),
            check_test_integer(rotate ? type_bit_int(table, 200, false) : BUSTER_TYPE_INT, 1),
        };
        const CheckBuiltinMiscResult result = check_builtin_bits_test_action(table, id, wide, rotate ? 2 : 1);
        check_builtin_bits_test_expect(test, result.action.diagnostic == CHECK_DIAG_UNSUPPORTED && result.action.diagnostic_operand == 0,
                                       "a _BitInt past 128 bits is refused by name");
    }
    const CheckValue wide_count[2] =
    {
        unsigned_value,
        check_test_integer(type_bit_int(table, 200, false), 1),
    };
    const CheckBuiltinMiscResult result = check_builtin_bits_test_action(table, CBUILTIN_STDC_ROTATE_RIGHT, wide_count, 2);
    check_builtin_bits_test_expect(test, result.action.diagnostic == CHECK_DIAG_UNSUPPORTED && result.action.diagnostic_operand == 1,
                                   "a rotation's count past 128 bits is refused by name");
    const CheckValue fixed_refused = { .type = type_pointer(table, BUSTER_TYPE_UINT), .category = CHECK_RVALUE };
    const CBuiltinId fixed[] = { CBUILTIN_BITREVERSE32, CBUILTIN_CLRSBL, CBUILTIN_CTZS };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixed); i += 1)
    {
        const CheckBuiltinMiscResult refused_fixed = check_builtin_bits_test_action(table, fixed[i], &fixed_refused, 1);
        check_builtin_bits_test_expect(test, refused_fixed.action.diagnostic == CHECK_DIAG_INCOMPATIBLE && refused_fixed.action.diagnostic_operand == 0,
                                       "a fixed prototype's integer parameter refuses a pointer (-Wint-conversion, an error)");
    }
}

// Each argument slice ending at a protected page: the action reads only its arguments.
static void check_builtin_bits_test_pages(CheckBuiltinBitsTest* test, TypeTable* table)
{
    const long page_value = sysconf(_SC_PAGESIZE);
    const size_t page = page_value > 0 ? (size_t)page_value : 0;
    uint8_t* mapping = page != 0 ? mmap(NULL, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    const bool ready = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0;
    check_builtin_bits_test_expect(test, ready, "guarded bit builtin argument slice allocated");
    static const struct
    {
        CBuiltinId id;
        uint32_t count;
    } calls[] =
    {
        { CBUILTIN_POPCOUNTG, 1 },
        { CBUILTIN_CLZG, 2 },
        { CBUILTIN_STDC_ROTATE_LEFT, 2 },
        { CBUILTIN_BITREVERSE16, 1 },
        { CBUILTIN_ADDCL, 4 },
    };
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(calls); i += 1)
    {
        CheckValue copy[4] =
        {
            check_test_integer(BUSTER_TYPE_ULONG, 0x8000),
            check_test_integer(BUSTER_TYPE_INT, 3),
            check_test_integer(BUSTER_TYPE_INT, 1),
            { .type = type_pointer(table, BUSTER_TYPE_ULONG), .category = CHECK_RVALUE },
        };
        if (calls[i].id == CBUILTIN_ADDCL)
        {
            copy[1] = copy[0];
        }
        CheckValue* guarded = (CheckValue*)(mapping + page) - calls[i].count;
        memcpy(guarded, copy, calls[i].count * sizeof(CheckValue));
        const CheckBuiltinMiscResult expected = check_builtin_bits_test_action(table, calls[i].id, copy, calls[i].count);
        const CheckBuiltinMiscResult result = check_builtin_bits_test_action(table, calls[i].id, guarded, calls[i].count);
        check_builtin_bits_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && check_test_value_equal(&result.action.value, &expected.action.value) &&
                                                 result.evaluated_operands == expected.evaluated_operands, "a guarded argument slice gives the copy's action");
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, 2 * page);
    }
}

// clang's acceptance of the source rules the tests above follow.
static void check_builtin_bits_test_clang(CheckBuiltinBitsTest* test, const char* clang)
{
    static const struct
    {
        const char* source;
        bool rejected;
    } cases[] =
    {
        { "#define T(e,t) _Static_assert(_Generic((e),t:1,default:0),#e);\nenum U{UA=1};\nvoid f(unsigned char c,_Bool b,enum U e,unsigned _BitInt(37) w,_BitInt(48) s){\n"
          "T(__builtin_bitreverse64(1),unsigned long)T(__builtin_clrsbll(1),int)T(__builtin_clzs(1),int)T(__builtin_popcountg(b),int)T(__builtin_popcountg(e),int)\n"
          "T(__builtin_clzg(c,(short)1),int)T(__builtin_bitreverseg(s),_BitInt(48))T(__builtin_bswapg(s),_BitInt(48))T(__builtin_stdc_bit_ceil(w),unsigned _BitInt(37))\n"
          "T(__builtin_stdc_has_single_bit(c),_Bool)T(__builtin_stdc_count_ones(w),unsigned)T(__builtin_stdc_rotate_left(b,-1),_Bool)T(__builtin_stdc_rotate_right(e,s),enum U)\n"
          "T(__builtin_addcb(c,c,c,&c),unsigned char)T(__builtin_subcll(1,2,0,(unsigned long long*)0),unsigned long long)}\n", false },
        { "_Static_assert(__builtin_clzg(0u,5)==5&&__builtin_stdc_bit_ceil(0x80000001u)==0&&__builtin_stdc_rotate_left((unsigned char)1,-1)==0x80&&"
          "__builtin_clrsb(-1)==31&&__builtin_bitreverseg((signed char)1)==-128&&__builtin_bswapg((unsigned short)0x1234)==0x3412,\"\");\n", false },
        { "int f(int i){return __builtin_popcountg(i);}\n", true },
        { "int f(char c){return __builtin_popcountg(c);}\n", true },
        { "unsigned f(_Bool b){return __builtin_stdc_count_ones(b);}\n", true },
        { "enum U{UA=1};unsigned f(enum U e){return __builtin_stdc_leading_zeros(e);}\n", true },
        { "int f(unsigned u,long l){return __builtin_clzg(u,l);}\n", true },
        { "int f(unsigned u,unsigned v){return __builtin_ctzg(u,v);}\n", true },
        { "struct B{unsigned x:31;};int f(unsigned u,struct B b){return __builtin_clzg(u,b.x);}\n", false },
        { "struct B{unsigned x:32;};int f(unsigned u,struct B b){return __builtin_clzg(u,b.x);}\n", true },
        { "unsigned f(int u){return __builtin_stdc_rotate_left(u,1);}\n", true },
        { "unsigned f(unsigned u,float c){return __builtin_stdc_rotate_left(u,c);}\n", true },
        { "_BitInt(24) f(_BitInt(24) x){return __builtin_bswapg(x);}\n", true },
        { "float f(float x){return __builtin_bitreverseg(x);}\n", true },
        { "unsigned f(unsigned u,int*q){return __builtin_addc(u,u,0,q);}\n", false },
        { "unsigned f(unsigned u,const unsigned*p){return __builtin_addc(u,u,0,p);}\n", false },
        { "unsigned f(unsigned u,unsigned long*p){return __builtin_addc(u,u,0,p);}\n", true },
        { "unsigned f(unsigned u,int x){return __builtin_addc(u,u,0,x);}\n", true },
        { "unsigned f(unsigned u,unsigned*p){return __builtin_addc(u,u,0);}\n", true },
        { "unsigned f(int*p){return __builtin_bitreverse32(p);}\n", true },
        { "unsigned f(double d){return __builtin_bitreverse32(d);}\n", false },
        { "int f(unsigned u){return __builtin_clzg(u,1,2);}\n", true },
        { "int z=__builtin_clzg(0u);\n", true },
        { "unsigned f(unsigned _BitInt(200) x){return __builtin_popcountg(x);}\n", false },
    };
    char directory[] = "/tmp/buster-check-bits-XXXXXX";
    bool ready = mkdtemp(directory) != NULL;
    char paths[BUSTER_ARRAY_LENGTH(cases)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(cases)];
    ParseTestClang results[BUSTER_ARRAY_LENGTH(cases)];
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], cases[i].source, strlen(cases[i].source));
    }
    const char* arguments[] = { "-std=gnu17" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, BUSTER_ARRAY_LENGTH(cases), results);
    check_builtin_bits_test_expect(test, ready, "the independent Clang bit builtin oracle ran");
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        char description[96];
        snprintf(description, sizeof(description), "clang agrees with bit builtin source rule %u", i);
        check_builtin_bits_test_expect(test, results[i].rejected == cases[i].rejected, description);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    rmdir(directory);
}

static int run_check_builtin_bits_tests(int argc, char** argv)
{
    const bool epoch = oracle_epoch_begin();
    CheckBuiltinBitsTest test = { .random = 148 };
    const char* clang = "clang";
    bool options = true;
    for (int i = 0; options && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--clang") == 0 && i + 1 < argc)
        {
            i += 1;
            clang = argv[i];
            options = *clang != 0;
        }
        else
        {
            options = false;
        }
    }
    check_builtin_bits_test_expect(&test, options, "usage: test-check-builtins-bits [--clang PATH]");
    CheckTestTypes types = { 0 };
    if (options)
    {
        const bool initialized = check_test_types_open(&types);
        check_builtin_bits_test_expect(&test, initialized, "type-table allocation");
        if (initialized)
        {
            check_builtin_bits_test_generic_folds(&test, &types.table);
            check_builtin_bits_test_fixed(&test, &types.table);
            check_builtin_bits_test_runtime(&test, &types.table);
            check_builtin_bits_test_carries(&test, &types.table);
            check_builtin_bits_test_constraints(&test, &types.table);
            check_builtin_bits_test_pages(&test, &types.table);
        }
        check_test_types_close(&types);
        check_builtin_bits_test_clang(&test, clang);
    }
    printf("test-check-builtins-bits: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    const int result = test.failures != 0;
    oracle_cache_report(oracle_cache(), "test-check-builtins-bits");
    oracle_epoch_end(epoch);
    return result;
}

#endif
