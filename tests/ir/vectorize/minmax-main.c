// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Link the same driver to Buster's kernels and Clang's. Each output row identifies the input and all nine results.
#include <limits.h>
#include <stdio.h>

#define BUSTER_DECLARE(type, suffix) \
    type min_if_##suffix(const type*, unsigned long, type); \
    type max_if_##suffix(const type*, unsigned long, type); \
    type min_cond_##suffix(const type*, unsigned long, type); \
    type max_cond_##suffix(const type*, unsigned long, type);

BUSTER_DECLARE(int, i32)
BUSTER_DECLARE(unsigned, u32)
int max_volatile(const volatile int*, unsigned long, int);

int main(void)
{
    static const unsigned seeds[] = { 0, 7, 23, INT_MAX, (unsigned)INT_MIN, UINT_MAX };
    static const unsigned extremes[] = { 0, UINT_MAX, INT_MAX, (unsigned)INT_MIN, 7, 23 };
    int s[65];
    unsigned u[65];
    for (unsigned long n = 0; n <= 65; n += 1)
    {
        // All ties, mixed signedness and extrema, then a unique winner in every possible lane and scalar tail.
        for (unsigned pattern = 0; pattern < 2 * n + 2; pattern += 1)
        {
            for (unsigned i = 0; i < n; i += 1)
            {
                u[i] = pattern == 1 ? extremes[i % 6] : 17;
                s[i] = (int)u[i];
                if (pattern >= 2 && i == (pattern - 2) / 2)
                {
                    u[i] = (pattern & 1) ? UINT_MAX : 0;
                    s[i] = (pattern & 1) ? INT_MAX : INT_MIN;
                }
            }
            for (unsigned k = 0; k < sizeof(seeds) / sizeof(*seeds); k += 1)
            {
                const int* a = n == 0 ? NULL : s;
                const unsigned* b = n == 0 ? NULL : u;
                const unsigned m = seeds[k];
                printf("%lu %u %u: %d %d %d %d %u %u %u %u %d\n", n, pattern, m,
                       min_if_i32(a, n, (int)m), max_if_i32(a, n, (int)m), min_cond_i32(a, n, (int)m), max_cond_i32(a, n, (int)m),
                       min_if_u32(b, n, m), max_if_u32(b, n, m), min_cond_u32(b, n, m), max_cond_u32(b, n, m), max_volatile(a, n, (int)m));
            }
        }
    }
    return 0;
}
