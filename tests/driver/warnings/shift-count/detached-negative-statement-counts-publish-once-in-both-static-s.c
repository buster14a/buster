int a(int x){return _Generic(x,int:0,default:({x<<-1;}));}int b(int x){return __builtin_choose_expr(0,({x>>-1;}),0);}
