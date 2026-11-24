void f(int x) __attribute__((diagnose_if(x > 0, "shadowed", "warning", "shadow")));
void h(int x) __attribute__((diagnose_if(x > 0, "formatted", "error", "format")));
void u(int x) __attribute__((diagnose_if(x > 0, "plain", "warning")));
void a(int x) __attribute__((diagnose_if(x > 0, "most", "warning", "all"), diagnose_if(x > 1, "unused", "warning", "unused-variable")));
void p(int x) __attribute__((diagnose_if(x > 0, "pedantic", "warning", "pedantic")));
int m(void) { f(1); h(2); u(1); a(2); p(1); f(0); return 0; }
