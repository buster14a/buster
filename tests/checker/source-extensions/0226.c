struct S { int n; int data[]; }; _Static_assert(sizeof(struct S)==4, "flexible");
