int g(int);
int h(void);
void f(int x) __attribute__((diagnose_if(h(), "c", "warning")));
