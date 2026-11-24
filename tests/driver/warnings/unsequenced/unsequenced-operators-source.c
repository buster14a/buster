int g(int, int);
struct S { int m; } s;
void f(int c, int x, int y, int *p, int a[], struct S *ps) {
    x = x++; x = ++x; x++ + x; x + (x = 1); g(x++, x); g(x++, x++); x = g(x++, 0);
    (x++, x++); x++ && x++; y = (x, x++) + x; a[x] = x++; p[x++] = x; x += x++;
    c ? x++ : x++; s.m = s.m++; ps->m = ps->m++; *p = (*p)++; (y = 1) + (y = 2);
    x = (2, x++); x = 1 && x++; x = 0 && x++; x = 1 ? x++ : 0; x = 0 ? x++ : 0;
    y = c ?: x++ + x; y = (c ? x : y) + x++; x = (x += 1) + x;
}
void h(int c, int x, int y, int z) {
    int b[2] = { x++, x++ }; int d[2] = { x, x++ }; int e[3] = { [0 ... 2] = x++ };
    int k[2][2] = { { y++ }, { y++ } }; int m = x++ + x++, n = z++;
    (void)b; (void)d; (void)e; (void)k; (void)m; (void)n;
}
