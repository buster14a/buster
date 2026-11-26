struct pair { char low; int high; };
int g(int);
int abs(int);
__attribute__((pure)) int p(int);
__attribute__((warn_unused_result)) int w(int);
int f(int a, int b, unsigned u)
{
    int r = 0;
    r = a;
    r += b;
    r++;
    if (a) r = b;
    if (b) { r -= a; }
    for (r = 0; r < a; r++) { b += r; }
    while (b > 100) b -= 100;
    g(a);
    p(a);
    w(b);
    abs(b);
    (void)a;
    a < b;
    a;
    a, b;
    r = a / 0;
    r = a << 40;
    r = a++ + a;
    if ((r = b) != 0) { r = 1; }
    while (b-- > b) { r += a; }
    if (0) { r = a << 41; }
    struct pair s = { 300, 2.5 };
    r = s.high;
    struct pair t = { a = 1, a };
    r += t.high;
    r = b % 0;
    r += (struct pair){ 400, 3 }.high;
    r = a;
    r -= a % 0;
    u += (unsigned)r;
    return r + (int)u;
}
