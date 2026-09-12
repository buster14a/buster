// SysV x87 values retain all 64 significand bits through MIR frame values,
// arithmetic, control-flow joins, conversions and call boundaries.
typedef union F80Image
{
    long double value;
    struct { unsigned long long significand; unsigned short exponent; } bits;
} F80Image;

typedef struct F80Wrapper { long double value; } F80Wrapper;

#ifndef F80_MACHINE_HOST
long double f80_add(long double left, long double right) { return left + right; }
long double f80_subtract(long double left, long double right) { return left - right; }
long double f80_multiply(long double left, long double right) { return left * right; }
long double f80_divide(long double left, long double right) { return left / right; }
long double f80_negate(long double value) { return -value; }

int f80_compare(long double left, long double right)
{
    return (left == right) | ((left != right) << 1) | ((left < right) << 2) |
           ((left <= right) << 3) | ((left > right) << 4) | ((left >= right) << 5);
}

long double f80_from_double(double value) { return (long double)value; }
long double f80_from_float(float value) { return (long double)value; }
double f80_to_double(long double value) { return (double)value; }
float f80_to_float(long double value) { return (float)value; }
long double f80_from_signed(long long value) { return (long double)value; }
long long f80_to_signed(long double value) { return (long long)value; }
long double f80_from_unsigned(unsigned value) { return (long double)value; }
unsigned f80_to_unsigned(long double value) { return (unsigned)value; }

void f80_copy(volatile long double* destination, volatile long double const* source)
{
    *destination = *source;
}

long double f80_join(int choose, long double left, long double right, int count)
{
    long double result = choose ? left : right;
    for (int index = 0; index < count; index += 1)
    {
        result = result + 1.0L;
    }
    return result;
}

long double f80_many(int a, int b, int c, int d, int e, int f, int g,
                     long double first, int h, long double second)
{
    return first - second + (a + b + c + d + e + f + g + h);
}

F80Wrapper f80_wrapper(F80Wrapper value) { return value; }
_Complex long double f80_complex(_Complex long double value) { return value; }

#else
long double f80_add(long double left, long double right);
long double f80_subtract(long double left, long double right);
long double f80_multiply(long double left, long double right);
long double f80_divide(long double left, long double right);
long double f80_negate(long double value);
int f80_compare(long double left, long double right);
long double f80_from_double(double value);
long double f80_from_float(float value);
double f80_to_double(long double value);
float f80_to_float(long double value);
long double f80_from_signed(long long value);
long long f80_to_signed(long double value);
long double f80_from_unsigned(unsigned value);
unsigned f80_to_unsigned(long double value);
void f80_copy(volatile long double* destination, volatile long double const* source);
long double f80_join(int choose, long double left, long double right, int count);
long double f80_many(int a, int b, int c, int d, int e, int f, int g,
                     long double first, int h, long double second);
F80Wrapper f80_wrapper(F80Wrapper value);
_Complex long double f80_complex(_Complex long double value);
#endif

#if defined(F80_MACHINE_FENV)
// Compiled only in the independent Clang caller. The compiler under test
// supplies every operation being checked, including C's truncating cast.
static int f80_control_word(void)
{
    unsigned short saved;
    unsigned short observed;
    __asm__ volatile("fnstcw %0" : "=m"(saved));
    unsigned short upward = (unsigned short)((saved & ~0x0c00u) | 0x0800u);
    __asm__ volatile("fldcw %0" : : "m"(upward) : "memory");
    volatile long long converted = f80_to_signed(-3.75L);
    volatile double rounded = f80_to_double(1.0L + 0x1p-54L);
    __asm__ volatile("fnstcw %0" : "=m"(observed));
    __asm__ volatile("fldcw %0" : : "m"(saved) : "memory");
    return converted != -3 || rounded != 0x1.0000000000001p0 || observed != upward;
}
#endif

#ifndef F80_MACHINE_LIBRARY
int main(void)
{
    volatile long double left = 9223372036854775808.0L;
    volatile long double right = 1.0L;
    long double precise = f80_add(left, right);
    int failed = precise == left || f80_subtract(precise, left) != 1.0L;
    failed |= f80_multiply(3.25L, -4.0L) != -13.0L;
    failed |= f80_divide(-13.0L, 4.0L) != -3.25L;
    failed |= f80_negate(-3.25L) != 3.25L;
    failed |= f80_compare(1.0L, 2.0L) != 14;
    failed |= f80_compare(2.0L, 1.0L) != 50;
    failed |= f80_compare(1.0L, 1.0L) != 41;
    F80Image nan;
    nan.bits.significand = 0xc000000000000123ULL;
    nan.bits.exponent = 0x7fff;
    failed |= f80_compare(nan.value, 1.0L) != 2;
    failed |= f80_compare(1.0L, nan.value) != 2;
    F80Image zero;
    zero.value = f80_negate(0.0L);
    failed |= zero.bits.significand != 0 || zero.bits.exponent != 0x8000;
    failed |= f80_compare(zero.value, 0.0L) != 41;
    F80Image infinity;
    infinity.bits.significand = 0x8000000000000000ULL;
    infinity.bits.exponent = 0x7fff;
    failed |= f80_compare(infinity.value, precise) != 50;
    failed |= f80_from_double(-3.25) != -3.25L || f80_from_float(1.5f) != 1.5L;
    failed |= f80_to_double(3.25L) != 3.25 || f80_to_float(-1.5L) != -1.5f;
    failed |= f80_from_signed(-9223372036854775807LL) != -9223372036854775807.0L;
    failed |= f80_to_signed(-3.75L) != -3 || f80_to_signed(3.75L) != 3;
    failed |= f80_from_unsigned(0xffffffffU) != 4294967295.0L;
    failed |= f80_to_unsigned(4294967295.75L) != 0xffffffffU;
    volatile long double copy = 0.0L;
    f80_copy(&copy, &left);
    failed |= copy != left;
    failed |= f80_join(1, 10.0L, -5.0L, 20) != 30.0L;
    failed |= f80_join(0, 10.0L, -5.0L, 20) != 15.0L;
    failed |= f80_many(1, 2, 3, 4, 5, 6, 7, 100.0L, 8, 20.0L) != 116.0L;
    F80Wrapper wrapped;
    wrapped.value = precise;
    wrapped = f80_wrapper(wrapped);
    failed |= wrapped.value != precise;
    _Complex long double complex_value;
    __real__ complex_value = precise;
    __imag__ complex_value = -3.25L;
    complex_value = f80_complex(complex_value);
    failed |= __real__ complex_value != precise || __imag__ complex_value != -3.25L;
    long double (*call)(long double, long double) = f80_subtract;
    failed |= call(precise, left) != 1.0L;
    for (int index = 0; index < 40; index += 1)
    {
        right = f80_divide(f80_multiply(right, 3.0L), 3.0L);
    }
    failed |= right != 1.0L;
#if defined(F80_MACHINE_FENV)
    failed |= f80_control_word();
#endif
    return failed;
}
#endif
