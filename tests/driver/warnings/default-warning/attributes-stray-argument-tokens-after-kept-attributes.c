int old __attribute__((deprecated, aligned(8 8))), skipped; int read_old(void) { return old; }
int g(void) { int older __attribute__((deprecated, aligned(8 8))), skipped2; return older; }
