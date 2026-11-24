#define AS1 __attribute__((address_space(1)))
int AS1 *x;
void f(int __attribute__((address_space(1+0))) *p)
{
    int *q = p;
}
