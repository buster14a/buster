// Static x87 `long double` initialization: a folded constant expression and an
// aggregate of them.  musl's `src/math` spells both, as
// `static const long double toint = 1/LDBL_EPSILON;` in floorl.c and its
// siblings, and as the `atanhi[]`/`P[]` coefficient tables in atanl.c, expl.c
// and the rest.
//
// Every expected byte below is the object representation Clang emits for the
// same declaration in the same file, read out of its `.rodata`.  A decimal
// rendering would not separate a correctly rounded 64-bit significand from one
// that is an ulp off, which is the whole failure mode this fixture exists to
// catch, so the check compares the stored bytes.
//
// Reading the objects through `unsigned char` also keeps the fixture inside the
// static initializer's own territory: what is under test is the ten bytes the
// compiler wrote and the six padding bytes it left zero, not the x87 arithmetic
// that basic_c_long_double_arithmetic.c covers.  `main` names each object
// directly, including the arrays and the struct: a function one of whose values
// had a type that merely *contained* an x87 `long double` was once refused by
// the canonical emitter, so reaching the objects here rather than through a
// file-scope pointer is also what keeps that refusal from coming back.

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

// 1.0842021724855044340e-19L is musl's own spelling of LDBL_EPSILON, which is
// exactly 2^-63; the quotient is therefore exactly 2^63, whose ulp is 1, so a
// fold that went through a double would land somewhere else entirely.
static const long double toint = 1/1.0842021724855044340e-19L;
static const long double toint_half = 1.5/1.0842021724855044340e-19L;
static const long double third = 1.0L/3.0L;
static const long double chained = 3.0L*1.0L/7.0L;
static const long double blended = 1.0L + 1.0L/3.0L;
static const long double reduced = 1.0L - 1.0L/3.0L;
static const long double grouped = (2.0L*(3.0L + 4.0L))/5.0L;
static const long double negated = -(1.0L/3.0L);
// A leaf converts in its own type before it widens: 0.1f is the float nearest
// 0.1 and 0.1 is the double nearest it, and the two differ from each other and
// from the long double nearest 0.1.
static const long double from_float = 0.1f*1.0L;
static const long double from_double = 0.1*1.0L;
static const long double from_integer = 7*1.0L;
// Unary minus on an unsigned literal wraps in that literal's own type first.
static const long double from_unsigned = -1U*1.0L;
static const long double hex_scaled = 0x1.23456789abcdefp+5L/0x1p-3L;
static const long double cancels = 1.0L - 1.0L;
static const long double negative_zero = -0.0L;

// The table musl's atanl.c opens with, an array whose elements are folded
// rather than spelled, a designated one, and a struct that mixes x87 members
// with an integer.
static const long double atanhi[] = {
    4.63647609000806116202e-01L,
    7.85398163397448309628e-01L,
    9.82793723247329067985e-01L,
    1.57079632679489661926e+00L,
};
static const long double coefficients[3] = {-1.0L/3.0L, 1.0L/5.0L, -1.0L/7.0L};
static const long double sparse[4] = {[1] = 1.0L/9.0L, [3] = -2.0L*4.0L};
static const struct Calibration
{
    int tag;
    long double scale;
    long double bias;
} calibration = {7, 1.0L/3.0L, -2.5L};

typedef struct Subject Subject;
struct Subject
{
    const unsigned char* bytes;
    const unsigned char* expected;
    unsigned count;
};

static const unsigned char expected_toint[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3e, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_toint_half[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x3e, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_third[16] = {0xab, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xfd, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_chained[16] = {0x6e, 0xdb, 0xb6, 0x6d, 0xdb, 0xb6, 0x6d, 0xdb, 0xfd, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_blended[16] = {0xab, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_reduced[16] = {0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xfe, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_grouped[16] = {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0xb3, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_negated[16] = {0xab, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xfd, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_from_float[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xcd, 0xcc, 0xcc, 0xfb, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_from_double[16] = {0x00, 0xd0, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xfb, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_from_integer[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x01, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_from_unsigned[16] = {0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x1e, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_hex_scaled[16] = {0x80, 0xf7, 0xe6, 0xd5, 0xc4, 0xb3, 0xa2, 0x91, 0x07, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_cancels[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_negative_zero[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char expected_atanhi[64] = {
    0x45, 0x7b, 0xda, 0x0d, 0x2b, 0x38, 0x63, 0xed,
    0xfd, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x35, 0xc2, 0x68, 0x21, 0xa2, 0xda, 0x0f, 0xc9,
    0xfe, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xd9, 0xb4, 0x0f, 0x94, 0x5e, 0x98, 0xfb,
    0xfe, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x35, 0xc2, 0x68, 0x21, 0xa2, 0xda, 0x0f, 0xc9,
    0xff, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static const unsigned char expected_coefficients[48] = {
    0xab, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
    0xfd, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xcd, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
    0xfc, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x49, 0x92, 0x24, 0x49, 0x92, 0x24, 0x49, 0x92,
    0xfc, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static const unsigned char expected_sparse[64] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x8e, 0xe3, 0x38, 0x8e, 0xe3, 0x38, 0x8e, 0xe3,
    0xfb, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
    0x02, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static const unsigned char expected_calibration[48] = {
    0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xab, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
    0xfd, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa0,
    0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// A function-local static folds the same way a file-scope object does, and its
// address is not a constant expression, so it is checked separately from the
// table `main` builds.  The value is the one `third` holds.
static const unsigned char* local_static_bytes(void)
{
    static const long double local = 1.0L/3.0L;
    return (const unsigned char*)&local;
}

int main(void)
{
    // The table is built here, naming each object, rather than at file scope
    // holding pointers to them: a function that names an x87 aggregate was
    // once refused outright, and this is what would catch that again.
    const Subject subjects[] = {
        {(const unsigned char*)&toint, expected_toint, 16},
        {(const unsigned char*)&toint_half, expected_toint_half, 16},
        {(const unsigned char*)&third, expected_third, 16},
        {(const unsigned char*)&chained, expected_chained, 16},
        {(const unsigned char*)&blended, expected_blended, 16},
        {(const unsigned char*)&reduced, expected_reduced, 16},
        {(const unsigned char*)&grouped, expected_grouped, 16},
        {(const unsigned char*)&negated, expected_negated, 16},
        {(const unsigned char*)&from_float, expected_from_float, 16},
        {(const unsigned char*)&from_double, expected_from_double, 16},
        {(const unsigned char*)&from_integer, expected_from_integer, 16},
        {(const unsigned char*)&from_unsigned, expected_from_unsigned, 16},
        {(const unsigned char*)&hex_scaled, expected_hex_scaled, 16},
        {(const unsigned char*)&cancels, expected_cancels, 16},
        {(const unsigned char*)&negative_zero, expected_negative_zero, 16},
        {(const unsigned char*)atanhi, expected_atanhi, 64},
        {(const unsigned char*)coefficients, expected_coefficients, 48},
        {(const unsigned char*)sparse, expected_sparse, 64},
        {(const unsigned char*)&calibration, expected_calibration, 48},
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
                return (int)(subject_index * 100 + byte_index + 1);
            }
        }
    }
    {
        const unsigned char* local = local_static_bytes();
        unsigned byte_index = 0;
        for (; byte_index < 16; byte_index++)
        {
            if (local[byte_index] != expected_third[byte_index]) return 9900 + (int)byte_index;
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
