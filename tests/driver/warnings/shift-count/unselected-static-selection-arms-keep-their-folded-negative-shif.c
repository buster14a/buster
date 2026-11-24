int f(long x){int y=_Generic(x,int:0,long:-1<<2);y+=__builtin_choose_expr(1,0,-1<<3);if(x)y=_Generic(x,int:x<<40,long:1);return y;}
void g(long x){(void)_Generic(x,int:0,long:-1<<4);(void)__builtin_choose_expr(0,-1<<5,0);}
