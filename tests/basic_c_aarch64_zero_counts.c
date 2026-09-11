/* Scalar and slot-backed i128 zero counts. The wide builtin spelling is a
 * Buster extension: external compilers use two defined 64-bit calls instead
 * of silently truncating an i128 argument to __builtin_clzll/__builtin_ctzll.
 * Expected answers below are independent bit-position and literal controls.
 */
typedef unsigned int U32;
typedef unsigned long long U64;
typedef unsigned __int128 U128;

static U32 leading32(U32 value) { return (U32)__builtin_clz(value); }
static U32 trailing32(U32 value) { return (U32)__builtin_ctz(value); }
static U32 leading64(U64 value) { return (U32)__builtin_clzll(value); }
static U32 trailing64(U64 value) { return (U32)__builtin_ctzll(value); }

static void counts128(U128 value, U128 *leading, U128 *trailing)
{
#ifdef __BUSTER__
    *leading = __builtin_clzll(value);
    *trailing = __builtin_ctzll(value);
#else
    U64 low = (U64)value;
    U64 high = (U64)(value >> 64);
    *leading = high ? (U32)__builtin_clzll(high) : low ? 64u + (U32)__builtin_clzll(low) : 128u;
    *trailing = low ? (U32)__builtin_ctzll(low) : high ? 64u + (U32)__builtin_ctzll(high) : 128u;
#endif
}

static int check128(U64 low, U64 high, U32 expected_leading, U32 expected_trailing)
{
    U128 value = ((U128)high << 64) | low;
    U128 leading = ~(U128)0;
    U128 trailing = ~(U128)0;
    counts128(value, &leading, &trailing);
    int failed = leading != (U128)expected_leading || trailing != (U128)expected_trailing;
    return failed;
}

int main(void)
{
    int failed = 0;
    for (U32 bit = 0; bit < 32; bit += 1)
    {
        U32 value = (U32)1 << bit;
        failed |= leading32(value) != 31u - bit;
        failed |= trailing32(value) != bit;
        failed |= leading32(value | 1u) != 31u - bit;
        failed |= trailing32(value | 0x80000000u) != bit;
    }
    for (U32 bit = 0; bit < 64; bit += 1)
    {
        U64 value = (U64)1 << bit;
        failed |= leading64(value) != 63u - bit;
        failed |= trailing64(value) != bit;
        failed |= leading64(value | 1ull) != 63u - bit;
        failed |= trailing64(value | 0x8000000000000000ull) != bit;
    }
    for (U32 bit = 0; bit < 128; bit += 1)
    {
        U64 low = 0;
        U64 high = 0;
        if (bit < 64)
        {
            low = (U64)1 << bit;
        }
        else
        {
            high = (U64)1 << (bit - 64);
        }
        failed |= check128(low, high, 127u - bit, bit);
        failed |= check128(low | 1ull, high, 127u - bit, 0);
        failed |= check128(low, high | 0x8000000000000000ull, 0, bit);
    }
    failed |= check128(~0ull, ~0ull, 0, 0);
    failed |= check128(~0ull, 0, 64, 0);
    failed |= check128(0, ~0ull, 0, 64);
    failed |= check128(0x0000000100000000ull, 0x0000000080000000ull, 32, 32);
    failed |= check128(0xaaaaaaaaaaaaaaaaull, 0x5555555555555555ull, 1, 1);
    /* The existing AArch64 direct oracle defines the all-zero pair as 128.
       External controls guard both limbs, never call a zero-input builtin. */
    failed |= check128(0, 0, 128, 128);
    return failed;
}
