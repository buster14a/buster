enum L : char { M = sizeof(enum L *) } __attribute__((aligned(8)));
_Static_assert(_Alignof(enum L) == 8, "L");
