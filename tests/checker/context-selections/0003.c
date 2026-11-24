static const int k=7; enum { E=1 ? __builtin_popcount(k) : 1/0 }; _Static_assert(E==3, "");
