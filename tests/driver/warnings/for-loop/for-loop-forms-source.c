struct S { int x; } gs; int g(int); extern int ext; enum { K = 3 };
typedef int T; volatile int vg;
void f(int n, int *p, int a[4], struct S s, struct S *ps) {
    int i = 0, j = 0, k = 0, m = 0, q = 0; static int st; volatile int vo = 0; int arr[4] = {0};
    for (; i < n; ) { i; }
    for (; i < n; ) { (void)i; }
    for (; i < n; ) { g(i); }
    for (; i < n; ) { int *pi = &i; (void)pi; }
    for (; i < n; ) { break; }
    for (; i < n; ) { for (;;) { break; } }
    for (; i < n; ) { if (g(0)) return; }
    for (; i < n; ) { continue; }
    for (; i < n && j < k && m > q; ) { }
    for (; i + j + k + m + q; ) { }
    for (; i < st; ) { }
    for (; i < vo; ) { }
    for (; i < ext; ) { }
    for (; i < K; ) { }
    for (; i < (T)n; ) { }
    for (; i < sizeof(n); ) { }
    for (; p; ) { p[0] = 1; }
    for (; p; ) { *p = 1; }
    for (; i < s.x; ) { }
    for (; ps; ) { ps->x = 1; }
    for (; i < n; ) { s.x = i; }
    for (; -i < ~n; ) { }
    for (; i ? j : k; ) { }
    for (; i < n; i++) { }
    for (; i < n; j++) { }
    for (; i < n; j++) { i++; }
    for (; i < n; i++) { i++; }
    for (; i < n; ++i) { --i; }
    for (; i < n; i--) { j++; i--; }
    for (; i < n; i++) { if (j) continue; i++; }
    for (; i < n; i++) { while (j) continue; i++; }
    for (; i < n; i++) { switch (j) { case 1: continue; } i++; }
    for (; i < n; i++) i++;
    for (; i < n; (i++)) { i++; }
    for (; i < n; i++) { (i++); }
    for (; i < n; i++) { i += 1; }
    for (; a; ) { }
    for (; i < n; ) { goto out; }
    for (; i < n; ) { ({ 0; }); }
    for (; i < 1.5; ) { }
    for (; i < 'c'; ) { }
    for (; (i) < (n); ) { }
    out: ;
}
