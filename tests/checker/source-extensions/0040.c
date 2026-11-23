union U{int *i;double *d;};void take(union U);
void(*before)(int*)=take;
typedef union U T __attribute__((transparent_union));
