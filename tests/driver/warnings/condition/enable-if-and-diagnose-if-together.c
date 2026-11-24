void u(int x) __attribute__((enable_if(x, "nz"), diagnose_if(x == 3, "three", "warning")));
void f(void) { u(3); }
