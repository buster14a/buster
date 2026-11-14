typedef int v4si __attribute__((vector_size(16)));
typedef struct { v4si v; int k; } H;
int f(int n, v4si v, H h)
{
    v4si a[] = { n ? v[0] : 2, 3 };
    _Static_assert(sizeof a == sizeof(v4si), "one");
    v4si b[] = { v[0] + 1, 2 };
    _Static_assert(sizeof b == sizeof(v4si), "one too");
    v4si c[] = { h.v[1], (v + v)[2], n ? 1 : v[3], 4, v };
    _Static_assert(sizeof c == 2 * sizeof(v4si), "four lanes, then a vector");
    return a[0][1] + b[0][0] + c[1][3];
}
