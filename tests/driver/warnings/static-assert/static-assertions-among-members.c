struct S { int a; _Static_assert(1, ""); int b; };
struct T { _Static_assert(sizeof(int) == 4, "x"); int c; union { _Static_assert(1, "y"); int d; }; };
union U { _Static_assert(1, "u"); int a; float b; };
struct V { int a; _Static_assert(sizeof(int) == 3, "fails"); char b : 2; };
struct X { int a; _Static_assert(1, ""); };
int z = sizeof(struct S) + sizeof(struct T) + sizeof(union U) + sizeof(struct V) + sizeof(struct X);
void f(struct S *s) { s->b = s->a; }
