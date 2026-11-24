struct S { int x:1; }; int f(void) {
 typeof_unqual(((struct S){0}).x) x = 0; return x; }
