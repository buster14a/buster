struct A { int x; };
_Atomic(struct A) a;
int f(int x, int y, int z) { return x < y < z; }
int g(void) { return 0 && a.x; }
