void f(void) { enum { N=__builtin_choose_expr(({1<<32;}),7,0) }; }
