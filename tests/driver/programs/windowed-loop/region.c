int f(int x) { return ({ static int k; k += x; k; }); }
