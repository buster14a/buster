int a[10];
int b[3];
unsigned long f(int x) { return sizeof(0, a) + sizeof(0, 1, a) + sizeof(a, 0) + sizeof((0, a)) + sizeof(x, b, a) + sizeof(b, a); }
void g(int (*q)[10], int *p) { (void)sizeof(*q + 1); (void)sizeof(p += 0); (void)sizeof(a[0] + 1); (void)sizeof(p = a); }
unsigned long h(void) { struct { int m[4]; } s; return sizeof(s.m + 1) + sizeof((s).m - 1) + sizeof(1 + a); }
