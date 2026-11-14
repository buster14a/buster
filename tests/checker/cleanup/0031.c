void f(int *p);
void g(int n) { { int a __attribute__((cleanup(f))); if (n) goto out; } out:; }
