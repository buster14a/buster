#define AS1 __attribute__((address_space(1)))
int AS1 *x;
void f(int __attribute__((address_space(1u))) *p, int __attribute__((address_space(1UL))) *r)
{
    int *q = p;
    int *s = r;
}
