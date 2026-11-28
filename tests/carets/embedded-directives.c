#define TAKE(x) x
TAKE(
#define A 1
#define B A
#if 0
#define SKIPPED 7
#else
#
#endif
B)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wembedded-directive"
TAKE(
#undef B
2)
#pragma clang diagnostic pop
TAKE(
#undef A
3)
