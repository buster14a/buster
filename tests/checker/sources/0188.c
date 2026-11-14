int f(enum E : long { K = 7 } e) { _Static_assert(sizeof(K) == sizeof(long), "C23"); return K == e; }
