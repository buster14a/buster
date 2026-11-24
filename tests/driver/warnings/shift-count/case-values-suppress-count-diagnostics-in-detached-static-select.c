int f(int x){switch(x){case _Generic(x,int:0,default:x<<32):return 1;case __builtin_choose_expr(1,1,x>>-1):return 2;}return 0;}
