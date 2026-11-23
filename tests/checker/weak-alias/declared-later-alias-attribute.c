int t(void) { return 1; }
int a(void);
#pragma weak a = t
int a(void) __attribute__((alias("t")));
