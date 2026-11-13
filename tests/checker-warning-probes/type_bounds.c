int a(int p[static const volatile restrict 10]) { return sizeof p; }
int b(int p[const static 10]) { return sizeof(p); }
int c(int (*p)[10]) { return sizeof p; }
int d(int p[2][3]) { return sizeof(p); }
int e(int n, int p[static n + 1]) { return sizeof(p); }
int f(int *p) { return p[(char)-1]; }
int g(int *p) { return p[(char)128]; }
int h(int *p) { return p[(char)127]; }
char *i(_BitInt(33) x) { return (char *)x; }
char *j(_BitInt(63) x) { return (char *)x; }
char *k(_BitInt(64) x) { return (char *)x; }
char *l(unsigned _BitInt(33) x) { return (char *)x; }
void *m(int x) { return (void *)x; }
char *n(const int x) { return (char *)x; }
int o(char c, int *p) { return p[c]; }
char *q(int x) { return (char *)(x ? 0 : 1); }
char *r(void) { const int x=1; return (char *)x; }
