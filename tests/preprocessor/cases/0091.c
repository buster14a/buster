#define ONE 1
#define TWO 2
#define CHAIN ONE
#define CHAIN2 CHAIN
#define NAME undefined_name
#define TRUE_NAME true
#define SELF SELF
#if CHAIN2 + TWO == 3 && defined(ONE) && defined TWO && !defined(THREE)
a
#elif CHAIN
b
#endif
#if NAME || SELF
c
#elif defined CHAIN2 && CHAIN2 > 0
d
#endif
#if TRUE_NAME
e
#endif
#if 0
#elif TWO*TWO == 4
f
#endif
#ifdef ONE
#if ONE ? CHAIN : TWO
g
#endif
#endif
