int *p; long *q; int arr[3]; long larr[2];
void g(void) { void *r = 1 ? p : q; (void)r; }
typedef int I; I *pi; const char *cc; unsigned *u;
void h(int c) { (void)(c ? pi : q); (void)(c ? cc : u); (void)(c ? (p) : (q + 1)); }
void (*f1)(void); void (*f2)(int);
void k(int c) { (void)(c ? f1 : f2); (void)(c ?: q); }
struct S { int a; } *sp; union U { int b; } *up;
void m(int c) { (void)(c ? sp : up); (void)(c ? arr : larr); (void)(c ? "x" : q); (void)(c ? (c ? p : q) : q); }
#define SEL(c, a, b) ((c) ? (a) : (b))
void n(int c, int i) { (void)SEL(c, p, q); (void)(c ? i : p); (void)(c ? p : 1L); }
