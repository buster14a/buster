union __attribute__((transparent_union)) U{int *i;double *d;};void take(union U);void f(int *i,double *d){take(i);take(d);take(0);}
