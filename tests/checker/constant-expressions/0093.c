int f(int n) { __typeof__(({int x=n; {int y=x; x=y;} x;})) t=1; return t; }
