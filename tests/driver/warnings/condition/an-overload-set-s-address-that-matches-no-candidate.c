#define OV __attribute__((overloadable))
int f(int a) OV __attribute__((enable_if(a > 0, "pos")));
int f(double a) OV;
int f(int a, int b) OV;
long f(char *s) OV;
int (*p)(int) = f;
void g(void) { int (*q)(long) = f; q = f; (void)q; }
int (*r)(int) = &f;
