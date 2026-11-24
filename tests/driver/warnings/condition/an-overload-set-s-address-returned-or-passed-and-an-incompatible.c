#define OV __attribute__((overloadable))
int f(double a) OV;
int f(char a) OV;
void take(int (*)(int));
void take2(int (*q)(int), int);
int (*ret(void))(int) { return f; }
void g(void) { take(f); take2(&f, 1); }
struct T { int y; };
int h(long *a) OV;
int h(struct T t) OV;
void k(int *ip) { h(ip); }
