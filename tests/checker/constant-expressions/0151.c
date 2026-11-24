int x; int f(void) { _Generic(1,int:x,default:0)=3; return x; }
