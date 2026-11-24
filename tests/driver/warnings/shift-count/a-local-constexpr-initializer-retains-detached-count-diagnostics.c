int f(int x){constexpr int y=_Generic(x,int:0,default:x>>-1);return y;}
