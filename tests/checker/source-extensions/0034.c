typedef union {int *i;double *d;} U __attribute__((transparent_union));void take(int*);void take(U);void f(int *p){take(p);}
