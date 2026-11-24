int g(int);
int h(void);
void f(int x) __attribute__((diagnose_if(x == g(1), "c", "warning")));
