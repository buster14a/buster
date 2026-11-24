enum E { A=(signed _BitInt(33))4294967295LL, B, C=_Generic(B,long:1,default:9) }; _Static_assert(B==4294967296UL && C==1 && _Generic(A,unsigned long:1,default:0),"bit precise");
