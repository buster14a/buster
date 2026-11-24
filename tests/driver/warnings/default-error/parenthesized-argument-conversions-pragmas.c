void take(int *p, char *c);
#pragma clang diagnostic push
#pragma clang diagnostic warning "-Wincompatible-pointer-types"
void f(long *l, unsigned char *u) { take(((l)), ((u))); }
#pragma clang diagnostic ignored "-Wpointer-sign"
void g(long *l, unsigned char *u) { take(((l)), ((u))); }
#pragma clang diagnostic pop
void h(long *l, unsigned char *u) { take(((l)), ((u))); }
