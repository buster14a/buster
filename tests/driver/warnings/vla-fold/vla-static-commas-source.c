void f(void)
{
    int a[2];
    a[3] = 0;
    static int b = sizeof(int[(6, 2)]);
    enum { E = (1, 2) };
    a[4] = 0;
}
