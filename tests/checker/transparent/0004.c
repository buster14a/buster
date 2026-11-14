typedef union __attribute__((transparent_union)){int i;unsigned u;}U;void f(U);void g(short s,long l,float x){f(s);f(l);f(x);}
