void t(char *a, int *b, char *c);
void f(const char *x, char *y, long z) { t(x, z, x); t(y, (char *)0, x); }
