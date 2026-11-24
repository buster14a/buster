enum { A=__builtin_popcount(7), B=__builtin_clz(1), C=__builtin_ctz(8), D=__builtin_parity(7), E=__builtin_ffs(8) }; _Static_assert(A==3 && B==31 && C==3 && D==1 && E==4, "integer builtins");
