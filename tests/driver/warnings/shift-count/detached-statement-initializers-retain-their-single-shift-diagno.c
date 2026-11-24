int a(int x){return _Generic(x,int:0,default:({int y=x<<32;y;}));}int b(int x){return __builtin_choose_expr(0,({int y=x>>32;y;}),0);}
