int g(int);
int h(void);
void f(int x) __attribute__((diagnose_if((x += 1) > 2, "c", "warning")));
