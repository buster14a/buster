typedef union {int *i;double *d;} U __attribute__((transparent_union));void take(U);void take(int*);
void f(double *p){take(p);}
