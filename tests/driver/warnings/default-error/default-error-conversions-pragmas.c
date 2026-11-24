#pragma clang diagnostic push
#pragma clang diagnostic warning "-Wint-conversion"
int *a(long x) { return x; }
#pragma clang diagnostic ignored "-Wincompatible-pointer-types"
char *b(int *q) { return q; }
void (*c(void (*p)(void)))(int) { return p; }
#pragma clang diagnostic pop
char *d(int *q) { return q; }
#pragma clang diagnostic error "-Wint-conversion"
long e(int *q) { return q; }
#pragma clang diagnostic warning "-Wreturn-mismatch"
int r(void) { return; }
void v(void) { return 1; }
