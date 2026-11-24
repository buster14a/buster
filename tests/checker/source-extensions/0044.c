typedef union {int *p;int i;} U __attribute__((transparent_union));void take(U);
void f(int *p){take(p);}
