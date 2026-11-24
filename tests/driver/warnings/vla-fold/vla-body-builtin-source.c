typedef int v4si __attribute__((vector_size(16)));
v4si f(v4si x)
{
    x = __builtin_ia32_pshufd(x, 300);
    x = __builtin_ia32_pshufd(x, (int)sizeof(struct { int m[(1, 2)]; }) + 300);
    return x;
}
