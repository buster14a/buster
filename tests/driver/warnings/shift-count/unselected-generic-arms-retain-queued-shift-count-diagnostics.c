int f(int x){return _Generic(x,int:7,default:x>>-1);}
