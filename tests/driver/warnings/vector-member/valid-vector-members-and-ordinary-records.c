typedef float v4 __attribute__((ext_vector_type(4)));
struct S { int q, xx; };
float f(v4 v, v4 *p, struct S *s)
{
    v.xy = p->zw;
    p->s01 += v.yx;
    v.x++;
    s->xx = s->q;
    int *q = &s->q;
    return v.x + p->y + v.lo[0] + v.hi[0] + v.even[0] + v.odd[0] + *q;
}
