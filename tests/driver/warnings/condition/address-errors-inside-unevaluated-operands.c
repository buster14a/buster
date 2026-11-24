void q(int x) __attribute__((enable_if(x > 0, "p1")));
void r(int x) __attribute__((enable_if(x > 0, "p2")));
void f(int c) { (void)sizeof(q == r); (void)sizeof(&q); (void)sizeof((q)); (void)sizeof q; (void)sizeof(q, r); }
void h(int c) { __typeof__(c ? q : r) *p = 0; (void)p; (void)__alignof__(q); (void)_Generic(q, default: 0); }
