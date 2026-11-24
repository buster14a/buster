int f(int n) { __typeof__(({int x=n; for(int i=0;i<2;i++)x++; do { x--; } while(x); x;})) t=1; return t; }
