typedef union {void *v;int *i;} U __attribute__((transparent_union));void take(U);
void f(const int *p){take(p);}
