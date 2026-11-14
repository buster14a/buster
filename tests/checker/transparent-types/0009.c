typedef union __attribute__((transparent_union)){int i;float f;}U;void f(U);void f(float);
