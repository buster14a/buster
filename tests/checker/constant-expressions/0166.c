enum { A=__builtin_popcount(1.5), B=__builtin_popcount((1,7)) }; _Static_assert(A==1 && B==3, "builtin conversions");
