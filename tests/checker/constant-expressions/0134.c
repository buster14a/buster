struct S; typedef struct S *P; enum{N=_Generic((P)0,const struct S*:1,P:7)}; _Static_assert(N==7,"selected"); int f(P x){return _Generic(x,const struct S*:1,P:7);}
