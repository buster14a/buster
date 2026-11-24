enum { A=__builtin_choose_expr(1<<32,7,0), B=__builtin_choose_expr(1<<-1,0,7), C=__builtin_choose_expr(2147483647+1,7,0) }; _Static_assert(A==7 && B==7 && C==7, "wrapped ICE");
