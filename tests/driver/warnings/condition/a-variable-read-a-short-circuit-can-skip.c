int g;
void f(int x) __attribute__((enable_if(x > 0 && g > 0, "c")));
void k(int x) __attribute__((enable_if(x > 0 ? g : 1, "c")));
void m(int x) __attribute__((diagnose_if(x > 0 || g, "c", "warning")));
void h(void) { f(0); }
void h2(void) { k(0); }
void h3(void) { m(1); m(0); }
