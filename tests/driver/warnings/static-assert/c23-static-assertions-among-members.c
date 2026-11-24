struct S { int a; static_assert(1); int b; static_assert(sizeof(int) == 4, "m"); };
struct W { static_assert(1); };
int z = sizeof(struct S);
