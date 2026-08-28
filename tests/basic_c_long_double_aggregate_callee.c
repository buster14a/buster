// The callee half of the x87 aggregate ABI pair.  Compiled by one compiler
// and linked against a caller compiled by the other, so a disagreement about
// where these values travel is a wrong answer rather than a
// wrong-looking disassembly.  See basic_c_long_double_aggregate_caller.c for
// the shapes and what each classification is.
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

// INTEGER-pair union: two general-purpose registers in and two out.
union ldshape ld_shape_of(long double value)
{
    union ldshape result = {value};
    return result;
}

// The integers on either side move the aggregate off the first argument
// register, so a classification that consumed the wrong number of registers
// answers wrong.
u64 ld_shape_mantissa(int before, union ldshape value, int after)
{
    if (before != 11 || after != 22) return 0;
    return value.i.m;
}

u16 ld_shape_sign_exponent(int before, union ldshape value, int after)
{
    if (before != 11 || after != 22) return 0;
    return value.i.se;
}

union ldshape ld_shape_bump(int before, union ldshape value, int after)
{
    value.i.m += (u64)before + (u64)after;
    return value;
}

// Memory-class union: byval in, sret out.
union ldmixed ld_mixed_bump(int before, union ldmixed value, int after)
{
    value.m += (u64)before + (u64)after;
    return value;
}

// Past two eightbytes, so memory on size alone.
struct ldpair ld_pair_swap(int before, struct ldpair value, int after)
{
    if (before != 11 || after != 22) return value;
    struct ldpair result;
    result.v[0] = value.v[1];
    result.v[1] = value.v[0];
    return result;
}

// The x87 pair itself, for contrast: a scalar returns in ST0 and travels in a
// sixteen-byte overflow slot.
long double ld_scalar_sum(int before, long double left, long double right, int after)
{
    if (before != 11 || after != 22) return 0.0L;
    return left + right;
}

#endif
