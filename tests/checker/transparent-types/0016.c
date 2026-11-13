union U{int*i;double*d;};void f(union U);typedef union U T __attribute__((transparent_union));void(*p)(int*)=f;
