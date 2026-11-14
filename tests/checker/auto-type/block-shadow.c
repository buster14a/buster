void f(void)
{
    extern int q;
    {
        __auto_type q = 2.0;
        _Static_assert(sizeof(q) == sizeof(double), "inner deduction");
    }
    {
        extern long *p;
        __auto_type *p = (long *)0;
        (void)p;
    }
    (void)q;
}
