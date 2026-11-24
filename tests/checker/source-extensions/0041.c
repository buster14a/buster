union U{int *i;double *d;};void take(union U);typedef union U T __attribute__((transparent_union));void(*after)(int*)=take;
