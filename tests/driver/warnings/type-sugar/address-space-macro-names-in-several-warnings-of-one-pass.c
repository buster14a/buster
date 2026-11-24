int printf(const char *, ...);
#define AS1 __attribute__((address_space(1)))
#define AS2 __attribute__((address_space(2)))
void f(AS1 int *p, AS2 char *q, AS1 const int *r)
{
    printf("%d\n", p);
    printf("%f\n", q);
    printf("%d %d\n", r, p);
}
