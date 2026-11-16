// The order and the effects of a condition's operands written as branches (#2883): each operand a call that adds its
// number to the trace and returns its value, so that the trace shows which operands ran and in what order; nested three
// deep and more, mixed `&&`, `||` and `!`; in `if`, `while`, `for` and `do` conditions, a conditional's condition and
// a statement expression's `if`; operands with side effects of their own (`i++`, assignments, a comma); operands of
// every scalar type, NaN among the doubles; a first operand the program knows; and a long chain.
typedef unsigned long U;

static U trace;
static int calls;

static int t(int id, int v)
{
    trace = trace * 131u + (U)id;
    calls += 1;
    return v;
}

static double td(int id, double v)
{
    trace = trace * 137u + (U)id;
    return v;
}

static void *tp(int id, void *p)
{
    trace = trace * 139u + (U)id;
    return p;
}

static int three(int a, int b, int c)
{
    int r = 0;
    if (t(1, a) && (t(2, b) || !t(3, c)))
    {
        r += 1;
    }
    if (!(t(4, a) || (t(5, b) && t(6, c))) && t(7, !a))
    {
        r += 2;
    }
    if ((t(8, a) || t(9, b)) && (t(10, c) || !(t(11, a) && t(12, b))))
    {
        r += 4;
    }
    if (!(!t(13, a) && !(t(14, b) || !(t(15, c) && !t(16, a)))))
    {
        r += 8;
    }
    return r;
}

static int loops(int a, int b)
{
    int n = 0;
    int i = 0;
    while (t(20, i < 6) && (t(21, a) || t(22, i & 1)))
    {
        i += 1;
        n += 1;
    }
    for (i = 0; !(t(23, i >= 5) || (t(24, b) && t(25, i == 3))); i += 1)
    {
        n += 10;
    }
    i = 0;
    do
    {
        i += 1;
        n += 100;
    } while ((t(26, i < 4) && t(27, a)) || (t(28, i < 2) && !t(29, b)));
    for (;;)
    {
        if (t(30, i > 6) || (t(31, a) && t(32, b) && t(33, i > 4)))
        {
            break;
        }
        i += 1;
    }
    return n + i * 1000;
}

static int choose(int a, int b, int c)
{
    int r = (t(40, a) && t(41, b)) ? t(42, 3) : t(43, 4);
    r = r * 5 + (!(t(44, a) || t(45, c)) ? 1 : 2);
    r = r * 5 + (t(46, a) || (t(47, b) && !t(48, c)) ? (t(49, b) && t(50, c) ? 1 : 2) : 3);
    r = r * 5 + (!!(t(51, c) || t(52, a)) ? 4 : 0);
    return r;
}

static int effects(int a, int b)
{
    int i = a;
    int x = 0;
    int r = 0;
    if (i++ > 0 && (x = b) != 0)
    {
        r += 1;
    }
    if ((t(60, 0), b) || (x += 7, i--))
    {
        r += 2;
    }
    while (i++ < 8 && (x += i) < 30)
    {
        r += 4;
    }
    return r + x * 64 + i * 4096;
}

static int types(int n, double d, float f, signed char sc, unsigned short us, _Bool z, long long ll, unsigned long long ull, int *p, double _Complex w)
{
    int r = 0;
    r += (td(70, d) && f) ? 1 : 0;
    if (sc && (us || !z))
    {
        r += 2;
    }
    if (!(ll || ull) || (tp(71, p) && *p > n))
    {
        r += 4;
    }
    if (w && !(d != d))
    {
        r += 8;
    }
    if ((d != d) || !(f < 0.5f && ull > 3u))
    {
        r += 16;
    }
    return r;
}

static int known(int a, int b)
{
    int r = 0;
    if (1 && t(80, a))
    {
        r += 1;
    }
    if (0 || (t(81, a) && t(82, b)))
    {
        r += 2;
    }
    if ((t(83, 0), 1) && t(84, b))
    {
        r += 4;
    }
    while (0 && t(85, a))
    {
        r += 8;
    }
    return r;
}

static int region(int a, int b, int c)
{
    return 1 + ({
        int q = 0;
        if (t(90, a) && (t(91, b) || t(92, c)))
        {
            q = 3;
        }
        q;
    });
}

static int chain(unsigned n)
{
    int r = 0;
    if ((n & 1) && (n & 2) && (n & 4) && (n & 8) && (n & 16) && (n & 32) && (n & 64) && (n & 128) && (n & 256) && (n & 512) &&
        (n & 1024) && (n & 2048) && (n & 4096) && (n & 8192) && (n & 16384) && (n & 32768) && (n & 65536))
    {
        r += 1;
    }
    if ((n & 1) || (n & 2) || (n & 4) || (n & 8) || (n & 16) || (n & 32) || (n & 64) || (n & 128) || (n & 256) || (n & 512) ||
        (n & 1024) || (n & 2048) || (n & 4096) || (n & 8192) || (n & 16384) || (n & 32768) || (n & 65536))
    {
        r += 2;
    }
    return r;
}

int tested(int n)
{
    U h = 0;
    trace = 0;
    calls = 0;
    int cell = n * 3;
    const double nan = __builtin_nan("");
    for (int k = 0; k < 8; k += 1)
    {
        const int a = k & 1;
        const int b = (k >> 1) & 1;
        const int c = (k >> 2) & 1;
        h = h * 31u + (U)three(a, b, c);
        h = h * 31u + (U)loops(a, b);
        h = h * 31u + (U)choose(a, b, c);
        h = h * 31u + (U)effects(n - 3 + k, c ? n : 0);
        h = h * 31u + (U)known(a, b);
        h = h * 31u + (U)region(a, b, c);
        h = h * 31u + (U)types(n, (k & 1) != 0 ? nan : (double)(n - k), (float)(k - 2) * 0.25f, (signed char)(n * k), (unsigned short)(k & 2), b, (long long)(n & k),
                               (unsigned long long)c, c ? &cell : 0, (double)a + (double)c * 1.0i);
        h = h * 31u + (U)chain((unsigned)(n * 2654435761u) >> k);
        h = h * 31u + (U)chain(k == 0 ? 0x1ffffu : 0);
        h = h * 31u + trace + (U)calls;
    }
    return (int)((h ^ (h >> 31)) & 0x7fffffff);
}
