// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Clang's own builtin calls are the integer-action oracle, compiled by the bootstrap compiler. The overflow oracle
// uses signed 128-bit operands (every supported input value fits), and an actual _BitInt(N) destination for each width.
// It therefore checks the infinite-precision operation and destination conversion without sharing either kernel's
// arithmetic. The generated rows, fixtures and guard-page inputs are original; no external corpus is incorporated.
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <unistd.h>

_Static_assert(__builtin_bswap16(0x1234) == 0x3412, "Clang bswap16 fold");
_Static_assert(__builtin_bswap32(0x12345678) == 0x78563412, "Clang bswap32 fold");
_Static_assert(__builtin_bswap64(UINT64_C(0x123456789abcdef0)) == UINT64_C(0xf0debc9a78563412), "Clang bswap64 fold");
_Static_assert(__builtin_clz(1) == 31 && __builtin_clzll(1) == 63, "Clang clz fold");
_Static_assert(__builtin_ctz(0x80000000u) == 31 && __builtin_ctzll(UINT64_C(1) << 63) == 63, "Clang ctz fold");
_Static_assert(__builtin_ffs(0) == 0 && __builtin_ffsll(UINT64_C(1) << 63) == 64, "Clang ffs fold");
_Static_assert(__builtin_popcountll(UINT64_MAX) == 64 && __builtin_parityll(UINT64_MAX) == 0, "Clang population fold");

#define BUSTER_CBUILTIN_TEST_ROWS 257u
typedef struct CBuiltinTestRows CBuiltinTestRows;
struct CBuiltinTestRows
{
    uint32_t id[BUSTER_CBUILTIN_TEST_ROWS];
    uint64_t a[BUSTER_CBUILTIN_TEST_ROWS];
    uint64_t b[BUSTER_CBUILTIN_TEST_ROWS];
    uint32_t at[BUSTER_CBUILTIN_TEST_ROWS];
    uint32_t bt[BUSTER_CBUILTIN_TEST_ROWS];
    uint32_t rt[BUSTER_CBUILTIN_TEST_ROWS];
    uint64_t value[2][BUSTER_CBUILTIN_TEST_ROWS];
    uint32_t overflow[2][BUSTER_CBUILTIN_TEST_ROWS];
    uint32_t status[2][BUSTER_CBUILTIN_TEST_ROWS];
};

static uint64_t cbuiltin_test_random(uint64_t* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

static __int128 cbuiltin_test_integer(uint64_t value, uint32_t type)
{
    const uint32_t width = type & 127;
    const unsigned __int128 modulus = (unsigned __int128)1 << width;
    const __int128 truncated = value & (uint64_t)(modulus - 1);
    return (type & BUSTER_CBUILTIN_SIGNED) != 0 && truncated >= (__int128)(modulus / 2) ? truncated - (__int128)modulus : truncated;
}

// No inline: random runtime calls must use the bootstrap compiler's actual overflow implementation.
static __attribute__((noinline)) uint32_t cbuiltin_test_overflow(uint32_t id, __int128 a, __int128 b, uint32_t type, uint64_t* value)
{
    uint32_t overflow = 0;
#define BUSTER_CBUILTIN_ORACLE(T) do { \
    T destination; \
    if (id == CBUILTIN_ADD_OVERFLOW) { overflow = __builtin_add_overflow(a, b, &destination); } \
    else if (id == CBUILTIN_SUB_OVERFLOW) { overflow = __builtin_sub_overflow(a, b, &destination); } \
    else { overflow = __builtin_mul_overflow(a, b, &destination); } \
    *value = (uint64_t)destination; \
} while (0)
#define BUSTER_CBUILTIN_ORACLE_WIDTH(N) case N: \
    if (type & BUSTER_CBUILTIN_SIGNED) { BUSTER_CBUILTIN_ORACLE(_BitInt(N)); } \
    else { BUSTER_CBUILTIN_ORACLE(unsigned _BitInt(N)); } \
    break;
    switch (type & 127)
    {
        case 1: BUSTER_CBUILTIN_ORACLE(unsigned _BitInt(1)); break;
        BUSTER_CBUILTIN_ORACLE_WIDTH(2)
        BUSTER_CBUILTIN_ORACLE_WIDTH(3)
        BUSTER_CBUILTIN_ORACLE_WIDTH(4)
        BUSTER_CBUILTIN_ORACLE_WIDTH(5)
        BUSTER_CBUILTIN_ORACLE_WIDTH(6)
        BUSTER_CBUILTIN_ORACLE_WIDTH(7)
        BUSTER_CBUILTIN_ORACLE_WIDTH(8)
        BUSTER_CBUILTIN_ORACLE_WIDTH(9)
        BUSTER_CBUILTIN_ORACLE_WIDTH(10)
        BUSTER_CBUILTIN_ORACLE_WIDTH(11)
        BUSTER_CBUILTIN_ORACLE_WIDTH(12)
        BUSTER_CBUILTIN_ORACLE_WIDTH(13)
        BUSTER_CBUILTIN_ORACLE_WIDTH(14)
        BUSTER_CBUILTIN_ORACLE_WIDTH(15)
        BUSTER_CBUILTIN_ORACLE_WIDTH(16)
        BUSTER_CBUILTIN_ORACLE_WIDTH(17)
        BUSTER_CBUILTIN_ORACLE_WIDTH(18)
        BUSTER_CBUILTIN_ORACLE_WIDTH(19)
        BUSTER_CBUILTIN_ORACLE_WIDTH(20)
        BUSTER_CBUILTIN_ORACLE_WIDTH(21)
        BUSTER_CBUILTIN_ORACLE_WIDTH(22)
        BUSTER_CBUILTIN_ORACLE_WIDTH(23)
        BUSTER_CBUILTIN_ORACLE_WIDTH(24)
        BUSTER_CBUILTIN_ORACLE_WIDTH(25)
        BUSTER_CBUILTIN_ORACLE_WIDTH(26)
        BUSTER_CBUILTIN_ORACLE_WIDTH(27)
        BUSTER_CBUILTIN_ORACLE_WIDTH(28)
        BUSTER_CBUILTIN_ORACLE_WIDTH(29)
        BUSTER_CBUILTIN_ORACLE_WIDTH(30)
        BUSTER_CBUILTIN_ORACLE_WIDTH(31)
        BUSTER_CBUILTIN_ORACLE_WIDTH(32)
        BUSTER_CBUILTIN_ORACLE_WIDTH(33)
        BUSTER_CBUILTIN_ORACLE_WIDTH(34)
        BUSTER_CBUILTIN_ORACLE_WIDTH(35)
        BUSTER_CBUILTIN_ORACLE_WIDTH(36)
        BUSTER_CBUILTIN_ORACLE_WIDTH(37)
        BUSTER_CBUILTIN_ORACLE_WIDTH(38)
        BUSTER_CBUILTIN_ORACLE_WIDTH(39)
        BUSTER_CBUILTIN_ORACLE_WIDTH(40)
        BUSTER_CBUILTIN_ORACLE_WIDTH(41)
        BUSTER_CBUILTIN_ORACLE_WIDTH(42)
        BUSTER_CBUILTIN_ORACLE_WIDTH(43)
        BUSTER_CBUILTIN_ORACLE_WIDTH(44)
        BUSTER_CBUILTIN_ORACLE_WIDTH(45)
        BUSTER_CBUILTIN_ORACLE_WIDTH(46)
        BUSTER_CBUILTIN_ORACLE_WIDTH(47)
        BUSTER_CBUILTIN_ORACLE_WIDTH(48)
        BUSTER_CBUILTIN_ORACLE_WIDTH(49)
        BUSTER_CBUILTIN_ORACLE_WIDTH(50)
        BUSTER_CBUILTIN_ORACLE_WIDTH(51)
        BUSTER_CBUILTIN_ORACLE_WIDTH(52)
        BUSTER_CBUILTIN_ORACLE_WIDTH(53)
        BUSTER_CBUILTIN_ORACLE_WIDTH(54)
        BUSTER_CBUILTIN_ORACLE_WIDTH(55)
        BUSTER_CBUILTIN_ORACLE_WIDTH(56)
        BUSTER_CBUILTIN_ORACLE_WIDTH(57)
        BUSTER_CBUILTIN_ORACLE_WIDTH(58)
        BUSTER_CBUILTIN_ORACLE_WIDTH(59)
        BUSTER_CBUILTIN_ORACLE_WIDTH(60)
        BUSTER_CBUILTIN_ORACLE_WIDTH(61)
        BUSTER_CBUILTIN_ORACLE_WIDTH(62)
        BUSTER_CBUILTIN_ORACLE_WIDTH(63)
        BUSTER_CBUILTIN_ORACLE_WIDTH(64)
    }
#undef BUSTER_CBUILTIN_ORACLE_WIDTH
#undef BUSTER_CBUILTIN_ORACLE
    *value &= UINT64_MAX >> (64 - (type & 127));
    return overflow;
}

static uint64_t cbuiltin_test_bits(uint32_t id, uint64_t a)
{
    uint64_t value = 0;
    switch (id)
    {
        case CBUILTIN_BSWAP16: value = __builtin_bswap16(a); break;
        case CBUILTIN_BSWAP32: value = __builtin_bswap32(a); break;
        case CBUILTIN_BSWAP64: value = __builtin_bswap64(a); break;
        case CBUILTIN_CLZ: value = __builtin_clz(a); break;
        case CBUILTIN_CLZL: value = __builtin_clzl(a); break;
        case CBUILTIN_CLZLL: value = __builtin_clzll(a); break;
        case CBUILTIN_CTZ: value = __builtin_ctz(a); break;
        case CBUILTIN_CTZL: value = __builtin_ctzl(a); break;
        case CBUILTIN_CTZLL: value = __builtin_ctzll(a); break;
        case CBUILTIN_POPCOUNT: value = __builtin_popcount(a); break;
        case CBUILTIN_POPCOUNTL: value = __builtin_popcountl(a); break;
        case CBUILTIN_POPCOUNTLL: value = __builtin_popcountll(a); break;
        case CBUILTIN_PARITY: value = __builtin_parity(a); break;
        case CBUILTIN_PARITYL: value = __builtin_parityl(a); break;
        case CBUILTIN_PARITYLL: value = __builtin_parityll(a); break;
        case CBUILTIN_FFS: value = __builtin_ffs(a); break;
        case CBUILTIN_FFSL: value = __builtin_ffsl(a); break;
        case CBUILTIN_FFSLL: value = __builtin_ffsll(a); break;
    }
    return value;
}

static bool cbuiltin_test_check(CBuiltinIntegers input, CBuiltinIntegerResults scalar, CBuiltinIntegerResults vector, uint32_t count)
{
    cbuiltin_fold_scalar(input, scalar, count);
    cbuiltin_fold(input, vector, count);
    bool passed = true;
    for (uint32_t i = 0; i < count && passed; i += 1)
    {
        const uint32_t id = input.id[i];
        uint64_t expected = 0;
        uint32_t overflow = 0;
        uint32_t status = CBUILTIN_FOLDED;
        if (id == 0 || id >= CBUILTIN_COUNT)
        {
            status = CBUILTIN_UNSUPPORTED;
        }
        else if (id >= CBUILTIN_ADD_OVERFLOW)
        {
            const uint32_t types[] = { input.a_type[i], input.b_type[i], input.result_type[i] };
            for (uint32_t t = 0; t < 3; t += 1)
            {
                const uint32_t bits = types[t] & 127;
                if (bits == 0 || bits > 64 || types[t] > 255 || types[t] == 129)
                {
                    status = CBUILTIN_INVALID_TYPE;
                }
            }
            if (status == CBUILTIN_FOLDED)
            {
                const __int128 a = cbuiltin_test_integer(input.a[i], types[0]);
                const __int128 b = cbuiltin_test_integer(input.b[i], types[1]);
                overflow = cbuiltin_test_overflow(id, a, b, types[2], &expected);
            }
        }
        else
        {
            const bool count32 = id == CBUILTIN_CLZ || id == CBUILTIN_CTZ;
            const bool count64 = id == CBUILTIN_CLZL || id == CBUILTIN_CLZLL || id == CBUILTIN_CTZL || id == CBUILTIN_CTZLL;
            if ((count32 && (uint32_t)input.a[i] == 0) || (count64 && input.a[i] == 0))
            {
                status = CBUILTIN_UNDEFINED;
            }
            else
            {
                expected = cbuiltin_test_bits(id, input.a[i]);
            }
        }
        passed = scalar.value[i] == expected && scalar.overflow[i] == overflow && scalar.status[i] == status &&
            vector.value[i] == expected && vector.overflow[i] == overflow && vector.status[i] == status;
        if (!passed)
        {
            printf("builtin row %u id=%u a=%016" PRIx64 " b=%016" PRIx64 " types=%u/%u/%u\n", i, id, input.a[i], input.b[i], input.a_type[i], input.b_type[i], input.result_type[i]);
            printf("expected=%016" PRIx64 "/%u/%u scalar=%016" PRIx64 "/%u/%u vector=%016" PRIx64 "/%u/%u\n", expected, overflow, status,
                scalar.value[i], scalar.overflow[i], scalar.status[i], vector.value[i], vector.overflow[i], vector.status[i]);
        }
    }
    return passed;
}

static bool cbuiltin_test_generated(uint64_t seed)
{
    CBuiltinTestRows rows;
    CBuiltinIntegers input = { rows.id, rows.a, rows.b, rows.at, rows.bt, rows.rt };
    CBuiltinIntegerResults scalar = { rows.value[0], rows.overflow[0], rows.status[0] };
    CBuiltinIntegerResults vector = { rows.value[1], rows.overflow[1], rows.status[1] };
    static const uint64_t edges[] = { 0, 1, 2, 127, 128, 255, 256, 32767, 32768, 65535, UINT32_MAX, UINT64_C(1) << 32, INT64_MAX, UINT64_C(1) << 63, UINT64_MAX };
    uint64_t random = seed;
    uint64_t checked = 0;
    bool passed = true;
    for (uint32_t round = 0; round < 4096 && passed; round += 1)
    {
        const uint32_t count = round < BUSTER_CBUILTIN_TEST_ROWS ? round : BUSTER_CBUILTIN_TEST_ROWS;
        for (uint32_t i = 0; i < count; i += 1)
        {
            rows.id[i] = round < 512 ? round % CBUILTIN_COUNT : cbuiltin_test_random(&random) % (CBUILTIN_COUNT + 2);
            rows.a[i] = round < 512 ? edges[i % 15] : cbuiltin_test_random(&random);
            rows.b[i] = round < 512 ? edges[(i / 15) % 15] : cbuiltin_test_random(&random);
            rows.at[i] = 1 + cbuiltin_test_random(&random) % 64;
            rows.bt[i] = 1 + cbuiltin_test_random(&random) % 64;
            rows.rt[i] = 1 + (round + i) % 64;
            rows.at[i] |= (cbuiltin_test_random(&random) & 1) * BUSTER_CBUILTIN_SIGNED;
            rows.bt[i] |= (cbuiltin_test_random(&random) & 1) * BUSTER_CBUILTIN_SIGNED;
            rows.rt[i] |= (cbuiltin_test_random(&random) & 1) * BUSTER_CBUILTIN_SIGNED;
            if (round >= 512 && i % 4 == 0)
            {
                rows.at[i] = (i & 4) ? 64 : 192;
                rows.bt[i] = (i & 8) ? 64 : 192;
                rows.rt[i] = (i & 16) ? 64 : 192;
            }
            if (round >= 512 && i % 13 == 0)
            {
                uint32_t* types[] = { rows.at, rows.bt, rows.rt };
                types[i % 3][i] = (uint32_t)cbuiltin_test_random(&random);
            }
        }
        memset(rows.value, 0xA5, sizeof(rows.value));
        memset(rows.status, 0xA5, sizeof(rows.status));
        memset(rows.overflow, 0xA5, sizeof(rows.overflow));
        passed = cbuiltin_test_check(input, scalar, vector, count);
        checked += count;
    }
    printf("builtin integer folds: %s (%" PRIu64 " Clang oracle rows, seed=%" PRIu64 ")\n", passed ? "pass" : "FAIL", checked, seed);
    return passed;
}

static bool cbuiltin_test_boundaries(void)
{
    CBuiltinTestRows rows;
    CBuiltinIntegers input = { rows.id, rows.a, rows.b, rows.at, rows.bt, rows.rt };
    CBuiltinIntegerResults scalar = { rows.value[0], rows.overflow[0], rows.status[0] };
    CBuiltinIntegerResults vector = { rows.value[1], rows.overflow[1], rows.status[1] };
    bool passed = true;
    uint32_t checked = 0;
    for (uint32_t width = 1; width <= 64 && passed; width += 1)
    {
        const uint64_t top = UINT64_C(1) << (width - 1);
        const uint64_t mask = UINT64_MAX >> (64 - width);
        const uint64_t values[] = { 0, 1, 2, top - 1, top, top + 1, mask, mask - 1, 0 - top, UINT64_MAX, INT64_MAX, UINT64_C(1) << 63 };
        for (uint32_t signs = 0; signs < 8 && passed; signs += 1)
        {
            for (uint32_t id = CBUILTIN_ADD_OVERFLOW; id <= CBUILTIN_MUL_OVERFLOW && passed; id += 1)
            {
                for (uint32_t i = 0; i < 144; i += 1)
                {
                    rows.id[i] = id;
                    rows.a[i] = values[i % 12];
                    rows.b[i] = values[i / 12];
                    rows.at[i] = 64 | ((signs & 1) ? BUSTER_CBUILTIN_SIGNED : 0);
                    rows.bt[i] = 64 | ((signs & 2) ? BUSTER_CBUILTIN_SIGNED : 0);
                    rows.rt[i] = width | ((signs & 4) ? BUSTER_CBUILTIN_SIGNED : 0);
                }
                passed = cbuiltin_test_check(input, scalar, vector, 144);
                checked += 144;
            }
        }
    }
    printf("builtin overflow destination boundaries: %s (%u Clang oracle rows)\n", passed ? "pass" : "FAIL", checked);
    return passed;
}

static bool cbuiltin_test_pages(void)
{
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    const size_t size = 24 * page;
    uint8_t* mapping = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool passed = mapping != MAP_FAILED;
    for (uint32_t i = 0; i < 12 && passed; i += 1)
    {
        passed = mprotect(mapping + (2 * i + 1) * page, page, PROT_NONE) == 0;
    }
    for (uint32_t count = 0; count <= 33 && passed; count += 1)
    {
        void* columns[12];
        const uint32_t widths[] = { 4, 8, 8, 4, 4, 4, 8, 4, 4, 8, 4, 4 };
        for (uint32_t c = 0; c < 12; c += 1)
        {
            columns[c] = mapping + (2 * c + 1) * page - count * widths[c];
            memset((uint8_t*)columns[c] - 16, 0xA5, 16 + count * widths[c]);
        }
        CBuiltinIntegers input = { columns[0], columns[1], columns[2], columns[3], columns[4], columns[5] };
        CBuiltinIntegerResults scalar = { columns[6], columns[7], columns[8] };
        CBuiltinIntegerResults vector = { columns[9], columns[10], columns[11] };
        for (uint32_t lane = 0; lane < count; lane += 1)
        {
            ((uint32_t*)columns[0])[lane] = 1 + lane % (CBUILTIN_COUNT - 1);
            ((uint64_t*)columns[1])[lane] = UINT64_MAX - lane;
            ((uint64_t*)columns[2])[lane] = lane;
            ((uint32_t*)columns[3])[lane] = 64;
            ((uint32_t*)columns[4])[lane] = 192;
            ((uint32_t*)columns[5])[lane] = 160;
        }
        for (uint32_t id = 0; id <= CBUILTIN_COUNT && passed; id += 1)
        {
            if (id != 0)
            {
                for (uint32_t lane = 0; lane < count; lane += 1)
                {
                    ((uint32_t*)columns[0])[lane] = id == CBUILTIN_COUNT ? UINT32_MAX : id;
                }
            }
            passed = cbuiltin_test_check(input, scalar, vector, count);
        }
        for (uint32_t c = 0; c < 12; c += 1)
        {
            for (uint32_t b = 1; b <= 16; b += 1)
            {
                passed &= *((uint8_t*)columns[c] - b) == 0xA5;
            }
        }
    }
    for (uint32_t id = 1; id < CBUILTIN_COUNT && passed; id += 1)
    {
        const uint32_t length = cbuiltin_names[id].length;
        char* name = (char*)mapping + page - length;
        memcpy(name, cbuiltin_names[id].bytes, length);
        passed &= cbuiltin_lookup(name, length) == id && cbuiltin_lookup_scalar(name, length) == id;
        for (uint32_t b = 0; b < length; b += 1)
        {
            name[b] ^= 1;
            passed &= cbuiltin_lookup(name, length) == cbuiltin_lookup_scalar(name, length);
            name[b] ^= 1;
        }
        passed &= cbuiltin_lookup(name, length - 1) == cbuiltin_lookup_scalar(name, length - 1);
    }
    passed &= cbuiltin_lookup(0, 0) == CBUILTIN_NONE && cbuiltin_lookup_scalar(0, 0) == CBUILTIN_NONE;
    passed &= cbuiltin_lookup("__builtin_memcpy", 16) == CBUILTIN_NONE;
    cbuiltin_fold((CBuiltinIntegers){0}, (CBuiltinIntegerResults){0}, 0);
    cbuiltin_fold_scalar((CBuiltinIntegers){0}, (CBuiltinIntegerResults){0}, 0);
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, size);
    }
    printf("builtin names, guarded columns and tails 0..33: %s\n", passed ? "pass" : "FAIL");
    return passed;
}

static int run_builtin_tests(int argc, char** argv)
{
    uint64_t seed = 148;
    bool valid = argc == 0;
    if (argc == 2 && strcmp(argv[0], "--seed") == 0)
    {
        char* end;
        seed = strtoull(argv[1], &end, 10);
        valid = *argv[1] != 0 && *end == 0 && seed != 0;
    }
    bool passed = false;
    if (valid)
    {
        passed = cbuiltin_test_pages();
        passed &= cbuiltin_test_boundaries();
        passed &= cbuiltin_test_generated(seed);
    }
    else
    {
        fprintf(stderr, "usage: test-builtins [--seed N (nonzero)]\n");
    }
    return passed ? 0 : 1;
}
