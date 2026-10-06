// C99 `_Complex` division of operands near the ends of the exponent range and
// of infinities, NaNs and zeroes, for float, double and long double.
//
// Each row is divided twice: once in a static initializer, which the compiler
// folds, and once at run time from volatile operands, which it lowers to
// code. The two paths are separate implementations of one algorithm (exact
// power-of-two scaling, Smith's algorithm, then C11 Annex G.5.2's recovery of
// a NaN-NaN quotient), so every row checks that both give the expected value
// and that they agree with each other bit for bit.
//
// The expected values are what libgcc's __divdc3 and compiler-rt return for
// the same operands, which a textbook (ac + bd) / (c*c + d*d) or an unscaled
// Smith's algorithm does not give: for (1e308 + 1e308i) / (1e308 + 1e308i)
// the first overflows its denominator and the second its intermediate sums,
// and both return NaN where the answer is 1 + 0i. The one row where the
// expectation is the mathematical answer rather than libgcc's run-time one is
// large_over_tiny, whose imaginary half libgcc loses to NaN while its own
// constant folder, like this compiler, returns 0.
//
// The program has no dependencies; main returns zero, or the number of the
// first failing row (plus 100 for double and 200 for long double).

// How an expected half is judged by matches(): an exact value (a zero of
// either sign equals zero), either infinity, a NaN, or a value within a
// relative tolerance.
enum
{
    KIND_VALUE,
    KIND_POSITIVE_INFINITY,
    KIND_NEGATIVE_INFINITY,
    KIND_NAN,
    KIND_APPROXIMATE,
};

// A row: name, the four operands a + bi and c + di, the expected real half
// (kind, value) and the expected imaginary half (kind, value). `P` is the
// prefix naming the type's helpers and statics, `BIG` and `TINY` are the
// type's extremes, and `INF` is spelled as a constant expression so that the
// static initializers fold it too.
#define DIVISION_ROWS(X, P, T, BIG, TINY, INF) \
    X(P, T, large_over_large, BIG, BIG, BIG, BIG, KIND_VALUE, 1, KIND_VALUE, 0) \
    X(P, T, tiny_over_tiny, TINY, TINY, TINY, TINY, KIND_VALUE, 1, KIND_VALUE, 0) \
    X(P, T, large_over_large_skewed, BIG, TINY, BIG, TINY, KIND_VALUE, 1, KIND_VALUE, 0) \
    X(P, T, large_over_tiny, BIG, BIG, TINY, TINY, KIND_POSITIVE_INFINITY, 0, KIND_VALUE, 0) \
    X(P, T, tiny_over_large, TINY, TINY, BIG, BIG, KIND_VALUE, 0, KIND_VALUE, 0) \
    X(P, T, large_real_over_large, BIG, 0, BIG, BIG, KIND_VALUE, 0.5, KIND_VALUE, -0.5) \
    X(P, T, tiny_real_over_tiny, TINY, 0, TINY, TINY, KIND_VALUE, 0.5, KIND_VALUE, -0.5) \
    X(P, T, large_over_one, BIG, BIG, 1, 0, KIND_VALUE, BIG, KIND_VALUE, BIG) \
    X(P, T, ordinary, 1, 2, 3, 4, KIND_APPROXIMATE, 0.44, KIND_APPROXIMATE, 0.08) \
    X(P, T, zero_denominator, 1, 1, 0, 0, KIND_POSITIVE_INFINITY, 0, KIND_POSITIVE_INFINITY, 0) \
    X(P, T, zero_denominator_negative, -1, 1, 0, 0, KIND_NEGATIVE_INFINITY, 0, KIND_POSITIVE_INFINITY, 0) \
    X(P, T, zero_denominator_real, 1, 0, 0, 0, KIND_POSITIVE_INFINITY, 0, KIND_NAN, 0) \
    X(P, T, zero_over_zero, 0, 0, 0, 0, KIND_NAN, 0, KIND_NAN, 0) \
    X(P, T, infinite_numerator, INF, 1, 1, 1, KIND_POSITIVE_INFINITY, 0, KIND_NEGATIVE_INFINITY, 0) \
    X(P, T, infinite_real_denominator, 1, 1, INF, 0, KIND_VALUE, 0, KIND_VALUE, 0) \
    X(P, T, infinite_both_denominator, 1, 1, INF, INF, KIND_VALUE, 0, KIND_VALUE, 0) \
    X(P, T, negative_infinite_denominator, 1, 1, -INF, 1, KIND_VALUE, 0, KIND_VALUE, 0) \
    X(P, T, infinite_over_infinite, INF, INF, INF, INF, KIND_NAN, 0, KIND_NAN, 0) \
    X(P, T, infinite_real_over_infinite, INF, 0, INF, 0, KIND_NAN, 0, KIND_NAN, 0) \
    X(P, T, nan_numerator, (INF - INF), 1, 1, 1, KIND_NAN, 0, KIND_NAN, 0)

typedef unsigned char Byte;

#define DEFINE_STATIC(P, T, ROW, A, B, C, D, RK, RV, IK, IV) \
    static T _Complex P##_static_##ROW = __builtin_complex((T)(A), (T)(B)) / __builtin_complex((T)(C), (T)(D));

#define DEFINE_RUNTIME(P, T, ROW, A, B, C, D, RK, RV, IK, IV) \
    { \
        volatile T a = (T)(A); \
        volatile T b = (T)(B); \
        volatile T c = (T)(C); \
        volatile T d = (T)(D); \
        T _Complex quotient = __builtin_complex((T)a, (T)b) / __builtin_complex((T)c, (T)d); \
        T _Complex folded = P##_static_##ROW; \
        row += 1; \
        if (!P##_matches(__real__ quotient, RK, (T)(RV)) || !P##_matches(__imag__ quotient, IK, (T)(IV)) || \
            !P##_matches(__real__ folded, RK, (T)(RV)) || !P##_matches(__imag__ folded, IK, (T)(IV)) || \
            !P##_same(__real__ quotient, __real__ folded) || !P##_same(__imag__ quotient, __imag__ folded)) \
        { \
            failed = failed ? failed : row; \
        } \
    }

// Static and run-time halves agree when both are NaN or both have the same
// bytes, which tells -0 from +0; a long double's padding is not compared.
#define DEFINE_DIVISION(P, T, BIG, TINY, INF) \
    static int P##_matches(T got, int kind, T expected) \
    { \
        int ordered = got == got; \
        int finite = ordered && got - got == 0; \
        int matched = 0; \
        if (kind == KIND_VALUE) matched = got == expected; \
        else if (kind == KIND_POSITIVE_INFINITY) matched = ordered && !finite && got > 0; \
        else if (kind == KIND_NEGATIVE_INFINITY) matched = ordered && !finite && got < 0; \
        else if (kind == KIND_NAN) matched = !ordered; \
        else matched = finite && got - expected < expected * 1e-6 && expected - got < expected * 1e-6; \
        return matched; \
    } \
    static int P##_same(T first, T second) \
    { \
        unsigned long used = sizeof(T) == 16 ? 10 : sizeof(T); \
        int same = (first != first) && (second != second); \
        if (first == first && second == second) \
        { \
            T copies[2] = {first, second}; \
            const Byte* bytes = (const Byte*)copies; \
            same = 1; \
            for (unsigned long index = 0; index < used; index += 1) \
            { \
                same = same && bytes[index] == bytes[sizeof(T) + index]; \
            } \
        } \
        return same; \
    } \
    DIVISION_ROWS(DEFINE_STATIC, P, T, BIG, TINY, INF) \
    static int P##_run(void) \
    { \
        int failed = 0; \
        int row = 0; \
        DIVISION_ROWS(DEFINE_RUNTIME, P, T, BIG, TINY, INF) \
        return failed; \
    }

DEFINE_DIVISION(float_division, float, 3.0e38f, 1.5e-38f, (3.0e38f * 3.0e38f))
DEFINE_DIVISION(double_division, double, 1e308, 1e-308, (1e308 * 1e308))
DEFINE_DIVISION(long_double_division, long double, 1e4932L, 1e-4932L, (1e4932L * 1e4932L))

int main(void)
{
    int failed = float_division_run();
    int double_failed = failed ? 0 : double_division_run();
    int long_double_failed = failed || double_failed ? 0 : long_double_division_run();
    return failed ? failed : double_failed ? 100 + double_failed : long_double_failed ? 200 + long_double_failed : 0;
}
