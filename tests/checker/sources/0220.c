int i;
int f(void) { return __sync_fetch_and_min(&i, 5, i); }
