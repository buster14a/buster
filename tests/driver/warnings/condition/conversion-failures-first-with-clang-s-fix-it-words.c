#define OV __attribute__((overloadable))
struct S { int x; };
struct T { int y; };
int f(int a) OV;
int f(struct T t, int b) OV;
int f(int a, int b, int c) OV;
void g(struct S s, long *lp) { f(s); f(s, 1); f(1, s); f(s, s); }
int h(long *a) OV;
int h(struct T t) OV;
void k(int i) { h(i); h(1.5); }
