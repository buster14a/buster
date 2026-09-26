// Retained diagnostic evidence for audit 2026-09-26T034206Z. Not part of the
// build graph; compiled by hand with `clang -O3 -march=native harness.c` and run
// as `./harness census.tsv`, where census.tsv is the raw per-call stream the
// audit describes (spelling, result and value per line).
// Incumbent: verbatim copy of c_integer_msvc_suffix_width / c_integer_suffix_valid /
// c_integer_digit / c_conditional_number from src/buster/lib/compiler/frontend/c/c_source.c
// at main ade6ac4b6ecb21f30b61b656439bac476c145e2f.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef unsigned __int128 u128;
typedef struct String8
{
    u8* pointer;
    u64 length;
} String8;

#define NOINLINE __attribute__((noinline))

// ---------------------------------------------------------------- incumbent
static u32 inc_msvc_suffix_width(String8 suffix)
{
    u32 width = 0;
    u64 index = suffix.length && (suffix.pointer[0] == 'u' || suffix.pointer[0] == 'U');
    if (index < suffix.length && (suffix.pointer[index] == 'i' || suffix.pointer[index] == 'I'))
    {
        index += 1;
        u64 digits = suffix.length - index;
        if (digits == 1 && suffix.pointer[index] == '8')
        {
            width = 8;
        }
        else if (digits == 2)
        {
            u8 first = suffix.pointer[index];
            u8 second = suffix.pointer[index + 1];
            if (first == '1' && second == '6')
            {
                width = 16;
            }
            else if (first == '3' && second == '2')
            {
                width = 32;
            }
            else if (first == '6' && second == '4')
            {
                width = 64;
            }
        }
    }
    return width;
}

static bool inc_suffix_valid(String8 suffix)
{
    bool valid = !suffix.length;
    if (suffix.length)
    {
        bool first_unsigned = suffix.pointer[0] == 'u' || suffix.pointer[0] == 'U';
        bool first_long = suffix.pointer[0] == 'l' || suffix.pointer[0] == 'L';
        if (inc_msvc_suffix_width(suffix))
        {
            valid = true;
        }
        else if (suffix.length == 1)
        {
            valid = first_unsigned || first_long;
        }
        else if (suffix.length == 2 || suffix.length == 3)
        {
            bool second_unsigned = suffix.pointer[1] == 'u' || suffix.pointer[1] == 'U';
            bool second_long = suffix.pointer[1] == 'l' || suffix.pointer[1] == 'L';
            bool long_pair = first_long && second_long && suffix.pointer[0] == suffix.pointer[1];
            if (suffix.length == 2)
            {
                valid = long_pair || (first_unsigned && second_long) || (first_long && second_unsigned);
            }
            else
            {
                bool third_unsigned = suffix.pointer[2] == 'u' || suffix.pointer[2] == 'U';
                bool third_long = suffix.pointer[2] == 'l' || suffix.pointer[2] == 'L';
                valid = (first_unsigned && second_long && third_long && suffix.pointer[1] == suffix.pointer[2]) ||
                        (long_pair && third_unsigned);
            }
        }
    }
    return valid;
}

static u32 inc_digit(u8 byte)
{
    u32 digit = UINT32_MAX;
    if (byte >= '0' && byte <= '9')
    {
        digit = (u32)(byte - '0');
    }
    else if (byte >= 'a' && byte <= 'f')
    {
        digit = (u32)(byte - 'a') + 10;
    }
    else if (byte >= 'A' && byte <= 'F')
    {
        digit = (u32)(byte - 'A') + 10;
    }
    return digit;
}

NOINLINE static bool inc_number(String8 spelling, u64* value)
{
    u32 base = 10;
    u64 index = 0;
    if (spelling.length >= 2 && spelling.pointer[0] == '0')
    {
        if (spelling.pointer[1] == 'x' || spelling.pointer[1] == 'X')
        {
            base = 16;
            index = 2;
        }
        else if (spelling.pointer[1] == 'b' || spelling.pointer[1] == 'B')
        {
            base = 2;
            index = 2;
        }
        else
        {
            base = 8;
        }
    }
    u64 parsed = 0;
    u64 cutoff = UINT64_MAX / base;
    u32 last_digit = (u32)(UINT64_MAX % base);
    bool any = false;
    bool valid = true;
    while (valid && index < spelling.length)
    {
        u8 byte = spelling.pointer[index];
        if (byte == '\'')
        {
            valid = any && index + 1 < spelling.length && inc_digit(spelling.pointer[index + 1]) < base;
            index += 1;
        }
        else
        {
            u32 digit = inc_digit(byte);
            if (digit >= base)
            {
                break;
            }
            valid = parsed < cutoff || (parsed == cutoff && digit <= last_digit);
            if (valid)
            {
                parsed = parsed * base + digit;
                any = true;
                index += 1;
            }
        }
    }
    valid = valid && any && inc_suffix_valid((String8){.pointer = spelling.pointer + index, .length = spelling.length - index});
    if (valid)
    {
        *value = parsed;
    }
    return valid;
}

// ------------------------------------------------ structurally different oracle
// Enumerates every accepted suffix spelling, classifies digits by searching an
// alphabet string, and accumulates in 128 bits with a sticky overflow flag.
static char const* oracle_suffixes[] = {
    "", "u", "U", "l", "L", "ul", "uL", "Ul", "UL", "lu", "lU", "Lu", "LU", "ll", "LL", "ull", "uLL", "Ull", "ULL", "llu", "llU",
    "LLu", "LLU",
};
static bool oracle_suffix(u8 const* p, u64 n)
{
    bool ok = false;
    for (u32 i = 0; i < sizeof(oracle_suffixes) / sizeof(oracle_suffixes[0]) && !ok; i += 1)
    {
        ok = strlen(oracle_suffixes[i]) == n && memcmp(oracle_suffixes[i], p, n) == 0;
    }
    // MSVC: [uU]?[iI](8|16|32|64)
    char const* widths[] = {"8", "16", "32", "64"};
    for (u32 u = 0; u < 2 && !ok; u += 1)
    {
        for (u32 w = 0; w < 4 && !ok; w += 1)
        {
            u64 need = u + 1 + strlen(widths[w]);
            if (need == n && (u == 0 || p[0] == 'u' || p[0] == 'U') && (p[u] == 'i' || p[u] == 'I') && memcmp(p + u + 1, widths[w], n - u - 1) == 0)
            {
                ok = true;
            }
        }
    }
    return ok;
}
static int oracle_digit(u8 c, u32 base)
{
    static char const alphabet[] = "0123456789abcdef";
    u8 lower = (c >= 'A' && c <= 'Z') ? (u8)(c + 32) : c;
    char const* hit = lower ? memchr(alphabet, lower, 16) : 0;
    int d = hit ? (int)(hit - alphabet) : -1;
    return d >= 0 && (u32)d < base ? d : -1;
}
static bool oracle_number(String8 s, u64* value)
{
    u32 base = 10;
    u64 i = 0;
    if (s.length >= 2 && s.pointer[0] == '0')
    {
        u8 c = s.pointer[1];
        base = (c == 'x' || c == 'X') ? 16 : (c == 'b' || c == 'B') ? 2 : 8;
        i = base == 8 ? 0 : 2;
    }
    u128 acc = 0;
    bool overflow = false, any = false, bad_separator = false;
    u64 j = i;
    for (; j < s.length; j += 1)
    {
        u8 c = s.pointer[j];
        if (c == '\'')
        {
            if (!(any && j + 1 < s.length && oracle_digit(s.pointer[j + 1], base) >= 0))
            {
                bad_separator = true;
                break;
            }
            continue;
        }
        int d = oracle_digit(c, base);
        if (d < 0)
        {
            break;
        }
        acc = acc * base + (u32)d;
        if (acc >> 64)
        {
            overflow = true;
            break;
        }
        any = true;
    }
    bool ok = !bad_separator && !overflow && any && oracle_suffix(s.pointer + j, s.length - j);
    if (ok)
    {
        *value = (u64)acc;
    }
    return ok;
}

// ------------------------------------------- strongest straightforward scalar
// Same prefix/suffix contract; digit classification by one table load, per-base
// constant cutoffs (no divide), and a check-free Horner step while the digit
// count is below the base's never-overflowing length.
static u8 scalar_digit_table[256];
static void scalar_tables_init(void)
{
    for (u32 c = 0; c < 256; c += 1)
    {
        u32 d = inc_digit((u8)c);
        scalar_digit_table[c] = d < 16 ? (u8)d : 0xff;
    }
}
static u32 scalar_safe_digits(u32 base)
{
    // Largest n with base^n - 1 <= UINT64_MAX guaranteed: any n-digit value fits.
    return base == 16 ? 16 : base == 10 ? 19 : base == 8 ? 21 : 64;
}
NOINLINE static bool scalar_number(String8 spelling, u64* value)
{
    u8 const* p = spelling.pointer;
    u64 n = spelling.length;
    u32 base = 10;
    u64 index = 0;
    if (n >= 2 && p[0] == '0')
    {
        u8 c = p[1];
        base = (c == 'x' || c == 'X') ? 16 : (c == 'b' || c == 'B') ? 2 : 8;
        index = base == 8 ? 0 : 2;
    }
    u64 cutoff = base == 16 ? UINT64_MAX >> 4 : base == 10 ? UINT64_MAX / 10 : base == 8 ? UINT64_MAX >> 3 : UINT64_MAX >> 1;
    u32 last_digit = base == 16 ? 15 : base == 10 ? 5 : base == 8 ? 7 : 1;
    u32 safe = scalar_safe_digits(base);
    u64 parsed = 0;
    u32 count = 0;
    bool valid = true;
    while (valid && index < n)
    {
        u8 byte = p[index];
        u32 digit = scalar_digit_table[byte];
        if (digit < base)
        {
            if (count >= safe)
            {
                valid = parsed < cutoff || (parsed == cutoff && digit <= last_digit);
            }
            parsed = valid ? parsed * base + digit : parsed;
            count += valid;
            index += valid;
        }
        else if (byte == '\'')
        {
            valid = count && index + 1 < n && scalar_digit_table[p[index + 1]] < base;
            index += 1;
        }
        else
        {
            break;
        }
    }
    valid = valid && count && inc_suffix_valid((String8){.pointer = (u8*)p + index, .length = n - index});
    if (valid)
    {
        *value = parsed;
    }
    return valid;
}

// ------------------------------------------------------ blocked formulation
// Block summary for k digits of base b: the affine map x -> x * b^k + v.
// Composition (m1, v1) then (m2, v2) = (m1 * m2, v1 * m2 + v2); identity (1, 0).
// Construction for k <= 8 digits: per-byte digit values by SWAR, left-justified
// with zero digits (leading zeros do not change a value), then three pairwise
// multiply-add reductions. Composition across blocks uses a 128-bit product so
// overflow is exact. A block that reaches a separator falls back to the
// scalar path from the start of the digits, because a separator is rare and
// its validity depends on the neighbouring bytes.
static u64 blocked_load(u8 const* p, u64 available)
{
    u64 x = 0;
    if (available >= 8)
    {
        memcpy(&x, p, 8);
    }
    else
    {
        // Overlapping loads of 4/2/1 bytes, no per-byte loop.
        u8 buffer[8] = {0};
        if (available >= 4)
        {
            memcpy(buffer, p, 4);
            memcpy(buffer + available - 4, p + available - 4, 4);
        }
        else if (available >= 2)
        {
            memcpy(buffer, p, 2);
            memcpy(buffer + available - 2, p + available - 2, 2);
        }
        else if (available == 1)
        {
            buffer[0] = p[0];
        }
        memcpy(&x, buffer, 8);
    }
    return x;
}
// Per-byte "is a digit of base" high-bit mask and per-byte digit values.
// Range tests use (x | 0x80..) - c, which cannot borrow across bytes; bytes
// with the high bit set are never digits.
static inline u64 blocked_classify(u64 x, u32 base, u64* digits)
{
    u64 const ones = 0x0101010101010101ull;
    u64 const high = 0x8080808080808080ull;
    u64 set = x | high;
    u64 at_least_zero = set - ones * '0';
    u64 at_least_limit = set - ones * (u64)('0' + (base < 10 ? base : 10));
    u64 is_dec = at_least_zero & ~at_least_limit & high & ~x;
    u64 values = at_least_zero & ~high;
    u64 is_digit = is_dec;
    if (base == 16)
    {
        u64 folded = set | ones * 0x20;
        u64 at_least_a = folded - ones * 'a';
        u64 at_least_g = folded - ones * 'g';
        u64 is_hex = at_least_a & ~at_least_g & high & ~x;
        u64 hex_values = (folded - ones * ('a' - 10)) & ~high;
        u64 hex_select = (is_hex >> 7) * 0xff;
        values = (values & ~hex_select) | (hex_values & hex_select);
        is_digit |= is_hex;
    }
    *digits = values & ((is_digit >> 7) * 0xff);
    return is_digit;
}
static u64 blocked_powers[17][9];
static void blocked_tables_init(void)
{
    u32 bases[] = {2, 8, 10, 16};
    for (u32 b = 0; b < 4; b += 1)
    {
        u64 m = 1;
        for (u32 k = 0; k <= 8; k += 1)
        {
            blocked_powers[bases[b]][k] = m;
            m *= bases[b];
        }
    }
}
static inline u64 blocked_reduce8(u64 d, u32 base)
{
    // d holds 8 digit values, most significant digit in byte 0.
    u64 b1 = base, b2 = b1 * b1, b4 = b2 * b2;
    d = (d * (b1 * 256 + 1)) >> 8;
    d &= 0x00FF00FF00FF00FFull;
    d = (d * (b2 * 65536 + 1)) >> 16;
    d &= 0x0000FFFF0000FFFFull;
    d = (d * ((b4 << 32) + 1)) >> 32;
    return d;
}
NOINLINE static bool blocked_number(String8 spelling, u64* value)
{
    u8 const* p = spelling.pointer;
    u64 n = spelling.length;
    u32 base = 10;
    u64 index = 0;
    if (n >= 2 && p[0] == '0')
    {
        u8 c = p[1];
        base = (c == 'x' || c == 'X') ? 16 : (c == 'b' || c == 'B') ? 2 : 8;
        index = base == 8 ? 0 : 2;
    }
    u64 parsed = 0;
    bool any = false;
    bool valid = true;
    bool fallback = false;
    bool more = true;
    while (more && index < n)
    {
        u64 available = n - index;
        u64 x = blocked_load(p + index, available);
        u64 digits = 0;
        u64 is_digit = blocked_classify(x, base, &digits);
        u64 limit_mask = available >= 8 ? ~0ull : ((1ull << (8 * available)) - 1);
        u64 run_break = ~is_digit & 0x8080808080808080ull & limit_mask;
        u32 k = run_break ? (u32)(__builtin_ctzll(run_break) >> 3) : (u32)(available >= 8 ? 8 : available);
        if (k < 8 && k < available && p[index + k] == '\'')
        {
            fallback = true;
            break;
        }
        if (k)
        {
            // Left-justify the k digits: shift out the unused bytes as leading zeros.
            u64 justified = k == 8 ? digits : digits << (8 * (8 - k));
            u64 v = blocked_reduce8(justified, base);
            u64 m = blocked_powers[base][k];
            u128 wide = (u128)parsed * m + v;
            valid = (wide >> 64) == 0;
            parsed = (u64)wide;
            any = true;
            index += k;
        }
        more = valid && k == 8;
    }
    if (fallback)
    {
        return scalar_number(spelling, value);
    }
    valid = valid && any && inc_suffix_valid((String8){.pointer = (u8*)p + index, .length = n - index});
    if (valid)
    {
        *value = parsed;
    }
    return valid;
}

// Best case for the blocked form: an unconditional 8-byte load (requires the
// caller's buffer to be readable 8 bytes past every spelling; production
// spellings carry no such guarantee), bytes past the spelling masked off.
NOINLINE static bool blocked_overread_number(String8 spelling, u64* value)
{
    u8 const* p = spelling.pointer;
    u64 n = spelling.length;
    u32 base = 10;
    u64 index = 0;
    if (n >= 2 && p[0] == '0')
    {
        u8 c = p[1];
        base = (c == 'x' || c == 'X') ? 16 : (c == 'b' || c == 'B') ? 2 : 8;
        index = base == 8 ? 0 : 2;
    }
    u64 parsed = 0;
    bool any = false;
    bool valid = true;
    bool fallback = false;
    bool more = true;
    while (more && index < n)
    {
        u64 available = n - index;
        u64 x;
        memcpy(&x, p + index, 8);
        u64 digits = 0;
        u64 is_digit = blocked_classify(x, base, &digits);
        u64 limit_mask = available >= 8 ? ~0ull : ((1ull << (8 * available)) - 1);
        u64 run_break = ~is_digit & 0x8080808080808080ull & limit_mask;
        u32 k = run_break ? (u32)(__builtin_ctzll(run_break) >> 3) : (u32)(available >= 8 ? 8 : available);
        if (k < 8 && k < available && p[index + k] == '\'')
        {
            fallback = true;
            break;
        }
        if (k)
        {
            u64 justified = k == 8 ? digits : digits << (8 * (8 - k));
            u64 v = blocked_reduce8(justified, base);
            u64 m = blocked_powers[base][k];
            u128 wide = (u128)parsed * m + v;
            valid = (wide >> 64) == 0;
            parsed = (u64)wide;
            any = true;
            index += k;
        }
        more = valid && k == 8;
    }
    if (fallback)
    {
        return scalar_number(spelling, value);
    }
    valid = valid && any && inc_suffix_valid((String8){.pointer = (u8*)p + index, .length = n - index});
    if (valid)
    {
        *value = parsed;
    }
    return valid;
}

// ---------------------------------------------------------------- driver
typedef bool (*NumberFunction)(String8, u64*);
static u64 failures;
static void check_one(String8 s, char const* where)
{
    u64 v_inc = 0xdeadbeef, v_ora = 0xdeadbeef, v_sca = 0xdeadbeef, v_blk = 0xdeadbeef;
    bool r_inc = inc_number(s, &v_inc);
    bool r_ora = oracle_number(s, &v_ora);
    bool r_sca = scalar_number(s, &v_sca);
    bool r_blk = blocked_number(s, &v_blk);
    u8 padded[128];
    u64 v_ovr = 0xdeadbeef;
    bool r_ovr = r_blk;
    if (s.length <= 100)
    {
        memcpy(padded, s.pointer, s.length);
        memset(padded + s.length, 'Z', 16);
        r_ovr = blocked_overread_number((String8){.pointer = padded, .length = s.length}, &v_ovr);
        memset(padded + s.length, '7', 16);
        u64 v_ovr2 = 0xdeadbeef;
        bool r_ovr2 = blocked_overread_number((String8){.pointer = padded, .length = s.length}, &v_ovr2);
        if (r_ovr2 != r_ovr || (r_ovr && v_ovr2 != v_ovr))
        {
            failures += 1;
        }
    }
    else
    {
        v_ovr = v_blk;
    }
    bool agree = r_inc == r_ora && r_inc == r_sca && r_inc == r_blk && r_inc == r_ovr &&
                 (!r_inc || (v_inc == v_ora && v_inc == v_sca && v_inc == v_blk && v_inc == v_ovr));
    if (!agree)
    {
        if (failures < 20)
        {
            printf("MISMATCH %s '%.*s' inc=%d:%llu ora=%d:%llu sca=%d:%llu blk=%d:%llu\n", where, (int)s.length, (char*)s.pointer, r_inc,
                   (unsigned long long)v_inc, r_ora, (unsigned long long)v_ora, r_sca, (unsigned long long)v_sca, r_blk,
                   (unsigned long long)v_blk);
        }
        failures += 1;
    }
}

// Composition law of the summary, checked independently of the parsers:
// the affine map of a||b equals the composition of the maps of a and b, with
// exact (128-bit) arithmetic and a sticky overflow bit.
typedef struct Summary
{
    u128 m;
    u128 v;
    bool overflow;
} Summary;
static Summary summary_identity(void)
{
    return (Summary){.m = 1, .v = 0, .overflow = false};
}
static Summary summary_compose(Summary a, Summary b)
{
    // x -> (x * a.m + a.v) * b.m + b.v. Saturate at 2^64 for the exactness test.
    u128 limit = (u128)1 << 64;
    Summary r;
    r.overflow = a.overflow || b.overflow;
    r.m = a.m * b.m;
    r.v = a.v * b.m + b.v;
    if (r.m >= limit * limit / 4 || r.v >= limit)
    {
        r.overflow = r.overflow || r.v >= limit;
        r.m = r.m >= limit ? limit : r.m;
        r.v = r.v >= limit ? limit : r.v;
    }
    return r;
}
static Summary summary_digits(u8 const* d, u32 k, u32 base)
{
    Summary s = summary_identity();
    for (u32 i = 0; i < k; i += 1)
    {
        s = summary_compose(s, (Summary){.m = base, .v = d[i], .overflow = false});
    }
    return s;
}
static u64 composition_checks;
static void check_composition(u8 const* d, u32 k, u32 base)
{
    Summary whole = summary_digits(d, k, base);
    for (u32 split = 0; split <= k; split += 1)
    {
        Summary left = summary_digits(d, split, base);
        Summary right = summary_digits(d + split, k - split, base);
        Summary joined = summary_compose(left, right);
        bool same = joined.overflow == whole.overflow && (whole.overflow || (joined.v == whole.v && joined.m == whole.m));
        composition_checks += 1;
        if (!same)
        {
            failures += 1;
            printf("COMPOSITION base=%u k=%u split=%u\n", base, k, split);
        }
        // Associativity over three parts.
        for (u32 second = split; second <= k; second += 1)
        {
            Summary a = summary_digits(d, split, base);
            Summary b = summary_digits(d + split, second - split, base);
            Summary c = summary_digits(d + second, k - second, base);
            Summary ab_c = summary_compose(summary_compose(a, b), c);
            Summary a_bc = summary_compose(a, summary_compose(b, c));
            bool assoc = ab_c.overflow == a_bc.overflow && (ab_c.overflow || (ab_c.v == a_bc.v && ab_c.m == a_bc.m));
            composition_checks += 1;
            if (!assoc)
            {
                failures += 1;
                printf("ASSOCIATIVITY base=%u k=%u\n", base, k);
            }
        }
    }
}

static u64 rng_state = 0x9E3779B97F4A7C15ull;
static u64 rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static double now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

int main(int argc, char** argv)
{
    scalar_tables_init();
    blocked_tables_init();
    // 1. Exhaustive small strings over an alphabet that reaches every branch.
    char const alphabet[] = "0178afgxXbB'uUlLi64 ";
    u32 alphabet_size = (u32)strlen(alphabet);
    u8 buffer[64];
    u64 exhaustive = 0;
    for (u32 length = 0; length <= 5; length += 1)
    {
        u64 total = 1;
        for (u32 i = 0; i < length; i += 1)
        {
            total *= alphabet_size;
        }
        for (u64 code = 0; code < total; code += 1)
        {
            u64 c = code;
            for (u32 i = 0; i < length; i += 1)
            {
                buffer[i] = (u8)alphabet[c % alphabet_size];
                c /= alphabet_size;
            }
            check_one((String8){.pointer = buffer, .length = length}, "exhaustive");
            exhaustive += 1;
        }
    }
    printf("exhaustive strings: %llu, failures so far %llu\n", (unsigned long long)exhaustive, (unsigned long long)failures);
    // 2. Every byte value at every position of short prefixes (irregular bytes, high-bit bytes).
    for (u32 prefix = 0; prefix < 4; prefix += 1)
    {
        char const* heads[] = {"1", "0x1", "0", "0b1"};
        for (u32 position = 0; position < 12; position += 1)
        {
            for (u32 byte = 0; byte < 256; byte += 1)
            {
                u32 n = (u32)strlen(heads[prefix]);
                memcpy(buffer, heads[prefix], n);
                for (u32 i = n; i < n + 12; i += 1)
                {
                    buffer[i] = '1';
                }
                buffer[n + position] = (u8)byte;
                check_one((String8){.pointer = buffer, .length = n + 12}, "bytes");
                check_one((String8){.pointer = buffer, .length = n + position + 1}, "bytes-tail");
            }
        }
    }
    printf("byte sweep done, failures so far %llu\n", (unsigned long long)failures);
    // 3. Extremes and every block boundary: digit runs of every length 1..70 in
    //    every base, with leading zeros, maximum digits, near-overflow values,
    //    and suffixes/separators placed at every offset.
    char const* bases_prefix[] = {"", "0x", "0", "0b"};
    char const* max_digit = "9f71";
    for (u32 b = 0; b < 4; b += 1)
    {
        for (u32 run = 1; run <= 70; run += 1)
        {
            for (u32 fill = 0; fill < 4; fill += 1)
            {
                u32 n = (u32)strlen(bases_prefix[b]);
                memcpy(buffer, bases_prefix[b], n);
                for (u32 i = 0; i < run && n + i < 60; i += 1)
                {
                    u8 d = fill == 0 ? '0' : fill == 1 ? (u8)max_digit[b] : fill == 2 ? (i + 1 == run ? '1' : '0') : (u8)(i == 0 ? '1' : '0');
                    buffer[n + i] = d;
                }
                u32 len = n + (run < 60 - n ? run : 60 - n);
                check_one((String8){.pointer = buffer, .length = len}, "runs");
                char const* suffixes[] = {"u", "ULL", "l", "i64", "ui8", "'1", "''1", "'", "x", "LLL"};
                for (u32 s = 0; s < 10; s += 1)
                {
                    u8 extended[80];
                    memcpy(extended, buffer, len);
                    u32 sl = (u32)strlen(suffixes[s]);
                    memcpy(extended + len, suffixes[s], sl);
                    check_one((String8){.pointer = extended, .length = len + sl}, "runs-suffix");
                }
            }
        }
    }
    char const* extremes[] = {"18446744073709551615", "18446744073709551616", "18446744073709551614", "99999999999999999999",
                              "0xFFFFFFFFFFFFFFFF", "0x10000000000000000", "0x0000000000000000FFFFFFFFFFFFFFFF", "01777777777777777777777",
                              "02000000000000000000000", "0b1111111111111111111111111111111111111111111111111111111111111111",
                              "0b11111111111111111111111111111111111111111111111111111111111111111", "000000000000000000000000000001",
                              "1844674407370955161'5", "0x'1", "1'", "1''2", "0x1'f'f", "0b", "0x", "0", "00", "08", "0b2", "0xg",
                              "0001844674407370955161", "18'446'744'073'709'551'615u", "18'446'744'073'709'551'616"};
    for (u32 i = 0; i < sizeof(extremes) / sizeof(extremes[0]); i += 1)
    {
        check_one((String8){.pointer = (u8*)extremes[i], .length = strlen(extremes[i])}, "extreme");
    }
    // 4. Randomized long strings with random separators/suffixes.
    for (u32 trial = 0; trial < 2000000; trial += 1)
    {
        u32 b = (u32)(rng() % 4);
        u32 n = (u32)strlen(bases_prefix[b]);
        memcpy(buffer, bases_prefix[b], n);
        u32 len = n + (u32)(rng() % 30);
        char const* digit_sets[] = {"0123456789", "0123456789abcdefABCDEF", "01234567", "01"};
        for (u32 i = n; i < len; i += 1)
        {
            u64 r = rng() % 100;
            buffer[i] = r < 3 ? '\'' : r < 5 ? (u8)"uUlLi8x.eE"[rng() % 10] : (u8)digit_sets[b][rng() % strlen(digit_sets[b])];
        }
        check_one((String8){.pointer = buffer, .length = len}, "random");
    }
    printf("random done, failures so far %llu\n", (unsigned long long)failures);
    // 5. Composition law: exhaustive for small alphabets, random for long.
    for (u32 b = 0; b < 4; b += 1)
    {
        u32 base = b == 0 ? 10 : b == 1 ? 16 : b == 2 ? 8 : 2;
        u32 max_len = base == 2 ? 10 : base == 8 ? 5 : base == 10 ? 4 : 3;
        u8 d[80];
        for (u32 k = 0; k <= max_len; k += 1)
        {
            u64 total = 1;
            for (u32 i = 0; i < k; i += 1)
            {
                total *= base;
            }
            for (u64 code = 0; code < total; code += 1)
            {
                u64 c = code;
                for (u32 i = 0; i < k; i += 1)
                {
                    d[i] = (u8)(c % base);
                    c /= base;
                }
                check_composition(d, k, base);
            }
        }
        for (u32 trial = 0; trial < 3000; trial += 1)
        {
            u32 k = (u32)(rng() % 70);
            for (u32 i = 0; i < k; i += 1)
            {
                d[i] = (u8)(rng() % base);
            }
            check_composition(d, k, base);
        }
    }
    printf("composition checks: %llu, failures so far %llu\n", (unsigned long long)composition_checks, (unsigned long long)failures);

    // 6. Census replay: verify and time on the recorded call stream.
    if (argc > 1)
    {
        FILE* f = fopen(argv[1], "rb");
        if (!f)
        {
            return 2;
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        u8* raw = malloc((size_t)size + 64);
        if (fread(raw, 1, (size_t)size, f) != (size_t)size)
        {
            return 3;
        }
        fclose(f);
        u64 capacity = 3000000;
        String8* calls = malloc(sizeof(String8) * capacity);
        u64* expected = malloc(sizeof(u64) * capacity);
        u64 count = 0;
        u8* arena = malloc((size_t)size + 64);
        u64 cursor = 0;
        for (long i = 0; i < size && count < capacity;)
        {
            long start = i;
            while (raw[i] != '\t')
            {
                i += 1;
            }
            long length = i - start;
            memcpy(arena + cursor, raw + start, (size_t)length);
            calls[count] = (String8){.pointer = arena + cursor, .length = (u64)length};
            cursor += (u64)length;
            arena[cursor++] = ' ';
            i += 1;
            i += 2; // ok flag + tab
            expected[count] = strtoull((char*)raw + i, 0, 10);
            while (raw[i] != '\n')
            {
                i += 1;
            }
            i += 1;
            count += 1;
        }
        for (u64 i = 0; i < count; i += 1)
        {
            check_one(calls[i], "census");
            u64 v = 0;
            if (!inc_number(calls[i], &v) || v != expected[i])
            {
                failures += 1;
            }
        }
        printf("census calls: %llu, failures so far %llu\n", (unsigned long long)count, (unsigned long long)failures);
        NumberFunction functions[] = {inc_number, scalar_number, blocked_number, blocked_overread_number};
        char const* names[] = {"incumbent", "scalar", "blocked", "overread"};
        u32 order[] = {0, 1, 2, 3, 3, 2, 1, 0, 1, 3, 0, 2, 0, 2, 3, 1, 2, 1, 3, 0, 1, 2, 0, 3, 3, 0, 1, 2};
        double best[4] = {1e30, 1e30, 1e30, 1e30};
        double all[4][8];
        u32 samples[4] = {0};
        u64 sink = 0;
        for (u32 o = 0; o < sizeof(order) / sizeof(order[0]); o += 1)
        {
            u32 which = order[o];
            double start = now_ns();
            for (u32 repeat = 0; repeat < 4; repeat += 1)
            {
                for (u64 i = 0; i < count; i += 1)
                {
                    u64 v = 0;
                    sink += functions[which](calls[i], &v) + v;
                }
            }
            double elapsed = (now_ns() - start) / (4.0 * (double)count);
            all[which][samples[which]++] = elapsed;
            best[which] = elapsed < best[which] ? elapsed : best[which];
        }
        for (u32 w = 0; w < 4; w += 1)
        {
            printf("%-10s min %.3f ns/call  samples:", names[w], best[w]);
            for (u32 s = 0; s < samples[w]; s += 1)
            {
                printf(" %.3f", all[w][s]);
            }
            printf("\n");
        }
        printf("sink %llu\n", (unsigned long long)sink);
    }
    // 7. Crossover: fixed digit count k, decimal and hexadecimal, no suffix.
    {
        u64 lits = 200000;
        u8* pool = malloc(lits * 40 + 64);
        String8* list = malloc(sizeof(String8) * lits);
        NumberFunction functions[] = {inc_number, scalar_number, blocked_number, blocked_overread_number};
        for (u32 hex = 0; hex < 2; hex += 1)
        {
            printf("crossover %s: k incumbent scalar blocked overread (ns/call, min of 5)\n", hex ? "hex" : "decimal");
            for (u32 k = 1; k <= (hex ? 16u : 19u); k += 1)
            {
                u64 cursor = 0;
                for (u64 i = 0; i < lits; i += 1)
                {
                    u8* at = pool + cursor;
                    u32 n = 0;
                    if (hex)
                    {
                        at[n++] = '0';
                        at[n++] = 'x';
                    }
                    for (u32 j = 0; j < k; j += 1)
                    {
                        at[n++] = hex ? (u8)"0123456789abcdef"[rng() % 16] : (u8)('0' + (j == 0 ? 1 + rng() % 9 : rng() % 10));
                    }
                    list[i] = (String8){.pointer = at, .length = n};
                    cursor += n;
                    pool[cursor++] = ',';
                }
                printf("  %2u", k);
                for (u32 w = 0; w < 4; w += 1)
                {
                    double best_time = 1e30;
                    u64 sink2 = 0;
                    for (u32 rep = 0; rep < 5; rep += 1)
                    {
                        double start = now_ns();
                        for (u64 i = 0; i < lits; i += 1)
                        {
                            u64 v = 0;
                            sink2 += functions[w](list[i], &v) + v;
                        }
                        double t = (now_ns() - start) / (double)lits;
                        best_time = t < best_time ? t : best_time;
                    }
                    printf(" %7.3f", best_time);
                    rng_state ^= sink2 & 1;
                }
                printf("\n");
            }
        }
    }
    printf("TOTAL FAILURES %llu\n", (unsigned long long)failures);
    return failures != 0;
}
