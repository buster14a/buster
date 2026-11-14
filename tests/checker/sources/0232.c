_Atomic int a;
int f(void) { return __hip_atomic_load(&a, 5, 1); }
