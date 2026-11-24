int a(int x){return __builtin_choose_expr(1,x,_Alignof(__typeof__(x>>-1)));}unsigned long b(int x){return _Alignof(__typeof__(__builtin_choose_expr(1,x<<32,x>>-1)));}
