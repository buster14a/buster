struct B { int x __attribute__((aligned(4 4))), y; int z; };
struct A { int m; int *p __attribute__((aligned(m m))); };
void f(void) { int w __attribute__((aligned(8 8))) = 1; (void)w; char c = 1000; (void)c; }
int w = 1.5;
