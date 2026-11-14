union U{int a;unsigned b;};void f(union U);typedef union U T __attribute__((transparent_union));void after(void){f(1);}
