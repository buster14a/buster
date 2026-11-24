#define AS1 __attribute__((address_space(1)))
#define ASA __attribute__((address_space(1)))
void h(ASA long *z);
void f(void)
{
    AS1 int *a = 0;
    ASA long *l = a;
    AS1 long *m = 0;
    m = a;
    l = a;
    h(a);
    int n = a;
    ASA char c = 0;
    char *pc = &c;
}
