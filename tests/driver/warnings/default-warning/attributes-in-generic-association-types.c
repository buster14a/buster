void g(void) { int x;
int a = _Generic(x, const int: 1, default: 2);
int c = _Generic(&x, int __attribute__((objc_gc(weak))) *: 1, default: 2);
int e = _Generic(&x, int __attribute__((objc_gc(strong))) *: 1, default: 2);
int f = _Generic(&x, int __attribute__((objc_ownership(weak))) *: 1, default: 2);
(void)a; (void)c; (void)e; (void)f; }
