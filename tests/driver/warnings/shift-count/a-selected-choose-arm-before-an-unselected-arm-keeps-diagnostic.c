int f(int x){return __builtin_choose_expr(1,x<<32,x>>-1);}
