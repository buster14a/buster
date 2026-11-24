int r(int a) __attribute__((overloadable, enable_if(a > 0, "e"), diagnose_if(a > 1, "one", "warning")));
int r(int a) __attribute__((overloadable, enable_if(a > 0, "e"), diagnose_if(a > 2, "two", "warning")));
int r(double a) __attribute__((overloadable));
void t(void) { r(5); r(-5); }
