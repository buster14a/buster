struct A { int x; };
_Atomic(struct A) a;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Watomic-access"
int ignored(void) { return a.x; }
#pragma clang diagnostic pop
#pragma clang diagnostic warning "-Watomic-access"
int warning(void) { return a.x; }
#pragma clang diagnostic error "-Watomic-access"
int error(void) { return a.x; }
