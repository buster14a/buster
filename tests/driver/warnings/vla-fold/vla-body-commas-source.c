int f(void)
{
    int a[3];
    a[5] = 0;
    enum { B = (9, 1), C = ((void)0, 2), D = (B, 3) };
    typedef int T[(7, 2)];
    a[6] = 1;
    enum { E = (4, 5) };
    struct S { int m[(1, 2)]; int w : (3, 4); };
    int u = 1 / 0;
    {
        static int s[(5, 2)];
        a[7] = s[0];
        {
            enum { G = (6, 7) };
            a[8] = G;
        }
    }
    a[9] = (int)sizeof(T) + E;
    return u;
}
int g(int n)
{
    int b[2];
    b[2] = n;
    enum { H = (8, 9) };
    if (n) { enum { I = (1, 2) }; b[3] = I; }
    do { enum { J = (3, 4) }; } while (b[4]);
    for (int i = b[5]; i < 1; i++) { enum { K = (5, 6) }; }
    return H;
}
