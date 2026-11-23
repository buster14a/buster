struct S { int x; struct S *next; }; _Static_assert(sizeof(struct S)==16, "record");
