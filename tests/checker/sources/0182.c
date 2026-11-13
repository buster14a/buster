enum { K = 5 };
int f(enum E { K = 1 } e) { _Static_assert(K == 1, "body"); return K + e; }
_Static_assert(K == 5, "file");
