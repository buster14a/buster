enum E { A=9223372036854775807LL, B, C=_Generic(B,long long:1,default:9) }; _Static_assert(B==(-9223372036854775807LL-1) && C==1 && _Generic(A,long:1,default:0),"largest signed");
