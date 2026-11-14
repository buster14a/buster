long double l; int f(void){ __atomic_fetch_min(&l, 1.0, 5); return 0; }
