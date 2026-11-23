// Automatic objects, a GNU old-style designator and chains after the failed designator (#1842).
struct P { int x; char y; };
struct Q { struct P a; struct P b; };
struct T { int x, y; };
void f(struct P p, int i)
{
    struct T l = { .nope = i, p, 3 };
    struct Q m = { nope: 1, { i, p } };
    struct Q n = { .nope.x = 1, { p } };
    struct Q o = { .nope[0] = 1, { 1 }, { "s" } };
    (void)l;
    (void)m;
    (void)n;
    (void)o;
}
