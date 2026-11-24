typedef int I; _Static_assert(__builtin_types_compatible_p(const I,int), "same"); _Static_assert(!__builtin_types_compatible_p(int*,const int*), "different");
