int g(int a[sizeof(void (*)(struct S { int x; }))]) {
 struct S s = { 1 }; return s.x; }
