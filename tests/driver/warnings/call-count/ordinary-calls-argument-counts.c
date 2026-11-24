int h(void);
int k(int, int);
int v(int, ...);
typedef int F(int x);
F m;
int g(void)
{
    int (*fp)(int) = 0;
    struct { int (*f)(int y); } s = { 0 };
    h(1);
    k(1);
    k(1, h(), 4, 5);
    v();
    m();
    m(1, 2);
    fp();
    fp(1, 2);
    s.f();
    (h)(1);
    (&k)(1);
    return 0;
}
