void c(int x) __attribute__((diagnose_if(((void)x, x), "m", "warning")));
void l(long x) __attribute__((diagnose_if(((void*)x, x), "m", "warning")));
void a1(int x) __attribute__((diagnose_if(((void)0, x), "m", "warning")));
void a2(int x) __attribute__((diagnose_if((void)x, "m", "warning")));
void a3(int x) __attribute__((diagnose_if(x && ((void)x, 1), "m", "warning")));
void a4(int x) __attribute__((diagnose_if(((void*)0, x), "m", "warning")));
void a8(int x) __attribute__((enable_if(((void)x, x), "m")));
void a9(int *x) __attribute__((diagnose_if(((void*)x, 1), "m", "warning")));
void b1(int x) __attribute__((diagnose_if(((void)0, 1), "m", "warning")));
void b5(int x) __attribute__((diagnose_if(((void)x, 1) && x, "m", "warning")));
void b6(int x) __attribute__((diagnose_if(sizeof((void)x, 1) == 4 && x, "m", "warning")));
void b7(int x) __attribute__((diagnose_if((long)(char*)0 == 0 && x, "m", "warning")));
void b8(int x) __attribute__((diagnose_if(x ? 1 : ((void)x, 0), "m", "warning")));
void h(void) { a3(1); a9(0); b6(1); b8(1); b8(0); }
