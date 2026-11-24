struct S { int x; }; int f(void) { struct S { long x; }; struct S s; return sizeof(s); }
