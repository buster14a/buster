int f(void); enum { A=sizeof(void), B=sizeof f, C=__alignof__(void), D=__alignof__(f) }; _Static_assert(A==1 && B==1 && C==1 && D==4, "GNU queries");
