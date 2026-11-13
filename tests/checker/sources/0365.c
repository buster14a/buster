typedef float v4 __attribute__((ext_vector_type(4)));
typedef float v2 __attribute__((ext_vector_type(2)));
struct s { int x, a; };
_Static_assert(__builtin_types_compatible_p(__typeof__(((v4*)0)->xy), v2), "xy");
float f(v4* p, __seg_gs v4* g, struct s* q, float x)
{
    p->w = x;
    p->xy = p->zw;
    _Static_assert(__builtin_types_compatible_p(__typeof__(p->wzyx), v4), "wzyx");
    _Static_assert(__builtin_types_compatible_p(__typeof__(g->lo), v2), "lo");
    return p->y + g->s3 + q->x + q->a;
}
