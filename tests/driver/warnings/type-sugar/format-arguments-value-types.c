int printf(const char *, ...);
typedef const int CI;
typedef int I;
typedef const char *CS;
const int g = 1;
const I gi = 2;
CI gc = 3;
struct s { const I m; CI n; const int k; };
void f(const int l, const I x, CI y, struct s t, const CS q, const I *pi, volatile I vi)
{
    printf("%s %s %s %s %s\n", g, gi, gc, l, x);
    printf("%s %s %s %s %d %d\n", y, t.m, t.n, t.k, q, pi);
    printf("%s %s\n", vi, *pi);
    const int a[2] = { 0 };
    printf("%d\n", a);
}
