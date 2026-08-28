#include <stdio.h>
#include <string.h>

// System V on x86-64 is the one target whose `long double` is the 80-bit x87
// format; Win64 and the AArch64 ABIs give it a narrower or a different
// representation that this backend still refuses to compute with.  The macros
// below are the ones both Clang and Buster predefine, so the fixture compiles
// to the same program under either compiler.
#if defined(__x86_64__) && !defined(_WIN32)
#define FIXTURE_WIDE_LONG_DOUBLE 1
#else
#define FIXTURE_WIDE_LONG_DOUBLE 0
#endif

#if FIXTURE_WIDE_LONG_DOUBLE

// The operands come through volatile storage so neither compiler can fold the
// arithmetic away: the point of the fixture is the emitted x87 sequences.
static volatile long double wide_left;
static volatile long double wide_right;
static volatile double narrow_value;
static volatile unsigned long long unsigned_value;
static volatile long long signed_value;

static long double wide_add(long double a, long double b) { return a + b; }
static long double wide_subtract(long double a, long double b) { return a - b; }
static long double wide_multiply(long double a, long double b) { return a * b; }
static long double wide_divide(long double a, long double b) { return a / b; }
static long double wide_negate(long double a) { return -a; }

// The shape LZ4's LZ4IO_toHuman has: an integer widened to long double, a
// comparison against an integer constant, a division, and the value passed
// through a variadic call.  System V passes it in a sixteen-byte, sixteen-
// aligned memory slot rather than a register, which is what makes the
// variadic case worth covering here.
static const char* to_human(long double size, char* buffer)
{
    const char units[] = {"\0KMGTPEZY"};
    unsigned index = 0;
    for (; size >= 1024; index++) size /= 1024;
    sprintf(buffer, "%.2Lf%c", size, units[index]);
    return buffer;
}

int main(void)
{
    char buffer[64];

    // A 64-bit significand is the whole reason the type exists: 2^63 + 1 is
    // exact here and is not in a double, so a lowering that quietly computed
    // in double would pass every other check in this file and fail these.
    wide_left = 9223372036854775808.0L;
    wide_right = 1.0L;
    long double sum = wide_add(wide_left, wide_right);
    if (sum == wide_left) return 1;
    if (!(sum > wide_left)) return 2;
    if (wide_subtract(sum, wide_left) != 1.0L) return 3;
    if ((unsigned long long)sum != 9223372036854775809ULL) return 4;

    // 3^40 needs 64 significand bits and comes back exactly.
    long double power = 1.0L;
    for (int step = 0; step < 40; step += 1)
    {
        wide_left = power;
        wide_right = 3.0L;
        power = wide_multiply(wide_left, wide_right);
    }
    if ((unsigned long long)power != 12157665459056928801ULL) return 5;
    for (int step = 0; step < 40; step += 1)
    {
        wide_left = power;
        wide_right = 3.0L;
        power = wide_divide(wide_left, wide_right);
    }
    if (power != 1.0L) return 6;

    // Negation, and the six comparisons in both directions.
    wide_left = 2.5L;
    wide_right = -2.5L;
    if (wide_negate(wide_left) != wide_right) return 7;
    if (wide_negate(wide_right) != wide_left) return 8;
    if (!(wide_right < wide_left)) return 9;
    if (!(wide_right <= wide_left)) return 10;
    if (wide_left < wide_right) return 11;
    if (!(wide_left > wide_right)) return 12;
    if (!(wide_left >= wide_right)) return 13;
    if (wide_right > wide_left) return 14;
    if (wide_left == wide_right) return 15;
    if (!(wide_left != wide_right)) return 16;
    wide_right = 2.5L;
    if (!(wide_left == wide_right)) return 17;
    if (wide_left != wide_right) return 18;
    if (!(wide_left <= wide_right)) return 19;
    if (!(wide_left >= wide_right)) return 20;

    // Truth conversion is a comparison against a zero of the same type.
    wide_left = 0.0L;
    if (wide_left) return 21;
    if (!(wide_left ? 0 : 1)) return 22;
    wide_left = -0.0L;
    if (wide_left) return 23;
    wide_left = 1e-40L;
    if (!wide_left) return 24;

    // Unsigned eightbyte conversions on both sides of 2^63, which is where
    // FILD's signed reading and FISTP's signed image each need a correction.
    unsigned_value = 18446744073709551615ULL;
    long double from_unsigned = (long double)unsigned_value;
    if (from_unsigned != 18446744073709551615.0L) return 25;
    if ((unsigned long long)from_unsigned != 18446744073709551615ULL) return 26;
    unsigned_value = 9223372036854775808ULL;
    from_unsigned = (long double)unsigned_value;
    if ((unsigned long long)from_unsigned != 9223372036854775808ULL) return 27;
    unsigned_value = 9223372036854775807ULL;
    from_unsigned = (long double)unsigned_value;
    if ((unsigned long long)from_unsigned != 9223372036854775807ULL) return 28;
    unsigned_value = 12345678901234567890ULL;
    from_unsigned = (long double)unsigned_value;
    if ((unsigned long long)from_unsigned != 12345678901234567890ULL) return 29;

    // Signed eightbyte, and the narrower integer widths in both directions.
    signed_value = -9223372036854775807LL - 1LL;
    long double from_signed = (long double)signed_value;
    if ((long long)from_signed != -9223372036854775807LL - 1LL) return 30;
    signed_value = -1000003LL;
    from_signed = (long double)signed_value;
    if ((long long)from_signed != -1000003LL) return 31;
    if ((int)from_signed != -1000003) return 32;
    if ((short)(long double)-12345 != -12345) return 33;
    if ((unsigned)(long double)4000000000u != 4000000000u) return 34;
    if ((unsigned char)(long double)200 != 200) return 35;
    if ((long double)(char)-7 != -7.0L) return 36;

    // Truncation is toward zero, and the control word is put back afterwards
    // so the surrounding double arithmetic still rounds to nearest.
    if ((long long)(long double)-2.75L != -2) return 37;
    if ((long long)(long double)2.75L != 2) return 38;
    narrow_value = 0.5;
    if ((double)((long double)narrow_value + (long double)narrow_value) != 1.0) return 39;

    // f64 and f32 conversions in both directions.
    narrow_value = 3.141592653589793;
    long double from_double = (long double)narrow_value;
    if ((double)from_double != 3.141592653589793) return 40;
    if (from_double <= 3.0L || from_double >= 3.2L) return 41;
    if ((float)(long double)0.5L != 0.5f) return 42;
    if ((long double)0.25f != 0.25L) return 43;
    // A double cannot hold the extra significand bits, so narrowing loses
    // them; that the wide value still differs from its narrowing is the
    // check that the value really was wide.
    wide_left = 9223372036854775809.0L;
    if ((long double)(double)wide_left == wide_left) return 44;

    // The LZ4 shape, end to end.
    if (strcmp(to_human((long double)0ULL, buffer), "0.00") != 0) return 45;
    if (strcmp(to_human((long double)1023ULL, buffer), "1023.00") != 0) return 46;
    if (strcmp(to_human((long double)1024ULL, buffer), "1.00K") != 0) return 47;
    if (strcmp(to_human((long double)12345ULL, buffer), "12.06K") != 0) return 48;
    unsigned_value = 18446744073709551615ULL;
    if (strcmp(to_human((long double)unsigned_value, buffer), "16.00E") != 0) return 49;
    sprintf(buffer, "%.20Lg", (long double)12157665459056928801ULL);
    if (strcmp(buffer, "12157665459056928801") != 0) return 50;

    return 0;
}

#else

int main(void)
{
    return 0;
}

#endif
