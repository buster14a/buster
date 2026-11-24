#define AS1 __attribute__((address_space(1)))
#define ASA __attribute__((address_space(1)))
#define AS2 __attribute__((address_space(2)))
typedef AS1 int T1;
void g(AS2 int *q);
AS1 int *ret(ASA int *x) { return x; }
void f(AS1 int *pa, ASA int *pb)
{
    AS1 int *a = 0;
    ASA int *b = 0;
    __attribute__((address_space(1))) int *c = 0;
    AS2 int *d = 0;
    T1 *t = 0;
    long *p;
    int *i = a;
    AS2 int *e = b;
    p = a;
    p = b;
    p = c;
    p = t;
    p = pa;
    p = pb;
    d = a;
    a = d;
    b = d;
    g(a);
    g(b);
    p = &*b;
    long *q = pb;
    int x = d - b;
    AS1 int **pp = 0;
    int **ip = pp;
}
