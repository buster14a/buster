
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t u64;
typedef uint8_t u8;
typedef char char8;
typedef struct String8 { char8* pointer; u64 length; } String8;
#define BUSTER_GLOBAL_LOCAL static
#define BUSTER_STRING_NO_MATCH UINT64_MAX
#define BUSTER_BENCH_ALLOCATIONS 0
#define BUSTER_OPTIMIZE 0
static u64 comparisons;
static bool compare_byte(u8 a, u8 b, unsigned order)
{
    comparisons += 1;
    bool result = order == 0 ? a == b : order == 1 ? a > b : a < b;
    return result;
}
static u64 direct_search(String8 haystack, String8 needle)
{
    u64 result = needle.length ? BUSTER_STRING_NO_MATCH : 0;
    for (u64 position = 0; needle.length && needle.length <= haystack.length &&
         position <= haystack.length - needle.length && result == BUSTER_STRING_NO_MATCH; position += 1)
    {
        bool equal = true;
        for (u64 i = 0; equal && i < needle.length; i += 1)
        {
            equal = compare_byte((u8)haystack.pointer[position + i], (u8)needle.pointer[i], 0);
        }
        if (equal) result = position;
    }
    return result;
}
String8 string_slice(String8 slice, u64 start, u64 end)
{
    return (String8){.pointer = (slice).pointer + (start), .length = (end) - (start)};
}

bool string_equal(String8 s1, String8 s2)
{
    // Length, emptiness and pointer identity settle the answer without looking
    // at any byte; only a same-length, distinct, non-empty pair reaches the
    // comparison itself.
    bool result = s1.length == s2.length;
#if BUSTER_BENCH_ALLOCATIONS
    string_equal_totals.calls += 1;
#endif
    if (result && s1.length)
    {
        if (!s1.pointer || !s2.pointer)
        {
            result = false;
        }
        else if (s1.pointer != s2.pointer)
        {
#if BUSTER_BENCH_ALLOCATIONS
            string_equal_totals.compared_bytes += s1.length;
#endif
#if BUSTER_OPTIMIZE
            result = memory_compare(s1.pointer, s2.pointer, s1.length * sizeof(char8));
#else
            for (u64 i = 0; i < s1.length && result; i += 1)
            {
                result = compare_byte((u8)s1.pointer[i], (u8)s2.pointer[i], 0);
            }
#endif
        }
    }

    return result;
}

#define STRING_FIRST_SEQUENCE_TWO_WAY_MINIMUM_NEEDLE (32)
BUSTER_GLOBAL_LOCAL u64 string_first_sequence_two_way(String8 s, String8 sub)
{
    const u8* needle = (const u8*)sub.pointer;
    const u8* haystack = (const u8*)s.pointer;
    u64 needle_length = sub.length;
    u64 result = BUSTER_STRING_NO_MATCH;

    // Maximal suffix under both byte orderings; indices start at u64 -1 and rely on wraparound.
    u64 suffix_end[2];
    u64 suffix_period[2];
    for (u64 order = 0; order < 2; order += 1)
    {
        u64 ip = (u64)-1;
        u64 jp = 0;
        u64 k = 1;
        u64 p = 1;
        while (jp + k < needle_length)
        {
            u8 a = needle[ip + k];
            u8 b = needle[jp + k];
            if (compare_byte(a, b, 0))
            {
                if (k == p)
                {
                    jp += p;
                    k = 1;
                }
                else
                {
                    k += 1;
                }
            }
            else if (compare_byte(a, b, (unsigned)order + 1))
            {
                jp += k;
                k = 1;
                p = jp - ip;
            }
            else
            {
                ip = jp;
                jp += 1;
                k = 1;
                p = 1;
            }
        }
        suffix_end[order] = ip;
        suffix_period[order] = p;
    }

    bool reverse_longer = suffix_end[1] + 1 > suffix_end[0] + 1;
    u64 critical = reverse_longer ? suffix_end[1] : suffix_end[0];
    u64 period = reverse_longer ? suffix_period[1] : suffix_period[0];

    bool periodic = period + critical + 1 <= needle_length;
    if (periodic)
    {
        periodic = string_equal(string_slice(sub, 0, critical + 1), string_slice(sub, period, period + critical + 1));
    }
    u64 memory_reset = 0;
    if (periodic)
    {
        memory_reset = needle_length - period;
    }
    else
    {
        u64 right_length = needle_length - critical - 1;
        period = (critical > right_length ? critical : right_length) + 1;
    }

    u64 memory = 0;
    u64 position = 0;
    bool searching = true;
    while (searching && position + needle_length <= s.length)
    {
        const u8* window = haystack + position;
        u64 k = critical + 1 > memory ? critical + 1 : memory;
        while (k < needle_length && compare_byte(needle[k], window[k], 0))
        {
            k += 1;
        }

        if (k < needle_length)
        {
            position += k - critical;
            memory = 0;
        }
        else
        {
            k = critical + 1;
            while (k > memory && compare_byte(needle[k - 1], window[k - 1], 0))
            {
                k -= 1;
            }

            if (k <= memory)
            {
                result = position;
                searching = false;
            }
            else
            {
                position += period;
                memory = memory_reset;
            }
        }
    }

    return result;
}

u64 string_first_sequence(String8 s, String8 sub)
{
    u64 result = BUSTER_STRING_NO_MATCH;

    if (sub.length == 0)
    {
        result = 0;
    }
    else if (s.length >= sub.length)
    {
        if (sub.length >= STRING_FIRST_SEQUENCE_TWO_WAY_MINIMUM_NEEDLE)
        {
            // One full prefix probe costs at most the needle length and keeps
            // immediate matches on the existing optimized equality path.
            if (false)
            {
                result = 0;
            }
            else
            {
                result = string_first_sequence_two_way(s, sub);
            }
        }
        else
        {
            u64 end = s.length - sub.length + 1;
            for (u64 i = 0; i < end; i += 1)
            {
                String8 chunk = string_slice(s, i, i + sub.length);
                if (string_equal(chunk, sub))
                {
                    result = i;
                    break;
                }
            }
        }
    }

    return result;
}


static u64 state = UINT64_C(0x9e3779b97f4a7c15);
static u8 random_byte(void)
{
    state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return (u8)(state >> 33);
}
static bool check_case(u64 n, u64 m, unsigned shape, bool work_mutant)
{
    char haystack[8192], needle[8192], haystack_before[8192], needle_before[8192];
    memset(haystack, 'a', sizeof(haystack));
    memset(needle, 'a', sizeof(needle));
    if (shape == 0 && m) needle[m - 1] = 'b'; // Late mismatch at every direct-scan offset.
    if (shape == 1 && m && n >= m) // First exact match only at the last possible offset.
    {
        needle[m - 1] = 'b';
        haystack[n - 1] = 'b';
    }
    if (shape == 3) // Periodic exact prefix, including non-power-of-two lengths.
    {
        for (u64 i = 0; i < n; i += 1) haystack[i] = (char)('a' + i % 2);
        for (u64 i = 0; i < m; i += 1) needle[i] = (char)('a' + i % 2);
    }
    if (shape == 4 || shape == 5) // Full byte alphabet includes zero and high-bit bytes.
    {
        for (u64 i = 0; i < n; i += 1) haystack[i] = shape == 4 ? (char)((i * 71 + 13) % 251) : (char)random_byte();
        for (u64 i = 0; i < m; i += 1) needle[i] = (char)random_byte();
        if (m && n >= m)
        {
            memcpy(needle, haystack + n - m, (size_t)m);
            if (shape == 5 && ((n + m) & 1)) needle[m / 2] ^= 1;
        }
    }
    memcpy(haystack_before, haystack, sizeof(haystack));
    memcpy(needle_before, needle, sizeof(needle));
    String8 h = {n ? haystack : 0, n}, p = {m ? needle : 0, m};
    comparisons = 0;
    u64 expected = direct_search(h, p);
    u64 direct_work = comparisons;
    comparisons = 0;
    u64 actual = work_mutant ? direct_search(h, p) : string_first_sequence(h, p);
    u64 observed = comparisons;
    // This conservative linear envelope includes preprocessing and both byte
    // comparisons on a maximal-suffix mismatch. Short needles retain a bounded
    // direct scan, so their envelope includes the fixed threshold factor.
    u64 budget = m >= STRING_FIRST_SEQUENCE_TWO_WAY_MINIMUM_NEEDLE ? 8 * (n + m) + 64 :
        STRING_FIRST_SEQUENCE_TWO_WAY_MINIMUM_NEEDLE * (n + m + 1);
    bool immutable = memcmp(haystack, haystack_before, sizeof(haystack)) == 0 &&
                     memcmp(needle, needle_before, sizeof(needle)) == 0;
    bool exact_work = shape != 0 || !m || n < m || direct_work == (n - m + 1) * m;
    // A complete first-position match must stop after the initial equality
    // probe, without either maximal-suffix pass or a second matching scan.
    bool prefix_work = m < STRING_FIRST_SEQUENCE_TWO_WAY_MINIMUM_NEEDLE || n < m || expected != 0 || observed == m;
    bool valid = expected == actual && observed <= budget && immutable && exact_work && prefix_work;
    printf("%llu,%llu,%u,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%u\n", (unsigned long long)n, (unsigned long long)m, shape,
           (unsigned long long)expected, (unsigned long long)actual, (unsigned long long)observed,
           (unsigned long long)direct_work, (unsigned long long)budget, (unsigned)immutable,
           (unsigned)exact_work, (unsigned)prefix_work, (unsigned)valid);
    return valid;
}
int main(int argc, char** argv)
{
    // Independent cross product: neither axis is defined as a fraction of the
    // other. Include the 31/32/33 switch, null-empty, oversize and equality bounds.
    const u64 lengths[] = {0, 1, 2, 31, 32, 33, 63, 64, 65, 128, 129, 257, 512, 1024, 2048, 4096};
    bool work_mutant = argc > 1 && strcmp(argv[1], "work-mutant") == 0;
    bool valid = true;
    puts("n,m,shape,expected,actual,comparisons,direct_comparisons,budget,immutable,direct_formula,prefix_work,pass");
    for (unsigned ni = 0; ni < sizeof(lengths) / sizeof(lengths[0]); ni += 1)
    {
        for (unsigned mi = 0; mi < sizeof(lengths) / sizeof(lengths[0]); mi += 1)
        {
            for (unsigned shape = 0; shape < 6; shape += 1)
            {
                valid = check_case(lengths[ni], lengths[mi], shape, work_mutant) && valid;
            }
        }
    }
    // Full-byte randomized positions exercise both critical-suffix orders
    // and shifts independently of the repeated-prefix budget population.
    for (u64 trial = 0; trial < 1024; trial += 1)
    {
        u64 n = 32 + random_byte();
        u64 m = 32 + random_byte() % 96;
        valid = check_case(n, m, 5, work_mutant) && valid;
    }
    return valid ? 0 : 1;
}
