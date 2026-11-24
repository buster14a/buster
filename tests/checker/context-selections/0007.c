static const int k=7; enum { E=__builtin_popcount(*(const int *)&k) }; _Static_assert(E==3, "");
