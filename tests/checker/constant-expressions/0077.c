typedef int I; enum { A=__builtin_types_compatible_p(const I,int), B=__builtin_types_compatible_p(int*,const int*) }; _Static_assert(A==1 && B==0, "compatibility");
