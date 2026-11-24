void f9(int *p) __attribute__((diagnose_if(p == 0, "null", "warning")));
void f10(double d) __attribute__((diagnose_if(d > 1.5, "dbl", "warning")));
void f11(int x) __attribute__((diagnose_if(sizeof(x) == 4 && x == 1, "sz", "warning")));
void f12(unsigned x) __attribute__((diagnose_if(x > -1, "u", "warning")));
void f14(int x) __attribute__((diagnose_if(x ? x / 2 == 3 : -x == 1, "cond", "warning")));
void f15(int x) __attribute__((diagnose_if((unsigned char)x == 1, "cast", "warning")));
void f16(int x) __attribute__((diagnose_if(x != 0 && 10 / x == 5, "div", "warning")));
void f17(char *p) __attribute__((diagnose_if(!p, "notp", "warning")));
void h(int n){ f9(0); f9(&n); f10(2.0); f10(1); f11(1); f12(5); f14(6); f14(-1); f14(2); f15(257); f15(2); f16(0); f16(2); f17((void*)0); f17(0); }
