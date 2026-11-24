void f(void)
{
    int a[3];
    a[5] = 0;
    enum { B = (1 == 1, 2), C = ((void *)0, 3), D = (4, 5), E = (1 < 2, 6) };
    a[6] = 1;
    typedef int T[(2 != 3, 2)];
    a[7] = 1;
    (void)sizeof(T);
}
