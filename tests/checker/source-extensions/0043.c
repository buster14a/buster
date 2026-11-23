typedef union {float f;int i;} U __attribute__((transparent_union));void take(U);
void f(void){take(1);}
