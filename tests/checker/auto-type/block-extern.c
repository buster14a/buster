void f(void)
{
    extern int q;
    __auto_type q = 2;
    (void)q;
}

void g(void)
{
    extern long r;
    __auto_type r = 2.0;
    (void)r;
}

void h(void)
{
    extern int s;
    static __auto_type s = 3;
    (void)s;
}
