struct S { unsigned a : 3; int b[3]; };
static struct S s = { .b = { [2] = 7 }, .a = 5 };
int f(void) { return s.b[2] + s.a; }
