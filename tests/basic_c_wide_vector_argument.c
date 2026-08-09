// The same shapes as tests/basic_c_wide_argument.c, written as a 512-bit
// vector rather than an aggregate: a vector is where the alignment the
// caller-owned copy has to honour comes from in real code, and where the
// reported Win64 failure was found.
//
// tests/basic_c_simd.c crosses a call boundary with a Simd512 too, but the
// driver builds it without an `-march` that turns BUSTER_SIMD_512 on, so there
// the type is the fallback's 64-byte struct and its alignment is eight. Only a
// native vector asks for sixty four.
//
// The driver builds this for the targets whose convention passes a vector this
// wide by reference -- Win64 and every aarch64 one. The x86-64 SystemV and
// Darwin conventions classify it as one 512-bit vector part instead, and the
// canonical backend's return path encodes vector parts of four, eight and
// sixteen bytes only, so they are not on that list.

typedef unsigned char u8;
typedef u8 Byte64 __attribute__((vector_size(64)));

static Byte64 vector_make(u8 base)
{
    Byte64 value;
    for (int lane = 0; lane < 64; lane += 1)
    {
        value[lane] = (u8)(base + lane);
    }
    return value;
}

static Byte64 vector_identity(Byte64 value)
{
    return value;
}

static Byte64 vector_ninth(Byte64 a, Byte64 b, Byte64 c, Byte64 d, Byte64 e, Byte64 f, Byte64 g, Byte64 h, Byte64 i)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    (void)g;
    (void)h;
    return i;
}

static Byte64 vector_mixed(int first, Byte64 value, long long second, Byte64 other, int third)
{
    Byte64 sum = value + other;
    u8 scalar = (u8)(first + second + third);
    for (int lane = 0; lane < 64; lane += 1)
    {
        sum[lane] = (u8)(sum[lane] + scalar);
    }
    return sum;
}

int main(void)
{
    Byte64 low = vector_make(0);
    Byte64 high = vector_make(100);

    Byte64 identity = vector_identity(low);
    for (int lane = 0; lane < 64; lane += 1)
    {
        if (identity[lane] != (u8)lane)
        {
            return 1;
        }
    }

    Byte64 ninth = vector_ninth(low, high, low, high, low, high, low, high, vector_make(7));
    for (int lane = 0; lane < 64; lane += 1)
    {
        if (ninth[lane] != (u8)(7 + lane))
        {
            return 2;
        }
    }

    Byte64 mixed = vector_mixed(1, low, 2, high, 3);
    for (int lane = 0; lane < 64; lane += 1)
    {
        if (mixed[lane] != (u8)((u8)lane + (u8)(100 + lane) + 6))
        {
            return 3;
        }
    }

    return 0;
}
