typedef union __attribute__((transparent_union)){int i;int j __attribute__((aligned(8)));}U;void f(U);void f(int);
