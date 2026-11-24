void q(int x) __attribute__((enable_if(x > 0, "p1")));
void r(int x) __attribute__((enable_if(x > 0, "p2")));
void s(int x);
struct P { void (*a)(int); void (*b)(int); };
void f1(int c) { void (*a[2])(int) = { q, r }; (void)a; }
void f2(int c) { void (*a[2])(int) = { q, c ? r : s }; (void)a; }
void f3(int c) { struct P p = { .a = q, .b = c ? r : s }; (void)p; }
void f4(int c) { struct P p[2] = { { q, s }, { c ? s : r, s } }; (void)p; }
