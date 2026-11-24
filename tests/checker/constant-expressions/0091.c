int f(int n) { __typeof__(({int x=n; if(x){x++;} else x--; while(x)x--; x;})) t=1; return t; }
