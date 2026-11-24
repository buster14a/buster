typedef float v4 __attribute__((ext_vector_type(4)));
typedef float v2 __attribute__((ext_vector_type(2)));
void f(v4 v, const v4 c)
{
    v.xx += v.zw;
    c.xx = v.xy;
    v.xx[0] = 1;
    (v + v).xx = v.xy;
    (v + v).xx[0] = 1;
    (void)&v[1];
    (void)&v.x;
    (void)&v.xx;
    (void)&(v + v).x;
    (void)&((v4){0}).x;
    v.xy = v.zw;
    v.x += 1;
}
