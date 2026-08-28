// The caller half of the x87 aggregate ABI pair.  System V's merger algorithm
// gives INTEGER precedence over x87, which makes musl's `union ldshape` -- an
// 80-bit `long double` overlaid with `struct { uint64_t m; uint16_t se; }` --
// two INTEGER eightbytes in general-purpose registers, not the memory value a
// literal "x87 means MEMORY" reading would produce.  `union ldmixed` leaves
// X87_UP alone in the second eightbyte with no X87 ahead of it and the
// post-merge cleanup sends it to memory whole; `struct ldpair` is past two
// eightbytes and goes to memory on size.  Clang compiles the three to
// { i64, i64 }, byval/sret, and byval/sret respectively.
#if defined(__x86_64__) && !defined(_WIN32)

typedef unsigned long u64;
typedef unsigned short u16;

union ldshape
{
    long double f;
    struct
    {
        u64 m;
        u16 se;
    } i;
};

union ldmixed
{
    long double f;
    u64 m;
};

struct ldpair
{
    long double v[2];
};

extern union ldshape ld_shape_of(long double value);
extern u64 ld_shape_mantissa(int before, union ldshape value, int after);
extern u16 ld_shape_sign_exponent(int before, union ldshape value, int after);
extern union ldshape ld_shape_bump(int before, union ldshape value, int after);
extern union ldmixed ld_mixed_bump(int before, union ldmixed value, int after);
extern struct ldpair ld_pair_swap(int before, struct ldpair value, int after);
extern long double ld_scalar_sum(int before, long double left, long double right, int after);

int main(void)
{
    // Exact to a 64-bit significand and not representable in a double, so a
    // half that quietly moved eight bytes instead of sixteen fails here.
    long double pi = 3.14159265358979323846264338327950288L;
    union ldshape shape = ld_shape_of(pi);
    if (shape.i.m != 0xc90fdaa22168c235UL) return 1;
    if (shape.i.se != 0x4000) return 2;
    if (ld_shape_mantissa(11, shape, 22) != 0xc90fdaa22168c235UL) return 3;
    if (ld_shape_sign_exponent(11, shape, 22) != 0x4000) return 4;

    union ldshape bumped = ld_shape_bump(11, shape, 22);
    if (bumped.i.m != 0xc90fdaa22168c235UL + 33UL) return 5;
    if (bumped.i.se != 0x4000) return 6;

    // A subnormal keeps a clear integer bit and a zero exponent field, which
    // a round trip through an f64 would flush away.
    union ldshape tiny = ld_shape_of(0x1p-16400L);
    if (tiny.i.se != 0) return 7;
    if (tiny.i.m != 0x0000200000000000UL) return 8;

    union ldshape minus_zero = ld_shape_of(-0.0L);
    if (minus_zero.i.se != 0x8000) return 9;
    if (minus_zero.i.m != 0) return 10;

    union ldmixed mixed;
    mixed.f = -1.5L;
    u64 mixed_bits = mixed.m;
    union ldmixed mixed_back = ld_mixed_bump(11, mixed, 22);
    if (mixed_back.m != mixed_bits + 33UL) return 11;

    struct ldpair pair;
    pair.v[0] = 1.25L;
    pair.v[1] = -2.5L;
    struct ldpair swapped = ld_pair_swap(11, pair, 22);
    if (swapped.v[0] != -2.5L || swapped.v[1] != 1.25L) return 12;

    if (ld_scalar_sum(11, pi, -pi, 22) != 0.0L) return 13;
    union ldshape scalar = ld_shape_of(ld_scalar_sum(11, 1.0L, 0x1p-63L, 22));
    if (scalar.i.m != 0x8000000000000001UL) return 14;
    if (scalar.i.se != 0x3fff) return 15;

    return 0;
}

#else

int main(void)
{
    return 0;
}

#endif
