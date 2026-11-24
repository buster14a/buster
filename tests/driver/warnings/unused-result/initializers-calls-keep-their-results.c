typedef struct { const char* p; unsigned long n; } S;
extern unsigned long strlen(const char*) __attribute__((__pure__));
__attribute__((const)) int c(int);
__attribute__((warn_unused_result)) int w(int);
unsigned long f(const char* x) { S s = { .p = x, .n = __builtin_strlen(x) }; return s.n; }
unsigned long g(const char* x) { const S s = { .p = x, .n = strlen(x), }; return s.n; }
unsigned long h(const char* x) { S s = { x, strlen(x) }; S t[1] = { { x, strlen(x) } }; return s.n + t[0].n; }
int k(int n) { int a = c(n); int b[2] = { c(n), w(n) }; int d = { c(n) }; return a + b[0] + d; }
unsigned long m(const char* x) { return ((S){ .p = x, .n = strlen(x) }).n; }
unsigned long q(const char* x) { S s; s = (S){ x, strlen(x) }; return s.n; }
static int z[2] = { sizeof(int), 1 };
int y(int n) { c(n); strlen(""); return z[0]; }
