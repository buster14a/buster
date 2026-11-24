int a(int x){if(0)return _Generic(x,int:0,default:({x<<32;}));return 0;}int b(int x){if(0)return __builtin_choose_expr(0,({x>>32;}),0);return 0;}
