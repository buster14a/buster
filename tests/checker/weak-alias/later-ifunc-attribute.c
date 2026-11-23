static int (*r(void))(void) { return 0; }
int t(void) { return 1; }
#pragma weak a = t
int a(void) __attribute__((ifunc("r")));
