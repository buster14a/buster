struct U { int :1; int data[]; };
_Static_assert(sizeof(struct U) == 4 && _Alignof(struct U) == 4 && __builtin_offsetof(struct U, data) == 4, "U");
