struct S { int m; int a[2]; struct { int k; } in; };
void f(void) {
    register int x; register struct S s; register int arr[2]; register int *rp;
    int *p1 = &(x);
    int *p2 = &((s).m);
    int *p3 = &s.a[0];
    int (*p4)[2] = &s.a;
    int *p5 = &(s.in.k);
    int **p6 = &rp;
    int *p7 = &*rp;
    int *p8 = &(arr)[0];
    void *p9 = &(arr);
    int *p10 = (&x);
    int *p11 = &x + 0, *p12 = &arr[1] + 1;
    (void)p1;(void)p2;(void)p3;(void)p4;(void)p5;(void)p6;(void)p7;(void)p8;(void)p9;(void)p10;(void)p11;(void)p12;
}
