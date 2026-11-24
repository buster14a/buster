int g(int);
int h(void);
int abs(int);
void f0(int x) __attribute__((diagnose_if(g(x) > 0, "c0", "warning")));
void f1(int x) __attribute__((diagnose_if(x > 3 || g(x), "c1", "warning")));
void f2(int x) __attribute__((diagnose_if(x > 0 && g(1), "c2", "warning")));
void f3(int x) __attribute__((diagnose_if(x ? g(1) : 1, "c3", "warning")));
void f4(int x) __attribute__((diagnose_if(g(g(x)), "c4", "warning")));
void f5(int x) __attribute__((diagnose_if(abs(x) > 3, "c5", "warning")));
void f6(int x) __attribute__((diagnose_if(x > 0 && (x = 3), "c6", "warning")));
void f7(int x) __attribute__((diagnose_if(x > 0 || (x = 1), "c7", "warning")));
void f8(int x) __attribute__((diagnose_if(x > 0 ? x++ : 0, "c8", "warning")));
void f9(int x) __attribute__((diagnose_if(x > 0 && g(x = 1), "c9", "warning")));
void k(void) { f0(5); f0(0); f1(5); f1(0); f2(5); f2(0); f3(5); f3(0); f4(5); f4(0); f5(5); f5(0); f6(5); f6(0); f7(5); f7(0); f8(5); f8(0); f9(5); f9(0); }
