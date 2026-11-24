#define F(a, b) [a | b]
#define ONE 1
F(x,
#if ONE + 1 == 2
yes
#else
no
#endif
)
F(
#ifdef ONE
p
#endif
, q)
F(r,
#define LATE 7
LATE)
LATE
#define G F
G(
#if defined(ONE) && __has_include("other.h")
s
#endif
, t)
