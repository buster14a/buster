int d(int x) __attribute__((diagnose_if(x == 4, "def", "warning"))) { return x; }
int f(void) { return d(4) + d(3); }
