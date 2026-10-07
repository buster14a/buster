// Complex constants converted to a real type, folded at compile time and
// computed at run time. GCC and Clang fold both groups of forms below:
//
//   1. an imaginary constant under a cast in an integer constant expression
//      (an enumerator, a _Static_assert, a case label); and
//   2. a complex constant converted to a real type in a static initializer.
//
// C 6.3.1.2 converts a complex value to _Bool by testing both halves, and
// C 6.3.1.7p2 converts it to any other real type through the real half. Every
// static value is checked against the same conversion done at run time from
// volatile operands, so the folder and the lowering cannot drift apart.
//
// `static _Bool b = 2.0i;` is deliberately absent: GCC rejects the implicit
// conversion while Clang accepts it. The explicit cast is accepted by both.
//
// main returns zero, or the number of the first failing check.

enum
{
    BOOL_TRUE = (_Bool)2.0i,
    BOOL_FALSE = (_Bool)0.0i,
    BOOL_FLOAT_TRUE = (_Bool)0.5fi,
    INT_ZERO = (int)3.5i,
    LONG_ZERO = (long)7.0i,
};

_Static_assert((_Bool)2.0i, "an imaginary constant is true");
_Static_assert(!(_Bool)0.0i, "a zero imaginary constant is false");
_Static_assert((int)5.0i == 0, "the real half of an imaginary constant is zero");
_Static_assert(BOOL_TRUE == 1 && BOOL_FALSE == 0 && BOOL_FLOAT_TRUE == 1 && INT_ZERO == 0 && LONG_ZERO == 0, "");

static double static_double = 4.0 + 1.0i;
static double static_negative_double = -2.5 + 3.0i;
static int static_int = 5.0 + 7.0i;
static int static_negative_int = -5.5 + 7.0i;
static float static_float = 1.5f + 2.0fi;
static _Bool static_bool_imaginary = (_Bool)(0.0 + 2.0i);
static _Bool static_bool_real = (_Bool)(2.0 + 0.0i);
static _Bool static_bool_both = (_Bool)(1.0 + 1.0i);
static _Bool static_bool_zero = (_Bool)(0.0 + 0.0i);
static _Bool static_bool_negative_zero = (_Bool)(-0.0 + 0.0i);
static _Bool static_bool_cast_imaginary = (_Bool)2.0i;
static _Bool static_bool_cast_zero = (_Bool)0.0i;
static long static_long = (long)(9.0 + 4.0i);

static int check_case(int value)
{
    int result = 0;
    switch (value)
    {
        case (_Bool)2.0i:
            result = 1;
            break;
        case (int)5.0i + 7:
            result = 7;
            break;
        default:
            result = 99;
            break;
    }
    return result;
}

int main(void)
{
    volatile double real = 4.0;
    volatile double imaginary = 1.0;
    volatile double negative_real = -2.5;
    volatile double three = 3.0;
    volatile double five = 5.0;
    volatile double seven = 7.0;
    volatile double negative_five_half = -5.5;
    volatile float float_real = 1.5f;
    volatile float float_imaginary = 2.0f;
    volatile double zero = 0.0;
    volatile double negative_zero = -0.0;
    volatile double two = 2.0;
    volatile double one = 1.0;
    volatile double nine = 9.0;
    volatile double four = 4.0;

    double _Complex z_double = real + imaginary * 1.0i;
    double _Complex z_negative_double = negative_real + three * 1.0i;
    double _Complex z_int = five + seven * 1.0i;
    double _Complex z_negative_int = negative_five_half + seven * 1.0i;
    float _Complex z_float = float_real + float_imaginary * 1.0fi;
    double _Complex z_imaginary = zero + two * 1.0i;
    double _Complex z_real = two + zero * 1.0i;
    double _Complex z_both = one + one * 1.0i;
    double _Complex z_zero = zero + zero * 1.0i;
    double _Complex z_negative_zero = negative_zero + zero * 1.0i;
    double _Complex z_long = nine + four * 1.0i;

    double runtime_double = z_double;
    double runtime_negative_double = z_negative_double;
    int runtime_int = z_int;
    int runtime_negative_int = z_negative_int;
    float runtime_float = z_float;
    long runtime_long = z_long;

    if (static_double != 4.0 || static_double != runtime_double) return 1;
    if (static_negative_double != -2.5 || static_negative_double != runtime_negative_double) return 2;
    if (static_int != 5 || static_int != runtime_int) return 3;
    if (static_negative_int != -5 || static_negative_int != runtime_negative_int) return 4;
    if (static_float != 1.5f || static_float != runtime_float) return 5;
    if (static_long != 9 || static_long != runtime_long) return 6;

    if (static_bool_imaginary != 1 || static_bool_imaginary != (_Bool)z_imaginary) return 10;
    if (static_bool_real != 1 || static_bool_real != (_Bool)z_real) return 11;
    if (static_bool_both != 1 || static_bool_both != (_Bool)z_both) return 12;
    if (static_bool_zero != 0 || static_bool_zero != (_Bool)z_zero) return 13;
    if (static_bool_negative_zero != 0 || static_bool_negative_zero != (_Bool)z_negative_zero) return 14;
    if (static_bool_cast_imaginary != 1 || static_bool_cast_imaginary != (_Bool)(zero + two * 1.0i)) return 15;
    if (static_bool_cast_zero != 0 || static_bool_cast_zero != (_Bool)(zero * 1.0i)) return 16;

    // The enumerators and the case labels agree with the run-time conversion.
    if (BOOL_TRUE != (_Bool)(zero + two * 1.0i)) return 20;
    if (BOOL_FALSE != (_Bool)(zero * 1.0i)) return 21;
    if (BOOL_FLOAT_TRUE != (_Bool)(zero + 0.5f * 1.0fi)) return 22;
    if (INT_ZERO != (int)(zero + 3.5 * 1.0i)) return 23;
    if (LONG_ZERO != (long)(zero + seven * 1.0i)) return 24;
    if (check_case(1) != 1 || check_case(7) != 7 || check_case(0) != 99) return 25;
    return 0;
}
