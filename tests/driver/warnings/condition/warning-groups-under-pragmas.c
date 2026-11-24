void f(int x) __attribute__((diagnose_if(x > 0, "shadowed", "warning", "shadow")));
void h(int x) __attribute__((diagnose_if(x > 0, "formatted", "error", "format")));
#pragma clang diagnostic warning "-Wformat"
int m(void) { f(1); h(2); return 0; }
#pragma clang diagnostic ignored "-Wshadow-all"
int n(void) { f(1); h(2); return 0; }
#pragma clang diagnostic error "-Wshadow"
#pragma clang diagnostic ignored "-Wmost"
int k(void) { f(1); h(2); return 0; }
