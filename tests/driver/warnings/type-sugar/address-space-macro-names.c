#define AS1 __attribute__((address_space(1)))
#define ASN(n) __attribute__((address_space(n)))
#define INNER __attribute__((address_space(4)))
#define OUTER INNER
typedef AS1 int I1;
struct m { AS1 int *f; };
void f(const AS1 int *a, AS1 const volatile int *b, int *AS1 *c, I1 *d, ASN(3) int *e, OUTER int *g, struct m mm, AS1 int *const k, I1 *const *l)
{
    a();
    b();
    c();
    d();
    e();
    g();
    mm.f();
    k();
    l();
    a = e;
    int x = a;
}
