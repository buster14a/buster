/* Preserve the direct native emitter's bit results outside defined C divide
   inputs. These are an implementation oracle, never a Clang differential:
   zero divisors produce an all-ones magnitude quotient and the dividend as
   remainder; the signed minimum divided by -1 wraps to the minimum. */
typedef unsigned __int128 U128;
typedef __int128 S128;

static volatile U128 numerator = ((U128)0x923456789abcdef0ULL << 64) | 0xfedcba9876543210ULL;
static volatile U128 zero = 0;
static volatile S128 minimum = (S128)((U128)1 << 127);
static volatile S128 minus_one = -1;

int main(void)
{
    U128 n = numerator;
    U128 d = zero;
    int result = n / d != ~(U128)0 || n % d != n;
    S128 sn = (S128)n;
    S128 sd = (S128)d;
    result |= sn / sd != 1 || sn % sd != sn;
    sn = (S128)(n >> 1);
    result |= sn / sd != -1 || sn % sd != sn;
    sn = minimum;
    sd = minus_one;
    result |= sn / sd != sn || sn % sd != 0;
    return result;
}
