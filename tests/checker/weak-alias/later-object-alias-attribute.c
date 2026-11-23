int t = 1;
#pragma weak a = t
extern int a __attribute__((alias("t")));
