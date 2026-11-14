enum F : char { B } __attribute__((aligned(8)));
_Static_assert(sizeof(enum F) == 1 && _Alignof(enum F) == 8, "F");
