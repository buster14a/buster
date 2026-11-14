typedef struct { int a, b; } S;
typedef short v16hi __attribute__((vector_size(32)));
typedef long v4di __attribute__((vector_size(32)));
int f(S* p, int n, v16hi h, v16hi k, long* l, long* m, v4di d)
{
    S a[] = { *(p + 1), *(1 + p), p[0], *(p - n), *(p + (l - m)), p[l == m] };
    _Static_assert(sizeof a == 6 * sizeof(S), "six");
    v16hi b[] = { h + (short)n, -h, ~k, h * k + (short)1, (short)n * h, -h + (short)n, h << 2 };
    _Static_assert(sizeof b == 7 * sizeof(v16hi), "seven");
    v16hi c[] = { (short)n + (short)1, 2 };
    _Static_assert(sizeof c == sizeof(v16hi), "one");
    v4di e[] = { (l - m) * d, d, (l == m) * d, (l < m) + d };
    _Static_assert(sizeof e == 4 * sizeof(v4di), "four vectors past pointers' differences and compares");
    return a[5].b + b[6][0] + c[0][1] + (int)e[3][0];
}
