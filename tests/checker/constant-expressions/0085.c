int f(int n) { int x=0; __typeof__(({x+=50;n;})) t=1; _Static_assert(sizeof t==sizeof n, "statement result"); return x+t; }
