typedef int e4 __attribute__((ext_vector_type(4)));
typedef int e2 __attribute__((ext_vector_type(2)));
typedef int g4 __attribute__((vector_size(16)));
typedef int e3 __attribute__((ext_vector_type(3)));
typedef int g2 __attribute__((vector_size(8)));
int f(int n, e4 v, e4 w, e2 t, g4 x, e3 u, g2 z)
{
    _Static_assert(__builtin_types_compatible_p(__typeof__(!v), e4), "not vector");
    _Static_assert(__builtin_types_compatible_p(__typeof__(v && w), e4), "and vector");
    _Static_assert(__builtin_types_compatible_p(__typeof__(v || w), e4), "or vector");
    g4 a[] = { !v, v && w, v || w, x };
    _Static_assert(sizeof a == 4 * sizeof(g4), "four");
    g4 b[] = { v.x, 2, 3, 4, x, v.s3, t.lo, !v.x, n && v.y };
    _Static_assert(sizeof b == 3 * sizeof(g4), "lanes braced into vectors");
    g4 c[] = { v.xyzw, v.wzyx, x, !n };
    _Static_assert(sizeof c == 4 * sizeof(g4), "two swizzled vectors, a vector, then a scalar");
    g2 d[] = { u.lo, u.hi, u.even, u.odd, z };
    _Static_assert(sizeof d == 5 * sizeof(g2), "odd lanes' halves round up");
    g4 e[] = { v.xy.lo, 2, 3, 4, x, v.xy.hi, v.zw.lo, v.xyz.lo.x, 0 };
    _Static_assert(sizeof e == 3 * sizeof(g4), "nested swizzles' lanes braced into vectors");
    g2 h[] = { v.xyzw.lo, z, v.wzyx.hi };
    _Static_assert(sizeof h == 3 * sizeof(g2), "nested swizzles' vectors whole");
    return a[3][0] + b[2][1] + c[3][0] + d[4][0] + e[2][0] + h[2][0];
}
