int f(int n) { __typeof__(({int x=n,y=x+1; y;})) t=1; return t; }
