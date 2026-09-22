/* Isolated switch interval research. No Buster production code is modified.
 * Entry points: pipeline, dispatch_kernel; portable scalar code first.
 * Input contract: normalized inclusive intervals must satisfy low <= high.
 * Return the earliest source-order current index overlapping any earlier one.
 * A parser integration must replay its own diagnostic loop on proof failure.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#if defined(__linux__)
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
#endif
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#define HAVE_X86 1
#include <immintrin.h>
#include <cpuid.h>
#define NOINLINE __attribute__((noinline))
#define TARGET(x) __attribute__((target(x),noinline))
#else
#define HAVE_X86 0
#define NOINLINE
#define TARGET(x)
#endif

typedef struct Interval
{
    uint64_t low;
    uint64_t high;
} Interval;

typedef struct Outcome
{
    size_t index;
    unsigned status; /* 0 disjoint; 1 overlap; 2 invalid interval; 3 allocation. */
} Outcome;

static unsigned have_avx2;
static unsigned have_evex256;
static unsigned have_avx512;
static uint64_t xcr0_value;
static const char *variant_names[] = {
    "scalar", "hull", "certificate", "avx2", "evex256", "avx512",
    "avx512_u2", "avx512_u4", "avx512_u8"
};

static void features_init(void)
{
#if HAVE_X86
    unsigned a, b, c, d;
    unsigned avx_state_bits = bit_XSAVE | bit_OSXSAVE | bit_AVX;
    if (__get_cpuid(1, &a, &b, &c, &d) && (c & avx_state_bits) == avx_state_bits)
    {
        unsigned lo, hi;
        __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
        xcr0_value = ((uint64_t)hi << 32) | lo;
    }
    if (__get_cpuid_count(7, 0, &a, &b, &c, &d))
    {
        have_avx2 = (b & bit_AVX2) && ((xcr0_value & 6) == 6);
        have_avx512 = (b & bit_AVX512F) && ((xcr0_value & 0xe6) == 0xe6);
        have_evex256 = have_avx512 && (b & bit_AVX512VL);
    }
#endif
}

static bool available(unsigned variant)
{
    bool result = variant < 3;
    if (variant == 3) result = have_avx2 != 0;
    if (variant == 4) result = have_evex256 != 0;
    if (variant >= 5 && variant < 9) result = have_avx512 != 0;
    return result;
}

NOINLINE size_t overlap_scalar(const uint64_t *low, const uint64_t *high, size_t n)
{
    size_t result = n;
    for (size_t j = 1; j < n && result == n; j += 1)
    {
        for (size_t i = 0; i < j; i += 1)
        {
            if (low[j] <= high[i] && low[i] <= high[j])
            {
                result = j;
                break;
            }
        }
    }
    return result;
}

/* A hull is a sufficient disjointness certificate, never a collision test.
 * An interval in an interior hole must fall back, even if actually disjoint. */
NOINLINE size_t overlap_hull(const uint64_t *low, const uint64_t *high, size_t n)
{
    size_t result = n;
    if (n > 1)
    {
        uint64_t min_low = low[0];
        uint64_t max_high = high[0];
        for (size_t j = 1; j < n && result == n; j += 1)
        {
            if (!(high[j] < min_low || low[j] > max_high))
            {
                for (size_t i = 0; i < j; i += 1)
                {
                    if (low[j] <= high[i] && low[i] <= high[j])
                    {
                        result = j;
                        break;
                    }
                }
            }
            if (low[j] < min_low) min_low = low[j];
            if (high[j] > max_high) max_high = high[j];
        }
    }
    return result;
}

static bool hull_certificate(const uint64_t *low, const uint64_t *high, size_t n)
{
    bool valid = true;
    if (n > 1)
    {
        uint64_t min_low = low[0];
        uint64_t max_high = high[0];
        for (size_t j = 1; j < n; j += 1)
        {
            if (high[j] < min_low)
            {
                min_low = low[j];
            }
            else if (low[j] > max_high)
            {
                max_high = high[j];
            }
            else
            {
                valid = false;
                break;
            }
        }
    }
    return valid;
}

/* Stable bottom-up mergesort. No comparator callback and no recursion.
 * Sort scratch is private; original order remains available for diagnostics. */
static Interval *sort_intervals(Interval *items, Interval *scratch, size_t n)
{
    Interval *src = items;
    Interval *dst = scratch;
    for (size_t width = 1; width < n;)
    {
        for (size_t start = 0; start < n;)
        {
            size_t mid = start + (n - start < width ? n - start : width);
            size_t end = mid + (n - mid < width ? n - mid : width);
            size_t a = start;
            size_t b = mid;
            for (size_t k = start; k < end; k += 1)
            {
                if (a < mid && (b == end || src[a].low <= src[b].low)) dst[k] = src[a++];
                else dst[k] = src[b++];
            }
            start = end;
        }
        Interval *swap = src;
        src = dst;
        dst = swap;
        if (width > n / 2) width = n;
        else width *= 2;
    }
    return src;
}

NOINLINE size_t overlap_certificate(const uint64_t *low, const uint64_t *high, size_t n)
{
    size_t result = n;
    if (!hull_certificate(low, high, n))
    {
        /* No measured tuning threshold: always charge sorting when unknown. */
        Interval *storage = n <= SIZE_MAX / (2 * sizeof(Interval)) ? malloc(2 * n * sizeof(Interval)) : NULL;
        if (storage)
        {
            for (size_t i = 0; i < n; i += 1) storage[i] = (Interval){low[i], high[i]};
            Interval *sorted = sort_intervals(storage, storage + n, n);
            bool disjoint = true;
            for (size_t i = 1; i < n; i += 1)
            {
                /* Adjacent comparison suffices for existential overlap. */
                if (sorted[i - 1].high >= sorted[i].low)
                {
                    disjoint = false;
                    break;
                }
            }
            if (!disjoint) result = overlap_scalar(low, high, n);
            free(storage);
        }
        else
        {
            /* An optional proof allocation may fail without changing results. */
            result = overlap_scalar(low, high, n);
        }
    }
    return result;
}

#if HAVE_X86
TARGET("avx2") size_t overlap_avx2(const uint64_t *low, const uint64_t *high, size_t n)
{
    size_t result = n;
    const __m256i sign = _mm256_set1_epi64x(INT64_MIN);
    for (size_t j = 1; j < n && result == n; j += 1)
    {
        __m256i l = _mm256_xor_si256(_mm256_set1_epi64x((long long)low[j]), sign);
        __m256i h = _mm256_xor_si256(_mm256_set1_epi64x((long long)high[j]), sign);
        size_t i = 0;
        for (; j - i >= 4; i += 4)
        {
            __m256i ph = _mm256_xor_si256(_mm256_loadu_si256((const __m256i *)(high + i)), sign);
            __m256i pl = _mm256_xor_si256(_mm256_loadu_si256((const __m256i *)(low + i)), sign);
            __m256i no = _mm256_or_si256(_mm256_cmpgt_epi64(l, ph), _mm256_cmpgt_epi64(pl, h));
            if ((unsigned)_mm256_movemask_pd(_mm256_castsi256_pd(no)) != 15)
            {
                result = j;
                break;
            }
        }
        for (; i < j && result == n; i += 1)
        {
            if (low[j] <= high[i] && low[i] <= high[j]) result = j;
        }
    }
    return result;
}

TARGET("avx512f,avx512vl") size_t overlap_evex256(const uint64_t *low, const uint64_t *high, size_t n)
{
    size_t result = n;
    for (size_t j = 1; j < n && result == n; j += 1)
    {
        __m256i l = _mm256_set1_epi64x((long long)low[j]);
        __m256i h = _mm256_set1_epi64x((long long)high[j]);
        size_t i = 0;
        for (; j - i >= 4; i += 4)
        {
            __mmask8 a = _mm256_cmp_epu64_mask(l, _mm256_loadu_si256((const __m256i *)(high + i)), _MM_CMPINT_LE);
            __mmask8 b = _mm256_cmp_epu64_mask(_mm256_loadu_si256((const __m256i *)(low + i)), h, _MM_CMPINT_LE);
            if ((a & b) != 0)
            {
                result = j;
                break;
            }
        }
        for (; i < j && result == n; i += 1)
        {
            if (low[j] <= high[i] && low[i] <= high[j]) result = j;
        }
    }
    return result;
}

/* U-chunk source templates are written out by generate_unroll.py. */
#include "unroll.inc"
#endif

static size_t dispatch_kernel(unsigned variant, const uint64_t *low, const uint64_t *high, size_t n)
{
    size_t result;
    switch (variant)
    {
        case 1: result = overlap_hull(low, high, n); break;
        case 2: result = overlap_certificate(low, high, n); break;
#if HAVE_X86
        case 3: result = overlap_avx2(low, high, n); break;
        case 4: result = overlap_evex256(low, high, n); break;
        case 5: result = overlap_avx512_1(low, high, n); break;
        case 6: result = overlap_avx512_2(low, high, n); break;
        case 7: result = overlap_avx512_4(low, high, n); break;
        case 8: result = overlap_avx512_8(low, high, n); break;
#endif
        default: result = overlap_scalar(low, high, n); break;
    }
    return result;
}

NOINLINE Outcome pipeline(unsigned variant, const uint64_t *raw_low, const uint64_t *raw_high,
                           size_t n, uint64_t value_mask, uint64_t sign_bit)
{
    Outcome out = {n, 0};
    uint64_t *storage = n && n <= SIZE_MAX / 16 ? malloc(n * 16) : NULL;
    if (n && !storage) out = (Outcome){n, 3};
    else if (n)
    {
        uint64_t *low = storage;
        uint64_t *high = storage + n;
        for (size_t j = 0; j < n; j += 1)
        {
            low[j] = (raw_low[j] & value_mask) ^ sign_bit;
            high[j] = (raw_high[j] & value_mask) ^ sign_bit;
            if (low[j] > high[j] && out.status == 0) out = (Outcome){j, 2};
        }
        if (out.status == 0)
        {
            size_t index = dispatch_kernel(variant, low, high, n);
            out = (Outcome){index, index < n ? 1u : 0u};
        }
    }
    free(storage);
    return out;
}

static uint64_t random64(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * UINT64_C(2685821657736338717);
}

static uint64_t hash_add(uint64_t hash, uint64_t value)
{
    for (unsigned i = 0; i < 8; i += 1)
    {
        hash ^= value & 255;
        hash *= UINT64_C(1099511628211);
        value >>= 8;
    }
    return hash;
}

/* Generated distributions are diagnostic, not a measured compiler corpus. */
static void make_data(uint64_t *low, uint64_t *high, size_t n, const char *pattern, uint64_t *seed)
{
    uint64_t offset = random64(seed) & UINT64_C(0xffffff);
    for (size_t i = 0; i < n; i += 1)
    {
        size_t index = i;
        if (!strcmp(pattern, "reverse")) index = n - 1 - i;
        if (!strcmp(pattern, "outward")) index = i & 1 ? n / 2 + (i + 1) / 2 : n / 2 - i / 2;
        low[i] = offset + (uint64_t)index * 4;
        high[i] = low[i] + (!strcmp(pattern, "ranges") ? 2 : 0);
    }
    if (!strcmp(pattern, "permuted") || !strcmp(pattern, "overlap_late") || !strcmp(pattern, "ranges"))
    {
        for (size_t i = n; i > 1; i -= 1)
        {
            size_t j = (size_t)(random64(seed) % i);
            uint64_t t = low[i - 1]; low[i - 1] = low[j]; low[j] = t;
            t = high[i - 1]; high[i - 1] = high[j]; high[j] = t;
        }
    }
    if (!strcmp(pattern, "overlap_late") && n > 1) low[n - 1] = high[n - 1] = low[0];
    if (!strcmp(pattern, "overlap_early") && n > 1) low[1] = high[1] = low[0];
    if (!strcmp(pattern, "random"))
    {
        for (size_t i = 0; i < n; i += 1)
        {
            uint64_t a = random64(seed), b = random64(seed);
            low[i] = a < b ? a : b;
            high[i] = a < b ? b : a;
        }
    }
}

static uint64_t case_count;
static unsigned failures;

static void check_case(const uint64_t *low, const uint64_t *high, size_t n,
                       uint64_t value_mask, uint64_t sign_bit)
{
    Outcome reference = pipeline(0, low, high, n, value_mask, sign_bit);
    for (unsigned v = 1; v < 9; v += 1)
    {
        if (available(v))
        {
            Outcome out = pipeline(v, low, high, n, value_mask, sign_bit);
            if (out.status != reference.status || out.index != reference.index)
            {
                fprintf(stderr, "mismatch case=%" PRIu64 " variant=%s n=%zu expected=%u,%zu actual=%u,%zu\n",
                        case_count, variant_names[v], n, reference.status, reference.index, out.status, out.index);
                failures += 1;
            }
        }
    }
    case_count += 1;
}

static void guard_page_tests(void)
{
#if defined(__linux__)
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    void *m1 = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    void *m2 = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m1 != MAP_FAILED && m2 != MAP_FAILED &&
        !mprotect((char *)m1 + page, page, PROT_NONE) && !mprotect((char *)m2 + page, page, PROT_NONE))
    {
        for (size_t n = 0; n <= 97; n += 1)
        {
            uint64_t *low = (uint64_t *)((char *)m1 + page) - n;
            uint64_t *high = (uint64_t *)((char *)m2 + page) - n;
            for (size_t i = 0; i < n; i += 1) low[i] = high[i] = i * 4;
            for (unsigned v = 0; v < 9; v += 1)
            {
                if (available(v) && dispatch_kernel(v, low, high, n) != n) failures += 1;
            }
            case_count += 1;
        }
    }
    else
    {
        fprintf(stderr, "guard-page setup failed\n");
        failures += 1;
    }
    if (m1 != MAP_FAILED) munmap(m1, page * 2);
    if (m2 != MAP_FAILED) munmap(m2, page * 2);
#endif
}

static int self_test(void)
{
    uint64_t low[260], high[260];
    uint64_t seed = UINT64_C(0xcafe1234ba987654);
    const char *patterns[] = {"ordered", "reverse", "outward", "permuted", "ranges", "random", "overlap_late", "overlap_early"};
    for (size_t n = 0; n <= 129; n += 1)
    {
        for (unsigned p = 0; p < sizeof(patterns) / sizeof(patterns[0]); p += 1)
        {
            make_data(low, high, n, patterns[p], &seed);
            check_case(low, high, n, UINT64_MAX, 0);
            for (size_t i = 0; i < n; i += 1) { low[i] ^= UINT64_C(1) << 63; high[i] ^= UINT64_C(1) << 63; }
            check_case(low, high, n, UINT64_MAX, UINT64_C(1) << 63);
        }
    }
    /* Every possible pair position, including collisions on equal endpoints. */
    for (size_t n = 2; n <= 33; n += 1)
    {
        for (size_t j = 1; j < n; j += 1)
        {
            for (size_t i = 0; i < j; i += 1)
            {
                make_data(low, high, n, "ordered", &seed);
                low[j] = high[j] = high[i];
                check_case(low, high, n, UINT64_MAX, 0);
                Outcome out = pipeline(0, low, high, n, UINT64_MAX, 0);
                if (out.status != 1 || out.index != j) failures += 1;
            }
        }
    }
    /* All 256 lane predicates, with disjoint prior singletons in source order. */
    for (unsigned mask = 0; mask < 256; mask += 1)
    {
        for (size_t i = 0; i < 8; i += 1) low[i] = high[i] = (mask & (1u << i)) ? i : 1000 + i;
        low[8] = 0; high[8] = 7;
        check_case(low, high, 9, UINT64_MAX, 0);
        Outcome predicate = pipeline(0, low, high, 9, UINT64_MAX, 0);
        if (predicate.status != (mask ? 1u : 0u) || predicate.index != (mask ? 8u : 9u)) failures += 1;
    }
    /* Wider batching edges, including each collision position in a 64-lane tile. */
    const size_t large_sizes[] = {130, 156, 167, 256, 259};
    for (size_t s = 0; s < sizeof(large_sizes) / sizeof(large_sizes[0]); s += 1)
    {
        size_t n = large_sizes[s];
        for (unsigned p = 0; p < sizeof(patterns) / sizeof(patterns[0]); p += 1)
        {
            make_data(low, high, n, patterns[p], &seed);
            check_case(low, high, n, UINT64_MAX, 0);
        }
        for (size_t i = 0; i < n - 1; i += 1)
        {
            make_data(low, high, n, "ordered", &seed);
            low[n - 1] = high[n - 1] = low[i];
            check_case(low, high, n, UINT64_MAX, 0);
        }
    }
    const unsigned widths[] = {8, 16, 32, 64};
    for (unsigned w = 0; w < 4; w += 1)
    {
        uint64_t mask = widths[w] == 64 ? UINT64_MAX : (UINT64_C(1) << widths[w]) - 1;
        for (unsigned signedness = 0; signedness < 2; signedness += 1)
        {
            uint64_t sign = signedness ? UINT64_C(1) << (widths[w] - 1) : 0;
            for (unsigned trial = 0; trial < 1000; trial += 1)
            {
                size_t n = trial % 98;
                for (size_t i = 0; i < n; i += 1)
                {
                    uint64_t a = random64(&seed) & mask, b = random64(&seed) & mask;
                    low[i] = (a < b ? a : b) ^ sign;
                    high[i] = (a < b ? b : a) ^ sign;
                    /* High raw bits exercise truncation before sign mapping. */
                    low[i] |= random64(&seed) & ~mask;
                    high[i] |= random64(&seed) & ~mask;
                }
                check_case(low, high, n, mask, sign);
            }
            uint64_t keys[] = {0, 1, mask / 2, mask / 2 + 1, mask - 1, mask};
            for (size_t i = 0; i < 6; i += 1) low[i] = high[i] = keys[i] ^ sign;
            check_case(low, high, 6, mask, sign);
            Outcome extremes = pipeline(0, low, high, 6, mask, sign);
            if (extremes.status != 0 || extremes.index != 6) failures += 1;
            low[3] = (mask - 1) ^ sign; high[3] = 1 ^ sign;
            check_case(low, high, 6, mask, sign);
            Outcome invalid = pipeline(0, low, high, 6, mask, sign);
            if (invalid.status != 2 || invalid.index != 3) failures += 1;
        }
    }
    /* Equal inputs may alias: no in-place normalization of caller storage. */
    for (size_t i = 0; i < 128; i += 1) low[i] = i;
    check_case(low, low, 128, UINT64_MAX, 0);
    /* Interior holes must never be misclassified as collisions by the hull. */
    low[0] = high[0] = 0; low[1] = high[1] = 100; low[2] = high[2] = 50;
    check_case(low, high, 3, UINT64_MAX, 0);
    /* A long interval overlaps a nonadjacent sorted interval and its neighbor. */
    low[0] = 0; high[0] = UINT64_MAX; low[1] = high[1] = 1; low[2] = high[2] = UINT64_MAX;
    check_case(low, high, 3, UINT64_MAX, 0);
    guard_page_tests();
    printf("{\"mode\":\"correctness\",\"cases\":%" PRIu64 ",\"failures\":%u,\"avx2\":%u,\"evex256\":%u,\"avx512\":%u,\"xcr0\":\"0x%" PRIx64 "\"}\n",
           case_count, failures, have_avx2, have_evex256, have_avx512, xcr0_value);
    return failures ? 1 : 0;
}

static uint64_t now_ns(void)
{
    struct timespec t;
#ifdef CLOCK_MONOTONIC_RAW
    clock_gettime(CLOCK_MONOTONIC_RAW, &t);
#else
    clock_gettime(CLOCK_MONOTONIC, &t);
#endif
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static int benchmark(unsigned variant, const char *pattern, size_t n, size_t calls, uint64_t seed, size_t pool)
{
    int error = 0;
    uint64_t *raw = n && pool && n <= SIZE_MAX / pool / 16 ? malloc(n * pool * 16) : NULL;
    if ((n && !raw) || !pool || !calls || !seed || !available(variant)) error = 2;
    else
    {
        uint64_t input_hash = UINT64_C(14695981039346656037);
        uint64_t result_hash = input_hash;
        for (size_t p = 0; p < pool; p += 1)
        {
            uint64_t *low = n ? raw + p * n * 2 : NULL;
            uint64_t *high = n ? low + n : NULL;
            make_data(low, high, n, pattern, &seed);
            for (size_t i = 0; i < n; i += 1)
            {
                input_hash = hash_add(input_hash, low[i]); input_hash = hash_add(input_hash, high[i]);
            }
        }
        /* One untimed traversal warms code/data; timed order varies over a pool. */
        for (size_t p = 0; p < pool; p += 1)
        {
            const uint64_t *low = n ? raw + p * n * 2 : NULL;
            Outcome out = pipeline(variant, low, n ? low + n : NULL, n, UINT64_MAX, 0);
            result_hash = hash_add(result_hash, out.index + out.status);
        }
        uint64_t start = now_ns();
        for (size_t c = 0; c < calls; c += 1)
        {
            size_t p = (size_t)(random64(&seed) % pool);
            const uint64_t *low = n ? raw + p * n * 2 : NULL;
            Outcome out = pipeline(variant, low, n ? low + n : NULL, n, UINT64_MAX, 0);
            result_hash = (result_hash << 7 | result_hash >> 57) ^ out.index ^ ((uint64_t)out.status << 32);
            if (out.status > 1) error = 2;
        }
        uint64_t elapsed = now_ns() - start;
        long peak_rss_kib = -1;
#if defined(__linux__)
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) peak_rss_kib = usage.ru_maxrss;
#endif
        printf("{\"mode\":\"pipeline-wall-clock\",\"variant\":\"%s\",\"pattern\":\"%s\",\"n\":%zu,\"calls\":%zu,\"pool\":%zu,\"elapsed_ns\":%" PRIu64 ",\"input_hash\":\"%016" PRIx64 "\",\"output_hash\":\"%016" PRIx64 "\",\"process_peak_rss_kib\":%ld}\n",
               variant_names[variant], pattern, n, calls, pool, elapsed, input_hash, result_hash, peak_rss_kib);
    }
    free(raw);
    return error;
}

int main(int argc, char **argv)
{
    features_init();
    int result = 2;
    if (argc == 2 && !strcmp(argv[1], "--test")) result = self_test();
    else if (argc == 8 && !strcmp(argv[1], "--bench"))
    {
        unsigned variant = 9;
        for (unsigned i = 0; i < 9; i += 1) if (!strcmp(argv[2], variant_names[i])) variant = i;
        const char *patterns[] = {"ordered", "reverse", "outward", "permuted", "ranges", "random", "overlap_late", "overlap_early"};
        bool valid_pattern = false;
        for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i += 1) if (!strcmp(argv[3], patterns[i])) valid_pattern = true;
        if (variant < 9 && valid_pattern)
        {
            result = benchmark(variant, argv[3], (size_t)strtoull(argv[4], NULL, 0), (size_t)strtoull(argv[5], NULL, 0),
                               strtoull(argv[6], NULL, 0), (size_t)strtoull(argv[7], NULL, 0));
        }
    }
    else fprintf(stderr, "Usage: %s --test | --bench VARIANT PATTERN N CALLS SEED POOL\n", argv[0]);
    return result;
}
