enum E { A=~(unsigned __int128)0, B, C=_Generic(B,unsigned __int128:7,default:9) }; _Static_assert(A==0xffffffffffffffffULL && B==0 && C==7,"wide successor");
