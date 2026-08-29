// Static x87 `long double` initialization of the values C reaches through
// overflow, underflow and the invalid operations: the infinities, the quiet
// NaN, and the signed zeros.  musl spells `INFINITY` as `1e5000f` and `NAN` as
// `(0.0f/0.0f)` whenever the compiler does not advertise the GNU builtins, so
// libc-test's `long double` tables -- `T(RN, nan, nan, ...)` at the top of
// nearly every src/math unit -- are made of exactly these two shapes, and a
// folder that refused them refused those tables whole.  It spells them
// `__builtin_inff()` and `__builtin_nanf("")` when the compiler does advertise
// the builtins, which is now every dialect, so both spellings are pinned here
// against one set of expected bytes.
//
// basic_c_long_double_static_initializer.c covers the finite arithmetic this
// one deliberately leaves alone; what is pinned here is where the value stops
// being finite, plus the narrower operand ranks that `(0.0f/0.0f)` forced into
// the folder: a float division rounds in float, and an integer operand rounds
// into the common type before the operation rather than after it.
//
// Every expected byte below is the object representation Clang emits for the
// same declaration, read out of the same object at run time.  Reading the
// objects through `unsigned char` is what keeps the check inside the static
// initializer's territory: a NaN does not compare equal to itself and a signed
// zero compares equal to the other one, so no floating-point comparison could
// tell these apart at all.

// System V on x86-64 is the one target whose `long double` is the 80-bit x87
// format; Win64 and the AArch64 ABIs give it a narrower or a different
// representation.  The macros below are the ones both Clang and Buster
// predefine, so the fixture compiles to the same program under either.
#if defined(__x86_64__) && !defined(_WIN32)
#define FIXTURE_WIDE_LONG_DOUBLE 1
#else
#define FIXTURE_WIDE_LONG_DOUBLE 0
#endif

#if FIXTURE_WIDE_LONG_DOUBLE

// musl's own two spellings, which is why every one of them appears below
// rather than a bare `__builtin_inff()`.
#define FIXTURE_INFINITY 1e5000f
#define FIXTURE_NAN (0.0f/0.0f)

// And the *other* two spellings of the same two values.  musl picks these
// whenever the compiler advertises the GNU builtins, which `ide cc` now does
// in every dialect and not only a GNU one (tests/basic_c_type_generic_math.c
// carries why).  They are constant-valued intrinsics rather than arithmetic,
// so they reach the folder by a different door and have to arrive at the
// identical bytes -- 21 of libc-test's `long double` units failed to compile
// on the day the header started choosing them.
#define FIXTURE_BUILTIN_INFINITY __builtin_inff()
#define FIXTURE_BUILTIN_NAN __builtin_nanf("")

// A literal whose magnitude does not fit its own type is the infinity C
// gives it, in every one of the three floating types.
static const long double infinity = FIXTURE_INFINITY;
static const long double negative_infinity = -FIXTURE_INFINITY;
static const long double double_overflow = 1e5000;
static const long double long_double_overflow = 1e5000L;

// The quiet NaN a zero-over-zero division produces, and the sign a unary
// minus puts on it.  Clang's folder answers with the positive default
// quiet NaN, so the significand is the integer bit plus the leading
// fraction bit and nothing else.
static const long double quiet_nan = FIXTURE_NAN;
static const long double negative_nan = -FIXTURE_NAN;

// A NaN operand wins the operation outright and carries its own sign, the
// left one first.  A subtraction flips the sign of its right operand only
// after that test, which is what leaves `1.0L - -nan` negative.
static const long double nan_absorbs_addition = FIXTURE_NAN + 1.0L;
static const long double nan_keeps_left_sign = -FIXTURE_NAN * 1.0L;
static const long double nan_survives_right = 1.0L - -FIXTURE_NAN;

// The infinity algebra, including the four invalid combinations that
// produce the default NaN rather than an infinity of either sign.
static const long double infinity_plus_finite = FIXTURE_INFINITY + 1.0L;
static const long double infinity_minus_infinity = FIXTURE_INFINITY - FIXTURE_INFINITY;
static const long double infinity_times_zero = FIXTURE_INFINITY * 0.0L;
static const long double infinity_over_infinity = FIXTURE_INFINITY / 1e5000L;

// Division by zero and by an infinity, whose signs come from the operands
// even where the magnitude does not.
static const long double divided_by_zero = 1.0L / 0.0L;
static const long double divided_by_negative_zero = 1.0L / -0.0L;
static const long double divided_by_infinity = -1.0L / FIXTURE_INFINITY;

// The usual arithmetic conversions decide where an operation rounds.  The
// two quotients differ from each other and from the long double one, and
// an integer operand rounds into the common type first: 16777217 is the
// smallest integer a float cannot hold, so the product is 2^24.
static const long double float_quotient = 1.0f / 3.0f;
static const long double double_quotient = 1.0 / 3.0;
static const long double float_rounds_integer = 16777217 * 1.0f;
static const long double double_rounds_integer = 9007199254740993 * 1.0;

// Overflow and underflow inside a narrower operation, and a long double
// spelling too small for its own subnormal range.  Underflow keeps the
// sign the exact result had.
static const long double float_overflows = 1e30f * 1e30f;
static const long double float_underflows = 1e-30f * -1e-30f;
static const long double long_double_underflow = 1e-5000L;

// Adding a zero to a near-maximum value: the identity is taken directly,
// because aligning a zero's exponent -- the minimum subnormal's -- against
// this operand's spans more bits than the folder's bignum holds.
static const long double zero_plus_huge = -0.0L + 1e4930L;
// The builtin spellings, whose bytes are checked against the expectations the
// arithmetic spellings above already pin.  `__builtin_huge_val()` is the
// double-ranked infinity and widens to the same x87 value.
static const long double builtin_infinity = FIXTURE_BUILTIN_INFINITY;
static const long double builtin_negative_infinity = -FIXTURE_BUILTIN_INFINITY;
static const long double builtin_huge_val = __builtin_huge_val();
static const long double builtin_quiet_nan = FIXTURE_BUILTIN_NAN;
static const long double builtin_negative_nan = -FIXTURE_BUILTIN_NAN;
// An intrinsic as an operand rather than as the whole initializer, which is
// what libc-test's tables are made of.
static const long double builtin_infinity_minus_infinity = FIXTURE_BUILTIN_INFINITY - FIXTURE_BUILTIN_INFINITY;
static const long double builtin_nan_absorbs_addition = FIXTURE_BUILTIN_NAN + 1.0L;

// The two aggregate shapes libc-test writes: a table of `long double`
// elements, and the row struct its `T(...)` macro expands to, whose padding
// between an `int` and the two x87 members has to stay zero as well.
static const long double table[] = {
    FIXTURE_INFINITY, -FIXTURE_INFINITY, FIXTURE_NAN, -FIXTURE_NAN, 0.0L, -0.0L, 1.0L/3.0L,
};

typedef struct Row Row;
struct Row
{
    int r;
    long double x;
    long double y;
    float dy;
    int e;
};

static const Row row = {0, FIXTURE_NAN, FIXTURE_NAN, 0.0f, 0};
static const Row row_infinite = {0, FIXTURE_INFINITY, -FIXTURE_INFINITY, 0.0f, 0};
// The same two rows in the builtin spelling, which is the shape libc-test's
// `T(RN, nan, nan, ...)` rows now expand to.
static const Row builtin_row = {0, FIXTURE_BUILTIN_NAN, FIXTURE_BUILTIN_NAN, 0.0f, 0};
static const Row builtin_row_infinite = {0, FIXTURE_BUILTIN_INFINITY, -FIXTURE_BUILTIN_INFINITY, 0.0f, 0};

typedef struct Subject Subject;
struct Subject
{
    const unsigned char* bytes;
    const unsigned char* expected;
    unsigned count;
};

static const unsigned char expected_infinity[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_negative_infinity[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_double_overflow[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_long_double_overflow[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_quiet_nan[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_negative_nan[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_nan_absorbs_addition[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_nan_keeps_left_sign[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_nan_survives_right[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_infinity_plus_finite[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_infinity_minus_infinity[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_infinity_times_zero[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_infinity_over_infinity[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_divided_by_zero[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_divided_by_negative_zero[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_divided_by_infinity[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_float_quotient[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xab, 0xaa, 0xaa, 0xfd, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_double_quotient[16] = {0x00, 0xa8, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xfd, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_float_rounds_integer[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x17, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_double_rounds_integer[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x34, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_float_overflows[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_float_underflows[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_long_double_underflow[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_zero_plus_huge[16] = {0x1d, 0xfa, 0x6f, 0x45, 0xe7, 0x34, 0xb6, 0x89, 0xf8, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_table[112] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xab, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xfd, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_row[64] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_row_infinite[64] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// A function-local static folds the same way a file-scope object does, and its
// address is not a constant expression, so it is checked apart from the table
// `main` builds.  The value is the one `quiet_nan` holds.
static const unsigned char* local_static_bytes(void)
{
    static const long double local = FIXTURE_NAN;
    return (const unsigned char*)&local;
}

int main(void)
{
    // The table is built here, naming each object, rather than at file scope
    // holding pointers to them: a function that names an x87 aggregate was
    // once refused outright, and this is what would catch that again.
    const Subject subjects[] = {
        {(const unsigned char*)&infinity, expected_infinity, 16},
        {(const unsigned char*)&negative_infinity, expected_negative_infinity, 16},
        {(const unsigned char*)&double_overflow, expected_double_overflow, 16},
        {(const unsigned char*)&long_double_overflow, expected_long_double_overflow, 16},
        {(const unsigned char*)&quiet_nan, expected_quiet_nan, 16},
        {(const unsigned char*)&negative_nan, expected_negative_nan, 16},
        {(const unsigned char*)&nan_absorbs_addition, expected_nan_absorbs_addition, 16},
        {(const unsigned char*)&nan_keeps_left_sign, expected_nan_keeps_left_sign, 16},
        {(const unsigned char*)&nan_survives_right, expected_nan_survives_right, 16},
        {(const unsigned char*)&infinity_plus_finite, expected_infinity_plus_finite, 16},
        {(const unsigned char*)&infinity_minus_infinity, expected_infinity_minus_infinity, 16},
        {(const unsigned char*)&infinity_times_zero, expected_infinity_times_zero, 16},
        {(const unsigned char*)&infinity_over_infinity, expected_infinity_over_infinity, 16},
        {(const unsigned char*)&divided_by_zero, expected_divided_by_zero, 16},
        {(const unsigned char*)&divided_by_negative_zero, expected_divided_by_negative_zero, 16},
        {(const unsigned char*)&divided_by_infinity, expected_divided_by_infinity, 16},
        {(const unsigned char*)&float_quotient, expected_float_quotient, 16},
        {(const unsigned char*)&double_quotient, expected_double_quotient, 16},
        {(const unsigned char*)&float_rounds_integer, expected_float_rounds_integer, 16},
        {(const unsigned char*)&double_rounds_integer, expected_double_rounds_integer, 16},
        {(const unsigned char*)&float_overflows, expected_float_overflows, 16},
        {(const unsigned char*)&float_underflows, expected_float_underflows, 16},
        {(const unsigned char*)&long_double_underflow, expected_long_double_underflow, 16},
        {(const unsigned char*)&zero_plus_huge, expected_zero_plus_huge, 16},
        {(const unsigned char*)table, expected_table, (unsigned)sizeof(table)},
        {(const unsigned char*)&row, expected_row, (unsigned)sizeof(row)},
        {(const unsigned char*)&row_infinite, expected_row_infinite, (unsigned)sizeof(row_infinite)},
        {(const unsigned char*)&builtin_infinity, expected_infinity, 16},
        {(const unsigned char*)&builtin_negative_infinity, expected_negative_infinity, 16},
        {(const unsigned char*)&builtin_huge_val, expected_infinity, 16},
        {(const unsigned char*)&builtin_quiet_nan, expected_quiet_nan, 16},
        {(const unsigned char*)&builtin_negative_nan, expected_negative_nan, 16},
        {(const unsigned char*)&builtin_infinity_minus_infinity, expected_infinity_minus_infinity, 16},
        {(const unsigned char*)&builtin_nan_absorbs_addition, expected_nan_absorbs_addition, 16},
        {(const unsigned char*)&builtin_row, expected_row, (unsigned)sizeof(builtin_row)},
        {(const unsigned char*)&builtin_row_infinite, expected_row_infinite, (unsigned)sizeof(builtin_row_infinite)},
    };

    unsigned subject_index = 0;
    for (; subject_index < sizeof(subjects)/sizeof(subjects[0]); subject_index++)
    {
        const Subject* subject = &subjects[subject_index];
        unsigned byte_index = 0;
        for (; byte_index < subject->count; byte_index++)
        {
            if (subject->bytes[byte_index] != subject->expected[byte_index])
            {
                // The exit code names the object and the byte that disagreed.
                return (int)(subject_index * 200 + byte_index + 1);
            }
        }
    }
    {
        const unsigned char* local = local_static_bytes();
        unsigned byte_index = 0;
        for (; byte_index < 16; byte_index++)
        {
            if (local[byte_index] != expected_quiet_nan[byte_index]) return 9900 + (int)byte_index;
        }
    }
    return 0;
}

#else

int main(void)
{
    return 0;
}

#endif
