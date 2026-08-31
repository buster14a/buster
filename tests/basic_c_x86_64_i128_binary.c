// The 128-bit binary subset the machine selectors carry, written so both
// targets run it: the AArch64 selector reached this subset first and the x86-64
// one refused all of it (issue #811), which is why tests/basic_c_aarch64_i128.c
// -- the fixture that had the coverage -- is an AArch64 file. That one also
// exercises the divides and the i128 GNU bit builtins, which no machine path
// selects and which the x86-64 canonical emitter does not carry at all, so this
// is a separate file rather than a second lane over it.
//
// Every operation here has to *select*: the driver test pins the fallback count
// as well as running the program, because the canonical emitter would produce
// the same answers and hide a selector that stopped firing.
//
// The values are chosen to cross the halfway boundary in both directions --
// carries out of the low half, borrows into it, shift amounts below, at and
// above 64, and comparisons that the high halves decide against ones only the
// low halves can.

typedef unsigned __int128 U128;
typedef __int128 S128;
typedef unsigned long long U64;

static const U128 low_only = (U128)0x0123456789abcdefULL;
static const U64 all_ones = 0xffffffffffffffffULL;

static U128 make(U64 high, U64 low)
{
    return ((U128)high << 64) | (U128)low;
}

static U64 high_half(U128 value)
{
    return (U64)(value >> 64);
}

static U64 low_half(U128 value)
{
    return (U64)value;
}

// The multiply lives in its own function on purpose. AArch64 still refuses it
// -- it needs a UMULH row, and that mnemonic has no generated form id yet
// (#810) -- so keeping it out of `main` leaves every other check in this file
// machine-selected on both targets, and leaves the fallback count able to say
// so: zero on x86-64, exactly this one function on AArch64.
static int multiply_checks(void)
{
    if (high_half(make(0, 3) * make(0, 5)) != 0 || low_half(make(0, 3) * make(0, 5)) != 15)
    {
        return 43;
    }
    {
        // The full-width product of two low halves, which is the whole reason
        // the high multiply exists: neither operand has a high half and the
        // answer does.
        U128 product = make(0, all_ones) * make(0, all_ones);
        if (high_half(product) != 0xfffffffffffffffeULL || low_half(product) != 1)
        {
            return 44;
        }
        product = make(0, 1ULL << 63) * make(0, 2);
        if (high_half(product) != 1 || low_half(product) != 0)
        {
            return 45;
        }
    }
    {
        // Each cross term alone, then both at once.
        U128 product = make(3, 4) * make(0, 5);
        if (high_half(product) != 15 || low_half(product) != 20)
        {
            return 46;
        }
        product = make(0, 5) * make(3, 4);
        if (high_half(product) != 15 || low_half(product) != 20)
        {
            return 47;
        }
        product = make(7, 0x0123456789abcdefULL) * make(0x11, 0xfedcba9876543210ULL);
        if (high_half(product) != 0x0c82b00c0e2de291ULL || low_half(product) != 0x2236d88fe5618cf0ULL)
        {
            return 48;
        }
    }
    {
        // The truncated product is signedness-blind, so the signed spelling of
        // the same bits answers the same way.
        S128 negative = (S128)make(all_ones, 0xfffffffffffffffbULL);
        S128 product = negative * (S128)7;
        if (high_half((U128)product) != all_ones || low_half((U128)product) != 0xffffffffffffffddULL)
        {
            return 49;
        }
    }
    {
        U128 product = make(all_ones, all_ones) * make(0, 2);
        if (high_half(product) != all_ones || low_half(product) != 0xfffffffffffffffeULL)
        {
            return 50;
        }
    }
    return 0;
}

int main(void)
{
    // Add: the carry out of the low half has to reach the high one.
    {
        U128 sum = make(0, all_ones) + make(0, 1);
        if (high_half(sum) != 1 || low_half(sum) != 0)
        {
            return 1;
        }
        sum = make(3, 4) + make(5, 6);
        if (high_half(sum) != 8 || low_half(sum) != 10)
        {
            return 2;
        }
        sum = make(all_ones, all_ones) + make(0, 1);
        if (high_half(sum) != 0 || low_half(sum) != 0)
        {
            return 3;
        }
    }
    // Subtract: the borrow out of the low half has to reach the high one, and
    // is read before either half moves.
    {
        U128 difference = make(1, 0) - make(0, 1);
        if (high_half(difference) != 0 || low_half(difference) != all_ones)
        {
            return 4;
        }
        difference = make(8, 10) - make(5, 6);
        if (high_half(difference) != 3 || low_half(difference) != 4)
        {
            return 5;
        }
        difference = make(0, 0) - make(0, 1);
        if (high_half(difference) != all_ones || low_half(difference) != all_ones)
        {
            return 6;
        }
    }
    // The bitwise trio, per half.
    {
        U128 left = make(0xf0f0f0f0f0f0f0f0ULL, 0x00ff00ff00ff00ffULL);
        U128 right = make(0x0f0f0f0f0f0f0f0fULL, 0xff00ff00ff00ff00ULL);
        if (high_half(left & right) != 0 || low_half(left & right) != 0)
        {
            return 7;
        }
        if (high_half(left | right) != all_ones || low_half(left | right) != all_ones)
        {
            return 8;
        }
        if (high_half(left ^ right) != all_ones || low_half(left ^ right) != all_ones)
        {
            return 9;
        }
        if (high_half(left & left) != 0xf0f0f0f0f0f0f0f0ULL || low_half(left & left) != 0x00ff00ff00ff00ffULL)
        {
            return 10;
        }
    }
    // Shift left: below, at and above the halfway boundary, plus the zero
    // amount that is a whole-value copy.
    {
        U128 value = make(0x1122334455667788ULL, 0x99aabbccddeeff00ULL);
        U128 shifted = value << 0;
        if (high_half(shifted) != 0x1122334455667788ULL || low_half(shifted) != 0x99aabbccddeeff00ULL)
        {
            return 11;
        }
        shifted = value << 4;
        if (high_half(shifted) != 0x1223344556677889ULL || low_half(shifted) != 0x9aabbccddeeff000ULL)
        {
            return 12;
        }
        shifted = value << 64;
        if (high_half(shifted) != 0x99aabbccddeeff00ULL || low_half(shifted) != 0)
        {
            return 13;
        }
        shifted = value << 68;
        if (high_half(shifted) != 0x9aabbccddeeff000ULL || low_half(shifted) != 0)
        {
            return 14;
        }
        shifted = low_only << 100;
        if (high_half(shifted) != 0x9abcdef000000000ULL || low_half(shifted) != 0)
        {
            return 15;
        }
    }
    // Unsigned shift right, the one direction that already selected.
    {
        U128 value = make(0x1122334455667788ULL, 0x99aabbccddeeff00ULL);
        U128 shifted = value >> 0;
        if (high_half(shifted) != 0x1122334455667788ULL || low_half(shifted) != 0x99aabbccddeeff00ULL)
        {
            return 16;
        }
        shifted = value >> 4;
        if (high_half(shifted) != 0x0112233445566778ULL || low_half(shifted) != 0x899aabbccddeeff0ULL)
        {
            return 17;
        }
        shifted = value >> 64;
        if (high_half(shifted) != 0 || low_half(shifted) != 0x1122334455667788ULL)
        {
            return 18;
        }
        shifted = value >> 68;
        if (high_half(shifted) != 0 || low_half(shifted) != 0x0112233445566778ULL)
        {
            return 19;
        }
    }
    // Signed shift right: the sign fills what leaves, at every boundary.
    {
        S128 negative = (S128)make(0x8000000000000000ULL, 0x0000000000000001ULL);
        S128 shifted = negative >> 0;
        if (high_half((U128)shifted) != 0x8000000000000000ULL || low_half((U128)shifted) != 1)
        {
            return 20;
        }
        shifted = negative >> 4;
        if (high_half((U128)shifted) != 0xf800000000000000ULL || low_half((U128)shifted) != 0)
        {
            return 21;
        }
        shifted = negative >> 64;
        if (high_half((U128)shifted) != all_ones || low_half((U128)shifted) != 0x8000000000000000ULL)
        {
            return 22;
        }
        shifted = negative >> 65;
        if (high_half((U128)shifted) != all_ones || low_half((U128)shifted) != 0xc000000000000000ULL)
        {
            return 23;
        }
        S128 positive = (S128)make(0x0123456789abcdefULL, 0xfedcba9876543210ULL);
        shifted = positive >> 68;
        if (high_half((U128)shifted) != 0 || low_half((U128)shifted) != 0x00123456789abcdeULL)
        {
            return 24;
        }
    }
    // i128-typed constants, which are slot-backed and written half by half.
    {
        U128 zero = (U128)0;
        U128 one = (U128)1;
        U128 wide = (U128)1 << 100;
        S128 negative_one = (S128)-1;
        if (high_half(zero) != 0 || low_half(zero) != 0)
        {
            return 25;
        }
        if (high_half(one) != 0 || low_half(one) != 1)
        {
            return 26;
        }
        if (high_half(wide) != 0x0000001000000000ULL || low_half(wide) != 0)
        {
            return 27;
        }
        if (high_half((U128)negative_one) != all_ones || low_half((U128)negative_one) != all_ones)
        {
            return 28;
        }
    }
    // Equality, through the per-half difference.
    {
        U128 left = make(7, 9);
        if (!(left == make(7, 9)) || left != make(7, 9))
        {
            return 29;
        }
        if (left == make(7, 10) || !(left != make(7, 10)))
        {
            return 30;
        }
        if (left == make(8, 9) || !(left != make(8, 9)))
        {
            return 31;
        }
    }
    // The eight ordered comparisons. Each triple is a pair the high halves
    // decide, a pair only the low halves can, and an equal pair.
    {
        U128 small_high = make(1, 9);
        U128 large_high = make(2, 0);
        U128 small_low = make(2, 1);
        if (!(small_high < large_high) || large_high < small_high || small_high < small_high)
        {
            return 32;
        }
        if (!(large_high < small_low) || small_low < large_high)
        {
            return 33;
        }
        if (!(small_high <= large_high) || !(small_high <= small_high) || large_high <= small_high)
        {
            return 34;
        }
        if (!(large_high > small_high) || small_high > large_high || small_high > small_high)
        {
            return 35;
        }
        if (!(large_high >= small_high) || !(small_high >= small_high) || small_high >= large_high)
        {
            return 36;
        }
    }
    {
        // The signed relations differ from the unsigned ones exactly where the
        // high half's top bit is set.
        S128 negative = (S128)make(0x8000000000000000ULL, 0);
        S128 positive = (S128)make(0x0000000000000001ULL, 0);
        S128 negative_small = (S128)make(0x8000000000000000ULL, 1);
        if (!(negative < positive) || positive < negative || negative < negative)
        {
            return 37;
        }
        if (!(negative < negative_small) || negative_small < negative)
        {
            return 38;
        }
        if (!(negative <= positive) || !(negative <= negative) || positive <= negative)
        {
            return 39;
        }
        if (!(positive > negative) || negative > positive || negative > negative)
        {
            return 40;
        }
        if (!(positive >= negative) || !(negative >= negative) || negative >= positive)
        {
            return 41;
        }
        // The same pair read as unsigned answers the other way round, which is
        // what says the signed and unsigned high-half conditions are distinct.
        if (!((U128)negative > (U128)positive))
        {
            return 42;
        }
    }
    {
        int multiply_result = multiply_checks();
        if (multiply_result)
        {
            return multiply_result;
        }
    }
    return 0;
}
