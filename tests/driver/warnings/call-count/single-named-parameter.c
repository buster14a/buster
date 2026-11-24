int h(int a);
int h(int b);
int k(int a, ...);
int u(int);
int g(void)
{
    h(1, 2);
    h();
    k();
    u();
    u(1, 2, 3);
    (h)();
    (&h)();
    return 0;
}
