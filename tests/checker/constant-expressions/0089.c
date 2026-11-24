int f(int n) { __typeof__(({int x=n; __typeof__(({long y=x; y;})) z=1; z;})) t=1; _Static_assert(sizeof t==sizeof(long), "nested local result"); return t; }
