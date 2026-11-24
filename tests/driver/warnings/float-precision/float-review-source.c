_Atomic float af;
_Atomic double ad;
__fp16 gh;
int atomic_compare(void)
{
    _Atomic float *p = &af;
    return (af == 0.1) + ((double)af == 0.1) + (0.1 != af) + (af != 0.1f) + (*p == 0.1);
}
enum { ATOMIC_SIZE = sizeof(af == 0.1) };
float compound(float f, _Float16 g, __bf16 b)
{
    gh += 1;
    f += ad;
    gh *= g;
    gh /= gh;
    g += gh;
    b *= gh;
    return f + g + b;
}
float commas(double d, int i, float f)
{
    f = (i, (i, d = 0.5));
    f = i ? (0, d = 0.5) : 0.5;
    f = (i, 0.5);
    f = (0, (i, 0.25));
    f = -(d = 0.5);
    f = (i, -(d = 0.5));
    f = (double)(d = 0.5);
    f = (d = 0.5) * 1.0;
    f = -(i, 0.5);
    f = (d = 1, 0.5);
    return f;
}
typedef double D;
_Atomic D atomic_d;
float compound_sugar(float f)
{
    f += atomic_d;
    return f;
}
