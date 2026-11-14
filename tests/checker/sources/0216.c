unsigned _BitInt(32) f(unsigned _BitInt(32)* p) { return __atomic_fetch_add(p, 1, 5); }
