typedef union {int *i;double *d;} U __attribute__((transparent_union));int take(int*);int take(U x){return *x.i;}
