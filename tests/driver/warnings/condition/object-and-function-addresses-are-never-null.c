int glob;
void helper(void) {}
void f1(char *p) __attribute__((enable_if(p != 0, "nonnull")));
void f3(void (*fp)(void)) __attribute__((enable_if(fp != 0, "nn")));
void f4(int *p) __attribute__((enable_if(p != 0, "nonnull")));
void d(int *p) __attribute__((diagnose_if(p != 0, "nonnull arg", "warning")));
void h(void) { int local; f1("lit"); f3(helper); f4(&glob); d(&glob); d(0); f4(&local); }
