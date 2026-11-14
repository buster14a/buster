void f(void) { struct S { int a; } s = { 0 }; __builtin_assume(s); }
