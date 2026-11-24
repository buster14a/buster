#define AS1 __attribute__((address_space(1)))
#define AS2 __attribute__((address_space(2)))
void f(AS1 int *p, AS2 char *q)
{
    AS1 char *a = p;
    AS2 int *b = q;
    AS1 long *c = p;
    AS2 short *d = q;
    (void)a; (void)b; (void)c; (void)d;
}
