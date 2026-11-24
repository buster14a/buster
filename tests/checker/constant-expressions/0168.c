enum { A=sizeof(__builtin_ctz(0)), B=1?3:__builtin_clz(0) }; _Static_assert(A==4 && B==3, "builtin faults");
