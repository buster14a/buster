#define ID(x) x
void take(int *p, char *c);
void f(void (*fp)(int *, char *), long *l, unsigned char *u, const char *c) { take((ID(l)), (ID(u))); fp((((l))), (((c)))); }
