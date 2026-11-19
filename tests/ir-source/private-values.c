// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

// One local is assigned before every read within its block. Another can reach
// a join through either arm or the backedge. Taking the third local's address
// makes its storage observable across a call.
__attribute__((noinline)) static void add_to(int* p, int n)
{
    *p += n;
}

__attribute__((noinline)) static int accumulate(int count, int input)
{
    int total = input;
    int addressed = 1;
    for (int i = 0; i < count; ++i)
    {
        int local;
        if (i & 1)
        {
            local = input + i;
            total += local;
        }
        else
        {
            local = input - i;
            total -= local;
        }
        add_to(&addressed, total);
    }
    return addressed + total;
}

#ifdef BUSTER_TEST_INLINE_VECTOR
typedef int FourInts __attribute__((vector_size(16)));

static inline __attribute__((always_inline)) int vector_callee(const FourInts* a, const FourInts* b)
{
    FourInts sum = *a + *b;
    return sum[1];
}

static inline __attribute__((always_inline)) int scalar_middle(const FourInts* a, const FourInts* b, int input)
{
    return vector_callee(a, b) + input;
}

__attribute__((noinline)) static int scalar_caller(const FourInts* a, const FourInts* b, int input)
{
    return scalar_middle(a, b, input) - input;
}
#endif

int main(void)
{
    if (accumulate(0, 7) != 8 || accumulate(1, 7) != 1 || accumulate(4, 7) != 38)
    {
        return 1;
    }
#ifdef BUSTER_TEST_INLINE_VECTOR
    static const FourInts a = { 1, 3, 5, 7 };
    static const FourInts b = { 2, 4, 6, 8 };
    if (scalar_caller(&a, &b, 5) != 7)
    {
        return 2;
    }
#endif
    return 0;
}
