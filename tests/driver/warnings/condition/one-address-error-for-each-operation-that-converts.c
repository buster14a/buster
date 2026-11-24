void q(int x) __attribute__((enable_if(x > 0, "p1")));
void r(int x) __attribute__((enable_if(x > 0, "p2")));
void s(int x);
void g(void (*)(int), void (*)(int));
void f1(int c) { void (*p)(int) = c ? q : r; (void)p; }
void f2(int c) { (void)(c ? r : q); (void)(c ? (q) : (r)); (void)(q ? q : 0); }
void f3(int c) { (void)(q == r); (void)(q, r); (void)(q && r); (void)(q ?: r); }
void f4(int c) { (void)(c ? (c ? q : r) : r); (void)(q == (c ? r : s)); }
void f5(int c) { (void)(&q == &r); (void)(c ? &q : &r); }
