struct { int a; } anon;
union { int b; } anon_u;
void f(int n, int m)
{
    int a[n];
    a++;
    int (*p)[n + 1] = 0;
    p();
    int b[n][m * 2];
    b++;
    char c[sizeof(int) * n];
    c++;
    int d[(n)];
    d();
    anon++;
    anon_u();
    struct { int x; } loc;
    loc();
    enum { E1 } e;
    e();
}
