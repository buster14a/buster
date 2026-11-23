typedef union {int i;float f;} U __attribute__((transparent_union));void take(U);void f(short x){take(x);take(1.5f);}
