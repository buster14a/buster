int f(const int* p) { return __atomic_exchange_n(p, 1, 5); }
