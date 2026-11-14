float f(float* p) { return __atomic_fetch_and(p, 1, 5); }
