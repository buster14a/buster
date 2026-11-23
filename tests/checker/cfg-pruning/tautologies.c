// clang's CFG builder's observer (#2990): -Wtautological-bitwise-compare, -Wtautological-overlap-compare and
// -Wtautological-negation-compare, -Wtautological-compare's, given at a body's end in the order the builder visits the
// body (its statements last first), before the body's deferred runtime warnings. A `|` that a logical operator's and its
// own reading both decide is given twice; an equality and a logical operator once. Code past a return is built too.
// A rule whose expression has a macro's token gives no warning, but still decides its branch.
#pragma clang diagnostic warning "-Wtautological-compare"
#define ONE 1
#define CHECK(c) if (c) w += 1
int ty[2];
struct S { int i; char c; _Bool b; } s;
int order(int x, int y)
{
    int w = ty[3];
    if ((x | 1) == 0) w++;
    int z = (y & 8) == 4;
    w += !(x | 2);
    w += (x * 0 && ty[3]) + (x | 1 || ty[3]);
    w += (x & 8) != 4;
    w += (x | 1);
    if (x < 5 || x > 3) w++;
    if (x > 5 && x < 3) w++;
    if (x && !x) w++;
    if (!x || x) w++;
    return w + z + (y == 1 && y == 2);
}
int m(int x) { if (x | 1) return 1; }
int loops(int x, int y)
{
    int w = 0;
    do w++; while (y || x | 1);
    while (y || x | 1) w++;
    for (; y || x | 1;) w++;
    return w + (x == 1 ? (x | 4 ? 1 : 2) : 3);
}
int dead(int x)
{
    return 0;
    if ((x & 3) == 4) return 1;
}
int macros(int x)
{
    int w = 0;
    if (x | ONE) w++;
    CHECK(x | 2);
    CHECK((x & 8) == 4);
    if (x | 1) return w;
    return ty[3];
}
int controls(int x, char c, long l)
{
    int w = 0;
    if (s.c && !s.c) w++;
    if (s.b && !s.b) w++;
    if (s.i && !s.i) w++;
    if (c == 1 && c == 2L) w++;
    if (l == 1 && l == 2) w++;
    if (x == 1 && x == 1) w++;
    if (x < 1 && x < 2) w++;
    if ((x | 1)) w++;
    if ((x & 8L) == 4) w++;
    if (x == 0 || x != 0) w++;
    return w;
}
int widened(long l, __int128 h, unsigned u)
{
    int w = 0;
    if (l < -128 || l > 127) w++;
    if (h < -128 || h > 127) w++;
    if (h < -1 && h > 1) w++;
    if (u == -1 && u == 1) w++;
    if (u > -2 || u < 3) w++;
    return w;
}
int gnu(int x, int y) { return x | 1 ?: y; }
int gnu_logical(int x, int y) { return (x == 1 && x == 2) ?: y; }
