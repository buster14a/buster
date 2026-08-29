// Reading an 80-bit x87 `long double` back out of a `va_list`.  System V
// classifies its X87/X87_UP pair into memory, so a variadic `long double` is
// never in the register save area whatever the argument counters say: the
// caller leaves it in a sixteen-aligned slot of the overflow area, and the
// read has to realign the cursor before taking one and advance past the whole
// sixteen afterwards.  musl's `pop_arg` in src/stdio/vfprintf.c is the shape
// this exists for, so every `%Lf` in that archive goes through it.
//
// Each value is checked as the significand and sign/exponent fields musl's
// own `union ldshape` reads it through, against constants a 53-bit
// significand cannot produce.  A lowering that quietly went through `double`,
// or one that took the value from the register save area, fails a comparison
// here rather than printing a plausible-looking decimal.

// System V on x86-64 is the one target whose `long double` is the 80-bit x87
// format; Win64 and the AArch64 ABIs give it a narrower or a different
// representation, and none of the classifications below apply there.  The
// macros are the ones both Clang and Buster predefine, so the fixture
// compiles to the same program under either compiler.
#if defined(__x86_64__) && !defined(_WIN32)
#define FIXTURE_WIDE_LONG_DOUBLE 1
#else
#define FIXTURE_WIDE_LONG_DOUBLE 0
#endif

#if FIXTURE_WIDE_LONG_DOUBLE

// Declared rather than included: the driver test compiles this without an
// -isysroot, and on macOS there are no system headers to find without the
// SDK.  The name is what the frontend recognizes -- it gives any object
// declared through one of the four `va_list` spellings the canonical
// variadic-list type -- while `__builtin_va_list` is what keeps the same
// declaration meaningful to another compiler, whose own list type is neither
// a pointer nor anything a fixture can spell.
typedef __builtin_va_list va_list;
typedef unsigned long u64;
typedef unsigned short u16;

// musl's `union ldshape` from src/internal/libm.h.  Its two eightbytes both
// classify INTEGER -- System V's merger gives INTEGER precedence over x87 --
// so it crosses a boundary in general-purpose registers and is read back out
// of the register save area, which is the other half of what this file
// covers.
union ldshape
{
    long double f;
    struct
    {
        u64 m;
        u16 se;
    } i;
};

#define WIDE_A 9223372036854775809.0L
#define WIDE_A_MANTISSA 0x8000000000000001UL
#define WIDE_A_SIGN_EXPONENT 0x403E

#define WIDE_B (-1.5L)
#define WIDE_B_MANTISSA 0xC000000000000000UL
#define WIDE_B_SIGN_EXPONENT 0xBFFF

#define WIDE_C 0.5L
#define WIDE_C_MANTISSA 0x8000000000000000UL
#define WIDE_C_SIGN_EXPONENT 0x3FFE

#define WIDE_D 1234.5L
#define WIDE_D_MANTISSA 0x9A50000000000000UL
#define WIDE_D_SIGN_EXPONENT 0x4009

static int wide_matches(long double value, u64 mantissa, u16 sign_exponent)
{
    union ldshape shape;
    shape.f = value;
    return shape.i.m == mantissa && shape.i.se == sign_exponent;
}

// The first variadic argument is the wide one, so the overflow cursor is
// already sixteen-aligned when it is read: this is the case that passes
// whether or not the realignment below is emitted.
static int first_argument_wide(int marker, ...)
{
    va_list arguments;
    long double value;
    int result;

    __builtin_va_start(arguments, marker);
    value = __builtin_va_arg(arguments, long double);
    __builtin_va_end(arguments);
    result = marker == 7 && wide_matches(value, WIDE_A_MANTISSA, WIDE_A_SIGN_EXPONENT);

    return result;
}

// One named parameter leaves five general-purpose registers, so the integers
// around the wide value come out of the register save area while the wide
// value itself comes out of the overflow area: the two cursors advance
// independently and a read that took the wide value from the save area would
// return one of the integers.
static int wide_between_register_integers(int marker, ...)
{
    va_list arguments;
    int before;
    long double value;
    int after;
    int result;

    __builtin_va_start(arguments, marker);
    before = __builtin_va_arg(arguments, int);
    value = __builtin_va_arg(arguments, long double);
    after = __builtin_va_arg(arguments, int);
    __builtin_va_end(arguments);
    result = marker == 7 && before == 11 && after == 22 && wide_matches(value, WIDE_B_MANTISSA, WIDE_B_SIGN_EXPONENT);

    return result;
}

// Six named integers fill the general-purpose registers, so every variadic
// argument here is in the overflow area.  The integer ahead of the wide value
// leaves the cursor on an eightbyte the wide value may not start at: the
// caller padded to sixteen and the read has to skip the same padding.  The
// integer after it is what proves the cursor then advanced by the whole
// sixteen-byte slot rather than by the ten bytes the value occupies.
static int wide_in_overflow_area(int first, int second, int third, int fourth, int fifth, int sixth, ...)
{
    va_list arguments;
    int before;
    long double value;
    int after;
    long double second_value;
    int result;

    __builtin_va_start(arguments, sixth);
    before = __builtin_va_arg(arguments, int);
    value = __builtin_va_arg(arguments, long double);
    after = __builtin_va_arg(arguments, int);
    second_value = __builtin_va_arg(arguments, long double);
    __builtin_va_end(arguments);
    result = first == 1 && second == 2 && third == 3 && fourth == 4 && fifth == 5 && sixth == 6 && before == 33 && after == 44 &&
             wide_matches(value, WIDE_C_MANTISSA, WIDE_C_SIGN_EXPONENT) &&
             wide_matches(second_value, WIDE_D_MANTISSA, WIDE_D_SIGN_EXPONENT);

    return result;
}

// The aggregate spelling of the same bytes.  `union ldshape` classifies into
// two INTEGER eightbytes, so this one really is read out of the register save
// area, and the value has to survive the round trip unchanged.
static int wide_aggregate_argument(int marker, ...)
{
    va_list arguments;
    union ldshape shape;
    int after;
    int result;

    __builtin_va_start(arguments, marker);
    shape = __builtin_va_arg(arguments, union ldshape);
    after = __builtin_va_arg(arguments, int);
    __builtin_va_end(arguments);
    result = marker == 7 && after == 55 && shape.i.m == WIDE_A_MANTISSA && shape.i.se == WIDE_A_SIGN_EXPONENT;

    return result;
}

// A copy taken before the wide read sees the same argument, which is what
// `vfprintf`'s two passes over its argument list depend on.
static int wide_through_copy(int marker, ...)
{
    va_list arguments;
    va_list copy;
    long double original;
    long double duplicate;
    int result;

    __builtin_va_start(arguments, marker);
    __builtin_va_copy(copy, arguments);
    original = __builtin_va_arg(arguments, long double);
    duplicate = __builtin_va_arg(copy, long double);
    __builtin_va_end(copy);
    __builtin_va_end(arguments);
    result = marker == 7 && wide_matches(original, WIDE_D_MANTISSA, WIDE_D_SIGN_EXPONENT) &&
             wide_matches(duplicate, WIDE_D_MANTISSA, WIDE_D_SIGN_EXPONENT);

    return result;
}

// The construct in musl's `pop_arg`: a switch over an argument-type enum whose
// wide arm pulls a `long double` into a union.  Nothing else in this file puts
// the read behind a branch, and the branch is what the frontend's prepared-call
// machinery has to carry the va_list expression through.
enum ArgumentKind
{
    ARGUMENT_INTEGER,
    ARGUMENT_DOUBLE,
    ARGUMENT_LONG_DOUBLE,
};

union Argument
{
    int i;
    double d;
    long double f;
};

static void pop_argument(union Argument* argument, int kind, va_list* arguments)
{
    switch (kind)
    {
    case ARGUMENT_INTEGER:
        argument->i = __builtin_va_arg(*arguments, int);
        break;
    case ARGUMENT_DOUBLE:
        argument->d = __builtin_va_arg(*arguments, double);
        break;
    case ARGUMENT_LONG_DOUBLE:
        argument->f = __builtin_va_arg(*arguments, long double);
        break;
    default:
        break;
    }
}

static int wide_through_indirect_list(int marker, ...)
{
    va_list arguments;
    union Argument popped;
    int integer_value;
    double double_value;
    long double wide_value;
    int result;

    __builtin_va_start(arguments, marker);
    pop_argument(&popped, ARGUMENT_INTEGER, &arguments);
    integer_value = popped.i;
    pop_argument(&popped, ARGUMENT_DOUBLE, &arguments);
    double_value = popped.d;
    pop_argument(&popped, ARGUMENT_LONG_DOUBLE, &arguments);
    wide_value = popped.f;
    __builtin_va_end(arguments);
    result = marker == 7 && integer_value == 66 && double_value == 2.5 && wide_matches(wide_value, WIDE_B_MANTISSA, WIDE_B_SIGN_EXPONENT);

    return result;
}

int main(void)
{
    union ldshape shape;
    int result;

    shape.f = WIDE_A;
    if (!first_argument_wide(7, WIDE_A))
    {
        result = 1;
    }
    else if (!wide_between_register_integers(7, 11, WIDE_B, 22))
    {
        result = 2;
    }
    else if (!wide_in_overflow_area(1, 2, 3, 4, 5, 6, 33, WIDE_C, 44, WIDE_D))
    {
        result = 3;
    }
    else if (!wide_aggregate_argument(7, shape, 55))
    {
        result = 4;
    }
    else if (!wide_through_copy(7, WIDE_D))
    {
        result = 5;
    }
    else if (!wide_through_indirect_list(7, 66, 2.5, WIDE_B))
    {
        result = 6;
    }
    else
    {
        result = 0;
    }

    return result;
}

#else

int main(void)
{
    return 0;
}

#endif
