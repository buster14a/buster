int printf(const char*, ...);
typedef _Atomic long AL;
typedef _Atomic(int) AI;
static AL al = 1;
static AI ai = 2;
static _Atomic unsigned char uc = 3;
static _Atomic(int *) ap;
static _Atomic(float) af;
struct S { _Atomic int m; } s;
int main(void) { printf("%d %d %s %d %p %f %x %*d\n", al, ai, uc, s.m, ap, af, ai, ai, 1); printf("%s %c\n", ap, ai); return 0; }
