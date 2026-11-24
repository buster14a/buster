int f(int x){return _Generic(x,default:1>>-1,int:x<<32);}
