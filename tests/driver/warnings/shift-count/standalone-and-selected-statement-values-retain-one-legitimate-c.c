int a(int x){return ({x<<32;});}int b(int x){return ({int y=x>>32;y;});}int c(int x){return _Generic(x,int:({x<<32;}),default:0);}int d(int x){return __builtin_choose_expr(1,({x>>32;}),0);}
