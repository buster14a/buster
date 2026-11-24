int b7(volatile int a) __attribute__((enable_if(a > 0, "")));
int b8(volatile int a) __attribute__((diagnose_if(a > 0, "x", "warning")));
int g; int c(int x) __attribute__((enable_if(g, "")));
int d(int x) __attribute__((enable_if(x, "")));
void h(void) { b7(1); c(1); d(0); }
