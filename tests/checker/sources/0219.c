long l;
int f(void) { return __sync_fetch_and_max(&l, 5); }
