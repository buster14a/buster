int a(int x){return _Generic(x,int:0,default:({__builtin_choose_expr(0,({x<<32;}),0);}));}int b(int x){return __builtin_choose_expr(0,({_Generic(x,int:0,default:({x>>32;}));}),0);}
