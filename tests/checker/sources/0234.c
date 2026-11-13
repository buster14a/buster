_Float16 h; int f(void){ __atomic_fetch_fmaximum(&h, 1.0f, 5); return 0; }
