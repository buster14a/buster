void q(int x) __attribute__((enable_if(x > 0, "p1")));
void r(int x) __attribute__((enable_if(x > 0, "p2")));
void s(int x);
void g(void (*)(int), void (*)(int));
void f6(int c) { g(q, r); g(s, q); }
void f7(int c) { q; r; }
void f8(int c) { (void)((q == s) + (r == s)); (void)((s == q) == r); }
void f9(int c) { (void)(c ? q : r) == (c ? r : q); }
