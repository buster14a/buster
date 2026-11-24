int a(int x){return _Generic(_Generic(x,int:x<<32,default:x>>-1),int:0,default:0);}int b(int x){return _Generic(__builtin_choose_expr(1,x<<32,x>>-1),int:0,default:0);}
