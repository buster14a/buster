enum E { A, B };
enum F { X, Y };
typedef enum E TE;
typedef long L;
typedef long *LP;
struct S { TE te; L l; LP lp; };
void take(TE e);
TE arr[2];
void f(enum F v, char *c)
{
    take(X);
    take(v);
    struct S s = { .te = X };
    struct S t = { v };
    s.te = v;
    arr[0] = v;
    TE local = v;
    TE ls[2] = { v, X };
    struct S w = { .lp = c };
    struct S z = { 0, c };
    L la[2] = { c };
    LP lpa[2] = { 0, c };
}
struct In { TE e; LP p; };
struct Out { int k; struct In in[2]; union { L ul; TE ue; } u; };
void g(enum F v, char *c, struct S *ps)
{
    struct Out o = { 0, { { X, 0 }, { .e = v } }, { .ue = v } };
    struct Out q = { 0, { { A, 0 }, { .p = c } } };
    void (*fp)(int, TE) = 0;
    fp(0, v);
    (*fp)(1, X);
}
