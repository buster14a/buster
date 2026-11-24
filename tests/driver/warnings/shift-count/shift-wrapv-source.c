int a = 1 << 40;
int b = -1 << 2;
int c = 3 << 31;
long d = -8L << 60;
int f(int x) { return (3 << 31) | (-4 << 1) | (x << 40) | (1 << -1); }
void g(void) { int v = (-1 << 1); (void)v; }
