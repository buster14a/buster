int f(int n) { __typeof__(({({n;});})) t=1; _Static_assert(sizeof t==sizeof n, "nested statement result"); return t; }
