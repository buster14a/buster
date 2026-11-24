int f(int n) __attribute__((enable_if(n < 100, "small")));
int f(int n);
int f(double d) __attribute__((overloadable, diagnose_if(d == 8.0, "dbl", "warning")));
void t(void) { f(8); }
