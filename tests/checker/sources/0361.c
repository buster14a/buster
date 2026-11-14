typedef struct { int a, b; } S;
typedef int v4si __attribute__((vector_size(16)));
int f(int n, v4si v, v4si w, S* p, S* q, S s, S t)
{
    v4si a[] = { n ? v : 0, n ? 0 : v, n ? v : w, n ? (v + w) : 1, v };
    _Static_assert(sizeof a == 5 * sizeof(v4si), "five");
    v4si b[] = { n ? 1 : 2, 3 };
    _Static_assert(sizeof b == sizeof(v4si), "one");
    S x[] = { *(n ? p : 0), *(n ? 0 : q), n ? s : t, *(p ?: q), s };
    _Static_assert(sizeof x == 5 * sizeof(S), "five");
    return a[4][0] + b[0][1] + x[4].a;
}
