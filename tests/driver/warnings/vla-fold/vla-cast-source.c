int f(int n, int *v) { return (int)sizeof(*(int (*)[n])v); }
int g(int n, int *v) { int (*p)[n] = (int (*)[n])v; return (int)sizeof *p; }
void h(int n) { (void)((int (*)[n])0); }
void k(int n) { (void)sizeof((int (*)[n])0); }
void a(int n, int *v) { (int (*)[n])v; }
int b(int n, int x) { return (int)sizeof(x++, (int(*)[n])0) + x; }
void d(int i) { (void)sizeof((int (*)[i++])0); }
void e(int n, int *v) { if ((int (*)[n])v) {} }
void q(int n, int *v) { (void)(typeof(int[n]) *)v; }
int r(int n, int *v) { return ((int (*)[n])v == (int (*)[n + 1])v); }
int m(int n, int *v) { return _Alignof(int[n]) + (int)_Generic((int (*)[n])v, int (*)[]: 1, default: 2); }
int u1(int n, int *v) { return ((int (*)[n++])v)[0][0] + n; }
int u2(int n) { return (int)sizeof(int[n++]) + n; }
int u3(int n, int *v) { return (int)sizeof(*(int (*)[n++])v) + n; }
