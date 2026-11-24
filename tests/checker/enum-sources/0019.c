enum E { A=0xffffffffffffffffULL, B, C=_Generic(B,unsigned long long:1,default:9) }; _Static_assert(A==0xffffffffffffffffUL && B==0 && C==1 && _Generic(A,unsigned long:1,default:0),"largest");
