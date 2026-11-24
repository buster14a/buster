void f(int x){_Static_assert(_Generic(x,int:1,default:x<<32),"generic");_Static_assert(__builtin_choose_expr(1,1,x>>-1),"choose");}
