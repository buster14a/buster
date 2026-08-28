// Both x86-64 ABIs leave the bits above a narrow integer argument's declared
// width unspecified in the register it is passed in, so a callee may not read
// them.  Buster stored the whole incoming register into the parameter's
// eightbyte slot and then read that slot whole, so a `uint32_t` parameter
// carried the caller's leftover high half into every 64-bit use of it -- an
// array index above all.  QuickJS's `JS_DupAtom(ctx, atom)` indexes
// `rt->atom_array[atom]` with a `JSAtom`, which is a `uint32_t`: with the high
// half set the index ran off the array and the engine died parsing its own
// test suite.
//
// The garbage is planted the way the ABI allows a caller to leave it: the
// callee is reached through a pointer whose parameters are wider, which is
// exactly the shape of a call whose argument register still holds an earlier
// value in its upper half.
extern void *memcpy(void *destination, const void *source, unsigned long count);

struct entry
{
    int value;
};

static struct entry table[4] = {{10}, {20}, {30}, {40}};

static int index_table(unsigned index)
{
    return table[index].value;
}

static unsigned long long widen_unsigned(unsigned value)
{
    return (unsigned long long)value;
}

static long long widen_signed(int value)
{
    return (long long)value;
}

static int narrow_sum(short first, unsigned char second, unsigned short third, signed char fourth)
{
    return (int)first + (int)second + (int)third + (int)fourth;
}

static int six_integers(int a, int b, int c, int d, int e, int f)
{
    return a + b + c + d + e + f;
}

// Only the parameters widen: each pointer keeps its callee's own return type,
// because the upper half of a return register is as unspecified as an
// argument's and this fixture is about the argument side.
typedef int (*WideIndex)(unsigned long long);
typedef unsigned long long (*WideUnsigned)(unsigned long long);
typedef long long (*WideSigned)(unsigned long long);
typedef int (*WideNarrow)(unsigned long long, unsigned long long, unsigned long long, unsigned long long);
typedef int (*WideSix)(unsigned long long, unsigned long long, unsigned long long, unsigned long long, unsigned long long, unsigned long long);

// The high halves below are what a caller is allowed to leave behind.
#define HIGH 0xdeadbeef00000000ull

int main(void)
{
    WideIndex wide_index;
    WideUnsigned wide_unsigned;
    WideSigned wide_signed;
    WideNarrow wide_narrow;
    WideSix wide_six;
    void *address;

    address = (void *)index_table;
    memcpy(&wide_index, &address, sizeof(wide_index));
    address = (void *)widen_unsigned;
    memcpy(&wide_unsigned, &address, sizeof(wide_unsigned));
    address = (void *)widen_signed;
    memcpy(&wide_signed, &address, sizeof(wide_signed));
    address = (void *)narrow_sum;
    memcpy(&wide_narrow, &address, sizeof(wide_narrow));
    address = (void *)six_integers;
    memcpy(&wide_six, &address, sizeof(wide_six));

    if (wide_index(HIGH | 2ull) != 30) return 1;
    if (wide_index(0xffffffff00000000ull | 3ull) != 40) return 2;
    if (wide_unsigned(HIGH | 7ull) != 7ull) return 3;
    // A signed narrow parameter keeps its own sign, which the value's own
    // width decides and the register's upper half never does.
    if (wide_signed(HIGH | 0xfffffffeull) != -2ll) return 4;
    if (wide_signed(HIGH | 5ull) != 5ll) return 5;
    if (wide_narrow(HIGH | 0xfffbull, HIGH | 0xffull, HIGH | 0xfffful, HIGH | 0xffull) != -5 + 255 + 65535 - 1) return 6;
    if (wide_six(HIGH | 1ull, HIGH | 2ull, HIGH | 3ull, HIGH | 4ull, HIGH | 5ull, HIGH | 6ull) != 21) return 7;
    if (wide_six(0xffffffff00000000ull, 0xffffffff00000000ull | 0xfffffffeull, 0, 0, 0, 0) != -2) return 8;
    // The ordinary in-language calls still answer the same values.
    if (index_table(2) != 30) return 9;
    if (narrow_sum(-5, 255, 65535, -1) != -5 + 255 + 65535 - 1) return 10;
    if (six_integers(1, 2, 3, 4, 5, 6) != 21) return 11;
    return 0;
}
