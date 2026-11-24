struct S{int x;}; typedef struct S T __attribute__((aligned(16))); int f(struct S x){return _Generic(x,struct S:1,T:2);}
