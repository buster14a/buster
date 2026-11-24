int f(void){return (int)sizeof(__builtin_choose_expr(1,0,__builtin_add_overflow(1,2,1))); }
