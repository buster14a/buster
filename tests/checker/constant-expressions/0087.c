int f(int n) { __typeof__(({int x=n; x;})) t=1; _Static_assert(sizeof t==sizeof n, "local result"); return t; }
