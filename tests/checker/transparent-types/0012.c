union __attribute__((transparent_union)) U;union U{int i;unsigned u;};void f(union U);void g(void){f(1);}
