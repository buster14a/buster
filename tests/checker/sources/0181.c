int f(enum E { K = 3 } e, char (*p)[K + 1]) { _Static_assert(sizeof(*p) == 4, "bound"); return K + e; }
