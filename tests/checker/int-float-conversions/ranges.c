// #3339: -Wimplicit-int-float-conversion, under -Wimplicit-float-conversion: an integer whose likely range (clang's
// TryGetExprRange, approximate) is wider than the floating target's significand, at every implicit conversion
// AnalyzeImplicitConversions reaches: assignments, initializers, brace elements, returns, arguments, arithmetic and
// comparison operands, compound assignments, conditional arms and a comma's result, in clang's order.
#pragma clang diagnostic warning "-Wimplicit-float-conversion"
typedef float F;
typedef long L;
struct Bits { unsigned narrow : 20; int wide : 30; };
struct Pair { float x; double y; };
void take(double);
F ranges(int i, long l, L tl, unsigned u, short s, unsigned char c, long long ll, struct Bits b, float f, double d)
{
    F a = i;
    d = l;
    d = tl;
    a = s;
    a = c;
    a = b.narrow;
    a = b.wide;
    a = i & 0xffff;
    a = i & 0x1ffffff;
    a = u >> 7;
    a = u >> 10;
    a = i % 1000;
    a = (short)i;
    a = i / 256;
    a = l / 4;
    a = ~u;
    a = -s;
    a = 1 << i;
    a = s * s;
    a = i + 1;
    a = i > 0;
    a = (i, l);
    a = i ? l : s;
    a = f * i;
    a += i;
    if (f < i) {}
    take(ll);
    struct Pair p = { i, l };
    double list[3] = { i, s, ll };
    if (0)
        a = i;
    return i + (float)p.x + (float)p.y + (float)list[0];
}
