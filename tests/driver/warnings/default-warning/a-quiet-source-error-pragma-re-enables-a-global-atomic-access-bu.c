struct A { int x; };
_Atomic(struct A) a;
#pragma clang diagnostic error "-Watomic-access"
int f(void) { return a.x; }
int *p = &a.x;
