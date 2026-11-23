// A union's list with a designator that names no member has its member: later positional values are left over, with no
// excess warning (#1842).
struct P { int x; char y; };
union U { int i; struct P p; };
struct W { union U u; int z; };
union U u1 = { .p.nope = 1, 5 };
union U u2 = { .nope = 1, 5 };
struct W w = { .u.p.nope = 1, 5 };
void f(int k)
{
    union U l = { .p.nope = 1, k };
    (void)l;
}
