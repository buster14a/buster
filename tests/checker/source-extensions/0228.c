struct T { int :0; int data[]; };
_Static_assert(sizeof(struct T) == 0 && _Alignof(struct T) == 4, "T");
