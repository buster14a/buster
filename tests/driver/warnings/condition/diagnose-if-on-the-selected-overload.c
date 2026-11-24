int k(int a) __attribute__((overloadable, diagnose_if(1, "always", "warning"), diagnose_if(a > 1, "dep", "warning")));
int k(double a) __attribute__((overloadable, diagnose_if(a > 5, "bigd", "error")));
int (*p)(int) = k;
void t(void) { k(2); k(6.0); k(1.0); int (*q)(double) = k; (void)q; }
