int r(int n) __attribute__((enable_if(n < 100, "small")));
int r(int n) __attribute__((diagnose_if(n == 9, "nine", "warning")));
void t(void) { r(8); }
int s(int n) __attribute__((enable_if(1, "one"), enable_if(0, "zero")));
int s(int n) __attribute__((diagnose_if(n == 9, "nine", "warning")));
void u(void) { int (*p)(int) = s; (void)p; s(8); }
