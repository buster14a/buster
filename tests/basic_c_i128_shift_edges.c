// Baked independent values cover both sides of the limb boundary. The
// volatile offset keeps every shift amount out of the constant selector.
typedef unsigned __int128 U128;
typedef __int128 S128;
typedef unsigned long long U64;

static volatile unsigned offset = 0;
static const struct ShiftCase
{
    unsigned amount;
    U64 left_high, left_low;
    U64 right_high, right_low;
    U64 signed_high, signed_low;
} cases[] = {
    {0, 0xfedcba9876543210ULL, 0x0123456789abcdefULL, 0xfedcba9876543210ULL, 0x0123456789abcdefULL, 0xfedcba9876543210ULL, 0x0123456789abcdefULL},
    {1, 0xfdb97530eca86420ULL, 0x02468acf13579bdeULL, 0x7f6e5d4c3b2a1908ULL, 0x0091a2b3c4d5e6f7ULL, 0xff6e5d4c3b2a1908ULL, 0x0091a2b3c4d5e6f7ULL},
    {7, 0x6e5d4c3b2a190800ULL, 0x91a2b3c4d5e6f780ULL, 0x01fdb97530eca864ULL, 0x2002468acf13579bULL, 0xfffdb97530eca864ULL, 0x2002468acf13579bULL},
    {31, 0x3b2a19080091a2b3ULL, 0xc4d5e6f780000000ULL, 0x00000001fdb97530ULL, 0xeca8642002468acfULL, 0xfffffffffdb97530ULL, 0xeca8642002468acfULL},
    {32, 0x7654321001234567ULL, 0x89abcdef00000000ULL, 0x00000000fedcba98ULL, 0x7654321001234567ULL, 0xfffffffffedcba98ULL, 0x7654321001234567ULL},
    {63, 0x0091a2b3c4d5e6f7ULL, 0x8000000000000000ULL, 0x0000000000000001ULL, 0xfdb97530eca86420ULL, 0xffffffffffffffffULL, 0xfdb97530eca86420ULL},
    {64, 0x0123456789abcdefULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0xfedcba9876543210ULL, 0xffffffffffffffffULL, 0xfedcba9876543210ULL},
    {65, 0x02468acf13579bdeULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x7f6e5d4c3b2a1908ULL, 0xffffffffffffffffULL, 0xff6e5d4c3b2a1908ULL},
    {95, 0xc4d5e6f780000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x00000001fdb97530ULL, 0xffffffffffffffffULL, 0xfffffffffdb97530ULL},
    {96, 0x89abcdef00000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x00000000fedcba98ULL, 0xffffffffffffffffULL, 0xfffffffffedcba98ULL},
    {127, 0x8000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000001ULL, 0xffffffffffffffffULL, 0xffffffffffffffffULL},
};

static U128 make(U64 high, U64 low)
{
    return ((U128)high << 64) | (U128)low;
}

int main(void)
{
    int result = 0;
    U128 value = make(0xfedcba9876543210ULL, 0x0123456789abcdefULL);
    for (unsigned index = 0; index < sizeof(cases) / sizeof(cases[0]); index += 1)
    {
        unsigned amount = cases[index].amount + offset;
        U128 left = value << amount;
        U128 right = value >> amount;
        S128 signed_right = (S128)value >> amount;
        result |= left != make(cases[index].left_high, cases[index].left_low);
        result |= right != make(cases[index].right_high, cases[index].right_low);
        result |= (U128)signed_right != make(cases[index].signed_high, cases[index].signed_low);
    }
    U128 inverted = ~value;
    U128 negated = -value;
    S128 signed_negated = -(S128)value;
    result |= inverted != make(0x0123456789abcdefULL, 0xfedcba9876543210ULL);
    result |= negated != make(0x0123456789abcdefULL, 0xfedcba9876543211ULL);
    result |= (U128)signed_negated != negated;
    // Negating an exact high-limb value must not borrow from a zero low limb.
    value = make(0x123456789abcdef0ULL, offset);
    result |= -value != make(0xedcba98765432110ULL, 0);
    return result;
}

