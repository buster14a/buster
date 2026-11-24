int f(void){return __builtin_choose_expr(1,7,_Generic(0,int:missing,default:0));}
