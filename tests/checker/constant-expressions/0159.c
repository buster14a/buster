int x; int f(void) { __builtin_choose_expr(1,x,0)=3; return x; }
