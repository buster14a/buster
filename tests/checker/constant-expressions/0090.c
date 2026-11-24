int f(int n) { __typeof__(({int x[2]={n,n+1}; x[0];})) t=1; return t; }
