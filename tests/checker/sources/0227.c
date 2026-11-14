int x;
void f(long* v) { __atomic_store(&x, v, 5); }
