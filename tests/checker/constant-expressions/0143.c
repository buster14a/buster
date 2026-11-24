enum { N=__builtin_choose_expr(1,7,1/0) }; _Static_assert(N==7, "choose");
