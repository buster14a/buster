struct S { int x:1; }; int f(void) {
 __typeof__(((struct S){0}).x) x = 0; return x; }
