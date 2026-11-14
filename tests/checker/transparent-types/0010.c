typedef union __attribute__((transparent_union)){_Atomic(float)f;int i;}U;void f(U);void f(_Atomic(float));
