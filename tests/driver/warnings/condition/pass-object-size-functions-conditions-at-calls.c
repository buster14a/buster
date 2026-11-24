#define OV __attribute__((overloadable))
void *f(void *p __attribute__((pass_object_size(0))), unsigned long n) __attribute__((enable_if(n < 100, "small")));
int g(int *p __attribute__((pass_object_size(1))), int k) OV __attribute__((enable_if(k > 0, "pos")));
int g(int *p, int k) OV;
int h(char *p __attribute__((pass_object_size(0))), int k) __attribute__((diagnose_if(k == 3, "three", "warning"), diagnose_if(k == 4, "four", "error")));
void t(void) { char b[4]; int i[2]; f(b, 8); f(b, 200); g(i, 1); g(i, 0); h(b, 3); h(b, 4); h(b, 5); (void)&f; }
