enum { A=sizeof(double _Complex), B=sizeof(_Complex), C=sizeof(long double _Complex), D=sizeof(_Complex long) }; _Static_assert(A==16 && B==16 && C==32 && D==16, "complex");
