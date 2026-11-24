enum E { A=2147483647, B, C=_Generic(B,long:2,default:9) }; _Static_assert(C==2,"provisional"); _Static_assert(_Generic(A,unsigned:1,default:0) && _Generic(B,unsigned:1,default:0),"completed");
