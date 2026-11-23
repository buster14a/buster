int t(void) { return 1; }
#pragma weak a = t
int a(void) __attribute__((alias("t")));
