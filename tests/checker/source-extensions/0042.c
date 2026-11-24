typedef union U U __attribute__((transparent_union));union U{int i;unsigned u;};void take(U);
void f(void){take(1);}
