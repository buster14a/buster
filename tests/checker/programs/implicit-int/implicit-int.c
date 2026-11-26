x = 3;
typedef t;
t y = 1;
int q = 7;
*const p = &q;
*__restrict r = &q;
*__attribute__((unused)) a = &q;
struct S { const x; };
int tested(int n)
{
    return x == 3 && sizeof(x) == sizeof(int) && y == 1 && sizeof(y) == sizeof(int) && q == 7 && *p == 7 && *a == 7 &&
           *r == 7 && p == &q && a == &q && r == &q && sizeof(*p) == sizeof(int) && sizeof(const) == sizeof(int) &&
           sizeof(volatile) == sizeof(int) && sizeof(struct S) == sizeof(int) && (const)1 == 1 ? 0 : 1;
}
