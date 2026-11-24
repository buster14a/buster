int f(int x){return _Generic(x,int:x,default:0&&(x>>-1));}
