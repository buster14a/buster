#define OV __attribute__((overloadable))
struct S { int x; };
int f(int a) OV;
int f(int *a, int b) OV;
void g(struct S s) { f(s); }
