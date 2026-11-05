/* Original issue 1447 diagnostic probes; one case selected per hosted Clang invocation. */
#if CASE == 1
void f(int *, int *b); void g(const int *p) { f(p, p); }
#endif
#if CASE == 2
void f(int *first); void f(int *second); void g(const int *p) { f(p); }
#endif
#if CASE == 3
void (*f)(int *named); void g(const int *p) { f(p); }
#endif
#if CASE == 4
typedef void (*F)(int *named); F f; void g(const int *p) { f(p); }
#endif
#if CASE == 5
void g(void (*f)(int *named), const int *p) { f(p); }
#endif
#if CASE == 6
void f(int *named); void g(const int *p) { (*f)(p); }
#endif
#if CASE == 7
void f(int *named); void g(const int *p) { ((void (*)(int *casted))f)(p); }
#endif
#if CASE == 8
void f(int *named); void g(const int *p) { f(p + 1); }
#endif
#if CASE == 9
void f(int **named); void g(const int **p) { f(p); }
#endif
#if CASE == 10
#define F f
#define P p
void f(int *named); void g(const int *p) { F(P); }
#endif
#if CASE == 11
void f(int *a, int *b); void g(const int *p, unsigned int *q) { f(p, q); }
#endif
#if CASE == 12
void f(char *a); void g(void) { const unsigned char p[2] = {0}; f(p); }
#endif
#if CASE == 13
int a; double a;
#endif
#if CASE == 14
int f(void); short f(void);
#endif
#if CASE == 15
int f(void) { return 1; } int f(void) { return 2; }
#endif
#if CASE == 16
int a = 1; int a = 2;
#endif
#if CASE == 17
void f(int x, int x);
#endif
#if CASE == 18
int a; static int a;
#endif
#if CASE == 19
int a = 0.9999999999999999; int b = 1.9999999999999998; int c = 1.0000000000000002;
#endif
#if CASE == 20
int f(void) { if (0) return 1.5; if (0) { int a = 2.5; } return 0; }
#endif
#if CASE == 21
int f(void) { return sizeof((int){1.5}); }
#endif
#if CASE == 22
int a = _Generic(1, int: 1, default: (int){1.5});
#endif
#if CASE == 23
void f(int,int); int g(int); void h(void) { f(g(1.5), 2.5); }
#endif
#if CASE == 24
void f(void) { int a[2] = {1.5,2.5}; int b = {3.5}; }
#endif
