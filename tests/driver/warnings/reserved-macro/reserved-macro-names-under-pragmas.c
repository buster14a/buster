#define _A 1
#pragma clang diagnostic warning "-Wreserved-macro-identifier"
#define _B 1
#undef _B
#pragma clang diagnostic ignored "-Wreserved-identifier"
#define _C 1
int z;
