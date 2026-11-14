typedef union __attribute__((transparent_union)){int*i;double*d;}U;int f(U);int f(double*p){return p!=0;}int g(double*);int g(U p){return p.d!=0;}
