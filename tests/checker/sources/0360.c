typedef struct { int a, b; } S;
typedef long v4di __attribute__((vector_size(32)));
int f(S* p, S* q, long* l, long* m, v4di d, long* (*get)(void))
{
    S x[] = { *(p + (p - (S*)q)), p[0], *((S*)q + (l - m)), *(p + (l - (*get)())), *((S*)l + 1), *(p - 1), *(p - sizeof(S)), *(p - !m), *(p - 'a') };
    _Static_assert(sizeof x == 9 * sizeof(S), "nine");
    v4di e[] = { (v4di)d * (l - m), (l - (long*)q) * d, d };
    _Static_assert(sizeof e == 3 * sizeof(v4di), "three");
    return x[8].a + (int)e[2][0];
}
