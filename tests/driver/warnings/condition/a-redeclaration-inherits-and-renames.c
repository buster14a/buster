void f8(int x);
void f8(int y) __attribute__((diagnose_if(y == 3, "redecl", "warning")));
void f8(int z);
void h(void){ f8(3); f8(2); }
