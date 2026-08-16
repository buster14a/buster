// Calling-convention shapes the machine register allocators must get right on
// whichever ABI the host builds for: positional register assignment (Win64
// pairs the integer and float files by argument index, System V advances them
// independently), stack arguments above the callee's shadow space, small
// aggregates by value and by return, and a large aggregate returned through a
// hidden pointer. Every callee is also called with enough live values around
// it that the allocator must bind callee-saved registers and prove they
// survive.
typedef struct Pair
{
    int a;
    int b;
} Pair;

typedef struct Trio
{
    int a;
    int b;
    int c;
} Trio;

static int mixed(int a, double b, int c, double d, int e, double f)
{
    return a + (int)b + c + (int)d + e + (int)f;
}

static double floats_first(double a, int b, double c, int d)
{
    return a + b + c + d;
}

static int eight_integers(int a, int b, int c, int d, int e, int f, int g, int h)
{
    return a * 1 + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8;
}

static int pair_by_value(Pair p, int k)
{
    return p.a * 10 + p.b + k;
}

static Pair pair_return(int a, int b)
{
    Pair p;
    p.a = a;
    p.b = b;
    return p;
}

static Trio trio_return(int a)
{
    Trio t;
    t.a = a;
    t.b = a + 1;
    t.c = a + 2;
    return t;
}

// Enough simultaneously live values to force callee-saved bindings across the
// calls it makes, so a caller that failed to preserve one is caught by the
// sums rather than by luck.
static int pressure(int a, int b, int c, int d)
{
    int x0 = a + 1;
    int x1 = b + 2;
    int x2 = c + 3;
    int x3 = d + 4;
    int x4 = a * b;
    int x5 = c * d;
    int x6 = a - d;
    int x7 = b - c;
    int s = eight_integers(x0, x1, x2, x3, x4, x5, x6, x7);
    s += eight_integers(x6, x7, x0, x1, x2, x3, x4, x5);
    s += mixed(x0, x1, x2, x3, x4, x5);
    return s + x0 + x1 + x2 + x3 + x4 + x5 + x6 + x7;
}

int main(void)
{
    if (mixed(1, 2.0, 3, 4.0, 5, 6.0) != 21)
    {
        return 1;
    }
    if (floats_first(1.5, 2, 3.5, 4) != 11.0)
    {
        return 2;
    }
    if (eight_integers(1, 2, 3, 4, 5, 6, 7, 8) != 204)
    {
        return 3;
    }
    Pair p;
    p.a = 3;
    p.b = 7;
    if (pair_by_value(p, 5) != 42)
    {
        return 4;
    }
    Pair returned = pair_return(11, 22);
    if (returned.a != 11 || returned.b != 22)
    {
        return 5;
    }
    Trio trio = trio_return(9);
    if (trio.a != 9 || trio.b != 10 || trio.c != 11)
    {
        return 6;
    }
    // A local the compiler is free to keep in a callee-saved register across
    // every call above and below.
    int keep = trio.a * 1000;
    if (pressure(1, 2, 3, 4) != 382)
    {
        return 7;
    }
    if (keep != 9000)
    {
        return 8;
    }
    return 0;
}
