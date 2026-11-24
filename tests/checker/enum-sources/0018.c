enum E { A=(unsigned _BitInt(33))8589934591ULL, B, C=_Generic(B,unsigned long:1,default:9) }; _Static_assert(B==8589934592UL && C==1,"bit precise");
