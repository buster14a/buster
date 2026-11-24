#pragma clang diagnostic ignored "-Wuser-defined-warnings"
void f(int x) __attribute__((diagnose_if(x > 1, "big", "warning")));
void g(void){ f(2); }
