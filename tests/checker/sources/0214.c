_Bool f(float* p, float* e) { return __atomic_compare_exchange_n(p, e, 1, 0, 5, 2); }
