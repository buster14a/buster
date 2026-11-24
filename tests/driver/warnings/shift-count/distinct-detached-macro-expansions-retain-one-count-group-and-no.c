#define BAD(x) ({(x)<<32;})
int a(int x){return _Generic(x,int:0,default:BAD(x));}int b(int x){return __builtin_choose_expr(0,BAD(x),0);}
