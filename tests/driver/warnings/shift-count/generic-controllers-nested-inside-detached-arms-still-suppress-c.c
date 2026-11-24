int a(int x){return _Generic(x,int:x,default:_Generic(x<<32,int:0,default:0));}int b(int x){return __builtin_choose_expr(1,x,_Generic(x>>-1,int:0,default:0));}
