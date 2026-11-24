struct S{int x;}; int f(void){return _Generic(0,struct S(*)[]:1,struct S(*)[3]:2,default:0);}
