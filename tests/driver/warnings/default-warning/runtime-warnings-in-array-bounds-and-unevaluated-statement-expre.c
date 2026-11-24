int g(void) __attribute__((warn_unused_result));
void f(int n, unsigned u) {
    int b[n + (1 << 40)];
    int c[n + 1 / 0];
    int d[n + (u < -1)];
    char cc = 0;
    int e[n + (cc = 300)];
    int h[n + (n, 1)];
    int a2[3]; int k[n + a2[5]];
    int s[n + sizeof(1 << 41)];
    int t = sizeof(int[n + (1 << 45)]);
    int v = (int)sizeof(({ int w = 1 << 46; w; })) + (int)sizeof(({ 1 << 47; }));
    int x = (1 << 48) + 1 / 0;
    (void)b; (void)c; (void)d; (void)e; (void)h; (void)k; (void)s; (void)t; (void)v; (void)x;
}
