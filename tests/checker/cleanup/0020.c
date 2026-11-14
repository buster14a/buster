void f(int *p);
int g(int n) { return ({ int a __attribute__((cleanup(f))) = n; a; }); }
