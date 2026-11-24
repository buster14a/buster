void f(int x) { x = x; if (x); }
void g(int x) { x = x; int y = 1 << 40; (void)y; }
void h(int x) { if (0) x = x; return; x = x; }
void k(int x) { x = x; if (x); int y = 0 ? (x = x) : 1; (void)y; }
int m(int x) { return (x = x) + _Generic(x, int: 1, default: (x = x)); }
float fl(float x) { x = x; return x; }
int *pt(int *p) { p = p; return p; }
void q(int x) { if (x = x) return; }
