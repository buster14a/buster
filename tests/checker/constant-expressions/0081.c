struct S { int x; };
int f(void) { __typeof__((__typeof__((struct S){0})){1}) s = {2}; return s.x; }
int g(void) { __typeof__((__typeof__((int){0})){1}) x = 1; return x; }
