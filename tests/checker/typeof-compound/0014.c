struct S { int x; }; int f(void) {
 __typeof__((struct S){.y=1}) s = {0}; return s.x; }
