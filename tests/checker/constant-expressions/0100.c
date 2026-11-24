enum { A=sizeof(_Atomic(int)), B=sizeof(_Atomic int), C=sizeof(const _Atomic(int)), D=sizeof(int * _Atomic) }; _Static_assert(A==4 && B==4 && C==4 && D==8, "atomic");
