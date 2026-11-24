int g;
void m(int x) __attribute__((diagnose_if(x > 0 || g, "or", "warning")));
void n(int x, int y) __attribute__((diagnose_if(x == 0 || y > 3, "either", "warning")));
void k(int x) __attribute__((enable_if(x > 0 ? g : 1, "c")));
void h(int v) { m(1); m(0); n(0, v); n(1, v); n(v, 5); k(0); }
