void f8(int x) __attribute__((diagnose_if(x, "m" "n", "warning"))); void g(void){f8(1);}
