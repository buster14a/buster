struct S { int data[]; };
_Static_assert(sizeof(struct S) == 0 && _Alignof(struct S) == 4, "S");
