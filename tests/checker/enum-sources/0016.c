enum E { A=-2147483649LL, B, C=_Generic(B,long long:1,default:9) }; _Static_assert(B==(-2147483647-1) && C==1 && _Generic(C,long:1,default:0),"negative");
