enum { A=_Alignof(double), B=sizeof(int (*[2])(int)) }; _Static_assert(A==8 && B==16, "types");
