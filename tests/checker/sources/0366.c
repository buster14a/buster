typedef short v8hi __attribute__((vector_size(16)));
typedef char v16qi __attribute__((vector_size(16)));
typedef int v4si __attribute__((vector_size(16)));
int f(v8hi h, int n, v16qi c, v4si x)
{
    v8hi a[] = { __builtin_ia32_psllwi128(h, n), h, __builtin_ia32_psrawi128(h, n) };
    _Static_assert(sizeof a == 3 * sizeof(v8hi), "three vectors");
    v4si b[] = { __builtin_ia32_pmovmskb128(c), 2, 3, 4, x };
    _Static_assert(sizeof b == 2 * sizeof(v4si), "four scalars braced into a vector, then a vector");
    return a[2][0] + b[1][0];
}
