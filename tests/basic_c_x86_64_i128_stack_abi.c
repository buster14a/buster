// System V x86-64 places a sixteen-byte-aligned stack argument at a
// sixteen-aligned offset in the argument area, not immediately after the
// eightbyte before it.  The machine placement used to pack the cursor tight,
// so a machine-selected callee read a stacked __int128 one eightbyte early
// while the canonical caller (and clang) had padded — a disagreement only a
// run can see, and only when the callee itself stays inside the machine
// subset.  Every callee here therefore forwards its halves to the one
// checker that owns the __int128 shifts: the shifts are what push a function
// to the canonical fallback, and a callee that fell back would silently stop
// covering the machine placement.  basic_c_x86_64_i128_stack_abi in
// driver_test.c pins that census.  This file has no hosted dependencies and
// the same shapes hold on AArch64, whose placement rounds by C.8/C.12.
typedef unsigned long long u64;

typedef struct I128Box
{
    __int128 value;
} I128Box;

typedef struct AlignedPair
{
    _Alignas(16) long low;
    long high;
} AlignedPair;

static int check_halves(__int128 value, u64 low, u64 high)
{
    return (u64)value == low && (u64)(value >> 64) == high;
}

// Seven integer arguments leave an odd stack cursor: g takes eightbyte zero,
// so the sixteen-aligned pair must skip eightbyte one and start at sixteen.
// The trailing scalar catches a callee that never counted the padding.
static int bare_after_seven(int a, int b, int c, int d, int e, int f, int g, __int128 value, int tail)
{
    return a == 1 && b == 2 && c == 3 && d == 4 && e == 5 && f == 6 && g == 7 && check_halves(value, 9, 10) && tail == 11;
}

// Eight integer arguments leave an even cursor: g and h take eightbytes zero
// and one, the pair starts at sixteen with no padding to insert.  This is the
// tight case, and the caller must not invent a gap where none belongs.
static int bare_after_eight(int a, int b, int c, int d, int e, int f, int g, int h, __int128 value, int tail)
{
    return a == 1 && b == 2 && c == 3 && d == 4 && e == 5 && f == 6 && g == 7 && h == 8 && check_halves(value, 12, 13) && tail == 14;
}

// The same rounding through the aggregate shape: a one-member wrapper keeps
// the sixteen-byte alignment of the pair it carries.
static int boxed_after_seven(int a, int b, int c, int d, int e, int f, int g, I128Box box, int tail)
{
    return a == 1 && b == 2 && c == 3 && d == 4 && e == 5 && f == 6 && g == 7 && check_halves(box.value, 15, 16) && tail == 17;
}

// A sixteen-aligned aggregate with no __int128 inside: _Alignas raises the
// slot alignment the same way the pair's own type does.
static int aligned_after_seven(int a, int b, int c, int d, int e, int f, int g, AlignedPair pair, int tail)
{
    return a == 1 && b == 2 && c == 3 && d == 4 && e == 5 && f == 6 && g == 7 && pair.low == 18 && pair.high == 19 && tail == 20;
}

int main(void)
{
    if (!bare_after_seven(1, 2, 3, 4, 5, 6, 7, ((__int128)10 << 64) | 9, 11))
    {
        return 1;
    }
    if (!bare_after_eight(1, 2, 3, 4, 5, 6, 7, 8, ((__int128)13 << 64) | 12, 14))
    {
        return 2;
    }
    I128Box box = {.value = ((__int128)16 << 64) | 15};
    if (!boxed_after_seven(1, 2, 3, 4, 5, 6, 7, box, 17))
    {
        return 3;
    }
    AlignedPair pair = {.low = 18, .high = 19};
    if (!aligned_after_seven(1, 2, 3, 4, 5, 6, 7, pair, 20))
    {
        return 4;
    }
    return 0;
}
