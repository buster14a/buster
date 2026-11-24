int f(void){return (int)sizeof(__builtin_choose_expr(1,__builtin_add_overflow(1,2,1),0)); }
