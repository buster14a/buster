typedef __attribute__((aligned(32))) int *P; _Static_assert(sizeof(P)==8 && _Alignof(P)==32,"pointer");
