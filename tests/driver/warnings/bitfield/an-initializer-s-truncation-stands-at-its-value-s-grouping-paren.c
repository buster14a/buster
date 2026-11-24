struct S { int b : 3; int c : 3; };
struct S g1 = { (9) };
struct S g2 = { ((9)), (1 << 4) };
struct S g3 = { .b = (9), .c = 9 };
struct T { struct S s; } g4 = { { (9) } };
struct S g5[2] = { [0 ... 1] = { (9) } };
int f(void) { struct S l = (struct S){ (9), -(9) }; return l.b; }
struct S g6 = { -(9), (int)9 };
