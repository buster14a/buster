struct S{int x;}; int f(void) { __typeof__(({struct S s; if(s){} 1;})) t=1; return t; }
