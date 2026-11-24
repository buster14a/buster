#define P "omp parallel for"
_Pragma(P)
#define Q(x) _Pragma(x)
Q("STDC FP_CONTRACT ON")
#define R _Pragma(P)
R after
