struct S { int a; } s;
unsigned f(void) { return __builtin_rotateleft32(s, 1); }
