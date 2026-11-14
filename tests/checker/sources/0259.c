_Complex long double z; int f(void){ __atomic_fetch_min(&z, 1.0, 5); return 0; }
