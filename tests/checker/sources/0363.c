typedef int e4 __attribute__((ext_vector_type(4)));
typedef int g4 __attribute__((vector_size(16)));
typedef struct { int a, b; } S;
int f(int n, e4 mask, g4 x, S* p, S* q)
{
    _Static_assert(__builtin_types_compatible_p(__typeof__(mask ? 1 : 2), e4), "vector-valued conditional");
    g4 a[] = { mask ? 1 : 2, x };
    _Static_assert(sizeof a == 2 * sizeof(g4), "two elements");
    g4 d[] = { (mask < 2) ? 3 : 4, x, n ? 5 : 6, 7 };
    _Static_assert(sizeof d == 3 * sizeof(g4), "a compare's mask, a vector, then two scalars braced into one");
    S c[] = { *(n ? p : q), *(mask[0] ? p : q) };
    _Static_assert(sizeof c == 2 * sizeof(S), "two records");
    return a[1][0] + d[2][1] + c[1].a;
}
