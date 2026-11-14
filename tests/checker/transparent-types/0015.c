union U{int*i;double*d;};void f(union U);
void(*p)(int*)=f;
typedef union U T __attribute__((transparent_union));
