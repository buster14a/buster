_Atomic int x;
int f(int* r) { __atomic_load(&x, r, 5); return *r; }
