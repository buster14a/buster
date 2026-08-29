// The sign of a NaN an operation creates.  IEEE-754 leaves it unspecified, and
// the two available answers differ: x86 hardware produces the negative default
// NaN for an invalid operation, while Clang folds the same constant expression
// to the positive quiet NaN.  Buster follows Clang, because the tree is
// differenced against it everywhere.
//
// musl is what makes this observable rather than academic.  Its <math.h>
// defines NAN as `__builtin_nanf("")` only when __GNUC__ is defined, and
// buster does not predefine __GNUC__ under -std=c99, so musl takes the
// `#define NAN (0.0f/0.0f)` branch instead: an unfolded divide left every NAN
// in the library negative, which printed `-nan` where the reference prints
// `nan` and failed libc-test's regression/fpclassify-invalid-ld80.
//
// Only the operations that *create* a NaN fold here.  An operation handed a
// NaN propagates that operand, sign included, which is what the hardware does
// too, so the propagation cases below check that nothing canonicalizes a sign
// the source already chose.

// The spellings are written out rather than included so the fixture needs no
// SDK; INFINITY and NAN are exactly how a hosted <math.h> spells them for a
// compiler without __GNUC__.
#define FIXTURE_INFINITY (__builtin_inff())
#define FIXTURE_NAN (0.0f/0.0f)

// A `long double` operand makes the operation happen in that format.  It is
// the 80-bit x87 one on System V x86-64, plain `double` under the Apple and
// Windows ABIs, and the 128-bit quad on AArch64 Linux -- the one width whose
// arithmetic the frontend does not lower yet, so the widened operation is
// spelled only where a lowering exists.  The `long double` cast below needs no
// such guard: a conversion is not arithmetic.
#if !defined(__aarch64__) || defined(__APPLE__)
#define FIXTURE_LONG_DOUBLE_ARITHMETIC 1
#else
#define FIXTURE_LONG_DOUBLE_ARITHMETIC 0
#endif

static int is_nan_float(float value)
{
    return value != value;
}

static int is_nan_double(double value)
{
    return value != value;
}

// A volatile zero keeps its divide out of every fold: the runtime answer is
// the hardware's, which is the value this fixture exists to *not* match at
// compile time.
static volatile float runtime_zero = 0.0f;

int main(void)
{
    // The four invalid operations, at float.
    if (!is_nan_float(0.0f/0.0f) || __builtin_signbitf(0.0f/0.0f)) return 1;
    if (!is_nan_float(FIXTURE_INFINITY/FIXTURE_INFINITY) || __builtin_signbitf(FIXTURE_INFINITY/FIXTURE_INFINITY)) return 2;
    if (!is_nan_float(0.0f*FIXTURE_INFINITY) || __builtin_signbitf(0.0f*FIXTURE_INFINITY)) return 3;
    if (!is_nan_float(FIXTURE_INFINITY-FIXTURE_INFINITY) || __builtin_signbitf(FIXTURE_INFINITY-FIXTURE_INFINITY)) return 4;
    if (!is_nan_float(FIXTURE_INFINITY+(-FIXTURE_INFINITY)) || __builtin_signbitf(FIXTURE_INFINITY+(-FIXTURE_INFINITY))) return 5;

    // The created NaN's sign does not follow its operands': a negative zero
    // over a positive one still creates the positive quiet NaN.
    if (!is_nan_float(-0.0f/0.0f) || __builtin_signbitf(-0.0f/0.0f)) return 6;
    if (!is_nan_float(0.0f/-0.0f) || __builtin_signbitf(0.0f/-0.0f)) return 7;
    if (!is_nan_float(-0.0f/-0.0f) || __builtin_signbitf(-0.0f/-0.0f)) return 8;

    // The same at double, and through the widening a `long double` operand
    // forces on a float spelling.
    if (!is_nan_double(0.0/0.0) || __builtin_signbit(0.0/0.0)) return 9;
    if (!is_nan_double(0.0f/0.0) || __builtin_signbit(0.0f/0.0)) return 10;
    if (__builtin_signbitl((long double)(0.0f/0.0f))) return 11;
#if FIXTURE_LONG_DOUBLE_ARITHMETIC
    if (__builtin_signbitl(0.0f/0.0L)) return 12;
#endif

    // A NaN that is propagated rather than created keeps the sign it had.
    if (!is_nan_float(FIXTURE_NAN + 1.0f) || __builtin_signbitf(FIXTURE_NAN + 1.0f)) return 13;
    if (!is_nan_float((-FIXTURE_NAN) + 1.0f) || !__builtin_signbitf((-FIXTURE_NAN) + 1.0f)) return 14;
    if (!__builtin_signbitf(-FIXTURE_NAN)) return 15;

    // Nothing else about a constant divide moves: the defined infinities keep
    // their signs and ordinary arithmetic keeps its value.
    if (1.0f/0.0f <= 1.0e38f || __builtin_signbitf(1.0f/0.0f)) return 16;
    if (-1.0f/0.0f >= -1.0e38f || !__builtin_signbitf(-1.0f/0.0f)) return 17;
    if (1.0f/-0.0f >= -1.0e38f || !__builtin_signbitf(1.0f/-0.0f)) return 18;
    if (1.5f + 2.25f != 3.75f) return 19;
    if (!__builtin_signbitf(-0.0f*0.0f)) return 20;

    // The hardware still answers a runtime invalid operation its own way; the
    // fold is a compile-time choice and does not rewrite what the divide unit
    // does.  Only that it is a NaN is portable, so the sign is not checked.
    if (!is_nan_float(runtime_zero/runtime_zero)) return 21;

    // A static initializer folds through the constant evaluator rather than
    // the path above, and has to agree with it.
    {
        static const float created = 0.0f/0.0f;
        static const double created_wide = 0.0/0.0;
        if (!is_nan_float(created) || __builtin_signbitf(created)) return 22;
        if (!is_nan_double(created_wide) || __builtin_signbit(created_wide)) return 23;
    }
    return 0;
}
