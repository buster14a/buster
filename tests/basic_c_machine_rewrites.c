// Verified local machine rewrites (docs/machine-rewrite-campaign.md): zero
// materializations next to live and dead flags, spills and reloads around
// calls, small and large frames (disp8 and disp32 frame offsets), and
// epilogues with and without callee-saved registers. Every check computes
// its expectation independently; main returns the first failing check.

typedef unsigned long long u64;
typedef long long s64;

static volatile int sink;

__attribute__((noinline)) static int opaque(int value)
{
    sink = value;
    return value;
}

__attribute__((noinline)) static u64 opaque64(u64 value)
{
    sink = (int)value;
    return value;
}

// A zero constant beside comparisons of every signedness and relation: the
// zero must not disturb the flags a following setcc/branch reads.
__attribute__((noinline)) static int compare_with_zero_between(int a, int b)
{
    int less = a < b;
    int zero = 0;
    int below = (unsigned)a < (unsigned)b;
    int equal = a == b;
    return less * 1 + below * 2 + equal * 4 + zero;
}

__attribute__((noinline)) static int zero_arguments(int a)
{
    return opaque(0) + opaque(a) + opaque(0);
}

__attribute__((noinline)) static s64 conditional_zero(s64 a, s64 b)
{
    return a < b ? 0 : a - b;
}

// Enough simultaneously live values across calls to force spills and
// reloads through frame slots.
__attribute__((noinline)) static u64 spill_pressure(u64 seed)
{
    u64 a = opaque64(seed + 1), b = opaque64(seed + 2), c = opaque64(seed + 3), d = opaque64(seed + 4);
    u64 e = opaque64(seed + 5), f = opaque64(seed + 6), g = opaque64(seed + 7), h = opaque64(seed + 8);
    u64 i = opaque64(seed + 9), j = opaque64(seed + 10), k = opaque64(seed + 11), l = opaque64(seed + 12);
    u64 m = opaque64(seed + 13), n = opaque64(seed + 14), o = opaque64(seed + 15), p = opaque64(seed + 16);
    u64 total = opaque64(a * 3 + b) + opaque64(c * 5 + d) + opaque64(e * 7 + f) + opaque64(g * 11 + h);
    total += opaque64(i ^ j) + opaque64(k | l) + opaque64(m & n) + opaque64(o - p);
    return total + a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p;
}

// A frame larger than a disp8 reach: locals at both displacement classes.
__attribute__((noinline)) static int large_frame(int seed)
{
    volatile int small[8];
    volatile int big[200];
    for (int index = 0; index < 8; index += 1)
    {
        small[index] = seed + index;
    }
    for (int index = 0; index < 200; index += 1)
    {
        big[index] = seed * index;
    }
    int total = 0;
    for (int index = 0; index < 8; index += 1)
    {
        total += small[index];
    }
    total += big[0] + big[99] + big[199];
    return total;
}

static int returns_zero(void)
{
    return 0;
}

int main(void)
{
    int result = 0;
    int inputs[] = {-3, -1, 0, 1, 2, 0x7fffffff, (int)0x80000000};
    for (unsigned x = 0; x < sizeof(inputs) / sizeof(inputs[0]) && !result; x += 1)
    {
        for (unsigned y = 0; y < sizeof(inputs) / sizeof(inputs[0]) && !result; y += 1)
        {
            int a = opaque(inputs[x]);
            int b = opaque(inputs[y]);
            int expected = (a < b) * 1 + ((unsigned)a < (unsigned)b) * 2 + (a == b) * 4;
            if (compare_with_zero_between(a, b) != expected) result = 1;
            s64 wide_a = (s64)a * 3;
            s64 wide_b = (s64)b * 5;
            s64 expected_zero = wide_a < wide_b ? 0 : wide_a - wide_b;
            if (!result && conditional_zero(wide_a, wide_b) != expected_zero) result = 2;
        }
    }
    if (!result && zero_arguments(41) != 41) result = 3;
    u64 seed = opaque64(1000);
    u64 expected_spill = 0;
    {
        u64 v[17];
        for (int index = 1; index <= 16; index += 1) v[index] = seed + (u64)index;
        expected_spill = (v[1] * 3 + v[2]) + (v[3] * 5 + v[4]) + (v[5] * 7 + v[6]) + (v[7] * 11 + v[8]);
        expected_spill += (v[9] ^ v[10]) + (v[11] | v[12]) + (v[13] & v[14]) + (v[15] - v[16]);
        for (int index = 1; index <= 16; index += 1) expected_spill += v[index];
    }
    if (!result && spill_pressure(seed) != expected_spill) result = 4;
    int expected_frame = 0;
    for (int index = 0; index < 8; index += 1) expected_frame += 7 + index;
    expected_frame += 7 * 0 + 7 * 99 + 7 * 199;
    if (!result && large_frame(7) != expected_frame) result = 5;
    if (!result && returns_zero() != 0) result = 6;
    return result;
}
