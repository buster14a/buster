struct S { int x; }; int f(void) { __typeof__((struct S){0}) s = {1}; return s.x; }
