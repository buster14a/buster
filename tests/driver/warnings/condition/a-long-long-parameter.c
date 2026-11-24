void f(long long x) __attribute__((diagnose_if(x > 4000000000LL, "huge", "warning")));
void g(void){ f(5000000000LL); f(1); }
