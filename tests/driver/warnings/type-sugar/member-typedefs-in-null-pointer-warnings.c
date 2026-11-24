typedef long *LP;
typedef unsigned U;
struct S { LP lp; U u; } s, *ps;
int n;
void f(void)
{
    s.lp = 1-1;
    ps->lp = 1-1;
    (s).lp = 1-1;
    int r = s.u < n;
    static int z = sizeof(s.u < -1);
    LP local = 1-1;
}
