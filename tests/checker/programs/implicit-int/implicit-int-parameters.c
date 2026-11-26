static f(*p) { return *p + (int)sizeof(*p); }
static g(const x) { return x * 2; }
static h(register r) { return r + 1; }
static m(const *p, volatile v) { return *p - v; }
int k(int a, *p);
int k(int a, int *p) { return a + *p; }
int tested(int n) { int v = 5; return f(&v) + g(n) + h(n) + m(&v, n) + k(n, &v); }
