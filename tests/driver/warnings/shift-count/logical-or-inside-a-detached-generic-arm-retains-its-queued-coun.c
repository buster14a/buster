int f(int x){return _Generic(x,int:x,default:1||(x<<32));}
