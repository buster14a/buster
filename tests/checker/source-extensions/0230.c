struct S { int n; int :0; int data[]; }; _Static_assert(sizeof(struct S)==4, "flexible padding");
