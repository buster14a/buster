/* Runtime i128 complements checked against independent 64-bit limb operations.
   Pointer signatures also exercise Windows without requiring a bare-i128 ABI. */
typedef unsigned long long U64;
typedef unsigned __int128 U128;
typedef __int128 I128;

static void complement_unsigned(U128* out, U128 const* in)
{
    *out = ~*in;
}

static void complement_signed(I128* out, I128 const* in)
{
    *out = ~*in;
}

static void complement_twice(U128* out, U128 const* in)
{
    U128 first = ~*in;
    *out = ~first;
}

static void complement_mix(U128* out, U128 const* in, U128 const* mask)
{
    U128 first = ~*in;
    *out = first ^ *mask;
}

static int check(U64 low, U64 high)
{
    U128 value = ((U128)high << 64) | low;
    U128 output = ((U128)0x0123456789abcdefULL << 64) | 0xfedcba9876543210ULL;
    complement_unsigned(&output, &value);
    int failed = (U64)output != ~low || (U64)(output >> 64) != ~high;
    complement_signed((I128*)&output, (I128 const*)&value);
    failed |= (U64)output != ~low || (U64)(output >> 64) != ~high;
    complement_twice(&output, &value);
    failed |= (U64)output != low || (U64)(output >> 64) != high;
    U128 mask = ((U128)0x8070605040302010ULL << 64) | 0x0102030405060708ULL;
    complement_mix(&output, &value, &mask);
    failed |= (U64)output != (~low ^ 0x0102030405060708ULL) || (U64)(output >> 64) != (~high ^ 0x8070605040302010ULL);
    complement_unsigned(&value, &value);
    failed |= (U64)value != ~low || (U64)(value >> 64) != ~high;
    return failed;
}

int main(void)
{
    int failed = check(0, 0);
    failed |= check(~(U64)0, ~(U64)0);
    failed |= check(0, ~(U64)0);
    failed |= check(~(U64)0, 0);
    failed |= check(0x123456789abcdef0ULL, 0xfedcba9876543210ULL);
    failed |= check(0xaaaaaaaaaaaaaaaaULL, 0x5555555555555555ULL);
    for (unsigned bit = 0; bit < 128; bit += 1)
    {
        U64 low = 0;
        U64 high = 0;
        if (bit < 64) low = (U64)1 << bit;
        else high = (U64)1 << (bit - 64);
        failed |= check(low, high);
        failed |= check(~low, ~high);
    }
    return failed;
}
