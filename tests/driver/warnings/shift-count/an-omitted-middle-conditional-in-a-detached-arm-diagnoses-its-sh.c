int f(int x){return _Generic(x,int:0,default:(x<<32)?:0);}
