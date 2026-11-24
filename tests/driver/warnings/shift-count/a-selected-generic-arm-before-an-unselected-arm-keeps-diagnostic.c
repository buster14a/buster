int f(int x){return _Generic(x,int:x<<32,default:x>>-1);}
