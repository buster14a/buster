// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Constants of the `_BitInt(N)` types past 128 bits (#1447): their representation, arithmetic, conversions and decimal
// printing. Included by c_check_actions.c after CheckValue, check_action_mask and check_action_set_fault.
//
// Representation. A wide constant is CHECK_CONSTANT_INTEGER of its `_BitInt(N)` type, N > 128. Its N bits are
// ceil(N / 64) 64-bit limbs, the least significant first, with the bits past N zero, as `integer` holds a narrow
// constant masked to its width. The limbs are immutable: the value points to them from `floating_storage[0]`, which no
// integer constant uses, and `integer` keeps their low 128 bits, so that a conversion to a type of up to 128 bits reads
// `integer` as for any other integer. CheckValue stays 96 bytes (check_value_copy). The limbs live in one region of
// reserved address space for the process's lifetime (check_wide_region), bump-allocated by an atomic add and committed
// by the kernel as they are touched, and never freed: a value's pointer stays valid wherever its row is copied (lane
// outputs, deferred bodies' merges, stream rows, initializer plans, emission). check_wide_limbs reads a value's limbs
// back only from that region, so a value of a wide type that some path built without them is no constant rather than
// one read from another payload.
//
// Entry points: check_wide_type (the gatekeeper's twin: an integer type past 128 bits), check_wide_truth,
// check_wide_convert (any conversion into or out of a wide type), check_wide_fold (an operator on wide constants, or a
// shift by a wide count), check_wide_decimal (clang's printing of a value) and check_wide_bytes (the object
// representation, for static data).
//
// Kernels. The bitwise operations, sums, differences and negations, compares and shifts take 8 limbs a zmm, with masked
// tails, and each has an exact scalar twin (CheckWideKernels.scalar, the checker's scalar path; test-wide compares
// them). A sum's carries cross the limbs of a zmm through its compare masks: a limb generates a carry where its sum
// wrapped and propagates one where its sum is all ones, and the carries into the limbs are ((G << 1 | c) + P) ^ P,
// the mask add. Products (64 x 64 -> 128-bit limb products, which AVX-512 has no instruction for), quotients and
// remainders (shift and subtract) and decimal printing stay scalar. Faults follow check_action_fold_integer and GNU's
// check_integer_extension, so the same contexts fold or refuse as clang's evaluator does.
#ifndef BUSTER_C_CHECK_WIDE_INCLUDED
#define BUSTER_C_CHECK_WIDE_INCLUDED

#include <sys/mman.h>

// C23's widest _BitInt, clang's BITINT_MAXWIDTH (c_preprocessor_impl.c's __BITINT_MAXWIDTH__).
#define BUSTER_CHECK_WIDE_MAX_WIDTH 8388608u
#define BUSTER_CHECK_WIDE_LIMB_BITS 64u
#define BUSTER_CHECK_WIDE_VECTOR_LIMBS 8u
// The address space the region reserves: room for 4096 constants of the widest type. A constant that doesn't fit is
// no constant (CHECK_FAULT_UNSUPPORTED_CONSTANT), never a truncated one.
#define BUSTER_CHECK_WIDE_REGION_BYTES (UINT64_C(1) << 32)

typedef struct CheckWideRegion CheckWideRegion;
struct CheckWideRegion
{
    // The reservation, made by the first constant that needs it; null before, or if the reservation failed.
    uint8_t* base;
    uint64_t used;
    bool failed;
};

// One region for the process: main.c's units share it, the checker's unit holds it.
#if defined(BUSTER_UNIT) && BUSTER_UNIT != BUSTER_UNIT_CHECKER
extern CheckWideRegion check_wide_region;
#elif defined(BUSTER_UNIT)
CheckWideRegion check_wide_region;
#else
static CheckWideRegion check_wide_region;
#endif

static uint32_t check_wide_count(uint32_t width)
{
    return (width + BUSTER_CHECK_WIDE_LIMB_BITS - 1) / BUSTER_CHECK_WIDE_LIMB_BITS;
}

// The bits of a width's top limb.
static uint64_t check_wide_top_mask(uint32_t width)
{
    const uint32_t bits = width % BUSTER_CHECK_WIDE_LIMB_BITS;
    return bits == 0 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
}

// An integer type past 128 bits, with its width and signedness: the integer types check_action_integer_type refuses.
static bool check_wide_type(const TypeTable* table, TypeId type, uint32_t* width, bool* is_signed)
{
    const TypeId base = type_unqualified(type_strip_aligned(table, type));
    bool result = type_kind(table, base) == BUSTER_TYPE_KIND_BIT_INT;
    *width = 0;
    *is_signed = false;
    if (result)
    {
        uint64_t rank = 0;
        uint64_t bits = 0;
        type_integer_properties(table, base, &rank, &bits, is_signed);
        result = bits > 128 && bits <= BUSTER_CHECK_WIDE_MAX_WIDTH;
        *width = result ? (uint32_t)bits : 0;
    }
    return result;
}

// Room for `count` limbs that live as long as the process, or null when the region is full or can't be reserved.
static __attribute__((noinline)) uint64_t* check_wide_allocate(uint32_t count)
{
    uint8_t* base = __atomic_load_n(&check_wide_region.base, __ATOMIC_ACQUIRE);
    if (base == NULL && !__atomic_load_n(&check_wide_region.failed, __ATOMIC_RELAXED))
    {
        void* mapping = mmap(NULL, BUSTER_CHECK_WIDE_REGION_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        uint8_t* reserved = mapping == MAP_FAILED ? NULL : (uint8_t*)mapping;
        uint8_t* expected = NULL;
        if (reserved == NULL)
        {
            __atomic_store_n(&check_wide_region.failed, true, __ATOMIC_RELAXED);
            base = __atomic_load_n(&check_wide_region.base, __ATOMIC_ACQUIRE);
        }
        else if (__atomic_compare_exchange_n(&check_wide_region.base, &expected, reserved, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        {
            base = reserved;
        }
        else
        {
            // Another thread reserved it first.
            munmap(reserved, BUSTER_CHECK_WIDE_REGION_BYTES);
            base = expected;
        }
    }
    uint64_t* result = NULL;
    if (base != NULL && count != 0)
    {
        // A header limb before them holds their count (check_wide_limbs, check_wide_same).
        const uint64_t bytes = ((uint64_t)count + 1) * sizeof(uint64_t);
        const uint64_t at = __atomic_fetch_add(&check_wide_region.used, bytes, __ATOMIC_RELAXED);
        result = at <= BUSTER_CHECK_WIDE_REGION_BYTES - bytes && bytes <= BUSTER_CHECK_WIDE_REGION_BYTES ? (uint64_t*)(base + at) + 1 : NULL;
        if (result != NULL)
        {
            result[-1] = count;
        }
    }
    return result;
}

// The limbs a value points to and their allocated count, or null when its pointer isn't the region's.
static const uint64_t* check_wide_held(const CheckValue* value, uint64_t* count)
{
    const uint8_t* base = __atomic_load_n(&check_wide_region.base, __ATOMIC_ACQUIRE);
    const uint64_t address = value->floating_storage[0];
    const uint64_t start = (uint64_t)(uintptr_t)base;
    const bool inside = base != NULL && value->constant_kind == CHECK_CONSTANT_INTEGER && (address & 7u) == 0 && address > start &&
                        address - start <= BUSTER_CHECK_WIDE_REGION_BYTES - sizeof(uint64_t);
    const uint64_t* limbs = inside ? (const uint64_t*)(uintptr_t)address : NULL;
    *count = limbs != NULL ? limbs[-1] : 0;
    const bool fits = limbs != NULL && *count <= (BUSTER_CHECK_WIDE_REGION_BYTES - (address - start)) / sizeof(uint64_t);
    *count = fits ? *count : 0;
    return fits ? limbs : NULL;
}

// Whether two values hold the same wide number (1), different ones or one of them none (0), or neither holds limbs
// (-1), whatever their pointers: the limbs past a shorter allocation's are zeros, as the bits past a width are. For
// comparisons of whole values, as the scalar and vector paths' (test-checker's check_test_value_equal).
static __attribute__((unused)) int32_t check_wide_same(const CheckValue* a, const CheckValue* b)
{
    uint64_t a_count = 0;
    uint64_t b_count = 0;
    const uint64_t* x = check_wide_held(a, &a_count);
    const uint64_t* y = check_wide_held(b, &b_count);
    int32_t result = x == NULL && y == NULL ? -1 : 0;
    if (x != NULL && y != NULL)
    {
        bool same = true;
        const uint64_t count = a_count > b_count ? a_count : b_count;
        for (uint64_t i = 0; i < count; i += 1)
        {
            same &= (i < a_count ? x[i] : 0) == (i < b_count ? y[i] : 0);
        }
        result = same ? 1 : 0;
    }
    return result;
}

// A wide constant's `count` limbs, or null when the value holds none: its pointer isn't the region's.
static const uint64_t* check_wide_limbs(const CheckValue* value, uint32_t count)
{
    uint64_t held = 0;
    const uint64_t* limbs = check_wide_held(value, &held);
    return held >= count ? limbs : NULL;
}

// `value` becomes the wide constant whose limbs are `limbs` (from check_wide_allocate, `count` of them, at least 3).
static void check_wide_publish(CheckValue* value, const uint64_t* limbs)
{
    value->constant_kind = CHECK_CONSTANT_INTEGER;
    value->integer = (unsigned __int128)limbs[1] << 64 | limbs[0];
    value->floating_storage[0] = (uint64_t)(uintptr_t)limbs;
    value->floating_storage[1] = 0;
}

// --- Kernels: `count` limbs, vector with a scalar twin. ---

typedef enum CheckWideBitwise
{
    CHECK_WIDE_AND,
    CHECK_WIDE_OR,
    CHECK_WIDE_XOR,
    CHECK_WIDE_NOT,
} CheckWideBitwise;

static void check_wide_bitwise_scalar(CheckWideBitwise op, uint64_t* destination, const uint64_t* a, const uint64_t* b, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint64_t x = a[i];
        const uint64_t y = b[i];
        destination[i] = op == CHECK_WIDE_AND ? x & y : op == CHECK_WIDE_OR ? x | y : op == CHECK_WIDE_XOR ? x ^ y : ~x;
    }
}

static void check_wide_bitwise_vector(CheckWideBitwise op, uint64_t* destination, const uint64_t* a, const uint64_t* b, uint32_t count)
{
#if BUSTER_HOST_VECTOR
    const uint8_t imm = op == CHECK_WIDE_AND ? 0xc0 : op == CHECK_WIDE_OR ? 0xfc : op == CHECK_WIDE_XOR ? 0x3c : 0x0f;
    for (uint32_t i = 0; i < count; i += BUSTER_CHECK_WIDE_VECTOR_LIMBS)
    {
        const uint32_t left = count - i;
        const __mmask8 mask = left >= BUSTER_CHECK_WIDE_VECTOR_LIMBS ? (__mmask8)0xff : (__mmask8)((1u << left) - 1);
        const __m512i x = _mm512_maskz_loadu_epi64(mask, a + i);
        const __m512i y = _mm512_maskz_loadu_epi64(mask, b + i);
        __m512i r;
        switch (imm)
        {
            case 0xc0: r = _mm512_ternarylogic_epi64(x, y, y, 0xc0); break;
            case 0xfc: r = _mm512_ternarylogic_epi64(x, y, y, 0xfc); break;
            case 0x3c: r = _mm512_ternarylogic_epi64(x, y, y, 0x3c); break;
            default: r = _mm512_ternarylogic_epi64(x, y, y, 0x0f); break;
        }
        _mm512_mask_storeu_epi64(destination + i, mask, r);
    }
#else
    check_wide_bitwise_scalar(op, destination, a, b, count);
#endif
}

// destination = a + (invert ? ~b : b) + carry, over `count` limbs; returns the carry out of the top limb.
static uint32_t check_wide_add_scalar(uint64_t* destination, const uint64_t* a, const uint64_t* b, uint32_t count, bool invert, uint32_t carry)
{
    uint32_t c = carry;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint64_t y = invert ? ~b[i] : b[i];
        const unsigned __int128 sum = (unsigned __int128)a[i] + y + c;
        destination[i] = (uint64_t)sum;
        c = (uint32_t)(sum >> 64);
    }
    return c;
}

static uint32_t check_wide_add_vector(uint64_t* destination, const uint64_t* a, const uint64_t* b, uint32_t count, bool invert, uint32_t carry)
{
    uint32_t c = carry;
#if BUSTER_HOST_VECTOR
    const __m512i ones = _mm512_set1_epi64(-1);
    const __m512i flip = invert ? ones : _mm512_setzero_si512();
    for (uint32_t i = 0; i < count; i += BUSTER_CHECK_WIDE_VECTOR_LIMBS)
    {
        const uint32_t left = count - i;
        const __mmask8 mask = left >= BUSTER_CHECK_WIDE_VECTOR_LIMBS ? (__mmask8)0xff : (__mmask8)((1u << left) - 1);
        const __m512i x = _mm512_maskz_loadu_epi64(mask, a + i);
        const __m512i y = _mm512_xor_si512(_mm512_maskz_loadu_epi64(mask, b + i), flip);
        const __m512i sum = _mm512_add_epi64(x, y);
        // Generate: the limb's sum wrapped. Propagate: it is all ones, so a carry in passes through.
        const uint32_t generate = (uint32_t)(_mm512_cmplt_epu64_mask(sum, x) & mask);
        const uint32_t propagate = (uint32_t)(_mm512_cmpeq_epi64_mask(sum, ones) & mask);
        const uint32_t total = ((generate << 1) | c) + propagate;
        const __mmask8 carries = (__mmask8)((total ^ propagate) & mask);
        const __m512i result = _mm512_mask_sub_epi64(sum, carries, sum, ones);
        _mm512_mask_storeu_epi64(destination + i, mask, result);
        c = (total >> left) & 1u;
        c = left >= BUSTER_CHECK_WIDE_VECTOR_LIMBS ? (total >> BUSTER_CHECK_WIDE_VECTOR_LIMBS) & 1u : c;
    }
#else
    c = check_wide_add_scalar(destination, a, b, count, invert, carry);
#endif
    return c;
}

// Unsigned compare of two `count`-limb numbers: -1, 0 or 1.
static int32_t check_wide_compare_scalar(const uint64_t* a, const uint64_t* b, uint32_t count)
{
    int32_t result = 0;
    for (uint32_t i = count; result == 0 && i != 0; i -= 1)
    {
        result = a[i - 1] < b[i - 1] ? -1 : a[i - 1] > b[i - 1] ? 1 : 0;
    }
    return result;
}

static int32_t check_wide_compare_vector(const uint64_t* a, const uint64_t* b, uint32_t count)
{
    int32_t result = 0;
#if BUSTER_HOST_VECTOR
    // From the top chunk down; the first that differs decides, by its highest differing limb.
    uint32_t end = count;
    while (result == 0 && end != 0)
    {
        const uint32_t take = end >= BUSTER_CHECK_WIDE_VECTOR_LIMBS ? BUSTER_CHECK_WIDE_VECTOR_LIMBS : end;
        const uint32_t start = end - take;
        const __mmask8 mask = (__mmask8)((1u << take) - 1);
        const __m512i x = _mm512_maskz_loadu_epi64(mask, a + start);
        const __m512i y = _mm512_maskz_loadu_epi64(mask, b + start);
        const uint32_t less = _mm512_cmplt_epu64_mask(x, y);
        const uint32_t greater = _mm512_cmpgt_epu64_mask(x, y);
        const uint32_t differ = less | greater;
        if (differ != 0)
        {
            const uint32_t top = 31u - (uint32_t)__builtin_clz(differ);
            result = ((less >> top) & 1u) != 0 ? -1 : 1;
        }
        end = start;
    }
#else
    result = check_wide_compare_scalar(a, b, count);
#endif
    return result;
}

// destination = a << amount over `count` limbs (amount below count * 64), bits shifted past the top dropped.
static void check_wide_shift_left_scalar(uint64_t* destination, const uint64_t* a, uint32_t count, uint32_t amount)
{
    const uint32_t limbs = amount / BUSTER_CHECK_WIDE_LIMB_BITS;
    const uint32_t bits = amount % BUSTER_CHECK_WIDE_LIMB_BITS;
    for (uint32_t i = count; i != 0; i -= 1)
    {
        const uint32_t index = i - 1;
        const uint64_t high = index >= limbs ? a[index - limbs] : 0;
        const uint64_t low = index >= limbs + 1 ? a[index - limbs - 1] : 0;
        destination[index] = bits == 0 ? high : (high << bits) | (low >> (BUSTER_CHECK_WIDE_LIMB_BITS - bits));
    }
}

static void check_wide_shift_left_vector(uint64_t* destination, const uint64_t* a, uint32_t count, uint32_t amount)
{
#if BUSTER_HOST_VECTOR
    const uint32_t limbs = amount / BUSTER_CHECK_WIDE_LIMB_BITS;
    const __m512i bits = _mm512_set1_epi64(amount % BUSTER_CHECK_WIDE_LIMB_BITS);
    const __m512i lane = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);
    for (uint32_t i = 0; i < count; i += BUSTER_CHECK_WIDE_VECTOR_LIMBS)
    {
        const uint32_t left = count - i;
        const __mmask8 mask = left >= BUSTER_CHECK_WIDE_VECTOR_LIMBS ? (__mmask8)0xff : (__mmask8)((1u << left) - 1);
        // Lane j reads limb i + j - limbs (high) and the one below it (low), where those are limbs of `a`.
        const __m512i index = _mm512_add_epi64(lane, _mm512_set1_epi64((int64_t)i - (int64_t)limbs));
        const __mmask8 high_valid = (__mmask8)(mask & _mm512_cmpge_epi64_mask(index, _mm512_setzero_si512()));
        const __mmask8 low_valid = (__mmask8)(mask & _mm512_cmpge_epi64_mask(index, _mm512_set1_epi64(1)));
        const int64_t base = (int64_t)i - (int64_t)limbs;
        // The loads are masked to the limbs of `a`; a lane whose limb would be below it loads nothing.
        const __m512i high = _mm512_maskz_loadu_epi64(high_valid, (const void*)((uintptr_t)a + (uint64_t)(base * (int64_t)sizeof(uint64_t))));
        const __m512i low = _mm512_maskz_loadu_epi64(low_valid, (const void*)((uintptr_t)a + (uint64_t)((base - 1) * (int64_t)sizeof(uint64_t))));
        _mm512_mask_storeu_epi64(destination + i, mask, _mm512_shldv_epi64(high, low, bits));
    }
#else
    check_wide_shift_left_scalar(destination, a, count, amount);
#endif
}

// destination = a >> amount over `count` limbs (amount below count * 64), zeros shifted in.
static void check_wide_shift_right_scalar(uint64_t* destination, const uint64_t* a, uint32_t count, uint32_t amount)
{
    const uint32_t limbs = amount / BUSTER_CHECK_WIDE_LIMB_BITS;
    const uint32_t bits = amount % BUSTER_CHECK_WIDE_LIMB_BITS;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint64_t low = i + limbs < count ? a[i + limbs] : 0;
        const uint64_t high = i + limbs + 1 < count ? a[i + limbs + 1] : 0;
        destination[i] = bits == 0 ? low : (low >> bits) | (high << (BUSTER_CHECK_WIDE_LIMB_BITS - bits));
    }
}

static void check_wide_shift_right_vector(uint64_t* destination, const uint64_t* a, uint32_t count, uint32_t amount)
{
#if BUSTER_HOST_VECTOR
    const uint32_t limbs = amount / BUSTER_CHECK_WIDE_LIMB_BITS;
    const __m512i bits = _mm512_set1_epi64(amount % BUSTER_CHECK_WIDE_LIMB_BITS);
    const __m512i lane = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);
    const __m512i limit = _mm512_set1_epi64(count);
    for (uint32_t i = 0; i < count; i += BUSTER_CHECK_WIDE_VECTOR_LIMBS)
    {
        const uint32_t left = count - i;
        const __mmask8 mask = left >= BUSTER_CHECK_WIDE_VECTOR_LIMBS ? (__mmask8)0xff : (__mmask8)((1u << left) - 1);
        // Lane j reads limb i + j + limbs (low) and the one above it (high), where those are limbs of `a`.
        const __m512i index = _mm512_add_epi64(lane, _mm512_set1_epi64((int64_t)i + limbs));
        const __mmask8 low_valid = (__mmask8)(mask & _mm512_cmplt_epi64_mask(index, limit));
        const __mmask8 high_valid = (__mmask8)(mask & _mm512_cmplt_epi64_mask(_mm512_add_epi64(index, _mm512_set1_epi64(1)), limit));
        const uint64_t base = (uint64_t)i + limbs;
        const __m512i low = _mm512_maskz_loadu_epi64(low_valid, (const void*)((uintptr_t)a + base * sizeof(uint64_t)));
        const __m512i high = _mm512_maskz_loadu_epi64(high_valid, (const void*)((uintptr_t)a + (base + 1) * sizeof(uint64_t)));
        _mm512_mask_storeu_epi64(destination + i, mask, _mm512_shrdv_epi64(low, high, bits));
    }
#else
    check_wide_shift_right_scalar(destination, a, count, amount);
#endif
}

// The kernels' choice: the checker's scalar path takes the twins.
typedef struct CheckWideKernels CheckWideKernels;
struct CheckWideKernels
{
    bool scalar;
};

static void check_wide_bitwise(CheckWideKernels kernels, CheckWideBitwise op, uint64_t* destination, const uint64_t* a, const uint64_t* b, uint32_t count)
{
    if (kernels.scalar)
    {
        check_wide_bitwise_scalar(op, destination, a, b, count);
    }
    else
    {
        check_wide_bitwise_vector(op, destination, a, b, count);
    }
}

static uint32_t check_wide_add(CheckWideKernels kernels, uint64_t* destination, const uint64_t* a, const uint64_t* b, uint32_t count, bool invert, uint32_t carry)
{
    const uint32_t result = kernels.scalar ? check_wide_add_scalar(destination, a, b, count, invert, carry) : check_wide_add_vector(destination, a, b, count, invert, carry);
    return result;
}

static int32_t check_wide_compare(CheckWideKernels kernels, const uint64_t* a, const uint64_t* b, uint32_t count)
{
    const int32_t result = kernels.scalar ? check_wide_compare_scalar(a, b, count) : check_wide_compare_vector(a, b, count);
    return result;
}

static void check_wide_shift(CheckWideKernels kernels, bool left, uint64_t* destination, const uint64_t* a, uint32_t count, uint32_t amount)
{
    if (left && kernels.scalar)
    {
        check_wide_shift_left_scalar(destination, a, count, amount);
    }
    else if (left)
    {
        check_wide_shift_left_vector(destination, a, count, amount);
    }
    else if (kernels.scalar)
    {
        check_wide_shift_right_scalar(destination, a, count, amount);
    }
    else
    {
        check_wide_shift_right_vector(destination, a, count, amount);
    }
}

// --- Scalar parts: products, quotients and remainders, and their helpers. ---

static bool check_wide_is_zero(const uint64_t* a, uint32_t count)
{
    uint64_t any = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        any |= a[i];
    }
    return any == 0;
}

// The number of significant bits of a `count`-limb number.
static uint32_t check_wide_bits(const uint64_t* a, uint32_t count)
{
    uint32_t result = 0;
    for (uint32_t i = count; result == 0 && i != 0; i -= 1)
    {
        result = a[i - 1] != 0 ? (i - 1) * BUSTER_CHECK_WIDE_LIMB_BITS + 64u - (uint32_t)__builtin_clzll(a[i - 1]) : 0;
    }
    return result;
}

// destination (2 * count limbs) = a * b, both `count` limbs.
static void check_wide_multiply(uint64_t* destination, const uint64_t* a, const uint64_t* b, uint32_t count)
{
    for (uint32_t i = 0; i < 2 * count; i += 1)
    {
        destination[i] = 0;
    }
    for (uint32_t i = 0; i < count; i += 1)
    {
        uint64_t carry = 0;
        const uint64_t x = a[i];
        for (uint32_t j = 0; j < count && x != 0; j += 1)
        {
            const unsigned __int128 product = (unsigned __int128)x * b[j] + destination[i + j] + carry;
            destination[i + j] = (uint64_t)product;
            carry = (uint64_t)(product >> 64);
        }
        destination[i + count] = x != 0 ? carry : destination[i + count];
    }
}

// quotient and remainder (`count` limbs each) of a / b, b nonzero, by shift and subtract from a's top bit.
static void check_wide_divide(uint64_t* quotient, uint64_t* remainder, const uint64_t* a, const uint64_t* b, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        quotient[i] = 0;
        remainder[i] = 0;
    }
    for (uint32_t bit = check_wide_bits(a, count); bit != 0; bit -= 1)
    {
        const uint32_t index = bit - 1;
        // remainder = remainder << 1 | a's bit; it stays below 2b, so its top bit never falls off below count limbs.
        uint64_t carry = (a[index / BUSTER_CHECK_WIDE_LIMB_BITS] >> (index % BUSTER_CHECK_WIDE_LIMB_BITS)) & 1u;
        uint64_t over = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint64_t limb = remainder[i];
            remainder[i] = (limb << 1) | carry;
            carry = limb >> 63;
        }
        over = carry;
        if (over != 0 || check_wide_compare_scalar(remainder, b, count) >= 0)
        {
            check_wide_add_scalar(remainder, remainder, b, count, true, 1);
            quotient[index / BUSTER_CHECK_WIDE_LIMB_BITS] |= UINT64_C(1) << (index % BUSTER_CHECK_WIDE_LIMB_BITS);
        }
    }
}

// a's two's complement negation in place, `count` limbs, masked to `width`.
static void check_wide_negate_in_place(uint64_t* a, uint32_t count, uint32_t width)
{
    uint32_t carry = 1;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const unsigned __int128 sum = (unsigned __int128)(~a[i]) + carry;
        a[i] = (uint64_t)sum;
        carry = (uint32_t)(sum >> 64);
    }
    a[count - 1] &= check_wide_top_mask(width);
}

static bool check_wide_negative(const uint64_t* a, uint32_t width, bool is_signed)
{
    return is_signed && ((a[(width - 1) / BUSTER_CHECK_WIDE_LIMB_BITS] >> ((width - 1) % BUSTER_CHECK_WIDE_LIMB_BITS)) & 1u) != 0;
}

// A number's limbs, `count` of them, from an integer constant of up to 128 bits or a wide one, extended from its width
// as its signedness says, into `destination`. False when the value is no integer constant with limbs.
static bool check_wide_operand(const TypeTable* table, const CheckValue* value, uint64_t* destination, uint32_t count)
{
    uint32_t width = 0;
    bool is_signed = false;
    bool valid = value->constant_kind == CHECK_CONSTANT_INTEGER;
    if (valid && check_action_integer_type(table, value->type, &width, &is_signed))
    {
        const unsigned __int128 mask = check_action_mask(width);
        const unsigned __int128 bits = value->integer & mask;
        const bool negative = is_signed && ((bits >> (width - 1)) & 1u) != 0;
        const unsigned __int128 extended = negative ? bits | ~mask : bits;
        const uint64_t fill = negative ? UINT64_MAX : 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            destination[i] = i == 0 ? (uint64_t)extended : i == 1 ? (uint64_t)(extended >> 64) : fill;
        }
    }
    else if (valid && check_wide_type(table, value->type, &width, &is_signed))
    {
        const uint32_t own = check_wide_count(width);
        const uint64_t* limbs = check_wide_limbs(value, own);
        valid = limbs != NULL;
        if (valid)
        {
            const bool negative = check_wide_negative(limbs, width, is_signed);
            const uint64_t fill = negative ? UINT64_MAX : 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                uint64_t limb = i < own ? limbs[i] : fill;
                limb |= i + 1 == own && negative ? ~check_wide_top_mask(width) : 0;
                destination[i] = limb;
            }
        }
    }
    else
    {
        valid = false;
    }
    return valid;
}

static bool check_wide_truth(const TypeTable* table, const CheckValue* value, bool* truth)
{
    uint32_t width = 0;
    bool is_signed = false;
    const bool wide = check_wide_type(table, value->type, &width, &is_signed);
    const uint32_t count = check_wide_count(width);
    const uint64_t* limbs = wide ? check_wide_limbs(value, count) : NULL;
    const bool known = limbs != NULL && value->fault == CHECK_FAULT_NONE;
    *truth = known && !check_wide_is_zero(limbs, count);
    return known;
}

// A wide constant's value as an int64_t, when it is one (check_action_i64's cold branch).
static __attribute__((noinline, cold)) bool check_wide_i64(const TypeTable* table, const CheckValue* value, int64_t* number)
{
    uint32_t width = 0;
    bool is_signed = false;
    const bool wide = value->fault == CHECK_FAULT_NONE && check_wide_type(table, value->type, &width, &is_signed);
    const uint32_t count = check_wide_count(width);
    const uint64_t* limbs = wide ? check_wide_limbs(value, count) : NULL;
    bool fits = limbs != NULL;
    *number = 0;
    if (fits)
    {
        // Every limb above the lowest is the sign's fill, within the width, and the lowest's top bit agrees with it.
        const bool negative = check_wide_negative(limbs, width, is_signed);
        for (uint32_t i = 1; i < count; i += 1)
        {
            const uint64_t fill = negative ? (i + 1 == count ? check_wide_top_mask(width) : UINT64_MAX) : 0;
            fits &= limbs[i] == fill;
        }
        fits &= ((limbs[0] >> 63) != 0) == negative;
        *number = fits ? (int64_t)limbs[0] : 0;
    }
    return fits;
}

// --- Conversions. ---

// A wide number's value as a floating type's, rounded once to nearest even: its top 128 bits with a sticky bit for the
// rest, which a 128-bit conversion rounds exactly as the whole would, scaled by the bits below them.
static long double check_wide_to_floating(const uint64_t* magnitude, uint32_t count, bool negative, TypeId to)
{
    const uint32_t bits = check_wide_bits(magnitude, count);
    unsigned __int128 top = 0;
    int32_t scale = 0;
    if (bits <= 128)
    {
        top = (unsigned __int128)magnitude[1] << 64 | magnitude[0];
    }
    else
    {
        const uint32_t shift = bits - 128;
        uint64_t window[3];
        for (uint32_t i = 0; i < 3; i += 1)
        {
            const uint32_t index = shift / BUSTER_CHECK_WIDE_LIMB_BITS + i;
            window[i] = index < count ? magnitude[index] : 0;
        }
        const uint32_t within = shift % BUSTER_CHECK_WIDE_LIMB_BITS;
        const uint64_t low = within == 0 ? window[0] : (window[0] >> within) | (window[1] << (BUSTER_CHECK_WIDE_LIMB_BITS - within));
        const uint64_t high = within == 0 ? window[1] : (window[1] >> within) | (window[2] << (BUSTER_CHECK_WIDE_LIMB_BITS - within));
        bool sticky = within != 0 && (window[0] & ((UINT64_C(1) << within) - 1)) != 0;
        for (uint32_t i = 0; i < shift / BUSTER_CHECK_WIDE_LIMB_BITS; i += 1)
        {
            sticky |= magnitude[i] != 0;
        }
        top = ((unsigned __int128)high << 64 | low) | (sticky ? 1u : 0u);
        scale = (int32_t)shift;
    }
    long double number;
    if (to == BUSTER_TYPE_FLOAT)
    {
        number = host_ldexp((double)(float)top, scale);
        number = (float)number;
    }
    else if (to == BUSTER_TYPE_DOUBLE)
    {
        number = host_ldexp((double)top, scale);
    }
    else
    {
        number = host_ldexpl((long double)top, scale);
    }
    return negative ? -number : number;
}

// A conversion into or out of a wide integer type (check_action_convert_plain's cold branch). Returns whether it
// converted: false leaves the value to the caller's other branches (a source and a destination both of up to 128 bits).
static __attribute__((noinline, cold)) bool check_wide_convert(const TypeTable* table, CheckValue* value, TypeId destination, uint32_t node)
{
    const TypeId source = value->type;
    uint32_t source_width = 0;
    uint32_t target_width = 0;
    bool source_signed = false;
    bool target_signed = false;
    const bool source_wide = check_wide_type(table, source, &source_width, &source_signed);
    const bool target_wide = check_wide_type(table, destination, &target_width, &target_signed);
    const TypeId to = type_unqualified(type_strip_aligned(table, destination));
    const uint8_t target_kind = type_kind(table, to);
    const bool source_narrow = !source_wide && check_action_integer_type(table, source, &source_width, &source_signed);
    const bool target_narrow = !target_wide && check_action_integer_type(table, destination, &target_width, &target_signed);
    const bool handled = source_wide || target_wide;
    if (handled && value->constant_kind == CHECK_CONSTANT_INTEGER && target_wide && (source_wide || source_narrow))
    {
        const uint32_t count = check_wide_count(target_width);
        uint64_t* limbs = check_wide_allocate(count);
        if (limbs != NULL && check_wide_operand(table, value, limbs, count))
        {
            limbs[count - 1] &= check_wide_top_mask(target_width);
            check_wide_publish(value, limbs);
        }
        else
        {
            check_action_set_fault(value, CHECK_FAULT_UNSUPPORTED_CONSTANT, node);
        }
    }
    else if (handled && value->constant_kind == CHECK_CONSTANT_INTEGER && source_wide && target_narrow)
    {
        // The low bits, which `integer` holds; a _Bool's truth is check_action_convert_plain's, before this.
        const bool known = check_wide_limbs(value, check_wide_count(source_width)) != NULL;
        if (known)
        {
            value->integer &= check_action_mask(target_width);
            value->floating_storage[0] = 0;
            value->floating_storage[1] = 0;
        }
        else
        {
            check_action_set_fault(value, CHECK_FAULT_UNSUPPORTED_CONSTANT, node);
        }
    }
    else if (handled && value->constant_kind == CHECK_CONSTANT_INTEGER && source_wide &&
             (to == BUSTER_TYPE_FLOAT || to == BUSTER_TYPE_DOUBLE || to == BUSTER_TYPE_LONG_DOUBLE))
    {
        const uint32_t count = check_wide_count(source_width);
        const uint64_t* limbs = check_wide_limbs(value, count);
        uint64_t* magnitude = limbs != NULL ? check_wide_allocate(count) : NULL;
        if (magnitude != NULL)
        {
            const bool negative = check_wide_negative(limbs, source_width, source_signed);
            for (uint32_t i = 0; i < count; i += 1)
            {
                magnitude[i] = limbs[i];
            }
            if (negative)
            {
                check_wide_negate_in_place(magnitude, count, source_width);
            }
            value->floating_storage[0] = 0;
            value->floating_storage[1] = 0;
            value->integer = 0;
            value->floating = check_wide_to_floating(magnitude, count, negative, to);
            value->constant_kind = CHECK_CONSTANT_FLOATING;
        }
        else
        {
            check_action_set_fault(value, CHECK_FAULT_UNSUPPORTED_CONSTANT, node);
        }
    }
    else if (handled && value->constant_kind == CHECK_CONSTANT_FLOATING && target_wide &&
             (type_unqualified(type_strip_aligned(table, source)) == BUSTER_TYPE_FLOAT || type_unqualified(type_strip_aligned(table, source)) == BUSTER_TYPE_DOUBLE ||
              type_unqualified(type_strip_aligned(table, source)) == BUSTER_TYPE_LONG_DOUBLE))
    {
        // Truncated toward zero; a value outside the type's range is GNU's saturated constant, a NaN 0, as
        // check_action_convert_plain's narrow ones.
        const uint32_t count = check_wide_count(target_width);
        uint64_t* limbs = check_wide_allocate(count);
        const long double input = value->floating;
        if (limbs == NULL)
        {
            check_action_set_fault(value, CHECK_FAULT_UNSUPPORTED_CONSTANT, node);
        }
        else
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                limbs[i] = 0;
            }
            const bool negative = input < 0;
            const long double magnitude = negative ? -input : input;
            int exponent = 0;
            const long double fraction = __builtin_isfinite(magnitude) && magnitude >= 1 ? host_frexpl(magnitude, &exponent) : 0;
            const uint32_t value_bits = magnitude >= 1 && __builtin_isfinite(magnitude) ? (uint32_t)exponent : 0;
            const uint32_t limit = target_signed ? target_width - 1 : target_width;
            // In range: below 2^limit, or the signed minimum exactly.
            const bool minimum = target_signed && negative && value_bits == limit + 1 && fraction == 0.5L;
            const bool in_range = __builtin_isfinite(input) && (value_bits <= limit || minimum) && (!negative || target_signed || value_bits == 0);
            if (in_range && value_bits != 0)
            {
                // The 64-bit significand, placed at its exponent.
                const uint64_t significand = (uint64_t)host_ldexpl(fraction, 64);
                const uint32_t shift = value_bits > 64 ? value_bits - 64 : 0;
                const uint64_t placed = value_bits >= 64 ? significand : significand >> (64 - value_bits);
                limbs[shift / BUSTER_CHECK_WIDE_LIMB_BITS] |= placed << (shift % BUSTER_CHECK_WIDE_LIMB_BITS);
                if (shift % BUSTER_CHECK_WIDE_LIMB_BITS != 0 && shift / BUSTER_CHECK_WIDE_LIMB_BITS + 1 < count)
                {
                    limbs[shift / BUSTER_CHECK_WIDE_LIMB_BITS + 1] |= placed >> (BUSTER_CHECK_WIDE_LIMB_BITS - shift % BUSTER_CHECK_WIDE_LIMB_BITS);
                }
                if (negative)
                {
                    check_wide_negate_in_place(limbs, count, target_width);
                }
            }
            else if (!in_range && !__builtin_isnan(input))
            {
                // Saturated: the signed type's minimum or maximum, or the unsigned one's 0 or maximum.
                const bool high = !negative;
                for (uint32_t i = 0; i < count; i += 1)
                {
                    limbs[i] = high ? UINT64_MAX : 0;
                }
                limbs[count - 1] &= check_wide_top_mask(target_width);
                const uint32_t top = (target_width - 1) / BUSTER_CHECK_WIDE_LIMB_BITS;
                const uint64_t sign = UINT64_C(1) << ((target_width - 1) % BUSTER_CHECK_WIDE_LIMB_BITS);
                limbs[top] = target_signed ? (high ? limbs[top] & ~sign : sign) : limbs[top];
            }
            limbs[count - 1] &= check_wide_top_mask(target_width);
            check_wide_publish(value, limbs);
            if (!in_range)
            {
                value->flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM);
                value->flags |= BUSTER_CHECK_GNU_CONSTANT | BUSTER_CHECK_GNU_FLOAT_RANGE;
            }
        }
    }
    else if (handled && value->constant_kind == CHECK_CONSTANT_INTEGER && source_wide && target_kind == BUSTER_TYPE_KIND_POINTER)
    {
        // The low address bits, as a narrow integer's (an absolute address).
        const bool known = check_wide_limbs(value, check_wide_count(source_width)) != NULL;
        if (known)
        {
            value->constant_kind = CHECK_CONSTANT_ADDRESS;
            value->object = 0;
            value->offset = (int64_t)(uint64_t)value->integer;
            value->integer = 0;
            value->floating_storage[0] = 0;
            value->floating_storage[1] = 0;
        }
        else
        {
            check_action_set_fault(value, CHECK_FAULT_UNSUPPORTED_CONSTANT, node);
        }
    }
    else if (handled && value->constant_kind == CHECK_CONSTANT_ADDRESS && target_wide && value->object == 0 &&
             type_kind(table, type_strip_aligned(table, source)) == BUSTER_TYPE_KIND_POINTER)
    {
        // An absolute address: its 64 bits, zero-extended.
        const uint32_t count = check_wide_count(target_width);
        uint64_t* limbs = check_wide_allocate(count);
        if (limbs != NULL)
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                limbs[i] = i == 0 ? (uint64_t)value->offset : 0;
            }
            check_wide_publish(value, limbs);
        }
        else
        {
            check_action_set_fault(value, CHECK_FAULT_UNSUPPORTED_CONSTANT, node);
        }
    }
    else if (handled && value->constant_kind != CHECK_CONSTANT_NONE && type_unqualified(source) != type_unqualified(destination))
    {
        // A symbolic address, a __float128 or a half-precision value: no constant of the other type.
        value->constant_kind = CHECK_CONSTANT_NONE;
        value->floating_storage[0] = 0;
    }
    return handled;
}

// --- Operators. ---

// A wide operator's operand count of limbs and result.
typedef struct CheckWideFold CheckWideFold;
struct CheckWideFold
{
    CheckOp op;
    uint32_t width;
    bool is_signed;
    uint32_t node;
    uint32_t extensions;
    CheckWideKernels kernels;
};

// The GNU folding of a faulted wide operation, as check_integer_extension's narrow one: a signed overflow wraps; a shift
// count past the width or negative shifts by width - 1 the other way, as clang's evaluator clamps it; a signed left
// shift that discards bits keeps what remains. `answer` holds the wrapped result where the fault leaves one.
static void check_wide_fold_result(const CheckWideFold* fold, CheckValue* value, uint64_t* answer, CheckFault fault, bool gnu_answer, uint32_t* warnings)
{
    const bool applied = fault != CHECK_FAULT_NONE && gnu_answer && (fold->extensions & BUSTER_CHECK_EXTENSION_GNU_CONSTANT) != 0;
    if (applied)
    {
        const bool ice_form = (value->flags & (BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM)) != 0;
        value->flags &= ~BUSTER_CHECK_ICE;
        value->flags |= BUSTER_CHECK_GNU_CONSTANT | (ice_form ? BUSTER_CHECK_GNU_ICE_FORM : 0);
        value->flags |= fold->op == CHECK_OP_DIVIDE || fold->op == CHECK_OP_REMAINDER ? BUSTER_CHECK_GNU_DIVISION : 0;
        check_wide_publish(value, answer);
    }
    else if (fault != CHECK_FAULT_NONE)
    {
        check_action_set_fault(value, fault, fold->node);
    }
    else
    {
        check_wide_publish(value, answer);
    }
    *warnings |= applied ? (fault == CHECK_FAULT_SIGNED_OVERFLOW ? CHECK_WARNING_INTEGER_OVERFLOW : 0) : 0;
}

// The shift count of `right` (an integer constant of any width): its magnitude saturated to UINT32_MAX and its sign.
static bool check_wide_shift_count(const TypeTable* table, const CheckValue* right, uint32_t* magnitude, bool* negative)
{
    uint32_t width = 0;
    bool is_signed = false;
    bool valid = right->constant_kind == CHECK_CONSTANT_INTEGER;
    *magnitude = 0;
    *negative = false;
    if (valid && check_action_integer_type(table, right->type, &width, &is_signed))
    {
        const unsigned __int128 mask = check_action_mask(width);
        const unsigned __int128 bits = right->integer & mask;
        *negative = is_signed && ((bits >> (width - 1)) & 1u) != 0;
        const unsigned __int128 size = *negative ? (0 - bits) & mask : bits;
        *magnitude = size > UINT32_MAX || (*negative && size == 0) ? UINT32_MAX : (uint32_t)size;
    }
    else if (valid && check_wide_type(table, right->type, &width, &is_signed))
    {
        const uint32_t count = check_wide_count(width);
        const uint64_t* limbs = check_wide_limbs(right, count);
        valid = limbs != NULL;
        if (valid)
        {
            *negative = check_wide_negative(limbs, width, is_signed);
            // The magnitude of a negative count is ~bits + 1; it is past 2^32 unless every limb above the lowest is
            // all ones within the width, which its low limb then decides.
            bool large = false;
            for (uint32_t i = 1; i < count; i += 1)
            {
                const uint64_t expected = *negative ? (i + 1 == count ? check_wide_top_mask(width) : UINT64_MAX) : 0;
                large |= limbs[i] != expected;
            }
            const uint64_t low = *negative ? 0 - limbs[0] : limbs[0];
            large |= *negative && limbs[0] == 0;
            *magnitude = large || low > UINT32_MAX ? UINT32_MAX : (uint32_t)low;
        }
    }
    else
    {
        valid = false;
    }
    return valid;
}

// `op` on constants where the left operand's type, the result's, is past 128 bits, or a narrow value shifted by a wide
// count. `left` and `right` are converted (the usual arithmetic conversions, or a shift's promotions); `right` is
// `left` for a unary operator. Returns whether it applied: false leaves the caller's unsupported fault.
static __attribute__((noinline, cold)) bool check_wide_fold(const TypeTable* table, CheckOp op, const CheckValue* left, const CheckValue* right, CheckValue* value,
                                                            uint32_t node, uint32_t extensions, bool scalar, uint32_t* warnings)
{
    uint32_t width = 0;
    bool is_signed = false;
    const bool shift = op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT;
    const bool wide = check_wide_type(table, left->type, &width, &is_signed);
    uint32_t right_width = 0;
    bool right_signed = false;
    const bool narrow_by_wide = !wide && shift && check_action_integer_type(table, left->type, &width, &is_signed) &&
                                check_wide_type(table, right->type, &right_width, &right_signed);
    bool handled = wide || narrow_by_wide;
    if (narrow_by_wide)
    {
        // A narrow value shifted by a wide count: the count saturated to 128 bits, which shifts as the exact one would.
        uint32_t magnitude = 0;
        bool negative = false;
        handled = check_wide_shift_count(table, right, &magnitude, &negative);
        if (handled)
        {
            const unsigned __int128 count = negative ? 0 - (unsigned __int128)magnitude : (unsigned __int128)magnitude;
            const CheckIntegerFold fold = {
                .active = true,
                .op = op,
                .left = left->integer,
                .right = count,
                .width = width,
                .right_width = 128,
                .is_signed = is_signed,
                .right_signed = negative,
                .node = node,
                .extensions = extensions,
            };
            *warnings |= check_action_fold_integer(fold, value);
        }
    }
    else if (wide)
    {
        const CheckWideFold fold = {
            .op = op,
            .width = width,
            .is_signed = is_signed,
            .node = node,
            .extensions = extensions,
            .kernels = { .scalar = scalar },
        };
        const uint32_t count = check_wide_count(width);
        const uint64_t top_mask = check_wide_top_mask(width);
        const bool comparison = op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL;
        // The operands, then room for a result and a product's or a quotient's work.
        uint64_t* a = check_wide_allocate(count);
        uint64_t* b = a != NULL ? check_wide_allocate(count) : NULL;
        const bool unary = op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT;
        const bool operands = b != NULL && check_wide_operand(table, left, a, count) && (unary || shift || check_wide_operand(table, right, b, count));
        uint64_t* answer = operands && !comparison ? check_wide_allocate(count) : NULL;
        CheckFault fault = CHECK_FAULT_NONE;
        // Whether the faulted operation's GNU folding has an answer (check_wide_fold_result).
        bool gnu_answer = false;
        handled = operands && (comparison || answer != NULL);
        // The operands are extended from their width; the arithmetic sees their `count` limbs masked to it.
        if (handled)
        {
            a[count - 1] &= top_mask;
            b[count - 1] &= unary || shift ? UINT64_MAX : top_mask;
        }
        const bool an = handled && check_wide_negative(a, width, is_signed);
        const bool bn = handled && !unary && !shift && check_wide_negative(b, width, is_signed);
        if (!handled)
        {
            // Out of room: no constant.
        }
        else if (comparison)
        {
            int32_t order = 0;
            if (an != bn)
            {
                order = an ? -1 : 1;
            }
            else
            {
                order = check_wide_compare(fold.kernels, a, b, count);
            }
            const bool truth = op == CHECK_OP_LESS ? order < 0 : op == CHECK_OP_LESS_EQUAL ? order <= 0 : op == CHECK_OP_GREATER ? order > 0 :
                               op == CHECK_OP_GREATER_EQUAL ? order >= 0 : op == CHECK_OP_EQUAL ? order == 0 : order != 0;
            value->constant_kind = CHECK_CONSTANT_INTEGER;
            value->integer = truth;
            value->floating_storage[0] = 0;
            value->floating_storage[1] = 0;
        }
        else
        {
            switch (op)
            {
                case CHECK_OP_POSITIVE:
                {
                    check_wide_bitwise(fold.kernels, CHECK_WIDE_OR, answer, a, a, count);
                } break;
                case CHECK_OP_BIT_NOT:
                {
                    check_wide_bitwise(fold.kernels, CHECK_WIDE_NOT, answer, a, a, count);
                } break;
                case CHECK_OP_BIT_AND:
                {
                    check_wide_bitwise(fold.kernels, CHECK_WIDE_AND, answer, a, b, count);
                } break;
                case CHECK_OP_BIT_OR:
                {
                    check_wide_bitwise(fold.kernels, CHECK_WIDE_OR, answer, a, b, count);
                } break;
                case CHECK_OP_BIT_XOR:
                {
                    check_wide_bitwise(fold.kernels, CHECK_WIDE_XOR, answer, a, b, count);
                } break;
                case CHECK_OP_NEGATIVE:
                case CHECK_OP_ADD:
                case CHECK_OP_SUBTRACT:
                {
                    // A negation is 0 - a: b's zeros less a.
                    const bool negation = op == CHECK_OP_NEGATIVE;
                    if (negation)
                    {
                        for (uint32_t i = 0; i < count; i += 1)
                        {
                            b[i] = 0;
                        }
                    }
                    const uint64_t* x = negation ? b : a;
                    const uint64_t* y = negation ? a : b;
                    check_wide_add(fold.kernels, answer, x, y, count, op != CHECK_OP_ADD, op != CHECK_OP_ADD);
                    answer[count - 1] &= top_mask;
                    const bool xn = negation ? false : an;
                    const bool yn = negation ? an : bn;
                    const bool rn = check_wide_negative(answer, width, true);
                    // Signed overflow: a sum of like signs or a difference of unlike signs whose result's sign differs.
                    const bool overflow = op == CHECK_OP_ADD ? xn == yn && rn != xn : xn != yn && rn != xn;
                    fault = is_signed && overflow ? CHECK_FAULT_SIGNED_OVERFLOW : CHECK_FAULT_NONE;
                    gnu_answer = true;
                } break;
                case CHECK_OP_MULTIPLY:
                case CHECK_OP_DIVIDE:
                case CHECK_OP_REMAINDER:
                {
                    // On the magnitudes, then the sign.
                    if (an)
                    {
                        check_wide_negate_in_place(a, count, width);
                    }
                    if (bn)
                    {
                        check_wide_negate_in_place(b, count, width);
                    }
                    if (op == CHECK_OP_MULTIPLY)
                    {
                        uint64_t* product = check_wide_allocate(2 * count);
                        handled = product != NULL;
                        if (handled)
                        {
                            check_wide_multiply(product, a, b, count);
                            // The magnitude's bits past the width-1 (signed) or width (unsigned) say whether it fits.
                            const uint32_t bits = check_wide_bits(product, 2 * count);
                            const bool negative = an != bn;
                            const bool minimum = negative && bits == width && check_wide_bits(product, 2 * count) == width;
                            bool exact_minimum = minimum;
                            for (uint32_t i = 0; exact_minimum && i < 2 * count; i += 1)
                            {
                                const uint64_t expected = i == (width - 1) / BUSTER_CHECK_WIDE_LIMB_BITS ? UINT64_C(1) << ((width - 1) % BUSTER_CHECK_WIDE_LIMB_BITS) : 0;
                                exact_minimum = product[i] == expected;
                            }
                            fault = is_signed && bits > width - 1 && !exact_minimum ? CHECK_FAULT_SIGNED_OVERFLOW : CHECK_FAULT_NONE;
                            for (uint32_t i = 0; i < count; i += 1)
                            {
                                answer[i] = product[i];
                            }
                            answer[count - 1] &= top_mask;
                            if (negative)
                            {
                                check_wide_negate_in_place(answer, count, width);
                            }
                            gnu_answer = true;
                        }
                    }
                    else if (check_wide_is_zero(b, count))
                    {
                        fault = CHECK_FAULT_DIVIDE_BY_ZERO;
                    }
                    else
                    {
                        uint64_t* other = check_wide_allocate(count);
                        handled = other != NULL;
                        if (handled)
                        {
                            const bool division = op == CHECK_OP_DIVIDE;
                            check_wide_divide(division ? answer : other, division ? other : answer, a, b, count);
                            // The signed minimum divided by -1: its quotient doesn't fit (GNU's is the minimum, its
                            // remainder 0, which the magnitudes give once negated).
                            const bool minimum_by_one = is_signed && an && bn && check_wide_bits(b, count) == 1 && check_wide_bits(a, count) == width;
                            fault = minimum_by_one ? CHECK_FAULT_SIGNED_OVERFLOW : CHECK_FAULT_NONE;
                            const bool negative = division ? an != bn : an;
                            answer[count - 1] &= top_mask;
                            if (negative)
                            {
                                check_wide_negate_in_place(answer, count, width);
                            }
                            gnu_answer = minimum_by_one;
                        }
                    }
                } break;
                case CHECK_OP_SHIFT_LEFT:
                case CHECK_OP_SHIFT_RIGHT:
                {
                    uint32_t magnitude = 0;
                    bool negative = false;
                    handled = check_wide_shift_count(table, right, &magnitude, &negative);
                    if (handled)
                    {
                        const bool invalid = negative || magnitude >= width;
                        // GNU's folding clamps an invalid count to width - 1 and a negative one shifts the other way.
                        const uint32_t amount = magnitude >= width ? width - 1 : magnitude;
                        const bool left_shift = (op == CHECK_OP_SHIFT_LEFT) != negative;
                        check_wide_shift(fold.kernels, left_shift, answer, a, count, amount);
                        if (!left_shift && an && amount != 0)
                        {
                            // The sign's copies into the vacated top `amount` bits of the width.
                            for (uint32_t bit = width - amount; bit < width; bit += 1)
                            {
                                answer[bit / BUSTER_CHECK_WIDE_LIMB_BITS] |= UINT64_C(1) << (bit % BUSTER_CHECK_WIDE_LIMB_BITS);
                            }
                        }
                        answer[count - 1] &= top_mask;
                        // A signed left shift discards a set bit, or shifts a negative value: clang's shift-value fault.
                        const bool discards = op == CHECK_OP_SHIFT_LEFT && !invalid && is_signed && (an || check_wide_bits(a, count) + amount > width);
                        fault = invalid ? CHECK_FAULT_SHIFT_COUNT : discards ? CHECK_FAULT_SHIFT_VALUE : CHECK_FAULT_NONE;
                        gnu_answer = true;
                        if (fault == CHECK_FAULT_SHIFT_COUNT)
                        {
                            *warnings |= (extensions & BUSTER_CHECK_EXTENSION_GNU_CONSTANT) != 0 ?
                                         (negative ? CHECK_WARNING_SHIFT_COUNT_NEGATIVE : CHECK_WARNING_SHIFT_COUNT_OVERFLOW) : 0;
                        }
                        else if (fault == CHECK_FAULT_SHIFT_VALUE)
                        {
                            *warnings |= (extensions & BUSTER_CHECK_EXTENSION_GNU_CONSTANT) != 0 ? (an ? CHECK_WARNING_SHIFT_NEGATIVE_VALUE : CHECK_WARNING_SHIFT_OVERFLOW) : 0;
                        }
                    }
                } break;
                default:
                {
                    fault = CHECK_FAULT_UNSUPPORTED_CONSTANT;
                } break;
            }
            if (handled)
            {
                check_wide_fold_result(&fold, value, answer, fault, gnu_answer, warnings);
            }
        }
    }
    return handled;
}

static uint32_t check_wide_decimal(const TypeTable* table, const CheckValue* value, char* out, uint32_t capacity);

// --- Diagnostics. ---

// Whether an implicit conversion of the integer constant `value` to the integer type `target` changes it, as
// -Wconstant-conversion reads it (c_check_common_warnings.c's check_common_conversion_changes, whose rules these are:
// an unsigned source changes when its bits past the target's width aren't zero; a negative signed one to an unsigned
// target only when the target's signed half can't name it; else when the target's value differs), where one of the two
// types is past 128 bits (*involved). A _Bool target never warns here.
static __attribute__((noinline, cold, unused)) bool check_wide_conversion_changes(const TypeTable* table, const CheckValue* value, TypeId target, bool* involved)
{
    uint32_t source_width = 0;
    uint32_t target_width = 0;
    bool source_signed = false;
    bool target_signed = false;
    const bool source_wide = check_wide_type(table, value->type, &source_width, &source_signed);
    const bool target_wide = check_wide_type(table, target, &target_width, &target_signed);
    const bool source_integer = source_wide || check_action_integer_type(table, value->type, &source_width, &source_signed);
    const bool target_integer = target_wide || check_action_integer_type(table, target, &target_width, &target_signed);
    const bool boolean = type_unqualified(type_strip_aligned(table, target)) == BUSTER_TYPE_BOOL;
    *involved = (source_wide || target_wide) && source_integer && target_integer;
    bool changes = false;
    const uint32_t count = check_wide_count(source_width > target_width ? source_width : target_width) + 1;
    uint64_t* original = *involved && !boolean && value->constant_kind == CHECK_CONSTANT_INTEGER ? check_wide_allocate(count) : NULL;
    if (original != NULL && check_wide_operand(table, value, original, count))
    {
        const bool negative = source_signed && (original[count - 1] >> 63) != 0;
        if (negative)
        {
            check_wide_negate_in_place(original, count, count * BUSTER_CHECK_WIDE_LIMB_BITS);
        }
        // `original` is now the magnitude; a negative one fits a signed target of tw bits up to 2^(tw - 1).
        const uint32_t bits = check_wide_bits(original, count);
        bool exact_power = bits != 0 && original[(bits - 1) / BUSTER_CHECK_WIDE_LIMB_BITS] == UINT64_C(1) << ((bits - 1) % BUSTER_CHECK_WIDE_LIMB_BITS);
        for (uint32_t i = 0; exact_power && i < (bits - 1) / BUSTER_CHECK_WIDE_LIMB_BITS; i += 1)
        {
            exact_power = original[i] == 0;
        }
        const bool fits_negative = bits < target_width || (bits == target_width && exact_power);
        if (!source_signed || !negative)
        {
            changes = bits > (target_signed && source_signed ? target_width - 1 : target_width);
        }
        else
        {
            changes = !fits_negative;
        }
    }
    return changes;
}

// check_common_same_width's twin where either type is past 128 bits: a signed constant converted to a narrower signed
// type that holds its bits but not its sign, 2^(tw - 1) <= value < 2^tw, which clang diagnoses at once.
static __attribute__((noinline, cold, unused)) bool check_wide_same_width(const TypeTable* table, const CheckValue* value, TypeId target)
{
    uint32_t source_width = 0;
    uint32_t target_width = 0;
    bool source_signed = false;
    bool target_signed = false;
    const bool source_wide = check_wide_type(table, value->type, &source_width, &source_signed);
    const bool target_wide = check_wide_type(table, target, &target_width, &target_signed);
    const bool source_integer = source_wide || check_action_integer_type(table, value->type, &source_width, &source_signed);
    const bool target_integer = target_wide || check_action_integer_type(table, target, &target_width, &target_signed);
    const bool shape = (source_wide || target_wide) && source_integer && target_integer && source_signed && target_signed && target_width < source_width;
    const uint32_t count = check_wide_count(source_width);
    uint64_t* limbs = shape && value->constant_kind == CHECK_CONSTANT_INTEGER ? check_wide_allocate(count) : NULL;
    const bool read = limbs != NULL && check_wide_operand(table, value, limbs, count);
    // Nonnegative, and its significant bits exactly the target's width.
    const bool result = read && !check_wide_negative(limbs, source_width, true) && check_wide_bits(limbs, count) == target_width;
    return result;
}

// The decimal digits of an integer constant of up to 128 bits or a wide one, in `scratch`, or null.
static __attribute__((noinline, cold, unused)) const char* check_wide_text(const TypeTable* table, const CheckValue* value, Arena* scratch)
{
    uint32_t width = 0;
    bool is_signed = false;
    char* text = NULL;
    if (check_wide_type(table, value->type, &width, &is_signed))
    {
        // log10(2) < 0.30103: digits, a sign and a terminator.
        const uint64_t capacity = (uint64_t)width * 30103u / 100000u + 3u;
        text = BUSTER_ARENA_PUSH_ARRAY(scratch, char, capacity);
        const uint32_t length = text != NULL ? check_wide_decimal(table, value, text, (uint32_t)capacity - 1) : 0;
        text = length != 0 ? text : NULL;
        if (text != NULL)
        {
            text[length] = 0;
        }
    }
    else if (check_action_integer_type(table, value->type, &width, &is_signed))
    {
        const unsigned __int128 mask = check_action_mask(width);
        const unsigned __int128 bits = value->integer & mask;
        const bool negative = is_signed && ((bits >> (width - 1)) & 1u) != 0;
        unsigned __int128 magnitude = negative ? (0 - bits) & mask : bits;
        text = BUSTER_ARENA_PUSH_ARRAY(scratch, char, 48);
        char digits[48];
        uint32_t count = 0;
        do
        {
            digits[count] = (char)('0' + (uint32_t)(magnitude % 10));
            count += 1;
            magnitude /= 10;
        }
        while (magnitude != 0);
        uint32_t at = 0;
        if (text != NULL && negative)
        {
            text[at] = '-';
            at += 1;
        }
        while (text != NULL && count != 0)
        {
            count -= 1;
            text[at] = digits[count];
            at += 1;
        }
        if (text != NULL)
        {
            text[at] = 0;
        }
    }
    return text;
}

// --- The alignment builtins (#1447). ---

// An integer constant's shape as an alignment: negative, zero, its magnitude's significant bits and whether it is a power
// of 2, for a value of up to 128 bits or a wide one. False when it holds no limbs.
static __attribute__((noinline, cold, unused)) bool check_wide_alignment_shape(const TypeTable* table, const CheckValue* value, bool* negative, bool* zero,
                                                                                uint32_t* bits, bool* power)
{
    uint32_t width = 0;
    bool is_signed = false;
    const bool wide = check_wide_type(table, value->type, &width, &is_signed);
    const uint32_t count = check_wide_count(width);
    uint64_t* limbs = wide && value->constant_kind == CHECK_CONSTANT_INTEGER ? check_wide_allocate(count) : NULL;
    const bool valid = limbs != NULL && check_wide_operand(table, value, limbs, count);
    if (valid)
    {
        limbs[count - 1] &= check_wide_top_mask(width);
        *negative = check_wide_negative(limbs, width, is_signed);
        *zero = check_wide_is_zero(limbs, count);
        *bits = check_wide_bits(limbs, count);
        uint32_t ones = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            ones += (uint32_t)__builtin_popcountll(limbs[i]);
        }
        *power = ones == 1;
    }
    return valid;
}

// An alignment builtin's fold of a wide constant `source` (CheckValue of its _BitInt type) by 2^`exponent`: align_down's
// x & ~(a - 1), align_up's (x + (a - 1)) & ~(a - 1), wrapping in the source's width, as the narrow fold's, or is_aligned's
// (x & (a - 1)) == 0 into `value`'s integer. `kind` is 0, 1 or 2 in that order. False when it holds no limbs.
static __attribute__((noinline, cold, unused)) bool check_wide_align_fold(const TypeTable* table, const CheckValue* source, uint32_t exponent, uint32_t kind,
                                                                          CheckValue* value)
{
    uint32_t width = 0;
    bool is_signed = false;
    const bool wide = check_wide_type(table, source->type, &width, &is_signed);
    const uint32_t count = check_wide_count(width);
    uint64_t* x = wide ? check_wide_allocate(count) : NULL;
    uint64_t* mask = x != NULL ? check_wide_allocate(count) : NULL;
    uint64_t* answer = mask != NULL ? check_wide_allocate(count) : NULL;
    const bool valid = answer != NULL && exponent < width && check_wide_operand(table, source, x, count);
    if (valid)
    {
        const CheckWideKernels kernels = { .scalar = false };
        x[count - 1] &= check_wide_top_mask(width);
        // a - 1: the low `exponent` bits.
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t low = i * BUSTER_CHECK_WIDE_LIMB_BITS;
            mask[i] = exponent >= low + 64 ? UINT64_MAX : exponent > low ? (UINT64_C(1) << (exponent - low)) - 1 : 0;
        }
        if (kind == 2)
        {
            check_wide_bitwise(kernels, CHECK_WIDE_AND, answer, x, mask, count);
            value->constant_kind = CHECK_CONSTANT_INTEGER;
            value->integer = check_wide_is_zero(answer, count);
            value->floating_storage[0] = 0;
        }
        else
        {
            if (kind == 1)
            {
                check_wide_add(kernels, x, x, mask, count, false, 0);
            }
            for (uint32_t i = 0; i < count; i += 1)
            {
                answer[i] = x[i] & ~mask[i];
            }
            answer[count - 1] &= check_wide_top_mask(width);
            check_wide_publish(value, answer);
        }
    }
    return valid;
}

// --- Printing and bytes. ---

// A wide constant's decimal digits, signed as its type says, into `out` (room for `capacity` bytes, the digits without
// a terminator); returns their length, 0 when the value holds no limbs or `out` is too small. Repeated division by
// 10^19, scalar, as clang's APInt::toString.
static __attribute__((noinline, cold, unused)) uint32_t check_wide_decimal(const TypeTable* table, const CheckValue* value, char* out, uint32_t capacity)
{
    uint32_t width = 0;
    bool is_signed = false;
    uint32_t length = 0;
    const bool wide = check_wide_type(table, value->type, &width, &is_signed);
    const uint32_t count = check_wide_count(width);
    const uint64_t* limbs = wide ? check_wide_limbs(value, count) : NULL;
    uint64_t* magnitude = limbs != NULL ? check_wide_allocate(count) : NULL;
    if (magnitude != NULL)
    {
        const bool negative = check_wide_negative(limbs, width, is_signed);
        for (uint32_t i = 0; i < count; i += 1)
        {
            magnitude[i] = limbs[i];
        }
        if (negative)
        {
            check_wide_negate_in_place(magnitude, count, width);
        }
        // The digits, least significant first, then reversed.
        uint32_t written = 0;
        bool fits = true;
        bool more = true;
        while (more && fits)
        {
            unsigned __int128 remainder = 0;
            const uint64_t divisor = UINT64_C(10000000000000000000);
            for (uint32_t i = count; i != 0; i -= 1)
            {
                const unsigned __int128 current = remainder << 64 | magnitude[i - 1];
                magnitude[i - 1] = (uint64_t)(current / divisor);
                remainder = current % divisor;
            }
            more = !check_wide_is_zero(magnitude, count);
            uint64_t chunk = (uint64_t)remainder;
            for (uint32_t digit = 0; digit < 19 && fits && (more || chunk != 0 || digit == 0); digit += 1)
            {
                fits = written < capacity;
                if (fits)
                {
                    out[written] = (char)('0' + chunk % 10);
                    written += 1;
                }
                chunk /= 10;
            }
        }
        fits &= !negative || written < capacity;
        if (fits && negative)
        {
            out[written] = '-';
            written += 1;
        }
        for (uint32_t i = 0; fits && i < written / 2; i += 1)
        {
            const char swap = out[i];
            out[i] = out[written - 1 - i];
            out[written - 1 - i] = swap;
        }
        length = fits ? written : 0;
    }
    return length;
}

// A wide constant's object representation: `size` bytes (its storage, a whole number of 8-byte limbs), its limbs with
// the bits past its width extended from its sign as the x86-64 _BitInt ABI keeps them. False when it holds no limbs.
static __attribute__((unused)) bool check_wide_bytes(const TypeTable* table, const CheckValue* value, uint8_t* bytes, uint64_t size)
{
    uint32_t width = 0;
    bool is_signed = false;
    const bool wide = check_wide_type(table, value->type, &width, &is_signed);
    const uint32_t count = check_wide_count(width);
    const uint64_t* limbs = wide ? check_wide_limbs(value, count) : NULL;
    const bool valid = limbs != NULL && size >= (uint64_t)count * sizeof(uint64_t);
    if (valid)
    {
        const bool negative = check_wide_negative(limbs, width, is_signed);
        for (uint64_t i = 0; i < size / sizeof(uint64_t); i += 1)
        {
            uint64_t limb = i < count ? limbs[i] : negative ? UINT64_MAX : 0;
            limb |= i + 1 == count && negative ? ~check_wide_top_mask(width) : 0;
            memcpy(bytes + i * sizeof(uint64_t), &limb, sizeof(limb));
        }
    }
    return valid;
}

#endif
