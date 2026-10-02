
#ifndef NEGATIVE_CONTROL
#define NEGATIVE_CONTROL 0
#endif
unsigned helper(unsigned x) { return (x * 33u) ^ (x >> 7u); }
unsigned sum_scale(const unsigned *a, const unsigned *b, unsigned n, unsigned salt)
{
    unsigned result = salt;
    (void)b;
    for (unsigned i = 0; i < n; i += 1) result += a[i] * 3u + 7u;
    return result + NEGATIVE_CONTROL;
}
unsigned divide7(const unsigned *a, const unsigned *b, unsigned n, unsigned salt)
{
    unsigned result = 0;
    (void)b;
    for (unsigned i = 0; i < n; i += 1) result += (a[i] + salt) / 7u;
    return result;
}
unsigned dot(const unsigned *a, const unsigned *b, unsigned n, unsigned salt)
{
    unsigned result = salt;
    for (unsigned i = 0; i < n; i += 1) result += a[i] * b[i];
    return result;
}
unsigned calls(const unsigned *a, const unsigned *b, unsigned n, unsigned salt)
{
    unsigned result = 0;
    (void)b;
    for (unsigned i = 0; i < n; i += 1) result += helper(a[i] + salt);
    return result;
}
