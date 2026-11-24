#define BEFORE p
#pragma GCC poison p
BEFORE
#if 0
p
#ifdef p
#endif
#endif
#define P(a, b) a ## b
#pragma GCC poison p
