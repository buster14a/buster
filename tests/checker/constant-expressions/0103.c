enum { A=sizeof(unsigned _BitInt(17)), B=sizeof(_BitInt(1+16)), C=sizeof(unsigned _BitInt(1)) }; _Static_assert(A==4 && B==4 && C==1, "bit int");
