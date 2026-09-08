// Shared canonical local promotion: every result is checked, not just compiled.
// This fixture runs under all allocators, with target-local promotion disabled,
// and in the execution-mode matrix on its supported targets.
static int diamond(int condition, int a, int b)
{
    int value = a;
    if (condition) value += b;
    else value -= b;
    return value;
}

static int rotate(int count)
{
    int a = 1, b = 2, c = 3;
    while (count > 0)
    {
        int t = a;
        a = b;
        b = c;
        c = t;
        count -= 1;
    }
    return a * 100 + b * 10 + c;
}

static int irreducible(int count, int side)
{
    int value = 1;
    if (side) goto right;
left:
    value += 2;
    count -= 1;
    if (count > 0) goto right;
    return value;
right:
    value += 3;
    count -= 1;
    if (count > 0) goto left;
    return value;
}

static double mixed_joins(int n)
{
    int i = 1;
    if (n & 1) i = 3;
    double d = 0.5;
    if (n & 2) d = 1.5;
    float f = 2.25f;
    while (n > 0)
    {
        i += 1;
        d += 0.5;
        f += 0.25f;
        n -= 1;
    }
    return i + d + f;
}

static int pointer_join(int n)
{
    int a = 7, b = 11;
    int* p = &a;
    if (n) p = &b;
    return *p;
}

// `same` is both the branch predicate and an incoming edge value. Selector
// use census must retain both roles when the local becomes a block parameter.
static int predicate_join(int a, int b)
{
    int same = a == b;
    if (same) same = 7;
    return same;
}

static int narrow_join(int n)
{
    signed char a = -7;
    unsigned short b = 65530;
    if (n) { a = -11; b = 65535; }
    return (int)a + (int)b;
}

static int reassigned_parameter(int n)
{
    if (n > 3) n = n * 2;
    else n = n + 9;
    return n;
}

static int switch_join(int n)
{
    int v;
    switch (n)
    {
    case 0: v = 2; break;
    case 1: case 2: v = 7; break;
    default: v = 11; break;
    }
    return v;
}

static void mutate(int* p) { *p += 5; }
static int escaped(void)
{
    int v = 7;
    mutate(&v);
    return v;
}
static int volatile_storage(void)
{
    volatile int v = 1;
    v = v + 3;
    return v;
}

int main(void)
{
    int failed = 0;
    for (int n = 0; n < 18; n += 1)
    {
        failed |= diamond(n & 1, 9, 4) != ((n & 1) ? 13 : 5);
        failed |= rotate(n) != (n % 3 == 0 ? 123 : n % 3 == 1 ? 231 : 312);
        failed |= pointer_join(n) != (n ? 11 : 7);
        failed |= predicate_join(n, n + (n & 1)) != ((n & 1) ? 0 : 7);
        failed |= narrow_join(n) != (n ? 65524 : 65523);
        failed |= reassigned_parameter(n) != (n > 3 ? n * 2 : n + 9);
        failed |= switch_join(n) != (n == 0 ? 2 : n <= 2 ? 7 : 11);
        int reference = 1;
        for (int k = 0; k < n + 1; k += 1) reference += (k & 1) ? 3 : 2;
        failed |= irreducible(n + 1, 0) != reference;
        reference = 1;
        for (int k = 0; k < n + 1; k += 1) reference += (k & 1) ? 2 : 3;
        failed |= irreducible(n + 1, 1) != reference;
        double expected = ((n & 1) ? 3 : 1) + ((n & 2) ? 1.5 : 0.5) + 2.25 + n * 1.75;
        failed |= mixed_joins(n) != expected;
    }
    failed |= escaped() != 12;
    failed |= volatile_storage() != 4;
    return failed;
}
