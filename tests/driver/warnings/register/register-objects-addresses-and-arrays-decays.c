struct S { int m; int a[2]; };
void f(void) {
    register int x; register int arr[3]; register struct S s;
    int *p = &x;
    int *q = arr;
    int *r = &arr[1];
    int *t = &s.m;
    int *u = s.a;
    int (*v)[3] = &arr;
    int y = arr[0];
    struct S *w = &s;
    (void)p; (void)q; (void)r; (void)t; (void)u; (void)v; (void)y; (void)w;
}
