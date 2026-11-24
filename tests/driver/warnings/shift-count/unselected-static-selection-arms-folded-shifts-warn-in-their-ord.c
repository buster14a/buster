int f(int x){return _Generic(x,int:1<<40,default:-1<<2);}
int g(int x){return _Generic(x,long:1<<41,short:-2<<2,default:x)+__builtin_choose_expr(0,-3<<1,1<<42);}
