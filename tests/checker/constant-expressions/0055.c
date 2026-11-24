enum { A=(unsigned unsigned)3, B=sizeof(short short), C=sizeof(_Complex _Complex double), D=sizeof(_Atomic _Atomic int) }; _Static_assert(A==3 && B==2 && C==16 && D==4, "duplicate modifiers");
