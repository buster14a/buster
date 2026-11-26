typedef int v4 __attribute__((ext_vector_type(4)));
typedef int v2 __attribute__((ext_vector_type(2)));

struct Ordinary
{
    int a;
    int xy;
    struct Ordinary* next;
};

_Static_assert(sizeof(((const v4*)0)->xy) == sizeof(v2), "a const pointer's swizzle in a constant query");
_Static_assert(sizeof(((volatile v4*)0)->s3) == sizeof(int), "a volatile pointer's lane in a constant query");
_Static_assert(__builtin_types_compatible_p(__typeof__(((v4*)0)->wzyx), v4), "a reordered arrow swizzle in typeof");

int mixed_members(struct Ordinary* ordinary, v4* mutable, const v4* fixed, volatile v4* observed)
{
    struct Ordinary local = *ordinary;
    ordinary->a = ordinary->next->a + local.a;
    local.xy = ordinary->xy;
    mutable->xy = fixed->wz;
    mutable->hi = observed->even;
    mutable->s3 += local.xy;
    _Static_assert(__builtin_types_compatible_p(__typeof__((mutable + 1)->lo), v2), "an arrow from a pointer expression");
    _Static_assert(sizeof(mutable->xy.yx) == sizeof(v2), "a dot swizzle follows an arrow swizzle");
    return mutable->y + fixed->x + observed->w + local.a + ordinary->xy;
}
