int a(int x){return __builtin_choose_expr(1,x,sizeof(x<<32));}unsigned long b(int x){return sizeof(__builtin_choose_expr(1,x<<32,x>>-1));}
