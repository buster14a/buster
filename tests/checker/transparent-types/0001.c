typedef union __attribute__((transparent_union)){int*i;double*d;}U;int f(U);int f(int*p){return p!=0;}int g(int*);int g(U p){return p.d!=0;}
