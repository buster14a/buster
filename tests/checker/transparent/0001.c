typedef union __attribute__((transparent_union)){void*p;int*q;}U;void f(U);void g(int*p){f(p);f(0);f((void*)0);}
