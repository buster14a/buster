#define AS1 __attribute__((address_space(1)))
#define ASA __attribute__((address_space(1)))
void f(void)
{
    AS1 int *a = 0;
    ASA int *b = 0;
    __attribute__((address_space(1))) int *c = 0;
    long *p;
    p = a;
    p = b;
    p = c;
    p = &*a;
}
