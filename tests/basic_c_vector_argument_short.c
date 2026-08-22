// Short vector arguments narrower than a full vector register, and the two
// shapes whose bugs this fixture exists to catch.
//
// AArch64 hands a one-, two-, four-, or eight-byte vector the low bytes of a
// V register. The canonical emitter classified such a parameter correctly but
// had no sized transfer for the one- and two-byte cases, and its capture site
// does not check the transfer helper's result, so the parameter's slot was
// simply left zero — a silent wrong answer no fixture executed. The second
// shape is a vector spilled to the stack followed by a *register* argument,
// which AAPCS64 permits because the integer file stays open behind a
// stack-passed composite: the machine callee read its stack parts back into
// fresh registers before the later argument was captured, so the allocator
// could place a read-back on top of an argument register still holding a live
// incoming value.
//
// Everything here is one translation unit, so the fixture only asserts that
// the compiler agrees with itself across a call boundary — which is what both
// bugs broke. The driver runs it through the default pipeline and through the
// canonical emitter, so a regression in either shows up.

typedef unsigned char Byte1 __attribute__((vector_size(1)));
typedef unsigned char Byte2 __attribute__((vector_size(2)));
typedef unsigned char Byte4 __attribute__((vector_size(4)));
typedef unsigned char Byte8 __attribute__((vector_size(8)));
typedef unsigned char Byte16 __attribute__((vector_size(16)));

static Byte1 byte1_identity(Byte1 value)
{
    return value;
}

static Byte2 byte2_identity(Byte2 value)
{
    return value;
}

static Byte4 byte4_identity(Byte4 value)
{
    return value;
}

static Byte8 byte8_identity(Byte8 value)
{
    return value;
}

// Nine short vectors: the ninth travels on the stack once the vector file is
// full, and the widths are mixed so a sized stack part and a full-width one
// both cross the boundary.
static long nine_short(Byte2 a, Byte2 b, Byte4 c, Byte4 d, Byte8 e, Byte8 f, Byte16 g, Byte16 h, Byte2 ninth)
{
    return a[0] + b[1] + c[2] + d[3] + e[4] + f[5] + g[6] + h[7] + ninth[0] * 100 + ninth[1] * 1000;
}

// A vector spilled to the stack, then an ordinary integer argument that still
// takes an integer register behind it.
static long spilled_then_scalar(Byte16 a, Byte16 b, Byte16 c, Byte16 d, Byte16 e, Byte16 f, Byte16 g, Byte16 h, Byte2 ninth,
                                long salt)
{
    return a[0] + b[0] + c[0] + d[0] + e[0] + f[0] + g[0] + h[0] + ninth[0] * 7 + ninth[1] * 11 + salt;
}

static long wide_spilled_then_scalar(Byte16 a, Byte16 b, Byte16 c, Byte16 d, Byte16 e, Byte16 f, Byte16 g, Byte16 h,
                                     Byte16 ninth, long salt)
{
    return a[0] + b[0] + c[0] + d[0] + e[0] + f[0] + g[0] + h[0] + ninth[0] * 7 + ninth[1] * 11 + salt;
}

int main(void)
{
    int result = 0;

    Byte1 one = {7};
    Byte2 two = {11, 12};
    Byte4 four = {21, 22, 23, 24};
    Byte8 eight = {31, 32, 33, 34, 35, 36, 37, 38};

    Byte1 one_back = byte1_identity(one);
    Byte2 two_back = byte2_identity(two);
    Byte4 four_back = byte4_identity(four);
    Byte8 eight_back = byte8_identity(eight);

    result |= one_back[0] != 7 ? 1 : 0;
    result |= (two_back[0] != 11 || two_back[1] != 12) ? 2 : 0;
    result |= (four_back[0] != 21 || four_back[3] != 24) ? 4 : 0;
    result |= (eight_back[0] != 31 || eight_back[7] != 38) ? 8 : 0;

    Byte2 a2 = {1, 2};
    Byte2 b2 = {3, 4};
    Byte4 a4 = {5, 6, 7, 8};
    Byte4 b4 = {9, 10, 11, 12};
    Byte8 a8 = {13, 14, 15, 16, 17, 18, 19, 20};
    Byte8 b8 = {21, 22, 23, 24, 25, 26, 27, 28};
    Byte16 a16 = {29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44};
    Byte16 b16 = {45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60};
    Byte2 c2 = {61, 62};

    // 1 + 4 + 7 + 12 + 17 + 26 + 35 + 52 + 6100 + 62000
    result |= nine_short(a2, b2, a4, b4, a8, b8, a16, b16, c2) != 68254 ? 16 : 0;

    // 4 * (29 + 45) + 61 * 7 + 62 * 11 + 5
    result |= spilled_then_scalar(a16, b16, a16, b16, a16, b16, a16, b16, c2, 5) != 1410 ? 32 : 0;

    Byte16 tail16 = {61, 62, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    result |= wide_spilled_then_scalar(a16, b16, a16, b16, a16, b16, a16, b16, tail16, 5) != 1410 ? 64 : 0;

    return result;
}
