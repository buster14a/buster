int f(void)
{
    int a[3];
    a[5] = 0;
    enum { B = (9, 1) };
    _Static_assert((1, 0) ? 1 : 0, "x");
    typedef int T[(7, 2)];
    a[6] = 1;
    enum { C = (4, 5) };
    a[7] = B + C;
    return 0;
}
