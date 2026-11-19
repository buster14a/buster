// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// The reload in the winning arm is deliberate. For a volatile array it is a second observable read.
#define BUSTER_MINMAX_KERNELS(type, suffix) \
type min_if_##suffix(const type* a, unsigned long n, type m) \
{ \
    for (unsigned long i = 0; i < n; i += 1) { if (m >= a[i]) { m = a[i]; } } \
    return m; \
} \
type max_if_##suffix(const type* a, unsigned long n, type m) \
{ \
    for (unsigned long i = 0; i < n; i += 1) { if (a[i] > m) { m = a[i]; } } \
    return m; \
} \
type min_cond_##suffix(const type* a, unsigned long n, type m) \
{ \
    for (unsigned long i = 0; i < n; i += 1) { m = a[i] < m ? a[i] : m; } \
    return m; \
} \
type max_cond_##suffix(const type* a, unsigned long n, type m) \
{ \
    for (unsigned long i = 0; i < n; i += 1) { m = m >= a[i] ? m : a[i]; } \
    return m; \
}

BUSTER_MINMAX_KERNELS(int, i32)
BUSTER_MINMAX_KERNELS(unsigned, u32)

int max_volatile(const volatile int* a, unsigned long n, int m)
{
    for (unsigned long i = 0; i < n; i += 1)
    {
        if (a[i] > m)
        {
            m = a[i];
        }
    }
    return m;
}

// The caller's parameter is unqualified. Both volatile accesses come from inside the inlined callee.
static __attribute__((always_inline)) inline int volatile_element(const int* p)
{
    return *(const volatile int*)p;
}

int max_inline_volatile(const int* a, unsigned long n, int m)
{
    for (unsigned long i = 0; i < n; i += 1)
    {
        m = volatile_element(a + i) > m ? volatile_element(a + i) : m;
    }
    return m;
}
