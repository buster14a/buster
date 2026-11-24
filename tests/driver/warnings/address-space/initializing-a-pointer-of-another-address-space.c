__attribute__((address_space(1))) int a[3];
int (*p)[3] = &a;
