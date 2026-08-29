// System V on x86-64 is the one target whose `long double` is the 80-bit x87
// format; Win64 and the AArch64 ABIs give it a narrower or a different
// representation, and none of the aggregate classifications below apply
// there.  The macros are the ones both Clang and Buster predefine, so the
// fixture compiles to the same program under either compiler.
#if defined(__x86_64__) && !defined(_WIN32)
#define FIXTURE_WIDE_LONG_DOUBLE 1
#else
#define FIXTURE_WIDE_LONG_DOUBLE 0
#endif

#if FIXTURE_WIDE_LONG_DOUBLE

typedef unsigned long u64;
typedef unsigned short u16;

// musl's `union ldshape` from src/internal/libm.h, and the reason this file
// exists: essentially every `long double` routine in musl's src/math reads a
// value's sign, exponent and mantissa through it.  System V's merger
// algorithm gives INTEGER precedence over x87, so both of its eightbytes
// classify INTEGER and the whole union rides two general-purpose registers --
// it is *not* a memory-class value, which a literal reading of the psABI's
// "x87 means MEMORY" line would make it.  Clang compiles it to `{ i64, i64 }`
// in both directions.
union ldshape
{
    long double f;
    struct
    {
        u64 m;
        u16 se;
    } i;
};

// The alternatives the merger cannot reconcile.  `mixed` leaves X87_UP alone
// in the second eightbyte with no X87 ahead of it, and `floating` merges x87
// with SSE; the post-merge cleanup sends both to memory whole, so they are
// byval arguments and sret results.  `pair` and `large` are past two
// eightbytes and go to memory on size alone.
union ldmixed
{
    long double f;
    u64 m;
};

union ldfloating
{
    long double f;
    double d;
};

struct ldpair
{
    long double v[2];
};

struct ldlarge
{
    long double f;
    int tail;
};

// Non-static so neither compiler can specialize the calls away: what is being
// tested is how the value crosses the boundary, not what the callee computes.
// The integer on each side of the aggregate makes a register or stack
// accounting slip show up as a wrong answer rather than a wrong-looking
// disassembly.
union ldshape shape_of(long double value)
{
    union ldshape result = {value};
    return result;
}

long double shape_value(int before, union ldshape value, int after)
{
    if (before != 11 || after != 22) return 0.0L;
    return value.f;
}

u64 shape_mantissa(union ldshape value) { return value.i.m; }
u16 shape_sign_exponent(union ldshape value) { return value.i.se; }

union ldshape shape_bump(int before, union ldshape value, int after)
{
    value.i.m += (u64)before + (u64)after;
    return value;
}

union ldmixed mixed_round_trip(int before, union ldmixed value, int after)
{
    if (before != 11 || after != 22) value.m = 0;
    return value;
}

union ldfloating floating_round_trip(int before, union ldfloating value, int after)
{
    if (before != 11 || after != 22) value.f = 0.0L;
    return value;
}

struct ldpair pair_round_trip(int before, struct ldpair value, int after)
{
    if (before != 11 || after != 22) value.v[0] = 0.0L;
    return value;
}

struct ldlarge large_round_trip(int before, struct ldlarge value, int after)
{
    if (before != 11 || after != 22) value.tail = 0;
    value.tail += 1;
    return value;
}

// A `long double` array as a local and as a global: both are objects of more
// than one wide float, which the value paths used to refuse outright.
static long double wide_globals[2];

static long double array_sum(long double a, long double b)
{
    long double locals[2];
    locals[0] = a;
    locals[1] = b;
    wide_globals[0] = a;
    wide_globals[1] = b;
    return locals[0] + locals[1] + wide_globals[0] - wide_globals[1];
}

// A file-scope `long double` table read at a runtime index, which is how
// atanl.c reaches `atanhi[id]`.  The object is addressable storage of four wide
// floats -- neither the scalar x87 shape nor anything an ABI decision ever sees
// -- and the element load still has to carry all ten bytes.
static const long double table[4] = {
    4.63647609000806116202e-01L,
    7.85398163397448309628e-01L,
    9.82793723247329067985e-01L,
    1.57079632679489661926e+00L,
};

static long double table_at(int index) { return table[index]; }

// C permits an object of incomplete type to be declared and addressed without
// ever being completed, which is how musl's src/include/stdio.h reaches
// `stderr`: it suppresses the definition of `struct _IO_FILE` and then
// declares `extern FILE __stderr_FILE`.
struct incomplete_object;
extern struct incomplete_object incomplete_definition;
static struct incomplete_object *incomplete_address(void) { return &incomplete_definition; }

// Provided here so the fixture links on its own; its contents are never read.
struct incomplete_object
{
    int value;
};
struct incomplete_object incomplete_definition = {7};

int main(void)
{
    // 3.14159265358979323846264338327950288L is exact to a 64-bit
    // significand and is not representable in a double, so a lowering that
    // quietly copied through an f64 fails the mantissa checks below.
    long double pi = 3.14159265358979323846264338327950288L;
    union ldshape shape = shape_of(pi);
    if (shape.i.m != 0xc90fdaa22168c235UL) return 1;
    if (shape.i.se != 0x4000) return 2;
    if (shape_mantissa(shape) != 0xc90fdaa22168c235UL) return 3;
    if (shape_sign_exponent(shape) != 0x4000) return 4;
    if (shape_value(11, shape, 22) != pi) return 5;

    union ldshape bumped = shape_bump(11, shape, 22);
    if (bumped.i.m != 0xc90fdaa22168c235UL + 33UL) return 6;
    if (bumped.i.se != 0x4000) return 7;

    // The sign bit and the biased exponent, read the way __signbitl and
    // __fpclassifyl read them.
    union ldshape negative = shape_of(-1.0L);
    if ((negative.i.se >> 15) != 1) return 8;
    if ((negative.i.se & 0x7fff) != 0x3fff) return 9;
    if (negative.i.m != 0x8000000000000000UL) return 10;

    // A subnormal: the explicit integer bit is clear and the exponent field
    // is zero, which is exactly what an f64 round trip would flush away.
    union ldshape tiny = shape_of(0x1p-16400L);
    if (tiny.i.se != 0) return 11;
    if (tiny.i.m != 0x0000200000000000UL) return 12;

    // Negative zero survives, and the union sees the sign an equality
    // comparison cannot.
    union ldshape minus_zero = shape_of(-0.0L);
    if (minus_zero.i.se != 0x8000) return 13;
    if (minus_zero.i.m != 0) return 14;

    union ldmixed mixed;
    mixed.f = -1.5L;
    union ldmixed mixed_back = mixed_round_trip(11, mixed, 22);
    if (mixed_back.f != -1.5L) return 15;
    if (mixed_back.m != mixed.m) return 16;

    union ldfloating floating;
    floating.f = 1.0L + 0x1p-63L;
    union ldfloating floating_back = floating_round_trip(11, floating, 22);
    if (floating_back.f != floating.f) return 17;
    if (floating_back.f == 1.0L) return 18;

    struct ldpair pair;
    pair.v[0] = 1.25L;
    pair.v[1] = -2.5L;
    struct ldpair pair_back = pair_round_trip(11, pair, 22);
    if (pair_back.v[0] != 1.25L || pair_back.v[1] != -2.5L) return 19;

    struct ldlarge large;
    large.f = 0x1p16000L;
    large.tail = 41;
    struct ldlarge large_back = large_round_trip(11, large, 22);
    if (large_back.f != 0x1p16000L || large_back.tail != 42) return 20;
    union ldshape large_shape = shape_of(large_back.f);
    if (large_shape.i.se != 0x7e7f) return 21;

    if (array_sum(1.25L, 0x1p-16400L) != 2.5L + 0x1p-16400L) return 22;
    if (wide_globals[0] != 1.25L) return 23;

    if (incomplete_address() != &incomplete_definition) return 24;

    // 1.5707963267948966 is the double nearest pi/2, so the subtraction below
    // is zero exactly when the element arrived through a 53-bit significand and
    // non-zero when all ten bytes did.  The index is volatile so the load stays
    // in the program rather than being folded into the comparison.
    volatile int table_index = 3;
    if (table_at(table_index) - 1.5707963267948966 == 0.0L) return 25;
    if (table_at(table_index) != 1.57079632679489661926e+00L) return 26;

    return 0;
}

#else

int main(void)
{
    return 0;
}

#endif
