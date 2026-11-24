typedef const int CI;
typedef volatile int VI;
typedef unsigned U;
struct s { const CI m; CI n; };
void f(const CI y, volatile U u, struct s *ps)
{
    const CI twice[1] = { 1 };
    twice++;
    const CI one = 1;
    one = 2;
    CI const after[2] = { 1 };
    after++;
    volatile CI mixed[2] = { 0 };
    mixed++;
    const VI cv[2] = { 0 };
    cv++;
    static const CI s2[2][3];
    s2++;
    const CI *p = 0;
    p();
    if (y < u) {}
    if (ps->m < u) {}
    ps->m();
    ps->n();
    const U a = 1, b = 2;
    if (a + b < y) {}
}
